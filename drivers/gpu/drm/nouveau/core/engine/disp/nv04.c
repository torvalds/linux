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

#include <engine/disp.h>

struct nv04_disp_priv {
	struct nouveau_disp base;
};

static struct nouveau_oclass
nv04_disp_sclass[] = {
	{},
};

static void
nv04_disp_intr_vblank(struct nv04_disp_priv *priv, int crtc)
{
	struct nouveau_disp *disp = &priv->base;
	if (disp->vblank.notify)
		disp->vblank.notify(disp->vblank.data, crtc);
}

static void
nv04_disp_intr(struct nouveau_subdev *subdev)
{
	struct nv04_disp_priv *priv = (void *)subdev;
	u32 crtc0 = nv_rd32(priv, 0x600100);
	u32 crtc1 = nv_rd32(priv, 0x602100);

	if (crtc0 & 0x00000001) {
		nv04_disp_intr_vblank(priv, 0);
		nv_wr32(priv, 0x600100, 0x00000001);
	}

	if (crtc1 & 0x00000001) {
		nv04_disp_intr_vblank(priv, 1);
		nv_wr32(priv, 0x602100, 0x00000001);
	}
}

static int
nv04_disp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nv04_disp_priv *priv;
	int ret;

	ret = nouveau_disp_create(parent, engine, oclass, "DISPLAY",
				  "display", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_engine(priv)->sclass = nv04_disp_sclass;
	nv_subdev(priv)->intr = nv04_disp_intr;
	return 0;
}

struct nouveau_oclass
nv04_disp_oclass = {
	.handle = NV_ENGINE(DISP, 0x04),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_disp_ctor,
		.dtor = _nouveau_disp_dtor,
		.init = _nouveau_disp_init,
		.fini = _nouveau_disp_fini,
	},
};
