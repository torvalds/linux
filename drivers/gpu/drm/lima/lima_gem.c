// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/mm.h>
#include <linux/sync_file.h>
#include <linux/pagemap.h>
#include <linux/shmem_fs.h>
#include <linux/dma-mapping.h>

#include <drm/drm_file.h>
#include <drm/drm_syncobj.h>
#include <drm/drm_utils.h>

#include <drm/lima_drm.h>

#include "lima_drv.h"
#include "lima_gem.h"
#include "lima_vm.h"

int lima_heap_alloc(struct lima_bo *bo, struct lima_vm *vm)
{
	struct page **pages;
	struct address_space *mapping = bo->base.base.filp->f_mapping;
	struct device *dev = bo->base.base.dev->dev;
	size_t old_size = bo->heap_size;
	size_t new_size = bo->heap_size ? bo->heap_size * 2 :
		(lima_heap_init_nr_pages << PAGE_SHIFT);
	struct sg_table sgt;
	int i, ret;

	if (bo->heap_size >= bo->base.base.size)
		return -ENOSPC;

	new_size = min(new_size, bo->base.base.size);

	mutex_lock(&bo->base.pages_lock);

	if (bo->base.pages) {
		pages = bo->base.pages;
	} else {
		pages = kvmalloc_array(bo->base.base.size >> PAGE_SHIFT,
				       sizeof(*pages), GFP_KERNEL | __GFP_ZERO);
		if (!pages) {
			mutex_unlock(&bo->base.pages_lock);
			return -ENOMEM;
		}

		bo->base.pages = pages;
		bo->base.pages_use_count = 1;

		mapping_set_unevictable(mapping);
	}

	for (i = old_size >> PAGE_SHIFT; i < new_size >> PAGE_SHIFT; i++) {
		struct page *page = shmem_read_mapping_page(mapping, i);

		if (IS_ERR(page)) {
			mutex_unlock(&bo->base.pages_lock);
			return PTR_ERR(page);
		}
		pages[i] = page;
	}

	mutex_unlock(&bo->base.pages_lock);

	ret = sg_alloc_table_from_pages(&sgt, pages, i, 0,
					new_size, GFP_KERNEL);
	if (ret)
		return ret;

	if (bo->base.sgt) {
		dma_unmap_sg(dev, bo->base.sgt->sgl,
			     bo->base.sgt->nents, DMA_BIDIRECTIONAL);
		sg_free_table(bo->base.sgt);
	} else {
		bo->base.sgt = kmalloc(sizeof(*bo->base.sgt), GFP_KERNEL);
		if (!bo->base.sgt) {
			sg_free_table(&sgt);
			return -ENOMEM;
		}
	}

	dma_map_sg(dev, sgt.sgl, sgt.nents, DMA_BIDIRECTIONAL);

	*bo->base.sgt = sgt;

	if (vm) {
		ret = lima_vm_map_bo(vm, bo, old_size >> PAGE_SHIFT);
		if (ret)
			return ret;
	}

	bo->heap_size = new_size;
	return 0;
}

int lima_gem_create_handle(struct drm_device *dev, struct drm_file *file,
			   u32 size, u32 flags, u32 *handle)
{
	int err;
	gfp_t mask;
	struct drm_gem_shmem_object *shmem;
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	bool is_heap = flags & LIMA_BO_FLAG_HEAP;

	shmem = drm_gem_shmem_create(dev, size);
	if (IS_ERR(shmem))
		return PTR_ERR(shmem);

	obj = &shmem->base;

	/* Mali Utgard GPU can only support 32bit address space */
	mask = mapping_gfp_mask(obj->filp->f_mapping);
	mask &= ~__GFP_HIGHMEM;
	mask |= __GFP_DMA32;
	mapping_set_gfp_mask(obj->filp->f_mapping, mask);

	if (is_heap) {
		bo = to_lima_bo(obj);
		err = lima_heap_alloc(bo, NULL);
		if (err)
			goto out;
	} else {
		struct sg_table *sgt = drm_gem_shmem_get_pages_sgt(obj);

		if (IS_ERR(sgt)) {
			err = PTR_ERR(sgt);
			goto out;
		}
	}

	err = drm_gem_handle_create(file, obj, handle);

out:
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_put(obj);

	return err;
}

static void lima_gem_free_object(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);

	if (!list_empty(&bo->va))
		dev_err(obj->dev->dev, "lima gem free bo still has va\n");

	drm_gem_shmem_free_object(obj);
}

static int lima_gem_object_open(struct drm_gem_object *obj, struct drm_file *file)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;

	return lima_vm_bo_add(vm, bo, true);
}

static void lima_gem_object_close(struct drm_gem_object *obj, struct drm_file *file)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;

	lima_vm_bo_del(vm, bo);
}

static int lima_gem_pin(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);

	if (bo->heap_size)
		return -EINVAL;

	return drm_gem_shmem_pin(obj);
}

static void *lima_gem_vmap(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);

	if (bo->heap_size)
		return ERR_PTR(-EINVAL);

	return drm_gem_shmem_vmap(obj);
}

static int lima_gem_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct lima_bo *bo = to_lima_bo(obj);

	if (bo->heap_size)
		return -EINVAL;

	return drm_gem_shmem_mmap(obj, vma);
}

static const struct drm_gem_object_funcs lima_gem_funcs = {
	.free = lima_gem_free_object,
	.open = lima_gem_object_open,
	.close = lima_gem_object_close,
	.print_info = drm_gem_shmem_print_info,
	.pin = lima_gem_pin,
	.unpin = drm_gem_shmem_unpin,
	.get_sg_table = drm_gem_shmem_get_sg_table,
	.vmap = lima_gem_vmap,
	.vunmap = drm_gem_shmem_vunmap,
	.mmap = lima_gem_mmap,
};

struct drm_gem_object *lima_gem_create_object(struct drm_device *dev, size_t size)
{
	struct lima_bo *bo;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return NULL;

	mutex_init(&bo->lock);
	INIT_LIST_HEAD(&bo->va);

	bo->base.base.funcs = &lima_gem_funcs;

	return &bo->base.base;
}

int lima_gem_get_info(struct drm_file *file, u32 handle, u32 *va, u64 *offset)
{
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	bo = to_lima_bo(obj);

	*va = lima_vm_get_va(vm, bo);

	*offset = drm_vma_node_offset_addr(&obj->vma_node);

	drm_gem_object_put(obj);
	return 0;
}

static int lima_gem_sync_bo(struct lima_sched_task *task, struct lima_bo *bo,
			    bool write, bool explicit)
{
	int err = 0;

	if (!write) {
		err = dma_resv_reserve_shared(lima_bo_resv(bo), 1);
		if (err)
			return err;
	}

	/* explicit sync use user passed dep fence */
	if (explicit)
		return 0;

	return drm_gem_fence_array_add_implicit(&task->deps, &bo->base.base, write);
}

static int lima_gem_add_deps(struct drm_file *file, struct lima_submit *submit)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(submit->in_sync); i++) {
		struct dma_fence *fence = NULL;

		if (!submit->in_sync[i])
			continue;

		err = drm_syncobj_find_fence(file, submit->in_sync[i],
					     0, 0, &fence);
		if (err)
			return err;

		err = drm_gem_fence_array_add(&submit->task->deps, fence);
		if (err) {
			dma_fence_put(fence);
			return err;
		}
	}

	return 0;
}

int lima_gem_submit(struct drm_file *file, struct lima_submit *submit)
{
	int i, err = 0;
	struct ww_acquire_ctx ctx;
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;
	struct drm_syncobj *out_sync = NULL;
	struct dma_fence *fence;
	struct lima_bo **bos = submit->lbos;

	if (submit->out_sync) {
		out_sync = drm_syncobj_find(file, submit->out_sync);
		if (!out_sync)
			return -ENOENT;
	}

	for (i = 0; i < submit->nr_bos; i++) {
		struct drm_gem_object *obj;
		struct lima_bo *bo;

		obj = drm_gem_object_lookup(file, submit->bos[i].handle);
		if (!obj) {
			err = -ENOENT;
			goto err_out0;
		}

		bo = to_lima_bo(obj);

		/* increase refcnt of gpu va map to prevent unmapped when executing,
		 * will be decreased when task done
		 */
		err = lima_vm_bo_add(vm, bo, false);
		if (err) {
			drm_gem_object_put(obj);
			goto err_out0;
		}

		bos[i] = bo;
	}

	err = drm_gem_lock_reservations((struct drm_gem_object **)bos,
					submit->nr_bos, &ctx);
	if (err)
		goto err_out0;

	err = lima_sched_task_init(
		submit->task, submit->ctx->context + submit->pipe,
		bos, submit->nr_bos, vm);
	if (err)
		goto err_out1;

	err = lima_gem_add_deps(file, submit);
	if (err)
		goto err_out2;

	for (i = 0; i < submit->nr_bos; i++) {
		err = lima_gem_sync_bo(
			submit->task, bos[i],
			submit->bos[i].flags & LIMA_SUBMIT_BO_WRITE,
			submit->flags & LIMA_SUBMIT_FLAG_EXPLICIT_FENCE);
		if (err)
			goto err_out2;
	}

	fence = lima_sched_context_queue_task(
		submit->ctx->context + submit->pipe, submit->task);

	for (i = 0; i < submit->nr_bos; i++) {
		if (submit->bos[i].flags & LIMA_SUBMIT_BO_WRITE)
			dma_resv_add_excl_fence(lima_bo_resv(bos[i]), fence);
		else
			dma_resv_add_shared_fence(lima_bo_resv(bos[i]), fence);
	}

	drm_gem_unlock_reservations((struct drm_gem_object **)bos,
				    submit->nr_bos, &ctx);

	for (i = 0; i < submit->nr_bos; i++)
		drm_gem_object_put(&bos[i]->base.base);

	if (out_sync) {
		drm_syncobj_replace_fence(out_sync, fence);
		drm_syncobj_put(out_sync);
	}

	dma_fence_put(fence);

	return 0;

err_out2:
	lima_sched_task_fini(submit->task);
err_out1:
	drm_gem_unlock_reservations((struct drm_gem_object **)bos,
				    submit->nr_bos, &ctx);
err_out0:
	for (i = 0; i < submit->nr_bos; i++) {
		if (!bos[i])
			break;
		lima_vm_bo_del(vm, bos[i]);
		drm_gem_object_put(&bos[i]->base.base);
	}
	if (out_sync)
		drm_syncobj_put(out_sync);
	return err;
}

int lima_gem_wait(struct drm_file *file, u32 handle, u32 op, s64 timeout_ns)
{
	bool write = op & LIMA_GEM_WAIT_WRITE;
	long ret, timeout;

	if (!op)
		return 0;

	timeout = drm_timeout_abs_to_jiffies(timeout_ns);

	ret = drm_gem_dma_resv_wait(file, handle, write, timeout);
	if (ret == -ETIME)
		ret = timeout ? -ETIMEDOUT : -EBUSY;

	return ret;
}
