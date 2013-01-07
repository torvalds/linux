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

struct nvd0_gpio_priv {
	struct nouveau_gpio base;
};

static void
nvd0_gpio_reset(struct nouveau_gpio *gpio, u8 match)
{
	struct nouveau_bios *bios = nouveau_bios(gpio);
	struct nvd0_gpio_priv *priv = (void *)gpio;
	u8 ver, len;
	u16 entry;
	int ent = -1;

	while ((entry = dcb_gpio_entry(bios, 0, ++ent, &ver, &len))) {
		u32 data = nv_ro32(bios, entry);
		u8  line =   (data & 0x0000003f);
		u8  defs = !!(data & 0x00000080);
		u8  func =   (data & 0x0000ff00) >> 8;
		u8  unk0 =   (data & 0x00ff0000) >> 16;
		u8  unk1 =   (data & 0x1f000000) >> 24;

		if ( func  == DCB_GPIO_UNUSED ||
		    (match != DCB_GPIO_UNUSED && match != func))
			continue;

		gpio->set(gpio, 0, func, line, defs);

		nv_mask(priv, 0x00d610 + (line * 4), 0xff, unk0);
		if (unk1--)
			nv_mask(priv, 0x00d740 + (unk1 * 4), 0xff, line);
	}
}

static int
nvd0_gpio_drive(struct nouveau_gpio *gpio, int line, int dir, int out)
{
	u32 data = ((dir ^ 1) << 13) | (out << 12);
	nv_mask(gpio, 0x00d610 + (line * 4), 0x00003000, data);
	nv_mask(gpio, 0x00d604, 0x00000001, 0x00000001); /* update? */
	return 0;
}

static int
nvd0_gpio_sense(struct nouveau_gpio *gpio, int line)
{
	return !!(nv_rd32(gpio, 0x00d610 + (line * 4)) & 0x00004000);
}

static int
nvd0_gpio_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nvd0_gpio_priv *priv;
	int ret;

	ret = nouveau_gpio_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.reset = nvd0_gpio_reset;
	priv->base.drive = nvd0_gpio_drive;
	priv->base.sense = nvd0_gpio_sense;
	priv->base.irq_enable = nv50_gpio_irq_enable;
	nv_subdev(priv)->intr = nv50_gpio_intr;
	return 0;
}

struct nouveau_oclass
nvd0_gpio_oclass = {
	.handle = NV_SUBDEV(GPIO, 0xd0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvd0_gpio_ctor,
		.dtor = nv50_gpio_dtor,
		.init = nv50_gpio_init,
		.fini = nv50_gpio_fini,
	},
};
