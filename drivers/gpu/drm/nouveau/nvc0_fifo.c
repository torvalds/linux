/*
 * Copyright 2010 Red Hat Inc.
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

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_mm.h"

static void nvc0_fifo_isr(struct drm_device *);

struct nvc0_fifo_priv {
	struct nouveau_gpuobj *playlist[2];
	int cur_playlist;
	struct nouveau_vma user_vma;
	int spoon_nr;
};

struct nvc0_fifo_chan {
	struct nouveau_gpuobj *user;
	struct nouveau_gpuobj *ramfc;
};

static void
nvc0_fifo_playlist_update(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nvc0_fifo_priv *priv = pfifo->priv;
	struct nouveau_gpuobj *cur;
	int i, p;

	cur = priv->playlist[priv->cur_playlist];
	priv->cur_playlist = !priv->cur_playlist;

	for (i = 0, p = 0; i < 128; i++) {
		if (!(nv_rd32(dev, 0x3004 + (i * 8)) & 1))
			continue;
		nv_wo32(cur, p + 0, i);
		nv_wo32(cur, p + 4, 0x00000004);
		p += 8;
	}
	pinstmem->flush(dev);

	nv_wr32(dev, 0x002270, cur->vinst >> 12);
	nv_wr32(dev, 0x002274, 0x01f00000 | (p >> 3));
	if (!nv_wait(dev, 0x00227c, 0x00100000, 0x00000000))
		NV_ERROR(dev, "PFIFO - playlist update failed\n");
}

void
nvc0_fifo_disable(struct drm_device *dev)
{
}

void
nvc0_fifo_enable(struct drm_device *dev)
{
}

bool
nvc0_fifo_reassign(struct drm_device *dev, bool enable)
{
	return false;
}

bool
nvc0_fifo_cache_pull(struct drm_device *dev, bool enable)
{
	return false;
}

int
nvc0_fifo_channel_id(struct drm_device *dev)
{
	return 127;
}

int
nvc0_fifo_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nvc0_fifo_priv *priv = pfifo->priv;
	struct nvc0_fifo_chan *fifoch;
	u64 ib_virt = chan->pushbuf_base + chan->dma.ib_base * 4;
	int ret;

	chan->fifo_priv = kzalloc(sizeof(*fifoch), GFP_KERNEL);
	if (!chan->fifo_priv)
		return -ENOMEM;
	fifoch = chan->fifo_priv;

	/* allocate vram for control regs, map into polling area */
	ret = nouveau_gpuobj_new(dev, NULL, 0x1000, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC, &fifoch->user);
	if (ret)
		goto error;

	nouveau_vm_map_at(&priv->user_vma, chan->id * 0x1000,
			  *(struct nouveau_mem **)fifoch->user->node);

	chan->user = ioremap_wc(pci_resource_start(dev->pdev, 1) +
				priv->user_vma.offset + (chan->id * 0x1000),
				PAGE_SIZE);
	if (!chan->user) {
		ret = -ENOMEM;
		goto error;
	}

	/* ramfc */
	ret = nouveau_gpuobj_new_fake(dev, chan->ramin->pinst,
				      chan->ramin->vinst, 0x100,
				      NVOBJ_FLAG_ZERO_ALLOC, &fifoch->ramfc);
	if (ret)
		goto error;

	nv_wo32(fifoch->ramfc, 0x08, lower_32_bits(fifoch->user->vinst));
	nv_wo32(fifoch->ramfc, 0x0c, upper_32_bits(fifoch->user->vinst));
	nv_wo32(fifoch->ramfc, 0x10, 0x0000face);
	nv_wo32(fifoch->ramfc, 0x30, 0xfffff902);
	nv_wo32(fifoch->ramfc, 0x48, lower_32_bits(ib_virt));
	nv_wo32(fifoch->ramfc, 0x4c, drm_order(chan->dma.ib_max + 1) << 16 |
				   upper_32_bits(ib_virt));
	nv_wo32(fifoch->ramfc, 0x54, 0x00000002);
	nv_wo32(fifoch->ramfc, 0x84, 0x20400000);
	nv_wo32(fifoch->ramfc, 0x94, 0x30000001);
	nv_wo32(fifoch->ramfc, 0x9c, 0x00000100);
	nv_wo32(fifoch->ramfc, 0xa4, 0x1f1f1f1f);
	nv_wo32(fifoch->ramfc, 0xa8, 0x1f1f1f1f);
	nv_wo32(fifoch->ramfc, 0xac, 0x0000001f);
	nv_wo32(fifoch->ramfc, 0xb8, 0xf8000000);
	nv_wo32(fifoch->ramfc, 0xf8, 0x10003080); /* 0x002310 */
	nv_wo32(fifoch->ramfc, 0xfc, 0x10000010); /* 0x002350 */
	pinstmem->flush(dev);

	nv_wr32(dev, 0x003000 + (chan->id * 8), 0xc0000000 |
						(chan->ramin->vinst >> 12));
	nv_wr32(dev, 0x003004 + (chan->id * 8), 0x001f0001);
	nvc0_fifo_playlist_update(dev);
	return 0;

error:
	pfifo->destroy_context(chan);
	return ret;
}

void
nvc0_fifo_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct nvc0_fifo_chan *fifoch;

	nv_mask(dev, 0x003004 + (chan->id * 8), 0x00000001, 0x00000000);
	nv_wr32(dev, 0x002634, chan->id);
	if (!nv_wait(dev, 0x0002634, 0xffffffff, chan->id))
		NV_WARN(dev, "0x2634 != chid: 0x%08x\n", nv_rd32(dev, 0x2634));

	nvc0_fifo_playlist_update(dev);

	nv_wr32(dev, 0x003000 + (chan->id * 8), 0x00000000);

	if (chan->user) {
		iounmap(chan->user);
		chan->user = NULL;
	}

	fifoch = chan->fifo_priv;
	chan->fifo_priv = NULL;
	if (!fifoch)
		return;

	nouveau_gpuobj_ref(NULL, &fifoch->ramfc);
	nouveau_gpuobj_ref(NULL, &fifoch->user);
	kfree(fifoch);
}

int
nvc0_fifo_load_context(struct nouveau_channel *chan)
{
	return 0;
}

int
nvc0_fifo_unload_context(struct drm_device *dev)
{
	int i;

	for (i = 0; i < 128; i++) {
		if (!(nv_rd32(dev, 0x003004 + (i * 8)) & 1))
			continue;

		nv_mask(dev, 0x003004 + (i * 8), 0x00000001, 0x00000000);
		nv_wr32(dev, 0x002634, i);
		if (!nv_wait(dev, 0x002634, 0xffffffff, i)) {
			NV_INFO(dev, "PFIFO: kick ch %d failed: 0x%08x\n",
				i, nv_rd32(dev, 0x002634));
			return -EBUSY;
		}
	}

	return 0;
}

static void
nvc0_fifo_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nvc0_fifo_priv *priv;

	priv = pfifo->priv;
	if (!priv)
		return;

	nouveau_vm_put(&priv->user_vma);
	nouveau_gpuobj_ref(NULL, &priv->playlist[1]);
	nouveau_gpuobj_ref(NULL, &priv->playlist[0]);
	kfree(priv);
}

void
nvc0_fifo_takedown(struct drm_device *dev)
{
	nv_wr32(dev, 0x002140, 0x00000000);
	nvc0_fifo_destroy(dev);
}

static int
nvc0_fifo_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nvc0_fifo_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	pfifo->priv = priv;

	ret = nouveau_gpuobj_new(dev, NULL, 0x1000, 0x1000, 0,
				 &priv->playlist[0]);
	if (ret)
		goto error;

	ret = nouveau_gpuobj_new(dev, NULL, 0x1000, 0x1000, 0,
				 &priv->playlist[1]);
	if (ret)
		goto error;

	ret = nouveau_vm_get(dev_priv->bar1_vm, pfifo->channels * 0x1000,
			     12, NV_MEM_ACCESS_RW, &priv->user_vma);
	if (ret)
		goto error;

	nouveau_irq_register(dev, 8, nvc0_fifo_isr);
	NVOBJ_CLASS(dev, 0x506e, SW); /* nvsw */
	return 0;

error:
	nvc0_fifo_destroy(dev);
	return ret;
}

int
nvc0_fifo_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_channel *chan;
	struct nvc0_fifo_priv *priv;
	int ret, i;

	if (!pfifo->priv) {
		ret = nvc0_fifo_create(dev);
		if (ret)
			return ret;
	}
	priv = pfifo->priv;

	/* reset PFIFO, enable all available PSUBFIFO areas */
	nv_mask(dev, 0x000200, 0x00000100, 0x00000000);
	nv_mask(dev, 0x000200, 0x00000100, 0x00000100);
	nv_wr32(dev, 0x000204, 0xffffffff);
	nv_wr32(dev, 0x002204, 0xffffffff);

	priv->spoon_nr = hweight32(nv_rd32(dev, 0x002204));
	NV_DEBUG(dev, "PFIFO: %d subfifo(s)\n", priv->spoon_nr);

	/* assign engines to subfifos */
	if (priv->spoon_nr >= 3) {
		nv_wr32(dev, 0x002208, ~(1 << 0)); /* PGRAPH */
		nv_wr32(dev, 0x00220c, ~(1 << 1)); /* PVP */
		nv_wr32(dev, 0x002210, ~(1 << 1)); /* PPP */
		nv_wr32(dev, 0x002214, ~(1 << 1)); /* PBSP */
		nv_wr32(dev, 0x002218, ~(1 << 2)); /* PCE0 */
		nv_wr32(dev, 0x00221c, ~(1 << 1)); /* PCE1 */
	}

	/* PSUBFIFO[n] */
	for (i = 0; i < priv->spoon_nr; i++) {
		nv_mask(dev, 0x04013c + (i * 0x2000), 0x10000100, 0x00000000);
		nv_wr32(dev, 0x040108 + (i * 0x2000), 0xffffffff); /* INTR */
		nv_wr32(dev, 0x04010c + (i * 0x2000), 0xfffffeff); /* INTR_EN */
	}

	nv_mask(dev, 0x002200, 0x00000001, 0x00000001);
	nv_wr32(dev, 0x002254, 0x10000000 | priv->user_vma.offset >> 12);

	nv_wr32(dev, 0x002a00, 0xffffffff); /* clears PFIFO.INTR bit 30 */
	nv_wr32(dev, 0x002100, 0xffffffff);
	nv_wr32(dev, 0x002140, 0xbfffffff);

	/* restore PFIFO context table */
	for (i = 0; i < 128; i++) {
		chan = dev_priv->channels.ptr[i];
		if (!chan || !chan->fifo_priv)
			continue;

		nv_wr32(dev, 0x003000 + (i * 8), 0xc0000000 |
						 (chan->ramin->vinst >> 12));
		nv_wr32(dev, 0x003004 + (i * 8), 0x001f0001);
	}
	nvc0_fifo_playlist_update(dev);

	return 0;
}

struct nouveau_enum nvc0_fifo_fault_unit[] = {
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

struct nouveau_enum nvc0_fifo_fault_reason[] = {
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

struct nouveau_enum nvc0_fifo_fault_hubclient[] = {
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

struct nouveau_enum nvc0_fifo_fault_gpcclient[] = {
	{ 0x01, "TEX" },
	{ 0x0c, "ESETUP" },
	{ 0x0e, "CTXCTL" },
	{ 0x0f, "PROP" },
	{}
};

struct nouveau_bitfield nvc0_fifo_subfifo_intr[] = {
/*	{ 0x00008000, "" }	seen with null ib push */
	{ 0x00200000, "ILLEGAL_MTHD" },
	{ 0x00800000, "EMPTY_SUBC" },
	{}
};

static void
nvc0_fifo_isr_vm_fault(struct drm_device *dev, int unit)
{
	u32 inst = nv_rd32(dev, 0x2800 + (unit * 0x10));
	u32 valo = nv_rd32(dev, 0x2804 + (unit * 0x10));
	u32 vahi = nv_rd32(dev, 0x2808 + (unit * 0x10));
	u32 stat = nv_rd32(dev, 0x280c + (unit * 0x10));
	u32 client = (stat & 0x00001f00) >> 8;

	NV_INFO(dev, "PFIFO: %s fault at 0x%010llx [",
		(stat & 0x00000080) ? "write" : "read", (u64)vahi << 32 | valo);
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
nvc0_fifo_page_flip(struct drm_device *dev, u32 chid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = NULL;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	if (likely(chid >= 0 && chid < dev_priv->engine.fifo.channels)) {
		chan = dev_priv->channels.ptr[chid];
		if (likely(chan))
			ret = nouveau_finish_page_flip(chan, NULL);
	}
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);
	return ret;
}

static void
nvc0_fifo_isr_subfifo_intr(struct drm_device *dev, int unit)
{
	u32 stat = nv_rd32(dev, 0x040108 + (unit * 0x2000));
	u32 addr = nv_rd32(dev, 0x0400c0 + (unit * 0x2000));
	u32 data = nv_rd32(dev, 0x0400c4 + (unit * 0x2000));
	u32 chid = nv_rd32(dev, 0x040120 + (unit * 0x2000)) & 0x7f;
	u32 subc = (addr & 0x00070000);
	u32 mthd = (addr & 0x00003ffc);
	u32 show = stat;

	if (stat & 0x00200000) {
		if (mthd == 0x0054) {
			if (!nvc0_fifo_page_flip(dev, chid))
				show &= ~0x00200000;
		}
	}

	if (show) {
		NV_INFO(dev, "PFIFO%d:", unit);
		nouveau_bitfield_print(nvc0_fifo_subfifo_intr, show);
		NV_INFO(dev, "PFIFO%d: ch %d subc %d mthd 0x%04x data 0x%08x\n",
			     unit, chid, subc, mthd, data);
	}

	nv_wr32(dev, 0x0400c0 + (unit * 0x2000), 0x80600008);
	nv_wr32(dev, 0x040108 + (unit * 0x2000), stat);
}

static void
nvc0_fifo_isr(struct drm_device *dev)
{
	u32 stat = nv_rd32(dev, 0x002100);

	if (stat & 0x00000100) {
		NV_INFO(dev, "PFIFO: unknown status 0x00000100\n");
		nv_wr32(dev, 0x002100, 0x00000100);
		stat &= ~0x00000100;
	}

	if (stat & 0x10000000) {
		u32 units = nv_rd32(dev, 0x00259c);
		u32 u = units;

		while (u) {
			int i = ffs(u) - 1;
			nvc0_fifo_isr_vm_fault(dev, i);
			u &= ~(1 << i);
		}

		nv_wr32(dev, 0x00259c, units);
		stat &= ~0x10000000;
	}

	if (stat & 0x20000000) {
		u32 units = nv_rd32(dev, 0x0025a0);
		u32 u = units;

		while (u) {
			int i = ffs(u) - 1;
			nvc0_fifo_isr_subfifo_intr(dev, i);
			u &= ~(1 << i);
		}

		nv_wr32(dev, 0x0025a0, units);
		stat &= ~0x20000000;
	}

	if (stat & 0x40000000) {
		NV_INFO(dev, "PFIFO: unknown status 0x40000000\n");
		nv_mask(dev, 0x002a00, 0x00000000, 0x00000000);
		stat &= ~0x40000000;
	}

	if (stat) {
		NV_INFO(dev, "PFIFO: unhandled status 0x%08x\n", stat);
		nv_wr32(dev, 0x002100, stat);
		nv_wr32(dev, 0x002140, 0);
	}
}
