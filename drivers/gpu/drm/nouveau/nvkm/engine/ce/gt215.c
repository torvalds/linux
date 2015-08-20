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
#include <engine/fifo.h>
#include "fuc/gt215.fuc3.h"

#include <core/client.h>
#include <core/enum.h>

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
gt215_ce_intr(struct nvkm_falcon *ce, struct nvkm_fifo_chan *chan)
{
	struct nvkm_subdev *subdev = &ce->engine.subdev;
	struct nvkm_device *device = subdev->device;
	const u32 base = (nv_subidx(subdev) - NVDEV_ENGINE_CE0) * 0x1000;
	u32 ssta = nvkm_rd32(device, 0x104040 + base) & 0x0000ffff;
	u32 addr = nvkm_rd32(device, 0x104040 + base) >> 16;
	u32 mthd = (addr & 0x07ff) << 2;
	u32 subc = (addr & 0x3800) >> 11;
	u32 data = nvkm_rd32(device, 0x104044 + base);
	const struct nvkm_enum *en =
		nvkm_enum_find(gt215_ce_isr_error_name, ssta);

	nvkm_error(subdev, "DISPATCH_ERROR %04x [%s] ch %d [%010llx %s] "
			   "subc %d mthd %04x data %08x\n", ssta,
		   en ? en->name : "", chan ? chan->chid : -1,
		   chan ? chan->inst : 0, nvkm_client_name(chan),
		   subc, mthd, data);
}

static const struct nvkm_falcon_func
gt215_ce_func = {
	.intr = gt215_ce_intr,
};

static int
gt215_ce_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	bool enable = (nv_device(parent)->chipset != 0xaf);
	struct nvkm_falcon *ce;
	int ret;

	ret = nvkm_falcon_create(&gt215_ce_func, parent, engine, oclass,
				 0x104000, enable, "PCE0", "ce0", &ce);
	*pobject = nv_object(ce);
	if (ret)
		return ret;

	nv_subdev(ce)->unit = 0x00802000;
	nv_engine(ce)->cclass = &gt215_ce_cclass;
	nv_engine(ce)->sclass = gt215_ce_sclass;
	nv_falcon(ce)->code.data = gt215_ce_code;
	nv_falcon(ce)->code.size = sizeof(gt215_ce_code);
	nv_falcon(ce)->data.data = gt215_ce_data;
	nv_falcon(ce)->data.size = sizeof(gt215_ce_data);
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
	},
};
