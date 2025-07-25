/*
    loraid-arduino-rfm.cpp
    Copyright © 2018 lora.id. All right reserved.

    Author: Andri Rahmadhani
    Date: 2018-04-25

    Encapsulate Ideetron LoRaWAN simple node demonstrator
    *This fimrware supports
        *Over The Air Activation
        *Activation By Personalization
        *Class switching between Class A and Class C motes
        *Channel hopping
        *
        *The following settings can be done
        *Channel Receive and Transmit
        *Datarate Receive and Transmit
        *Transmit power
        *Confirmed or unconfirmed messages
        *Device Address
        *Application Session Key
        *Network Session Key
        *Device EUI
        *Application EUI
        *Application key
        *Mote Class

    Use of this source code is governed by the MIT license that can be found in the LICENSE file.
*/

#include "lorawan-arduino-rfm.h"
#include "Conversions.h"

// define lora objet
LoRaWANClass lora;

LoRaWANClass::LoRaWANClass()
{
}

LoRaWANClass::~LoRaWANClass()
{
}

bool LoRaWANClass::init(int rngSeed)
{
    // Lora Setting Class
    dev_class = CLASS_A;

    // Random seed
    // The previous version used an unconditional analogRead(0) which
    // assumes that pin 0 is analog capable (not true on some architectures).
    // Forcing the user to provide a seed gives them flexibility to do something
    // more appropriate. Even using a simple counter in eeprom that's 
    // incremented on boot is probably good enough. Note that we no longer
    // use a random number to generate DevNonce - randomisation is only
    // used for choosing channel numbers.
    randomSeed(rngSeed);

    // Status
    RFM_Command_Status = NO_RFM_COMMAND;
    Rx_Status = NO_RX;
    Ack_Status = NO_ACK;

    // current channel
    currentChannel = MULTI;

    // Initialise session data struct (Semtech default key)
    memset(Address_Tx, 0x00, 4);
    memset(NwkSKey, 0x00, 16);
    memset(AppSKey, 0x00, 16);

    Frame_Counter_Tx = 0x0000;
    Session_Data.NwkSKey = NwkSKey;
    Session_Data.AppSKey = AppSKey;
    Session_Data.DevAddr = Address_Tx;
    Session_Data.Frame_Counter_Tx = &Frame_Counter_Tx;
    Session_Data.Frame_Counter_Rx = &Frame_Counter_Rx;
    Session_Data.Device_Class = &dev_class;

    //Initialize OTAA data struct
    memset(DevEUI, 0x00, 8);
    memset(AppEUI, 0x00, 8);

    memset(AppKey, 0x00, 16);
    memset(DevNonce, 0x00, 2);
    memset(AppNonce, 0x00, 3);
    memset(NetID, 0x00, 3);
    OTAA_Data.DevEUI = DevEUI;
    OTAA_Data.AppEUI = AppEUI;
    OTAA_Data.AppKey = AppKey;
    OTAA_Data.DevNonce = DevNonce;
    OTAA_Data.AppNonce = AppNonce;
    OTAA_Data.NetID = NetID;

    // Device Class
    LoRa_Settings.Mote_Class = 0x00; //0x00 is type A, 0x01 is type C

    // Rx
#if defined(AS_923) || defined(AS_923_2) 
    LoRa_Settings.Datarate_Rx = 0x02; //set to SF10 BW 125 kHz
#elif defined(EU_868)
    LoRa_Settings.Datarate_Rx = 0x03;                //set to SF9 BW 125 kHz
#elif defined(IN_865)
    LoRa_Settings.Datarate_Rx = 0x02;               //set to SF10 BW 125 kHz
#else //US_915 or AU_915
    LoRa_Settings.Datarate_Rx = 0x0C;                //set to SF8 BW 500 kHz
#endif
    LoRa_Settings.Channel_Rx = 0x08; // set to recv channel

    // Tx
#if defined(US_915)
    LoRa_Settings.Datarate_Tx = drate_common = 0x02; //set to SF7 BW 125 kHz
#elif defined(AU_915)
    LoRa_Settings.Datarate_Tx = drate_common = 0x02; //set to SF7 BW 125 kHz
#else
    LoRa_Settings.Datarate_Tx = drate_common = 0x00; //set to SF12 BW 125 kHz
#endif
    LoRa_Settings.Channel_Tx = 0x00; // set to channel 0

    LoRa_Settings.Confirm = 0x00;         //0x00 unconfirmed, 0x01 confirmed
    LoRa_Settings.Channel_Hopping = 0x00; //0x00 no channel hopping, 0x01 channel hopping

    // Set default rx delay and window
    LoRa_Settings.RX1_Window = 1000;
    LoRa_Settings.RX2_Window = 1000;
    
    LoRa_Settings.Rx1_Delay = 5000; // Thing stack seems to be 5000 ms (so Rx2_delay 6000 ms)
    LoRa_Settings.Rx2_Delay = LoRa_Settings.Rx1_Delay + LoRa_Settings.RX1_Window;
    

    // Initialise buffer for data to transmit
    memset(Data_Tx, 0x00, sizeof(Data_Tx));
    Buffer_Tx.Data = Data_Tx;
    Buffer_Tx.Counter = 0x00;
    // Initialise buffer for data to receive
    memset(Data_Rx, 0x00, sizeof(Data_Rx));
    Buffer_Rx.Data = Data_Rx;
    Buffer_Rx.Counter = 0x00;
    Message_Rx.Direction = 0x01; //Set down direction for Rx message

    //Initialize I/O pins
    if (RFM_pins.DIO0 != -1)
    {
        pinMode(RFM_pins.DIO0, INPUT);
        pinMode(RFM_pins.DIO1, INPUT);
        pinMode(RFM_pins.DIO2, INPUT);
    }
#ifdef BOARD_DRAGINO_SHIELD
    pinMode(RFM_pins.DIO5, INPUT);
#endif
    pinMode(RFM_pins.CS, OUTPUT);
    digitalWrite(RFM_pins.CS, HIGH);

    // Reset
    if (RFM_pins.RST != -1)
    {
        pinMode(RFM_pins.RST, OUTPUT);
        digitalWrite(RFM_pins.RST, HIGH);
        delay(10);
        digitalWrite(RFM_pins.RST, LOW);
        delay(10);
        digitalWrite(RFM_pins.RST, HIGH);
    }

    //Initialise the SPI port
    SPI.begin();

    /*** This prevents the use of other SPI devices with different settings ***/
    //SPI.beginTransaction(SPISettings(4000000,MSBFIRST,SPI_MODE0));

    //Wait until RFM module is started
    delay(50);

    //Initialize RFM module
    if (!RFM_Init())
    {
        return 0;
    }
    return 1;
}

bool LoRaWANClass::join(void)
{
    bool join_status;
    const unsigned long timeout = 6000;
    unsigned long prev_millis;

    if (currentChannel == MULTI)
    {
        randomChannel();
    }
    // join request
    LoRa_Send_JoinReq(&OTAA_Data, &LoRa_Settings);
    // delay(900);
    // loop for <timeout> wait for join accept
    prev_millis = millis();
    do
    {
        join_status = LORA_join_Accept(&Buffer_Rx, &Session_Data, &OTAA_Data, &Message_Rx, &LoRa_Settings);

    } while ((millis() - prev_millis) < timeout && !join_status);

    return join_status;
}

void LoRaWANClass::sleep(void)
{
    //Switch RFM to sleep
    //DON'T USE Switch mode function
    RFM_Write(RFM_REG_OP_MODE, RFM_MODE_SLEEP);
}

void LoRaWANClass::wakeUp(void)
{
    RFM_Switch_Mode(RFM_MODE_STANDBY);
}

void LoRaWANClass::setDevEUI(const char *devEUI_in)
{
    for (byte i = 0; i < 8; ++i)
        DevEUI[i] = ASCII2Hex(devEUI_in[i * 2], devEUI_in[(i * 2) + 1]);
    //Reset frame counter
    Frame_Counter_Tx = 0x0000;

    //Reset RFM command status
    RFM_Command_Status = NO_RFM_COMMAND;
}

void LoRaWANClass::setAppEUI(const char *appEUI_in)
{
    for (byte i = 0; i < 8; ++i)
        AppEUI[i] = ASCII2Hex(appEUI_in[i * 2], appEUI_in[(i * 2) + 1]);
    //Reset frame counter
    Frame_Counter_Tx = 0x0000;

    //Reset RFM command status
    RFM_Command_Status = NO_RFM_COMMAND;
}

void LoRaWANClass::setAppKey(const char *appKey_in)
{
    for (byte i = 0; i < 16; ++i)
        AppKey[i] = ASCII2Hex(appKey_in[i * 2], appKey_in[(i * 2) + 1]);
    //Reset frame counter
    Frame_Counter_Tx = 0x0000;

    //Reset RFM command status
    RFM_Command_Status = NO_RFM_COMMAND;
}

void LoRaWANClass::setNwkSKey(const char *NwkKey_in)
{
    for (uint8_t i = 0; i < 16; ++i)
        NwkSKey[i] = ASCII2Hex(NwkKey_in[i * 2], NwkKey_in[(i * 2) + 1]);

    //Reset frame counter
    Frame_Counter_Tx = 0x0000;

    //Reset RFM command status
    RFM_Command_Status = NO_RFM_COMMAND;
}

void LoRaWANClass::setAppSKey(const char *ApskKey_in)
{
    for (uint8_t i = 0; i < 16; ++i)
        AppSKey[i] = ASCII2Hex(ApskKey_in[i * 2], ApskKey_in[(i * 2) + 1]);

    //Reset frame counter
    Frame_Counter_Tx = 0x0000;

    //Reset RFM command status
    RFM_Command_Status = NO_RFM_COMMAND;
}

void LoRaWANClass::setDevAddr(const char *devAddr_in)
{
    //memset(Session_Data.DevAddr, 0x30, sizeof(Session_Data.DevAddr));

    size_t devAddrSize = sizeof(Session_Data.DevAddr);
    memset(Session_Data.DevAddr, 0x30, devAddrSize);

    //Check if it is a set command and there is enough data sent
    Address_Tx[0] = ASCII2Hex(devAddr_in[0], devAddr_in[1]);
    Address_Tx[1] = ASCII2Hex(devAddr_in[2], devAddr_in[3]);
    Address_Tx[2] = ASCII2Hex(devAddr_in[4], devAddr_in[5]);
    Address_Tx[3] = ASCII2Hex(devAddr_in[6], devAddr_in[7]);

    //Reset frame counter
    Frame_Counter_Tx = 0x0000;

    //Reset RFM command status
    RFM_Command_Status = NO_RFM_COMMAND;
}

void LoRaWANClass::setTxPower(int level, txPin_t pinTx)
{
    RFM_Set_Tx_Power(level, pinTx);
}

int LoRaWANClass::getRssi()
{
    // return rssi value in dBm - convertion according to sx1276 datasheet
    return -157 + RFM_Get_Rssi();
}

void LoRaWANClass::setDeviceClass(devclass_t dev_class)
{
    this->dev_class = dev_class;
}

void LoRaWANClass::setRunningDeviceClass(devclass_t dev_class)
{
    LoRa_Settings.Mote_Class = (dev_class == CLASS_A) ? CLASS_A : CLASS_C;

    if (LoRa_Settings.Mote_Class == CLASS_A)
    {
        RFM_Switch_Mode(RFM_MODE_STANDBY);
    }
    else
    {
        RFM_Continuous_Receive(&LoRa_Settings);
    }

    //Reset RFM command
    RFM_Command_Status = NO_RFM_COMMAND;
}

void LoRaWANClass::sendUplink(char *data, unsigned int len, unsigned char confirm, unsigned char mport)
{
    lora.setRunningDeviceClass(CLASS_A); // start as class a device

    if (currentChannel == MULTI)
    {
        randomChannel();
    }
    LoRa_Settings.Confirm = (confirm == 0) ? 0 : 1;
    if (mport == 0)
        mport = 1;
    if (mport > 223)
        mport = 1;
    LoRa_Settings.Mport = mport;
    //Set new command for RFM
    RFM_Command_Status = NEW_RFM_COMMAND;
    upMsg_Type = MSG_UP;
    Buffer_Tx.Counter = len;
    memcpy(Buffer_Tx.Data, data, len);
}

void LoRaWANClass::sendACK()
{
    char Str[10];
    #ifdef LORAWAN_DEBUG_STREAM
    LORAWAN_DEBUG_STREAM.println(F("LoRaWAN: sendACK triggered!!"));
    #endif
    lora.setRunningDeviceClass(CLASS_A); // start as class a device

    if (currentChannel == MULTI)
    {
        randomChannel();
    }
    //Set new command for RFM
    RFM_Command_Status = NEW_RFM_COMMAND;
    upMsg_Type = MSG_ACK;
    //sprintf(Str, "");
    Str[0] = '\0';
    memcpy(Buffer_Tx.Data, Str, sizeof(Str));

}

void LoRaWANClass::setDataRate(unsigned char data_rate)
{
    drate_common = data_rate;
#if defined(US_915)
    if (drate_common <= 0x04)
    {
        LoRa_Settings.Datarate_Tx = drate_common;
        LoRa_Settings.Datarate_Rx = data_rate + 0x0A;
    }
#elif defined(AU_915)
    if (drate_common <= 0x04)
    {
        LoRa_Settings.Datarate_Tx = drate_common;
        LoRa_Settings.Datarate_Rx = data_rate + 0x0A;
    }
#else
    //Check if the value is oke
    if (drate_common <= 0x06)
    {
        LoRa_Settings.Datarate_Tx = drate_common;
        LoRa_Settings.Datarate_Rx = drate_common;
    }

#endif
    RFM_Command_Status = NO_RFM_COMMAND;
}

void LoRaWANClass::setChannel(unsigned char channel)
{
    if (channel <= 7)
    {
        currentChannel = channel;
        LoRa_Settings.Channel_Tx = channel;
#ifdef US_915
        LoRa_Settings.Channel_Rx = channel + 0x08;
#elif defined(AU_915)
        LoRa_Settings.Channel_Rx = channel + 0x08;
#elif defined(EU_868)
        LoRa_Settings.Channel_Rx = channel;
#elif defined(IN_865)
        LoRa_Settings.Channel_Rx = channel;
#elif defined(AS_923) || defined(AS_923_2)
        LoRa_Settings.Channel_Rx = channel;
#endif
    }
    else if (channel == MULTI)
    {
        currentChannel = MULTI;
    }
}

unsigned char LoRaWANClass::getChannel()
{
    return LoRa_Settings.Channel_Tx;
}

unsigned char LoRaWANClass::getDataRate()
{
    return LoRa_Settings.Datarate_Tx;
}

void LoRaWANClass::setTxPower1(unsigned char power_idx)
{
    unsigned char RFM_Data;
    LoRa_Settings.Transmit_Power = (power_idx > 0x0F) ? 0x0F : power_idx;
    RFM_Data = LoRa_Settings.Transmit_Power + 0xF0;
    RFM_Write(RFM_REG_PA_CONFIG, RFM_Data);
}

void LoRaWANClass::setRx1Delay(unsigned int ms) 
{
    LoRa_Settings.Rx1_Delay = ms;
    LoRa_Settings.Rx2_Delay = max(LoRa_Settings.Rx2_Delay,LoRa_Settings.Rx1_Delay + LoRa_Settings.RX1_Window);
}

void LoRaWANClass::setRx2Delay(unsigned int ms)
{
    LoRa_Settings.Rx2_Delay = max(ms,LoRa_Settings.Rx1_Delay + LoRa_Settings.RX1_Window);
}

void LoRaWANClass::setRx1Window(unsigned int ms)
{
    LoRa_Settings.RX1_Window = ms;
    LoRa_Settings.Rx2_Delay = max(LoRa_Settings.Rx2_Delay,LoRa_Settings.Rx1_Delay + LoRa_Settings.RX1_Window);
}

void LoRaWANClass::setRx2Window(unsigned int ms)
{
    LoRa_Settings.RX2_Window = ms;
}

int LoRaWANClass::readData(char *outBuff)
{
    int res = 0;
    //If there is new data
    if (Rx_Status == NEW_RX)
    {
        res = Buffer_Rx.Counter;
        memset(outBuff, 0x00, res + 1);
        memcpy(outBuff, Buffer_Rx.Data, res);

        // Clear Buffer counter
        Buffer_Rx.Counter = 0x00;

        Rx_Status = NO_RX;
    }

    return res;
}

/**
 * Get ACK flag from downlink packet
 * 
 * @return true in case of ACK received
 */
bool LoRaWANClass::readAck(void)
{
    if (Ack_Status == NEW_ACK)
    {
        Ack_Status = NO_ACK;
        return true;
    }
    return false;
}

void LoRaWANClass::switchToClassC(sSettings *LoRa_Settings)
{
    lora.setRunningDeviceClass(CLASS_C);
 	LoRa_Settings->Channel_Rx = CHRX2;    // set Rx2 channel 868.500 MHZ 
	LoRa_Settings->Datarate_Rx = SF12BW125;   //set RX2 datarate 12
    RFM_Continuous_Receive(LoRa_Settings);
}

void LoRaWANClass::onMessage(void(*callback)(sBuffer *Data_Rx, bool isConfirmed, uint8_t fPort))
{
	messageCallback = callback;
}

void LoRaWANClass::update(void)
{
    //Type A mote transmit receive cycle
    if ((RFM_Command_Status == NEW_RFM_COMMAND || RFM_Command_Status == JOIN) && LoRa_Settings.Mote_Class == CLASS_A)
    {
        //LoRaWAN TX/RX cycle
        LORA_Cycle(&Buffer_Tx, &Buffer_Rx, &RFM_Command_Status, &Session_Data, &OTAA_Data, &Message_Rx, &LoRa_Settings, &upMsg_Type);
        
        if ((Message_Rx.Frame_Control & 0x20) > 0){ // ack get only in RX1 window
            #ifdef LORAWAN_DEBUG_STREAM
            LORAWAN_DEBUG_STREAM.println(F("LoRaWAN: Got an ACK"));
            #endif
            Ack_Status = NEW_ACK;
            Message_Rx.Frame_Control = 0; // clear ack bit after reading
        }

        if (Buffer_Rx.Counter != 0x00)
        {
            bool isConfirmed =  ((Message_Rx.MAC_Header & 0xE0)>>5) == 5 ? true : false ; // MType
            uint8_t fPort = Message_Rx.Frame_Port;
            if(lora.messageCallback) lora.messageCallback(&Buffer_Rx, isConfirmed, fPort);
            Rx_Status = NEW_RX;
            #ifdef LORAWAN_DEBUG_STREAM
            LORAWAN_DEBUG_STREAM.println(F("LoRaWAN: Data received over RX1"));
            #endif
        }

        RFM_Command_Status = NO_RFM_COMMAND;

    }

    //Type C mote transmit and receive handling
    if (LoRa_Settings.Mote_Class == CLASS_C)
    {
        //Transmit -> this will be never used in Class C since device start as class A all the time.
        // in Class C mode we will only listen upcoming messages
        if (RFM_Command_Status == NEW_RFM_COMMAND)
        {
            //LoRaWAN TX/RX cycle
            LORA_Cycle(&Buffer_Tx, &Buffer_Rx, &RFM_Command_Status, &Session_Data, &OTAA_Data, &Message_Rx, &LoRa_Settings,&upMsg_Type);
            if (Buffer_Rx.Counter != 0x00)
            {
                Rx_Status = NEW_RX;
            }
            RFM_Command_Status = NO_RFM_COMMAND;
        }

        //Receive in Class C mode
        bool isRxDone;
        if (RFM_pins.DIO0 != -1)
        {
            isRxDone = digitalRead(RFM_pins.DIO0) == HIGH;
        }
        else
        {
            isRxDone = RFM_isRxDone();
        }
        if (isRxDone)
        {
            #ifdef LORAWAN_DEBUG_STREAM
            LORAWAN_DEBUG_STREAM.println(F("LoRaWAN: Got an RX in class C mode"));
            #endif
            LORA_Receive_Data(&Buffer_Rx, &Session_Data, &OTAA_Data, &Message_Rx, &LoRa_Settings);
            if (Buffer_Rx.Counter != 0x00)
            {
            // MType 3:unconfirmed dwn / 5:confirmed dwn
            bool isConfirmed =  ((Message_Rx.MAC_Header & 0xE0)>>5) == 5 ? true : false ; 
            uint8_t fPort = Message_Rx.Frame_Port;
            if(lora.messageCallback) lora.messageCallback(&Buffer_Rx, isConfirmed, fPort);
            Buffer_Rx.Counter = 0x00; // clear counter for the next cycle
            #ifdef LORAWAN_DEBUG_STREAM
            LORAWAN_DEBUG_STREAM.println(F("LoRaWAN: Data received over RX2"));
            #endif 
            
            }
        }

        RFM_Command_Status = NO_RFM_COMMAND;
    }
    
        if ((dev_class == CLASS_C) && (LoRa_Settings.Mote_Class != CLASS_C))
        {
            #ifdef LORAWAN_DEBUG_STREAM
            LORAWAN_DEBUG_STREAM.println(F("LoRaWAN: Switch to class C called"));
            #endif
            lora.switchToClassC(&LoRa_Settings);
        }

}

void LoRaWANClass::randomChannel()
{
    unsigned char freq_idx;
#ifdef AS_923
	freq_idx = random(0, 8);
	LoRa_Settings.Channel_Rx=freq_idx;      // same rx and tx channel
#elif defined(AS_923_2)
    freq_idx = random(0, 8);
	LoRa_Settings.Channel_Rx=freq_idx;      // same rx and tx channel
#elif defined(EU_868)    
    freq_idx = random(0,8);
    LoRa_Settings.Channel_Rx=freq_idx;      // same rx and tx channel 
#elif defined(IN_865)
    freq_idx = random(0,3);
    LoRa_Settings.Channel_Rx=freq_idx;      // same rx and tx channel 
#else // US_915 or AU_915
    freq_idx = random(0, 8);
    LoRa_Settings.Channel_Rx = freq_idx + 0x08;
#endif
    LoRa_Settings.Channel_Tx = freq_idx;
}

unsigned int LoRaWANClass::getFrameCounterTx()
{
    return Frame_Counter_Tx;
}

void LoRaWANClass::setFrameCounterTx(unsigned int FrameCounter)
{
    Frame_Counter_Tx = FrameCounter;
}

unsigned int LoRaWANClass::getFrameCounterRx()
{
    return Frame_Counter_Rx;
}

void LoRaWANClass::setFrameCounterRx(unsigned int FrameCounter)
{
    Frame_Counter_Rx = FrameCounter;
}


