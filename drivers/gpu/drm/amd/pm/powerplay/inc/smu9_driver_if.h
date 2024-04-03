/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef SMU9_DRIVER_IF_H
#define SMU9_DRIVER_IF_H

#include "smu9.h"

/**** IMPORTANT ***
 * SMU TEAM: Always increment the interface version if
 * any structure is changed in this file
 */
#define SMU9_DRIVER_IF_VERSION 0xE

#define PPTABLE_V10_SMU_VERSION 1

#define NUM_GFXCLK_DPM_LEVELS  8
#define NUM_UVD_DPM_LEVELS     8
#define NUM_VCE_DPM_LEVELS     8
#define NUM_MP0CLK_DPM_LEVELS  8
#define NUM_UCLK_DPM_LEVELS    4
#define NUM_SOCCLK_DPM_LEVELS  8
#define NUM_DCEFCLK_DPM_LEVELS 8
#define NUM_LINK_LEVELS        2

#define MAX_GFXCLK_DPM_LEVEL  (NUM_GFXCLK_DPM_LEVELS  - 1)
#define MAX_UVD_DPM_LEVEL     (NUM_UVD_DPM_LEVELS     - 1)
#define MAX_VCE_DPM_LEVEL     (NUM_VCE_DPM_LEVELS     - 1)
#define MAX_MP0CLK_DPM_LEVEL  (NUM_MP0CLK_DPM_LEVELS  - 1)
#define MAX_UCLK_DPM_LEVEL    (NUM_UCLK_DPM_LEVELS    - 1)
#define MAX_SOCCLK_DPM_LEVEL  (NUM_SOCCLK_DPM_LEVELS  - 1)
#define MAX_DCEFCLK_DPM_LEVEL (NUM_DCEFCLK_DPM_LEVELS - 1)
#define MAX_LINK_DPM_LEVEL    (NUM_LINK_LEVELS        - 1)

#define MIN_GFXCLK_DPM_LEVEL  0
#define MIN_UVD_DPM_LEVEL     0
#define MIN_VCE_DPM_LEVEL     0
#define MIN_MP0CLK_DPM_LEVEL  0
#define MIN_UCLK_DPM_LEVEL    0
#define MIN_SOCCLK_DPM_LEVEL  0
#define MIN_DCEFCLK_DPM_LEVEL 0
#define MIN_LINK_DPM_LEVEL    0

#define NUM_EVV_VOLTAGE_LEVELS 8
#define MAX_EVV_VOLTAGE_LEVEL (NUM_EVV_VOLTAGE_LEVELS - 1)
#define MIN_EVV_VOLTAGE_LEVEL 0

#define NUM_PSP_LEVEL_MAP 4

/* Gemini Modes */
#define PPSMC_GeminiModeNone   0  /* Single GPU board */
#define PPSMC_GeminiModeMaster 1  /* Master GPU on a Gemini board */
#define PPSMC_GeminiModeSlave  2  /* Slave GPU on a Gemini board */

/* Voltage Modes for DPMs */
#define VOLTAGE_MODE_AVFS_INTERPOLATE 0
#define VOLTAGE_MODE_AVFS_WORST_CASE  1
#define VOLTAGE_MODE_STATIC           2

typedef struct {
  uint32_t FbMult; /* Feedback Multiplier, bit 8:0 int, bit 15:12 post_div, bit 31:16 frac */
  uint32_t SsFbMult; /* Spread FB Mult: bit 8:0 int, bit 31:16 frac */
  uint16_t SsSlewFrac;
  uint8_t  SsOn;
  uint8_t  Did;      /* DID */
} PllSetting_t;

typedef struct {
  int32_t a0;
  int32_t a1;
  int32_t a2;

  uint8_t a0_shift;
  uint8_t a1_shift;
  uint8_t a2_shift;
  uint8_t padding;
} GbVdroopTable_t;

typedef struct {
  int32_t m1;
  int32_t m2;
  int32_t b;

  uint8_t m1_shift;
  uint8_t m2_shift;
  uint8_t b_shift;
  uint8_t padding;
} QuadraticInt_t;

#define NUM_DSPCLK_LEVELS 8

typedef enum {
  DSPCLK_DCEFCLK = 0,
  DSPCLK_DISPCLK,
  DSPCLK_PIXCLK,
  DSPCLK_PHYCLK,
  DSPCLK_COUNT,
} DSPCLK_e;

typedef struct {
  uint16_t Freq; /* in MHz */
  uint16_t Vid;  /* min voltage in SVI2 VID */
} DisplayClockTable_t;

#pragma pack(push, 1)
typedef struct {
  /* PowerTune */
  uint16_t SocketPowerLimit; /* Watts */
  uint16_t TdcLimit;         /* Amps */
  uint16_t EdcLimit;         /* Amps */
  uint16_t TedgeLimit;       /* Celcius */
  uint16_t ThotspotLimit;    /* Celcius */
  uint16_t ThbmLimit;        /* Celcius */
  uint16_t Tvr_socLimit;     /* Celcius */
  uint16_t Tvr_memLimit;     /* Celcius */
  uint16_t Tliquid1Limit;    /* Celcius */
  uint16_t Tliquid2Limit;    /* Celcius */
  uint16_t TplxLimit;        /* Celcius */
  uint16_t LoadLineResistance; /* in mOhms */
  uint32_t FitLimit;         /* Failures in time (failures per million parts over the defined lifetime) */

  /* External Component Communication Settings */
  uint8_t  Liquid1_I2C_address;
  uint8_t  Liquid2_I2C_address;
  uint8_t  Vr_I2C_address;
  uint8_t  Plx_I2C_address;

  uint8_t  GeminiMode;
  uint8_t  spare17[3];
  uint32_t GeminiApertureHigh;
  uint32_t GeminiApertureLow;

  uint8_t  Liquid_I2C_LineSCL;
  uint8_t  Liquid_I2C_LineSDA;
  uint8_t  Vr_I2C_LineSCL;
  uint8_t  Vr_I2C_LineSDA;
  uint8_t  Plx_I2C_LineSCL;
  uint8_t  Plx_I2C_LineSDA;
  uint8_t  paddingx[2];

  /* ULV Settings */
  uint8_t  UlvOffsetVid;     /* SVI2 VID */
  uint8_t  UlvSmnclkDid;     /* DID for ULV mode. 0 means CLK will not be modified in ULV. */
  uint8_t  UlvMp1clkDid;     /* DID for ULV mode. 0 means CLK will not be modified in ULV. */
  uint8_t  UlvGfxclkBypass;  /* 1 to turn off/bypass Gfxclk during ULV, 0 to leave Gfxclk on during ULV */

  /* VDDCR_SOC Voltages */
  uint8_t      SocVid[NUM_EVV_VOLTAGE_LEVELS];

  /* This is the minimum voltage needed to run the SOC. */
  uint8_t      MinVoltageVid; /* Minimum Voltage ("Vmin") of ASIC */
  uint8_t      MaxVoltageVid; /* Maximum Voltage allowable */
  uint8_t      MaxVidStep; /* Max VID step that SMU will request. Multiple steps are taken if voltage change exceeds this value. */
  uint8_t      padding8;

  uint8_t      UlvPhaseSheddingPsi0; /* set this to 1 to set PSI0/1 to 1 in ULV mode */
  uint8_t      UlvPhaseSheddingPsi1; /* set this to 1 to set PSI0/1 to 1 in ULV mode */
  uint8_t      padding8_2[2];

  /* SOC Frequencies */
  PllSetting_t GfxclkLevel[NUM_GFXCLK_DPM_LEVELS];

  uint8_t      SocclkDid[NUM_SOCCLK_DPM_LEVELS];          /* DID */
  uint8_t      SocDpmVoltageIndex[NUM_SOCCLK_DPM_LEVELS];

  uint8_t      VclkDid[NUM_UVD_DPM_LEVELS];            /* DID */
  uint8_t      DclkDid[NUM_UVD_DPM_LEVELS];            /* DID */
  uint8_t      UvdDpmVoltageIndex[NUM_UVD_DPM_LEVELS];

  uint8_t      EclkDid[NUM_VCE_DPM_LEVELS];            /* DID */
  uint8_t      VceDpmVoltageIndex[NUM_VCE_DPM_LEVELS];

  uint8_t      Mp0clkDid[NUM_MP0CLK_DPM_LEVELS];          /* DID */
  uint8_t      Mp0DpmVoltageIndex[NUM_MP0CLK_DPM_LEVELS];

  DisplayClockTable_t DisplayClockTable[DSPCLK_COUNT][NUM_DSPCLK_LEVELS];
  QuadraticInt_t      DisplayClock2Gfxclk[DSPCLK_COUNT];

  uint8_t      GfxDpmVoltageMode;
  uint8_t      SocDpmVoltageMode;
  uint8_t      UclkDpmVoltageMode;
  uint8_t      UvdDpmVoltageMode;

  uint8_t      VceDpmVoltageMode;
  uint8_t      Mp0DpmVoltageMode;
  uint8_t      DisplayDpmVoltageMode;
  uint8_t      padding8_3;

  uint16_t     GfxclkSlewRate;
  uint16_t     padding;

  uint32_t     LowGfxclkInterruptThreshold;  /* in units of 10KHz */

  /* Alpha parameters for clock averages. ("255"=1) */
  uint8_t      GfxclkAverageAlpha;
  uint8_t      SocclkAverageAlpha;
  uint8_t      UclkAverageAlpha;
  uint8_t      GfxActivityAverageAlpha;

  /* UCLK States */
  uint8_t      MemVid[NUM_UCLK_DPM_LEVELS];    /* VID */
  PllSetting_t UclkLevel[NUM_UCLK_DPM_LEVELS];   /* Full PLL settings */
  uint8_t      MemSocVoltageIndex[NUM_UCLK_DPM_LEVELS];
  uint8_t      LowestUclkReservedForUlv; /* Set this to 1 if UCLK DPM0 is reserved for ULV-mode only */
  uint8_t      paddingUclk[3];
  uint16_t     NumMemoryChannels;  /* Used for memory bandwidth calculations */
  uint16_t     MemoryChannelWidth; /* Used for memory bandwidth calculations */

  /* CKS Settings */
  uint8_t      CksEnable[NUM_GFXCLK_DPM_LEVELS];
  uint8_t      CksVidOffset[NUM_GFXCLK_DPM_LEVELS];

  /* MP0 Mapping Table */
  uint8_t      PspLevelMap[NUM_PSP_LEVEL_MAP];

  /* Link DPM Settings */
  uint8_t     PcieGenSpeed[NUM_LINK_LEVELS];           /* 0:PciE-gen1 1:PciE-gen2 2:PciE-gen3 */
  uint8_t     PcieLaneCount[NUM_LINK_LEVELS];          /* 1=x1, 2=x2, 3=x4, 4=x8, 5=x12, 6=x16 */
  uint8_t     LclkDid[NUM_LINK_LEVELS];                /* Leave at 0 to use hardcoded values in FW */
  uint8_t     paddingLinkDpm[2];

  /* Fan Control */
  uint16_t     FanStopTemp;          /* Celcius */
  uint16_t     FanStartTemp;         /* Celcius */

  uint16_t     FanGainEdge;
  uint16_t     FanGainHotspot;
  uint16_t     FanGainLiquid;
  uint16_t     FanGainVrVddc;
  uint16_t     FanGainVrMvdd;
  uint16_t     FanGainPlx;
  uint16_t     FanGainHbm;
  uint16_t     FanPwmMin;
  uint16_t     FanAcousticLimitRpm;
  uint16_t     FanThrottlingRpm;
  uint16_t     FanMaximumRpm;
  uint16_t     FanTargetTemperature;
  uint16_t     FanTargetGfxclk;
  uint8_t      FanZeroRpmEnable;
  uint8_t      FanSpare;

  /* The following are AFC override parameters. Leave at 0 to use FW defaults. */
  int16_t      FuzzyFan_ErrorSetDelta;
  int16_t      FuzzyFan_ErrorRateSetDelta;
  int16_t      FuzzyFan_PwmSetDelta;
  uint16_t     FuzzyFan_Reserved;

  /* GPIO Settings */
  uint8_t      AcDcGpio;        /* GPIO pin configured for AC/DC switching */
  uint8_t      AcDcPolarity;    /* GPIO polarity for AC/DC switching */
  uint8_t      VR0HotGpio;      /* GPIO pin configured for VR0 HOT event */
  uint8_t      VR0HotPolarity;  /* GPIO polarity for VR0 HOT event */
  uint8_t      VR1HotGpio;      /* GPIO pin configured for VR1 HOT event */
  uint8_t      VR1HotPolarity;  /* GPIO polarity for VR1 HOT event */
  uint8_t      Padding1;       /* replace GPIO pin configured for CTF */
  uint8_t      Padding2;       /* replace GPIO polarity for CTF */

  /* LED Display Settings */
  uint8_t      LedPin0;         /* GPIO number for LedPin[0] */
  uint8_t      LedPin1;         /* GPIO number for LedPin[1] */
  uint8_t      LedPin2;         /* GPIO number for LedPin[2] */
  uint8_t      padding8_4;

  /* AVFS */
  uint8_t      OverrideBtcGbCksOn;
  uint8_t      OverrideAvfsGbCksOn;
  uint8_t      PaddingAvfs8[2];

  GbVdroopTable_t BtcGbVdroopTableCksOn;
  GbVdroopTable_t BtcGbVdroopTableCksOff;

  QuadraticInt_t  AvfsGbCksOn;  /* Replacement equation */
  QuadraticInt_t  AvfsGbCksOff; /* Replacement equation */

  uint8_t      StaticVoltageOffsetVid[NUM_GFXCLK_DPM_LEVELS]; /* This values are added on to the final voltage calculation */

  /* Ageing Guardband Parameters */
  uint32_t     AConstant[3];
  uint16_t     DC_tol_sigma;
  uint16_t     Platform_mean;
  uint16_t     Platform_sigma;
  uint16_t     PSM_Age_CompFactor;

  uint32_t     DpmLevelPowerDelta;

  uint8_t      EnableBoostState;
  uint8_t      AConstant_Shift;
  uint8_t      DC_tol_sigma_Shift;
  uint8_t      PSM_Age_CompFactor_Shift;

  uint16_t     BoostStartTemperature;
  uint16_t     BoostStopTemperature;

  PllSetting_t GfxBoostState;

  uint8_t      AcgEnable[NUM_GFXCLK_DPM_LEVELS];
  GbVdroopTable_t AcgBtcGbVdroopTable;
  QuadraticInt_t  AcgAvfsGb;

  /* ACG Frequency Table, in Mhz */
  uint32_t     AcgFreqTable[NUM_GFXCLK_DPM_LEVELS];

  /* Padding - ignore */
  uint32_t     MmHubPadding[3]; /* SMU internal use */

} PPTable_t;
#pragma pack(pop)

typedef struct {
  uint16_t MinClock; // This is either DCEFCLK or SOCCLK (in MHz)
  uint16_t MaxClock; // This is either DCEFCLK or SOCCLK (in MHz)
  uint16_t MinUclk;
  uint16_t MaxUclk;

  uint8_t  WmSetting;
  uint8_t  Padding[3];
} WatermarkRowGeneric_t;

#define NUM_WM_RANGES 4

typedef enum {
  WM_SOCCLK = 0,
  WM_DCEFCLK,
  WM_COUNT,
} WM_CLOCK_e;

typedef struct {
  /* Watermarks */
  WatermarkRowGeneric_t WatermarkRow[WM_COUNT][NUM_WM_RANGES];

  uint32_t     MmHubPadding[7]; /* SMU internal use */
} Watermarks_t;

#ifdef PPTABLE_V10_SMU_VERSION
typedef struct {
  float        AvfsGbCksOn[NUM_GFXCLK_DPM_LEVELS];
  float        AcBtcGbCksOn[NUM_GFXCLK_DPM_LEVELS];
  float        AvfsGbCksOff[NUM_GFXCLK_DPM_LEVELS];
  float        AcBtcGbCksOff[NUM_GFXCLK_DPM_LEVELS];
  float        DcBtcGb;

  uint32_t     MmHubPadding[7]; /* SMU internal use */
} AvfsTable_t;
#else
typedef struct {
  uint32_t     AvfsGbCksOn[NUM_GFXCLK_DPM_LEVELS];
  uint32_t     AcBtcGbCksOn[NUM_GFXCLK_DPM_LEVELS];
  uint32_t     AvfsGbCksOff[NUM_GFXCLK_DPM_LEVELS];
  uint32_t     AcBtcGbCksOff[NUM_GFXCLK_DPM_LEVELS];
  uint32_t     DcBtcGb;

  uint32_t     MmHubPadding[7]; /* SMU internal use */
} AvfsTable_t;
#endif

typedef struct {
  uint16_t avgPsmCount[30];
  uint16_t minPsmCount[30];
  float    avgPsmVoltage[30];
  float    minPsmVoltage[30];

  uint32_t MmHubPadding[7]; /* SMU internal use */
} AvfsDebugTable_t;

typedef struct {
  uint8_t  AvfsEn;
  uint8_t  AvfsVersion;
  uint8_t  Padding[2];

  int32_t VFT0_m1; /* Q8.24 */
  int32_t VFT0_m2; /* Q12.12 */
  int32_t VFT0_b;  /* Q32 */

  int32_t VFT1_m1; /* Q8.16 */
  int32_t VFT1_m2; /* Q12.12 */
  int32_t VFT1_b;  /* Q32 */

  int32_t VFT2_m1; /* Q8.16 */
  int32_t VFT2_m2; /* Q12.12 */
  int32_t VFT2_b;  /* Q32 */

  int32_t AvfsGb0_m1; /* Q8.16 */
  int32_t AvfsGb0_m2; /* Q12.12 */
  int32_t AvfsGb0_b;  /* Q32 */

  int32_t AcBtcGb_m1; /* Q8.24 */
  int32_t AcBtcGb_m2; /* Q12.12 */
  int32_t AcBtcGb_b;  /* Q32 */

  uint32_t AvfsTempCold;
  uint32_t AvfsTempMid;
  uint32_t AvfsTempHot;

  uint32_t InversionVoltage; /*  in mV with 2 fractional bits */

  int32_t P2V_m1; /* Q8.24 */
  int32_t P2V_m2; /* Q12.12 */
  int32_t P2V_b;  /* Q32 */

  uint32_t P2VCharzFreq; /* in 10KHz units */

  uint32_t EnabledAvfsModules;

  uint32_t MmHubPadding[7]; /* SMU internal use */
} AvfsFuseOverride_t;

/* These defines are used with the following messages:
 * SMC_MSG_TransferTableDram2Smu
 * SMC_MSG_TransferTableSmu2Dram
 */
#define TABLE_PPTABLE            0
#define TABLE_WATERMARKS         1
#define TABLE_AVFS               2
#define TABLE_AVFS_PSM_DEBUG     3
#define TABLE_AVFS_FUSE_OVERRIDE 4
#define TABLE_PMSTATUSLOG        5
#define TABLE_COUNT              6

/* These defines are used with the SMC_MSG_SetUclkFastSwitch message. */
#define UCLK_SWITCH_SLOW 0
#define UCLK_SWITCH_FAST 1

/* GFX DIDT Configuration */
#define SQ_Enable_MASK 0x1
#define SQ_IR_MASK 0x2
#define SQ_PCC_MASK 0x4
#define SQ_EDC_MASK 0x8

#define TCP_Enable_MASK 0x100
#define TCP_IR_MASK 0x200
#define TCP_PCC_MASK 0x400
#define TCP_EDC_MASK 0x800

#define TD_Enable_MASK 0x10000
#define TD_IR_MASK 0x20000
#define TD_PCC_MASK 0x40000
#define TD_EDC_MASK 0x80000

#define DB_Enable_MASK 0x1000000
#define DB_IR_MASK 0x2000000
#define DB_PCC_MASK 0x4000000
#define DB_EDC_MASK 0x8000000

#define SQ_Enable_SHIFT 0
#define SQ_IR_SHIFT 1
#define SQ_PCC_SHIFT 2
#define SQ_EDC_SHIFT 3

#define TCP_Enable_SHIFT 8
#define TCP_IR_SHIFT 9
#define TCP_PCC_SHIFT 10
#define TCP_EDC_SHIFT 11

#define TD_Enable_SHIFT 16
#define TD_IR_SHIFT 17
#define TD_PCC_SHIFT 18
#define TD_EDC_SHIFT 19

#define DB_Enable_SHIFT 24
#define DB_IR_SHIFT 25
#define DB_PCC_SHIFT 26
#define DB_EDC_SHIFT 27

#define REMOVE_FMAX_MARGIN_BIT     0x0
#define REMOVE_DCTOL_MARGIN_BIT    0x1
#define REMOVE_PLATFORM_MARGIN_BIT 0x2

#endif
