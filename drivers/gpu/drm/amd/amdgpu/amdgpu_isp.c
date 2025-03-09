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
