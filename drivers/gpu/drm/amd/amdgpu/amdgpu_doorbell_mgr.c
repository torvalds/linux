// SPDX-License-Identifier: MIT
/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#include "amdgpu.h"

/**
 * amdgpu_mm_rdoorbell - read a doorbell dword
 *
 * @adev: amdgpu_device pointer
 * @index: doorbell index
 *
 * Returns the value in the doorbell aperture at the
 * requested doorbell index (CIK).
 */
u32 amdgpu_mm_rdoorbell(struct amdgpu_device *adev, u32 index)
{
	if (amdgpu_device_skip_hw_access(adev))
		return 0;

	if (index < adev->doorbell.num_kernel_doorbells)
		return readl(adev->doorbell.ptr + index);

	DRM_ERROR("reading beyond doorbell aperture: 0x%08x!\n", index);
	return 0;
}

/**
 * amdgpu_mm_wdoorbell - write a doorbell dword
 *
 * @adev: amdgpu_device pointer
 * @index: doorbell index
 * @v: value to write
 *
 * Writes @v to the doorbell aperture at the
 * requested doorbell index (CIK).
 */
void amdgpu_mm_wdoorbell(struct amdgpu_device *adev, u32 index, u32 v)
{
	if (amdgpu_device_skip_hw_access(adev))
		return;

	if (index < adev->doorbell.num_kernel_doorbells)
		writel(v, adev->doorbell.ptr + index);
	else
		DRM_ERROR("writing beyond doorbell aperture: 0x%08x!\n", index);
}

/**
 * amdgpu_mm_rdoorbell64 - read a doorbell Qword
 *
 * @adev: amdgpu_device pointer
 * @index: doorbell index
 *
 * Returns the value in the doorbell aperture at the
 * requested doorbell index (VEGA10+).
 */
u64 amdgpu_mm_rdoorbell64(struct amdgpu_device *adev, u32 index)
{
	if (amdgpu_device_skip_hw_access(adev))
		return 0;

	if (index < adev->doorbell.num_kernel_doorbells)
		return atomic64_read((atomic64_t *)(adev->doorbell.ptr + index));

	DRM_ERROR("reading beyond doorbell aperture: 0x%08x!\n", index);
	return 0;
}

/**
 * amdgpu_mm_wdoorbell64 - write a doorbell Qword
 *
 * @adev: amdgpu_device pointer
 * @index: doorbell index
 * @v: value to write
 *
 * Writes @v to the doorbell aperture at the
 * requested doorbell index (VEGA10+).
 */
void amdgpu_mm_wdoorbell64(struct amdgpu_device *adev, u32 index, u64 v)
{
	if (amdgpu_device_skip_hw_access(adev))
		return;

	if (index < adev->doorbell.num_kernel_doorbells)
		atomic64_set((atomic64_t *)(adev->doorbell.ptr + index), v);
	else
		DRM_ERROR("writing beyond doorbell aperture: 0x%08x!\n", index);
}

/*
 * GPU doorbell aperture helpers function.
 */
/**
 * amdgpu_doorbell_init - Init doorbell driver information.
 *
 * @adev: amdgpu_device pointer
 *
 * Init doorbell driver information (CIK)
 * Returns 0 on success, error on failure.
 */
int amdgpu_doorbell_init(struct amdgpu_device *adev)
{

	/* No doorbell on SI hardware generation */
	if (adev->asic_type < CHIP_BONAIRE) {
		adev->doorbell.base = 0;
		adev->doorbell.size = 0;
		adev->doorbell.num_kernel_doorbells = 0;
		adev->doorbell.ptr = NULL;
		return 0;
	}

	if (pci_resource_flags(adev->pdev, 2) & IORESOURCE_UNSET)
		return -EINVAL;

	amdgpu_asic_init_doorbell_index(adev);

	/* doorbell bar mapping */
	adev->doorbell.base = pci_resource_start(adev->pdev, 2);
	adev->doorbell.size = pci_resource_len(adev->pdev, 2);

	if (adev->enable_mes) {
		adev->doorbell.num_kernel_doorbells =
			adev->doorbell.size / sizeof(u32);
	} else {
		adev->doorbell.num_kernel_doorbells =
			min_t(u32, adev->doorbell.size / sizeof(u32),
			      adev->doorbell_index.max_assignment+1);
		if (adev->doorbell.num_kernel_doorbells == 0)
			return -EINVAL;

		/* For Vega, reserve and map two pages on doorbell BAR since SDMA
		 * paging queue doorbell use the second page. The
		 * AMDGPU_DOORBELL64_MAX_ASSIGNMENT definition assumes all the
		 * doorbells are in the first page. So with paging queue enabled,
		 * the max num_kernel_doorbells should + 1 page (0x400 in dword)
		 */
		if (adev->asic_type >= CHIP_VEGA10)
			adev->doorbell.num_kernel_doorbells += 0x400;
	}

	adev->doorbell.ptr = ioremap(adev->doorbell.base,
				     adev->doorbell.num_kernel_doorbells *
				     sizeof(u32));
	if (adev->doorbell.ptr == NULL)
		return -ENOMEM;

	return 0;
}

/**
 * amdgpu_doorbell_fini - Tear down doorbell driver information.
 *
 * @adev: amdgpu_device pointer
 *
 * Tear down doorbell driver information (CIK)
 */
void amdgpu_doorbell_fini(struct amdgpu_device *adev)
{
	iounmap(adev->doorbell.ptr);
	adev->doorbell.ptr = NULL;
}
