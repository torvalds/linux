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

#include <engine/fifo.h>
#include <engine/copy.h>

#include "fuc/nvc0.fuc.h"

struct nvc0_copy_priv {
	struct nouveau_copy base;
};

struct nvc0_copy_chan {
	struct nouveau_copy_chan base;
};

/*******************************************************************************
 * Copy object classes
 ******************************************************************************/

static struct nouveau_oclass
nvc0_copy0_sclass[] = {
	{ 0x90b5, &nouveau_object_ofuncs },
	{},
};

static struct nouveau_oclass
nvc0_copy1_sclass[] = {
	{ 0x90b8, &nouveau_object_ofuncs },
	{},
};

/*******************************************************************************
 * PCOPY context
 ******************************************************************************/

static int
nvc0_copy_context_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nvc0_copy_chan *priv;
	int ret;

	ret = nouveau_copy_context_create(parent, engine, oclass, NULL, 256,
					  256, NVOBJ_FLAG_ZERO_ALLOC, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	return 0;
}

static struct nouveau_ofuncs
nvc0_copy_context_ofuncs = {
	.ctor = nvc0_copy_context_ctor,
	.dtor = _nouveau_copy_context_dtor,
	.init = _nouveau_copy_context_init,
	.fini = _nouveau_copy_context_fini,
	.rd32 = _nouveau_copy_context_rd32,
	.wr32 = _nouveau_copy_context_wr32,
};

static struct nouveau_oclass
nvc0_copy0_cclass = {
	.handle = NV_ENGCTX(COPY0, 0xc0),
	.ofuncs = &nvc0_copy_context_ofuncs,
};

static struct nouveau_oclass
nvc0_copy1_cclass = {
	.handle = NV_ENGCTX(COPY1, 0xc0),
	.ofuncs = &nvc0_copy_context_ofuncs,
};

/*******************************************************************************
 * PCOPY engine/subdev functions
 ******************************************************************************/

static const struct nouveau_enum nvc0_copy_isr_error_name[] = {
	{ 0x0001, "ILLEGAL_MTHD" },
	{ 0x0002, "INVALID_ENUM" },
	{ 0x0003, "INVALID_BITFIELD" },
	{}
};

static void
nvc0_copy_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_fifo *pfifo = nouveau_fifo(subdev);
	struct nouveau_engine *engine = nv_engine(subdev);
	struct nouveau_object *engctx;
	int idx = nv_engidx(nv_object(subdev)) - NVDEV_ENGINE_COPY0;
	struct nvc0_copy_priv *priv = (void *)subdev;
	u32 disp = nv_rd32(priv, 0x10401c + (idx * 0x1000));
	u32 intr = nv_rd32(priv, 0x104008 + (idx * 0x1000));
	u32 stat = intr & disp & ~(disp >> 16);
	u64 inst = nv_rd32(priv, 0x104050 + (idx * 0x1000)) & 0x0fffffff;
	u32 ssta = nv_rd32(priv, 0x104040 + (idx * 0x1000)) & 0x0000ffff;
	u32 addr = nv_rd32(priv, 0x104040 + (idx * 0x1000)) >> 16;
	u32 mthd = (addr & 0x07ff) << 2;
	u32 subc = (addr & 0x3800) >> 11;
	u32 data = nv_rd32(priv, 0x104044 + (idx * 0x1000));
	int chid;

	engctx = nouveau_engctx_get(engine, inst);
	chid   = pfifo->chid(pfifo, engctx);

	if (stat & 0x00000040) {
		nv_error(priv, "DISPATCH_ERROR [");
		nouveau_enum_print(nvc0_copy_isr_error_name, ssta);
		printk("] ch %d [0x%010llx] subc %d mthd 0x%04x data 0x%08x\n",
		       chid, (u64)inst << 12, subc, mthd, data);
		nv_wr32(priv, 0x104004 + (idx * 0x1000), 0x00000040);
		stat &= ~0x00000040;
	}

	if (stat) {
		nv_error(priv, "unhandled intr 0x%08x\n", stat);
		nv_wr32(priv, 0x104004 + (idx * 0x1000), stat);
	}

	nouveau_engctx_put(engctx);
}

static int
nvc0_copy0_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvc0_copy_priv *priv;
	int ret;

	if (nv_rd32(parent, 0x022500) & 0x00000100)
		return -ENODEV;

	ret = nouveau_copy_create(parent, engine, oclass, true, 0, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000040;
	nv_subdev(priv)->intr = nvc0_copy_intr;
	nv_engine(priv)->cclass = &nvc0_copy0_cclass;
	nv_engine(priv)->sclass = nvc0_copy0_sclass;
	return 0;
}

static int
nvc0_copy1_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvc0_copy_priv *priv;
	int ret;

	if (nv_rd32(parent, 0x022500) & 0x00000200)
		return -ENODEV;

	ret = nouveau_copy_create(parent, engine, oclass, true, 1, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000080;
	nv_subdev(priv)->intr = nvc0_copy_intr;
	nv_engine(priv)->cclass = &nvc0_copy1_cclass;
	nv_engine(priv)->sclass = nvc0_copy1_sclass;
	return 0;
}

static int
nvc0_copy_init(struct nouveau_object *object)
{
	int idx = nv_engidx(object) - NVDEV_ENGINE_COPY0;
	struct nvc0_copy_priv *priv = (void *)object;
	int ret, i;

	ret = nouveau_copy_init(&priv->base);
	if (ret)
		return ret;

	/* disable all interrupts */
	nv_wr32(priv, 0x104014 + (idx * 0x1000), 0xffffffff);

	/* upload ucode */
	nv_wr32(priv, 0x1041c0 + (idx * 0x1000), 0x01000000);
	for (i = 0; i < sizeof(nvc0_pcopy_data) / 4; i++)
		nv_wr32(priv, 0x1041c4 + (idx * 0x1000), nvc0_pcopy_data[i]);

	nv_wr32(priv, 0x104180 + (idx * 0x1000), 0x01000000);
	for (i = 0; i < sizeof(nvc0_pcopy_code) / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(priv, 0x104188 + (idx * 0x1000), i >> 6);
		nv_wr32(priv, 0x104184 + (idx * 0x1000), nvc0_pcopy_code[i]);
	}

	/* start it running */
	nv_wr32(priv, 0x104084 + (idx * 0x1000), idx);
	nv_wr32(priv, 0x10410c + (idx * 0x1000), 0x00000000);
	nv_wr32(priv, 0x104104 + (idx * 0x1000), 0x00000000); /* ENTRY */
	nv_wr32(priv, 0x104100 + (idx * 0x1000), 0x00000002); /* TRIGGER */
	return 0;
}

static int
nvc0_copy_fini(struct nouveau_object *object, bool suspend)
{
	int idx = nv_engidx(object) - NVDEV_ENGINE_COPY0;
	struct nvc0_copy_priv *priv = (void *)object;

	nv_mask(priv, 0x104048 + (idx * 0x1000), 0x00000003, 0x00000000);
	nv_wr32(priv, 0x104014 + (idx * 0x1000), 0xffffffff);

	return nouveau_copy_fini(&priv->base, suspend);
}

struct nouveau_oclass
nvc0_copy0_oclass = {
	.handle = NV_ENGINE(COPY0, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_copy0_ctor,
		.dtor = _nouveau_copy_dtor,
		.init = nvc0_copy_init,
		.fini = nvc0_copy_fini,
	},
};

struct nouveau_oclass
nvc0_copy1_oclass = {
	.handle = NV_ENGINE(COPY1, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_copy1_ctor,
		.dtor = _nouveau_copy_dtor,
		.init = nvc0_copy_init,
		.fini = nvc0_copy_fini,
	},
};
