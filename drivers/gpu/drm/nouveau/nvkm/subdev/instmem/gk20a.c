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
 * implementation must be done directly on top of system memory, while providing
 * coherent read and write operations.
 *
 * Instmem can be allocated through two means:
 * 1) If an IOMMU mapping has been probed, the IOMMU API is used to make memory
 *    pages contiguous to the GPU. This is the preferred way.
 * 2) If no IOMMU mapping is probed, the DMA API is used to allocate physically
 *    contiguous memory.
 *
 * In both cases CPU read and writes are performed using PRAMIN (i.e. using the
 * GPU path) to ensure these operations are coherent for the GPU. This allows us
 * to use more "relaxed" allocation parameters when using the DMA API, since we
 * never need a kernel mapping.
 */
#define gk20a_instmem(p) container_of((p), struct gk20a_instmem, base)
#include "priv.h"

#include <core/memory.h>
#include <core/mm.h>
#include <core/tegra.h>
#include <subdev/fb.h>

#define gk20a_instobj(p) container_of((p), struct gk20a_instobj, memory)

struct gk20a_instobj {
	struct nvkm_memory memory;
	struct gk20a_instmem *imem;
	struct nvkm_mem mem;
};

/*
 * Used for objects allocated using the DMA API
 */
struct gk20a_instobj_dma {
	struct gk20a_instobj base;

	void *cpuaddr;
	dma_addr_t handle;
	struct nvkm_mm_node r;
};

/*
 * Used for objects flattened using the IOMMU API
 */
struct gk20a_instobj_iommu {
	struct gk20a_instobj base;

	/* array of base.mem->size pages */
	struct page *pages[];
};

struct gk20a_instmem {
	struct nvkm_instmem base;
	unsigned long lock_flags;
	spinlock_t lock;
	u64 addr;

	/* Only used if IOMMU if present */
	struct mutex *mm_mutex;
	struct nvkm_mm *mm;
	struct iommu_domain *domain;
	unsigned long iommu_pgshift;

	/* Only used by DMA API */
	struct dma_attrs attrs;
};

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

static void __iomem *
gk20a_instobj_acquire(struct nvkm_memory *memory)
{
	struct gk20a_instmem *imem = gk20a_instobj(memory)->imem;
	unsigned long flags;
	spin_lock_irqsave(&imem->lock, flags);
	imem->lock_flags = flags;
	return NULL;
}

static void
gk20a_instobj_release(struct nvkm_memory *memory)
{
	struct gk20a_instmem *imem = gk20a_instobj(memory)->imem;
	spin_unlock_irqrestore(&imem->lock, imem->lock_flags);
}

/*
 * Use PRAMIN to read/write data and avoid coherency issues.
 * PRAMIN uses the GPU path and ensures data will always be coherent.
 *
 * A dynamic mapping based solution would be desirable in the future, but
 * the issue remains of how to maintain coherency efficiently. On ARM it is
 * not easy (if possible at all?) to create uncached temporary mappings.
 */

static u32
gk20a_instobj_rd32(struct nvkm_memory *memory, u64 offset)
{
	struct gk20a_instobj *node = gk20a_instobj(memory);
	struct gk20a_instmem *imem = node->imem;
	struct nvkm_device *device = imem->base.subdev.device;
	u64 base = (node->mem.offset + offset) & 0xffffff00000ULL;
	u64 addr = (node->mem.offset + offset) & 0x000000fffffULL;
	u32 data;

	if (unlikely(imem->addr != base)) {
		nvkm_wr32(device, 0x001700, base >> 16);
		imem->addr = base;
	}
	data = nvkm_rd32(device, 0x700000 + addr);
	return data;
}

static void
gk20a_instobj_wr32(struct nvkm_memory *memory, u64 offset, u32 data)
{
	struct gk20a_instobj *node = gk20a_instobj(memory);
	struct gk20a_instmem *imem = node->imem;
	struct nvkm_device *device = imem->base.subdev.device;
	u64 base = (node->mem.offset + offset) & 0xffffff00000ULL;
	u64 addr = (node->mem.offset + offset) & 0x000000fffffULL;

	if (unlikely(imem->addr != base)) {
		nvkm_wr32(device, 0x001700, base >> 16);
		imem->addr = base;
	}
	nvkm_wr32(device, 0x700000 + addr, data);
}

static void
gk20a_instobj_map(struct nvkm_memory *memory, struct nvkm_vma *vma, u64 offset)
{
	struct gk20a_instobj *node = gk20a_instobj(memory);
	nvkm_vm_map_at(vma, offset, &node->mem);
}

static void
gk20a_instobj_dtor_dma(struct gk20a_instobj *_node)
{
	struct gk20a_instobj_dma *node = (void *)_node;
	struct gk20a_instmem *imem = _node->imem;
	struct device *dev = imem->base.subdev.device->dev;

	if (unlikely(!node->cpuaddr))
		return;

	dma_free_attrs(dev, _node->mem.size << PAGE_SHIFT, node->cpuaddr,
		       node->handle, &imem->attrs);
}

static void
gk20a_instobj_dtor_iommu(struct gk20a_instobj *_node)
{
	struct gk20a_instobj_iommu *node = (void *)_node;
	struct gk20a_instmem *imem = _node->imem;
	struct nvkm_mm_node *r;
	int i;

	if (unlikely(list_empty(&_node->mem.regions)))
		return;

	r = list_first_entry(&_node->mem.regions, struct nvkm_mm_node,
			     rl_entry);

	/* clear bit 34 to unmap pages */
	r->offset &= ~BIT(34 - imem->iommu_pgshift);

	/* Unmap pages from GPU address space and free them */
	for (i = 0; i < _node->mem.size; i++) {
		iommu_unmap(imem->domain,
			    (r->offset + i) << imem->iommu_pgshift, PAGE_SIZE);
		__free_page(node->pages[i]);
	}

	/* Release area from GPU address space */
	mutex_lock(imem->mm_mutex);
	nvkm_mm_free(imem->mm, &r);
	mutex_unlock(imem->mm_mutex);
}

static void *
gk20a_instobj_dtor(struct nvkm_memory *memory)
{
	struct gk20a_instobj *node = gk20a_instobj(memory);
	struct gk20a_instmem *imem = node->imem;

	if (imem->domain)
		gk20a_instobj_dtor_iommu(node);
	else
		gk20a_instobj_dtor_dma(node);

	return node;
}

static const struct nvkm_memory_func
gk20a_instobj_func = {
	.dtor = gk20a_instobj_dtor,
	.target = gk20a_instobj_target,
	.addr = gk20a_instobj_addr,
	.size = gk20a_instobj_size,
	.acquire = gk20a_instobj_acquire,
	.release = gk20a_instobj_release,
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

	node->cpuaddr = dma_alloc_attrs(dev, npages << PAGE_SHIFT,
					&node->handle, GFP_KERNEL,
					&imem->attrs);
	if (!node->cpuaddr) {
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
	struct nvkm_mm_node *r;
	int ret;
	int i;

	if (!(node = kzalloc(sizeof(*node) +
			     sizeof( node->pages[0]) * npages, GFP_KERNEL)))
		return -ENOMEM;
	*_node = &node->base;

	/* Allocate backing memory */
	for (i = 0; i < npages; i++) {
		struct page *p = alloc_page(GFP_KERNEL);

		if (p == NULL) {
			ret = -ENOMEM;
			goto free_pages;
		}
		node->pages[i] = p;
	}

	mutex_lock(imem->mm_mutex);
	/* Reserve area from GPU address space */
	ret = nvkm_mm_head(imem->mm, 0, 1, npages, npages,
			   align >> imem->iommu_pgshift, &r);
	mutex_unlock(imem->mm_mutex);
	if (ret) {
		nvkm_error(subdev, "virtual space is full!\n");
		goto free_pages;
	}

	/* Map into GPU address space */
	for (i = 0; i < npages; i++) {
		struct page *p = node->pages[i];
		u32 offset = (r->offset + i) << imem->iommu_pgshift;

		ret = iommu_map(imem->domain, offset, page_to_phys(p),
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

	/* Bit 34 tells that an address is to be resolved through the IOMMU */
	r->offset |= BIT(34 - imem->iommu_pgshift);

	node->base.mem.offset = ((u64)r->offset) << imem->iommu_pgshift;

	INIT_LIST_HEAD(&node->base.mem.regions);
	list_add_tail(&r->rl_entry, &node->base.mem.regions);

	return 0;

release_area:
	mutex_lock(imem->mm_mutex);
	nvkm_mm_free(imem->mm, &r);
	mutex_unlock(imem->mm_mutex);

free_pages:
	for (i = 0; i < npages && node->pages[i] != NULL; i++)
		__free_page(node->pages[i]);

	return ret;
}

static int
gk20a_instobj_new(struct nvkm_instmem *base, u32 size, u32 align, bool zero,
		  struct nvkm_memory **pmemory)
{
	struct gk20a_instmem *imem = gk20a_instmem(base);
	struct gk20a_instobj *node = NULL;
	struct nvkm_subdev *subdev = &imem->base.subdev;
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

	nvkm_memory_ctor(&gk20a_instobj_func, &node->memory);
	node->imem = imem;

	/* present memory for being mapped using small pages */
	node->mem.size = size >> 12;
	node->mem.memtype = 0;
	node->mem.page_shift = 12;

	nvkm_debug(subdev, "alloc size: 0x%x, align: 0x%x, gaddr: 0x%llx\n",
		   size, align, node->mem.offset);

	return 0;
}

static void
gk20a_instmem_fini(struct nvkm_instmem *base)
{
	gk20a_instmem(base)->addr = ~0ULL;
}

static const struct nvkm_instmem_func
gk20a_instmem = {
	.fini = gk20a_instmem_fini,
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

	if (tdev->iommu.domain) {
		imem->domain = tdev->iommu.domain;
		imem->mm = &tdev->iommu.mm;
		imem->iommu_pgshift = tdev->iommu.pgshift;
		imem->mm_mutex = &tdev->iommu.mutex;

		nvkm_info(&imem->base.subdev, "using IOMMU\n");
	} else {
		init_dma_attrs(&imem->attrs);
		/*
		 * We will access instmem through PRAMIN and thus do not need a
		 * consistent CPU pointer or kernel mapping
		 */
		dma_set_attr(DMA_ATTR_NON_CONSISTENT, &imem->attrs);
		dma_set_attr(DMA_ATTR_WEAK_ORDERING, &imem->attrs);
		dma_set_attr(DMA_ATTR_WRITE_COMBINE, &imem->attrs);
		dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &imem->attrs);

		nvkm_info(&imem->base.subdev, "using DMA API\n");
	}

	return 0;
}
