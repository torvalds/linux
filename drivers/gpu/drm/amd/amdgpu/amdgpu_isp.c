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

#include "ivsrcid/isp/irqsrcs_isp_4_1.h"

#define mmDAGB0_WRCLI5_V4_1	0x6811C
#define mmDAGB0_WRCLI9_V4_1	0x6812C
#define mmDAGB0_WRCLI10_V4_1	0x68130
#define mmDAGB0_WRCLI14_V4_1	0x68140
#define mmDAGB0_WRCLI19_V4_1	0x68154
#define mmDAGB0_WRCLI20_V4_1	0x68158

static const unsigned int isp_int_srcid[MAX_ISP_INT_SRC] = {
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT9,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT10,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT11,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT12,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT13,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT14,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT15,
	ISP_4_1__SRCID__ISP_RINGBUFFER_WPT16
};

static int isp_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->isp.parent = adev->dev;

	adev->isp.cgs_device = amdgpu_cgs_create_device(adev);
	if (!adev->isp.cgs_device)
		return -EINVAL;

	return 0;
}

static int isp_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->isp.cgs_device)
		amdgpu_cgs_destroy_device(adev->isp.cgs_device);

	return 0;
}

/**
 * isp_hw_init - start and test isp block
 *
 * @handle: handle for amdgpu_device pointer
 *
 */
static int isp_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	const struct amdgpu_ip_block *ip_block =
		amdgpu_device_ip_get_ip_block(adev, AMD_IP_BLOCK_TYPE_ISP);
	u64 isp_base;
	int int_idx;
	int r;

	if (!ip_block)
		return -EINVAL;

	if (adev->rmmio_size == 0 || adev->rmmio_size < 0x5289)
		return -EINVAL;

	isp_base = adev->rmmio_base;

	adev->isp.isp_cell = kcalloc(1, sizeof(struct mfd_cell), GFP_KERNEL);
	if (!adev->isp.isp_cell) {
		r = -ENOMEM;
		DRM_ERROR("%s: isp mfd cell alloc failed\n", __func__);
		goto failure;
	}

	adev->isp.isp_res = kcalloc(9, sizeof(struct resource), GFP_KERNEL);
	if (!adev->isp.isp_res) {
		r = -ENOMEM;
		DRM_ERROR("%s: isp mfd res alloc failed\n", __func__);
		goto failure;
	}

	adev->isp.isp_pdata = kzalloc(sizeof(*adev->isp.isp_pdata), GFP_KERNEL);
	if (!adev->isp.isp_pdata) {
		r = -ENOMEM;
		DRM_ERROR("%s: isp platform data alloc failed\n", __func__);
		goto failure;
	}

	/* initialize isp platform data */
	adev->isp.isp_pdata->adev = (void *)adev;
	adev->isp.isp_pdata->asic_type = adev->asic_type;
	adev->isp.isp_pdata->base_rmmio_size = adev->rmmio_size;

	adev->isp.isp_res[0].name = "isp_reg";
	adev->isp.isp_res[0].flags = IORESOURCE_MEM;
	adev->isp.isp_res[0].start = isp_base;
	adev->isp.isp_res[0].end = isp_base + ISP_REGS_OFFSET_END;

	for (int_idx = 0; int_idx < MAX_ISP_INT_SRC; int_idx++) {
		adev->isp.isp_res[int_idx + 1].name = "isp_irq";
		adev->isp.isp_res[int_idx + 1].flags = IORESOURCE_IRQ;
		adev->isp.isp_res[int_idx + 1].start =
			amdgpu_irq_create_mapping(adev, isp_int_srcid[int_idx]);
		adev->isp.isp_res[int_idx + 1].end =
			adev->isp.isp_res[int_idx + 1].start;
	}

	adev->isp.isp_cell[0].name = "amd_isp_capture";
	adev->isp.isp_cell[0].num_resources = 9;
	adev->isp.isp_cell[0].resources = &adev->isp.isp_res[0];
	adev->isp.isp_cell[0].platform_data = adev->isp.isp_pdata;
	adev->isp.isp_cell[0].pdata_size = sizeof(struct isp_platform_data);

	r = mfd_add_hotplug_devices(adev->isp.parent, adev->isp.isp_cell, 1);
	if (r) {
		DRM_ERROR("%s: add mfd hotplug device failed\n", __func__);
		goto failure;
	}

	/*
	 * Temporary WA added to disable MMHUB TLSi until the GART initialization
	 * is ready to support MMHUB TLSi and SAW for ISP HW to access GART memory
	 * using the TLSi path
	 */
	cgs_write_register(adev->isp.cgs_device, mmDAGB0_WRCLI5_V4_1 >> 2, 0xFE5FEAA8);
	cgs_write_register(adev->isp.cgs_device, mmDAGB0_WRCLI9_V4_1 >> 2, 0xFE5FEAA8);
	cgs_write_register(adev->isp.cgs_device, mmDAGB0_WRCLI10_V4_1 >> 2, 0xFE5FEAA8);
	cgs_write_register(adev->isp.cgs_device, mmDAGB0_WRCLI14_V4_1 >> 2, 0xFE5FEAA8);
	cgs_write_register(adev->isp.cgs_device, mmDAGB0_WRCLI19_V4_1 >> 2, 0xFE5FEAA8);
	cgs_write_register(adev->isp.cgs_device, mmDAGB0_WRCLI20_V4_1 >> 2, 0xFE5FEAA8);

	return 0;

failure:

	kfree(adev->isp.isp_pdata);
	kfree(adev->isp.isp_res);
	kfree(adev->isp.isp_cell);

	return r;
}

/**
 * isp_hw_fini - stop the hardware block
 *
 * @handle: handle for amdgpu_device pointer
 *
 */
static int isp_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* remove isp mfd device */
	mfd_remove_devices(adev->isp.parent);

	kfree(adev->isp.isp_res);
	kfree(adev->isp.isp_cell);
	kfree(adev->isp.isp_pdata);

	return 0;
}

static int isp_suspend(void *handle)
{
	return 0;
}

static int isp_resume(void *handle)
{
	return 0;
}

static int isp_load_fw_by_psp(struct amdgpu_device *adev)
{
	const struct common_firmware_header *hdr;
	char ucode_prefix[30];
	char fw_name[40];
	int r = 0;

	/* get isp fw binary name and path */
	amdgpu_ucode_ip_version_decode(adev, ISP_HWIP, ucode_prefix,
				       sizeof(ucode_prefix));
	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s.bin", ucode_prefix);

	/* read isp fw */
	r = amdgpu_ucode_request(adev, &adev->isp.fw, fw_name);
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

static int isp_early_init(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	ret = isp_load_fw_by_psp(adev);
	if (ret) {
		DRM_WARN("%s: isp fw load failed %d\n", __func__, ret);
		/* allow amdgpu init to proceed though isp fw load fails */
		ret = 0;
	}

	return ret;
}

static bool isp_is_idle(void *handle)
{
	return true;
}

static int isp_wait_for_idle(void *handle)
{
	return 0;
}

static int isp_soft_reset(void *handle)
{
	return 0;
}

static int isp_set_clockgating_state(void *handle,
				     enum amd_clockgating_state state)
{
	return 0;
}

static int isp_set_powergating_state(void *handle,
				     enum amd_powergating_state state)
{
	return 0;
}

static const struct amd_ip_funcs isp_ip_funcs = {
	.name = "isp_ip",
	.early_init = isp_early_init,
	.late_init = NULL,
	.sw_init = isp_sw_init,
	.sw_fini = isp_sw_fini,
	.hw_init = isp_hw_init,
	.hw_fini = isp_hw_fini,
	.suspend = isp_suspend,
	.resume = isp_resume,
	.is_idle = isp_is_idle,
	.wait_for_idle = isp_wait_for_idle,
	.soft_reset = isp_soft_reset,
	.set_clockgating_state = isp_set_clockgating_state,
	.set_powergating_state = isp_set_powergating_state,
};

const struct amdgpu_ip_block_version isp_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_ISP,
	.major = 4,
	.minor = 1,
	.rev = 0,
	.funcs = &isp_ip_funcs,
};
