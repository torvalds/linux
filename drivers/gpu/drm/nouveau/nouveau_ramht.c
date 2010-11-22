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

static u32
nouveau_ramht_hash_handle(struct nouveau_channel *chan, u32 handle)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_ramht *ramht = chan->ramht;
	u32 hash = 0;
	int i;

	NV_DEBUG(dev, "ch%d handle=0x%08x\n", chan->id, handle);

	for (i = 32; i > 0; i -= ramht->bits) {
		hash ^= (handle & ((1 << ramht->bits) - 1));
		handle >>= ramht->bits;
	}

	if (dev_priv->card_type < NV_50)
		hash ^= chan->id << (ramht->bits - 4);
	hash <<= 3;

	NV_DEBUG(dev, "hash=0x%08x\n", hash);
	return hash;
}

static int
nouveau_ramht_entry_valid(struct drm_device *dev, struct nouveau_gpuobj *ramht,
			  u32 offset)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 ctx = nv_ro32(ramht, offset + 4);

	if (dev_priv->card_type < NV_40)
		return ((ctx & NV_RAMHT_CONTEXT_VALID) != 0);
	return (ctx != 0);
}

static int
nouveau_ramht_entry_same_channel(struct nouveau_channel *chan,
				 struct nouveau_gpuobj *ramht, u32 offset)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	u32 ctx = nv_ro32(ramht, offset + 4);

	if (dev_priv->card_type >= NV_50)
		return true;
	else if (dev_priv->card_type >= NV_40)
		return chan->id ==
			((ctx >> NV40_RAMHT_CONTEXT_CHANNEL_SHIFT) & 0x1f);
	else
		return chan->id ==
			((ctx >> NV_RAMHT_CONTEXT_CHANNEL_SHIFT) & 0x1f);
}

int
nouveau_ramht_insert(struct nouveau_channel *chan, u32 handle,
		     struct nouveau_gpuobj *gpuobj)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *instmem = &dev_priv->engine.instmem;
	struct nouveau_ramht_entry *entry;
	struct nouveau_gpuobj *ramht = chan->ramht->gpuobj;
	unsigned long flags;
	u32 ctx, co, ho;

	if (nouveau_ramht_find(chan, handle))
		return -EEXIST;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	entry->channel = chan;
	entry->gpuobj = NULL;
	entry->handle = handle;
	nouveau_gpuobj_ref(gpuobj, &entry->gpuobj);

	if (dev_priv->card_type < NV_40) {
		ctx = NV_RAMHT_CONTEXT_VALID | (gpuobj->cinst >> 4) |
		      (chan->id << NV_RAMHT_CONTEXT_CHANNEL_SHIFT) |
		      (gpuobj->engine << NV_RAMHT_CONTEXT_ENGINE_SHIFT);
	} else
	if (dev_priv->card_type < NV_50) {
		ctx = (gpuobj->cinst >> 4) |
		      (chan->id << NV40_RAMHT_CONTEXT_CHANNEL_SHIFT) |
		      (gpuobj->engine << NV40_RAMHT_CONTEXT_ENGINE_SHIFT);
	} else {
		if (gpuobj->engine == NVOBJ_ENGINE_DISPLAY) {
			ctx = (gpuobj->cinst << 10) | 2;
		} else {
			ctx = (gpuobj->cinst >> 4) |
			      ((gpuobj->engine <<
				NV40_RAMHT_CONTEXT_ENGINE_SHIFT));
		}
	}

	spin_lock_irqsave(&chan->ramht->lock, flags);
	list_add(&entry->head, &chan->ramht->entries);

	co = ho = nouveau_ramht_hash_handle(chan, handle);
	do {
		if (!nouveau_ramht_entry_valid(dev, ramht, co)) {
			NV_DEBUG(dev,
				 "insert ch%d 0x%08x: h=0x%08x, c=0x%08x\n",
				 chan->id, co, handle, ctx);
			nv_wo32(ramht, co + 0, handle);
			nv_wo32(ramht, co + 4, ctx);

			spin_unlock_irqrestore(&chan->ramht->lock, flags);
			instmem->flush(dev);
			return 0;
		}
		NV_DEBUG(dev, "collision ch%d 0x%08x: h=0x%08x\n",
			 chan->id, co, nv_ro32(ramht, co));

		co += 8;
		if (co >= ramht->size)
			co = 0;
	} while (co != ho);

	NV_ERROR(dev, "RAMHT space exhausted. ch=%d\n", chan->id);
	list_del(&entry->head);
	spin_unlock_irqrestore(&chan->ramht->lock, flags);
	kfree(entry);
	return -ENOMEM;
}

static struct nouveau_ramht_entry *
nouveau_ramht_remove_entry(struct nouveau_channel *chan, u32 handle)
{
	struct nouveau_ramht *ramht = chan ? chan->ramht : NULL;
	struct nouveau_ramht_entry *entry;
	unsigned long flags;

	if (!ramht)
		return NULL;

	spin_lock_irqsave(&ramht->lock, flags);
	list_for_each_entry(entry, &ramht->entries, head) {
		if (entry->channel == chan &&
		    (!handle || entry->handle == handle)) {
			list_del(&entry->head);
			spin_unlock_irqrestore(&ramht->lock, flags);

			return entry;
		}
	}
	spin_unlock_irqrestore(&ramht->lock, flags);

	return NULL;
}

static void
nouveau_ramht_remove_hash(struct nouveau_channel *chan, u32 handle)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *instmem = &dev_priv->engine.instmem;
	struct nouveau_gpuobj *ramht = chan->ramht->gpuobj;
	unsigned long flags;
	u32 co, ho;

	spin_lock_irqsave(&chan->ramht->lock, flags);
	co = ho = nouveau_ramht_hash_handle(chan, handle);
	do {
		if (nouveau_ramht_entry_valid(dev, ramht, co) &&
		    nouveau_ramht_entry_same_channel(chan, ramht, co) &&
		    (handle == nv_ro32(ramht, co))) {
			NV_DEBUG(dev,
				 "remove ch%d 0x%08x: h=0x%08x, c=0x%08x\n",
				 chan->id, co, handle, nv_ro32(ramht, co + 4));
			nv_wo32(ramht, co + 0, 0x00000000);
			nv_wo32(ramht, co + 4, 0x00000000);
			instmem->flush(dev);
			goto out;
		}

		co += 8;
		if (co >= ramht->size)
			co = 0;
	} while (co != ho);

	NV_ERROR(dev, "RAMHT entry not found. ch=%d, handle=0x%08x\n",
		 chan->id, handle);
out:
	spin_unlock_irqrestore(&chan->ramht->lock, flags);
}

void
nouveau_ramht_remove(struct nouveau_channel *chan, u32 handle)
{
	struct nouveau_ramht_entry *entry;

	entry = nouveau_ramht_remove_entry(chan, handle);
	if (!entry)
		return;

	nouveau_ramht_remove_hash(chan, entry->handle);
	nouveau_gpuobj_ref(NULL, &entry->gpuobj);
	kfree(entry);
}

struct nouveau_gpuobj *
nouveau_ramht_find(struct nouveau_channel *chan, u32 handle)
{
	struct nouveau_ramht *ramht = chan->ramht;
	struct nouveau_ramht_entry *entry;
	struct nouveau_gpuobj *gpuobj = NULL;
	unsigned long flags;

	if (unlikely(!chan->ramht))
		return NULL;

	spin_lock_irqsave(&ramht->lock, flags);
	list_for_each_entry(entry, &chan->ramht->entries, head) {
		if (entry->channel == chan && entry->handle == handle) {
			gpuobj = entry->gpuobj;
			break;
		}
	}
	spin_unlock_irqrestore(&ramht->lock, flags);

	return gpuobj;
}

int
nouveau_ramht_new(struct drm_device *dev, struct nouveau_gpuobj *gpuobj,
		  struct nouveau_ramht **pramht)
{
	struct nouveau_ramht *ramht;

	ramht = kzalloc(sizeof(*ramht), GFP_KERNEL);
	if (!ramht)
		return -ENOMEM;

	ramht->dev = dev;
	kref_init(&ramht->refcount);
	ramht->bits = drm_order(gpuobj->size / 8);
	INIT_LIST_HEAD(&ramht->entries);
	spin_lock_init(&ramht->lock);
	nouveau_gpuobj_ref(gpuobj, &ramht->gpuobj);

	*pramht = ramht;
	return 0;
}

static void
nouveau_ramht_del(struct kref *ref)
{
	struct nouveau_ramht *ramht =
		container_of(ref, struct nouveau_ramht, refcount);

	nouveau_gpuobj_ref(NULL, &ramht->gpuobj);
	kfree(ramht);
}

void
nouveau_ramht_ref(struct nouveau_ramht *ref, struct nouveau_ramht **ptr,
		  struct nouveau_channel *chan)
{
	struct nouveau_ramht_entry *entry;
	struct nouveau_ramht *ramht;

	if (ref)
		kref_get(&ref->refcount);

	ramht = *ptr;
	if (ramht) {
		while ((entry = nouveau_ramht_remove_entry(chan, 0))) {
			nouveau_ramht_remove_hash(chan, entry->handle);
			nouveau_gpuobj_ref(NULL, &entry->gpuobj);
			kfree(entry);
		}

		kref_put(&ramht->refcount, nouveau_ramht_del);
	}
	*ptr = ref;
}
