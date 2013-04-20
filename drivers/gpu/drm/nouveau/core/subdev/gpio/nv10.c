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

struct nv10_gpio_priv {
	struct nouveau_gpio base;
};

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
nv10_gpio_intr(struct nouveau_subdev *subdev)
{
	struct nv10_gpio_priv *priv = (void *)subdev;
	u32 intr = nv_rd32(priv, 0x001104);
	u32 hi = (intr & 0x0000ffff) >> 0;
	u32 lo = (intr & 0xffff0000) >> 16;
	int i;

	for (i = 0; (hi | lo) && i < 32; i++) {
		if ((hi | lo) & (1 << i))
			nouveau_event_trigger(priv->base.events, i);
	}

	nv_wr32(priv, 0x001104, intr);
}

static void
nv10_gpio_intr_enable(struct nouveau_event *event, int line)
{
	nv_wr32(event->priv, 0x001104, 0x00010001 << line);
	nv_mask(event->priv, 0x001144, 0x00010001 << line, 0x00010001 << line);
}

static void
nv10_gpio_intr_disable(struct nouveau_event *event, int line)
{
	nv_wr32(event->priv, 0x001104, 0x00010001 << line);
	nv_mask(event->priv, 0x001144, 0x00010001 << line, 0x00000000);
}

static int
nv10_gpio_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv10_gpio_priv *priv;
	int ret;

	ret = nouveau_gpio_create(parent, engine, oclass, 16, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.drive = nv10_gpio_drive;
	priv->base.sense = nv10_gpio_sense;
	priv->base.events->priv = priv;
	priv->base.events->enable = nv10_gpio_intr_enable;
	priv->base.events->disable = nv10_gpio_intr_disable;
	nv_subdev(priv)->intr = nv10_gpio_intr;
	return 0;
}

static void
nv10_gpio_dtor(struct nouveau_object *object)
{
	struct nv10_gpio_priv *priv = (void *)object;
	nouveau_gpio_destroy(&priv->base);
}

static int
nv10_gpio_init(struct nouveau_object *object)
{
	struct nv10_gpio_priv *priv = (void *)object;
	int ret;

	ret = nouveau_gpio_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x001144, 0x00000000);
	nv_wr32(priv, 0x001104, 0xffffffff);
	return 0;
}

static int
nv10_gpio_fini(struct nouveau_object *object, bool suspend)
{
	struct nv10_gpio_priv *priv = (void *)object;
	nv_wr32(priv, 0x001144, 0x00000000);
	return nouveau_gpio_fini(&priv->base, suspend);
}

struct nouveau_oclass
nv10_gpio_oclass = {
	.handle = NV_SUBDEV(GPIO, 0x10),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv10_gpio_ctor,
		.dtor = nv10_gpio_dtor,
		.init = nv10_gpio_init,
		.fini = nv10_gpio_fini,
	},
};
