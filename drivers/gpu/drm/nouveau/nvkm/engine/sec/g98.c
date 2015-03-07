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
#include <engine/sec.h>
#include <engine/falcon.h>
#include "fuc/g98.fuc0s.h"

#include <core/client.h>
#include <core/enum.h>
#include <engine/fifo.h>

struct g98_sec_priv {
	struct nvkm_falcon base;
};

/*******************************************************************************
 * Crypt object classes
 ******************************************************************************/

static struct nvkm_oclass
g98_sec_sclass[] = {
	{ 0x88b4, &nvkm_object_ofuncs },
	{},
};

/*******************************************************************************
 * PSEC context
 ******************************************************************************/

static struct nvkm_oclass
g98_sec_cclass = {
	.handle = NV_ENGCTX(SEC, 0x98),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_falcon_context_ctor,
		.dtor = _nvkm_falcon_context_dtor,
		.init = _nvkm_falcon_context_init,
		.fini = _nvkm_falcon_context_fini,
		.rd32 = _nvkm_falcon_context_rd32,
		.wr32 = _nvkm_falcon_context_wr32,
	},
};

/*******************************************************************************
 * PSEC engine/subdev functions
 ******************************************************************************/

static const struct nvkm_enum g98_sec_isr_error_name[] = {
	{ 0x0000, "ILLEGAL_MTHD" },
	{ 0x0001, "INVALID_BITFIELD" },
	{ 0x0002, "INVALID_ENUM" },
	{ 0x0003, "QUERY" },
	{}
};

static void
g98_sec_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_fifo *pfifo = nvkm_fifo(subdev);
	struct nvkm_engine *engine = nv_engine(subdev);
	struct nvkm_object *engctx;
	struct g98_sec_priv *priv = (void *)subdev;
	u32 disp = nv_rd32(priv, 0x08701c);
	u32 stat = nv_rd32(priv, 0x087008) & disp & ~(disp >> 16);
	u32 inst = nv_rd32(priv, 0x087050) & 0x3fffffff;
	u32 ssta = nv_rd32(priv, 0x087040) & 0x0000ffff;
	u32 addr = nv_rd32(priv, 0x087040) >> 16;
	u32 mthd = (addr & 0x07ff) << 2;
	u32 subc = (addr & 0x3800) >> 11;
	u32 data = nv_rd32(priv, 0x087044);
	int chid;

	engctx = nvkm_engctx_get(engine, inst);
	chid   = pfifo->chid(pfifo, engctx);

	if (stat & 0x00000040) {
		nv_error(priv, "DISPATCH_ERROR [");
		nvkm_enum_print(g98_sec_isr_error_name, ssta);
		pr_cont("] ch %d [0x%010llx %s] subc %d mthd 0x%04x data 0x%08x\n",
		       chid, (u64)inst << 12, nvkm_client_name(engctx),
		       subc, mthd, data);
		nv_wr32(priv, 0x087004, 0x00000040);
		stat &= ~0x00000040;
	}

	if (stat) {
		nv_error(priv, "unhandled intr 0x%08x\n", stat);
		nv_wr32(priv, 0x087004, stat);
	}

	nvkm_engctx_put(engctx);
}

static int
g98_sec_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	     struct nvkm_oclass *oclass, void *data, u32 size,
	     struct nvkm_object **pobject)
{
	struct g98_sec_priv *priv;
	int ret;

	ret = nvkm_falcon_create(parent, engine, oclass, 0x087000, true,
				 "PSEC", "sec", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00004000;
	nv_subdev(priv)->intr = g98_sec_intr;
	nv_engine(priv)->cclass = &g98_sec_cclass;
	nv_engine(priv)->sclass = g98_sec_sclass;
	nv_falcon(priv)->code.data = g98_psec_code;
	nv_falcon(priv)->code.size = sizeof(g98_psec_code);
	nv_falcon(priv)->data.data = g98_psec_data;
	nv_falcon(priv)->data.size = sizeof(g98_psec_data);
	return 0;
}

struct nvkm_oclass
g98_sec_oclass = {
	.handle = NV_ENGINE(SEC, 0x98),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = g98_sec_ctor,
		.dtor = _nvkm_falcon_dtor,
		.init = _nvkm_falcon_init,
		.fini = _nvkm_falcon_fini,
		.rd32 = _nvkm_falcon_rd32,
		.wr32 = _nvkm_falcon_wr32,
	},
};
