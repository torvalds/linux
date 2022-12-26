/*
 * Copyright 2021 Red Hat Inc.
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

#include <nvfw/acr.h>

static int
ga102_acr_wpr_patch(struct nvkm_acr *acr, s64 adjust)
{
	struct wpr_header_v2 hdr;
	struct lsb_header_v2 *lsb;
	struct nvkm_acr_lsfw *lsfw;
	u32 offset = 0;

	lsb = kvmalloc(sizeof(*lsb), GFP_KERNEL);
	if (!lsb)
		return -ENOMEM;

	do {
		nvkm_robj(acr->wpr, offset, &hdr, sizeof(hdr));
		wpr_header_v2_dump(&acr->subdev, &hdr);

		list_for_each_entry(lsfw, &acr->lsfw, head) {
			if (lsfw->id != hdr.wpr.falcon_id)
				continue;

			nvkm_robj(acr->wpr, hdr.wpr.lsb_offset, lsb, sizeof(*lsb));
			lsb_header_v2_dump(&acr->subdev, lsb);

			lsfw->func->bld_patch(acr, lsb->bl_data_off, adjust);
			break;
		}

		offset += sizeof(hdr);
	} while (hdr.wpr.falcon_id != WPR_HEADER_V1_FALCON_ID_INVALID);

	kvfree(lsb);
	return 0;
}

static int
ga102_acr_wpr_build_lsb(struct nvkm_acr *acr, struct nvkm_acr_lsfw *lsfw)
{
	struct lsb_header_v2 *hdr;
	int ret = 0;

	if (WARN_ON(lsfw->sig->size != sizeof(hdr->signature)))
		return -EINVAL;

	hdr = kvzalloc(sizeof(*hdr), GFP_KERNEL);
	if (!hdr)
		return -ENOMEM;

	hdr->hdr.identifier = WPR_GENERIC_HEADER_ID_LSF_LSB_HEADER;
	hdr->hdr.version = 2;
	hdr->hdr.size = sizeof(*hdr);

	memcpy(&hdr->signature, lsfw->sig->data, lsfw->sig->size);
	hdr->ucode_off = lsfw->offset.img;
	hdr->ucode_size = lsfw->ucode_size;
	hdr->data_size = lsfw->data_size;
	hdr->bl_code_size = lsfw->bootloader_size;
	hdr->bl_imem_off = lsfw->bootloader_imem_offset;
	hdr->bl_data_off = lsfw->offset.bld;
	hdr->bl_data_size = lsfw->bl_data_size;
	hdr->app_code_off = lsfw->app_start_offset + lsfw->app_resident_code_offset;
	hdr->app_code_size = ALIGN(lsfw->app_resident_code_size, 0x100);
	hdr->app_data_off = lsfw->app_start_offset + lsfw->app_resident_data_offset;
	hdr->app_data_size = ALIGN(lsfw->app_resident_data_size, 0x100);
	hdr->app_imem_offset = lsfw->app_imem_offset;
	hdr->app_dmem_offset = lsfw->app_dmem_offset;
	hdr->flags = lsfw->func->flags;
	hdr->monitor_code_offset = 0;
	hdr->monitor_data_offset = 0;
	hdr->manifest_offset = 0;

	if (lsfw->secure_bootloader) {
		struct nvkm_falcon_fw fw = {
			.fw.img = hdr->hs_fmc_params.pkc_signature,
			.fw.name = "LSFW",
			.func = &(const struct nvkm_falcon_fw_func) {
				.signature = ga100_flcn_fw_signature,
			},
			.sig_size = lsfw->sig_size,
			.sig_nr = lsfw->sig_nr,
			.sigs = lsfw->sigs,
			.fuse_ver = lsfw->fuse_ver,
			.engine_id = lsfw->engine_id,
			.ucode_id = lsfw->ucode_id,
			.falcon = lsfw->falcon,

		};

		ret = nvkm_falcon_get(fw.falcon, &acr->subdev);
		if (ret == 0) {
			hdr->hs_fmc_params.hs_fmc = 1;
			hdr->hs_fmc_params.pkc_algo = 0;
			hdr->hs_fmc_params.pkc_algo_version = 1;
			hdr->hs_fmc_params.engid_mask = lsfw->engine_id;
			hdr->hs_fmc_params.ucode_id = lsfw->ucode_id;
			hdr->hs_fmc_params.fuse_ver = lsfw->fuse_ver;
			ret = nvkm_falcon_fw_patch(&fw);
			nvkm_falcon_put(fw.falcon, &acr->subdev);
		}
	}

	nvkm_wobj(acr->wpr, lsfw->offset.lsb, hdr, sizeof(*hdr));
	kvfree(hdr);
	return ret;
}

static int
ga102_acr_wpr_build(struct nvkm_acr *acr, struct nvkm_acr_lsf *rtos)
{
	struct nvkm_acr_lsfw *lsfw;
	struct wpr_header_v2 hdr;
	u32 offset = 0;
	int ret;

	/*XXX: shared sub-WPR headers, fill terminator for now. */
	nvkm_wo32(acr->wpr, 0x300, (2 << 16) | WPR_GENERIC_HEADER_ID_LSF_SHARED_SUB_WPR);
	nvkm_wo32(acr->wpr, 0x304, 0x14);
	nvkm_wo32(acr->wpr, 0x308, 0xffffffff);
	nvkm_wo32(acr->wpr, 0x30c, 0);
	nvkm_wo32(acr->wpr, 0x310, 0);

	/* Fill per-LSF structures. */
	list_for_each_entry(lsfw, &acr->lsfw, head) {
		struct lsf_signature_v2 *sig = (void *)lsfw->sig->data;

		hdr.hdr.identifier = WPR_GENERIC_HEADER_ID_LSF_WPR_HEADER;
		hdr.hdr.version = 2;
		hdr.hdr.size = sizeof(hdr);
		hdr.wpr.falcon_id = lsfw->id;
		hdr.wpr.lsb_offset = lsfw->offset.lsb;
		hdr.wpr.bootstrap_owner = NVKM_ACR_LSF_GSPLITE;
		hdr.wpr.lazy_bootstrap = 1;
		hdr.wpr.bin_version = sig->ls_ucode_version;
		hdr.wpr.status = WPR_HEADER_V1_STATUS_COPY;

		/* Write WPR header. */
		nvkm_wobj(acr->wpr, offset, &hdr, sizeof(hdr));
		offset += sizeof(hdr);

		/* Write LSB header. */
		ret = ga102_acr_wpr_build_lsb(acr, lsfw);
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
	hdr.hdr.identifier = WPR_GENERIC_HEADER_ID_LSF_WPR_HEADER;
	hdr.hdr.version = 2;
	hdr.hdr.size = sizeof(hdr);
	hdr.wpr.falcon_id = WPR_HEADER_V1_FALCON_ID_INVALID;
	nvkm_wobj(acr->wpr, offset, &hdr, sizeof(hdr));
	return 0;
}

static u32
ga102_acr_wpr_layout(struct nvkm_acr *acr)
{
	struct nvkm_acr_lsfw *lsfw;
	u32 wpr = 0;

	wpr += 21 /* MAX_LSF */ * sizeof(struct wpr_header_v2);
	wpr  = ALIGN(wpr, 256);

	wpr += 0x100; /* Shared sub-WPR headers. */

	list_for_each_entry(lsfw, &acr->lsfw, head) {
		wpr  = ALIGN(wpr, 256);
		lsfw->offset.lsb = wpr;
		wpr += sizeof(struct lsb_header_v2);

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

static int
ga102_acr_wpr_parse(struct nvkm_acr *acr)
{
	const struct wpr_header_v2 *hdr = (void *)acr->wpr_fw->data;

	while (hdr->wpr.falcon_id != WPR_HEADER_V1_FALCON_ID_INVALID) {
		wpr_header_v2_dump(&acr->subdev, hdr);
		if (!nvkm_acr_lsfw_add(NULL, acr, NULL, (hdr++)->wpr.falcon_id))
			return -ENOMEM;
	}

	return 0;
}

MODULE_FIRMWARE("nvidia/ga102/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/ga103/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/ga104/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/ga106/acr/ucode_unload.bin");
MODULE_FIRMWARE("nvidia/ga107/acr/ucode_unload.bin");

static const struct nvkm_acr_hsf_fwif
ga102_acr_unload_fwif[] = {
	{  0, ga100_acr_hsfw_ctor, &ga102_flcn_fw, NVKM_ACR_HSF_SEC2 },
	{}
};

MODULE_FIRMWARE("nvidia/ga102/acr/ucode_asb.bin");
MODULE_FIRMWARE("nvidia/ga103/acr/ucode_asb.bin");
MODULE_FIRMWARE("nvidia/ga104/acr/ucode_asb.bin");
MODULE_FIRMWARE("nvidia/ga106/acr/ucode_asb.bin");
MODULE_FIRMWARE("nvidia/ga107/acr/ucode_asb.bin");

static const struct nvkm_acr_hsf_fwif
ga102_acr_asb_fwif[] = {
	{  0, ga100_acr_hsfw_ctor, &ga102_flcn_fw, NVKM_ACR_HSF_GSP },
	{}
};

static const struct nvkm_falcon_fw_func
ga102_acr_ahesasc_0 = {
	.signature = ga100_flcn_fw_signature,
	.reset = gm200_flcn_fw_reset,
	.setup = gp102_acr_load_setup,
	.load = ga102_flcn_fw_load,
	.boot = ga102_flcn_fw_boot,
};

MODULE_FIRMWARE("nvidia/ga102/acr/ucode_ahesasc.bin");
MODULE_FIRMWARE("nvidia/ga103/acr/ucode_ahesasc.bin");
MODULE_FIRMWARE("nvidia/ga104/acr/ucode_ahesasc.bin");
MODULE_FIRMWARE("nvidia/ga106/acr/ucode_ahesasc.bin");
MODULE_FIRMWARE("nvidia/ga107/acr/ucode_ahesasc.bin");

static const struct nvkm_acr_hsf_fwif
ga102_acr_ahesasc_fwif[] = {
	{  0, ga100_acr_hsfw_ctor, &ga102_acr_ahesasc_0, NVKM_ACR_HSF_SEC2 },
	{}
};

static const struct nvkm_acr_func
ga102_acr = {
	.ahesasc = ga102_acr_ahesasc_fwif,
	.asb = ga102_acr_asb_fwif,
	.unload = ga102_acr_unload_fwif,
	.wpr_parse = ga102_acr_wpr_parse,
	.wpr_layout = ga102_acr_wpr_layout,
	.wpr_alloc = gp102_acr_wpr_alloc,
	.wpr_patch = ga102_acr_wpr_patch,
	.wpr_build = ga102_acr_wpr_build,
	.wpr_check = ga100_acr_wpr_check,
	.init = tu102_acr_init,
};

static int
ga102_acr_load(struct nvkm_acr *acr, int version,
	       const struct nvkm_acr_fwif *fwif)
{
	struct nvkm_subdev *subdev = &acr->subdev;
	const struct nvkm_acr_hsf_fwif *hsfwif;

	hsfwif = nvkm_firmware_load(subdev, fwif->func->ahesasc, "AcrAHESASC",
				    acr, NULL, "acr/ucode_ahesasc", "AHESASC");
	if (IS_ERR(hsfwif))
		return PTR_ERR(hsfwif);

	hsfwif = nvkm_firmware_load(subdev, fwif->func->asb, "AcrASB",
				    acr, NULL, "acr/ucode_asb", "ASB");
	if (IS_ERR(hsfwif))
		return PTR_ERR(hsfwif);

	hsfwif = nvkm_firmware_load(subdev, fwif->func->unload, "AcrUnload",
				    acr, NULL, "acr/ucode_unload", "unload");
	if (IS_ERR(hsfwif))
		return PTR_ERR(hsfwif);

	return 0;
}

static const struct nvkm_acr_fwif
ga102_acr_fwif[] = {
	{  0, ga102_acr_load, &ga102_acr },
	{ -1, gm200_acr_nofw, &gm200_acr },
	{}
};

int
ga102_acr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_acr **pacr)
{
	return nvkm_acr_new_(ga102_acr_fwif, device, type, inst, pacr);
}
