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
#include <core/handle.h>
#include <core/namedb.h>
#include <core/gpuobj.h>
#include <core/engctx.h>
#include <core/event.h>
#include <nvif/unpack.h>
#include <nvif/class.h>
#include <core/enum.h>

#include <subdev/timer.h>
#include <subdev/bar.h>
#include <subdev/fb.h>
#include <subdev/vm.h>

#include <engine/dmaobj.h>
#include <engine/fifo.h>

struct nvc0_fifo_priv {
	struct nouveau_fifo base;

	struct work_struct fault;
	u64 mask;

	struct {
		struct nouveau_gpuobj *mem[2];
		int active;
		wait_queue_head_t wait;
	} runlist;

	struct {
		struct nouveau_gpuobj *mem;
		struct nouveau_vma bar;
	} user;
	int spoon_nr;
};

struct nvc0_fifo_base {
	struct nouveau_fifo_base base;
	struct nouveau_gpuobj *pgd;
	struct nouveau_vm *vm;
};

struct nvc0_fifo_chan {
	struct nouveau_fifo_chan base;
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
nvc0_fifo_runlist_update(struct nvc0_fifo_priv *priv)
{
	struct nouveau_bar *bar = nouveau_bar(priv);
	struct nouveau_gpuobj *cur;
	int i, p;

	mutex_lock(&nv_subdev(priv)->mutex);
	cur = priv->runlist.mem[priv->runlist.active];
	priv->runlist.active = !priv->runlist.active;

	for (i = 0, p = 0; i < 128; i++) {
		struct nvc0_fifo_chan *chan = (void *)priv->base.channel[i];
		if (chan && chan->state == RUNNING) {
			nv_wo32(cur, p + 0, i);
			nv_wo32(cur, p + 4, 0x00000004);
			p += 8;
		}
	}
	bar->flush(bar);

	nv_wr32(priv, 0x002270, cur->addr >> 12);
	nv_wr32(priv, 0x002274, 0x01f00000 | (p >> 3));

	if (wait_event_timeout(priv->runlist.wait,
			       !(nv_rd32(priv, 0x00227c) & 0x00100000),
			       msecs_to_jiffies(2000)) == 0)
		nv_error(priv, "runlist update timeout\n");
	mutex_unlock(&nv_subdev(priv)->mutex);
}

static int
nvc0_fifo_context_attach(struct nouveau_object *parent,
			 struct nouveau_object *object)
{
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nvc0_fifo_base *base = (void *)parent->parent;
	struct nouveau_engctx *ectx = (void *)object;
	u32 addr;
	int ret;

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_SW   : return 0;
	case NVDEV_ENGINE_GR   : addr = 0x0210; break;
	case NVDEV_ENGINE_COPY0: addr = 0x0230; break;
	case NVDEV_ENGINE_COPY1: addr = 0x0240; break;
	case NVDEV_ENGINE_BSP  : addr = 0x0270; break;
	case NVDEV_ENGINE_VP   : addr = 0x0250; break;
	case NVDEV_ENGINE_PPP  : addr = 0x0260; break;
	default:
		return -EINVAL;
	}

	if (!ectx->vma.node) {
		ret = nouveau_gpuobj_map_vm(nv_gpuobj(ectx), base->vm,
					    NV_MEM_ACCESS_RW, &ectx->vma);
		if (ret)
			return ret;

		nv_engctx(ectx)->addr = nv_gpuobj(base)->addr >> 12;
	}

	nv_wo32(base, addr + 0x00, lower_32_bits(ectx->vma.offset) | 4);
	nv_wo32(base, addr + 0x04, upper_32_bits(ectx->vma.offset));
	bar->flush(bar);
	return 0;
}

static int
nvc0_fifo_context_detach(struct nouveau_object *parent, bool suspend,
			 struct nouveau_object *object)
{
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nvc0_fifo_priv *priv = (void *)parent->engine;
	struct nvc0_fifo_base *base = (void *)parent->parent;
	struct nvc0_fifo_chan *chan = (void *)parent;
	u32 addr;

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_SW   : return 0;
	case NVDEV_ENGINE_GR   : addr = 0x0210; break;
	case NVDEV_ENGINE_COPY0: addr = 0x0230; break;
	case NVDEV_ENGINE_COPY1: addr = 0x0240; break;
	case NVDEV_ENGINE_BSP  : addr = 0x0270; break;
	case NVDEV_ENGINE_VP   : addr = 0x0250; break;
	case NVDEV_ENGINE_PPP  : addr = 0x0260; break;
	default:
		return -EINVAL;
	}

	nv_wr32(priv, 0x002634, chan->base.chid);
	if (!nv_wait(priv, 0x002634, 0xffffffff, chan->base.chid)) {
		nv_error(priv, "channel %d [%s] kick timeout\n",
			 chan->base.chid, nouveau_client_name(chan));
		if (suspend)
			return -EBUSY;
	}

	nv_wo32(base, addr + 0x00, 0x00000000);
	nv_wo32(base, addr + 0x04, 0x00000000);
	bar->flush(bar);
	return 0;
}

static int
nvc0_fifo_chan_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	union {
		struct nv50_channel_gpfifo_v0 v0;
	} *args = data;
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nvc0_fifo_priv *priv = (void *)engine;
	struct nvc0_fifo_base *base = (void *)parent;
	struct nvc0_fifo_chan *chan;
	u64 usermem, ioffset, ilength;
	int ret, i;

	nv_ioctl(parent, "create channel gpfifo size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create channel gpfifo vers %d pushbuf %08x "
				 "ioffset %016llx ilength %08x\n",
			 args->v0.version, args->v0.pushbuf, args->v0.ioffset,
			 args->v0.ilength);
	} else
		return ret;

	ret = nouveau_fifo_channel_create(parent, engine, oclass, 1,
					  priv->user.bar.offset, 0x1000,
					  args->v0.pushbuf,
					  (1ULL << NVDEV_ENGINE_SW) |
					  (1ULL << NVDEV_ENGINE_GR) |
					  (1ULL << NVDEV_ENGINE_COPY0) |
					  (1ULL << NVDEV_ENGINE_COPY1) |
					  (1ULL << NVDEV_ENGINE_BSP) |
					  (1ULL << NVDEV_ENGINE_VP) |
					  (1ULL << NVDEV_ENGINE_PPP), &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	args->v0.chid = chan->base.chid;

	nv_parent(chan)->context_attach = nvc0_fifo_context_attach;
	nv_parent(chan)->context_detach = nvc0_fifo_context_detach;

	usermem = chan->base.chid * 0x1000;
	ioffset = args->v0.ioffset;
	ilength = order_base_2(args->v0.ilength / 8);

	for (i = 0; i < 0x1000; i += 4)
		nv_wo32(priv->user.mem, usermem + i, 0x00000000);

	nv_wo32(base, 0x08, lower_32_bits(priv->user.mem->addr + usermem));
	nv_wo32(base, 0x0c, upper_32_bits(priv->user.mem->addr + usermem));
	nv_wo32(base, 0x10, 0x0000face);
	nv_wo32(base, 0x30, 0xfffff902);
	nv_wo32(base, 0x48, lower_32_bits(ioffset));
	nv_wo32(base, 0x4c, upper_32_bits(ioffset) | (ilength << 16));
	nv_wo32(base, 0x54, 0x00000002);
	nv_wo32(base, 0x84, 0x20400000);
	nv_wo32(base, 0x94, 0x30000001);
	nv_wo32(base, 0x9c, 0x00000100);
	nv_wo32(base, 0xa4, 0x1f1f1f1f);
	nv_wo32(base, 0xa8, 0x1f1f1f1f);
	nv_wo32(base, 0xac, 0x0000001f);
	nv_wo32(base, 0xb8, 0xf8000000);
	nv_wo32(base, 0xf8, 0x10003080); /* 0x002310 */
	nv_wo32(base, 0xfc, 0x10000010); /* 0x002350 */
	bar->flush(bar);
	return 0;
}

static int
nvc0_fifo_chan_init(struct nouveau_object *object)
{
	struct nouveau_gpuobj *base = nv_gpuobj(object->parent);
	struct nvc0_fifo_priv *priv = (void *)object->engine;
	struct nvc0_fifo_chan *chan = (void *)object;
	u32 chid = chan->base.chid;
	int ret;

	ret = nouveau_fifo_channel_init(&chan->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x003000 + (chid * 8), 0xc0000000 | base->addr >> 12);

	if (chan->state == STOPPED && (chan->state = RUNNING) == RUNNING) {
		nv_wr32(priv, 0x003004 + (chid * 8), 0x001f0001);
		nvc0_fifo_runlist_update(priv);
	}

	return 0;
}

static void nvc0_fifo_intr_engine(struct nvc0_fifo_priv *priv);

static int
nvc0_fifo_chan_fini(struct nouveau_object *object, bool suspend)
{
	struct nvc0_fifo_priv *priv = (void *)object->engine;
	struct nvc0_fifo_chan *chan = (void *)object;
	u32 chid = chan->base.chid;

	if (chan->state == RUNNING && (chan->state = STOPPED) == STOPPED) {
		nv_mask(priv, 0x003004 + (chid * 8), 0x00000001, 0x00000000);
		nvc0_fifo_runlist_update(priv);
	}

	nvc0_fifo_intr_engine(priv);

	nv_wr32(priv, 0x003000 + (chid * 8), 0x00000000);
	return nouveau_fifo_channel_fini(&chan->base, suspend);
}

static struct nouveau_ofuncs
nvc0_fifo_ofuncs = {
	.ctor = nvc0_fifo_chan_ctor,
	.dtor = _nouveau_fifo_channel_dtor,
	.init = nvc0_fifo_chan_init,
	.fini = nvc0_fifo_chan_fini,
	.map  = _nouveau_fifo_channel_map,
	.rd32 = _nouveau_fifo_channel_rd32,
	.wr32 = _nouveau_fifo_channel_wr32,
	.ntfy = _nouveau_fifo_channel_ntfy
};

static struct nouveau_oclass
nvc0_fifo_sclass[] = {
	{ FERMI_CHANNEL_GPFIFO, &nvc0_fifo_ofuncs },
	{}
};

/*******************************************************************************
 * FIFO context - instmem heap and vm setup
 ******************************************************************************/

static int
nvc0_fifo_context_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nvc0_fifo_base *base;
	int ret;

	ret = nouveau_fifo_context_create(parent, engine, oclass, NULL, 0x1000,
				          0x1000, NVOBJ_FLAG_ZERO_ALLOC |
					  NVOBJ_FLAG_HEAP, &base);
	*pobject = nv_object(base);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(base), NULL, 0x10000, 0x1000, 0,
				&base->pgd);
	if (ret)
		return ret;

	nv_wo32(base, 0x0200, lower_32_bits(base->pgd->addr));
	nv_wo32(base, 0x0204, upper_32_bits(base->pgd->addr));
	nv_wo32(base, 0x0208, 0xffffffff);
	nv_wo32(base, 0x020c, 0x000000ff);

	ret = nouveau_vm_ref(nouveau_client(parent)->vm, &base->vm, base->pgd);
	if (ret)
		return ret;

	return 0;
}

static void
nvc0_fifo_context_dtor(struct nouveau_object *object)
{
	struct nvc0_fifo_base *base = (void *)object;
	nouveau_vm_ref(NULL, &base->vm, base->pgd);
	nouveau_gpuobj_ref(NULL, &base->pgd);
	nouveau_fifo_context_destroy(&base->base);
}

static struct nouveau_oclass
nvc0_fifo_cclass = {
	.handle = NV_ENGCTX(FIFO, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_fifo_context_ctor,
		.dtor = nvc0_fifo_context_dtor,
		.init = _nouveau_fifo_context_init,
		.fini = _nouveau_fifo_context_fini,
		.rd32 = _nouveau_fifo_context_rd32,
		.wr32 = _nouveau_fifo_context_wr32,
	},
};

/*******************************************************************************
 * PFIFO engine
 ******************************************************************************/

static inline int
nvc0_fifo_engidx(struct nvc0_fifo_priv *priv, u32 engn)
{
	switch (engn) {
	case NVDEV_ENGINE_GR   : engn = 0; break;
	case NVDEV_ENGINE_BSP  : engn = 1; break;
	case NVDEV_ENGINE_PPP  : engn = 2; break;
	case NVDEV_ENGINE_VP   : engn = 3; break;
	case NVDEV_ENGINE_COPY0: engn = 4; break;
	case NVDEV_ENGINE_COPY1: engn = 5; break;
	default:
		return -1;
	}

	return engn;
}

static inline struct nouveau_engine *
nvc0_fifo_engine(struct nvc0_fifo_priv *priv, u32 engn)
{
	switch (engn) {
	case 0: engn = NVDEV_ENGINE_GR; break;
	case 1: engn = NVDEV_ENGINE_BSP; break;
	case 2: engn = NVDEV_ENGINE_PPP; break;
	case 3: engn = NVDEV_ENGINE_VP; break;
	case 4: engn = NVDEV_ENGINE_COPY0; break;
	case 5: engn = NVDEV_ENGINE_COPY1; break;
	default:
		return NULL;
	}

	return nouveau_engine(priv, engn);
}

static void
nvc0_fifo_recover_work(struct work_struct *work)
{
	struct nvc0_fifo_priv *priv = container_of(work, typeof(*priv), fault);
	struct nouveau_object *engine;
	unsigned long flags;
	u32 engn, engm = 0;
	u64 mask, todo;

	spin_lock_irqsave(&priv->base.lock, flags);
	mask = priv->mask;
	priv->mask = 0ULL;
	spin_unlock_irqrestore(&priv->base.lock, flags);

	for (todo = mask; engn = __ffs64(todo), todo; todo &= ~(1 << engn))
		engm |= 1 << nvc0_fifo_engidx(priv, engn);
	nv_mask(priv, 0x002630, engm, engm);

	for (todo = mask; engn = __ffs64(todo), todo; todo &= ~(1 << engn)) {
		if ((engine = (void *)nouveau_engine(priv, engn))) {
			nv_ofuncs(engine)->fini(engine, false);
			WARN_ON(nv_ofuncs(engine)->init(engine));
		}
	}

	nvc0_fifo_runlist_update(priv);
	nv_wr32(priv, 0x00262c, engm);
	nv_mask(priv, 0x002630, engm, 0x00000000);
}

static void
nvc0_fifo_recover(struct nvc0_fifo_priv *priv, struct nouveau_engine *engine,
		  struct nvc0_fifo_chan *chan)
{
	struct nouveau_object *engobj = nv_object(engine);
	u32 chid = chan->base.chid;
	unsigned long flags;

	nv_error(priv, "%s engine fault on channel %d, recovering...\n",
		       nv_subdev(engine)->name, chid);

	nv_mask(priv, 0x003004 + (chid * 0x08), 0x00000001, 0x00000000);
	chan->state = KILLED;

	spin_lock_irqsave(&priv->base.lock, flags);
	priv->mask |= 1ULL << nv_engidx(engobj);
	spin_unlock_irqrestore(&priv->base.lock, flags);
	schedule_work(&priv->fault);
}

static int
nvc0_fifo_swmthd(struct nvc0_fifo_priv *priv, u32 chid, u32 mthd, u32 data)
{
	struct nvc0_fifo_chan *chan = NULL;
	struct nouveau_handle *bind;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&priv->base.lock, flags);
	if (likely(chid >= priv->base.min && chid <= priv->base.max))
		chan = (void *)priv->base.channel[chid];
	if (unlikely(!chan))
		goto out;

	bind = nouveau_namedb_get_class(nv_namedb(chan), 0x906e);
	if (likely(bind)) {
		if (!mthd || !nv_call(bind->object, mthd, data))
			ret = 0;
		nouveau_namedb_put(bind);
	}

out:
	spin_unlock_irqrestore(&priv->base.lock, flags);
	return ret;
}

static const struct nouveau_enum
nvc0_fifo_sched_reason[] = {
	{ 0x0a, "CTXSW_TIMEOUT" },
	{}
};

static void
nvc0_fifo_intr_sched_ctxsw(struct nvc0_fifo_priv *priv)
{
	struct nouveau_engine *engine;
	struct nvc0_fifo_chan *chan;
	u32 engn;

	for (engn = 0; engn < 6; engn++) {
		u32 stat = nv_rd32(priv, 0x002640 + (engn * 0x04));
		u32 busy = (stat & 0x80000000);
		u32 save = (stat & 0x00100000); /* maybe? */
		u32 unk0 = (stat & 0x00040000);
		u32 unk1 = (stat & 0x00001000);
		u32 chid = (stat & 0x0000007f);
		(void)save;

		if (busy && unk0 && unk1) {
			if (!(chan = (void *)priv->base.channel[chid]))
				continue;
			if (!(engine = nvc0_fifo_engine(priv, engn)))
				continue;
			nvc0_fifo_recover(priv, engine, chan);
		}
	}
}

static void
nvc0_fifo_intr_sched(struct nvc0_fifo_priv *priv)
{
	u32 intr = nv_rd32(priv, 0x00254c);
	u32 code = intr & 0x000000ff;
	const struct nouveau_enum *en;
	char enunk[6] = "";

	en = nouveau_enum_find(nvc0_fifo_sched_reason, code);
	if (!en)
		snprintf(enunk, sizeof(enunk), "UNK%02x", code);

	nv_error(priv, "SCHED_ERROR [ %s ]\n", en ? en->name : enunk);

	switch (code) {
	case 0x0a:
		nvc0_fifo_intr_sched_ctxsw(priv);
		break;
	default:
		break;
	}
}

static const struct nouveau_enum
nvc0_fifo_fault_engine[] = {
	{ 0x00, "PGRAPH", NULL, NVDEV_ENGINE_GR },
	{ 0x03, "PEEPHOLE", NULL, NVDEV_ENGINE_IFB },
	{ 0x04, "BAR1", NULL, NVDEV_SUBDEV_BAR },
	{ 0x05, "BAR3", NULL, NVDEV_SUBDEV_INSTMEM },
	{ 0x07, "PFIFO", NULL, NVDEV_ENGINE_FIFO },
	{ 0x10, "PBSP", NULL, NVDEV_ENGINE_BSP },
	{ 0x11, "PPPP", NULL, NVDEV_ENGINE_PPP },
	{ 0x13, "PCOUNTER" },
	{ 0x14, "PVP", NULL, NVDEV_ENGINE_VP },
	{ 0x15, "PCOPY0", NULL, NVDEV_ENGINE_COPY0 },
	{ 0x16, "PCOPY1", NULL, NVDEV_ENGINE_COPY1 },
	{ 0x17, "PDAEMON" },
	{}
};

static const struct nouveau_enum
nvc0_fifo_fault_reason[] = {
	{ 0x00, "PT_NOT_PRESENT" },
	{ 0x01, "PT_TOO_SHORT" },
	{ 0x02, "PAGE_NOT_PRESENT" },
	{ 0x03, "VM_LIMIT_EXCEEDED" },
	{ 0x04, "NO_CHANNEL" },
	{ 0x05, "PAGE_SYSTEM_ONLY" },
	{ 0x06, "PAGE_READ_ONLY" },
	{ 0x0a, "COMPRESSED_SYSRAM" },
	{ 0x0c, "INVALID_STORAGE_TYPE" },
	{}
};

static const struct nouveau_enum
nvc0_fifo_fault_hubclient[] = {
	{ 0x01, "PCOPY0" },
	{ 0x02, "PCOPY1" },
	{ 0x04, "DISPATCH" },
	{ 0x05, "CTXCTL" },
	{ 0x06, "PFIFO" },
	{ 0x07, "BAR_READ" },
	{ 0x08, "BAR_WRITE" },
	{ 0x0b, "PVP" },
	{ 0x0c, "PPPP" },
	{ 0x0d, "PBSP" },
	{ 0x11, "PCOUNTER" },
	{ 0x12, "PDAEMON" },
	{ 0x14, "CCACHE" },
	{ 0x15, "CCACHE_POST" },
	{}
};

static const struct nouveau_enum
nvc0_fifo_fault_gpcclient[] = {
	{ 0x01, "TEX" },
	{ 0x0c, "ESETUP" },
	{ 0x0e, "CTXCTL" },
	{ 0x0f, "PROP" },
	{}
};

static void
nvc0_fifo_intr_fault(struct nvc0_fifo_priv *priv, int unit)
{
	u32 inst = nv_rd32(priv, 0x002800 + (unit * 0x10));
	u32 valo = nv_rd32(priv, 0x002804 + (unit * 0x10));
	u32 vahi = nv_rd32(priv, 0x002808 + (unit * 0x10));
	u32 stat = nv_rd32(priv, 0x00280c + (unit * 0x10));
	u32 gpc    = (stat & 0x1f000000) >> 24;
	u32 client = (stat & 0x00001f00) >> 8;
	u32 write  = (stat & 0x00000080);
	u32 hub    = (stat & 0x00000040);
	u32 reason = (stat & 0x0000000f);
	struct nouveau_object *engctx = NULL, *object;
	struct nouveau_engine *engine = NULL;
	const struct nouveau_enum *er, *eu, *ec;
	char erunk[6] = "";
	char euunk[6] = "";
	char ecunk[6] = "";
	char gpcid[3] = "";

	er = nouveau_enum_find(nvc0_fifo_fault_reason, reason);
	if (!er)
		snprintf(erunk, sizeof(erunk), "UNK%02X", reason);

	eu = nouveau_enum_find(nvc0_fifo_fault_engine, unit);
	if (eu) {
		switch (eu->data2) {
		case NVDEV_SUBDEV_BAR:
			nv_mask(priv, 0x001704, 0x00000000, 0x00000000);
			break;
		case NVDEV_SUBDEV_INSTMEM:
			nv_mask(priv, 0x001714, 0x00000000, 0x00000000);
			break;
		case NVDEV_ENGINE_IFB:
			nv_mask(priv, 0x001718, 0x00000000, 0x00000000);
			break;
		default:
			engine = nouveau_engine(priv, eu->data2);
			if (engine)
				engctx = nouveau_engctx_get(engine, inst);
			break;
		}
	} else {
		snprintf(euunk, sizeof(euunk), "UNK%02x", unit);
	}

	if (hub) {
		ec = nouveau_enum_find(nvc0_fifo_fault_hubclient, client);
	} else {
		ec = nouveau_enum_find(nvc0_fifo_fault_gpcclient, client);
		snprintf(gpcid, sizeof(gpcid), "%d", gpc);
	}

	if (!ec)
		snprintf(ecunk, sizeof(ecunk), "UNK%02x", client);

	nv_error(priv, "%s fault at 0x%010llx [%s] from %s/%s%s%s%s on "
		       "channel 0x%010llx [%s]\n", write ? "write" : "read",
		 (u64)vahi << 32 | valo, er ? er->name : erunk,
		 eu ? eu->name : euunk, hub ? "" : "GPC", gpcid, hub ? "" : "/",
		 ec ? ec->name : ecunk, (u64)inst << 12,
		 nouveau_client_name(engctx));

	object = engctx;
	while (object) {
		switch (nv_mclass(object)) {
		case FERMI_CHANNEL_GPFIFO:
			nvc0_fifo_recover(priv, engine, (void *)object);
			break;
		}
		object = object->parent;
	}

	nouveau_engctx_put(engctx);
}

static const struct nouveau_bitfield
nvc0_fifo_pbdma_intr[] = {
/*	{ 0x00008000, "" }	seen with null ib push */
	{ 0x00200000, "ILLEGAL_MTHD" },
	{ 0x00800000, "EMPTY_SUBC" },
	{}
};

static void
nvc0_fifo_intr_pbdma(struct nvc0_fifo_priv *priv, int unit)
{
	u32 stat = nv_rd32(priv, 0x040108 + (unit * 0x2000));
	u32 addr = nv_rd32(priv, 0x0400c0 + (unit * 0x2000));
	u32 data = nv_rd32(priv, 0x0400c4 + (unit * 0x2000));
	u32 chid = nv_rd32(priv, 0x040120 + (unit * 0x2000)) & 0x7f;
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00003ffc);
	u32 show = stat;

	if (stat & 0x00800000) {
		if (!nvc0_fifo_swmthd(priv, chid, mthd, data))
			show &= ~0x00800000;
	}

	if (show) {
		nv_error(priv, "PBDMA%d:", unit);
		nouveau_bitfield_print(nvc0_fifo_pbdma_intr, show);
		pr_cont("\n");
		nv_error(priv,
			 "PBDMA%d: ch %d [%s] subc %d mthd 0x%04x data 0x%08x\n",
			 unit, chid,
			 nouveau_client_name_for_fifo_chid(&priv->base, chid),
			 subc, mthd, data);
	}

	nv_wr32(priv, 0x0400c0 + (unit * 0x2000), 0x80600008);
	nv_wr32(priv, 0x040108 + (unit * 0x2000), stat);
}

static void
nvc0_fifo_intr_runlist(struct nvc0_fifo_priv *priv)
{
	u32 intr = nv_rd32(priv, 0x002a00);

	if (intr & 0x10000000) {
		wake_up(&priv->runlist.wait);
		nv_wr32(priv, 0x002a00, 0x10000000);
		intr &= ~0x10000000;
	}

	if (intr) {
		nv_error(priv, "RUNLIST 0x%08x\n", intr);
		nv_wr32(priv, 0x002a00, intr);
	}
}

static void
nvc0_fifo_intr_engine_unit(struct nvc0_fifo_priv *priv, int engn)
{
	u32 intr = nv_rd32(priv, 0x0025a8 + (engn * 0x04));
	u32 inte = nv_rd32(priv, 0x002628);
	u32 unkn;

	nv_wr32(priv, 0x0025a8 + (engn * 0x04), intr);

	for (unkn = 0; unkn < 8; unkn++) {
		u32 ints = (intr >> (unkn * 0x04)) & inte;
		if (ints & 0x1) {
			nouveau_fifo_uevent(&priv->base);
			ints &= ~1;
		}
		if (ints) {
			nv_error(priv, "ENGINE %d %d %01x", engn, unkn, ints);
			nv_mask(priv, 0x002628, ints, 0);
		}
	}
}

static void
nvc0_fifo_intr_engine(struct nvc0_fifo_priv *priv)
{
	u32 mask = nv_rd32(priv, 0x0025a4);
	while (mask) {
		u32 unit = __ffs(mask);
		nvc0_fifo_intr_engine_unit(priv, unit);
		mask &= ~(1 << unit);
	}
}

static void
nvc0_fifo_intr(struct nouveau_subdev *subdev)
{
	struct nvc0_fifo_priv *priv = (void *)subdev;
	u32 mask = nv_rd32(priv, 0x002140);
	u32 stat = nv_rd32(priv, 0x002100) & mask;

	if (stat & 0x00000001) {
		u32 intr = nv_rd32(priv, 0x00252c);
		nv_warn(priv, "INTR 0x00000001: 0x%08x\n", intr);
		nv_wr32(priv, 0x002100, 0x00000001);
		stat &= ~0x00000001;
	}

	if (stat & 0x00000100) {
		nvc0_fifo_intr_sched(priv);
		nv_wr32(priv, 0x002100, 0x00000100);
		stat &= ~0x00000100;
	}

	if (stat & 0x00010000) {
		u32 intr = nv_rd32(priv, 0x00256c);
		nv_warn(priv, "INTR 0x00010000: 0x%08x\n", intr);
		nv_wr32(priv, 0x002100, 0x00010000);
		stat &= ~0x00010000;
	}

	if (stat & 0x01000000) {
		u32 intr = nv_rd32(priv, 0x00258c);
		nv_warn(priv, "INTR 0x01000000: 0x%08x\n", intr);
		nv_wr32(priv, 0x002100, 0x01000000);
		stat &= ~0x01000000;
	}

	if (stat & 0x10000000) {
		u32 mask = nv_rd32(priv, 0x00259c);
		while (mask) {
			u32 unit = __ffs(mask);
			nvc0_fifo_intr_fault(priv, unit);
			nv_wr32(priv, 0x00259c, (1 << unit));
			mask &= ~(1 << unit);
		}
		stat &= ~0x10000000;
	}

	if (stat & 0x20000000) {
		u32 mask = nv_rd32(priv, 0x0025a0);
		while (mask) {
			u32 unit = __ffs(mask);
			nvc0_fifo_intr_pbdma(priv, unit);
			nv_wr32(priv, 0x0025a0, (1 << unit));
			mask &= ~(1 << unit);
		}
		stat &= ~0x20000000;
	}

	if (stat & 0x40000000) {
		nvc0_fifo_intr_runlist(priv);
		stat &= ~0x40000000;
	}

	if (stat & 0x80000000) {
		nvc0_fifo_intr_engine(priv);
		stat &= ~0x80000000;
	}

	if (stat) {
		nv_error(priv, "INTR 0x%08x\n", stat);
		nv_mask(priv, 0x002140, stat, 0x00000000);
		nv_wr32(priv, 0x002100, stat);
	}
}

static void
nvc0_fifo_uevent_init(struct nvkm_event *event, int type, int index)
{
	struct nouveau_fifo *fifo = container_of(event, typeof(*fifo), uevent);
	nv_mask(fifo, 0x002140, 0x80000000, 0x80000000);
}

static void
nvc0_fifo_uevent_fini(struct nvkm_event *event, int type, int index)
{
	struct nouveau_fifo *fifo = container_of(event, typeof(*fifo), uevent);
	nv_mask(fifo, 0x002140, 0x80000000, 0x00000000);
}

static const struct nvkm_event_func
nvc0_fifo_uevent_func = {
	.ctor = nouveau_fifo_uevent_ctor,
	.init = nvc0_fifo_uevent_init,
	.fini = nvc0_fifo_uevent_fini,
};

static int
nvc0_fifo_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nvc0_fifo_priv *priv;
	int ret;

	ret = nouveau_fifo_create(parent, engine, oclass, 0, 127, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	INIT_WORK(&priv->fault, nvc0_fifo_recover_work);

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x1000, 0x1000, 0,
				&priv->runlist.mem[0]);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x1000, 0x1000, 0,
				&priv->runlist.mem[1]);
	if (ret)
		return ret;

	init_waitqueue_head(&priv->runlist.wait);

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 128 * 0x1000, 0x1000, 0,
				&priv->user.mem);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_map(priv->user.mem, NV_MEM_ACCESS_RW,
				&priv->user.bar);
	if (ret)
		return ret;

	ret = nvkm_event_init(&nvc0_fifo_uevent_func, 1, 1, &priv->base.uevent);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000100;
	nv_subdev(priv)->intr = nvc0_fifo_intr;
	nv_engine(priv)->cclass = &nvc0_fifo_cclass;
	nv_engine(priv)->sclass = nvc0_fifo_sclass;
	return 0;
}

static void
nvc0_fifo_dtor(struct nouveau_object *object)
{
	struct nvc0_fifo_priv *priv = (void *)object;

	nouveau_gpuobj_unmap(&priv->user.bar);
	nouveau_gpuobj_ref(NULL, &priv->user.mem);
	nouveau_gpuobj_ref(NULL, &priv->runlist.mem[0]);
	nouveau_gpuobj_ref(NULL, &priv->runlist.mem[1]);

	nouveau_fifo_destroy(&priv->base);
}

static int
nvc0_fifo_init(struct nouveau_object *object)
{
	struct nvc0_fifo_priv *priv = (void *)object;
	int ret, i;

	ret = nouveau_fifo_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x000204, 0xffffffff);
	nv_wr32(priv, 0x002204, 0xffffffff);

	priv->spoon_nr = hweight32(nv_rd32(priv, 0x002204));
	nv_debug(priv, "%d PBDMA unit(s)\n", priv->spoon_nr);

	/* assign engines to PBDMAs */
	if (priv->spoon_nr >= 3) {
		nv_wr32(priv, 0x002208, ~(1 << 0)); /* PGRAPH */
		nv_wr32(priv, 0x00220c, ~(1 << 1)); /* PVP */
		nv_wr32(priv, 0x002210, ~(1 << 1)); /* PPP */
		nv_wr32(priv, 0x002214, ~(1 << 1)); /* PBSP */
		nv_wr32(priv, 0x002218, ~(1 << 2)); /* PCE0 */
		nv_wr32(priv, 0x00221c, ~(1 << 1)); /* PCE1 */
	}

	/* PBDMA[n] */
	for (i = 0; i < priv->spoon_nr; i++) {
		nv_mask(priv, 0x04013c + (i * 0x2000), 0x10000100, 0x00000000);
		nv_wr32(priv, 0x040108 + (i * 0x2000), 0xffffffff); /* INTR */
		nv_wr32(priv, 0x04010c + (i * 0x2000), 0xfffffeff); /* INTREN */
	}

	nv_mask(priv, 0x002200, 0x00000001, 0x00000001);
	nv_wr32(priv, 0x002254, 0x10000000 | priv->user.bar.offset >> 12);

	nv_wr32(priv, 0x002100, 0xffffffff);
	nv_wr32(priv, 0x002140, 0x7fffffff);
	nv_wr32(priv, 0x002628, 0x00000001); /* ENGINE_INTR_EN */
	return 0;
}

struct nouveau_oclass *
nvc0_fifo_oclass = &(struct nouveau_oclass) {
	.handle = NV_ENGINE(FIFO, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_fifo_ctor,
		.dtor = nvc0_fifo_dtor,
		.init = nvc0_fifo_init,
		.fini = _nouveau_fifo_fini,
	},
};
