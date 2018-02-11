/*
 * Copyright (C) 2015 Etnaviv Project
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
#include <linux/sched/mm.h>
#include <linux/sched/task.h>

#include "etnaviv_drv.h"
#include "etnaviv_gem.h"
#include "etnaviv_gpu.h"
#include "etnaviv_mmu.h"

static struct lock_class_key etnaviv_shm_lock_class;
static struct lock_class_key etnaviv_userptr_lock_class;

static void etnaviv_gem_scatter_map(struct etnaviv_gem_object *etnaviv_obj)
{
	struct drm_device *dev = etnaviv_obj->base.dev;
	struct sg_table *sgt = etnaviv_obj->sgt;

	/*
	 * For non-cached buffers, ensure the new pages are clean
	 * because display controller, GPU, etc. are not coherent.
	 */
	if (etnaviv_obj->flags & ETNA_BO_CACHE_MASK)
		dma_map_sg(dev->dev, sgt->sgl, sgt->nents, DMA_BIDIRECTIONAL);
}

static void etnaviv_gem_scatterlist_unmap(struct etnaviv_gem_object *etnaviv_obj)
{
	struct drm_device *dev = etnaviv_obj->base.dev;
	struct sg_table *sgt = etnaviv_obj->sgt;

	/*
	 * For non-cached buffers, ensure the new pages are clean
	 * because display controller, GPU, etc. are not coherent:
	 *
	 * WARNING: The DMA API does not support concurrent CPU
	 * and device access to the memory area.  With BIDIRECTIONAL,
	 * we will clean the cache lines which overlap the region,
	 * and invalidate all cache lines (partially) contained in
	 * the region.
	 *
	 * If you have dirty data in the overlapping cache lines,
	 * that will corrupt the GPU-written data.  If you have
	 * written into the remainder of the region, this can
	 * discard those writes.
	 */
	if (etnaviv_obj->flags & ETNA_BO_CACHE_MASK)
		dma_unmap_sg(dev->dev, sgt->sgl, sgt->nents, DMA_BIDIRECTIONAL);
}

/* called with etnaviv_obj->lock held */
static int etnaviv_gem_shmem_get_pages(struct etnaviv_gem_object *etnaviv_obj)
{
	struct drm_device *dev = etnaviv_obj->base.dev;
	struct page **p = drm_gem_get_pages(&etnaviv_obj->base);

	if (IS_ERR(p)) {
		dev_dbg(dev->dev, "could not get pages: %ld\n", PTR_ERR(p));
		return PTR_ERR(p);
	}

	etnaviv_obj->pages = p;

	return 0;
}

static void put_pages(struct etnaviv_gem_object *etnaviv_obj)
{
	if (etnaviv_obj->sgt) {
		etnaviv_gem_scatterlist_unmap(etnaviv_obj);
		sg_free_table(etnaviv_obj->sgt);
		kfree(etnaviv_obj->sgt);
		etnaviv_obj->sgt = NULL;
	}
	if (etnaviv_obj->pages) {
		drm_gem_put_pages(&etnaviv_obj->base, etnaviv_obj->pages,
				  true, false);

		etnaviv_obj->pages = NULL;
	}
}

struct page **etnaviv_gem_get_pages(struct etnaviv_gem_object *etnaviv_obj)
{
	int ret;

	lockdep_assert_held(&etnaviv_obj->lock);

	if (!etnaviv_obj->pages) {
		ret = etnaviv_obj->ops->get_pages(etnaviv_obj);
		if (ret < 0)
			return ERR_PTR(ret);
	}

	if (!etnaviv_obj->sgt) {
		struct drm_device *dev = etnaviv_obj->base.dev;
		int npages = etnaviv_obj->base.size >> PAGE_SHIFT;
		struct sg_table *sgt;

		sgt = drm_prime_pages_to_sg(etnaviv_obj->pages, npages);
		if (IS_ERR(sgt)) {
			dev_err(dev->dev, "failed to allocate sgt: %ld\n",
				PTR_ERR(sgt));
			return ERR_CAST(sgt);
		}

		etnaviv_obj->sgt = sgt;

		etnaviv_gem_scatter_map(etnaviv_obj);
	}

	return etnaviv_obj->pages;
}

void etnaviv_gem_put_pages(struct etnaviv_gem_object *etnaviv_obj)
{
	lockdep_assert_held(&etnaviv_obj->lock);
	/* when we start tracking the pin count, then do something here */
}

static int etnaviv_gem_mmap_obj(struct etnaviv_gem_object *etnaviv_obj,
		struct vm_area_struct *vma)
{
	pgprot_t vm_page_prot;

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;

	vm_page_prot = vm_get_page_prot(vma->vm_flags);

	if (etnaviv_obj->flags & ETNA_BO_WC) {
		vma->vm_page_prot = pgprot_writecombine(vm_page_prot);
	} else if (etnaviv_obj->flags & ETNA_BO_UNCACHED) {
		vma->vm_page_prot = pgprot_noncached(vm_page_prot);
	} else {
		/*
		 * Shunt off cached objs to shmem file so they have their own
		 * address_space (so unmap_mapping_range does what we want,
		 * in particular in the case of mmap'd dmabufs)
		 */
		fput(vma->vm_file);
		get_file(etnaviv_obj->base.filp);
		vma->vm_pgoff = 0;
		vma->vm_file  = etnaviv_obj->base.filp;

		vma->vm_page_prot = vm_page_prot;
	}

	return 0;
}

int etnaviv_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct etnaviv_gem_object *obj;
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret) {
		DBG("mmap failed: %d", ret);
		return ret;
	}

	obj = to_etnaviv_bo(vma->vm_private_data);
	return obj->ops->mmap(obj, vma);
}

int etnaviv_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);
	struct page **pages, *page;
	pgoff_t pgoff;
	int ret;

	/*
	 * Make sure we don't parallel update on a fault, nor move or remove
	 * something from beneath our feet.  Note that vm_insert_page() is
	 * specifically coded to take care of this, so we don't have to.
	 */
	ret = mutex_lock_interruptible(&etnaviv_obj->lock);
	if (ret)
		goto out;

	/* make sure we have pages attached now */
	pages = etnaviv_gem_get_pages(etnaviv_obj);
	mutex_unlock(&etnaviv_obj->lock);

	if (IS_ERR(pages)) {
		ret = PTR_ERR(pages);
		goto out;
	}

	/* We don't use vmf->pgoff since that has the fake offset: */
	pgoff = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	page = pages[pgoff];

	VERB("Inserting %p pfn %lx, pa %lx", (void *)vmf->address,
	     page_to_pfn(page), page_to_pfn(page) << PAGE_SHIFT);

	ret = vm_insert_page(vma, vmf->address, page);

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

int etnaviv_gem_mmap_offset(struct drm_gem_object *obj, u64 *offset)
{
	int ret;

	/* Make it mmapable */
	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		dev_err(obj->dev->dev, "could not allocate mmap offset\n");
	else
		*offset = drm_vma_node_offset_addr(&obj->vma_node);

	return ret;
}

static struct etnaviv_vram_mapping *
etnaviv_gem_get_vram_mapping(struct etnaviv_gem_object *obj,
			     struct etnaviv_iommu *mmu)
{
	struct etnaviv_vram_mapping *mapping;

	list_for_each_entry(mapping, &obj->vram_list, obj_node) {
		if (mapping->mmu == mmu)
			return mapping;
	}

	return NULL;
}

void etnaviv_gem_mapping_reference(struct etnaviv_vram_mapping *mapping)
{
	struct etnaviv_gem_object *etnaviv_obj = mapping->object;

	drm_gem_object_get(&etnaviv_obj->base);

	mutex_lock(&etnaviv_obj->lock);
	WARN_ON(mapping->use == 0);
	mapping->use += 1;
	mutex_unlock(&etnaviv_obj->lock);
}

void etnaviv_gem_mapping_unreference(struct etnaviv_vram_mapping *mapping)
{
	struct etnaviv_gem_object *etnaviv_obj = mapping->object;

	mutex_lock(&etnaviv_obj->lock);
	WARN_ON(mapping->use == 0);
	mapping->use -= 1;
	mutex_unlock(&etnaviv_obj->lock);

	drm_gem_object_put_unlocked(&etnaviv_obj->base);
}

struct etnaviv_vram_mapping *etnaviv_gem_mapping_get(
	struct drm_gem_object *obj, struct etnaviv_gpu *gpu)
{
	struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);
	struct etnaviv_vram_mapping *mapping;
	struct page **pages;
	int ret = 0;

	mutex_lock(&etnaviv_obj->lock);
	mapping = etnaviv_gem_get_vram_mapping(etnaviv_obj, gpu->mmu);
	if (mapping) {
		/*
		 * Holding the object lock prevents the use count changing
		 * beneath us.  If the use count is zero, the MMU might be
		 * reaping this object, so take the lock and re-check that
		 * the MMU owns this mapping to close this race.
		 */
		if (mapping->use == 0) {
			mutex_lock(&gpu->mmu->lock);
			if (mapping->mmu == gpu->mmu)
				mapping->use += 1;
			else
				mapping = NULL;
			mutex_unlock(&gpu->mmu->lock);
			if (mapping)
				goto out;
		} else {
			mapping->use += 1;
			goto out;
		}
	}

	pages = etnaviv_gem_get_pages(etnaviv_obj);
	if (IS_ERR(pages)) {
		ret = PTR_ERR(pages);
		goto out;
	}

	/*
	 * See if we have a reaped vram mapping we can re-use before
	 * allocating a fresh mapping.
	 */
	mapping = etnaviv_gem_get_vram_mapping(etnaviv_obj, NULL);
	if (!mapping) {
		mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
		if (!mapping) {
			ret = -ENOMEM;
			goto out;
		}

		INIT_LIST_HEAD(&mapping->scan_node);
		mapping->object = etnaviv_obj;
	} else {
		list_del(&mapping->obj_node);
	}

	mapping->mmu = gpu->mmu;
	mapping->use = 1;

	ret = etnaviv_iommu_map_gem(gpu->mmu, etnaviv_obj, gpu->memory_base,
				    mapping);
	if (ret < 0)
		kfree(mapping);
	else
		list_add_tail(&mapping->obj_node, &etnaviv_obj->vram_list);

out:
	mutex_unlock(&etnaviv_obj->lock);

	if (ret)
		return ERR_PTR(ret);

	/* Take a reference on the object */
	drm_gem_object_get(obj);
	return mapping;
}

void *etnaviv_gem_vmap(struct drm_gem_object *obj)
{
	struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);

	if (etnaviv_obj->vaddr)
		return etnaviv_obj->vaddr;

	mutex_lock(&etnaviv_obj->lock);
	/*
	 * Need to check again, as we might have raced with another thread
	 * while waiting for the mutex.
	 */
	if (!etnaviv_obj->vaddr)
		etnaviv_obj->vaddr = etnaviv_obj->ops->vmap(etnaviv_obj);
	mutex_unlock(&etnaviv_obj->lock);

	return etnaviv_obj->vaddr;
}

static void *etnaviv_gem_vmap_impl(struct etnaviv_gem_object *obj)
{
	struct page **pages;

	lockdep_assert_held(&obj->lock);

	pages = etnaviv_gem_get_pages(obj);
	if (IS_ERR(pages))
		return NULL;

	return vmap(pages, obj->base.size >> PAGE_SHIFT,
			VM_MAP, pgprot_writecombine(PAGE_KERNEL));
}

static inline enum dma_data_direction etnaviv_op_to_dma_dir(u32 op)
{
	if (op & ETNA_PREP_READ)
		return DMA_FROM_DEVICE;
	else if (op & ETNA_PREP_WRITE)
		return DMA_TO_DEVICE;
	else
		return DMA_BIDIRECTIONAL;
}

int etnaviv_gem_cpu_prep(struct drm_gem_object *obj, u32 op,
		struct timespec *timeout)
{
	struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);
	struct drm_device *dev = obj->dev;
	bool write = !!(op & ETNA_PREP_WRITE);
	int ret;

	if (!etnaviv_obj->sgt) {
		void *ret;

		mutex_lock(&etnaviv_obj->lock);
		ret = etnaviv_gem_get_pages(etnaviv_obj);
		mutex_unlock(&etnaviv_obj->lock);
		if (IS_ERR(ret))
			return PTR_ERR(ret);
	}

	if (op & ETNA_PREP_NOSYNC) {
		if (!reservation_object_test_signaled_rcu(etnaviv_obj->resv,
							  write))
			return -EBUSY;
	} else {
		unsigned long remain = etnaviv_timeout_to_jiffies(timeout);

		ret = reservation_object_wait_timeout_rcu(etnaviv_obj->resv,
							  write, true, remain);
		if (ret <= 0)
			return ret == 0 ? -ETIMEDOUT : ret;
	}

	if (etnaviv_obj->flags & ETNA_BO_CACHED) {
		dma_sync_sg_for_cpu(dev->dev, etnaviv_obj->sgt->sgl,
				    etnaviv_obj->sgt->nents,
				    etnaviv_op_to_dma_dir(op));
		etnaviv_obj->last_cpu_prep_op = op;
	}

	return 0;
}

int etnaviv_gem_cpu_fini(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);

	if (etnaviv_obj->flags & ETNA_BO_CACHED) {
		/* fini without a prep is almost certainly a userspace error */
		WARN_ON(etnaviv_obj->last_cpu_prep_op == 0);
		dma_sync_sg_for_device(dev->dev, etnaviv_obj->sgt->sgl,
			etnaviv_obj->sgt->nents,
			etnaviv_op_to_dma_dir(etnaviv_obj->last_cpu_prep_op));
		etnaviv_obj->last_cpu_prep_op = 0;
	}

	return 0;
}

int etnaviv_gem_wait_bo(struct etnaviv_gpu *gpu, struct drm_gem_object *obj,
	struct timespec *timeout)
{
	struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);

	return etnaviv_gpu_wait_obj_inactive(gpu, etnaviv_obj, timeout);
}

#ifdef CONFIG_DEBUG_FS
static void etnaviv_gem_describe_fence(struct dma_fence *fence,
	const char *type, struct seq_file *m)
{
	if (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		seq_printf(m, "\t%9s: %s %s seq %u\n",
			   type,
			   fence->ops->get_driver_name(fence),
			   fence->ops->get_timeline_name(fence),
			   fence->seqno);
}

static void etnaviv_gem_describe(struct drm_gem_object *obj, struct seq_file *m)
{
	struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);
	struct reservation_object *robj = etnaviv_obj->resv;
	struct reservation_object_list *fobj;
	struct dma_fence *fence;
	unsigned long off = drm_vma_node_start(&obj->vma_node);

	seq_printf(m, "%08x: %c %2d (%2d) %08lx %p %zd\n",
			etnaviv_obj->flags, is_active(etnaviv_obj) ? 'A' : 'I',
			obj->name, kref_read(&obj->refcount),
			off, etnaviv_obj->vaddr, obj->size);

	rcu_read_lock();
	fobj = rcu_dereference(robj->fence);
	if (fobj) {
		unsigned int i, shared_count = fobj->shared_count;

		for (i = 0; i < shared_count; i++) {
			fence = rcu_dereference(fobj->shared[i]);
			etnaviv_gem_describe_fence(fence, "Shared", m);
		}
	}

	fence = rcu_dereference(robj->fence_excl);
	if (fence)
		etnaviv_gem_describe_fence(fence, "Exclusive", m);
	rcu_read_unlock();
}

void etnaviv_gem_describe_objects(struct etnaviv_drm_private *priv,
	struct seq_file *m)
{
	struct etnaviv_gem_object *etnaviv_obj;
	int count = 0;
	size_t size = 0;

	mutex_lock(&priv->gem_lock);
	list_for_each_entry(etnaviv_obj, &priv->gem_list, gem_node) {
		struct drm_gem_object *obj = &etnaviv_obj->base;

		seq_puts(m, "   ");
		etnaviv_gem_describe(obj, m);
		count++;
		size += obj->size;
	}
	mutex_unlock(&priv->gem_lock);

	seq_printf(m, "Total %d objects, %zu bytes\n", count, size);
}
#endif

static void etnaviv_gem_shmem_release(struct etnaviv_gem_object *etnaviv_obj)
{
	vunmap(etnaviv_obj->vaddr);
	put_pages(etnaviv_obj);
}

static const struct etnaviv_gem_ops etnaviv_gem_shmem_ops = {
	.get_pages = etnaviv_gem_shmem_get_pages,
	.release = etnaviv_gem_shmem_release,
	.vmap = etnaviv_gem_vmap_impl,
	.mmap = etnaviv_gem_mmap_obj,
};

void etnaviv_gem_free_object(struct drm_gem_object *obj)
{
	struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);
	struct etnaviv_drm_private *priv = obj->dev->dev_private;
	struct etnaviv_vram_mapping *mapping, *tmp;

	/* object should not be active */
	WARN_ON(is_active(etnaviv_obj));

	mutex_lock(&priv->gem_lock);
	list_del(&etnaviv_obj->gem_node);
	mutex_unlock(&priv->gem_lock);

	list_for_each_entry_safe(mapping, tmp, &etnaviv_obj->vram_list,
				 obj_node) {
		struct etnaviv_iommu *mmu = mapping->mmu;

		WARN_ON(mapping->use);

		if (mmu)
			etnaviv_iommu_unmap_gem(mmu, mapping);

		list_del(&mapping->obj_node);
		kfree(mapping);
	}

	drm_gem_free_mmap_offset(obj);
	etnaviv_obj->ops->release(etnaviv_obj);
	if (etnaviv_obj->resv == &etnaviv_obj->_resv)
		reservation_object_fini(&etnaviv_obj->_resv);
	drm_gem_object_release(obj);

	kfree(etnaviv_obj);
}

void etnaviv_gem_obj_add(struct drm_device *dev, struct drm_gem_object *obj)
{
	struct etnaviv_drm_private *priv = dev->dev_private;
	struct etnaviv_gem_object *etnaviv_obj = to_etnaviv_bo(obj);

	mutex_lock(&priv->gem_lock);
	list_add_tail(&etnaviv_obj->gem_node, &priv->gem_list);
	mutex_unlock(&priv->gem_lock);
}

static int etnaviv_gem_new_impl(struct drm_device *dev, u32 size, u32 flags,
	struct reservation_object *robj, const struct etnaviv_gem_ops *ops,
	struct drm_gem_object **obj)
{
	struct etnaviv_gem_object *etnaviv_obj;
	unsigned sz = sizeof(*etnaviv_obj);
	bool valid = true;

	/* validate flags */
	switch (flags & ETNA_BO_CACHE_MASK) {
	case ETNA_BO_UNCACHED:
	case ETNA_BO_CACHED:
	case ETNA_BO_WC:
		break;
	default:
		valid = false;
	}

	if (!valid) {
		dev_err(dev->dev, "invalid cache flag: %x\n",
			(flags & ETNA_BO_CACHE_MASK));
		return -EINVAL;
	}

	etnaviv_obj = kzalloc(sz, GFP_KERNEL);
	if (!etnaviv_obj)
		return -ENOMEM;

	etnaviv_obj->flags = flags;
	etnaviv_obj->ops = ops;
	if (robj) {
		etnaviv_obj->resv = robj;
	} else {
		etnaviv_obj->resv = &etnaviv_obj->_resv;
		reservation_object_init(&etnaviv_obj->_resv);
	}

	mutex_init(&etnaviv_obj->lock);
	INIT_LIST_HEAD(&etnaviv_obj->vram_list);

	*obj = &etnaviv_obj->base;

	return 0;
}

/* convenience method to construct a GEM buffer object, and userspace handle */
int etnaviv_gem_new_handle(struct drm_device *dev, struct drm_file *file,
	u32 size, u32 flags, u32 *handle)
{
	struct drm_gem_object *obj = NULL;
	int ret;

	size = PAGE_ALIGN(size);

	ret = etnaviv_gem_new_impl(dev, size, flags, NULL,
				   &etnaviv_gem_shmem_ops, &obj);
	if (ret)
		goto fail;

	lockdep_set_class(&to_etnaviv_bo(obj)->lock, &etnaviv_shm_lock_class);

	ret = drm_gem_object_init(dev, obj, size);
	if (ret == 0) {
		struct address_space *mapping;

		/*
		 * Our buffers are kept pinned, so allocating them
		 * from the MOVABLE zone is a really bad idea, and
		 * conflicts with CMA. See comments above new_inode()
		 * why this is required _and_ expected if you're
		 * going to pin these pages.
		 */
		mapping = obj->filp->f_mapping;
		mapping_set_gfp_mask(mapping, GFP_HIGHUSER |
				     __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	}

	if (ret)
		goto fail;

	etnaviv_gem_obj_add(dev, obj);

	ret = drm_gem_handle_create(file, obj, handle);

	/* drop reference from allocate - handle holds it now */
fail:
	drm_gem_object_put_unlocked(obj);

	return ret;
}

int etnaviv_gem_new_private(struct drm_device *dev, size_t size, u32 flags,
	struct reservation_object *robj, const struct etnaviv_gem_ops *ops,
	struct etnaviv_gem_object **res)
{
	struct drm_gem_object *obj;
	int ret;

	ret = etnaviv_gem_new_impl(dev, size, flags, robj, ops, &obj);
	if (ret)
		return ret;

	drm_gem_private_object_init(dev, obj, size);

	*res = to_etnaviv_bo(obj);

	return 0;
}

static int etnaviv_gem_userptr_get_pages(struct etnaviv_gem_object *etnaviv_obj)
{
	struct page **pvec = NULL;
	struct etnaviv_gem_userptr *userptr = &etnaviv_obj->userptr;
	int ret, pinned = 0, npages = etnaviv_obj->base.size >> PAGE_SHIFT;

	might_lock_read(&current->mm->mmap_sem);

	if (userptr->mm != current->mm)
		return -EPERM;

	pvec = kvmalloc_array(npages, sizeof(struct page *), GFP_KERNEL);
	if (!pvec)
		return -ENOMEM;

	do {
		unsigned num_pages = npages - pinned;
		uint64_t ptr = userptr->ptr + pinned * PAGE_SIZE;
		struct page **pages = pvec + pinned;

		ret = get_user_pages_fast(ptr, num_pages,
					  !userptr->ro ? FOLL_WRITE : 0, pages);
		if (ret < 0) {
			release_pages(pvec, pinned);
			kvfree(pvec);
			return ret;
		}

		pinned += ret;

	} while (pinned < npages);

	etnaviv_obj->pages = pvec;

	return 0;
}

static void etnaviv_gem_userptr_release(struct etnaviv_gem_object *etnaviv_obj)
{
	if (etnaviv_obj->sgt) {
		etnaviv_gem_scatterlist_unmap(etnaviv_obj);
		sg_free_table(etnaviv_obj->sgt);
		kfree(etnaviv_obj->sgt);
	}
	if (etnaviv_obj->pages) {
		int npages = etnaviv_obj->base.size >> PAGE_SHIFT;

		release_pages(etnaviv_obj->pages, npages);
		kvfree(etnaviv_obj->pages);
	}
}

static int etnaviv_gem_userptr_mmap_obj(struct etnaviv_gem_object *etnaviv_obj,
		struct vm_area_struct *vma)
{
	return -EINVAL;
}

static const struct etnaviv_gem_ops etnaviv_gem_userptr_ops = {
	.get_pages = etnaviv_gem_userptr_get_pages,
	.release = etnaviv_gem_userptr_release,
	.vmap = etnaviv_gem_vmap_impl,
	.mmap = etnaviv_gem_userptr_mmap_obj,
};

int etnaviv_gem_new_userptr(struct drm_device *dev, struct drm_file *file,
	uintptr_t ptr, u32 size, u32 flags, u32 *handle)
{
	struct etnaviv_gem_object *etnaviv_obj;
	int ret;

	ret = etnaviv_gem_new_private(dev, size, ETNA_BO_CACHED, NULL,
				      &etnaviv_gem_userptr_ops, &etnaviv_obj);
	if (ret)
		return ret;

	lockdep_set_class(&etnaviv_obj->lock, &etnaviv_userptr_lock_class);

	etnaviv_obj->userptr.ptr = ptr;
	etnaviv_obj->userptr.mm = current->mm;
	etnaviv_obj->userptr.ro = !(flags & ETNA_USERPTR_WRITE);

	etnaviv_gem_obj_add(dev, &etnaviv_obj->base);

	ret = drm_gem_handle_create(file, &etnaviv_obj->base, handle);

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_put_unlocked(&etnaviv_obj->base);
	return ret;
}
