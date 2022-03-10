// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include <uapi/misc/habanalabs.h>
#include "habanalabs.h"

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static int cb_map_mem(struct hl_ctx *ctx, struct hl_cb *cb)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_vm_va_block *va_block, *tmp;
	dma_addr_t bus_addr;
	u64 virt_addr;
	u32 page_size = prop->pmmu.page_size;
	s32 offset;
	int rc;

	if (!hdev->supports_cb_mapping) {
		dev_err_ratelimited(hdev->dev,
				"Cannot map CB because no VA range is allocated for CB mapping\n");
		return -EINVAL;
	}

	if (!hdev->mmu_enable) {
		dev_err_ratelimited(hdev->dev,
				"Cannot map CB because MMU is disabled\n");
		return -EINVAL;
	}

	INIT_LIST_HEAD(&cb->va_block_list);

	for (bus_addr = cb->bus_address;
			bus_addr < cb->bus_address + cb->size;
			bus_addr += page_size) {

		virt_addr = (u64) gen_pool_alloc(ctx->cb_va_pool, page_size);
		if (!virt_addr) {
			dev_err(hdev->dev,
				"Failed to allocate device virtual address for CB\n");
			rc = -ENOMEM;
			goto err_va_pool_free;
		}

		va_block = kzalloc(sizeof(*va_block), GFP_KERNEL);
		if (!va_block) {
			rc = -ENOMEM;
			gen_pool_free(ctx->cb_va_pool, virt_addr, page_size);
			goto err_va_pool_free;
		}

		va_block->start = virt_addr;
		va_block->end = virt_addr + page_size - 1;
		va_block->size = page_size;
		list_add_tail(&va_block->node, &cb->va_block_list);
	}

	mutex_lock(&ctx->mmu_lock);

	bus_addr = cb->bus_address;
	offset = 0;
	list_for_each_entry(va_block, &cb->va_block_list, node) {
		rc = hl_mmu_map_page(ctx, va_block->start, bus_addr,
				va_block->size, list_is_last(&va_block->node,
							&cb->va_block_list));
		if (rc) {
			dev_err(hdev->dev, "Failed to map VA %#llx to CB\n",
				va_block->start);
			goto err_va_umap;
		}

		bus_addr += va_block->size;
		offset += va_block->size;
	}

	rc = hl_mmu_invalidate_cache(hdev, false, MMU_OP_USERPTR | MMU_OP_SKIP_LOW_CACHE_INV);

	mutex_unlock(&ctx->mmu_lock);

	cb->is_mmu_mapped = true;

	return rc;

err_va_umap:
	list_for_each_entry(va_block, &cb->va_block_list, node) {
		if (offset <= 0)
			break;
		hl_mmu_unmap_page(ctx, va_block->start, va_block->size,
				offset <= va_block->size);
		offset -= va_block->size;
	}

	rc = hl_mmu_invalidate_cache(hdev, true, MMU_OP_USERPTR);

	mutex_unlock(&ctx->mmu_lock);

err_va_pool_free:
	list_for_each_entry_safe(va_block, tmp, &cb->va_block_list, node) {
		gen_pool_free(ctx->cb_va_pool, va_block->start, va_block->size);
		list_del(&va_block->node);
		kfree(va_block);
	}

	return rc;
}

static void cb_unmap_mem(struct hl_ctx *ctx, struct hl_cb *cb)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_vm_va_block *va_block, *tmp;

	mutex_lock(&ctx->mmu_lock);

	list_for_each_entry(va_block, &cb->va_block_list, node)
		if (hl_mmu_unmap_page(ctx, va_block->start, va_block->size,
				list_is_last(&va_block->node,
						&cb->va_block_list)))
			dev_warn_ratelimited(hdev->dev,
					"Failed to unmap CB's va 0x%llx\n",
					va_block->start);

	hl_mmu_invalidate_cache(hdev, true, MMU_OP_USERPTR);

	mutex_unlock(&ctx->mmu_lock);

	list_for_each_entry_safe(va_block, tmp, &cb->va_block_list, node) {
		gen_pool_free(ctx->cb_va_pool, va_block->start, va_block->size);
		list_del(&va_block->node);
		kfree(va_block);
	}
}

static void cb_fini(struct hl_device *hdev, struct hl_cb *cb)
{
	if (cb->is_internal)
		gen_pool_free(hdev->internal_cb_pool,
				(uintptr_t)cb->kernel_address, cb->size);
	else
		hdev->asic_funcs->asic_dma_free_coherent(hdev, cb->size,
				cb->kernel_address, cb->bus_address);

	kfree(cb);
}

static void cb_do_release(struct hl_device *hdev, struct hl_cb *cb)
{
	if (cb->is_pool) {
		spin_lock(&hdev->cb_pool_lock);
		list_add(&cb->pool_list, &hdev->cb_pool);
		spin_unlock(&hdev->cb_pool_lock);
	} else {
		cb_fini(hdev, cb);
	}
}

static void cb_release(struct kref *ref)
{
	struct hl_device *hdev;
	struct hl_cb *cb;

	cb = container_of(ref, struct hl_cb, refcount);
	hdev = cb->hdev;

	hl_debugfs_remove_cb(cb);

	if (cb->is_mmu_mapped)
		cb_unmap_mem(cb->ctx, cb);

	hl_ctx_put(cb->ctx);

	cb_do_release(hdev, cb);
}

static struct hl_cb *hl_cb_alloc(struct hl_device *hdev, u32 cb_size,
					int ctx_id, bool internal_cb)
{
	struct hl_cb *cb = NULL;
	u32 cb_offset;
	void *p;

	/*
	 * We use of GFP_ATOMIC here because this function can be called from
	 * the latency-sensitive code path for command submission. Due to H/W
	 * limitations in some of the ASICs, the kernel must copy the user CB
	 * that is designated for an external queue and actually enqueue
	 * the kernel's copy. Hence, we must never sleep in this code section
	 * and must use GFP_ATOMIC for all memory allocations.
	 */
	if (ctx_id == HL_KERNEL_ASID_ID && !hdev->disabled)
		cb = kzalloc(sizeof(*cb), GFP_ATOMIC);

	if (!cb)
		cb = kzalloc(sizeof(*cb), GFP_KERNEL);

	if (!cb)
		return NULL;

	if (internal_cb) {
		p = (void *) gen_pool_alloc(hdev->internal_cb_pool, cb_size);
		if (!p) {
			kfree(cb);
			return NULL;
		}

		cb_offset = p - hdev->internal_cb_pool_virt_addr;
		cb->is_internal = true;
		cb->bus_address =  hdev->internal_cb_va_base + cb_offset;
	} else if (ctx_id == HL_KERNEL_ASID_ID) {
		p = hdev->asic_funcs->asic_dma_alloc_coherent(hdev, cb_size,
						&cb->bus_address, GFP_ATOMIC);
		if (!p)
			p = hdev->asic_funcs->asic_dma_alloc_coherent(hdev,
					cb_size, &cb->bus_address, GFP_KERNEL);
	} else {
		p = hdev->asic_funcs->asic_dma_alloc_coherent(hdev, cb_size,
						&cb->bus_address,
						GFP_USER | __GFP_ZERO);
	}

	if (!p) {
		dev_err(hdev->dev,
			"failed to allocate %d of dma memory for CB\n",
			cb_size);
		kfree(cb);
		return NULL;
	}

	cb->kernel_address = p;
	cb->size = cb_size;

	return cb;
}

int hl_cb_create(struct hl_device *hdev, struct hl_cb_mgr *mgr,
			struct hl_ctx *ctx, u32 cb_size, bool internal_cb,
			bool map_cb, u64 *handle)
{
	struct hl_cb *cb;
	bool alloc_new_cb = true;
	int rc, ctx_id = ctx->asid;

	/*
	 * Can't use generic function to check this because of special case
	 * where we create a CB as part of the reset process
	 */
	if ((hdev->disabled) || (hdev->reset_info.in_reset && (ctx_id != HL_KERNEL_ASID_ID))) {
		dev_warn_ratelimited(hdev->dev,
			"Device is disabled or in reset. Can't create new CBs\n");
		rc = -EBUSY;
		goto out_err;
	}

	if (cb_size > SZ_2M) {
		dev_err(hdev->dev, "CB size %d must be less than %d\n",
			cb_size, SZ_2M);
		rc = -EINVAL;
		goto out_err;
	}

	if (!internal_cb) {
		/* Minimum allocation must be PAGE SIZE */
		if (cb_size < PAGE_SIZE)
			cb_size = PAGE_SIZE;

		if (ctx_id == HL_KERNEL_ASID_ID &&
				cb_size <= hdev->asic_prop.cb_pool_cb_size) {

			spin_lock(&hdev->cb_pool_lock);
			if (!list_empty(&hdev->cb_pool)) {
				cb = list_first_entry(&hdev->cb_pool,
						typeof(*cb), pool_list);
				list_del(&cb->pool_list);
				spin_unlock(&hdev->cb_pool_lock);
				alloc_new_cb = false;
			} else {
				spin_unlock(&hdev->cb_pool_lock);
				dev_dbg(hdev->dev, "CB pool is empty\n");
			}
		}
	}

	if (alloc_new_cb) {
		cb = hl_cb_alloc(hdev, cb_size, ctx_id, internal_cb);
		if (!cb) {
			rc = -ENOMEM;
			goto out_err;
		}
	}

	cb->hdev = hdev;
	cb->ctx = ctx;
	hl_ctx_get(hdev, cb->ctx);

	if (map_cb) {
		if (ctx_id == HL_KERNEL_ASID_ID) {
			dev_err(hdev->dev,
				"CB mapping is not supported for kernel context\n");
			rc = -EINVAL;
			goto release_cb;
		}

		rc = cb_map_mem(ctx, cb);
		if (rc)
			goto release_cb;
	}

	spin_lock(&mgr->cb_lock);
	rc = idr_alloc(&mgr->cb_handles, cb, 1, 0, GFP_ATOMIC);
	spin_unlock(&mgr->cb_lock);

	if (rc < 0) {
		dev_err(hdev->dev, "Failed to allocate IDR for a new CB\n");
		goto unmap_mem;
	}

	cb->id = (u64) rc;

	kref_init(&cb->refcount);
	spin_lock_init(&cb->lock);

	/*
	 * idr is 32-bit so we can safely OR it with a mask that is above
	 * 32 bit
	 */
	*handle = cb->id | HL_MMAP_TYPE_CB;
	*handle <<= PAGE_SHIFT;

	hl_debugfs_add_cb(cb);

	return 0;

unmap_mem:
	if (cb->is_mmu_mapped)
		cb_unmap_mem(cb->ctx, cb);
release_cb:
	hl_ctx_put(cb->ctx);
	cb_do_release(hdev, cb);
out_err:
	*handle = 0;

	return rc;
}

int hl_cb_destroy(struct hl_device *hdev, struct hl_cb_mgr *mgr, u64 cb_handle)
{
	struct hl_cb *cb;
	u32 handle;
	int rc = 0;

	/*
	 * handle was given to user to do mmap, I need to shift it back to
	 * how the idr module gave it to me
	 */
	cb_handle >>= PAGE_SHIFT;
	handle = (u32) cb_handle;

	spin_lock(&mgr->cb_lock);

	cb = idr_find(&mgr->cb_handles, handle);
	if (cb) {
		idr_remove(&mgr->cb_handles, handle);
		spin_unlock(&mgr->cb_lock);
		kref_put(&cb->refcount, cb_release);
	} else {
		spin_unlock(&mgr->cb_lock);
		dev_err(hdev->dev,
			"CB destroy failed, no match to handle 0x%x\n", handle);
		rc = -EINVAL;
	}

	return rc;
}

static int hl_cb_info(struct hl_device *hdev, struct hl_cb_mgr *mgr,
			u64 cb_handle, u32 flags, u32 *usage_cnt, u64 *device_va)
{
	struct hl_vm_va_block *va_block;
	struct hl_cb *cb;
	u32 handle;
	int rc = 0;

	/* The CB handle was given to user to do mmap, so need to shift it back
	 * to the value which was allocated by the IDR module.
	 */
	cb_handle >>= PAGE_SHIFT;
	handle = (u32) cb_handle;

	spin_lock(&mgr->cb_lock);

	cb = idr_find(&mgr->cb_handles, handle);
	if (!cb) {
		dev_err(hdev->dev,
			"CB info failed, no match to handle 0x%x\n", handle);
		rc = -EINVAL;
		goto out;
	}

	if (flags & HL_CB_FLAGS_GET_DEVICE_VA) {
		va_block = list_first_entry(&cb->va_block_list, struct hl_vm_va_block, node);
		if (va_block) {
			*device_va = va_block->start;
		} else {
			dev_err(hdev->dev, "CB is not mapped to the device's MMU\n");
			rc = -EINVAL;
			goto out;
		}
	} else {
		*usage_cnt = atomic_read(&cb->cs_cnt);
	}

out:
	spin_unlock(&mgr->cb_lock);
	return rc;
}

int hl_cb_ioctl(struct hl_fpriv *hpriv, void *data)
{
	union hl_cb_args *args = data;
	struct hl_device *hdev = hpriv->hdev;
	u64 handle = 0, device_va = 0;
	enum hl_device_status status;
	u32 usage_cnt = 0;
	int rc;

	if (!hl_device_operational(hdev, &status)) {
		dev_warn_ratelimited(hdev->dev,
			"Device is %s. Can't execute CB IOCTL\n",
			hdev->status[status]);
		return -EBUSY;
	}

	switch (args->in.op) {
	case HL_CB_OP_CREATE:
		if (args->in.cb_size > HL_MAX_CB_SIZE) {
			dev_err(hdev->dev,
				"User requested CB size %d must be less than %d\n",
				args->in.cb_size, HL_MAX_CB_SIZE);
			rc = -EINVAL;
		} else {
			rc = hl_cb_create(hdev, &hpriv->cb_mgr, hpriv->ctx,
					args->in.cb_size, false,
					!!(args->in.flags & HL_CB_FLAGS_MAP),
					&handle);
		}

		memset(args, 0, sizeof(*args));
		args->out.cb_handle = handle;
		break;

	case HL_CB_OP_DESTROY:
		rc = hl_cb_destroy(hdev, &hpriv->cb_mgr,
					args->in.cb_handle);
		break;

	case HL_CB_OP_INFO:
		rc = hl_cb_info(hdev, &hpriv->cb_mgr, args->in.cb_handle,
				args->in.flags,
				&usage_cnt,
				&device_va);
		if (rc)
			break;

		memset(&args->out, 0, sizeof(args->out));

		if (args->in.flags & HL_CB_FLAGS_GET_DEVICE_VA)
			args->out.device_va = device_va;
		else
			args->out.usage_cnt = usage_cnt;
		break;

	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static void cb_vm_close(struct vm_area_struct *vma)
{
	struct hl_cb *cb = (struct hl_cb *) vma->vm_private_data;
	long new_mmap_size;

	new_mmap_size = cb->mmap_size - (vma->vm_end - vma->vm_start);

	if (new_mmap_size > 0) {
		cb->mmap_size = new_mmap_size;
		return;
	}

	spin_lock(&cb->lock);
	cb->mmap = false;
	spin_unlock(&cb->lock);

	hl_cb_put(cb);
	vma->vm_private_data = NULL;
}

static const struct vm_operations_struct cb_vm_ops = {
	.close = cb_vm_close
};

int hl_cb_mmap(struct hl_fpriv *hpriv, struct vm_area_struct *vma)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_cb *cb;
	u32 handle, user_cb_size;
	int rc;

	/* We use the page offset to hold the idr and thus we need to clear
	 * it before doing the mmap itself
	 */
	handle = vma->vm_pgoff;
	vma->vm_pgoff = 0;

	/* reference was taken here */
	cb = hl_cb_get(hdev, &hpriv->cb_mgr, handle);
	if (!cb) {
		dev_err(hdev->dev,
			"CB mmap failed, no match to handle 0x%x\n", handle);
		return -EINVAL;
	}

	/* Validation check */
	user_cb_size = vma->vm_end - vma->vm_start;
	if (user_cb_size != ALIGN(cb->size, PAGE_SIZE)) {
		dev_err(hdev->dev,
			"CB mmap failed, mmap size 0x%lx != 0x%x cb size\n",
			vma->vm_end - vma->vm_start, cb->size);
		rc = -EINVAL;
		goto put_cb;
	}

	if (!access_ok((void __user *) (uintptr_t) vma->vm_start,
							user_cb_size)) {
		dev_err(hdev->dev,
			"user pointer is invalid - 0x%lx\n",
			vma->vm_start);

		rc = -EINVAL;
		goto put_cb;
	}

	spin_lock(&cb->lock);

	if (cb->mmap) {
		dev_err(hdev->dev,
			"CB mmap failed, CB already mmaped to user\n");
		rc = -EINVAL;
		goto release_lock;
	}

	cb->mmap = true;

	spin_unlock(&cb->lock);

	vma->vm_ops = &cb_vm_ops;

	/*
	 * Note: We're transferring the cb reference to
	 * vma->vm_private_data here.
	 */

	vma->vm_private_data = cb;

	rc = hdev->asic_funcs->mmap(hdev, vma, cb->kernel_address,
					cb->bus_address, cb->size);
	if (rc) {
		spin_lock(&cb->lock);
		cb->mmap = false;
		goto release_lock;
	}

	cb->mmap_size = cb->size;
	vma->vm_pgoff = handle;

	return 0;

release_lock:
	spin_unlock(&cb->lock);
put_cb:
	hl_cb_put(cb);
	return rc;
}

struct hl_cb *hl_cb_get(struct hl_device *hdev, struct hl_cb_mgr *mgr,
			u32 handle)
{
	struct hl_cb *cb;

	spin_lock(&mgr->cb_lock);
	cb = idr_find(&mgr->cb_handles, handle);

	if (!cb) {
		spin_unlock(&mgr->cb_lock);
		dev_warn(hdev->dev,
			"CB get failed, no match to handle 0x%x\n", handle);
		return NULL;
	}

	kref_get(&cb->refcount);

	spin_unlock(&mgr->cb_lock);

	return cb;

}

void hl_cb_put(struct hl_cb *cb)
{
	kref_put(&cb->refcount, cb_release);
}

void hl_cb_mgr_init(struct hl_cb_mgr *mgr)
{
	spin_lock_init(&mgr->cb_lock);
	idr_init(&mgr->cb_handles);
}

void hl_cb_mgr_fini(struct hl_device *hdev, struct hl_cb_mgr *mgr)
{
	struct hl_cb *cb;
	struct idr *idp;
	u32 id;

	idp = &mgr->cb_handles;

	idr_for_each_entry(idp, cb, id) {
		if (kref_put(&cb->refcount, cb_release) != 1)
			dev_err(hdev->dev,
				"CB %d for CTX ID %d is still alive\n",
				id, cb->ctx->asid);
	}

	idr_destroy(&mgr->cb_handles);
}

struct hl_cb *hl_cb_kernel_create(struct hl_device *hdev, u32 cb_size,
					bool internal_cb)
{
	u64 cb_handle;
	struct hl_cb *cb;
	int rc;

	rc = hl_cb_create(hdev, &hdev->kernel_cb_mgr, hdev->kernel_ctx, cb_size,
				internal_cb, false, &cb_handle);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to allocate CB for the kernel driver %d\n", rc);
		return NULL;
	}

	cb_handle >>= PAGE_SHIFT;
	cb = hl_cb_get(hdev, &hdev->kernel_cb_mgr, (u32) cb_handle);
	/* hl_cb_get should never fail here */
	if (!cb) {
		dev_crit(hdev->dev, "Kernel CB handle invalid 0x%x\n",
				(u32) cb_handle);
		goto destroy_cb;
	}

	return cb;

destroy_cb:
	hl_cb_destroy(hdev, &hdev->kernel_cb_mgr, cb_handle << PAGE_SHIFT);

	return NULL;
}

int hl_cb_pool_init(struct hl_device *hdev)
{
	struct hl_cb *cb;
	int i;

	INIT_LIST_HEAD(&hdev->cb_pool);
	spin_lock_init(&hdev->cb_pool_lock);

	for (i = 0 ; i < hdev->asic_prop.cb_pool_cb_cnt ; i++) {
		cb = hl_cb_alloc(hdev, hdev->asic_prop.cb_pool_cb_size,
				HL_KERNEL_ASID_ID, false);
		if (cb) {
			cb->is_pool = true;
			list_add(&cb->pool_list, &hdev->cb_pool);
		} else {
			hl_cb_pool_fini(hdev);
			return -ENOMEM;
		}
	}

	return 0;
}

int hl_cb_pool_fini(struct hl_device *hdev)
{
	struct hl_cb *cb, *tmp;

	list_for_each_entry_safe(cb, tmp, &hdev->cb_pool, pool_list) {
		list_del(&cb->pool_list);
		cb_fini(hdev, cb);
	}

	return 0;
}

int hl_cb_va_pool_init(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	if (!hdev->supports_cb_mapping)
		return 0;

	ctx->cb_va_pool = gen_pool_create(__ffs(prop->pmmu.page_size), -1);
	if (!ctx->cb_va_pool) {
		dev_err(hdev->dev,
			"Failed to create VA gen pool for CB mapping\n");
		return -ENOMEM;
	}

	rc = gen_pool_add(ctx->cb_va_pool, prop->cb_va_start_addr,
			prop->cb_va_end_addr - prop->cb_va_start_addr, -1);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to add memory to VA gen pool for CB mapping\n");
		goto err_pool_destroy;
	}

	return 0;

err_pool_destroy:
	gen_pool_destroy(ctx->cb_va_pool);

	return rc;
}

void hl_cb_va_pool_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;

	if (!hdev->supports_cb_mapping)
		return;

	gen_pool_destroy(ctx->cb_va_pool);
}
