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

#include "nv40.h"

/*******************************************************************************
 * Perfmon object classes
 ******************************************************************************/

/*******************************************************************************
 * PPM context
 ******************************************************************************/

/*******************************************************************************
 * PPM engine/subdev functions
 ******************************************************************************/

static const struct nouveau_specdom
nva3_perfmon[] = {
	{ 0x20, (const struct nouveau_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nouveau_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nouveau_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nouveau_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nouveau_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nouveau_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nouveau_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nouveau_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{}
};

static int
nva3_perfmon_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **object)
{
	int ret = nv40_perfmon_ctor(parent, engine, oclass, data, size, object);
	if (ret == 0) {
		struct nv40_perfmon_priv *priv = (void *)*object;
		ret = nouveau_perfdom_new(&priv->base, "pwr", 0, 0, 0, 0,
					   nva3_perfmon_pwr);
		if (ret)
			return ret;

		priv->base.last = 3;
	}
	return ret;
}

struct nouveau_oclass *
nva3_perfmon_oclass = &(struct nv40_perfmon_oclass) {
	.base.handle = NV_ENGINE(PERFMON, 0xa3),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nva3_perfmon_ctor,
		.dtor = _nouveau_perfmon_dtor,
		.init = _nouveau_perfmon_init,
		.fini = _nouveau_perfmon_fini,
	},
	.doms = nva3_perfmon,
}.base;
