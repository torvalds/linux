#ifndef __TUNER_MAX3543_H
#define __TUNER_MAX3543_H

/**

@file

@brief   MAX3543 tuner module declaration

One can manipulate MAX3543 tuner through MAX3543 module.
MAX3543 module is derived from tuner module.



@par Example:
@code

// The example is the same as the tuner example in tuner_base.h except the listed lines.



#include "tuner_max3543.h"


...



int main(void)
{
	TUNER_MODULE         *pTuner;
	MAX3543_EXTRA_MODULE *pTunerExtra;

	TUNER_MODULE          TunerModuleMemory;
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;
	I2C_BRIDGE_MODULE     I2cBridgeModuleMemory;

	unsigned long BandwidthMode;


	...



	// Build MAX3543 tuner module.
	BuildMax3543Module(
		&pTuner,
		&TunerModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0xc0,								// I2C device address is 0xac in 8-bit format.
		CRYSTAL_FREQ_16000000HZ,			// Crystal frequency is 16.0 MHz.
		MAX3543_STANDARD_DVBT,				// The MAX3543 standard mode is DVB-T.
		IF_FREQ_36170000HZ,					// The MAX3543 IF frequency is 36.17 MHz.
		MAX3543_SAW_INPUT_SE				// The MAX3543 SAW input type is single-ended.
		);





	// Get MAX3543 tuner extra module.
	pTunerExtra = (MAX3543_EXTRA_MODULE *)(pTuner->pExtra);





	// ==== Initialize tuner and set its parameters =====

	...

	// Set MAX3543 bandwidth.
	pTunerExtra->SetBandwidthMode(pTuner, MAX3543_BANDWIDTH_7MHZ);





	// ==== Get tuner information =====

	...

	// Get MAX3543 bandwidth.
	pTunerExtra->GetBandwidthMode(pTuner, &BandwidthMode);



	// See the example for other tuner functions in tuner_base.h


	return 0;
}


@endcode

*/





#include "tuner_base.h"





// The following context is implemented for MAX3543 source code.


// Definition (implemeted for MAX3543)

// Function return status
#define MAX3543_SUCCESS		1
#define MAX3543_ERROR		0



// Function (implemeted for MAX3543)
int
Max3543_Read(
	TUNER_MODULE *pTuner,
	unsigned short RegAddr,
	unsigned short *pData
	);

int
Max3543_Write(
	TUNER_MODULE *pTuner,
	unsigned short RegAddr,
	unsigned short Data
	);



























// The following context is source code provided by Analog Devices.





// MAXIM source code - mxmdef.h



//#ifndef MXMDEF_H
//#define MXMDEF_H

#define MAX_PATH          260
/*
#ifndef NULL
#ifdef __cplusplus
#define NULL    0
#else
#define NULL    ((void *)0)
#endif
#endif

#ifndef FALSE
#define FALSE               0
#endif

#ifndef TRUE
#define TRUE                1
#endif
*/

#define MAX_FALSE		0
#define MAX_TRUE		1


typedef unsigned long       DWORD;

//#endif /* _WINDEF_ */



























// MAXIM source code - Max3543_Driver.h



 /*
------------------------------------------------------
| Max3543_Driver.h, v 1.0.3, 12/9/2009, Paul Nichol    
| Description: Max3543 Driver Includes.                
|                                                      
| Copyright (C) 2009 Maxim Integrated Products         
|                                                      
------------------------------------------------------
*/




//#ifndef Max3543_Driver_H

/* integer only mode = 1, floating point mode = 0 */
//#define intmode 0


//#define Max3543_Driver_H


#define MAX3543_ADDR 0xc0

#define MAX3543_NUMREGS 0x15


/* Register Address offsets.  Used when sending or indexing registers.  */
#define REG3543_VCO 0
#define REG3543_NDIV 0x1
#define REG3543_FRAC2 0x2
#define REG3543_FRAC1 0x3
#define REG3543_FRAC0 0x4
#define REG3543_MODE 0x5
#define REG3543_TFS 0x6
#define REG3543_TFP 0x7
#define REG3543_SHDN 0x8
#define REG3543_REF 0x9
#define REG3543_VAS 0xa
#define REG3543_PD_CFG1 0xb
#define REG3543_PD_CFG2 0xc
#define REG3543_FILT_CF 0xd
#define REG3543_ROM_ADDR 0xe
#define REG3543_IRHR 0xf
#define REG3543_ROM_READ 0x10
#define REG3543_VAS_STATUS 0x11
#define REG3543_GEN_STATUS 0x12
#define REG3543_BIAS_ADJ 0x13
#define REG3543_TEST1 0x14
#define REG3543_ROM_WRITE 0x15

/* Band constants: */
#define VHF_L 0
#define VHF_H 1
#define UHF 2

/* Channel Bandwidth: */
#define BW7MHZ 0
#define BW8MHZ 1

#define SER0 0
#define SER1 1
#define PAR0 2
#define PAR1 3




typedef enum  {IFOUT1_DIFF_DTVOUT, IFOUT1_SE_DTVOUT,IFOUT2} outputmode;

typedef enum {DVB_T, DVB_C, ATV, ATV_SECAM_L} standard;

//standard broadcast_standard;



/* Note:
   The SetFrequency() routine must make it's calculations without
	overflowing 32 bit accumulators.  This is a difficult balance of LO, IF and Xtal frequencies.
	Scaling factors are applied to these frequencies to keep the numbers below the 32 bit result during
	caltculations.   The calculations have been checked for only the following combinations of frequencies
	and settings: Xtal freqencies of 16.0MHz, 20.25 MHz, 20.48 MHz; IF Frequencies of 30.0 MHz and 30.15MHz;
	R-Dividers /1 and /2.  Any combination of the above numbers may be used.
	If other combinations or frequencies are needed, the scaling factors: LOSCALE and XTALSCALE must be
	recalculated.  This has been done in a spreadsheet calc.xls.  Without checking these
	scale factors carefully, there could be overflow and tuning errors or amplitude losses due to an
	incorrect tracking filter setting.
*/

/* Scaling factor for the IF and RF freqencies.
	Freqencies passed to functions must be multiplied by this factor.
	(See Note above).
*/
//#define LOSCALE 40         

/* Scaling factor for Xtal frequency.  
   Use 32 for 16.0MHz, 25 for 20.48 and 4 for 20.25MHz.
	(See Note above).
*/
//#define XTALSCALE 4      

//#if intmode
	/* Macros used for scaling frequency constants.  */
	/* Use this form if using floating point math. */

#define scalefrq(x) ( (UINT_32) ( ( (UINT_16) x) * (UINT_16) pExtra->LOSCALE ) ) 
//	#define scalextal(x) ( (UINT_32) ( ( (UINT_16) x ) * (UINT_16) XTALSCALE ) ) 


	/* Note, this is a scaling factor for the Xtal Reference applied to the MAX3543 Xtal Pin.
		The only valid frequencies are 16.0, 20.25 or 20.48MHz and only with the following conditions:
		RDiv = /1 or RDiv = /2, IF = 36.0MHz, IF = 36.15 MHz. 
		(See Note above).
	*/
//	#define XTALREF 81
	/* 20.25 * XTALSCALE = 81, where XTALSCALE=4
		Use this form if NOT using floating point math.
	*/
//#else
	/* Macros used for scaling frequency constants.  */
	/* Use this form if NOT using floating point math. */
//		#define scalefrq(x)  ( (unsigned short) ( ( (float) x ) * (float) LOSCALE ) ) 
//		#define scalextal(x) ( (unsigned short) ( ( (float) x ) * (float) XTALSCALE ) )

	/* Note, this is a scaling factor for the Xtal Reference applied to the MAX3543 Xtal Pin.
		The only valid frequencies are 16.0, 20.25 or 20.48MHz and only with the following conditions:
		RDiv = /1 or RDiv = /2, IF = 36.0MHz, IF = 36.15 MHz. 
		(See Note above).
	*/
//	#define XTALREF scalextal(20.25)
	/* Use this form if NOT using floating point math. */
	/* #define XTALREF 81 */
	/* (XTALSCALE * Reference frequency 20.24 * 4 = 81) */
//#endif




#define ATV_SINGLE 2

typedef  short INT_16;          /* compiler type for 16 bit integer */
typedef  unsigned short UINT_16; /* compiler type for 16 bit unsigned integer */
typedef  unsigned long UINT_32;  /* compiler type for 32 bit unsigned integer */

typedef enum {IFOUT_1,IFOUT_2} outmd;

//int MAX3543_Init(TUNER_MODULE *pTuner, UINT_16 RfFreq);
int MAX3543_Init(TUNER_MODULE *pTuner);
int MAX3543_SetFrequency(TUNER_MODULE *pTuner, UINT_16 RF_Frequency);
//unsigned short MAX3543_Read(UINT_16 reg);
//void MAX3543_Write(UINT_16 reg, UINT_16 value);
int MAX3543_LockDetect(TUNER_MODULE *pTuner, int *pAnswer);
int MAX3543_Standard(TUNER_MODULE *pTuner, standard bcstd, outputmode outmd);
int MAX3543_ChannelBW(TUNER_MODULE *pTuner, UINT_16 bw);
int MAX3543_SetTrackingFilter(TUNER_MODULE *pTuner, UINT_16 RF_Frequency);
int MAX3543_ReadROM(TUNER_MODULE *pTuner);
UINT_16 tfs_i(UINT_16 S0, UINT_16 S1, UINT_16 FreqRF, const UINT_16 c[5]);
int MAX3543_SeedVCO(TUNER_MODULE *pTuner, UINT_16 Fvco);


/******* External functions called by Max3543 code *******/


		/*   The implementation of these functions is left for the end user.  */
		/*   This is because of the many different microcontrollers and serial */
		/*   I/O methods that can be used.  */

//    void  Max3543_Write(unsigned short RegAddr, unsigned short data);

		/*   This function sends out a byte of data using the following format.    */
		/*   Start, IC_address, ACK, Register Address, Ack, Data, Ack, Stop */

		/*   IC_address is 0xC0 or 0xC4 depending on JP8 of the Max3543 evkit board. */
		/*   0xC0 if the ADDR pin of the Max3543 is low, x0C4 if the pin is high. */
		/*   The register address is the Index into the register */
		/*   you wish to fill. */

//    unsigned short Max3543_Read(unsigned short reg);

    /*   This reads and returns a byte from the Max3543.    */
    /*   The read sequence is: */
    /*   Start, IC_address, ACK, Register Address, ack, Start, DeviceReadAddress, ack, */
	 /*   Data, NAck, Stop */
    /*   Note that there is a IC_Address (0xC0 or 0xC4 as above) and a Device Read */
	 /*   Address which is the IC_Address + 1  (0xC1 or 0xC5).    */
    /*   There are also two start conditions in the read back sequence. */
    /*   The Register Address is an index into the register you */
	 /*   wish to read back. */


//#endif



























// The following context is MAX3543 tuner API source code





// Definitions

// Standard mode
enum MAX3543_STANDARD_MODE
{
	MAX3543_STANDARD_DVBT = DVB_T,
	MAX3543_STANDARD_QAM  = DVB_C,
};


// Bandwidth mode
enum MAX3543_BANDWIDTH_MODE
{
	MAX3543_BANDWIDTH_7000000HZ = BW7MHZ,
	MAX3543_BANDWIDTH_8000000HZ = BW8MHZ,
};


// SAW input type
enum MAX3543_SAW_INPUT_TYPE
{
	MAX3543_SAW_INPUT_DIFF = IFOUT1_DIFF_DTVOUT,
	MAX3543_SAW_INPUT_SE   = IFOUT1_SE_DTVOUT,
};



// Default for initialing
#define MAX3543_RF_FREQ_HZ_DEFAULT			474000000
#define MAX3543_BANDWIDTH_MODE_DEFAULT		MAX3543_BANDWIDTH_8000000HZ


// Definition for RF frequency setting.
#define MAX3543_CONST_1_MHZ		1000000





// Builder
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
	);





// Manipulaing functions
void
max3543_GetTunerType(
	TUNER_MODULE *pTuner,
	int *pTunerType
	);

void
max3543_GetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char *pDeviceAddr
	);

int
max3543_Initialize(
	TUNER_MODULE *pTuner
	);

int
max3543_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	);

int
max3543_GetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long *pRfFreqHz
	);





// Extra manipulaing functions
int
max3543_SetBandwidthMode(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

int
max3543_GetBandwidthMode(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);













#endif
