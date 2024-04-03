// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/dma-map-ops.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>
#include <linux/pfn_t.h>

#include <drm/drm_prime.h>

#include "msm_drv.h"
#include "msm_fence.h"
#include "msm_gem.h"
#include "msm_gpu.h"
#include "msm_mmu.h"

static dma_addr_t physaddr(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_drm_private *priv = obj->dev->dev_private;
	return (((dma_addr_t)msm_obj->vram_node->start) << PAGE_SHIFT) +
			priv->vram.paddr;
}

static bool use_pages(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	return !msm_obj->vram_node;
}

/*
 * Cache sync.. this is a bit over-complicated, to fit dma-mapping
 * API.  Really GPU cache is out of scope here (handled on cmdstream)
 * and all we need to do is invalidate newly allocated pages before
 * mapping to CPU as uncached/writecombine.
 *
 * On top of this, we have the added headache, that depending on
 * display generation, the display's iommu may be wired up to either
 * the toplevel drm device (mdss), or to the mdp sub-node, meaning
 * that here we either have dma-direct or iommu ops.
 *
 * Let this be a cautionary tail of abstraction gone wrong.
 */

static void sync_for_device(struct msm_gem_object *msm_obj)
{
	struct device *dev = msm_obj->base.dev->dev;

	dma_map_sgtable(dev, msm_obj->sgt, DMA_BIDIRECTIONAL, 0);
}

static void sync_for_cpu(struct msm_gem_object *msm_obj)
{
	struct device *dev = msm_obj->base.dev->dev;

	dma_unmap_sgtable(dev, msm_obj->sgt, DMA_BIDIRECTIONAL, 0);
}

static void update_lru_active(struct drm_gem_object *obj)
{
	struct msm_drm_private *priv = obj->dev->dev_private;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	GEM_WARN_ON(!msm_obj->pages);

	if (msm_obj->pin_count) {
		drm_gem_lru_move_tail_locked(&priv->lru.pinned, obj);
	} else if (msm_obj->madv == MSM_MADV_WILLNEED) {
		drm_gem_lru_move_tail_locked(&priv->lru.willneed, obj);
	} else {
		GEM_WARN_ON(msm_obj->madv != MSM_MADV_DONTNEED);

		drm_gem_lru_move_tail_locked(&priv->lru.dontneed, obj);
	}
}

static void update_lru_locked(struct drm_gem_object *obj)
{
	struct msm_drm_private *priv = obj->dev->dev_private;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	msm_gem_assert_locked(&msm_obj->base);

	if (!msm_obj->pages) {
		GEM_WARN_ON(msm_obj->pin_count);

		drm_gem_lru_move_tail_locked(&priv->lru.unbacked, obj);
	} else {
		update_lru_active(obj);
	}
}

static void update_lru(struct drm_gem_object *obj)
{
	struct msm_drm_private *priv = obj->dev->dev_private;

	mutex_lock(&priv->lru.lock);
	update_lru_locked(obj);
	mutex_unlock(&priv->lru.lock);
}

/* allocate pages from VRAM carveout, used when no IOMMU: */
static struct page **get_pages_vram(struct drm_gem_object *obj, int npages)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_drm_private *priv = obj->dev->dev_private;
	dma_addr_t paddr;
	struct page **p;
	int ret, i;

	p = kvmalloc_array(npages, sizeof(struct page *), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	spin_lock(&priv->vram.lock);
	ret = drm_mm_insert_node(&priv->vram.mm, msm_obj->vram_node, npages);
	spin_unlock(&priv->vram.lock);
	if (ret) {
		kvfree(p);
		return ERR_PTR(ret);
	}

	paddr = physaddr(obj);
	for (i = 0; i < npages; i++) {
		p[i] = pfn_to_page(__phys_to_pfn(paddr));
		paddr += PAGE_SIZE;
	}

	return p;
}

static struct page **get_pages(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	msm_gem_assert_locked(obj);

	if (!msm_obj->pages) {
		struct drm_device *dev = obj->dev;
		struct page **p;
		int npages = obj->size >> PAGE_SHIFT;

		if (use_pages(obj))
			p = drm_gem_get_pages(obj);
		else
			p = get_pages_vram(obj, npages);

		if (IS_ERR(p)) {
			DRM_DEV_ERROR(dev->dev, "could not get pages: %ld\n",
					PTR_ERR(p));
			return p;
		}

		msm_obj->pages = p;

		msm_obj->sgt = drm_prime_pages_to_sg(obj->dev, p, npages);
		if (IS_ERR(msm_obj->sgt)) {
			void *ptr = ERR_CAST(msm_obj->sgt);

			DRM_DEV_ERROR(dev->dev, "failed to allocate sgt\n");
			msm_obj->sgt = NULL;
			return ptr;
		}

		/* For non-cached buffers, ensure the new pages are clean
		 * because display controller, GPU, etc. are not coherent:
		 */
		if (msm_obj->flags & MSM_BO_WC)
			sync_for_device(msm_obj);

		update_lru(obj);
	}

	return msm_obj->pages;
}

static void put_pages_vram(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_drm_private *priv = obj->dev->dev_private;

	spin_lock(&priv->vram.lock);
	drm_mm_remove_node(msm_obj->vram_node);
	spin_unlock(&priv->vram.lock);

	kvfree(msm_obj->pages);
}

static void put_pages(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	if (msm_obj->pages) {
		if (msm_obj->sgt) {
			/* For non-cached buffers, ensure the new
			 * pages are clean because display controller,
			 * GPU, etc. are not coherent:
			 */
			if (msm_obj->flags & MSM_BO_WC)
				sync_for_cpu(msm_obj);

			sg_free_table(msm_obj->sgt);
			kfree(msm_obj->sgt);
			msm_obj->sgt = NULL;
		}

		if (use_pages(obj))
			drm_gem_put_pages(obj, msm_obj->pages, true, false);
		else
			put_pages_vram(obj);

		msm_obj->pages = NULL;
		update_lru(obj);
	}
}

static struct page **msm_gem_pin_pages_locked(struct drm_gem_object *obj,
					      unsigned madv)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	msm_gem_assert_locked(obj);

	if (msm_obj->madv > madv) {
		DRM_DEV_DEBUG_DRIVER(obj->dev->dev, "Invalid madv state: %u vs %u\n",
				     msm_obj->madv, madv);
		return ERR_PTR(-EBUSY);
	}

	return get_pages(obj);
}

/*
 * Update the pin count of the object, call under lru.lock
 */
void msm_gem_pin_obj_locked(struct drm_gem_object *obj)
{
	struct msm_drm_private *priv = obj->dev->dev_private;

	msm_gem_assert_locked(obj);

	to_msm_bo(obj)->pin_count++;
	drm_gem_lru_move_tail_locked(&priv->lru.pinned, obj);
}

static void pin_obj_locked(struct drm_gem_object *obj)
{
	struct msm_drm_private *priv = obj->dev->dev_private;

	mutex_lock(&priv->lru.lock);
	msm_gem_pin_obj_locked(obj);
	mutex_unlock(&priv->lru.lock);
}

struct page **msm_gem_pin_pages(struct drm_gem_object *obj)
{
	struct page **p;

	msm_gem_lock(obj);
	p = msm_gem_pin_pages_locked(obj, MSM_MADV_WILLNEED);
	if (!IS_ERR(p))
		pin_obj_locked(obj);
	msm_gem_unlock(obj);

	return p;
}

void msm_gem_unpin_pages(struct drm_gem_object *obj)
{
	msm_gem_lock(obj);
	msm_gem_unpin_locked(obj);
	msm_gem_unlock(obj);
}

static pgprot_t msm_gem_pgprot(struct msm_gem_object *msm_obj, pgprot_t prot)
{
	if (msm_obj->flags & MSM_BO_WC)
		return pgprot_writecombine(prot);
	return prot;
}

static vm_fault_t msm_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct page **pages;
	unsigned long pfn;
	pgoff_t pgoff;
	int err;
	vm_fault_t ret;

	/*
	 * vm_ops.open/drm_gem_mmap_obj and close get and put
	 * a reference on obj. So, we dont need to hold one here.
	 */
	err = msm_gem_lock_interruptible(obj);
	if (err) {
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

	if (GEM_WARN_ON(msm_obj->madv != MSM_MADV_WILLNEED)) {
		msm_gem_unlock(obj);
		return VM_FAULT_SIGBUS;
	}

	/* make sure we have pages attached now */
	pages = get_pages(obj);
	if (IS_ERR(pages)) {
		ret = vmf_error(PTR_ERR(pages));
		goto out_unlock;
	}

	/* We don't use vmf->pgoff since that has the fake offset: */
	pgoff = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	pfn = page_to_pfn(pages[pgoff]);

	VERB("Inserting %p pfn %lx, pa %lx", (void *)vmf->address,
			pfn, pfn << PAGE_SHIFT);

	ret = vmf_insert_pfn(vma, vmf->address, pfn);

out_unlock:
	msm_gem_unlock(obj);
out:
	return ret;
}

/** get mmap offset */
static uint64_t mmap_offset(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	int ret;

	msm_gem_assert_locked(obj);

	/* Make it mmapable */
	ret = drm_gem_create_mmap_offset(obj);

	if (ret) {
		DRM_DEV_ERROR(dev->dev, "could not allocate mmap offset\n");
		return 0;
	}

	return drm_vma_node_offset_addr(&obj->vma_node);
}

uint64_t msm_gem_mmap_offset(struct drm_gem_object *obj)
{
	uint64_t offset;

	msm_gem_lock(obj);
	offset = mmap_offset(obj);
	msm_gem_unlock(obj);
	return offset;
}

static struct msm_gem_vma *add_vma(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma;

	msm_gem_assert_locked(obj);

	vma = msm_gem_vma_new(aspace);
	if (!vma)
		return ERR_PTR(-ENOMEM);

	list_add_tail(&vma->list, &msm_obj->vmas);

	return vma;
}

static struct msm_gem_vma *lookup_vma(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma;

	msm_gem_assert_locked(obj);

	list_for_each_entry(vma, &msm_obj->vmas, list) {
		if (vma->aspace == aspace)
			return vma;
	}

	return NULL;
}

static void del_vma(struct msm_gem_vma *vma)
{
	if (!vma)
		return;

	list_del(&vma->list);
	kfree(vma);
}

/*
 * If close is true, this also closes the VMA (releasing the allocated
 * iova range) in addition to removing the iommu mapping.  In the eviction
 * case (!close), we keep the iova allocated, but only remove the iommu
 * mapping.
 */
static void
put_iova_spaces(struct drm_gem_object *obj, bool close)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma;

	msm_gem_assert_locked(obj);

	list_for_each_entry(vma, &msm_obj->vmas, list) {
		if (vma->aspace) {
			msm_gem_vma_purge(vma);
			if (close)
				msm_gem_vma_close(vma);
		}
	}
}

/* Called with msm_obj locked */
static void
put_iova_vmas(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma, *tmp;

	msm_gem_assert_locked(obj);

	list_for_each_entry_safe(vma, tmp, &msm_obj->vmas, list) {
		del_vma(vma);
	}
}

static struct msm_gem_vma *get_vma_locked(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace,
		u64 range_start, u64 range_end)
{
	struct msm_gem_vma *vma;

	msm_gem_assert_locked(obj);

	vma = lookup_vma(obj, aspace);

	if (!vma) {
		int ret;

		vma = add_vma(obj, aspace);
		if (IS_ERR(vma))
			return vma;

		ret = msm_gem_vma_init(vma, obj->size,
			range_start, range_end);
		if (ret) {
			del_vma(vma);
			return ERR_PTR(ret);
		}
	} else {
		GEM_WARN_ON(vma->iova < range_start);
		GEM_WARN_ON((vma->iova + obj->size) > range_end);
	}

	return vma;
}

int msm_gem_pin_vma_locked(struct drm_gem_object *obj, struct msm_gem_vma *vma)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct page **pages;
	int prot = IOMMU_READ;

	if (!(msm_obj->flags & MSM_BO_GPU_READONLY))
		prot |= IOMMU_WRITE;

	if (msm_obj->flags & MSM_BO_MAP_PRIV)
		prot |= IOMMU_PRIV;

	if (msm_obj->flags & MSM_BO_CACHED_COHERENT)
		prot |= IOMMU_CACHE;

	msm_gem_assert_locked(obj);

	pages = msm_gem_pin_pages_locked(obj, MSM_MADV_WILLNEED);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	return msm_gem_vma_map(vma, prot, msm_obj->sgt, obj->size);
}

void msm_gem_unpin_locked(struct drm_gem_object *obj)
{
	struct msm_drm_private *priv = obj->dev->dev_private;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	msm_gem_assert_locked(obj);

	mutex_lock(&priv->lru.lock);
	msm_obj->pin_count--;
	GEM_WARN_ON(msm_obj->pin_count < 0);
	update_lru_locked(obj);
	mutex_unlock(&priv->lru.lock);
}

/* Special unpin path for use in fence-signaling path, avoiding the need
 * to hold the obj lock by only depending on things that a protected by
 * the LRU lock.  In particular we know that that we already have backing
 * and and that the object's dma_resv has the fence for the current
 * submit/job which will prevent us racing against page eviction.
 */
void msm_gem_unpin_active(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	msm_obj->pin_count--;
	GEM_WARN_ON(msm_obj->pin_count < 0);
	update_lru_active(obj);
}

struct msm_gem_vma *msm_gem_get_vma_locked(struct drm_gem_object *obj,
					   struct msm_gem_address_space *aspace)
{
	return get_vma_locked(obj, aspace, 0, U64_MAX);
}

static int get_and_pin_iova_range_locked(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova,
		u64 range_start, u64 range_end)
{
	struct msm_gem_vma *vma;
	int ret;

	msm_gem_assert_locked(obj);

	vma = get_vma_locked(obj, aspace, range_start, range_end);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	ret = msm_gem_pin_vma_locked(obj, vma);
	if (!ret) {
		*iova = vma->iova;
		pin_obj_locked(obj);
	}

	return ret;
}

/*
 * get iova and pin it. Should have a matching put
 * limits iova to specified range (in pages)
 */
int msm_gem_get_and_pin_iova_range(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova,
		u64 range_start, u64 range_end)
{
	int ret;

	msm_gem_lock(obj);
	ret = get_and_pin_iova_range_locked(obj, aspace, iova, range_start, range_end);
	msm_gem_unlock(obj);

	return ret;
}

/* get iova and pin it. Should have a matching put */
int msm_gem_get_and_pin_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova)
{
	return msm_gem_get_and_pin_iova_range(obj, aspace, iova, 0, U64_MAX);
}

/*
 * Get an iova but don't pin it. Doesn't need a put because iovas are currently
 * valid for the life of the object
 */
int msm_gem_get_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova)
{
	struct msm_gem_vma *vma;
	int ret = 0;

	msm_gem_lock(obj);
	vma = get_vma_locked(obj, aspace, 0, U64_MAX);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
	} else {
		*iova = vma->iova;
	}
	msm_gem_unlock(obj);

	return ret;
}

static int clear_iova(struct drm_gem_object *obj,
		      struct msm_gem_address_space *aspace)
{
	struct msm_gem_vma *vma = lookup_vma(obj, aspace);

	if (!vma)
		return 0;

	msm_gem_vma_purge(vma);
	msm_gem_vma_close(vma);
	del_vma(vma);

	return 0;
}

/*
 * Get the requested iova but don't pin it.  Fails if the requested iova is
 * not available.  Doesn't need a put because iovas are currently valid for
 * the life of the object.
 *
 * Setting an iova of zero will clear the vma.
 */
int msm_gem_set_iova(struct drm_gem_object *obj,
		     struct msm_gem_address_space *aspace, uint64_t iova)
{
	int ret = 0;

	msm_gem_lock(obj);
	if (!iova) {
		ret = clear_iova(obj, aspace);
	} else {
		struct msm_gem_vma *vma;
		vma = get_vma_locked(obj, aspace, iova, iova + obj->size);
		if (IS_ERR(vma)) {
			ret = PTR_ERR(vma);
		} else if (GEM_WARN_ON(vma->iova != iova)) {
			clear_iova(obj, aspace);
			ret = -EBUSY;
		}
	}
	msm_gem_unlock(obj);

	return ret;
}

/*
 * Unpin a iova by updating the reference counts. The memory isn't actually
 * purged until something else (shrinker, mm_notifier, destroy, etc) decides
 * to get rid of it
 */
void msm_gem_unpin_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace)
{
	struct msm_gem_vma *vma;

	msm_gem_lock(obj);
	vma = lookup_vma(obj, aspace);
	if (!GEM_WARN_ON(!vma)) {
		msm_gem_unpin_locked(obj);
	}
	msm_gem_unlock(obj);
}

int msm_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args)
{
	args->pitch = align_pitch(args->width, args->bpp);
	args->size  = PAGE_ALIGN(args->pitch * args->height);
	return msm_gem_new_handle(dev, file, args->size,
			MSM_BO_SCANOUT | MSM_BO_WC, &args->handle, "dumb");
}

int msm_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
		uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *obj;
	int ret = 0;

	/* GEM does all our handle to object mapping */
	obj = drm_gem_object_lookup(file, handle);
	if (obj == NULL) {
		ret = -ENOENT;
		goto fail;
	}

	*offset = msm_gem_mmap_offset(obj);

	drm_gem_object_put(obj);

fail:
	return ret;
}

static void *get_vaddr(struct drm_gem_object *obj, unsigned madv)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct page **pages;
	int ret = 0;

	msm_gem_assert_locked(obj);

	if (obj->import_attach)
		return ERR_PTR(-ENODEV);

	pages = msm_gem_pin_pages_locked(obj, madv);
	if (IS_ERR(pages))
		return ERR_CAST(pages);

	pin_obj_locked(obj);

	/* increment vmap_count *before* vmap() call, so shrinker can
	 * check vmap_count (is_vunmapable()) outside of msm_obj lock.
	 * This guarantees that we won't try to msm_gem_vunmap() this
	 * same object from within the vmap() call (while we already
	 * hold msm_obj lock)
	 */
	msm_obj->vmap_count++;

	if (!msm_obj->vaddr) {
		msm_obj->vaddr = vmap(pages, obj->size >> PAGE_SHIFT,
				VM_MAP, msm_gem_pgprot(msm_obj, PAGE_KERNEL));
		if (msm_obj->vaddr == NULL) {
			ret = -ENOMEM;
			goto fail;
		}
	}

	return msm_obj->vaddr;

fail:
	msm_obj->vmap_count--;
	msm_gem_unpin_locked(obj);
	return ERR_PTR(ret);
}

void *msm_gem_get_vaddr_locked(struct drm_gem_object *obj)
{
	return get_vaddr(obj, MSM_MADV_WILLNEED);
}

void *msm_gem_get_vaddr(struct drm_gem_object *obj)
{
	void *ret;

	msm_gem_lock(obj);
	ret = msm_gem_get_vaddr_locked(obj);
	msm_gem_unlock(obj);

	return ret;
}

/*
 * Don't use this!  It is for the very special case of dumping
 * submits from GPU hangs or faults, were the bo may already
 * be MSM_MADV_DONTNEED, but we know the buffer is still on the
 * active list.
 */
void *msm_gem_get_vaddr_active(struct drm_gem_object *obj)
{
	return get_vaddr(obj, __MSM_MADV_PURGED);
}

void msm_gem_put_vaddr_locked(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	msm_gem_assert_locked(obj);
	GEM_WARN_ON(msm_obj->vmap_count < 1);

	msm_obj->vmap_count--;
	msm_gem_unpin_locked(obj);
}

void msm_gem_put_vaddr(struct drm_gem_object *obj)
{
	msm_gem_lock(obj);
	msm_gem_put_vaddr_locked(obj);
	msm_gem_unlock(obj);
}

/* Update madvise status, returns true if not purged, else
 * false or -errno.
 */
int msm_gem_madvise(struct drm_gem_object *obj, unsigned madv)
{
	struct msm_drm_private *priv = obj->dev->dev_private;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	msm_gem_lock(obj);

	mutex_lock(&priv->lru.lock);

	if (msm_obj->madv != __MSM_MADV_PURGED)
		msm_obj->madv = madv;

	madv = msm_obj->madv;

	/* If the obj is inactive, we might need to move it
	 * between inactive lists
	 */
	update_lru_locked(obj);

	mutex_unlock(&priv->lru.lock);

	msm_gem_unlock(obj);

	return (madv != __MSM_MADV_PURGED);
}

void msm_gem_purge(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct msm_drm_private *priv = obj->dev->dev_private;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	msm_gem_assert_locked(obj);
	GEM_WARN_ON(!is_purgeable(msm_obj));

	/* Get rid of any iommu mapping(s): */
	put_iova_spaces(obj, true);

	msm_gem_vunmap(obj);

	drm_vma_node_unmap(&obj->vma_node, dev->anon_inode->i_mapping);

	put_pages(obj);

	put_iova_vmas(obj);

	mutex_lock(&priv->lru.lock);
	/* A one-way transition: */
	msm_obj->madv = __MSM_MADV_PURGED;
	mutex_unlock(&priv->lru.lock);

	drm_gem_free_mmap_offset(obj);

	/* Our goal here is to return as much of the memory as
	 * is possible back to the system as we are called from OOM.
	 * To do this we must instruct the shmfs to drop all of its
	 * backing pages, *now*.
	 */
	shmem_truncate_range(file_inode(obj->filp), 0, (loff_t)-1);

	invalidate_mapping_pages(file_inode(obj->filp)->i_mapping,
			0, (loff_t)-1);
}

/*
 * Unpin the backing pages and make them available to be swapped out.
 */
void msm_gem_evict(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	msm_gem_assert_locked(obj);
	GEM_WARN_ON(is_unevictable(msm_obj));

	/* Get rid of any iommu mapping(s): */
	put_iova_spaces(obj, false);

	drm_vma_node_unmap(&obj->vma_node, dev->anon_inode->i_mapping);

	put_pages(obj);
}

void msm_gem_vunmap(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	msm_gem_assert_locked(obj);

	if (!msm_obj->vaddr || GEM_WARN_ON(!is_vunmapable(msm_obj)))
		return;

	vunmap(msm_obj->vaddr);
	msm_obj->vaddr = NULL;
}

bool msm_gem_active(struct drm_gem_object *obj)
{
	msm_gem_assert_locked(obj);

	if (to_msm_bo(obj)->pin_count)
		return true;

	return !dma_resv_test_signaled(obj->resv, dma_resv_usage_rw(true));
}

int msm_gem_cpu_prep(struct drm_gem_object *obj, uint32_t op, ktime_t *timeout)
{
	bool write = !!(op & MSM_PREP_WRITE);
	unsigned long remain =
		op & MSM_PREP_NOSYNC ? 0 : timeout_to_jiffies(timeout);
	long ret;

	if (op & MSM_PREP_BOOST) {
		dma_resv_set_deadline(obj->resv, dma_resv_usage_rw(write),
				      ktime_get());
	}

	ret = dma_resv_wait_timeout(obj->resv, dma_resv_usage_rw(write),
				    true,  remain);
	if (ret == 0)
		return remain == 0 ? -EBUSY : -ETIMEDOUT;
	else if (ret < 0)
		return ret;

	/* TODO cache maintenance */

	return 0;
}

int msm_gem_cpu_fini(struct drm_gem_object *obj)
{
	/* TODO cache maintenance */
	return 0;
}

#ifdef CONFIG_DEBUG_FS
void msm_gem_describe(struct drm_gem_object *obj, struct seq_file *m,
		struct msm_gem_stats *stats)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct dma_resv *robj = obj->resv;
	struct msm_gem_vma *vma;
	uint64_t off = drm_vma_node_start(&obj->vma_node);
	const char *madv;

	msm_gem_lock(obj);

	stats->all.count++;
	stats->all.size += obj->size;

	if (msm_gem_active(obj)) {
		stats->active.count++;
		stats->active.size += obj->size;
	}

	if (msm_obj->pages) {
		stats->resident.count++;
		stats->resident.size += obj->size;
	}

	switch (msm_obj->madv) {
	case __MSM_MADV_PURGED:
		stats->purged.count++;
		stats->purged.size += obj->size;
		madv = " purged";
		break;
	case MSM_MADV_DONTNEED:
		stats->purgeable.count++;
		stats->purgeable.size += obj->size;
		madv = " purgeable";
		break;
	case MSM_MADV_WILLNEED:
	default:
		madv = "";
		break;
	}

	seq_printf(m, "%08x: %c %2d (%2d) %08llx %p",
			msm_obj->flags, msm_gem_active(obj) ? 'A' : 'I',
			obj->name, kref_read(&obj->refcount),
			off, msm_obj->vaddr);

	seq_printf(m, " %08zu %9s %-32s\n", obj->size, madv, msm_obj->name);

	if (!list_empty(&msm_obj->vmas)) {

		seq_puts(m, "      vmas:");

		list_for_each_entry(vma, &msm_obj->vmas, list) {
			const char *name, *comm;
			if (vma->aspace) {
				struct msm_gem_address_space *aspace = vma->aspace;
				struct task_struct *task =
					get_pid_task(aspace->pid, PIDTYPE_PID);
				if (task) {
					comm = kstrdup(task->comm, GFP_KERNEL);
					put_task_struct(task);
				} else {
					comm = NULL;
				}
				name = aspace->name;
			} else {
				name = comm = NULL;
			}
			seq_printf(m, " [%s%s%s: aspace=%p, %08llx,%s]",
				name, comm ? ":" : "", comm ? comm : "",
				vma->aspace, vma->iova,
				vma->mapped ? "mapped" : "unmapped");
			kfree(comm);
		}

		seq_puts(m, "\n");
	}

	dma_resv_describe(robj, m);
	msm_gem_unlock(obj);
}

void msm_gem_describe_objects(struct list_head *list, struct seq_file *m)
{
	struct msm_gem_stats stats = {};
	struct msm_gem_object *msm_obj;

	seq_puts(m, "   flags       id ref  offset   kaddr            size     madv      name\n");
	list_for_each_entry(msm_obj, list, node) {
		struct drm_gem_object *obj = &msm_obj->base;
		seq_puts(m, "   ");
		msm_gem_describe(obj, m, &stats);
	}

	seq_printf(m, "Total:     %4d objects, %9zu bytes\n",
			stats.all.count, stats.all.size);
	seq_printf(m, "Active:    %4d objects, %9zu bytes\n",
			stats.active.count, stats.active.size);
	seq_printf(m, "Resident:  %4d objects, %9zu bytes\n",
			stats.resident.count, stats.resident.size);
	seq_printf(m, "Purgeable: %4d objects, %9zu bytes\n",
			stats.purgeable.count, stats.purgeable.size);
	seq_printf(m, "Purged:    %4d objects, %9zu bytes\n",
			stats.purged.count, stats.purged.size);
}
#endif

/* don't call directly!  Use drm_gem_object_put() */
static void msm_gem_free_object(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct drm_device *dev = obj->dev;
	struct msm_drm_private *priv = dev->dev_private;

	mutex_lock(&priv->obj_lock);
	list_del(&msm_obj->node);
	mutex_unlock(&priv->obj_lock);

	put_iova_spaces(obj, true);

	if (obj->import_attach) {
		GEM_WARN_ON(msm_obj->vaddr);

		/* Don't drop the pages for imported dmabuf, as they are not
		 * ours, just free the array we allocated:
		 */
		kvfree(msm_obj->pages);

		put_iova_vmas(obj);

		drm_prime_gem_destroy(obj, msm_obj->sgt);
	} else {
		msm_gem_vunmap(obj);
		put_pages(obj);
		put_iova_vmas(obj);
	}

	drm_gem_object_release(obj);

	kfree(msm_obj->metadata);
	kfree(msm_obj);
}

static int msm_gem_object_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	vm_flags_set(vma, VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_page_prot = msm_gem_pgprot(msm_obj, vm_get_page_prot(vma->vm_flags));

	return 0;
}

/* convenience method to construct a GEM buffer object, and userspace handle */
int msm_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		uint32_t size, uint32_t flags, uint32_t *handle,
		char *name)
{
	struct drm_gem_object *obj;
	int ret;

	obj = msm_gem_new(dev, size, flags);

	if (IS_ERR(obj))
		return PTR_ERR(obj);

	if (name)
		msm_gem_object_set_name(obj, "%s", name);

	ret = drm_gem_handle_create(file, obj, handle);

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_put(obj);

	return ret;
}

static enum drm_gem_object_status msm_gem_status(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	enum drm_gem_object_status status = 0;

	if (msm_obj->pages)
		status |= DRM_GEM_OBJECT_RESIDENT;

	if (msm_obj->madv == MSM_MADV_DONTNEED)
		status |= DRM_GEM_OBJECT_PURGEABLE;

	return status;
}

static const struct vm_operations_struct vm_ops = {
	.fault = msm_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_gem_object_funcs msm_gem_object_funcs = {
	.free = msm_gem_free_object,
	.pin = msm_gem_prime_pin,
	.unpin = msm_gem_prime_unpin,
	.get_sg_table = msm_gem_prime_get_sg_table,
	.vmap = msm_gem_prime_vmap,
	.vunmap = msm_gem_prime_vunmap,
	.mmap = msm_gem_object_mmap,
	.status = msm_gem_status,
	.vm_ops = &vm_ops,
};

static int msm_gem_new_impl(struct drm_device *dev,
		uint32_t size, uint32_t flags,
		struct drm_gem_object **obj)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gem_object *msm_obj;

	switch (flags & MSM_BO_CACHE_MASK) {
	case MSM_BO_CACHED:
	case MSM_BO_WC:
		break;
	case MSM_BO_CACHED_COHERENT:
		if (priv->has_cached_coherent)
			break;
		fallthrough;
	default:
		DRM_DEV_DEBUG(dev->dev, "invalid cache flag: %x\n",
				(flags & MSM_BO_CACHE_MASK));
		return -EINVAL;
	}

	msm_obj = kzalloc(sizeof(*msm_obj), GFP_KERNEL);
	if (!msm_obj)
		return -ENOMEM;

	msm_obj->flags = flags;
	msm_obj->madv = MSM_MADV_WILLNEED;

	INIT_LIST_HEAD(&msm_obj->node);
	INIT_LIST_HEAD(&msm_obj->vmas);

	*obj = &msm_obj->base;
	(*obj)->funcs = &msm_gem_object_funcs;

	return 0;
}

struct drm_gem_object *msm_gem_new(struct drm_device *dev, uint32_t size, uint32_t flags)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gem_object *msm_obj;
	struct drm_gem_object *obj = NULL;
	bool use_vram = false;
	int ret;

	size = PAGE_ALIGN(size);

	if (!msm_use_mmu(dev))
		use_vram = true;
	else if ((flags & (MSM_BO_STOLEN | MSM_BO_SCANOUT)) && priv->vram.size)
		use_vram = true;

	if (GEM_WARN_ON(use_vram && !priv->vram.size))
		return ERR_PTR(-EINVAL);

	/* Disallow zero sized objects as they make the underlying
	 * infrastructure grumpy
	 */
	if (size == 0)
		return ERR_PTR(-EINVAL);

	ret = msm_gem_new_impl(dev, size, flags, &obj);
	if (ret)
		return ERR_PTR(ret);

	msm_obj = to_msm_bo(obj);

	if (use_vram) {
		struct msm_gem_vma *vma;
		struct page **pages;

		drm_gem_private_object_init(dev, obj, size);

		msm_gem_lock(obj);

		vma = add_vma(obj, NULL);
		msm_gem_unlock(obj);
		if (IS_ERR(vma)) {
			ret = PTR_ERR(vma);
			goto fail;
		}

		to_msm_bo(obj)->vram_node = &vma->node;

		msm_gem_lock(obj);
		pages = get_pages(obj);
		msm_gem_unlock(obj);
		if (IS_ERR(pages)) {
			ret = PTR_ERR(pages);
			goto fail;
		}

		vma->iova = physaddr(obj);
	} else {
		ret = drm_gem_object_init(dev, obj, size);
		if (ret)
			goto fail;
		/*
		 * Our buffers are kept pinned, so allocating them from the
		 * MOVABLE zone is a really bad idea, and conflicts with CMA.
		 * See comments above new_inode() why this is required _and_
		 * expected if you're going to pin these pages.
		 */
		mapping_set_gfp_mask(obj->filp->f_mapping, GFP_HIGHUSER);
	}

	drm_gem_lru_move_tail(&priv->lru.unbacked, obj);

	mutex_lock(&priv->obj_lock);
	list_add_tail(&msm_obj->node, &priv->objects);
	mutex_unlock(&priv->obj_lock);

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		goto fail;

	return obj;

fail:
	drm_gem_object_put(obj);
	return ERR_PTR(ret);
}

struct drm_gem_object *msm_gem_import(struct drm_device *dev,
		struct dma_buf *dmabuf, struct sg_table *sgt)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gem_object *msm_obj;
	struct drm_gem_object *obj;
	uint32_t size;
	int ret, npages;

	/* if we don't have IOMMU, don't bother pretending we can import: */
	if (!msm_use_mmu(dev)) {
		DRM_DEV_ERROR(dev->dev, "cannot import without IOMMU\n");
		return ERR_PTR(-EINVAL);
	}

	size = PAGE_ALIGN(dmabuf->size);

	ret = msm_gem_new_impl(dev, size, MSM_BO_WC, &obj);
	if (ret)
		return ERR_PTR(ret);

	drm_gem_private_object_init(dev, obj, size);

	npages = size / PAGE_SIZE;

	msm_obj = to_msm_bo(obj);
	msm_gem_lock(obj);
	msm_obj->sgt = sgt;
	msm_obj->pages = kvmalloc_array(npages, sizeof(struct page *), GFP_KERNEL);
	if (!msm_obj->pages) {
		msm_gem_unlock(obj);
		ret = -ENOMEM;
		goto fail;
	}

	ret = drm_prime_sg_to_page_array(sgt, msm_obj->pages, npages);
	if (ret) {
		msm_gem_unlock(obj);
		goto fail;
	}

	msm_gem_unlock(obj);

	drm_gem_lru_move_tail(&priv->lru.pinned, obj);

	mutex_lock(&priv->obj_lock);
	list_add_tail(&msm_obj->node, &priv->objects);
	mutex_unlock(&priv->obj_lock);

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		goto fail;

	return obj;

fail:
	drm_gem_object_put(obj);
	return ERR_PTR(ret);
}

void *msm_gem_kernel_new(struct drm_device *dev, uint32_t size,
		uint32_t flags, struct msm_gem_address_space *aspace,
		struct drm_gem_object **bo, uint64_t *iova)
{
	void *vaddr;
	struct drm_gem_object *obj = msm_gem_new(dev, size, flags);
	int ret;

	if (IS_ERR(obj))
		return ERR_CAST(obj);

	if (iova) {
		ret = msm_gem_get_and_pin_iova(obj, aspace, iova);
		if (ret)
			goto err;
	}

	vaddr = msm_gem_get_vaddr(obj);
	if (IS_ERR(vaddr)) {
		msm_gem_unpin_iova(obj, aspace);
		ret = PTR_ERR(vaddr);
		goto err;
	}

	if (bo)
		*bo = obj;

	return vaddr;
err:
	drm_gem_object_put(obj);

	return ERR_PTR(ret);

}

void msm_gem_kernel_put(struct drm_gem_object *bo,
		struct msm_gem_address_space *aspace)
{
	if (IS_ERR_OR_NULL(bo))
		return;

	msm_gem_put_vaddr(bo);
	msm_gem_unpin_iova(bo, aspace);
	drm_gem_object_put(bo);
}

void msm_gem_object_set_name(struct drm_gem_object *bo, const char *fmt, ...)
{
	struct msm_gem_object *msm_obj = to_msm_bo(bo);
	va_list ap;

	if (!fmt)
		return;

	va_start(ap, fmt);
	vsnprintf(msm_obj->name, sizeof(msm_obj->name), fmt, ap);
	va_end(ap);
}
