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

struct nv50_fifo_priv {
	struct nouveau_fifo_priv base;
	struct nouveau_gpuobj *playlist[2];
	int cur_playlist;
};

struct nv50_fifo_chan {
	struct nouveau_fifo_chan base;
};

void
nv50_fifo_playlist_update(struct drm_device *dev)
{
	struct nv50_fifo_priv *priv = nv_engine(dev, NVOBJ_ENGINE_FIFO);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *cur;
	int i, p;

	cur = priv->playlist[priv->cur_playlist];
	priv->cur_playlist = !priv->cur_playlist;

	for (i = 0, p = 0; i < priv->base.channels; i++) {
		if (nv_rd32(dev, 0x002600 + (i * 4)) & 0x80000000)
			nv_wo32(cur, p++ * 4, i);
	}

	dev_priv->engine.instmem.flush(dev);

	nv_wr32(dev, 0x0032f4, cur->vinst >> 12);
	nv_wr32(dev, 0x0032ec, p);
	nv_wr32(dev, 0x002500, 0x00000101);
}

static int
nv50_fifo_context_new(struct nouveau_channel *chan, int engine)
{
	struct nv50_fifo_priv *priv = nv_engine(chan->dev, engine);
	struct nv50_fifo_chan *fctx;
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u64 ib_offset = chan->pushbuf_base + chan->dma.ib_base * 4;
	u64 instance = chan->ramin->vinst >> 12;
	unsigned long flags;
	int ret = 0, i;

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

	for (i = 0; i < 0x100; i += 4)
		nv_wo32(chan->ramin, i, 0x00000000);
	nv_wo32(chan->ramin, 0x3c, 0x403f6078);
	nv_wo32(chan->ramin, 0x40, 0x00000000);
	nv_wo32(chan->ramin, 0x44, 0x01003fff);
	nv_wo32(chan->ramin, 0x48, chan->pushbuf->cinst >> 4);
	nv_wo32(chan->ramin, 0x50, lower_32_bits(ib_offset));
	nv_wo32(chan->ramin, 0x54, upper_32_bits(ib_offset) |
				   drm_order(chan->dma.ib_max + 1) << 16);
	nv_wo32(chan->ramin, 0x60, 0x7fffffff);
	nv_wo32(chan->ramin, 0x78, 0x00000000);
	nv_wo32(chan->ramin, 0x7c, 0x30000001);
	nv_wo32(chan->ramin, 0x80, ((chan->ramht->bits - 9) << 27) |
				   (4 << 24) /* SEARCH_FULL */ |
				   (chan->ramht->gpuobj->cinst >> 4));

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

static bool
nv50_fifo_kickoff(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	bool done = true;
	u32 me;

	/* HW bug workaround:
	 *
	 * PFIFO will hang forever if the connected engines don't report
	 * that they've processed the context switch request.
	 *
	 * In order for the kickoff to work, we need to ensure all the
	 * connected engines are in a state where they can answer.
	 *
	 * Newer chipsets don't seem to suffer from this issue, and well,
	 * there's also a "ignore these engines" bitmask reg we can use
	 * if we hit the issue there..
	 */

	/* PME: make sure engine is enabled */
	me = nv_mask(dev, 0x00b860, 0x00000001, 0x00000001);

	/* do the kickoff... */
	nv_wr32(dev, 0x0032fc, chan->ramin->vinst >> 12);
	if (!nv_wait_ne(dev, 0x0032fc, 0xffffffff, 0xffffffff)) {
		NV_INFO(dev, "PFIFO: channel %d unload timeout\n", chan->id);
		done = false;
	}

	/* restore any engine states we changed, and exit */
	nv_wr32(dev, 0x00b860, me);
	return done;
}

static void
nv50_fifo_context_del(struct nouveau_channel *chan, int engine)
{
	struct nv50_fifo_chan *fctx = chan->engctx[engine];
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;

	/* remove channel from playlist, will context switch if active */
	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_mask(dev, 0x002600 + (chan->id * 4), 0x80000000, 0x00000000);
	nv50_fifo_playlist_update(dev);

	/* tell any engines on this channel to unload their contexts */
	nv50_fifo_kickoff(chan);

	nv_wr32(dev, 0x002600 + (chan->id * 4), 0x00000000);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	/* clean up */
	if (chan->user) {
		iounmap(chan->user);
		chan->user = NULL;
	}

	atomic_dec(&chan->vm->engref[engine]);
	chan->engctx[engine] = NULL;
	kfree(fctx);
}

static int
nv50_fifo_init(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
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
		if (chan && chan->engctx[engine])
			instance = 0x80000000 | chan->ramin->vinst >> 12;
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
nv50_fifo_fini(struct drm_device *dev, int engine, bool suspend)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_fifo_priv *priv = nv_engine(dev, engine);
	int i;

	/* set playlist length to zero, fifo will unload context */
	nv_wr32(dev, 0x0032ec, 0);

	/* tell all connected engines to unload their contexts */
	for (i = 0; i < priv->base.channels; i++) {
		struct nouveau_channel *chan = dev_priv->channels.ptr[i];
		if (chan && !nv50_fifo_kickoff(chan))
			return -EBUSY;
	}

	nv_wr32(dev, 0x002140, 0);
	return 0;
}

void
nv50_fifo_tlb_flush(struct drm_device *dev, int engine)
{
	nv50_vm_flush_engine(dev, 5);
}

void
nv50_fifo_destroy(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_fifo_priv *priv = nv_engine(dev, engine);

	nouveau_irq_unregister(dev, 8);

	nouveau_gpuobj_ref(NULL, &priv->playlist[0]);
	nouveau_gpuobj_ref(NULL, &priv->playlist[1]);

	dev_priv->eng[engine] = NULL;
	kfree(priv);
}

int
nv50_fifo_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_fifo_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.base.destroy = nv50_fifo_destroy;
	priv->base.base.init = nv50_fifo_init;
	priv->base.base.fini = nv50_fifo_fini;
	priv->base.base.context_new = nv50_fifo_context_new;
	priv->base.base.context_del = nv50_fifo_context_del;
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
