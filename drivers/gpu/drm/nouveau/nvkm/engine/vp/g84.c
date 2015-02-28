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
 * Authors: Ben Skeggs, Ilia Mirkin
 */
#include <engine/vp.h>
#include <engine/xtensa.h>

#include <core/engctx.h>

/*******************************************************************************
 * VP object classes
 ******************************************************************************/

static struct nvkm_oclass
g84_vp_sclass[] = {
	{ 0x7476, &nvkm_object_ofuncs },
	{},
};

/*******************************************************************************
 * PVP context
 ******************************************************************************/

static struct nvkm_oclass
g84_vp_cclass = {
	.handle = NV_ENGCTX(VP, 0x84),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_xtensa_engctx_ctor,
		.dtor = _nvkm_engctx_dtor,
		.init = _nvkm_engctx_init,
		.fini = _nvkm_engctx_fini,
		.rd32 = _nvkm_engctx_rd32,
		.wr32 = _nvkm_engctx_wr32,
	},
};

/*******************************************************************************
 * PVP engine/subdev functions
 ******************************************************************************/

static int
g84_vp_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	    struct nvkm_oclass *oclass, void *data, u32 size,
	    struct nvkm_object **pobject)
{
	struct nvkm_xtensa *priv;
	int ret;

	ret = nvkm_xtensa_create(parent, engine, oclass, 0xf000, true,
				 "PVP", "vp", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x01020000;
	nv_engine(priv)->cclass = &g84_vp_cclass;
	nv_engine(priv)->sclass = g84_vp_sclass;
	priv->fifo_val = 0x111;
	priv->unkd28 = 0x9c544;
	return 0;
}

struct nvkm_oclass
g84_vp_oclass = {
	.handle = NV_ENGINE(VP, 0x84),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = g84_vp_ctor,
		.dtor = _nvkm_xtensa_dtor,
		.init = _nvkm_xtensa_init,
		.fini = _nvkm_xtensa_fini,
		.rd32 = _nvkm_xtensa_rd32,
		.wr32 = _nvkm_xtensa_wr32,
	},
};
