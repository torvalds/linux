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
#include "nouveau_util.h"
#include "nouveau_ramht.h"

static struct ramfc_desc {
	unsigned bits:6;
	unsigned ctxs:5;
	unsigned ctxp:8;
	unsigned regs:5;
	unsigned regp;
} nv10_ramfc[] = {
	{ 32,  0, 0x00,  0, NV04_PFIFO_CACHE1_DMA_PUT },
	{ 32,  0, 0x04,  0, NV04_PFIFO_CACHE1_DMA_GET },
	{ 32,  0, 0x08,  0, NV10_PFIFO_CACHE1_REF_CNT },
	{ 16,  0, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_INSTANCE },
	{ 16, 16, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_DCOUNT },
	{ 32,  0, 0x10,  0, NV04_PFIFO_CACHE1_DMA_STATE },
	{ 32,  0, 0x14,  0, NV04_PFIFO_CACHE1_DMA_FETCH },
	{ 32,  0, 0x18,  0, NV04_PFIFO_CACHE1_ENGINE },
	{ 32,  0, 0x1c,  0, NV04_PFIFO_CACHE1_PULL1 },
	{}
};

struct nv10_fifo_priv {
	struct nouveau_fifo_priv base;
	struct ramfc_desc *ramfc_desc;
};

struct nv10_fifo_chan {
	struct nouveau_fifo_chan base;
	struct nouveau_gpuobj *ramfc;
};

static int
nv10_fifo_context_new(struct nouveau_channel *chan, int engine)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv10_fifo_priv *priv = nv_engine(dev, engine);
	struct nv10_fifo_chan *fctx;
	unsigned long flags;
	int ret;

	fctx = chan->engctx[engine] = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	/* map channel control registers */
	chan->user = ioremap(pci_resource_start(dev->pdev, 0) +
			     NV03_USER(chan->id), PAGE_SIZE);
	if (!chan->user) {
		ret = -ENOMEM;
		goto error;
	}

	/* initialise default fifo context */
	ret = nouveau_gpuobj_new_fake(dev, dev_priv->ramfc->pinst +
				      chan->id * 32, ~0, 32,
				      NVOBJ_FLAG_ZERO_FREE, &fctx->ramfc);
	if (ret)
		goto error;

	nv_wo32(fctx->ramfc, 0x00, chan->pushbuf_base);
	nv_wo32(fctx->ramfc, 0x04, chan->pushbuf_base);
	nv_wo32(fctx->ramfc, 0x08, 0x00000000);
	nv_wo32(fctx->ramfc, 0x0c, chan->pushbuf->pinst >> 4);
	nv_wo32(fctx->ramfc, 0x10, 0x00000000);
	nv_wo32(fctx->ramfc, 0x14, NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
				   NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
#ifdef __BIG_ENDIAN
				   NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
				   NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8);
	nv_wo32(fctx->ramfc, 0x18, 0x00000000);
	nv_wo32(fctx->ramfc, 0x1c, 0x00000000);

	/* enable dma mode on the channel */
	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_mask(dev, NV04_PFIFO_MODE, (1 << chan->id), (1 << chan->id));
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

error:
	if (ret)
		priv->base.base.context_del(chan, engine);
	return ret;
}

int
nv10_fifo_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv10_fifo_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.base.destroy = nv04_fifo_destroy;
	priv->base.base.init = nv04_fifo_init;
	priv->base.base.fini = nv04_fifo_fini;
	priv->base.base.context_new = nv10_fifo_context_new;
	priv->base.base.context_del = nv04_fifo_context_del;
	priv->base.channels = 31;
	priv->ramfc_desc = nv10_ramfc;
	dev_priv->eng[NVOBJ_ENGINE_FIFO] = &priv->base.base;

	nouveau_irq_register(dev, 8, nv04_fifo_isr);
	return 0;
}
