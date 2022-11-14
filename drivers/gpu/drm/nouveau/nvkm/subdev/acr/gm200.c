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

#include <core/falcon.h>
#include <core/firmware.h>
#include <core/memory.h>
#include <subdev/mc.h>
#include <subdev/mmu.h>
#include <subdev/pmu.h>
#include <subdev/timer.h>

#include <nvfw/acr.h>
#include <nvfw/flcn.h>

const struct nvkm_acr_func
gm200_acr = {
};

int
gm200_acr_nofw(struct nvkm_acr *acr, int ver, const struct nvkm_acr_fwif *fwif)
{
	nvkm_warn(&acr->subdev, "firmware unavailable\n");
	return 0;
}

int
gm200_acr_init(struct nvkm_acr *acr)
{
	return nvkm_acr_hsfw_boot(acr, "load");
}

void
gm200_acr_wpr_check(struct nvkm_acr *acr, u64 *start, u64 *limit)
{
	struct nvkm_device *device = acr->subdev.device;

	nvkm_wr32(device, 0x100cd4, 2);
	*start = (u64)(nvkm_rd32(device, 0x100cd4) & 0xffffff00) << 8;
	nvkm_wr32(device, 0x100cd4, 3);
	*limit = (u64)(nvkm_rd32(device, 0x100cd4) & 0xffffff00) << 8;
	*limit = *limit + 0x20000;
}

int
gm200_acr_wpr_patch(struct nvkm_acr *acr, s64 adjust)
{
	struct nvkm_subdev *subdev = &acr->subdev;
	struct wpr_header hdr;
	struct lsb_header lsb;
	struct nvkm_acr_lsf *lsfw;
	u32 offset = 0;

	do {
		nvkm_robj(acr->wpr, offset, &hdr, sizeof(hdr));
		wpr_header_dump(subdev, &hdr);

		list_for_each_entry(lsfw, &acr->lsfw, head) {
			if (lsfw->id != hdr.falcon_id)
				continue;

			nvkm_robj(acr->wpr, hdr.lsb_offset, &lsb, sizeof(lsb));
			lsb_header_dump(subdev, &lsb);

			lsfw->func->bld_patch(acr, lsb.tail.bl_data_off, adjust);
			break;
		}
		offset += sizeof(hdr);
	} while (hdr.falcon_id != WPR_HEADER_V0_FALCON_ID_INVALID);

	return 0;
}

void
gm200_acr_wpr_build_lsb_tail(struct nvkm_acr_lsfw *lsfw,
			     struct lsb_header_tail *hdr)
{
	hdr->ucode_off = lsfw->offset.img;
	hdr->ucode_size = lsfw->ucode_size;
	hdr->data_size = lsfw->data_size;
	hdr->bl_code_size = lsfw->bootloader_size;
	hdr->bl_imem_off = lsfw->bootloader_imem_offset;
	hdr->bl_data_off = lsfw->offset.bld;
	hdr->bl_data_size = lsfw->bl_data_size;
	hdr->app_code_off = lsfw->app_start_offset +
			   lsfw->app_resident_code_offset;
	hdr->app_code_size = lsfw->app_resident_code_size;
	hdr->app_data_off = lsfw->app_start_offset +
			   lsfw->app_resident_data_offset;
	hdr->app_data_size = lsfw->app_resident_data_size;
	hdr->flags = lsfw->func->flags;
}

static int
gm200_acr_wpr_build_lsb(struct nvkm_acr *acr, struct nvkm_acr_lsfw *lsfw)
{
	struct lsb_header hdr;

	if (WARN_ON(lsfw->sig->size != sizeof(hdr.signature)))
		return -EINVAL;

	memcpy(&hdr.signature, lsfw->sig->data, lsfw->sig->size);
	gm200_acr_wpr_build_lsb_tail(lsfw, &hdr.tail);

	nvkm_wobj(acr->wpr, lsfw->offset.lsb, &hdr, sizeof(hdr));
	return 0;
}

int
gm200_acr_wpr_build(struct nvkm_acr *acr, struct nvkm_acr_lsf *rtos)
{
	struct nvkm_acr_lsfw *lsfw;
	u32 offset = 0;
	int ret;

	/* Fill per-LSF structures. */
	list_for_each_entry(lsfw, &acr->lsfw, head) {
		struct wpr_header hdr = {
			.falcon_id = lsfw->id,
			.lsb_offset = lsfw->offset.lsb,
			.bootstrap_owner = NVKM_ACR_LSF_PMU,
			.lazy_bootstrap = rtos && lsfw->id != rtos->id,
			.status = WPR_HEADER_V0_STATUS_COPY,
		};

		/* Write WPR header. */
		nvkm_wobj(acr->wpr, offset, &hdr, sizeof(hdr));
		offset += sizeof(hdr);

		/* Write LSB header. */
		ret = gm200_acr_wpr_build_lsb(acr, lsfw);
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
	nvkm_wo32(acr->wpr, offset, WPR_HEADER_V0_FALCON_ID_INVALID);
	return 0;
}

static int
gm200_acr_wpr_alloc(struct nvkm_acr *acr, u32 wpr_size)
{
	int ret = nvkm_memory_new(acr->subdev.device, NVKM_MEM_TARGET_INST,
				  ALIGN(wpr_size, 0x40000), 0x40000, true,
				  &acr->wpr);
	if (ret)
		return ret;

	acr->wpr_start = nvkm_memory_addr(acr->wpr);
	acr->wpr_end = acr->wpr_start + nvkm_memory_size(acr->wpr);
	return 0;
}

u32
gm200_acr_wpr_layout(struct nvkm_acr *acr)
{
	struct nvkm_acr_lsfw *lsfw;
	u32 wpr = 0;

	wpr += 11 /* MAX_LSF */ * sizeof(struct wpr_header);

	list_for_each_entry(lsfw, &acr->lsfw, head) {
		wpr  = ALIGN(wpr, 256);
		lsfw->offset.lsb = wpr;
		wpr += sizeof(struct lsb_header);

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
gm200_acr_wpr_parse(struct nvkm_acr *acr)
{
	const struct wpr_header *hdr = (void *)acr->wpr_fw->data;
	struct nvkm_acr_lsfw *lsfw;

	while (hdr->falcon_id != WPR_HEADER_V0_FALCON_ID_INVALID) {
		wpr_header_dump(&acr->subdev, hdr);
		lsfw = nvkm_acr_lsfw_add(NULL, acr, NULL, (hdr++)->falcon_id);
		if (IS_ERR(lsfw))
			return PTR_ERR(lsfw);
	}

	return 0;
}

int
gm200_acr_hsfw_load_bld(struct nvkm_falcon_fw *fw)
{
	struct flcn_bl_dmem_desc_v1 hsdesc = {
		.ctx_dma = FALCON_DMAIDX_VIRT,
		.code_dma_base = fw->vma->addr,
		.non_sec_code_off = fw->nmem_base,
		.non_sec_code_size = fw->nmem_size,
		.sec_code_off = fw->imem_base,
		.sec_code_size = fw->imem_size,
		.code_entry_point = 0,
		.data_dma_base = fw->vma->addr + fw->dmem_base_img,
		.data_size = fw->dmem_size,
	};

	flcn_bl_dmem_desc_v1_dump(fw->falcon->user, &hsdesc);

	return nvkm_falcon_pio_wr(fw->falcon, (u8 *)&hsdesc, 0, 0, DMEM, 0, sizeof(hsdesc), 0, 0);
}

int
gm200_acr_hsfw_ctor(struct nvkm_acr *acr, const char *bl, const char *fw, const char *name, int ver,
		    const struct nvkm_acr_hsf_fwif *fwif)
{
	struct nvkm_acr_hsfw *hsfw;

	if (!(hsfw = kzalloc(sizeof(*hsfw), GFP_KERNEL)))
		return -ENOMEM;

	hsfw->falcon_id = fwif->falcon_id;
	hsfw->boot_mbox0 = fwif->boot_mbox0;
	hsfw->intr_clear = fwif->intr_clear;
	list_add_tail(&hsfw->head, &acr->hsfw);

	return nvkm_falcon_fw_ctor_hs(fwif->func, name, &acr->subdev, bl, fw, ver, NULL, &hsfw->fw);
}

const struct nvkm_falcon_fw_func
gm200_acr_unload_0 = {
	.signature = gm200_flcn_fw_signature,
	.reset = gm200_flcn_fw_reset,
	.load = gm200_flcn_fw_load,
	.load_bld = gm200_acr_hsfw_load_bld,
	.boot = gm200_flcn_fw_boot,
};

MODULE_FIRMWARE("nvidia/gm200/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gm204/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gm206/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/gp100/acr/ucode_unload.bin");

static const struct nvkm_acr_hsf_fwif
gm200_acr_unload_fwif[] = {
	{ 0, gm200_acr_hsfw_ctor, &gm200_acr_unload_0, NVKM_ACR_HSF_PMU, 0, 0x00000010 },
	{}
};

static int
gm200_acr_load_setup(struct nvkm_falcon_fw *fw)
{
	struct flcn_acr_desc *desc = (void *)&fw->fw.img[fw->dmem_base_img];
	struct nvkm_acr *acr = fw->falcon->owner->device->acr;

	desc->wpr_region_id = 1;
	desc->regions.no_regions = 2;
	desc->regions.region_props[0].start_addr = acr->wpr_start >> 8;
	desc->regions.region_props[0].end_addr = acr->wpr_end >> 8;
	desc->regions.region_props[0].region_id = 1;
	desc->regions.region_props[0].read_mask = 0xf;
	desc->regions.region_props[0].write_mask = 0xc;
	desc->regions.region_props[0].client_mask = 0x2;
	flcn_acr_desc_dump(&acr->subdev, desc);
	return 0;
}

static const struct nvkm_falcon_fw_func
gm200_acr_load_0 = {
	.signature = gm200_flcn_fw_signature,
	.reset = gm200_flcn_fw_reset,
	.setup = gm200_acr_load_setup,
	.load = gm200_flcn_fw_load,
	.load_bld = gm200_acr_hsfw_load_bld,
	.boot = gm200_flcn_fw_boot,
};

MODULE_FIRMWARE("nvidia/gm200/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gm200/acr/ucode_load.bin");

MODULE_FIRMWARE("nvidia/gm204/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gm204/acr/ucode_load.bin");

MODULE_FIRMWARE("nvidia/gm206/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gm206/acr/ucode_load.bin");

MODULE_FIRMWARE("nvidia/gp100/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp100/acr/ucode_load.bin");

static const struct nvkm_acr_hsf_fwif
gm200_acr_load_fwif[] = {
	{ 0, gm200_acr_hsfw_ctor, &gm200_acr_load_0, NVKM_ACR_HSF_PMU, 0, 0x00000010 },
	{}
};

static const struct nvkm_acr_func
gm200_acr_0 = {
	.load = gm200_acr_load_fwif,
	.unload = gm200_acr_unload_fwif,
	.wpr_parse = gm200_acr_wpr_parse,
	.wpr_layout = gm200_acr_wpr_layout,
	.wpr_alloc = gm200_acr_wpr_alloc,
	.wpr_build = gm200_acr_wpr_build,
	.wpr_patch = gm200_acr_wpr_patch,
	.wpr_check = gm200_acr_wpr_check,
	.init = gm200_acr_init,
	.bootstrap_falcons = BIT_ULL(NVKM_ACR_LSF_FECS) |
			     BIT_ULL(NVKM_ACR_LSF_GPCCS),
};

static int
gm200_acr_load(struct nvkm_acr *acr, int ver, const struct nvkm_acr_fwif *fwif)
{
	struct nvkm_subdev *subdev = &acr->subdev;
	const struct nvkm_acr_hsf_fwif *hsfwif;

	hsfwif = nvkm_firmware_load(subdev, fwif->func->load, "AcrLoad",
				    acr, "acr/bl", "acr/ucode_load", "load");
	if (IS_ERR(hsfwif))
		return PTR_ERR(hsfwif);

	hsfwif = nvkm_firmware_load(subdev, fwif->func->unload, "AcrUnload",
				    acr, "acr/bl", "acr/ucode_unload",
				    "unload");
	if (IS_ERR(hsfwif))
		return PTR_ERR(hsfwif);

	return 0;
}

static const struct nvkm_acr_fwif
gm200_acr_fwif[] = {
	{  0, gm200_acr_load, &gm200_acr_0 },
	{ -1, gm200_acr_nofw, &gm200_acr },
	{}
};

int
gm200_acr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_acr **pacr)
{
	return nvkm_acr_new_(gm200_acr_fwif, device, type, inst, pacr);
}
