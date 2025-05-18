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

static const struct nvkm_gsp_func
tu116_gsp = {
	.flcn = &tu102_gsp_flcn,
	.fwsec = &tu102_gsp_fwsec,

	.sig_section = ".fwsignature_tu11x",

	.booter.ctor = tu102_gsp_booter_ctor,

	.dtor = r535_gsp_dtor,
	.oneinit = tu102_gsp_oneinit,
	.init = tu102_gsp_init,
	.fini = tu102_gsp_fini,
	.reset = tu102_gsp_reset,

	.rm.gpu = &tu1xx_gpu,
};

static struct nvkm_gsp_fwif
tu116_gsps[] = {
	{  1, tu102_gsp_load, &tu116_gsp, &r570_rm_tu102, "570.144" },
	{  0, tu102_gsp_load, &tu116_gsp, &r535_rm_tu102, "535.113.01" },
	{ -1, gv100_gsp_nofw, &gv100_gsp },
	{}
};

int
tu116_gsp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_gsp **pgsp)
{
	return nvkm_gsp_new_(tu116_gsps, device, type, inst, pgsp);
}

NVKM_GSP_FIRMWARE_BOOTER(tu116, 535.113.01);
NVKM_GSP_FIRMWARE_BOOTER(tu117, 535.113.01);

NVKM_GSP_FIRMWARE_BOOTER(tu116, 570.144);
NVKM_GSP_FIRMWARE_BOOTER(tu117, 570.144);
