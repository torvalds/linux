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

#ifndef __AMDGPU_PM_H__
#define __AMDGPU_PM_H__

struct cg_flag_name
{
	u32 flag;
	const char *name;
};

void amdgpu_pm_acpi_event_handler(struct amdgpu_device *adev);
int amdgpu_pm_sysfs_init(struct amdgpu_device *adev);
void amdgpu_pm_sysfs_fini(struct amdgpu_device *adev);
void amdgpu_pm_print_power_states(struct amdgpu_device *adev);
void amdgpu_pm_compute_clocks(struct amdgpu_device *adev);
void amdgpu_dpm_thermal_work_handler(struct work_struct *work);
void amdgpu_dpm_enable_uvd(struct amdgpu_device *adev, bool enable);
void amdgpu_dpm_enable_vce(struct amdgpu_device *adev, bool enable);

#endif
