/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#include <linux/firmware.h>
#include <linux/mfd/core.h>

#include "amdgpu.h"
#include "amdgpu_isp.h"
#include "isp_v4_1_0.h"
#include "isp_v4_1_1.h"

#define ISP_MC_ADDR_ALIGN (1024 * 32)

/**
 * isp_hw_init - start and test isp block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 */
static int isp_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_isp *isp = &adev->isp;

	if (isp->funcs->hw_init != NULL)
		return isp->funcs->hw_init(isp);

	return -ENODEV;
}

/**
 * isp_hw_fini - stop the hardware block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 */
static int isp_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_isp *isp = &ip_block->adev->isp;

	if (isp->funcs->hw_fini != NULL)
		return isp->funcs->hw_fini(isp);

	return -ENODEV;
}

static int isp_load_fw_by_psp(struct amdgpu_device *adev)
{
	const struct common_firmware_header *hdr;
	char ucode_prefix[10];
	int r = 0;

	/* get isp fw binary name and path */
	amdgpu_ucode_ip_version_decode(adev, ISP_HWIP, ucode_prefix,
				       sizeof(ucode_prefix));

	/* read isp fw */
	r = amdgpu_ucode_request(adev, &adev->isp.fw, AMDGPU_UCODE_OPTIONAL,
				"amdgpu/%s.bin", ucode_prefix);
	if (r) {
		amdgpu_ucode_release(&adev->isp.fw);
		return r;
	}

	hdr = (const struct common_firmware_header *)adev->isp.fw->data;

	adev->firmware.ucode[AMDGPU_UCODE_ID_ISP].ucode_id =
		AMDGPU_UCODE_ID_ISP;
	adev->firmware.ucode[AMDGPU_UCODE_ID_ISP].fw = adev->isp.fw;

	adev->firmware.fw_size +=
		ALIGN(le32_to_cpu(hdr->ucode_size_bytes), PAGE_SIZE);

	return r;
}

static int isp_early_init(struct amdgpu_ip_block *ip_block)
{

	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_isp *isp = &adev->isp;

	switch (amdgpu_ip_version(adev, ISP_HWIP, 0)) {
	case IP_VERSION(4, 1, 0):
		isp_v4_1_0_set_isp_funcs(isp);
		break;
	case IP_VERSION(4, 1, 1):
		isp_v4_1_1_set_isp_funcs(isp);
		break;
	default:
		return -EINVAL;
	}

	isp->adev = adev;
	isp->parent = adev->dev;

	if (isp_load_fw_by_psp(adev)) {
		DRM_DEBUG_DRIVER("%s: isp fw load failed\n", __func__);
		return -ENOENT;
	}

	return 0;
}

static bool isp_is_idle(struct amdgpu_ip_block *ip_block)
{
	return true;
}

static int isp_set_clockgating_state(struct amdgpu_ip_block *ip_block,
				     enum amd_clockgating_state state)
{
	return 0;
}

static int isp_set_powergating_state(struct amdgpu_ip_block *ip_block,
				     enum amd_powergating_state state)
{
	return 0;
}

static int is_valid_isp_device(struct device *isp_parent, struct device *amdgpu_dev)
{
	if (isp_parent != amdgpu_dev)
		return -EINVAL;

	return 0;
}

/**
 * isp_user_buffer_alloc - create user buffer object (BO) for isp
 *
 * @dev: isp device handle
 * @dmabuf: DMABUF handle for isp buffer allocated in system memory
 * @buf_obj: GPU buffer object handle to initialize
 * @buf_addr: GPU addr of the pinned BO to initialize
 *
 * Imports isp DMABUF to allocate and pin a user BO for isp internal use. It does
 * GART alloc to generate GPU addr for BO to make it accessible through the
 * GART aperture for ISP HW.
 *
 * This function is exported to allow the V4L2 isp device external to drm device
 * to create and access the isp user BO.
 *
 * Returns:
 * 0 on success, negative error code otherwise.
 */
int isp_user_buffer_alloc(struct device *dev, void *dmabuf,
			  void **buf_obj, u64 *buf_addr)
{
	struct platform_device *ispdev = to_platform_device(dev);
	const struct isp_platform_data *isp_pdata;
	struct amdgpu_device *adev;
	struct mfd_cell *mfd_cell;
	struct amdgpu_bo *bo;
	u64 gpu_addr;
	int ret;

	if (WARN_ON(!ispdev))
		return -ENODEV;

	if (WARN_ON(!buf_obj))
		return -EINVAL;

	if (WARN_ON(!buf_addr))
		return -EINVAL;

	mfd_cell = &ispdev->mfd_cell[0];
	if (!mfd_cell)
		return -ENODEV;

	isp_pdata = mfd_cell->platform_data;
	adev = isp_pdata->adev;

	ret = is_valid_isp_device(ispdev->dev.parent, adev->dev);
	if (ret)
		return ret;

	ret = amdgpu_bo_create_isp_user(adev, dmabuf,
					AMDGPU_GEM_DOMAIN_GTT, &bo, &gpu_addr);
	if (ret) {
		drm_err(&adev->ddev, "failed to alloc gart user buffer (%d)", ret);
		return ret;
	}

	*buf_obj = (void *)bo;
	*buf_addr = gpu_addr;

	return 0;
}
EXPORT_SYMBOL(isp_user_buffer_alloc);

/**
 * isp_user_buffer_free - free isp user buffer object (BO)
 *
 * @buf_obj: amdgpu isp user BO to free
 *
 * unpin and unref BO for isp internal use.
 *
 * This function is exported to allow the V4L2 isp device
 * external to drm device to free the isp user BO.
 */
void isp_user_buffer_free(void *buf_obj)
{
	amdgpu_bo_free_isp_user(buf_obj);
}
EXPORT_SYMBOL(isp_user_buffer_free);

/**
 * isp_kernel_buffer_alloc - create kernel buffer object (BO) for isp
 *
 * @dev: isp device handle
 * @size: size for the new BO
 * @buf_obj: GPU BO handle to initialize
 * @gpu_addr: GPU addr of the pinned BO
 * @cpu_addr: CPU address mapping of BO
 *
 * Allocates and pins a kernel BO for internal isp firmware use.
 *
 * This function is exported to allow the V4L2 isp device
 * external to drm device to create and access the kernel BO.
 *
 * Returns:
 * 0 on success, negative error code otherwise.
 */
int isp_kernel_buffer_alloc(struct device *dev, u64 size,
			    void **buf_obj, u64 *gpu_addr, void **cpu_addr)
{
	struct platform_device *ispdev = to_platform_device(dev);
	struct amdgpu_bo **bo = (struct amdgpu_bo **)buf_obj;
	const struct isp_platform_data *isp_pdata;
	struct amdgpu_device *adev;
	struct mfd_cell *mfd_cell;
	int ret;

	if (WARN_ON(!ispdev))
		return -ENODEV;

	if (WARN_ON(!buf_obj))
		return -EINVAL;

	if (WARN_ON(!gpu_addr))
		return -EINVAL;

	if (WARN_ON(!cpu_addr))
		return -EINVAL;

	mfd_cell = &ispdev->mfd_cell[0];
	if (!mfd_cell)
		return -ENODEV;

	isp_pdata = mfd_cell->platform_data;
	adev = isp_pdata->adev;

	ret = is_valid_isp_device(ispdev->dev.parent, adev->dev);
	if (ret)
		return ret;

	/* Ensure *bo is NULL so a new BO will be created */
	*bo = NULL;
	ret = amdgpu_bo_create_kernel(adev,
				      size,
				      ISP_MC_ADDR_ALIGN,
				      AMDGPU_GEM_DOMAIN_GTT,
				      bo,
				      gpu_addr,
				      cpu_addr);
	if (!cpu_addr || ret) {
		drm_err(&adev->ddev, "failed to alloc gart kernel buffer (%d)", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(isp_kernel_buffer_alloc);

/**
 * isp_kernel_buffer_free - free isp kernel buffer object (BO)
 *
 * @buf_obj: amdgpu isp user BO to free
 * @gpu_addr: GPU addr of isp kernel BO
 * @cpu_addr: CPU addr of isp kernel BO
 *
 * unmaps and unpin a isp kernel BO.
 *
 * This function is exported to allow the V4L2 isp device
 * external to drm device to free the kernel BO.
 */
void isp_kernel_buffer_free(void **buf_obj, u64 *gpu_addr, void **cpu_addr)
{
	struct amdgpu_bo **bo = (struct amdgpu_bo **)buf_obj;

	amdgpu_bo_free_kernel(bo, gpu_addr, cpu_addr);
}
EXPORT_SYMBOL(isp_kernel_buffer_free);

static const struct amd_ip_funcs isp_ip_funcs = {
	.name = "isp_ip",
	.early_init = isp_early_init,
	.hw_init = isp_hw_init,
	.hw_fini = isp_hw_fini,
	.is_idle = isp_is_idle,
	.set_clockgating_state = isp_set_clockgating_state,
	.set_powergating_state = isp_set_powergating_state,
};

const struct amdgpu_ip_block_version isp_v4_1_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_ISP,
	.major = 4,
	.minor = 1,
	.rev = 0,
	.funcs = &isp_ip_funcs,
};

const struct amdgpu_ip_block_version isp_v4_1_1_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_ISP,
	.major = 4,
	.minor = 1,
	.rev = 1,
	.funcs = &isp_ip_funcs,
};
