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

static void
nv50_fifo_playlist_update(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_gpuobj *cur;
	int i, nr;

	NV_DEBUG(dev, "\n");

	cur = pfifo->playlist[pfifo->cur_playlist];
	pfifo->cur_playlist = !pfifo->cur_playlist;

	/* We never schedule channel 0 or 127 */
	for (i = 1, nr = 0; i < 127; i++) {
		if (dev_priv->fifos[i] && dev_priv->fifos[i]->ramfc) {
			nv_wo32(cur, (nr * 4), i);
			nr++;
		}
	}
	dev_priv->engine.instmem.flush(dev);

	nv_wr32(dev, 0x32f4, cur->vinst >> 12);
	nv_wr32(dev, 0x32ec, nr);
	nv_wr32(dev, 0x2500, 0x101);
}

static void
nv50_fifo_channel_enable(struct drm_device *dev, int channel)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = dev_priv->fifos[channel];
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
		if (dev_priv->fifos[i])
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
	dev_priv->engine.fifo.enable(dev);
	dev_priv->engine.fifo.reassign(dev, true);

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

	nouveau_gpuobj_ref(NULL, &pfifo->playlist[0]);
	nouveau_gpuobj_ref(NULL, &pfifo->playlist[1]);
}

int
nv50_fifo_channel_id(struct drm_device *dev)
{
	return nv_rd32(dev, NV03_PFIFO_CACHE1_PUSH1) &
			NV50_PFIFO_CACHE1_PUSH1_CHID_MASK;
}

int
nv50_fifo_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramfc = NULL;
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

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);

	nv_wo32(ramfc, 0x48, chan->pushbuf->cinst >> 4);
	nv_wo32(ramfc, 0x80, ((chan->ramht->bits - 9) << 27) |
			     (4 << 24) /* SEARCH_FULL */ |
			     (chan->ramht->gpuobj->cinst >> 4));
	nv_wo32(ramfc, 0x44, 0x2101ffff);
	nv_wo32(ramfc, 0x60, 0x7fffffff);
	nv_wo32(ramfc, 0x40, 0x00000000);
	nv_wo32(ramfc, 0x7c, 0x30000001);
	nv_wo32(ramfc, 0x78, 0x00000000);
	nv_wo32(ramfc, 0x3c, 0x403f6078);
	nv_wo32(ramfc, 0x50, chan->pushbuf_base + chan->dma.ib_base * 4);
	nv_wo32(ramfc, 0x54, drm_order(chan->dma.ib_max + 1) << 16);

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

void
nv50_fifo_destroy_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_gpuobj *ramfc = NULL;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	/* This will ensure the channel is seen as disabled. */
	nouveau_gpuobj_ref(chan->ramfc, &ramfc);
	nouveau_gpuobj_ref(NULL, &chan->ramfc);
	nv50_fifo_channel_disable(dev, chan->id);

	/* Dummy channel, also used on ch 127 */
	if (chan->id == 0)
		nv50_fifo_channel_disable(dev, 127);
	nv50_fifo_playlist_update(dev);

	nouveau_gpuobj_ref(NULL, &ramfc);
	nouveau_gpuobj_ref(NULL, &chan->cache);
}

int
nv50_fifo_load_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramfc = chan->ramfc;
	struct nouveau_gpuobj *cache = chan->cache;
	int ptr, cnt;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	nv_wr32(dev, 0x3330, nv_ro32(ramfc, 0x00));
	nv_wr32(dev, 0x3334, nv_ro32(ramfc, 0x04));
	nv_wr32(dev, 0x3240, nv_ro32(ramfc, 0x08));
	nv_wr32(dev, 0x3320, nv_ro32(ramfc, 0x0c));
	nv_wr32(dev, 0x3244, nv_ro32(ramfc, 0x10));
	nv_wr32(dev, 0x3328, nv_ro32(ramfc, 0x14));
	nv_wr32(dev, 0x3368, nv_ro32(ramfc, 0x18));
	nv_wr32(dev, 0x336c, nv_ro32(ramfc, 0x1c));
	nv_wr32(dev, 0x3370, nv_ro32(ramfc, 0x20));
	nv_wr32(dev, 0x3374, nv_ro32(ramfc, 0x24));
	nv_wr32(dev, 0x3378, nv_ro32(ramfc, 0x28));
	nv_wr32(dev, 0x337c, nv_ro32(ramfc, 0x2c));
	nv_wr32(dev, 0x3228, nv_ro32(ramfc, 0x30));
	nv_wr32(dev, 0x3364, nv_ro32(ramfc, 0x34));
	nv_wr32(dev, 0x32a0, nv_ro32(ramfc, 0x38));
	nv_wr32(dev, 0x3224, nv_ro32(ramfc, 0x3c));
	nv_wr32(dev, 0x324c, nv_ro32(ramfc, 0x40));
	nv_wr32(dev, 0x2044, nv_ro32(ramfc, 0x44));
	nv_wr32(dev, 0x322c, nv_ro32(ramfc, 0x48));
	nv_wr32(dev, 0x3234, nv_ro32(ramfc, 0x4c));
	nv_wr32(dev, 0x3340, nv_ro32(ramfc, 0x50));
	nv_wr32(dev, 0x3344, nv_ro32(ramfc, 0x54));
	nv_wr32(dev, 0x3280, nv_ro32(ramfc, 0x58));
	nv_wr32(dev, 0x3254, nv_ro32(ramfc, 0x5c));
	nv_wr32(dev, 0x3260, nv_ro32(ramfc, 0x60));
	nv_wr32(dev, 0x3264, nv_ro32(ramfc, 0x64));
	nv_wr32(dev, 0x3268, nv_ro32(ramfc, 0x68));
	nv_wr32(dev, 0x326c, nv_ro32(ramfc, 0x6c));
	nv_wr32(dev, 0x32e4, nv_ro32(ramfc, 0x70));
	nv_wr32(dev, 0x3248, nv_ro32(ramfc, 0x74));
	nv_wr32(dev, 0x2088, nv_ro32(ramfc, 0x78));
	nv_wr32(dev, 0x2058, nv_ro32(ramfc, 0x7c));
	nv_wr32(dev, 0x2210, nv_ro32(ramfc, 0x80));

	cnt = nv_ro32(ramfc, 0x84);
	for (ptr = 0; ptr < cnt; ptr++) {
		nv_wr32(dev, NV40_PFIFO_CACHE1_METHOD(ptr),
			nv_ro32(cache, (ptr * 8) + 0));
		nv_wr32(dev, NV40_PFIFO_CACHE1_DATA(ptr),
			nv_ro32(cache, (ptr * 8) + 4));
	}
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUT, cnt << 2);
	nv_wr32(dev, NV03_PFIFO_CACHE1_GET, 0);

	/* guessing that all the 0x34xx regs aren't on NV50 */
	if (dev_priv->chipset != 0x50) {
		nv_wr32(dev, 0x340c, nv_ro32(ramfc, 0x88));
		nv_wr32(dev, 0x3400, nv_ro32(ramfc, 0x8c));
		nv_wr32(dev, 0x3404, nv_ro32(ramfc, 0x90));
		nv_wr32(dev, 0x3408, nv_ro32(ramfc, 0x94));
		nv_wr32(dev, 0x3410, nv_ro32(ramfc, 0x98));
	}

	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1, chan->id | (1<<16));
	return 0;
}

int
nv50_fifo_unload_context(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_gpuobj *ramfc, *cache;
	struct nouveau_channel *chan = NULL;
	int chid, get, put, ptr;

	NV_DEBUG(dev, "\n");

	chid = pfifo->channel_id(dev);
	if (chid < 1 || chid >= dev_priv->engine.fifo.channels - 1)
		return 0;

	chan = dev_priv->fifos[chid];
	if (!chan) {
		NV_ERROR(dev, "Inactive channel on PFIFO: %d\n", chid);
		return -EINVAL;
	}
	NV_DEBUG(dev, "ch%d\n", chan->id);
	ramfc = chan->ramfc;
	cache = chan->cache;

	nv_wo32(ramfc, 0x00, nv_rd32(dev, 0x3330));
	nv_wo32(ramfc, 0x04, nv_rd32(dev, 0x3334));
	nv_wo32(ramfc, 0x08, nv_rd32(dev, 0x3240));
	nv_wo32(ramfc, 0x0c, nv_rd32(dev, 0x3320));
	nv_wo32(ramfc, 0x10, nv_rd32(dev, 0x3244));
	nv_wo32(ramfc, 0x14, nv_rd32(dev, 0x3328));
	nv_wo32(ramfc, 0x18, nv_rd32(dev, 0x3368));
	nv_wo32(ramfc, 0x1c, nv_rd32(dev, 0x336c));
	nv_wo32(ramfc, 0x20, nv_rd32(dev, 0x3370));
	nv_wo32(ramfc, 0x24, nv_rd32(dev, 0x3374));
	nv_wo32(ramfc, 0x28, nv_rd32(dev, 0x3378));
	nv_wo32(ramfc, 0x2c, nv_rd32(dev, 0x337c));
	nv_wo32(ramfc, 0x30, nv_rd32(dev, 0x3228));
	nv_wo32(ramfc, 0x34, nv_rd32(dev, 0x3364));
	nv_wo32(ramfc, 0x38, nv_rd32(dev, 0x32a0));
	nv_wo32(ramfc, 0x3c, nv_rd32(dev, 0x3224));
	nv_wo32(ramfc, 0x40, nv_rd32(dev, 0x324c));
	nv_wo32(ramfc, 0x44, nv_rd32(dev, 0x2044));
	nv_wo32(ramfc, 0x48, nv_rd32(dev, 0x322c));
	nv_wo32(ramfc, 0x4c, nv_rd32(dev, 0x3234));
	nv_wo32(ramfc, 0x50, nv_rd32(dev, 0x3340));
	nv_wo32(ramfc, 0x54, nv_rd32(dev, 0x3344));
	nv_wo32(ramfc, 0x58, nv_rd32(dev, 0x3280));
	nv_wo32(ramfc, 0x5c, nv_rd32(dev, 0x3254));
	nv_wo32(ramfc, 0x60, nv_rd32(dev, 0x3260));
	nv_wo32(ramfc, 0x64, nv_rd32(dev, 0x3264));
	nv_wo32(ramfc, 0x68, nv_rd32(dev, 0x3268));
	nv_wo32(ramfc, 0x6c, nv_rd32(dev, 0x326c));
	nv_wo32(ramfc, 0x70, nv_rd32(dev, 0x32e4));
	nv_wo32(ramfc, 0x74, nv_rd32(dev, 0x3248));
	nv_wo32(ramfc, 0x78, nv_rd32(dev, 0x2088));
	nv_wo32(ramfc, 0x7c, nv_rd32(dev, 0x2058));
	nv_wo32(ramfc, 0x80, nv_rd32(dev, 0x2210));

	put = (nv_rd32(dev, NV03_PFIFO_CACHE1_PUT) & 0x7ff) >> 2;
	get = (nv_rd32(dev, NV03_PFIFO_CACHE1_GET) & 0x7ff) >> 2;
	ptr = 0;
	while (put != get) {
		nv_wo32(cache, ptr + 0,
			nv_rd32(dev, NV40_PFIFO_CACHE1_METHOD(get)));
		nv_wo32(cache, ptr + 4,
			nv_rd32(dev, NV40_PFIFO_CACHE1_DATA(get)));
		get = (get + 1) & 0x1ff;
		ptr += 8;
	}

	/* guessing that all the 0x34xx regs aren't on NV50 */
	if (dev_priv->chipset != 0x50) {
		nv_wo32(ramfc, 0x84, ptr >> 3);
		nv_wo32(ramfc, 0x88, nv_rd32(dev, 0x340c));
		nv_wo32(ramfc, 0x8c, nv_rd32(dev, 0x3400));
		nv_wo32(ramfc, 0x90, nv_rd32(dev, 0x3404));
		nv_wo32(ramfc, 0x94, nv_rd32(dev, 0x3408));
		nv_wo32(ramfc, 0x98, nv_rd32(dev, 0x3410));
	}

	dev_priv->engine.instmem.flush(dev);

	/*XXX: probably reload ch127 (NULL) state back too */
	nv_wr32(dev, NV03_PFIFO_CACHE1_PUSH1, 127);
	return 0;
}

