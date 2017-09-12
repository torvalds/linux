/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/spinlock.h>
#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>
#include <linux/pfn_t.h>

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
			dev_err(dev->dev, "could not get pages: %ld\n",
					PTR_ERR(p));
			return p;
		}

		msm_obj->sgt = drm_prime_pages_to_sg(p, npages);
		if (IS_ERR(msm_obj->sgt)) {
			dev_err(dev->dev, "failed to allocate sgt\n");
			return ERR_CAST(msm_obj->sgt);
		}

		msm_obj->pages = p;

		/* For non-cached buffers, ensure the new pages are clean
		 * because display controller, GPU, etc. are not coherent:
		 */
		if (msm_obj->flags & (MSM_BO_WC|MSM_BO_UNCACHED))
			dma_map_sg(dev->dev, msm_obj->sgt->sgl,
					msm_obj->sgt->nents, DMA_BIDIRECTIONAL);
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
		/* For non-cached buffers, ensure the new pages are clean
		 * because display controller, GPU, etc. are not coherent:
		 */
		if (msm_obj->flags & (MSM_BO_WC|MSM_BO_UNCACHED))
			dma_unmap_sg(obj->dev->dev, msm_obj->sgt->sgl,
					msm_obj->sgt->nents, DMA_BIDIRECTIONAL);
		sg_free_table(msm_obj->sgt);
		kfree(msm_obj->sgt);

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

int msm_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct page **pages;
	unsigned long pfn;
	pgoff_t pgoff;
	int ret;

	/*
	 * vm_ops.open/drm_gem_mmap_obj and close get and put
	 * a reference on obj. So, we dont need to hold one here.
	 */
	ret = mutex_lock_interruptible(&msm_obj->lock);
	if (ret)
		goto out;

	if (WARN_ON(msm_obj->madv != MSM_MADV_WILLNEED)) {
		mutex_unlock(&msm_obj->lock);
		return VM_FAULT_SIGBUS;
	}

	/* make sure we have pages attached now */
	pages = get_pages(obj);
	if (IS_ERR(pages)) {
		ret = PTR_ERR(pages);
		goto out_unlock;
	}

	/* We don't use vmf->pgoff since that has the fake offset: */
	pgoff = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	pfn = page_to_pfn(pages[pgoff]);

	VERB("Inserting %p pfn %lx, pa %lx", (void *)vmf->address,
			pfn, pfn << PAGE_SHIFT);

	ret = vm_insert_mixed(vma, vmf->address, __pfn_to_pfn_t(pfn, PFN_DEV));

out_unlock:
	mutex_unlock(&msm_obj->lock);
out:
	switch (ret) {
	case -EAGAIN:
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
	case -EBUSY:
		/*
		 * EBUSY is ok: this just means that another thread
		 * already did the job.
		 */
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
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
		dev_err(dev->dev, "could not allocate mmap offset\n");
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
		msm_gem_unmap_vma(vma->aspace, vma, msm_obj->sgt);
		del_vma(vma);
	}
}

/* get iova, taking a reference.  Should have a matching put */
int msm_gem_get_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace, uint64_t *iova)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct msm_gem_vma *vma;
	int ret = 0;

	mutex_lock(&msm_obj->lock);

	if (WARN_ON(msm_obj->madv != MSM_MADV_WILLNEED)) {
		mutex_unlock(&msm_obj->lock);
		return -EBUSY;
	}

	vma = lookup_vma(obj, aspace);

	if (!vma) {
		struct page **pages;

		vma = add_vma(obj, aspace);
		if (IS_ERR(vma)) {
			ret = PTR_ERR(vma);
			goto unlock;
		}

		pages = get_pages(obj);
		if (IS_ERR(pages)) {
			ret = PTR_ERR(pages);
			goto fail;
		}

		ret = msm_gem_map_vma(aspace, vma, msm_obj->sgt,
				obj->size >> PAGE_SHIFT);
		if (ret)
			goto fail;
	}

	*iova = vma->iova;

	mutex_unlock(&msm_obj->lock);
	return 0;

fail:
	del_vma(vma);
unlock:
	mutex_unlock(&msm_obj->lock);
	return ret;
}

/* get iova without taking a reference, used in places where you have
 * already done a 'msm_gem_get_iova()'.
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

void msm_gem_put_iova(struct drm_gem_object *obj,
		struct msm_gem_address_space *aspace)
{
	// XXX TODO ..
	// NOTE: probably don't need a _locked() version.. we wouldn't
	// normally unmap here, but instead just mark that it could be
	// unmapped (if the iova refcnt drops to zero), but then later
	// if another _get_iova_locked() fails we can start unmapping
	// things that are no longer needed..
}

int msm_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
		struct drm_mode_create_dumb *args)
{
	args->pitch = align_pitch(args->width, args->bpp);
	args->size  = PAGE_ALIGN(args->pitch * args->height);
	return msm_gem_new_handle(dev, file, args->size,
			MSM_BO_SCANOUT | MSM_BO_WC, &args->handle);
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

	drm_gem_object_unreference_unlocked(obj);

fail:
	return ret;
}

void *msm_gem_get_vaddr(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	int ret = 0;

	mutex_lock(&msm_obj->lock);

	if (WARN_ON(msm_obj->madv != MSM_MADV_WILLNEED)) {
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
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct reservation_object_list *fobj;
	struct dma_fence *fence;
	int i, ret;

	fobj = reservation_object_get_list(msm_obj->resv);
	if (!fobj || (fobj->shared_count == 0)) {
		fence = reservation_object_get_excl(msm_obj->resv);
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
						reservation_object_held(msm_obj->resv));
		if (fence->context != fctx->context) {
			ret = dma_fence_wait(fence, true);
			if (ret)
				return ret;
		}
	}

	return 0;
}

void msm_gem_move_to_active(struct drm_gem_object *obj,
		struct msm_gpu *gpu, bool exclusive, struct dma_fence *fence)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	WARN_ON(msm_obj->madv != MSM_MADV_WILLNEED);
	msm_obj->gpu = gpu;
	if (exclusive)
		reservation_object_add_excl_fence(msm_obj->resv, fence);
	else
		reservation_object_add_shared_fence(msm_obj->resv, fence);
	list_del_init(&msm_obj->mm_list);
	list_add_tail(&msm_obj->mm_list, &gpu->active_list);
}

void msm_gem_move_to_inactive(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	msm_obj->gpu = NULL;
	list_del_init(&msm_obj->mm_list);
	list_add_tail(&msm_obj->mm_list, &priv->inactive_list);
}

int msm_gem_cpu_prep(struct drm_gem_object *obj, uint32_t op, ktime_t *timeout)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	bool write = !!(op & MSM_PREP_WRITE);
	unsigned long remain =
		op & MSM_PREP_NOSYNC ? 0 : timeout_to_jiffies(timeout);
	long ret;

	ret = reservation_object_wait_timeout_rcu(msm_obj->resv, write,
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
		seq_printf(m, "\t%9s: %s %s seq %u\n", type,
				fence->ops->get_driver_name(fence),
				fence->ops->get_timeline_name(fence),
				fence->seqno);
}

void msm_gem_describe(struct drm_gem_object *obj, struct seq_file *m)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct reservation_object *robj = msm_obj->resv;
	struct reservation_object_list *fobj;
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

	seq_printf(m, "%08x: %c %2d (%2d) %08llx %p\t",
			msm_obj->flags, is_active(msm_obj) ? 'A' : 'I',
			obj->name, kref_read(&obj->refcount),
			off, msm_obj->vaddr);

	/* FIXME: we need to print the address space here too */
	list_for_each_entry(vma, &msm_obj->vmas, list)
		seq_printf(m, " %08llx", vma->iova);

	seq_printf(m, " %zu%s\n", obj->size, madv);

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

	list_for_each_entry(msm_obj, list, mm_list) {
		struct drm_gem_object *obj = &msm_obj->base;
		seq_printf(m, "   ");
		msm_gem_describe(obj, m);
		count++;
		size += obj->size;
	}

	seq_printf(m, "Total %d objects, %zu bytes\n", count, size);
}
#endif

void msm_gem_free_object(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	/* object should not be on active list: */
	WARN_ON(is_active(msm_obj));

	list_del(&msm_obj->mm_list);

	mutex_lock(&msm_obj->lock);

	put_iova(obj);

	if (obj->import_attach) {
		if (msm_obj->vaddr)
			dma_buf_vunmap(obj->import_attach->dmabuf, msm_obj->vaddr);

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

	if (msm_obj->resv == &msm_obj->_resv)
		reservation_object_fini(msm_obj->resv);

	drm_gem_object_release(obj);

	mutex_unlock(&msm_obj->lock);
	kfree(msm_obj);
}

/* convenience method to construct a GEM buffer object, and userspace handle */
int msm_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		uint32_t size, uint32_t flags, uint32_t *handle)
{
	struct drm_gem_object *obj;
	int ret;

	obj = msm_gem_new(dev, size, flags);

	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = drm_gem_handle_create(file, obj, handle);

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

static int msm_gem_new_impl(struct drm_device *dev,
		uint32_t size, uint32_t flags,
		struct reservation_object *resv,
		struct drm_gem_object **obj,
		bool struct_mutex_locked)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gem_object *msm_obj;

	switch (flags & MSM_BO_CACHE_MASK) {
	case MSM_BO_UNCACHED:
	case MSM_BO_CACHED:
	case MSM_BO_WC:
		break;
	default:
		dev_err(dev->dev, "invalid cache flag: %x\n",
				(flags & MSM_BO_CACHE_MASK));
		return -EINVAL;
	}

	msm_obj = kzalloc(sizeof(*msm_obj), GFP_KERNEL);
	if (!msm_obj)
		return -ENOMEM;

	mutex_init(&msm_obj->lock);

	msm_obj->flags = flags;
	msm_obj->madv = MSM_MADV_WILLNEED;

	if (resv) {
		msm_obj->resv = resv;
	} else {
		msm_obj->resv = &msm_obj->_resv;
		reservation_object_init(msm_obj->resv);
	}

	INIT_LIST_HEAD(&msm_obj->submit_entry);
	INIT_LIST_HEAD(&msm_obj->vmas);

	if (struct_mutex_locked) {
		WARN_ON(!mutex_is_locked(&dev->struct_mutex));
		list_add_tail(&msm_obj->mm_list, &priv->inactive_list);
	} else {
		mutex_lock(&dev->struct_mutex);
		list_add_tail(&msm_obj->mm_list, &priv->inactive_list);
		mutex_unlock(&dev->struct_mutex);
	}

	*obj = &msm_obj->base;

	return 0;
}

static struct drm_gem_object *_msm_gem_new(struct drm_device *dev,
		uint32_t size, uint32_t flags, bool struct_mutex_locked)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct drm_gem_object *obj = NULL;
	bool use_vram = false;
	int ret;

	size = PAGE_ALIGN(size);

	if (!iommu_present(&platform_bus_type))
		use_vram = true;
	else if ((flags & MSM_BO_STOLEN) && priv->vram.size)
		use_vram = true;

	if (WARN_ON(use_vram && !priv->vram.size))
		return ERR_PTR(-EINVAL);

	/* Disallow zero sized objects as they make the underlying
	 * infrastructure grumpy
	 */
	if (size == 0)
		return ERR_PTR(-EINVAL);

	ret = msm_gem_new_impl(dev, size, flags, NULL, &obj, struct_mutex_locked);
	if (ret)
		goto fail;

	if (use_vram) {
		struct msm_gem_vma *vma;
		struct page **pages;
		struct msm_gem_object *msm_obj = to_msm_bo(obj);

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
	}

	return obj;

fail:
	drm_gem_object_unreference_unlocked(obj);
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
	struct msm_gem_object *msm_obj;
	struct drm_gem_object *obj;
	uint32_t size;
	int ret, npages;

	/* if we don't have IOMMU, don't bother pretending we can import: */
	if (!iommu_present(&platform_bus_type)) {
		dev_err(dev->dev, "cannot import without IOMMU\n");
		return ERR_PTR(-EINVAL);
	}

	size = PAGE_ALIGN(dmabuf->size);

	ret = msm_gem_new_impl(dev, size, MSM_BO_WC, dmabuf->resv, &obj, false);
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
	return obj;

fail:
	drm_gem_object_unreference_unlocked(obj);
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
		ret = msm_gem_get_iova(obj, aspace, iova);
		if (ret) {
			drm_gem_object_unreference(obj);
			return ERR_PTR(ret);
		}
	}

	vaddr = msm_gem_get_vaddr(obj);
	if (!vaddr) {
		msm_gem_put_iova(obj, aspace);
		drm_gem_object_unreference(obj);
		return ERR_PTR(-ENOMEM);
	}

	if (bo)
		*bo = obj;

	return vaddr;
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
