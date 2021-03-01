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
#define tu102_mc(p) container_of((p), struct tu102_mc, base)
#include "priv.h"

struct tu102_mc {
	struct nvkm_mc base;
	spinlock_t lock;
	bool intr;
	u32 mask;
};

static void
tu102_mc_intr_update(struct tu102_mc *mc)
{
	struct nvkm_device *device = mc->base.subdev.device;
	u32 mask = mc->intr ? mc->mask : 0, i;

	for (i = 0; i < 2; i++) {
		nvkm_wr32(device, 0x000180 + (i * 0x04), ~mask);
		nvkm_wr32(device, 0x000160 + (i * 0x04),  mask);
	}

	if (mask & 0x00000200)
		nvkm_wr32(device, 0xb81608, 0x6);
	else
		nvkm_wr32(device, 0xb81610, 0x6);
}

void
tu102_mc_intr_unarm(struct nvkm_mc *base)
{
	struct tu102_mc *mc = tu102_mc(base);
	unsigned long flags;

	spin_lock_irqsave(&mc->lock, flags);
	mc->intr = false;
	tu102_mc_intr_update(mc);
	spin_unlock_irqrestore(&mc->lock, flags);
}

void
tu102_mc_intr_rearm(struct nvkm_mc *base)
{
	struct tu102_mc *mc = tu102_mc(base);
	unsigned long flags;

	spin_lock_irqsave(&mc->lock, flags);
	mc->intr = true;
	tu102_mc_intr_update(mc);
	spin_unlock_irqrestore(&mc->lock, flags);
}

void
tu102_mc_intr_mask(struct nvkm_mc *base, u32 mask, u32 intr)
{
	struct tu102_mc *mc = tu102_mc(base);
	unsigned long flags;

	spin_lock_irqsave(&mc->lock, flags);
	mc->mask = (mc->mask & ~mask) | intr;
	tu102_mc_intr_update(mc);
	spin_unlock_irqrestore(&mc->lock, flags);
}

static u32
tu102_mc_intr_stat(struct nvkm_mc *mc)
{
	struct nvkm_device *device = mc->subdev.device;
	u32 intr0 = nvkm_rd32(device, 0x000100);
	u32 intr1 = nvkm_rd32(device, 0x000104);
	u32 intr_top = nvkm_rd32(device, 0xb81600);

	/* Turing and above route the MMU fault interrupts via a different
	 * interrupt tree with different control registers. For the moment remap
	 * them back to the old PMC vector.
	 */
	if (intr_top & 0x00000006)
		intr0 |= 0x00000200;

	return intr0 | intr1;
}


static const struct nvkm_mc_func
tu102_mc = {
	.init = nv50_mc_init,
	.intr = gp100_mc_intr,
	.intr_unarm = tu102_mc_intr_unarm,
	.intr_rearm = tu102_mc_intr_rearm,
	.intr_mask = tu102_mc_intr_mask,
	.intr_stat = tu102_mc_intr_stat,
	.reset = gk104_mc_reset,
};

static int
tu102_mc_new_(const struct nvkm_mc_func *func, struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	struct tu102_mc *mc;

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
tu102_mc_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_mc **pmc)
{
	return tu102_mc_new_(&tu102_mc, device, type, inst, pmc);
}
