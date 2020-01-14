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

#include <subdev/mmu.h>

#include <nvfw/flcn.h>

void
gp108_acr_hsfw_bld(struct nvkm_acr *acr, struct nvkm_acr_hsf *hsf)
{
	struct flcn_bl_dmem_desc_v2 hsdesc = {
		.ctx_dma = FALCON_DMAIDX_VIRT,
		.code_dma_base = hsf->vma->addr,
		.non_sec_code_off = hsf->non_sec_addr,
		.non_sec_code_size = hsf->non_sec_size,
		.sec_code_off = hsf->sec_addr,
		.sec_code_size = hsf->sec_size,
		.code_entry_point = 0,
		.data_dma_base = hsf->vma->addr + hsf->data_addr,
		.data_size = hsf->data_size,
		.argc = 0,
		.argv = 0,
	};

	flcn_bl_dmem_desc_v2_dump(&acr->subdev, &hsdesc);

	nvkm_falcon_load_dmem(hsf->falcon, &hsdesc, 0, sizeof(hsdesc), 0);
}

const struct nvkm_acr_hsf_func
gp108_acr_unload_0 = {
	.load = gm200_acr_unload_load,
	.boot = gm200_acr_unload_boot,
	.bld = gp108_acr_hsfw_bld,
};

MODULE_FIRMWARE("nvidia/gp108/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gp108/acr/ucode_unload.bin");

MODULE_FIRMWARE("nvidia/gv100/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gv100/acr/ucode_unload.bin");

static const struct nvkm_acr_hsf_fwif
gp108_acr_unload_fwif[] = {
	{ 0, nvkm_acr_hsfw_load, &gp108_acr_unload_0 },
	{}
};

static const struct nvkm_acr_hsf_func
gp108_acr_load_0 = {
	.load = gp102_acr_load_load,
	.boot = gm200_acr_load_boot,
	.bld = gp108_acr_hsfw_bld,
};

MODULE_FIRMWARE("nvidia/gp108/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gp108/acr/ucode_load.bin");

MODULE_FIRMWARE("nvidia/gv100/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gv100/acr/ucode_load.bin");

static const struct nvkm_acr_hsf_fwif
gp108_acr_load_fwif[] = {
	{ 0, nvkm_acr_hsfw_load, &gp108_acr_load_0 },
	{}
};

static const struct nvkm_acr_func
gp108_acr = {
	.load = gp108_acr_load_fwif,
	.unload = gp108_acr_unload_fwif,
	.wpr_parse = gp102_acr_wpr_parse,
	.wpr_layout = gp102_acr_wpr_layout,
	.wpr_alloc = gp102_acr_wpr_alloc,
	.wpr_build = gp102_acr_wpr_build,
	.wpr_patch = gp102_acr_wpr_patch,
	.wpr_check = gm200_acr_wpr_check,
	.init = gm200_acr_init,
};

static const struct nvkm_acr_fwif
gp108_acr_fwif[] = {
	{ 0, gp102_acr_load, &gp108_acr },
	{}
};

int
gp108_acr_new(struct nvkm_device *device, int index, struct nvkm_acr **pacr)
{
	return nvkm_acr_new_(gp108_acr_fwif, device, index, pacr);
}
