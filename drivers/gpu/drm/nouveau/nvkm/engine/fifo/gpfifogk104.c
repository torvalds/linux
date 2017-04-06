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
#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <subdev/mmu.h>
#include <subdev/timer.h>

#include <nvif/class.h>
#include <nvif/cla06f.h>
#include <nvif/unpack.h>

static int
gk104_fifo_gpfifo_kick(struct gk104_fifo_chan *chan)
{
	struct gk104_fifo *fifo = chan->fifo;
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_client *client = chan->base.object.client;
	int ret = 0;

	mutex_lock(&subdev->mutex);
	nvkm_wr32(device, 0x002634, chan->base.chid);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x002634) & 0x00100000))
			break;
	) < 0) {
		nvkm_error(subdev, "channel %d [%s] kick timeout\n",
			   chan->base.chid, client->name);
		nvkm_fifo_recover_chan(&fifo->base, chan->base.chid);
		ret = -ETIMEDOUT;
	}
	mutex_unlock(&subdev->mutex);
	return ret;
}

static u32
gk104_fifo_gpfifo_engine_addr(struct nvkm_engine *engine)
{
	switch (engine->subdev.index) {
	case NVKM_ENGINE_SW    :
	case NVKM_ENGINE_CE0   :
	case NVKM_ENGINE_CE1   :
	case NVKM_ENGINE_CE2   : return 0x0000;
	case NVKM_ENGINE_GR    : return 0x0210;
	case NVKM_ENGINE_SEC   : return 0x0220;
	case NVKM_ENGINE_MSPDEC: return 0x0250;
	case NVKM_ENGINE_MSPPP : return 0x0260;
	case NVKM_ENGINE_MSVLD : return 0x0270;
	case NVKM_ENGINE_VIC   : return 0x0280;
	case NVKM_ENGINE_MSENC : return 0x0290;
	case NVKM_ENGINE_NVDEC : return 0x02100270;
	case NVKM_ENGINE_NVENC0: return 0x02100290;
	case NVKM_ENGINE_NVENC1: return 0x0210;
	default:
		WARN_ON(1);
		return 0;
	}
}

static int
gk104_fifo_gpfifo_engine_fini(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine, bool suspend)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	struct nvkm_gpuobj *inst = chan->base.inst;
	u32 offset = gk104_fifo_gpfifo_engine_addr(engine);
	int ret;

	ret = gk104_fifo_gpfifo_kick(chan);
	if (ret && suspend)
		return ret;

	if (offset) {
		nvkm_kmap(inst);
		nvkm_wo32(inst, (offset & 0xffff) + 0x00, 0x00000000);
		nvkm_wo32(inst, (offset & 0xffff) + 0x04, 0x00000000);
		if ((offset >>= 16)) {
			nvkm_wo32(inst, offset + 0x00, 0x00000000);
			nvkm_wo32(inst, offset + 0x04, 0x00000000);
		}
		nvkm_done(inst);
	}

	return ret;
}

static int
gk104_fifo_gpfifo_engine_init(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	struct nvkm_gpuobj *inst = chan->base.inst;
	u32 offset = gk104_fifo_gpfifo_engine_addr(engine);

	if (offset) {
		u64   addr = chan->engn[engine->subdev.index].vma.offset;
		u32 datalo = lower_32_bits(addr) | 0x00000004;
		u32 datahi = upper_32_bits(addr);
		nvkm_kmap(inst);
		nvkm_wo32(inst, (offset & 0xffff) + 0x00, datalo);
		nvkm_wo32(inst, (offset & 0xffff) + 0x04, datahi);
		if ((offset >>= 16)) {
			nvkm_wo32(inst, offset + 0x00, datalo);
			nvkm_wo32(inst, offset + 0x04, datahi);
		}
		nvkm_done(inst);
	}

	return 0;
}

static void
gk104_fifo_gpfifo_engine_dtor(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	nvkm_gpuobj_unmap(&chan->engn[engine->subdev.index].vma);
	nvkm_gpuobj_del(&chan->engn[engine->subdev.index].inst);
}

static int
gk104_fifo_gpfifo_engine_ctor(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine,
			      struct nvkm_object *object)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	int engn = engine->subdev.index;
	int ret;

	if (!gk104_fifo_gpfifo_engine_addr(engine))
		return 0;

	ret = nvkm_object_bind(object, NULL, 0, &chan->engn[engn].inst);
	if (ret)
		return ret;

	return nvkm_gpuobj_map(chan->engn[engn].inst, chan->vm,
			       NV_MEM_ACCESS_RW, &chan->engn[engn].vma);
}

static void
gk104_fifo_gpfifo_fini(struct nvkm_fifo_chan *base)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	struct gk104_fifo *fifo = chan->fifo;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 coff = chan->base.chid * 8;

	if (!list_empty(&chan->head)) {
		gk104_fifo_runlist_remove(fifo, chan);
		nvkm_mask(device, 0x800004 + coff, 0x00000800, 0x00000800);
		gk104_fifo_gpfifo_kick(chan);
		gk104_fifo_runlist_commit(fifo, chan->runl);
	}

	nvkm_wr32(device, 0x800000 + coff, 0x00000000);
}

static void
gk104_fifo_gpfifo_init(struct nvkm_fifo_chan *base)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	struct gk104_fifo *fifo = chan->fifo;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 addr = chan->base.inst->addr >> 12;
	u32 coff = chan->base.chid * 8;

	nvkm_mask(device, 0x800004 + coff, 0x000f0000, chan->runl << 16);
	nvkm_wr32(device, 0x800000 + coff, 0x80000000 | addr);

	if (list_empty(&chan->head) && !chan->killed) {
		gk104_fifo_runlist_insert(fifo, chan);
		nvkm_mask(device, 0x800004 + coff, 0x00000400, 0x00000400);
		gk104_fifo_runlist_commit(fifo, chan->runl);
		nvkm_mask(device, 0x800004 + coff, 0x00000400, 0x00000400);
	}
}

static void *
gk104_fifo_gpfifo_dtor(struct nvkm_fifo_chan *base)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	nvkm_vm_ref(NULL, &chan->vm, chan->pgd);
	nvkm_gpuobj_del(&chan->pgd);
	return chan;
}

static const struct nvkm_fifo_chan_func
gk104_fifo_gpfifo_func = {
	.dtor = gk104_fifo_gpfifo_dtor,
	.init = gk104_fifo_gpfifo_init,
	.fini = gk104_fifo_gpfifo_fini,
	.ntfy = gf100_fifo_chan_ntfy,
	.engine_ctor = gk104_fifo_gpfifo_engine_ctor,
	.engine_dtor = gk104_fifo_gpfifo_engine_dtor,
	.engine_init = gk104_fifo_gpfifo_engine_init,
	.engine_fini = gk104_fifo_gpfifo_engine_fini,
};

struct gk104_fifo_chan_func {
	u32 engine;
	u64 subdev;
};

static int
gk104_fifo_gpfifo_new_(const struct gk104_fifo_chan_func *func,
		       struct gk104_fifo *fifo, u32 *engmask, u16 *chid,
		       u64 vm, u64 ioffset, u64 ilength,
		       const struct nvkm_oclass *oclass,
		       struct nvkm_object **pobject)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct gk104_fifo_chan *chan;
	int runlist = -1, ret = -ENOSYS, i, j;
	u32 engines = 0, present = 0;
	u64 subdevs = 0;
	u64 usermem;

	/* Determine which downstream engines are present */
	for (i = 0; i < fifo->engine_nr; i++) {
		struct nvkm_engine *engine = fifo->engine[i].engine;
		if (engine) {
			u64 submask = BIT_ULL(engine->subdev.index);
			for (j = 0; func[j].subdev; j++) {
				if (func[j].subdev & submask) {
					present |= func[j].engine;
					break;
				}
			}

			if (!func[j].subdev)
				continue;

			if (runlist < 0 && (*engmask & present))
				runlist = fifo->engine[i].runl;
			if (runlist == fifo->engine[i].runl) {
				engines |= func[j].engine;
				subdevs |= func[j].subdev;
			}
		}
	}

	/* Just an engine mask query?  All done here! */
	if (!*engmask) {
		*engmask = present;
		return nvkm_object_new(oclass, NULL, 0, pobject);
	}

	/* No runlist?  No supported engines. */
	*engmask = present;
	if (runlist < 0)
		return -ENODEV;
	*engmask = engines;

	/* Allocate the channel. */
	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &chan->base.object;
	chan->fifo = fifo;
	chan->runl = runlist;
	INIT_LIST_HEAD(&chan->head);

	ret = nvkm_fifo_chan_ctor(&gk104_fifo_gpfifo_func, &fifo->base,
				  0x1000, 0x1000, true, vm, 0, subdevs,
				  1, fifo->user.bar.offset, 0x200,
				  oclass, &chan->base);
	if (ret)
		return ret;

	*chid = chan->base.chid;

	/* Page directory. */
	ret = nvkm_gpuobj_new(device, 0x10000, 0x1000, false, NULL, &chan->pgd);
	if (ret)
		return ret;

	nvkm_kmap(chan->base.inst);
	nvkm_wo32(chan->base.inst, 0x0200, lower_32_bits(chan->pgd->addr));
	nvkm_wo32(chan->base.inst, 0x0204, upper_32_bits(chan->pgd->addr));
	nvkm_wo32(chan->base.inst, 0x0208, 0xffffffff);
	nvkm_wo32(chan->base.inst, 0x020c, 0x000000ff);
	nvkm_done(chan->base.inst);

	ret = nvkm_vm_ref(chan->base.vm, &chan->vm, chan->pgd);
	if (ret)
		return ret;

	/* Clear channel control registers. */
	usermem = chan->base.chid * 0x200;
	ilength = order_base_2(ilength / 8);

	nvkm_kmap(fifo->user.mem);
	for (i = 0; i < 0x200; i += 4)
		nvkm_wo32(fifo->user.mem, usermem + i, 0x00000000);
	nvkm_done(fifo->user.mem);
	usermem = nvkm_memory_addr(fifo->user.mem) + usermem;

	/* RAMFC */
	nvkm_kmap(chan->base.inst);
	nvkm_wo32(chan->base.inst, 0x08, lower_32_bits(usermem));
	nvkm_wo32(chan->base.inst, 0x0c, upper_32_bits(usermem));
	nvkm_wo32(chan->base.inst, 0x10, 0x0000face);
	nvkm_wo32(chan->base.inst, 0x30, 0xfffff902);
	nvkm_wo32(chan->base.inst, 0x48, lower_32_bits(ioffset));
	nvkm_wo32(chan->base.inst, 0x4c, upper_32_bits(ioffset) |
					 (ilength << 16));
	nvkm_wo32(chan->base.inst, 0x84, 0x20400000);
	nvkm_wo32(chan->base.inst, 0x94, 0x30000001);
	nvkm_wo32(chan->base.inst, 0x9c, 0x00000100);
	nvkm_wo32(chan->base.inst, 0xac, 0x0000001f);
	nvkm_wo32(chan->base.inst, 0xe8, chan->base.chid);
	nvkm_wo32(chan->base.inst, 0xb8, 0xf8000000);
	nvkm_wo32(chan->base.inst, 0xf8, 0x10003080); /* 0x002310 */
	nvkm_wo32(chan->base.inst, 0xfc, 0x10000010); /* 0x002350 */
	nvkm_done(chan->base.inst);
	return 0;
}

static const struct gk104_fifo_chan_func
gk104_fifo_gpfifo[] = {
	{ NVA06F_V0_ENGINE_SW | NVA06F_V0_ENGINE_GR,
		BIT_ULL(NVKM_ENGINE_SW) | BIT_ULL(NVKM_ENGINE_GR)
	},
	{ NVA06F_V0_ENGINE_SEC   , BIT_ULL(NVKM_ENGINE_SEC   ) },
	{ NVA06F_V0_ENGINE_MSVLD , BIT_ULL(NVKM_ENGINE_MSVLD ) },
	{ NVA06F_V0_ENGINE_MSPDEC, BIT_ULL(NVKM_ENGINE_MSPDEC) },
	{ NVA06F_V0_ENGINE_MSPPP , BIT_ULL(NVKM_ENGINE_MSPPP ) },
	{ NVA06F_V0_ENGINE_MSENC , BIT_ULL(NVKM_ENGINE_MSENC ) },
	{ NVA06F_V0_ENGINE_VIC   , BIT_ULL(NVKM_ENGINE_VIC   ) },
	{ NVA06F_V0_ENGINE_NVDEC , BIT_ULL(NVKM_ENGINE_NVDEC ) },
	{ NVA06F_V0_ENGINE_NVENC0, BIT_ULL(NVKM_ENGINE_NVENC0) },
	{ NVA06F_V0_ENGINE_NVENC1, BIT_ULL(NVKM_ENGINE_NVENC1) },
	{ NVA06F_V0_ENGINE_CE0   , BIT_ULL(NVKM_ENGINE_CE0   ) },
	{ NVA06F_V0_ENGINE_CE1   , BIT_ULL(NVKM_ENGINE_CE1   ) },
	{ NVA06F_V0_ENGINE_CE2   , BIT_ULL(NVKM_ENGINE_CE2   ) },
	{}
};

int
gk104_fifo_gpfifo_new(struct nvkm_fifo *base, const struct nvkm_oclass *oclass,
		      void *data, u32 size, struct nvkm_object **pobject)
{
	struct nvkm_object *parent = oclass->parent;
	union {
		struct kepler_channel_gpfifo_a_v0 v0;
	} *args = data;
	struct gk104_fifo *fifo = gk104_fifo(base);
	int ret = -ENOSYS;

	nvif_ioctl(parent, "create channel gpfifo size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(parent, "create channel gpfifo vers %d vm %llx "
				   "ioffset %016llx ilength %08x engine %08x\n",
			   args->v0.version, args->v0.vm, args->v0.ioffset,
			   args->v0.ilength, args->v0.engines);
		return gk104_fifo_gpfifo_new_(gk104_fifo_gpfifo, fifo,
					      &args->v0.engines,
					      &args->v0.chid,
					       args->v0.vm,
					       args->v0.ioffset,
					       args->v0.ilength,
					      oclass, pobject);

	}

	return ret;
}

const struct nvkm_fifo_chan_oclass
gk104_fifo_gpfifo_oclass = {
	.base.oclass = KEPLER_CHANNEL_GPFIFO_A,
	.base.minver = 0,
	.base.maxver = 0,
	.ctor = gk104_fifo_gpfifo_new,
};
