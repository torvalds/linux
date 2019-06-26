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

#include "acr_r370.h"
#include "acr_r367.h"

#include <core/msgqueue.h>
#include <subdev/pmu.h>

static void
acr_r375_generate_pmu_bl_desc(const struct nvkm_acr *acr,
			      const struct ls_ucode_img *img, u64 wpr_addr,
			      void *_desc)
{
	const struct ls_ucode_img_desc *pdesc = &img->ucode_desc;
	const struct nvkm_pmu *pmu = acr->subdev->device->pmu;
	struct acr_r370_flcn_bl_desc *desc = _desc;
	u64 base, addr_code, addr_data;
	u32 addr_args;

	base = wpr_addr + img->ucode_off + pdesc->app_start_offset;
	addr_code = base + pdesc->app_resident_code_offset;
	addr_data = base + pdesc->app_resident_data_offset;
	addr_args = pmu->falcon->data.limit;
	addr_args -= NVKM_MSGQUEUE_CMDLINE_SIZE;

	desc->ctx_dma = FALCON_DMAIDX_UCODE;
	desc->code_dma_base = u64_to_flcn64(addr_code);
	desc->non_sec_code_off = pdesc->app_resident_code_offset;
	desc->non_sec_code_size = pdesc->app_resident_code_size;
	desc->code_entry_point = pdesc->app_imem_entry;
	desc->data_dma_base = u64_to_flcn64(addr_data);
	desc->data_size = pdesc->app_resident_data_size;
	desc->argc = 1;
	desc->argv = addr_args;
}

static const struct acr_r352_lsf_func
acr_r375_ls_pmu_func_0 = {
	.generate_bl_desc = acr_r375_generate_pmu_bl_desc,
	.bl_desc_size = sizeof(struct acr_r370_flcn_bl_desc),
};

const struct acr_r352_ls_func
acr_r375_ls_pmu_func = {
	.load = acr_ls_ucode_load_pmu,
	.post_run = acr_ls_pmu_post_run,
	.version_max = 0,
	.version = {
		&acr_r375_ls_pmu_func_0,
	}
};

const struct acr_r352_func
acr_r375_func = {
	.fixup_hs_desc = acr_r367_fixup_hs_desc,
	.generate_hs_bl_desc = acr_r370_generate_hs_bl_desc,
	.hs_bl_desc_size = sizeof(struct acr_r370_flcn_bl_desc),
	.shadow_blob = true,
	.ls_ucode_img_load = acr_r367_ls_ucode_img_load,
	.ls_fill_headers = acr_r367_ls_fill_headers,
	.ls_write_wpr = acr_r367_ls_write_wpr,
	.ls_func = {
		[NVKM_SECBOOT_FALCON_FECS] = &acr_r370_ls_fecs_func,
		[NVKM_SECBOOT_FALCON_GPCCS] = &acr_r370_ls_gpccs_func,
		[NVKM_SECBOOT_FALCON_PMU] = &acr_r375_ls_pmu_func,
	},
};

struct nvkm_acr *
acr_r375_new(enum nvkm_secboot_falcon boot_falcon,
	     unsigned long managed_falcons)
{
	return acr_r352_new_(&acr_r375_func, boot_falcon, managed_falcons);
}
