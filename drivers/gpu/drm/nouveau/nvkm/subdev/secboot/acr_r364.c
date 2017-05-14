/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
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

#include <core/gpuobj.h>

/*
 * r364 ACR: hsflcn_desc structure has changed to introduce the shadow_mem
 * parameter.
 */

struct acr_r364_hsflcn_desc {
	union {
		u8 reserved_dmem[0x200];
		u32 signatures[4];
	} ucode_reserved_space;
	u32 wpr_region_id;
	u32 wpr_offset;
	u32 mmu_memory_range;
	struct {
		u32 no_regions;
		struct {
			u32 start_addr;
			u32 end_addr;
			u32 region_id;
			u32 read_mask;
			u32 write_mask;
			u32 client_mask;
			u32 shadow_mem_start_addr;
		} region_props[2];
	} regions;
	u32 ucode_blob_size;
	u64 ucode_blob_base __aligned(8);
	struct {
		u32 vpr_enabled;
		u32 vpr_start;
		u32 vpr_end;
		u32 hdcp_policies;
	} vpr_desc;
};

static void
acr_r364_fixup_hs_desc(struct acr_r352 *acr, struct nvkm_secboot *sb,
		       void *_desc)
{
	struct acr_r364_hsflcn_desc *desc = _desc;
	struct nvkm_gpuobj *ls_blob = acr->ls_blob;

	/* WPR region information if WPR is not fixed */
	if (sb->wpr_size == 0) {
		u64 wpr_start = ls_blob->addr;
		u64 wpr_end = ls_blob->addr + ls_blob->size;

		if (acr->func->shadow_blob)
			wpr_start += ls_blob->size / 2;

		desc->wpr_region_id = 1;
		desc->regions.no_regions = 2;
		desc->regions.region_props[0].start_addr = wpr_start >> 8;
		desc->regions.region_props[0].end_addr = wpr_end >> 8;
		desc->regions.region_props[0].region_id = 1;
		desc->regions.region_props[0].read_mask = 0xf;
		desc->regions.region_props[0].write_mask = 0xc;
		desc->regions.region_props[0].client_mask = 0x2;
		if (acr->func->shadow_blob)
			desc->regions.region_props[0].shadow_mem_start_addr =
							     ls_blob->addr >> 8;
		else
			desc->regions.region_props[0].shadow_mem_start_addr = 0;
	} else {
		desc->ucode_blob_base = ls_blob->addr;
		desc->ucode_blob_size = ls_blob->size;
	}
}

const struct acr_r352_func
acr_r364_func = {
	.fixup_hs_desc = acr_r364_fixup_hs_desc,
	.generate_hs_bl_desc = acr_r361_generate_hs_bl_desc,
	.hs_bl_desc_size = sizeof(struct acr_r361_flcn_bl_desc),
	.ls_ucode_img_load = acr_r352_ls_ucode_img_load,
	.ls_fill_headers = acr_r352_ls_fill_headers,
	.ls_write_wpr = acr_r352_ls_write_wpr,
	.ls_func = {
		[NVKM_SECBOOT_FALCON_FECS] = &acr_r361_ls_fecs_func,
		[NVKM_SECBOOT_FALCON_GPCCS] = &acr_r361_ls_gpccs_func,
		[NVKM_SECBOOT_FALCON_PMU] = &acr_r361_ls_pmu_func,
	},
};


struct nvkm_acr *
acr_r364_new(unsigned long managed_falcons)
{
	return acr_r352_new_(&acr_r364_func, NVKM_SECBOOT_FALCON_PMU,
			     managed_falcons);
}
