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

static void
nve0_aux_stat(struct nouveau_i2c *i2c, u32 *hi, u32 *lo, u32 *rq, u32 *tx)
{
	u32 intr = nv_rd32(i2c, 0x00dc60);
	u32 stat = nv_rd32(i2c, 0x00dc68) & intr, i;
	for (i = 0, *hi = *lo = *rq = *tx = 0; i < 8; i++) {
		if ((stat & (1 << (i * 4)))) *hi |= 1 << i;
		if ((stat & (2 << (i * 4)))) *lo |= 1 << i;
		if ((stat & (4 << (i * 4)))) *rq |= 1 << i;
		if ((stat & (8 << (i * 4)))) *tx |= 1 << i;
	}
	nv_wr32(i2c, 0x00dc60, intr);
}

static void
nve0_aux_mask(struct nouveau_i2c *i2c, u32 type, u32 mask, u32 data)
{
	u32 temp = nv_rd32(i2c, 0x00dc68), i;
	for (i = 0; i < 8; i++) {
		if (mask & (1 << i)) {
			if (!(data & (1 << i))) {
				temp &= ~(type << (i * 4));
				continue;
			}
			temp |= type << (i * 4);
		}
	}
	nv_wr32(i2c, 0x00dc68, temp);
}

struct nouveau_oclass *
nve0_i2c_oclass = &(struct nouveau_i2c_impl) {
	.base.handle = NV_SUBDEV(I2C, 0xe0),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = _nouveau_i2c_ctor,
		.dtor = _nouveau_i2c_dtor,
		.init = _nouveau_i2c_init,
		.fini = _nouveau_i2c_fini,
	},
	.sclass = nvd0_i2c_sclass,
	.pad_x = &nv04_i2c_pad_oclass,
	.pad_s = &nv94_i2c_pad_oclass,
	.aux = 4,
	.aux_stat = nve0_aux_stat,
	.aux_mask = nve0_aux_mask,
}.base;
