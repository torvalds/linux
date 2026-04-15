/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef SMU_15_0_8_DRIVER_IF_H
#define SMU_15_0_8_DRIVER_IF_H

//I2C Interface
#define NUM_I2C_CONTROLLERS                8
#define I2C_CONTROLLER_ENABLED             1
#define I2C_CONTROLLER_DISABLED            0

#define MAX_SW_I2C_COMMANDS                24

typedef enum {
	I2C_CONTROLLER_PORT_0,
	I2C_CONTROLLER_PORT_COUNT,
} I2cControllerPort_e;

typedef enum {
	/* 50  Kbits/s not supported anymore! */
	UNSUPPORTED_1,
	/* 100 Kbits/s */
	I2C_SPEED_STANDARD_100K,
	/* 400 Kbits/s */
	I2C_SPEED_FAST_400K,
	/* 1   Mbits/s (in fast mode) */
	I2C_SPEED_FAST_PLUS_1M,
	/* 1   Mbits/s (in high speed mode)  not supported anymore!*/
	UNSUPPORTED_2,
	/* 2.3 Mbits/s  not supported anymore! */
	UNSUPPORTED_3,
	I2C_SPEED_COUNT,
} I2cSpeed_e;

typedef enum {
	I2C_CMD_READ,
	I2C_CMD_WRITE,
	I2C_CMD_COUNT,
} I2cCmdType_e;

#define CMDCONFIG_STOP_BIT             0
#define CMDCONFIG_RESTART_BIT          1
/* bit should be 0 for read, 1 for write */
#define CMDCONFIG_READWRITE_BIT        2

#define CMDCONFIG_STOP_MASK           (1 << CMDCONFIG_STOP_BIT)
#define CMDCONFIG_RESTART_MASK        (1 << CMDCONFIG_RESTART_BIT)
#define CMDCONFIG_READWRITE_MASK      (1 << CMDCONFIG_READWRITE_BIT)

/* 64 Bit register offsets for PPSMC_MSG_McaBankDumpDW, PPSMC_MSG_McaBankCeDumpDW messages
 * eg to read MCA_BANK_OFFSET_SYND for CE index, call PPSMC_MSG_McaBankCeDumpDW twice,
 * (index << 16 + MCA_BANK_OFFSET_SYND*8) argument for 1st DWORD, and
 * ((index << 16 ) + MCA_BANK_OFFSET_SYND*8 + 4) argument for 2nd DWORD */
typedef enum {
	MCA_BANK_OFFSET_CTL 		= 0,
	MCA_BANK_OFFSET_STATUS 		= 1,
	MCA_BANK_OFFSET_ADDR 		= 2,
	MCA_BANK_OFFSET_MISC 		= 3,
	MCA_BANK_OFFSET_IPID 		= 5,
	MCA_BANK_OFFSET_SYND 		= 6,
	MCA_BANK_OFFSET_MAX 		= 16,
} MCA_BANK_OFFSET_e;

/* Firmware MP1 AID MCA Error Codes stored in MCA_MP_MP1:MCMP1_SYNDT0 errorinformation */
typedef enum {
	/* MMHUB */
	CODE_DAGB0        = 0,
	CODE_DAGB1        = 1,
	CODE_DAGB2        = 2,
	CODE_DAGB3        = 3,
	CODE_DAGB4        = 4,
	CODE_EA0          = 5,
	CODE_EA1          = 6,
	CODE_EA2          = 7,
	CODE_EA3          = 8,
	CODE_EA4          = 9,
	CODE_UTCL2_ROUTER = 10,
	CODE_VML2         = 11,
	CODE_VML2_WALKER  = 12,
	CODE_MMCANE       = 13,

	/* VCN VCPU */
	CODE_VIDD         = 14,
	CODE_VIDV         = 15,
	/* VCN JPEG */
	CODE_JPEG0S       = 16,
	CODE_JPEG0D       = 17,
	CODE_JPEG1S       = 18,
	CODE_JPEG1D       = 19,
	CODE_JPEG2S       = 20,
	CODE_JPEG2D       = 21,
	CODE_JPEG3S       = 22,
	CODE_JPEG3D       = 23,
	CODE_JPEG4S       = 24,
	CODE_JPEG4D       = 25,
	CODE_JPEG5S       = 26,
	CODE_JPEG5D       = 27,
	CODE_JPEG6S       = 28,
	CODE_JPEG6D       = 29,
	CODE_JPEG7S       = 30,
	CODE_JPEG7D       = 31,
	/* VCN MMSCH */
	CODE_MMSCHD       = 32,

	/* SDMA */
	CODE_SDMA0        = 33,
	CODE_SDMA1        = 34,
	CODE_SDMA2        = 35,
	CODE_SDMA3        = 36,

	/* SOC */
	CODE_HDP          = 37,
	CODE_ATHUB        = 38,
	CODE_IH           = 39,
	CODE_XHUB_POISON  = 40,
	CODE_SMN_SLVERR   = 41,
	CODE_WDT          = 42,

	CODE_UNKNOWN      = 43,
	CODE_DMA          = 44,
	CODE_COUNT        = 45,
} ERR_CODE_e;

/* Firmware MP5 XCD MCA Error Codes stored in MCA_MP_MP5:MCMP5_SYNDT0 errorinformation */
typedef enum {
	/* SH POISON FED */
	SH_FED_CODE      = 0,
	/* GCEA Pin UE_ERR regs */
	GCEA_CODE        = 1,
	SQ_CODE          = 2,
	LDS_CODE         = 3,
	GDS_CODE         = 4,
	SP0_CODE         = 5,
	SP1_CODE         = 6,
	TCC_CODE         = 7,
	TCA_CODE         = 8,
	TCX_CODE         = 9,
	CPC_CODE         = 10,
	CPF_CODE         = 11,
	CPG_CODE         = 12,
	SPI_CODE         = 13,
	RLC_CODE         = 14,
	/* GCEA Pin, UE_EDC regs */
	SQC_CODE         = 15,
	TA_CODE          = 16,
	TD_CODE          = 17,
	TCP_CODE         = 18,
	TCI_CODE         = 19,
	/* GC Router */
	GC_ROUTER_CODE   = 20,
	VML2_CODE        = 21,
	VML2_WALKER_CODE = 22,
	ATCL2_CODE       = 23,
	GC_CANE_CODE     = 24,

	/* SOC error codes 41-43 are common with ERR_CODE_e */
	MP5_CODE_SMN_SLVERR = CODE_SMN_SLVERR,
	MP5_CODE_UNKNOWN = CODE_UNKNOWN,
} GC_ERROR_CODE_e;

/* SW I2C Command Table */
typedef struct {
	/* Return data for read. Data to send for write*/
	uint8_t ReadWriteData;
	/* Includes whether associated command should have a stop or restart command,
	 * and is a read or write */
	uint8_t CmdConfig;
} SwI2cCmd_t;

/* SW I2C Request Table */
typedef struct {
	/* CKSVII2C0(0) or //CKSVII2C1(1) */
	uint8_t    I2CcontrollerPort;
	/* Use I2cSpeed_e to indicate speed to select */
	uint8_t    I2CSpeed;
	/* Slave address of device */
	uint8_t    SlaveAddress;
	/* Number of commands */
	uint8_t    NumCmds;
	SwI2cCmd_t SwI2cCmds[MAX_SW_I2C_COMMANDS];
} SwI2cRequest_t;

typedef struct {
	SwI2cRequest_t SwI2cRequest;
	uint32_t       Spare[8];
	/* SMU internal use */
	uint32_t       MmHubPadding[8];
} SwI2cRequestExternal_t;

typedef enum {
  PPCLK_UCLK,
  PPCLK_COUNT,
} PPCLK_e;

typedef enum {
	GPIO_INT_POLARITY_ACTIVE_LOW,
	GPIO_INT_POLARITY_ACTIVE_HIGH,
} GpioIntPolarity_e;

/* TODO confirm if this is used in MI300 PPSMC_MSG_SetUclkDpmMode */
typedef enum {
	UCLK_DPM_MODE_BANDWIDTH,
	UCLK_DPM_MODE_LATENCY,
} UCLK_DPM_MODE_e;

typedef struct {
	/* 2 AVFS.PSM chains */
	uint16_t  AvgPsmCount_Chain0[13];
	uint16_t  AvgPsmCount_Chain1[15];
	uint16_t  MinPsmCount_Chain0[13];
	uint16_t  MinPsmCount_Chain1[15];
	float     MaxTemperature;

	/* For voltage conversions, these are the array indexes
	 * 0:SOCIO
	 * 1:065_UCIE
	 * 2:075_UCIE
	 * 3:11_GTA
	 * 4:075_GTA */
	float     MinPsmVoltage[5];
	float     AvgPsmVoltage[5];
} AvfsDebugTableMid_t;

typedef struct {
	/* 7 AVFS.PSM chains - not including TRO */
	uint16_t  AvgPsmCount_Chain0[15];
	uint16_t  AvgPsmCount_Chain1[15];
	uint16_t  AvgPsmCount_Chain2[13];
	uint16_t  AvgPsmCount_Chain3[13];
	uint16_t  AvgPsmCount_Chain4[15];
	uint16_t  AvgPsmCount_Chain5[15];
	uint16_t  AvgPsmCount_Chain6[5];
	uint16_t  MinPsmCount_Chain0[15];
	uint16_t  MinPsmCount_Chain1[15];
	uint16_t  MinPsmCount_Chain2[13];
	uint16_t  MinPsmCount_Chain3[13];
	uint16_t  MinPsmCount_Chain4[15];
	uint16_t  MinPsmCount_Chain5[15];
	uint16_t  MinPsmCount_Chain6[5];
	float     MaxTemperature;

	/* For voltage conversions, these are the array indexes
	 * 0:VDDX */
	float     MinPsmVoltage;
	float     AvgPsmVoltage;
} AvfsDebugTableAid_t;

typedef struct {
	/* 0-27 GFX, 28-29 SOC */
	uint16_t avgPsmCount[30];
	uint16_t minPsmCount[30];
	float    avgPsmVoltage[30];
	float    minPsmVoltage[30];
} AvfsDebugTableXcd_t;

/* Defines used for IH-based thermal interrupts to GFX driver - A/X only */
#define IH_INTERRUPT_ID_TO_DRIVER                   0xFE
#define IH_INTERRUPT_CONTEXT_ID_THERMAL_THROTTLING  0x7
#define IH_INTERRUPT_VFFLR_INT                      0xA

/* thermal over-temp mask defines for IH interrup to host */
#define THROTTLER_PROCHOT_BIT           0
#define THROTTLER_RESERVED              1
/* AID, XCD, CCD throttling */
#define THROTTLER_THERMAL_SOCKET_BIT    2
/* VRHOT */
#define THROTTLER_THERMAL_VR_BIT        3
#define THROTTLER_THERMAL_HBM_BIT       4
/* UEs are always reported, set flag to 0 to prevent clearing of UEs */
#define ClearMcaOnRead_UE_FLAG_MASK              0x1
/* Enable CE logging and clearing to driver */
#define ClearMcaOnRead_CE_POLL_MASK              0x2
/* AID MMHUB client IP CE Logging and clearing */
#define ClearMcaOnRead_MMHUB_POLL_MASK           0x4

#endif
