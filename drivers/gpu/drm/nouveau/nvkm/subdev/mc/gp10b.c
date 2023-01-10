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
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "priv.h"

static void
gp10b_mc_init(struct nvkm_mc *mc)
{
	struct nvkm_device *device = mc->subdev.device;
	nvkm_wr32(device, 0x000200, 0xffffffff); /* everything on */
	nvkm_wr32(device, 0x00020c, 0xffffffff); /* everything out of ELPG */
}

static const struct nvkm_mc_func
gp10b_mc = {
	.init = gp10b_mc_init,
	.intr = &gp100_mc_intr,
	.intrs = gp100_mc_intrs,
	.intr_nonstall = true,
	.device = &nv04_mc_device,
	.reset = gk104_mc_reset,
};

int
gp10b_mc_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	return nvkm_mc_new_(&gp10b_mc, device, type, inst, pmc);
}
