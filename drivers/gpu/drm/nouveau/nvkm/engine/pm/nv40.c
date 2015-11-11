/*
 * Copyright 2013 Red Hat Inc.
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
#include "nv40.h"

static void
nv40_perfctr_init(struct nvkm_pm *pm, struct nvkm_perfdom *dom,
		  struct nvkm_perfctr *ctr)
{
	struct nvkm_device *device = pm->engine.subdev.device;
	u32 log = ctr->logic_op;
	u32 src = 0x00000000;
	int i;

	for (i = 0; i < 4; i++)
		src |= ctr->signal[i] << (i * 8);

	nvkm_wr32(device, 0x00a7c0 + dom->addr, 0x00000001 | (dom->mode << 4));
	nvkm_wr32(device, 0x00a400 + dom->addr + (ctr->slot * 0x40), src);
	nvkm_wr32(device, 0x00a420 + dom->addr + (ctr->slot * 0x40), log);
}

static void
nv40_perfctr_read(struct nvkm_pm *pm, struct nvkm_perfdom *dom,
		  struct nvkm_perfctr *ctr)
{
	struct nvkm_device *device = pm->engine.subdev.device;

	switch (ctr->slot) {
	case 0: ctr->ctr = nvkm_rd32(device, 0x00a700 + dom->addr); break;
	case 1: ctr->ctr = nvkm_rd32(device, 0x00a6c0 + dom->addr); break;
	case 2: ctr->ctr = nvkm_rd32(device, 0x00a680 + dom->addr); break;
	case 3: ctr->ctr = nvkm_rd32(device, 0x00a740 + dom->addr); break;
	}
	dom->clk = nvkm_rd32(device, 0x00a600 + dom->addr);
}

static void
nv40_perfctr_next(struct nvkm_pm *pm, struct nvkm_perfdom *dom)
{
	struct nvkm_device *device = pm->engine.subdev.device;
	if (pm->sequence != pm->sequence) {
		nvkm_wr32(device, 0x400084, 0x00000020);
		pm->sequence = pm->sequence;
	}
}

const struct nvkm_funcdom
nv40_perfctr_func = {
	.init = nv40_perfctr_init,
	.read = nv40_perfctr_read,
	.next = nv40_perfctr_next,
};

static const struct nvkm_pm_func
nv40_pm_ = {
};

int
nv40_pm_new_(const struct nvkm_specdom *doms, struct nvkm_device *device,
	     int index, struct nvkm_pm **ppm)
{
	struct nv40_pm *pm;
	int ret;

	if (!(pm = kzalloc(sizeof(*pm), GFP_KERNEL)))
		return -ENOMEM;
	*ppm = &pm->base;

	ret = nvkm_pm_ctor(&nv40_pm_, device, index, &pm->base);
	if (ret)
		return ret;

	return nvkm_perfdom_new(&pm->base, "pc", 0, 0, 0, 4, doms);
}

static const struct nvkm_specdom
nv40_pm[] = {
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{}
};

int
nv40_pm_new(struct nvkm_device *device, int index, struct nvkm_pm **ppm)
{
	return nv40_pm_new_(nv40_pm, device, index, ppm);
}
