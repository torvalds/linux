/*
 * Copyright (C) 2006 Ben Skeggs.
 *
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

/*
 * Authors:
 *   Ben Skeggs <darktama@iinet.net.au>
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

/* NVidia uses context objects to drive drawing operations.

   Context objects can be selected into 8 subchannels in the FIFO,
   and then used via DMA command buffers.

   A context object is referenced by a user defined handle (CARD32). The HW
   looks up graphics objects in a hash table in the instance RAM.

   An entry in the hash table consists of 2 CARD32. The first CARD32 contains
   the handle, the second one a bitfield, that contains the address of the
   object in instance RAM.

   The format of the second CARD32 seems to be:

   NV4 to NV30:

   15: 0  instance_addr >> 4
   17:16  engine (here uses 1 = graphics)
   28:24  channel id (here uses 0)
   31	  valid (use 1)

   NV40:

   15: 0  instance_addr >> 4   (maybe 19-0)
   21:20  engine (here uses 1 = graphics)
   I'm unsure about the other bits, but using 0 seems to work.

   The key into the hash table depends on the object handle and channel id and
   is given as:
*/
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

static int
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

	instmem->prepare_access(dev, true);
	co = ho = nouveau_ramht_hash_handle(dev, chan->id, ref->handle);
	do {
		if (!nouveau_ramht_entry_valid(dev, ramht, co)) {
			NV_DEBUG(dev,
				 "insert ch%d 0x%08x: h=0x%08x, c=0x%08x\n",
				 chan->id, co, ref->handle, ctx);
			nv_wo32(dev, ramht, (co + 0)/4, ref->handle);
			nv_wo32(dev, ramht, (co + 4)/4, ctx);

			list_add_tail(&ref->list, &chan->ramht_refs);
			instmem->finish_access(dev);
			return 0;
		}
		NV_DEBUG(dev, "collision ch%d 0x%08x: h=0x%08x\n",
			 chan->id, co, nv_ro32(dev, ramht, co/4));

		co += 8;
		if (co >= dev_priv->ramht_size)
			co = 0;
	} while (co != ho);
	instmem->finish_access(dev);

	NV_ERROR(dev, "RAMHT space exhausted. ch=%d\n", chan->id);
	return -ENOMEM;
}

static void
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

	instmem->prepare_access(dev, true);
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
			instmem->finish_access(dev);
			return;
		}

		co += 8;
		if (co >= dev_priv->ramht_size)
			co = 0;
	} while (co != ho);
	list_del(&ref->list);
	instmem->finish_access(dev);

	NV_ERROR(dev, "RAMHT entry not found. ch=%d, handle=0x%08x\n",
		 chan->id, ref->handle);
}

int
nouveau_gpuobj_new(struct drm_device *dev, struct nouveau_channel *chan,
		   uint32_t size, int align, uint32_t flags,
		   struct nouveau_gpuobj **gpuobj_ret)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	struct nouveau_gpuobj *gpuobj;
	struct mem_block *pramin = NULL;
	int ret;

	NV_DEBUG(dev, "ch%d size=%u align=%d flags=0x%08x\n",
		 chan ? chan->id : -1, size, align, flags);

	if (!dev_priv || !gpuobj_ret || *gpuobj_ret != NULL)
		return -EINVAL;

	gpuobj = kzalloc(sizeof(*gpuobj), GFP_KERNEL);
	if (!gpuobj)
		return -ENOMEM;
	NV_DEBUG(dev, "gpuobj %p\n", gpuobj);
	gpuobj->flags = flags;
	gpuobj->im_channel = chan;

	list_add_tail(&gpuobj->list, &dev_priv->gpuobj_list);

	/* Choose between global instmem heap, and per-channel private
	 * instmem heap.  On <NV50 allow requests for private instmem
	 * to be satisfied from global heap if no per-channel area
	 * available.
	 */
	if (chan) {
		if (chan->ramin_heap) {
			NV_DEBUG(dev, "private heap\n");
			pramin = chan->ramin_heap;
		} else
		if (dev_priv->card_type < NV_50) {
			NV_DEBUG(dev, "global heap fallback\n");
			pramin = dev_priv->ramin_heap;
		}
	} else {
		NV_DEBUG(dev, "global heap\n");
		pramin = dev_priv->ramin_heap;
	}

	if (!pramin) {
		NV_ERROR(dev, "No PRAMIN heap!\n");
		return -EINVAL;
	}

	if (!chan) {
		ret = engine->instmem.populate(dev, gpuobj, &size);
		if (ret) {
			nouveau_gpuobj_del(dev, &gpuobj);
			return ret;
		}
	}

	/* Allocate a chunk of the PRAMIN aperture */
	gpuobj->im_pramin = nouveau_mem_alloc_block(pramin, size,
						    drm_order(align),
						    (struct drm_file *)-2, 0);
	if (!gpuobj->im_pramin) {
		nouveau_gpuobj_del(dev, &gpuobj);
		return -ENOMEM;
	}

	if (!chan) {
		ret = engine->instmem.bind(dev, gpuobj);
		if (ret) {
			nouveau_gpuobj_del(dev, &gpuobj);
			return ret;
		}
	}

	if (gpuobj->flags & NVOBJ_FLAG_ZERO_ALLOC) {
		int i;

		engine->instmem.prepare_access(dev, true);
		for (i = 0; i < gpuobj->im_pramin->size; i += 4)
			nv_wo32(dev, gpuobj, i/4, 0);
		engine->instmem.finish_access(dev);
	}

	*gpuobj_ret = gpuobj;
	return 0;
}

int
nouveau_gpuobj_early_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	NV_DEBUG(dev, "\n");

	INIT_LIST_HEAD(&dev_priv->gpuobj_list);

	return 0;
}

int
nouveau_gpuobj_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	NV_DEBUG(dev, "\n");

	if (dev_priv->card_type < NV_50) {
		ret = nouveau_gpuobj_new_fake(dev,
			dev_priv->ramht_offset, ~0, dev_priv->ramht_size,
			NVOBJ_FLAG_ZERO_ALLOC | NVOBJ_FLAG_ALLOW_NO_REFS,
						&dev_priv->ramht, NULL);
		if (ret)
			return ret;
	}

	return 0;
}

void
nouveau_gpuobj_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	NV_DEBUG(dev, "\n");

	nouveau_gpuobj_del(dev, &dev_priv->ramht);
}

void
nouveau_gpuobj_late_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = NULL;
	struct list_head *entry, *tmp;

	NV_DEBUG(dev, "\n");

	list_for_each_safe(entry, tmp, &dev_priv->gpuobj_list) {
		gpuobj = list_entry(entry, struct nouveau_gpuobj, list);

		NV_ERROR(dev, "gpuobj %p still exists at takedown, refs=%d\n",
			 gpuobj, gpuobj->refcount);
		gpuobj->refcount = 0;
		nouveau_gpuobj_del(dev, &gpuobj);
	}
}

int
nouveau_gpuobj_del(struct drm_device *dev, struct nouveau_gpuobj **pgpuobj)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	struct nouveau_gpuobj *gpuobj;
	int i;

	NV_DEBUG(dev, "gpuobj %p\n", pgpuobj ? *pgpuobj : NULL);

	if (!dev_priv || !pgpuobj || !(*pgpuobj))
		return -EINVAL;
	gpuobj = *pgpuobj;

	if (gpuobj->refcount != 0) {
		NV_ERROR(dev, "gpuobj refcount is %d\n", gpuobj->refcount);
		return -EINVAL;
	}

	if (gpuobj->im_pramin && (gpuobj->flags & NVOBJ_FLAG_ZERO_FREE)) {
		engine->instmem.prepare_access(dev, true);
		for (i = 0; i < gpuobj->im_pramin->size; i += 4)
			nv_wo32(dev, gpuobj, i/4, 0);
		engine->instmem.finish_access(dev);
	}

	if (gpuobj->dtor)
		gpuobj->dtor(dev, gpuobj);

	if (gpuobj->im_backing && !(gpuobj->flags & NVOBJ_FLAG_FAKE))
		engine->instmem.clear(dev, gpuobj);

	if (gpuobj->im_pramin) {
		if (gpuobj->flags & NVOBJ_FLAG_FAKE)
			kfree(gpuobj->im_pramin);
		else
			nouveau_mem_free_block(gpuobj->im_pramin);
	}

	list_del(&gpuobj->list);

	*pgpuobj = NULL;
	kfree(gpuobj);
	return 0;
}

static int
nouveau_gpuobj_instance_get(struct drm_device *dev,
			    struct nouveau_channel *chan,
			    struct nouveau_gpuobj *gpuobj, uint32_t *inst)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *cpramin;

	/* <NV50 use PRAMIN address everywhere */
	if (dev_priv->card_type < NV_50) {
		*inst = gpuobj->im_pramin->start;
		return 0;
	}

	if (chan && gpuobj->im_channel != chan) {
		NV_ERROR(dev, "Channel mismatch: obj %d, ref %d\n",
			 gpuobj->im_channel->id, chan->id);
		return -EINVAL;
	}

	/* NV50 channel-local instance */
	if (chan) {
		cpramin = chan->ramin->gpuobj;
		*inst = gpuobj->im_pramin->start - cpramin->im_pramin->start;
		return 0;
	}

	/* NV50 global (VRAM) instance */
	if (!gpuobj->im_channel) {
		/* ...from global heap */
		if (!gpuobj->im_backing) {
			NV_ERROR(dev, "AII, no VRAM backing gpuobj\n");
			return -EINVAL;
		}
		*inst = gpuobj->im_backing_start;
		return 0;
	} else {
		/* ...from local heap */
		cpramin = gpuobj->im_channel->ramin->gpuobj;
		*inst = cpramin->im_backing_start +
			(gpuobj->im_pramin->start - cpramin->im_pramin->start);
		return 0;
	}

	return -EINVAL;
}

int
nouveau_gpuobj_ref_add(struct drm_device *dev, struct nouveau_channel *chan,
		       uint32_t handle, struct nouveau_gpuobj *gpuobj,
		       struct nouveau_gpuobj_ref **ref_ret)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj_ref *ref;
	uint32_t instance;
	int ret;

	NV_DEBUG(dev, "ch%d h=0x%08x gpuobj=%p\n",
		 chan ? chan->id : -1, handle, gpuobj);

	if (!dev_priv || !gpuobj || (ref_ret && *ref_ret != NULL))
		return -EINVAL;

	if (!chan && !ref_ret)
		return -EINVAL;

	if (gpuobj->engine == NVOBJ_ENGINE_SW && !gpuobj->im_pramin) {
		/* sw object */
		instance = 0x40;
	} else {
		ret = nouveau_gpuobj_instance_get(dev, chan, gpuobj, &instance);
		if (ret)
			return ret;
	}

	ref = kzalloc(sizeof(*ref), GFP_KERNEL);
	if (!ref)
		return -ENOMEM;
	INIT_LIST_HEAD(&ref->list);
	ref->gpuobj   = gpuobj;
	ref->channel  = chan;
	ref->instance = instance;

	if (!ref_ret) {
		ref->handle = handle;

		ret = nouveau_ramht_insert(dev, ref);
		if (ret) {
			kfree(ref);
			return ret;
		}
	} else {
		ref->handle = ~0;
		*ref_ret = ref;
	}

	ref->gpuobj->refcount++;
	return 0;
}

int nouveau_gpuobj_ref_del(struct drm_device *dev, struct nouveau_gpuobj_ref **pref)
{
	struct nouveau_gpuobj_ref *ref;

	NV_DEBUG(dev, "ref %p\n", pref ? *pref : NULL);

	if (!dev || !pref || *pref == NULL)
		return -EINVAL;
	ref = *pref;

	if (ref->handle != ~0)
		nouveau_ramht_remove(dev, ref);

	if (ref->gpuobj) {
		ref->gpuobj->refcount--;

		if (ref->gpuobj->refcount == 0) {
			if (!(ref->gpuobj->flags & NVOBJ_FLAG_ALLOW_NO_REFS))
				nouveau_gpuobj_del(dev, &ref->gpuobj);
		}
	}

	*pref = NULL;
	kfree(ref);
	return 0;
}

int
nouveau_gpuobj_new_ref(struct drm_device *dev,
		       struct nouveau_channel *oc, struct nouveau_channel *rc,
		       uint32_t handle, uint32_t size, int align,
		       uint32_t flags, struct nouveau_gpuobj_ref **ref)
{
	struct nouveau_gpuobj *gpuobj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(dev, oc, size, align, flags, &gpuobj);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_ref_add(dev, rc, handle, gpuobj, ref);
	if (ret) {
		nouveau_gpuobj_del(dev, &gpuobj);
		return ret;
	}

	return 0;
}

int
nouveau_gpuobj_ref_find(struct nouveau_channel *chan, uint32_t handle,
			struct nouveau_gpuobj_ref **ref_ret)
{
	struct nouveau_gpuobj_ref *ref;
	struct list_head *entry, *tmp;

	list_for_each_safe(entry, tmp, &chan->ramht_refs) {
		ref = list_entry(entry, struct nouveau_gpuobj_ref, list);

		if (ref->handle == handle) {
			if (ref_ret)
				*ref_ret = ref;
			return 0;
		}
	}

	return -EINVAL;
}

int
nouveau_gpuobj_new_fake(struct drm_device *dev, uint32_t p_offset,
			uint32_t b_offset, uint32_t size,
			uint32_t flags, struct nouveau_gpuobj **pgpuobj,
			struct nouveau_gpuobj_ref **pref)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = NULL;
	int i;

	NV_DEBUG(dev,
		 "p_offset=0x%08x b_offset=0x%08x size=0x%08x flags=0x%08x\n",
		 p_offset, b_offset, size, flags);

	gpuobj = kzalloc(sizeof(*gpuobj), GFP_KERNEL);
	if (!gpuobj)
		return -ENOMEM;
	NV_DEBUG(dev, "gpuobj %p\n", gpuobj);
	gpuobj->im_channel = NULL;
	gpuobj->flags      = flags | NVOBJ_FLAG_FAKE;

	list_add_tail(&gpuobj->list, &dev_priv->gpuobj_list);

	if (p_offset != ~0) {
		gpuobj->im_pramin = kzalloc(sizeof(struct mem_block),
					    GFP_KERNEL);
		if (!gpuobj->im_pramin) {
			nouveau_gpuobj_del(dev, &gpuobj);
			return -ENOMEM;
		}
		gpuobj->im_pramin->start = p_offset;
		gpuobj->im_pramin->size  = size;
	}

	if (b_offset != ~0) {
		gpuobj->im_backing = (struct nouveau_bo *)-1;
		gpuobj->im_backing_start = b_offset;
	}

	if (gpuobj->flags & NVOBJ_FLAG_ZERO_ALLOC) {
		dev_priv->engine.instmem.prepare_access(dev, true);
		for (i = 0; i < gpuobj->im_pramin->size; i += 4)
			nv_wo32(dev, gpuobj, i/4, 0);
		dev_priv->engine.instmem.finish_access(dev);
	}

	if (pref) {
		i = nouveau_gpuobj_ref_add(dev, NULL, 0, gpuobj, pref);
		if (i) {
			nouveau_gpuobj_del(dev, &gpuobj);
			return i;
		}
	}

	if (pgpuobj)
		*pgpuobj = gpuobj;
	return 0;
}


static uint32_t
nouveau_gpuobj_class_instmem_size(struct drm_device *dev, int class)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	/*XXX: dodgy hack for now */
	if (dev_priv->card_type >= NV_50)
		return 24;
	if (dev_priv->card_type >= NV_40)
		return 32;
	return 16;
}

/*
   DMA objects are used to reference a piece of memory in the
   framebuffer, PCI or AGP address space. Each object is 16 bytes big
   and looks as follows:

   entry[0]
   11:0  class (seems like I can always use 0 here)
   12    page table present?
   13    page entry linear?
   15:14 access: 0 rw, 1 ro, 2 wo
   17:16 target: 0 NV memory, 1 NV memory tiled, 2 PCI, 3 AGP
   31:20 dma adjust (bits 0-11 of the address)
   entry[1]
   dma limit (size of transfer)
   entry[X]
   1     0 readonly, 1 readwrite
   31:12 dma frame address of the page (bits 12-31 of the address)
   entry[N]
   page table terminator, same value as the first pte, as does nvidia
   rivatv uses 0xffffffff

   Non linear page tables need a list of frame addresses afterwards,
   the rivatv project has some info on this.

   The method below creates a DMA object in instance RAM and returns a handle
   to it that can be used to set up context objects.
*/
int
nouveau_gpuobj_dma_new(struct nouveau_channel *chan, int class,
		       uint64_t offset, uint64_t size, int access,
		       int target, struct nouveau_gpuobj **gpuobj)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *instmem = &dev_priv->engine.instmem;
	int ret;

	NV_DEBUG(dev, "ch%d class=0x%04x offset=0x%llx size=0x%llx\n",
		 chan->id, class, offset, size);
	NV_DEBUG(dev, "access=%d target=%d\n", access, target);

	switch (target) {
	case NV_DMA_TARGET_AGP:
		offset += dev_priv->gart_info.aper_base;
		break;
	default:
		break;
	}

	ret = nouveau_gpuobj_new(dev, chan,
				 nouveau_gpuobj_class_instmem_size(dev, class),
				 16, NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, gpuobj);
	if (ret) {
		NV_ERROR(dev, "Error creating gpuobj: %d\n", ret);
		return ret;
	}

	instmem->prepare_access(dev, true);

	if (dev_priv->card_type < NV_50) {
		uint32_t frame, adjust, pte_flags = 0;

		if (access != NV_DMA_ACCESS_RO)
			pte_flags |= (1<<1);
		adjust = offset &  0x00000fff;
		frame  = offset & ~0x00000fff;

		nv_wo32(dev, *gpuobj, 0, ((1<<12) | (1<<13) |
				(adjust << 20) |
				 (access << 14) |
				 (target << 16) |
				  class));
		nv_wo32(dev, *gpuobj, 1, size - 1);
		nv_wo32(dev, *gpuobj, 2, frame | pte_flags);
		nv_wo32(dev, *gpuobj, 3, frame | pte_flags);
	} else {
		uint64_t limit = offset + size - 1;
		uint32_t flags0, flags5;

		if (target == NV_DMA_TARGET_VIDMEM) {
			flags0 = 0x00190000;
			flags5 = 0x00010000;
		} else {
			flags0 = 0x7fc00000;
			flags5 = 0x00080000;
		}

		nv_wo32(dev, *gpuobj, 0, flags0 | class);
		nv_wo32(dev, *gpuobj, 1, lower_32_bits(limit));
		nv_wo32(dev, *gpuobj, 2, lower_32_bits(offset));
		nv_wo32(dev, *gpuobj, 3, ((upper_32_bits(limit) & 0xff) << 24) |
					(upper_32_bits(offset) & 0xff));
		nv_wo32(dev, *gpuobj, 5, flags5);
	}

	instmem->finish_access(dev);

	(*gpuobj)->engine = NVOBJ_ENGINE_SW;
	(*gpuobj)->class  = class;
	return 0;
}

int
nouveau_gpuobj_gart_dma_new(struct nouveau_channel *chan,
			    uint64_t offset, uint64_t size, int access,
			    struct nouveau_gpuobj **gpuobj,
			    uint32_t *o_ret)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	if (dev_priv->gart_info.type == NOUVEAU_GART_AGP ||
	    (dev_priv->card_type >= NV_50 &&
	     dev_priv->gart_info.type == NOUVEAU_GART_SGDMA)) {
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     offset + dev_priv->vm_gart_base,
					     size, access, NV_DMA_TARGET_AGP,
					     gpuobj);
		if (o_ret)
			*o_ret = 0;
	} else
	if (dev_priv->gart_info.type == NOUVEAU_GART_SGDMA) {
		*gpuobj = dev_priv->gart_info.sg_ctxdma;
		if (offset & ~0xffffffffULL) {
			NV_ERROR(dev, "obj offset exceeds 32-bits\n");
			return -EINVAL;
		}
		if (o_ret)
			*o_ret = (uint32_t)offset;
		ret = (*gpuobj != NULL) ? 0 : -EINVAL;
	} else {
		NV_ERROR(dev, "Invalid GART type %d\n", dev_priv->gart_info.type);
		return -EINVAL;
	}

	return ret;
}

/* Context objects in the instance RAM have the following structure.
 * On NV40 they are 32 byte long, on NV30 and smaller 16 bytes.

   NV4 - NV30:

   entry[0]
   11:0 class
   12   chroma key enable
   13   user clip enable
   14   swizzle enable
   17:15 patch config:
       scrcopy_and, rop_and, blend_and, scrcopy, srccopy_pre, blend_pre
   18   synchronize enable
   19   endian: 1 big, 0 little
   21:20 dither mode
   23    single step enable
   24    patch status: 0 invalid, 1 valid
   25    context_surface 0: 1 valid
   26    context surface 1: 1 valid
   27    context pattern: 1 valid
   28    context rop: 1 valid
   29,30 context beta, beta4
   entry[1]
   7:0   mono format
   15:8  color format
   31:16 notify instance address
   entry[2]
   15:0  dma 0 instance address
   31:16 dma 1 instance address
   entry[3]
   dma method traps

   NV40:
   No idea what the exact format is. Here's what can be deducted:

   entry[0]:
   11:0  class  (maybe uses more bits here?)
   17    user clip enable
   21:19 patch config
   25    patch status valid ?
   entry[1]:
   15:0  DMA notifier  (maybe 20:0)
   entry[2]:
   15:0  DMA 0 instance (maybe 20:0)
   24    big endian
   entry[3]:
   15:0  DMA 1 instance (maybe 20:0)
   entry[4]:
   entry[5]:
   set to 0?
*/
int
nouveau_gpuobj_gr_new(struct nouveau_channel *chan, int class,
		      struct nouveau_gpuobj **gpuobj)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	NV_DEBUG(dev, "ch%d class=0x%04x\n", chan->id, class);

	ret = nouveau_gpuobj_new(dev, chan,
				 nouveau_gpuobj_class_instmem_size(dev, class),
				 16,
				 NVOBJ_FLAG_ZERO_ALLOC | NVOBJ_FLAG_ZERO_FREE,
				 gpuobj);
	if (ret) {
		NV_ERROR(dev, "Error creating gpuobj: %d\n", ret);
		return ret;
	}

	dev_priv->engine.instmem.prepare_access(dev, true);
	if (dev_priv->card_type >= NV_50) {
		nv_wo32(dev, *gpuobj, 0, class);
		nv_wo32(dev, *gpuobj, 5, 0x00010000);
	} else {
		switch (class) {
		case NV_CLASS_NULL:
			nv_wo32(dev, *gpuobj, 0, 0x00001030);
			nv_wo32(dev, *gpuobj, 1, 0xFFFFFFFF);
			break;
		default:
			if (dev_priv->card_type >= NV_40) {
				nv_wo32(dev, *gpuobj, 0, class);
#ifdef __BIG_ENDIAN
				nv_wo32(dev, *gpuobj, 2, 0x01000000);
#endif
			} else {
#ifdef __BIG_ENDIAN
				nv_wo32(dev, *gpuobj, 0, class | 0x00080000);
#else
				nv_wo32(dev, *gpuobj, 0, class);
#endif
			}
		}
	}
	dev_priv->engine.instmem.finish_access(dev);

	(*gpuobj)->engine = NVOBJ_ENGINE_GR;
	(*gpuobj)->class  = class;
	return 0;
}

int
nouveau_gpuobj_sw_new(struct nouveau_channel *chan, int class,
		      struct nouveau_gpuobj **gpuobj_ret)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nouveau_gpuobj *gpuobj;

	if (!chan || !gpuobj_ret || *gpuobj_ret != NULL)
		return -EINVAL;

	gpuobj = kzalloc(sizeof(*gpuobj), GFP_KERNEL);
	if (!gpuobj)
		return -ENOMEM;
	gpuobj->engine = NVOBJ_ENGINE_SW;
	gpuobj->class = class;

	list_add_tail(&gpuobj->list, &dev_priv->gpuobj_list);
	*gpuobj_ret = gpuobj;
	return 0;
}

static int
nouveau_gpuobj_channel_init_pramin(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *pramin = NULL;
	uint32_t size;
	uint32_t base;
	int ret;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	/* Base amount for object storage (4KiB enough?) */
	size = 0x1000;
	base = 0;

	/* PGRAPH context */

	if (dev_priv->card_type == NV_50) {
		/* Various fixed table thingos */
		size += 0x1400; /* mostly unknown stuff */
		size += 0x4000; /* vm pd */
		base  = 0x6000;
		/* RAMHT, not sure about setting size yet, 32KiB to be safe */
		size += 0x8000;
		/* RAMFC */
		size += 0x1000;
		/* PGRAPH context */
		size += 0x70000;
	}

	NV_DEBUG(dev, "ch%d PRAMIN size: 0x%08x bytes, base alloc=0x%08x\n",
		 chan->id, size, base);
	ret = nouveau_gpuobj_new_ref(dev, NULL, NULL, 0, size, 0x1000, 0,
				     &chan->ramin);
	if (ret) {
		NV_ERROR(dev, "Error allocating channel PRAMIN: %d\n", ret);
		return ret;
	}
	pramin = chan->ramin->gpuobj;

	ret = nouveau_mem_init_heap(&chan->ramin_heap,
				    pramin->im_pramin->start + base, size);
	if (ret) {
		NV_ERROR(dev, "Error creating PRAMIN heap: %d\n", ret);
		nouveau_gpuobj_ref_del(dev, &chan->ramin);
		return ret;
	}

	return 0;
}

int
nouveau_gpuobj_channel_init(struct nouveau_channel *chan,
			    uint32_t vram_h, uint32_t tt_h)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *instmem = &dev_priv->engine.instmem;
	struct nouveau_gpuobj *vram = NULL, *tt = NULL;
	int ret, i;

	INIT_LIST_HEAD(&chan->ramht_refs);

	NV_DEBUG(dev, "ch%d vram=0x%08x tt=0x%08x\n", chan->id, vram_h, tt_h);

	/* Reserve a block of PRAMIN for the channel
	 *XXX: maybe on <NV50 too at some point
	 */
	if (0 || dev_priv->card_type == NV_50) {
		ret = nouveau_gpuobj_channel_init_pramin(chan);
		if (ret) {
			NV_ERROR(dev, "init pramin\n");
			return ret;
		}
	}

	/* NV50 VM
	 *  - Allocate per-channel page-directory
	 *  - Map GART and VRAM into the channel's address space at the
	 *    locations determined during init.
	 */
	if (dev_priv->card_type >= NV_50) {
		uint32_t vm_offset, pde;

		instmem->prepare_access(dev, true);

		vm_offset = (dev_priv->chipset & 0xf0) == 0x50 ? 0x1400 : 0x200;
		vm_offset += chan->ramin->gpuobj->im_pramin->start;

		ret = nouveau_gpuobj_new_fake(dev, vm_offset, ~0, 0x4000,
							0, &chan->vm_pd, NULL);
		if (ret) {
			instmem->finish_access(dev);
			return ret;
		}
		for (i = 0; i < 0x4000; i += 8) {
			nv_wo32(dev, chan->vm_pd, (i+0)/4, 0x00000000);
			nv_wo32(dev, chan->vm_pd, (i+4)/4, 0xdeadcafe);
		}

		pde = (dev_priv->vm_gart_base / (512*1024*1024)) * 2;
		ret = nouveau_gpuobj_ref_add(dev, NULL, 0,
					     dev_priv->gart_info.sg_ctxdma,
					     &chan->vm_gart_pt);
		if (ret) {
			instmem->finish_access(dev);
			return ret;
		}
		nv_wo32(dev, chan->vm_pd, pde++,
			    chan->vm_gart_pt->instance | 0x03);
		nv_wo32(dev, chan->vm_pd, pde++, 0x00000000);

		pde = (dev_priv->vm_vram_base / (512*1024*1024)) * 2;
		for (i = 0; i < dev_priv->vm_vram_pt_nr; i++) {
			ret = nouveau_gpuobj_ref_add(dev, NULL, 0,
						     dev_priv->vm_vram_pt[i],
						     &chan->vm_vram_pt[i]);
			if (ret) {
				instmem->finish_access(dev);
				return ret;
			}

			nv_wo32(dev, chan->vm_pd, pde++,
				    chan->vm_vram_pt[i]->instance | 0x61);
			nv_wo32(dev, chan->vm_pd, pde++, 0x00000000);
		}

		instmem->finish_access(dev);
	}

	/* RAMHT */
	if (dev_priv->card_type < NV_50) {
		ret = nouveau_gpuobj_ref_add(dev, NULL, 0, dev_priv->ramht,
					     &chan->ramht);
		if (ret)
			return ret;
	} else {
		ret = nouveau_gpuobj_new_ref(dev, chan, chan, 0,
					     0x8000, 16,
					     NVOBJ_FLAG_ZERO_ALLOC,
					     &chan->ramht);
		if (ret)
			return ret;
	}

	/* VRAM ctxdma */
	if (dev_priv->card_type >= NV_50) {
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     0, dev_priv->vm_end,
					     NV_DMA_ACCESS_RW,
					     NV_DMA_TARGET_AGP, &vram);
		if (ret) {
			NV_ERROR(dev, "Error creating VRAM ctxdma: %d\n", ret);
			return ret;
		}
	} else {
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
						0, dev_priv->fb_available_size,
						NV_DMA_ACCESS_RW,
						NV_DMA_TARGET_VIDMEM, &vram);
		if (ret) {
			NV_ERROR(dev, "Error creating VRAM ctxdma: %d\n", ret);
			return ret;
		}
	}

	ret = nouveau_gpuobj_ref_add(dev, chan, vram_h, vram, NULL);
	if (ret) {
		NV_ERROR(dev, "Error referencing VRAM ctxdma: %d\n", ret);
		return ret;
	}

	/* TT memory ctxdma */
	if (dev_priv->card_type >= NV_50) {
		tt = vram;
	} else
	if (dev_priv->gart_info.type != NOUVEAU_GART_NONE) {
		ret = nouveau_gpuobj_gart_dma_new(chan, 0,
						  dev_priv->gart_info.aper_size,
						  NV_DMA_ACCESS_RW, &tt, NULL);
	} else {
		NV_ERROR(dev, "Invalid GART type %d\n", dev_priv->gart_info.type);
		ret = -EINVAL;
	}

	if (ret) {
		NV_ERROR(dev, "Error creating TT ctxdma: %d\n", ret);
		return ret;
	}

	ret = nouveau_gpuobj_ref_add(dev, chan, tt_h, tt, NULL);
	if (ret) {
		NV_ERROR(dev, "Error referencing TT ctxdma: %d\n", ret);
		return ret;
	}

	return 0;
}

void
nouveau_gpuobj_channel_takedown(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct drm_device *dev = chan->dev;
	struct list_head *entry, *tmp;
	struct nouveau_gpuobj_ref *ref;
	int i;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	if (!chan->ramht_refs.next)
		return;

	list_for_each_safe(entry, tmp, &chan->ramht_refs) {
		ref = list_entry(entry, struct nouveau_gpuobj_ref, list);

		nouveau_gpuobj_ref_del(dev, &ref);
	}

	nouveau_gpuobj_ref_del(dev, &chan->ramht);

	nouveau_gpuobj_del(dev, &chan->vm_pd);
	nouveau_gpuobj_ref_del(dev, &chan->vm_gart_pt);
	for (i = 0; i < dev_priv->vm_vram_pt_nr; i++)
		nouveau_gpuobj_ref_del(dev, &chan->vm_vram_pt[i]);

	if (chan->ramin_heap)
		nouveau_mem_takedown(&chan->ramin_heap);
	if (chan->ramin)
		nouveau_gpuobj_ref_del(dev, &chan->ramin);

}

int
nouveau_gpuobj_suspend(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj;
	int i;

	if (dev_priv->card_type < NV_50) {
		dev_priv->susres.ramin_copy = vmalloc(dev_priv->ramin_rsvd_vram);
		if (!dev_priv->susres.ramin_copy)
			return -ENOMEM;

		for (i = 0; i < dev_priv->ramin_rsvd_vram; i += 4)
			dev_priv->susres.ramin_copy[i/4] = nv_ri32(dev, i);
		return 0;
	}

	list_for_each_entry(gpuobj, &dev_priv->gpuobj_list, list) {
		if (!gpuobj->im_backing || (gpuobj->flags & NVOBJ_FLAG_FAKE))
			continue;

		gpuobj->im_backing_suspend = vmalloc(gpuobj->im_pramin->size);
		if (!gpuobj->im_backing_suspend) {
			nouveau_gpuobj_resume(dev);
			return -ENOMEM;
		}

		dev_priv->engine.instmem.prepare_access(dev, false);
		for (i = 0; i < gpuobj->im_pramin->size / 4; i++)
			gpuobj->im_backing_suspend[i] = nv_ro32(dev, gpuobj, i);
		dev_priv->engine.instmem.finish_access(dev);
	}

	return 0;
}

void
nouveau_gpuobj_suspend_cleanup(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj;

	if (dev_priv->card_type < NV_50) {
		vfree(dev_priv->susres.ramin_copy);
		dev_priv->susres.ramin_copy = NULL;
		return;
	}

	list_for_each_entry(gpuobj, &dev_priv->gpuobj_list, list) {
		if (!gpuobj->im_backing_suspend)
			continue;

		vfree(gpuobj->im_backing_suspend);
		gpuobj->im_backing_suspend = NULL;
	}
}

void
nouveau_gpuobj_resume(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj;
	int i;

	if (dev_priv->card_type < NV_50) {
		for (i = 0; i < dev_priv->ramin_rsvd_vram; i += 4)
			nv_wi32(dev, i, dev_priv->susres.ramin_copy[i/4]);
		nouveau_gpuobj_suspend_cleanup(dev);
		return;
	}

	list_for_each_entry(gpuobj, &dev_priv->gpuobj_list, list) {
		if (!gpuobj->im_backing_suspend)
			continue;

		dev_priv->engine.instmem.prepare_access(dev, true);
		for (i = 0; i < gpuobj->im_pramin->size / 4; i++)
			nv_wo32(dev, gpuobj, i, gpuobj->im_backing_suspend[i]);
		dev_priv->engine.instmem.finish_access(dev);
	}

	nouveau_gpuobj_suspend_cleanup(dev);
}

int nouveau_ioctl_grobj_alloc(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_grobj_alloc *init = data;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_pgraph_object_class *grc;
	struct nouveau_gpuobj *gr = NULL;
	struct nouveau_channel *chan;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(init->channel, file_priv, chan);

	if (init->handle == ~0)
		return -EINVAL;

	grc = pgraph->grclass;
	while (grc->id) {
		if (grc->id == init->class)
			break;
		grc++;
	}

	if (!grc->id) {
		NV_ERROR(dev, "Illegal object class: 0x%x\n", init->class);
		return -EPERM;
	}

	if (nouveau_gpuobj_ref_find(chan, init->handle, NULL) == 0)
		return -EEXIST;

	if (!grc->software)
		ret = nouveau_gpuobj_gr_new(chan, grc->id, &gr);
	else
		ret = nouveau_gpuobj_sw_new(chan, grc->id, &gr);

	if (ret) {
		NV_ERROR(dev, "Error creating object: %d (%d/0x%08x)\n",
			 ret, init->channel, init->handle);
		return ret;
	}

	ret = nouveau_gpuobj_ref_add(dev, chan, init->handle, gr, NULL);
	if (ret) {
		NV_ERROR(dev, "Error referencing object: %d (%d/0x%08x)\n",
			 ret, init->channel, init->handle);
		nouveau_gpuobj_del(dev, &gr);
		return ret;
	}

	return 0;
}

int nouveau_ioctl_gpuobj_free(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_nouveau_gpuobj_free *objfree = data;
	struct nouveau_gpuobj_ref *ref;
	struct nouveau_channel *chan;
	int ret;

	NOUVEAU_CHECK_INITIALISED_WITH_RETURN;
	NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(objfree->channel, file_priv, chan);

	ret = nouveau_gpuobj_ref_find(chan, objfree->handle, &ref);
	if (ret)
		return ret;
	nouveau_gpuobj_ref_del(dev, &ref);

	return 0;
}
