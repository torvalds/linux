// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/slab.h>

void hl_encaps_handle_do_release(struct kref *ref)
{
	struct hl_cs_encaps_sig_handle *handle =
		container_of(ref, struct hl_cs_encaps_sig_handle, refcount);
	struct hl_encaps_signals_mgr *mgr = &handle->ctx->sig_mgr;

	spin_lock(&mgr->lock);
	idr_remove(&mgr->handles, handle->id);
	spin_unlock(&mgr->lock);

	hl_ctx_put(handle->ctx);
	kfree(handle);
}

static void hl_encaps_handle_do_release_sob(struct kref *ref)
{
	struct hl_cs_encaps_sig_handle *handle =
		container_of(ref, struct hl_cs_encaps_sig_handle, refcount);
	struct hl_encaps_signals_mgr *mgr = &handle->ctx->sig_mgr;

	/* if we're here, then there was a signals reservation but cs with
	 * encaps signals wasn't submitted, so need to put refcount
	 * to hw_sob taken at the reservation.
	 */
	hw_sob_put(handle->hw_sob);

	spin_lock(&mgr->lock);
	idr_remove(&mgr->handles, handle->id);
	spin_unlock(&mgr->lock);

	hl_ctx_put(handle->ctx);
	kfree(handle);
}

static void hl_encaps_sig_mgr_init(struct hl_encaps_signals_mgr *mgr)
{
	spin_lock_init(&mgr->lock);
	idr_init(&mgr->handles);
}

static void hl_encaps_sig_mgr_fini(struct hl_device *hdev,
			struct hl_encaps_signals_mgr *mgr)
{
	struct hl_cs_encaps_sig_handle *handle;
	struct idr *idp;
	u32 id;

	idp = &mgr->handles;

	if (!idr_is_empty(idp)) {
		dev_warn(hdev->dev, "device released while some encaps signals handles are still allocated\n");
		idr_for_each_entry(idp, handle, id)
			kref_put(&handle->refcount,
					hl_encaps_handle_do_release_sob);
	}

	idr_destroy(&mgr->handles);
}

static void hl_ctx_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	int i;

	/* Release all allocated HW block mapped list entries and destroy
	 * the mutex.
	 */
	hl_hw_block_mem_fini(ctx);

	/*
	 * If we arrived here, there are no jobs waiting for this context
	 * on its queues so we can safely remove it.
	 * This is because for each CS, we increment the ref count and for
	 * every CS that was finished we decrement it and we won't arrive
	 * to this function unless the ref count is 0
	 */

	for (i = 0 ; i < hdev->asic_prop.max_pending_cs ; i++)
		hl_fence_put(ctx->cs_pending[i]);

	kfree(ctx->cs_pending);

	if (ctx->asid != HL_KERNEL_ASID_ID) {
		dev_dbg(hdev->dev, "closing user context %d\n", ctx->asid);

		/* The engines are stopped as there is no executing CS, but the
		 * Coresight might be still working by accessing addresses
		 * related to the stopped engines. Hence stop it explicitly.
		 */
		if (hdev->in_debug)
			hl_device_set_debug_mode(hdev, ctx, false);

		hdev->asic_funcs->ctx_fini(ctx);
		hl_cb_va_pool_fini(ctx);
		hl_vm_ctx_fini(ctx);
		hl_asid_free(hdev, ctx->asid);
		hl_encaps_sig_mgr_fini(hdev, &ctx->sig_mgr);

		/* Scrub both SRAM and DRAM */
		hdev->asic_funcs->scrub_device_mem(hdev, 0, 0);
	} else {
		dev_dbg(hdev->dev, "closing kernel context\n");
		hdev->asic_funcs->ctx_fini(ctx);
		hl_vm_ctx_fini(ctx);
		hl_mmu_ctx_fini(ctx);
	}
}

void hl_ctx_do_release(struct kref *ref)
{
	struct hl_ctx *ctx;

	ctx = container_of(ref, struct hl_ctx, refcount);

	hl_ctx_fini(ctx);

	if (ctx->hpriv)
		hl_hpriv_put(ctx->hpriv);

	kfree(ctx);
}

int hl_ctx_create(struct hl_device *hdev, struct hl_fpriv *hpriv)
{
	struct hl_ctx_mgr *mgr = &hpriv->ctx_mgr;
	struct hl_ctx *ctx;
	int rc;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		rc = -ENOMEM;
		goto out_err;
	}

	mutex_lock(&mgr->ctx_lock);
	rc = idr_alloc(&mgr->ctx_handles, ctx, 1, 0, GFP_KERNEL);
	mutex_unlock(&mgr->ctx_lock);

	if (rc < 0) {
		dev_err(hdev->dev, "Failed to allocate IDR for a new CTX\n");
		goto free_ctx;
	}

	ctx->handle = rc;

	rc = hl_ctx_init(hdev, ctx, false);
	if (rc)
		goto remove_from_idr;

	hl_hpriv_get(hpriv);
	ctx->hpriv = hpriv;

	/* TODO: remove for multiple contexts per process */
	hpriv->ctx = ctx;

	/* TODO: remove the following line for multiple process support */
	hdev->is_compute_ctx_active = true;

	return 0;

remove_from_idr:
	mutex_lock(&mgr->ctx_lock);
	idr_remove(&mgr->ctx_handles, ctx->handle);
	mutex_unlock(&mgr->ctx_lock);
free_ctx:
	kfree(ctx);
out_err:
	return rc;
}

int hl_ctx_init(struct hl_device *hdev, struct hl_ctx *ctx, bool is_kernel_ctx)
{
	int rc = 0;

	ctx->hdev = hdev;

	kref_init(&ctx->refcount);

	ctx->cs_sequence = 1;
	spin_lock_init(&ctx->cs_lock);
	atomic_set(&ctx->thread_ctx_switch_token, 1);
	ctx->thread_ctx_switch_wait_token = 0;
	ctx->cs_pending = kcalloc(hdev->asic_prop.max_pending_cs,
				sizeof(struct hl_fence *),
				GFP_KERNEL);
	if (!ctx->cs_pending)
		return -ENOMEM;

	hl_hw_block_mem_init(ctx);

	if (is_kernel_ctx) {
		ctx->asid = HL_KERNEL_ASID_ID; /* Kernel driver gets ASID 0 */
		rc = hl_vm_ctx_init(ctx);
		if (rc) {
			dev_err(hdev->dev, "Failed to init mem ctx module\n");
			rc = -ENOMEM;
			goto err_hw_block_mem_fini;
		}

		rc = hdev->asic_funcs->ctx_init(ctx);
		if (rc) {
			dev_err(hdev->dev, "ctx_init failed\n");
			goto err_vm_ctx_fini;
		}
	} else {
		ctx->asid = hl_asid_alloc(hdev);
		if (!ctx->asid) {
			dev_err(hdev->dev, "No free ASID, failed to create context\n");
			rc = -ENOMEM;
			goto err_hw_block_mem_fini;
		}

		rc = hl_vm_ctx_init(ctx);
		if (rc) {
			dev_err(hdev->dev, "Failed to init mem ctx module\n");
			rc = -ENOMEM;
			goto err_asid_free;
		}

		rc = hl_cb_va_pool_init(ctx);
		if (rc) {
			dev_err(hdev->dev,
				"Failed to init VA pool for mapped CB\n");
			goto err_vm_ctx_fini;
		}

		rc = hdev->asic_funcs->ctx_init(ctx);
		if (rc) {
			dev_err(hdev->dev, "ctx_init failed\n");
			goto err_cb_va_pool_fini;
		}

		hl_encaps_sig_mgr_init(&ctx->sig_mgr);

		dev_dbg(hdev->dev, "create user context %d\n", ctx->asid);
	}

	return 0;

err_cb_va_pool_fini:
	hl_cb_va_pool_fini(ctx);
err_vm_ctx_fini:
	hl_vm_ctx_fini(ctx);
err_asid_free:
	if (ctx->asid != HL_KERNEL_ASID_ID)
		hl_asid_free(hdev, ctx->asid);
err_hw_block_mem_fini:
	hl_hw_block_mem_fini(ctx);
	kfree(ctx->cs_pending);

	return rc;
}

void hl_ctx_get(struct hl_ctx *ctx)
{
	kref_get(&ctx->refcount);
}

int hl_ctx_put(struct hl_ctx *ctx)
{
	return kref_put(&ctx->refcount, hl_ctx_do_release);
}

struct hl_ctx *hl_get_compute_ctx(struct hl_device *hdev)
{
	struct hl_ctx *ctx = NULL;
	struct hl_fpriv *hpriv;

	mutex_lock(&hdev->fpriv_list_lock);

	list_for_each_entry(hpriv, &hdev->fpriv_list, dev_node) {
		/* There can only be a single user which has opened the compute device, so exit
		 * immediately once we find him
		 */
		ctx = hpriv->ctx;
		hl_ctx_get(ctx);
		break;
	}

	mutex_unlock(&hdev->fpriv_list_lock);

	return ctx;
}

/*
 * hl_ctx_get_fence_locked - get CS fence under CS lock
 *
 * @ctx: pointer to the context structure.
 * @seq: CS sequences number
 *
 * @return valid fence pointer on success, NULL if fence is gone, otherwise
 *         error pointer.
 *
 * NOTE: this function shall be called with cs_lock locked
 */
static struct hl_fence *hl_ctx_get_fence_locked(struct hl_ctx *ctx, u64 seq)
{
	struct asic_fixed_properties *asic_prop = &ctx->hdev->asic_prop;
	struct hl_fence *fence;

	if (seq >= ctx->cs_sequence)
		return ERR_PTR(-EINVAL);

	if (seq + asic_prop->max_pending_cs < ctx->cs_sequence)
		return NULL;

	fence = ctx->cs_pending[seq & (asic_prop->max_pending_cs - 1)];
	hl_fence_get(fence);
	return fence;
}

struct hl_fence *hl_ctx_get_fence(struct hl_ctx *ctx, u64 seq)
{
	struct hl_fence *fence;

	spin_lock(&ctx->cs_lock);

	fence = hl_ctx_get_fence_locked(ctx, seq);

	spin_unlock(&ctx->cs_lock);

	return fence;
}

/*
 * hl_ctx_get_fences - get multiple CS fences under the same CS lock
 *
 * @ctx: pointer to the context structure.
 * @seq_arr: array of CS sequences to wait for
 * @fence: fence array to store the CS fences
 * @arr_len: length of seq_arr and fence_arr
 *
 * @return 0 on success, otherwise non 0 error code
 */
int hl_ctx_get_fences(struct hl_ctx *ctx, u64 *seq_arr,
				struct hl_fence **fence, u32 arr_len)
{
	struct hl_fence **fence_arr_base = fence;
	int i, rc = 0;

	spin_lock(&ctx->cs_lock);

	for (i = 0; i < arr_len; i++, fence++) {
		u64 seq = seq_arr[i];

		*fence = hl_ctx_get_fence_locked(ctx, seq);

		if (IS_ERR(*fence)) {
			dev_err(ctx->hdev->dev,
				"Failed to get fence for CS with seq 0x%llx\n",
					seq);
			rc = PTR_ERR(*fence);
			break;
		}
	}

	spin_unlock(&ctx->cs_lock);

	if (rc)
		hl_fences_put(fence_arr_base, i);

	return rc;
}

/*
 * hl_ctx_mgr_init - initialize the context manager
 *
 * @mgr: pointer to context manager structure
 *
 * This manager is an object inside the hpriv object of the user process.
 * The function is called when a user process opens the FD.
 */
void hl_ctx_mgr_init(struct hl_ctx_mgr *mgr)
{
	mutex_init(&mgr->ctx_lock);
	idr_init(&mgr->ctx_handles);
}

/*
 * hl_ctx_mgr_fini - finalize the context manager
 *
 * @hdev: pointer to device structure
 * @mgr: pointer to context manager structure
 *
 * This function goes over all the contexts in the manager and frees them.
 * It is called when a process closes the FD.
 */
void hl_ctx_mgr_fini(struct hl_device *hdev, struct hl_ctx_mgr *mgr)
{
	struct hl_ctx *ctx;
	struct idr *idp;
	u32 id;

	idp = &mgr->ctx_handles;

	idr_for_each_entry(idp, ctx, id)
		kref_put(&ctx->refcount, hl_ctx_do_release);

	idr_destroy(&mgr->ctx_handles);
	mutex_destroy(&mgr->ctx_lock);
}
