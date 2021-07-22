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
#include <engine/sec2.h>

#include <nvfw/acr.h>
#include <nvfw/flcn.h>

void
gp102_acr_wpr_patch(struct nvkm_acr *acr, s64 adjust)
{
	struct wpr_header_v1 hdr;
	struct lsb_header_v1 lsb;
	struct nvkm_acr_lsfw *lsfw;
	u32 offset = 0;

	do {
		nvkm_robj(acr->wpr, offset, &hdr, sizeof(hdr));
		wpr_header_v1_dump(&acr->subdev, &hdr);

		list_for_each_entry(lsfw, &acr->lsfw, head) {
			if (lsfw->id != hdr.falcon_id)
				continue;

			nvkm_robj(acr->wpr, hdr.lsb_offset, &lsb, sizeof(lsb));
			lsb_header_v1_dump(&acr->subdev, &lsb);

			lsfw->func->bld_patch(acr, lsb.tail.bl_data_off, adjust);
			break;
		}

		offset += sizeof(hdr);
	} while (hdr.falcon_id != WPR_HEADER_V1_FALCON_ID_INVALID);
}

int
gp102_acr_wpr_build_lsb(struct nvkm_acr *acr, struct nvkm_acr_lsfw *lsfw)
{
	struct lsb_header_v1 hdr;

	if (WARN_ON(lsfw->sig->size != sizeof(hdr.signature)))
		return -EINVAL;

	memcpy(&hdr.signature, lsfw->sig->data, lsfw->sig->size);
	gm200_acr_wpr_build_lsb_tail(lsfw, &hdr.tail);

	nvkm_wobj(acr->wpr, lsfw->offset.lsb, &hdr, sizeof(hdr));
	return 0;
}

int
gp102_acr_wpr_build(struct nvkm_acr *acr, struct nvkm_acr_lsf *rtos)
{
	struct nvkm_acr_lsfw *lsfw;
	u32 offset = 0;
	int ret;

	/* Fill per-LSF structures. */
	list_for_each_entry(lsfw, &acr->lsfw, head) {
		struct lsf_signature_v1 *sig = (void *)lsfw->sig->data;
		struct wpr_header_v1 hdr = {
			.falcon_id = lsfw->id,
			.lsb_offset = lsfw->offset.lsb,
			.bootstrap_owner = NVKM_ACR_LSF_SEC2,
			.lazy_bootstrap = rtos && lsfw->id != rtos->id,
			.bin_version = sig->version,
			.status = WPR_HEADER_V1_STATUS_COPY,
		};

		/* Write WPR header. */
		nvkm_wobj(acr->wpr, offset, &hdr, sizeof(hdr));
		offset += sizeof(hdr);

		/* Write LSB header. */
		ret = gp102_acr_wpr_build_lsb(acr, lsfw);
		if (ret)
			return ret;

		/* Write ucode image. */
		nvkm_wobj(acr->wpr, lsfw->offset.img,
				    lsfw->img.data,
				    lsfw->img.size);

		/* Write bootloader data. */
		lsfw->func->bld_write(acr, lsfw->offset.bld, lsfw);
	}

	/* Finalise WPR. */
	nvkm_wo32(acr->wpr, offset, WPR_HEADER_V1_FALCON_ID_INVALID);
	return 0;
}

int
gp102_acr_wpr_alloc(struct nvkm_acr *acr, u32 wpr_size)
{
	int ret = nvkm_memory_new(acr->subdev.device, NVKM_MEM_TARGET_INST,
				  ALIGN(wpr_size, 0x40000) << 1, 0x40000, true,
				  &acr->wpr);
	if (ret)
		return ret;

	acr->shadow_start = nvkm_memory_addr(acr->wpr);
	acr->wpr_start = acr->shadow_start + (nvkm_memory_size(acr->wpr) >> 1);
	acr->wpr_end = acr->wpr_start + (nvkm_memory_size(acr->wpr) >> 1);
	return 0;
}

u32
gp102_acr_wpr_layout(struct nvkm_acr *acr)
{
	struct nvkm_acr_lsfw *lsfw;
	u32 wpr = 0;

	wpr += 11 /* MAX_LSF */ * sizeof(struct wpr_header_v1);
	wpr  = ALIGN(wpr, 256);

	wpr += 0x100; /* Shared sub-WPR headers. */

	list_for_each_entry(lsfw, &acr->lsfw, head) {
		wpr  = ALIGN(wpr, 256);
		lsfw->offset.lsb = wpr;
		wpr += sizeof(struct lsb_header_v1);

		wpr  = ALIGN(wpr, 4096);
		lsfw->offset.img = wpr;
		wpr += lsfw->img.size;

		wpr  = ALIGN(wpr, 256);
		lsfw->offset.bld = wpr;
		lsfw->bl_data_size = ALIGN(lsfw->func->bld_size, 256);
		wpr += lsfw->bl_data_size;
	}

	return wpr;
}

int
gp102_acr_wpr_parse(struct nvkm_acr *acr)
{
	const struct wpr_header_v1 *hdr = (void *)acr->wpr_fw->data;

	while (hdr->falcon_id != WPR_HEADER_V1_FALCON_ID_INVALID) {
		wpr_header_v1_dump(&acr->subdev, hdr);
		if (!nvkm_acr_lsfw_add(NULL, acr, NULL, (hdr++)->falcon_id))
			return -ENOMEM;
	}

	return 0;
}

MODULE_FIRMWARE("nvidia/gp102/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp102/acr/ucode_unload.bin");

MODULE_FIRMWARE("nvidia/gp104/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp104/acr/ucode_unload.bin");

MODULE_FIRMWARE("nvidia/gp106/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp106/acr/ucode_unload.bin");

MODULE_FIRMWARE("nvidia/gp107/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp107/acr/ucode_unload.bin");

static const struct nvkm_acr_hsf_fwif
gp102_acr_unload_fwif[] = {
	{ 0, nvkm_acr_hsfw_load, &gm200_acr_unload_0 },
	{}
};

int
gp102_acr_load_load(struct nvkm_acr *acr, struct nvkm_acr_hsfw *hsfw)
{
	struct flcn_acr_desc_v1 *desc = (void *)&hsfw->image[hsfw->data_addr];

	desc->wpr_region_id = 1;
	desc->regions.no_regions = 2;
	desc->regions.region_props[0].start_addr = acr->wpr_start >> 8;
	desc->regions.region_props[0].end_addr = acr->wpr_end >> 8;
	desc->regions.region_props[0].region_id = 1;
	desc->regions.region_props[0].read_mask = 0xf;
	desc->regions.region_props[0].write_mask = 0xc;
	desc->regions.region_props[0].client_mask = 0x2;
	desc->regions.region_props[0].shadow_mem_start_addr =
		acr->shadow_start >> 8;
	flcn_acr_desc_v1_dump(&acr->subdev, desc);

	return gm200_acr_hsfw_load(acr, hsfw,
				  &acr->subdev.device->sec2->falcon);
}

static const struct nvkm_acr_hsf_func
gp102_acr_load_0 = {
	.load = gp102_acr_load_load,
	.boot = gm200_acr_load_boot,
	.bld = gm200_acr_hsfw_bld,
};

MODULE_FIRMWARE("nvidia/gp102/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp102/acr/ucode_load.bin");

MODULE_FIRMWARE("nvidia/gp104/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp104/acr/ucode_load.bin");

MODULE_FIRMWARE("nvidia/gp106/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp106/acr/ucode_load.bin");

MODULE_FIRMWARE("nvidia/gp107/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp107/acr/ucode_load.bin");

static const struct nvkm_acr_hsf_fwif
gp102_acr_load_fwif[] = {
	{ 0, nvkm_acr_hsfw_load, &gp102_acr_load_0 },
	{}
};

static const struct nvkm_acr_func
gp102_acr = {
	.load = gp102_acr_load_fwif,
	.unload = gp102_acr_unload_fwif,
	.wpr_parse = gp102_acr_wpr_parse,
	.wpr_layout = gp102_acr_wpr_layout,
	.wpr_alloc = gp102_acr_wpr_alloc,
	.wpr_build = gp102_acr_wpr_build,
	.wpr_patch = gp102_acr_wpr_patch,
	.wpr_check = gm200_acr_wpr_check,
	.init = gm200_acr_init,
};

int
gp102_acr_load(struct nvkm_acr *acr, int ver, const struct nvkm_acr_fwif *fwif)
{
	struct nvkm_subdev *subdev = &acr->subdev;
	const struct nvkm_acr_hsf_fwif *hsfwif;

	hsfwif = nvkm_firmware_load(subdev, fwif->func->load, "AcrLoad",
				    acr, "acr/bl", "acr/ucode_load", "load");
	if (IS_ERR(hsfwif))
		return PTR_ERR(hsfwif);

	hsfwif = nvkm_firmware_load(subdev, fwif->func->unload, "AcrUnload",
				    acr, "acr/unload_bl", "acr/ucode_unload",
				    "unload");
	if (IS_ERR(hsfwif))
		return PTR_ERR(hsfwif);

	return 0;
}

static const struct nvkm_acr_fwif
gp102_acr_fwif[] = {
	{  0, gp102_acr_load, &gp102_acr },
	{ -1, gm200_acr_nofw, &gm200_acr },
	{}
};

int
gp102_acr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_acr **pacr)
{
	return nvkm_acr_new_(gp102_acr_fwif, device, type, inst, pacr);
}
