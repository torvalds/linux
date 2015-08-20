/*
 * Copyright 2014 Red Hat Inc.
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

#include <subdev/fb.h>
#include <subdev/timer.h>

static void
gm107_ltc_cbc_clear(struct nvkm_ltc_priv *ltc, u32 start, u32 limit)
{
	struct nvkm_device *device = ltc->base.subdev.device;
	nvkm_wr32(device, 0x17e270, start);
	nvkm_wr32(device, 0x17e274, limit);
	nvkm_wr32(device, 0x17e26c, 0x00000004);
}

static void
gm107_ltc_cbc_wait(struct nvkm_ltc_priv *ltc)
{
	struct nvkm_device *device = ltc->base.subdev.device;
	int c, s;
	for (c = 0; c < ltc->ltc_nr; c++) {
		for (s = 0; s < ltc->lts_nr; s++) {
			const u32 addr = 0x14046c + (c * 0x2000) + (s * 0x200);
			nvkm_msec(device, 2000,
				if (!nvkm_rd32(device, addr))
					break;
			);
		}
	}
}

static void
gm107_ltc_zbc_clear_color(struct nvkm_ltc_priv *ltc, int i, const u32 color[4])
{
	struct nvkm_device *device = ltc->base.subdev.device;
	nvkm_mask(device, 0x17e338, 0x0000000f, i);
	nvkm_wr32(device, 0x17e33c, color[0]);
	nvkm_wr32(device, 0x17e340, color[1]);
	nvkm_wr32(device, 0x17e344, color[2]);
	nvkm_wr32(device, 0x17e348, color[3]);
}

static void
gm107_ltc_zbc_clear_depth(struct nvkm_ltc_priv *ltc, int i, const u32 depth)
{
	struct nvkm_device *device = ltc->base.subdev.device;
	nvkm_mask(device, 0x17e338, 0x0000000f, i);
	nvkm_wr32(device, 0x17e34c, depth);
}

static void
gm107_ltc_lts_isr(struct nvkm_ltc_priv *ltc, int c, int s)
{
	struct nvkm_subdev *subdev = &ltc->base.subdev;
	struct nvkm_device *device = subdev->device;
	u32 base = 0x140000 + (c * 0x2000) + (s * 0x400);
	u32 stat = nvkm_rd32(device, base + 0x00c);

	if (stat) {
		nvkm_error(subdev, "LTC%d_LTS%d: %08x\n", c, s, stat);
		nvkm_wr32(device, base + 0x00c, stat);
	}
}

static void
gm107_ltc_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_ltc_priv *ltc = (void *)subdev;
	struct nvkm_device *device = ltc->base.subdev.device;
	u32 mask;

	mask = nvkm_rd32(device, 0x00017c);
	while (mask) {
		u32 s, c = __ffs(mask);
		for (s = 0; s < ltc->lts_nr; s++)
			gm107_ltc_lts_isr(ltc, c, s);
		mask &= ~(1 << c);
	}
}

static int
gm107_ltc_init(struct nvkm_object *object)
{
	struct nvkm_ltc_priv *ltc = (void *)object;
	struct nvkm_device *device = ltc->base.subdev.device;
	u32 lpg128 = !(nvkm_rd32(device, 0x100c80) & 0x00000001);
	int ret;

	ret = nvkm_ltc_init(ltc);
	if (ret)
		return ret;

	nvkm_wr32(device, 0x17e27c, ltc->ltc_nr);
	nvkm_wr32(device, 0x17e278, ltc->tag_base);
	nvkm_mask(device, 0x17e264, 0x00000002, lpg128 ? 0x00000002 : 0x00000000);
	return 0;
}

static int
gm107_ltc_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_device *device = (void *)parent;
	struct nvkm_fb *fb = device->fb;
	struct nvkm_ltc_priv *ltc;
	u32 parts, mask;
	int ret, i;

	ret = nvkm_ltc_create(parent, engine, oclass, &ltc);
	*pobject = nv_object(ltc);
	if (ret)
		return ret;

	parts = nvkm_rd32(device, 0x022438);
	mask = nvkm_rd32(device, 0x021c14);
	for (i = 0; i < parts; i++) {
		if (!(mask & (1 << i)))
			ltc->ltc_nr++;
	}
	ltc->lts_nr = nvkm_rd32(device, 0x17e280) >> 28;

	ret = gf100_ltc_init_tag_ram(fb, ltc);
	if (ret)
		return ret;

	return 0;
}

struct nvkm_oclass *
gm107_ltc_oclass = &(struct nvkm_ltc_impl) {
	.base.handle = NV_SUBDEV(LTC, 0xff),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gm107_ltc_ctor,
		.dtor = gf100_ltc_dtor,
		.init = gm107_ltc_init,
		.fini = _nvkm_ltc_fini,
	},
	.intr = gm107_ltc_intr,
	.cbc_clear = gm107_ltc_cbc_clear,
	.cbc_wait = gm107_ltc_cbc_wait,
	.zbc = 16,
	.zbc_clear_color = gm107_ltc_zbc_clear_color,
	.zbc_clear_depth = gm107_ltc_zbc_clear_depth,
}.base;
