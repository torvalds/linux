#ifndef __TUNER_MXL5007T_H
#define __TUNER_MXL5007T_H

/**

@file

@brief   MxL5007T tuner module declaration

One can manipulate MxL5007T tuner through MxL5007T module.
MxL5007T module is derived from tuner module.



@par Example:
@code

// The example is the same as the tuner example in tuner_base.h except the listed lines.



#include "tuner_mxl5007t.h"


...



int main(void)
{
	TUNER_MODULE        *pTuner;
	MXL5007T_EXTRA_MODULE *pTunerExtra;

	TUNER_MODULE          TunerModuleMemory;
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;
	I2C_BRIDGE_MODULE     I2cBridgeModuleMemory;

	unsigned long BandwidthMode;


	...



	// Build MxL5007T tuner module.
	BuildMxl5007tModule(
		&pTuner,
		&TunerModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0xc0,								// I2C device address is 0xc0 in 8-bit format.
		CRYSTAL_FREQ_16000000HZ,			// Crystal frequency is 16.0 MHz.
		MXL5007T_STANDARD_DVBT,				// The MxL5007T standard mode is DVB-T.
		IF_FREQ_4570000HZ,					// The MxL5007T IF frequency is 4.57 MHz.
		SPECTRUM_NORMAL,					// The MxL5007T spectrum mode is normal.
		MXL5007T_LOOP_THROUGH_DISABLE,		// The MxL5007T loop-through mode is disabled.
		MXL5007T_CLK_OUT_DISABLE,			// The MxL5007T clock output mode is disabled.
		MXL5007T_CLK_OUT_AMP_0,				// The MxL5007T clock output amplitude is 0.
		0									// The MxL5007T QAM IF differential output level is 0 for cable only.
		);





	// Get MxL5007T tuner extra module.
	pTunerExtra = (T2266_EXTRA_MODULE *)(pTuner->pExtra);





	// ==== Initialize tuner and set its parameters =====

	...

	// Set MxL5007T bandwidth.
	pTunerExtra->SetBandwidthMode(pTuner, MXL5007T_BANDWIDTH_6MHZ);





	// ==== Get tuner information =====

	...

	// Get MxL5007T bandwidth.
	pTunerExtra->GetBandwidthMode(pTuner, &BandwidthMode);



	// See the example for other tuner functions in tuner_base.h


	return 0;
}


@endcode

*/





#include "tuner_base.h"





// The following context is source code provided by MaxLinear.





// MaxLinear source code - MxL5007_Common.h


/*******************************************************************************
 *
 * FILE NAME          : MxL_Common.h
 * 
 * AUTHOR             : Kyle Huang
 * DATE CREATED       : May 05, 2008
 *
 * DESCRIPTION        : 
 *
 *******************************************************************************
 *                Copyright (c) 2006, MaxLinear, Inc.
 ******************************************************************************/
 
//#ifndef __MXL5007_COMMON_H
//#define __MXL5007_COMMON_H



/******************************************************************************
*						User-Defined Types (Typedefs)
******************************************************************************/


/****************************************************************************
*       Imports and definitions for WIN32                             
****************************************************************************/
//#include <windows.h>

#ifndef _UINT_X_
#define _UINT_X_ 1
typedef unsigned char  UINT8;
typedef unsigned short UINT16;
typedef unsigned int   UINT32;
#endif
typedef char           SINT8;
typedef short          SINT16;
typedef int            SINT32;
//typedef float          REAL32;

// Additional definition
#define BOOL		int
#define MxL_FALSE	0
#define MxL_TRUE	1

/****************************************************************************\
*      Imports and definitions for non WIN32 platforms                   *
\****************************************************************************/
/*
typedef unsigned char  UINT8;
typedef unsigned short UINT16;
typedef unsigned int   UINT32;
typedef char           SINT8;
typedef short          SINT16;
typedef int            SINT32;
typedef float          REAL32;

// create a boolean 
#ifndef __boolean__
#define __boolean__
typedef enum {FALSE=0,TRUE} BOOL;
#endif //boolean
*/


/****************************************************************************\
*          Definitions for all platforms					                 *
\****************************************************************************/
#ifndef NULL
#define NULL (void*)0
#endif



/******************************/
/*	MxL5007 Err message  	  */
/******************************/
typedef enum{
	MxL_OK				=   0,
	MxL_ERR_INIT		=   1,
	MxL_ERR_RFTUNE		=   2,
	MxL_ERR_SET_REG		=   3,
	MxL_ERR_GET_REG		=	4,
	MxL_ERR_OTHERS		=   10
}MxL_ERR_MSG;

/******************************/
/*	MxL5007 Chip verstion     */
/******************************/
typedef enum{
	MxL_UNKNOWN_ID		= 0x00,
	MxL_5007T_V4		= 0x14,
	MxL_GET_ID_FAIL		= 0xFF
}MxL5007_ChipVersion;


/******************************************************************************
    CONSTANTS
******************************************************************************/

#ifndef MHz
	#define MHz 1000000
#endif

#define MAX_ARRAY_SIZE 100


// Enumeration of Mode
// Enumeration of Mode
typedef enum 
{
	MxL_MODE_ISDBT = 0,
	MxL_MODE_DVBT = 1,
	MxL_MODE_ATSC = 2,	
	MxL_MODE_CABLE = 0x10
} MxL5007_Mode ;

typedef enum
{
	MxL_IF_4_MHZ	  = 4000000,
	MxL_IF_4_5_MHZ	  = 4500000,
	MxL_IF_4_57_MHZ	  =	4570000,
	MxL_IF_5_MHZ	  =	5000000,
	MxL_IF_5_38_MHZ	  =	5380000,
	MxL_IF_6_MHZ	  =	6000000,
	MxL_IF_6_28_MHZ	  =	6280000,
	MxL_IF_9_1915_MHZ =	9191500,
	MxL_IF_35_25_MHZ  = 35250000,
	MxL_IF_36_15_MHZ  = 36150000,
	MxL_IF_44_MHZ	  = 44000000
} MxL5007_IF_Freq ;

typedef enum
{
	MxL_XTAL_16_MHZ		= 16000000,
	MxL_XTAL_20_MHZ		= 20000000,
	MxL_XTAL_20_25_MHZ	= 20250000,
	MxL_XTAL_20_48_MHZ	= 20480000,
	MxL_XTAL_24_MHZ		= 24000000,
	MxL_XTAL_25_MHZ		= 25000000,
	MxL_XTAL_25_14_MHZ	= 25140000,
	MxL_XTAL_27_MHZ		= 27000000,
	MxL_XTAL_28_8_MHZ	= 28800000,
	MxL_XTAL_32_MHZ		= 32000000,
	MxL_XTAL_40_MHZ		= 40000000,
	MxL_XTAL_44_MHZ		= 44000000,
	MxL_XTAL_48_MHZ		= 48000000,
	MxL_XTAL_49_3811_MHZ = 49381100	
} MxL5007_Xtal_Freq ;

typedef enum
{
	MxL_BW_6MHz = 6,
	MxL_BW_7MHz = 7,
	MxL_BW_8MHz = 8
} MxL5007_BW_MHz;

typedef enum
{
	MxL_NORMAL_IF = 0 ,
	MxL_INVERT_IF

} MxL5007_IF_Spectrum ;

typedef enum
{
	MxL_LT_DISABLE = 0 ,
	MxL_LT_ENABLE

} MxL5007_LoopThru ;

typedef enum
{
	MxL_CLKOUT_DISABLE = 0 ,
	MxL_CLKOUT_ENABLE

} MxL5007_ClkOut;

typedef enum
{
	MxL_CLKOUT_AMP_0 = 0 ,
	MxL_CLKOUT_AMP_1,
	MxL_CLKOUT_AMP_2,
	MxL_CLKOUT_AMP_3,
	MxL_CLKOUT_AMP_4,
	MxL_CLKOUT_AMP_5,
	MxL_CLKOUT_AMP_6,
	MxL_CLKOUT_AMP_7
} MxL5007_ClkOut_Amp;

typedef enum
{
	MxL_I2C_ADDR_96 = 96 ,
	MxL_I2C_ADDR_97 = 97 ,
	MxL_I2C_ADDR_98 = 98 ,
	MxL_I2C_ADDR_99 = 99 	
} MxL5007_I2CAddr ;
/*
//
// MxL5007 TunerConfig Struct
//
typedef struct _MxL5007_TunerConfigS
{
	MxL5007_I2CAddr		I2C_Addr;
	MxL5007_Mode		Mode;
	SINT32				IF_Diff_Out_Level;
	MxL5007_Xtal_Freq	Xtal_Freq;
	MxL5007_IF_Freq	    IF_Freq;
	MxL5007_IF_Spectrum IF_Spectrum;
	MxL5007_ClkOut		ClkOut_Setting;
    MxL5007_ClkOut_Amp	ClkOut_Amp;
	MxL5007_BW_MHz		BW_MHz;
	UINT32				RF_Freq_Hz;

	// Additional definition
	TUNER_MODULE *pTuner;

} MxL5007_TunerConfigS;
*/



//#endif /* __MXL5007_COMMON_H__*/























// MaxLinear source code - MxL5007.h



/*
 
 Driver APIs for MxL5007 Tuner
 
 Copyright, Maxlinear, Inc.
 All Rights Reserved
 
 File Name:      MxL5007.h

 */


//#include "MxL5007_Common.h"


typedef struct
{
	UINT8 Num;	//Register number
	UINT8 Val;	//Register value
} IRVType, *PIRVType;


UINT32 MxL5007_Init(UINT8* pArray,				// a array pointer that store the addr and data pairs for I2C write
					UINT32* Array_Size,			// a integer pointer that store the number of element in above array
					UINT8 Mode,				
					SINT32 IF_Diff_Out_Level,
					UINT32 Xtal_Freq_Hz,		
					UINT32 IF_Freq_Hz,		
					UINT8 Invert_IF,			
					UINT8 Clk_Out_Enable,    
					UINT8 Clk_Out_Amp		
					);
UINT32 MxL5007_RFTune(UINT8* pArray, UINT32* Array_Size, 
					 UINT32 RF_Freq,			// RF Frequency in Hz
					 UINT8 BWMHz		// Bandwidth in MHz
					 );
UINT32 SetIRVBit(PIRVType pIRV, UINT8 Num, UINT8 Mask, UINT8 Val);























// MaxLinear source code - MxL5007.h



/*
 
 Driver APIs for MxL5007 Tuner
 
 Copyright, Maxlinear, Inc.
 All Rights Reserved
 
 File Name:      MxL5007_API.h
 
 */
//#ifndef __MxL5007_API_H
//#define __MxL5007_API_H

//#include "MxL5007_Common.h"

/******************************************************************************
**
**  Name: MxL_Set_Register
**
**  Description:    Write one register to MxL5007
**
**  Parameters:    	myTuner				- Pointer to MxL5007_TunerConfigS
**					RegAddr				- Register address to be written
**					RegData				- Data to be written
**
**  Returns:        MxL_ERR_MSG			- MxL_OK if success	
**										- MxL_ERR_SET_REG if fail
**
******************************************************************************/
MxL_ERR_MSG MxL_Set_Register(MxL5007_TunerConfigS* myTuner, UINT8 RegAddr, UINT8 RegData);

/******************************************************************************
**
**  Name: MxL_Get_Register
**
**  Description:    Read one register from MxL5007
**
**  Parameters:    	myTuner				- Pointer to MxL5007_TunerConfigS
**					RegAddr				- Register address to be read
**					RegData				- Pointer to register read
**
**  Returns:        MxL_ERR_MSG			- MxL_OK if success	
**										- MxL_ERR_GET_REG if fail
**
******************************************************************************/
MxL_ERR_MSG MxL_Get_Register(MxL5007_TunerConfigS* myTuner, UINT8 RegAddr, UINT8 *RegData);

/******************************************************************************
**
**  Name: MxL_Tuner_Init
**
**  Description:    MxL5007 Initialization
**
**  Parameters:    	myTuner				- Pointer to MxL5007_TunerConfigS
**
**  Returns:        MxL_ERR_MSG			- MxL_OK if success	
**										- MxL_ERR_INIT if fail
**
******************************************************************************/
MxL_ERR_MSG MxL_Tuner_Init(MxL5007_TunerConfigS* );

/******************************************************************************
**
**  Name: MxL_Tuner_RFTune
**
**  Description:    Frequency tunning for channel
**
**  Parameters:    	myTuner				- Pointer to MxL5007_TunerConfigS
**					RF_Freq_Hz			- RF Frequency in Hz
**					BWMHz				- Bandwidth 6, 7 or 8 MHz
**
**  Returns:        MxL_ERR_MSG			- MxL_OK if success	
**										- MxL_ERR_RFTUNE if fail
**
******************************************************************************/
MxL_ERR_MSG MxL_Tuner_RFTune(MxL5007_TunerConfigS*, UINT32 RF_Freq_Hz, MxL5007_BW_MHz BWMHz);		

/******************************************************************************
**
**  Name: MxL_Soft_Reset
**
**  Description:    Software Reset the MxL5007 Tuner
**
**  Parameters:    	myTuner				- Pointer to MxL5007_TunerConfigS
**
**  Returns:        MxL_ERR_MSG			- MxL_OK if success	
**										- MxL_ERR_OTHERS if fail
**
******************************************************************************/
MxL_ERR_MSG MxL_Soft_Reset(MxL5007_TunerConfigS*);

/******************************************************************************
**
**  Name: MxL_Loop_Through_On
**
**  Description:    Turn On/Off on-chip Loop-through
**
**  Parameters:    	myTuner				- Pointer to MxL5007_TunerConfigS
**					isOn				- True to turn On Loop Through
**										- False to turn off Loop Through
**
**  Returns:        MxL_ERR_MSG			- MxL_OK if success	
**										- MxL_ERR_OTHERS if fail
**
******************************************************************************/
MxL_ERR_MSG MxL_Loop_Through_On(MxL5007_TunerConfigS*, MxL5007_LoopThru);

/******************************************************************************
**
**  Name: MxL_Standby
**
**  Description:    Enter Standby Mode
**
**  Parameters:    	myTuner				- Pointer to MxL5007_TunerConfigS
**
**  Returns:        MxL_ERR_MSG			- MxL_OK if success	
**										- MxL_ERR_OTHERS if fail
**
******************************************************************************/
MxL_ERR_MSG MxL_Stand_By(MxL5007_TunerConfigS*);

/******************************************************************************
**
**  Name: MxL_Wakeup
**
**  Description:    Wakeup from Standby Mode (Note: after wake up, please call RF_Tune again)
**
**  Parameters:    	myTuner				- Pointer to MxL5007_TunerConfigS
**
**  Returns:        MxL_ERR_MSG			- MxL_OK if success	
**										- MxL_ERR_OTHERS if fail
**
******************************************************************************/
MxL_ERR_MSG MxL_Wake_Up(MxL5007_TunerConfigS*);

/******************************************************************************
**
**  Name: MxL_Check_ChipVersion
**
**  Description:    Return the MxL5007 Chip ID
**
**  Parameters:    	myTuner				- Pointer to MxL5007_TunerConfigS
**			
**  Returns:        MxL_ChipVersion			
**
******************************************************************************/
MxL5007_ChipVersion MxL_Check_ChipVersion(MxL5007_TunerConfigS*);

/******************************************************************************
**
**  Name: MxL_RFSynth_Lock_Status
**
**  Description:    RF synthesizer lock status of MxL5007
**
**  Parameters:    	myTuner				- Pointer to MxL5007_TunerConfigS
**					isLock				- Pointer to Lock Status
**
**  Returns:        MxL_ERR_MSG			- MxL_OK if success	
**										- MxL_ERR_OTHERS if fail
**
******************************************************************************/
MxL_ERR_MSG MxL_RFSynth_Lock_Status(MxL5007_TunerConfigS* , BOOL* isLock);

/******************************************************************************
**
**  Name: MxL_REFSynth_Lock_Status
**
**  Description:    REF synthesizer lock status of MxL5007
**
**  Parameters:    	myTuner				- Pointer to MxL5007_TunerConfigS
**					isLock				- Pointer to Lock Status
**
**  Returns:        MxL_ERR_MSG			- MxL_OK if success	
**										- MxL_ERR_OTHERS if fail	
**
******************************************************************************/
MxL_ERR_MSG MxL_REFSynth_Lock_Status(MxL5007_TunerConfigS* , BOOL* isLock);

//#endif //__MxL5007_API_H























// The following context is MxL5007T tuner API source code





// Definitions

// Standard mode
enum MXL5007T_STANDARD_MODE
{
	MXL5007T_STANDARD_DVBT,
	MXL5007T_STANDARD_ATSC,
	MXL5007T_STANDARD_QAM,
	MXL5007T_STANDARD_ISDBT,
};


// Loop-through mode
enum MXL5007T_LOOP_THROUGH_MODE
{
	MXL5007T_LOOP_THROUGH_DISABLE = MxL_LT_DISABLE,
	MXL5007T_LOOP_THROUGH_ENABLE  = MxL_LT_ENABLE,
};


// Clock output mode
enum MXL5007T_CLK_OUT_MODE
{
	MXL5007T_CLK_OUT_DISABLE,
	MXL5007T_CLK_OUT_ENABLE,
};


// Clock output amplitude mode
enum MXL5007T_CLK_OUT_AMP_MODE
{
	MXL5007T_CLK_OUT_AMP_0,
	MXL5007T_CLK_OUT_AMP_1,
	MXL5007T_CLK_OUT_AMP_2,
	MXL5007T_CLK_OUT_AMP_3,
	MXL5007T_CLK_OUT_AMP_4,
	MXL5007T_CLK_OUT_AMP_5,
	MXL5007T_CLK_OUT_AMP_6,
	MXL5007T_CLK_OUT_AMP_7,
};


// Bandwidth mode
enum MXL5007T_BANDWIDTH_MODE
{
	MXL5007T_BANDWIDTH_6000000HZ,
	MXL5007T_BANDWIDTH_7000000HZ,
	MXL5007T_BANDWIDTH_8000000HZ,
};



// Constant
#define MXL5007T_I2C_READING_CONST		0xfb

// Default value
#define MXL5007T_RF_FREQ_HZ_DEFAULT			44000000;
#define MXL5007T_BANDWIDTH_MODE_DEFAULT		MxL_BW_6MHz;





// Builder
void
BuildMxl5007tModule(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned long CrystalFreqHz,
	int StandardMode,
	unsigned long IfFreqHz,
	int SpectrumMode,
	int LoopThroughMode,
	int ClkOutMode,
	int ClkOutAmpMode,
	long QamIfDiffOutLevel
	);





// Manipulaing functions
void
mxl5007t_GetTunerType(
	TUNER_MODULE *pTuner,
	int *pTunerType
	);

void
mxl5007t_GetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char *pDeviceAddr
	);

int
mxl5007t_Initialize(
	TUNER_MODULE *pTuner
	);

int
mxl5007t_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	);

int
mxl5007t_GetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long *pRfFreqHz
	);





// Extra manipulaing functions
int
mxl5007t_SetBandwidthMode(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

int
mxl5007t_GetBandwidthMode(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);













#endif
