/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_ramht.h"

static uint32_t
nouveau_ramht_hash_handle(struct drm_device *dev, int channel, uint32_t handle)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t hash = 0;
	int i;

	NV_DEBUG(dev, "ch%d handle=0x%08x\n", channel, handle);

	for (i = 32; i > 0; i -= dev_priv->ramht_bits) {
		hash ^= (handle & ((1 << dev_priv->ramht_bits) - 1));
		handle >>= dev_priv->ramht_bits;
	}

	if (dev_priv->card_type < NV_50)
		hash ^= channel << (dev_priv->ramht_bits - 4);
	hash <<= 3;

	NV_DEBUG(dev, "hash=0x%08x\n", hash);
	return hash;
}

static int
nouveau_ramht_entry_valid(struct drm_device *dev, struct nouveau_gpuobj *ramht,
			  uint32_t offset)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t ctx = nv_ro32(dev, ramht, (offset + 4)/4);

	if (dev_priv->card_type < NV_40)
		return ((ctx & NV_RAMHT_CONTEXT_VALID) != 0);
	return (ctx != 0);
}

int
nouveau_ramht_insert(struct drm_device *dev, struct nouveau_gpuobj_ref *ref)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *instmem = &dev_priv->engine.instmem;
	struct nouveau_channel *chan = ref->channel;
	struct nouveau_gpuobj *ramht = chan->ramht ? chan->ramht->gpuobj : NULL;
	uint32_t ctx, co, ho;

	if (!ramht) {
		NV_ERROR(dev, "No hash table!\n");
		return -EINVAL;
	}

	if (dev_priv->card_type < NV_40) {
		ctx = NV_RAMHT_CONTEXT_VALID | (ref->instance >> 4) |
		      (chan->id << NV_RAMHT_CONTEXT_CHANNEL_SHIFT) |
		      (ref->gpuobj->engine << NV_RAMHT_CONTEXT_ENGINE_SHIFT);
	} else
	if (dev_priv->card_type < NV_50) {
		ctx = (ref->instance >> 4) |
		      (chan->id << NV40_RAMHT_CONTEXT_CHANNEL_SHIFT) |
		      (ref->gpuobj->engine << NV40_RAMHT_CONTEXT_ENGINE_SHIFT);
	} else {
		if (ref->gpuobj->engine == NVOBJ_ENGINE_DISPLAY) {
			ctx = (ref->instance << 10) | 2;
		} else {
			ctx = (ref->instance >> 4) |
			      ((ref->gpuobj->engine <<
				NV40_RAMHT_CONTEXT_ENGINE_SHIFT));
		}
	}

	co = ho = nouveau_ramht_hash_handle(dev, chan->id, ref->handle);
	do {
		if (!nouveau_ramht_entry_valid(dev, ramht, co)) {
			NV_DEBUG(dev,
				 "insert ch%d 0x%08x: h=0x%08x, c=0x%08x\n",
				 chan->id, co, ref->handle, ctx);
			nv_wo32(dev, ramht, (co + 0)/4, ref->handle);
			nv_wo32(dev, ramht, (co + 4)/4, ctx);

			list_add_tail(&ref->list, &chan->ramht_refs);
			instmem->flush(dev);
			return 0;
		}
		NV_DEBUG(dev, "collision ch%d 0x%08x: h=0x%08x\n",
			 chan->id, co, nv_ro32(dev, ramht, co/4));

		co += 8;
		if (co >= dev_priv->ramht_size)
			co = 0;
	} while (co != ho);

	NV_ERROR(dev, "RAMHT space exhausted. ch=%d\n", chan->id);
	return -ENOMEM;
}

void
nouveau_ramht_remove(struct drm_device *dev, struct nouveau_gpuobj_ref *ref)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *instmem = &dev_priv->engine.instmem;
	struct nouveau_channel *chan = ref->channel;
	struct nouveau_gpuobj *ramht = chan->ramht ? chan->ramht->gpuobj : NULL;
	uint32_t co, ho;

	if (!ramht) {
		NV_ERROR(dev, "No hash table!\n");
		return;
	}

	co = ho = nouveau_ramht_hash_handle(dev, chan->id, ref->handle);
	do {
		if (nouveau_ramht_entry_valid(dev, ramht, co) &&
		    (ref->handle == nv_ro32(dev, ramht, (co/4)))) {
			NV_DEBUG(dev,
				 "remove ch%d 0x%08x: h=0x%08x, c=0x%08x\n",
				 chan->id, co, ref->handle,
				 nv_ro32(dev, ramht, (co + 4)));
			nv_wo32(dev, ramht, (co + 0)/4, 0x00000000);
			nv_wo32(dev, ramht, (co + 4)/4, 0x00000000);

			list_del(&ref->list);
			instmem->flush(dev);
			return;
		}

		co += 8;
		if (co >= dev_priv->ramht_size)
			co = 0;
	} while (co != ho);
	list_del(&ref->list);

	NV_ERROR(dev, "RAMHT entry not found. ch=%d, handle=0x%08x\n",
		 chan->id, ref->handle);
}
