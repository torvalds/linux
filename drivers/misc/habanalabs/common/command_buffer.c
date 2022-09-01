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
		hl_asic_dma_free_coherent(hdev, cb->size, cb->kernel_address, cb->bus_address);

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
		p = hl_asic_dma_alloc_coherent(hdev, cb_size, &cb->bus_address, GFP_ATOMIC);
		if (!p)
			p = hl_asic_dma_alloc_coherent(hdev, cb_size, &cb->bus_address, GFP_KERNEL);
	} else {
		p = hl_asic_dma_alloc_coherent(hdev, cb_size, &cb->bus_address,
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

struct hl_cb_mmap_mem_alloc_args {
	struct hl_device *hdev;
	struct hl_ctx *ctx;
	u32 cb_size;
	bool internal_cb;
	bool map_cb;
};

static void hl_cb_mmap_mem_release(struct hl_mmap_mem_buf *buf)
{
	struct hl_cb *cb = buf->private;

	hl_debugfs_remove_cb(cb);

	if (cb->is_mmu_mapped)
		cb_unmap_mem(cb->ctx, cb);

	hl_ctx_put(cb->ctx);

	cb_do_release(cb->hdev, cb);
}

static int hl_cb_mmap_mem_alloc(struct hl_mmap_mem_buf *buf, gfp_t gfp, void *args)
{
	struct hl_cb_mmap_mem_alloc_args *cb_args = args;
	struct hl_cb *cb;
	int rc, ctx_id = cb_args->ctx->asid;
	bool alloc_new_cb = true;

	if (!cb_args->internal_cb) {
		/* Minimum allocation must be PAGE SIZE */
		if (cb_args->cb_size < PAGE_SIZE)
			cb_args->cb_size = PAGE_SIZE;

		if (ctx_id == HL_KERNEL_ASID_ID &&
				cb_args->cb_size <= cb_args->hdev->asic_prop.cb_pool_cb_size) {

			spin_lock(&cb_args->hdev->cb_pool_lock);
			if (!list_empty(&cb_args->hdev->cb_pool)) {
				cb = list_first_entry(&cb_args->hdev->cb_pool,
						typeof(*cb), pool_list);
				list_del(&cb->pool_list);
				spin_unlock(&cb_args->hdev->cb_pool_lock);
				alloc_new_cb = false;
			} else {
				spin_unlock(&cb_args->hdev->cb_pool_lock);
				dev_dbg(cb_args->hdev->dev, "CB pool is empty\n");
			}
		}
	}

	if (alloc_new_cb) {
		cb = hl_cb_alloc(cb_args->hdev, cb_args->cb_size, ctx_id, cb_args->internal_cb);
		if (!cb)
			return -ENOMEM;
	}

	cb->hdev = cb_args->hdev;
	cb->ctx = cb_args->ctx;
	cb->buf = buf;
	cb->buf->mappable_size = cb->size;
	cb->buf->private = cb;

	hl_ctx_get(cb->ctx);

	if (cb_args->map_cb) {
		if (ctx_id == HL_KERNEL_ASID_ID) {
			dev_err(cb_args->hdev->dev,
				"CB mapping is not supported for kernel context\n");
			rc = -EINVAL;
			goto release_cb;
		}

		rc = cb_map_mem(cb_args->ctx, cb);
		if (rc)
			goto release_cb;
	}

	hl_debugfs_add_cb(cb);

	return 0;

release_cb:
	hl_ctx_put(cb->ctx);
	cb_do_release(cb_args->hdev, cb);

	return rc;
}

static int hl_cb_mmap(struct hl_mmap_mem_buf *buf,
				      struct vm_area_struct *vma, void *args)
{
	struct hl_cb *cb = buf->private;

	return cb->hdev->asic_funcs->mmap(cb->hdev, vma, cb->kernel_address,
					cb->bus_address, cb->size);
}

static struct hl_mmap_mem_buf_behavior cb_behavior = {
	.topic = "CB",
	.mem_id = HL_MMAP_TYPE_CB,
	.alloc = hl_cb_mmap_mem_alloc,
	.release = hl_cb_mmap_mem_release,
	.mmap = hl_cb_mmap,
};

int hl_cb_create(struct hl_device *hdev, struct hl_mem_mgr *mmg,
			struct hl_ctx *ctx, u32 cb_size, bool internal_cb,
			bool map_cb, u64 *handle)
{
	struct hl_cb_mmap_mem_alloc_args args = {
		.hdev = hdev,
		.ctx = ctx,
		.cb_size = cb_size,
		.internal_cb = internal_cb,
		.map_cb = map_cb,
	};
	struct hl_mmap_mem_buf *buf;
	int ctx_id = ctx->asid;

	if ((hdev->disabled) || (hdev->reset_info.in_reset && (ctx_id != HL_KERNEL_ASID_ID))) {
		dev_warn_ratelimited(hdev->dev,
			"Device is disabled or in reset. Can't create new CBs\n");
		return -EBUSY;
	}

	if (cb_size > SZ_2M) {
		dev_err(hdev->dev, "CB size %d must be less than %d\n",
			cb_size, SZ_2M);
		return -EINVAL;
	}

	buf = hl_mmap_mem_buf_alloc(
		mmg, &cb_behavior,
		ctx_id == HL_KERNEL_ASID_ID ? GFP_ATOMIC : GFP_KERNEL, &args);
	if (!buf)
		return -ENOMEM;

	*handle = buf->handle;

	return 0;
}

int hl_cb_destroy(struct hl_mem_mgr *mmg, u64 cb_handle)
{
	int rc;

	rc = hl_mmap_mem_buf_put_handle(mmg, cb_handle);
	if (rc < 0)
		return rc; /* Invalid handle */

	if (rc == 0)
		dev_dbg(mmg->dev, "CB 0x%llx is destroyed while still in use\n", cb_handle);

	return 0;
}

static int hl_cb_info(struct hl_mem_mgr *mmg,
			u64 handle, u32 flags, u32 *usage_cnt, u64 *device_va)
{
	struct hl_vm_va_block *va_block;
	struct hl_cb *cb;
	int rc = 0;

	cb = hl_cb_get(mmg, handle);
	if (!cb) {
		dev_err(mmg->dev,
			"CB info failed, no match to handle 0x%llx\n", handle);
		return -EINVAL;
	}

	if (flags & HL_CB_FLAGS_GET_DEVICE_VA) {
		va_block = list_first_entry(&cb->va_block_list, struct hl_vm_va_block, node);
		if (va_block) {
			*device_va = va_block->start;
		} else {
			dev_err(mmg->dev, "CB is not mapped to the device's MMU\n");
			rc = -EINVAL;
			goto out;
		}
	} else {
		*usage_cnt = atomic_read(&cb->cs_cnt);
	}

out:
	hl_cb_put(cb);
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
			rc = hl_cb_create(hdev, &hpriv->mem_mgr, hpriv->ctx,
					args->in.cb_size, false,
					!!(args->in.flags & HL_CB_FLAGS_MAP),
					&handle);
		}

		memset(args, 0, sizeof(*args));
		args->out.cb_handle = handle;
		break;

	case HL_CB_OP_DESTROY:
		rc = hl_cb_destroy(&hpriv->mem_mgr,
					args->in.cb_handle);
		break;

	case HL_CB_OP_INFO:
		rc = hl_cb_info(&hpriv->mem_mgr, args->in.cb_handle,
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

struct hl_cb *hl_cb_get(struct hl_mem_mgr *mmg, u64 handle)
{
	struct hl_mmap_mem_buf *buf;

	buf = hl_mmap_mem_buf_get(mmg, handle);
	if (!buf)
		return NULL;
	return buf->private;

}

void hl_cb_put(struct hl_cb *cb)
{
	hl_mmap_mem_buf_put(cb->buf);
}

struct hl_cb *hl_cb_kernel_create(struct hl_device *hdev, u32 cb_size,
					bool internal_cb)
{
	u64 cb_handle;
	struct hl_cb *cb;
	int rc;

	rc = hl_cb_create(hdev, &hdev->kernel_mem_mgr, hdev->kernel_ctx, cb_size,
				internal_cb, false, &cb_handle);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to allocate CB for the kernel driver %d\n", rc);
		return NULL;
	}

	cb = hl_cb_get(&hdev->kernel_mem_mgr, cb_handle);
	/* hl_cb_get should never fail here */
	if (!cb) {
		dev_crit(hdev->dev, "Kernel CB handle invalid 0x%x\n",
				(u32) cb_handle);
		goto destroy_cb;
	}

	return cb;

destroy_cb:
	hl_cb_destroy(&hdev->kernel_mem_mgr, cb_handle);

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
