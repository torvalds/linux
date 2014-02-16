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

#include <subdev/pwr.h>

#include "fuc/nva3.fuc.h"

struct nva3_pwr_priv {
	struct nouveau_pwr base;
};

static int
nva3_pwr_init(struct nouveau_object *object)
{
	struct nva3_pwr_priv *priv = (void *)object;
	nv_mask(priv, 0x022210, 0x00000001, 0x00000000);
	nv_mask(priv, 0x022210, 0x00000001, 0x00000001);
	return nouveau_pwr_init(&priv->base);
}

static int
nva3_pwr_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	      struct nouveau_oclass *oclass, void *data, u32 size,
	      struct nouveau_object **pobject)
{
	struct nva3_pwr_priv *priv;
	int ret;

	ret = nouveau_pwr_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.code.data = nva3_pwr_code;
	priv->base.code.size = sizeof(nva3_pwr_code);
	priv->base.data.data = nva3_pwr_data;
	priv->base.data.size = sizeof(nva3_pwr_data);
	return 0;
}

struct nouveau_oclass
nva3_pwr_oclass = {
	.handle = NV_SUBDEV(PWR, 0xa3),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nva3_pwr_ctor,
		.dtor = _nouveau_pwr_dtor,
		.init = nva3_pwr_init,
		.fini = _nouveau_pwr_fini,
	},
};
