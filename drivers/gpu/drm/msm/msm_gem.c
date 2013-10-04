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

#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_gpu.h"


/* called with dev->struct_mutex held */
static struct page **get_pages(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);

	if (!msm_obj->pages) {
		struct drm_device *dev = obj->dev;
		struct page **p = drm_gem_get_pages(obj, 0);
		int npages = obj->size >> PAGE_SHIFT;

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

		drm_gem_put_pages(obj, msm_obj->pages, true, false);
		msm_obj->pages = NULL;
	}
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

int msm_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	struct drm_device *dev = obj->dev;
	struct page **pages;
	unsigned long pfn;
	pgoff_t pgoff;
	int ret;

	/* Make sure we don't parallel update on a fault, nor move or remove
	 * something from beneath our feet
	 */
	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		goto out;

	/* make sure we have pages attached now */
	pages = get_pages(obj);
	if (IS_ERR(pages)) {
		ret = PTR_ERR(pages);
		goto out_unlock;
	}

	/* We don't use vmf->pgoff since that has the fake offset: */
	pgoff = ((unsigned long)vmf->virtual_address -
			vma->vm_start) >> PAGE_SHIFT;

	pfn = page_to_pfn(msm_obj->pages[pgoff]);

	VERB("Inserting %p pfn %lx, pa %lx", vmf->virtual_address,
			pfn, pfn << PAGE_SHIFT);

	ret = vm_insert_mixed(vma, (unsigned long)vmf->virtual_address, pfn);

out_unlock:
	mutex_unlock(&dev->struct_mutex);
out:
	switch (ret) {
	case -EAGAIN:
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
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
	int ret;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

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
	mutex_lock(&obj->dev->struct_mutex);
	offset = mmap_offset(obj);
	mutex_unlock(&obj->dev->struct_mutex);
	return offset;
}

/* helpers for dealing w/ iommu: */
static int map_range(struct iommu_domain *domain, unsigned int iova,
		struct sg_table *sgt, unsigned int len, int prot)
{
	struct scatterlist *sg;
	unsigned int da = iova;
	unsigned int i, j;
	int ret;

	if (!domain || !sgt)
		return -EINVAL;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		u32 pa = sg_phys(sg) - sg->offset;
		size_t bytes = sg->length + sg->offset;

		VERB("map[%d]: %08x %08x(%x)", i, iova, pa, bytes);

		ret = iommu_map(domain, da, pa, bytes, prot);
		if (ret)
			goto fail;

		da += bytes;
	}

	return 0;

fail:
	da = iova;

	for_each_sg(sgt->sgl, sg, i, j) {
		size_t bytes = sg->length + sg->offset;
		iommu_unmap(domain, da, bytes);
		da += bytes;
	}
	return ret;
}

static void unmap_range(struct iommu_domain *domain, unsigned int iova,
		struct sg_table *sgt, unsigned int len)
{
	struct scatterlist *sg;
	unsigned int da = iova;
	int i;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		size_t bytes = sg->length + sg->offset;
		size_t unmapped;

		unmapped = iommu_unmap(domain, da, bytes);
		if (unmapped < bytes)
			break;

		VERB("unmap[%d]: %08x(%x)", i, iova, bytes);

		BUG_ON(!IS_ALIGNED(bytes, PAGE_SIZE));

		da += bytes;
	}
}

/* should be called under struct_mutex.. although it can be called
 * from atomic context without struct_mutex to acquire an extra
 * iova ref if you know one is already held.
 *
 * That means when I do eventually need to add support for unpinning
 * the refcnt counter needs to be atomic_t.
 */
int msm_gem_get_iova_locked(struct drm_gem_object *obj, int id,
		uint32_t *iova)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	int ret = 0;

	if (!msm_obj->domain[id].iova) {
		struct msm_drm_private *priv = obj->dev->dev_private;
		uint32_t offset = (uint32_t)mmap_offset(obj);
		struct page **pages;
		pages = get_pages(obj);
		if (IS_ERR(pages))
			return PTR_ERR(pages);
		// XXX ideally we would not map buffers writable when not needed...
		ret = map_range(priv->iommus[id], offset, msm_obj->sgt,
				obj->size, IOMMU_READ | IOMMU_WRITE);
		msm_obj->domain[id].iova = offset;
	}

	if (!ret)
		*iova = msm_obj->domain[id].iova;

	return ret;
}

int msm_gem_get_iova(struct drm_gem_object *obj, int id, uint32_t *iova)
{
	int ret;
	mutex_lock(&obj->dev->struct_mutex);
	ret = msm_gem_get_iova_locked(obj, id, iova);
	mutex_unlock(&obj->dev->struct_mutex);
	return ret;
}

void msm_gem_put_iova(struct drm_gem_object *obj, int id)
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
	obj = drm_gem_object_lookup(dev, file, handle);
	if (obj == NULL) {
		ret = -ENOENT;
		goto fail;
	}

	*offset = msm_gem_mmap_offset(obj);

	drm_gem_object_unreference_unlocked(obj);

fail:
	return ret;
}

void *msm_gem_vaddr_locked(struct drm_gem_object *obj)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	WARN_ON(!mutex_is_locked(&obj->dev->struct_mutex));
	if (!msm_obj->vaddr) {
		struct page **pages = get_pages(obj);
		if (IS_ERR(pages))
			return ERR_CAST(pages);
		msm_obj->vaddr = vmap(pages, obj->size >> PAGE_SHIFT,
				VM_MAP, pgprot_writecombine(PAGE_KERNEL));
	}
	return msm_obj->vaddr;
}

void *msm_gem_vaddr(struct drm_gem_object *obj)
{
	void *ret;
	mutex_lock(&obj->dev->struct_mutex);
	ret = msm_gem_vaddr_locked(obj);
	mutex_unlock(&obj->dev->struct_mutex);
	return ret;
}

int msm_gem_queue_inactive_work(struct drm_gem_object *obj,
		struct work_struct *work)
{
	struct drm_device *dev = obj->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	int ret = 0;

	mutex_lock(&dev->struct_mutex);
	if (!list_empty(&work->entry)) {
		ret = -EINVAL;
	} else if (is_active(msm_obj)) {
		list_add_tail(&work->entry, &msm_obj->inactive_work);
	} else {
		queue_work(priv->wq, work);
	}
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

void msm_gem_move_to_active(struct drm_gem_object *obj,
		struct msm_gpu *gpu, bool write, uint32_t fence)
{
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	msm_obj->gpu = gpu;
	if (write)
		msm_obj->write_fence = fence;
	else
		msm_obj->read_fence = fence;
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
	msm_obj->read_fence = 0;
	msm_obj->write_fence = 0;
	list_del_init(&msm_obj->mm_list);
	list_add_tail(&msm_obj->mm_list, &priv->inactive_list);

	while (!list_empty(&msm_obj->inactive_work)) {
		struct work_struct *work;

		work = list_first_entry(&msm_obj->inactive_work,
				struct work_struct, entry);

		list_del_init(&work->entry);
		queue_work(priv->wq, work);
	}
}

int msm_gem_cpu_prep(struct drm_gem_object *obj, uint32_t op,
		struct timespec *timeout)
{
	struct drm_device *dev = obj->dev;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	int ret = 0;

	if (is_active(msm_obj)) {
		uint32_t fence = 0;

		if (op & MSM_PREP_READ)
			fence = msm_obj->write_fence;
		if (op & MSM_PREP_WRITE)
			fence = max(fence, msm_obj->read_fence);
		if (op & MSM_PREP_NOSYNC)
			timeout = NULL;

		ret = msm_wait_fence_interruptable(dev, fence, timeout);
	}

	/* TODO cache maintenance */

	return ret;
}

int msm_gem_cpu_fini(struct drm_gem_object *obj)
{
	/* TODO cache maintenance */
	return 0;
}

#ifdef CONFIG_DEBUG_FS
void msm_gem_describe(struct drm_gem_object *obj, struct seq_file *m)
{
	struct drm_device *dev = obj->dev;
	struct msm_gem_object *msm_obj = to_msm_bo(obj);
	uint64_t off = drm_vma_node_start(&obj->vma_node);

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));
	seq_printf(m, "%08x: %c(r=%u,w=%u) %2d (%2d) %08llx %p %d\n",
			msm_obj->flags, is_active(msm_obj) ? 'A' : 'I',
			msm_obj->read_fence, msm_obj->write_fence,
			obj->name, obj->refcount.refcount.counter,
			off, msm_obj->vaddr, obj->size);
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
	int id;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	/* object should not be on active list: */
	WARN_ON(is_active(msm_obj));

	list_del(&msm_obj->mm_list);

	for (id = 0; id < ARRAY_SIZE(msm_obj->domain); id++) {
		if (msm_obj->domain[id].iova) {
			struct msm_drm_private *priv = obj->dev->dev_private;
			uint32_t offset = (uint32_t)mmap_offset(obj);
			unmap_range(priv->iommus[id], offset,
					msm_obj->sgt, obj->size);
		}
	}

	drm_gem_free_mmap_offset(obj);

	if (msm_obj->vaddr)
		vunmap(msm_obj->vaddr);

	put_pages(obj);

	if (msm_obj->resv == &msm_obj->_resv)
		reservation_object_fini(msm_obj->resv);

	drm_gem_object_release(obj);

	kfree(msm_obj);
}

/* convenience method to construct a GEM buffer object, and userspace handle */
int msm_gem_new_handle(struct drm_device *dev, struct drm_file *file,
		uint32_t size, uint32_t flags, uint32_t *handle)
{
	struct drm_gem_object *obj;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	obj = msm_gem_new(dev, size, flags);

	mutex_unlock(&dev->struct_mutex);

	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = drm_gem_handle_create(file, obj, handle);

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(obj);

	return ret;
}

struct drm_gem_object *msm_gem_new(struct drm_device *dev,
		uint32_t size, uint32_t flags)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_gem_object *msm_obj;
	struct drm_gem_object *obj = NULL;
	int ret;

	WARN_ON(!mutex_is_locked(&dev->struct_mutex));

	size = PAGE_ALIGN(size);

	switch (flags & MSM_BO_CACHE_MASK) {
	case MSM_BO_UNCACHED:
	case MSM_BO_CACHED:
	case MSM_BO_WC:
		break;
	default:
		dev_err(dev->dev, "invalid cache flag: %x\n",
				(flags & MSM_BO_CACHE_MASK));
		ret = -EINVAL;
		goto fail;
	}

	msm_obj = kzalloc(sizeof(*msm_obj), GFP_KERNEL);
	if (!msm_obj) {
		ret = -ENOMEM;
		goto fail;
	}

	obj = &msm_obj->base;

	ret = drm_gem_object_init(dev, obj, size);
	if (ret)
		goto fail;

	msm_obj->flags = flags;

	msm_obj->resv = &msm_obj->_resv;
	reservation_object_init(msm_obj->resv);

	INIT_LIST_HEAD(&msm_obj->submit_entry);
	INIT_LIST_HEAD(&msm_obj->inactive_work);
	list_add_tail(&msm_obj->mm_list, &priv->inactive_list);

	return obj;

fail:
	if (obj)
		drm_gem_object_unreference_unlocked(obj);

	return ERR_PTR(ret);
}
