/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * GK20A does not have dedicated video memory, and to accurately represent this
 * fact Nouveau will not create a RAM device for it. Therefore its instmem
 * implementation must be done directly on top of system memory, while
 * preserving coherency for read and write operations.
 *
 * Instmem can be allocated through two means:
 * 1) If an IOMMU unit has been probed, the IOMMU API is used to make memory
 *    pages contiguous to the GPU. This is the preferred way.
 * 2) If no IOMMU unit is probed, the DMA API is used to allocate physically
 *    contiguous memory.
 *
 * In both cases CPU read and writes are performed by creating a write-combined
 * mapping. The GPU L2 cache must thus be flushed/invalidated when required. To
 * be conservative we do this every time we acquire or release an instobj, but
 * ideally L2 management should be handled at a higher level.
 *
 * To improve performance, CPU mappings are not removed upon instobj release.
 * Instead they are placed into a LRU list to be recycled when the mapped space
 * goes beyond a certain threshold. At the moment this limit is 1MB.
 */
#include "priv.h"

#include <core/memory.h>
#include <core/mm.h>
#include <core/tegra.h>
#include <subdev/fb.h>
#include <subdev/ltc.h>

struct gk20a_instobj {
	struct nvkm_memory memory;
	struct nvkm_mem mem;
	struct gk20a_instmem *imem;

	/* CPU mapping */
	u32 *vaddr;
};
#define gk20a_instobj(p) container_of((p), struct gk20a_instobj, memory)

/*
 * Used for objects allocated using the DMA API
 */
struct gk20a_instobj_dma {
	struct gk20a_instobj base;

	dma_addr_t handle;
	struct nvkm_mm_node r;
};
#define gk20a_instobj_dma(p) \
	container_of(gk20a_instobj(p), struct gk20a_instobj_dma, base)

/*
 * Used for objects flattened using the IOMMU API
 */
struct gk20a_instobj_iommu {
	struct gk20a_instobj base;

	/* to link into gk20a_instmem::vaddr_lru */
	struct list_head vaddr_node;
	/* how many clients are using vaddr? */
	u32 use_cpt;

	/* will point to the higher half of pages */
	dma_addr_t *dma_addrs;
	/* array of base.mem->size pages (+ dma_addr_ts) */
	struct page *pages[];
};
#define gk20a_instobj_iommu(p) \
	container_of(gk20a_instobj(p), struct gk20a_instobj_iommu, base)

struct gk20a_instmem {
	struct nvkm_instmem base;

	/* protects vaddr_* and gk20a_instobj::vaddr* */
	spinlock_t lock;

	/* CPU mappings LRU */
	unsigned int vaddr_use;
	unsigned int vaddr_max;
	struct list_head vaddr_lru;

	/* Only used if IOMMU if present */
	struct mutex *mm_mutex;
	struct nvkm_mm *mm;
	struct iommu_domain *domain;
	unsigned long iommu_pgshift;
	u16 iommu_bit;

	/* Only used by DMA API */
	struct dma_attrs attrs;
};
#define gk20a_instmem(p) container_of((p), struct gk20a_instmem, base)

static enum nvkm_memory_target
gk20a_instobj_target(struct nvkm_memory *memory)
{
	return NVKM_MEM_TARGET_HOST;
}

static u64
gk20a_instobj_addr(struct nvkm_memory *memory)
{
	return gk20a_instobj(memory)->mem.offset;
}

static u64
gk20a_instobj_size(struct nvkm_memory *memory)
{
	return (u64)gk20a_instobj(memory)->mem.size << 12;
}

/*
 * Recycle the vaddr of obj. Must be called with gk20a_instmem::lock held.
 */
static void
gk20a_instobj_iommu_recycle_vaddr(struct gk20a_instobj_iommu *obj)
{
	struct gk20a_instmem *imem = obj->base.imem;
	/* there should not be any user left... */
	WARN_ON(obj->use_cpt);
	list_del(&obj->vaddr_node);
	vunmap(obj->base.vaddr);
	obj->base.vaddr = NULL;
	imem->vaddr_use -= nvkm_memory_size(&obj->base.memory);
	nvkm_debug(&imem->base.subdev, "vaddr used: %x/%x\n", imem->vaddr_use,
		   imem->vaddr_max);
}

/*
 * Must be called while holding gk20a_instmem::lock
 */
static void
gk20a_instmem_vaddr_gc(struct gk20a_instmem *imem, const u64 size)
{
	while (imem->vaddr_use + size > imem->vaddr_max) {
		/* no candidate that can be unmapped, abort... */
		if (list_empty(&imem->vaddr_lru))
			break;

		gk20a_instobj_iommu_recycle_vaddr(
				list_first_entry(&imem->vaddr_lru,
				struct gk20a_instobj_iommu, vaddr_node));
	}
}

static void __iomem *
gk20a_instobj_acquire_dma(struct nvkm_memory *memory)
{
	struct gk20a_instobj *node = gk20a_instobj(memory);
	struct gk20a_instmem *imem = node->imem;
	struct nvkm_ltc *ltc = imem->base.subdev.device->ltc;

	nvkm_ltc_flush(ltc);

	return node->vaddr;
}

static void __iomem *
gk20a_instobj_acquire_iommu(struct nvkm_memory *memory)
{
	struct gk20a_instobj_iommu *node = gk20a_instobj_iommu(memory);
	struct gk20a_instmem *imem = node->base.imem;
	struct nvkm_ltc *ltc = imem->base.subdev.device->ltc;
	const u64 size = nvkm_memory_size(memory);
	unsigned long flags;

	nvkm_ltc_flush(ltc);

	spin_lock_irqsave(&imem->lock, flags);

	if (node->base.vaddr) {
		if (!node->use_cpt) {
			/* remove from LRU list since mapping in use again */
			list_del(&node->vaddr_node);
		}
		goto out;
	}

	/* try to free some address space if we reached the limit */
	gk20a_instmem_vaddr_gc(imem, size);

	/* map the pages */
	node->base.vaddr = vmap(node->pages, size >> PAGE_SHIFT, VM_MAP,
				pgprot_writecombine(PAGE_KERNEL));
	if (!node->base.vaddr) {
		nvkm_error(&imem->base.subdev, "cannot map instobj - "
			   "this is not going to end well...\n");
		goto out;
	}

	imem->vaddr_use += size;
	nvkm_debug(&imem->base.subdev, "vaddr used: %x/%x\n",
		   imem->vaddr_use, imem->vaddr_max);

out:
	node->use_cpt++;
	spin_unlock_irqrestore(&imem->lock, flags);

	return node->base.vaddr;
}

static void
gk20a_instobj_release_dma(struct nvkm_memory *memory)
{
	struct gk20a_instobj *node = gk20a_instobj(memory);
	struct gk20a_instmem *imem = node->imem;
	struct nvkm_ltc *ltc = imem->base.subdev.device->ltc;

	/* in case we got a write-combined mapping */
	wmb();
	nvkm_ltc_invalidate(ltc);
}

static void
gk20a_instobj_release_iommu(struct nvkm_memory *memory)
{
	struct gk20a_instobj_iommu *node = gk20a_instobj_iommu(memory);
	struct gk20a_instmem *imem = node->base.imem;
	struct nvkm_ltc *ltc = imem->base.subdev.device->ltc;
	unsigned long flags;

	spin_lock_irqsave(&imem->lock, flags);

	/* we should at least have one user to release... */
	if (WARN_ON(node->use_cpt == 0))
		goto out;

	/* add unused objs to the LRU list to recycle their mapping */
	if (--node->use_cpt == 0)
		list_add_tail(&node->vaddr_node, &imem->vaddr_lru);

out:
	spin_unlock_irqrestore(&imem->lock, flags);

	wmb();
	nvkm_ltc_invalidate(ltc);
}

static u32
gk20a_instobj_rd32(struct nvkm_memory *memory, u64 offset)
{
	struct gk20a_instobj *node = gk20a_instobj(memory);

	return node->vaddr[offset / 4];
}

static void
gk20a_instobj_wr32(struct nvkm_memory *memory, u64 offset, u32 data)
{
	struct gk20a_instobj *node = gk20a_instobj(memory);

	node->vaddr[offset / 4] = data;
}

static void
gk20a_instobj_map(struct nvkm_memory *memory, struct nvkm_vma *vma, u64 offset)
{
	struct gk20a_instobj *node = gk20a_instobj(memory);

	nvkm_vm_map_at(vma, offset, &node->mem);
}

static void *
gk20a_instobj_dtor_dma(struct nvkm_memory *memory)
{
	struct gk20a_instobj_dma *node = gk20a_instobj_dma(memory);
	struct gk20a_instmem *imem = node->base.imem;
	struct device *dev = imem->base.subdev.device->dev;

	if (unlikely(!node->base.vaddr))
		goto out;

	dma_free_attrs(dev, node->base.mem.size << PAGE_SHIFT, node->base.vaddr,
		       node->handle, &imem->attrs);

out:
	return node;
}

static void *
gk20a_instobj_dtor_iommu(struct nvkm_memory *memory)
{
	struct gk20a_instobj_iommu *node = gk20a_instobj_iommu(memory);
	struct gk20a_instmem *imem = node->base.imem;
	struct device *dev = imem->base.subdev.device->dev;
	struct nvkm_mm_node *r;
	unsigned long flags;
	int i;

	if (unlikely(list_empty(&node->base.mem.regions)))
		goto out;

	spin_lock_irqsave(&imem->lock, flags);

	/* vaddr has already been recycled */
	if (node->base.vaddr)
		gk20a_instobj_iommu_recycle_vaddr(node);

	spin_unlock_irqrestore(&imem->lock, flags);

	r = list_first_entry(&node->base.mem.regions, struct nvkm_mm_node,
			     rl_entry);

	/* clear IOMMU bit to unmap pages */
	r->offset &= ~BIT(imem->iommu_bit - imem->iommu_pgshift);

	/* Unmap pages from GPU address space and free them */
	for (i = 0; i < node->base.mem.size; i++) {
		iommu_unmap(imem->domain,
			    (r->offset + i) << imem->iommu_pgshift, PAGE_SIZE);
		dma_unmap_page(dev, node->dma_addrs[i], PAGE_SIZE,
			       DMA_BIDIRECTIONAL);
		__free_page(node->pages[i]);
	}

	/* Release area from GPU address space */
	mutex_lock(imem->mm_mutex);
	nvkm_mm_free(imem->mm, &r);
	mutex_unlock(imem->mm_mutex);

out:
	return node;
}

static const struct nvkm_memory_func
gk20a_instobj_func_dma = {
	.dtor = gk20a_instobj_dtor_dma,
	.target = gk20a_instobj_target,
	.addr = gk20a_instobj_addr,
	.size = gk20a_instobj_size,
	.acquire = gk20a_instobj_acquire_dma,
	.release = gk20a_instobj_release_dma,
	.rd32 = gk20a_instobj_rd32,
	.wr32 = gk20a_instobj_wr32,
	.map = gk20a_instobj_map,
};

static const struct nvkm_memory_func
gk20a_instobj_func_iommu = {
	.dtor = gk20a_instobj_dtor_iommu,
	.target = gk20a_instobj_target,
	.addr = gk20a_instobj_addr,
	.size = gk20a_instobj_size,
	.acquire = gk20a_instobj_acquire_iommu,
	.release = gk20a_instobj_release_iommu,
	.rd32 = gk20a_instobj_rd32,
	.wr32 = gk20a_instobj_wr32,
	.map = gk20a_instobj_map,
};

static int
gk20a_instobj_ctor_dma(struct gk20a_instmem *imem, u32 npages, u32 align,
		       struct gk20a_instobj **_node)
{
	struct gk20a_instobj_dma *node;
	struct nvkm_subdev *subdev = &imem->base.subdev;
	struct device *dev = subdev->device->dev;

	if (!(node = kzalloc(sizeof(*node), GFP_KERNEL)))
		return -ENOMEM;
	*_node = &node->base;

	nvkm_memory_ctor(&gk20a_instobj_func_dma, &node->base.memory);

	node->base.vaddr = dma_alloc_attrs(dev, npages << PAGE_SHIFT,
					   &node->handle, GFP_KERNEL,
					   &imem->attrs);
	if (!node->base.vaddr) {
		nvkm_error(subdev, "cannot allocate DMA memory\n");
		return -ENOMEM;
	}

	/* alignment check */
	if (unlikely(node->handle & (align - 1)))
		nvkm_warn(subdev,
			  "memory not aligned as requested: %pad (0x%x)\n",
			  &node->handle, align);

	/* present memory for being mapped using small pages */
	node->r.type = 12;
	node->r.offset = node->handle >> 12;
	node->r.length = (npages << PAGE_SHIFT) >> 12;

	node->base.mem.offset = node->handle;

	INIT_LIST_HEAD(&node->base.mem.regions);
	list_add_tail(&node->r.rl_entry, &node->base.mem.regions);

	return 0;
}

static int
gk20a_instobj_ctor_iommu(struct gk20a_instmem *imem, u32 npages, u32 align,
			 struct gk20a_instobj **_node)
{
	struct gk20a_instobj_iommu *node;
	struct nvkm_subdev *subdev = &imem->base.subdev;
	struct device *dev = subdev->device->dev;
	struct nvkm_mm_node *r;
	int ret;
	int i;

	/*
	 * despite their variable size, instmem allocations are small enough
	 * (< 1 page) to be handled by kzalloc
	 */
	if (!(node = kzalloc(sizeof(*node) + ((sizeof(node->pages[0]) +
			     sizeof(*node->dma_addrs)) * npages), GFP_KERNEL)))
		return -ENOMEM;
	*_node = &node->base;
	node->dma_addrs = (void *)(node->pages + npages);

	nvkm_memory_ctor(&gk20a_instobj_func_iommu, &node->base.memory);

	/* Allocate backing memory */
	for (i = 0; i < npages; i++) {
		struct page *p = alloc_page(GFP_KERNEL);
		dma_addr_t dma_adr;

		if (p == NULL) {
			ret = -ENOMEM;
			goto free_pages;
		}
		node->pages[i] = p;
		dma_adr = dma_map_page(dev, p, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, dma_adr)) {
			nvkm_error(subdev, "DMA mapping error!\n");
			ret = -ENOMEM;
			goto free_pages;
		}
		node->dma_addrs[i] = dma_adr;
	}

	mutex_lock(imem->mm_mutex);
	/* Reserve area from GPU address space */
	ret = nvkm_mm_head(imem->mm, 0, 1, npages, npages,
			   align >> imem->iommu_pgshift, &r);
	mutex_unlock(imem->mm_mutex);
	if (ret) {
		nvkm_error(subdev, "IOMMU space is full!\n");
		goto free_pages;
	}

	/* Map into GPU address space */
	for (i = 0; i < npages; i++) {
		u32 offset = (r->offset + i) << imem->iommu_pgshift;

		ret = iommu_map(imem->domain, offset, node->dma_addrs[i],
				PAGE_SIZE, IOMMU_READ | IOMMU_WRITE);
		if (ret < 0) {
			nvkm_error(subdev, "IOMMU mapping failure: %d\n", ret);

			while (i-- > 0) {
				offset -= PAGE_SIZE;
				iommu_unmap(imem->domain, offset, PAGE_SIZE);
			}
			goto release_area;
		}
	}

	/* IOMMU bit tells that an address is to be resolved through the IOMMU */
	r->offset |= BIT(imem->iommu_bit - imem->iommu_pgshift);

	node->base.mem.offset = ((u64)r->offset) << imem->iommu_pgshift;

	INIT_LIST_HEAD(&node->base.mem.regions);
	list_add_tail(&r->rl_entry, &node->base.mem.regions);

	return 0;

release_area:
	mutex_lock(imem->mm_mutex);
	nvkm_mm_free(imem->mm, &r);
	mutex_unlock(imem->mm_mutex);

free_pages:
	for (i = 0; i < npages && node->pages[i] != NULL; i++) {
		dma_addr_t dma_addr = node->dma_addrs[i];
		if (dma_addr)
			dma_unmap_page(dev, dma_addr, PAGE_SIZE,
				       DMA_BIDIRECTIONAL);
		__free_page(node->pages[i]);
	}

	return ret;
}

static int
gk20a_instobj_new(struct nvkm_instmem *base, u32 size, u32 align, bool zero,
		  struct nvkm_memory **pmemory)
{
	struct gk20a_instmem *imem = gk20a_instmem(base);
	struct nvkm_subdev *subdev = &imem->base.subdev;
	struct gk20a_instobj *node = NULL;
	int ret;

	nvkm_debug(subdev, "%s (%s): size: %x align: %x\n", __func__,
		   imem->domain ? "IOMMU" : "DMA", size, align);

	/* Round size and align to page bounds */
	size = max(roundup(size, PAGE_SIZE), PAGE_SIZE);
	align = max(roundup(align, PAGE_SIZE), PAGE_SIZE);

	if (imem->domain)
		ret = gk20a_instobj_ctor_iommu(imem, size >> PAGE_SHIFT,
					       align, &node);
	else
		ret = gk20a_instobj_ctor_dma(imem, size >> PAGE_SHIFT,
					     align, &node);
	*pmemory = node ? &node->memory : NULL;
	if (ret)
		return ret;

	node->imem = imem;

	/* present memory for being mapped using small pages */
	node->mem.size = size >> 12;
	node->mem.memtype = 0;
	node->mem.page_shift = 12;

	nvkm_debug(subdev, "alloc size: 0x%x, align: 0x%x, gaddr: 0x%llx\n",
		   size, align, node->mem.offset);

	return 0;
}

static void *
gk20a_instmem_dtor(struct nvkm_instmem *base)
{
	struct gk20a_instmem *imem = gk20a_instmem(base);

	/* perform some sanity checks... */
	if (!list_empty(&imem->vaddr_lru))
		nvkm_warn(&base->subdev, "instobj LRU not empty!\n");

	if (imem->vaddr_use != 0)
		nvkm_warn(&base->subdev, "instobj vmap area not empty! "
			  "0x%x bytes still mapped\n", imem->vaddr_use);

	return imem;
}

static const struct nvkm_instmem_func
gk20a_instmem = {
	.dtor = gk20a_instmem_dtor,
	.memory_new = gk20a_instobj_new,
	.persistent = true,
	.zero = false,
};

int
gk20a_instmem_new(struct nvkm_device *device, int index,
		  struct nvkm_instmem **pimem)
{
	struct nvkm_device_tegra *tdev = device->func->tegra(device);
	struct gk20a_instmem *imem;

	if (!(imem = kzalloc(sizeof(*imem), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_instmem_ctor(&gk20a_instmem, device, index, &imem->base);
	spin_lock_init(&imem->lock);
	*pimem = &imem->base;

	/* do not allow more than 1MB of CPU-mapped instmem */
	imem->vaddr_use = 0;
	imem->vaddr_max = 0x100000;
	INIT_LIST_HEAD(&imem->vaddr_lru);

	if (tdev->iommu.domain) {
		imem->mm_mutex = &tdev->iommu.mutex;
		imem->mm = &tdev->iommu.mm;
		imem->domain = tdev->iommu.domain;
		imem->iommu_pgshift = tdev->iommu.pgshift;
		imem->iommu_bit = tdev->func->iommu_bit;

		nvkm_info(&imem->base.subdev, "using IOMMU\n");
	} else {
		init_dma_attrs(&imem->attrs);
		dma_set_attr(DMA_ATTR_NON_CONSISTENT, &imem->attrs);
		dma_set_attr(DMA_ATTR_WEAK_ORDERING, &imem->attrs);
		dma_set_attr(DMA_ATTR_WRITE_COMBINE, &imem->attrs);

		nvkm_info(&imem->base.subdev, "using DMA API\n");
	}

	return 0;
}
