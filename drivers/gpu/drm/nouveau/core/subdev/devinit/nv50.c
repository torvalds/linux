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

#include <subdev/devinit.h>
#include <subdev/vga.h>

struct nv50_devinit_priv {
	struct nouveau_devinit base;
};

static int
nv50_devinit_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nv50_devinit_priv *priv;
	int ret;

	ret = nouveau_devinit_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	return 0;
}

static void
nv50_devinit_dtor(struct nouveau_object *object)
{
	struct nv50_devinit_priv *priv = (void *)object;
	nouveau_devinit_destroy(&priv->base);
}

static int
nv50_devinit_init(struct nouveau_object *object)
{
	struct nv50_devinit_priv *priv = (void *)object;

	if (!priv->base.post) {
		if (!nv_rdvgac(priv, 0, 0x00) &&
		    !nv_rdvgac(priv, 0, 0x1a)) {
			nv_info(priv, "adaptor not initialised\n");
			priv->base.post = true;
		}
	}

	return nouveau_devinit_init(&priv->base);
}

static int
nv50_devinit_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_devinit_priv *priv = (void *)object;
	return nouveau_devinit_fini(&priv->base, suspend);
}

struct nouveau_oclass
nv50_devinit_oclass = {
	.handle = NV_SUBDEV(DEVINIT, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_devinit_ctor,
		.dtor = nv50_devinit_dtor,
		.init = nv50_devinit_init,
		.fini = nv50_devinit_fini,
	},
};
