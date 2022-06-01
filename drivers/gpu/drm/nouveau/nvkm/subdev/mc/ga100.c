/*
 * Copyright 2021 Red Hat Inc.
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
ga100_mc_device_disable(struct nvkm_mc *mc, u32 mask)
{
	struct nvkm_device *device = mc->subdev.device;

	nvkm_mask(device, 0x000600, mask, 0x00000000);
	nvkm_rd32(device, 0x000600);
	nvkm_rd32(device, 0x000600);
}

static void
ga100_mc_device_enable(struct nvkm_mc *mc, u32 mask)
{
	struct nvkm_device *device = mc->subdev.device;

	nvkm_mask(device, 0x000600, mask, mask);
	nvkm_rd32(device, 0x000600);
	nvkm_rd32(device, 0x000600);
}

static bool
ga100_mc_device_enabled(struct nvkm_mc *mc, u32 mask)
{
	return (nvkm_rd32(mc->subdev.device, 0x000600) & mask) == mask;
}

const struct nvkm_mc_device_func
ga100_mc_device = {
	.enabled = ga100_mc_device_enabled,
	.enable = ga100_mc_device_enable,
	.disable = ga100_mc_device_disable,
};

static void
ga100_mc_init(struct nvkm_mc *mc)
{
	struct nvkm_device *device = mc->subdev.device;

	nvkm_wr32(device, 0x000200, 0xffffffff);
	nvkm_wr32(device, 0x000600, 0xffffffff);
}

static const struct nvkm_mc_func
ga100_mc = {
	.init = ga100_mc_init,
	.device = &ga100_mc_device,
};

int
ga100_mc_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	return nvkm_mc_new_(&ga100_mc, device, type, inst, pmc);
}
