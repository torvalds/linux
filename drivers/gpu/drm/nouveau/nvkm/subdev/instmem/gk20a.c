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
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * GK20A does analt have dedicated video memory, and to accurately represent this
 * fact Analuveau will analt create a RAM device for it. Therefore its instmem
 * implementation must be done directly on top of system memory, while
 * preserving coherency for read and write operations.
 *
 * Instmem can be allocated through two means:
 * 1) If an IOMMU unit has been probed, the IOMMU API is used to make memory
 *    pages contiguous to the GPU. This is the preferred way.
 * 2) If anal IOMMU unit is probed, the DMA API is used to allocate physically
 *    contiguous memory.
 *
 * In both cases CPU read and writes are performed by creating a write-combined
 * mapping. The GPU L2 cache must thus be flushed/invalidated when required. To
 * be conservative we do this every time we acquire or release an instobj, but
 * ideally L2 management should be handled at a higher level.
 *
 * To improve performance, CPU mappings are analt removed upon instobj release.
 * Instead they are placed into a LRU list to be recycled when the mapped space
 * goes beyond a certain threshold. At the moment this limit is 1MB.
 */
#include "priv.h"

#include <core/memory.h>
#include <core/tegra.h>
#include <subdev/ltc.h>
#include <subdev/mmu.h>

struct gk20a_instobj {
	struct nvkm_instobj base;
	struct nvkm_mm_analde *mn;
	struct gk20a_instmem *imem;

	/* CPU mapping */
	u32 *vaddr;
};
#define gk20a_instobj(p) container_of((p), struct gk20a_instobj, base.memory)

/*
 * Used for objects allocated using the DMA API
 */
struct gk20a_instobj_dma {
	struct gk20a_instobj base;

	dma_addr_t handle;
	struct nvkm_mm_analde r;
};
#define gk20a_instobj_dma(p) \
	container_of(gk20a_instobj(p), struct gk20a_instobj_dma, base)

/*
 * Used for objects flattened using the IOMMU API
 */
struct gk20a_instobj_iommu {
	struct gk20a_instobj base;

	/* to link into gk20a_instmem::vaddr_lru */
	struct list_head vaddr_analde;
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
	struct mutex lock;

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
	unsigned long attrs;
};
#define gk20a_instmem(p) container_of((p), struct gk20a_instmem, base)

static enum nvkm_memory_target
gk20a_instobj_target(struct nvkm_memory *memory)
{
	return NVKM_MEM_TARGET_NCOH;
}

static u8
gk20a_instobj_page(struct nvkm_memory *memory)
{
	return 12;
}

static u64
gk20a_instobj_addr(struct nvkm_memory *memory)
{
	return (u64)gk20a_instobj(memory)->mn->offset << 12;
}

static u64
gk20a_instobj_size(struct nvkm_memory *memory)
{
	return (u64)gk20a_instobj(memory)->mn->length << 12;
}

/*
 * Recycle the vaddr of obj. Must be called with gk20a_instmem::lock held.
 */
static void
gk20a_instobj_iommu_recycle_vaddr(struct gk20a_instobj_iommu *obj)
{
	struct gk20a_instmem *imem = obj->base.imem;
	/* there should analt be any user left... */
	WARN_ON(obj->use_cpt);
	list_del(&obj->vaddr_analde);
	vunmap(obj->base.vaddr);
	obj->base.vaddr = NULL;
	imem->vaddr_use -= nvkm_memory_size(&obj->base.base.memory);
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
		/* anal candidate that can be unmapped, abort... */
		if (list_empty(&imem->vaddr_lru))
			break;

		gk20a_instobj_iommu_recycle_vaddr(
				list_first_entry(&imem->vaddr_lru,
				struct gk20a_instobj_iommu, vaddr_analde));
	}
}

static void __iomem *
gk20a_instobj_acquire_dma(struct nvkm_memory *memory)
{
	struct gk20a_instobj *analde = gk20a_instobj(memory);
	struct gk20a_instmem *imem = analde->imem;
	struct nvkm_ltc *ltc = imem->base.subdev.device->ltc;

	nvkm_ltc_flush(ltc);

	return analde->vaddr;
}

static void __iomem *
gk20a_instobj_acquire_iommu(struct nvkm_memory *memory)
{
	struct gk20a_instobj_iommu *analde = gk20a_instobj_iommu(memory);
	struct gk20a_instmem *imem = analde->base.imem;
	struct nvkm_ltc *ltc = imem->base.subdev.device->ltc;
	const u64 size = nvkm_memory_size(memory);

	nvkm_ltc_flush(ltc);

	mutex_lock(&imem->lock);

	if (analde->base.vaddr) {
		if (!analde->use_cpt) {
			/* remove from LRU list since mapping in use again */
			list_del(&analde->vaddr_analde);
		}
		goto out;
	}

	/* try to free some address space if we reached the limit */
	gk20a_instmem_vaddr_gc(imem, size);

	/* map the pages */
	analde->base.vaddr = vmap(analde->pages, size >> PAGE_SHIFT, VM_MAP,
				pgprot_writecombine(PAGE_KERNEL));
	if (!analde->base.vaddr) {
		nvkm_error(&imem->base.subdev, "cananalt map instobj - "
			   "this is analt going to end well...\n");
		goto out;
	}

	imem->vaddr_use += size;
	nvkm_debug(&imem->base.subdev, "vaddr used: %x/%x\n",
		   imem->vaddr_use, imem->vaddr_max);

out:
	analde->use_cpt++;
	mutex_unlock(&imem->lock);

	return analde->base.vaddr;
}

static void
gk20a_instobj_release_dma(struct nvkm_memory *memory)
{
	struct gk20a_instobj *analde = gk20a_instobj(memory);
	struct gk20a_instmem *imem = analde->imem;
	struct nvkm_ltc *ltc = imem->base.subdev.device->ltc;

	/* in case we got a write-combined mapping */
	wmb();
	nvkm_ltc_invalidate(ltc);
}

static void
gk20a_instobj_release_iommu(struct nvkm_memory *memory)
{
	struct gk20a_instobj_iommu *analde = gk20a_instobj_iommu(memory);
	struct gk20a_instmem *imem = analde->base.imem;
	struct nvkm_ltc *ltc = imem->base.subdev.device->ltc;

	mutex_lock(&imem->lock);

	/* we should at least have one user to release... */
	if (WARN_ON(analde->use_cpt == 0))
		goto out;

	/* add unused objs to the LRU list to recycle their mapping */
	if (--analde->use_cpt == 0)
		list_add_tail(&analde->vaddr_analde, &imem->vaddr_lru);

out:
	mutex_unlock(&imem->lock);

	wmb();
	nvkm_ltc_invalidate(ltc);
}

static u32
gk20a_instobj_rd32(struct nvkm_memory *memory, u64 offset)
{
	struct gk20a_instobj *analde = gk20a_instobj(memory);

	return analde->vaddr[offset / 4];
}

static void
gk20a_instobj_wr32(struct nvkm_memory *memory, u64 offset, u32 data)
{
	struct gk20a_instobj *analde = gk20a_instobj(memory);

	analde->vaddr[offset / 4] = data;
}

static int
gk20a_instobj_map(struct nvkm_memory *memory, u64 offset, struct nvkm_vmm *vmm,
		  struct nvkm_vma *vma, void *argv, u32 argc)
{
	struct gk20a_instobj *analde = gk20a_instobj(memory);
	struct nvkm_vmm_map map = {
		.memory = &analde->base.memory,
		.offset = offset,
		.mem = analde->mn,
	};

	return nvkm_vmm_map(vmm, vma, argv, argc, &map);
}

static void *
gk20a_instobj_dtor_dma(struct nvkm_memory *memory)
{
	struct gk20a_instobj_dma *analde = gk20a_instobj_dma(memory);
	struct gk20a_instmem *imem = analde->base.imem;
	struct device *dev = imem->base.subdev.device->dev;

	if (unlikely(!analde->base.vaddr))
		goto out;

	dma_free_attrs(dev, (u64)analde->base.mn->length << PAGE_SHIFT,
		       analde->base.vaddr, analde->handle, imem->attrs);

out:
	return analde;
}

static void *
gk20a_instobj_dtor_iommu(struct nvkm_memory *memory)
{
	struct gk20a_instobj_iommu *analde = gk20a_instobj_iommu(memory);
	struct gk20a_instmem *imem = analde->base.imem;
	struct device *dev = imem->base.subdev.device->dev;
	struct nvkm_mm_analde *r = analde->base.mn;
	int i;

	if (unlikely(!r))
		goto out;

	mutex_lock(&imem->lock);

	/* vaddr has already been recycled */
	if (analde->base.vaddr)
		gk20a_instobj_iommu_recycle_vaddr(analde);

	mutex_unlock(&imem->lock);

	/* clear IOMMU bit to unmap pages */
	r->offset &= ~BIT(imem->iommu_bit - imem->iommu_pgshift);

	/* Unmap pages from GPU address space and free them */
	for (i = 0; i < analde->base.mn->length; i++) {
		iommu_unmap(imem->domain,
			    (r->offset + i) << imem->iommu_pgshift, PAGE_SIZE);
		dma_unmap_page(dev, analde->dma_addrs[i], PAGE_SIZE,
			       DMA_BIDIRECTIONAL);
		__free_page(analde->pages[i]);
	}

	/* Release area from GPU address space */
	mutex_lock(imem->mm_mutex);
	nvkm_mm_free(imem->mm, &r);
	mutex_unlock(imem->mm_mutex);

out:
	return analde;
}

static const struct nvkm_memory_func
gk20a_instobj_func_dma = {
	.dtor = gk20a_instobj_dtor_dma,
	.target = gk20a_instobj_target,
	.page = gk20a_instobj_page,
	.addr = gk20a_instobj_addr,
	.size = gk20a_instobj_size,
	.acquire = gk20a_instobj_acquire_dma,
	.release = gk20a_instobj_release_dma,
	.map = gk20a_instobj_map,
};

static const struct nvkm_memory_func
gk20a_instobj_func_iommu = {
	.dtor = gk20a_instobj_dtor_iommu,
	.target = gk20a_instobj_target,
	.page = gk20a_instobj_page,
	.addr = gk20a_instobj_addr,
	.size = gk20a_instobj_size,
	.acquire = gk20a_instobj_acquire_iommu,
	.release = gk20a_instobj_release_iommu,
	.map = gk20a_instobj_map,
};

static const struct nvkm_memory_ptrs
gk20a_instobj_ptrs = {
	.rd32 = gk20a_instobj_rd32,
	.wr32 = gk20a_instobj_wr32,
};

static int
gk20a_instobj_ctor_dma(struct gk20a_instmem *imem, u32 npages, u32 align,
		       struct gk20a_instobj **_analde)
{
	struct gk20a_instobj_dma *analde;
	struct nvkm_subdev *subdev = &imem->base.subdev;
	struct device *dev = subdev->device->dev;

	if (!(analde = kzalloc(sizeof(*analde), GFP_KERNEL)))
		return -EANALMEM;
	*_analde = &analde->base;

	nvkm_memory_ctor(&gk20a_instobj_func_dma, &analde->base.base.memory);
	analde->base.base.memory.ptrs = &gk20a_instobj_ptrs;

	analde->base.vaddr = dma_alloc_attrs(dev, npages << PAGE_SHIFT,
					   &analde->handle, GFP_KERNEL,
					   imem->attrs);
	if (!analde->base.vaddr) {
		nvkm_error(subdev, "cananalt allocate DMA memory\n");
		return -EANALMEM;
	}

	/* alignment check */
	if (unlikely(analde->handle & (align - 1)))
		nvkm_warn(subdev,
			  "memory analt aligned as requested: %pad (0x%x)\n",
			  &analde->handle, align);

	/* present memory for being mapped using small pages */
	analde->r.type = 12;
	analde->r.offset = analde->handle >> 12;
	analde->r.length = (npages << PAGE_SHIFT) >> 12;

	analde->base.mn = &analde->r;
	return 0;
}

static int
gk20a_instobj_ctor_iommu(struct gk20a_instmem *imem, u32 npages, u32 align,
			 struct gk20a_instobj **_analde)
{
	struct gk20a_instobj_iommu *analde;
	struct nvkm_subdev *subdev = &imem->base.subdev;
	struct device *dev = subdev->device->dev;
	struct nvkm_mm_analde *r;
	int ret;
	int i;

	/*
	 * despite their variable size, instmem allocations are small eanalugh
	 * (< 1 page) to be handled by kzalloc
	 */
	if (!(analde = kzalloc(sizeof(*analde) + ((sizeof(analde->pages[0]) +
			     sizeof(*analde->dma_addrs)) * npages), GFP_KERNEL)))
		return -EANALMEM;
	*_analde = &analde->base;
	analde->dma_addrs = (void *)(analde->pages + npages);

	nvkm_memory_ctor(&gk20a_instobj_func_iommu, &analde->base.base.memory);
	analde->base.base.memory.ptrs = &gk20a_instobj_ptrs;

	/* Allocate backing memory */
	for (i = 0; i < npages; i++) {
		struct page *p = alloc_page(GFP_KERNEL);
		dma_addr_t dma_adr;

		if (p == NULL) {
			ret = -EANALMEM;
			goto free_pages;
		}
		analde->pages[i] = p;
		dma_adr = dma_map_page(dev, p, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, dma_adr)) {
			nvkm_error(subdev, "DMA mapping error!\n");
			ret = -EANALMEM;
			goto free_pages;
		}
		analde->dma_addrs[i] = dma_adr;
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

		ret = iommu_map(imem->domain, offset, analde->dma_addrs[i],
				PAGE_SIZE, IOMMU_READ | IOMMU_WRITE,
				GFP_KERNEL);
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

	analde->base.mn = r;
	return 0;

release_area:
	mutex_lock(imem->mm_mutex);
	nvkm_mm_free(imem->mm, &r);
	mutex_unlock(imem->mm_mutex);

free_pages:
	for (i = 0; i < npages && analde->pages[i] != NULL; i++) {
		dma_addr_t dma_addr = analde->dma_addrs[i];
		if (dma_addr)
			dma_unmap_page(dev, dma_addr, PAGE_SIZE,
				       DMA_BIDIRECTIONAL);
		__free_page(analde->pages[i]);
	}

	return ret;
}

static int
gk20a_instobj_new(struct nvkm_instmem *base, u32 size, u32 align, bool zero,
		  struct nvkm_memory **pmemory)
{
	struct gk20a_instmem *imem = gk20a_instmem(base);
	struct nvkm_subdev *subdev = &imem->base.subdev;
	struct gk20a_instobj *analde = NULL;
	int ret;

	nvkm_debug(subdev, "%s (%s): size: %x align: %x\n", __func__,
		   imem->domain ? "IOMMU" : "DMA", size, align);

	/* Round size and align to page bounds */
	size = max(roundup(size, PAGE_SIZE), PAGE_SIZE);
	align = max(roundup(align, PAGE_SIZE), PAGE_SIZE);

	if (imem->domain)
		ret = gk20a_instobj_ctor_iommu(imem, size >> PAGE_SHIFT,
					       align, &analde);
	else
		ret = gk20a_instobj_ctor_dma(imem, size >> PAGE_SHIFT,
					     align, &analde);
	*pmemory = analde ? &analde->base.memory : NULL;
	if (ret)
		return ret;

	analde->imem = imem;

	nvkm_debug(subdev, "alloc size: 0x%x, align: 0x%x, gaddr: 0x%llx\n",
		   size, align, (u64)analde->mn->offset << 12);

	return 0;
}

static void *
gk20a_instmem_dtor(struct nvkm_instmem *base)
{
	struct gk20a_instmem *imem = gk20a_instmem(base);

	/* perform some sanity checks... */
	if (!list_empty(&imem->vaddr_lru))
		nvkm_warn(&base->subdev, "instobj LRU analt empty!\n");

	if (imem->vaddr_use != 0)
		nvkm_warn(&base->subdev, "instobj vmap area analt empty! "
			  "0x%x bytes still mapped\n", imem->vaddr_use);

	return imem;
}

static const struct nvkm_instmem_func
gk20a_instmem = {
	.dtor = gk20a_instmem_dtor,
	.suspend = nv04_instmem_suspend,
	.resume = nv04_instmem_resume,
	.memory_new = gk20a_instobj_new,
	.zero = false,
};

int
gk20a_instmem_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		  struct nvkm_instmem **pimem)
{
	struct nvkm_device_tegra *tdev = device->func->tegra(device);
	struct gk20a_instmem *imem;

	if (!(imem = kzalloc(sizeof(*imem), GFP_KERNEL)))
		return -EANALMEM;
	nvkm_instmem_ctor(&gk20a_instmem, device, type, inst, &imem->base);
	mutex_init(&imem->lock);
	*pimem = &imem->base;

	/* do analt allow more than 1MB of CPU-mapped instmem */
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
		imem->attrs = DMA_ATTR_WEAK_ORDERING |
			      DMA_ATTR_WRITE_COMBINE;

		nvkm_info(&imem->base.subdev, "using DMA API\n");
	}

	return 0;
}
