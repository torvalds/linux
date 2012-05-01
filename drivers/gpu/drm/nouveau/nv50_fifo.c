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
#include "nouveau_vm.h"

static void
nv50_fifo_playlist_update(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_gpuobj *cur;
	int i, p;

	NV_DEBUG(dev, "\n");

	cur = pfifo->playlist[pfifo->cur_playlist];
	pfifo->cur_playlist = !pfifo->cur_playlist;

	for (i = 0, p = 0; i < pfifo->channels; i++) {
		if (nv_rd32(dev, 0x002600 + (i * 4)) & 0x80000000)
			nv_wo32(cur, p++ * 4, i);
	}

	dev_priv->engine.instmem.flush(dev);

	nv_wr32(dev, 0x32f4, cur->vinst >> 12);
	nv_wr32(dev, 0x32ec, p);
	nv_wr32(dev, 0x2500, 0x101);
}

static void
nv50_fifo_channel_enable(struct drm_device *dev, int channel)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->channels.ptr[channel];
	uint32_t inst;

	NV_DEBUG(dev, "ch%d\n", channel);

	if (dev_priv->chipset == 0x50)
		inst = chan->ramfc->vinst >> 12;
	else
		inst = chan->ramfc->vinst >> 8;

	nv_wr32(dev, NV50_PFIFO_CTX_TABLE(channel), inst |
		     NV50_PFIFO_CTX_TABLE_CHANNEL_ENABLED);
}

static void
nv50_fifo_channel_disable(struct drm_device *dev, int channel)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t inst;

	NV_DEBUG(dev, "ch%d\n", channel);

	if (dev_priv->chipset == 0x50)
		inst = NV50_PFIFO_CTX_TABLE_INSTANCE_MASK_G80;
	else
		inst = NV50_PFIFO_CTX_TABLE_INSTANCE_MASK_G84;
	nv_wr32(dev, NV50_PFIFO_CTX_TABLE(channel), inst);
}

static void
nv50_fifo_init_reset(struct drm_device *dev)
{
	uint32_t pmc_e = NV_PMC_ENABLE_PFIFO;

	NV_DEBUG(dev, "\n");

	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) & ~pmc_e);
	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) |  pmc_e);
}

static void
nv50_fifo_init_intr(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");

	nouveau_irq_register(dev, 8, nv04_fifo_isr);
	nv_wr32(dev, NV03_PFIFO_INTR_0, 0xFFFFFFFF);
	nv_wr32(dev, NV03_PFIFO_INTR_EN_0, 0xFFFFFFFF);
}

static void
nv50_fifo_init_context_table(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i;

	NV_DEBUG(dev, "\n");

	for (i = 0; i < NV50_PFIFO_CTX_TABLE__SIZE; i++) {
		if (dev_priv->channels.ptr[i])
			nv50_fifo_channel_enable(dev, i);
		else
			nv50_fifo_channel_disable(dev, i);
	}

	nv50_fifo_playlist_update(dev);
}

static void
nv50_fifo_init_regs__nv(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");

	nv_wr32(dev, 0x250c, 0x6f3cfc34);
}

static void
nv50_fifo_init_regs(struct drm_device *dev)
{
	NV_DEBUG(dev, "\n");

	nv_wr32(dev, 0x2500, 0);
	nv_wr32(dev, 0x3250, 0);
	nv_wr32(dev, 0x3220, 0);
	nv_wr32(dev, 0x3204, 0);
	nv_wr32(dev, 0x3210, 0);
	nv_wr32(dev, 0x3270, 0);
	nv_wr32(dev, 0x2044, 0x01003fff);

	/* Enable dummy channels setup by nv50_instmem.c */
	nv50_fifo_channel_enable(dev, 0);
	nv50_fifo_channel_enable(dev, 127);
}

int
nv50_fifo_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	int ret;

	NV_DEBUG(dev, "\n");

	if (pfifo->playlist[0]) {
		pfifo->cur_playlist = !pfifo->cur_playlist;
		goto just_reset;
	}

	ret = nouveau_gpuobj_new(dev, NULL, 128*4, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC,
				 &pfifo->playlist[0]);
	if (ret) {
		NV_ERROR(dev, "error creating playlist 0: %d\n", ret);
		return ret;
	}

	ret = nouveau_gpuobj_new(dev, NULL, 128*4, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC,
				 &pfifo->playlist[1]);
	if (ret) {
		nouveau_gpuobj_ref(NULL, &pfifo->playlist[0]);
		NV_ERROR(dev, "error creating playlist 1: %d\n", ret);
		return ret;
	}

just_reset:
	nv50_fifo_init_reset(dev);
	nv50_fifo_init_intr(dev);
	nv50_fifo_init_context_table(dev);
	nv50_fifo_init_regs__nv(dev);
	nv50_fifo_init_regs(dev);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0, 1);
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 1);
	nv_wr32(dev, NV03_PFIFO_CACHES, 1);

	return 0;
}

void
nv50_fifo_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;

	NV_DEBUG(dev, "\n");

	if (!pfifo->playlist[0])
		return;

	nv_wr32(dev, 0x2140, 0x00000000);
	nouveau_irq_unregister(dev, 8);

	nouveau_gpuobj_ref(NULL, &pfifo->playlist[0]);
	nouveau_gpuobj_ref(NULL, &pfifo->playlist[1]);
}

int
nv50_fifo_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramfc = NULL;
        uint64_t ib_offset = chan->pushbuf_base + chan->dma.ib_base * 4;
	unsigned long flags;
	int ret;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	if (dev_priv->chipset == 0x50) {
		ret = nouveau_gpuobj_new_fake(dev, chan->ramin->pinst,
					      chan->ramin->vinst, 0x100,
					      NVOBJ_FLAG_ZERO_ALLOC |
					      NVOBJ_FLAG_ZERO_FREE,
					      &chan->ramfc);
		if (ret)
			return ret;

		ret = nouveau_gpuobj_new_fake(dev, chan->ramin->pinst + 0x0400,
					      chan->ramin->vinst + 0x0400,
					      4096, 0, &chan->cache);
		if (ret)
			return ret;
	} else {
		ret = nouveau_gpuobj_new(dev, chan, 0x100, 256,
					 NVOBJ_FLAG_ZERO_ALLOC |
					 NVOBJ_FLAG_ZERO_FREE, &chan->ramfc);
		if (ret)
			return ret;

		ret = nouveau_gpuobj_new(dev, chan, 4096, 1024,
					 0, &chan->cache);
		if (ret)
			return ret;
	}
	ramfc = chan->ramfc;

	chan->user = ioremap(pci_resource_start(dev->pdev, 0) +
			     NV50_USER(chan->id), PAGE_SIZE);
	if (!chan->user)
		return -ENOMEM;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);

	nv_wo32(ramfc, 0x48, chan->pushbuf->cinst >> 4);
	nv_wo32(ramfc, 0x80, ((chan->ramht->bits - 9) << 27) |
			     (4 << 24) /* SEARCH_FULL */ |
			     (chan->ramht->gpuobj->cinst >> 4));
	nv_wo32(ramfc, 0x44, 0x01003fff);
	nv_wo32(ramfc, 0x60, 0x7fffffff);
	nv_wo32(ramfc, 0x40, 0x00000000);
	nv_wo32(ramfc, 0x7c, 0x30000001);
	nv_wo32(ramfc, 0x78, 0x00000000);
	nv_wo32(ramfc, 0x3c, 0x403f6078);
	nv_wo32(ramfc, 0x50, lower_32_bits(ib_offset));
	nv_wo32(ramfc, 0x54, upper_32_bits(ib_offset) |
                drm_order(chan->dma.ib_max + 1) << 16);

	if (dev_priv->chipset != 0x50) {
		nv_wo32(chan->ramin, 0, chan->id);
		nv_wo32(chan->ramin, 4, chan->ramfc->vinst >> 8);

		nv_wo32(ramfc, 0x88, chan->cache->vinst >> 10);
		nv_wo32(ramfc, 0x98, chan->ramin->vinst >> 12);
	}

	dev_priv->engine.instmem.flush(dev);

	nv50_fifo_channel_enable(dev, chan->id);
	nv50_fifo_playlist_update(dev);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);
	return 0;
}

static bool
nv50_fifo_wait_kickoff(void *data)
{
	struct drm_nouveau_private *dev_priv = data;
	struct drm_device *dev = dev_priv->dev;

	if (dev_priv->chipset == 0x50) {
		u32 me_enable = nv_mask(dev, 0x00b860, 0x00000001, 0x00000001);
		nv_wr32(dev, 0x00b860, me_enable);
	}

	return nv_rd32(dev, 0x0032fc) != 0xffffffff;
}

void
nv50_fifo_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;

	/* remove channel from playlist, will context switch if active */
	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_mask(dev, 0x002600 + (chan->id * 4), 0x80000000, 0x00000000);
	nv50_fifo_playlist_update(dev);

	/* tell any engines on this channel to unload their contexts */
	nv_wr32(dev, 0x0032fc, chan->ramin->vinst >> 12);
	if (!nv_wait_cb(dev, nv50_fifo_wait_kickoff, dev_priv))
		NV_INFO(dev, "PFIFO: channel %d unload timeout\n", chan->id);

	nv_wr32(dev, 0x002600 + (chan->id * 4), 0x00000000);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	/* clean up */
	if (chan->user) {
		iounmap(chan->user);
		chan->user = NULL;
	}

	nouveau_gpuobj_ref(NULL, &chan->ramfc);
	nouveau_gpuobj_ref(NULL, &chan->cache);
}

int
nv50_fifo_load_context(struct nouveau_channel *chan)
{
	return 0;
}

int
nv50_fifo_unload_context(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int i;

	/* set playlist length to zero, fifo will unload context */
	nv_wr32(dev, 0x0032ec, 0);

	/* tell all connected engines to unload their contexts */
	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		struct nouveau_channel *chan = dev_priv->channels.ptr[i];
		if (chan)
			nv_wr32(dev, 0x0032fc, chan->ramin->vinst >> 12);
		if (!nv_wait_cb(dev, nv50_fifo_wait_kickoff, dev_priv)) {
			NV_INFO(dev, "PFIFO: channel %d unload timeout\n", i);
			return -EBUSY;
		}
	}

	return 0;
}

void
nv50_fifo_tlb_flush(struct drm_device *dev)
{
	nv50_vm_flush_engine(dev, 5);
}
