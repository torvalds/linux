/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
#ifndef __AMDGPU_DPM_H__
#define __AMDGPU_DPM_H__

#define R600_SSTU_DFLT                               0
#define R600_SST_DFLT                                0x00C8

/* XXX are these ok? */
#define R600_TEMP_RANGE_MIN (90 * 1000)
#define R600_TEMP_RANGE_MAX (120 * 1000)

#define FDO_PWM_MODE_STATIC  1
#define FDO_PWM_MODE_STATIC_RPM 5

enum amdgpu_td {
	AMDGPU_TD_AUTO,
	AMDGPU_TD_UP,
	AMDGPU_TD_DOWN,
};

enum amdgpu_display_watermark {
	AMDGPU_DISPLAY_WATERMARK_LOW = 0,
	AMDGPU_DISPLAY_WATERMARK_HIGH = 1,
};

enum amdgpu_display_gap
{
    AMDGPU_PM_DISPLAY_GAP_VBLANK_OR_WM = 0,
    AMDGPU_PM_DISPLAY_GAP_VBLANK       = 1,
    AMDGPU_PM_DISPLAY_GAP_WATERMARK    = 2,
    AMDGPU_PM_DISPLAY_GAP_IGNORE       = 3,
};

void amdgpu_dpm_print_class_info(u32 class, u32 class2);
void amdgpu_dpm_print_cap_info(u32 caps);
void amdgpu_dpm_print_ps_status(struct amdgpu_device *adev,
				struct amdgpu_ps *rps);
u32 amdgpu_dpm_get_vblank_time(struct amdgpu_device *adev);
u32 amdgpu_dpm_get_vrefresh(struct amdgpu_device *adev);
bool amdgpu_is_uvd_state(u32 class, u32 class2);
void amdgpu_calculate_u_and_p(u32 i, u32 r_c, u32 p_b,
			      u32 *p, u32 *u);
int amdgpu_calculate_at(u32 t, u32 h, u32 fh, u32 fl, u32 *tl, u32 *th);

bool amdgpu_is_internal_thermal_sensor(enum amdgpu_int_thermal_type sensor);

int amdgpu_get_platform_caps(struct amdgpu_device *adev);

int amdgpu_parse_extended_power_table(struct amdgpu_device *adev);
void amdgpu_free_extended_power_table(struct amdgpu_device *adev);

void amdgpu_add_thermal_controller(struct amdgpu_device *adev);

enum amdgpu_pcie_gen amdgpu_get_pcie_gen_support(struct amdgpu_device *adev,
						 u32 sys_mask,
						 enum amdgpu_pcie_gen asic_gen,
						 enum amdgpu_pcie_gen default_gen);

u16 amdgpu_get_pcie_lane_support(struct amdgpu_device *adev,
				 u16 asic_lanes,
				 u16 default_lanes);
u8 amdgpu_encode_pci_lane_width(u32 lanes);

#endif
