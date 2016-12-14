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

#include "acr_r352.h"
#include "ls_ucode.h"

#include <engine/falcon.h>

/**
 * struct acr_r361_flcn_bl_desc - DMEM bootloader descriptor
 * @signature:		16B signature for secure code. 0s if no secure code
 * @ctx_dma:		DMA context to be used by BL while loading code/data
 * @code_dma_base:	256B-aligned Physical FB Address where code is located
 *			(falcon's $xcbase register)
 * @non_sec_code_off:	offset from code_dma_base where the non-secure code is
 *                      located. The offset must be multiple of 256 to help perf
 * @non_sec_code_size:	the size of the nonSecure code part.
 * @sec_code_off:	offset from code_dma_base where the secure code is
 *                      located. The offset must be multiple of 256 to help perf
 * @sec_code_size:	offset from code_dma_base where the secure code is
 *                      located. The offset must be multiple of 256 to help perf
 * @code_entry_point:	code entry point which will be invoked by BL after
 *                      code is loaded.
 * @data_dma_base:	256B aligned Physical FB Address where data is located.
 *			(falcon's $xdbase register)
 * @data_size:		size of data block. Should be multiple of 256B
 *
 * Structure used by the bootloader to load the rest of the code. This has
 * to be filled by host and copied into DMEM at offset provided in the
 * hsflcn_bl_desc.bl_desc_dmem_load_off.
 */
struct acr_r361_flcn_bl_desc {
	u32 reserved[4];
	u32 signature[4];
	u32 ctx_dma;
	struct flcn_u64 code_dma_base;
	u32 non_sec_code_off;
	u32 non_sec_code_size;
	u32 sec_code_off;
	u32 sec_code_size;
	u32 code_entry_point;
	struct flcn_u64 data_dma_base;
	u32 data_size;
};

static void
acr_r361_generate_flcn_bl_desc(const struct nvkm_acr *acr,
			       const struct ls_ucode_img *img, u64 wpr_addr,
			       void *_desc)
{
	struct acr_r361_flcn_bl_desc *desc = _desc;
	const struct ls_ucode_img_desc *pdesc = &img->ucode_desc;
	u64 base, addr_code, addr_data;

	base = wpr_addr + img->lsb_header.ucode_off + pdesc->app_start_offset;
	addr_code = base + pdesc->app_resident_code_offset;
	addr_data = base + pdesc->app_resident_data_offset;

	memset(desc, 0, sizeof(*desc));
	desc->ctx_dma = FALCON_DMAIDX_UCODE;
	desc->code_dma_base = u64_to_flcn64(addr_code);
	desc->non_sec_code_off = pdesc->app_resident_code_offset;
	desc->non_sec_code_size = pdesc->app_resident_code_size;
	desc->code_entry_point = pdesc->app_imem_entry;
	desc->data_dma_base = u64_to_flcn64(addr_data);
	desc->data_size = pdesc->app_resident_data_size;
}

static void
acr_r361_generate_hs_bl_desc(const struct hsf_load_header *hdr, void *_bl_desc,
			    u64 offset)
{
	struct acr_r361_flcn_bl_desc *bl_desc = _bl_desc;

	memset(bl_desc, 0, sizeof(*bl_desc));
	bl_desc->ctx_dma = FALCON_DMAIDX_VIRT;
	bl_desc->code_dma_base = u64_to_flcn64(offset);
	bl_desc->non_sec_code_off = hdr->non_sec_code_off;
	bl_desc->non_sec_code_size = hdr->non_sec_code_size;
	bl_desc->sec_code_off = hdr->app[0].sec_code_off;
	bl_desc->sec_code_size = hdr->app[0].sec_code_size;
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

const struct acr_r352_func
acr_r361_func = {
	.generate_hs_bl_desc = acr_r361_generate_hs_bl_desc,
	.hs_bl_desc_size = sizeof(struct acr_r361_flcn_bl_desc),
	.ls_func = {
		[NVKM_SECBOOT_FALCON_FECS] = &acr_r361_ls_fecs_func,
		[NVKM_SECBOOT_FALCON_GPCCS] = &acr_r361_ls_gpccs_func,
	},
};

struct nvkm_acr *
acr_r361_new(unsigned long managed_falcons)
{
	return acr_r352_new_(&acr_r361_func, NVKM_SECBOOT_FALCON_PMU,
			     managed_falcons);
}
