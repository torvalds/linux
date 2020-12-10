/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#ifndef __SMU11_DRIVER_IF_VANGOGH_H__
#define __SMU11_DRIVER_IF_VANGOGH_H__

// *** IMPORTANT ***
// SMU TEAM: Always increment the interface version if
// any structure is changed in this file
#define SMU13_DRIVER_IF_VERSION 2

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

#define NUM_DCFCLK_DPM_LEVELS 7
#define NUM_DISPCLK_DPM_LEVELS 7
#define NUM_DPPCLK_DPM_LEVELS 7
#define NUM_SOCCLK_DPM_LEVELS 7
#define NUM_ISPICLK_DPM_LEVELS 7
#define NUM_ISPXCLK_DPM_LEVELS 7
#define NUM_VCN_DPM_LEVELS 5
#define NUM_FCLK_DPM_LEVELS 4
#define NUM_SOC_VOLTAGE_LEVELS 8

typedef struct {
  uint32_t fclk;
  uint32_t memclk;
  uint32_t voltage;
} df_pstate_t;

typedef struct {
  uint32_t vclk;
  uint32_t dclk;
} vcn_clk_t;

//Freq in MHz
//Voltage in milli volts with 2 fractional bits

typedef struct {
  uint32_t DcfClocks[NUM_DCFCLK_DPM_LEVELS];
  uint32_t DispClocks[NUM_DISPCLK_DPM_LEVELS];
  uint32_t DppClocks[NUM_DPPCLK_DPM_LEVELS];
  uint32_t SocClocks[NUM_SOCCLK_DPM_LEVELS];
  uint32_t IspiClocks[NUM_ISPICLK_DPM_LEVELS];
  uint32_t IspxClocks[NUM_ISPXCLK_DPM_LEVELS];
  vcn_clk_t VcnClocks[NUM_VCN_DPM_LEVELS];

  uint32_t SocVoltage[NUM_SOC_VOLTAGE_LEVELS];

  df_pstate_t DfPstateTable[NUM_FCLK_DPM_LEVELS];

  uint32_t MinGfxClk;
  uint32_t MaxGfxClk;

  uint8_t NumDfPstatesEnabled;
  uint8_t NumDpmLevelsEnabled;
  uint8_t NumDcfclkLevelsEnabled;
  uint8_t NumDispClkLevelsEnabled;  //applies to both dispclk and dppclk
  uint8_t NumSocClkLevelsEnabled;

  uint8_t IspClkLevelsEnabled;  //applies to both ispiclk and ispxclk
  uint8_t VcnClkLevelsEnabled;  //applies to both vclk/dclk
  uint8_t spare[2];
} DpmClocks_t;


// Throttler Status Bitmask
#define THROTTLER_STATUS_BIT_SPL 0
#define THROTTLER_STATUS_BIT_FPPT 1
#define THROTTLER_STATUS_BIT_SPPT 2
#define THROTTLER_STATUS_BIT_SPPT_APU 3
#define THROTTLER_STATUS_BIT_THM_CORE 4
#define THROTTLER_STATUS_BIT_THM_GFX 5
#define THROTTLER_STATUS_BIT_THM_SOC 6
#define THROTTLER_STATUS_BIT_TDC_VDD 7
#define THROTTLER_STATUS_BIT_TDC_SOC 8
#define THROTTLER_STATUS_BIT_TDC_GFX 9
#define THROTTLER_STATUS_BIT_TDC_CVIP 10

typedef struct {
  uint16_t GfxclkFrequency;      //[MHz]
  uint16_t SocclkFrequency;      //[MHz]
  uint16_t VclkFrequency;        //[MHz]
  uint16_t DclkFrequency;        //[MHz]
  uint16_t MemclkFrequency;      //[MHz]
  uint16_t spare;

  uint16_t GfxActivity;          //[centi]
  uint16_t UvdActivity;          //[centi]

  uint16_t Voltage[3];           //[mV] indices: VDDCR_VDD, VDDCR_SOC, VDDCR_GFX
  uint16_t Current[3];           //[mA] indices: VDDCR_VDD, VDDCR_SOC, VDDCR_GFX
  uint16_t Power[3];             //[mW] indices: VDDCR_VDD, VDDCR_SOC, VDDCR_GFX
  uint16_t CurrentSocketPower;   //[mW]

  //3rd party tools in Windows need info in the case of APUs
  uint16_t CoreFrequency[8];     //[MHz]
  uint16_t CorePower[8];         //[mW]
  uint16_t CoreTemperature[8];   //[centi-Celsius]
  uint16_t L3Frequency[2];       //[MHz]
  uint16_t L3Temperature[2];     //[centi-Celsius]

  uint16_t GfxTemperature;       //[centi-Celsius]
  uint16_t SocTemperature;       //[centi-Celsius]
  uint16_t EdgeTemperature;
  uint16_t ThrottlerStatus;
} SmuMetrics_t;


// Workload bits
#define WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT 0
#define WORKLOAD_PPLIB_VIDEO_BIT 2
#define WORKLOAD_PPLIB_VR_BIT 3
#define WORKLOAD_PPLIB_COMPUTE_BIT 4
#define WORKLOAD_PPLIB_CUSTOM_BIT 5
#define WORKLOAD_PPLIB_COUNT 6

#define TABLE_BIOS_IF            0 // Called by BIOS
#define TABLE_WATERMARKS         1 // Called by DAL through VBIOS
#define TABLE_CUSTOM_DPM         2 // Called by Driver
#define TABLE_SPARE1             3
#define TABLE_DPMCLOCKS          4 // Called by Driver
#define TABLE_SPARE2             5 // Called by Tools
#define TABLE_MODERN_STDBY       6 // Called by Tools for Modern Standby Log
#define TABLE_SMU_METRICS        7 // Called by Driver
#define TABLE_COUNT              8

//ISP tile definitions
typedef enum {
  TILE_ISPX = 0, // ISPX
  TILE_ISPM,     // ISPM
  TILE_ISPC,  // ISPCORE
  TILE_ISPPRE,   // ISPPRE
  TILE_ISPPOST,  // ISPPOST
  TILE_MAX
} TILE_NUM_e;

// Tile Selection (Based on arguments)
#define TILE_SEL_ISPX       (1<<(TILE_ISPX))
#define TILE_SEL_ISPM       (1<<(TILE_ISPM))
#define TILE_SEL_ISPC       (1<<(TILE_ISPC))
#define TILE_SEL_ISPPRE     (1<<(TILE_ISPPRE))
#define TILE_SEL_ISPPOST    (1<<(TILE_ISPPOST))

// Mask for ISP tiles in PGFSM PWR Status Registers
//Bit[1:0] maps to ISPX, (ISPX)
//Bit[3:2] maps to ISPM, (ISPM)
//Bit[5:4] maps to ISPCORE, (ISPCORE)
//Bit[7:6] maps to ISPPRE, (ISPPRE)
//Bit[9:8] maps to POST, (ISPPOST
#define TILE_ISPX_MASK      ((1<<0) | (1<<1))
#define TILE_ISPM_MASK      ((1<<2) | (1<<3))
#define TILE_ISPC_MASK      ((1<<4) | (1<<5))
#define TILE_ISPPRE_MASK    ((1<<6) | (1<<7))
#define TILE_ISPPOST_MASK   ((1<<8) | (1<<9))

#endif
