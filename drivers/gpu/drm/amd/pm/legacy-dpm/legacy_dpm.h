/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#ifndef __LEGACY_DPM_H__
#define __LEGACY_DPM_H__

void amdgpu_dpm_print_class_info(u32 class, u32 class2);
void amdgpu_dpm_print_cap_info(u32 caps);
void amdgpu_dpm_print_ps_status(struct amdgpu_device *adev,
				struct amdgpu_ps *rps);
int amdgpu_get_platform_caps(struct amdgpu_device *adev);
int amdgpu_parse_extended_power_table(struct amdgpu_device *adev);
void amdgpu_free_extended_power_table(struct amdgpu_device *adev);
void amdgpu_add_thermal_controller(struct amdgpu_device *adev);
struct amd_vce_state* amdgpu_get_vce_clock_state(void *handle, u32 idx);
void amdgpu_pm_print_power_states(struct amdgpu_device *adev);
void amdgpu_legacy_dpm_compute_clocks(void *handle);
void amdgpu_dpm_thermal_work_handler(struct work_struct *work);
#endif
