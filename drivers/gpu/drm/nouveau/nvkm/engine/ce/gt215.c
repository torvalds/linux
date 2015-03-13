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
#include <engine/ce.h>
#include <engine/falcon.h>
#include <engine/fifo.h>
#include "fuc/gt215.fuc3.h"

#include <core/client.h>
#include <core/device.h>
#include <core/enum.h>

struct gt215_ce_priv {
	struct nvkm_falcon base;
};

/*******************************************************************************
 * Copy object classes
 ******************************************************************************/

static struct nvkm_oclass
gt215_ce_sclass[] = {
	{ 0x85b5, &nvkm_object_ofuncs },
	{}
};

/*******************************************************************************
 * PCE context
 ******************************************************************************/

static struct nvkm_oclass
gt215_ce_cclass = {
	.handle = NV_ENGCTX(CE0, 0xa3),
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
 * PCE engine/subdev functions
 ******************************************************************************/

static const struct nvkm_enum
gt215_ce_isr_error_name[] = {
	{ 0x0001, "ILLEGAL_MTHD" },
	{ 0x0002, "INVALID_ENUM" },
	{ 0x0003, "INVALID_BITFIELD" },
	{}
};

void
gt215_ce_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_fifo *pfifo = nvkm_fifo(subdev);
	struct nvkm_engine *engine = nv_engine(subdev);
	struct nvkm_falcon *falcon = (void *)subdev;
	struct nvkm_object *engctx;
	u32 dispatch = nv_ro32(falcon, 0x01c);
	u32 stat = nv_ro32(falcon, 0x008) & dispatch & ~(dispatch >> 16);
	u64 inst = nv_ro32(falcon, 0x050) & 0x3fffffff;
	u32 ssta = nv_ro32(falcon, 0x040) & 0x0000ffff;
	u32 addr = nv_ro32(falcon, 0x040) >> 16;
	u32 mthd = (addr & 0x07ff) << 2;
	u32 subc = (addr & 0x3800) >> 11;
	u32 data = nv_ro32(falcon, 0x044);
	int chid;

	engctx = nvkm_engctx_get(engine, inst);
	chid   = pfifo->chid(pfifo, engctx);

	if (stat & 0x00000040) {
		nv_error(falcon, "DISPATCH_ERROR [");
		nvkm_enum_print(gt215_ce_isr_error_name, ssta);
		pr_cont("] ch %d [0x%010llx %s] subc %d mthd 0x%04x data 0x%08x\n",
		       chid, inst << 12, nvkm_client_name(engctx), subc,
		       mthd, data);
		nv_wo32(falcon, 0x004, 0x00000040);
		stat &= ~0x00000040;
	}

	if (stat) {
		nv_error(falcon, "unhandled intr 0x%08x\n", stat);
		nv_wo32(falcon, 0x004, stat);
	}

	nvkm_engctx_put(engctx);
}

static int
gt215_ce_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	bool enable = (nv_device(parent)->chipset != 0xaf);
	struct gt215_ce_priv *priv;
	int ret;

	ret = nvkm_falcon_create(parent, engine, oclass, 0x104000, enable,
				 "PCE0", "ce0", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00802000;
	nv_subdev(priv)->intr = gt215_ce_intr;
	nv_engine(priv)->cclass = &gt215_ce_cclass;
	nv_engine(priv)->sclass = gt215_ce_sclass;
	nv_falcon(priv)->code.data = gt215_pce_code;
	nv_falcon(priv)->code.size = sizeof(gt215_pce_code);
	nv_falcon(priv)->data.data = gt215_pce_data;
	nv_falcon(priv)->data.size = sizeof(gt215_pce_data);
	return 0;
}

struct nvkm_oclass
gt215_ce_oclass = {
	.handle = NV_ENGINE(CE0, 0xa3),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gt215_ce_ctor,
		.dtor = _nvkm_falcon_dtor,
		.init = _nvkm_falcon_init,
		.fini = _nvkm_falcon_fini,
		.rd32 = _nvkm_falcon_rd32,
		.wr32 = _nvkm_falcon_wr32,
	},
};
