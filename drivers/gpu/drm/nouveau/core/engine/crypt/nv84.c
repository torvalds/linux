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

#include <core/os.h>
#include <core/enum.h>
#include <core/class.h>
#include <core/engctx.h>
#include <core/gpuobj.h>

#include <subdev/fb.h>

#include <engine/fifo.h>
#include <engine/crypt.h>

struct nv84_crypt_priv {
	struct nouveau_crypt base;
};

struct nv84_crypt_chan {
	struct nouveau_crypt_chan base;
};

/*******************************************************************************
 * Crypt object classes
 ******************************************************************************/

static int
nv84_crypt_object_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nouveau_gpuobj *obj;
	int ret;

	ret = nouveau_gpuobj_create(parent, engine, oclass, 0, parent,
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

static struct nouveau_ofuncs
nv84_crypt_ofuncs = {
	.ctor = nv84_crypt_object_ctor,
	.dtor = _nouveau_gpuobj_dtor,
	.init = _nouveau_gpuobj_init,
	.fini = _nouveau_gpuobj_fini,
	.rd32 = _nouveau_gpuobj_rd32,
	.wr32 = _nouveau_gpuobj_wr32,
};

static struct nouveau_oclass
nv84_crypt_sclass[] = {
	{ 0x74c1, &nv84_crypt_ofuncs },
	{}
};

/*******************************************************************************
 * PCRYPT context
 ******************************************************************************/

static int
nv84_crypt_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nv84_crypt_chan *priv;
	int ret;

	ret = nouveau_crypt_context_create(parent, engine, oclass, NULL, 256,
					   0, NVOBJ_FLAG_ZERO_ALLOC, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	return 0;
}

static struct nouveau_oclass
nv84_crypt_cclass = {
	.handle = NV_ENGCTX(CRYPT, 0x84),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv84_crypt_context_ctor,
		.dtor = _nouveau_crypt_context_dtor,
		.init = _nouveau_crypt_context_init,
		.fini = _nouveau_crypt_context_fini,
		.rd32 = _nouveau_crypt_context_rd32,
		.wr32 = _nouveau_crypt_context_wr32,
	},
};

/*******************************************************************************
 * PCRYPT engine/subdev functions
 ******************************************************************************/

static const struct nouveau_bitfield nv84_crypt_intr_mask[] = {
	{ 0x00000001, "INVALID_STATE" },
	{ 0x00000002, "ILLEGAL_MTHD" },
	{ 0x00000004, "ILLEGAL_CLASS" },
	{ 0x00000080, "QUERY" },
	{ 0x00000100, "FAULT" },
	{}
};

static void
nv84_crypt_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_fifo *pfifo = nouveau_fifo(subdev);
	struct nouveau_engine *engine = nv_engine(subdev);
	struct nouveau_object *engctx;
	struct nv84_crypt_priv *priv = (void *)subdev;
	u32 stat = nv_rd32(priv, 0x102130);
	u32 mthd = nv_rd32(priv, 0x102190);
	u32 data = nv_rd32(priv, 0x102194);
	u32 inst = nv_rd32(priv, 0x102188) & 0x7fffffff;
	int chid;

	engctx = nouveau_engctx_get(engine, inst);
	chid   = pfifo->chid(pfifo, engctx);

	if (stat) {
		nv_error(priv, "");
		nouveau_bitfield_print(nv84_crypt_intr_mask, stat);
		printk(" ch %d [0x%010llx] mthd 0x%04x data 0x%08x\n",
		       chid, (u64)inst << 12, mthd, data);
	}

	nv_wr32(priv, 0x102130, stat);
	nv_wr32(priv, 0x10200c, 0x10);

	nv50_fb_trap(nouveau_fb(priv), 1);
	nouveau_engctx_put(engctx);
}

static int
nv84_crypt_tlb_flush(struct nouveau_engine *engine)
{
	nv50_vm_flush_engine(&engine->base, 0x0a);
	return 0;
}

static int
nv84_crypt_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv84_crypt_priv *priv;
	int ret;

	ret = nouveau_crypt_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00004000;
	nv_subdev(priv)->intr = nv84_crypt_intr;
	nv_engine(priv)->cclass = &nv84_crypt_cclass;
	nv_engine(priv)->sclass = nv84_crypt_sclass;
	nv_engine(priv)->tlb_flush = nv84_crypt_tlb_flush;
	return 0;
}

static int
nv84_crypt_init(struct nouveau_object *object)
{
	struct nv84_crypt_priv *priv = (void *)object;
	int ret;

	ret = nouveau_crypt_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x102130, 0xffffffff);
	nv_wr32(priv, 0x102140, 0xffffffbf);
	nv_wr32(priv, 0x10200c, 0x00000010);
	return 0;
}

struct nouveau_oclass
nv84_crypt_oclass = {
	.handle = NV_ENGINE(CRYPT, 0x84),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv84_crypt_ctor,
		.dtor = _nouveau_crypt_dtor,
		.init = nv84_crypt_init,
		.fini = _nouveau_crypt_fini,
	},
};
