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

#include "priv.h"

void
nv50_gpio_reset(struct nouveau_gpio *gpio, u8 match)
{
	struct nouveau_bios *bios = nouveau_bios(gpio);
	u8 ver, len;
	u16 entry;
	int ent = -1;

	while ((entry = dcb_gpio_entry(bios, 0, ++ent, &ver, &len))) {
		static const u32 regs[] = { 0xe100, 0xe28c };
		u32 data = nv_ro32(bios, entry);
		u8  line =   (data & 0x0000001f);
		u8  func =   (data & 0x0000ff00) >> 8;
		u8  defs = !!(data & 0x01000000);
		u8  unk0 = !!(data & 0x02000000);
		u8  unk1 = !!(data & 0x04000000);
		u32 val = (unk1 << 16) | unk0;
		u32 reg = regs[line >> 4];
		u32 lsh = line & 0x0f;

		if ( func  == DCB_GPIO_UNUSED ||
		    (match != DCB_GPIO_UNUSED && match != func))
			continue;

		gpio->set(gpio, 0, func, line, defs);

		nv_mask(gpio, reg, 0x00010001 << lsh, val << lsh);
	}
}

int
nv50_gpio_location(int line, u32 *reg, u32 *shift)
{
	const u32 nv50_gpio_reg[4] = { 0xe104, 0xe108, 0xe280, 0xe284 };

	if (line >= 32)
		return -EINVAL;

	*reg = nv50_gpio_reg[line >> 3];
	*shift = (line & 7) << 2;
	return 0;
}

int
nv50_gpio_drive(struct nouveau_gpio *gpio, int line, int dir, int out)
{
	u32 reg, shift;

	if (nv50_gpio_location(line, &reg, &shift))
		return -EINVAL;

	nv_mask(gpio, reg, 3 << shift, (((dir ^ 1) << 1) | out) << shift);
	return 0;
}

int
nv50_gpio_sense(struct nouveau_gpio *gpio, int line)
{
	u32 reg, shift;

	if (nv50_gpio_location(line, &reg, &shift))
		return -EINVAL;

	return !!(nv_rd32(gpio, reg) & (4 << shift));
}

static void
nv50_gpio_intr_stat(struct nouveau_gpio *gpio, u32 *hi, u32 *lo)
{
	u32 intr = nv_rd32(gpio, 0x00e054);
	u32 stat = nv_rd32(gpio, 0x00e050) & intr;
	*lo = (stat & 0xffff0000) >> 16;
	*hi = (stat & 0x0000ffff);
	nv_wr32(gpio, 0x00e054, intr);
}

static void
nv50_gpio_intr_mask(struct nouveau_gpio *gpio, u32 type, u32 mask, u32 data)
{
	u32 inte = nv_rd32(gpio, 0x00e050);
	if (type & NVKM_GPIO_LO)
		inte = (inte & ~(mask << 16)) | (data << 16);
	if (type & NVKM_GPIO_HI)
		inte = (inte & ~mask) | data;
	nv_wr32(gpio, 0x00e050, inte);
}

struct nouveau_oclass *
nv50_gpio_oclass = &(struct nouveau_gpio_impl) {
	.base.handle = NV_SUBDEV(GPIO, 0x50),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = _nouveau_gpio_ctor,
		.dtor = _nouveau_gpio_dtor,
		.init = _nouveau_gpio_init,
		.fini = _nouveau_gpio_fini,
	},
	.lines = 16,
	.intr_stat = nv50_gpio_intr_stat,
	.intr_mask = nv50_gpio_intr_mask,
	.drive = nv50_gpio_drive,
	.sense = nv50_gpio_sense,
	.reset = nv50_gpio_reset,
}.base;
