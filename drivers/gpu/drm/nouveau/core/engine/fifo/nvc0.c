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
#include <core/class.h>
#include <core/math.h>
#include <core/enum.h>

#include <subdev/timer.h>
#include <subdev/bar.h>
#include <subdev/vm.h>

#include <engine/dmaobj.h>
#include <engine/fifo.h>

struct nvc0_fifo_priv {
	struct nouveau_fifo base;
	struct nouveau_gpuobj *playlist[2];
	int cur_playlist;
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
};

/*******************************************************************************
 * FIFO channel objects
 ******************************************************************************/

static void
nvc0_fifo_playlist_update(struct nvc0_fifo_priv *priv)
{
	struct nouveau_bar *bar = nouveau_bar(priv);
	struct nouveau_gpuobj *cur;
	int i, p;

	cur = priv->playlist[priv->cur_playlist];
	priv->cur_playlist = !priv->cur_playlist;

	for (i = 0, p = 0; i < 128; i++) {
		if (!(nv_rd32(priv, 0x003004 + (i * 8)) & 1))
			continue;
		nv_wo32(cur, p + 0, i);
		nv_wo32(cur, p + 4, 0x00000004);
		p += 8;
	}
	bar->flush(bar);

	nv_wr32(priv, 0x002270, cur->addr >> 12);
	nv_wr32(priv, 0x002274, 0x01f00000 | (p >> 3));
	if (!nv_wait(priv, 0x00227c, 0x00100000, 0x00000000))
		nv_error(priv, "playlist update failed\n");
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
		nv_error(priv, "channel %d kick timeout\n", chan->base.chid);
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
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nvc0_fifo_priv *priv = (void *)engine;
	struct nvc0_fifo_base *base = (void *)parent;
	struct nvc0_fifo_chan *chan;
	struct nv50_channel_ind_class *args = data;
	u64 usermem, ioffset, ilength;
	int ret, i;

	if (size < sizeof(*args))
		return -EINVAL;

	ret = nouveau_fifo_channel_create(parent, engine, oclass, 1,
					  priv->user.bar.offset, 0x1000,
					  args->pushbuf,
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

	nv_parent(chan)->context_attach = nvc0_fifo_context_attach;
	nv_parent(chan)->context_detach = nvc0_fifo_context_detach;

	usermem = chan->base.chid * 0x1000;
	ioffset = args->ioffset;
	ilength = log2i(args->ilength / 8);

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
	nv_wr32(priv, 0x003004 + (chid * 8), 0x001f0001);
	nvc0_fifo_playlist_update(priv);
	return 0;
}

static int
nvc0_fifo_chan_fini(struct nouveau_object *object, bool suspend)
{
	struct nvc0_fifo_priv *priv = (void *)object->engine;
	struct nvc0_fifo_chan *chan = (void *)object;
	u32 chid = chan->base.chid;

	nv_mask(priv, 0x003004 + (chid * 8), 0x00000001, 0x00000000);
	nvc0_fifo_playlist_update(priv);
	nv_wr32(priv, 0x003000 + (chid * 8), 0x00000000);

	return nouveau_fifo_channel_fini(&chan->base, suspend);
}

static struct nouveau_ofuncs
nvc0_fifo_ofuncs = {
	.ctor = nvc0_fifo_chan_ctor,
	.dtor = _nouveau_fifo_channel_dtor,
	.init = nvc0_fifo_chan_init,
	.fini = nvc0_fifo_chan_fini,
	.rd32 = _nouveau_fifo_channel_rd32,
	.wr32 = _nouveau_fifo_channel_wr32,
};

static struct nouveau_oclass
nvc0_fifo_sclass[] = {
	{ NVC0_CHANNEL_IND_CLASS, &nvc0_fifo_ofuncs },
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

	ret = nouveau_gpuobj_new(parent, NULL, 0x10000, 0x1000, 0, &base->pgd);
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

static const struct nouveau_enum nvc0_fifo_fault_unit[] = {
	{ 0x00, "PGRAPH" },
	{ 0x03, "PEEPHOLE" },
	{ 0x04, "BAR1" },
	{ 0x05, "BAR3" },
	{ 0x07, "PFIFO" },
	{ 0x10, "PBSP" },
	{ 0x11, "PPPP" },
	{ 0x13, "PCOUNTER" },
	{ 0x14, "PVP" },
	{ 0x15, "PCOPY0" },
	{ 0x16, "PCOPY1" },
	{ 0x17, "PDAEMON" },
	{}
};

static const struct nouveau_enum nvc0_fifo_fault_reason[] = {
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

static const struct nouveau_enum nvc0_fifo_fault_hubclient[] = {
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

static const struct nouveau_enum nvc0_fifo_fault_gpcclient[] = {
	{ 0x01, "TEX" },
	{ 0x0c, "ESETUP" },
	{ 0x0e, "CTXCTL" },
	{ 0x0f, "PROP" },
	{}
};

static const struct nouveau_bitfield nvc0_fifo_subfifo_intr[] = {
/*	{ 0x00008000, "" }	seen with null ib push */
	{ 0x00200000, "ILLEGAL_MTHD" },
	{ 0x00800000, "EMPTY_SUBC" },
	{}
};

static void
nvc0_fifo_isr_vm_fault(struct nvc0_fifo_priv *priv, int unit)
{
	u32 inst = nv_rd32(priv, 0x002800 + (unit * 0x10));
	u32 valo = nv_rd32(priv, 0x002804 + (unit * 0x10));
	u32 vahi = nv_rd32(priv, 0x002808 + (unit * 0x10));
	u32 stat = nv_rd32(priv, 0x00280c + (unit * 0x10));
	u32 client = (stat & 0x00001f00) >> 8;

	switch (unit) {
	case 3: /* PEEPHOLE */
		nv_mask(priv, 0x001718, 0x00000000, 0x00000000);
		break;
	case 4: /* BAR1 */
		nv_mask(priv, 0x001704, 0x00000000, 0x00000000);
		break;
	case 5: /* BAR3 */
		nv_mask(priv, 0x001714, 0x00000000, 0x00000000);
		break;
	default:
		break;
	}

	nv_error(priv, "%s fault at 0x%010llx [", (stat & 0x00000080) ?
		 "write" : "read", (u64)vahi << 32 | valo);
	nouveau_enum_print(nvc0_fifo_fault_reason, stat & 0x0000000f);
	printk("] from ");
	nouveau_enum_print(nvc0_fifo_fault_unit, unit);
	if (stat & 0x00000040) {
		printk("/");
		nouveau_enum_print(nvc0_fifo_fault_hubclient, client);
	} else {
		printk("/GPC%d/", (stat & 0x1f000000) >> 24);
		nouveau_enum_print(nvc0_fifo_fault_gpcclient, client);
	}
	printk(" on channel 0x%010llx\n", (u64)inst << 12);
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

static void
nvc0_fifo_isr_subfifo_intr(struct nvc0_fifo_priv *priv, int unit)
{
	u32 stat = nv_rd32(priv, 0x040108 + (unit * 0x2000));
	u32 addr = nv_rd32(priv, 0x0400c0 + (unit * 0x2000));
	u32 data = nv_rd32(priv, 0x0400c4 + (unit * 0x2000));
	u32 chid = nv_rd32(priv, 0x040120 + (unit * 0x2000)) & 0x7f;
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00003ffc);
	u32 show = stat;

	if (stat & 0x00200000) {
		if (mthd == 0x0054) {
			if (!nvc0_fifo_swmthd(priv, chid, 0x0500, 0x00000000))
				show &= ~0x00200000;
		}
	}

	if (stat & 0x00800000) {
		if (!nvc0_fifo_swmthd(priv, chid, mthd, data))
			show &= ~0x00800000;
	}

	if (show) {
		nv_error(priv, "SUBFIFO%d:", unit);
		nouveau_bitfield_print(nvc0_fifo_subfifo_intr, show);
		printk("\n");
		nv_error(priv, "SUBFIFO%d: ch %d subc %d mthd 0x%04x "
			       "data 0x%08x\n",
			 unit, chid, subc, mthd, data);
	}

	nv_wr32(priv, 0x0400c0 + (unit * 0x2000), 0x80600008);
	nv_wr32(priv, 0x040108 + (unit * 0x2000), stat);
}

static void
nvc0_fifo_intr(struct nouveau_subdev *subdev)
{
	struct nvc0_fifo_priv *priv = (void *)subdev;
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
			nvc0_fifo_isr_vm_fault(priv, i);
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
			nvc0_fifo_isr_subfifo_intr(priv, i);
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

	if (stat) {
		nv_fatal(priv, "unhandled status 0x%08x\n", stat);
		nv_wr32(priv, 0x002100, stat);
		nv_wr32(priv, 0x002140, 0);
	}
}

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

	ret = nouveau_gpuobj_new(parent, NULL, 0x1000, 0x1000, 0,
				&priv->playlist[0]);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(parent, NULL, 0x1000, 0x1000, 0,
				&priv->playlist[1]);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(parent, NULL, 128 * 0x1000, 0x1000, 0,
				&priv->user.mem);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_map(priv->user.mem, NV_MEM_ACCESS_RW,
				&priv->user.bar);
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
	nouveau_gpuobj_ref(NULL, &priv->playlist[1]);
	nouveau_gpuobj_ref(NULL, &priv->playlist[0]);

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
	nv_debug(priv, "%d subfifo(s)\n", priv->spoon_nr);

	/* assign engines to subfifos */
	if (priv->spoon_nr >= 3) {
		nv_wr32(priv, 0x002208, ~(1 << 0)); /* PGRAPH */
		nv_wr32(priv, 0x00220c, ~(1 << 1)); /* PVP */
		nv_wr32(priv, 0x002210, ~(1 << 1)); /* PPP */
		nv_wr32(priv, 0x002214, ~(1 << 1)); /* PBSP */
		nv_wr32(priv, 0x002218, ~(1 << 2)); /* PCE0 */
		nv_wr32(priv, 0x00221c, ~(1 << 1)); /* PCE1 */
	}

	/* PSUBFIFO[n] */
	for (i = 0; i < priv->spoon_nr; i++) {
		nv_mask(priv, 0x04013c + (i * 0x2000), 0x10000100, 0x00000000);
		nv_wr32(priv, 0x040108 + (i * 0x2000), 0xffffffff); /* INTR */
		nv_wr32(priv, 0x04010c + (i * 0x2000), 0xfffffeff); /* INTREN */
	}

	nv_mask(priv, 0x002200, 0x00000001, 0x00000001);
	nv_wr32(priv, 0x002254, 0x10000000 | priv->user.bar.offset >> 12);

	nv_wr32(priv, 0x002a00, 0xffffffff); /* clears PFIFO.INTR bit 30 */
	nv_wr32(priv, 0x002100, 0xffffffff);
	nv_wr32(priv, 0x002140, 0xbfffffff);
	return 0;
}

struct nouveau_oclass
nvc0_fifo_oclass = {
	.handle = NV_ENGINE(FIFO, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_fifo_ctor,
		.dtor = nvc0_fifo_dtor,
		.init = nvc0_fifo_init,
		.fini = _nouveau_fifo_fini,
	},
};
