/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef SMU14_DRIVER_IF_V14_0_0_H
#define SMU14_DRIVER_IF_V14_0_0_H

typedef struct {
  int32_t value;
  uint32_t numFractionalBits;
} FloatInIntFormat_t;

typedef enum {
  DSPCLK_DCFCLK = 0,
  DSPCLK_DISPCLK,
  DSPCLK_PIXCLK,
  DSPCLK_PHYCLK,
  DSPCLK_COUNT,
} DSPCLK_e;

typedef struct {
  uint16_t Freq; // in MHz
  uint16_t Vid;  // min voltage in SVI3 VID
} DisplayClockTable_t;

typedef struct {
  uint16_t MinClock; // This is either DCFCLK or SOCCLK (in MHz)
  uint16_t MaxClock; // This is either DCFCLK or SOCCLK (in MHz)
  uint16_t MinMclk;
  uint16_t MaxMclk;

  uint8_t  WmSetting;
  uint8_t  WmType;  // Used for normal pstate change or memory retraining
  uint8_t  Padding[2];
} WatermarkRowGeneric_t;

#define NUM_WM_RANGES 4
#define WM_PSTATE_CHG 0
#define WM_RETRAINING 1

typedef enum {
  WM_SOCCLK = 0,
  WM_DCFCLK,
  WM_COUNT,
} WM_CLOCK_e;

typedef struct {
  // Watermarks
  WatermarkRowGeneric_t WatermarkRow[WM_COUNT][NUM_WM_RANGES];

  uint32_t MmHubPadding[7]; // SMU internal use
} Watermarks_t;

typedef enum {
  CUSTOM_DPM_SETTING_GFXCLK,
  CUSTOM_DPM_SETTING_CCLK,
  CUSTOM_DPM_SETTING_FCLK_CCX,
  CUSTOM_DPM_SETTING_FCLK_GFX,
  CUSTOM_DPM_SETTING_FCLK_STALLS,
  CUSTOM_DPM_SETTING_LCLK,
  CUSTOM_DPM_SETTING_COUNT,
} CUSTOM_DPM_SETTING_e;

typedef struct {
  uint8_t             ActiveHystLimit;
  uint8_t             IdleHystLimit;
  uint8_t             FPS;
  uint8_t             MinActiveFreqType;
  FloatInIntFormat_t  MinActiveFreq;
  FloatInIntFormat_t  PD_Data_limit;
  FloatInIntFormat_t  PD_Data_time_constant;
  FloatInIntFormat_t  PD_Data_error_coeff;
  FloatInIntFormat_t  PD_Data_error_rate_coeff;
} DpmActivityMonitorCoeffExt_t;

typedef struct {
  DpmActivityMonitorCoeffExt_t DpmActivityMonitorCoeff[CUSTOM_DPM_SETTING_COUNT];
} CustomDpmSettings_t;

#define NUM_DCFCLK_DPM_LEVELS     8
#define NUM_DISPCLK_DPM_LEVELS    8
#define NUM_DPPCLK_DPM_LEVELS     8
#define NUM_SOCCLK_DPM_LEVELS     8
#define NUM_VCN_DPM_LEVELS        8
#define NUM_SOC_VOLTAGE_LEVELS    8
#define NUM_VPE_DPM_LEVELS        8
#define NUM_FCLK_DPM_LEVELS       8
#define NUM_MEM_PSTATE_LEVELS     4


typedef struct {
  uint32_t UClk;
  uint32_t MemClk;
  uint32_t Voltage;
  uint8_t  WckRatio;
  uint8_t  Spare[3];
} MemPstateTable_t;

//Freq in MHz
//Voltage in milli volts with 2 fractional bits
typedef struct {
  uint32_t DcfClocks[NUM_DCFCLK_DPM_LEVELS];
  uint32_t DispClocks[NUM_DISPCLK_DPM_LEVELS];
  uint32_t DppClocks[NUM_DPPCLK_DPM_LEVELS];
  uint32_t SocClocks[NUM_SOCCLK_DPM_LEVELS];
  uint32_t VClocks[NUM_VCN_DPM_LEVELS];
  uint32_t DClocks[NUM_VCN_DPM_LEVELS];
  uint32_t VPEClocks[NUM_VPE_DPM_LEVELS];
  uint32_t FclkClocks_Freq[NUM_FCLK_DPM_LEVELS];
  uint32_t FclkClocks_Voltage[NUM_FCLK_DPM_LEVELS];
  uint32_t SocVoltage[NUM_SOC_VOLTAGE_LEVELS];
  MemPstateTable_t MemPstateTable[NUM_MEM_PSTATE_LEVELS];

  uint8_t  NumDcfClkLevelsEnabled;
  uint8_t  NumDispClkLevelsEnabled; //Applies to both Dispclk and Dppclk
  uint8_t  NumSocClkLevelsEnabled;
  uint8_t  VcnClkLevelsEnabled;     //Applies to both Vclk and Dclk
  uint8_t  VpeClkLevelsEnabled;

  uint8_t  NumMemPstatesEnabled;
  uint8_t  NumFclkLevelsEnabled;
  uint8_t  spare[2];

  uint32_t MinGfxClk;
  uint32_t MaxGfxClk;
} DpmClocks_t;

//Freq in MHz
//Voltage in milli volts with 2 fractional bits
typedef struct {
  uint32_t DcfClocks[NUM_DCFCLK_DPM_LEVELS];
  uint32_t DispClocks[NUM_DISPCLK_DPM_LEVELS];
  uint32_t DppClocks[NUM_DPPCLK_DPM_LEVELS];
  uint32_t SocClocks[NUM_SOCCLK_DPM_LEVELS];
  uint32_t VClocks0[NUM_VCN_DPM_LEVELS];
  uint32_t VClocks1[NUM_VCN_DPM_LEVELS];
  uint32_t DClocks0[NUM_VCN_DPM_LEVELS];
  uint32_t DClocks1[NUM_VCN_DPM_LEVELS];
  uint32_t VPEClocks[NUM_VPE_DPM_LEVELS];
  uint32_t FclkClocks_Freq[NUM_FCLK_DPM_LEVELS];
  uint32_t FclkClocks_Voltage[NUM_FCLK_DPM_LEVELS];
  uint32_t SocVoltage[NUM_SOC_VOLTAGE_LEVELS];
  MemPstateTable_t MemPstateTable[NUM_MEM_PSTATE_LEVELS];

  uint8_t  NumDcfClkLevelsEnabled;
  uint8_t  NumDispClkLevelsEnabled; //Applies to both Dispclk and Dppclk
  uint8_t  NumSocClkLevelsEnabled;
  uint8_t  Vcn0ClkLevelsEnabled;     //Applies to both Vclk0 and Dclk0
  uint8_t  Vcn1ClkLevelsEnabled;     //Applies to both Vclk1 and Dclk1
  uint8_t  VpeClkLevelsEnabled;
  uint8_t  NumMemPstatesEnabled;
  uint8_t  NumFclkLevelsEnabled;

  uint32_t MinGfxClk;
  uint32_t MaxGfxClk;
} DpmClocks_t_v14_0_1;

typedef struct {
  uint16_t CoreFrequency[16];          //Target core frequency [MHz]
  uint16_t CorePower[16];              //CAC calculated core power [mW]
  uint16_t CoreTemperature[16];        //TSEN measured core temperature [centi-C]
  uint16_t GfxTemperature;             //TSEN measured GFX temperature [centi-C]
  uint16_t SocTemperature;             //TSEN measured SOC temperature [centi-C]
  uint16_t StapmOpnLimit;              //Maximum IRM defined STAPM power limit [mW]
  uint16_t StapmCurrentLimit;          //Time filtered STAPM power limit [mW]
  uint16_t InfrastructureCpuMaxFreq;   //CCLK frequency limit enforced on classic cores [MHz]
  uint16_t InfrastructureGfxMaxFreq;   //GFXCLK frequency limit enforced on GFX [MHz]
  uint16_t SkinTemp;                   //Maximum skin temperature reported by APU and HS2 chassis sensors [centi-C]
  uint16_t GfxclkFrequency;            //Time filtered target GFXCLK frequency [MHz]
  uint16_t FclkFrequency;              //Time filtered target FCLK frequency [MHz]
  uint16_t GfxActivity;                //Time filtered GFX busy % [0-100]
  uint16_t SocclkFrequency;            //Time filtered target SOCCLK frequency [MHz]
  uint16_t VclkFrequency;              //Time filtered target VCLK frequency [MHz]
  uint16_t VcnActivity;                //Time filtered VCN busy % [0-100]
  uint16_t VpeclkFrequency;            //Time filtered target VPECLK frequency [MHz]
  uint16_t IpuclkFrequency;            //Time filtered target IPUCLK frequency [MHz]
  uint16_t IpuBusy[8];                 //Time filtered IPU per-column busy % [0-100]
  uint16_t DRAMReads;                  //Time filtered DRAM read bandwidth [MB/sec]
  uint16_t DRAMWrites;                 //Time filtered DRAM write bandwidth [MB/sec]
  uint16_t CoreC0Residency[16];        //Time filtered per-core C0 residency % [0-100]
  uint16_t IpuPower;                   //Time filtered IPU power [mW]
  uint32_t ApuPower;                   //Time filtered APU power [mW]
  uint32_t GfxPower;                   //Time filtered GFX power [mW]
  uint32_t dGpuPower;                  //Time filtered dGPU power [mW]
  uint32_t SocketPower;                //Time filtered power used for PPT/STAPM [APU+dGPU] [mW]
  uint32_t AllCorePower;               //Time filtered sum of core power across all cores in the socket [mW]
  uint32_t FilterAlphaValue;           //Metrics table alpha filter time constant [us]
  uint32_t MetricsCounter;             //Counter that is incremented on every metrics table update [PM_TIMER cycles]
  uint16_t MemclkFrequency;            //Time filtered target MEMCLK frequency [MHz]
  uint16_t MpipuclkFrequency;          //Time filtered target MPIPUCLK frequency [MHz]
  uint16_t IpuReads;                   //Time filtered IPU read bandwidth [MB/sec]
  uint16_t IpuWrites;                  //Time filtered IPU write bandwidth [MB/sec]
  uint32_t ThrottleResidency_PROCHOT;  //Counter that is incremented on every metrics table update when PROCHOT was engaged [PM_TIMER cycles]
  uint32_t ThrottleResidency_SPL;      //Counter that is incremented on every metrics table update when SPL was engaged [PM_TIMER cycles]
  uint32_t ThrottleResidency_FPPT;     //Counter that is incremented on every metrics table update when fast PPT was engaged [PM_TIMER cycles]
  uint32_t ThrottleResidency_SPPT;     //Counter that is incremented on every metrics table update when slow PPT was engaged [PM_TIMER cycles]
  uint32_t ThrottleResidency_THM_CORE; //Counter that is incremented on every metrics table update when CORE thermal throttling was engaged [PM_TIMER cycles]
  uint32_t ThrottleResidency_THM_GFX;  //Counter that is incremented on every metrics table update when GFX thermal throttling was engaged [PM_TIMER cycles]
  uint32_t ThrottleResidency_THM_SOC;  //Counter that is incremented on every metrics table update when SOC thermal throttling was engaged [PM_TIMER cycles]
  uint16_t Psys;                       //Time filtered Psys power [mW]
  uint16_t spare1;
  uint32_t spare[6];
} SmuMetrics_t;

//ISP tile definitions
typedef enum {
  TILE_XTILE = 0,         //ONO0
  TILE_MTILE,             //ONO1
  TILE_PDP,               //ONO2
  TILE_CSTAT,             //ONO2
  TILE_LME,               //ONO3
  TILE_BYRP,              //ONO4
  TILE_GRBP,              //ONO4
  TILE_MCFP,              //ONO4
  TILE_YUVP,              //ONO4
  TILE_MCSC,              //ONO4
  TILE_GDC,               //ONO5
  TILE_MAX
} TILE_NUM_e;

// Tile Selection (Based on arguments)
#define ISP_TILE_SEL(tile)   (1<<tile)
#define ISP_TILE_SEL_ALL     0x7FF

// Workload bits
#define WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT 0
#define WORKLOAD_PPLIB_VIDEO_BIT          2
#define WORKLOAD_PPLIB_VR_BIT             3
#define WORKLOAD_PPLIB_COMPUTE_BIT        4
#define WORKLOAD_PPLIB_CUSTOM_BIT         5
#define WORKLOAD_PPLIB_COUNT              6

#define TABLE_BIOS_IF               0 // Called by BIOS
#define TABLE_WATERMARKS            1 // Called by DAL through VBIOS
#define TABLE_CUSTOM_DPM            2 // Called by Driver
#define TABLE_BIOS_GPIO_CONFIG      3 // Called by BIOS
#define TABLE_DPMCLOCKS             4 // Called by Driver and VBIOS
#define TABLE_MOMENTARY_PM          5 // Called by Tools
#define TABLE_MODERN_STDBY          6 // Called by Tools for Modern Standby Log
#define TABLE_SMU_METRICS           7 // Called by Driver and SMF/PMF
#define TABLE_COUNT                 8

#endif
