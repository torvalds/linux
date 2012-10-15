/*
 * Copyright (C) 2012 Ben Skeggs.
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
#include "nouveau_fifo.h"
#include "nouveau_ramht.h"
#include "nouveau_vm.h"

struct nv84_fifo_priv {
	struct nouveau_fifo_priv base;
	struct nouveau_gpuobj *playlist[2];
	int cur_playlist;
};

struct nv84_fifo_chan {
	struct nouveau_fifo_chan base;
	struct nouveau_gpuobj *ramfc;
	struct nouveau_gpuobj *cache;
};

static int
nv84_fifo_context_new(struct nouveau_channel *chan, int engine)
{
	struct nv84_fifo_priv *priv = nv_engine(chan->dev, engine);
	struct nv84_fifo_chan *fctx;
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
        u64 ib_offset = chan->pushbuf_base + chan->dma.ib_base * 4;
	u64 instance;
	unsigned long flags;
	int ret;

	fctx = chan->engctx[engine] = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;
	atomic_inc(&chan->vm->engref[engine]);

	chan->user = ioremap(pci_resource_start(dev->pdev, 0) +
			     NV50_USER(chan->id), PAGE_SIZE);
	if (!chan->user) {
		ret = -ENOMEM;
		goto error;
	}

	ret = nouveau_gpuobj_new(dev, chan, 256, 256, NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &fctx->ramfc);
	if (ret)
		goto error;

	instance = fctx->ramfc->vinst >> 8;

	ret = nouveau_gpuobj_new(dev, chan, 4096, 1024, 0, &fctx->cache);
	if (ret)
		goto error;

	nv_wo32(fctx->ramfc, 0x3c, 0x403f6078);
	nv_wo32(fctx->ramfc, 0x40, 0x00000000);
	nv_wo32(fctx->ramfc, 0x44, 0x01003fff);
	nv_wo32(fctx->ramfc, 0x48, chan->pushbuf->cinst >> 4);
	nv_wo32(fctx->ramfc, 0x50, lower_32_bits(ib_offset));
	nv_wo32(fctx->ramfc, 0x54, upper_32_bits(ib_offset) |
				   drm_order(chan->dma.ib_max + 1) << 16);
	nv_wo32(fctx->ramfc, 0x60, 0x7fffffff);
	nv_wo32(fctx->ramfc, 0x78, 0x00000000);
	nv_wo32(fctx->ramfc, 0x7c, 0x30000001);
	nv_wo32(fctx->ramfc, 0x80, ((chan->ramht->bits - 9) << 27) |
				   (4 << 24) /* SEARCH_FULL */ |
				   (chan->ramht->gpuobj->cinst >> 4));
	nv_wo32(fctx->ramfc, 0x88, fctx->cache->vinst >> 10);
	nv_wo32(fctx->ramfc, 0x98, chan->ramin->vinst >> 12);

	nv_wo32(chan->ramin, 0x00, chan->id);
	nv_wo32(chan->ramin, 0x04, fctx->ramfc->vinst >> 8);

	dev_priv->engine.instmem.flush(dev);

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_wr32(dev, 0x002600 + (chan->id * 4), 0x80000000 | instance);
	nv50_fifo_playlist_update(dev);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

error:
	if (ret)
		priv->base.base.context_del(chan, engine);
	return ret;
}

static void
nv84_fifo_context_del(struct nouveau_channel *chan, int engine)
{
	struct nv84_fifo_chan *fctx = chan->engctx[engine];
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;
	u32 save;

	/* remove channel from playlist, will context switch if active */
	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_mask(dev, 0x002600 + (chan->id * 4), 0x80000000, 0x00000000);
	nv50_fifo_playlist_update(dev);

	save = nv_mask(dev, 0x002520, 0x0000003f, 0x15);

	/* tell any engines on this channel to unload their contexts */
	nv_wr32(dev, 0x0032fc, chan->ramin->vinst >> 12);
	if (!nv_wait_ne(dev, 0x0032fc, 0xffffffff, 0xffffffff))
		NV_INFO(dev, "PFIFO: channel %d unload timeout\n", chan->id);

	nv_wr32(dev, 0x002520, save);

	nv_wr32(dev, 0x002600 + (chan->id * 4), 0x00000000);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	/* clean up */
	if (chan->user) {
		iounmap(chan->user);
		chan->user = NULL;
	}

	nouveau_gpuobj_ref(NULL, &fctx->ramfc);
	nouveau_gpuobj_ref(NULL, &fctx->cache);

	atomic_dec(&chan->vm->engref[engine]);
	chan->engctx[engine] = NULL;
	kfree(fctx);
}

static int
nv84_fifo_init(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv84_fifo_chan *fctx;
	u32 instance;
	int i;

	nv_mask(dev, 0x000200, 0x00000100, 0x00000000);
	nv_mask(dev, 0x000200, 0x00000100, 0x00000100);
	nv_wr32(dev, 0x00250c, 0x6f3cfc34);
	nv_wr32(dev, 0x002044, 0x01003fff);

	nv_wr32(dev, 0x002100, 0xffffffff);
	nv_wr32(dev, 0x002140, 0xffffffff);

	for (i = 0; i < 128; i++) {
		struct nouveau_channel *chan = dev_priv->channels.ptr[i];
		if (chan && (fctx = chan->engctx[engine]))
			instance = 0x80000000 | fctx->ramfc->vinst >> 8;
		else
			instance = 0x00000000;
		nv_wr32(dev, 0x002600 + (i * 4), instance);
	}

	nv50_fifo_playlist_update(dev);

	nv_wr32(dev, 0x003200, 1);
	nv_wr32(dev, 0x003250, 1);
	nv_wr32(dev, 0x002500, 1);
	return 0;
}

static int
nv84_fifo_fini(struct drm_device *dev, int engine, bool suspend)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv84_fifo_priv *priv = nv_engine(dev, engine);
	int i;
	u32 save;

	/* set playlist length to zero, fifo will unload context */
	nv_wr32(dev, 0x0032ec, 0);

	save = nv_mask(dev, 0x002520, 0x0000003f, 0x15);

	/* tell all connected engines to unload their contexts */
	for (i = 0; i < priv->base.channels; i++) {
		struct nouveau_channel *chan = dev_priv->channels.ptr[i];
		if (chan)
			nv_wr32(dev, 0x0032fc, chan->ramin->vinst >> 12);
		if (!nv_wait_ne(dev, 0x0032fc, 0xffffffff, 0xffffffff)) {
			NV_INFO(dev, "PFIFO: channel %d unload timeout\n", i);
			return -EBUSY;
		}
	}

	nv_wr32(dev, 0x002520, save);
	nv_wr32(dev, 0x002140, 0);
	return 0;
}

int
nv84_fifo_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv84_fifo_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.base.destroy = nv50_fifo_destroy;
	priv->base.base.init = nv84_fifo_init;
	priv->base.base.fini = nv84_fifo_fini;
	priv->base.base.context_new = nv84_fifo_context_new;
	priv->base.base.context_del = nv84_fifo_context_del;
	priv->base.base.tlb_flush = nv50_fifo_tlb_flush;
	priv->base.channels = 127;
	dev_priv->eng[NVOBJ_ENGINE_FIFO] = &priv->base.base;

	ret = nouveau_gpuobj_new(dev, NULL, priv->base.channels * 4, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC, &priv->playlist[0]);
	if (ret)
		goto error;

	ret = nouveau_gpuobj_new(dev, NULL, priv->base.channels * 4, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC, &priv->playlist[1]);
	if (ret)
		goto error;

	nouveau_irq_register(dev, 8, nv04_fifo_isr);
error:
	if (ret)
		priv->base.base.destroy(dev, NVOBJ_ENGINE_FIFO);
	return ret;
}
