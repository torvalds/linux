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

#include "acr_r361.h"

#include <engine/falcon.h>
#include <core/msgqueue.h>
#include <subdev/pmu.h>
#include <engine/sec2.h>

static void
acr_r361_generate_flcn_bl_desc(const struct nvkm_acr *acr,
			       const struct ls_ucode_img *img, u64 wpr_addr,
			       void *_desc)
{
	struct acr_r361_flcn_bl_desc *desc = _desc;
	const struct ls_ucode_img_desc *pdesc = &img->ucode_desc;
	u64 base, addr_code, addr_data;

	base = wpr_addr + img->ucode_off + pdesc->app_start_offset;
	addr_code = base + pdesc->app_resident_code_offset;
	addr_data = base + pdesc->app_resident_data_offset;

	desc->ctx_dma = FALCON_DMAIDX_UCODE;
	desc->code_dma_base = u64_to_flcn64(addr_code);
	desc->non_sec_code_off = pdesc->app_resident_code_offset;
	desc->non_sec_code_size = pdesc->app_resident_code_size;
	desc->code_entry_point = pdesc->app_imem_entry;
	desc->data_dma_base = u64_to_flcn64(addr_data);
	desc->data_size = pdesc->app_resident_data_size;
}

void
acr_r361_generate_hs_bl_desc(const struct hsf_load_header *hdr, void *_bl_desc,
			    u64 offset)
{
	struct acr_r361_flcn_bl_desc *bl_desc = _bl_desc;

	bl_desc->ctx_dma = FALCON_DMAIDX_VIRT;
	bl_desc->code_dma_base = u64_to_flcn64(offset);
	bl_desc->non_sec_code_off = hdr->non_sec_code_off;
	bl_desc->non_sec_code_size = hdr->non_sec_code_size;
	bl_desc->sec_code_off = hsf_load_header_app_off(hdr, 0);
	bl_desc->sec_code_size = hsf_load_header_app_size(hdr, 0);
	bl_desc->code_entry_point = 0;
	bl_desc->data_dma_base = u64_to_flcn64(offset + hdr->data_dma_base);
	bl_desc->data_size = hdr->data_size;
}

const struct acr_r352_ls_func
acr_r361_ls_fecs_func = {
	.load = acr_ls_ucode_load_fecs,
	.generate_bl_desc = acr_r361_generate_flcn_bl_desc,
	.bl_desc_size = sizeof(struct acr_r361_flcn_bl_desc),
};

const struct acr_r352_ls_func
acr_r361_ls_gpccs_func = {
	.load = acr_ls_ucode_load_gpccs,
	.generate_bl_desc = acr_r361_generate_flcn_bl_desc,
	.bl_desc_size = sizeof(struct acr_r361_flcn_bl_desc),
	/* GPCCS will be loaded using PRI */
	.lhdr_flags = LSF_FLAG_FORCE_PRIV_LOAD,
};

struct acr_r361_pmu_bl_desc {
	u32 reserved;
	u32 dma_idx;
	struct flcn_u64 code_dma_base;
	u32 total_code_size;
	u32 code_size_to_load;
	u32 code_entry_point;
	struct flcn_u64 data_dma_base;
	u32 data_size;
	struct flcn_u64 overlay_dma_base;
	u32 argc;
	u32 argv;
};

static void
acr_r361_generate_pmu_bl_desc(const struct nvkm_acr *acr,
			      const struct ls_ucode_img *img, u64 wpr_addr,
			      void *_desc)
{
	const struct ls_ucode_img_desc *pdesc = &img->ucode_desc;
	const struct nvkm_pmu *pmu = acr->subdev->device->pmu;
	struct acr_r361_pmu_bl_desc *desc = _desc;
	u64 base, addr_code, addr_data;
	u32 addr_args;

	base = wpr_addr + img->ucode_off + pdesc->app_start_offset;
	addr_code = base + pdesc->app_resident_code_offset;
	addr_data = base + pdesc->app_resident_data_offset;
	addr_args = pmu->falcon->data.limit;
	addr_args -= NVKM_MSGQUEUE_CMDLINE_SIZE;

	desc->dma_idx = FALCON_DMAIDX_UCODE;
	desc->code_dma_base = u64_to_flcn64(addr_code);
	desc->total_code_size = pdesc->app_size;
	desc->code_size_to_load = pdesc->app_resident_code_size;
	desc->code_entry_point = pdesc->app_imem_entry;
	desc->data_dma_base = u64_to_flcn64(addr_data);
	desc->data_size = pdesc->app_resident_data_size;
	desc->overlay_dma_base = u64_to_flcn64(addr_code);
	desc->argc = 1;
	desc->argv = addr_args;
}

const struct acr_r352_ls_func
acr_r361_ls_pmu_func = {
	.load = acr_ls_ucode_load_pmu,
	.generate_bl_desc = acr_r361_generate_pmu_bl_desc,
	.bl_desc_size = sizeof(struct acr_r361_pmu_bl_desc),
	.post_run = acr_ls_pmu_post_run,
};

static void
acr_r361_generate_sec2_bl_desc(const struct nvkm_acr *acr,
			       const struct ls_ucode_img *img, u64 wpr_addr,
			       void *_desc)
{
	const struct ls_ucode_img_desc *pdesc = &img->ucode_desc;
	const struct nvkm_sec2 *sec = acr->subdev->device->sec2;
	struct acr_r361_pmu_bl_desc *desc = _desc;
	u64 base, addr_code, addr_data;
	u32 addr_args;

	base = wpr_addr + img->ucode_off + pdesc->app_start_offset;
	/* For some reason we should not add app_resident_code_offset here */
	addr_code = base;
	addr_data = base + pdesc->app_resident_data_offset;
	addr_args = sec->falcon->data.limit;
	addr_args -= NVKM_MSGQUEUE_CMDLINE_SIZE;

	desc->dma_idx = FALCON_SEC2_DMAIDX_UCODE;
	desc->code_dma_base = u64_to_flcn64(addr_code);
	desc->total_code_size = pdesc->app_size;
	desc->code_size_to_load = pdesc->app_resident_code_size;
	desc->code_entry_point = pdesc->app_imem_entry;
	desc->data_dma_base = u64_to_flcn64(addr_data);
	desc->data_size = pdesc->app_resident_data_size;
	desc->overlay_dma_base = u64_to_flcn64(addr_code);
	desc->argc = 1;
	/* args are stored at the beginning of EMEM */
	desc->argv = 0x01000000;
}

const struct acr_r352_ls_func
acr_r361_ls_sec2_func = {
	.load = acr_ls_ucode_load_sec2,
	.generate_bl_desc = acr_r361_generate_sec2_bl_desc,
	.bl_desc_size = sizeof(struct acr_r361_pmu_bl_desc),
	.post_run = acr_ls_sec2_post_run,
};


const struct acr_r352_func
acr_r361_func = {
	.fixup_hs_desc = acr_r352_fixup_hs_desc,
	.generate_hs_bl_desc = acr_r361_generate_hs_bl_desc,
	.hs_bl_desc_size = sizeof(struct acr_r361_flcn_bl_desc),
	.ls_ucode_img_load = acr_r352_ls_ucode_img_load,
	.ls_fill_headers = acr_r352_ls_fill_headers,
	.ls_write_wpr = acr_r352_ls_write_wpr,
	.ls_func = {
		[NVKM_SECBOOT_FALCON_FECS] = &acr_r361_ls_fecs_func,
		[NVKM_SECBOOT_FALCON_GPCCS] = &acr_r361_ls_gpccs_func,
		[NVKM_SECBOOT_FALCON_PMU] = &acr_r361_ls_pmu_func,
		[NVKM_SECBOOT_FALCON_SEC2] = &acr_r361_ls_sec2_func,
	},
};

struct nvkm_acr *
acr_r361_new(unsigned long managed_falcons)
{
	return acr_r352_new_(&acr_r361_func, NVKM_SECBOOT_FALCON_PMU,
			     managed_falcons);
}
