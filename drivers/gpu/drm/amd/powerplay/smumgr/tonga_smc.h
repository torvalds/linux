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
#ifndef _TONGA_SMC_H
#define _TONGA_SMC_H

#include "smumgr.h"
#include "smu72.h"


#define ASICID_IS_TONGA_P(wDID, bRID)	 \
	(((wDID == 0x6930) && ((bRID == 0xF0) || (bRID == 0xF1) || (bRID == 0xFF))) \
	|| ((wDID == 0x6920) && ((bRID == 0) || (bRID == 1))))


struct tonga_pt_defaults {
	uint8_t   svi_load_line_en;
	uint8_t   svi_load_line_vddC;
	uint8_t   tdc_vddc_throttle_release_limit_perc;
	uint8_t   tdc_mawt;
	uint8_t   tdc_waterfall_ctl;
	uint8_t   dte_ambient_temp_base;
	uint32_t  display_cac;
	uint32_t  bamp_temp_gradient;
	uint16_t  bapmti_r[SMU72_DTE_ITERATIONS * SMU72_DTE_SOURCES * SMU72_DTE_SINKS];
	uint16_t  bapmti_rc[SMU72_DTE_ITERATIONS * SMU72_DTE_SOURCES * SMU72_DTE_SINKS];
};

int tonga_populate_all_graphic_levels(struct pp_hwmgr *hwmgr);
int tonga_populate_all_memory_levels(struct pp_hwmgr *hwmgr);
int tonga_init_smc_table(struct pp_hwmgr *hwmgr);
int tonga_thermal_setup_fan_table(struct pp_hwmgr *hwmgr);
int tonga_update_smc_table(struct pp_hwmgr *hwmgr, uint32_t type);
int tonga_update_sclk_threshold(struct pp_hwmgr *hwmgr);
uint32_t tonga_get_offsetof(uint32_t type, uint32_t member);
uint32_t tonga_get_mac_definition(uint32_t value);
int tonga_process_firmware_header(struct pp_hwmgr *hwmgr);
int tonga_initialize_mc_reg_table(struct pp_hwmgr *hwmgr);
bool tonga_is_dpm_running(struct pp_hwmgr *hwmgr);
#endif

