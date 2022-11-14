/*
 * Copyright 2022 Red Hat Inc.
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

MODULE_FIRMWARE("nvidia/gv100/acr/unload_bl.bin");
MODULE_FIRMWARE("nvidia/gv100/acr/ucode_unload.bin");

static const struct nvkm_acr_hsf_fwif
gv100_acr_unload_fwif[] = {
	{ 0, gm200_acr_hsfw_ctor, &gp108_acr_hsfw_0, NVKM_ACR_HSF_PMU, 0, 0x00000000 },
	{}
};

MODULE_FIRMWARE("nvidia/gv100/acr/bl.bin");
MODULE_FIRMWARE("nvidia/gv100/acr/ucode_load.bin");

static const struct nvkm_acr_hsf_fwif
gv100_acr_load_fwif[] = {
	{ 0, gm200_acr_hsfw_ctor, &gp108_acr_load_0, NVKM_ACR_HSF_SEC2, 0, 0x00000010 },
	{}
};

static const struct nvkm_acr_func
gv100_acr = {
	.load = gv100_acr_load_fwif,
	.unload = gv100_acr_unload_fwif,
	.wpr_parse = gp102_acr_wpr_parse,
	.wpr_layout = gp102_acr_wpr_layout,
	.wpr_alloc = gp102_acr_wpr_alloc,
	.wpr_build = gp102_acr_wpr_build,
	.wpr_patch = gp102_acr_wpr_patch,
	.wpr_check = gm200_acr_wpr_check,
	.init = gm200_acr_init,
};

static const struct nvkm_acr_fwif
gv100_acr_fwif[] = {
	{  0, gp102_acr_load, &gv100_acr },
	{ -1, gm200_acr_nofw, &gm200_acr },
	{}
};

int
gv100_acr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_acr **pacr)
{
	return nvkm_acr_new_(gv100_acr_fwif, device, type, inst, pacr);
}
