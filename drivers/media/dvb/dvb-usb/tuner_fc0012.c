/**

@file

@brief   FC0012 tuner module definition

One can manipulate FC0012 tuner through FC0012 module.
FC0012 module is derived from tuner module.

*/


#include "tuner_fc0012.h"





/**

@brief   FC0012 tuner module builder

Use BuildFc0012Module() to build FC0012 module, set all module function pointers with the corresponding functions,
and initialize module private variables.


@param [in]   ppTuner                      Pointer to FC0012 tuner module pointer
@param [in]   pTunerModuleMemory           Pointer to an allocated tuner module memory
@param [in]   pBaseInterfaceModuleMemory   Pointer to an allocated base interface module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   FC0012 I2C device address
@param [in]   CrystalFreqHz                FC0012 crystal frequency in Hz


@note
	-# One should call BuildFc0012Module() to build FC0012 module before using it.

*/
void
BuildFc0012Module(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned long CrystalFreqHz
	)
{
	TUNER_MODULE *pTuner;
	FC0012_EXTRA_MODULE *pExtra;



	// Set tuner module pointer.
	*ppTuner = pTunerModuleMemory;

	// Get tuner module.
	pTuner = *ppTuner;

	// Set base interface module pointer and I2C bridge module pointer.
	pTuner->pBaseInterface = pBaseInterfaceModuleMemory;
	pTuner->pI2cBridge = pI2cBridgeModuleMemory;

	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc0012);



	// Set tuner type.
	pTuner->TunerType = TUNER_TYPE_FC0012;

	// Set tuner I2C device address.
	pTuner->DeviceAddr = DeviceAddr;


	// Initialize tuner parameter setting status.
	pTuner->IsRfFreqHzSet = NO;


	// Set tuner module manipulating function pointers.
	pTuner->GetTunerType  = fc0012_GetTunerType;
	pTuner->GetDeviceAddr = fc0012_GetDeviceAddr;

	pTuner->Initialize    = fc0012_Initialize;
	pTuner->SetRfFreqHz   = fc0012_SetRfFreqHz;
	pTuner->GetRfFreqHz   = fc0012_GetRfFreqHz;


	// Initialize tuner extra module variables.
	pExtra->CrystalFreqHz      = CrystalFreqHz;
	pExtra->IsBandwidthModeSet = NO;

	// Set tuner extra module function pointers.
	pExtra->SetBandwidthMode = fc0012_SetBandwidthMode;
	pExtra->GetBandwidthMode = fc0012_GetBandwidthMode;


	// Set tuner RF frequency and tuner bandwidth mode.
	// Note: Need to give default tuner RF frequency and tuner bandwidth mode,
	//       because FC0012 API use one funnction to set RF frequency and bandwidth mode.
	pTuner->RfFreqHz      = FC0012_RF_FREQ_HZ_DEFAULT;
	pExtra->BandwidthMode = FC0012_BANDWIDTH_MODE_DEFAULT;


	return;
}





/**

@see   TUNER_FP_GET_TUNER_TYPE

*/
void
fc0012_GetTunerType(
	TUNER_MODULE *pTuner,
	int *pTunerType
	)
{
	// Get tuner type from tuner module.
	*pTunerType = pTuner->TunerType;


	return;
}





/**

@see   TUNER_FP_GET_DEVICE_ADDR

*/
void
fc0012_GetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char *pDeviceAddr
	)
{
	// Get tuner I2C device address from tuner module.
	*pDeviceAddr = pTuner->DeviceAddr;


	return;
}





/**

@see   TUNER_FP_INITIALIZE

*/
int
fc0012_Initialize(
	TUNER_MODULE *pTuner
	)
{
	FC0012_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc0012);


	// Initialize tuner.
	if(FC0012_Open(pTuner) != FC0012_FUNCTION_SUCCESS)
		goto error_status_initialize_tuner;


	return FUNCTION_SUCCESS;


error_status_initialize_tuner:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_SET_RF_FREQ_HZ

*/
int
fc0012_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	)
{
	FC0012_EXTRA_MODULE *pExtra;
	unsigned long RfFreqKhz;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc0012);


	// Set tuner RF frequency in KHz.
	// Note: RfFreqKhz = round(RfFreqHz / 1000)
	RfFreqKhz = (RfFreqHz + 500) / 1000;

	if(FC0011_SetFrequency(pTuner, RfFreqKhz, (unsigned short)(pExtra->BandwidthMode)) != FC0012_FUNCTION_SUCCESS)
		goto error_status_set_tuner_rf_frequency;


	// Set tuner RF frequency parameter.
	pTuner->RfFreqHz      = RfFreqHz;
	pTuner->IsRfFreqHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_tuner_rf_frequency:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_GET_RF_FREQ_HZ

*/
int
fc0012_GetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long *pRfFreqHz
	)
{
	// Get tuner RF frequency in Hz from tuner module.
	if(pTuner->IsRfFreqHzSet != YES)
		goto error_status_get_tuner_rf_frequency;

	*pRfFreqHz = pTuner->RfFreqHz;


	return FUNCTION_SUCCESS;


error_status_get_tuner_rf_frequency:
	return FUNCTION_ERROR;
}





/**

@brief   Set FC0012 tuner bandwidth mode.

*/
int
fc0012_SetBandwidthMode(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	)
{
	FC0012_EXTRA_MODULE *pExtra;
	unsigned long RfFreqKhz;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc0012);


	// Set tuner bandwidth mode.
	// Note: RfFreqKhz = round(RfFreqHz / 1000)
	RfFreqKhz = (pTuner->RfFreqHz + 500) / 1000;

	if(FC0011_SetFrequency(pTuner, RfFreqKhz, (unsigned short)BandwidthMode) != FC0012_FUNCTION_SUCCESS)
		goto error_status_set_tuner_bandwidth_mode;


	// Set tuner bandwidth mode parameter.
	pExtra->BandwidthMode      = BandwidthMode;
	pExtra->IsBandwidthModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_tuner_bandwidth_mode:
	return FUNCTION_ERROR;
}





/**

@brief   Get FC0012 tuner bandwidth mode.

*/
int
fc0012_GetBandwidthMode(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	)
{
	FC0012_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc0012);


	// Get tuner bandwidth mode from tuner module.
	if(pExtra->IsBandwidthModeSet != YES)
		goto error_status_get_tuner_bandwidth_mode;

	*pBandwidthMode = pExtra->BandwidthMode;


	return FUNCTION_SUCCESS;


error_status_get_tuner_bandwidth_mode:
	return FUNCTION_ERROR;
}























// The following context is implemented for FC0012 source code.


// Read FC0012 register.
int FC0012_Read(TUNER_MODULE *pTuner, unsigned char RegAddr, unsigned char *pByte)
{
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char DeviceAddr;


	// Get I2C bridge.
	pI2cBridge = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Set tuner register reading address.
	// Note: The I2C format of tuner register reading address setting is as follows:
	//       start_bit + (DeviceAddr | writing_bit) + addr + stop_bit
	if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, &RegAddr, LEN_1_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_register_reading_address;

	// Get tuner register byte.
	// Note: The I2C format of tuner register byte getting is as follows:
	//       start_bit + (DeviceAddr | reading_bit) + read_data + stop_bit
	if(pI2cBridge->ForwardI2cReadingCmd(pI2cBridge, DeviceAddr, pByte, LEN_1_BYTE) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;


	return FC0012_I2C_SUCCESS;


error_status_get_tuner_registers:
error_status_set_tuner_register_reading_address:
	return FC0012_I2C_ERROR;
}





// Write FC0012 register.
int FC0012_Write(TUNER_MODULE *pTuner, unsigned char RegAddr, unsigned char Byte)
{
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char DeviceAddr;
	unsigned char WritingBuffer[LEN_2_BYTE];


	// Get I2C bridge.
	pI2cBridge = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Set writing bytes.
	// Note: The I2C format of tuner register byte setting is as follows:
	//       start_bit + (DeviceAddr | writing_bit) + addr + data + stop_bit
	WritingBuffer[0] = RegAddr;
	WritingBuffer[1] = Byte;

	// Set tuner register bytes with writing buffer.
	if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, WritingBuffer, LEN_2_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	return FC0012_I2C_SUCCESS;


error_status_set_tuner_registers:
	return FC0012_I2C_ERROR;
}





// Set FC0012 register mask bits.
int
fc0012_SetRegMaskBits(
	TUNER_MODULE *pTuner,
	unsigned char RegAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned char WritingValue
	)
{
	int i;

	unsigned char ReadingByte;
	unsigned char WritingByte;

	unsigned char Mask;
	unsigned char Shift;


	// Generate mask and shift according to MSB and LSB.
	Mask = 0;

	for(i = Lsb; i < (Msb + 1); i++)
		Mask |= 0x1 << i;

	Shift = Lsb;


	// Get tuner register byte according to register adddress.
	if(FC0012_Read(pTuner, RegAddr, &ReadingByte) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;


	// Reserve byte unmask bit with mask and inlay writing value into it.
	WritingByte = ReadingByte & (~Mask);
	WritingByte |= (WritingValue << Shift) & Mask;


	// Write tuner register byte with writing byte.
	if(FC0012_Write(pTuner, RegAddr, WritingByte) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	return FUNCTION_SUCCESS;


error_status_get_tuner_registers:
error_status_set_tuner_registers:
	return FUNCTION_ERROR;
}





// Get FC0012 register mask bits.
int
fc0012_GetRegMaskBits(
	TUNER_MODULE *pTuner,
	unsigned char RegAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned char *pReadingValue
	)
{
	int i;

	unsigned char ReadingByte;

	unsigned char Mask;
	unsigned char Shift;


	// Generate mask and shift according to MSB and LSB.
	Mask = 0;

	for(i = Lsb; i < (Msb + 1); i++)
		Mask |= 0x1 << i;

	Shift = Lsb;


	// Get tuner register byte according to register adddress.
	if(FC0012_Read(pTuner, RegAddr, &ReadingByte) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;


	// Get register bits from reading byte with mask and shift
	*pReadingValue = (ReadingByte & Mask) >> Shift;


	return FUNCTION_SUCCESS;


error_status_get_tuner_registers:
	return FUNCTION_ERROR;
}



























// The following context is source code provided by fitipower.





// fitipower source code - FC0012_Tuner_Code.cpp


//===========================================================
//	Fitipower Integrated Technology Inc.
//
//	FC0012 Tuner Code
//
//	Version 1.2e
//
//	Date: 2009/09/15
//
//	Copyright 2008, All rights reversed.
//
//	Compile in Visual Studio 2005 C++ Win32 Console
//
//---------------------------------------------------------------------
//	Modify History: 
//		2009-05-11: Change to recieve 28.8 MHz clock
//		2009-05-14: fix frequency lock problem on 111MHz~148MHz
//		2009-05-15: remove the limitation of Xin range
//		2009-07-30: Add VHF filter control
//					Add VHF frequency offset
//					Add reference RSSI function
//					Change register[0x07] to 0x20
//		2009-09-15: update VCO re-calibration function
//---------------------------------------------------------------------
//=====================================================================

// Data Format:
// BYTE: unsigned char, 1 byte, 8 bits
// WORD: unsighed short, 2 bytes, 16 bits
// DWORD: unsigned int, 4 bytes, 32 bits

// include header, just for testing.
//#include "stdafx.h"
//#include "stdlib.h"
//#include <complex>

//void FC0012_Write(unsigned char address, unsigned char data);
//unsigned char FC0012_Read(unsigned char address);
//void FC0012_Open();
//void FC0012_Close();
//void FC0012_SetFrequency(unsigned int Frequency, unsigned short Bandwidth);

/*
// Console main function, just for testing
int main(int argc, const char* argv[])
{
	printf("\n");

	if ( argc > 1 )
	{
		for( int i = 1; i < argc; i++ )
		{
			FC0012_SetFrequency( atoi(argv[i]), 6);
		}	
	}

	return 0;
}

// FC0012 I2C Write Function
void FC0012_Write(unsigned char address, unsigned char data)
{
	// depend on driver function in demodulator vendor.
}

// FC0012 I2C Read Function
unsigned char FC0012_Read(unsigned char address)
{
	// depend on driver function in demodulator vendor.
	unsigned char value = 0;
	// value = ........
	return value;
}
*/

// FC0012 Open Function, includes enable/reset pin control and registers initialization.
int FC0012_Open(TUNER_MODULE *pTuner)
{
	FC0012_EXTRA_MODULE *pExtra;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc0012);


	// Enable FC0012 Power
//	(...)
	// FC0012 Enable = High
//	(...)
	// FC0012 Reset = High -> Low
//	(...)

    //================================ Initial FC0012 Tuner Register
    if(FC0012_Write(pTuner, 0x01, 0x05) != FC0012_I2C_SUCCESS) goto error_status;
    if(FC0012_Write(pTuner, 0x02, 0x10) != FC0012_I2C_SUCCESS) goto error_status;
    if(FC0012_Write(pTuner, 0x03, 0x00) != FC0012_I2C_SUCCESS) goto error_status;
    if(FC0012_Write(pTuner, 0x04, 0x00) != FC0012_I2C_SUCCESS) goto error_status;
	
	
	//===================================== Arios Modify - 2009-10-23
	// modify for Realtek CNR test
    if(FC0012_Write(pTuner, 0x05, 0x0F) != FC0012_I2C_SUCCESS) goto error_status;
	//===================================== Arios Modify - 2009-10-23

	
    if(FC0012_Write(pTuner, 0x06, 0x00) != FC0012_I2C_SUCCESS) goto error_status;      // divider 2, VCO slow.

	switch(pExtra->CrystalFreqHz)      // Gain Shift: 15	// Set bit 5 to 1 for 28.8 MHz clock input (2009/05/12)
	{
		default:
		case CRYSTAL_FREQ_28800000HZ:

			if(FC0012_Write(pTuner, 0x07, 0x20) != FC0012_I2C_SUCCESS) goto error_status;

			break;


		case CRYSTAL_FREQ_27000000HZ:

			if(FC0012_Write(pTuner, 0x07, 0x20) != FC0012_I2C_SUCCESS) goto error_status;

			break;


		case CRYSTAL_FREQ_36000000HZ:

			if(FC0012_Write(pTuner, 0x07, 0x00) != FC0012_I2C_SUCCESS) goto error_status;

			break;
	}

	if(FC0012_Write(pTuner, 0x08, 0xFF) != FC0012_I2C_SUCCESS) goto error_status;      // AGC Clock divide by 256, AGC gain 1/256, Loop Bw 1/8
    if(FC0012_Write(pTuner, 0x09, 0x6E) != FC0012_I2C_SUCCESS) goto error_status;      // Disable LoopThrough
    if(FC0012_Write(pTuner, 0x0A, 0xB8) != FC0012_I2C_SUCCESS) goto error_status;      // Disable LO Test Buffer
    if(FC0012_Write(pTuner, 0x0B, 0x82) != FC0012_I2C_SUCCESS) goto error_status;      // Output Clock is same as clock frequency

	//--------------------------------------------------------------------------
	// reg[12] need to be changed if the system is in AGC Up-Down mode
	//--------------------------------------------------------------------------
//	if(FC0012_Write(pTuner, 0x0C, 0xF8) != FC0012_I2C_SUCCESS) goto error_status;      

	// Modified for up-dowm AGC by Realtek.
	if(FC0012_Write(pTuner, 0x0C, 0xFC) != FC0012_I2C_SUCCESS) goto error_status;      

	// 0x0D, val=0x2 for DVBT
	if(FC0012_Write(pTuner, 0x0D, 0x02) != FC0012_I2C_SUCCESS) goto error_status;      // AGC Not Forcing & LNA Forcing 
	
	// Modified for 2836B DTMB by Realtek.
//	if(FC0012_Write(pTuner, 0x0D, 0x06) != FC0012_I2C_SUCCESS) goto error_status;      // AGC Not Forcing & LNA Forcing 
    
	if(FC0012_Write(pTuner, 0x0E, 0x00) != FC0012_I2C_SUCCESS) goto error_status;
    if(FC0012_Write(pTuner, 0x0F, 0x00) != FC0012_I2C_SUCCESS) goto error_status;
    if(FC0012_Write(pTuner, 0x10, 0x00) != FC0012_I2C_SUCCESS) goto error_status;
	if(FC0012_Write(pTuner, 0x11, 0x00) != FC0012_I2C_SUCCESS) goto error_status;
	if(FC0012_Write(pTuner, 0x12, 0x1F) != FC0012_I2C_SUCCESS) goto error_status;	   // Set to maximum gain

	
	//===================================== Arios Modify - 2009-10-23
	// modify for Realtek CNR test
//    if(FC0012_Write(pTuner, 0x13, 0x10) != FC0012_I2C_SUCCESS) goto error_status;	   // Enable IX2, Set to High Gain
    if(FC0012_Write(pTuner, 0x13, 0x08) != FC0012_I2C_SUCCESS) goto error_status;	   // Enable IX2, Set to Middle Gain
//    if(FC0012_Write(pTuner, 0x13, 0x00) != FC0012_I2C_SUCCESS) goto error_status;	   // Enable IX2, Set to Low Gain
	//===================================== Arios Modify - 2009-10-23

    if(FC0012_Write(pTuner, 0x14, 0x00) != FC0012_I2C_SUCCESS) goto error_status;
    if(FC0012_Write(pTuner, 0x15, 0x04) != FC0012_I2C_SUCCESS) goto error_status;	   // Enable LNA COMPS

	return FC0012_FUNCTION_SUCCESS;

error_status:
	return FC0012_FUNCTION_ERROR;
}


/*
// FC0012 Close Function, control enable/reset and power.
void FC0012_Close()
{
	// FC0012 Enable = Low
	// (...)
	// FC0012 Reset = Low -> High
	// (...)
	// Disable FC0012 Power
	// (...)
}

void Delay(unsigned int)
{
	// delay function
}


//===================================== RSSI & LNA Control			2009/07/30
// better ACI Performance for D-Book & field-test
void FC0012_RSSI()
{
	unsigned char Input_Power;

	Delay(200);								// Delay 200 ms


	switch( FC0012_Read(0x13) )					// Read FC0012 LNA gain setting
	{
		case 0x10:								// High gain 19.7 dB
			if( Input_Power > -40 )				// if intput power level is bigger than -40 dBm
				FC0012_Write(0x13, 0x17);		// Switch to 17.9 dB
			break;

		case 0x17:								// High gain 17.9 dB
			if( Input_Power > -15 )				// if intput power level is bigger than -15 dBm
				FC0012_Write(0x13, 0x08);		// Switch to 7.1 dB		
			else if( Input_Power < -45 )		// if intput power level is smaller than -45 dBm
				FC0012_Write(0x13, 0x10);		// Switch to 19.7 dB
			break;

		case 0x08:								// Middle gain 7.1 dB
			if( Input_Power > -5 )				// if intput power level is bigger than -5 dBm
				FC0012_Write(0x13, 0x02);		// Switch to -9.9 dB
			else if( Input_Power < -20 )		// if intput power level is smaller than -20 dBm
				FC0012_Write(0x13, 0x17);		// Switch to 17.9 dB
			break;

		case 0x02:								// Low gain -9.9 dB
			if( Input_Power < -12 )				// if intput power level is smaller than -12 dBm
				FC0012_Write(0x13, 0x08);		// Switch to 7.1 dB
			break;
	}

}




//===================================== Frequency Control  2009/07/30
// Frequency unit: KHz, bandwidth unit: MHz
void FC0012_Frequency_Control(unsigned int Frequency, unsigned short Bandwidth)
{
	if( Frequency < 260000 && Frequency > 150000 )
	{
		// set GPIO6 = low

		//	1. Set tuner frequency
		//	2. if the program quality is not good enough, switch to frequency + 500kHz
		//	3. if the program quality is still no good, switch to frequency - 500kHz
	}
	else
	{
		// set GPIO6 = high

		// set tuner frequency
	}
}
*/



// FC0012 frequency/bandwidth setting function.
// Frequency unit: KHz, bandwidth unit: MHz
int FC0011_SetFrequency(TUNER_MODULE *pTuner, unsigned long Frequency, unsigned short Bandwidth)
{
//    bool VCO1 = false;
//    unsigned int doubleVCO;
//    unsigned short xin, xdiv;
//	unsigned char reg[21], am, pm, multi;
    int VCO1 = FC0012_FALSE;
    unsigned long doubleVCO;
    unsigned short xin, xdiv;
	unsigned char reg[21], am, pm, multi;
	unsigned char read_byte;

	FC0012_EXTRA_MODULE *pExtra;
	unsigned long CrystalFreqKhz;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Fc0012);

	// Get tuner crystal frequency in KHz.
	// Note: CrystalFreqKhz = round(CrystalFreqHz / 1000)
	CrystalFreqKhz = (pExtra->CrystalFreqHz + 500) / 1000;


	//===================================== Select frequency divider and the frequency of VCO
	if (Frequency * 96 < 3560000)
    {
        multi = 96;
        reg[5] = 0x82;
        reg[6] = 0x00;
    }
    else if (Frequency * 64 < 3560000)
    {
        multi = 64;
        reg[5] = 0x82;
        reg[6] = 0x02;
    }
    else if (Frequency * 48 < 3560000)
    {
        multi = 48;
        reg[5] = 0x42;
        reg[6] = 0x00;
    }
    else if (Frequency * 32 < 3560000)
    {
        multi = 32;
        reg[5] = 0x42;
        reg[6] = 0x02;
    }
    else if (Frequency * 24 < 3560000)
    {
        multi = 24;
        reg[5] = 0x22;
        reg[6] = 0x00;
    }
    else if (Frequency * 16 < 3560000)
    {
        multi = 16;
        reg[5] = 0x22;
        reg[6] = 0x02;
    }
    else if (Frequency * 12 < 3560000)
    {
        multi = 12;
        reg[5] = 0x12;
        reg[6] = 0x00;
    }
    else if (Frequency * 8 < 3560000)
    {
        multi = 8;
        reg[5] = 0x12;
        reg[6] = 0x02;
    }
    else if (Frequency * 6 < 3560000)
    {
        multi = 6;
        reg[5] = 0x0A;
        reg[6] = 0x00;
    }
    else
    {
        multi = 4;
        reg[5] = 0x0A;
        reg[6] = 0x02;
    }

    doubleVCO = Frequency * multi;



	//===================================== Arios Modify - 2009-10-23
	// modify for Realtek CNR test
    reg[6] = reg[6] | 0x08;
//	VCO1 = true;
	VCO1 = FC0012_TRUE;
	/*
    if (doubleVCO >= 3060000)
    {
        reg[6] = reg[6] | 0x08;
//        VCO1 = true;
        VCO1 = FC0012_TRUE;
    }
	*/
	//===================================== Arios Modify - 2009-10-23

	//===================================== From divided value (XDIV) determined the FA and FP value 
//	xdiv = (unsigned short)(doubleVCO / 14400);		// implement round function, 2009-05-01 by Arios
//	if( (doubleVCO - xdiv * 14400) >= 7200 )
	xdiv = (unsigned short)(doubleVCO / (CrystalFreqKhz / 2));		// implement round function, 2009-05-01 by Arios
	if( (doubleVCO - xdiv * (CrystalFreqKhz / 2)) >= (CrystalFreqKhz / 4) )
		xdiv = xdiv + 1;

	pm = (unsigned char)( xdiv / 8 );			
    am = (unsigned char)( xdiv - (8 * pm));		

    if (am < 2)
    {
        reg[1] = am + 8;
        reg[2] = pm - 1;
    }
    else
    {
        reg[1] = am;
        reg[2] = pm;
    }

	//===================================== From VCO frequency determines the XIN ( fractional part of Delta Sigma PLL) and divided value (XDIV).
//	xin = (unsigned short)(doubleVCO - ((unsigned short)(doubleVCO / 14400)) * 14400);						
//	xin = ((xin << 15)/14400);															
	xin = (unsigned short)(doubleVCO - ((unsigned short)(doubleVCO / (CrystalFreqKhz / 2))) * (CrystalFreqKhz / 2));						
	xin = ((xin << 15)/(unsigned short)(CrystalFreqKhz / 2));															

//	if( xin >= (unsigned short) pow( (double)2, (double)14) )
//		xin = xin + (unsigned short) pow( (double)2, (double)15);
	if( xin >= (unsigned short) 16384 )
		xin = xin + (unsigned short) 32768;

	reg[3] = (unsigned char)(xin >> 8);			
	reg[4] = (unsigned char)(xin & 0x00FF);		

	
	//===================================== Only for testing, 2009-05-01 by Arios
//	printf("Frequency: %d, Fa: %d, Fp: %d, Xin:%d \n", Frequency, am, pm, xin);

	//===================================== Set Bandwidth
    switch(Bandwidth)
    {
        case 6: 
			reg[6] = 0x80 | reg[6];
            break;
        case 7: 
			reg[6] = ~0x80 & reg[6];
            reg[6] = 0x40 | reg[6];
            break;
        case 8:
        default: 
			reg[6] = ~0xC0 & reg[6];
            break;
    }

	//===================================== Write registers 
    if(FC0012_Write(pTuner, 0x01, reg[1]) != FC0012_I2C_SUCCESS) goto error_status;
    if(FC0012_Write(pTuner, 0x02, reg[2]) != FC0012_I2C_SUCCESS) goto error_status;
    if(FC0012_Write(pTuner, 0x03, reg[3]) != FC0012_I2C_SUCCESS) goto error_status;
    if(FC0012_Write(pTuner, 0x04, reg[4]) != FC0012_I2C_SUCCESS) goto error_status;

	//===================================== Arios Modify - 2009-10-23
	// modify for Realtek CNR Test
	reg[5] = reg[5] | 0x07;
    if(FC0012_Write(pTuner, 0x05, reg[5]) != FC0012_I2C_SUCCESS) goto error_status;
	//===================================== Arios Modify - 2009-10-23
    
    if(FC0012_Write(pTuner, 0x06, reg[6]) != FC0012_I2C_SUCCESS) goto error_status;

	//===================================== VCO Calibration
    if(FC0012_Write(pTuner, 0x0E, 0x80) != FC0012_I2C_SUCCESS) goto error_status;
    if(FC0012_Write(pTuner, 0x0E, 0x00) != FC0012_I2C_SUCCESS) goto error_status;

	//===================================== VCO Re-Calibration if needed
	if(FC0012_Write(pTuner, 0x0E, 0x00) != FC0012_I2C_SUCCESS) goto error_status;
//    reg[14] = 0x3F & FC0012_Read(0x0E);
	if(FC0012_Read(pTuner, 0x0E, &read_byte) != FC0012_I2C_SUCCESS) goto error_status;
    reg[14] = 0x3F & read_byte;

	if (VCO1)
    {
        if (reg[14] > 0x3C)				// modify 2009-09-15
        {
            reg[6] = ~0x08 & reg[6];

            if(FC0012_Write(pTuner, 0x06, reg[6]) != FC0012_I2C_SUCCESS) goto error_status;

            if(FC0012_Write(pTuner, 0x0E, 0x80) != FC0012_I2C_SUCCESS) goto error_status;
            if(FC0012_Write(pTuner, 0x0E, 0x00) != FC0012_I2C_SUCCESS) goto error_status;
        }
    }
    else
    {
        if (reg[14] < 0x02)				// modify 2009-09-15
        {
            reg[6] = 0x08 | reg[6];

            if(FC0012_Write(pTuner, 0x06, reg[6]) != FC0012_I2C_SUCCESS) goto error_status;

            if(FC0012_Write(pTuner, 0x0E, 0x80) != FC0012_I2C_SUCCESS) goto error_status;
            if(FC0012_Write(pTuner, 0x0E, 0x00) != FC0012_I2C_SUCCESS) goto error_status;
        }
    }

	return FC0012_FUNCTION_SUCCESS;

error_status:
	return FC0012_FUNCTION_ERROR;
}











