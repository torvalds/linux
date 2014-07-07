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

#include "nvc0.h"

/*******************************************************************************
 * Perfmon object classes
 ******************************************************************************/

/*******************************************************************************
 * PPM context
 ******************************************************************************/

/*******************************************************************************
 * PPM engine/subdev functions
 ******************************************************************************/

static int
nvf0_perfmon_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nvc0_perfmon_priv *priv;
	int ret;

	ret = nouveau_perfmon_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nouveau_perfdom_new(&priv->base, "pwr", 0, 0, 0, 0,
				   nve0_perfmon_pwr);
	if (ret)
		return ret;

	nv_engine(priv)->cclass = &nouveau_perfmon_cclass;
	nv_engine(priv)->sclass =  nouveau_perfmon_sclass;
	return 0;
}

struct nouveau_oclass
nvf0_perfmon_oclass = {
	.handle = NV_ENGINE(PERFMON, 0xf0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvf0_perfmon_ctor,
		.dtor = _nouveau_perfmon_dtor,
		.init = _nouveau_perfmon_init,
		.fini = nvc0_perfmon_fini,
	},
};
