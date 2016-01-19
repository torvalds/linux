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

#ifndef _CZ_CLOCK_POWER_GATING_H_
#define _CZ_CLOCK_POWER_GATING_H_

#include "cz_hwmgr.h"
#include "pp_asicblocks.h"

extern int cz_phm_set_asic_block_gating(struct pp_hwmgr *hwmgr, enum PHM_AsicBlock block, enum PHM_ClockGateSetting gating);
extern struct phm_master_table_header cz_phm_enable_clock_power_gatings_master;
extern struct phm_master_table_header cz_phm_disable_clock_power_gatings_master;
extern int cz_dpm_powergate_vce(struct pp_hwmgr *hwmgr, bool bgate);
extern int cz_dpm_powergate_uvd(struct pp_hwmgr *hwmgr, bool bgate);
extern int cz_enable_disable_vce_dpm(struct pp_hwmgr *hwmgr, bool enable);
extern int cz_enable_disable_uvd_dpm(struct pp_hwmgr *hwmgr, bool enable);
#endif /* _CZ_CLOCK_POWER_GATING_H_ */
