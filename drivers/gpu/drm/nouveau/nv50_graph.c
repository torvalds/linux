/*
 * Copyright (C) 2007 Ben Skeggs.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_ramht.h"
#include "nouveau_grctx.h"
#include "nouveau_dma.h"
#include "nv50_evo.h"

static int nv50_graph_register(struct drm_device *);

static void
nv50_graph_init_reset(struct drm_device *dev)
{
	uint32_t pmc_e = NV_PMC_ENABLE_PGRAPH | (1 << 21);

	NV_DEBUG(dev, "\n");

	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) & ~pmc_e);
	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) |  pmc_e);
}

static void
nv50_graph_init_intr(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");

	nv_wr32(dev, NV03_PGRAPH_INTR, 0xffffffff);
	nv_wr32(dev, 0x400138, 0xffffffff);
	nv_wr32(dev, NV40_PGRAPH_INTR_EN, 0xffffffff);
}

static void
nv50_graph_init_regs__nv(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t units = nv_rd32(dev, 0x1540);
	int i;

	NV_DEBUG(dev, "\n");

	nv_wr32(dev, 0x400804, 0xc0000000);
	nv_wr32(dev, 0x406800, 0xc0000000);
	nv_wr32(dev, 0x400c04, 0xc0000000);
	nv_wr32(dev, 0x401800, 0xc0000000);
	nv_wr32(dev, 0x405018, 0xc0000000);
	nv_wr32(dev, 0x402000, 0xc0000000);

	for (i = 0; i < 16; i++) {
		if (units & 1 << i) {
			if (dev_priv->chipset < 0xa0) {
				nv_wr32(dev, 0x408900 + (i << 12), 0xc0000000);
				nv_wr32(dev, 0x408e08 + (i << 12), 0xc0000000);
				nv_wr32(dev, 0x408314 + (i << 12), 0xc0000000);
			} else {
				nv_wr32(dev, 0x408600 + (i << 11), 0xc0000000);
				nv_wr32(dev, 0x408708 + (i << 11), 0xc0000000);
				nv_wr32(dev, 0x40831c + (i << 11), 0xc0000000);
			}
		}
	}

	nv_wr32(dev, 0x400108, 0xffffffff);

	nv_wr32(dev, 0x400824, 0x00004000);
	nv_wr32(dev, 0x400500, 0x00010001);
}

static void
nv50_graph_init_regs(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");

	nv_wr32(dev, NV04_PGRAPH_DEBUG_3,
				(1 << 2) /* HW_CONTEXT_SWITCH_ENABLED */);
	nv_wr32(dev, 0x402ca8, 0x800);
}

static int
nv50_graph_init_ctxctl(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_grctx ctx = {};
	uint32_t *cp;
	int i;

	NV_DEBUG(dev, "\n");

	cp = kmalloc(512 * 4, GFP_KERNEL);
	if (!cp) {
		NV_ERROR(dev, "failed to allocate ctxprog\n");
		dev_priv->engine.graph.accel_blocked = true;
		return 0;
	}

	ctx.dev = dev;
	ctx.mode = NOUVEAU_GRCTX_PROG;
	ctx.data = cp;
	ctx.ctxprog_max = 512;
	if (!nv50_grctx_init(&ctx)) {
		dev_priv->engine.graph.grctx_size = ctx.ctxvals_pos * 4;

		nv_wr32(dev, NV40_PGRAPH_CTXCTL_UCODE_INDEX, 0);
		for (i = 0; i < ctx.ctxprog_len; i++)
			nv_wr32(dev, NV40_PGRAPH_CTXCTL_UCODE_DATA, cp[i]);
	} else {
		dev_priv->engine.graph.accel_blocked = true;
	}
	kfree(cp);

	nv_wr32(dev, 0x400320, 4);
	nv_wr32(dev, NV40_PGRAPH_CTXCTL_CUR, 0);
	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_POINTER, 0);
	return 0;
}

int
nv50_graph_init(struct drm_device *dev)
{
	int ret;

	NV_DEBUG(dev, "\n");

	nv50_graph_init_reset(dev);
	nv50_graph_init_regs__nv(dev);
	nv50_graph_init_regs(dev);

	ret = nv50_graph_init_ctxctl(dev);
	if (ret)
		return ret;

	ret = nv50_graph_register(dev);
	if (ret)
		return ret;
	nv50_graph_init_intr(dev);
	return 0;
}

void
nv50_graph_takedown(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");
}

void
nv50_graph_fifo_access(struct drm_device *dev, bool enabled)
{
	const uint32_t mask = 0x00010001;

	if (enabled)
		nv_wr32(dev, 0x400500, nv_rd32(dev, 0x400500) | mask);
	else
		nv_wr32(dev, 0x400500, nv_rd32(dev, 0x400500) & ~mask);
}

struct nouveau_channel *
nv50_graph_channel(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t inst;
	int i;

	/* Be sure we're not in the middle of a context switch or bad things
	 * will happen, such as unloading the wrong pgraph context.
	 */
	if (!nv_wait(dev, 0x400300, 0x00000001, 0x00000000))
		NV_ERROR(dev, "Ctxprog is still running\n");

	inst = nv_rd32(dev, NV50_PGRAPH_CTXCTL_CUR);
	if (!(inst & NV50_PGRAPH_CTXCTL_CUR_LOADED))
		return NULL;
	inst = (inst & NV50_PGRAPH_CTXCTL_CUR_INSTANCE) << 12;

	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		struct nouveau_channel *chan = dev_priv->channels.ptr[i];

		if (chan && chan->ramin && chan->ramin->vinst == inst)
			return chan;
	}

	return NULL;
}

int
nv50_graph_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramin = chan->ramin;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_grctx ctx = {};
	int hdr, ret;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	ret = nouveau_gpuobj_new(dev, chan, pgraph->grctx_size, 0,
				 NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &chan->ramin_grctx);
	if (ret)
		return ret;

	hdr = (dev_priv->chipset == 0x50) ? 0x200 : 0x20;
	nv_wo32(ramin, hdr + 0x00, 0x00190002);
	nv_wo32(ramin, hdr + 0x04, chan->ramin_grctx->vinst +
				   pgraph->grctx_size - 1);
	nv_wo32(ramin, hdr + 0x08, chan->ramin_grctx->vinst);
	nv_wo32(ramin, hdr + 0x0c, 0);
	nv_wo32(ramin, hdr + 0x10, 0);
	nv_wo32(ramin, hdr + 0x14, 0x00010000);

	ctx.dev = chan->dev;
	ctx.mode = NOUVEAU_GRCTX_VALS;
	ctx.data = chan->ramin_grctx;
	nv50_grctx_init(&ctx);

	nv_wo32(chan->ramin_grctx, 0x00000, chan->ramin->vinst >> 12);

	dev_priv->engine.instmem.flush(dev);
	return 0;
}

void
nv50_graph_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	int i, hdr = (dev_priv->chipset == 0x50) ? 0x200 : 0x20;
	unsigned long flags;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	if (!chan->ramin)
		return;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	pgraph->fifo_access(dev, false);

	if (pgraph->channel(dev) == chan)
		pgraph->unload_context(dev);

	for (i = hdr; i < hdr + 24; i += 4)
		nv_wo32(chan->ramin, i, 0);
	dev_priv->engine.instmem.flush(dev);

	pgraph->fifo_access(dev, true);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	nouveau_gpuobj_ref(NULL, &chan->ramin_grctx);
}

static int
nv50_graph_do_load_context(struct drm_device *dev, uint32_t inst)
{
	uint32_t fifo = nv_rd32(dev, 0x400500);

	nv_wr32(dev, 0x400500, fifo & ~1);
	nv_wr32(dev, 0x400784, inst);
	nv_wr32(dev, 0x400824, nv_rd32(dev, 0x400824) | 0x40);
	nv_wr32(dev, 0x400320, nv_rd32(dev, 0x400320) | 0x11);
	nv_wr32(dev, 0x400040, 0xffffffff);
	(void)nv_rd32(dev, 0x400040);
	nv_wr32(dev, 0x400040, 0x00000000);
	nv_wr32(dev, 0x400304, nv_rd32(dev, 0x400304) | 1);

	if (nouveau_wait_for_idle(dev))
		nv_wr32(dev, 0x40032c, inst | (1<<31));
	nv_wr32(dev, 0x400500, fifo);

	return 0;
}

int
nv50_graph_load_context(struct nouveau_channel *chan)
{
	uint32_t inst = chan->ramin->vinst >> 12;

	NV_DEBUG(chan->dev, "ch%d\n", chan->id);
	return nv50_graph_do_load_context(chan->dev, inst);
}

int
nv50_graph_unload_context(struct drm_device *dev)
{
	uint32_t inst;

	inst  = nv_rd32(dev, NV50_PGRAPH_CTXCTL_CUR);
	if (!(inst & NV50_PGRAPH_CTXCTL_CUR_LOADED))
		return 0;
	inst &= NV50_PGRAPH_CTXCTL_CUR_INSTANCE;

	nouveau_wait_for_idle(dev);
	nv_wr32(dev, 0x400784, inst);
	nv_wr32(dev, 0x400824, nv_rd32(dev, 0x400824) | 0x20);
	nv_wr32(dev, 0x400304, nv_rd32(dev, 0x400304) | 0x01);
	nouveau_wait_for_idle(dev);

	nv_wr32(dev, NV50_PGRAPH_CTXCTL_CUR, inst);
	return 0;
}

void
nv50_graph_context_switch(struct drm_device *dev)
{
	uint32_t inst;

	nv50_graph_unload_context(dev);

	inst  = nv_rd32(dev, NV50_PGRAPH_CTXCTL_NEXT);
	inst &= NV50_PGRAPH_CTXCTL_NEXT_INSTANCE;
	nv50_graph_do_load_context(dev, inst);

	nv_wr32(dev, NV40_PGRAPH_INTR_EN, nv_rd32(dev,
		NV40_PGRAPH_INTR_EN) | NV_PGRAPH_INTR_CONTEXT_SWITCH);
}

static int
nv50_graph_nvsw_dma_vblsem(struct nouveau_channel *chan,
			   u32 class, u32 mthd, u32 data)
{
	struct nouveau_gpuobj *gpuobj;

	gpuobj = nouveau_ramht_find(chan, data);
	if (!gpuobj)
		return -ENOENT;

	if (nouveau_notifier_offset(gpuobj, NULL))
		return -EINVAL;

	chan->nvsw.vblsem = gpuobj;
	chan->nvsw.vblsem_offset = ~0;
	return 0;
}

static int
nv50_graph_nvsw_vblsem_offset(struct nouveau_channel *chan,
			      u32 class, u32 mthd, u32 data)
{
	if (nouveau_notifier_offset(chan->nvsw.vblsem, &data))
		return -ERANGE;

	chan->nvsw.vblsem_offset = data >> 2;
	return 0;
}

static int
nv50_graph_nvsw_vblsem_release_val(struct nouveau_channel *chan,
				   u32 class, u32 mthd, u32 data)
{
	chan->nvsw.vblsem_rval = data;
	return 0;
}

static int
nv50_graph_nvsw_vblsem_release(struct nouveau_channel *chan,
			       u32 class, u32 mthd, u32 data)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (!chan->nvsw.vblsem || chan->nvsw.vblsem_offset == ~0 || data > 1)
		return -EINVAL;

	drm_vblank_get(dev, data);
	list_add(&chan->nvsw.vbl_wait, &dev_priv->vbl_waiting);
	return 0;
}

static int
nv50_graph_nvsw_mthd_page_flip(struct nouveau_channel *chan,
			       u32 class, u32 mthd, u32 data)
{
	struct nouveau_page_flip_state s;

	if (!nouveau_finish_page_flip(chan, &s)) {
		/* XXX - Do something here */
	}

	return 0;
}

static int
nv50_graph_register(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->engine.graph.registered)
		return 0;

	NVOBJ_CLASS(dev, 0x506e, SW); /* nvsw */
	NVOBJ_MTHD (dev, 0x506e, 0x018c, nv50_graph_nvsw_dma_vblsem);
	NVOBJ_MTHD (dev, 0x506e, 0x0400, nv50_graph_nvsw_vblsem_offset);
	NVOBJ_MTHD (dev, 0x506e, 0x0404, nv50_graph_nvsw_vblsem_release_val);
	NVOBJ_MTHD (dev, 0x506e, 0x0408, nv50_graph_nvsw_vblsem_release);
	NVOBJ_MTHD (dev, 0x506e, 0x0500, nv50_graph_nvsw_mthd_page_flip);

	NVOBJ_CLASS(dev, 0x0030, GR); /* null */
	NVOBJ_CLASS(dev, 0x5039, GR); /* m2mf */
	NVOBJ_CLASS(dev, 0x502d, GR); /* 2d */
	NVOBJ_CLASS(dev, 0x50c0, GR); /* compute */
	NVOBJ_CLASS(dev, 0x85c0, GR); /* compute (nva3, nva5, nva8) */

	/* tesla */
	if (dev_priv->chipset == 0x50)
		NVOBJ_CLASS(dev, 0x5097, GR); /* tesla (nv50) */
	else
	if (dev_priv->chipset < 0xa0)
		NVOBJ_CLASS(dev, 0x8297, GR); /* tesla (nv8x/nv9x) */
	else {
		switch (dev_priv->chipset) {
		case 0xa0:
		case 0xaa:
		case 0xac:
			NVOBJ_CLASS(dev, 0x8397, GR);
			break;
		case 0xa3:
		case 0xa5:
		case 0xa8:
			NVOBJ_CLASS(dev, 0x8597, GR);
			break;
		case 0xaf:
			NVOBJ_CLASS(dev, 0x8697, GR);
			break;
		}
	}

	dev_priv->engine.graph.registered = true;
	return 0;
}

void
nv50_graph_tlb_flush(struct drm_device *dev)
{
	nv50_vm_flush(dev, 0);
}

void
nv86_graph_tlb_flush(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	bool idle, timeout = false;
	unsigned long flags;
	u64 start;
	u32 tmp;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_mask(dev, 0x400500, 0x00000001, 0x00000000);

	start = ptimer->read(dev);
	do {
		idle = true;

		for (tmp = nv_rd32(dev, 0x400380); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nv_rd32(dev, 0x400384); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}

		for (tmp = nv_rd32(dev, 0x400388); tmp && idle; tmp >>= 3) {
			if ((tmp & 7) == 1)
				idle = false;
		}
	} while (!idle && !(timeout = ptimer->read(dev) - start > 2000000000));

	if (timeout) {
		NV_ERROR(dev, "PGRAPH TLB flush idle timeout fail: "
			      "0x%08x 0x%08x 0x%08x 0x%08x\n",
			 nv_rd32(dev, 0x400700), nv_rd32(dev, 0x400380),
			 nv_rd32(dev, 0x400384), nv_rd32(dev, 0x400388));
	}

	nv50_vm_flush(dev, 0);

	nv_mask(dev, 0x400500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);
}
