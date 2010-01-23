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

#define NV04_RAMFC(c) (dev_priv->ramfc_offset + ((c) * NV04_RAMFC__SIZE))
#define NV04_RAMFC__SIZE 32
#define NV04_RAMFC_DMA_PUT                                       0x00
#define NV04_RAMFC_DMA_GET                                       0x04
#define NV04_RAMFC_DMA_INSTANCE                                  0x08
#define NV04_RAMFC_DMA_STATE                                     0x0C
#define NV04_RAMFC_DMA_FETCH                                     0x10
#define NV04_RAMFC_ENGINE                                        0x14
#define NV04_RAMFC_PULL1_ENGINE                                  0x18

#define RAMFC_WR(offset, val) nv_wo32(dev, chan->ramfc->gpuobj, \
					 NV04_RAMFC_##offset/4, (val))
#define RAMFC_RD(offset)      nv_ro32(dev, chan->ramfc->gpuobj, \
					 NV04_RAMFC_##offset/4)

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
nv04_fifo_cache_flush(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_timer_engine *ptimer = &dev_priv->engine.timer;
	uint64_t start = ptimer->read(dev);

	do {
		if (nv_rd32(dev, NV03_PFIFO_CACHE1_GET) ==
		    nv_rd32(dev, NV03_PFIFO_CACHE1_PUT))
			return true;

	} while (ptimer->read(dev) - start < 100000000);

	NV_ERROR(dev, "Timeout flushing the PFIFO cache.\n");

	return false;
}

bool
nv04_fifo_cache_pull(struct drm_device *dev, bool enable)
{
	uint32_t pull = nv_rd32(dev, NV04_PFIFO_CACHE1_PULL0);

	if (enable) {
		nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, pull | 1);
	} else {
		nv_wr32(dev, NV04_PFIFO_CACHE1_PULL0, pull & ~1);
		nv_wr32(dev, NV04_PFIFO_CACHE1_HASH, 0);
	}

	return !!(pull & 1);
}

int
nv04_fifo_channel_id(struct drm_device *dev)
{
	return nv_rd32(dev, NV03_PFIFO_CACHE1_PUSH1) &
			NV03_PFIFO_CACHE1_PUSH1_CHID_MASK;
}

int
nv04_fifo_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	ret = nouveau_gpuobj_new_fake(dev, NV04_RAMFC(chan->id), ~0,
						NV04_RAMFC__SIZE,
						NVOBJ_FLAG_ZERO_ALLOC |
						NVOBJ_FLAG_ZERO_FREE,
						NULL, &chan->ramfc);
	if (ret)
		return ret;

	/* Setup initial state */
	dev_priv->engine.instmem.prepare_access(dev, true);
	RAMFC_WR(DMA_PUT, chan->pushbuf_base);
	RAMFC_WR(DMA_GET, chan->pushbuf_base);
	RAMFC_WR(DMA_INSTANCE, chan->pushbuf->instance >> 4);
	RAMFC_WR(DMA_FETCH, (NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
			     NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
			     NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8 |
#ifdef __BIG_ENDIAN
			     NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
			     0));
	dev_priv->engine.instmem.finish_access(dev);

	/* enable the fifo dma operation */
	nv_wr32(dev, NV04_PFIFO_MODE,
		nv_rd32(dev, NV04_PFIFO_MODE) | (1 << chan->id));
	return 0;
}

void
nv04_fifo_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;

	nv_wr32(dev, NV04_PFIFO_MODE,
		nv_rd32(dev, NV04_PFIFO_MODE) & ~(1 << chan->id));

	nouveau_gpuobj_ref_del(dev, &chan->ramfc);
}

static void
nv04_fifo_do_load_context(struct drm_device *dev, int chid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t fc = NV04_RAMFC(chid), tmp;

	dev_priv->engine.instmem.prepare_access(dev, false);

	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUT, nv_ri32(dev, fc + 0));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_GET, nv_ri32(dev, fc + 4));
	tmp = nv_ri32(dev, fc + 8);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE, tmp & 0xFFFF);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_DCOUNT, tmp >> 16);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_STATE, nv_ri32(dev, fc + 12));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_FETCH, nv_ri32(dev, fc + 16));
	nv_wr32(dev, NV04_PFIFO_CACHE1_ENGINE, nv_ri32(dev, fc + 20));
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL1, nv_ri32(dev, fc + 24));

	dev_priv->engine.instmem.finish_access(dev);

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

	chan = dev_priv->fifos[chid];
	if (!chan) {
		NV_ERROR(dev, "Inactive channel on PFIFO: %d\n", chid);
		return -EINVAL;
	}

	dev_priv->engine.instmem.prepare_access(dev, true);
	RAMFC_WR(DMA_PUT, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUT));
	RAMFC_WR(DMA_GET, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_GET));
	tmp  = nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_DCOUNT) << 16;
	tmp |= nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE);
	RAMFC_WR(DMA_INSTANCE, tmp);
	RAMFC_WR(DMA_STATE, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_STATE));
	RAMFC_WR(DMA_FETCH, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_FETCH));
	RAMFC_WR(ENGINE, nv_rd32(dev, NV04_PFIFO_CACHE1_ENGINE));
	RAMFC_WR(PULL1_ENGINE, nv_rd32(dev, NV04_PFIFO_CACHE1_PULL1));
	dev_priv->engine.instmem.finish_access(dev);

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
				       ((dev_priv->ramht_bits - 9) << 16) |
				       (dev_priv->ramht_offset >> 8));
	nv_wr32(dev, NV03_PFIFO_RAMRO, dev_priv->ramro_offset>>8);
	nv_wr32(dev, NV03_PFIFO_RAMFC, dev_priv->ramfc_offset >> 8);
}

static void
nv04_fifo_init_intr(struct drm_device *dev)
{
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

	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		if (dev_priv->fifos[i]) {
			uint32_t mode = nv_rd32(dev, NV04_PFIFO_MODE);
			nv_wr32(dev, NV04_PFIFO_MODE, mode | (1 << i));
		}
	}

	return 0;
}

