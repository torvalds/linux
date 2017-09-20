/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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
#ifndef CI_SMC_H
#define CI_SMC_H

#include <linux/types.h>


struct pp_smumgr;
struct pp_hwmgr;
struct amd_pp_profile;

int ci_send_msg_to_smc_with_parameter(struct pp_hwmgr *hwmgr,
					uint16_t msg, uint32_t parameter);
int ci_send_msg_to_smc(struct pp_hwmgr *hwmgr, uint16_t msg);
int ci_populate_all_graphic_levels(struct pp_hwmgr *hwmgr);
int ci_populate_all_memory_levels(struct pp_hwmgr *hwmgr);
int ci_init_smc_table(struct pp_hwmgr *hwmgr);
int ci_thermal_setup_fan_table(struct pp_hwmgr *hwmgr);
int ci_update_smc_table(struct pp_hwmgr *hwmgr, uint32_t type);
int ci_update_sclk_threshold(struct pp_hwmgr *hwmgr);
uint32_t ci_get_offsetof(uint32_t type, uint32_t member);
uint32_t ci_get_mac_definition(uint32_t value);
int ci_process_firmware_header(struct pp_hwmgr *hwmgr);
int ci_initialize_mc_reg_table(struct pp_hwmgr *hwmgr);
bool ci_is_dpm_running(struct pp_hwmgr *hwmgr);
int ci_populate_requested_graphic_levels(struct pp_hwmgr *hwmgr,
					struct amd_pp_profile *request);


#endif

