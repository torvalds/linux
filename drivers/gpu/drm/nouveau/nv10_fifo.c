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

#define NV10_RAMFC(c) (dev_priv->ramfc->pinst + ((c) * NV10_RAMFC__SIZE))
#define NV10_RAMFC__SIZE ((dev_priv->chipset) >= 0x17 ? 64 : 32)

int
nv10_fifo_channel_id(struct drm_device *dev)
{
	return nv_rd32(dev, NV03_PFIFO_CACHE1_PUSH1) &
			NV10_PFIFO_CACHE1_PUSH1_CHID_MASK;
}

int
nv10_fifo_create_context(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct drm_device *dev = chan->dev;
	uint32_t fc = NV10_RAMFC(chan->id);
	int ret;

	ret = nouveau_gpuobj_new_fake(dev, NV10_RAMFC(chan->id), ~0,
				      NV10_RAMFC__SIZE, NVOBJ_FLAG_ZERO_ALLOC |
				      NVOBJ_FLAG_ZERO_FREE, &chan->ramfc);
	if (ret)
		return ret;

	/* Fill entries that are seen filled in dumps of nvidia driver just
	 * after channel's is put into DMA mode
	 */
	nv_wi32(dev, fc +  0, chan->pushbuf_base);
	nv_wi32(dev, fc +  4, chan->pushbuf_base);
	nv_wi32(dev, fc + 12, chan->pushbuf->pinst >> 4);
	nv_wi32(dev, fc + 20, NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
			      NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
			      NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8 |
#ifdef __BIG_ENDIAN
			      NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
			      0);

	/* enable the fifo dma operation */
	nv_wr32(dev, NV04_PFIFO_MODE,
		nv_rd32(dev, NV04_PFIFO_MODE) | (1 << chan->id));
	return 0;
}

void
nv10_fifo_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;

	nv_wr32(dev, NV04_PFIFO_MODE,
			nv_rd32(dev, NV04_PFIFO_MODE) & ~(1 << chan->id));

	nouveau_gpuobj_ref(NULL, &chan->ramfc);
}

static void
nv10_fifo_do_load_context(struct drm_device *dev, int chid)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t fc = NV10_RAMFC(chid), tmp;

	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUT, nv_ri32(dev, fc + 0));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_GET, nv_ri32(dev, fc + 4));
	nv_wr32(dev, NV10_PFIFO_CACHE1_REF_CNT, nv_ri32(dev, fc + 8));

	tmp = nv_ri32(dev, fc + 12);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE, tmp & 0xFFFF);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_DCOUNT, tmp >> 16);

	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_STATE, nv_ri32(dev, fc + 16));
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_FETCH, nv_ri32(dev, fc + 20));
	nv_wr32(dev, NV04_PFIFO_CACHE1_ENGINE, nv_ri32(dev, fc + 24));
	nv_wr32(dev, NV04_PFIFO_CACHE1_PULL1, nv_ri32(dev, fc + 28));

	if (dev_priv->chipset < 0x17)
		goto out;

	nv_wr32(dev, NV10_PFIFO_CACHE1_ACQUIRE_VALUE, nv_ri32(dev, fc + 32));
	tmp = nv_ri32(dev, fc + 36);
	nv_wr32(dev, NV10_PFIFO_CACHE1_ACQUIRE_TIMESTAMP, tmp);
	nv_wr32(dev, NV10_PFIFO_CACHE1_ACQUIRE_TIMEOUT, nv_ri32(dev, fc + 40));
	nv_wr32(dev, NV10_PFIFO_CACHE1_SEMAPHORE, nv_ri32(dev, fc + 44));
	nv_wr32(dev, NV10_PFIFO_CACHE1_DMA_SUBROUTINE, nv_ri32(dev, fc + 48));

out:
	nv_wr32(dev, NV03_PFIFO_CACHE1_GET, 0);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUT, 0);
}

int
nv10_fifo_load_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	uint32_t tmp;

	nv10_fifo_do_load_context(dev, chan->id);

	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1,
		     NV03_PFIFO_CACHE1_PUSH1_DMA | chan->id);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_PUSH, 1);

	/* Reset NV04_PFIFO_CACHE1_DMA_CTL_AT_INFO to INVALID */
	tmp = nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_CTL) & ~(1 << 31);
	nv_wr32(dev, NV04_PFIFO_CACHE1_DMA_CTL, tmp);

	return 0;
}

int
nv10_fifo_unload_context(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	uint32_t fc, tmp;
	int chid;

	chid = pfifo->channel_id(dev);
	if (chid < 0 || chid >= dev_priv->engine.fifo.channels)
		return 0;
	fc = NV10_RAMFC(chid);

	nv_wi32(dev, fc +  0, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_PUT));
	nv_wi32(dev, fc +  4, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_GET));
	nv_wi32(dev, fc +  8, nv_rd32(dev, NV10_PFIFO_CACHE1_REF_CNT));
	tmp  = nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_INSTANCE) & 0xFFFF;
	tmp |= (nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_DCOUNT) << 16);
	nv_wi32(dev, fc + 12, tmp);
	nv_wi32(dev, fc + 16, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_STATE));
	nv_wi32(dev, fc + 20, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_FETCH));
	nv_wi32(dev, fc + 24, nv_rd32(dev, NV04_PFIFO_CACHE1_ENGINE));
	nv_wi32(dev, fc + 28, nv_rd32(dev, NV04_PFIFO_CACHE1_PULL1));

	if (dev_priv->chipset < 0x17)
		goto out;

	nv_wi32(dev, fc + 32, nv_rd32(dev, NV10_PFIFO_CACHE1_ACQUIRE_VALUE));
	tmp = nv_rd32(dev, NV10_PFIFO_CACHE1_ACQUIRE_TIMESTAMP);
	nv_wi32(dev, fc + 36, tmp);
	nv_wi32(dev, fc + 40, nv_rd32(dev, NV10_PFIFO_CACHE1_ACQUIRE_TIMEOUT));
	nv_wi32(dev, fc + 44, nv_rd32(dev, NV10_PFIFO_CACHE1_SEMAPHORE));
	nv_wi32(dev, fc + 48, nv_rd32(dev, NV04_PFIFO_CACHE1_DMA_GET));

out:
	nv10_fifo_do_load_context(dev, pfifo->channels - 1);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1, pfifo->channels - 1);
	return 0;
}

static void
nv10_fifo_init_reset(struct drm_device *dev)
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

	nv_wr32(dev, 0x003258, 0x00000000);
	nv_wr32(dev, 0x003210, 0x00000000);
	nv_wr32(dev, 0x003270, 0x00000000);
}

static void
nv10_fifo_init_ramxx(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	nv_wr32(dev, NV03_PFIFO_RAMHT, (0x03 << 24) /* search 128 */ |
				       ((dev_priv->ramht->bits - 9) << 16) |
				       (dev_priv->ramht->gpuobj->pinst >> 8));
	nv_wr32(dev, NV03_PFIFO_RAMRO, dev_priv->ramro->pinst >> 8);

	if (dev_priv->chipset < 0x17) {
		nv_wr32(dev, NV03_PFIFO_RAMFC, dev_priv->ramfc->pinst >> 8);
	} else {
		nv_wr32(dev, NV03_PFIFO_RAMFC, (dev_priv->ramfc->pinst >> 8) |
					       (1 << 16) /* 64 Bytes entry*/);
		/* XXX nvidia blob set bit 18, 21,23 for nv20 & nv30 */
	}
}

static void
nv10_fifo_init_intr(struct drm_device *dev)
{
	nv_wr32(dev, 0x002100, 0xffffffff);
	nv_wr32(dev, 0x002140, 0xffffffff);
}

int
nv10_fifo_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	int i;

	nv10_fifo_init_reset(dev);
	nv10_fifo_init_ramxx(dev);

	nv10_fifo_do_load_context(dev, pfifo->channels - 1);
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1, pfifo->channels - 1);

	nv10_fifo_init_intr(dev);
	pfifo->enable(dev);
	pfifo->reassign(dev, true);

	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		if (dev_priv->fifos[i]) {
			uint32_t mode = nv_rd32(dev, NV04_PFIFO_MODE);
			nv_wr32(dev, NV04_PFIFO_MODE, mode | (1 << i));
		}
	}

	return 0;
}
