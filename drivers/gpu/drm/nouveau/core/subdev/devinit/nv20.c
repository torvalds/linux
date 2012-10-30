/*
 * Copyright (C) 2010 Francisco Jerez.
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

#include <subdev/devinit.h>
#include <subdev/vga.h>

#include "fbmem.h"

struct nv20_devinit_priv {
	struct nouveau_devinit base;
	u8 owner;
};

static void
nv20_devinit_meminit(struct nouveau_devinit *devinit)
{
	struct nv20_devinit_priv *priv = (void *)devinit;
	struct nouveau_device *device = nv_device(priv);
	uint32_t mask = (device->chipset >= 0x25 ? 0x300 : 0x900);
	uint32_t amount, off;
	struct io_mapping *fb;

	/* Map the framebuffer aperture */
	fb = fbmem_init(nv_device(priv)->pdev);
	if (!fb) {
		nv_error(priv, "failed to map fb\n");
		return;
	}

	nv_wr32(priv, NV10_PFB_REFCTRL, NV10_PFB_REFCTRL_VALID_1);

	/* Allow full addressing */
	nv_mask(priv, NV04_PFB_CFG0, 0, mask);

	amount = nv_rd32(priv, 0x10020c);
	for (off = amount; off > 0x2000000; off -= 0x2000000)
		fbmem_poke(fb, off - 4, off);

	amount = nv_rd32(priv, 0x10020c);
	if (amount != fbmem_peek(fb, amount - 4))
		/* IC missing - disable the upper half memory space. */
		nv_mask(priv, NV04_PFB_CFG0, mask, 0);

	fbmem_fini(fb);
}

static int
nv20_devinit_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nv20_devinit_priv *priv;
	int ret;

	ret = nouveau_devinit_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.meminit = nv20_devinit_meminit;
	return 0;
}

struct nouveau_oclass
nv20_devinit_oclass = {
	.handle = NV_SUBDEV(DEVINIT, 0x20),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv20_devinit_ctor,
		.dtor = nv04_devinit_dtor,
		.init = nv04_devinit_init,
		.fini = nv04_devinit_fini,
	},
};
