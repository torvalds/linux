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

static const struct nvkm_falcon_func
gv100_gsp_flcn = {
	.disable = gm200_flcn_disable,
	.enable = gm200_flcn_enable,
	.reset_eng = gp102_flcn_reset_eng,
	.reset_wait_mem_scrubbing = gm200_flcn_reset_wait_mem_scrubbing,
	.bind_inst = gm200_flcn_bind_inst,
	.bind_stat = gm200_flcn_bind_stat,
	.bind_intr = true,
	.imem_pio = &gm200_flcn_imem_pio,
	.dmem_pio = &gm200_flcn_dmem_pio,
};

const struct nvkm_gsp_func
gv100_gsp = {
	.flcn = &gv100_gsp_flcn,
};

int
gv100_gsp_nofw(struct nvkm_gsp *gsp, int ver, const struct nvkm_gsp_fwif *fwif)
{
	return 0;
}

static struct nvkm_gsp_fwif
gv100_gsps[] = {
	{ -1, gv100_gsp_nofw, &gv100_gsp },
	{}
};

int
gv100_gsp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_gsp **pgsp)
{
	return nvkm_gsp_new_(gv100_gsps, device, type, inst, pgsp);
}
