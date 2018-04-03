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
#ifndef _VEGA12_POWERTUNE_H_
#define _VEGA12_POWERTUNE_H_

enum vega12_didt_config_reg_type {
	VEGA12_CONFIGREG_DIDT = 0,
	VEGA12_CONFIGREG_GCCAC,
	VEGA12_CONFIGREG_SECAC
};

/* PowerContainment Features */
#define POWERCONTAINMENT_FEATURE_DTE             0x00000001
#define POWERCONTAINMENT_FEATURE_TDCLimit        0x00000002
#define POWERCONTAINMENT_FEATURE_PkgPwrLimit     0x00000004

struct vega12_didt_config_reg {
	uint32_t		offset;
	uint32_t		mask;
	uint32_t		shift;
	uint32_t		value;
};

int vega12_enable_power_containment(struct pp_hwmgr *hwmgr);
int vega12_set_power_limit(struct pp_hwmgr *hwmgr, uint32_t n);
int vega12_power_control_set_level(struct pp_hwmgr *hwmgr);
int vega12_disable_power_containment(struct pp_hwmgr *hwmgr);

int vega12_enable_didt_config(struct pp_hwmgr *hwmgr);
int vega12_disable_didt_config(struct pp_hwmgr *hwmgr);

#endif  /* _VEGA12_POWERTUNE_H_ */

