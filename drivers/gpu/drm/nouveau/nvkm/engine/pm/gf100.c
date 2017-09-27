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
#include "gf100.h"

const struct nvkm_specsrc
gf100_pbfb_sources[] = {
	{ 0x10f100, (const struct nvkm_specmux[]) {
			{ 0x1, 0, "unk0" },
			{ 0x3f, 4, "unk4" },
			{}
		}, "pbfb_broadcast_pm_unk100" },
	{}
};

const struct nvkm_specsrc
gf100_pmfb_sources[] = {
	{ 0x140028, (const struct nvkm_specmux[]) {
			{ 0x3fff, 0, "unk0" },
			{ 0x7, 16, "unk16" },
			{ 0x3, 24, "unk24" },
			{ 0x2, 29, "unk29" },
			{}
		}, "pmfb0_pm_unk28" },
	{}
};

static const struct nvkm_specsrc
gf100_l1_sources[] = {
	{ 0x5044a8, (const struct nvkm_specmux[]) {
			{ 0x3f, 0, "sel", true },
			{}
		}, "pgraph_gpc0_tpc0_l1_pm_mux" },
	{}
};

static const struct nvkm_specsrc
gf100_tex_sources[] = {
	{ 0x5042c0, (const struct nvkm_specmux[]) {
			{ 0xf, 0, "sel0", true },
			{ 0x7, 8, "sel1", true },
			{}
		}, "pgraph_gpc0_tpc0_tex_pm_mux_c_d" },
	{}
};

static const struct nvkm_specsrc
gf100_unk400_sources[] = {
	{ 0x50440c, (const struct nvkm_specmux[]) {
			{ 0x3f, 0, "sel", true },
			{}
		}, "pgraph_gpc0_tpc0_unk400_pm_mux" },
	{}
};

static const struct nvkm_specdom
gf100_pm_hub[] = {
	{}
};

const struct nvkm_specdom
gf100_pm_gpc[] = {
	{ 0xe0, (const struct nvkm_specsig[]) {
			{ 0x00, "gpc00_l1_00", gf100_l1_sources },
			{ 0x01, "gpc00_l1_01", gf100_l1_sources },
			{ 0x02, "gpc00_l1_02", gf100_l1_sources },
			{ 0x03, "gpc00_l1_03", gf100_l1_sources },
			{ 0x05, "gpc00_l1_04", gf100_l1_sources },
			{ 0x06, "gpc00_l1_05", gf100_l1_sources },
			{ 0x0a, "gpc00_tex_00", gf100_tex_sources },
			{ 0x0b, "gpc00_tex_01", gf100_tex_sources },
			{ 0x0c, "gpc00_tex_02", gf100_tex_sources },
			{ 0x0d, "gpc00_tex_03", gf100_tex_sources },
			{ 0x0e, "gpc00_tex_04", gf100_tex_sources },
			{ 0x0f, "gpc00_tex_05", gf100_tex_sources },
			{ 0x10, "gpc00_tex_06", gf100_tex_sources },
			{ 0x11, "gpc00_tex_07", gf100_tex_sources },
			{ 0x12, "gpc00_tex_08", gf100_tex_sources },
			{ 0x26, "gpc00_unk400_00", gf100_unk400_sources },
			{}
		}, &gf100_perfctr_func },
	{}
};

static const struct nvkm_specdom
gf100_pm_part[] = {
	{ 0xe0, (const struct nvkm_specsig[]) {
			{ 0x0f, "part00_pbfb_00", gf100_pbfb_sources },
			{ 0x10, "part00_pbfb_01", gf100_pbfb_sources },
			{ 0x21, "part00_pmfb_00", gf100_pmfb_sources },
			{ 0x04, "part00_pmfb_01", gf100_pmfb_sources },
			{ 0x00, "part00_pmfb_02", gf100_pmfb_sources },
			{ 0x02, "part00_pmfb_03", gf100_pmfb_sources },
			{ 0x01, "part00_pmfb_04", gf100_pmfb_sources },
			{ 0x2e, "part00_pmfb_05", gf100_pmfb_sources },
			{ 0x2f, "part00_pmfb_06", gf100_pmfb_sources },
			{ 0x1b, "part00_pmfb_07", gf100_pmfb_sources },
			{ 0x1c, "part00_pmfb_08", gf100_pmfb_sources },
			{ 0x1d, "part00_pmfb_09", gf100_pmfb_sources },
			{ 0x1e, "part00_pmfb_0a", gf100_pmfb_sources },
			{ 0x1f, "part00_pmfb_0b", gf100_pmfb_sources },
			{}
		}, &gf100_perfctr_func },
	{}
};

static void
gf100_perfctr_init(struct nvkm_pm *pm, struct nvkm_perfdom *dom,
		   struct nvkm_perfctr *ctr)
{
	struct nvkm_device *device = pm->engine.subdev.device;
	u32 log = ctr->logic_op;
	u32 src = 0x00000000;
	int i;

	for (i = 0; i < 4; i++)
		src |= ctr->signal[i] << (i * 8);

	nvkm_wr32(device, dom->addr + 0x09c, 0x00040002 | (dom->mode << 3));
	nvkm_wr32(device, dom->addr + 0x100, 0x00000000);
	nvkm_wr32(device, dom->addr + 0x040 + (ctr->slot * 0x08), src);
	nvkm_wr32(device, dom->addr + 0x044 + (ctr->slot * 0x08), log);
}

static void
gf100_perfctr_read(struct nvkm_pm *pm, struct nvkm_perfdom *dom,
		   struct nvkm_perfctr *ctr)
{
	struct nvkm_device *device = pm->engine.subdev.device;

	switch (ctr->slot) {
	case 0: ctr->ctr = nvkm_rd32(device, dom->addr + 0x08c); break;
	case 1: ctr->ctr = nvkm_rd32(device, dom->addr + 0x088); break;
	case 2: ctr->ctr = nvkm_rd32(device, dom->addr + 0x080); break;
	case 3: ctr->ctr = nvkm_rd32(device, dom->addr + 0x090); break;
	}
	dom->clk = nvkm_rd32(device, dom->addr + 0x070);
}

static void
gf100_perfctr_next(struct nvkm_pm *pm, struct nvkm_perfdom *dom)
{
	struct nvkm_device *device = pm->engine.subdev.device;
	nvkm_wr32(device, dom->addr + 0x06c, dom->signal_nr - 0x40 + 0x27);
	nvkm_wr32(device, dom->addr + 0x0ec, 0x00000011);
}

const struct nvkm_funcdom
gf100_perfctr_func = {
	.init = gf100_perfctr_init,
	.read = gf100_perfctr_read,
	.next = gf100_perfctr_next,
};

static void
gf100_pm_fini(struct nvkm_pm *pm)
{
	struct nvkm_device *device = pm->engine.subdev.device;
	nvkm_mask(device, 0x000200, 0x10000000, 0x00000000);
	nvkm_mask(device, 0x000200, 0x10000000, 0x10000000);
}

static const struct nvkm_pm_func
gf100_pm_ = {
	.fini = gf100_pm_fini,
};

int
gf100_pm_new_(const struct gf100_pm_func *func, struct nvkm_device *device,
	      int index, struct nvkm_pm **ppm)
{
	struct nvkm_pm *pm;
	u32 mask;
	int ret;

	if (!(pm = *ppm = kzalloc(sizeof(*pm), GFP_KERNEL)))
		return -ENOMEM;

	ret = nvkm_pm_ctor(&gf100_pm_, device, index, pm);
	if (ret)
		return ret;

	/* HUB */
	ret = nvkm_perfdom_new(pm, "hub", 0, 0x1b0000, 0, 0x200,
			       func->doms_hub);
	if (ret)
		return ret;

	/* GPC */
	mask  = (1 << nvkm_rd32(device, 0x022430)) - 1;
	mask &= ~nvkm_rd32(device, 0x022504);
	mask &= ~nvkm_rd32(device, 0x022584);

	ret = nvkm_perfdom_new(pm, "gpc", mask, 0x180000,
			       0x1000, 0x200, func->doms_gpc);
	if (ret)
		return ret;

	/* PART */
	mask  = (1 << nvkm_rd32(device, 0x022438)) - 1;
	mask &= ~nvkm_rd32(device, 0x022548);
	mask &= ~nvkm_rd32(device, 0x0225c8);

	ret = nvkm_perfdom_new(pm, "part", mask, 0x1a0000,
			       0x1000, 0x200, func->doms_part);
	if (ret)
		return ret;

	return 0;
}

static const struct gf100_pm_func
gf100_pm = {
	.doms_gpc = gf100_pm_gpc,
	.doms_hub = gf100_pm_hub,
	.doms_part = gf100_pm_part,
};

int
gf100_pm_new(struct nvkm_device *device, int index, struct nvkm_pm **ppm)
{
	return gf100_pm_new_(&gf100_pm, device, index, ppm);
}
