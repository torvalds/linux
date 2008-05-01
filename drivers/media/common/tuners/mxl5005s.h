/*
 * For the Realtek RTL chip RTL2831U
 * Realtek Release Date: 2008-03-14, ver 080314
 * Realtek version RTL2831 Linux driver version 080314
 * ver 080314
 *
 * for linux kernel version 2.6.21.4 - 2.6.22-14
 * support MXL5005s and MT2060 tuners (support tuner auto-detecting)
 * support two IR types -- RC5 and NEC
 *
 * Known boards with Realtek RTL chip RTL2821U
 *    Freecom USB stick 14aa:0160 (version 4)
 *    Conceptronic CTVDIGRCU
 *
 * Copyright (c) 2008 Realtek
 * Copyright (c) 2008 Jan Hoogenraad, Barnaby Shearer, Andy Hasper
 * This code is placed under the terms of the GNU General Public License
 *
 * Released by Realtek under GPLv2.
 * Thanks to Realtek for a lot of support we received !
 *
 *  Revision: 080314 - original version
 */


#ifndef __TUNER_MXL5005S_H
#define __TUNER_MXL5005S_H



// The following context is source code provided by MaxLinear.


// MaxLinear source code - Common.h



//#pragma once

typedef unsigned char		_u8;						// At least 1 Byte
typedef unsigned short		_u16;						// At least 2 Bytes
typedef signed short		_s16;
typedef unsigned long		_u32;						// At least 4 Bytes
typedef void *				HANDLE;						// Pointer to memory location

#define TUNER_REGS_NUM		104
#define INITCTRL_NUM		40
#ifdef _MXL_PRODUCTION
#define CHCTRL_NUM			39
#else
#define CHCTRL_NUM			36
#endif

#define MXLCTRL_NUM			189

#define MASTER_CONTROL_ADDR		9




// Enumeration of AGC Mode
typedef enum
{
	MXL_DUAL_AGC = 0 ,
	MXL_SINGLE_AGC
} AGC_Mode ;

//
// Enumeration of Master Control Register State
//
typedef enum
{
	MC_LOAD_START = 1 ,
	MC_POWER_DOWN ,
	MC_SYNTH_RESET ,
	MC_SEQ_OFF
} Master_Control_State ;

//
// Enumeration of MXL5005 Tuner Mode
//
typedef enum
{
	MXL_ANALOG_MODE = 0 ,
	MXL_DIGITAL_MODE

} Tuner_Mode ;

//
// Enumeration of MXL5005 Tuner IF Mode
//
typedef enum
{
	MXL_ZERO_IF = 0 ,
	MXL_LOW_IF

} Tuner_IF_Mode ;

//
// Enumeration of MXL5005 Tuner Clock Out Mode
//
typedef enum
{
	MXL_CLOCK_OUT_DISABLE = 0 ,
	MXL_CLOCK_OUT_ENABLE
} Tuner_Clock_Out ;

//
// Enumeration of MXL5005 Tuner Div Out Mode
//
typedef enum
{
	MXL_DIV_OUT_1 = 0 ,
	MXL_DIV_OUT_4

} Tuner_Div_Out ;

//
// Enumeration of MXL5005 Tuner Pull-up Cap Select Mode
//
typedef enum
{
	MXL_CAP_SEL_DISABLE = 0 ,
	MXL_CAP_SEL_ENABLE

} Tuner_Cap_Select ;

//
// Enumeration of MXL5005 Tuner RSSI Mode
//
typedef enum
{
	MXL_RSSI_DISABLE = 0 ,
	MXL_RSSI_ENABLE

} Tuner_RSSI ;

//
// Enumeration of MXL5005 Tuner Modulation Type
//
typedef enum
{
	MXL_DEFAULT_MODULATION = 0 ,
	MXL_DVBT,
	MXL_ATSC,
	MXL_QAM,
	MXL_ANALOG_CABLE,
	MXL_ANALOG_OTA

} Tuner_Modu_Type ;

//
// Enumeration of MXL5005 Tuner Tracking Filter Type
//
typedef enum
{
	MXL_TF_DEFAULT = 0 ,
	MXL_TF_OFF,
	MXL_TF_C,
	MXL_TF_C_H,
	MXL_TF_D,
	MXL_TF_D_L,
	MXL_TF_E,
	MXL_TF_F,
	MXL_TF_E_2,
	MXL_TF_E_NA,
	MXL_TF_G


} Tuner_TF_Type ;


//
// MXL5005 Tuner Register Struct
//
typedef struct _TunerReg_struct
{
	_u16 	Reg_Num ;							// Tuner Register Address
	_u16	Reg_Val ;							// Current sofware programmed value waiting to be writen
} TunerReg_struct ;

//
// MXL5005 Tuner Control Struct
//
typedef struct _TunerControl_struct {
	_u16	Ctrl_Num ;							// Control Number
	_u16	size ;								// Number of bits to represent Value
	_u16 	addr[25] ;							// Array of Tuner Register Address for each bit position
	_u16 	bit[25] ;							// Array of bit position in Register Address for each bit position
	_u16 	val[25] ;							// Binary representation of Value
} TunerControl_struct ;

//
// MXL5005 Tuner Struct
//
typedef struct _Tuner_struct
{
	_u8			Mode ;				// 0: Analog Mode ; 1: Digital Mode
	_u8			IF_Mode ;			// for Analog Mode, 0: zero IF; 1: low IF
	_u32			Chan_Bandwidth ;	// filter  channel bandwidth (6, 7, 8)
	_u32			IF_OUT ;			// Desired IF Out Frequency
	_u16			IF_OUT_LOAD ;		// IF Out Load Resistor (200/300 Ohms)
	_u32			RF_IN ;				// RF Input Frequency
	_u32			Fxtal ;				// XTAL Frequency
	_u8			AGC_Mode ;			// AGC Mode 0: Dual AGC; 1: Single AGC
	_u16			TOP ;				// Value: take over point
	_u8			CLOCK_OUT ;			// 0: turn off clock out; 1: turn on clock out
	_u8			DIV_OUT ;			// 4MHz or 16MHz
	_u8			CAPSELECT ;			// 0: disable On-Chip pulling cap; 1: enable
	_u8			EN_RSSI ;			// 0: disable RSSI; 1: enable RSSI
	_u8			Mod_Type ;			// Modulation Type;
										// 0 - Default;	1 - DVB-T; 2 - ATSC; 3 - QAM; 4 - Analog Cable
	_u8			TF_Type ;			// Tracking Filter Type
										// 0 - Default; 1 - Off; 2 - Type C; 3 - Type C-H

	// Calculated Settings
	_u32			RF_LO ;				// Synth RF LO Frequency
	_u32			IF_LO ;				// Synth IF LO Frequency
	_u32			TG_LO ;				// Synth TG_LO Frequency

	// Pointers to ControlName Arrays
	_u16					Init_Ctrl_Num ;					// Number of INIT Control Names
	TunerControl_struct		Init_Ctrl[INITCTRL_NUM] ;		// INIT Control Names Array Pointer
	_u16					CH_Ctrl_Num ;					// Number of CH Control Names
	TunerControl_struct		CH_Ctrl[CHCTRL_NUM] ;			// CH Control Name Array Pointer
	_u16					MXL_Ctrl_Num ;					// Number of MXL Control Names
	TunerControl_struct		MXL_Ctrl[MXLCTRL_NUM] ;			// MXL Control Name Array Pointer

	// Pointer to Tuner Register Array
	_u16					TunerRegs_Num ;		// Number of Tuner Registers
	TunerReg_struct			TunerRegs[TUNER_REGS_NUM] ;			// Tuner Register Array Pointer
} Tuner_struct ;



typedef enum
{
	//
	// Initialization Control Names
	//
	DN_IQTN_AMP_CUT = 1 ,       // 1
	BB_MODE ,                   // 2
	BB_BUF ,                    // 3
	BB_BUF_OA ,			// 4
	BB_ALPF_BANDSELECT ,        // 5
	BB_IQSWAP ,                 // 6
	BB_DLPF_BANDSEL ,           // 7
	RFSYN_CHP_GAIN ,            // 8
	RFSYN_EN_CHP_HIGAIN ,       // 9
	AGC_IF ,                    // 10
	AGC_RF ,                    // 11
	IF_DIVVAL ,                 // 12
	IF_VCO_BIAS ,               // 13
	CHCAL_INT_MOD_IF ,          // 14
	CHCAL_FRAC_MOD_IF ,         // 15
	DRV_RES_SEL ,               // 16
	I_DRIVER ,                  // 17
	EN_AAF ,                    // 18
	EN_3P ,                     // 19
	EN_AUX_3P ,                 // 20
	SEL_AAF_BAND ,              // 21
	SEQ_ENCLK16_CLK_OUT ,       // 22
	SEQ_SEL4_16B ,              // 23
	XTAL_CAPSELECT ,            // 24
	IF_SEL_DBL ,                // 25
	RFSYN_R_DIV ,               // 26
	SEQ_EXTSYNTHCALIF ,         // 27
	SEQ_EXTDCCAL ,              // 28
	AGC_EN_RSSI ,               // 29
	RFA_ENCLKRFAGC ,            // 30
	RFA_RSSI_REFH ,             // 31
	RFA_RSSI_REF ,              // 32
	RFA_RSSI_REFL ,             // 33
	RFA_FLR ,                   // 34
	RFA_CEIL ,                  // 35
	SEQ_EXTIQFSMPULSE ,         // 36
	OVERRIDE_1 ,                // 37
	BB_INITSTATE_DLPF_TUNE,     // 38
	TG_R_DIV,					// 39
	EN_CHP_LIN_B ,				// 40

	//
	// Channel Change Control Names
	//
	DN_POLY = 51 ,              // 51
	DN_RFGAIN ,                 // 52
	DN_CAP_RFLPF ,              // 53
	DN_EN_VHFUHFBAR ,           // 54
	DN_GAIN_ADJUST ,            // 55
	DN_IQTNBUF_AMP ,            // 56
	DN_IQTNGNBFBIAS_BST ,       // 57
	RFSYN_EN_OUTMUX ,           // 58
	RFSYN_SEL_VCO_OUT ,         // 59
	RFSYN_SEL_VCO_HI ,          // 60
	RFSYN_SEL_DIVM ,            // 61
	RFSYN_RF_DIV_BIAS ,         // 62
	DN_SEL_FREQ ,               // 63
	RFSYN_VCO_BIAS ,            // 64
	CHCAL_INT_MOD_RF ,          // 65
	CHCAL_FRAC_MOD_RF ,         // 66
	RFSYN_LPF_R ,               // 67
	CHCAL_EN_INT_RF ,           // 68
	TG_LO_DIVVAL ,              // 69
	TG_LO_SELVAL ,              // 70
	TG_DIV_VAL ,                // 71
	TG_VCO_BIAS ,               // 72
	SEQ_EXTPOWERUP ,            // 73
	OVERRIDE_2 ,                // 74
	OVERRIDE_3 ,                // 75
	OVERRIDE_4 ,                // 76
	SEQ_FSM_PULSE ,             // 77
	GPIO_4B,                    // 78
	GPIO_3B,                    // 79
	GPIO_4,                     // 80
	GPIO_3,                     // 81
	GPIO_1B,                    // 82
	DAC_A_ENABLE ,              // 83
	DAC_B_ENABLE ,              // 84
	DAC_DIN_A ,			// 85
	DAC_DIN_B ,                 // 86
#ifdef _MXL_PRODUCTION
	RFSYN_EN_DIV,				// 87
	RFSYN_DIVM,					// 88
	DN_BYPASS_AGC_I2C			// 89
#endif

} MXL5005_ControlName ;















// MaxLinear source code - MXL5005_c.h



// MXL5005.h : main header file for the MXL5005 DLL
//
//#pragma once

//#include "Common.h"
#ifdef _MXL_INTERNAL
#include "Common_MXL.h"
#endif

void InitTunerControls( Tuner_struct *Tuner) ;

_u16 MXL_BlockInit( Tuner_struct *Tuner ) ;

_u16 MXL5005_RegisterInit (Tuner_struct * Tuner) ;
_u16 MXL5005_ControlInit (Tuner_struct *Tuner) ;

#ifdef _MXL_INTERNAL
	_u16 MXL5005_MXLControlInit(Tuner_struct *Tuner) ;
#endif

_u16 MXL5005_TunerConfig(Tuner_struct *Tuner,
	_u8		Mode,		// 0: Analog Mode ; 1: Digital Mode
	_u8		IF_mode,	// for Analog Mode, 0: zero IF; 1: low IF
	_u32	Bandwidth,	// filter  channel bandwidth (6, 7, 8)
	_u32	IF_out,		// Desired IF Out Frequency
	_u32	Fxtal,		// XTAL Frequency
	_u8		AGC_Mode,	// AGC Mode - Dual AGC: 0, Single AGC: 1
	_u16	TOP,        // 0: Dual AGC; Value: take over point
	_u16	IF_OUT_LOAD,// IF Out Load Resistor (200 / 300 Ohms)
	_u8		CLOCK_OUT, 	// 0: turn off clock out; 1: turn on clock out
	_u8		DIV_OUT,	// 4MHz or 16MHz
	_u8		CAPSELECT, 	// 0: disable On-Chip pulling cap; 1: enable
	_u8		EN_RSSI, 	// 0: disable RSSI; 1: enable RSSI
	_u8		Mod_Type,	// Modulation Type;
						// 0 - Default;		1 - DVB-T;	2 - ATSC;	3 - QAM; 4 - Analog Cable
	_u8		TF_Type		// Tracking Filter Type
						// 0 - Default; 1 - Off; 2 - Type C; 3 - Type C-H
	) ;

void MXL_SynthIFLO_Calc(Tuner_struct *Tuner) ;
void MXL_SynthRFTGLO_Calc(Tuner_struct *Tuner) ;
_u16 MXL_RegWrite(Tuner_struct *Tuner, _u8 RegNum, _u8 RegVal) ;
_u16 MXL_RegRead(Tuner_struct *Tuner, _u8 RegNum, _u8 *RegVal) ;
_u16 MXL_ControlWrite(Tuner_struct *Tuner, _u16 ControlNum, _u32 value) ;
_u16 MXL_ControlWrite_Group(Tuner_struct *Tuner, _u16 ControlNum, _u32 value, _u16 controlGroup) ;
_u16 MXL_ControlRead(Tuner_struct *Tuner, _u16 ControlNum, _u32 * value) ;
_u16 MXL_ControlRegRead(Tuner_struct *Tuner, _u16 ControlNum, _u8 *RegNum, int * count) ;
void MXL_RegWriteBit(Tuner_struct *Tuner, _u8 address, _u8 bit, _u8 bitVal);
_u16 MXL_IFSynthInit( Tuner_struct * Tuner ) ;
_u16 MXL_TuneRF(Tuner_struct *Tuner, _u32 RF_Freq) ;
_u16 MXL_OverwriteICDefault( Tuner_struct *Tuner) ;
_u16 MXL_SetGPIO(Tuner_struct *Tuner, _u8 GPIO_Num, _u8 GPIO_Val) ;
_u32 MXL_Ceiling( _u32 value, _u32 resolution ) ;
_u32 MXL_GetXtalInt(_u32 Xtal_Freq) ;

_u16 MXL_GetInitRegister(Tuner_struct *Tuner, _u8 * RegNum, _u8 *RegVal, int *count) ;
_u16 MXL_GetCHRegister(Tuner_struct *Tuner, _u8 * RegNum, _u8 *RegVal, int *count) ;
_u16 MXL_GetCHRegister_ZeroIF(Tuner_struct *Tuner, _u8 * RegNum, _u8 *RegVal, int *count) ;
_u16 MXL_GetCHRegister_LowIF(Tuner_struct *Tuner, _u8 * RegNum, _u8 *RegVal, int *count) ;
_u16 MXL_GetMasterControl(_u8 *MasterReg, int state) ;

#ifdef _MXL_PRODUCTION
_u16 MXL_VCORange_Test(Tuner_struct *Tuner, int VCO_Range) ;
_u16 MXL_Hystersis_Test(Tuner_struct *Tuner, int Hystersis) ;
#endif























// The following context is MxL5005S tuner API source code





/**

@file

@brief   MxL5005S tuner module declaration

One can manipulate MxL5005S tuner through MxL5005S module.
MxL5005S module is derived from tuner module.

*/



#include "tuner_base.h"





// Definitions

// Constants
#define MXL5005S_REG_WRITING_TABLE_LEN_MAX		104
#define MXL5005S_LATCH_BYTE						0xfe

// Register address, MSB, and LSB
#define MXL5005S_BB_IQSWAP_ADDR				59
#define MXL5005S_BB_IQSWAP_MSB				0
#define MXL5005S_BB_IQSWAP_LSB				0

#define MXL5005S_BB_DLPF_BANDSEL_ADDR		53
#define MXL5005S_BB_DLPF_BANDSEL_MSB		4
#define MXL5005S_BB_DLPF_BANDSEL_LSB		3



// Standard modes
enum
{
	MXL5005S_STANDARD_DVBT,
	MXL5005S_STANDARD_ATSC,
};
#define MXL5005S_STANDARD_MODE_NUM		2


// Bandwidth modes
enum
{
	MXL5005S_BANDWIDTH_6MHZ = 6000000,
	MXL5005S_BANDWIDTH_7MHZ = 7000000,
	MXL5005S_BANDWIDTH_8MHZ = 8000000,
};
#define MXL5005S_BANDWIDTH_MODE_NUM		3


// Top modes
enum
{
	MXL5005S_TOP_5P5  =  55,
	MXL5005S_TOP_7P2  =  72,
	MXL5005S_TOP_9P2  =  92,
	MXL5005S_TOP_11P0 = 110,
	MXL5005S_TOP_12P9 = 129,
	MXL5005S_TOP_14P7 = 147,
	MXL5005S_TOP_16P8 = 168,
	MXL5005S_TOP_19P4 = 194,
	MXL5005S_TOP_21P2 = 212,
	MXL5005S_TOP_23P2 = 232,
	MXL5005S_TOP_25P2 = 252,
	MXL5005S_TOP_27P1 = 271,
	MXL5005S_TOP_29P2 = 292,
	MXL5005S_TOP_31P7 = 317,
	MXL5005S_TOP_34P9 = 349,
};


// IF output load
enum
{
	MXL5005S_IF_OUTPUT_LOAD_200_OHM = 200,
	MXL5005S_IF_OUTPUT_LOAD_300_OHM = 300,
};





/// MxL5005S extra module alias
typedef struct MXL5005S_EXTRA_MODULE_TAG MXL5005S_EXTRA_MODULE;





// MxL5005S register setting function pointer
typedef int
(*MXL5005S_FP_SET_REGS_WITH_TABLE)(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned char *pAddrTable,
	unsigned char *pByteTable,
	int TableLen
	);


// MxL5005S register mask bits setting function pointer
typedef int
(*MXL5005S_FP_SET_REG_MASK_BITS)(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned char RegAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned char WritingValue
	);


// MxL5005S spectrum mode setting function pointer
typedef int
(*MXL5005S_FP_SET_SPECTRUM_MODE)(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	int SpectrumMode
	);


// MxL5005S bandwidth setting function pointer
typedef int
(*MXL5005S_FP_SET_BANDWIDTH_HZ)(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned long BandwidthHz
	);





// MxL5005S extra module
struct MXL5005S_EXTRA_MODULE_TAG
{
	// MxL5005S function pointers
	MXL5005S_FP_SET_REGS_WITH_TABLE   SetRegsWithTable;
	MXL5005S_FP_SET_REG_MASK_BITS     SetRegMaskBits;
	MXL5005S_FP_SET_SPECTRUM_MODE     SetSpectrumMode;
	MXL5005S_FP_SET_BANDWIDTH_HZ      SetBandwidthHz;


	// MxL5005S extra data
	unsigned char AgcMasterByte;			//   Variable name in MaxLinear source code: AGC_MASTER_BYTE

	// MaxLinear defined struct
	Tuner_struct MxlDefinedTunerStructure;
};





// Builder
void
BuildMxl5005sModule(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	MXL5005S_EXTRA_MODULE *pMxl5005sExtraModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	int StandardMode
	);





// Manipulaing functions
void
mxl5005s_SetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char DeviceAddr
	);

void
mxl5005s_GetTunerType(
	TUNER_MODULE *pTuner,
	int *pTunerType
	);

int
mxl5005s_GetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char *pDeviceAddr
	);

int
mxl5005s_Initialize(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner
	);

int
mxl5005s_SetRfFreqHz(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	);

int
mxl5005s_GetRfFreqHz(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned long *pRfFreqHz
	);





// Extra manipulaing functions
int
mxl5005s_SetRegsWithTable(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned char *pAddrTable,
	unsigned char *pByteTable,
	int TableLen
	);

int
mxl5005s_SetRegMaskBits(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned char RegAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned char WritingValue
	);

int
mxl5005s_SetSpectrumMode(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	int SpectrumMode
	);

int
mxl5005s_SetBandwidthHz(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned long BandwidthHz
	);





// I2C birdge module demod argument setting
void
mxl5005s_SetI2cBridgeModuleTunerArg(
	TUNER_MODULE *pTuner
	);















#endif

