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

struct nve0_gpio_priv {
	struct nouveau_gpio base;
};

void
nve0_gpio_intr(struct nouveau_subdev *subdev)
{
	struct nve0_gpio_priv *priv = (void *)subdev;
	u32 intr0 = nv_rd32(priv, 0xdc00) & nv_rd32(priv, 0xdc08);
	u32 intr1 = nv_rd32(priv, 0xdc80) & nv_rd32(priv, 0xdc88);
	u32 hi = (intr0 & 0x0000ffff) | (intr1 << 16);
	u32 lo = (intr0 >> 16) | (intr1 & 0xffff0000);
	int i;

	for (i = 0; (hi | lo) && i < 32; i++) {
		if ((hi | lo) & (1 << i))
			nouveau_event_trigger(priv->base.events, i);
	}

	nv_wr32(priv, 0xdc00, intr0);
	nv_wr32(priv, 0xdc88, intr1);
}

void
nve0_gpio_intr_enable(struct nouveau_event *event, int line)
{
	const u32 addr = line < 16 ? 0xdc00 : 0xdc80;
	const u32 mask = 0x00010001 << (line & 0xf);
	nv_wr32(event->priv, addr + 0x08, mask);
	nv_mask(event->priv, addr + 0x00, mask, mask);
}

void
nve0_gpio_intr_disable(struct nouveau_event *event, int line)
{
	const u32 addr = line < 16 ? 0xdc00 : 0xdc80;
	const u32 mask = 0x00010001 << (line & 0xf);
	nv_wr32(event->priv, addr + 0x08, mask);
	nv_mask(event->priv, addr + 0x00, mask, 0x00000000);
}

int
nve0_gpio_fini(struct nouveau_object *object, bool suspend)
{
	struct nve0_gpio_priv *priv = (void *)object;
	nv_wr32(priv, 0xdc08, 0x00000000);
	nv_wr32(priv, 0xdc88, 0x00000000);
	return nouveau_gpio_fini(&priv->base, suspend);
}

int
nve0_gpio_init(struct nouveau_object *object)
{
	struct nve0_gpio_priv *priv = (void *)object;
	int ret;

	ret = nouveau_gpio_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0xdc00, 0xffffffff);
	nv_wr32(priv, 0xdc80, 0xffffffff);
	return 0;
}

void
nve0_gpio_dtor(struct nouveau_object *object)
{
	struct nve0_gpio_priv *priv = (void *)object;
	nouveau_gpio_destroy(&priv->base);
}

static int
nve0_gpio_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nve0_gpio_priv *priv;
	int ret;

	ret = nouveau_gpio_create(parent, engine, oclass, 32, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.reset = nvd0_gpio_reset;
	priv->base.drive = nvd0_gpio_drive;
	priv->base.sense = nvd0_gpio_sense;
	priv->base.events->priv = priv;
	priv->base.events->enable = nve0_gpio_intr_enable;
	priv->base.events->disable = nve0_gpio_intr_disable;
	nv_subdev(priv)->intr = nve0_gpio_intr;
	return 0;
}

struct nouveau_oclass
nve0_gpio_oclass = {
	.handle = NV_SUBDEV(GPIO, 0xe0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nve0_gpio_ctor,
		.dtor = nv50_gpio_dtor,
		.init = nve0_gpio_init,
		.fini = nve0_gpio_fini,
	},
};
