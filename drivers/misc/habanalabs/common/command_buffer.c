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
#include <linux/genalloc.h>

static void cb_fini(struct hl_device *hdev, struct hl_cb *cb)
{
	if (cb->is_internal)
		gen_pool_free(hdev->internal_cb_pool,
				cb->kernel_address, cb->size);
	else
		hdev->asic_funcs->asic_dma_free_coherent(hdev, cb->size,
				(void *) (uintptr_t) cb->kernel_address,
				cb->bus_address);

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

	cb_do_release(hdev, cb);
}

static struct hl_cb *hl_cb_alloc(struct hl_device *hdev, u32 cb_size,
					int ctx_id, bool internal_cb)
{
	struct hl_cb *cb;
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
	if (ctx_id == HL_KERNEL_ASID_ID)
		cb = kzalloc(sizeof(*cb), GFP_ATOMIC);
	else
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

	cb->kernel_address = (u64) (uintptr_t) p;
	cb->size = cb_size;

	return cb;
}

int hl_cb_create(struct hl_device *hdev, struct hl_cb_mgr *mgr,
			u32 cb_size, u64 *handle, int ctx_id, bool internal_cb)
{
	struct hl_cb *cb;
	bool alloc_new_cb = true;
	int rc;

	/*
	 * Can't use generic function to check this because of special case
	 * where we create a CB as part of the reset process
	 */
	if ((hdev->disabled) || ((atomic_read(&hdev->in_reset)) &&
					(ctx_id != HL_KERNEL_ASID_ID))) {
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
	cb->ctx_id = ctx_id;

	spin_lock(&mgr->cb_lock);
	rc = idr_alloc(&mgr->cb_handles, cb, 1, 0, GFP_ATOMIC);
	spin_unlock(&mgr->cb_lock);

	if (rc < 0) {
		dev_err(hdev->dev, "Failed to allocate IDR for a new CB\n");
		goto release_cb;
	}

	cb->id = rc;

	kref_init(&cb->refcount);
	spin_lock_init(&cb->lock);

	/*
	 * idr is 32-bit so we can safely OR it with a mask that is above
	 * 32 bit
	 */
	*handle = cb->id | HL_MMAP_CB_MASK;
	*handle <<= PAGE_SHIFT;

	hl_debugfs_add_cb(cb);

	return 0;

release_cb:
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

int hl_cb_ioctl(struct hl_fpriv *hpriv, void *data)
{
	union hl_cb_args *args = data;
	struct hl_device *hdev = hpriv->hdev;
	u64 handle = 0;
	int rc;

	if (hl_device_disabled_or_in_reset(hdev)) {
		dev_warn_ratelimited(hdev->dev,
			"Device is %s. Can't execute CB IOCTL\n",
			atomic_read(&hdev->in_reset) ? "in_reset" : "disabled");
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
			rc = hl_cb_create(hdev, &hpriv->cb_mgr,
					args->in.cb_size, &handle,
					hpriv->ctx->asid, false);
		}

		memset(args, 0, sizeof(*args));
		args->out.cb_handle = handle;
		break;

	case HL_CB_OP_DESTROY:
		rc = hl_cb_destroy(hdev, &hpriv->cb_mgr,
					args->in.cb_handle);
		break;

	default:
		rc = -ENOTTY;
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
	phys_addr_t address;
	u32 handle, user_cb_size;
	int rc;

	handle = vma->vm_pgoff;

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

	/* Calculate address for CB */
	address = virt_to_phys((void *) (uintptr_t) cb->kernel_address);

	rc = hdev->asic_funcs->cb_mmap(hdev, vma, cb->kernel_address,
					address, cb->size);

	if (rc) {
		spin_lock(&cb->lock);
		cb->mmap = false;
		goto release_lock;
	}

	cb->mmap_size = cb->size;

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
				id, cb->ctx_id);
	}

	idr_destroy(&mgr->cb_handles);
}

struct hl_cb *hl_cb_kernel_create(struct hl_device *hdev, u32 cb_size,
					bool internal_cb)
{
	u64 cb_handle;
	struct hl_cb *cb;
	int rc;

	rc = hl_cb_create(hdev, &hdev->kernel_cb_mgr, cb_size, &cb_handle,
			HL_KERNEL_ASID_ID, internal_cb);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to allocate CB for the kernel driver %d\n", rc);
		return NULL;
	}

	cb_handle >>= PAGE_SHIFT;
	cb = hl_cb_get(hdev, &hdev->kernel_cb_mgr, (u32) cb_handle);
	/* hl_cb_get should never fail here so use kernel WARN */
	WARN(!cb, "Kernel CB handle invalid 0x%x\n", (u32) cb_handle);
	if (!cb)
		goto destroy_cb;

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
