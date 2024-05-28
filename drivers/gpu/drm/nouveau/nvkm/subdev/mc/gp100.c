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

#include <subdev/gsp.h>

const struct nvkm_intr_data
gp100_mc_intrs[] = {
	{ NVKM_ENGINE_DISP    , 0, 0, 0x04000000, true },
	{ NVKM_ENGINE_FIFO    , 0, 0, 0x00000100 },
	{ NVKM_SUBDEV_FAULT   , 0, 0, 0x00000200, true },
	{ NVKM_SUBDEV_PRIVRING, 0, 0, 0x40000000, true },
	{ NVKM_SUBDEV_BUS     , 0, 0, 0x10000000, true },
	{ NVKM_SUBDEV_FB      , 0, 0, 0x08002000, true },
	{ NVKM_SUBDEV_LTC     , 0, 0, 0x02000000, true },
	{ NVKM_SUBDEV_PMU     , 0, 0, 0x01000000, true },
	{ NVKM_SUBDEV_GPIO    , 0, 0, 0x00200000, true },
	{ NVKM_SUBDEV_I2C     , 0, 0, 0x00200000, true },
	{ NVKM_SUBDEV_TIMER   , 0, 0, 0x00100000, true },
	{ NVKM_SUBDEV_THERM   , 0, 0, 0x00040000, true },
	{ NVKM_SUBDEV_TOP     , 0, 0, 0x00009000 },
	{ NVKM_SUBDEV_TOP     , 0, 0, 0xffff6fff, true },
	{},
};

static void
gp100_mc_intr_allow(struct nvkm_intr *intr, int leaf, u32 mask)
{
	struct nvkm_mc *mc = container_of(intr, typeof(*mc), intr);

	nvkm_wr32(mc->subdev.device, 0x000160 + (leaf * 4), mask);
}

static void
gp100_mc_intr_block(struct nvkm_intr *intr, int leaf, u32 mask)
{
	struct nvkm_mc *mc = container_of(intr, typeof(*mc), intr);

	nvkm_wr32(mc->subdev.device, 0x000180 + (leaf * 4), mask);
}

static void
gp100_mc_intr_rearm(struct nvkm_intr *intr)
{
	int i;

	for (i = 0; i < intr->leaves; i++)
		intr->func->allow(intr, i, intr->mask[i]);
}

static void
gp100_mc_intr_unarm(struct nvkm_intr *intr)
{
	int i;

	for (i = 0; i < intr->leaves; i++)
		intr->func->block(intr, i, 0xffffffff);
}

const struct nvkm_intr_func
gp100_mc_intr = {
	.pending = nv04_mc_intr_pending,
	.unarm = gp100_mc_intr_unarm,
	.rearm = gp100_mc_intr_rearm,
	.block = gp100_mc_intr_block,
	.allow = gp100_mc_intr_allow,
};

static const struct nvkm_mc_func
gp100_mc = {
	.init = nv50_mc_init,
	.intr = &gp100_mc_intr,
	.intrs = gp100_mc_intrs,
	.intr_nonstall = true,
	.device = &nv04_mc_device,
	.reset = gk104_mc_reset,
};

int
gp100_mc_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	if (nvkm_gsp_rm(device->gsp))
		return -ENODEV;

	return nvkm_mc_new_(&gp100_mc, device, type, inst, pmc);
}
