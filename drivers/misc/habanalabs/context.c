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

	if (ctx->asid != HL_KERNEL_ASID_ID)
		hl_asid_free(hdev, ctx->asid);
}

void hl_ctx_do_release(struct kref *ref)
{
	struct hl_ctx *ctx;

	ctx = container_of(ref, struct hl_ctx, refcount);

	dev_dbg(ctx->hdev->dev, "Now really releasing context %d\n", ctx->asid);

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

	rc = hl_ctx_init(hdev, ctx, false);
	if (rc)
		goto free_ctx;

	hl_hpriv_get(hpriv);
	ctx->hpriv = hpriv;

	/* TODO: remove for multiple contexts */
	hpriv->ctx = ctx;
	hdev->user_ctx = ctx;

	mutex_lock(&mgr->ctx_lock);
	rc = idr_alloc(&mgr->ctx_handles, ctx, 1, 0, GFP_KERNEL);
	mutex_unlock(&mgr->ctx_lock);

	if (rc < 0) {
		dev_err(hdev->dev, "Failed to allocate IDR for a new CTX\n");
		hl_ctx_free(hdev, ctx);
		goto out_err;
	}

	return 0;

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
		"Context %d closed or terminated but its CS are executing\n",
		ctx->asid);
}

int hl_ctx_init(struct hl_device *hdev, struct hl_ctx *ctx, bool is_kernel_ctx)
{
	ctx->hdev = hdev;

	kref_init(&ctx->refcount);

	if (is_kernel_ctx) {
		ctx->asid = HL_KERNEL_ASID_ID; /* KMD gets ASID 0 */
	} else {
		ctx->asid = hl_asid_alloc(hdev);
		if (!ctx->asid) {
			dev_err(hdev->dev, "No free ASID, failed to create context\n");
			return -ENOMEM;
		}
	}

	dev_dbg(hdev->dev, "Created context with ASID %u\n", ctx->asid);

	return 0;
}

void hl_ctx_get(struct hl_device *hdev, struct hl_ctx *ctx)
{
	kref_get(&ctx->refcount);
}

int hl_ctx_put(struct hl_ctx *ctx)
{
	return kref_put(&ctx->refcount, hl_ctx_do_release);
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
