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

static const struct nvkm_specdom
gt215_pm[] = {
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{ 0x20, (const struct nvkm_specsig[]) {
			{}
		}, &nv40_perfctr_func },
	{}
};

static int
gt215_pm_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **object)
{
	int ret = nv40_pm_ctor(parent, engine, oclass, data, size, object);
	if (ret == 0) {
		struct nv40_pm_priv *priv = (void *)*object;
		ret = nvkm_perfdom_new(&priv->base, "pwr", 0, 0, 0, 0,
				       gt215_pm_pwr);
		if (ret)
			return ret;

		priv->base.last = 3;
	}
	return ret;
}

struct nvkm_oclass *
gt215_pm_oclass = &(struct nv40_pm_oclass) {
	.base.handle = NV_ENGINE(PM, 0xa3),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gt215_pm_ctor,
		.dtor = _nvkm_pm_dtor,
		.init = _nvkm_pm_init,
		.fini = _nvkm_pm_fini,
	},
	.doms = gt215_pm,
}.base;
