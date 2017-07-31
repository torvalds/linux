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
#ifndef _SMU7_POWERTUNE_H
#define _SMU7_POWERTUNE_H

#define DIDT_SQ_CTRL0__UNUSED_0_MASK    0xfffc0000
#define DIDT_SQ_CTRL0__UNUSED_0__SHIFT  0x12
#define DIDT_TD_CTRL0__UNUSED_0_MASK    0xfffc0000
#define DIDT_TD_CTRL0__UNUSED_0__SHIFT  0x12
#define DIDT_TCP_CTRL0__UNUSED_0_MASK   0xfffc0000
#define DIDT_TCP_CTRL0__UNUSED_0__SHIFT 0x12
#define DIDT_SQ_TUNING_CTRL__UNUSED_0_MASK                 0xc0000000
#define DIDT_SQ_TUNING_CTRL__UNUSED_0__SHIFT               0x0000001e
#define DIDT_TD_TUNING_CTRL__UNUSED_0_MASK                 0xc0000000
#define DIDT_TD_TUNING_CTRL__UNUSED_0__SHIFT               0x0000001e
#define DIDT_TCP_TUNING_CTRL__UNUSED_0_MASK                0xc0000000
#define DIDT_TCP_TUNING_CTRL__UNUSED_0__SHIFT              0x0000001e

/* PowerContainment Features */
#define POWERCONTAINMENT_FEATURE_DTE             0x00000001
#define POWERCONTAINMENT_FEATURE_TDCLimit        0x00000002
#define POWERCONTAINMENT_FEATURE_PkgPwrLimit     0x00000004

#define ixGC_CAC_CNTL 0x0000
#define ixDIDT_SQ_STALL_CTRL 0x0004
#define ixDIDT_SQ_TUNING_CTRL 0x0005
#define ixDIDT_TD_STALL_CTRL 0x0044
#define ixDIDT_TD_TUNING_CTRL 0x0045
#define ixDIDT_TCP_STALL_CTRL 0x0064
#define ixDIDT_TCP_TUNING_CTRL 0x0065


int smu7_enable_smc_cac(struct pp_hwmgr *hwmgr);
int smu7_disable_smc_cac(struct pp_hwmgr *hwmgr);
int smu7_enable_power_containment(struct pp_hwmgr *hwmgr);
int smu7_disable_power_containment(struct pp_hwmgr *hwmgr);
int smu7_set_power_limit(struct pp_hwmgr *hwmgr, uint32_t n);
int smu7_power_control_set_level(struct pp_hwmgr *hwmgr);
int smu7_enable_didt_config(struct pp_hwmgr *hwmgr);
int smu7_disable_didt_config(struct pp_hwmgr *hwmgr);
#endif  /* DGPU_POWERTUNE_H */

