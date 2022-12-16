/*
 * Copyright 2021 Red Hat Inc.
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
#include "priv.h"

static void
ga102_ltc_zbc_clear_color(struct nvkm_ltc *ltc, int i, const u32 color[4])
{
	struct nvkm_device *device = ltc->subdev.device;

	nvkm_mask(device, 0x17e338, 0x0000001f, i);
	nvkm_wr32(device, 0x17e33c, color[0]);
	nvkm_wr32(device, 0x17e340, color[1]);
	nvkm_wr32(device, 0x17e344, color[2]);
	nvkm_wr32(device, 0x17e348, color[3]);
}

static const struct nvkm_ltc_func
ga102_ltc = {
	.oneinit = gp100_ltc_oneinit,
	.init = gp100_ltc_init,
	.intr = gp100_ltc_intr,
	.cbc_clear = gm107_ltc_cbc_clear,
	.cbc_wait = gm107_ltc_cbc_wait,
	.zbc_color = 31,
	.zbc_depth = 16,
	.zbc_clear_color = ga102_ltc_zbc_clear_color,
	.zbc_clear_depth = gm107_ltc_zbc_clear_depth,
	.zbc_clear_stencil = gp102_ltc_zbc_clear_stencil,
	.invalidate = gf100_ltc_invalidate,
	.flush = gf100_ltc_flush,
};

int
ga102_ltc_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_ltc **pltc)
{
	return nvkm_ltc_new_(&ga102_ltc, device, type, inst, pltc);
}
