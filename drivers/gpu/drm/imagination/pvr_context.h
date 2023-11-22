/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_CONTEXT_H
#define PVR_CONTEXT_H

#include <drm/gpu_scheduler.h>

#include <linux/compiler_attributes.h>
#include <linux/dma-fence.h>
#include <linux/kref.h>
#include <linux/types.h>
#include <linux/xarray.h>
#include <uapi/drm/pvr_drm.h>

#include "pvr_cccb.h"
#include "pvr_device.h"
#include "pvr_queue.h"

/* Forward declaration from pvr_gem.h. */
struct pvr_fw_object;

enum pvr_context_priority {
	PVR_CTX_PRIORITY_LOW = 0,
	PVR_CTX_PRIORITY_MEDIUM,
	PVR_CTX_PRIORITY_HIGH,
};

/**
 * struct pvr_context - Context data
 */
struct pvr_context {
	/** @ref_count: Refcount for context. */
	struct kref ref_count;

	/** @pvr_dev: Pointer to owning device. */
	struct pvr_device *pvr_dev;

	/** @vm_ctx: Pointer to associated VM context. */
	struct pvr_vm_context *vm_ctx;

	/** @type: Type of context. */
	enum drm_pvr_ctx_type type;

	/** @flags: Context flags. */
	u32 flags;

	/** @priority: Context priority*/
	enum pvr_context_priority priority;

	/** @fw_obj: FW object representing FW-side context data. */
	struct pvr_fw_object *fw_obj;

	/** @data: Pointer to local copy of FW context data. */
	void *data;

	/** @data_size: Size of FW context data, in bytes. */
	u32 data_size;

	/** @ctx_id: FW context ID. */
	u32 ctx_id;

	/**
	 * @faulty: Set to 1 when the context queues had unfinished job when
	 * a GPU reset happened.
	 *
	 * In that case, the context is in an inconsistent state and can't be
	 * used anymore.
	 */
	atomic_t faulty;

	/** @queues: Union containing all kind of queues. */
	union {
		struct {
			/** @geometry: Geometry queue. */
			struct pvr_queue *geometry;

			/** @fragment: Fragment queue. */
			struct pvr_queue *fragment;
		};

		/** @compute: Compute queue. */
		struct pvr_queue *compute;

		/** @compute: Transfer queue. */
		struct pvr_queue *transfer;
	} queues;
};

static __always_inline struct pvr_queue *
pvr_context_get_queue_for_job(struct pvr_context *ctx, enum drm_pvr_job_type type)
{
	switch (type) {
	case DRM_PVR_JOB_TYPE_GEOMETRY:
		return ctx->type == DRM_PVR_CTX_TYPE_RENDER ? ctx->queues.geometry : NULL;
	case DRM_PVR_JOB_TYPE_FRAGMENT:
		return ctx->type == DRM_PVR_CTX_TYPE_RENDER ? ctx->queues.fragment : NULL;
	case DRM_PVR_JOB_TYPE_COMPUTE:
		return ctx->type == DRM_PVR_CTX_TYPE_COMPUTE ? ctx->queues.compute : NULL;
	case DRM_PVR_JOB_TYPE_TRANSFER_FRAG:
		return ctx->type == DRM_PVR_CTX_TYPE_TRANSFER_FRAG ? ctx->queues.transfer : NULL;
	}

	return NULL;
}

/**
 * pvr_context_get() - Take additional reference on context.
 * @ctx: Context pointer.
 *
 * Call pvr_context_put() to release.
 *
 * Returns:
 *  * The requested context on success, or
 *  * %NULL if no context pointer passed.
 */
static __always_inline struct pvr_context *
pvr_context_get(struct pvr_context *ctx)
{
	if (ctx)
		kref_get(&ctx->ref_count);

	return ctx;
}

/**
 * pvr_context_lookup() - Lookup context pointer from handle and file.
 * @pvr_file: Pointer to pvr_file structure.
 * @handle: Context handle.
 *
 * Takes reference on context. Call pvr_context_put() to release.
 *
 * Return:
 *  * The requested context on success, or
 *  * %NULL on failure (context does not exist, or does not belong to @pvr_file).
 */
static __always_inline struct pvr_context *
pvr_context_lookup(struct pvr_file *pvr_file, u32 handle)
{
	struct pvr_context *ctx;

	/* Take the array lock to protect against context removal.  */
	xa_lock(&pvr_file->ctx_handles);
	ctx = pvr_context_get(xa_load(&pvr_file->ctx_handles, handle));
	xa_unlock(&pvr_file->ctx_handles);

	return ctx;
}

/**
 * pvr_context_lookup_id() - Lookup context pointer from ID.
 * @pvr_dev: Device pointer.
 * @id: FW context ID.
 *
 * Takes reference on context. Call pvr_context_put() to release.
 *
 * Return:
 *  * The requested context on success, or
 *  * %NULL on failure (context does not exist).
 */
static __always_inline struct pvr_context *
pvr_context_lookup_id(struct pvr_device *pvr_dev, u32 id)
{
	struct pvr_context *ctx;

	/* Take the array lock to protect against context removal.  */
	xa_lock(&pvr_dev->ctx_ids);

	/* Contexts are removed from the ctx_ids set in the context release path,
	 * meaning the ref_count reached zero before they get removed. We need
	 * to make sure we're not trying to acquire a context that's being
	 * destroyed.
	 */
	ctx = xa_load(&pvr_dev->ctx_ids, id);
	if (!kref_get_unless_zero(&ctx->ref_count))
		ctx = NULL;

	xa_unlock(&pvr_dev->ctx_ids);

	return ctx;
}

static __always_inline u32
pvr_context_get_fw_addr(struct pvr_context *ctx)
{
	u32 ctx_fw_addr = 0;

	pvr_fw_object_get_fw_addr(ctx->fw_obj, &ctx_fw_addr);

	return ctx_fw_addr;
}

void pvr_context_put(struct pvr_context *ctx);

int pvr_context_create(struct pvr_file *pvr_file, struct drm_pvr_ioctl_create_context_args *args);

int pvr_context_destroy(struct pvr_file *pvr_file, u32 handle);

void pvr_destroy_contexts_for_file(struct pvr_file *pvr_file);

void pvr_context_device_init(struct pvr_device *pvr_dev);

void pvr_context_device_fini(struct pvr_device *pvr_dev);

#endif /* PVR_CONTEXT_H */
