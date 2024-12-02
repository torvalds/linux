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
#include "nv50.h"

#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <subdev/mmu.h>
#include <subdev/timer.h>

static void
nv50_bar_flush(struct nvkm_bar *base)
{
	struct nv50_bar *bar = nv50_bar(base);
	struct nvkm_device *device = bar->base.subdev.device;
	unsigned long flags;
	spin_lock_irqsave(&bar->base.lock, flags);
	nvkm_wr32(device, 0x00330c, 0x00000001);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x00330c) & 0x00000002))
			break;
	);
	spin_unlock_irqrestore(&bar->base.lock, flags);
}

struct nvkm_vmm *
nv50_bar_bar1_vmm(struct nvkm_bar *base)
{
	return nv50_bar(base)->bar1_vmm;
}

void
nv50_bar_bar1_wait(struct nvkm_bar *base)
{
	nvkm_bar_flush(base);
}

void
nv50_bar_bar1_fini(struct nvkm_bar *bar)
{
	nvkm_wr32(bar->subdev.device, 0x001708, 0x00000000);
}

void
nv50_bar_bar1_init(struct nvkm_bar *base)
{
	struct nvkm_device *device = base->subdev.device;
	struct nv50_bar *bar = nv50_bar(base);
	nvkm_wr32(device, 0x001708, 0x80000000 | bar->bar1->node->offset >> 4);
}

struct nvkm_vmm *
nv50_bar_bar2_vmm(struct nvkm_bar *base)
{
	return nv50_bar(base)->bar2_vmm;
}

void
nv50_bar_bar2_fini(struct nvkm_bar *bar)
{
	nvkm_wr32(bar->subdev.device, 0x00170c, 0x00000000);
}

void
nv50_bar_bar2_init(struct nvkm_bar *base)
{
	struct nvkm_device *device = base->subdev.device;
	struct nv50_bar *bar = nv50_bar(base);
	nvkm_wr32(device, 0x001704, 0x00000000 | bar->mem->addr >> 12);
	nvkm_wr32(device, 0x001704, 0x40000000 | bar->mem->addr >> 12);
	nvkm_wr32(device, 0x00170c, 0x80000000 | bar->bar2->node->offset >> 4);
}

void
nv50_bar_init(struct nvkm_bar *base)
{
	struct nv50_bar *bar = nv50_bar(base);
	struct nvkm_device *device = bar->base.subdev.device;
	int i;

	for (i = 0; i < 8; i++)
		nvkm_wr32(device, 0x001900 + (i * 4), 0x00000000);
}

int
nv50_bar_oneinit(struct nvkm_bar *base)
{
	struct nv50_bar *bar = nv50_bar(base);
	struct nvkm_device *device = bar->base.subdev.device;
	static struct lock_class_key bar1_lock;
	static struct lock_class_key bar2_lock;
	u64 start, limit, size;
	int ret;

	ret = nvkm_gpuobj_new(device, 0x20000, 0, false, NULL, &bar->mem);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, bar->pgd_addr, 0, false, bar->mem,
			      &bar->pad);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x4000, 0, false, bar->mem, &bar->pgd);
	if (ret)
		return ret;

	/* BAR2 */
	start = 0x0100000000ULL;
	size = device->func->resource_size(device, 3);
	if (!size)
		return -ENOMEM;
	limit = start + size;

	ret = nvkm_vmm_new(device, start, limit-- - start, NULL, 0,
			   &bar2_lock, "bar2", &bar->bar2_vmm);
	if (ret)
		return ret;

	atomic_inc(&bar->bar2_vmm->engref[NVKM_SUBDEV_BAR]);
	bar->bar2_vmm->debug = bar->base.subdev.debug;

	ret = nvkm_vmm_boot(bar->bar2_vmm);
	if (ret)
		return ret;

	ret = nvkm_vmm_join(bar->bar2_vmm, bar->mem->memory);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 24, 16, false, bar->mem, &bar->bar2);
	if (ret)
		return ret;

	nvkm_kmap(bar->bar2);
	nvkm_wo32(bar->bar2, 0x00, 0x7fc00000);
	nvkm_wo32(bar->bar2, 0x04, lower_32_bits(limit));
	nvkm_wo32(bar->bar2, 0x08, lower_32_bits(start));
	nvkm_wo32(bar->bar2, 0x0c, upper_32_bits(limit) << 24 |
				   upper_32_bits(start));
	nvkm_wo32(bar->bar2, 0x10, 0x00000000);
	nvkm_wo32(bar->bar2, 0x14, 0x00000000);
	nvkm_done(bar->bar2);

	bar->base.subdev.oneinit = true;
	nvkm_bar_bar2_init(device);

	/* BAR1 */
	start = 0x0000000000ULL;
	size = device->func->resource_size(device, 1);
	if (!size)
		return -ENOMEM;
	limit = start + size;

	ret = nvkm_vmm_new(device, start, limit-- - start, NULL, 0,
			   &bar1_lock, "bar1", &bar->bar1_vmm);
	if (ret)
		return ret;

	atomic_inc(&bar->bar1_vmm->engref[NVKM_SUBDEV_BAR]);
	bar->bar1_vmm->debug = bar->base.subdev.debug;

	ret = nvkm_vmm_join(bar->bar1_vmm, bar->mem->memory);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 24, 16, false, bar->mem, &bar->bar1);
	if (ret)
		return ret;

	nvkm_kmap(bar->bar1);
	nvkm_wo32(bar->bar1, 0x00, 0x7fc00000);
	nvkm_wo32(bar->bar1, 0x04, lower_32_bits(limit));
	nvkm_wo32(bar->bar1, 0x08, lower_32_bits(start));
	nvkm_wo32(bar->bar1, 0x0c, upper_32_bits(limit) << 24 |
				   upper_32_bits(start));
	nvkm_wo32(bar->bar1, 0x10, 0x00000000);
	nvkm_wo32(bar->bar1, 0x14, 0x00000000);
	nvkm_done(bar->bar1);
	return 0;
}

void *
nv50_bar_dtor(struct nvkm_bar *base)
{
	struct nv50_bar *bar = nv50_bar(base);
	if (bar->mem) {
		nvkm_gpuobj_del(&bar->bar1);
		nvkm_vmm_part(bar->bar1_vmm, bar->mem->memory);
		nvkm_vmm_unref(&bar->bar1_vmm);
		nvkm_gpuobj_del(&bar->bar2);
		nvkm_vmm_part(bar->bar2_vmm, bar->mem->memory);
		nvkm_vmm_unref(&bar->bar2_vmm);
		nvkm_gpuobj_del(&bar->pgd);
		nvkm_gpuobj_del(&bar->pad);
		nvkm_gpuobj_del(&bar->mem);
	}
	return bar;
}

int
nv50_bar_new_(const struct nvkm_bar_func *func, struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, u32 pgd_addr, struct nvkm_bar **pbar)
{
	struct nv50_bar *bar;
	if (!(bar = kzalloc(sizeof(*bar), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_bar_ctor(func, device, type, inst, &bar->base);
	bar->pgd_addr = pgd_addr;
	*pbar = &bar->base;
	return 0;
}

static const struct nvkm_bar_func
nv50_bar_func = {
	.dtor = nv50_bar_dtor,
	.oneinit = nv50_bar_oneinit,
	.init = nv50_bar_init,
	.bar1.init = nv50_bar_bar1_init,
	.bar1.fini = nv50_bar_bar1_fini,
	.bar1.wait = nv50_bar_bar1_wait,
	.bar1.vmm = nv50_bar_bar1_vmm,
	.bar2.init = nv50_bar_bar2_init,
	.bar2.fini = nv50_bar_bar2_fini,
	.bar2.wait = nv50_bar_bar1_wait,
	.bar2.vmm = nv50_bar_bar2_vmm,
	.flush = nv50_bar_flush,
};

int
nv50_bar_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	     struct nvkm_bar **pbar)
{
	return nv50_bar_new_(&nv50_bar_func, device, type, inst, 0x1400, pbar);
}
