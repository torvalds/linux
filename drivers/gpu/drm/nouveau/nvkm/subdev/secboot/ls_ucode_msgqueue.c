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
#include <subdev/mc.h>
#include <subdev/timer.h>

/**
 * acr_ls_ucode_load_msgqueue - load and prepare a ucode img for a msgqueue fw
 *
 * Load the LS microcode, desc and signature and pack them into a single
 * blob.
 */
static int
acr_ls_ucode_load_msgqueue(const struct nvkm_subdev *subdev, const char *name,
			   int maxver, struct ls_ucode_img *img)
{
	const struct firmware *image, *desc, *sig;
	char f[64];
	int ver, ret;

	snprintf(f, sizeof(f), "%s/image", name);
	ver = nvkm_firmware_get_version(subdev, f, 0, maxver, &image);
	if (ver < 0)
		return ver;
	img->ucode_data = kmemdup(image->data, image->size, GFP_KERNEL);
	nvkm_firmware_put(image);
	if (!img->ucode_data)
		return -ENOMEM;

	snprintf(f, sizeof(f), "%s/desc", name);
	ret = nvkm_firmware_get_version(subdev, f, ver, ver, &desc);
	if (ret < 0)
		return ret;
	memcpy(&img->ucode_desc, desc->data, sizeof(img->ucode_desc));
	img->ucode_size = ALIGN(img->ucode_desc.app_start_offset + img->ucode_desc.app_size, 256);
	nvkm_firmware_put(desc);

	snprintf(f, sizeof(f), "%s/sig", name);
	ret = nvkm_firmware_get_version(subdev, f, ver, ver, &sig);
	if (ret < 0)
		return ret;
	img->sig_size = sig->size;
	img->sig = kmemdup(sig->data, sig->size, GFP_KERNEL);
	nvkm_firmware_put(sig);
	if (!img->sig)
		return -ENOMEM;

	return ver;
}

static int
acr_ls_msgqueue_post_run(struct nvkm_msgqueue *queue,
			 struct nvkm_falcon *falcon, u32 addr_args)
{
	struct nvkm_device *device = falcon->owner->device;
	u8 buf[NVKM_MSGQUEUE_CMDLINE_SIZE];

	memset(buf, 0, sizeof(buf));
	nvkm_msgqueue_write_cmdline(queue, buf);
	nvkm_falcon_load_dmem(falcon, buf, addr_args, sizeof(buf), 0);
	/* rearm the queue so it will wait for the init message */
	nvkm_msgqueue_reinit(queue);

	/* Enable interrupts */
	nvkm_falcon_wr32(falcon, 0x10, 0xff);
	nvkm_mc_intr_mask(device, falcon->owner->index, true);

	/* Start LS firmware on boot falcon */
	nvkm_falcon_start(falcon);

	return 0;
}

int
acr_ls_ucode_load_pmu(const struct nvkm_secboot *sb, int maxver,
		      struct ls_ucode_img *img)
{
	struct nvkm_pmu *pmu = sb->subdev.device->pmu;
	int ret;

	ret = acr_ls_ucode_load_msgqueue(&sb->subdev, "pmu", maxver, img);
	if (ret)
		return ret;

	/* Allocate the PMU queue corresponding to the FW version */
	ret = nvkm_msgqueue_new(img->ucode_desc.app_version, pmu->falcon,
				sb, &pmu->queue);
	if (ret)
		return ret;

	return 0;
}

int
acr_ls_pmu_post_run(const struct nvkm_acr *acr, const struct nvkm_secboot *sb)
{
	struct nvkm_device *device = sb->subdev.device;
	struct nvkm_pmu *pmu = device->pmu;
	u32 addr_args = pmu->falcon->data.limit - NVKM_MSGQUEUE_CMDLINE_SIZE;
	int ret;

	ret = acr_ls_msgqueue_post_run(pmu->queue, pmu->falcon, addr_args);
	if (ret)
		return ret;

	nvkm_debug(&sb->subdev, "%s started\n",
		   nvkm_secboot_falcon_name[acr->boot_falcon]);

	return 0;
}

int
acr_ls_ucode_load_sec2(const struct nvkm_secboot *sb, int maxver,
		       struct ls_ucode_img *img)
{
	struct nvkm_sec2 *sec = sb->subdev.device->sec2;
	int ver, ret;

	ver = acr_ls_ucode_load_msgqueue(&sb->subdev, "sec2", maxver, img);
	if (ver < 0)
		return ver;

	/* Allocate the PMU queue corresponding to the FW version */
	ret = nvkm_msgqueue_new(img->ucode_desc.app_version, sec->falcon,
				sb, &sec->queue);
	if (ret)
		return ret;

	return ver;
}

int
acr_ls_sec2_post_run(const struct nvkm_acr *acr, const struct nvkm_secboot *sb)
{
	const struct nvkm_subdev *subdev = &sb->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_sec2 *sec = device->sec2;
	/* on SEC arguments are always at the beginning of EMEM */
	const u32 addr_args = 0x01000000;
	u32 reg;
	int ret;

	ret = acr_ls_msgqueue_post_run(sec->queue, sec->falcon, addr_args);
	if (ret)
		return ret;

	/*
	 * There is a bug where the LS firmware sometimes require to be started
	 * twice (this happens only on SEC). Detect and workaround that
	 * condition.
	 *
	 * Once started, the falcon will end up in STOPPED condition (bit 5)
	 * if successful, or in HALT condition (bit 4) if not.
	 */
	nvkm_msec(device, 1,
		  if ((reg = nvkm_falcon_rd32(sb->boot_falcon, 0x100) & 0x30) != 0)
			  break;
	);
	if (reg & BIT(4)) {
		nvkm_debug(subdev, "applying workaround for start bug...\n");
		nvkm_falcon_start(sb->boot_falcon);
		nvkm_msec(subdev->device, 1,
			if ((reg = nvkm_rd32(subdev->device,
					     sb->boot_falcon->addr + 0x100)
			     & 0x30) != 0)
				break;
		);
		if (reg & BIT(4)) {
			nvkm_error(subdev, "%s failed to start\n",
			       nvkm_secboot_falcon_name[acr->boot_falcon]);
			return -EINVAL;
		}
	}

	nvkm_debug(&sb->subdev, "%s started\n",
		   nvkm_secboot_falcon_name[acr->boot_falcon]);

	return 0;
}
