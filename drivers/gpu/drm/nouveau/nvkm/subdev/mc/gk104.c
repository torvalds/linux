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

const struct nvkm_mc_map
gk104_mc_reset[] = {
	{ 0x00000100, NVKM_ENGINE_FIFO },
	{ 0x00002000, NVKM_SUBDEV_PMU, true },
	{}
};

const struct nvkm_mc_map
gk104_mc_intr[] = {
	{ 0x04000000, NVKM_ENGINE_DISP },
	{ 0x00000100, NVKM_ENGINE_FIFO },
	{ 0x40000000, NVKM_SUBDEV_IBUS },
	{ 0x10000000, NVKM_SUBDEV_BUS },
	{ 0x08000000, NVKM_SUBDEV_FB },
	{ 0x02000000, NVKM_SUBDEV_LTC },
	{ 0x01000000, NVKM_SUBDEV_PMU },
	{ 0x00200000, NVKM_SUBDEV_GPIO },
	{ 0x00200000, NVKM_SUBDEV_I2C },
	{ 0x00100000, NVKM_SUBDEV_TIMER },
	{ 0x00040000, NVKM_SUBDEV_THERM },
	{ 0x00002000, NVKM_SUBDEV_FB },
	{},
};

static const struct nvkm_mc_func
gk104_mc = {
	.init = nv50_mc_init,
	.intr = gk104_mc_intr,
	.intr_unarm = gf100_mc_intr_unarm,
	.intr_rearm = gf100_mc_intr_rearm,
	.intr_mask = gf100_mc_intr_mask,
	.intr_stat = gf100_mc_intr_stat,
	.reset = gk104_mc_reset,
	.unk260 = gf100_mc_unk260,
};

int
gk104_mc_new(struct nvkm_device *device, int index, struct nvkm_mc **pmc)
{
	return nvkm_mc_new_(&gk104_mc, device, index, pmc);
}
