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
#include <engine/mpeg.h>

#include <subdev/bar.h>
#include <subdev/timer.h>

struct nv50_mpeg_chan {
	struct nvkm_mpeg_chan base;
};

/*******************************************************************************
 * MPEG object classes
 ******************************************************************************/

static int
nv50_mpeg_object_ctor(struct nvkm_object *parent,
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

struct nvkm_ofuncs
nv50_mpeg_ofuncs = {
	.ctor = nv50_mpeg_object_ctor,
	.dtor = _nvkm_gpuobj_dtor,
	.init = _nvkm_gpuobj_init,
	.fini = _nvkm_gpuobj_fini,
	.rd32 = _nvkm_gpuobj_rd32,
	.wr32 = _nvkm_gpuobj_wr32,
};

static struct nvkm_oclass
nv50_mpeg_sclass[] = {
	{ 0x3174, &nv50_mpeg_ofuncs },
	{}
};

/*******************************************************************************
 * PMPEG context
 ******************************************************************************/

int
nv50_mpeg_context_ctor(struct nvkm_object *parent,
		       struct nvkm_object *engine,
		       struct nvkm_oclass *oclass, void *data, u32 size,
		       struct nvkm_object **pobject)
{
	struct nvkm_bar *bar = nvkm_bar(parent);
	struct nv50_mpeg_chan *chan;
	int ret;

	ret = nvkm_mpeg_context_create(parent, engine, oclass, NULL, 128 * 4,
				       0, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	nv_wo32(chan, 0x0070, 0x00801ec1);
	nv_wo32(chan, 0x007c, 0x0000037c);
	bar->flush(bar);
	return 0;
}

static struct nvkm_oclass
nv50_mpeg_cclass = {
	.handle = NV_ENGCTX(MPEG, 0x50),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_mpeg_context_ctor,
		.dtor = _nvkm_mpeg_context_dtor,
		.init = _nvkm_mpeg_context_init,
		.fini = _nvkm_mpeg_context_fini,
		.rd32 = _nvkm_mpeg_context_rd32,
		.wr32 = _nvkm_mpeg_context_wr32,
	},
};

/*******************************************************************************
 * PMPEG engine/subdev functions
 ******************************************************************************/

void
nv50_mpeg_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_mpeg *mpeg = (void *)subdev;
	struct nvkm_device *device = mpeg->engine.subdev.device;
	u32 stat = nvkm_rd32(device, 0x00b100);
	u32 type = nvkm_rd32(device, 0x00b230);
	u32 mthd = nvkm_rd32(device, 0x00b234);
	u32 data = nvkm_rd32(device, 0x00b238);
	u32 show = stat;

	if (stat & 0x01000000) {
		/* happens on initial binding of the object */
		if (type == 0x00000020 && mthd == 0x0000) {
			nvkm_wr32(device, 0x00b308, 0x00000100);
			show &= ~0x01000000;
		}
	}

	if (show) {
		nv_info(mpeg, "0x%08x 0x%08x 0x%08x 0x%08x\n",
			stat, type, mthd, data);
	}

	nvkm_wr32(device, 0x00b100, stat);
	nvkm_wr32(device, 0x00b230, 0x00000001);
}

static void
nv50_vpe_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_mpeg *mpeg = (void *)subdev;
	struct nvkm_device *device = mpeg->engine.subdev.device;

	if (nvkm_rd32(device, 0x00b100))
		nv50_mpeg_intr(subdev);

	if (nvkm_rd32(device, 0x00b800)) {
		u32 stat = nvkm_rd32(device, 0x00b800);
		nv_info(mpeg, "PMSRCH: 0x%08x\n", stat);
		nvkm_wr32(device, 0xb800, stat);
	}
}

static int
nv50_mpeg_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_mpeg *mpeg;
	int ret;

	ret = nvkm_mpeg_create(parent, engine, oclass, &mpeg);
	*pobject = nv_object(mpeg);
	if (ret)
		return ret;

	nv_subdev(mpeg)->unit = 0x00400002;
	nv_subdev(mpeg)->intr = nv50_vpe_intr;
	nv_engine(mpeg)->cclass = &nv50_mpeg_cclass;
	nv_engine(mpeg)->sclass = nv50_mpeg_sclass;
	return 0;
}

int
nv50_mpeg_init(struct nvkm_object *object)
{
	struct nvkm_mpeg *mpeg = (void *)object;
	struct nvkm_device *device = mpeg->engine.subdev.device;
	int ret;

	ret = nvkm_mpeg_init(mpeg);
	if (ret)
		return ret;

	nvkm_wr32(device, 0x00b32c, 0x00000000);
	nvkm_wr32(device, 0x00b314, 0x00000100);
	nvkm_wr32(device, 0x00b0e0, 0x0000001a);

	nvkm_wr32(device, 0x00b220, 0x00000044);
	nvkm_wr32(device, 0x00b300, 0x00801ec1);
	nvkm_wr32(device, 0x00b390, 0x00000000);
	nvkm_wr32(device, 0x00b394, 0x00000000);
	nvkm_wr32(device, 0x00b398, 0x00000000);
	nvkm_mask(device, 0x00b32c, 0x00000001, 0x00000001);

	nvkm_wr32(device, 0x00b100, 0xffffffff);
	nvkm_wr32(device, 0x00b140, 0xffffffff);

	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x00b200) & 0x00000001))
			break;
	) < 0) {
		nv_error(mpeg, "timeout 0x%08x\n", nvkm_rd32(device, 0x00b200));
		return -EBUSY;
	}

	return 0;
}

struct nvkm_oclass
nv50_mpeg_oclass = {
	.handle = NV_ENGINE(MPEG, 0x50),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_mpeg_ctor,
		.dtor = _nvkm_mpeg_dtor,
		.init = nv50_mpeg_init,
		.fini = _nvkm_mpeg_fini,
	},
};
