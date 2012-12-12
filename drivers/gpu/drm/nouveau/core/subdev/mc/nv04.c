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

#include <subdev/mc.h>

struct nv04_mc_priv {
	struct nouveau_mc base;
};

const struct nouveau_mc_intr
nv04_mc_intr[] = {
	{ 0x00000001, NVDEV_ENGINE_MPEG },	/* NV17- MPEG/ME */
	{ 0x00000100, NVDEV_ENGINE_FIFO },
	{ 0x00001000, NVDEV_ENGINE_GR },
	{ 0x00020000, NVDEV_ENGINE_VP },	/* NV40- */
	{ 0x00100000, NVDEV_SUBDEV_TIMER },
	{ 0x01000000, NVDEV_ENGINE_DISP },	/* NV04- PCRTC0 */
	{ 0x02000000, NVDEV_ENGINE_DISP },	/* NV11- PCRTC1 */
	{ 0x10000000, NVDEV_SUBDEV_GPIO },	/* PBUS */
	{ 0x80000000, NVDEV_ENGINE_SW },
	{}
};

static int
nv04_mc_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	     struct nouveau_oclass *oclass, void *data, u32 size,
	     struct nouveau_object **pobject)
{
	struct nv04_mc_priv *priv;
	int ret;

	ret = nouveau_mc_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->intr = nouveau_mc_intr;
	priv->base.intr_map = nv04_mc_intr;
	return 0;
}

int
nv04_mc_init(struct nouveau_object *object)
{
	struct nv04_mc_priv *priv = (void *)object;

	nv_wr32(priv, 0x000200, 0xffffffff); /* everything enabled */
	nv_wr32(priv, 0x001850, 0x00000001); /* disable rom access */

	return nouveau_mc_init(&priv->base);
}

struct nouveau_oclass
nv04_mc_oclass = {
	.handle = NV_SUBDEV(MC, 0x04),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_mc_ctor,
		.dtor = _nouveau_mc_dtor,
		.init = nv04_mc_init,
		.fini = _nouveau_mc_fini,
	},
};
