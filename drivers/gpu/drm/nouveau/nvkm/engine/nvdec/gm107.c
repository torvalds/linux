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
#include "priv.h"

static const struct nvkm_falcon_func
gm107_nvdec_flcn = {
	.debug = 0xd00,
	.fbif = 0x600,
	.load_imem = nvkm_falcon_v1_load_imem,
	.load_dmem = nvkm_falcon_v1_load_dmem,
	.read_dmem = nvkm_falcon_v1_read_dmem,
	.bind_context = nvkm_falcon_v1_bind_context,
	.wait_for_halt = nvkm_falcon_v1_wait_for_halt,
	.clear_interrupt = nvkm_falcon_v1_clear_interrupt,
	.set_start_addr = nvkm_falcon_v1_set_start_addr,
	.start = nvkm_falcon_v1_start,
	.enable = nvkm_falcon_v1_enable,
	.disable = nvkm_falcon_v1_disable,
};

static const struct nvkm_nvdec_func
gm107_nvdec = {
	.flcn = &gm107_nvdec_flcn,
};

static int
gm107_nvdec_nofw(struct nvkm_nvdec *nvdec, int ver,
		 const struct nvkm_nvdec_fwif *fwif)
{
	return 0;
}

static const struct nvkm_nvdec_fwif
gm107_nvdec_fwif[] = {
	{ -1, gm107_nvdec_nofw, &gm107_nvdec },
	{}
};

int
gm107_nvdec_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		struct nvkm_nvdec **pnvdec)
{
	return nvkm_nvdec_new_(gm107_nvdec_fwif, device, type, inst, pnvdec);
}
