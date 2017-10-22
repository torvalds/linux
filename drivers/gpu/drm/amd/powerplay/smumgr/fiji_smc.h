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
#ifndef FIJI_SMC_H
#define FIJI_SMC_H

#include "smumgr.h"
#include "smu73.h"

struct fiji_pt_defaults {
	uint8_t   SviLoadLineEn;
	uint8_t   SviLoadLineVddC;
	uint8_t   TDC_VDDC_ThrottleReleaseLimitPerc;
	uint8_t   TDC_MAWt;
	uint8_t   TdcWaterfallCtl;
	uint8_t   DTEAmbientTempBase;
};

int fiji_populate_all_graphic_levels(struct pp_hwmgr *hwmgr);
int fiji_populate_all_memory_levels(struct pp_hwmgr *hwmgr);
int fiji_init_smc_table(struct pp_hwmgr *hwmgr);
int fiji_thermal_setup_fan_table(struct pp_hwmgr *hwmgr);
int fiji_update_smc_table(struct pp_hwmgr *hwmgr, uint32_t type);
int fiji_update_sclk_threshold(struct pp_hwmgr *hwmgr);
uint32_t fiji_get_offsetof(uint32_t type, uint32_t member);
uint32_t fiji_get_mac_definition(uint32_t value);
int fiji_process_firmware_header(struct pp_hwmgr *hwmgr);
int fiji_initialize_mc_reg_table(struct pp_hwmgr *hwmgr);
bool fiji_is_dpm_running(struct pp_hwmgr *hwmgr);
int fiji_populate_requested_graphic_levels(struct pp_hwmgr *hwmgr,
		struct amd_pp_profile *request);
int fiji_thermal_avfs_enable(struct pp_hwmgr *hwmgr);
#endif

