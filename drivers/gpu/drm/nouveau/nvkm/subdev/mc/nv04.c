/*
 * Copyright 2012 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */
#include "priv.h"

const struct nvkm_mc_map
nv04_mc_intr[] = {
	{ 0x00000001, NVKM_ENGINE_MPEG },	/* NV17- MPEG/ME */
	{ 0x00000100, NVKM_ENGINE_FIFO },
	{ 0x00001000, NVKM_ENGINE_GR },
	{ 0x00010000, NVKM_ENGINE_DISP },
	{ 0x00020000, NVKM_ENGINE_VP },	/* NV40- */
	{ 0x00100000, NVKM_SUBDEV_TIMER },
	{ 0x01000000, NVKM_ENGINE_DISP },	/* NV04- PCRTC0 */
	{ 0x02000000, NVKM_ENGINE_DISP },	/* NV11- PCRTC1 */
	{ 0x10000000, NVKM_SUBDEV_BUS },
	{ 0x80000000, NVKM_ENGINE_SW },
	{}
};

void
nv04_mc_intr_unarm(struct nvkm_mc *mc)
{
	struct nvkm_device *device = mc->subdev.device;
	nvkm_wr32(device, 0x000140, 0x00000000);
	nvkm_rd32(device, 0x000140);
}

void
nv04_mc_intr_rearm(struct nvkm_mc *mc)
{
	struct nvkm_device *device = mc->subdev.device;
	nvkm_wr32(device, 0x000140, 0x00000001);
}

u32
nv04_mc_intr_mask(struct nvkm_mc *mc)
{
	return nvkm_rd32(mc->subdev.device, 0x000100);
}

void
nv04_mc_init(struct nvkm_mc *mc)
{
	struct nvkm_device *device = mc->subdev.device;
	nvkm_wr32(device, 0x000200, 0xffffffff); /* everything enabled */
	nvkm_wr32(device, 0x001850, 0x00000001); /* disable rom access */
}

static const struct nvkm_mc_func
nv04_mc = {
	.init = nv04_mc_init,
	.intr = nv04_mc_intr,
	.intr_unarm = nv04_mc_intr_unarm,
	.intr_rearm = nv04_mc_intr_rearm,
	.intr_mask = nv04_mc_intr_mask,
};

int
nv04_mc_new(struct nvkm_device *device, int index, struct nvkm_mc **pmc)
{
	return nvkm_mc_new_(&nv04_mc, device, index, pmc);
}
