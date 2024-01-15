/*
 * Copyright 2023 Red Hat Inc.
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
ad102_gsp_r535_113_01 = {
	.flcn = &ga102_gsp_flcn,
	.fwsec = &ga102_gsp_fwsec,

	.sig_section = ".fwsignature_ad10x",

	.wpr_heap.os_carveout_size = 20 << 20,
	.wpr_heap.base_size = 8 << 20,
	.wpr_heap.min_size = 84 << 20,

	.booter.ctor = ga102_gsp_booter_ctor,

	.dtor = r535_gsp_dtor,
	.oneinit = tu102_gsp_oneinit,
	.init = r535_gsp_init,
	.fini = r535_gsp_fini,
	.reset = ga102_gsp_reset,

	.rm = &r535_gsp_rm,
};

static struct nvkm_gsp_fwif
ad102_gsps[] = {
	{ 0, r535_gsp_load, &ad102_gsp_r535_113_01, "535.113.01", true },
	{}
};

int
ad102_gsp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_gsp **pgsp)
{
	return nvkm_gsp_new_(ad102_gsps, device, type, inst, pgsp);
}
