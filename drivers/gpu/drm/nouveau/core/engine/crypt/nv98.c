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

#include <core/client.h>
#include <core/os.h>
#include <core/enum.h>
#include <core/class.h>
#include <core/engctx.h>
#include <core/falcon.h>

#include <subdev/timer.h>
#include <subdev/fb.h>

#include <engine/fifo.h>
#include <engine/crypt.h>

#include "fuc/nv98.fuc.h"

struct nv98_crypt_priv {
	struct nouveau_falcon base;
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

static struct nouveau_oclass
nv98_crypt_cclass = {
	.handle = NV_ENGCTX(CRYPT, 0x98),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = _nouveau_falcon_context_ctor,
		.dtor = _nouveau_falcon_context_dtor,
		.init = _nouveau_falcon_context_init,
		.fini = _nouveau_falcon_context_fini,
		.rd32 = _nouveau_falcon_context_rd32,
		.wr32 = _nouveau_falcon_context_wr32,
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
		pr_cont("] ch %d [0x%010llx %s] subc %d mthd 0x%04x data 0x%08x\n",
		       chid, (u64)inst << 12, nouveau_client_name(engctx),
		       subc, mthd, data);
		nv_wr32(priv, 0x087004, 0x00000040);
		stat &= ~0x00000040;
	}

	if (stat) {
		nv_error(priv, "unhandled intr 0x%08x\n", stat);
		nv_wr32(priv, 0x087004, stat);
	}

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

	ret = nouveau_falcon_create(parent, engine, oclass, 0x087000, true,
				    "PCRYPT", "crypt", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00004000;
	nv_subdev(priv)->intr = nv98_crypt_intr;
	nv_engine(priv)->cclass = &nv98_crypt_cclass;
	nv_engine(priv)->sclass = nv98_crypt_sclass;
	nv_engine(priv)->tlb_flush = nv98_crypt_tlb_flush;
	nv_falcon(priv)->code.data = nv98_pcrypt_code;
	nv_falcon(priv)->code.size = sizeof(nv98_pcrypt_code);
	nv_falcon(priv)->data.data = nv98_pcrypt_data;
	nv_falcon(priv)->data.size = sizeof(nv98_pcrypt_data);
	return 0;
}

struct nouveau_oclass
nv98_crypt_oclass = {
	.handle = NV_ENGINE(CRYPT, 0x98),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv98_crypt_ctor,
		.dtor = _nouveau_falcon_dtor,
		.init = _nouveau_falcon_init,
		.fini = _nouveau_falcon_fini,
		.rd32 = _nouveau_falcon_rd32,
		.wr32 = _nouveau_falcon_wr32,
	},
};
