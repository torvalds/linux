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
#include "nouveau_fifo.h"
#include "nouveau_ramht.h"
#include "nouveau_software.h"
#include "nouveau_vm.h"

struct nouveau_gpuobj_method {
	struct list_head head;
	u32 mthd;
	int (*exec)(struct nouveau_channel *, u32 class, u32 mthd, u32 data);
};

struct nouveau_gpuobj_class {
	struct list_head head;
	struct list_head methods;
	u32 id;
	u32 engine;
};

int
nouveau_gpuobj_class_new(struct drm_device *dev, u32 class, u32 engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj_class *oc;

	oc = kzalloc(sizeof(*oc), GFP_KERNEL);
	if (!oc)
		return -ENOMEM;

	INIT_LIST_HEAD(&oc->methods);
	oc->id = class;
	oc->engine = engine;
	list_add(&oc->head, &dev_priv->classes);
	return 0;
}

int
nouveau_gpuobj_mthd_new(struct drm_device *dev, u32 class, u32 mthd,
			int (*exec)(struct nouveau_channel *, u32, u32, u32))
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj_method *om;
	struct nouveau_gpuobj_class *oc;

	list_for_each_entry(oc, &dev_priv->classes, head) {
		if (oc->id == class)
			goto found;
	}

	return -EINVAL;

found:
	om = kzalloc(sizeof(*om), GFP_KERNEL);
	if (!om)
		return -ENOMEM;

	om->mthd = mthd;
	om->exec = exec;
	list_add(&om->head, &oc->methods);
	return 0;
}

int
nouveau_gpuobj_mthd_call(struct nouveau_channel *chan,
			 u32 class, u32 mthd, u32 data)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nouveau_gpuobj_method *om;
	struct nouveau_gpuobj_class *oc;

	list_for_each_entry(oc, &dev_priv->classes, head) {
		if (oc->id != class)
			continue;

		list_for_each_entry(om, &oc->methods, head) {
			if (om->mthd == mthd)
				return om->exec(chan, class, mthd, data);
		}
	}

	return -ENOENT;
}

int
nouveau_gpuobj_mthd_call2(struct drm_device *dev, int chid,
			  u32 class, u32 mthd, u32 data)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_priv *pfifo = nv_engine(dev, NVOBJ_ENGINE_FIFO);
	struct nouveau_channel *chan = NULL;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	if (chid >= 0 && chid < pfifo->channels)
		chan = dev_priv->channels.ptr[chid];
	if (chan)
		ret = nouveau_gpuobj_mthd_call(chan, class, mthd, data);
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);
	return ret;
}

int
nouveau_gpuobj_new(struct drm_device *dev, struct nouveau_channel *chan,
		   uint32_t size, int align, uint32_t flags,
		   struct nouveau_gpuobj **gpuobj_ret)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *instmem = &dev_priv->engine.instmem;
	struct nouveau_gpuobj *gpuobj;
	struct drm_mm_node *ramin = NULL;
	int ret, i;

	NV_DEBUG(dev, "ch%d size=%u align=%d flags=0x%08x\n",
		 chan ? chan->id : -1, size, align, flags);

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

	if (!(flags & NVOBJ_FLAG_VM) && chan) {
		ramin = drm_mm_search_free(&chan->ramin_heap, size, align, 0);
		if (ramin)
			ramin = drm_mm_get_block(ramin, size, align);
		if (!ramin) {
			nouveau_gpuobj_ref(NULL, &gpuobj);
			return -ENOMEM;
		}

		gpuobj->pinst = chan->ramin->pinst;
		if (gpuobj->pinst != ~0)
			gpuobj->pinst += ramin->start;

		gpuobj->cinst = ramin->start;
		gpuobj->vinst = ramin->start + chan->ramin->vinst;
		gpuobj->node  = ramin;
	} else {
		ret = instmem->get(gpuobj, chan, size, align);
		if (ret) {
			nouveau_gpuobj_ref(NULL, &gpuobj);
			return ret;
		}

		ret = -ENOSYS;
		if (!(flags & NVOBJ_FLAG_DONT_MAP))
			ret = instmem->map(gpuobj);
		if (ret)
			gpuobj->pinst = ~0;

		gpuobj->cinst = NVOBJ_CINST_GLOBAL;
	}

	if (gpuobj->flags & NVOBJ_FLAG_ZERO_ALLOC) {
		for (i = 0; i < gpuobj->size; i += 4)
			nv_wo32(gpuobj, i, 0);
		instmem->flush(dev);
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
	INIT_LIST_HEAD(&dev_priv->classes);
	spin_lock_init(&dev_priv->ramin_lock);
	dev_priv->ramin_base = ~0;

	return 0;
}

void
nouveau_gpuobj_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj_method *om, *tm;
	struct nouveau_gpuobj_class *oc, *tc;

	NV_DEBUG(dev, "\n");

	list_for_each_entry_safe(oc, tc, &dev_priv->classes, head) {
		list_for_each_entry_safe(om, tm, &oc->methods, head) {
			list_del(&om->head);
			kfree(om);
		}
		list_del(&oc->head);
		kfree(oc);
	}

	BUG_ON(!list_empty(&dev_priv->gpuobj_list));
}


static void
nouveau_gpuobj_del(struct kref *ref)
{
	struct nouveau_gpuobj *gpuobj =
		container_of(ref, struct nouveau_gpuobj, refcount);
	struct drm_device *dev = gpuobj->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *instmem = &dev_priv->engine.instmem;
	int i;

	NV_DEBUG(dev, "gpuobj %p\n", gpuobj);

	if (gpuobj->node && (gpuobj->flags & NVOBJ_FLAG_ZERO_FREE)) {
		for (i = 0; i < gpuobj->size; i += 4)
			nv_wo32(gpuobj, i, 0);
		instmem->flush(dev);
	}

	if (gpuobj->dtor)
		gpuobj->dtor(dev, gpuobj);

	if (gpuobj->cinst == NVOBJ_CINST_GLOBAL) {
		if (gpuobj->node) {
			instmem->unmap(gpuobj);
			instmem->put(gpuobj);
		}
	} else {
		if (gpuobj->node) {
			spin_lock(&dev_priv->ramin_lock);
			drm_mm_put_block(gpuobj->node);
			spin_unlock(&dev_priv->ramin_lock);
		}
	}

	spin_lock(&dev_priv->ramin_lock);
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
	gpuobj->cinst = NVOBJ_CINST_GLOBAL;
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

void
nv50_gpuobj_dma_init(struct nouveau_gpuobj *obj, u32 offset, int class,
		     u64 base, u64 size, int target, int access,
		     u32 type, u32 comp)
{
	struct drm_nouveau_private *dev_priv = obj->dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	u32 flags0;

	flags0  = (comp << 29) | (type << 22) | class;
	flags0 |= 0x00100000;

	switch (access) {
	case NV_MEM_ACCESS_RO: flags0 |= 0x00040000; break;
	case NV_MEM_ACCESS_RW:
	case NV_MEM_ACCESS_WO: flags0 |= 0x00080000; break;
	default:
		break;
	}

	switch (target) {
	case NV_MEM_TARGET_VRAM:
		flags0 |= 0x00010000;
		break;
	case NV_MEM_TARGET_PCI:
		flags0 |= 0x00020000;
		break;
	case NV_MEM_TARGET_PCI_NOSNOOP:
		flags0 |= 0x00030000;
		break;
	case NV_MEM_TARGET_GART:
		base += dev_priv->gart_info.aper_base;
	default:
		flags0 &= ~0x00100000;
		break;
	}

	/* convert to base + limit */
	size = (base + size) - 1;

	nv_wo32(obj, offset + 0x00, flags0);
	nv_wo32(obj, offset + 0x04, lower_32_bits(size));
	nv_wo32(obj, offset + 0x08, lower_32_bits(base));
	nv_wo32(obj, offset + 0x0c, upper_32_bits(size) << 24 |
				    upper_32_bits(base));
	nv_wo32(obj, offset + 0x10, 0x00000000);
	nv_wo32(obj, offset + 0x14, 0x00000000);

	pinstmem->flush(obj->dev);
}

int
nv50_gpuobj_dma_new(struct nouveau_channel *chan, int class, u64 base, u64 size,
		    int target, int access, u32 type, u32 comp,
		    struct nouveau_gpuobj **pobj)
{
	struct drm_device *dev = chan->dev;
	int ret;

	ret = nouveau_gpuobj_new(dev, chan, 24, 16, NVOBJ_FLAG_ZERO_FREE, pobj);
	if (ret)
		return ret;

	nv50_gpuobj_dma_init(*pobj, 0, class, base, size, target,
			     access, type, comp);
	return 0;
}

int
nouveau_gpuobj_dma_new(struct nouveau_channel *chan, int class, u64 base,
		       u64 size, int access, int target,
		       struct nouveau_gpuobj **pobj)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct drm_device *dev = chan->dev;
	struct nouveau_gpuobj *obj;
	u32 flags0, flags2;
	int ret;

	if (dev_priv->card_type >= NV_50) {
		u32 comp = (target == NV_MEM_TARGET_VM) ? NV_MEM_COMP_VM : 0;
		u32 type = (target == NV_MEM_TARGET_VM) ? NV_MEM_TYPE_VM : 0;

		return nv50_gpuobj_dma_new(chan, class, base, size,
					   target, access, type, comp, pobj);
	}

	if (target == NV_MEM_TARGET_GART) {
		struct nouveau_gpuobj *gart = dev_priv->gart_info.sg_ctxdma;

		if (dev_priv->gart_info.type == NOUVEAU_GART_PDMA) {
			if (base == 0) {
				nouveau_gpuobj_ref(gart, pobj);
				return 0;
			}

			base   = nouveau_sgdma_get_physical(dev, base);
			target = NV_MEM_TARGET_PCI;
		} else {
			base += dev_priv->gart_info.aper_base;
			if (dev_priv->gart_info.type == NOUVEAU_GART_AGP)
				target = NV_MEM_TARGET_PCI_NOSNOOP;
			else
				target = NV_MEM_TARGET_PCI;
		}
	}

	flags0  = class;
	flags0 |= 0x00003000; /* PT present, PT linear */
	flags2  = 0;

	switch (target) {
	case NV_MEM_TARGET_PCI:
		flags0 |= 0x00020000;
		break;
	case NV_MEM_TARGET_PCI_NOSNOOP:
		flags0 |= 0x00030000;
		break;
	default:
		break;
	}

	switch (access) {
	case NV_MEM_ACCESS_RO:
		flags0 |= 0x00004000;
		break;
	case NV_MEM_ACCESS_WO:
		flags0 |= 0x00008000;
	default:
		flags2 |= 0x00000002;
		break;
	}

	flags0 |= (base & 0x00000fff) << 20;
	flags2 |= (base & 0xfffff000);

	ret = nouveau_gpuobj_new(dev, chan, 16, 16, NVOBJ_FLAG_ZERO_FREE, &obj);
	if (ret)
		return ret;

	nv_wo32(obj, 0x00, flags0);
	nv_wo32(obj, 0x04, size - 1);
	nv_wo32(obj, 0x08, flags2);
	nv_wo32(obj, 0x0c, flags2);

	obj->engine = NVOBJ_ENGINE_SW;
	obj->class  = class;
	*pobj = obj;
	return 0;
}

int
nouveau_gpuobj_gr_new(struct nouveau_channel *chan, u32 handle, int class)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct drm_device *dev = chan->dev;
	struct nouveau_gpuobj_class *oc;
	int ret;

	NV_DEBUG(dev, "ch%d class=0x%04x\n", chan->id, class);

	list_for_each_entry(oc, &dev_priv->classes, head) {
		struct nouveau_exec_engine *eng = dev_priv->eng[oc->engine];

		if (oc->id != class)
			continue;

		if (!chan->engctx[oc->engine]) {
			ret = eng->context_new(chan, oc->engine);
			if (ret)
				return ret;
		}

		return eng->object_new(chan, oc->engine, handle, class);
	}

	NV_ERROR(dev, "illegal object class: 0x%x\n", class);
	return -EINVAL;
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
	size = 0x2000;
	base = 0;

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

	ret = drm_mm_init(&chan->ramin_heap, base, size - base);
	if (ret) {
		NV_ERROR(dev, "Error creating PRAMIN heap: %d\n", ret);
		nouveau_gpuobj_ref(NULL, &chan->ramin);
		return ret;
	}

	return 0;
}

static int
nvc0_gpuobj_channel_init(struct nouveau_channel *chan, struct nouveau_vm *vm)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_gpuobj *pgd = NULL;
	struct nouveau_vm_pgd *vpgd;
	int ret;

	ret = nouveau_gpuobj_new(dev, NULL, 4096, 0x1000, 0, &chan->ramin);
	if (ret)
		return ret;

	/* create page directory for this vm if none currently exists,
	 * will be destroyed automagically when last reference to the
	 * vm is removed
	 */
	if (list_empty(&vm->pgd_list)) {
		ret = nouveau_gpuobj_new(dev, NULL, 65536, 0x1000, 0, &pgd);
		if (ret)
			return ret;
	}
	nouveau_vm_ref(vm, &chan->vm, pgd);
	nouveau_gpuobj_ref(NULL, &pgd);

	/* point channel at vm's page directory */
	vpgd = list_first_entry(&vm->pgd_list, struct nouveau_vm_pgd, head);
	nv_wo32(chan->ramin, 0x0200, lower_32_bits(vpgd->obj->vinst));
	nv_wo32(chan->ramin, 0x0204, upper_32_bits(vpgd->obj->vinst));
	nv_wo32(chan->ramin, 0x0208, 0xffffffff);
	nv_wo32(chan->ramin, 0x020c, 0x000000ff);

	return 0;
}

int
nouveau_gpuobj_channel_init(struct nouveau_channel *chan,
			    uint32_t vram_h, uint32_t tt_h)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fpriv *fpriv = nouveau_fpriv(chan->file_priv);
	struct nouveau_vm *vm = fpriv ? fpriv->vm : dev_priv->chan_vm;
	struct nouveau_gpuobj *vram = NULL, *tt = NULL;
	int ret;

	NV_DEBUG(dev, "ch%d vram=0x%08x tt=0x%08x\n", chan->id, vram_h, tt_h);
	if (dev_priv->card_type >= NV_C0)
		return nvc0_gpuobj_channel_init(chan, vm);

	/* Allocate a chunk of memory for per-channel object storage */
	ret = nouveau_gpuobj_channel_init_pramin(chan);
	if (ret) {
		NV_ERROR(dev, "init pramin\n");
		return ret;
	}

	/* NV50 VM
	 *  - Allocate per-channel page-directory
	 *  - Link with shared channel VM
	 */
	if (vm) {
		u32 pgd_offs = (dev_priv->chipset == 0x50) ? 0x1400 : 0x0200;
		u64 vm_vinst = chan->ramin->vinst + pgd_offs;
		u32 vm_pinst = chan->ramin->pinst;

		if (vm_pinst != ~0)
			vm_pinst += pgd_offs;

		ret = nouveau_gpuobj_new_fake(dev, vm_pinst, vm_vinst, 0x4000,
					      0, &chan->vm_pd);
		if (ret)
			return ret;

		nouveau_vm_ref(vm, &chan->vm, chan->vm_pd);
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
					     0, (1ULL << 40), NV_MEM_ACCESS_RW,
					     NV_MEM_TARGET_VM, &vram);
		if (ret) {
			NV_ERROR(dev, "Error creating VRAM ctxdma: %d\n", ret);
			return ret;
		}
	} else {
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     0, dev_priv->fb_available_size,
					     NV_MEM_ACCESS_RW,
					     NV_MEM_TARGET_VRAM, &vram);
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
					     0, (1ULL << 40), NV_MEM_ACCESS_RW,
					     NV_MEM_TARGET_VM, &tt);
	} else {
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     0, dev_priv->gart_info.aper_size,
					     NV_MEM_ACCESS_RW,
					     NV_MEM_TARGET_GART, &tt);
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
	NV_DEBUG(chan->dev, "ch%d\n", chan->id);

	nouveau_vm_ref(NULL, &chan->vm, chan->vm_pd);
	nouveau_gpuobj_ref(NULL, &chan->vm_pd);

	if (drm_mm_initialized(&chan->ramin_heap))
		drm_mm_takedown(&chan->ramin_heap);
	nouveau_gpuobj_ref(NULL, &chan->ramin);
}

int
nouveau_gpuobj_suspend(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj;
	int i;

	list_for_each_entry(gpuobj, &dev_priv->gpuobj_list, list) {
		if (gpuobj->cinst != NVOBJ_CINST_GLOBAL)
			continue;

		gpuobj->suspend = vmalloc(gpuobj->size);
		if (!gpuobj->suspend) {
			nouveau_gpuobj_resume(dev);
			return -ENOMEM;
		}

		for (i = 0; i < gpuobj->size; i += 4)
			gpuobj->suspend[i/4] = nv_ro32(gpuobj, i);
	}

	return 0;
}

void
nouveau_gpuobj_resume(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *gpuobj;
	int i;

	list_for_each_entry(gpuobj, &dev_priv->gpuobj_list, list) {
		if (!gpuobj->suspend)
			continue;

		for (i = 0; i < gpuobj->size; i += 4)
			nv_wo32(gpuobj, i, gpuobj->suspend[i/4]);

		vfree(gpuobj->suspend);
		gpuobj->suspend = NULL;
	}

	dev_priv->engine.instmem.flush(dev);
}

int nouveau_ioctl_grobj_alloc(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_nouveau_grobj_alloc *init = data;
	struct nouveau_channel *chan;
	int ret;

	if (init->handle == ~0)
		return -EINVAL;

	/* compatibility with userspace that assumes 506e for all chipsets */
	if (init->class == 0x506e) {
		init->class = nouveau_software_class(dev);
		if (init->class == 0x906e)
			return 0;
	} else
	if (init->class == 0x906e) {
		NV_ERROR(dev, "906e not supported yet\n");
		return -EINVAL;
	}

	chan = nouveau_channel_get(file_priv, init->channel);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	if (nouveau_ramht_find(chan, init->handle)) {
		ret = -EEXIST;
		goto out;
	}

	ret = nouveau_gpuobj_gr_new(chan, init->handle, init->class);
	if (ret) {
		NV_ERROR(dev, "Error creating object: %d (%d/0x%08x)\n",
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

	chan = nouveau_channel_get(file_priv, objfree->channel);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	/* Synchronize with the user channel */
	nouveau_channel_idle(chan);

	ret = nouveau_ramht_remove(chan, objfree->handle);
	nouveau_channel_put(&chan);
	return ret;
}

u32
nv_ro32(struct nouveau_gpuobj *gpuobj, u32 offset)
{
	struct drm_nouveau_private *dev_priv = gpuobj->dev->dev_private;
	struct drm_device *dev = gpuobj->dev;
	unsigned long flags;

	if (gpuobj->pinst == ~0 || !dev_priv->ramin_available) {
		u64  ptr = gpuobj->vinst + offset;
		u32 base = ptr >> 16;
		u32  val;

		spin_lock_irqsave(&dev_priv->vm_lock, flags);
		if (dev_priv->ramin_base != base) {
			dev_priv->ramin_base = base;
			nv_wr32(dev, 0x001700, dev_priv->ramin_base);
		}
		val = nv_rd32(dev, 0x700000 + (ptr & 0xffff));
		spin_unlock_irqrestore(&dev_priv->vm_lock, flags);
		return val;
	}

	return nv_ri32(dev, gpuobj->pinst + offset);
}

void
nv_wo32(struct nouveau_gpuobj *gpuobj, u32 offset, u32 val)
{
	struct drm_nouveau_private *dev_priv = gpuobj->dev->dev_private;
	struct drm_device *dev = gpuobj->dev;
	unsigned long flags;

	if (gpuobj->pinst == ~0 || !dev_priv->ramin_available) {
		u64  ptr = gpuobj->vinst + offset;
		u32 base = ptr >> 16;

		spin_lock_irqsave(&dev_priv->vm_lock, flags);
		if (dev_priv->ramin_base != base) {
			dev_priv->ramin_base = base;
			nv_wr32(dev, 0x001700, dev_priv->ramin_base);
		}
		nv_wr32(dev, 0x700000 + (ptr & 0xffff), val);
		spin_unlock_irqrestore(&dev_priv->vm_lock, flags);
		return;
	}

	nv_wi32(dev, gpuobj->pinst + offset, val);
}
