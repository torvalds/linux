/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "priv.h"

static int
nv10_gpio_sense(struct nouveau_gpio *gpio, int line)
{
	if (line < 2) {
		line = line * 16;
		line = nv_rd32(gpio, 0x600818) >> line;
		return !!(line & 0x0100);
	} else
	if (line < 10) {
		line = (line - 2) * 4;
		line = nv_rd32(gpio, 0x60081c) >> line;
		return !!(line & 0x04);
	} else
	if (line < 14) {
		line = (line - 10) * 4;
		line = nv_rd32(gpio, 0x600850) >> line;
		return !!(line & 0x04);
	}

	return -EINVAL;
}

static int
nv10_gpio_drive(struct nouveau_gpio *gpio, int line, int dir, int out)
{
	u32 reg, mask, data;

	if (line < 2) {
		line = line * 16;
		reg  = 0x600818;
		mask = 0x00000011;
		data = (dir << 4) | out;
	} else
	if (line < 10) {
		line = (line - 2) * 4;
		reg  = 0x60081c;
		mask = 0x00000003;
		data = (dir << 1) | out;
	} else
	if (line < 14) {
		line = (line - 10) * 4;
		reg  = 0x600850;
		mask = 0x00000003;
		data = (dir << 1) | out;
	} else {
		return -EINVAL;
	}

	nv_mask(gpio, reg, mask << line, data << line);
	return 0;
}

static void
nv10_gpio_intr_stat(struct nouveau_gpio *gpio, u32 *hi, u32 *lo)
{
	u32 intr = nv_rd32(gpio, 0x001104);
	u32 stat = nv_rd32(gpio, 0x001144) & intr;
	*lo = (stat & 0xffff0000) >> 16;
	*hi = (stat & 0x0000ffff);
	nv_wr32(gpio, 0x001104, intr);
}

static void
nv10_gpio_intr_mask(struct nouveau_gpio *gpio, u32 type, u32 mask, u32 data)
{
	u32 inte = nv_rd32(gpio, 0x001144);
	if (type & NVKM_GPIO_LO)
		inte = (inte & ~(mask << 16)) | (data << 16);
	if (type & NVKM_GPIO_HI)
		inte = (inte & ~mask) | data;
	nv_wr32(gpio, 0x001144, inte);
}

struct nouveau_oclass *
nv10_gpio_oclass = &(struct nouveau_gpio_impl) {
	.base.handle = NV_SUBDEV(GPIO, 0x10),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = _nouveau_gpio_ctor,
		.dtor = _nouveau_gpio_dtor,
		.init = _nouveau_gpio_init,
		.fini = _nouveau_gpio_fini,
	},
	.lines = 16,
	.intr_stat = nv10_gpio_intr_stat,
	.intr_mask = nv10_gpio_intr_mask,
	.drive = nv10_gpio_drive,
	.sense = nv10_gpio_sense,
}.base;
