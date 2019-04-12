// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/mm.h>
#include <linux/sync_file.h>
#include <linux/pfn_t.h>

#include <drm/drm_file.h>
#include <drm/drm_syncobj.h>
#include <drm/drm_utils.h>

#include <drm/lima_drm.h>

#include "lima_drv.h"
#include "lima_gem.h"
#include "lima_gem_prime.h"
#include "lima_vm.h"
#include "lima_object.h"

int lima_gem_create_handle(struct drm_device *dev, struct drm_file *file,
			   u32 size, u32 flags, u32 *handle)
{
	int err;
	struct lima_bo *bo;
	struct lima_device *ldev = to_lima_dev(dev);

	bo = lima_bo_create(ldev, size, flags, NULL, NULL);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	err = drm_gem_handle_create(file, &bo->gem, handle);

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_put_unlocked(&bo->gem);

	return err;
}

void lima_gem_free_object(struct drm_gem_object *obj)
{
	struct lima_bo *bo = to_lima_bo(obj);

	if (!list_empty(&bo->va))
		dev_err(obj->dev->dev, "lima gem free bo still has va\n");

	lima_bo_destroy(bo);
}

int lima_gem_object_open(struct drm_gem_object *obj, struct drm_file *file)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;

	return lima_vm_bo_add(vm, bo, true);
}

void lima_gem_object_close(struct drm_gem_object *obj, struct drm_file *file)
{
	struct lima_bo *bo = to_lima_bo(obj);
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;

	lima_vm_bo_del(vm, bo);
}

int lima_gem_get_info(struct drm_file *file, u32 handle, u32 *va, u64 *offset)
{
	struct drm_gem_object *obj;
	struct lima_bo *bo;
	struct lima_drm_priv *priv = to_lima_drm_priv(file);
	struct lima_vm *vm = priv->vm;
	int err;

	obj = drm_gem_object_lookup(file, handle);
	if (!obj)
		return -ENOENT;

	bo = to_lima_bo(obj);

	*va = lima_vm_get_va(vm, bo);

	err = drm_gem_create_mmap_offset(obj);
	if (!err)
		*offset = drm_vma_node_offset_addr(&obj->vma_node);

	drm_gem_object_put_unlocked(obj);
	return err;
}

static vm_fault_t lima_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct lima_bo *bo = to_lima_bo(obj);
	pfn_t pfn;
	pgoff_t pgoff;

	/* We don't use vmf->pgoff since that has the fake offset: */
	pgoff = (vmf->address - vma->vm_start) >> PAGE_SHIFT;
	pfn = __pfn_to_pfn_t(page_to_pfn(bo->pages[pgoff]), PFN_DEV);

	return vmf_insert_mixed(vma, vmf->address, pfn);
}

const struct vm_operations_struct lima_gem_vm_ops = {
	.fault = lima_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

void lima_set_vma_flags(struct vm_area_struct *vma)
{
	pgprot_t prot = vm_get_page_prot(vma->vm_flags);

	vma->vm_flags |= VM_MIXEDMAP;
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_page_prot = pgprot_writecombine(prot);
}

int lima_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;

	ret = drm_gem_mmap(filp, vma);
	if (ret)
		return ret;

	lima_set_vma_flags(vma);
	return 0;
}

static int lima_gem_sync_bo(struct lima_sched_task *task, struct lima_bo *bo,
			    bool write, bool explicit)
{
	int err = 0;

	if (!write) {
		err = reservation_object_reserve_shared(bo->gem.resv, 1);
		if (err)
			return err;
	}

	/* explicit sync use user passed dep fence */
	if (explicit)
		return 0;

	/* implicit sync use bo fence in resv obj */
	if (write) {
		unsigned nr_fences;
		struct dma_fence **fences;
		int i;

		err = reservation_object_get_fences_rcu(
			bo->gem.resv, NULL, &nr_fences, &fences);
		if (err || !nr_fences)
			return err;

		for (i = 0; i < nr_fences; i++) {
			err = lima_sched_task_add_dep(task, fences[i]);
			if (err)
				break;
		}

		/* for error case free remaining fences */
		for ( ; i < nr_fences; i++)
			dma_fence_put(fences[i]);

		kfree(fences);
	} else {
		struct dma_fence *fence;

		fence = reservation_object_get_excl_rcu(bo->gem.resv);
		if (fence) {
			err = lima_sched_task_add_dep(task, fence);
			if (err)
				dma_fence_put(fence);
		}
	}

	return err;
}

static int lima_gem_lock_bos(struct lima_bo **bos, u32 nr_bos,
			     struct ww_acquire_ctx *ctx)
{
	int i, ret = 0, contended, slow_locked = -1;

	ww_acquire_init(ctx, &reservation_ww_class);

retry:
	for (i = 0; i < nr_bos; i++) {
		if (i == slow_locked) {
			slow_locked = -1;
			continue;
		}

		ret = ww_mutex_lock_interruptible(&bos[i]->gem.resv->lock, ctx);
		if (ret < 0) {
			contended = i;
			goto err;
		}
	}

	ww_acquire_done(ctx);
	return 0;

err:
	for (i--; i >= 0; i--)
		ww_mutex_unlock(&bos[i]->gem.resv->lock);

	if (slow_locked >= 0)
		ww_mutex_unlock(&bos[slow_locked]->gem.resv->lock);

	if (ret == -EDEADLK) {
		/* we lost out in a seqno race, lock and retry.. */
		ret = ww_mutex_lock_slow_interruptible(
			&bos[contended]->gem.resv->lock, ctx);
		if (!ret) {
			slow_locked = contended;
			goto retry;
		}
	}
	ww_acquire_fini(ctx);

	return ret;
}

static void lima_gem_unlock_bos(struct lima_bo **bos, u32 nr_bos,
				struct ww_acquire_ctx *ctx)
{
	int i;

	for (i = 0; i < nr_bos; i++)
		ww_mutex_unlock(&bos[i]->gem.resv->lock);
	ww_acquire_fini(ctx);
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

		err = lima_sched_task_add_dep(submit->task, fence);
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
			drm_gem_object_put_unlocked(obj);
			goto err_out0;
		}

		bos[i] = bo;
	}

	err = lima_gem_lock_bos(bos, submit->nr_bos, &ctx);
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
			reservation_object_add_excl_fence(bos[i]->gem.resv, fence);
		else
			reservation_object_add_shared_fence(bos[i]->gem.resv, fence);
	}

	lima_gem_unlock_bos(bos, submit->nr_bos, &ctx);

	for (i = 0; i < submit->nr_bos; i++)
		drm_gem_object_put_unlocked(&bos[i]->gem);

	if (out_sync) {
		drm_syncobj_replace_fence(out_sync, fence);
		drm_syncobj_put(out_sync);
	}

	dma_fence_put(fence);

	return 0;

err_out2:
	lima_sched_task_fini(submit->task);
err_out1:
	lima_gem_unlock_bos(bos, submit->nr_bos, &ctx);
err_out0:
	for (i = 0; i < submit->nr_bos; i++) {
		if (!bos[i])
			break;
		lima_vm_bo_del(vm, bos[i]);
		drm_gem_object_put_unlocked(&bos[i]->gem);
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

	ret = drm_gem_reservation_object_wait(file, handle, write, timeout);
	if (ret == 0)
		ret = timeout ? -ETIMEDOUT : -EBUSY;

	return ret;
}
