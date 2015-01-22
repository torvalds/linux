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
#include <engine/cipher.h>
#include <engine/fifo.h>

#include <core/client.h>
#include <core/engctx.h>
#include <core/enum.h>

struct g84_cipher_priv {
	struct nvkm_engine base;
};

/*******************************************************************************
 * Crypt object classes
 ******************************************************************************/

static int
g84_cipher_object_ctor(struct nvkm_object *parent,
		       struct nvkm_object *engine,
		       struct nvkm_oclass *oclass, void *data, u32 size,
		       struct nvkm_object **pobject)
{
	struct nvkm_gpuobj *obj;
	int ret;

	ret = nvkm_gpuobj_create(parent, engine, oclass, 0, parent,
				 16, 16, 0, &obj);
	*pobject = nv_object(obj);
	if (ret)
		return ret;

	nv_wo32(obj, 0x00, nv_mclass(obj));
	nv_wo32(obj, 0x04, 0x00000000);
	nv_wo32(obj, 0x08, 0x00000000);
	nv_wo32(obj, 0x0c, 0x00000000);
	return 0;
}

static struct nvkm_ofuncs
g84_cipher_ofuncs = {
	.ctor = g84_cipher_object_ctor,
	.dtor = _nvkm_gpuobj_dtor,
	.init = _nvkm_gpuobj_init,
	.fini = _nvkm_gpuobj_fini,
	.rd32 = _nvkm_gpuobj_rd32,
	.wr32 = _nvkm_gpuobj_wr32,
};

static struct nvkm_oclass
g84_cipher_sclass[] = {
	{ 0x74c1, &g84_cipher_ofuncs },
	{}
};

/*******************************************************************************
 * PCIPHER context
 ******************************************************************************/

static struct nvkm_oclass
g84_cipher_cclass = {
	.handle = NV_ENGCTX(CIPHER, 0x84),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_engctx_ctor,
		.dtor = _nvkm_engctx_dtor,
		.init = _nvkm_engctx_init,
		.fini = _nvkm_engctx_fini,
		.rd32 = _nvkm_engctx_rd32,
		.wr32 = _nvkm_engctx_wr32,
	},
};

/*******************************************************************************
 * PCIPHER engine/subdev functions
 ******************************************************************************/

static const struct nvkm_bitfield
g84_cipher_intr_mask[] = {
	{ 0x00000001, "INVALID_STATE" },
	{ 0x00000002, "ILLEGAL_MTHD" },
	{ 0x00000004, "ILLEGAL_CLASS" },
	{ 0x00000080, "QUERY" },
	{ 0x00000100, "FAULT" },
	{}
};

static void
g84_cipher_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_fifo *pfifo = nvkm_fifo(subdev);
	struct nvkm_engine *engine = nv_engine(subdev);
	struct nvkm_object *engctx;
	struct g84_cipher_priv *priv = (void *)subdev;
	u32 stat = nv_rd32(priv, 0x102130);
	u32 mthd = nv_rd32(priv, 0x102190);
	u32 data = nv_rd32(priv, 0x102194);
	u32 inst = nv_rd32(priv, 0x102188) & 0x7fffffff;
	int chid;

	engctx = nvkm_engctx_get(engine, inst);
	chid   = pfifo->chid(pfifo, engctx);

	if (stat) {
		nv_error(priv, "%s", "");
		nvkm_bitfield_print(g84_cipher_intr_mask, stat);
		pr_cont(" ch %d [0x%010llx %s] mthd 0x%04x data 0x%08x\n",
		       chid, (u64)inst << 12, nvkm_client_name(engctx),
		       mthd, data);
	}

	nv_wr32(priv, 0x102130, stat);
	nv_wr32(priv, 0x10200c, 0x10);

	nvkm_engctx_put(engctx);
}

static int
g84_cipher_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct g84_cipher_priv *priv;
	int ret;

	ret = nvkm_engine_create(parent, engine, oclass, true,
				 "PCIPHER", "cipher", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00004000;
	nv_subdev(priv)->intr = g84_cipher_intr;
	nv_engine(priv)->cclass = &g84_cipher_cclass;
	nv_engine(priv)->sclass = g84_cipher_sclass;
	return 0;
}

static int
g84_cipher_init(struct nvkm_object *object)
{
	struct g84_cipher_priv *priv = (void *)object;
	int ret;

	ret = nvkm_engine_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x102130, 0xffffffff);
	nv_wr32(priv, 0x102140, 0xffffffbf);
	nv_wr32(priv, 0x10200c, 0x00000010);
	return 0;
}

struct nvkm_oclass
g84_cipher_oclass = {
	.handle = NV_ENGINE(CIPHER, 0x84),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = g84_cipher_ctor,
		.dtor = _nvkm_engine_dtor,
		.init = g84_cipher_init,
		.fini = _nvkm_engine_fini,
	},
};
