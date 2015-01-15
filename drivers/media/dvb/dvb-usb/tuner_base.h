#ifndef __TUNER_BASE_H
#define __TUNER_BASE_H

/**

@file

@brief   Tuner base module definition

Tuner base module definitions contains tuner module structure, tuner funciton pointers, and tuner definitions.



@par Example:
@code


#include "demod_xxx.h"
#include "tuner_xxx.h"



int
CustomI2cRead(
	BASE_INTERFACE_MODULE *pBaseInterface,
	unsigned char DeviceAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	)
{
	// I2C reading format:
	// start_bit + (DeviceAddr | reading_bit) + reading_byte * ByteNum + stop_bit

	...

	return FUNCTION_SUCCESS;

error_status:
	return FUNCTION_ERROR;
}



int
CustomI2cWrite(
	BASE_INTERFACE_MODULE *pBaseInterface,
	unsigned char DeviceAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	)
{
	// I2C writing format:
	// start_bit + (DeviceAddr | writing_bit) + writing_byte * ByteNum + stop_bit

	...

	return FUNCTION_SUCCESS;

error_status:
	return FUNCTION_ERROR;
}



void
CustomWaitMs(
	BASE_INTERFACE_MODULE *pBaseInterface,
	unsigned long WaitTimeMs
	)
{
	// Wait WaitTimeMs milliseconds.

	...

	return;
}



int main(void)
{
	BASE_INTERFACE_MODULE *pBaseInterface;
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;

	XXX_DEMOD_MODULE *pDemod;
	XXX_DEMOD_MODULE XxxDemodModuleMemory;

	TUNER_MODULE *pTuner;
	TUNER_MODULE TunerModuleMemory;

	I2C_BRIDGE_MODULE I2cBridgeModuleMemory;

	unsigned long RfFreqHz;

	int TunerType;
	unsigned char DeviceAddr;



	// Build base interface module.
	BuildBaseInterface(
		&pBaseInterface,
		&BaseInterfaceModuleMemory,
		9,								// Set maximum I2C reading byte number with 9.
		8,								// Set maximum I2C writing byte number with 8.
		CustomI2cRead,					// Employ CustomI2cRead() as basic I2C reading function.
		CustomI2cWrite,					// Employ CustomI2cWrite() as basic I2C writing function.
		CustomWaitMs					// Employ CustomWaitMs() as basic waiting function.
		);


	// Build dmeod XXX module.
	// Note: Demod module builder will set I2cBridgeModuleMemory for tuner I2C command forwarding.
	//       Must execute demod builder to set I2cBridgeModuleMemory before use tuner functions.
	BuildDemodXxxModule(
		&pDemod,
		&XxxDemodModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		...								// Other arguments by each demod module
		)


	// Build tuner XXX module.
	BuildTunerXxxModule(
		&pTuner,
		&TunerModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0xc0,							// Tuner I2C device address is 0xc0 in 8-bit format.
		...								// Other arguments by each demod module
		);





	// ==== Initialize tuner and set its parameters =====

	// Initialize tuner.
	pTuner->Initialize(pTuner);


	// Set tuner parameters. (RF frequency)
	// Note: In the example, RF frequency is 474 MHz.
	RfFreqHz = 474000000;

	pTuner->SetIfFreqHz(pTuner, RfFreqHz);





	// ==== Get tuner information =====

	// Get tuenr type.
	// Note: One can find tuner type in MODULE_TYPE enumeration.
	pTuner->GetTunerType(pTuner, &TunerType);

	// Get tuner I2C device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Get tuner parameters. (RF frequency)
	pTuner->GetRfFreqHz(pTuner, &RfFreqHz);



	return 0;
}


@endcode

*/


#include "foundation.h"





/// tuner module pre-definition
typedef struct TUNER_MODULE_TAG TUNER_MODULE;





/**

@brief   Tuner type getting function pointer

One can use TUNER_FP_GET_TUNER_TYPE() to get tuner type.


@param [in]    pTuner       The tuner module pointer
@param [out]   pTunerType   Pointer to an allocated memory for storing tuner type


@note
	-# Tuner building function will set TUNER_FP_GET_TUNER_TYPE() with the corresponding function.


@see   TUNER_TYPE

*/
typedef void
(*TUNER_FP_GET_TUNER_TYPE)(
	TUNER_MODULE *pTuner,
	int *pTunerType
	);





/**

@brief   Tuner I2C device address getting function pointer

One can use TUNER_FP_GET_DEVICE_ADDR() to get tuner I2C device address.


@param [in]    pTuner        The tuner module pointer
@param [out]   pDeviceAddr   Pointer to an allocated memory for storing tuner I2C device address


@retval   FUNCTION_SUCCESS   Get tuner device address successfully.
@retval   FUNCTION_ERROR     Get tuner device address unsuccessfully.


@note
	-# Tuner building function will set TUNER_FP_GET_DEVICE_ADDR() with the corresponding function.

*/
typedef void
(*TUNER_FP_GET_DEVICE_ADDR)(
	TUNER_MODULE *pTuner,
	unsigned char *pDeviceAddr
	);





/**

@brief   Tuner initializing function pointer

One can use TUNER_FP_INITIALIZE() to initialie tuner.


@param [in]   pTuner   The tuner module pointer


@retval   FUNCTION_SUCCESS   Initialize tuner successfully.
@retval   FUNCTION_ERROR     Initialize tuner unsuccessfully.


@note
	-# Tuner building function will set TUNER_FP_INITIALIZE() with the corresponding function.

*/
typedef int
(*TUNER_FP_INITIALIZE)(
	TUNER_MODULE *pTuner
	);





/**

@brief   Tuner RF frequency setting function pointer

One can use TUNER_FP_SET_RF_FREQ_HZ() to set tuner RF frequency in Hz.


@param [in]   pTuner     The tuner module pointer
@param [in]   RfFreqHz   RF frequency in Hz for setting


@retval   FUNCTION_SUCCESS   Set tuner RF frequency successfully.
@retval   FUNCTION_ERROR     Set tuner RF frequency unsuccessfully.


@note
	-# Tuner building function will set TUNER_FP_SET_RF_FREQ_HZ() with the corresponding function.

*/
typedef int
(*TUNER_FP_SET_RF_FREQ_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	);





/**

@brief   Tuner RF frequency getting function pointer

One can use TUNER_FP_GET_RF_FREQ_HZ() to get tuner RF frequency in Hz.


@param [in]   pTuner      The tuner module pointer
@param [in]   pRfFreqHz   Pointer to an allocated memory for storing demod RF frequency in Hz


@retval   FUNCTION_SUCCESS   Get tuner RF frequency successfully.
@retval   FUNCTION_ERROR     Get tuner RF frequency unsuccessfully.


@note
	-# Tuner building function will set TUNER_FP_GET_RF_FREQ_HZ() with the corresponding function.

*/
typedef int
(*TUNER_FP_GET_RF_FREQ_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long *pRfFreqHz
	);





// TDCG-G052D extra module
typedef struct TDCGG052D_EXTRA_MODULE_TAG TDCGG052D_EXTRA_MODULE;
struct TDCGG052D_EXTRA_MODULE_TAG
{
	// TDCG-G052D extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control;
	unsigned char BandSwitch;
	unsigned char Auxiliary;
};





// TDCH-G001D extra module
typedef struct TDCHG001D_EXTRA_MODULE_TAG TDCHG001D_EXTRA_MODULE;
struct TDCHG001D_EXTRA_MODULE_TAG
{
	// TDCH-G001D extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control;
	unsigned char BandSwitch;
	unsigned char Auxiliary;
};





// TDQE3-003A extra module
#define TDQE3003A_CONTROL_BYTE_NUM		3

typedef struct TDQE3003A_EXTRA_MODULE_TAG TDQE3003A_EXTRA_MODULE;
struct TDQE3003A_EXTRA_MODULE_TAG
{
	// TDQE3-003A extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control[TDQE3003A_CONTROL_BYTE_NUM];
};





// DCT-7045 extra module
typedef struct DCT7045_EXTRA_MODULE_TAG DCT7045_EXTRA_MODULE;
struct DCT7045_EXTRA_MODULE_TAG
{
	// DCT-7045 extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control;
};





/// MT2062 extra module
typedef struct MT2062_EXTRA_MODULE_TAG MT2062_EXTRA_MODULE;

// MT2062 handle openning function pointer
typedef int
(*MT2062_FP_OPEN_HANDLE)(
	TUNER_MODULE *pTuner
	);

// MT2062 handle closing function pointer
typedef int
(*MT2062_FP_CLOSE_HANDLE)(
	TUNER_MODULE *pTuner
	);

// MT2062 handle getting function pointer
typedef void
(*MT2062_FP_GET_HANDLE)(
	TUNER_MODULE *pTuner,
	void **pDeviceHandle
	);

// MT2062 IF frequency setting function pointer
typedef int
(*MT2062_FP_SET_IF_FREQ_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long IfFreqHz
	);

// MT2062 IF frequency getting function pointer
typedef int
(*MT2062_FP_GET_IF_FREQ_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long *pIfFreqHz
	);

// MT2062 extra module
struct MT2062_EXTRA_MODULE_TAG
{
	// MT2062 extra variables
	void *DeviceHandle;
	int LoopThroughMode;

	unsigned long IfFreqHz;

	int IsIfFreqHzSet;

	// MT2062 extra function pointers
	MT2062_FP_OPEN_HANDLE      OpenHandle;
	MT2062_FP_CLOSE_HANDLE     CloseHandle;
	MT2062_FP_GET_HANDLE       GetHandle;
	MT2062_FP_SET_IF_FREQ_HZ   SetIfFreqHz;
	MT2062_FP_GET_IF_FREQ_HZ   GetIfFreqHz;
};





/// MxL5005S extra module
#define INITCTRL_NUM		40
#define CHCTRL_NUM			36
#define MXLCTRL_NUM			189
#define TUNER_REGS_NUM		104

typedef struct MXL5005S_EXTRA_MODULE_TAG MXL5005S_EXTRA_MODULE;

// MXL5005 Tuner Register Struct
typedef struct _TunerReg_struct
{
	unsigned short 	Reg_Num ;							// Tuner Register Address
	unsigned short	Reg_Val ;							// Current sofware programmed value waiting to be writen
} TunerReg_struct ;

// MXL5005 Tuner Control Struct
typedef struct _TunerControl_struct {
	unsigned short	Ctrl_Num ;							// Control Number
	unsigned short	size ;								// Number of bits to represent Value
	unsigned short 	addr[25] ;							// Array of Tuner Register Address for each bit position
	unsigned short 	bit[25] ;							// Array of bit position in Register Address for each bit position
	unsigned short 	val[25] ;							// Binary representation of Value
} TunerControl_struct ;

// MXL5005 Tuner Struct
typedef struct _Tuner_struct
{
	unsigned char			Mode ;				// 0: Analog Mode ; 1: Digital Mode
	unsigned char			IF_Mode ;			// for Analog Mode, 0: zero IF; 1: low IF
	unsigned long			Chan_Bandwidth ;	// filter  channel bandwidth (6, 7, 8)
	unsigned long			IF_OUT ;			// Desired IF Out Frequency
	unsigned short			IF_OUT_LOAD ;		// IF Out Load Resistor (200/300 Ohms)
	unsigned long			RF_IN ;				// RF Input Frequency
	unsigned long			Fxtal ;				// XTAL Frequency
	unsigned char			AGC_Mode ;			// AGC Mode 0: Dual AGC; 1: Single AGC
	unsigned short			TOP ;				// Value: take over point
	unsigned char			CLOCK_OUT ;			// 0: turn off clock out; 1: turn on clock out
	unsigned char			DIV_OUT ;			// 4MHz or 16MHz
	unsigned char			CAPSELECT ;			// 0: disable On-Chip pulling cap; 1: enable
	unsigned char			EN_RSSI ;			// 0: disable RSSI; 1: enable RSSI
	unsigned char			Mod_Type ;			// Modulation Type; 
										// 0 - Default;	1 - DVB-T; 2 - ATSC; 3 - QAM; 4 - Analog Cable
	unsigned char			TF_Type ;			// Tracking Filter Type
										// 0 - Default; 1 - Off; 2 - Type C; 3 - Type C-H

	// Calculated Settings
	unsigned long			RF_LO ;				// Synth RF LO Frequency
	unsigned long			IF_LO ;				// Synth IF LO Frequency
	unsigned long			TG_LO ;				// Synth TG_LO Frequency

	// Pointers to ControlName Arrays
	unsigned short					Init_Ctrl_Num ;					// Number of INIT Control Names
	TunerControl_struct		Init_Ctrl[INITCTRL_NUM] ;		// INIT Control Names Array Pointer
	unsigned short					CH_Ctrl_Num ;					// Number of CH Control Names
	TunerControl_struct		CH_Ctrl[CHCTRL_NUM] ;			// CH Control Name Array Pointer
	unsigned short					MXL_Ctrl_Num ;					// Number of MXL Control Names
	TunerControl_struct		MXL_Ctrl[MXLCTRL_NUM] ;			// MXL Control Name Array Pointer

	// Pointer to Tuner Register Array
	unsigned short					TunerRegs_Num ;		// Number of Tuner Registers
	TunerReg_struct			TunerRegs[TUNER_REGS_NUM] ;			// Tuner Register Array Pointer
} Tuner_struct ;

// MxL5005S register setting function pointer
typedef int
(*MXL5005S_FP_SET_REGS_WITH_TABLE)(
	TUNER_MODULE *pTuner,
	unsigned char *pAddrTable,
	unsigned char *pByteTable,
	int TableLen
	);

// MxL5005S register mask bits setting function pointer
typedef int
(*MXL5005S_FP_SET_REG_MASK_BITS)(
	TUNER_MODULE *pTuner,
	unsigned char RegAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned char WritingValue
	);

// MxL5005S spectrum mode setting function pointer
typedef int
(*MXL5005S_FP_SET_SPECTRUM_MODE)(
	TUNER_MODULE *pTuner,
	int SpectrumMode
	);

// MxL5005S bandwidth setting function pointer
typedef int
(*MXL5005S_FP_SET_BANDWIDTH_HZ)(
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





// TDVM-H715P extra module
typedef struct TDVMH715P_EXTRA_MODULE_TAG TDVMH751P_EXTRA_MODULE;
struct TDVMH715P_EXTRA_MODULE_TAG
{
	// TDVM-H715P extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control;
	unsigned char BandSwitch;
	unsigned char Auxiliary;
};





// UBA00AL extra module
typedef struct UBA00AL_EXTRA_MODULE_TAG UBA00AL_EXTRA_MODULE;
struct UBA00AL_EXTRA_MODULE_TAG
{
	// UBA00AL extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char BandSwitch;
	unsigned char Control2;
};





/// MT2266 extra module
typedef struct MT2266_EXTRA_MODULE_TAG MT2266_EXTRA_MODULE;

// MT2266 handle openning function pointer
typedef int
(*MT2266_FP_OPEN_HANDLE)(
	TUNER_MODULE *pTuner
	);

// MT2266 handle closing function pointer
typedef int
(*MT2266_FP_CLOSE_HANDLE)(
	TUNER_MODULE *pTuner
	);

// MT2266 handle getting function pointer
typedef void
(*MT2266_FP_GET_HANDLE)(
	TUNER_MODULE *pTuner,
	void **pDeviceHandle
	);

// MT2266 bandwidth setting function pointer
typedef int
(*MT2266_FP_SET_BANDWIDTH_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long BandwidthHz
	);

// MT2266 bandwidth getting function pointer
typedef int
(*MT2266_FP_GET_BANDWIDTH_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long *pBandwidthHz
	);

// MT2266 extra module
struct MT2266_EXTRA_MODULE_TAG
{
	// MT2266 extra variables
	void *DeviceHandle;
	unsigned long BandwidthHz;
	int IsBandwidthHzSet;

	// MT2266 extra function pointers
	MT2266_FP_OPEN_HANDLE        OpenHandle;
	MT2266_FP_CLOSE_HANDLE       CloseHandle;
	MT2266_FP_GET_HANDLE         GetHandle;
	MT2266_FP_SET_BANDWIDTH_HZ   SetBandwidthHz;
	MT2266_FP_GET_BANDWIDTH_HZ   GetBandwidthHz;
};





// FC2580 extra module
typedef struct FC2580_EXTRA_MODULE_TAG FC2580_EXTRA_MODULE;

// FC2580 bandwidth mode setting function pointer
typedef int
(*FC2580_FP_SET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

// FC2580 bandwidth mode getting function pointer
typedef int
(*FC2580_FP_GET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);

// FC2580 extra module
struct FC2580_EXTRA_MODULE_TAG
{
	// FC2580 extra variables
	unsigned long CrystalFreqHz;
	int AgcMode;
	int BandwidthMode;
	int IsBandwidthModeSet;

	// FC2580 extra function pointers
	FC2580_FP_SET_BANDWIDTH_MODE   SetBandwidthMode;
	FC2580_FP_GET_BANDWIDTH_MODE   GetBandwidthMode;
};





/// TUA9001 extra module
typedef struct TUA9001_EXTRA_MODULE_TAG TUA9001_EXTRA_MODULE;

// Extra manipulaing function
typedef int
(*TUA9001_FP_SET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

typedef int
(*TUA9001_FP_GET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);

typedef int
(*TUA9001_FP_GET_REG_BYTES_WITH_REG_ADDR)(
	TUNER_MODULE *pTuner,
	unsigned char DeviceAddr,
	unsigned char RegAddr,
	unsigned char *pReadingByte,
	unsigned char ByteNum
	);

typedef int
(*TUA9001_FP_SET_SYS_REG_BYTE)(
	TUNER_MODULE *pTuner,
	unsigned short RegAddr,
	unsigned char WritingByte
	);

typedef int
(*TUA9001_FP_GET_SYS_REG_BYTE)(
	TUNER_MODULE *pTuner,
	unsigned short RegAddr,
	unsigned char *pReadingByte
	);

// TUA9001 extra module
struct TUA9001_EXTRA_MODULE_TAG
{
	// TUA9001 extra variables
	int BandwidthMode;
	int IsBandwidthModeSet;

	// TUA9001 extra function pointers
	TUA9001_FP_SET_BANDWIDTH_MODE            SetBandwidthMode;
	TUA9001_FP_GET_BANDWIDTH_MODE            GetBandwidthMode;
	TUA9001_FP_GET_REG_BYTES_WITH_REG_ADDR   GetRegBytesWithRegAddr;
	TUA9001_FP_SET_SYS_REG_BYTE              SetSysRegByte;
	TUA9001_FP_GET_SYS_REG_BYTE              GetSysRegByte;
};





// DTT-75300 extra module
typedef struct DTT75300_EXTRA_MODULE_TAG DTT75300_EXTRA_MODULE;
struct DTT75300_EXTRA_MODULE_TAG
{
	// DTT-75300 extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char Control2;
	unsigned char Control3;
	unsigned char Control4;
};





// MxL5007T extra module
typedef struct MXL5007T_EXTRA_MODULE_TAG MXL5007T_EXTRA_MODULE;

// MxL5007 TunerConfig Struct
typedef struct _MxL5007_TunerConfigS
{
	int	I2C_Addr;
	int	Mode;
	int	IF_Diff_Out_Level;
	int	Xtal_Freq;
	int	IF_Freq;
	int IF_Spectrum;
	int	ClkOut_Setting;
    int	ClkOut_Amp;
	int BW_MHz;
	unsigned int RF_Freq_Hz;

	// Additional definition
	TUNER_MODULE *pTuner;

} MxL5007_TunerConfigS;

// MxL5007T bandwidth mode setting function pointer
typedef int
(*MXL5007T_FP_SET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

// MxL5007T bandwidth mode getting function pointer
typedef int
(*MXL5007T_FP_GET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);

// MxL5007T extra module
struct MXL5007T_EXTRA_MODULE_TAG
{
	// MxL5007T extra variables
	int LoopThroughMode;
	int BandwidthMode;
	int IsBandwidthModeSet;

	// MxL5007T MaxLinear-defined structure
	MxL5007_TunerConfigS *pTunerConfigS;
	MxL5007_TunerConfigS TunerConfigSMemory;

	// MxL5007T extra function pointers
	MXL5007T_FP_SET_BANDWIDTH_MODE   SetBandwidthMode;
	MXL5007T_FP_GET_BANDWIDTH_MODE   GetBandwidthMode;
};





// VA1T1ED6093 extra module
typedef struct VA1T1ED6093_EXTRA_MODULE_TAG VA1T1ED6093_EXTRA_MODULE;
struct VA1T1ED6093_EXTRA_MODULE_TAG
{
	// VA1T1ED6093 extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char Control2;
	unsigned char Control3;
};





/// TUA8010 extra module
typedef struct TUA8010_EXTRA_MODULE_TAG TUA8010_EXTRA_MODULE;

// Extra manipulaing function
typedef int
(*TUA8010_FP_SET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

typedef int
(*TUA8010_FP_GET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);

typedef int
(*TUA8010_FP_GET_REG_BYTES_WITH_REG_ADDR)(
	TUNER_MODULE *pTuner,
	unsigned char DeviceAddr,
	unsigned char RegAddr,
	unsigned char *pReadingByte,
	unsigned char ByteNum
	);

// TUA8010 extra module
struct TUA8010_EXTRA_MODULE_TAG
{
	// TUA8010 extra variables
	int BandwidthMode;
	int IsBandwidthModeSet;

	// TUA8010 extra function pointers
	TUA8010_FP_SET_BANDWIDTH_MODE            SetBandwidthMode;
	TUA8010_FP_GET_BANDWIDTH_MODE            GetBandwidthMode;
	TUA8010_FP_GET_REG_BYTES_WITH_REG_ADDR   GetRegBytesWithRegAddr;
};





// E4000 extra module
typedef struct E4000_EXTRA_MODULE_TAG E4000_EXTRA_MODULE;

// E4000 register byte getting function pointer
typedef int
(*E4000_FP_GET_REG_BYTE)(
	TUNER_MODULE *pTuner,
	unsigned char RegAddr,
	unsigned char *pReadingByte
	);

// E4000 bandwidth Hz setting function pointer
typedef int
(*E4000_FP_SET_BANDWIDTH_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long BandwidthHz
	);

// E4000 bandwidth Hz getting function pointer
typedef int
(*E4000_FP_GET_BANDWIDTH_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long *pBandwidthHz
	);

// E4000 extra module
struct E4000_EXTRA_MODULE_TAG
{
	// E4000 extra variables
	unsigned long CrystalFreqHz;
	int BandwidthHz;
	int IsBandwidthHzSet;

	// E4000 extra function pointers
	E4000_FP_GET_REG_BYTE       GetRegByte;
	E4000_FP_SET_BANDWIDTH_HZ   SetBandwidthHz;
	E4000_FP_GET_BANDWIDTH_HZ   GetBandwidthHz;
};





// E4005 extra module
typedef struct E4005_EXTRA_MODULE_TAG E4005_EXTRA_MODULE;

// E4005 extra module
struct E4005_EXTRA_MODULE_TAG
{
	// E4005 extra variables
	unsigned int TunerIfFreq;
	unsigned int TunerIfMode;
	unsigned int BandwidthHz;
	unsigned int CrystalFreqHz;
	unsigned int IsBandwidthHzSet;
	unsigned int TunerGainMode;
	unsigned int TunerCoding;
};





// DCT-70704 extra module
typedef struct DCT70704_EXTRA_MODULE_TAG DCT70704_EXTRA_MODULE;
struct DCT70704_EXTRA_MODULE_TAG
{
	// DCT-70704 extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char BandSwitch;
	unsigned char Control2;
};





/// MT2063 extra module
typedef struct MT2063_EXTRA_MODULE_TAG MT2063_EXTRA_MODULE;

// MT2063 handle openning function pointer
typedef int
(*MT2063_FP_OPEN_HANDLE)(
	TUNER_MODULE *pTuner
	);

// MT2063 handle closing function pointer
typedef int
(*MT2063_FP_CLOSE_HANDLE)(
	TUNER_MODULE *pTuner
	);

// MT2063 handle getting function pointer
typedef void
(*MT2063_FP_GET_HANDLE)(
	TUNER_MODULE *pTuner,
	void **pDeviceHandle
	);

// MT2063 IF frequency setting function pointer
typedef int
(*MT2063_FP_SET_IF_FREQ_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long IfFreqHz
	);

// MT2063 IF frequency getting function pointer
typedef int
(*MT2063_FP_GET_IF_FREQ_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long *pIfFreqHz
	);

// MT2063 bandwidth setting function pointer
typedef int
(*MT2063_FP_SET_BANDWIDTH_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long BandwidthHz
	);

// MT2063 bandwidth getting function pointer
typedef int
(*MT2063_FP_GET_BANDWIDTH_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long *pBandwidthHz
	);

// MT2063 extra module
struct MT2063_EXTRA_MODULE_TAG
{
	// MT2063 extra variables
	void *DeviceHandle;
	int StandardMode;
	unsigned long VgaGc;

	unsigned long IfFreqHz;
	unsigned long BandwidthHz;

	int IsIfFreqHzSet;
	int IsBandwidthHzSet;

	// MT2063 extra function pointers
	MT2063_FP_OPEN_HANDLE        OpenHandle;
	MT2063_FP_CLOSE_HANDLE       CloseHandle;
	MT2063_FP_GET_HANDLE         GetHandle;
	MT2063_FP_SET_IF_FREQ_HZ     SetIfFreqHz;
	MT2063_FP_GET_IF_FREQ_HZ     GetIfFreqHz;
	MT2063_FP_SET_BANDWIDTH_HZ   SetBandwidthHz;
	MT2063_FP_GET_BANDWIDTH_HZ   GetBandwidthHz;
};





// FC0012 extra module
typedef struct FC0012_EXTRA_MODULE_TAG FC0012_EXTRA_MODULE;

// FC0012 bandwidth mode setting function pointer
typedef int
(*FC0012_FP_SET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

// FC0012 bandwidth mode getting function pointer
typedef int
(*FC0012_FP_GET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);

// FC0012 extra module
struct FC0012_EXTRA_MODULE_TAG
{
	// FC0012 extra variables
	unsigned long CrystalFreqHz;
	int BandwidthMode;
	int IsBandwidthModeSet;

	// FC0012 extra function pointers
	FC0012_FP_SET_BANDWIDTH_MODE   SetBandwidthMode;
	FC0012_FP_GET_BANDWIDTH_MODE   GetBandwidthMode;
};





// TDAG extra module
typedef struct TDAG_EXTRA_MODULE_TAG TDAG_EXTRA_MODULE;
struct TDAG_EXTRA_MODULE_TAG
{
	// TDAG extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char Control2;
	unsigned char Control3;
};





// ADMTV804 extra module
typedef struct ADMTV804_EXTRA_MODULE_TAG ADMTV804_EXTRA_MODULE;

// ADMTV804 standard bandwidth mode setting function pointer
typedef int
(*ADMTV804_FP_SET_STANDARD_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

// ADMTV804 standard bandwidth mode getting function pointer
typedef int
(*ADMTV804_FP_GET_STANDARD_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);

// ADMTV804 RF power level getting function pointer
typedef int
(*ADMTV804_FP_GET_RF_POWER_LEVEL)(
	TUNER_MODULE *pTuner,
	long *pRfPowerLevel
	);

// ADMTV804 extra module
struct ADMTV804_EXTRA_MODULE_TAG
{
	// ADMTV804 extra variables (from ADMTV804 source code)
	unsigned char REV_INFO;
	unsigned char g_curBand;
	unsigned char pddiv3;
	unsigned char REG24;
	unsigned char REG2B;
	unsigned char REG2E;
	unsigned char REG30;
	unsigned char REG31;
	unsigned char REG5A;
	unsigned char REG61;  // V1.6 added 

	// ADMTV804 extra variables
	unsigned long CrystalFreqHz;
	int StandardBandwidthMode;
	int IsStandardBandwidthModeSet;
	long RfPowerLevel;

	// ADMTV804 extra function pointers
	ADMTV804_FP_SET_STANDARD_BANDWIDTH_MODE   SetStandardBandwidthMode;
	ADMTV804_FP_GET_STANDARD_BANDWIDTH_MODE   GetStandardBandwidthMode;
	ADMTV804_FP_GET_RF_POWER_LEVEL            GetRfPowerLevel;
};





// MAX3543 extra module
typedef struct MAX3543_EXTRA_MODULE_TAG MAX3543_EXTRA_MODULE;

// MAX3543 bandwidth mode setting function pointer
typedef int
(*MAX3543_FP_SET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

// MAX3543 bandwidth mode getting function pointer
typedef int
(*MAX3543_FP_GET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);

// MAX3543 extra module
struct MAX3543_EXTRA_MODULE_TAG
{
	// MAX3543 extra variables (from MAX3543 source code)
	unsigned short TFRomCoefs[3][4];
	unsigned short denominator;   
	unsigned long  fracscale ;
	unsigned short regs[22];
	unsigned short IF_Frequency;

	int broadcast_standard;
	int XTALSCALE;
	int XTALREF;
	int LOSCALE;


	// MAX3543 extra variables
	unsigned long CrystalFreqHz;
	int StandardMode;
	unsigned long IfFreqHz;
	int OutputMode;
	int BandwidthMode;
	int IsBandwidthModeSet;

	// MAX3543 extra function pointers
	MAX3543_FP_SET_BANDWIDTH_MODE   SetBandwidthMode;
	MAX3543_FP_GET_BANDWIDTH_MODE   GetBandwidthMode;
};





// TDA18272 extra module
typedef struct TDA18272_EXTRA_MODULE_TAG TDA18272_EXTRA_MODULE;

// TDA18272 standard bandwidth mode setting function pointer
typedef int
(*TDA18272_FP_SET_STANDARD_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int StandardBandwidthMode
	);

// TDA18272 standard bandwidth mode getting function pointer
typedef int
(*TDA18272_FP_GET_STANDARD_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pStandardBandwidthMode
	);

// TDA18272 power mode setting function pointer
typedef int
(*TDA18272_FP_SET_POWER_MODE)(
	TUNER_MODULE *pTuner,
	int PowerMode
	);

// TDA18272 power mode getting function pointer
typedef int
(*TDA18272_FP_GET_POWER_MODE)(
	TUNER_MODULE *pTuner,
	int *pPowerMode
	);

// TDA18272 IF frequency getting function pointer
typedef int
(*TDA18272_FP_GET_IF_FREQ_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long *pIfFreqHz
	);

// TDA18272 extra module
struct TDA18272_EXTRA_MODULE_TAG
{
	// TDA18272 extra variables
	unsigned long CrystalFreqHz;
	int UnitNo;
	int IfOutputVppMode;
	int StandardBandwidthMode;
	int IsStandardBandwidthModeSet;
	int PowerMode;
	int IsPowerModeSet;

	// TDA18272 extra function pointers
	TDA18272_FP_SET_STANDARD_BANDWIDTH_MODE   SetStandardBandwidthMode;
	TDA18272_FP_GET_STANDARD_BANDWIDTH_MODE   GetStandardBandwidthMode;
	TDA18272_FP_SET_POWER_MODE                SetPowerMode;
	TDA18272_FP_GET_POWER_MODE                GetPowerMode;
	TDA18272_FP_GET_IF_FREQ_HZ                GetIfFreqHz;
};





// TDA18250 extra module
typedef struct TDA18250_EXTRA_MODULE_TAG TDA18250_EXTRA_MODULE;

// TDA18250 standard bandwidth mode setting function pointer
typedef int
(*TDA18250_FP_SET_STANDARD_MODE)(
	TUNER_MODULE *pTuner,
	int StandardMode
	);

// TDA18250 standard bandwidth mode getting function pointer
typedef int
(*TDA18250_FP_GET_STANDARD_MODE)(
	TUNER_MODULE *pTuner,
	int *pStandardMode
	);

// TDA18250 power mode setting function pointer
typedef int
(*TDA18250_FP_SET_POWER_STATE)(
	TUNER_MODULE *pTuner,
	int PowerState
	);

// TDA18250 power mode getting function pointer
typedef int
(*TDA18250_FP_GET_POWER_STATE)(
	TUNER_MODULE *pTuner,
	int *pPowerState
	);

// TDA18250 IF frequency getting function pointer
typedef int
(*TDA18250_FP_GET_IF_FREQ_HZ)(
	TUNER_MODULE *pTuner,
	unsigned long *pIfFreqHz
	);

// TDA18250 PowerLevel getting function pointer
typedef int
(*TDA18250_FP_Get_PowerLevel)(
	TUNER_MODULE *pTuner,
	unsigned long *pPowerLevel
	);

// TDA18250 PowerLevel getting function pointer
typedef int
(*TDA18250_FP_Reset_AGC)(
	TUNER_MODULE *pTuner
	);
	
// TDA18250 extra module
struct TDA18250_EXTRA_MODULE_TAG
{
	// TDA18250 extra variables
	unsigned long CrystalFreqHz;
	int UnitNo;
	int IfOutputVppMode;
	int StandardMode;
	int IsStandardModeSet;
	int PowerState;
	int IsPowerStateSet;

	// TDA18250 extra function pointers
	TDA18250_FP_SET_STANDARD_MODE   	SetStandardMode;
	TDA18250_FP_GET_STANDARD_MODE   	GetStandardMode;
	TDA18250_FP_SET_POWER_STATE         SetPowerState;
	TDA18250_FP_GET_POWER_STATE         GetPowerState;
	TDA18250_FP_GET_IF_FREQ_HZ          GetIfFreqHz;
	TDA18250_FP_Get_PowerLevel			GetPowerLevel;
	TDA18250_FP_Reset_AGC				ResetAGC;
};





// FC0013 extra module
typedef struct FC0013_EXTRA_MODULE_TAG FC0013_EXTRA_MODULE;

// FC0013 bandwidth mode setting function pointer
typedef int
(*FC0013_FP_SET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

// FC0013 bandwidth mode getting function pointer
typedef int
(*FC0013_FP_GET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);

// FC0013 reset IQ LPF BW
typedef int
(*FC0013_FP_RC_CAL_RESET)(
	TUNER_MODULE *pTuner
	);

// FC0013 increase IQ LPF BW
typedef int
(*FC0013_FP_RC_CAL_ADD)(
	TUNER_MODULE *pTuner,
	int RcValue
	);

// FC0013 extra module
struct FC0013_EXTRA_MODULE_TAG
{
	// FC0013 extra variables
	unsigned long CrystalFreqHz;
	int BandwidthMode;
	int IsBandwidthModeSet;

	// FC0013 extra function pointers
	FC0013_FP_SET_BANDWIDTH_MODE   SetBandwidthMode;
	FC0013_FP_GET_BANDWIDTH_MODE   GetBandwidthMode;
	FC0013_FP_RC_CAL_RESET         RcCalReset;
	FC0013_FP_RC_CAL_ADD           RcCalAdd;
};





// VA1E1ED2403 extra module
typedef struct VA1E1ED2403_EXTRA_MODULE_TAG VA1E1ED2403_EXTRA_MODULE;
struct VA1E1ED2403_EXTRA_MODULE_TAG
{
	// VA1E1ED2403 extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char Control2;
	unsigned char Control3;
};





// Avalon extra module
#define AVALON_RF_REG_MAX_ADDR		0x60
#define AVALON_BB_REG_MAX_ADDR		0x4f

typedef struct _AVALON_CONFIG_STRUCT {
	double RFChannelMHz;
	double dIfFrequencyMHz;
	double f_dac_MHz;
	int TVStandard;
} AVALON_CONFIG_STRUCT, *PAVALON_CONFIG_STRUCT;

typedef struct AVALON_EXTRA_MODULE_TAG AVALON_EXTRA_MODULE;

// AVALON standard bandwidth mode setting function pointer
typedef int
(*AVALON_FP_SET_STANDARD_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int StandardBandwidthMode
	);

// AVALON standard bandwidth mode getting function pointer
typedef int
(*AVALON_FP_GET_STANDARD_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pStandardBandwidthMode
	);

// AVALON extra module
struct AVALON_EXTRA_MODULE_TAG
{
	// AVALON extra variables (from AVALON source code)
	unsigned char g_avalon_rf_reg_work_table[AVALON_RF_REG_MAX_ADDR+1];
	unsigned char g_avalon_bb_reg_work_table[AVALON_BB_REG_MAX_ADDR+1];

	int
	(*g_avalon_i2c_read_handler)(
		TUNER_MODULE *pTuner,
		unsigned char DeviceAddr,
		unsigned char *pReadingBuffer,
		unsigned short *pByteNum
		);

	int
	(*g_avalon_i2c_write_handler)(
		TUNER_MODULE *pTuner,
		unsigned char DeviceAddr,
		unsigned char *pWritingBuffer,
		unsigned short *pByteNum
		);

	AVALON_CONFIG_STRUCT AvalonConfig;


	// AVALON extra variables
	unsigned char AtvDemodDeviceAddr;
	unsigned long CrystalFreqHz;
	unsigned long IfFreqHz;
	int StandardBandwidthMode;
	int IsStandardBandwidthModeSet;

	// AVALON extra function pointers
	AVALON_FP_SET_STANDARD_BANDWIDTH_MODE   SetStandardBandwidthMode;
	AVALON_FP_GET_STANDARD_BANDWIDTH_MODE   GetStandardBandwidthMode;
};





// SutRx201 extra module
typedef struct SUTRE201_EXTRA_MODULE_TAG SUTRE201_EXTRA_MODULE;

// SUTRE201 standard bandwidth mode setting function pointer
typedef int
(*SUTRE201_FP_SET_STANDARD_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int StandardBandwidthMode
	);

// SUTRE201 standard bandwidth mode getting function pointer
typedef int
(*SUTRE201_FP_GET_STANDARD_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pStandardBandwidthMode
	);

// SUTRE201 tuner enabling function pointer
typedef int
(*SUTRE201_FP_ENABLE)(
	TUNER_MODULE *pTuner
	);

// SUTRE201 tuner disabling function pointer
typedef int
(*SUTRE201_FP_DISABLE)(
	TUNER_MODULE *pTuner
	);

// SUTRE201 tuner IF port mode setting function pointer
typedef int
(*SUTRE201_FP_SET_IF_PORT_MODE)(
	TUNER_MODULE *pTuner,
	int IfPortMode
	);

// SUTRE201 extra module
struct SUTRE201_EXTRA_MODULE_TAG
{
	// SUTRE201 extra variables
	unsigned long CrystalFreqHz;
	unsigned long IfFreqHz;
	int IfPortMode;
	int CountryMode;
	int StandardBandwidthMode;
	int IsStandardBandwidthModeSet;

	// SUTRE201 extra function pointers
	SUTRE201_FP_SET_STANDARD_BANDWIDTH_MODE   SetStandardBandwidthMode;
	SUTRE201_FP_GET_STANDARD_BANDWIDTH_MODE   GetStandardBandwidthMode;
	SUTRE201_FP_ENABLE                        Enable;
	SUTRE201_FP_DISABLE                       Disable;
	SUTRE201_FP_SET_IF_PORT_MODE              SetIfPortMode;
};





// MR1300 extra module
typedef struct MR1300_EXTRA_MODULE_TAG MR1300_EXTRA_MODULE;
struct MR1300_EXTRA_MODULE_TAG
{
	// MR1300 extra data
	unsigned long CrystalFreqHz;
};





// TDAC7 extra module
typedef struct TDAC7_EXTRA_MODULE_TAG TDAC7_EXTRA_MODULE;
struct TDAC7_EXTRA_MODULE_TAG
{
	// TDAC7 extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char Control2;
	unsigned char Control3;
};





// VA1T1ER2094 extra module
typedef struct VA1T1ER2094_EXTRA_MODULE_TAG VA1T1ER2094_EXTRA_MODULE;
struct VA1T1ER2094_EXTRA_MODULE_TAG
{
	// VA1T1ER2094 extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char Control2;
	unsigned char Control3;
};





// TDAC3 extra module
typedef struct TDAC3_EXTRA_MODULE_TAG TDAC3_EXTRA_MODULE;
struct TDAC3_EXTRA_MODULE_TAG
{
	// TDAC3 extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char Control2;
	unsigned char Control3;
};





// RT910 extra module
#define RT910_INITIAL_TABLE_LENGTH     25
typedef struct RT910_EXTRA_MODULE_TAG RT910_EXTRA_MODULE;

// RT910 bandwidth mode setting function pointer
typedef int
(*RT910_FP_SET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

// RT910 bandwidth mode getting function pointer
typedef int
(*RT910_FP_GET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);

// RT910 extra module
struct RT910_EXTRA_MODULE_TAG
{
	// RT910 extra variables
	unsigned long CrystalFreqHz;
	int BandwidthMode;
	int IsBandwidthModeSet;
	unsigned char RT910RegsMap[RT910_INITIAL_TABLE_LENGTH];
	unsigned char RT910BW8MCalib[RT910_INITIAL_TABLE_LENGTH];  // 80MHz for C.F. cali.


	// RT910 extra function pointers
	RT910_FP_SET_BANDWIDTH_MODE   SetBandwidthMode;
	RT910_FP_GET_BANDWIDTH_MODE   GetBandwidthMode;
};





// DTM4C20 extra module
typedef struct DTM4C20_EXTRA_MODULE_TAG DTM4C20_EXTRA_MODULE;
struct DTM4C20_EXTRA_MODULE_TAG
{
	// DTM4C20 extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char Control2;
	unsigned char Control3;
};





// GTFD32 extra module
typedef struct GTFD32_EXTRA_MODULE_TAG GTFD32_EXTRA_MODULE;
struct GTFD32_EXTRA_MODULE_TAG
{
	// GTFD32 extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char Control2;
	unsigned char Control3;
};





// GTLP10 extra module
typedef struct GTLP10_EXTRA_MODULE_TAG GTLP10_EXTRA_MODULE;
struct GTLP10_EXTRA_MODULE_TAG
{
	// GTLP10 extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char Control2;
	unsigned char Control3;
};





// JSS66T extra module
typedef struct JSS66T_EXTRA_MODULE_TAG JSS66T_EXTRA_MODULE;
struct JSS66T_EXTRA_MODULE_TAG
{
	// JSS66T extra data
	unsigned char DividerMsb;
	unsigned char DividerLsb;
	unsigned char Control1;
	unsigned char Control2;
	unsigned char Control3;
};





// FC0013B extra module
typedef struct FC0013B_EXTRA_MODULE_TAG FC0013B_EXTRA_MODULE;

// FC0013B bandwidth mode setting function pointer
typedef int
(*FC0013B_FP_SET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

// FC0013B bandwidth mode getting function pointer
typedef int
(*FC0013B_FP_GET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);

// FC0013B reset IQ LPF BW
typedef int
(*FC0013B_FP_RC_CAL_RESET)(
	TUNER_MODULE *pTuner
	);

// FC0013B increase IQ LPF BW
typedef int
(*FC0013B_FP_RC_CAL_ADD)(
	TUNER_MODULE *pTuner,
	int RcValue
	);

// FC0013B extra module
struct FC0013B_EXTRA_MODULE_TAG
{
	// FC0013B extra variables
	unsigned long CrystalFreqHz;
	int BandwidthMode;
	int IsBandwidthModeSet;

	// FC0013B extra function pointers
	FC0013B_FP_SET_BANDWIDTH_MODE   SetBandwidthMode;
	FC0013B_FP_GET_BANDWIDTH_MODE   GetBandwidthMode;
	FC0013B_FP_RC_CAL_RESET         RcCalReset;
	FC0013B_FP_RC_CAL_ADD           RcCalAdd;
};





// MR1500 extra module
typedef struct MR1500_EXTRA_MODULE_TAG MR1500_EXTRA_MODULE;

// MR1500 bandwidth mode setting function pointer
typedef int
(*MR1500_FP_SET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

// MR1500 bandwidth mode getting function pointer
typedef int
(*MR1500_FP_GET_BANDWIDTH_MODE)(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);

struct MR1500_EXTRA_MODULE_TAG
{
	// MR1500 extra data
	unsigned long CrystalFreqHz;
	int BandwidthMode;
	int IsBandwidthModeSet;

	// MR1500 extra function pointers
	MR1500_FP_SET_BANDWIDTH_MODE   SetBandwidthMode;
	MR1500_FP_GET_BANDWIDTH_MODE   GetBandwidthMode;
};







// R820T extra module
typedef struct R820T_EXTRA_MODULE_TAG R820T_EXTRA_MODULE;


// R820T standard mode setting function pointer
typedef int
(*R820T_FP_SET_STANDARD_MODE)(
	TUNER_MODULE *pTuner,
	int StandardMode
	);

// R820T standard mode getting function pointer
typedef int
(*R820T_FP_GET_STANDARD_MODE)(
	TUNER_MODULE *pTuner,
	int *pStandardMode
	);

// R820T standby setting function pointer
typedef int
(*R820T_FP_SET_STANDBY)(
	TUNER_MODULE *pTuner,
	int LoopThroughType
	);




struct R820T_EXTRA_MODULE_TAG
{

	unsigned char Rafael_Chip;

	// R820T extra variables
	unsigned long IfFreqHz;
	int BandwidthMode;
	int IsBandwidthModeSet;

	int StandardMode;
	int IsStandardModeSet;

	unsigned long CrystalFreqkHz;	

	// R820T extra function pointers
	R820T_FP_SET_STANDARD_MODE   SetStandardMode;
	R820T_FP_GET_STANDARD_MODE   GetStandardMode;
	
	R820T_FP_SET_STANDBY   SetStandby;
	
};




/// Tuner module structure
struct TUNER_MODULE_TAG
{
	// Private variables
	int           TunerType;									///<   Tuner type
	unsigned char DeviceAddr;									///<   Tuner I2C device address
	unsigned long RfFreqHz;										///<   Tuner RF frequency in Hz

	int IsRfFreqHzSet;											///<   Tuner RF frequency in Hz (setting status)

	union														///<   Tuner extra module used by driving module
	{
		TDCGG052D_EXTRA_MODULE   Tdcgg052d;
		TDCHG001D_EXTRA_MODULE   Tdchg001d;
		TDQE3003A_EXTRA_MODULE   Tdqe3003a;
		DCT7045_EXTRA_MODULE     Dct7045;
		MT2062_EXTRA_MODULE      Mt2062;
		MXL5005S_EXTRA_MODULE    Mxl5005s;
		TDVMH751P_EXTRA_MODULE   Tdvmh751p;
		UBA00AL_EXTRA_MODULE     Uba00al;
		MT2266_EXTRA_MODULE      Mt2266;
		FC2580_EXTRA_MODULE      Fc2580;
		TUA9001_EXTRA_MODULE     Tua9001;
		DTT75300_EXTRA_MODULE    Dtt75300;
		MXL5007T_EXTRA_MODULE    Mxl5007t;
		VA1T1ED6093_EXTRA_MODULE Va1t1ed6093;
		TUA8010_EXTRA_MODULE     Tua8010;
		E4000_EXTRA_MODULE       E4000;
		E4005_EXTRA_MODULE       E4005;
		DCT70704_EXTRA_MODULE    Dct70704;
		MT2063_EXTRA_MODULE      Mt2063;
		FC0012_EXTRA_MODULE      Fc0012;
		TDAG_EXTRA_MODULE        Tdag;
		ADMTV804_EXTRA_MODULE    Admtv804;
		MAX3543_EXTRA_MODULE     Max3543;
		TDA18272_EXTRA_MODULE    Tda18272;
		TDA18250_EXTRA_MODULE    Tda18250;
		FC0013_EXTRA_MODULE      Fc0013;
		VA1E1ED2403_EXTRA_MODULE Va1e1ed2403;
		AVALON_EXTRA_MODULE      Avalon;
		SUTRE201_EXTRA_MODULE    Sutre201;
		MR1300_EXTRA_MODULE      Mr1300;
		TDAC7_EXTRA_MODULE       Tdac7;
		VA1T1ER2094_EXTRA_MODULE Va1t1er2094;
		TDAC3_EXTRA_MODULE       Tdac3;
		RT910_EXTRA_MODULE       Rt910;
		DTM4C20_EXTRA_MODULE     Dtm4c20;
		GTFD32_EXTRA_MODULE      Gtfd32;
		GTLP10_EXTRA_MODULE      Gtlp10;
		JSS66T_EXTRA_MODULE      Jss66t;
		FC0013B_EXTRA_MODULE     Fc0013b;
		MR1500_EXTRA_MODULE      Mr1500;
		R820T_EXTRA_MODULE       R820t;
	}
	Extra;

	BASE_INTERFACE_MODULE *pBaseInterface;						///<   Base interface module
	I2C_BRIDGE_MODULE *pI2cBridge;								///<   I2C bridge module


	// Tuner manipulating functions
	TUNER_FP_GET_TUNER_TYPE    GetTunerType;				///<   Tuner type getting function pointer
	TUNER_FP_GET_DEVICE_ADDR   GetDeviceAddr;				///<   Tuner I2C device address getting function pointer

	TUNER_FP_INITIALIZE        Initialize;					///<   Tuner initializing function pointer
	TUNER_FP_SET_RF_FREQ_HZ    SetRfFreqHz;					///<   Tuner RF frequency setting function pointer
	TUNER_FP_GET_RF_FREQ_HZ    GetRfFreqHz;					///<   Tuner RF frequency getting function pointer
};

















#endif
