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

void
gm107_ltc_cbc_clear(struct nvkm_ltc *ltc, u32 start, u32 limit)
{
	struct nvkm_device *device = ltc->subdev.device;
	nvkm_wr32(device, 0x17e270, start);
	nvkm_wr32(device, 0x17e274, limit);
	nvkm_mask(device, 0x17e26c, 0x00000000, 0x00000004);
}

void
gm107_ltc_cbc_wait(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	int c, s;
	for (c = 0; c < ltc->ltc_nr; c++) {
		for (s = 0; s < ltc->lts_nr; s++) {
			const u32 addr = 0x14046c + (c * 0x2000) + (s * 0x200);
			nvkm_wait_msec(device, 2000, addr,
				       0x00000004, 0x00000000);
		}
	}
}

void
gm107_ltc_zbc_clear_color(struct nvkm_ltc *ltc, int i, const u32 color[4])
{
	struct nvkm_device *device = ltc->subdev.device;
	nvkm_mask(device, 0x17e338, 0x0000000f, i);
	nvkm_wr32(device, 0x17e33c, color[0]);
	nvkm_wr32(device, 0x17e340, color[1]);
	nvkm_wr32(device, 0x17e344, color[2]);
	nvkm_wr32(device, 0x17e348, color[3]);
}

void
gm107_ltc_zbc_clear_depth(struct nvkm_ltc *ltc, int i, const u32 depth)
{
	struct nvkm_device *device = ltc->subdev.device;
	nvkm_mask(device, 0x17e338, 0x0000000f, i);
	nvkm_wr32(device, 0x17e34c, depth);
}

void
gm107_ltc_intr_lts(struct nvkm_ltc *ltc, int c, int s)
{
	struct nvkm_subdev *subdev = &ltc->subdev;
	struct nvkm_device *device = subdev->device;
	u32 base = 0x140400 + (c * 0x2000) + (s * 0x200);
	u32 intr = nvkm_rd32(device, base + 0x00c);
	u16 stat = intr & 0x0000ffff;
	char msg[128];

	if (stat) {
		nvkm_snprintbf(msg, sizeof(msg), gf100_ltc_lts_intr_name, stat);
		nvkm_error(subdev, "LTC%d_LTS%d: %08x [%s]\n", c, s, intr, msg);
	}

	nvkm_wr32(device, base + 0x00c, intr);
}

void
gm107_ltc_intr(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	u32 mask;

	mask = nvkm_rd32(device, 0x00017c);
	while (mask) {
		u32 s, c = __ffs(mask);
		for (s = 0; s < ltc->lts_nr; s++)
			gm107_ltc_intr_lts(ltc, c, s);
		mask &= ~(1 << c);
	}
}

static int
gm107_ltc_oneinit(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	const u32 parts = nvkm_rd32(device, 0x022438);
	const u32  mask = nvkm_rd32(device, 0x021c14);
	const u32 slice = nvkm_rd32(device, 0x17e280) >> 28;
	int i;

	for (i = 0; i < parts; i++) {
		if (!(mask & (1 << i)))
			ltc->ltc_nr++;
	}
	ltc->lts_nr = slice;

	return gf100_ltc_oneinit_tag_ram(ltc);
}

static void
gm107_ltc_init(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	u32 lpg128 = !(nvkm_rd32(device, 0x100c80) & 0x00000001);

	nvkm_wr32(device, 0x17e27c, ltc->ltc_nr);
	nvkm_wr32(device, 0x17e278, ltc->tag_base);
	nvkm_mask(device, 0x17e264, 0x00000002, lpg128 ? 0x00000002 : 0x00000000);
}

static const struct nvkm_ltc_func
gm107_ltc = {
	.oneinit = gm107_ltc_oneinit,
	.init = gm107_ltc_init,
	.intr = gm107_ltc_intr,
	.cbc_clear = gm107_ltc_cbc_clear,
	.cbc_wait = gm107_ltc_cbc_wait,
	.zbc = 16,
	.zbc_clear_color = gm107_ltc_zbc_clear_color,
	.zbc_clear_depth = gm107_ltc_zbc_clear_depth,
	.invalidate = gf100_ltc_invalidate,
	.flush = gf100_ltc_flush,
};

int
gm107_ltc_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_ltc **pltc)
{
	return nvkm_ltc_new_(&gm107_ltc, device, type, inst, pltc);
}
