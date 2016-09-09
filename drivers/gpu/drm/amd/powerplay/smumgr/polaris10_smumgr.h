/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef _POLARIS10_SMUMANAGER_H
#define _POLARIS10_SMUMANAGER_H


#include <pp_endian.h>
#include "smu74.h"
#include "smu74_discrete.h"


#define SMC_RAM_END 0x40000

struct polaris10_avfs {
	enum AVFS_BTC_STATUS avfs_btc_status;
	uint32_t           avfs_btc_param;
};

struct polaris10_pt_defaults {
	uint8_t   SviLoadLineEn;
	uint8_t   SviLoadLineVddC;
	uint8_t   TDC_VDDC_ThrottleReleaseLimitPerc;
	uint8_t   TDC_MAWt;
	uint8_t   TdcWaterfallCtl;
	uint8_t   DTEAmbientTempBase;

	uint32_t  DisplayCac;
	uint32_t  BAPM_TEMP_GRADIENT;
	uint16_t  BAPMTI_R[SMU74_DTE_ITERATIONS * SMU74_DTE_SOURCES * SMU74_DTE_SINKS];
	uint16_t  BAPMTI_RC[SMU74_DTE_ITERATIONS * SMU74_DTE_SOURCES * SMU74_DTE_SINKS];
};

struct polaris10_buffer_entry {
	uint32_t data_size;
	uint32_t mc_addr_low;
	uint32_t mc_addr_high;
	void *kaddr;
	unsigned long  handle;
};

struct polaris10_range_table {
	uint32_t trans_lower_frequency; /* in 10khz */
	uint32_t trans_upper_frequency;
};

struct polaris10_smumgr {
	uint8_t *header;
	uint8_t *mec_image;
	struct polaris10_buffer_entry smu_buffer;
	struct polaris10_buffer_entry header_buffer;

	uint32_t                             soft_regs_start;
	uint32_t                             dpm_table_start;
	uint32_t                             mc_reg_table_start;
	uint32_t                             fan_table_start;
	uint32_t                             arb_table_start;

	uint8_t *read_rrm_straps;
	uint32_t read_drm_straps_mc_address_high;
	uint32_t read_drm_straps_mc_address_low;
	uint32_t acpi_optimization;
	bool post_initial_boot;
	uint8_t protected_mode;
	uint8_t security_hard_key;
	struct polaris10_avfs  avfs;
	SMU74_Discrete_DpmTable              smc_state_table;
	struct SMU74_Discrete_Ulv            ulv_setting;
	struct SMU74_Discrete_PmFuses  power_tune_table;
	struct polaris10_range_table                range_table[NUM_SCLK_RANGE];
	const struct polaris10_pt_defaults       *power_tune_defaults;
	uint32_t                   activity_target[SMU74_MAX_LEVELS_GRAPHICS];
	uint32_t                   bif_sclk_table[SMU74_MAX_LEVELS_LINK];
};


int polaris10_smum_init(struct pp_smumgr *smumgr);
int polaris10_read_smc_sram_dword(struct pp_smumgr *smumgr, uint32_t smc_addr, uint32_t *value, uint32_t limit);
int polaris10_write_smc_sram_dword(struct pp_smumgr *smumgr, uint32_t smc_addr, uint32_t value, uint32_t limit);
int polaris10_copy_bytes_to_smc(struct pp_smumgr *smumgr, uint32_t smc_start_address,
				const uint8_t *src, uint32_t byte_count, uint32_t limit);

#endif
