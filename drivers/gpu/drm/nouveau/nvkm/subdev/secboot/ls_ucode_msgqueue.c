/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */


#include "ls_ucode.h"
#include "acr.h"

#include <core/firmware.h>
#include <core/msgqueue.h>
#include <subdev/pmu.h>
#include <engine/sec2.h>

/**
 * acr_ls_ucode_load_msgqueue - load and prepare a ucode img for a msgqueue fw
 *
 * Load the LS microcode, desc and signature and pack them into a single
 * blob.
 */
static int
acr_ls_ucode_load_msgqueue(const struct nvkm_subdev *subdev, const char *name,
			   struct ls_ucode_img *img)
{
	const struct firmware *image, *desc, *sig;
	char f[64];
	int ret;

	snprintf(f, sizeof(f), "%s/image", name);
	ret = nvkm_firmware_get(subdev->device, f, &image);
	if (ret)
		return ret;
	img->ucode_data = kmemdup(image->data, image->size, GFP_KERNEL);
	nvkm_firmware_put(image);
	if (!img->ucode_data)
		return -ENOMEM;

	snprintf(f, sizeof(f), "%s/desc", name);
	ret = nvkm_firmware_get(subdev->device, f, &desc);
	if (ret)
		return ret;
	memcpy(&img->ucode_desc, desc->data, sizeof(img->ucode_desc));
	img->ucode_size = ALIGN(img->ucode_desc.app_start_offset + img->ucode_desc.app_size, 256);
	nvkm_firmware_put(desc);

	snprintf(f, sizeof(f), "%s/sig", name);
	ret = nvkm_firmware_get(subdev->device, f, &sig);
	if (ret)
		return ret;
	img->sig_size = sig->size;
	img->sig = kmemdup(sig->data, sig->size, GFP_KERNEL);
	nvkm_firmware_put(sig);
	if (!img->sig)
		return -ENOMEM;

	return 0;
}

static void
acr_ls_msgqueue_post_run(struct nvkm_msgqueue *queue,
			 struct nvkm_falcon *falcon, u32 addr_args)
{
	u32 cmdline_size = NVKM_MSGQUEUE_CMDLINE_SIZE;
	u8 buf[cmdline_size];

	memset(buf, 0, cmdline_size);
	nvkm_msgqueue_write_cmdline(queue, buf);
	nvkm_falcon_load_dmem(falcon, buf, addr_args, cmdline_size, 0);
	/* rearm the queue so it will wait for the init message */
	nvkm_msgqueue_reinit(queue);
}

int
acr_ls_ucode_load_pmu(const struct nvkm_subdev *subdev,
		      struct ls_ucode_img *img)
{
	struct nvkm_pmu *pmu = subdev->device->pmu;
	int ret;

	ret = acr_ls_ucode_load_msgqueue(subdev, "pmu", img);
	if (ret)
		return ret;

	/* Allocate the PMU queue corresponding to the FW version */
	ret = nvkm_msgqueue_new(img->ucode_desc.app_version, pmu->falcon,
				&pmu->queue);
	if (ret)
		return ret;

	return 0;
}

void
acr_ls_pmu_post_run(const struct nvkm_acr *acr, const struct nvkm_secboot *sb)
{
	struct nvkm_device *device = sb->subdev.device;
	struct nvkm_pmu *pmu = device->pmu;
	u32 addr_args = pmu->falcon->data.limit - NVKM_MSGQUEUE_CMDLINE_SIZE;

	acr_ls_msgqueue_post_run(pmu->queue, pmu->falcon, addr_args);
}

int
acr_ls_ucode_load_sec2(const struct nvkm_subdev *subdev,
		       struct ls_ucode_img *img)
{
	struct nvkm_sec2 *sec = subdev->device->sec2;
	int ret;

	ret = acr_ls_ucode_load_msgqueue(subdev, "sec2", img);
	if (ret)
		return ret;

	/* Allocate the PMU queue corresponding to the FW version */
	ret = nvkm_msgqueue_new(img->ucode_desc.app_version, sec->falcon,
				&sec->queue);
	if (ret)
		return ret;

	return 0;
}

void
acr_ls_sec2_post_run(const struct nvkm_acr *acr, const struct nvkm_secboot *sb)
{
	struct nvkm_device *device = sb->subdev.device;
	struct nvkm_sec2 *sec = device->sec2;
	/* on SEC arguments are always at the beginning of EMEM */
	u32 addr_args = 0x01000000;

	acr_ls_msgqueue_post_run(sec->queue, sec->falcon, addr_args);
}
