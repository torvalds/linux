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
 * Authors: AMD
 *
 */

#ifndef DAL_DC_301_SMU_H_
#define DAL_DC_301_SMU_H_

#define SMU13_DRIVER_IF_VERSION 2

typedef struct {
	uint32_t fclk;
	uint32_t memclk;
	uint32_t voltage;
} df_pstate_t;

typedef struct {
	uint32_t vclk;
	uint32_t dclk;
} vcn_clk_t;

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


#define TABLE_WATERMARKS         1
#define TABLE_DPMCLOCKS          4 // Called by Driver


#define VG_NUM_DCFCLK_DPM_LEVELS   7
#define VG_NUM_DISPCLK_DPM_LEVELS  7
#define VG_NUM_DPPCLK_DPM_LEVELS   7
#define VG_NUM_SOCCLK_DPM_LEVELS   7
#define VG_NUM_ISPICLK_DPM_LEVELS  7
#define VG_NUM_ISPXCLK_DPM_LEVELS  7
#define VG_NUM_VCN_DPM_LEVELS      5
#define VG_NUM_FCLK_DPM_LEVELS     4
#define VG_NUM_SOC_VOLTAGE_LEVELS  8

// copy from vgh/vangogh/pmfw_driver_if.h
struct vg_dpm_clocks {
	uint32_t DcfClocks[VG_NUM_DCFCLK_DPM_LEVELS];
	uint32_t DispClocks[VG_NUM_DISPCLK_DPM_LEVELS];
	uint32_t DppClocks[VG_NUM_DPPCLK_DPM_LEVELS];
	uint32_t SocClocks[VG_NUM_SOCCLK_DPM_LEVELS];
	uint32_t IspiClocks[VG_NUM_ISPICLK_DPM_LEVELS];
	uint32_t IspxClocks[VG_NUM_ISPXCLK_DPM_LEVELS];
	vcn_clk_t VcnClocks[VG_NUM_VCN_DPM_LEVELS];

	uint32_t SocVoltage[VG_NUM_SOC_VOLTAGE_LEVELS];

	df_pstate_t DfPstateTable[VG_NUM_FCLK_DPM_LEVELS];

	uint32_t MinGfxClk;
	uint32_t MaxGfxClk;

	uint8_t NumDfPstatesEnabled;
	uint8_t NumDcfclkLevelsEnabled;
	uint8_t NumDispClkLevelsEnabled;  //applies to both dispclk and dppclk
	uint8_t NumSocClkLevelsEnabled;

	uint8_t IspClkLevelsEnabled;  //applies to both ispiclk and ispxclk
	uint8_t VcnClkLevelsEnabled;  //applies to both vclk/dclk
	uint8_t spare[2];
};

struct smu_dpm_clks {
	struct vg_dpm_clocks *dpm_clks;
	union large_integer mc_address;
};

struct watermarks {
  // Watermarks
	WatermarkRowGeneric_t WatermarkRow[WM_COUNT][NUM_WM_RANGES];

	uint32_t     MmHubPadding[7]; // SMU internal use
};


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


int dcn301_smu_get_smu_version(struct clk_mgr_internal *clk_mgr);
int dcn301_smu_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_dispclk_khz);
int dcn301_smu_set_dprefclk(struct clk_mgr_internal *clk_mgr);
int dcn301_smu_set_hard_min_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_dcfclk_khz);
int dcn301_smu_set_min_deep_sleep_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_min_ds_dcfclk_khz);
int dcn301_smu_set_dppclk(struct clk_mgr_internal *clk_mgr, int requested_dpp_khz);
void dcn301_smu_set_display_idle_optimization(struct clk_mgr_internal *clk_mgr, uint32_t idle_info);
void dcn301_smu_enable_phy_refclk_pwrdwn(struct clk_mgr_internal *clk_mgr, bool enable);
void dcn301_smu_enable_pme_wa(struct clk_mgr_internal *clk_mgr);
void dcn301_smu_set_dram_addr_high(struct clk_mgr_internal *clk_mgr, uint32_t addr_high);
void dcn301_smu_set_dram_addr_low(struct clk_mgr_internal *clk_mgr, uint32_t addr_low);
void dcn301_smu_transfer_dpm_table_smu_2_dram(struct clk_mgr_internal *clk_mgr);
void dcn301_smu_transfer_wm_table_dram_2_smu(struct clk_mgr_internal *clk_mgr);

#endif /* DAL_DC_301_SMU_H_ */
