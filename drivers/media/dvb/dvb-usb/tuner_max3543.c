/**

@file

@brief   MAX3543 tuner module definition

One can manipulate MAX3543 tuner through MAX3543 module.
MAX3543 module is derived from tuner module.

*/


#include "tuner_max3543.h"





/**

@brief   MAX3543 tuner module builder

Use BuildMax3543Module() to build MAX3543 module, set all module function pointers with the corresponding functions,
and initialize module private variables.


@param [in]   ppTuner                      Pointer to MAX3543 tuner module pointer
@param [in]   pTunerModuleMemory           Pointer to an allocated tuner module memory
@param [in]   pBaseInterfaceModuleMemory   Pointer to an allocated base interface module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   MAX3543 I2C device address
@param [in]   CrystalFreqHz                MAX3543 crystal frequency in Hz
@param [in]   StandardMode                 MAX3543 standard mode
@param [in]   IfFreqHz                     MAX3543 IF frequency in Hz
@param [in]   OutputMode                   MAX3543 output mode


@note
	-# One should call BuildMax3543Module() to build MAX3543 module before using it.

*/
void
BuildMax3543Module(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned long CrystalFreqHz,
	int StandardMode,
	unsigned long IfFreqHz,
	int OutputMode
	)
{
	TUNER_MODULE *pTuner;
	MAX3543_EXTRA_MODULE *pExtra;



	// Set tuner module pointer.
	*ppTuner = pTunerModuleMemory;

	// Get tuner module.
	pTuner = *ppTuner;

	// Set base interface module pointer and I2C bridge module pointer.
	pTuner->pBaseInterface = pBaseInterfaceModuleMemory;
	pTuner->pI2cBridge = pI2cBridgeModuleMemory;

	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);



	// Set tuner type.
	pTuner->TunerType = TUNER_TYPE_MAX3543;

	// Set tuner I2C device address.
	pTuner->DeviceAddr = DeviceAddr;


	// Initialize tuner parameter setting status.
	pTuner->RfFreqHz      = MAX3543_RF_FREQ_HZ_DEFAULT;
	pTuner->IsRfFreqHzSet = NO;


	// Set tuner module manipulating function pointers.
	pTuner->GetTunerType  = max3543_GetTunerType;
	pTuner->GetDeviceAddr = max3543_GetDeviceAddr;

	pTuner->Initialize    = max3543_Initialize;
	pTuner->SetRfFreqHz   = max3543_SetRfFreqHz;
	pTuner->GetRfFreqHz   = max3543_GetRfFreqHz;


	// Initialize tuner extra module variables.
	pExtra->CrystalFreqHz      = CrystalFreqHz;
	pExtra->StandardMode       = StandardMode;
	pExtra->IfFreqHz           = IfFreqHz;
	pExtra->OutputMode         = OutputMode;
	pExtra->BandwidthMode      = MAX3543_BANDWIDTH_MODE_DEFAULT;
	pExtra->IsBandwidthModeSet = NO;

	pExtra->broadcast_standard = StandardMode;

	switch(CrystalFreqHz)
	{
		default:
		case CRYSTAL_FREQ_16000000HZ:

			switch(IfFreqHz)
			{
				default:
				case IF_FREQ_36170000HZ:
					
					pExtra->XTALSCALE = 8;
					pExtra->XTALREF = 128;
					pExtra->LOSCALE = 65;

					break;
			}
	}


	// Set tuner extra module function pointers.
	pExtra->SetBandwidthMode = max3543_SetBandwidthMode;
	pExtra->GetBandwidthMode = max3543_GetBandwidthMode;


	return;
}





/**

@see   TUNER_FP_GET_TUNER_TYPE

*/
void
max3543_GetTunerType(
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
max3543_GetDeviceAddr(
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
max3543_Initialize(
	TUNER_MODULE *pTuner
	)
{
	MAX3543_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);


	// Initializing tuner.
	// Note: Call MAX3543 source code function.
	if(MAX3543_Init(pTuner) != MAX3543_SUCCESS)
		goto error_status_execute_function;

	// Set tuner standard mode and output mode.
	// Note: Call MAX3543 source code function.
	if(MAX3543_Standard(pTuner, pExtra->StandardMode, pExtra->OutputMode) != MAX3543_SUCCESS)
		goto error_status_execute_function;


	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_SET_RF_FREQ_HZ

*/
int
max3543_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	)
{
	MAX3543_EXTRA_MODULE *pExtra;

	unsigned long FreqUnit;
	unsigned short CalculatedValue;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);


	// Calculate frequency unit.
	FreqUnit = MAX3543_CONST_1_MHZ / pExtra->LOSCALE;

	// Set tuner RF frequency in KHz.
	// Note: 1. CalculatedValue = round(RfFreqHz / FreqUnit)
	//       2. Call MAX3543 source code function.
	CalculatedValue = (unsigned short)((RfFreqHz + (FreqUnit / 2)) / FreqUnit);

	if(MAX3543_SetFrequency(pTuner, CalculatedValue) != MAX3543_SUCCESS)
		goto error_status_execute_function;


	// Set tuner RF frequency parameter.
	pTuner->RfFreqHz      = RfFreqHz;
	pTuner->IsRfFreqHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_GET_RF_FREQ_HZ

*/
int
max3543_GetRfFreqHz(
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

@brief   Set MAX3543 tuner bandwidth mode.

*/
int
max3543_SetBandwidthMode(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	)
{
	MAX3543_EXTRA_MODULE *pExtra;

	unsigned short BandwidthModeUshort;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);


	// Set tuner bandwidth mode.
	// Note: Call MAX3543 source code function.
	BandwidthModeUshort = (unsigned short)BandwidthMode;

	if(MAX3543_ChannelBW(pTuner, BandwidthModeUshort) != MAX3543_SUCCESS)
		goto error_status_execute_function;


	// Set tuner bandwidth Hz parameter.
	pExtra->BandwidthMode      = BandwidthMode;
	pExtra->IsBandwidthModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@brief   Get MAX3543 tuner bandwidth mode.

*/
int
max3543_GetBandwidthMode(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	)
{
	MAX3543_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);


	// Get tuner bandwidth Hz from tuner module.
	if(pExtra->IsBandwidthModeSet != YES)
		goto error_status_get_tuner_bandwidth_mode;

	*pBandwidthMode = pExtra->BandwidthMode;


	return FUNCTION_SUCCESS;


error_status_get_tuner_bandwidth_mode:
	return FUNCTION_ERROR;
}























// Function (implemeted for MAX3543)
int
Max3543_Read(
	TUNER_MODULE *pTuner,
	unsigned short RegAddr,
	unsigned short *pData
	)
{
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char DeviceAddr;
	unsigned char WritingByte;
	unsigned char ReadingByte;


	// Get I2C bridge.
	pI2cBridge = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Set tuner register reading address.
	// Note: The I2C format of tuner register reading address setting is as follows:
	//       start_bit + (DeviceAddr | writing_bit) + addr + stop_bit
	WritingByte = (unsigned char)RegAddr;
	if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, &WritingByte, LEN_1_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_register_reading_address;

	// Get tuner register byte.
	// Note: The I2C format of tuner register byte getting is as follows:
	//       start_bit + (DeviceAddr | reading_bit) + read_data + stop_bit
	if(pI2cBridge->ForwardI2cReadingCmd(pI2cBridge, DeviceAddr, &ReadingByte, LEN_1_BYTE) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	*pData = (unsigned short)ReadingByte;


	return MAX3543_SUCCESS;


error_status_get_tuner_registers:
error_status_set_tuner_register_reading_address:
	return MAX3543_ERROR;
}





int
Max3543_Write(
	TUNER_MODULE *pTuner,
	unsigned short RegAddr,
	unsigned short Data
	)
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
	WritingBuffer[0] = (unsigned char)RegAddr;
	WritingBuffer[1] = (unsigned char)Data;

	// Set tuner register bytes with writing buffer.
	if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, WritingBuffer, LEN_2_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	return MAX3543_SUCCESS;


error_status_set_tuner_registers:
	return MAX3543_ERROR;
}























// The following context is source code provided by MAXIM.





// MAXIM source code - Max3543_Driver.c


/*
------------------------------------------------------------------------- 
| MAX3543 Tuner Driver                                               
| Author: Paul Nichol                                                       
|                                                                           
| Version: 1.0.3                                                            
| Date:    12/09/09                                                         
|                                                                           
|                                                                           
| Copyright (C) 2009 Maxim Integrated Products.                             
| PLEASE READ THE ATTACHED LICENSE CAREFULLY BEFORE USING THIS SOFTWARE.
| BY USING THE SOFTWARE OF MAXIM INTEGRATED PRODUCTS, INC, YOU ARE AGREEING 
| TO BE BOUND BY THE TERMS OF THE ATTACHED LICENSE, WHICH INCLUDES THE SOFTWARE
| LICENSE AND SOFTWARE WARRANTY DISCLAIMER, EVEN WITHOUT YOUR SIGNATURE. 
| IF YOU DO NOT AGREE TO THE TERMS OF THIS AGREEMENT, DO NOT USE THIS SOFTWARE.
|
| IMPORTANT: This code is operate the Max3543 Multi-Band Terrestrial        
| Hybrid Tuner.  Routines include: initializing, tuning, reading the        
| ROM table, reading the lock detect status and tuning the tracking         
| filter.  Only integer math is used in this program and no floating point
| variables are used.  Your MCU must be capable of processing unsigned 32 bit integer
| math to use this routine.  (That is: two 16 bit numbers multiplied resulting in a 
| 32 bit result, or a 32 bit number divided by a 16 bit number).
|                                                                           
-------------------------------------------------------------------------- 
*/


//#include <stdio.h>
//#include <string.h>
///#include <stdlib.h>
//#include "mxmdef.h"
//#include "Max3543_Driver.h"
    
/*
double debugreg[10];
UINT_16 TFRomCoefs[3][4];
UINT_16 denominator;   
UINT_32 fracscale ;
UINT_16 regs[22];
UINT_16 IF_Frequency;
*/

/* table of fixed coefficients for the tracking filter equations. */

const
static UINT_16 co[6][5]={{ 26, 6,  68, 20, 45 },  /* VHF LO TFS */
                        { 16, 8,  88, 40, 0 },  /* VHF LO TFP */
                        { 27, 10, 54, 30,20 },  /* VHF HI TFS */
                        { 18, 10, 41, 20, 0 },  /* VHF HI TFP */
                        { 32, 10, 34, 8, 10 },  /* UHF TFS */
                        { 13, 15, 21, 16, 0 }}; /* UHF TFP */



//int MAX3543_Init(TUNER_MODULE *pTuner, UINT_16 RfFreq)
int MAX3543_Init(TUNER_MODULE *pTuner)
{  
	/* 
		Initialize every register. Don't rely on MAX3543 power on reset.  
	   Call before using the other routines in this file.                
   */  
   UINT_16 RegNumber;
   
	MAX3543_EXTRA_MODULE *pExtra;

	unsigned long FreqUnit;
	unsigned short CalculatedValue;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);


   /*Initialize the registers in the MAX3543:*/



	pExtra->regs[REG3543_VCO]				=	0x4c;
	pExtra->regs[REG3543_NDIV]			=	0x2B;
	pExtra->regs[REG3543_FRAC2]			=	0x8E; 
	pExtra->regs[REG3543_FRAC1]			=	0x26;
	pExtra->regs[REG3543_FRAC0]			=	0x66;
	pExtra->regs[REG3543_MODE]			=	0xd8;
	pExtra->regs[REG3543_TFS]				=	0x00;
	pExtra->regs[REG3543_TFP]				= 	0x00;
	pExtra->regs[REG3543_SHDN]			=	0x00;
	pExtra->regs[REG3543_REF]				=	0x0a;
	pExtra->regs[REG3543_VAS]				=	0x17;
	pExtra->regs[REG3543_PD_CFG1]		=	0x43;
	pExtra->regs[REG3543_PD_CFG2]		=	0x01;
	pExtra->regs[REG3543_FILT_CF]		=	0x25;
	pExtra->regs[REG3543_ROM_ADDR]		=	0x00;
	pExtra->regs[REG3543_IRHR]			=	0x80;
	pExtra->regs[REG3543_BIAS_ADJ]		=	0x57;
	pExtra->regs[REG3543_TEST1]			=	0x40;
	pExtra->regs[REG3543_ROM_WRITE]		=	0x00;

	

   /* Write each register out to the MAX3543: */
	for (RegNumber=0;RegNumber<=MAX3543_NUMREGS;RegNumber++)
	{
//      Max3543_Write(RegNumber, regs[RegNumber]);
	  if(Max3543_Write(pTuner, RegNumber, pExtra->regs[RegNumber]) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;
	}

	/* First read calibration constants from MAX3543 and store in global
      variables for use when setting RF tracking filter */   
//	MAX3543_ReadROM();
	if(MAX3543_ReadROM(pTuner) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;

   /* Define the IF frequency used.
		If using non-floating point math, enter the IF Frequency multiplied 
	   by the factor LOSCALE here as an integer.
		i.e. IF_Frequency = 1445;
		If using floating point math, use the scalefrq macro:
		i.e. IF_Frequency = scalefrq(36.125); 
	*/
//	IF_Frequency = scalefrq(36.125); 


	// Calculate frequency unit.
	FreqUnit = MAX3543_CONST_1_MHZ / pExtra->LOSCALE;

	// Set tuner RF frequency in KHz.
	// Note: 1. CalculatedValue = round(RfFreqHz / FreqUnit)
	//       2. Call MAX3543 source code function.
	CalculatedValue = (unsigned short)((pExtra->IfFreqHz + (FreqUnit / 2)) / FreqUnit);
	pExtra->IF_Frequency = CalculatedValue;


	/* Calculate the denominator just once since it is dependant on the xtal freq only.
	   The denominator is used to calculate the N+FractionalN ratio.  
	*/
	pExtra->denominator = pExtra->XTALREF * 4 * pExtra->LOSCALE;   

   /* The fracscale is used to calculate the fractional remainder of the N+FractionalN ratio.  */
	pExtra->fracscale = 2147483648U/pExtra->denominator;


//   Note: Set standard mode, channel bandwith, and RF frequency in other functions.

//   MAX3543_Standard(DVB_T, IFOUT1_DIFF_DTVOUT);

//   MAX3543_ChannelBW(BW8MHZ);
   // Note: Set bandwidth with 8 MHz for QAM.
   MAX3543_ChannelBW(pTuner, MAX3543_BANDWIDTH_MODE_DEFAULT);

//   MAX3543_SetFrequency(RfFreq);	


	return MAX3543_SUCCESS;


error_status_access_tuner:
	return MAX3543_ERROR;
}

/* Set the channel bandwidth.  Call with arguments: BW7MHZ or BW8MHZ */
int MAX3543_ChannelBW(TUNER_MODULE *pTuner, UINT_16 bw)
{
	MAX3543_EXTRA_MODULE *pExtra;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);


   pExtra->regs[REG3543_MODE] = (pExtra->regs[REG3543_MODE] & 0xef) | (bw<<4);
//	Max3543_Write(REG3543_MODE, regs[REG3543_MODE]);
	if(Max3543_Write(pTuner, REG3543_MODE, pExtra->regs[REG3543_MODE]) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;


	return MAX3543_SUCCESS;


error_status_access_tuner:
	return MAX3543_ERROR;
}

/* 
	Set the broadcast standared and RF signal path.
	This routine must be called prior to tuning (Set_Frequency() ) 
	such as in MAX3543_Init() or when necessary to change modes.

	This sub routine has 2 input/function
	1. bcstd:it set MAX3543 to optimized power detector/bias setting for each standard (dvb-t,pal?, currently it has 4 choice:
      1.1 bcstd=DVBT, optimized for DVB-T
      1.2 bcstd=DVBC, optimized for DVB-C
      1.3 bcstd=ATV1, optimized for PAL/SECAM - B/G/D/K/I
      1.4 bcstd=ATV2, optimized for SECAM-L
	2. outputmode: this setting has to match you hardware signal path, it has 3 choice:
      2.1 outputmode=IFOUT1_DIFF_IFOUT_DIFF
            signal path: IFOUT1 (pin6/7) driving a diff input IF filter (ie LC filter or 6966 SAW), 
            then go back to IFVGA input (pin 10/11) and IF output of MAX3543 is pin 15/16. 
            this is common seting for all DTV_only demod and hybrid demod
      2.2 outputmode=IFOUT1_SE_IFOUT_DIFF
            signal path: IFOUT1 (pin6) driving a single-ended input IF filter (ie 7251 SAW)
            then go back to IFVGA input (pin 10/11) and IF output of MAX3543 is pin 15/16. 
            this is common seting for all DTV_only demod and hybrid demod
      2.3 outputmode=IFOUT2
            signal path: IFOUT2 (pin14) is MAX3543 IF output, normally it drives a ATV demod. 
            The IFVGA is shutoff
            this is common setting for separate ATV demod app
*/

int MAX3543_Standard(TUNER_MODULE *pTuner, standard bcstd, outputmode outmd)
{
	char IFSEL;
	char LNA2G;   
	char SDIVG;
	char WPDA;
	char NPDA; 
	char RFIFD; 
	char MIXGM;
	char LNA2B;
	char MIXB;

   
	MAX3543_EXTRA_MODULE *pExtra;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);


	pExtra->broadcast_standard = bcstd;   /* used later in tuning */


	switch ( bcstd )
      {
         case DVB_T:
				LNA2G = 1;
				WPDA = 4;
				NPDA = 3;
				RFIFD = 1;
				MIXGM = 1;
				LNA2B = 1;
				MIXB = 1;
            break;
			case DVB_C:
				LNA2G = 1;
				WPDA = 3;
				NPDA = 3;
				RFIFD = 1;
				MIXGM = 1;
				LNA2B = 1;
				MIXB = 1;
            break;
			case ATV:
				LNA2G = 0;
				WPDA = 3;
				NPDA = 5;
				RFIFD = 2;
				MIXGM = 0;
				LNA2B = 3;
				MIXB = 3;
            break;
			case ATV_SECAM_L:
				LNA2G = 0;
				WPDA = 3;
				NPDA = 3;
				RFIFD = 2;
				MIXGM = 0;
				LNA2B = 3;
				MIXB = 3;
            break;
			default:
				return MAX3543_ERROR;
		}

	   /* the outmd must be set after the standard mode bits are set. 
		   Please do not change order.  */
		switch ( outmd )
      {
			case IFOUT1_DIFF_DTVOUT:
				IFSEL = 0;
				SDIVG = 0;
				break;
			case IFOUT1_SE_DTVOUT:
				IFSEL = 1;
				SDIVG = 0;
				break;
			case IFOUT2:
				IFSEL = 2;
				SDIVG = 1;
				NPDA = 3;
				LNA2G = 1;   /* overrites value chosen above for this case */
				RFIFD = 3;   /* overrites value chosen above for this case */
				break;
			default:
				return MAX3543_ERROR;
		}	
	

	/* Mask in each set of bits into the register variables */
   pExtra->regs[REG3543_MODE] = (pExtra->regs[REG3543_MODE] & 0x7c) | IFSEL | (LNA2G<<7);
   pExtra->regs[REG3543_SHDN] = (pExtra->regs[REG3543_SHDN] & 0xf7) | (SDIVG<<3);
   pExtra->regs[REG3543_PD_CFG1] = (pExtra->regs[REG3543_PD_CFG1] & 0x88) | (WPDA<<4) | NPDA;
   pExtra->regs[REG3543_PD_CFG2] = (pExtra->regs[REG3543_PD_CFG2] & 0xfc) | RFIFD;
   pExtra->regs[REG3543_BIAS_ADJ] = (pExtra->regs[REG3543_BIAS_ADJ] & 0x13) | (MIXGM<<6) | (LNA2B<<4) | (MIXB<<2);

	/* Send each register variable: */
//   Max3543_Write(REG3543_MODE, regs[REG3543_MODE]);
   if(Max3543_Write(pTuner, REG3543_MODE, pExtra->regs[REG3543_MODE]) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;

//   Max3543_Write(REG3543_SHDN, regs[REG3543_SHDN]);
   if(Max3543_Write(pTuner, REG3543_SHDN, pExtra->regs[REG3543_SHDN]) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;

//   Max3543_Write(REG3543_PD_CFG1, regs[REG3543_PD_CFG1]);
   if(Max3543_Write(pTuner, REG3543_PD_CFG1, pExtra->regs[REG3543_PD_CFG1]) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;

//   Max3543_Write(REG3543_PD_CFG2, regs[REG3543_PD_CFG2]);
   if(Max3543_Write(pTuner, REG3543_PD_CFG2, pExtra->regs[REG3543_PD_CFG2]) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;

//   Max3543_Write(REG3543_BIAS_ADJ, regs[REG3543_BIAS_ADJ]);
   if(Max3543_Write(pTuner, REG3543_BIAS_ADJ, pExtra->regs[REG3543_BIAS_ADJ]) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;


	return MAX3543_SUCCESS;


error_status_access_tuner:
	return MAX3543_ERROR;
}


/*This will set the LO Frequency and all other tuning related register bits.

  NOTE!!!!  The frequency passed to this routine must be scaled by th factor 
				LOSCALE.  For example if the frequency was 105.25 MHz you multiply
				this by LOSCALE which results in a whole number frequency.
				For example if LOSCALE = 20, then 105.25 * 20 = 2105.
				You would then call: MAX3543_SetFrequency(2105);
*/
int MAX3543_SetFrequency(TUNER_MODULE *pTuner, UINT_16 RF_Frequency)		
{
	UINT_16 RDiv, NewR, NDiv, Vdiv;
	UINT_32 Num;
	UINT_16 LO_Frequency;
	UINT_16 Rem;
   UINT_32 FracN;


	MAX3543_EXTRA_MODULE *pExtra;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);


   LO_Frequency = RF_Frequency + pExtra->IF_Frequency ;
   
	/* Determine VCO Divider */
	if (LO_Frequency < scalefrq(138))  /* 138MHz scaled UHF band */
	{
		Vdiv = 3;  /*  divide by 32   */
	}
	else if ( LO_Frequency < scalefrq(275))                   /* VHF Band */
	{
		Vdiv = 2;  /*  divide by 16   */
	}
	else if (LO_Frequency < scalefrq(550))
	{
		Vdiv = 1;  /*  divide by 8   */
	}
	else
	{	
		Vdiv = 0;  /*  divide by 4   */
	}
   

	/* calculate the r-divider from the RDIV bits: 
	 RDIV bits   RDIV
		00				1
		01				2
		10				4
		11				8
	*/
	RDiv = 1<<((pExtra->regs[REG3543_FRAC2] & 0x30) >> 4);  
   
	/* Set the R-Divider based on the frequency if in DVB mode.
      Otherwise set the R-Divider to 2.
	   Only send RDivider if it needs to change from the current state. 
	*/
	NewR = 0;
	if ((pExtra->broadcast_standard == DVB_T || pExtra->broadcast_standard == DVB_C) )
	{
		if ((LO_Frequency <= scalefrq(275)) && (RDiv == 1))
			NewR = 2;
		else if ((LO_Frequency > scalefrq(275)) && (RDiv == 2))
			NewR = 1;
	}
	else if (RDiv == 1) 
		NewR = 2;

	if (NewR != 0){
		RDiv = NewR;
		pExtra->regs[REG3543_FRAC2] = (pExtra->regs[REG3543_FRAC2] & 0xcf) | ((NewR-1) <<4);  
//		Max3543_Write(REG3543_FRAC2, regs[REG3543_FRAC2]);
		if(Max3543_Write(pTuner, REG3543_FRAC2, pExtra->regs[REG3543_FRAC2]) != MAX3543_SUCCESS)
			goto error_status_access_tuner;
	}

	/* Update the VDIV bits in the VCO variable.
	   we will write this variable out to the VCO register during the MAX3543_SeedVCO routine.
		We can write over all the other bits (D<7:2>) in that register variable because they
		will be filled in: MAX3543_SeedVCO later.
	*/
	pExtra->regs[REG3543_VCO] = Vdiv;

	/* now convert the Vdiv bits into the multiplier for use in the equation:
		Vdiv   Mult
			0      4
			1      8
			2      16
			3      32
	*/
	Vdiv = 1<<(Vdiv+2);


	//#ifdef HAVE_32BIT_MATH

	/* Calculate Numerator and Denominator for N+FN calculation  */
	Num = LO_Frequency * RDiv * Vdiv * pExtra->XTALSCALE;


	NDiv = (UINT_16) (Num/(UINT_32)pExtra->denominator);   /* Note: this is an integer division, returns 16 bit value. */

//   Max3543_Write(REG3543_NDIV,NDiv); 
   if(Max3543_Write(pTuner, REG3543_NDIV,NDiv) != MAX3543_SUCCESS)
		  goto error_status_access_tuner; 

	/* Calculate whole number remainder from division of Num by denom: 
	   Returns 16 bit value.  */
	Rem = (UINT_16)(Num - (UINT_32) NDiv * (UINT_32) pExtra->denominator); 

	/* FracN = Rem * 2^20/Denom, Scale 2^20/Denom 2048 X larger for more accuracy. */   
   /* fracscale = 2^31/denom.  2048 = 2^31/2^20  */
   FracN =(Rem*pExtra->fracscale)/2048;  



	/* Optional - Seed the VCO to cause it to tune faster.  
		(LO_Frequency/LOSCALE) * Vdiv = the unscaled VCO Frequency.
		It is unscaled to prevent overflow when it is multiplied by vdiv.
	*/
//   MAX3543_SeedVCO((LO_Frequency/LOSCALE) * Vdiv);
   if(MAX3543_SeedVCO(pTuner, (LO_Frequency/pExtra->LOSCALE) * Vdiv) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;



	pExtra->regs[REG3543_FRAC2] = (pExtra->regs[REG3543_FRAC2] & 0xf0) | ((UINT_16)(FracN >> 16) & 0xf);
//   Max3543_Write(REG3543_FRAC2, regs[REG3543_FRAC2]); 
   if(Max3543_Write(pTuner, REG3543_FRAC2, pExtra->regs[REG3543_FRAC2]) != MAX3543_SUCCESS)
		  goto error_status_access_tuner; 

//   Max3543_Write(REG3543_FRAC1,(UINT_16)(FracN >> 8) & 0xff); 
   if(Max3543_Write(pTuner, REG3543_FRAC1,(UINT_16)(FracN >> 8) & 0xff) != MAX3543_SUCCESS)
		  goto error_status_access_tuner; 

//   Max3543_Write(REG3543_FRAC0, (UINT_16) FracN & 0xff); 
   if(Max3543_Write(pTuner, REG3543_FRAC0, (UINT_16) FracN & 0xff) != MAX3543_SUCCESS)
		  goto error_status_access_tuner; 

   /* Program tracking filters and other frequency dependent registers */
//   MAX3543_SetTrackingFilter(RF_Frequency);
   if(MAX3543_SetTrackingFilter(pTuner, RF_Frequency) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;


	return MAX3543_SUCCESS;


error_status_access_tuner:
	return MAX3543_ERROR;
}



/*As you tune in frequency, the tracking filter needs
to be set as a function of frequency.  Stored in the ROM on the
IC are two end points for the VHFL, VHFH, and UHF frequency bands. 
This routine performs the necessary function to calculate the
needed series and parallel capacitor values from these two end
points for the current frequency.  Once the value is calculated, 
it is loaded into the Tracking Filter Caps register and the 
internal capacitors are switched in tuning the tracking 
filter.
*/
int MAX3543_SetTrackingFilter(TUNER_MODULE *pTuner, UINT_16 RF_Frequency)
{
	/*  Calculate the series and parallel capacitor values for the given frequency  */
	/*  band.  These values are then written to the registers.  This causes the     */
	/*  MAX3543's internal series and parallel capacitors to change thus tuning the */
	/*  tracking filter to the proper frequency.                                    */
   
   UINT_16 TFB, tfs, tfp, RFin, HRF;
  
	MAX3543_EXTRA_MODULE *pExtra;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);

	
   /*  Set the TFB Bits (Tracking Filter Band) for the given frequency. */
	if (RF_Frequency < scalefrq(196))  /* VHF Low Band */
   {
        TFB = VHF_L;  
   }
   else if (RF_Frequency < scalefrq(440)) /* VHF High  196-440 MHz */
   {
        TFB = VHF_H;
   }
   else{    /* UHF */
        TFB = UHF;
	}

   /*  Set the RFIN bit.  RFIN selects a input low pass filter */
	if (RF_Frequency < scalefrq(345)){  /* 345 MHz is the change over point. */
		RFin = 0;
	}
	else{
      RFin = 1;
	}

	if (RF_Frequency < scalefrq(110)){  /* 110 MHz is the change over point. */
		HRF = 1;
	}
	else{
      HRF = 0;
	}

	/* Write the TFB<1:0> Bits and the RFIN bit into the IFOVLD register */
	/* TFB sets the tracking filter band in the chip, RFIN selects the RF input */
   pExtra->regs[REG3543_MODE] = (pExtra->regs[REG3543_MODE] & 0x93 ) | (TFB << 2) | (RFin << 6) | (HRF<<5); 
//   Max3543_Write(REG3543_MODE,(regs[REG3543_MODE])) ;
   if(Max3543_Write(pTuner, REG3543_MODE,(pExtra->regs[REG3543_MODE])) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;

   tfs = tfs_i(pExtra->TFRomCoefs[TFB][SER0], pExtra->TFRomCoefs[TFB][SER1],  RF_Frequency/pExtra->LOSCALE, co[TFB*2]);
   tfp = tfs_i(pExtra->TFRomCoefs[TFB][PAR0], pExtra->TFRomCoefs[TFB][PAR1],  RF_Frequency/pExtra->LOSCALE, co[(TFB*2)+1]);

	/* Write the TFS Bits into the Tracking Filter Series Capacitor register */
	if (tfs > 255)   /* 255 = 8 bits of TFS */
		tfs = 255;
	if (tfs < 0)
		tfs = 0;
   pExtra->regs[REG3543_TFS] = tfs;

	/* Write the TFP Bits into the Tracking Filter Parallel Capacitor register */
	if (tfp > 63)   /* 63 = 6 bits of TFP  */
		tfp = 63;
	if (tfp < 0)
		tfp = 0;
   pExtra->regs[REG3543_TFP] = (pExtra->regs[REG3543_TFP] & 0xc0 ) | tfp;

	   /*  Send registers that have been changed */
   /*  Maxim evkit I2c communication... Replace by microprocessor specific code */
//   Max3543_Write(REG3543_TFS,(regs[REG3543_TFS])) ;
   if(Max3543_Write(pTuner, REG3543_TFS,(pExtra->regs[REG3543_TFS])) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;

//   Max3543_Write(REG3543_TFP,(regs[REG3543_TFP])) ;
   if(Max3543_Write(pTuner, REG3543_TFP,(pExtra->regs[REG3543_TFP])) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;


	return MAX3543_SUCCESS;


error_status_access_tuner:
	return MAX3543_ERROR;
}





/* calculate aproximation for Max3540 tracking filter useing integer math only */

UINT_16 tfs_i(UINT_16 S0, UINT_16 S1, UINT_16 FreqRF, const UINT_16 c[5])
{  UINT_16 i,y,y1,y2,y3,y4,y5,y6,y7,add;
   UINT_32 res;

/* y=4*((64*c[0])+c[1]*S0)-((64*c[2]-(S1*c[3]))*(FreqRF))/250;   */
   y1=64*c[0];
   y2=c[1]*S0;
   y3=4*(y1+y2);
   y4=S1*c[3];
   y5=64*c[2];
   y6=y5-y4;
   y7=(y6*(FreqRF))/250;
   if (y7<y3)
   { y= y3-y7;
                    /* above sequence has been choosen to avoid overflows */
     y=(10*y)/111;                /* approximation for nom*10*LN(10)/256 */
     add=y; res=100+y;
     for (i=2; i<12; i++)  
     { 
       add=(add*y)/(i*100);       /* this only works with 32bit math */
       res+=add;
     }
   }
   else 
     res=0;
   if (((UINT_32)res+50*1)>((UINT_32)100*c[4])) res=(res+50*1)/100-c[4];
   else res=0;

   if (res<255) return (UINT_16) res; else return 255;
}

/*
   As soon as you program the Frac0 register, a state machine is started to find the
	correct VCO for the N and Fractional-N values entered.
	If the VASS bit is set, the search routine will start from the band and	
	sub-band that is currently programmed into the VCO register (VCO and VSUB bits = seed). 
	If you seed the register with	bands close to where the auto routine will 
	finally select, the search routine will finish much faster.
	This routine determines the best seed to use for the VCO and VSUB values.
	If VASS is cleared, the search will start as the lowest VCO and search up.
	This takes much longer.  Make sure VASS is set before using this routine.
	For the seed value to be read in the VCO register, it must be there prior to
	setting the Frac0 register.  So call this just before setting the Fractional-N
	registers.  The VASS bit is bit 5 of register 10 (or REG3543_VAS).
*/
int MAX3543_SeedVCO(TUNER_MODULE *pTuner, UINT_16 Fvco){
   /* Fvco is the VCO frequency in MHz and is not scaled by LOSCALE  */
	UINT_16 VCO;
	UINT_16 VSUB;

	MAX3543_EXTRA_MODULE *pExtra;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);


	if (Fvco  <= 2750)
	{
		/* The VCO seed: */

		VCO = 0;
		/* Determine the VCO sub-band (VSUB) seed:  */
	   if (Fvco  < 2068)
         VSUB = 0;
		else if (Fvco  < 2101)
         VSUB = 1;
		else if (Fvco  < 2137)
         VSUB = 2;
		else if (Fvco  < 2174)
         VSUB = 3;
		else if (Fvco  < 2215)
         VSUB = 4;
		else if (Fvco  < 2256)
         VSUB = 5;
		else if (Fvco  < 2300)
         VSUB = 6;
		else if (Fvco  < 2347)
         VSUB = 7;
		else if (Fvco  < 2400)
         VSUB = 8;
		else if (Fvco  < 2453)
         VSUB = 9;
		else if (Fvco  < 2510)
         VSUB = 10;
		else if (Fvco  < 2571)
         VSUB = 11;
		else if (Fvco  < 2639)
         VSUB = 12;
		else if (Fvco  < 2709)
         VSUB = 13;
		else if (Fvco  < 2787)
         VSUB = 14;
	}
	else if (Fvco  <= 3650)
	{
		/* The VCO seed: */
		VCO = 1;
		/* Determine the VCO sub-band (VSUB) seed:  */
	   if (Fvco  <= 2792)
         VSUB = 1;
		else if (Fvco  <= 2839)
         VSUB = 2;
		else if (Fvco  <= 2890)
         VSUB = 3;
		else if (Fvco  <= 2944)
         VSUB = 4;
		else if (Fvco  <= 3000)
         VSUB = 5;
		else if (Fvco  <= 3059)
         VSUB = 6;
		else if (Fvco  <= 3122)
         VSUB = 7;
		else if (Fvco  <= 3194)
         VSUB = 8;
		else if (Fvco  <= 3266)
         VSUB = 9;
		else if (Fvco  <= 3342)
         VSUB = 10;
		else if (Fvco  <= 3425)
         VSUB = 11;
		else if (Fvco  <= 3516)
         VSUB = 12;
		else if (Fvco  <= 3612)
         VSUB = 13;
		else if (Fvco  <= 3715)
         VSUB = 14;
	}
	else
	{
		/* The VCO seed: */
		VCO = 2;
		/* Determine the VCO sub-band (VSUB) seed:  */
	   if (Fvco  <= 3658)
         VSUB = 0;
		else if (Fvco  <= 3716)
         VSUB = 2;
		else if (Fvco  <= 3776)
         VSUB = 2;
		else if (Fvco  <= 3840)
         VSUB = 3;
		else if (Fvco  <= 3909)
         VSUB = 4;
		else if (Fvco  <= 3980)
         VSUB = 5;
		else if (Fvco  <= 4054)
         VSUB = 6;
		else if (Fvco  <= 4134)
         VSUB = 7;
		else if (Fvco  <= 4226)
         VSUB = 8;
		else if (Fvco  <= 4318)
         VSUB = 9;
		else if (Fvco  <= 4416)
         VSUB = 10;
		else if (Fvco  <= 4520)
         VSUB = 11;
		else if (Fvco  <= 4633)
         VSUB = 12;
		else if (Fvco  <= 4751)
         VSUB = 13;
		else if (Fvco  <= 4876)
         VSUB = 14;
		else 
         VSUB = 15;
	}
	/* VCO = D<7:6>, VSUB = D<5:2> */
	pExtra->regs[REG3543_VCO] = (pExtra->regs[REG3543_VCO] & 3) | (VSUB<<2) | (VCO<<6);
	/* Program the VCO register with the seed: */
//	Max3543_Write(REG3543_VCO, regs[REG3543_VCO]);
	if(Max3543_Write(pTuner, REG3543_VCO, pExtra->regs[REG3543_VCO]) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;


	return MAX3543_SUCCESS;


error_status_access_tuner:
	return MAX3543_ERROR;
}



/* Returns the lock detect status.  This is accomplished by 
   examining the ADC value read from the MAX3543.  The ADC
	value is the tune voltage digitized.  If it is too close to
	ground or Vcc, the part is unlocked.  The ADC ranges from 0-7.
	Values 1 to 6 are considered locked.  0 or 7 is unlocked.
	Returns True for locked, fase for unlocked.
*/
int MAX3543_LockDetect(TUNER_MODULE *pTuner, int *pAnswer)
{
//   BOOL vcoselected;
   int vcoselected;
	UINT_16 tries = 65535;
	char adc;
	unsigned short Data;

	MAX3543_EXTRA_MODULE *pExtra;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);


	/* The ADC will not be stable until the VCO is selected.
	   usually the selection process will take 25ms or less but
		it could theoretically take 100ms.  Set tries to some number
		of your processor clocks corresponding to 100ms.
		You have to take into account all instructions processed
		in determining this time.  I am picking a value out of the air
		for now.
	*/
	vcoselected = MAX_FALSE;
	while ((--tries > 0) && (vcoselected == MAX_FALSE))
	{
//		if ((Max3543_Read(REG3543_VAS_STATUS) & 1) == 1)
		if(Max3543_Read(pTuner, REG3543_VAS_STATUS, &Data) != MAX3543_SUCCESS)
			goto error_status_access_tuner;
		if ((Data & 1) == 1)
			vcoselected = MAX_TRUE;
	}
	/* open the ADC latch:  ADL=0, ADE=1  */
	pExtra->regs[REG3543_VAS] = (pExtra->regs[REG3543_VAS] & 0xf3) | 4;  
//	Max3543_Write(REG3543_VAS, regs[REG3543_VAS]); 
	if(Max3543_Write(pTuner, REG3543_VAS, pExtra->regs[REG3543_VAS]) != MAX3543_SUCCESS)
		  goto error_status_access_tuner; 

	/* ADC = 3 LSBs of Gen Status Register:  */
//	adc = Max3543_Read(REG3543_GEN_STATUS) & 0x7;
	if(Max3543_Read(pTuner, REG3543_GEN_STATUS, &Data) != MAX3543_SUCCESS)
		  goto error_status_access_tuner;
	adc = Data & 0x7;




	/* locked if ADC within range of 1-6: */
	if ((adc<1 ) || (adc>6))
		return MAX_FALSE;
	else
		return MAX_TRUE;


	return MAX3543_SUCCESS;


error_status_access_tuner:
	return MAX3543_ERROR;
}



int MAX3543_ReadROM(TUNER_MODULE *pTuner)
{
	unsigned short Data;

   /* Read the ROM table, extract tracking filter ROM coefficients,
		IRHR and CFSET constants.  
		This is to be called after the Max3543 powers up.              
	*/
	UINT_16 rom_data[12];
	UINT_16 i;

	MAX3543_EXTRA_MODULE *pExtra;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Max3543);


	for (i=0; i <= 10; i++)     
	{
//		Max3543_Write(REG3543_ROM_ADDR,i);   /*Select ROM Row by setting address register */
		if(Max3543_Write(pTuner, REG3543_ROM_ADDR,i) != MAX3543_SUCCESS)
			goto error_status_access_tuner;   /*Select ROM Row by setting address register */

//		rom_data[i] = Max3543_Read(REG3543_ROM_READ);  /* Read from ROMR Register */
		if(Max3543_Read(pTuner, REG3543_ROM_READ, &Data) != MAX3543_SUCCESS)
			goto error_status_access_tuner;
		rom_data[i] = Data;  /* Read from ROMR Register */
	}


	/* The ROM address must be returned to 0 for normal operation or the part will not be biased properly. */
//	Max3543_Write(REG3543_ROM_ADDR,0);   
	if(Max3543_Write(pTuner, REG3543_ROM_ADDR,0) != MAX3543_SUCCESS)
			goto error_status_access_tuner;   

	/* Copy all of ROM Row 10 to Filt_CF register. */
//	Max3543_Write(REG3543_FILT_CF,rom_data[10]);   
	if(Max3543_Write(pTuner, REG3543_FILT_CF,rom_data[10]) != MAX3543_SUCCESS)
			goto error_status_access_tuner;   

	/* Copy the IRHR ROM value to the IRHR register: */
//	Max3543_Write(REG3543_IRHR, rom_data[0xb]);
	if(Max3543_Write(pTuner, REG3543_IRHR, rom_data[0xb]) != MAX3543_SUCCESS)
			goto error_status_access_tuner;


	/* assemble the broken up word pairs from the ROM table  into complete ROM coefficients:  */
	pExtra->TFRomCoefs[VHF_L][SER0] = (rom_data[1] & 0xFC) >> 2;  /*'LS0 )*/
	pExtra->TFRomCoefs[VHF_L][SER1] = ((rom_data[1] & 0x3 ) << 4) + ((rom_data[2] & 0xf0) >> 4);  /* 'LS1*/
	pExtra->TFRomCoefs[VHF_L][PAR0] = ((rom_data[2] & 0xf) << 2) + ((rom_data[3] & 0xc0) >> 6);  /*'LP0*/
	pExtra->TFRomCoefs[VHF_L][PAR1] = rom_data[3] & 0x3f;  /*LP1 */

	pExtra->TFRomCoefs[VHF_H][SER0] = ((rom_data[4] & 0xfc) >> 2);  /*'HS0 */
	pExtra->TFRomCoefs[VHF_H][SER1] = ((rom_data[4] & 0x3) << 4) + ((rom_data[5] & 0xF0) >> 4);  /*'HS1 */
	pExtra->TFRomCoefs[VHF_H][PAR0] = ((rom_data[5] & 0xf) << 2) + ((rom_data[6] & 0xc0) >> 6);  /*'HP0 */
	pExtra->TFRomCoefs[VHF_H][PAR1] = rom_data[6] & 0x3F;  /*'HP1 */

	pExtra->TFRomCoefs[UHF][SER0] =  ((rom_data[7]  & 0xFC) >> 2);  /*'US0 */
	pExtra->TFRomCoefs[UHF][SER1] = ((rom_data[7] & 0x3) << 4) + ((rom_data[8] & 0xf0) >> 4 );  /*'US1 */
	pExtra->TFRomCoefs[UHF][PAR0] = ((rom_data[8] & 0xF) << 2) + ((rom_data[9] & 0xc0) >> 6);  /*'UP0 */
	pExtra->TFRomCoefs[UHF][PAR1] = rom_data[9] & 0x3f;  /*'UP1 */


	return MAX3543_SUCCESS;


error_status_access_tuner:
	return MAX3543_ERROR;
 }










