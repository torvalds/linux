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
#define gp100_mc(p) container_of((p), struct gp100_mc, base)
#include "priv.h"

struct gp100_mc {
	struct nvkm_mc base;
	spinlock_t lock;
	bool intr;
	u32 mask;
};

static void
gp100_mc_intr_update(struct gp100_mc *mc)
{
	struct nvkm_device *device = mc->base.subdev.device;
	u32 mask = mc->intr ? mc->mask : 0, i;
	for (i = 0; i < 2; i++) {
		nvkm_wr32(device, 0x000180 + (i * 0x04), ~mask);
		nvkm_wr32(device, 0x000160 + (i * 0x04),  mask);
	}
}

void
gp100_mc_intr_unarm(struct nvkm_mc *base)
{
	struct gp100_mc *mc = gp100_mc(base);
	unsigned long flags;
	spin_lock_irqsave(&mc->lock, flags);
	mc->intr = false;
	gp100_mc_intr_update(mc);
	spin_unlock_irqrestore(&mc->lock, flags);
}

void
gp100_mc_intr_rearm(struct nvkm_mc *base)
{
	struct gp100_mc *mc = gp100_mc(base);
	unsigned long flags;
	spin_lock_irqsave(&mc->lock, flags);
	mc->intr = true;
	gp100_mc_intr_update(mc);
	spin_unlock_irqrestore(&mc->lock, flags);
}

void
gp100_mc_intr_mask(struct nvkm_mc *base, u32 mask, u32 intr)
{
	struct gp100_mc *mc = gp100_mc(base);
	unsigned long flags;
	spin_lock_irqsave(&mc->lock, flags);
	mc->mask = (mc->mask & ~mask) | intr;
	gp100_mc_intr_update(mc);
	spin_unlock_irqrestore(&mc->lock, flags);
}

const struct nvkm_mc_map
gp100_mc_intr[] = {
	{ 0x04000000, NVKM_ENGINE_DISP },
	{ 0x00000100, NVKM_ENGINE_FIFO },
	{ 0x00000200, NVKM_SUBDEV_FAULT },
	{ 0x40000000, NVKM_SUBDEV_PRIVRING },
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
gp100_mc = {
	.init = nv50_mc_init,
	.intr = gp100_mc_intr,
	.intr_unarm = gp100_mc_intr_unarm,
	.intr_rearm = gp100_mc_intr_rearm,
	.intr_mask = gp100_mc_intr_mask,
	.intr_stat = gf100_mc_intr_stat,
	.reset = gk104_mc_reset,
};

int
gp100_mc_new_(const struct nvkm_mc_func *func, struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	struct gp100_mc *mc;

	if (!(mc = kzalloc(sizeof(*mc), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_mc_ctor(func, device, type, inst, &mc->base);
	*pmc = &mc->base;

	spin_lock_init(&mc->lock);
	mc->intr = false;
	mc->mask = 0x7fffffff;
	return 0;
}

int
gp100_mc_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	return gp100_mc_new_(&gp100_mc, device, type, inst, pmc);
}
