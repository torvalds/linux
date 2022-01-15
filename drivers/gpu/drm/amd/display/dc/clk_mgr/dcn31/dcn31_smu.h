/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef DAL_DC_31_SMU_H_
#define DAL_DC_31_SMU_H_

#ifndef PMFW_DRIVER_IF_H
#define PMFW_DRIVER_IF_H
#define PMFW_DRIVER_IF_VERSION 4

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

#define NUM_DCFCLK_DPM_LEVELS   8
#define NUM_DISPCLK_DPM_LEVELS  8
#define NUM_DPPCLK_DPM_LEVELS   8
#define NUM_SOCCLK_DPM_LEVELS   8
#define NUM_VCN_DPM_LEVELS      8
#define NUM_SOC_VOLTAGE_LEVELS  8
#define NUM_DF_PSTATE_LEVELS    4

typedef enum{
  WCK_RATIO_1_1 = 0,  // DDR5, Wck:ck is always 1:1;
  WCK_RATIO_1_2,
  WCK_RATIO_1_4,
  WCK_RATIO_MAX
} WCK_RATIO_e;

typedef struct {
  uint32_t FClk;
  uint32_t MemClk;
  uint32_t Voltage;
  uint8_t  WckRatio;
  uint8_t  Spare[3];
} DfPstateTable_t;

//Freq in MHz
//Voltage in milli volts with 2 fractional bits
typedef struct {
  uint32_t DcfClocks[NUM_DCFCLK_DPM_LEVELS];
  uint32_t DispClocks[NUM_DISPCLK_DPM_LEVELS];
  uint32_t DppClocks[NUM_DPPCLK_DPM_LEVELS];
  uint32_t SocClocks[NUM_SOCCLK_DPM_LEVELS];
  uint32_t VClocks[NUM_VCN_DPM_LEVELS];
  uint32_t DClocks[NUM_VCN_DPM_LEVELS];
  uint32_t SocVoltage[NUM_SOC_VOLTAGE_LEVELS];
  DfPstateTable_t DfPstateTable[NUM_DF_PSTATE_LEVELS];

  uint8_t  NumDcfClkLevelsEnabled;
  uint8_t  NumDispClkLevelsEnabled; //Applies to both Dispclk and Dppclk
  uint8_t  NumSocClkLevelsEnabled;
  uint8_t  VcnClkLevelsEnabled;     //Applies to both Vclk and Dclk
  uint8_t  NumDfPstatesEnabled;
  uint8_t  spare[3];

  uint32_t MinGfxClk;
  uint32_t MaxGfxClk;
} DpmClocks_t;


// Throttler Status Bitmask
#define THROTTLER_STATUS_BIT_SPL            0
#define THROTTLER_STATUS_BIT_FPPT           1
#define THROTTLER_STATUS_BIT_SPPT           2
#define THROTTLER_STATUS_BIT_SPPT_APU       3
#define THROTTLER_STATUS_BIT_THM_CORE       4
#define THROTTLER_STATUS_BIT_THM_GFX        5
#define THROTTLER_STATUS_BIT_THM_SOC        6
#define THROTTLER_STATUS_BIT_TDC_VDD        7
#define THROTTLER_STATUS_BIT_TDC_SOC        8
#define THROTTLER_STATUS_BIT_PROCHOT_CPU    9
#define THROTTLER_STATUS_BIT_PROCHOT_GFX   10
#define THROTTLER_STATUS_BIT_EDC_CPU       11
#define THROTTLER_STATUS_BIT_EDC_GFX       12

typedef struct {
  uint16_t GfxclkFrequency;             //[MHz]
  uint16_t SocclkFrequency;             //[MHz]
  uint16_t VclkFrequency;               //[MHz]
  uint16_t DclkFrequency;               //[MHz]
  uint16_t MemclkFrequency;             //[MHz]
  uint16_t spare;

  uint16_t GfxActivity;                 //[centi]
  uint16_t UvdActivity;                 //[centi]

  uint16_t Voltage[2];                  //[mV] indices: VDDCR_VDD, VDDCR_SOC
  uint16_t Current[2];                  //[mA] indices: VDDCR_VDD, VDDCR_SOC
  uint16_t Power[2];                    //[mW] indices: VDDCR_VDD, VDDCR_SOC

  //3rd party tools in Windows need this info in the case of APUs
  uint16_t CoreFrequency[8];            //[MHz]
  uint16_t CorePower[8];                //[mW]
  uint16_t CoreTemperature[8];          //[centi-Celsius]
  uint16_t L3Frequency;                 //[MHz]
  uint16_t L3Temperature;               //[centi-Celsius]

  uint16_t GfxTemperature;              //[centi-Celsius]
  uint16_t SocTemperature;              //[centi-Celsius]
  uint16_t ThrottlerStatus;

  uint16_t CurrentSocketPower;          //[mW]
  uint16_t StapmOriginalLimit;          //[W]
  uint16_t StapmCurrentLimit;           //[W]
  uint16_t ApuPower;                    //[W]
  uint16_t dGpuPower;                   //[W]

  uint16_t VddTdcValue;                 //[mA]
  uint16_t SocTdcValue;                 //[mA]
  uint16_t VddEdcValue;                 //[mA]
  uint16_t SocEdcValue;                 //[mA]

  uint16_t InfrastructureCpuMaxFreq;    //[MHz]
  uint16_t InfrastructureGfxMaxFreq;    //[MHz]
} SmuMetrics_t;


// Workload bits
#define WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT 0
#define WORKLOAD_PPLIB_VIDEO_BIT          2
#define WORKLOAD_PPLIB_VR_BIT             3
#define WORKLOAD_PPLIB_COMPUTE_BIT        4
#define WORKLOAD_PPLIB_CUSTOM_BIT         5
#define WORKLOAD_PPLIB_COUNT              6

#define TABLE_BIOS_IF            0 // Called by BIOS
#define TABLE_WATERMARKS         1 // Called by DAL through VBIOS
#define TABLE_CUSTOM_DPM         2 // Called by Driver
#define TABLE_SPARE1             3
#define TABLE_DPMCLOCKS          4 // Called by Driver
#define TABLE_MOMENTARY_PM       5 // Called by Tools
#define TABLE_MODERN_STDBY       6 // Called by Tools for Modern Standby Log
#define TABLE_SMU_METRICS        7 // Called by Driver
#define TABLE_COUNT              8

#endif

struct dcn31_watermarks {
  // Watermarks
  WatermarkRowGeneric_t WatermarkRow[WM_COUNT][NUM_WM_RANGES];

  uint32_t MmHubPadding[7]; // SMU internal use
};

struct dcn31_smu_dpm_clks {
	DpmClocks_t *dpm_clks;
	union large_integer mc_address;
};

/* TODO: taken from vgh, may not be correct */
struct display_idle_optimization {
	unsigned int df_request_disabled : 1;
	unsigned int phy_ref_clk_off     : 1;
	unsigned int s0i2_rdy            : 1;
	unsigned int reserved            : 29;
};

union display_idle_optimization_u {
	struct display_idle_optimization idle_info;
	uint32_t data;
};

int dcn31_smu_get_smu_version(struct clk_mgr_internal *clk_mgr);
int dcn31_smu_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_dispclk_khz);
int dcn31_smu_set_dprefclk(struct clk_mgr_internal *clk_mgr);
int dcn31_smu_set_hard_min_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_dcfclk_khz);
int dcn31_smu_set_min_deep_sleep_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_min_ds_dcfclk_khz);
int dcn31_smu_set_dppclk(struct clk_mgr_internal *clk_mgr, int requested_dpp_khz);
void dcn31_smu_set_display_idle_optimization(struct clk_mgr_internal *clk_mgr, uint32_t idle_info);
void dcn31_smu_enable_phy_refclk_pwrdwn(struct clk_mgr_internal *clk_mgr, bool enable);
void dcn31_smu_enable_pme_wa(struct clk_mgr_internal *clk_mgr);
void dcn31_smu_set_dram_addr_high(struct clk_mgr_internal *clk_mgr, uint32_t addr_high);
void dcn31_smu_set_dram_addr_low(struct clk_mgr_internal *clk_mgr, uint32_t addr_low);
void dcn31_smu_transfer_dpm_table_smu_2_dram(struct clk_mgr_internal *clk_mgr);
void dcn31_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr);

void dcn31_smu_set_Z9_support(struct clk_mgr_internal *clk_mgr, bool support);
void dcn31_smu_set_dtbclk(struct clk_mgr_internal *clk_mgr, bool enable);

#endif /* DAL_DC_31_SMU_H_ */
