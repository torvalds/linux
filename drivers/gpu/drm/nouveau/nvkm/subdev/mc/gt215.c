/*
 * Copyright 2016 Red Hat Inc.
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

static const struct nvkm_mc_map
gt215_mc_reset[] = {
	{ 0x04008000, NVKM_ENGINE_MSVLD },
	{ 0x01020000, NVKM_ENGINE_MSPDEC },
	{ 0x00802000, NVKM_ENGINE_CE, 0 },
	{ 0x00400002, NVKM_ENGINE_MSPPP },
	{ 0x00201000, NVKM_ENGINE_GR },
	{ 0x00000100, NVKM_ENGINE_FIFO },
	{}
};

static const struct nvkm_intr_data
gt215_mc_intrs[] = {
	{ NVKM_ENGINE_DISP  , 0, 0, 0x04000000, true },
	{ NVKM_ENGINE_CE    , 0, 0, 0x00400000, true },
	{ NVKM_ENGINE_MSPDEC, 0, 0, 0x00020000, true },
	{ NVKM_ENGINE_MSVLD , 0, 0, 0x00008000, true },
	{ NVKM_ENGINE_GR    , 0, 0, 0x00001000, true },
	{ NVKM_ENGINE_FIFO  , 0, 0, 0x00000100 },
	{ NVKM_ENGINE_MSPPP , 0, 0, 0x00000001, true },
	{ NVKM_SUBDEV_FB    , 0, 0, 0x00429101, true },
	{ NVKM_SUBDEV_BUS   , 0, 0, 0x10000000, true },
	{ NVKM_SUBDEV_GPIO  , 0, 0, 0x00200000, true },
	{ NVKM_SUBDEV_I2C   , 0, 0, 0x00200000, true },
	{ NVKM_SUBDEV_TIMER , 0, 0, 0x00100000, true },
	{ NVKM_SUBDEV_THERM , 0, 0, 0x00080000, true },
	{ NVKM_SUBDEV_PMU   , 0, 0, 0x00040000, true },
	{},
};

static void
gt215_mc_intr_allow(struct nvkm_intr *intr, int leaf, u32 mask)
{
	struct nvkm_mc *mc = container_of(intr, typeof(*mc), intr);

	nvkm_mask(mc->subdev.device, 0x000640 + (leaf * 4), mask, mask);
}

static void
gt215_mc_intr_block(struct nvkm_intr *intr, int leaf, u32 mask)
{
	struct nvkm_mc *mc = container_of(intr, typeof(*mc), intr);

	nvkm_mask(mc->subdev.device, 0x000640 + (leaf * 4), mask, 0);
}

const struct nvkm_intr_func
gt215_mc_intr = {
	.pending = nv04_mc_intr_pending,
	.unarm = nv04_mc_intr_unarm,
	.rearm = nv04_mc_intr_rearm,
	.block = gt215_mc_intr_block,
	.allow = gt215_mc_intr_allow,
};

static const struct nvkm_mc_func
gt215_mc = {
	.init = nv50_mc_init,
	.intr = &nv04_mc_intr,
	.intrs = gt215_mc_intrs,
	.device = &nv04_mc_device,
	.reset = gt215_mc_reset,
};

int
gt215_mc_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	return nvkm_mc_new_(&gt215_mc, device, type, inst, pmc);
}
