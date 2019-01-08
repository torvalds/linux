/*
 * Copyright 2018 Red Hat Inc.
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

static void
tu104_mc_intr_hack(struct nvkm_mc *mc, bool *handled)
{
	struct nvkm_device *device = mc->subdev.device;
	u32 stat = nvkm_rd32(device, 0xb81010);
	if (stat & 0x00000050) {
		struct nvkm_subdev *subdev =
			nvkm_device_subdev(device, NVKM_SUBDEV_FAULT);
		nvkm_wr32(device, 0xb81010, stat & 0x00000050);
		if (subdev)
			nvkm_subdev_intr(subdev);
		*handled = true;
	}
}

static const struct nvkm_mc_func
tu104_mc = {
	.init = nv50_mc_init,
	.intr = gp100_mc_intr,
	.intr_unarm = gp100_mc_intr_unarm,
	.intr_rearm = gp100_mc_intr_rearm,
	.intr_mask = gp100_mc_intr_mask,
	.intr_stat = gf100_mc_intr_stat,
	.intr_hack = tu104_mc_intr_hack,
	.reset = gk104_mc_reset,
};

int
tu104_mc_new(struct nvkm_device *device, int index, struct nvkm_mc **pmc)
{
	return gp100_mc_new_(&tu104_mc, device, index, pmc);
}
