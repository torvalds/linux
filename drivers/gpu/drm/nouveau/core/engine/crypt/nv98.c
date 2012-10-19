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

#include <subdev/timer.h>
#include <subdev/fb.h>

#include <engine/fifo.h>
#include <engine/crypt.h>

#include "fuc/nv98.fuc.h"

struct nv98_crypt_priv {
	struct nouveau_crypt base;
};

struct nv98_crypt_chan {
	struct nouveau_crypt_chan base;
};

/*******************************************************************************
 * Crypt object classes
 ******************************************************************************/

static struct nouveau_oclass
nv98_crypt_sclass[] = {
	{ 0x88b4, &nouveau_object_ofuncs },
	{},
};

/*******************************************************************************
 * PCRYPT context
 ******************************************************************************/

static int
nv98_crypt_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nv98_crypt_chan *priv;
	int ret;

	ret = nouveau_crypt_context_create(parent, engine, oclass, NULL, 256,
					   256, NVOBJ_FLAG_ZERO_ALLOC, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	return 0;
}

static struct nouveau_oclass
nv98_crypt_cclass = {
	.handle = NV_ENGCTX(CRYPT, 0x98),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv98_crypt_context_ctor,
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

static const struct nouveau_enum nv98_crypt_isr_error_name[] = {
	{ 0x0000, "ILLEGAL_MTHD" },
	{ 0x0001, "INVALID_BITFIELD" },
	{ 0x0002, "INVALID_ENUM" },
	{ 0x0003, "QUERY" },
	{}
};

static void
nv98_crypt_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_fifo *pfifo = nouveau_fifo(subdev);
	struct nouveau_engine *engine = nv_engine(subdev);
	struct nouveau_object *engctx;
	struct nv98_crypt_priv *priv = (void *)subdev;
	u32 disp = nv_rd32(priv, 0x08701c);
	u32 stat = nv_rd32(priv, 0x087008) & disp & ~(disp >> 16);
	u32 inst = nv_rd32(priv, 0x087050) & 0x3fffffff;
	u32 ssta = nv_rd32(priv, 0x087040) & 0x0000ffff;
	u32 addr = nv_rd32(priv, 0x087040) >> 16;
	u32 mthd = (addr & 0x07ff) << 2;
	u32 subc = (addr & 0x3800) >> 11;
	u32 data = nv_rd32(priv, 0x087044);
	int chid;

	engctx = nouveau_engctx_get(engine, inst);
	chid   = pfifo->chid(pfifo, engctx);

	if (stat & 0x00000040) {
		nv_error(priv, "DISPATCH_ERROR [");
		nouveau_enum_print(nv98_crypt_isr_error_name, ssta);
		printk("] ch %d [0x%010llx] subc %d mthd 0x%04x data 0x%08x\n",
		       chid, (u64)inst << 12, subc, mthd, data);
		nv_wr32(priv, 0x087004, 0x00000040);
		stat &= ~0x00000040;
	}

	if (stat) {
		nv_error(priv, "unhandled intr 0x%08x\n", stat);
		nv_wr32(priv, 0x087004, stat);
	}

	nv50_fb_trap(nouveau_fb(priv), 1);
	nouveau_engctx_put(engctx);
}

static int
nv98_crypt_tlb_flush(struct nouveau_engine *engine)
{
	nv50_vm_flush_engine(&engine->base, 0x0a);
	return 0;
}

static int
nv98_crypt_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv98_crypt_priv *priv;
	int ret;

	ret = nouveau_crypt_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00004000;
	nv_subdev(priv)->intr = nv98_crypt_intr;
	nv_engine(priv)->cclass = &nv98_crypt_cclass;
	nv_engine(priv)->sclass = nv98_crypt_sclass;
	nv_engine(priv)->tlb_flush = nv98_crypt_tlb_flush;
	return 0;
}

static int
nv98_crypt_init(struct nouveau_object *object)
{
	struct nv98_crypt_priv *priv = (void *)object;
	int ret, i;

	ret = nouveau_crypt_init(&priv->base);
	if (ret)
		return ret;

	/* wait for exit interrupt to signal */
	nv_wait(priv, 0x087008, 0x00000010, 0x00000010);
	nv_wr32(priv, 0x087004, 0x00000010);

	/* upload microcode code and data segments */
	nv_wr32(priv, 0x087ff8, 0x00100000);
	for (i = 0; i < ARRAY_SIZE(nv98_pcrypt_code); i++)
		nv_wr32(priv, 0x087ff4, nv98_pcrypt_code[i]);

	nv_wr32(priv, 0x087ff8, 0x00000000);
	for (i = 0; i < ARRAY_SIZE(nv98_pcrypt_data); i++)
		nv_wr32(priv, 0x087ff4, nv98_pcrypt_data[i]);

	/* start it running */
	nv_wr32(priv, 0x08710c, 0x00000000);
	nv_wr32(priv, 0x087104, 0x00000000); /* ENTRY */
	nv_wr32(priv, 0x087100, 0x00000002); /* TRIGGER */
	return 0;
}

struct nouveau_oclass
nv98_crypt_oclass = {
	.handle = NV_ENGINE(CRYPT, 0x98),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv98_crypt_ctor,
		.dtor = _nouveau_crypt_dtor,
		.init = nv98_crypt_init,
		.fini = _nouveau_crypt_fini,
	},
};
