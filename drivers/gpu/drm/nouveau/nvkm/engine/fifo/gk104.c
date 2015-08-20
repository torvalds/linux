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
#include "gk104.h"

#include <core/client.h>
#include <core/engctx.h>
#include <core/enum.h>
#include <core/handle.h>
#include <subdev/bar.h>
#include <subdev/fb.h>
#include <subdev/mmu.h>
#include <subdev/timer.h>

#include <nvif/class.h>
#include <nvif/ioctl.h>
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

struct gk104_fifo_engn {
	struct nvkm_memory *runlist[2];
	int cur_runlist;
	wait_queue_head_t wait;
};

struct gk104_fifo {
	struct nvkm_fifo base;

	struct work_struct fault;
	u64 mask;

	struct gk104_fifo_engn engine[FIFO_ENGINE_NR];
	struct {
		struct nvkm_memory *mem;
		struct nvkm_vma bar;
	} user;
	int spoon_nr;
};

struct gk104_fifo_base {
	struct nvkm_fifo_base base;
	struct nvkm_gpuobj *pgd;
	struct nvkm_vm *vm;
};

struct gk104_fifo_chan {
	struct nvkm_fifo_chan base;
	u32 engine;
	enum {
		STOPPED,
		RUNNING,
		KILLED
	} state;
};

/*******************************************************************************
 * FIFO channel objects
 ******************************************************************************/

static void
gk104_fifo_runlist_update(struct gk104_fifo *fifo, u32 engine)
{
	struct gk104_fifo_engn *engn = &fifo->engine[engine];
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_memory *cur;
	int i, p;

	mutex_lock(&nv_subdev(fifo)->mutex);
	cur = engn->runlist[engn->cur_runlist];
	engn->cur_runlist = !engn->cur_runlist;

	nvkm_kmap(cur);
	for (i = 0, p = 0; i < fifo->base.max; i++) {
		struct gk104_fifo_chan *chan = (void *)fifo->base.channel[i];
		if (chan && chan->state == RUNNING && chan->engine == engine) {
			nvkm_wo32(cur, p + 0, i);
			nvkm_wo32(cur, p + 4, 0x00000000);
			p += 8;
		}
	}
	nvkm_done(cur);

	nvkm_wr32(device, 0x002270, nvkm_memory_addr(cur) >> 12);
	nvkm_wr32(device, 0x002274, (engine << 20) | (p >> 3));

	if (wait_event_timeout(engn->wait, !(nvkm_rd32(device, 0x002284 +
			       (engine * 0x08)) & 0x00100000),
				msecs_to_jiffies(2000)) == 0)
		nvkm_error(subdev, "runlist %d update timeout\n", engine);
	mutex_unlock(&nv_subdev(fifo)->mutex);
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
		nvif_ioctl(parent, "create channel gpfifo vers %d "
				   "ioffset %016llx ilength %08x engine %08x\n",
			   args->v0.version, args->v0.ioffset,
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

static struct nvkm_oclass
gk104_fifo_sclass[] = {
	{ KEPLER_CHANNEL_GPFIFO_A, &gk104_fifo_chan_ofuncs },
	{}
};

/*******************************************************************************
 * FIFO context - instmem heap and vm setup
 ******************************************************************************/

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

static struct nvkm_oclass
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

/*******************************************************************************
 * PFIFO engine
 ******************************************************************************/

static inline int
gk104_fifo_engidx(struct gk104_fifo *fifo, u32 engn)
{
	switch (engn) {
	case NVDEV_ENGINE_GR    :
	case NVDEV_ENGINE_CE2   : engn = 0; break;
	case NVDEV_ENGINE_MSVLD : engn = 1; break;
	case NVDEV_ENGINE_MSPPP : engn = 2; break;
	case NVDEV_ENGINE_MSPDEC: engn = 3; break;
	case NVDEV_ENGINE_CE0   : engn = 4; break;
	case NVDEV_ENGINE_CE1   : engn = 5; break;
	case NVDEV_ENGINE_MSENC : engn = 6; break;
	default:
		return -1;
	}

	return engn;
}

static inline struct nvkm_engine *
gk104_fifo_engine(struct gk104_fifo *fifo, u32 engn)
{
	if (engn >= ARRAY_SIZE(fifo_engine))
		return NULL;
	return nvkm_engine(fifo, fifo_engine[engn].subdev);
}

static void
gk104_fifo_recover_work(struct work_struct *work)
{
	struct gk104_fifo *fifo = container_of(work, typeof(*fifo), fault);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_engine *engine;
	unsigned long flags;
	u32 engn, engm = 0;
	u64 mask, todo;

	spin_lock_irqsave(&fifo->base.lock, flags);
	mask = fifo->mask;
	fifo->mask = 0ULL;
	spin_unlock_irqrestore(&fifo->base.lock, flags);

	for (todo = mask; engn = __ffs64(todo), todo; todo &= ~(1 << engn))
		engm |= 1 << gk104_fifo_engidx(fifo, engn);
	nvkm_mask(device, 0x002630, engm, engm);

	for (todo = mask; engn = __ffs64(todo), todo; todo &= ~(1 << engn)) {
		if ((engine = nvkm_device_engine(device, engn))) {
			nvkm_subdev_fini(&engine->subdev, false);
			WARN_ON(nvkm_subdev_init(&engine->subdev));
		}
		gk104_fifo_runlist_update(fifo, gk104_fifo_engidx(fifo, engn));
	}

	nvkm_wr32(device, 0x00262c, engm);
	nvkm_mask(device, 0x002630, engm, 0x00000000);
}

static void
gk104_fifo_recover(struct gk104_fifo *fifo, struct nvkm_engine *engine,
		  struct gk104_fifo_chan *chan)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 chid = chan->base.chid;

	nvkm_error(subdev, "%s engine fault on channel %d, recovering...\n",
		   nvkm_subdev_name[nv_subdev(engine)->index], chid);
	assert_spin_locked(&fifo->base.lock);

	nvkm_mask(device, 0x800004 + (chid * 0x08), 0x00000800, 0x00000800);
	chan->state = KILLED;

	fifo->mask |= 1ULL << nv_engidx(engine);
	schedule_work(&fifo->fault);
}

static int
gk104_fifo_swmthd(struct gk104_fifo *fifo, u32 chid, u32 mthd, u32 data)
{
	struct gk104_fifo_chan *chan = NULL;
	struct nvkm_handle *bind;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&fifo->base.lock, flags);
	if (likely(chid >= fifo->base.min && chid <= fifo->base.max))
		chan = (void *)fifo->base.channel[chid];
	if (unlikely(!chan))
		goto out;

	bind = nvkm_namedb_get_class(nv_namedb(chan), NVIF_IOCTL_NEW_V0_SW_GF100);
	if (likely(bind)) {
		if (!mthd || !nv_call(bind->object, mthd, data))
			ret = 0;
		nvkm_namedb_put(bind);
	}

out:
	spin_unlock_irqrestore(&fifo->base.lock, flags);
	return ret;
}

static const struct nvkm_enum
gk104_fifo_bind_reason[] = {
	{ 0x01, "BIND_NOT_UNBOUND" },
	{ 0x02, "SNOOP_WITHOUT_BAR1" },
	{ 0x03, "UNBIND_WHILE_RUNNING" },
	{ 0x05, "INVALID_RUNLIST" },
	{ 0x06, "INVALID_CTX_TGT" },
	{ 0x0b, "UNBIND_WHILE_PARKED" },
	{}
};

static void
gk104_fifo_intr_bind(struct gk104_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 intr = nvkm_rd32(device, 0x00252c);
	u32 code = intr & 0x000000ff;
	const struct nvkm_enum *en =
		nvkm_enum_find(gk104_fifo_bind_reason, code);

	nvkm_error(subdev, "BIND_ERROR %02x [%s]\n", code, en ? en->name : "");
}

static const struct nvkm_enum
gk104_fifo_sched_reason[] = {
	{ 0x0a, "CTXSW_TIMEOUT" },
	{}
};

static void
gk104_fifo_intr_sched_ctxsw(struct gk104_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_engine *engine;
	struct gk104_fifo_chan *chan;
	unsigned long flags;
	u32 engn;

	spin_lock_irqsave(&fifo->base.lock, flags);
	for (engn = 0; engn < ARRAY_SIZE(fifo_engine); engn++) {
		u32 stat = nvkm_rd32(device, 0x002640 + (engn * 0x04));
		u32 busy = (stat & 0x80000000);
		u32 next = (stat & 0x07ff0000) >> 16;
		u32 chsw = (stat & 0x00008000);
		u32 save = (stat & 0x00004000);
		u32 load = (stat & 0x00002000);
		u32 prev = (stat & 0x000007ff);
		u32 chid = load ? next : prev;
		(void)save;

		if (busy && chsw) {
			if (!(chan = (void *)fifo->base.channel[chid]))
				continue;
			if (!(engine = gk104_fifo_engine(fifo, engn)))
				continue;
			gk104_fifo_recover(fifo, engine, chan);
		}
	}
	spin_unlock_irqrestore(&fifo->base.lock, flags);
}

static void
gk104_fifo_intr_sched(struct gk104_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 intr = nvkm_rd32(device, 0x00254c);
	u32 code = intr & 0x000000ff;
	const struct nvkm_enum *en =
		nvkm_enum_find(gk104_fifo_sched_reason, code);

	nvkm_error(subdev, "SCHED_ERROR %02x [%s]\n", code, en ? en->name : "");

	switch (code) {
	case 0x0a:
		gk104_fifo_intr_sched_ctxsw(fifo);
		break;
	default:
		break;
	}
}

static void
gk104_fifo_intr_chsw(struct gk104_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x00256c);
	nvkm_error(subdev, "CHSW_ERROR %08x\n", stat);
	nvkm_wr32(device, 0x00256c, stat);
}

static void
gk104_fifo_intr_dropped_fault(struct gk104_fifo *fifo)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x00259c);
	nvkm_error(subdev, "DROPPED_MMU_FAULT %08x\n", stat);
}

static const struct nvkm_enum
gk104_fifo_fault_engine[] = {
	{ 0x00, "GR", NULL, NVDEV_ENGINE_GR },
	{ 0x03, "IFB", NULL, NVDEV_ENGINE_IFB },
	{ 0x04, "BAR1", NULL, NVDEV_SUBDEV_BAR },
	{ 0x05, "BAR3", NULL, NVDEV_SUBDEV_INSTMEM },
	{ 0x07, "PBDMA0", NULL, NVDEV_ENGINE_FIFO },
	{ 0x08, "PBDMA1", NULL, NVDEV_ENGINE_FIFO },
	{ 0x09, "PBDMA2", NULL, NVDEV_ENGINE_FIFO },
	{ 0x10, "MSVLD", NULL, NVDEV_ENGINE_MSVLD },
	{ 0x11, "MSPPP", NULL, NVDEV_ENGINE_MSPPP },
	{ 0x13, "PERF" },
	{ 0x14, "MSPDEC", NULL, NVDEV_ENGINE_MSPDEC },
	{ 0x15, "CE0", NULL, NVDEV_ENGINE_CE0 },
	{ 0x16, "CE1", NULL, NVDEV_ENGINE_CE1 },
	{ 0x17, "PMU" },
	{ 0x19, "MSENC", NULL, NVDEV_ENGINE_MSENC },
	{ 0x1b, "CE2", NULL, NVDEV_ENGINE_CE2 },
	{}
};

static const struct nvkm_enum
gk104_fifo_fault_reason[] = {
	{ 0x00, "PDE" },
	{ 0x01, "PDE_SIZE" },
	{ 0x02, "PTE" },
	{ 0x03, "VA_LIMIT_VIOLATION" },
	{ 0x04, "UNBOUND_INST_BLOCK" },
	{ 0x05, "PRIV_VIOLATION" },
	{ 0x06, "RO_VIOLATION" },
	{ 0x07, "WO_VIOLATION" },
	{ 0x08, "PITCH_MASK_VIOLATION" },
	{ 0x09, "WORK_CREATION" },
	{ 0x0a, "UNSUPPORTED_APERTURE" },
	{ 0x0b, "COMPRESSION_FAILURE" },
	{ 0x0c, "UNSUPPORTED_KIND" },
	{ 0x0d, "REGION_VIOLATION" },
	{ 0x0e, "BOTH_PTES_VALID" },
	{ 0x0f, "INFO_TYPE_POISONED" },
	{}
};

static const struct nvkm_enum
gk104_fifo_fault_hubclient[] = {
	{ 0x00, "VIP" },
	{ 0x01, "CE0" },
	{ 0x02, "CE1" },
	{ 0x03, "DNISO" },
	{ 0x04, "FE" },
	{ 0x05, "FECS" },
	{ 0x06, "HOST" },
	{ 0x07, "HOST_CPU" },
	{ 0x08, "HOST_CPU_NB" },
	{ 0x09, "ISO" },
	{ 0x0a, "MMU" },
	{ 0x0b, "MSPDEC" },
	{ 0x0c, "MSPPP" },
	{ 0x0d, "MSVLD" },
	{ 0x0e, "NISO" },
	{ 0x0f, "P2P" },
	{ 0x10, "PD" },
	{ 0x11, "PERF" },
	{ 0x12, "PMU" },
	{ 0x13, "RASTERTWOD" },
	{ 0x14, "SCC" },
	{ 0x15, "SCC_NB" },
	{ 0x16, "SEC" },
	{ 0x17, "SSYNC" },
	{ 0x18, "GR_CE" },
	{ 0x19, "CE2" },
	{ 0x1a, "XV" },
	{ 0x1b, "MMU_NB" },
	{ 0x1c, "MSENC" },
	{ 0x1d, "DFALCON" },
	{ 0x1e, "SKED" },
	{ 0x1f, "AFALCON" },
	{}
};

static const struct nvkm_enum
gk104_fifo_fault_gpcclient[] = {
	{ 0x00, "L1_0" }, { 0x01, "T1_0" }, { 0x02, "PE_0" },
	{ 0x03, "L1_1" }, { 0x04, "T1_1" }, { 0x05, "PE_1" },
	{ 0x06, "L1_2" }, { 0x07, "T1_2" }, { 0x08, "PE_2" },
	{ 0x09, "L1_3" }, { 0x0a, "T1_3" }, { 0x0b, "PE_3" },
	{ 0x0c, "RAST" },
	{ 0x0d, "GCC" },
	{ 0x0e, "GPCCS" },
	{ 0x0f, "PROP_0" },
	{ 0x10, "PROP_1" },
	{ 0x11, "PROP_2" },
	{ 0x12, "PROP_3" },
	{ 0x13, "L1_4" }, { 0x14, "T1_4" }, { 0x15, "PE_4" },
	{ 0x16, "L1_5" }, { 0x17, "T1_5" }, { 0x18, "PE_5" },
	{ 0x19, "L1_6" }, { 0x1a, "T1_6" }, { 0x1b, "PE_6" },
	{ 0x1c, "L1_7" }, { 0x1d, "T1_7" }, { 0x1e, "PE_7" },
	{ 0x1f, "GPM" },
	{ 0x20, "LTP_UTLB_0" },
	{ 0x21, "LTP_UTLB_1" },
	{ 0x22, "LTP_UTLB_2" },
	{ 0x23, "LTP_UTLB_3" },
	{ 0x24, "GPC_RGG_UTLB" },
	{}
};

static void
gk104_fifo_intr_fault(struct gk104_fifo *fifo, int unit)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 inst = nvkm_rd32(device, 0x002800 + (unit * 0x10));
	u32 valo = nvkm_rd32(device, 0x002804 + (unit * 0x10));
	u32 vahi = nvkm_rd32(device, 0x002808 + (unit * 0x10));
	u32 stat = nvkm_rd32(device, 0x00280c + (unit * 0x10));
	u32 gpc    = (stat & 0x1f000000) >> 24;
	u32 client = (stat & 0x00001f00) >> 8;
	u32 write  = (stat & 0x00000080);
	u32 hub    = (stat & 0x00000040);
	u32 reason = (stat & 0x0000000f);
	const struct nvkm_enum *er, *eu, *ec;
	struct nvkm_engine *engine = NULL;
	struct nvkm_fifo_chan *chan;
	unsigned long flags;
	char gpcid[8] = "";

	er = nvkm_enum_find(gk104_fifo_fault_reason, reason);
	eu = nvkm_enum_find(gk104_fifo_fault_engine, unit);
	if (hub) {
		ec = nvkm_enum_find(gk104_fifo_fault_hubclient, client);
	} else {
		ec = nvkm_enum_find(gk104_fifo_fault_gpcclient, client);
		snprintf(gpcid, sizeof(gpcid), "GPC%d/", gpc);
	}

	if (eu) {
		switch (eu->data2) {
		case NVDEV_SUBDEV_BAR:
			nvkm_mask(device, 0x001704, 0x00000000, 0x00000000);
			break;
		case NVDEV_SUBDEV_INSTMEM:
			nvkm_mask(device, 0x001714, 0x00000000, 0x00000000);
			break;
		case NVDEV_ENGINE_IFB:
			nvkm_mask(device, 0x001718, 0x00000000, 0x00000000);
			break;
		default:
			engine = nvkm_engine(fifo, eu->data2);
			break;
		}
	}

	chan = nvkm_fifo_chan_inst(&fifo->base, (u64)inst << 12, &flags);

	nvkm_error(subdev,
		   "%s fault at %010llx engine %02x [%s] client %02x [%s%s] "
		   "reason %02x [%s] on channel %d [%010llx %s]\n",
		   write ? "write" : "read", (u64)vahi << 32 | valo,
		   unit, eu ? eu->name : "", client, gpcid, ec ? ec->name : "",
		   reason, er ? er->name : "", chan ? chan->chid : -1,
		   (u64)inst << 12, nvkm_client_name(chan));

	if (engine && chan)
		gk104_fifo_recover(fifo, engine, (void *)chan);
	nvkm_fifo_chan_put(&fifo->base, flags, &chan);
}

static const struct nvkm_bitfield gk104_fifo_pbdma_intr_0[] = {
	{ 0x00000001, "MEMREQ" },
	{ 0x00000002, "MEMACK_TIMEOUT" },
	{ 0x00000004, "MEMACK_EXTRA" },
	{ 0x00000008, "MEMDAT_TIMEOUT" },
	{ 0x00000010, "MEMDAT_EXTRA" },
	{ 0x00000020, "MEMFLUSH" },
	{ 0x00000040, "MEMOP" },
	{ 0x00000080, "LBCONNECT" },
	{ 0x00000100, "LBREQ" },
	{ 0x00000200, "LBACK_TIMEOUT" },
	{ 0x00000400, "LBACK_EXTRA" },
	{ 0x00000800, "LBDAT_TIMEOUT" },
	{ 0x00001000, "LBDAT_EXTRA" },
	{ 0x00002000, "GPFIFO" },
	{ 0x00004000, "GPPTR" },
	{ 0x00008000, "GPENTRY" },
	{ 0x00010000, "GPCRC" },
	{ 0x00020000, "PBPTR" },
	{ 0x00040000, "PBENTRY" },
	{ 0x00080000, "PBCRC" },
	{ 0x00100000, "XBARCONNECT" },
	{ 0x00200000, "METHOD" },
	{ 0x00400000, "METHODCRC" },
	{ 0x00800000, "DEVICE" },
	{ 0x02000000, "SEMAPHORE" },
	{ 0x04000000, "ACQUIRE" },
	{ 0x08000000, "PRI" },
	{ 0x20000000, "NO_CTXSW_SEG" },
	{ 0x40000000, "PBSEG" },
	{ 0x80000000, "SIGNATURE" },
	{}
};

static void
gk104_fifo_intr_pbdma_0(struct gk104_fifo *fifo, int unit)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mask = nvkm_rd32(device, 0x04010c + (unit * 0x2000));
	u32 stat = nvkm_rd32(device, 0x040108 + (unit * 0x2000)) & mask;
	u32 addr = nvkm_rd32(device, 0x0400c0 + (unit * 0x2000));
	u32 data = nvkm_rd32(device, 0x0400c4 + (unit * 0x2000));
	u32 chid = nvkm_rd32(device, 0x040120 + (unit * 0x2000)) & 0xfff;
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00003ffc);
	u32 show = stat;
	char msg[128];

	if (stat & 0x00800000) {
		if (!gk104_fifo_swmthd(fifo, chid, mthd, data))
			show &= ~0x00800000;
		nvkm_wr32(device, 0x0400c0 + (unit * 0x2000), 0x80600008);
	}

	if (show) {
		nvkm_snprintbf(msg, sizeof(msg), gk104_fifo_pbdma_intr_0, show);
		nvkm_error(subdev, "PBDMA%d: %08x [%s] ch %d [%s] subc %d "
				   "mthd %04x data %08x\n",
			   unit, show, msg, chid,
			   nvkm_client_name_for_fifo_chid(&fifo->base, chid),
			   subc, mthd, data);
	}

	nvkm_wr32(device, 0x040108 + (unit * 0x2000), stat);
}

static const struct nvkm_bitfield gk104_fifo_pbdma_intr_1[] = {
	{ 0x00000001, "HCE_RE_ILLEGAL_OP" },
	{ 0x00000002, "HCE_RE_ALIGNB" },
	{ 0x00000004, "HCE_PRIV" },
	{ 0x00000008, "HCE_ILLEGAL_MTHD" },
	{ 0x00000010, "HCE_ILLEGAL_CLASS" },
	{}
};

static void
gk104_fifo_intr_pbdma_1(struct gk104_fifo *fifo, int unit)
{
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mask = nvkm_rd32(device, 0x04014c + (unit * 0x2000));
	u32 stat = nvkm_rd32(device, 0x040148 + (unit * 0x2000)) & mask;
	u32 chid = nvkm_rd32(device, 0x040120 + (unit * 0x2000)) & 0xfff;
	char msg[128];

	if (stat) {
		nvkm_snprintbf(msg, sizeof(msg), gk104_fifo_pbdma_intr_1, stat);
		nvkm_error(subdev, "PBDMA%d: %08x [%s] ch %d %08x %08x\n",
			   unit, stat, msg, chid,
			   nvkm_rd32(device, 0x040150 + (unit * 0x2000)),
			   nvkm_rd32(device, 0x040154 + (unit * 0x2000)));
	}

	nvkm_wr32(device, 0x040148 + (unit * 0x2000), stat);
}

static void
gk104_fifo_intr_runlist(struct gk104_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 mask = nvkm_rd32(device, 0x002a00);
	while (mask) {
		u32 engn = __ffs(mask);
		wake_up(&fifo->engine[engn].wait);
		nvkm_wr32(device, 0x002a00, 1 << engn);
		mask &= ~(1 << engn);
	}
}

static void
gk104_fifo_intr_engine(struct gk104_fifo *fifo)
{
	nvkm_fifo_uevent(&fifo->base);
}

static void
gk104_fifo_intr(struct nvkm_subdev *subdev)
{
	struct gk104_fifo *fifo = (void *)subdev;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 mask = nvkm_rd32(device, 0x002140);
	u32 stat = nvkm_rd32(device, 0x002100) & mask;

	if (stat & 0x00000001) {
		gk104_fifo_intr_bind(fifo);
		nvkm_wr32(device, 0x002100, 0x00000001);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000010) {
		nvkm_error(subdev, "PIO_ERROR\n");
		nvkm_wr32(device, 0x002100, 0x00000010);
		stat &= ~0x00000010;
	}

	if (stat & 0x00000100) {
		gk104_fifo_intr_sched(fifo);
		nvkm_wr32(device, 0x002100, 0x00000100);
		stat &= ~0x00000100;
	}

	if (stat & 0x00010000) {
		gk104_fifo_intr_chsw(fifo);
		nvkm_wr32(device, 0x002100, 0x00010000);
		stat &= ~0x00010000;
	}

	if (stat & 0x00800000) {
		nvkm_error(subdev, "FB_FLUSH_TIMEOUT\n");
		nvkm_wr32(device, 0x002100, 0x00800000);
		stat &= ~0x00800000;
	}

	if (stat & 0x01000000) {
		nvkm_error(subdev, "LB_ERROR\n");
		nvkm_wr32(device, 0x002100, 0x01000000);
		stat &= ~0x01000000;
	}

	if (stat & 0x08000000) {
		gk104_fifo_intr_dropped_fault(fifo);
		nvkm_wr32(device, 0x002100, 0x08000000);
		stat &= ~0x08000000;
	}

	if (stat & 0x10000000) {
		u32 mask = nvkm_rd32(device, 0x00259c);
		while (mask) {
			u32 unit = __ffs(mask);
			gk104_fifo_intr_fault(fifo, unit);
			nvkm_wr32(device, 0x00259c, (1 << unit));
			mask &= ~(1 << unit);
		}
		stat &= ~0x10000000;
	}

	if (stat & 0x20000000) {
		u32 mask = nvkm_rd32(device, 0x0025a0);
		while (mask) {
			u32 unit = __ffs(mask);
			gk104_fifo_intr_pbdma_0(fifo, unit);
			gk104_fifo_intr_pbdma_1(fifo, unit);
			nvkm_wr32(device, 0x0025a0, (1 << unit));
			mask &= ~(1 << unit);
		}
		stat &= ~0x20000000;
	}

	if (stat & 0x40000000) {
		gk104_fifo_intr_runlist(fifo);
		stat &= ~0x40000000;
	}

	if (stat & 0x80000000) {
		nvkm_wr32(device, 0x002100, 0x80000000);
		gk104_fifo_intr_engine(fifo);
		stat &= ~0x80000000;
	}

	if (stat) {
		nvkm_error(subdev, "INTR %08x\n", stat);
		nvkm_mask(device, 0x002140, stat, 0x00000000);
		nvkm_wr32(device, 0x002100, stat);
	}
}

static void
gk104_fifo_uevent_init(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), uevent);
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x80000000, 0x80000000);
}

static void
gk104_fifo_uevent_fini(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), uevent);
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x80000000, 0x00000000);
}

static const struct nvkm_event_func
gk104_fifo_uevent_func = {
	.ctor = nvkm_fifo_uevent_ctor,
	.init = gk104_fifo_uevent_init,
	.fini = gk104_fifo_uevent_fini,
};

int
gk104_fifo_fini(struct nvkm_object *object, bool suspend)
{
	struct gk104_fifo *fifo = (void *)object;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	int ret;

	ret = nvkm_fifo_fini(&fifo->base, suspend);
	if (ret)
		return ret;

	/* allow mmu fault interrupts, even when we're not using fifo */
	nvkm_mask(device, 0x002140, 0x10000000, 0x10000000);
	return 0;
}

int
gk104_fifo_init(struct nvkm_object *object)
{
	struct gk104_fifo *fifo = (void *)object;
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ret, i;

	ret = nvkm_fifo_init(&fifo->base);
	if (ret)
		return ret;

	/* enable all available PBDMA units */
	nvkm_wr32(device, 0x000204, 0xffffffff);
	fifo->spoon_nr = hweight32(nvkm_rd32(device, 0x000204));
	nvkm_debug(subdev, "%d PBDMA unit(s)\n", fifo->spoon_nr);

	/* PBDMA[n] */
	for (i = 0; i < fifo->spoon_nr; i++) {
		nvkm_mask(device, 0x04013c + (i * 0x2000), 0x10000100, 0x00000000);
		nvkm_wr32(device, 0x040108 + (i * 0x2000), 0xffffffff); /* INTR */
		nvkm_wr32(device, 0x04010c + (i * 0x2000), 0xfffffeff); /* INTREN */
	}

	/* PBDMA[n].HCE */
	for (i = 0; i < fifo->spoon_nr; i++) {
		nvkm_wr32(device, 0x040148 + (i * 0x2000), 0xffffffff); /* INTR */
		nvkm_wr32(device, 0x04014c + (i * 0x2000), 0xffffffff); /* INTREN */
	}

	nvkm_wr32(device, 0x002254, 0x10000000 | fifo->user.bar.offset >> 12);

	nvkm_wr32(device, 0x002100, 0xffffffff);
	nvkm_wr32(device, 0x002140, 0x7fffffff);
	return 0;
}

void
gk104_fifo_dtor(struct nvkm_object *object)
{
	struct gk104_fifo *fifo = (void *)object;
	int i;

	nvkm_vm_put(&fifo->user.bar);
	nvkm_memory_del(&fifo->user.mem);

	for (i = 0; i < FIFO_ENGINE_NR; i++) {
		nvkm_memory_del(&fifo->engine[i].runlist[1]);
		nvkm_memory_del(&fifo->engine[i].runlist[0]);
	}

	nvkm_fifo_destroy(&fifo->base);
}

int
gk104_fifo_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct nvkm_device *device = (void *)parent;
	struct nvkm_bar *bar = device->bar;
	struct gk104_fifo_impl *impl = (void *)oclass;
	struct gk104_fifo *fifo;
	int ret, i;

	ret = nvkm_fifo_create(parent, engine, oclass, 0,
			       impl->channels - 1, &fifo);
	*pobject = nv_object(fifo);
	if (ret)
		return ret;

	INIT_WORK(&fifo->fault, gk104_fifo_recover_work);

	for (i = 0; i < FIFO_ENGINE_NR; i++) {
		ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
				      0x8000, 0x1000, false,
				      &fifo->engine[i].runlist[0]);
		if (ret)
			return ret;

		ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
				      0x8000, 0x1000, false,
				      &fifo->engine[i].runlist[1]);
		if (ret)
			return ret;

		init_waitqueue_head(&fifo->engine[i].wait);
	}

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
			      impl->channels * 0x200, 0x1000,
			      true, &fifo->user.mem);
	if (ret)
		return ret;

	ret = bar->umap(bar, impl->channels * 0x200, 12, &fifo->user.bar);
	if (ret)
		return ret;

	nvkm_memory_map(fifo->user.mem, &fifo->user.bar, 0);

	ret = nvkm_event_init(&gk104_fifo_uevent_func, 1, 1, &fifo->base.uevent);
	if (ret)
		return ret;

	nv_subdev(fifo)->unit = 0x00000100;
	nv_subdev(fifo)->intr = gk104_fifo_intr;
	nv_engine(fifo)->cclass = &gk104_fifo_cclass;
	nv_engine(fifo)->sclass = gk104_fifo_sclass;
	return 0;
}

struct nvkm_oclass *
gk104_fifo_oclass = &(struct gk104_fifo_impl) {
	.base.handle = NV_ENGINE(FIFO, 0xe0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gk104_fifo_ctor,
		.dtor = gk104_fifo_dtor,
		.init = gk104_fifo_init,
		.fini = gk104_fifo_fini,
	},
	.channels = 4096,
}.base;
