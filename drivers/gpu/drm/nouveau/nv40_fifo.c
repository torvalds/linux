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
} nv40_ramfc[] = {
	{ 32,  0, 0x00,  0, NV04_PFIFO_CACHE1_DMA_PUT },
	{ 32,  0, 0x04,  0, NV04_PFIFO_CACHE1_DMA_GET },
	{ 32,  0, 0x08,  0, NV10_PFIFO_CACHE1_REF_CNT },
	{ 32,  0, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_INSTANCE },
	{ 32,  0, 0x10,  0, NV04_PFIFO_CACHE1_DMA_DCOUNT },
	{ 32,  0, 0x14,  0, NV04_PFIFO_CACHE1_DMA_STATE },
	{ 28,  0, 0x18,  0, NV04_PFIFO_CACHE1_DMA_FETCH },
	{  2, 28, 0x18, 28, 0x002058 },
	{ 32,  0, 0x1c,  0, NV04_PFIFO_CACHE1_ENGINE },
	{ 32,  0, 0x20,  0, NV04_PFIFO_CACHE1_PULL1 },
	{ 32,  0, 0x24,  0, NV10_PFIFO_CACHE1_ACQUIRE_VALUE },
	{ 32,  0, 0x28,  0, NV10_PFIFO_CACHE1_ACQUIRE_TIMESTAMP },
	{ 32,  0, 0x2c,  0, NV10_PFIFO_CACHE1_ACQUIRE_TIMEOUT },
	{ 32,  0, 0x30,  0, NV10_PFIFO_CACHE1_SEMAPHORE },
	{ 32,  0, 0x34,  0, NV10_PFIFO_CACHE1_DMA_SUBROUTINE },
	{ 32,  0, 0x38,  0, NV40_PFIFO_GRCTX_INSTANCE },
	{ 17,  0, 0x3c,  0, NV04_PFIFO_DMA_TIMESLICE },
	{ 32,  0, 0x40,  0, 0x0032e4 },
	{ 32,  0, 0x44,  0, 0x0032e8 },
	{ 32,  0, 0x4c,  0, 0x002088 },
	{ 32,  0, 0x50,  0, 0x003300 },
	{ 32,  0, 0x54,  0, 0x00330c },
	{}
};

struct nv40_fifo_priv {
	struct nouveau_fifo_priv base;
	struct ramfc_desc *ramfc_desc;
};

struct nv40_fifo_chan {
	struct nouveau_fifo_chan base;
	struct nouveau_gpuobj *ramfc;
};

static int
nv40_fifo_context_new(struct nouveau_channel *chan, int engine)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv40_fifo_priv *priv = nv_engine(dev, engine);
	struct nv40_fifo_chan *fctx;
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
				      chan->id * 128, ~0, 128,
				      NVOBJ_FLAG_ZERO_ALLOC |
				      NVOBJ_FLAG_ZERO_FREE, &fctx->ramfc);
	if (ret)
		goto error;

	nv_wo32(fctx->ramfc, 0x00, chan->pushbuf_base);
	nv_wo32(fctx->ramfc, 0x04, chan->pushbuf_base);
	nv_wo32(fctx->ramfc, 0x0c, chan->pushbuf->pinst >> 4);
	nv_wo32(fctx->ramfc, 0x18, 0x30000000 |
				   NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
				   NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
#ifdef __BIG_ENDIAN
				   NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
				   NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8);
	nv_wo32(fctx->ramfc, 0x3c, 0x0001ffff);

	/* enable dma mode on the channel */
	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_mask(dev, NV04_PFIFO_MODE, (1 << chan->id), (1 << chan->id));
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	/*XXX: remove this later, need fifo engine context commit hook */
	nouveau_gpuobj_ref(fctx->ramfc, &chan->ramfc);

error:
	if (ret)
		priv->base.base.context_del(chan, engine);
	return ret;
}

static int
nv40_fifo_init(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv40_fifo_priv *priv = nv_engine(dev, engine);
	int i;

	nv_mask(dev, NV03_PMC_ENABLE, NV_PMC_ENABLE_PFIFO, 0);
	nv_mask(dev, NV03_PMC_ENABLE, NV_PMC_ENABLE_PFIFO, NV_PMC_ENABLE_PFIFO);

	nv_wr32(dev, 0x002040, 0x000000ff);
	nv_wr32(dev, 0x002044, 0x2101ffff);
	nv_wr32(dev, 0x002058, 0x00000001);

	nv_wr32(dev, NV03_PFIFO_RAMHT, (0x03 << 24) /* search 128 */ |
				       ((dev_priv->ramht->bits - 9) << 16) |
				       (dev_priv->ramht->gpuobj->pinst >> 8));
	nv_wr32(dev, NV03_PFIFO_RAMRO, dev_priv->ramro->pinst >> 8);

	switch (dev_priv->chipset) {
	case 0x47:
	case 0x49:
	case 0x4b:
		nv_wr32(dev, 0x002230, 0x00000001);
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x45:
	case 0x48:
		nv_wr32(dev, 0x002220, 0x00030002);
		break;
	default:
		nv_wr32(dev, 0x002230, 0x00000000);
		nv_wr32(dev, 0x002220, ((dev_priv->vram_size - 512 * 1024 +
					 dev_priv->ramfc->pinst) >> 16) |
				       0x00030000);
		break;
	}

	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1, priv->base.channels);

	nv_wr32(dev, NV03_PFIFO_INTR_0, 0xffffffff);
	nv_wr32(dev, NV03_PFIFO_INTR_EN_0, 0xffffffff);

	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0, 1);
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 1);
	nv_wr32(dev, NV03_PFIFO_CACHES, 1);

	for (i = 0; i < priv->base.channels; i++) {
		if (dev_priv->channels.ptr[i])
			nv_mask(dev, NV04_PFIFO_MODE, (1 << i), (1 << i));
	}

	return 0;
}

int
nv40_fifo_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv40_fifo_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.base.destroy = nv04_fifo_destroy;
	priv->base.base.init = nv40_fifo_init;
	priv->base.base.fini = nv04_fifo_fini;
	priv->base.base.context_new = nv40_fifo_context_new;
	priv->base.base.context_del = nv04_fifo_context_del;
	priv->base.channels = 31;
	priv->ramfc_desc = nv40_ramfc;
	dev_priv->eng[NVOBJ_ENGINE_FIFO] = &priv->base.base;

	nouveau_irq_register(dev, 8, nv04_fifo_isr);
	return 0;
}
