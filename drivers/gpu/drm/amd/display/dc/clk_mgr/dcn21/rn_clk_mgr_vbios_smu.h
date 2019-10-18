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

#ifndef DAL_DC_RN_CLK_MGR_VBIOS_SMU_H_
#define DAL_DC_RN_CLK_MGR_VBIOS_SMU_H_

int rn_vbios_smu_get_smu_version(struct clk_mgr_internal *clk_mgr);
int rn_vbios_smu_set_dispclk(struct clk_mgr_internal *clk_mgr, int requested_dispclk_khz);
int rn_vbios_smu_set_dprefclk(struct clk_mgr_internal *clk_mgr);
int rn_vbios_smu_set_hard_min_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_dcfclk_khz);
int rn_vbios_smu_set_min_deep_sleep_dcfclk(struct clk_mgr_internal *clk_mgr, int requested_min_ds_dcfclk_khz);
void rn_vbios_smu_set_phyclk(struct clk_mgr_internal *clk_mgr, int requested_phyclk_khz);
int rn_vbios_smu_set_dppclk(struct clk_mgr_internal *clk_mgr, int requested_dpp_khz);
void rn_vbios_smu_set_display_count(struct clk_mgr_internal *clk_mgr, int display_count);
void rn_vbios_smu_enable_48mhz_tmdp_refclk_pwrdwn(struct clk_mgr_internal *clk_mgr);
void rn_vbios_smu_enable_pme_wa(struct clk_mgr_internal *clk_mgr);

#endif /* DAL_DC_DCN10_RV1_CLK_MGR_VBIOS_SMU_H_ */
