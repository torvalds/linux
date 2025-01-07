/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef DAL_DC_35_SMU_H_
#define DAL_DC_35_SMU_H_

#include "os_types.h"

#ifndef PMFW_DRIVER_IF_H
#define PMFW_DRIVER_IF_H
#define PMFW_DRIVER_IF_VERSION 4

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

#define NUM_DCFCLK_DPM_LEVELS   8
#define NUM_DISPCLK_DPM_LEVELS  8
#define NUM_DPPCLK_DPM_LEVELS   8
#define NUM_SOCCLK_DPM_LEVELS   8
#define NUM_VCN_DPM_LEVELS      8
#define NUM_SOC_VOLTAGE_LEVELS  8
#define NUM_VPE_DPM_LEVELS        8
#define NUM_FCLK_DPM_LEVELS       8
#define NUM_MEM_PSTATE_LEVELS     4

typedef enum{
  WCK_RATIO_1_1 = 0,  // DDR5, Wck:ck is always 1:1;
  WCK_RATIO_1_2,
  WCK_RATIO_1_4,
  WCK_RATIO_MAX
} WCK_RATIO_e;

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
} DpmClocks_t_dcn35;

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
	uint8_t NumDcfClkLevelsEnabled;
	uint8_t NumDispClkLevelsEnabled; // Applies to both Dispclk and Dppclk
	uint8_t NumSocClkLevelsEnabled;
	uint8_t Vcn0ClkLevelsEnabled; // Applies to both Vclk0 and Dclk0
	uint8_t Vcn1ClkLevelsEnabled; // Applies to both Vclk1 and Dclk1
	uint8_t VpeClkLevelsEnabled;
	uint8_t NumMemPstatesEnabled;
	uint8_t NumFclkLevelsEnabled;
	uint32_t MinGfxClk;
	uint32_t MaxGfxClk;
} DpmClocks_t_dcn351;

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

struct dcn35_watermarks {
  // Watermarks
  WatermarkRowGeneric_t WatermarkRow[WM_COUNT][NUM_WM_RANGES];

  uint32_t MmHubPadding[7]; // SMU internal use
};

struct dcn35_smu_dpm_clks {
	DpmClocks_t_dcn35 *dpm_clks;
	union large_integer mc_address;
};

struct dcn351_smu_dpm_clks {
	DpmClocks_t_dcn351 *dpm_clks;
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

int dcn35_smu_get_smu_version(struct clk_mgr_internal *clk_mgr);
int dcn35_smu_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_dispclk_khz);
int dcn35_smu_set_dprefclk(struct clk_mgr_internal *clk_mgr);
int dcn35_smu_set_hard_min_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_dcfclk_khz);
int dcn35_smu_set_min_deep_sleep_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_min_ds_dcfclk_khz);
int dcn35_smu_set_dppclk(struct clk_mgr_internal *clk_mgr, int requested_dpp_khz);
void dcn35_smu_set_display_idle_optimization(struct clk_mgr_internal *clk_mgr, uint32_t idle_info);
void dcn35_smu_enable_phy_refclk_pwrdwn(struct clk_mgr_internal *clk_mgr, bool enable);
void dcn35_smu_enable_pme_wa(struct clk_mgr_internal *clk_mgr);
void dcn35_smu_set_dram_addr_high(struct clk_mgr_internal *clk_mgr, uint32_t addr_high);
void dcn35_smu_set_dram_addr_low(struct clk_mgr_internal *clk_mgr, uint32_t addr_low);
void dcn35_smu_transfer_dpm_table_smu_2_dram(struct clk_mgr_internal *clk_mgr);
void dcn35_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr);

void dcn35_smu_set_zstate_support(struct clk_mgr_internal *clk_mgr, enum dcn_zstate_support_state support);
void dcn35_smu_set_dtbclk(struct clk_mgr_internal *clk_mgr, bool enable);
void dcn35_vbios_smu_enable_48mhz_tmdp_refclk_pwrdwn(struct clk_mgr_internal *clk_mgr, bool enable);

int dcn35_smu_exit_low_power_state(struct clk_mgr_internal *clk_mgr);
int dcn35_smu_get_ips_supported(struct clk_mgr_internal *clk_mgr);
int dcn35_smu_get_dtbclk(struct clk_mgr_internal *clk_mgr);
int dcn35_smu_get_dprefclk(struct clk_mgr_internal *clk_mgr);
void dcn35_smu_notify_host_router_bw(struct clk_mgr_internal *clk_mgr, uint32_t hr_id, uint32_t bw_kbps);

#endif /* DAL_DC_35_SMU_H_ */
