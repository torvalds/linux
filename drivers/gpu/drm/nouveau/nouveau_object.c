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
#include "nouveau_ramht.h"

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

int
nouveau_gpuobj_new(struct drm_device *dev, struct nouveau_channel *chan,
		   uint32_t size, int align, uint32_t flags,
		   struct nouveau_gpuobj **gpuobj_ret)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	struct nouveau_gpuobj *gpuobj;
	struct drm_mm_node *ramin = NULL;
	int ret;

	NV_DEBUG(dev, "ch%d size=%u align=%d flags=0x%08x\n",
		 chan ? chan->id : -1, size, align, flags);

	if (!dev_priv || !gpuobj_ret || *gpuobj_ret != NULL)
		return -EINVAL;

	gpuobj = kzalloc(sizeof(*gpuobj), GFP_KERNEL);
	if (!gpuobj)
		return -ENOMEM;
	NV_DEBUG(dev, "gpuobj %p\n", gpuobj);
	gpuobj->dev = dev;
	gpuobj->flags = flags;
	kref_init(&gpuobj->refcount);
	gpuobj->size = size;

	spin_lock(&dev_priv->ramin_lock);
	list_add_tail(&gpuobj->list, &dev_priv->gpuobj_list);
	spin_unlock(&dev_priv->ramin_lock);

	if (chan) {
		NV_DEBUG(dev, "channel heap\n");

		ramin = drm_mm_search_free(&chan->ramin_heap, size, align, 0);
		if (ramin)
			ramin = drm_mm_get_block(ramin, size, align);

		if (!ramin) {
			nouveau_gpuobj_ref(NULL, &gpuobj);
			return -ENOMEM;
		}
	} else {
		NV_DEBUG(dev, "global heap\n");

		/* allocate backing pages, sets vinst */
		ret = engine->instmem.populate(dev, gpuobj, &size, align);
		if (ret) {
			nouveau_gpuobj_ref(NULL, &gpuobj);
			return ret;
		}

		/* try and get aperture space */
		do {
			if (drm_mm_pre_get(&dev_priv->ramin_heap))
				return -ENOMEM;

			spin_lock(&dev_priv->ramin_lock);
			ramin = drm_mm_search_free(&dev_priv->ramin_heap, size,
						   align, 0);
			if (ramin == NULL) {
				spin_unlock(&dev_priv->ramin_lock);
				nouveau_gpuobj_ref(NULL, &gpuobj);
				return -ENOMEM;
			}

			ramin = drm_mm_get_block_atomic(ramin, size, align);
			spin_unlock(&dev_priv->ramin_lock);
		} while (ramin == NULL);

		/* on nv50 it's ok to fail, we have a fallback path */
		if (!ramin && dev_priv->card_type < NV_50) {
			nouveau_gpuobj_ref(NULL, &gpuobj);
			return -ENOMEM;
		}
	}

	/* if we got a chunk of the aperture, map pages into it */
	gpuobj->im_pramin = ramin;
	if (!chan && gpuobj->im_pramin && dev_priv->ramin_available) {
		ret = engine->instmem.bind(dev, gpuobj);
		if (ret) {
			nouveau_gpuobj_ref(NULL, &gpuobj);
			return ret;
		}
	}

	/* calculate the various different addresses for the object */
	if (chan) {
		gpuobj->pinst = chan->ramin->pinst;
		if (gpuobj->pinst != ~0)
			gpuobj->pinst += gpuobj->im_pramin->start;

		if (dev_priv->card_type < NV_50) {
			gpuobj->cinst = gpuobj->pinst;
		} else {
			gpuobj->cinst = gpuobj->im_pramin->start;
			gpuobj->vinst = gpuobj->im_pramin->start +
					chan->ramin->vinst;
		}
	} else {
		if (gpuobj->im_pramin)
			gpuobj->pinst = gpuobj->im_pramin->start;
		else
			gpuobj->pinst = ~0;
		gpuobj->cinst = 0xdeadbeef;
	}

	if (gpuobj->flags & NVOBJ_FLAG_ZERO_ALLOC) {
		int i;

		for (i = 0; i < gpuobj->size; i += 4)
			nv_wo32(gpuobj, i, 0);
		engine->instmem.flush(dev);
	}


	*gpuobj_ret = gpuobj;
	return 0;
}

int
nouveau_gpuobj_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	NV_DEBUG(dev, "\n");

	INIT_LIST_HEAD(&dev_priv->gpuobj_list);
	spin_lock_init(&dev_priv->ramin_lock);
	dev_priv->ramin_base = ~0;

	return 0;
}

void
nouveau_gpuobj_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	NV_DEBUG(dev, "\n");

	BUG_ON(!list_empty(&dev_priv->gpuobj_list));
}


static void
nouveau_gpuobj_del(struct kref *ref)
{
	struct nouveau_gpuobj *gpuobj =
		container_of(ref, struct nouveau_gpuobj, refcount);
	struct drm_device *dev = gpuobj->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	int i;

	NV_DEBUG(dev, "gpuobj %p\n", gpuobj);

	if (gpuobj->im_pramin && (gpuobj->flags & NVOBJ_FLAG_ZERO_FREE)) {
		for (i = 0; i < gpuobj->size; i += 4)
			nv_wo32(gpuobj, i, 0);
		engine->instmem.flush(dev);
	}

	if (gpuobj->dtor)
		gpuobj->dtor(dev, gpuobj);

	if (gpuobj->im_backing)
		engine->instmem.clear(dev, gpuobj);

	spin_lock(&dev_priv->ramin_lock);
	if (gpuobj->im_pramin)
		drm_mm_put_block(gpuobj->im_pramin);
	list_del(&gpuobj->list);
	spin_unlock(&dev_priv->ramin_lock);

	kfree(gpuobj);
}

void
nouveau_gpuobj_ref(struct nouveau_gpuobj *ref, struct nouveau_gpuobj **ptr)
{
	if (ref)
		kref_get(&ref->refcount);

	if (*ptr)
		kref_put(&(*ptr)->refcount, nouveau_gpuobj_del);

	*ptr = ref;
}

int
nouveau_gpuobj_new_fake(struct drm_device *dev, u32 pinst, u64 vinst,
			u32 size, u32 flags, struct nouveau_gpuobj **pgpuobj)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj = NULL;
	int i;

	NV_DEBUG(dev,
		 "pinst=0x%08x vinst=0x%010llx size=0x%08x flags=0x%08x\n",
		 pinst, vinst, size, flags);

	gpuobj = kzalloc(sizeof(*gpuobj), GFP_KERNEL);
	if (!gpuobj)
		return -ENOMEM;
	NV_DEBUG(dev, "gpuobj %p\n", gpuobj);
	gpuobj->dev = dev;
	gpuobj->flags = flags;
	kref_init(&gpuobj->refcount);
	gpuobj->size  = size;
	gpuobj->pinst = pinst;
	gpuobj->cinst = 0xdeadbeef;
	gpuobj->vinst = vinst;

	if (gpuobj->flags & NVOBJ_FLAG_ZERO_ALLOC) {
		for (i = 0; i < gpuobj->size; i += 4)
			nv_wo32(gpuobj, i, 0);
		dev_priv->engine.instmem.flush(dev);
	}

	spin_lock(&dev_priv->ramin_lock);
	list_add_tail(&gpuobj->list, &dev_priv->gpuobj_list);
	spin_unlock(&dev_priv->ramin_lock);
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

	if (dev_priv->card_type < NV_50) {
		uint32_t frame, adjust, pte_flags = 0;

		if (access != NV_DMA_ACCESS_RO)
			pte_flags |= (1<<1);
		adjust = offset &  0x00000fff;
		frame  = offset & ~0x00000fff;

		nv_wo32(*gpuobj,  0, ((1<<12) | (1<<13) | (adjust << 20) |
				      (access << 14) | (target << 16) |
				      class));
		nv_wo32(*gpuobj,  4, size - 1);
		nv_wo32(*gpuobj,  8, frame | pte_flags);
		nv_wo32(*gpuobj, 12, frame | pte_flags);
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

		nv_wo32(*gpuobj,  0, flags0 | class);
		nv_wo32(*gpuobj,  4, lower_32_bits(limit));
		nv_wo32(*gpuobj,  8, lower_32_bits(offset));
		nv_wo32(*gpuobj, 12, ((upper_32_bits(limit) & 0xff) << 24) |
				      (upper_32_bits(offset) & 0xff));
		nv_wo32(*gpuobj, 20, flags5);
	}

	instmem->flush(dev);

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
		nouveau_gpuobj_ref(dev_priv->gart_info.sg_ctxdma, gpuobj);
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

	if (dev_priv->card_type >= NV_50) {
		nv_wo32(*gpuobj,  0, class);
		nv_wo32(*gpuobj, 20, 0x00010000);
	} else {
		switch (class) {
		case NV_CLASS_NULL:
			nv_wo32(*gpuobj, 0, 0x00001030);
			nv_wo32(*gpuobj, 4, 0xFFFFFFFF);
			break;
		default:
			if (dev_priv->card_type >= NV_40) {
				nv_wo32(*gpuobj, 0, class);
#ifdef __BIG_ENDIAN
				nv_wo32(*gpuobj, 8, 0x01000000);
#endif
			} else {
#ifdef __BIG_ENDIAN
				nv_wo32(*gpuobj, 0, class | 0x00080000);
#else
				nv_wo32(*gpuobj, 0, class);
#endif
			}
		}
	}
	dev_priv->engine.instmem.flush(dev);

	(*gpuobj)->engine = NVOBJ_ENGINE_GR;
	(*gpuobj)->class  = class;
	return 0;
}

int
nouveau_gpuobj_sw_new(struct nouveau_channel *chan, int class,
		      struct nouveau_gpuobj **gpuobj_ret)
{
	struct drm_nouveau_private *dev_priv;
	struct nouveau_gpuobj *gpuobj;

	if (!chan || !gpuobj_ret || *gpuobj_ret != NULL)
		return -EINVAL;
	dev_priv = chan->dev->dev_private;

	gpuobj = kzalloc(sizeof(*gpuobj), GFP_KERNEL);
	if (!gpuobj)
		return -ENOMEM;
	gpuobj->dev = chan->dev;
	gpuobj->engine = NVOBJ_ENGINE_SW;
	gpuobj->class = class;
	kref_init(&gpuobj->refcount);
	gpuobj->cinst = 0x40;

	spin_lock(&dev_priv->ramin_lock);
	list_add_tail(&gpuobj->list, &dev_priv->gpuobj_list);
	spin_unlock(&dev_priv->ramin_lock);
	*gpuobj_ret = gpuobj;
	return 0;
}

static int
nouveau_gpuobj_channel_init_pramin(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t size;
	uint32_t base;
	int ret;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	/* Base amount for object storage (4KiB enough?) */
	size = 0x1000;
	base = 0;

	/* PGRAPH context */
	size += dev_priv->engine.graph.grctx_size;

	if (dev_priv->card_type == NV_50) {
		/* Various fixed table thingos */
		size += 0x1400; /* mostly unknown stuff */
		size += 0x4000; /* vm pd */
		base  = 0x6000;
		/* RAMHT, not sure about setting size yet, 32KiB to be safe */
		size += 0x8000;
		/* RAMFC */
		size += 0x1000;
	}

	ret = nouveau_gpuobj_new(dev, NULL, size, 0x1000, 0, &chan->ramin);
	if (ret) {
		NV_ERROR(dev, "Error allocating channel PRAMIN: %d\n", ret);
		return ret;
	}

	ret = drm_mm_init(&chan->ramin_heap, base, size);
	if (ret) {
		NV_ERROR(dev, "Error creating PRAMIN heap: %d\n", ret);
		nouveau_gpuobj_ref(NULL, &chan->ramin);
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

	NV_DEBUG(dev, "ch%d vram=0x%08x tt=0x%08x\n", chan->id, vram_h, tt_h);

	/* Allocate a chunk of memory for per-channel object storage */
	ret = nouveau_gpuobj_channel_init_pramin(chan);
	if (ret) {
		NV_ERROR(dev, "init pramin\n");
		return ret;
	}

	/* NV50 VM
	 *  - Allocate per-channel page-directory
	 *  - Map GART and VRAM into the channel's address space at the
	 *    locations determined during init.
	 */
	if (dev_priv->card_type >= NV_50) {
		u32 pgd_offs = (dev_priv->chipset == 0x50) ? 0x1400 : 0x0200;
		u64 vm_vinst = chan->ramin->vinst + pgd_offs;
		u32 vm_pinst = chan->ramin->pinst;
		u32 pde;

		if (vm_pinst != ~0)
			vm_pinst += pgd_offs;

		ret = nouveau_gpuobj_new_fake(dev, vm_pinst, vm_vinst, 0x4000,
					      0, &chan->vm_pd);
		if (ret)
			return ret;
		for (i = 0; i < 0x4000; i += 8) {
			nv_wo32(chan->vm_pd, i + 0, 0x00000000);
			nv_wo32(chan->vm_pd, i + 4, 0xdeadcafe);
		}

		nouveau_gpuobj_ref(dev_priv->gart_info.sg_ctxdma,
				   &chan->vm_gart_pt);
		pde = (dev_priv->vm_gart_base / (512*1024*1024)) * 8;
		nv_wo32(chan->vm_pd, pde + 0, chan->vm_gart_pt->vinst | 3);
		nv_wo32(chan->vm_pd, pde + 4, 0x00000000);

		pde = (dev_priv->vm_vram_base / (512*1024*1024)) * 8;
		for (i = 0; i < dev_priv->vm_vram_pt_nr; i++) {
			nouveau_gpuobj_ref(dev_priv->vm_vram_pt[i],
					   &chan->vm_vram_pt[i]);

			nv_wo32(chan->vm_pd, pde + 0,
				chan->vm_vram_pt[i]->vinst | 0x61);
			nv_wo32(chan->vm_pd, pde + 4, 0x00000000);
			pde += 8;
		}

		instmem->flush(dev);
	}

	/* RAMHT */
	if (dev_priv->card_type < NV_50) {
		nouveau_ramht_ref(dev_priv->ramht, &chan->ramht, NULL);
	} else {
		struct nouveau_gpuobj *ramht = NULL;

		ret = nouveau_gpuobj_new(dev, chan, 0x8000, 16,
					 NVOBJ_FLAG_ZERO_ALLOC, &ramht);
		if (ret)
			return ret;

		ret = nouveau_ramht_new(dev, ramht, &chan->ramht);
		nouveau_gpuobj_ref(NULL, &ramht);
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

	ret = nouveau_ramht_insert(chan, vram_h, vram);
	nouveau_gpuobj_ref(NULL, &vram);
	if (ret) {
		NV_ERROR(dev, "Error adding VRAM ctxdma to RAMHT: %d\n", ret);
		return ret;
	}

	/* TT memory ctxdma */
	if (dev_priv->card_type >= NV_50) {
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     0, dev_priv->vm_end,
					     NV_DMA_ACCESS_RW,
					     NV_DMA_TARGET_AGP, &tt);
		if (ret) {
			NV_ERROR(dev, "Error creating VRAM ctxdma: %d\n", ret);
			return ret;
		}
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

	ret = nouveau_ramht_insert(chan, tt_h, tt);
	nouveau_gpuobj_ref(NULL, &tt);
	if (ret) {
		NV_ERROR(dev, "Error adding TT ctxdma to RAMHT: %d\n", ret);
		return ret;
	}

	return 0;
}

void
nouveau_gpuobj_channel_takedown(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct drm_device *dev = chan->dev;
	int i;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	if (!chan->ramht)
		return;

	nouveau_ramht_ref(NULL, &chan->ramht, chan);

	nouveau_gpuobj_ref(NULL, &chan->vm_pd);
	nouveau_gpuobj_ref(NULL, &chan->vm_gart_pt);
	for (i = 0; i < dev_priv->vm_vram_pt_nr; i++)
		nouveau_gpuobj_ref(NULL, &chan->vm_vram_pt[i]);

	if (chan->ramin_heap.free_stack.next)
		drm_mm_takedown(&chan->ramin_heap);
	nouveau_gpuobj_ref(NULL, &chan->ramin);
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
		if (!gpuobj->im_backing)
			continue;

		gpuobj->im_backing_suspend = vmalloc(gpuobj->size);
		if (!gpuobj->im_backing_suspend) {
			nouveau_gpuobj_resume(dev);
			return -ENOMEM;
		}

		for (i = 0; i < gpuobj->size; i += 4)
			gpuobj->im_backing_suspend[i/4] = nv_ro32(gpuobj, i);
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

		for (i = 0; i < gpuobj->size; i += 4)
			nv_wo32(gpuobj, i, gpuobj->im_backing_suspend[i/4]);
		dev_priv->engine.instmem.flush(dev);
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

	chan = nouveau_channel_get(dev, file_priv, init->channel);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	if (nouveau_ramht_find(chan, init->handle)) {
		ret = -EEXIST;
		goto out;
	}

	if (!grc->software)
		ret = nouveau_gpuobj_gr_new(chan, grc->id, &gr);
	else
		ret = nouveau_gpuobj_sw_new(chan, grc->id, &gr);
	if (ret) {
		NV_ERROR(dev, "Error creating object: %d (%d/0x%08x)\n",
			 ret, init->channel, init->handle);
		goto out;
	}

	ret = nouveau_ramht_insert(chan, init->handle, gr);
	nouveau_gpuobj_ref(NULL, &gr);
	if (ret) {
		NV_ERROR(dev, "Error referencing object: %d (%d/0x%08x)\n",
			 ret, init->channel, init->handle);
	}

out:
	nouveau_channel_put(&chan);
	return ret;
}

int nouveau_ioctl_gpuobj_free(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_nouveau_gpuobj_free *objfree = data;
	struct nouveau_channel *chan;
	int ret;

	chan = nouveau_channel_get(dev, file_priv, objfree->channel);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	ret = nouveau_ramht_remove(chan, objfree->handle);
	nouveau_channel_put(&chan);
	return ret;
}

u32
nv_ro32(struct nouveau_gpuobj *gpuobj, u32 offset)
{
	struct drm_nouveau_private *dev_priv = gpuobj->dev->dev_private;
	struct drm_device *dev = gpuobj->dev;

	if (gpuobj->pinst == ~0 || !dev_priv->ramin_available) {
		u64  ptr = gpuobj->vinst + offset;
		u32 base = ptr >> 16;
		u32  val;

		spin_lock(&dev_priv->ramin_lock);
		if (dev_priv->ramin_base != base) {
			dev_priv->ramin_base = base;
			nv_wr32(dev, 0x001700, dev_priv->ramin_base);
		}
		val = nv_rd32(dev, 0x700000 + (ptr & 0xffff));
		spin_unlock(&dev_priv->ramin_lock);
		return val;
	}

	return nv_ri32(dev, gpuobj->pinst + offset);
}

void
nv_wo32(struct nouveau_gpuobj *gpuobj, u32 offset, u32 val)
{
	struct drm_nouveau_private *dev_priv = gpuobj->dev->dev_private;
	struct drm_device *dev = gpuobj->dev;

	if (gpuobj->pinst == ~0 || !dev_priv->ramin_available) {
		u64  ptr = gpuobj->vinst + offset;
		u32 base = ptr >> 16;

		spin_lock(&dev_priv->ramin_lock);
		if (dev_priv->ramin_base != base) {
			dev_priv->ramin_base = base;
			nv_wr32(dev, 0x001700, dev_priv->ramin_base);
		}
		nv_wr32(dev, 0x700000 + (ptr & 0xffff), val);
		spin_unlock(&dev_priv->ramin_lock);
		return;
	}

	nv_wi32(dev, gpuobj->pinst + offset, val);
}
