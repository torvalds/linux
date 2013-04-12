/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
#ifndef __BTC_DPM_H__
#define __BTC_DPM_H__

#define BTC_RLP_UVD_DFLT                              20
#define BTC_RMP_UVD_DFLT                              50
#define BTC_LHP_UVD_DFLT                              50
#define BTC_LMP_UVD_DFLT                              20
#define BARTS_MGCGCGTSSMCTRL_DFLT                     0x81944000
#define TURKS_MGCGCGTSSMCTRL_DFLT                     0x6e944000
#define CAICOS_MGCGCGTSSMCTRL_DFLT                    0x46944040
#define BTC_CGULVPARAMETER_DFLT                       0x00040035
#define BTC_CGULVCONTROL_DFLT                         0x00001450

extern u32 btc_valid_sclk[40];

void btc_read_arb_registers(struct radeon_device *rdev);
void btc_program_mgcg_hw_sequence(struct radeon_device *rdev,
				  const u32 *sequence, u32 count);
void btc_skip_blacklist_clocks(struct radeon_device *rdev,
			       const u32 max_sclk, const u32 max_mclk,
			       u32 *sclk, u32 *mclk);
void btc_adjust_clock_combinations(struct radeon_device *rdev,
				   const struct radeon_clock_and_voltage_limits *max_limits,
				   struct rv7xx_pl *pl);
void btc_apply_voltage_dependency_rules(struct radeon_clock_voltage_dependency_table *table,
					u32 clock, u16 max_voltage, u16 *voltage);
void btc_apply_voltage_delta_rules(struct radeon_device *rdev,
				   u16 max_vddc, u16 max_vddci,
				   u16 *vddc, u16 *vddci);
bool btc_dpm_enabled(struct radeon_device *rdev);
int btc_reset_to_default(struct radeon_device *rdev);
void btc_notify_uvd_to_smc(struct radeon_device *rdev);

#endif
