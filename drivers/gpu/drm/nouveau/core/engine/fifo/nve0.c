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
#include <core/class.h>
#include <core/math.h>
#include <core/enum.h>

#include <subdev/timer.h>
#include <subdev/bar.h>
#include <subdev/vm.h>

#include <engine/dmaobj.h>
#include <engine/fifo.h>

#define _(a,b) { (a), ((1ULL << (a)) | (b)) }
static const struct {
	u64 subdev;
	u64 mask;
} fifo_engine[] = {
	_(NVDEV_ENGINE_GR      , (1ULL << NVDEV_ENGINE_SW) |
				 (1ULL << NVDEV_ENGINE_COPY2)),
	_(NVDEV_ENGINE_VP      , 0),
	_(NVDEV_ENGINE_PPP     , 0),
	_(NVDEV_ENGINE_BSP     , 0),
	_(NVDEV_ENGINE_COPY0   , 0),
	_(NVDEV_ENGINE_COPY1   , 0),
	_(NVDEV_ENGINE_VENC    , 0),
};
#undef _
#define FIFO_ENGINE_NR ARRAY_SIZE(fifo_engine)

struct nve0_fifo_engn {
	struct nouveau_gpuobj *playlist[2];
	int cur_playlist;
};

struct nve0_fifo_priv {
	struct nouveau_fifo base;
	struct nve0_fifo_engn engine[FIFO_ENGINE_NR];
	struct {
		struct nouveau_gpuobj *mem;
		struct nouveau_vma bar;
	} user;
	int spoon_nr;
};

struct nve0_fifo_base {
	struct nouveau_fifo_base base;
	struct nouveau_gpuobj *pgd;
	struct nouveau_vm *vm;
};

struct nve0_fifo_chan {
	struct nouveau_fifo_chan base;
	u32 engine;
};

/*******************************************************************************
 * FIFO channel objects
 ******************************************************************************/

static void
nve0_fifo_playlist_update(struct nve0_fifo_priv *priv, u32 engine)
{
	struct nouveau_bar *bar = nouveau_bar(priv);
	struct nve0_fifo_engn *engn = &priv->engine[engine];
	struct nouveau_gpuobj *cur;
	u32 match = (engine << 16) | 0x00000001;
	int i, p;

	mutex_lock(&nv_subdev(priv)->mutex);
	cur = engn->playlist[engn->cur_playlist];
	engn->cur_playlist = !engn->cur_playlist;

	for (i = 0, p = 0; i < priv->base.max; i++) {
		u32 ctrl = nv_rd32(priv, 0x800004 + (i * 8)) & 0x001f0001;
		if (ctrl != match)
			continue;
		nv_wo32(cur, p + 0, i);
		nv_wo32(cur, p + 4, 0x00000000);
		p += 8;
	}
	bar->flush(bar);

	nv_wr32(priv, 0x002270, cur->addr >> 12);
	nv_wr32(priv, 0x002274, (engine << 20) | (p >> 3));
	if (!nv_wait(priv, 0x002284 + (engine * 4), 0x00100000, 0x00000000))
		nv_error(priv, "playlist %d update timeout\n", engine);
	mutex_unlock(&nv_subdev(priv)->mutex);
}

static int
nve0_fifo_context_attach(struct nouveau_object *parent,
			 struct nouveau_object *object)
{
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nve0_fifo_base *base = (void *)parent->parent;
	struct nouveau_engctx *ectx = (void *)object;
	u32 addr;
	int ret;

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_SW   :
	case NVDEV_ENGINE_COPY0:
	case NVDEV_ENGINE_COPY1:
	case NVDEV_ENGINE_COPY2:
		return 0;
	case NVDEV_ENGINE_GR   : addr = 0x0210; break;
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
nve0_fifo_context_detach(struct nouveau_object *parent, bool suspend,
			 struct nouveau_object *object)
{
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nve0_fifo_priv *priv = (void *)parent->engine;
	struct nve0_fifo_base *base = (void *)parent->parent;
	struct nve0_fifo_chan *chan = (void *)parent;
	u32 addr;

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_SW   : return 0;
	case NVDEV_ENGINE_COPY0:
	case NVDEV_ENGINE_COPY1:
	case NVDEV_ENGINE_COPY2: addr = 0x0000; break;
	case NVDEV_ENGINE_GR   : addr = 0x0210; break;
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

	if (addr) {
		nv_wo32(base, addr + 0x00, 0x00000000);
		nv_wo32(base, addr + 0x04, 0x00000000);
		bar->flush(bar);
	}

	return 0;
}

static int
nve0_fifo_chan_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nve0_fifo_priv *priv = (void *)engine;
	struct nve0_fifo_base *base = (void *)parent;
	struct nve0_fifo_chan *chan;
	struct nve0_channel_ind_class *args = data;
	u64 usermem, ioffset, ilength;
	int ret, i;

	if (size < sizeof(*args))
		return -EINVAL;

	for (i = 0; i < FIFO_ENGINE_NR; i++) {
		if (args->engine & (1 << i)) {
			if (nouveau_engine(parent, fifo_engine[i].subdev)) {
				args->engine = (1 << i);
				break;
			}
		}
	}

	if (i == FIFO_ENGINE_NR) {
		nv_error(priv, "unsupported engines 0x%08x\n", args->engine);
		return -ENODEV;
	}

	ret = nouveau_fifo_channel_create(parent, engine, oclass, 1,
					  priv->user.bar.offset, 0x200,
					  args->pushbuf,
					  fifo_engine[i].mask, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	nv_parent(chan)->context_attach = nve0_fifo_context_attach;
	nv_parent(chan)->context_detach = nve0_fifo_context_detach;
	chan->engine = i;

	usermem = chan->base.chid * 0x200;
	ioffset = args->ioffset;
	ilength = log2i(args->ilength / 8);

	for (i = 0; i < 0x200; i += 4)
		nv_wo32(priv->user.mem, usermem + i, 0x00000000);

	nv_wo32(base, 0x08, lower_32_bits(priv->user.mem->addr + usermem));
	nv_wo32(base, 0x0c, upper_32_bits(priv->user.mem->addr + usermem));
	nv_wo32(base, 0x10, 0x0000face);
	nv_wo32(base, 0x30, 0xfffff902);
	nv_wo32(base, 0x48, lower_32_bits(ioffset));
	nv_wo32(base, 0x4c, upper_32_bits(ioffset) | (ilength << 16));
	nv_wo32(base, 0x84, 0x20400000);
	nv_wo32(base, 0x94, 0x30000001);
	nv_wo32(base, 0x9c, 0x00000100);
	nv_wo32(base, 0xac, 0x0000001f);
	nv_wo32(base, 0xe8, chan->base.chid);
	nv_wo32(base, 0xb8, 0xf8000000);
	nv_wo32(base, 0xf8, 0x10003080); /* 0x002310 */
	nv_wo32(base, 0xfc, 0x10000010); /* 0x002350 */
	bar->flush(bar);
	return 0;
}

static int
nve0_fifo_chan_init(struct nouveau_object *object)
{
	struct nouveau_gpuobj *base = nv_gpuobj(object->parent);
	struct nve0_fifo_priv *priv = (void *)object->engine;
	struct nve0_fifo_chan *chan = (void *)object;
	u32 chid = chan->base.chid;
	int ret;

	ret = nouveau_fifo_channel_init(&chan->base);
	if (ret)
		return ret;

	nv_mask(priv, 0x800004 + (chid * 8), 0x000f0000, chan->engine << 16);
	nv_wr32(priv, 0x800000 + (chid * 8), 0x80000000 | base->addr >> 12);
	nv_mask(priv, 0x800004 + (chid * 8), 0x00000400, 0x00000400);
	nve0_fifo_playlist_update(priv, chan->engine);
	nv_mask(priv, 0x800004 + (chid * 8), 0x00000400, 0x00000400);
	return 0;
}

static int
nve0_fifo_chan_fini(struct nouveau_object *object, bool suspend)
{
	struct nve0_fifo_priv *priv = (void *)object->engine;
	struct nve0_fifo_chan *chan = (void *)object;
	u32 chid = chan->base.chid;

	nv_mask(priv, 0x800004 + (chid * 8), 0x00000800, 0x00000800);
	nve0_fifo_playlist_update(priv, chan->engine);
	nv_wr32(priv, 0x800000 + (chid * 8), 0x00000000);

	return nouveau_fifo_channel_fini(&chan->base, suspend);
}

static struct nouveau_ofuncs
nve0_fifo_ofuncs = {
	.ctor = nve0_fifo_chan_ctor,
	.dtor = _nouveau_fifo_channel_dtor,
	.init = nve0_fifo_chan_init,
	.fini = nve0_fifo_chan_fini,
	.rd32 = _nouveau_fifo_channel_rd32,
	.wr32 = _nouveau_fifo_channel_wr32,
};

static struct nouveau_oclass
nve0_fifo_sclass[] = {
	{ NVE0_CHANNEL_IND_CLASS, &nve0_fifo_ofuncs },
	{}
};

/*******************************************************************************
 * FIFO context - instmem heap and vm setup
 ******************************************************************************/

static int
nve0_fifo_context_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nve0_fifo_base *base;
	int ret;

	ret = nouveau_fifo_context_create(parent, engine, oclass, NULL, 0x1000,
				          0x1000, NVOBJ_FLAG_ZERO_ALLOC, &base);
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
nve0_fifo_context_dtor(struct nouveau_object *object)
{
	struct nve0_fifo_base *base = (void *)object;
	nouveau_vm_ref(NULL, &base->vm, base->pgd);
	nouveau_gpuobj_ref(NULL, &base->pgd);
	nouveau_fifo_context_destroy(&base->base);
}

static struct nouveau_oclass
nve0_fifo_cclass = {
	.handle = NV_ENGCTX(FIFO, 0xe0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nve0_fifo_context_ctor,
		.dtor = nve0_fifo_context_dtor,
		.init = _nouveau_fifo_context_init,
		.fini = _nouveau_fifo_context_fini,
		.rd32 = _nouveau_fifo_context_rd32,
		.wr32 = _nouveau_fifo_context_wr32,
	},
};

/*******************************************************************************
 * PFIFO engine
 ******************************************************************************/

static const struct nouveau_enum nve0_fifo_fault_unit[] = {
	{}
};

static const struct nouveau_enum nve0_fifo_fault_reason[] = {
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

static const struct nouveau_enum nve0_fifo_fault_hubclient[] = {
	{}
};

static const struct nouveau_enum nve0_fifo_fault_gpcclient[] = {
	{}
};

static const struct nouveau_bitfield nve0_fifo_subfifo_intr[] = {
	{ 0x00200000, "ILLEGAL_MTHD" },
	{ 0x00800000, "EMPTY_SUBC" },
	{}
};

static void
nve0_fifo_isr_vm_fault(struct nve0_fifo_priv *priv, int unit)
{
	u32 inst = nv_rd32(priv, 0x2800 + (unit * 0x10));
	u32 valo = nv_rd32(priv, 0x2804 + (unit * 0x10));
	u32 vahi = nv_rd32(priv, 0x2808 + (unit * 0x10));
	u32 stat = nv_rd32(priv, 0x280c + (unit * 0x10));
	u32 client = (stat & 0x00001f00) >> 8;
	const struct nouveau_enum *en;
	struct nouveau_engine *engine;
	struct nouveau_object *engctx = NULL;

	nv_error(priv, "PFIFO: %s fault at 0x%010llx [", (stat & 0x00000080) ?
		       "write" : "read", (u64)vahi << 32 | valo);
	nouveau_enum_print(nve0_fifo_fault_reason, stat & 0x0000000f);
	pr_cont("] from ");
	en = nouveau_enum_print(nve0_fifo_fault_unit, unit);
	if (stat & 0x00000040) {
		pr_cont("/");
		nouveau_enum_print(nve0_fifo_fault_hubclient, client);
	} else {
		pr_cont("/GPC%d/", (stat & 0x1f000000) >> 24);
		nouveau_enum_print(nve0_fifo_fault_gpcclient, client);
	}

	if (en && en->data2) {
		engine = nouveau_engine(priv, en->data2);
		if (engine)
			engctx = nouveau_engctx_get(engine, inst);

	}

	pr_cont(" on channel 0x%010llx [%s]\n", (u64)inst << 12,
			nouveau_client_name(engctx));

	nouveau_engctx_put(engctx);
}

static int
nve0_fifo_swmthd(struct nve0_fifo_priv *priv, u32 chid, u32 mthd, u32 data)
{
	struct nve0_fifo_chan *chan = NULL;
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

static void
nve0_fifo_isr_subfifo_intr(struct nve0_fifo_priv *priv, int unit)
{
	u32 stat = nv_rd32(priv, 0x040108 + (unit * 0x2000));
	u32 addr = nv_rd32(priv, 0x0400c0 + (unit * 0x2000));
	u32 data = nv_rd32(priv, 0x0400c4 + (unit * 0x2000));
	u32 chid = nv_rd32(priv, 0x040120 + (unit * 0x2000)) & 0xfff;
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00003ffc);
	u32 show = stat;

	if (stat & 0x00200000) {
		if (mthd == 0x0054) {
			if (!nve0_fifo_swmthd(priv, chid, 0x0500, 0x00000000))
				show &= ~0x00200000;
		}
	}

	if (stat & 0x00800000) {
		if (!nve0_fifo_swmthd(priv, chid, mthd, data))
			show &= ~0x00800000;
	}

	if (show) {
		nv_error(priv, "SUBFIFO%d:", unit);
		nouveau_bitfield_print(nve0_fifo_subfifo_intr, show);
		pr_cont("\n");
		nv_error(priv,
			 "SUBFIFO%d: ch %d [%s] subc %d mthd 0x%04x data 0x%08x\n",
			 unit, chid,
			 nouveau_client_name_for_fifo_chid(&priv->base, chid),
			 subc, mthd, data);
	}

	nv_wr32(priv, 0x0400c0 + (unit * 0x2000), 0x80600008);
	nv_wr32(priv, 0x040108 + (unit * 0x2000), stat);
}

static void
nve0_fifo_intr(struct nouveau_subdev *subdev)
{
	struct nve0_fifo_priv *priv = (void *)subdev;
	u32 mask = nv_rd32(priv, 0x002140);
	u32 stat = nv_rd32(priv, 0x002100) & mask;

	if (stat & 0x00000100) {
		nv_warn(priv, "unknown status 0x00000100\n");
		nv_wr32(priv, 0x002100, 0x00000100);
		stat &= ~0x00000100;
	}

	if (stat & 0x10000000) {
		u32 units = nv_rd32(priv, 0x00259c);
		u32 u = units;

		while (u) {
			int i = ffs(u) - 1;
			nve0_fifo_isr_vm_fault(priv, i);
			u &= ~(1 << i);
		}

		nv_wr32(priv, 0x00259c, units);
		stat &= ~0x10000000;
	}

	if (stat & 0x20000000) {
		u32 units = nv_rd32(priv, 0x0025a0);
		u32 u = units;

		while (u) {
			int i = ffs(u) - 1;
			nve0_fifo_isr_subfifo_intr(priv, i);
			u &= ~(1 << i);
		}

		nv_wr32(priv, 0x0025a0, units);
		stat &= ~0x20000000;
	}

	if (stat & 0x40000000) {
		nv_warn(priv, "unknown status 0x40000000\n");
		nv_mask(priv, 0x002a00, 0x00000000, 0x00000000);
		stat &= ~0x40000000;
	}

	if (stat & 0x80000000) {
		nouveau_event_trigger(priv->base.uevent, 0);
		nv_wr32(priv, 0x002100, 0x80000000);
		stat &= ~0x80000000;
	}

	if (stat) {
		nv_fatal(priv, "unhandled status 0x%08x\n", stat);
		nv_wr32(priv, 0x002100, stat);
		nv_wr32(priv, 0x002140, 0);
	}
}

static void
nve0_fifo_uevent_enable(struct nouveau_event *event, int index)
{
	struct nve0_fifo_priv *priv = event->priv;
	nv_mask(priv, 0x002140, 0x80000000, 0x80000000);
}

static void
nve0_fifo_uevent_disable(struct nouveau_event *event, int index)
{
	struct nve0_fifo_priv *priv = event->priv;
	nv_mask(priv, 0x002140, 0x80000000, 0x00000000);
}

static int
nve0_fifo_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nve0_fifo_priv *priv;
	int ret, i;

	ret = nouveau_fifo_create(parent, engine, oclass, 0, 4095, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	for (i = 0; i < FIFO_ENGINE_NR; i++) {
		ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x8000, 0x1000,
					 0, &priv->engine[i].playlist[0]);
		if (ret)
			return ret;

		ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x8000, 0x1000,
					 0, &priv->engine[i].playlist[1]);
		if (ret)
			return ret;
	}

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 4096 * 0x200, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC, &priv->user.mem);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_map(priv->user.mem, NV_MEM_ACCESS_RW,
				&priv->user.bar);
	if (ret)
		return ret;

	priv->base.uevent->enable = nve0_fifo_uevent_enable;
	priv->base.uevent->disable = nve0_fifo_uevent_disable;
	priv->base.uevent->priv = priv;

	nv_subdev(priv)->unit = 0x00000100;
	nv_subdev(priv)->intr = nve0_fifo_intr;
	nv_engine(priv)->cclass = &nve0_fifo_cclass;
	nv_engine(priv)->sclass = nve0_fifo_sclass;
	return 0;
}

static void
nve0_fifo_dtor(struct nouveau_object *object)
{
	struct nve0_fifo_priv *priv = (void *)object;
	int i;

	nouveau_gpuobj_unmap(&priv->user.bar);
	nouveau_gpuobj_ref(NULL, &priv->user.mem);

	for (i = 0; i < FIFO_ENGINE_NR; i++) {
		nouveau_gpuobj_ref(NULL, &priv->engine[i].playlist[1]);
		nouveau_gpuobj_ref(NULL, &priv->engine[i].playlist[0]);
	}

	nouveau_fifo_destroy(&priv->base);
}

static int
nve0_fifo_init(struct nouveau_object *object)
{
	struct nve0_fifo_priv *priv = (void *)object;
	int ret, i;

	ret = nouveau_fifo_init(&priv->base);
	if (ret)
		return ret;

	/* enable all available PSUBFIFOs */
	nv_wr32(priv, 0x000204, 0xffffffff);
	priv->spoon_nr = hweight32(nv_rd32(priv, 0x000204));
	nv_debug(priv, "%d subfifo(s)\n", priv->spoon_nr);

	/* PSUBFIFO[n] */
	for (i = 0; i < priv->spoon_nr; i++) {
		nv_mask(priv, 0x04013c + (i * 0x2000), 0x10000100, 0x00000000);
		nv_wr32(priv, 0x040108 + (i * 0x2000), 0xffffffff); /* INTR */
		nv_wr32(priv, 0x04010c + (i * 0x2000), 0xfffffeff); /* INTREN */
	}

	nv_wr32(priv, 0x002254, 0x10000000 | priv->user.bar.offset >> 12);

	nv_wr32(priv, 0x002a00, 0xffffffff);
	nv_wr32(priv, 0x002100, 0xffffffff);
	nv_wr32(priv, 0x002140, 0x3fffffff);
	return 0;
}

struct nouveau_oclass
nve0_fifo_oclass = {
	.handle = NV_ENGINE(FIFO, 0xe0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nve0_fifo_ctor,
		.dtor = nve0_fifo_dtor,
		.init = nve0_fifo_init,
		.fini = _nouveau_fifo_fini,
	},
};
