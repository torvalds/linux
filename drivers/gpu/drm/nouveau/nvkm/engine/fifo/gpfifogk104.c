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
#include "changk104.h"

#include <core/client.h>
#include <subdev/fb.h>
#include <subdev/mmu.h>
#include <subdev/timer.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

#define _(a,b) { (a), ((1ULL << (a)) | (b)) }
static const struct {
	u64 subdev;
	u64 mask;
} fifo_engine[] = {
	_(NVDEV_ENGINE_GR      , (1ULL << NVDEV_ENGINE_SW) |
				 (1ULL << NVDEV_ENGINE_CE2)),
	_(NVDEV_ENGINE_MSPDEC  , 0),
	_(NVDEV_ENGINE_MSPPP   , 0),
	_(NVDEV_ENGINE_MSVLD   , 0),
	_(NVDEV_ENGINE_CE0     , 0),
	_(NVDEV_ENGINE_CE1     , 0),
	_(NVDEV_ENGINE_MSENC   , 0),
};
#undef _
#define FIFO_ENGINE_NR ARRAY_SIZE(fifo_engine)

static int
gk104_fifo_chan_kick(struct gk104_fifo_chan *chan)
{
	struct nvkm_object *obj = (void *)chan;
	struct gk104_fifo *fifo = (void *)obj->engine;
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;

	nvkm_wr32(device, 0x002634, chan->base.chid);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x002634) & 0x00100000))
			break;
	) < 0) {
		nvkm_error(subdev, "channel %d [%s] kick timeout\n",
			   chan->base.chid, nvkm_client_name(chan));
		return -EBUSY;
	}

	return 0;
}

static int
gk104_fifo_context_detach(struct nvkm_object *parent, bool suspend,
			  struct nvkm_object *object)
{
	struct gk104_fifo_base *base = (void *)parent->parent;
	struct gk104_fifo_chan *chan = (void *)parent;
	struct nvkm_gpuobj *engn = &base->base.gpuobj;
	u32 addr;
	int ret;

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_SW    : return 0;
	case NVDEV_ENGINE_CE0   :
	case NVDEV_ENGINE_CE1   :
	case NVDEV_ENGINE_CE2   : addr = 0x0000; break;
	case NVDEV_ENGINE_GR    : addr = 0x0210; break;
	case NVDEV_ENGINE_MSVLD : addr = 0x0270; break;
	case NVDEV_ENGINE_MSPDEC: addr = 0x0250; break;
	case NVDEV_ENGINE_MSPPP : addr = 0x0260; break;
	default:
		return -EINVAL;
	}

	ret = gk104_fifo_chan_kick(chan);
	if (ret && suspend)
		return ret;

	if (addr) {
		nvkm_kmap(engn);
		nvkm_wo32(engn, addr + 0x00, 0x00000000);
		nvkm_wo32(engn, addr + 0x04, 0x00000000);
		nvkm_done(engn);
	}

	return 0;
}

static int
gk104_fifo_context_attach(struct nvkm_object *parent,
			  struct nvkm_object *object)
{
	struct gk104_fifo_base *base = (void *)parent->parent;
	struct nvkm_gpuobj *engn = &base->base.gpuobj;
	struct nvkm_engctx *ectx = (void *)object;
	u32 addr;
	int ret;

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_SW   :
		return 0;
	case NVDEV_ENGINE_CE0:
	case NVDEV_ENGINE_CE1:
	case NVDEV_ENGINE_CE2:
		nv_engctx(ectx)->addr = nv_gpuobj(base)->addr >> 12;
		return 0;
	case NVDEV_ENGINE_GR    : addr = 0x0210; break;
	case NVDEV_ENGINE_MSVLD : addr = 0x0270; break;
	case NVDEV_ENGINE_MSPDEC: addr = 0x0250; break;
	case NVDEV_ENGINE_MSPPP : addr = 0x0260; break;
	default:
		return -EINVAL;
	}

	if (!ectx->vma.node) {
		ret = nvkm_gpuobj_map(nv_gpuobj(ectx), base->vm,
				      NV_MEM_ACCESS_RW, &ectx->vma);
		if (ret)
			return ret;

		nv_engctx(ectx)->addr = nv_gpuobj(base)->addr >> 12;
	}

	nvkm_kmap(engn);
	nvkm_wo32(engn, addr + 0x00, lower_32_bits(ectx->vma.offset) | 4);
	nvkm_wo32(engn, addr + 0x04, upper_32_bits(ectx->vma.offset));
	nvkm_done(engn);
	return 0;
}

static int
gk104_fifo_chan_fini(struct nvkm_object *object, bool suspend)
{
	struct gk104_fifo *fifo = (void *)object->engine;
	struct gk104_fifo_chan *chan = (void *)object;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 chid = chan->base.chid;

	if (chan->state == RUNNING && (chan->state = STOPPED) == STOPPED) {
		nvkm_mask(device, 0x800004 + (chid * 8), 0x00000800, 0x00000800);
		gk104_fifo_runlist_update(fifo, chan->engine);
	}

	nvkm_wr32(device, 0x800000 + (chid * 8), 0x00000000);
	return nvkm_fifo_channel_fini(&chan->base, suspend);
}

static int
gk104_fifo_chan_init(struct nvkm_object *object)
{
	struct nvkm_gpuobj *base = nv_gpuobj(object->parent);
	struct gk104_fifo *fifo = (void *)object->engine;
	struct gk104_fifo_chan *chan = (void *)object;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 chid = chan->base.chid;
	int ret;

	ret = nvkm_fifo_channel_init(&chan->base);
	if (ret)
		return ret;

	nvkm_mask(device, 0x800004 + (chid * 8), 0x000f0000, chan->engine << 16);
	nvkm_wr32(device, 0x800000 + (chid * 8), 0x80000000 | base->addr >> 12);

	if (chan->state == STOPPED && (chan->state = RUNNING) == RUNNING) {
		nvkm_mask(device, 0x800004 + (chid * 8), 0x00000400, 0x00000400);
		gk104_fifo_runlist_update(fifo, chan->engine);
		nvkm_mask(device, 0x800004 + (chid * 8), 0x00000400, 0x00000400);
	}

	return 0;
}

static int
gk104_fifo_chan_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, void *data, u32 size,
		     struct nvkm_object **pobject)
{
	union {
		struct kepler_channel_gpfifo_a_v0 v0;
	} *args = data;
	struct gk104_fifo *fifo = (void *)engine;
	struct gk104_fifo_base *base = (void *)parent;
	struct gk104_fifo_chan *chan;
	struct nvkm_gpuobj *ramfc = &base->base.gpuobj;
	u64 usermem, ioffset, ilength;
	u32 engines;
	int ret, i;

	nvif_ioctl(parent, "create channel gpfifo size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(parent, "create channel gpfifo vers %d vm %llx"
				   "ioffset %016llx ilength %08x engine %08x\n",
			   args->v0.version, args->v0.vm, args->v0.ioffset,
			   args->v0.ilength, args->v0.engine);
		if (args->v0.vm)
			return -ENOENT;
	} else
		return ret;

	for (i = 0, engines = 0; i < FIFO_ENGINE_NR; i++) {
		if (!nvkm_engine(parent, fifo_engine[i].subdev))
			continue;
		engines |= (1 << i);
	}

	if (!args->v0.engine) {
		static struct nvkm_oclass oclass = {
			.ofuncs = &nvkm_object_ofuncs,
		};
		args->v0.engine = engines;
		return nvkm_object_old(parent, engine, &oclass, NULL, 0, pobject);
	}

	engines &= args->v0.engine;
	if (!engines) {
		nvif_ioctl(parent, "unsupported engines %08x\n",
			   args->v0.engine);
		return -ENODEV;
	}
	i = __ffs(engines);

	ret = nvkm_fifo_channel_create(parent, engine, oclass, 1,
				       fifo->user.bar.offset, 0x200, 0,
				       fifo_engine[i].mask, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->base.inst = base->base.gpuobj.addr;
	args->v0.chid = chan->base.chid;

	nv_parent(chan)->context_attach = gk104_fifo_context_attach;
	nv_parent(chan)->context_detach = gk104_fifo_context_detach;
	chan->engine = i;

	usermem = chan->base.chid * 0x200;
	ioffset = args->v0.ioffset;
	ilength = order_base_2(args->v0.ilength / 8);

	nvkm_kmap(fifo->user.mem);
	for (i = 0; i < 0x200; i += 4)
		nvkm_wo32(fifo->user.mem, usermem + i, 0x00000000);
	nvkm_done(fifo->user.mem);
	usermem = nvkm_memory_addr(fifo->user.mem) + usermem;

	nvkm_kmap(ramfc);
	nvkm_wo32(ramfc, 0x08, lower_32_bits(usermem));
	nvkm_wo32(ramfc, 0x0c, upper_32_bits(usermem));
	nvkm_wo32(ramfc, 0x10, 0x0000face);
	nvkm_wo32(ramfc, 0x30, 0xfffff902);
	nvkm_wo32(ramfc, 0x48, lower_32_bits(ioffset));
	nvkm_wo32(ramfc, 0x4c, upper_32_bits(ioffset) | (ilength << 16));
	nvkm_wo32(ramfc, 0x84, 0x20400000);
	nvkm_wo32(ramfc, 0x94, 0x30000001);
	nvkm_wo32(ramfc, 0x9c, 0x00000100);
	nvkm_wo32(ramfc, 0xac, 0x0000001f);
	nvkm_wo32(ramfc, 0xe8, chan->base.chid);
	nvkm_wo32(ramfc, 0xb8, 0xf8000000);
	nvkm_wo32(ramfc, 0xf8, 0x10003080); /* 0x002310 */
	nvkm_wo32(ramfc, 0xfc, 0x10000010); /* 0x002350 */
	nvkm_done(ramfc);
	return 0;
}

struct nvkm_ofuncs
gk104_fifo_chan_ofuncs = {
	.ctor = gk104_fifo_chan_ctor,
	.dtor = _nvkm_fifo_channel_dtor,
	.init = gk104_fifo_chan_init,
	.fini = gk104_fifo_chan_fini,
	.map  = _nvkm_fifo_channel_map,
	.rd32 = _nvkm_fifo_channel_rd32,
	.wr32 = _nvkm_fifo_channel_wr32,
	.ntfy = _nvkm_fifo_channel_ntfy
};

struct nvkm_oclass
gk104_fifo_sclass[] = {
	{ KEPLER_CHANNEL_GPFIFO_A, &gk104_fifo_chan_ofuncs },
	{}
};

static int
gk104_fifo_context_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
			struct nvkm_oclass *oclass, void *data, u32 size,
			struct nvkm_object **pobject)
{
	struct nvkm_device *device = nv_engine(engine)->subdev.device;
	struct gk104_fifo_base *base;
	int ret;

	ret = nvkm_fifo_context_create(parent, engine, oclass, NULL, 0x1000,
				       0x1000, NVOBJ_FLAG_ZERO_ALLOC, &base);
	*pobject = nv_object(base);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x10000, 0x1000, false, NULL, &base->pgd);
	if (ret)
		return ret;

	nvkm_kmap(&base->base.gpuobj);
	nvkm_wo32(&base->base.gpuobj, 0x0200, lower_32_bits(base->pgd->addr));
	nvkm_wo32(&base->base.gpuobj, 0x0204, upper_32_bits(base->pgd->addr));
	nvkm_wo32(&base->base.gpuobj, 0x0208, 0xffffffff);
	nvkm_wo32(&base->base.gpuobj, 0x020c, 0x000000ff);
	nvkm_done(&base->base.gpuobj);

	ret = nvkm_vm_ref(nvkm_client(parent)->vm, &base->vm, base->pgd);
	if (ret)
		return ret;

	return 0;
}

static void
gk104_fifo_context_dtor(struct nvkm_object *object)
{
	struct gk104_fifo_base *base = (void *)object;
	nvkm_vm_ref(NULL, &base->vm, base->pgd);
	nvkm_gpuobj_del(&base->pgd);
	nvkm_fifo_context_destroy(&base->base);
}

struct nvkm_oclass
gk104_fifo_cclass = {
	.handle = NV_ENGCTX(FIFO, 0xe0),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gk104_fifo_context_ctor,
		.dtor = gk104_fifo_context_dtor,
		.init = _nvkm_fifo_context_init,
		.fini = _nvkm_fifo_context_fini,
		.rd32 = _nvkm_fifo_context_rd32,
		.wr32 = _nvkm_fifo_context_wr32,
	},
};
