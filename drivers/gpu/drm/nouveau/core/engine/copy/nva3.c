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

#include <subdev/fb.h>
#include <subdev/vm.h>

#include <engine/fifo.h>
#include <engine/copy.h>

#include "fuc/nva3.fuc.h"

struct nva3_copy_priv {
	struct nouveau_copy base;
};

struct nva3_copy_chan {
	struct nouveau_copy_chan base;
};

/*******************************************************************************
 * Copy object classes
 ******************************************************************************/

static struct nouveau_oclass
nva3_copy_sclass[] = {
	{ 0x85b5, &nouveau_object_ofuncs },
	{}
};

/*******************************************************************************
 * PCOPY context
 ******************************************************************************/

static int
nva3_copy_context_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nva3_copy_chan *priv;
	int ret;

	ret = nouveau_copy_context_create(parent, engine, oclass, NULL, 256, 0,
					  NVOBJ_FLAG_ZERO_ALLOC, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	return 0;
}

static struct nouveau_oclass
nva3_copy_cclass = {
	.handle = NV_ENGCTX(COPY0, 0xa3),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nva3_copy_context_ctor,
		.dtor = _nouveau_copy_context_dtor,
		.init = _nouveau_copy_context_init,
		.fini = _nouveau_copy_context_fini,
		.rd32 = _nouveau_copy_context_rd32,
		.wr32 = _nouveau_copy_context_wr32,

	},
};

/*******************************************************************************
 * PCOPY engine/subdev functions
 ******************************************************************************/

static const struct nouveau_enum nva3_copy_isr_error_name[] = {
	{ 0x0001, "ILLEGAL_MTHD" },
	{ 0x0002, "INVALID_ENUM" },
	{ 0x0003, "INVALID_BITFIELD" },
	{}
};

static void
nva3_copy_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_fifo *pfifo = nouveau_fifo(subdev);
	struct nouveau_engine *engine = nv_engine(subdev);
	struct nouveau_object *engctx;
	struct nva3_copy_priv *priv = (void *)subdev;
	u32 dispatch = nv_rd32(priv, 0x10401c);
	u32 stat = nv_rd32(priv, 0x104008) & dispatch & ~(dispatch >> 16);
	u64 inst = nv_rd32(priv, 0x104050) & 0x3fffffff;
	u32 ssta = nv_rd32(priv, 0x104040) & 0x0000ffff;
	u32 addr = nv_rd32(priv, 0x104040) >> 16;
	u32 mthd = (addr & 0x07ff) << 2;
	u32 subc = (addr & 0x3800) >> 11;
	u32 data = nv_rd32(priv, 0x104044);
	int chid;

	engctx = nouveau_engctx_get(engine, inst);
	chid   = pfifo->chid(pfifo, engctx);

	if (stat & 0x00000040) {
		nv_error(priv, "DISPATCH_ERROR [");
		nouveau_enum_print(nva3_copy_isr_error_name, ssta);
		printk("] ch %d [0x%010llx] subc %d mthd 0x%04x data 0x%08x\n",
		       chid, inst << 12, subc, mthd, data);
		nv_wr32(priv, 0x104004, 0x00000040);
		stat &= ~0x00000040;
	}

	if (stat) {
		nv_error(priv, "unhandled intr 0x%08x\n", stat);
		nv_wr32(priv, 0x104004, stat);
	}

	nv50_fb_trap(nouveau_fb(priv), 1);
	nouveau_engctx_put(engctx);
}

static int
nva3_copy_tlb_flush(struct nouveau_engine *engine)
{
	nv50_vm_flush_engine(&engine->base, 0x0d);
	return 0;
}

static int
nva3_copy_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	bool enable = (nv_device(parent)->chipset != 0xaf);
	struct nva3_copy_priv *priv;
	int ret;

	ret = nouveau_copy_create(parent, engine, oclass, enable, 0, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00802000;
	nv_subdev(priv)->intr = nva3_copy_intr;
	nv_engine(priv)->cclass = &nva3_copy_cclass;
	nv_engine(priv)->sclass = nva3_copy_sclass;
	nv_engine(priv)->tlb_flush = nva3_copy_tlb_flush;
	return 0;
}

static int
nva3_copy_init(struct nouveau_object *object)
{
	struct nva3_copy_priv *priv = (void *)object;
	int ret, i;

	ret = nouveau_copy_init(&priv->base);
	if (ret)
		return ret;

	/* disable all interrupts */
	nv_wr32(priv, 0x104014, 0xffffffff);

	/* upload ucode */
	nv_wr32(priv, 0x1041c0, 0x01000000);
	for (i = 0; i < sizeof(nva3_pcopy_data) / 4; i++)
		nv_wr32(priv, 0x1041c4, nva3_pcopy_data[i]);

	nv_wr32(priv, 0x104180, 0x01000000);
	for (i = 0; i < sizeof(nva3_pcopy_code) / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(priv, 0x104188, i >> 6);
		nv_wr32(priv, 0x104184, nva3_pcopy_code[i]);
	}

	/* start it running */
	nv_wr32(priv, 0x10410c, 0x00000000);
	nv_wr32(priv, 0x104104, 0x00000000); /* ENTRY */
	nv_wr32(priv, 0x104100, 0x00000002); /* TRIGGER */
	return 0;
}

static int
nva3_copy_fini(struct nouveau_object *object, bool suspend)
{
	struct nva3_copy_priv *priv = (void *)object;

	nv_mask(priv, 0x104048, 0x00000003, 0x00000000);
	nv_wr32(priv, 0x104014, 0xffffffff);

	return nouveau_copy_fini(&priv->base, suspend);
}

struct nouveau_oclass
nva3_copy_oclass = {
	.handle = NV_ENGINE(COPY0, 0xa3),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nva3_copy_ctor,
		.dtor = _nouveau_copy_dtor,
		.init = nva3_copy_init,
		.fini = nva3_copy_fini,
	},
};
