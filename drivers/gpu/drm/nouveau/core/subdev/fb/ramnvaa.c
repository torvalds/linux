/*
 * Copyright 2013 Red Hat Inc.
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

struct nvaa_ram_priv {
	struct nouveau_ram base;
};

static int
nvaa_ram_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	      struct nouveau_oclass *oclass, void *data, u32 datasize,
	      struct nouveau_object **pobject)
{
	const u32 rsvd_head = ( 256 * 1024) >> 12; /* vga memory */
	const u32 rsvd_tail = (1024 * 1024) >> 12; /* vbios etc */
	struct nouveau_fb *pfb = nouveau_fb(parent);
	struct nvaa_ram_priv *priv;
	int ret;

	ret = nouveau_ram_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.size = nv_rd32(pfb, 0x10020c);
	priv->base.size = (priv->base.size & 0xffffff00) | ((priv->base.size & 0x000000ff) << 32);

	ret = nouveau_mm_init(&pfb->vram, rsvd_head, (priv->base.size >> 12) -
			      (rsvd_head + rsvd_tail), 1);
	if (ret)
		return ret;

	priv->base.type   = NV_MEM_TYPE_STOLEN;
	priv->base.stolen = (u64)nv_rd32(pfb, 0x100e10) << 12;
	priv->base.get = nv50_ram_get;
	priv->base.put = nv50_ram_put;
	return 0;
}

struct nouveau_oclass
nvaa_ram_oclass = {
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvaa_ram_ctor,
		.dtor = _nouveau_ram_dtor,
		.init = _nouveau_ram_init,
		.fini = _nouveau_ram_fini,
	},
};
