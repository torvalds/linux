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

static void
gp100_ltc_intr(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	u32 mask;

	mask = nvkm_rd32(device, 0x0001c0);
	while (mask) {
		u32 s, c = __ffs(mask);
		for (s = 0; s < ltc->lts_nr; s++)
			gm107_ltc_intr_lts(ltc, c, s);
		mask &= ~(1 << c);
	}
}

static int
gp100_ltc_oneinit(struct nvkm_ltc *ltc)
{
	struct nvkm_device *device = ltc->subdev.device;
	ltc->ltc_nr = nvkm_rd32(device, 0x12006c);
	ltc->lts_nr = nvkm_rd32(device, 0x17e280) >> 28;
	/*XXX: tagram allocation - TBD */
	return nvkm_mm_init(&ltc->tags, 0, 0, 1);
}

static void
gp100_ltc_init(struct nvkm_ltc *ltc)
{
	/*XXX: PMU LS call to setup tagram address */
}

static const struct nvkm_ltc_func
gp100_ltc = {
	.oneinit = gp100_ltc_oneinit,
	.init = gp100_ltc_init,
	.intr = gp100_ltc_intr,
	.cbc_clear = gm107_ltc_cbc_clear,
	.cbc_wait = gm107_ltc_cbc_wait,
	.zbc = 16,
	.zbc_clear_color = gm107_ltc_zbc_clear_color,
	.zbc_clear_depth = gm107_ltc_zbc_clear_depth,
	.invalidate = gf100_ltc_invalidate,
	.flush = gf100_ltc_flush,
};

int
gp100_ltc_new(struct nvkm_device *device, int index, struct nvkm_ltc **pltc)
{
	return nvkm_ltc_new_(&gp100_ltc, device, index, pltc);
}
