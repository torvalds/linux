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

#include <subdev/gpio.h>

struct nv50_gpio_priv {
	struct nouveau_gpio base;
};

static void
nv50_gpio_reset(struct nouveau_gpio *gpio, u8 match)
{
	struct nouveau_bios *bios = nouveau_bios(gpio);
	struct nv50_gpio_priv *priv = (void *)gpio;
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
		u32 reg = regs[line >> 4]; line &= 0x0f;

		if ( func  == DCB_GPIO_UNUSED ||
		    (match != DCB_GPIO_UNUSED && match != func))
			continue;

		gpio->set(gpio, 0, func, line, defs);

		nv_mask(priv, reg, 0x00010001 << line, val << line);
	}
}

static int
nv50_gpio_location(int line, u32 *reg, u32 *shift)
{
	const u32 nv50_gpio_reg[4] = { 0xe104, 0xe108, 0xe280, 0xe284 };

	if (line >= 32)
		return -EINVAL;

	*reg = nv50_gpio_reg[line >> 3];
	*shift = (line & 7) << 2;
	return 0;
}

static int
nv50_gpio_drive(struct nouveau_gpio *gpio, int line, int dir, int out)
{
	u32 reg, shift;

	if (nv50_gpio_location(line, &reg, &shift))
		return -EINVAL;

	nv_mask(gpio, reg, 7 << shift, (((dir ^ 1) << 1) | out) << shift);
	return 0;
}

static int
nv50_gpio_sense(struct nouveau_gpio *gpio, int line)
{
	u32 reg, shift;

	if (nv50_gpio_location(line, &reg, &shift))
		return -EINVAL;

	return !!(nv_rd32(gpio, reg) & (4 << shift));
}

void
nv50_gpio_irq_enable(struct nouveau_gpio *gpio, int line, bool on)
{
	u32 reg  = line < 16 ? 0xe050 : 0xe070;
	u32 mask = 0x00010001 << (line & 0xf);

	nv_wr32(gpio, reg + 4, mask);
	nv_mask(gpio, reg + 0, mask, on ? mask : 0);
}

void
nv50_gpio_intr(struct nouveau_subdev *subdev)
{
	struct nv50_gpio_priv *priv = (void *)subdev;
	u32 intr0, intr1 = 0;
	u32 hi, lo;

	intr0 = nv_rd32(priv, 0xe054) & nv_rd32(priv, 0xe050);
	if (nv_device(priv)->chipset >= 0x90)
		intr1 = nv_rd32(priv, 0xe074) & nv_rd32(priv, 0xe070);

	hi = (intr0 & 0x0000ffff) | (intr1 << 16);
	lo = (intr0 >> 16) | (intr1 & 0xffff0000);
	priv->base.isr_run(&priv->base, 0, hi | lo);

	nv_wr32(priv, 0xe054, intr0);
	if (nv_device(priv)->chipset >= 0x90)
		nv_wr32(priv, 0xe074, intr1);
}

static int
nv50_gpio_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv50_gpio_priv *priv;
	int ret;

	ret = nouveau_gpio_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.reset = nv50_gpio_reset;
	priv->base.drive = nv50_gpio_drive;
	priv->base.sense = nv50_gpio_sense;
	priv->base.irq_enable = nv50_gpio_irq_enable;
	nv_subdev(priv)->intr = nv50_gpio_intr;
	return 0;
}

void
nv50_gpio_dtor(struct nouveau_object *object)
{
	struct nv50_gpio_priv *priv = (void *)object;
	nouveau_gpio_destroy(&priv->base);
}

int
nv50_gpio_init(struct nouveau_object *object)
{
	struct nv50_gpio_priv *priv = (void *)object;
	int ret;

	ret = nouveau_gpio_init(&priv->base);
	if (ret)
		return ret;

	/* disable, and ack any pending gpio interrupts */
	nv_wr32(priv, 0xe050, 0x00000000);
	nv_wr32(priv, 0xe054, 0xffffffff);
	if (nv_device(priv)->chipset >= 0x90) {
		nv_wr32(priv, 0xe070, 0x00000000);
		nv_wr32(priv, 0xe074, 0xffffffff);
	}

	return 0;
}

int
nv50_gpio_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_gpio_priv *priv = (void *)object;
	nv_wr32(priv, 0xe050, 0x00000000);
	if (nv_device(priv)->chipset >= 0x90)
		nv_wr32(priv, 0xe070, 0x00000000);
	return nouveau_gpio_fini(&priv->base, suspend);
}

struct nouveau_oclass
nv50_gpio_oclass = {
	.handle = NV_SUBDEV(GPIO, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_gpio_ctor,
		.dtor = nv50_gpio_dtor,
		.init = nv50_gpio_init,
		.fini = nv50_gpio_fini,
	},
};
