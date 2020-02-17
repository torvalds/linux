/*
 * Copyright 2019 Red Hat Inc.
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
 */
#include "priv.h"

#include <core/firmware.h>
#include <core/memory.h>
#include <subdev/mmu.h>
#include <subdev/pmu.h>

#include <nvfw/acr.h>
#include <nvfw/flcn.h>

int
gm20b_acr_wpr_alloc(struct nvkm_acr *acr, u32 wpr_size)
{
	struct nvkm_subdev *subdev = &acr->subdev;

	acr->func->wpr_check(acr, &acr->wpr_start, &acr->wpr_end);

	if ((acr->wpr_end - acr->wpr_start) < wpr_size) {
		nvkm_error(subdev, "WPR image too big for WPR!\n");
		return -ENOSPC;
	}

	return nvkm_memory_new(subdev->device, NVKM_MEM_TARGET_INST,
			       wpr_size, 0, true, &acr->wpr);
}

static void
gm20b_acr_load_bld(struct nvkm_acr *acr, struct nvkm_acr_hsf *hsf)
{
	struct flcn_bl_dmem_desc hsdesc = {
		.ctx_dma = FALCON_DMAIDX_VIRT,
		.code_dma_base = hsf->vma->addr >> 8,
		.non_sec_code_off = hsf->non_sec_addr,
		.non_sec_code_size = hsf->non_sec_size,
		.sec_code_off = hsf->sec_addr,
		.sec_code_size = hsf->sec_size,
		.code_entry_point = 0,
		.data_dma_base = (hsf->vma->addr + hsf->data_addr) >> 8,
		.data_size = hsf->data_size,
	};

	flcn_bl_dmem_desc_dump(&acr->subdev, &hsdesc);

	nvkm_falcon_load_dmem(hsf->falcon, &hsdesc, 0, sizeof(hsdesc), 0);
}

static int
gm20b_acr_load_load(struct nvkm_acr *acr, struct nvkm_acr_hsfw *hsfw)
{
	struct flcn_acr_desc *desc = (void *)&hsfw->image[hsfw->data_addr];

	desc->ucode_blob_base = nvkm_memory_addr(acr->wpr);
	desc->ucode_blob_size = nvkm_memory_size(acr->wpr);
	flcn_acr_desc_dump(&acr->subdev, desc);

	return gm200_acr_hsfw_load(acr, hsfw, &acr->subdev.device->pmu->falcon);
}

const struct nvkm_acr_hsf_func
gm20b_acr_load_0 = {
	.load = gm20b_acr_load_load,
	.boot = gm200_acr_load_boot,
	.bld = gm20b_acr_load_bld,
};

#if IS_ENABLED(CONFIG_ARCH_TEGRA_210_SOC)
MODULE_FIRMWARE("nvidia/gm20b/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gm20b/acr/ucode_load.bin");
#endif

static const struct nvkm_acr_hsf_fwif
gm20b_acr_load_fwif[] = {
	{ 0, nvkm_acr_hsfw_load, &gm20b_acr_load_0 },
	{}
};

static const struct nvkm_acr_func
gm20b_acr = {
	.load = gm20b_acr_load_fwif,
	.wpr_parse = gm200_acr_wpr_parse,
	.wpr_layout = gm200_acr_wpr_layout,
	.wpr_alloc = gm20b_acr_wpr_alloc,
	.wpr_build = gm200_acr_wpr_build,
	.wpr_patch = gm200_acr_wpr_patch,
	.wpr_check = gm200_acr_wpr_check,
	.init = gm200_acr_init,
};

int
gm20b_acr_load(struct nvkm_acr *acr, int ver, const struct nvkm_acr_fwif *fwif)
{
	struct nvkm_subdev *subdev = &acr->subdev;
	const struct nvkm_acr_hsf_fwif *hsfwif;

	hsfwif = nvkm_firmware_load(subdev, fwif->func->load, "AcrLoad",
				    acr, "acr/bl", "acr/ucode_load", "load");
	if (IS_ERR(hsfwif))
		return PTR_ERR(hsfwif);

	return 0;
}

static const struct nvkm_acr_fwif
gm20b_acr_fwif[] = {
	{ 0, gm20b_acr_load, &gm20b_acr },
	{}
};

int
gm20b_acr_new(struct nvkm_device *device, int index, struct nvkm_acr **pacr)
{
	return nvkm_acr_new_(gm20b_acr_fwif, device, index, pacr);
}
