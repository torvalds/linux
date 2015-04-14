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
#include "gf100.h"

static int
gk110_pm_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct gf100_pm_priv *priv;
	int ret;

	ret = nvkm_pm_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nvkm_perfdom_new(&priv->base, "pwr", 0, 0, 0, 0, gk104_pm_pwr);
	if (ret)
		return ret;

	nv_engine(priv)->cclass = &nvkm_pm_cclass;
	nv_engine(priv)->sclass =  nvkm_pm_sclass;
	return 0;
}

struct nvkm_oclass
gk110_pm_oclass = {
	.handle = NV_ENGINE(PM, 0xf0),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gk110_pm_ctor,
		.dtor = _nvkm_pm_dtor,
		.init = _nvkm_pm_init,
		.fini = gf100_pm_fini,
	},
};
