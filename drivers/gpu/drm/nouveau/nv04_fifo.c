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
#include "nouveau_util.h"

#define NV04_RAMFC(c) (dev_priv->ramfc->pinst + ((c) * NV04_RAMFC__SIZE))
#define NV04_RAMFC__SIZE 32
#define NV04_RAMFC_DMA_PUT                                       0x00
#define NV04_RAMFC_DMA_GET                                       0x04
#define NV04_RAMFC_DMA_INSTANCE                                  0x08
#define NV04_RAMFC_DMA_STATE                                     0x0C
#define NV04_RAMFC_DMA_FETCH                                     0x10
#define NV04_RAMFC_ENGINE                                        0x14
#define NV04_RAMFC_PULL1_ENGINE                                  0x18

#define RAMFC_WR(offset, val) nv_wo32(chan->ramfc, NV04_RAMFC_##offset, (val))
#define RAMFC_RD(offset)      nv_ro32(chan->ramfc, NV04_RAMFC_##offset)

void
nv04_fifo_disable(struct drm_device *dev)
{
	uint32_t tmp;

	tmp = nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUSH);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUSH, tmp & ~1);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0, 0);
	tmp = nv_rd32(dev, NV03_PFIFO_CACHE1_PULL1);
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, tmp & ~1);
}

void
nv04_fifo_enable(struct drm_device *dev)
{
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0, 1);
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 1);
}

bool
nv04_fifo_reassign(struct drm_device *dev, bool enable)
{
	uint32_t reassign = nv_rd32(dev, NV03_PFIFO_CACHES);

	nv_wr32(dev, NV03_PFIFO_CACHES, enable ? 1 : 0);
	return (reassign == 1);
}

bool
nv04_fifo_cache_pull(struct drm_device *dev, bool enable)
{
	int pull = nv_mask(dev, NV04_PFIFO_CACHE1_PULL0, 1, enable);

	if (!enable) {
		/* In some cases the PFIFO puller may be left in an
		 * inconsistent state if you try to stop it when it's
		 * busy translating handles. Sometimes you get a
		 * PFIFO_CACHE_ERROR, sometimes it just fails silently
		 * sending incorrect instance offsets to PGRAPH after
		 * it's started up again. To avoid the latter we
		 * invalidate the most recently calculated instance.
		 */
		if (!nv_wait(dev, NV04_PFIFO_CACHE1_PULL0,
			     NV04_PFIFO_CACHE1_PULL0_HASH_BUSY, 0))
			NV_ERROR(dev, "Timeout idling the PFIFO puller.\n");

		if (nv_rd32(dev, NV04_PFIFO_CACHE1_PULL0) &
		    NV04_PFIFO_CACHE1_PULL0_HASH_FAILED)
			nv_wr32(dev, NV03_PFIFO_INTR_0,
				NV_PFIFO_INTR_CACHE_ERROR);

		nv_wr32(dev, NV04_PFIFO_CACHE1_HASH, 0);
	}

	return pull & 1;
}

int
nv04_fifo_channel_id(struct drm_device *dev)
{
	return nv_rd32(dev, NV03_PFIFO_CACHE1_PUSH1) &
			NV03_PFIFO_CACHE1_PUSH1_CHID_MASK;
}

#ifdef __BIG_ENDIAN
#define DMA_FETCH_ENDIANNESS NV_PFIFO_CACHE1_BIG_ENDIAN
#else
#define DMA_FETCH_ENDIANNESS 0
#endif

int
nv04_fifo_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;
	int ret;

	ret = nouveau_gpuobj_new_fake(dev, NV04_RAMFC(chan->id), ~0,
						NV04_RAMFC__SIZE,
						NVOBJ_FLAG_ZERO_ALLOC |
						NVOBJ_FLAG_ZERO_FREE,
						&chan->ramfc);
	if (ret)
		return ret;

	chan->user = ioremap(pci_resource_start(dev->pdev, 0) +
			     NV03_USER(chan->id), PAGE_SIZE);
	if (!chan->user)
		return -ENOMEM;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);

	/* Setup initial state */
	RAMFC_WR(DMA_PUT, chan->pushbuf_base);
	RAMFC_WR(DMA_GET, chan->pushbuf_base);
	RAMFC_WR(DMA_INSTANCE, chan->pushbuf->pinst >> 4);
	RAMFC_WR(DMA_FETCH, (NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
			     NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
			     NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8 |
			     DMA_FETCH_ENDIANNESS));

	/* enable the fifo dma operation */
	nv_wr32(dev, NV04_PFIFO_MODE,
		nv_rd32(dev, NV04_PFIFO_MODE) | (1 << chan->id));

	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);
	return 0;
}

void
nv04_fifo_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	pfifo->reassign(dev, false);

	/* Unload the context if it's the currently active one */
	if (pfifo->channel_id(dev) == chan->id) {
		pfifo->disable(dev);
		pfifo->unload_context(dev);
		pfifo->enable(dev);
	}

	/* Keep it from being rescheduled */
	nv_mask(dev, NV04_PFIFO_MODE, 1 << chan->id, 0);

	pfifo->reassign(dev, true);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	/* Free the channel resources */
	if (chan->user) {
		iounmap(chan->user);
		chan->user = NULL;
	}
	nouveau_gpuobj_ref(NULL, &chan->ramfc);
}

static void
nv04_fifo_do_load_context(struct drm_device *dev, int chid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t fc = NV04_RAMFC(chid), tmp;

	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUT, nv_ri32(dev, fc + 0));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_GET, nv_ri32(dev, fc + 4));
	tmp = nv_ri32(dev, fc + 8);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE, tmp & 0xFFFF);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_DCOUNT, tmp >> 16);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_STATE, nv_ri32(dev, fc + 12));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_FETCH, nv_ri32(dev, fc + 16));
	nv_wr32(dev, NV04_PFIFO_CACHE1_ENGINE, nv_ri32(dev, fc + 20));
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL1, nv_ri32(dev, fc + 24));

	nv_wr32(dev, NV03_PFIFO_CACHE1_GET, 0);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUT, 0);
}

int
nv04_fifo_load_context(struct nouveau_channel *chan)
{
	uint32_t tmp;

	nv_wr32(chan->dev, NV03_PFIFO_CACHE1_PUSH1,
			   NV03_PFIFO_CACHE1_PUSH1_DMA | chan->id);
	nv04_fifo_do_load_context(chan->dev, chan->id);
	nv_wr32(chan->dev, NV04_PFIFO_CACHE1_DMA_PUSH, 1);

	/* Reset NV04_PFIFO_CACHE1_DMA_CTL_AT_INFO to INVALID */
	tmp = nv_rd32(chan->dev, NV04_PFIFO_CACHE1_DMA_CTL) & ~(1 << 31);
	nv_wr32(chan->dev, NV04_PFIFO_CACHE1_DMA_CTL, tmp);

	return 0;
}

int
nv04_fifo_unload_context(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_channel *chan = NULL;
	uint32_t tmp;
	int chid;

	chid = pfifo->channel_id(dev);
	if (chid < 0 || chid >= dev_priv->engine.fifo.channels)
		return 0;

	chan = dev_priv->channels.ptr[chid];
	if (!chan) {
		NV_ERROR(dev, "Inactive channel on PFIFO: %d\n", chid);
		return -EINVAL;
	}

	RAMFC_WR(DMA_PUT, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUT));
	RAMFC_WR(DMA_GET, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_GET));
	tmp  = nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_DCOUNT) << 16;
	tmp |= nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE);
	RAMFC_WR(DMA_INSTANCE, tmp);
	RAMFC_WR(DMA_STATE, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_STATE));
	RAMFC_WR(DMA_FETCH, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_FETCH));
	RAMFC_WR(ENGINE, nv_rd32(dev, NV04_PFIFO_CACHE1_ENGINE));
	RAMFC_WR(PULL1_ENGINE, nv_rd32(dev, NV04_PFIFO_CACHE1_PULL1));

	nv04_fifo_do_load_context(dev, pfifo->channels - 1);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1, pfifo->channels - 1);
	return 0;
}

static void
nv04_fifo_init_reset(struct drm_device *dev)
{
	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) & ~NV_PMC_ENABLE_PFIFO);
	nv_wr32(dev, NV03_PMC_ENABLE,
		nv_rd32(dev, NV03_PMC_ENABLE) |  NV_PMC_ENABLE_PFIFO);

	nv_wr32(dev, 0x003224, 0x000f0078);
	nv_wr32(dev, 0x002044, 0x0101ffff);
	nv_wr32(dev, 0x002040, 0x000000ff);
	nv_wr32(dev, 0x002500, 0x00000000);
	nv_wr32(dev, 0x003000, 0x00000000);
	nv_wr32(dev, 0x003050, 0x00000000);
	nv_wr32(dev, 0x003200, 0x00000000);
	nv_wr32(dev, 0x003250, 0x00000000);
	nv_wr32(dev, 0x003220, 0x00000000);

	nv_wr32(dev, 0x003250, 0x00000000);
	nv_wr32(dev, 0x003270, 0x00000000);
	nv_wr32(dev, 0x003210, 0x00000000);
}

static void
nv04_fifo_init_ramxx(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	nv_wr32(dev, NV03_PFIFO_RAMHT, (0x03 << 24) /* search 128 */ |
				       ((dev_priv->ramht->bits - 9) << 16) |
				       (dev_priv->ramht->gpuobj->pinst >> 8));
	nv_wr32(dev, NV03_PFIFO_RAMRO, dev_priv->ramro->pinst >> 8);
	nv_wr32(dev, NV03_PFIFO_RAMFC, dev_priv->ramfc->pinst >> 8);
}

static void
nv04_fifo_init_intr(struct drm_device *dev)
{
	nouveau_irq_register(dev, 8, nv04_fifo_isr);
	nv_wr32(dev, 0x002100, 0xffffffff);
	nv_wr32(dev, 0x002140, 0xffffffff);
}

int
nv04_fifo_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	int i;

	nv04_fifo_init_reset(dev);
	nv04_fifo_init_ramxx(dev);

	nv04_fifo_do_load_context(dev, pfifo->channels - 1);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1, pfifo->channels - 1);

	nv04_fifo_init_intr(dev);
	pfifo->enable(dev);
	pfifo->reassign(dev, true);

	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		if (dev_priv->channels.ptr[i]) {
			uint32_t mode = nv_rd32(dev, NV04_PFIFO_MODE);
			nv_wr32(dev, NV04_PFIFO_MODE, mode | (1 << i));
		}
	}

	return 0;
}

void
nv04_fifo_fini(struct drm_device *dev)
{
	nv_wr32(dev, 0x2140, 0x00000000);
	nouveau_irq_unregister(dev, 8);
}

static bool
nouveau_fifo_swmthd(struct drm_device *dev, u32 chid, u32 addr, u32 data)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = NULL;
	struct nouveau_gpuobj *obj;
	unsigned long flags;
	const int subc = (addr >> 13) & 0x7;
	const int mthd = addr & 0x1ffc;
	bool handled = false;
	u32 engine;

	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	if (likely(chid >= 0 && chid < dev_priv->engine.fifo.channels))
		chan = dev_priv->channels.ptr[chid];
	if (unlikely(!chan))
		goto out;

	switch (mthd) {
	case 0x0000: /* bind object to subchannel */
		obj = nouveau_ramht_find(chan, data);
		if (unlikely(!obj || obj->engine != NVOBJ_ENGINE_SW))
			break;

		chan->sw_subchannel[subc] = obj->class;
		engine = 0x0000000f << (subc * 4);

		nv_mask(dev, NV04_PFIFO_CACHE1_ENGINE, engine, 0x00000000);
		handled = true;
		break;
	default:
		engine = nv_rd32(dev, NV04_PFIFO_CACHE1_ENGINE);
		if (unlikely(((engine >> (subc * 4)) & 0xf) != 0))
			break;

		if (!nouveau_gpuobj_mthd_call(chan, chan->sw_subchannel[subc],
					      mthd, data))
			handled = true;
		break;
	}

out:
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);
	return handled;
}

void
nv04_fifo_isr(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	uint32_t status, reassign;
	int cnt = 0;

	reassign = nv_rd32(dev, NV03_PFIFO_CACHES) & 1;
	while ((status = nv_rd32(dev, NV03_PFIFO_INTR_0)) && (cnt++ < 100)) {
		uint32_t chid, get;

		nv_wr32(dev, NV03_PFIFO_CACHES, 0);

		chid = engine->fifo.channel_id(dev);
		get  = nv_rd32(dev, NV03_PFIFO_CACHE1_GET);

		if (status & NV_PFIFO_INTR_CACHE_ERROR) {
			uint32_t mthd, data;
			int ptr;

			/* NV_PFIFO_CACHE1_GET actually goes to 0xffc before
			 * wrapping on my G80 chips, but CACHE1 isn't big
			 * enough for this much data.. Tests show that it
			 * wraps around to the start at GET=0x800.. No clue
			 * as to why..
			 */
			ptr = (get & 0x7ff) >> 2;

			if (dev_priv->card_type < NV_40) {
				mthd = nv_rd32(dev,
					NV04_PFIFO_CACHE1_METHOD(ptr));
				data = nv_rd32(dev,
					NV04_PFIFO_CACHE1_DATA(ptr));
			} else {
				mthd = nv_rd32(dev,
					NV40_PFIFO_CACHE1_METHOD(ptr));
				data = nv_rd32(dev,
					NV40_PFIFO_CACHE1_DATA(ptr));
			}

			if (!nouveau_fifo_swmthd(dev, chid, mthd, data)) {
				NV_INFO(dev, "PFIFO_CACHE_ERROR - Ch %d/%d "
					     "Mthd 0x%04x Data 0x%08x\n",
					chid, (mthd >> 13) & 7, mthd & 0x1ffc,
					data);
			}

			nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUSH, 0);
			nv_wr32(dev, NV03_PFIFO_INTR_0,
						NV_PFIFO_INTR_CACHE_ERROR);

			nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0,
				nv_rd32(dev, NV03_PFIFO_CACHE1_PUSH0) & ~1);
			nv_wr32(dev, NV03_PFIFO_CACHE1_GET, get + 4);
			nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH0,
				nv_rd32(dev, NV03_PFIFO_CACHE1_PUSH0) | 1);
			nv_wr32(dev, NV04_PFIFO_CACHE1_HASH, 0);

			nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUSH,
				nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUSH) | 1);
			nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 1);

			status &= ~NV_PFIFO_INTR_CACHE_ERROR;
		}

		if (status & NV_PFIFO_INTR_DMA_PUSHER) {
			u32 dma_get = nv_rd32(dev, 0x003244);
			u32 dma_put = nv_rd32(dev, 0x003240);
			u32 push = nv_rd32(dev, 0x003220);
			u32 state = nv_rd32(dev, 0x003228);

			if (dev_priv->card_type == NV_50) {
				u32 ho_get = nv_rd32(dev, 0x003328);
				u32 ho_put = nv_rd32(dev, 0x003320);
				u32 ib_get = nv_rd32(dev, 0x003334);
				u32 ib_put = nv_rd32(dev, 0x003330);

				if (nouveau_ratelimit())
					NV_INFO(dev, "PFIFO_DMA_PUSHER - Ch %d Get 0x%02x%08x "
					     "Put 0x%02x%08x IbGet 0x%08x IbPut 0x%08x "
					     "State 0x%08x Push 0x%08x\n",
						chid, ho_get, dma_get, ho_put,
						dma_put, ib_get, ib_put, state,
						push);

				/* METHOD_COUNT, in DMA_STATE on earlier chipsets */
				nv_wr32(dev, 0x003364, 0x00000000);
				if (dma_get != dma_put || ho_get != ho_put) {
					nv_wr32(dev, 0x003244, dma_put);
					nv_wr32(dev, 0x003328, ho_put);
				} else
				if (ib_get != ib_put) {
					nv_wr32(dev, 0x003334, ib_put);
				}
			} else {
				NV_INFO(dev, "PFIFO_DMA_PUSHER - Ch %d Get 0x%08x "
					     "Put 0x%08x State 0x%08x Push 0x%08x\n",
					chid, dma_get, dma_put, state, push);

				if (dma_get != dma_put)
					nv_wr32(dev, 0x003244, dma_put);
			}

			nv_wr32(dev, 0x003228, 0x00000000);
			nv_wr32(dev, 0x003220, 0x00000001);
			nv_wr32(dev, 0x002100, NV_PFIFO_INTR_DMA_PUSHER);
			status &= ~NV_PFIFO_INTR_DMA_PUSHER;
		}

		if (status & NV_PFIFO_INTR_SEMAPHORE) {
			uint32_t sem;

			status &= ~NV_PFIFO_INTR_SEMAPHORE;
			nv_wr32(dev, NV03_PFIFO_INTR_0,
				NV_PFIFO_INTR_SEMAPHORE);

			sem = nv_rd32(dev, NV10_PFIFO_CACHE1_SEMAPHORE);
			nv_wr32(dev, NV10_PFIFO_CACHE1_SEMAPHORE, sem | 0x1);

			nv_wr32(dev, NV03_PFIFO_CACHE1_GET, get + 4);
			nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, 1);
		}

		if (dev_priv->card_type == NV_50) {
			if (status & 0x00000010) {
				nv50_fb_vm_trap(dev, 1, "PFIFO_BAR_FAULT");
				status &= ~0x00000010;
				nv_wr32(dev, 0x002100, 0x00000010);
			}
		}

		if (status) {
			if (nouveau_ratelimit())
				NV_INFO(dev, "PFIFO_INTR 0x%08x - Ch %d\n",
					status, chid);
			nv_wr32(dev, NV03_PFIFO_INTR_0, status);
			status = 0;
		}

		nv_wr32(dev, NV03_PFIFO_CACHES, reassign);
	}

	if (status) {
		NV_INFO(dev, "PFIFO still angry after %d spins, halt\n", cnt);
		nv_wr32(dev, 0x2140, 0);
		nv_wr32(dev, 0x140, 0);
	}

	nv_wr32(dev, NV03_PMC_INTR_0, NV_PMC_INTR_0_PFIFO_PENDING);
}
