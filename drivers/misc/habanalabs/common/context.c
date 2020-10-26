// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"

#include <linux/slab.h>

static void hl_ctx_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	u64 idle_mask = 0;
	int i;

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
		 * Stop only if this is the compute context, as there can be
		 * only one compute context
		 */
		if ((hdev->in_debug) && (hdev->compute_ctx == ctx))
			hl_device_set_debug_mode(hdev, false);

		hl_cb_va_pool_fini(ctx);
		hl_vm_ctx_fini(ctx);
		hl_asid_free(hdev, ctx->asid);

		if ((!hdev->pldm) && (hdev->pdev) &&
				(!hdev->asic_funcs->is_device_idle(hdev,
							&idle_mask, NULL)))
			dev_notice(hdev->dev,
				"device not idle after user context is closed (0x%llx)\n",
				idle_mask);
	} else {
		dev_dbg(hdev->dev, "closing kernel context\n");
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
	hdev->compute_ctx = ctx;

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

void hl_ctx_free(struct hl_device *hdev, struct hl_ctx *ctx)
{
	if (kref_put(&ctx->refcount, hl_ctx_do_release) == 1)
		return;

	dev_warn(hdev->dev,
		"user process released device but its command submissions are still executing\n");
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

	if (is_kernel_ctx) {
		ctx->asid = HL_KERNEL_ASID_ID; /* Kernel driver gets ASID 0 */
		rc = hl_mmu_ctx_init(ctx);
		if (rc) {
			dev_err(hdev->dev, "Failed to init mmu ctx module\n");
			goto err_free_cs_pending;
		}
	} else {
		ctx->asid = hl_asid_alloc(hdev);
		if (!ctx->asid) {
			dev_err(hdev->dev, "No free ASID, failed to create context\n");
			rc = -ENOMEM;
			goto err_free_cs_pending;
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

		dev_dbg(hdev->dev, "create user context %d\n", ctx->asid);
	}

	return 0;

err_cb_va_pool_fini:
	hl_cb_va_pool_fini(ctx);
err_vm_ctx_fini:
	hl_vm_ctx_fini(ctx);
err_asid_free:
	hl_asid_free(hdev, ctx->asid);
err_free_cs_pending:
	kfree(ctx->cs_pending);

	return rc;
}

void hl_ctx_get(struct hl_device *hdev, struct hl_ctx *ctx)
{
	kref_get(&ctx->refcount);
}

int hl_ctx_put(struct hl_ctx *ctx)
{
	return kref_put(&ctx->refcount, hl_ctx_do_release);
}

struct hl_fence *hl_ctx_get_fence(struct hl_ctx *ctx, u64 seq)
{
	struct asic_fixed_properties *asic_prop = &ctx->hdev->asic_prop;
	struct hl_fence *fence;

	spin_lock(&ctx->cs_lock);

	if (seq >= ctx->cs_sequence) {
		spin_unlock(&ctx->cs_lock);
		return ERR_PTR(-EINVAL);
	}

	if (seq + asic_prop->max_pending_cs < ctx->cs_sequence) {
		spin_unlock(&ctx->cs_lock);
		return NULL;
	}

	fence = ctx->cs_pending[seq & (asic_prop->max_pending_cs - 1)];
	hl_fence_get(fence);

	spin_unlock(&ctx->cs_lock);

	return fence;
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
		hl_ctx_free(hdev, ctx);

	idr_destroy(&mgr->ctx_handles);
	mutex_destroy(&mgr->ctx_lock);
}
