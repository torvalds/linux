// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

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

static void msm_gem_vunmap_locked(struct drm_gem_object *obj);


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
		p[i] = phys_to_page(paddr);
		paddr += PAGE_SIZE;
	}

	return p;
}

static struct page **get_pages(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

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
		if (msm_obj->flags & (MSM_BO_WC|MSM_BO_UNCACHED))
			sync_for_device(msm_obj);
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
			if (msm_obj->flags & (MSM_BO_WC|MSM_BO_UNCACHED))
				sync_for_cpu(msm_obj);

			sg_free_table(msm_obj->sgt);
			kfree(msm_obj->sgt);
		}

		if (use_pages(obj))
			drm_gem_put_pages(obj, msm_obj->pages, true, false);
		else
			put_pages_vram(obj);

		msm_obj->pages = NULL;
	}
}

struct page **msm_gem_get_pages(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct page **p;

	mutex_lock(&msm_obj->lock);

	if (WARN_ON(msm_obj->madv != MSM_MADV_WILLNEED)) {
		mutex_unlock(&msm_obj->lock);
		return ERR_PTR(-EBUSY);
	}

	p = get_pages(obj);
	mutex_unlock(&msm_obj->lock);
	return p;
}

void msm_gem_put_pages(struct drm_gem_object *obj)
{
	/* when we start tracking the pin count, then do something here */
}

int msm_gem_mmap_obj(struct drm_gem_object *obj,
		struct vm_area_struct *vma)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;

	if (msm_obj->flags & MSM_BO_WC) {
		vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	} else if (msm_obj->flags & MSM_BO_UNCACHED) {
		vma->vm_page_prot = pgprot_noncached(vm_get_page_prot(vma->vm_flags));
	} else {
		/*
		 * Shunt off cached objs to shmem file so they have their own
		 * address_space (so unmap_mapping_range does what we want,
		 * in particular in the case of mmap'd dmabufs)
		 */
		fput(vma->vm_file);
		get_file(obj->filp);
		vma->vm_pgoff = 0;
		vma->vm_file  = obj->filp;

		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	}

	return 0;
}

int msm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret) {
		DBG("mmap failed: %d", ret);
		return ret;
	}

	return msm_gem_mmap_obj(vma->vm_private_data, vma);
}

vm_fault_t msm_gem_fault(struct vm_fault *vmf)
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
	err = mutex_lock_interruptible(&msm_obj->lock);
	if (err) {
		ret = VM_FAULT_NOPAGE;
		goto out;
	}

	if (WARN_ON(msm_obj->madv != MSM_MADV_WILLNEED)) {
		mutex_unlock(&msm_obj->lock);
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

	ret = vmf_insert_mixed(vma, vmf->address, __pfn_to_pfn_t(pfn, PFN_DEV));
out_unlock:
	mutex_unlock(&msm_obj->lock);
out:
	return ret;
}

/** get mmap offset */
static uint64_t mmap_offset(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	int ret;

	WARN_ON(!mutex_is_locked(&msm_obj->lock));

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
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	mutex_lock(&msm_obj->lock);
	offset = mmap_offset(obj);
	mutex_unlock(&msm_obj->lock);
	return offset;
}

static struct msm_gem_vma *add_vma(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma;

	WARN_ON(!mutex_is_locked(&msm_obj->lock));

	vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	if (!vma)
		return ERR_PTR(-ENOMEM);

	vma->aspace = aspace;

	list_add_tail(&vma->list, &msm_obj->vmas);

	return vma;
}

static struct msm_gem_vma *lookup_vma(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma;

	WARN_ON(!mutex_is_locked(&msm_obj->lock));

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

/* Called with msm_obj->lock locked */
static void
put_iova(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma, *tmp;

	WARN_ON(!mutex_is_locked(&msm_obj->lock));

	list_for_each_entry_safe(vma, tmp, &msm_obj->vmas, list) {
		if (vma->aspace) {
			msm_gem_purge_vma(vma->aspace, vma);
			msm_gem_close_vma(vma->aspace, vma);
		}
		del_vma(vma);
	}
}

static int msm_gem_get_iova_locked(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova,
		u64 range_start, u64 range_end)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma;
	int ret = 0;

	WARN_ON(!mutex_is_locked(&msm_obj->lock));

	vma = lookup_vma(obj, aspace);

	if (!vma) {
		vma = add_vma(obj, aspace);
		if (IS_ERR(vma))
			return PTR_ERR(vma);

		ret = msm_gem_init_vma(aspace, vma, obj->size >> PAGE_SHIFT,
			range_start, range_end);
		if (ret) {
			del_vma(vma);
			return ret;
		}
	}

	*iova = vma->iova;
	return 0;
}

static int msm_gem_pin_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma;
	struct page **pages;
	int prot = IOMMU_READ;

	if (!(msm_obj->flags & MSM_BO_GPU_READONLY))
		prot |= IOMMU_WRITE;

	if (msm_obj->flags & MSM_BO_MAP_PRIV)
		prot |= IOMMU_PRIV;

	WARN_ON(!mutex_is_locked(&msm_obj->lock));

	if (WARN_ON(msm_obj->madv != MSM_MADV_WILLNEED))
		return -EBUSY;

	vma = lookup_vma(obj, aspace);
	if (WARN_ON(!vma))
		return -EINVAL;

	pages = get_pages(obj);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	return msm_gem_map_vma(aspace, vma, prot,
			msm_obj->sgt, obj->size >> PAGE_SHIFT);
}

/*
 * get iova and pin it. Should have a matching put
 * limits iova to specified range (in pages)
 */
int msm_gem_get_and_pin_iova_range(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova,
		u64 range_start, u64 range_end)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	u64 local;
	int ret;

	mutex_lock(&msm_obj->lock);

	ret = msm_gem_get_iova_locked(obj, aspace, &local,
		range_start, range_end);

	if (!ret)
		ret = msm_gem_pin_iova(obj, aspace);

	if (!ret)
		*iova = local;

	mutex_unlock(&msm_obj->lock);
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
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	int ret;

	mutex_lock(&msm_obj->lock);
	ret = msm_gem_get_iova_locked(obj, aspace, iova, 0, U64_MAX);
	mutex_unlock(&msm_obj->lock);

	return ret;
}

/* get iova without taking a reference, used in places where you have
 * already done a 'msm_gem_get_and_pin_iova' or 'msm_gem_get_iova'
 */
uint64_t msm_gem_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma;

	mutex_lock(&msm_obj->lock);
	vma = lookup_vma(obj, aspace);
	mutex_unlock(&msm_obj->lock);
	WARN_ON(!vma);

	return vma ? vma->iova : 0;
}

/*
 * Unpin a iova by updating the reference counts. The memory isn't actually
 * purged until something else (shrinker, mm_notifier, destroy, etc) decides
 * to get rid of it
 */
void msm_gem_unpin_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma;

	mutex_lock(&msm_obj->lock);
	vma = lookup_vma(obj, aspace);

	if (!WARN_ON(!vma))
		msm_gem_unmap_vma(aspace, vma);

	mutex_unlock(&msm_obj->lock);
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
	int ret = 0;

	if (obj->import_attach)
		return ERR_PTR(-ENODEV);

	mutex_lock(&msm_obj->lock);

	if (WARN_ON(msm_obj->madv > madv)) {
		DRM_DEV_ERROR(obj->dev->dev, "Invalid madv state: %u vs %u\n",
			msm_obj->madv, madv);
		mutex_unlock(&msm_obj->lock);
		return ERR_PTR(-EBUSY);
	}

	/* increment vmap_count *before* vmap() call, so shrinker can
	 * check vmap_count (is_vunmapable()) outside of msm_obj->lock.
	 * This guarantees that we won't try to msm_gem_vunmap() this
	 * same object from within the vmap() call (while we already
	 * hold msm_obj->lock)
	 */
	msm_obj->vmap_count++;

	if (!msm_obj->vaddr) {
		struct page **pages = get_pages(obj);
		if (IS_ERR(pages)) {
			ret = PTR_ERR(pages);
			goto fail;
		}
		msm_obj->vaddr = vmap(pages, obj->size >> PAGE_SHIFT,
				VM_MAP, pgprot_writecombine(PAGE_KERNEL));
		if (msm_obj->vaddr == NULL) {
			ret = -ENOMEM;
			goto fail;
		}
	}

	mutex_unlock(&msm_obj->lock);
	return msm_obj->vaddr;

fail:
	msm_obj->vmap_count--;
	mutex_unlock(&msm_obj->lock);
	return ERR_PTR(ret);
}

void *msm_gem_get_vaddr(struct drm_gem_object *obj)
{
	return get_vaddr(obj, MSM_MADV_WILLNEED);
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

void msm_gem_put_vaddr(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	mutex_lock(&msm_obj->lock);
	WARN_ON(msm_obj->vmap_count < 1);
	msm_obj->vmap_count--;
	mutex_unlock(&msm_obj->lock);
}

/* Update madvise status, returns true if not purged, else
 * false or -errno.
 */
int msm_gem_madvise(struct drm_gem_object *obj, unsigned madv)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	mutex_lock(&msm_obj->lock);

	WARN_ON(!mutex_is_locked(&obj->dev->struct_mutex));

	if (msm_obj->madv != __MSM_MADV_PURGED)
		msm_obj->madv = madv;

	madv = msm_obj->madv;

	mutex_unlock(&msm_obj->lock);

	return (madv != __MSM_MADV_PURGED);
}

void msm_gem_purge(struct drm_gem_object *obj, enum msm_gem_lock subclass)
{
	struct drm_device *dev = obj->dev;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));
	WARN_ON(!is_purgeable(msm_obj));
	WARN_ON(obj->import_attach);

	mutex_lock_nested(&msm_obj->lock, subclass);

	put_iova(obj);

	msm_gem_vunmap_locked(obj);

	put_pages(obj);

	msm_obj->madv = __MSM_MADV_PURGED;

	drm_vma_node_unmap(&obj->vma_node, dev->anon_inode->i_mapping);
	drm_gem_free_mmap_offset(obj);

	/* Our goal here is to return as much of the memory as
	 * is possible back to the system as we are called from OOM.
	 * To do this we must instruct the shmfs to drop all of its
	 * backing pages, *now*.
	 */
	shmem_truncate_range(file_inode(obj->filp), 0, (loff_t)-1);

	invalidate_mapping_pages(file_inode(obj->filp)->i_mapping,
			0, (loff_t)-1);

	mutex_unlock(&msm_obj->lock);
}

static void msm_gem_vunmap_locked(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	WARN_ON(!mutex_is_locked(&msm_obj->lock));

	if (!msm_obj->vaddr || WARN_ON(!is_vunmapable(msm_obj)))
		return;

	vunmap(msm_obj->vaddr);
	msm_obj->vaddr = NULL;
}

void msm_gem_vunmap(struct drm_gem_object *obj, enum msm_gem_lock subclass)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	mutex_lock_nested(&msm_obj->lock, subclass);
	msm_gem_vunmap_locked(obj);
	mutex_unlock(&msm_obj->lock);
}

/* must be called before _move_to_active().. */
int msm_gem_sync_object(struct drm_gem_object *obj,
		struct msm_fence_context *fctx, bool exclusive)
{
	struct dma_resv_list *fobj;
	struct dma_fence *fence;
	int i, ret;

	fobj = dma_resv_get_list(obj->resv);
	if (!fobj || (fobj->shared_count == 0)) {
		fence = dma_resv_get_excl(obj->resv);
		/* don't need to wait on our own fences, since ring is fifo */
		if (fence && (fence->context != fctx->context)) {
			ret = dma_fence_wait(fence, true);
			if (ret)
				return ret;
		}
	}

	if (!exclusive || !fobj)
		return 0;

	for (i = 0; i < fobj->shared_count; i++) {
		fence = rcu_dereference_protected(fobj->shared[i],
						dma_resv_held(obj->resv));
		if (fence->context != fctx->context) {
			ret = dma_fence_wait(fence, true);
			if (ret)
				return ret;
		}
	}

	return 0;
}

void msm_gem_active_get(struct drm_gem_object *obj, struct msm_gpu *gpu)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	WARN_ON(!mutex_is_locked(&obj->dev->struct_mutex));
	WARN_ON(msm_obj->madv != MSM_MADV_WILLNEED);

	if (!atomic_fetch_inc(&msm_obj->active_count)) {
		msm_obj->gpu = gpu;
		list_del_init(&msm_obj->mm_list);
		list_add_tail(&msm_obj->mm_list, &gpu->active_list);
	}
}

void msm_gem_active_put(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_drm_private *priv = obj->dev->dev_private;

	WARN_ON(!mutex_is_locked(&obj->dev->struct_mutex));

	if (!atomic_dec_return(&msm_obj->active_count)) {
		msm_obj->gpu = NULL;
		list_del_init(&msm_obj->mm_list);
		list_add_tail(&msm_obj->mm_list, &priv->inactive_list);
	}
}

int msm_gem_cpu_prep(struct drm_gem_object *obj, uint32_t op, ktime_t *timeout)
{
	bool write = !!(op & MSM_PREP_WRITE);
	unsigned long remain =
		op & MSM_PREP_NOSYNC ? 0 : timeout_to_jiffies(timeout);
	long ret;

	ret = dma_resv_wait_timeout_rcu(obj->resv, write,
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
static void describe_fence(struct dma_fence *fence, const char *type,
		struct seq_file *m)
{
	if (!dma_fence_is_signaled(fence))
		seq_printf(m, "\t%9s: %s %s seq %llu\n", type,
				fence->ops->get_driver_name(fence),
				fence->ops->get_timeline_name(fence),
				fence->seqno);
}

void msm_gem_describe(struct drm_gem_object *obj, struct seq_file *m)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct dma_resv *robj = obj->resv;
	struct dma_resv_list *fobj;
	struct dma_fence *fence;
	struct msm_gem_vma *vma;
	uint64_t off = drm_vma_node_start(&obj->vma_node);
	const char *madv;

	mutex_lock(&msm_obj->lock);

	switch (msm_obj->madv) {
	case __MSM_MADV_PURGED:
		madv = " purged";
		break;
	case MSM_MADV_DONTNEED:
		madv = " purgeable";
		break;
	case MSM_MADV_WILLNEED:
	default:
		madv = "";
		break;
	}

	seq_printf(m, "%08x: %c %2d (%2d) %08llx %p",
			msm_obj->flags, is_active(msm_obj) ? 'A' : 'I',
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
				} else {
					comm = NULL;
				}
				name = aspace->name;
			} else {
				name = comm = NULL;
			}
			seq_printf(m, " [%s%s%s: aspace=%p, %08llx,%s,inuse=%d]",
				name, comm ? ":" : "", comm ? comm : "",
				vma->aspace, vma->iova,
				vma->mapped ? "mapped" : "unmapped",
				vma->inuse);
			kfree(comm);
		}

		seq_puts(m, "\n");
	}

	rcu_read_lock();
	fobj = rcu_dereference(robj->fence);
	if (fobj) {
		unsigned int i, shared_count = fobj->shared_count;

		for (i = 0; i < shared_count; i++) {
			fence = rcu_dereference(fobj->shared[i]);
			describe_fence(fence, "Shared", m);
		}
	}

	fence = rcu_dereference(robj->fence_excl);
	if (fence)
		describe_fence(fence, "Exclusive", m);
	rcu_read_unlock();

	mutex_unlock(&msm_obj->lock);
}

void msm_gem_describe_objects(struct list_head *list, struct seq_file *m)
{
	struct msm_gem_object *msm_obj;
	int count = 0;
	size_t size = 0;

	seq_puts(m, "   flags       id ref  offset   kaddr            size     madv      name\n");
	list_for_each_entry(msm_obj, list, mm_list) {
		struct drm_gem_object *obj = &msm_obj->base;
		seq_puts(m, "   ");
		msm_gem_describe(obj, m);
		count++;
		size += obj->size;
	}

	seq_printf(m, "Total %d objects, %zu bytes\n", count, size);
}
#endif

/* don't call directly!  Use drm_gem_object_put_locked() and friends */
void msm_gem_free_object(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct drm_device *dev = obj->dev;
	struct msm_drm_private *priv = dev->dev_private;

	if (llist_add(&msm_obj->freed, &priv->free_list))
		queue_work(priv->wq, &priv->free_work);
}

static void free_object(struct msm_gem_object *msm_obj)
{
	struct drm_gem_object *obj = &msm_obj->base;
	struct drm_device *dev = obj->dev;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	/* object should not be on active list: */
	WARN_ON(is_active(msm_obj));

	list_del(&msm_obj->mm_list);

	mutex_lock(&msm_obj->lock);

	put_iova(obj);

	if (obj->import_attach) {
		WARN_ON(msm_obj->vaddr);

		/* Don't drop the pages for imported dmabuf, as they are not
		 * ours, just free the array we allocated:
		 */
		if (msm_obj->pages)
			kvfree(msm_obj->pages);

		drm_prime_gem_destroy(obj, msm_obj->sgt);
	} else {
		msm_gem_vunmap_locked(obj);
		put_pages(obj);
	}

	drm_gem_object_release(obj);

	mutex_unlock(&msm_obj->lock);
	kfree(msm_obj);
}

void msm_gem_free_work(struct work_struct *work)
{
	struct msm_drm_private *priv =
		container_of(work, struct msm_drm_private, free_work);
	struct drm_device *dev = priv->dev;
	struct llist_node *freed;
	struct msm_gem_object *msm_obj, *next;

	while ((freed = llist_del_all(&priv->free_list))) {

		mutex_lock(&dev->struct_mutex);

		llist_for_each_entry_safe(msm_obj, next,
					  freed, freed)
			free_object(msm_obj);

		mutex_unlock(&dev->struct_mutex);

		if (need_resched())
			break;
	}
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

static int msm_gem_new_impl(struct drm_device *dev,
		uint32_t size, uint32_t flags,
		struct drm_gem_object **obj)
{
	struct msm_gem_object *msm_obj;

	switch (flags & MSM_BO_CACHE_MASK) {
	case MSM_BO_UNCACHED:
	case MSM_BO_CACHED:
	case MSM_BO_WC:
		break;
	default:
		DRM_DEV_ERROR(dev->dev, "invalid cache flag: %x\n",
				(flags & MSM_BO_CACHE_MASK));
		return -EINVAL;
	}

	msm_obj = kzalloc(sizeof(*msm_obj), GFP_KERNEL);
	if (!msm_obj)
		return -ENOMEM;

	mutex_init(&msm_obj->lock);

	msm_obj->flags = flags;
	msm_obj->madv = MSM_MADV_WILLNEED;

	INIT_LIST_HEAD(&msm_obj->submit_entry);
	INIT_LIST_HEAD(&msm_obj->vmas);

	*obj = &msm_obj->base;

	return 0;
}

static struct drm_gem_object *_msm_gem_new(struct drm_device *dev,
		uint32_t size, uint32_t flags, bool struct_mutex_locked)
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

	if (WARN_ON(use_vram && !priv->vram.size))
		return ERR_PTR(-EINVAL);

	/* Disallow zero sized objects as they make the underlying
	 * infrastructure grumpy
	 */
	if (size == 0)
		return ERR_PTR(-EINVAL);

	ret = msm_gem_new_impl(dev, size, flags, &obj);
	if (ret)
		goto fail;

	msm_obj = to_msm_bo(obj);

	if (use_vram) {
		struct msm_gem_vma *vma;
		struct page **pages;

		mutex_lock(&msm_obj->lock);

		vma = add_vma(obj, NULL);
		mutex_unlock(&msm_obj->lock);
		if (IS_ERR(vma)) {
			ret = PTR_ERR(vma);
			goto fail;
		}

		to_msm_bo(obj)->vram_node = &vma->node;

		drm_gem_private_object_init(dev, obj, size);

		pages = get_pages(obj);
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

	if (struct_mutex_locked) {
		WARN_ON(!mutex_is_locked(&dev->struct_mutex));
		list_add_tail(&msm_obj->mm_list, &priv->inactive_list);
	} else {
		mutex_lock(&dev->struct_mutex);
		list_add_tail(&msm_obj->mm_list, &priv->inactive_list);
		mutex_unlock(&dev->struct_mutex);
	}

	return obj;

fail:
	drm_gem_object_put(obj);
	return ERR_PTR(ret);
}

struct drm_gem_object *msm_gem_new_locked(struct drm_device *dev,
		uint32_t size, uint32_t flags)
{
	return _msm_gem_new(dev, size, flags, true);
}

struct drm_gem_object *msm_gem_new(struct drm_device *dev,
		uint32_t size, uint32_t flags)
{
	return _msm_gem_new(dev, size, flags, false);
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
		goto fail;

	drm_gem_private_object_init(dev, obj, size);

	npages = size / PAGE_SIZE;

	msm_obj = to_msm_bo(obj);
	mutex_lock(&msm_obj->lock);
	msm_obj->sgt = sgt;
	msm_obj->pages = kvmalloc_array(npages, sizeof(struct page *), GFP_KERNEL);
	if (!msm_obj->pages) {
		mutex_unlock(&msm_obj->lock);
		ret = -ENOMEM;
		goto fail;
	}

	ret = drm_prime_sg_to_page_addr_arrays(sgt, msm_obj->pages, NULL, npages);
	if (ret) {
		mutex_unlock(&msm_obj->lock);
		goto fail;
	}

	mutex_unlock(&msm_obj->lock);

	mutex_lock(&dev->struct_mutex);
	list_add_tail(&msm_obj->mm_list, &priv->inactive_list);
	mutex_unlock(&dev->struct_mutex);

	return obj;

fail:
	drm_gem_object_put(obj);
	return ERR_PTR(ret);
}

static void *_msm_gem_kernel_new(struct drm_device *dev, uint32_t size,
		uint32_t flags, struct msm_gem_address_space *aspace,
		struct drm_gem_object **bo, uint64_t *iova, bool locked)
{
	void *vaddr;
	struct drm_gem_object *obj = _msm_gem_new(dev, size, flags, locked);
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
	if (locked)
		drm_gem_object_put_locked(obj);
	else
		drm_gem_object_put(obj);

	return ERR_PTR(ret);

}

void *msm_gem_kernel_new(struct drm_device *dev, uint32_t size,
		uint32_t flags, struct msm_gem_address_space *aspace,
		struct drm_gem_object **bo, uint64_t *iova)
{
	return _msm_gem_kernel_new(dev, size, flags, aspace, bo, iova, false);
}

void *msm_gem_kernel_new_locked(struct drm_device *dev, uint32_t size,
		uint32_t flags, struct msm_gem_address_space *aspace,
		struct drm_gem_object **bo, uint64_t *iova)
{
	return _msm_gem_kernel_new(dev, size, flags, aspace, bo, iova, true);
}

void msm_gem_kernel_put(struct drm_gem_object *bo,
		struct msm_gem_address_space *aspace, bool locked)
{
	if (IS_ERR_OR_NULL(bo))
		return;

	msm_gem_put_vaddr(bo);
	msm_gem_unpin_iova(bo, aspace);

	if (locked)
		drm_gem_object_put_locked(bo);
	else
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
