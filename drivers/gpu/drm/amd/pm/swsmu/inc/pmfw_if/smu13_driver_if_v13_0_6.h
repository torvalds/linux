/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#ifndef SMU_13_0_6_DRIVER_IF_H
#define SMU_13_0_6_DRIVER_IF_H

// *** IMPORTANT ***
// PMFW TEAM: Always increment the interface version if
// anything is changed in this file
#define SMU13_0_6_DRIVER_IF_VERSION 0x08042024

//I2C Interface
#define NUM_I2C_CONTROLLERS                8
#define I2C_CONTROLLER_ENABLED             1
#define I2C_CONTROLLER_DISABLED            0

#define MAX_SW_I2C_COMMANDS                24

typedef enum {
  I2C_CONTROLLER_PORT_0, //CKSVII2C0
  I2C_CONTROLLER_PORT_1, //CKSVII2C1
  I2C_CONTROLLER_PORT_COUNT,
} I2cControllerPort_e;

typedef enum {
  UNSUPPORTED_1,              //50  Kbits/s not supported anymore!
  I2C_SPEED_STANDARD_100K,    //100 Kbits/s
  I2C_SPEED_FAST_400K,        //400 Kbits/s
  I2C_SPEED_FAST_PLUS_1M,     //1   Mbits/s (in fast mode)
  UNSUPPORTED_2,              //1   Mbits/s (in high speed mode)  not supported anymore!
  UNSUPPORTED_3,              //2.3 Mbits/s  not supported anymore!
  I2C_SPEED_COUNT,
} I2cSpeed_e;

typedef enum {
  I2C_CMD_READ,
  I2C_CMD_WRITE,
  I2C_CMD_COUNT,
} I2cCmdType_e;

#define CMDCONFIG_STOP_BIT             0
#define CMDCONFIG_RESTART_BIT          1
#define CMDCONFIG_READWRITE_BIT        2 //bit should be 0 for read, 1 for write

#define CMDCONFIG_STOP_MASK           (1 << CMDCONFIG_STOP_BIT)
#define CMDCONFIG_RESTART_MASK        (1 << CMDCONFIG_RESTART_BIT)
#define CMDCONFIG_READWRITE_MASK      (1 << CMDCONFIG_READWRITE_BIT)

typedef enum {
  // MMHUB
  CODE_DAGB0,
  CODE_EA0 = 5,
  CODE_UTCL2_ROUTER = 10,
  CODE_VML2,
  CODE_VML2_WALKER,
  CODE_MMCANE,

  // VCN
  // VCN VCPU
  CODE_VIDD,
  CODE_VIDV,
  // VCN JPEG
  CODE_JPEG0S,
  CODE_JPEG0D,
  CODE_JPEG1S,
  CODE_JPEG1D,
  CODE_JPEG2S,
  CODE_JPEG2D,
  CODE_JPEG3S,
  CODE_JPEG3D,
  CODE_JPEG4S,
  CODE_JPEG4D,
  CODE_JPEG5S,
  CODE_JPEG5D,
  CODE_JPEG6S,
  CODE_JPEG6D,
  CODE_JPEG7S,
  CODE_JPEG7D,
  // VCN MMSCH
  CODE_MMSCHD,

  // SDMA
  CODE_SDMA0,
  CODE_SDMA1,
  CODE_SDMA2,
  CODE_SDMA3,

  // SOC
  CODE_HDP,
  CODE_ATHUB,
  CODE_IH,
  CODE_XHUB_POISON,
  CODE_SMN_SLVERR = 40,
  CODE_WDT,

  CODE_UNKNOWN = 42,
  CODE_COUNT,
} ERR_CODE_e;

typedef enum {
  // SH POISON FED
  SH_FED_CODE,
  // GCEA Pin UE_ERR regs
  GCEA_CODE,
  SQ_CODE,
  LDS_CODE,
  GDS_CODE,
  SP0_CODE,
  SP1_CODE,
  TCC_CODE,
  TCA_CODE,
  TCX_CODE,
  CPC_CODE,
  CPF_CODE,
  CPG_CODE,
  SPI_CODE,
  RLC_CODE,
  // GCEA Pin, UE_EDC regs
  SQC_CODE,
  TA_CODE,
  TD_CODE,
  TCP_CODE,
  TCI_CODE,
  // GC Router
  GC_ROUTER_CODE,
  VML2_CODE,
  VML2_WALKER_CODE,
  ATCL2_CODE,
  GC_CANE_CODE,

  // SOC error codes 40-42 are common with ERR_CODE_e
  MP5_CODE_SMN_SLVERR = 40,
  MP5_CODE_UNKNOWN = 42,
} GC_ERROR_CODE_e;


typedef struct {
  uint8_t ReadWriteData;  //Return data for read. Data to send for write
  uint8_t CmdConfig; //Includes whether associated command should have a stop or restart command, and is a read or write
} SwI2cCmd_t; //SW I2C Command Table

typedef struct {
  uint8_t    I2CcontrollerPort; //CKSVII2C0(0) or //CKSVII2C1(1)
  uint8_t    I2CSpeed;          //Use I2cSpeed_e to indicate speed to select
  uint8_t    SlaveAddress;      //Slave address of device
  uint8_t    NumCmds;           //Number of commands
  SwI2cCmd_t SwI2cCmds[MAX_SW_I2C_COMMANDS];
} SwI2cRequest_t; // SW I2C Request Table

typedef struct {
  SwI2cRequest_t SwI2cRequest;
  uint32_t       Spare[8];
  uint32_t       MmHubPadding[8]; // SMU internal use
} SwI2cRequestExternal_t;

typedef enum {
  PPCLK_VCLK,
  PPCLK_DCLK,
  PPCLK_SOCCLK,
  PPCLK_UCLK,
  PPCLK_FCLK,
  PPCLK_LCLK,
  PPCLK_COUNT,
} PPCLK_e;

typedef enum {
  GPIO_INT_POLARITY_ACTIVE_LOW,
  GPIO_INT_POLARITY_ACTIVE_HIGH,
} GpioIntPolarity_e;

//TODO confirm if this is used in SMU_13_0_6 PPSMC_MSG_SetUclkDpmMode
typedef enum {
  UCLK_DPM_MODE_BANDWIDTH,
  UCLK_DPM_MODE_LATENCY,
} UCLK_DPM_MODE_e;

typedef struct {
  //0-23 SOC, 24-26 SOCIO, 27-29 SOC
  uint16_t avgPsmCount[30];
  uint16_t minPsmCount[30];
  float    avgPsmVoltage[30];
  float    minPsmVoltage[30];
} AvfsDebugTableAid_t;

typedef struct {
  //0-27 GFX, 28-29 SOC
  uint16_t avgPsmCount[30];
  uint16_t minPsmCount[30];
  float    avgPsmVoltage[30];
  float    minPsmVoltage[30];
} AvfsDebugTableXcd_t;

// Defines used for IH-based thermal interrupts to GFX driver - A/X only
#define IH_INTERRUPT_ID_TO_DRIVER                   0xFE
#define IH_INTERRUPT_CONTEXT_ID_THERMAL_THROTTLING  0x7

//thermal over-temp mask defines for IH interrupt to host
#define THROTTLER_PROCHOT_BIT           0
#define THROTTLER_PPT_BIT               1
#define THROTTLER_THERMAL_SOCKET_BIT    2//AID, XCD, CCD throttling
#define THROTTLER_THERMAL_VR_BIT        3//VRHOT
#define THROTTLER_THERMAL_HBM_BIT       4

#define ClearMcaOnRead_UE_FLAG_MASK              0x1
#define ClearMcaOnRead_CE_POLL_MASK              0x2

// These defines are used with the following messages:
// SMC_MSG_TransferTableDram2Smu
// SMC_MSG_TransferTableSmu2Dram
// #define TABLE_PPTABLE                 0
// #define TABLE_AVFS_PSM_DEBUG          1
// #define TABLE_AVFS_FUSE_OVERRIDE      2
// #define TABLE_PMSTATUSLOG             3
// #define TABLE_SMU_METRICS             4
// #define TABLE_DRIVER_SMU_CONFIG       5
// #define TABLE_I2C_COMMANDS            6
// #define TABLE_COUNT                   7

// // Table transfer status
// #define TABLE_TRANSFER_OK         0x0
// #define TABLE_TRANSFER_FAILED     0xFF
// #define TABLE_TRANSFER_PENDING    0xAB

#endif
