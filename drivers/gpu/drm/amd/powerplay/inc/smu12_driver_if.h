/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef SMU12_DRIVER_IF_H
#define SMU12_DRIVER_IF_H

// *** IMPORTANT ***
// SMU TEAM: Always increment the interface version if 
// any structure is changed in this file
#define SMU12_DRIVER_IF_VERSION 11

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
  uint16_t Vid;  // min voltage in SVI2 VID
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

  uint32_t     MmHubPadding[7]; // SMU internal use
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


#define NUM_DCFCLK_DPM_LEVELS 8
#define NUM_SOCCLK_DPM_LEVELS 8
#define NUM_FCLK_DPM_LEVELS   4
#define NUM_MEMCLK_DPM_LEVELS 4
#define NUM_VCN_DPM_LEVELS    8

typedef struct {
  uint32_t Freq;    // In MHz
  uint32_t Vol;     // Millivolts with 2 fractional bits
} DpmClock_t;

typedef struct {
  DpmClock_t DcfClocks[NUM_DCFCLK_DPM_LEVELS];
  DpmClock_t SocClocks[NUM_SOCCLK_DPM_LEVELS];
  DpmClock_t FClocks[NUM_FCLK_DPM_LEVELS];
  DpmClock_t MemClocks[NUM_MEMCLK_DPM_LEVELS];
  DpmClock_t VClocks[NUM_VCN_DPM_LEVELS];
  DpmClock_t DClocks[NUM_VCN_DPM_LEVELS];

  uint8_t NumDcfClkDpmEnabled;
  uint8_t NumSocClkDpmEnabled;
  uint8_t NumFClkDpmEnabled;
  uint8_t NumMemClkDpmEnabled;
  uint8_t NumVClkDpmEnabled;
  uint8_t NumDClkDpmEnabled;
  uint8_t spare[2];
} DpmClocks_t;


typedef enum {
  CLOCK_SMNCLK = 0,
  CLOCK_SOCCLK,
  CLOCK_MP0CLK,
  CLOCK_MP1CLK,
  CLOCK_MP2CLK,
  CLOCK_VCLK,
  CLOCK_LCLK,
  CLOCK_DCLK,
  CLOCK_ACLK,
  CLOCK_ISPCLK,
  CLOCK_SHUBCLK,
  CLOCK_DISPCLK,
  CLOCK_DPPCLK,
  CLOCK_DPREFCLK,
  CLOCK_DCFCLK,
  CLOCK_FCLK,
  CLOCK_UMCCLK,
  CLOCK_GFXCLK,
  CLOCK_COUNT,
} CLOCK_IDs_e;

// Throttler Status Bitmask
#define THROTTLER_STATUS_BIT_SPL        0
#define THROTTLER_STATUS_BIT_FPPT       1
#define THROTTLER_STATUS_BIT_SPPT       2
#define THROTTLER_STATUS_BIT_SPPT_APU   3
#define THROTTLER_STATUS_BIT_THM_CORE   4
#define THROTTLER_STATUS_BIT_THM_GFX    5
#define THROTTLER_STATUS_BIT_THM_SOC    6
#define THROTTLER_STATUS_BIT_TDC_VDD    7
#define THROTTLER_STATUS_BIT_TDC_SOC    8

typedef struct {
  uint16_t ClockFrequency[CLOCK_COUNT]; //[MHz]

  uint16_t AverageGfxclkFrequency;      //[MHz]
  uint16_t AverageSocclkFrequency;      //[MHz]
  uint16_t AverageVclkFrequency;        //[MHz]
  uint16_t AverageFclkFrequency;        //[MHz]

  uint16_t AverageGfxActivity;          //[centi]
  uint16_t AverageUvdActivity;          //[centi]

  uint16_t Voltage[2];                  //[mV] indices: VDDCR_VDD, VDDCR_SOC
  uint16_t Current[2];                  //[mA] indices: VDDCR_VDD, VDDCR_SOC
  uint16_t Power[2];                    //[mW] indices: VDDCR_VDD, VDDCR_SOC

  uint16_t FanPwm;                      //[milli]
  uint16_t CurrentSocketPower;          //[mW]

  uint16_t CoreFrequency[8];            //[MHz]
  uint16_t CorePower[8];                //[mW]
  uint16_t CoreTemperature[8];          //[centi-Celsius]
  uint16_t L3Frequency[2];              //[MHz]
  uint16_t L3Temperature[2];            //[centi-Celsius]

  uint16_t GfxTemperature;              //[centi-Celsius]
  uint16_t SocTemperature;              //[centi-Celsius]
  uint16_t ThrottlerStatus;
  uint16_t spare;

  uint16_t StapmOriginalLimit;          //[mW]
  uint16_t StapmCurrentLimit;           //[mW]
  uint16_t ApuPower;              //[mW]
  uint16_t dGpuPower;               //[mW]
} SmuMetrics_t;


// Workload bits
#define WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT 0
#define WORKLOAD_PPLIB_VIDEO_BIT          2
#define WORKLOAD_PPLIB_VR_BIT             3
#define WORKLOAD_PPLIB_COMPUTE_BIT        4
#define WORKLOAD_PPLIB_CUSTOM_BIT         5
#define WORKLOAD_PPLIB_COUNT              6

#define TABLE_BIOS_IF            0 // Called by BIOS
#define TABLE_WATERMARKS         1 // Called by Driver
#define TABLE_CUSTOM_DPM         2 // Called by Driver
#define TABLE_SPARE1             3
#define TABLE_DPMCLOCKS          4 // Called by Driver
#define TABLE_MOMENTARY_PM       5 // Called by Tools
#define TABLE_MODERN_STDBY       6 // Called by Tools for Modern Standby Log
#define TABLE_SMU_METRICS        7 // Called by Driver
#define TABLE_COUNT              8


#endif
