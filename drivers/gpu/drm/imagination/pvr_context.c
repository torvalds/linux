// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_cccb.h"
#include "pvr_context.h"
#include "pvr_device.h"
#include "pvr_drv.h"
#include "pvr_gem.h"
#include "pvr_job.h"
#include "pvr_power.h"
#include "pvr_rogue_fwif.h"
#include "pvr_rogue_fwif_common.h"
#include "pvr_rogue_fwif_resetframework.h"
#include "pvr_stream.h"
#include "pvr_stream_defs.h"
#include "pvr_vm.h"

#include <drm/drm_auth.h>
#include <drm/drm_managed.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/xarray.h>

static int
remap_priority(struct pvr_file *pvr_file, s32 uapi_priority,
	       enum pvr_context_priority *priority_out)
{
	switch (uapi_priority) {
	case DRM_PVR_CTX_PRIORITY_LOW:
		*priority_out = PVR_CTX_PRIORITY_LOW;
		break;
	case DRM_PVR_CTX_PRIORITY_NORMAL:
		*priority_out = PVR_CTX_PRIORITY_MEDIUM;
		break;
	case DRM_PVR_CTX_PRIORITY_HIGH:
		if (!capable(CAP_SYS_NICE) && !drm_is_current_master(from_pvr_file(pvr_file)))
			return -EACCES;
		*priority_out = PVR_CTX_PRIORITY_HIGH;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int get_fw_obj_size(enum drm_pvr_ctx_type type)
{
	switch (type) {
	case DRM_PVR_CTX_TYPE_RENDER:
		return sizeof(struct rogue_fwif_fwrendercontext);
	case DRM_PVR_CTX_TYPE_COMPUTE:
		return sizeof(struct rogue_fwif_fwcomputecontext);
	case DRM_PVR_CTX_TYPE_TRANSFER_FRAG:
		return sizeof(struct rogue_fwif_fwtransfercontext);
	}

	return -EINVAL;
}

static int
process_static_context_state(struct pvr_device *pvr_dev, const struct pvr_stream_cmd_defs *cmd_defs,
			     u64 stream_user_ptr, u32 stream_size, void *dest)
{
	void *stream;
	int err;

	stream = kzalloc(stream_size, GFP_KERNEL);
	if (!stream)
		return -ENOMEM;

	if (copy_from_user(stream, u64_to_user_ptr(stream_user_ptr), stream_size)) {
		err = -EFAULT;
		goto err_free;
	}

	err = pvr_stream_process(pvr_dev, cmd_defs, stream, stream_size, dest);
	if (err)
		goto err_free;

	kfree(stream);

	return 0;

err_free:
	kfree(stream);

	return err;
}

static int init_render_fw_objs(struct pvr_context *ctx,
			       struct drm_pvr_ioctl_create_context_args *args,
			       void *fw_ctx_map)
{
	struct rogue_fwif_static_rendercontext_state *static_rendercontext_state;
	struct rogue_fwif_fwrendercontext *fw_render_context = fw_ctx_map;

	if (!args->static_context_state_len)
		return -EINVAL;

	static_rendercontext_state = &fw_render_context->static_render_context_state;

	/* Copy static render context state from userspace. */
	return process_static_context_state(ctx->pvr_dev,
					    &pvr_static_render_context_state_stream,
					    args->static_context_state,
					    args->static_context_state_len,
					    &static_rendercontext_state->ctxswitch_regs[0]);
}

static int init_compute_fw_objs(struct pvr_context *ctx,
				struct drm_pvr_ioctl_create_context_args *args,
				void *fw_ctx_map)
{
	struct rogue_fwif_fwcomputecontext *fw_compute_context = fw_ctx_map;
	struct rogue_fwif_cdm_registers_cswitch *ctxswitch_regs;

	if (!args->static_context_state_len)
		return -EINVAL;

	ctxswitch_regs = &fw_compute_context->static_compute_context_state.ctxswitch_regs;

	/* Copy static render context state from userspace. */
	return process_static_context_state(ctx->pvr_dev,
					    &pvr_static_compute_context_state_stream,
					    args->static_context_state,
					    args->static_context_state_len,
					    ctxswitch_regs);
}

static int init_transfer_fw_objs(struct pvr_context *ctx,
				 struct drm_pvr_ioctl_create_context_args *args,
				 void *fw_ctx_map)
{
	if (args->static_context_state_len)
		return -EINVAL;

	return 0;
}

static int init_fw_objs(struct pvr_context *ctx,
			struct drm_pvr_ioctl_create_context_args *args,
			void *fw_ctx_map)
{
	switch (ctx->type) {
	case DRM_PVR_CTX_TYPE_RENDER:
		return init_render_fw_objs(ctx, args, fw_ctx_map);
	case DRM_PVR_CTX_TYPE_COMPUTE:
		return init_compute_fw_objs(ctx, args, fw_ctx_map);
	case DRM_PVR_CTX_TYPE_TRANSFER_FRAG:
		return init_transfer_fw_objs(ctx, args, fw_ctx_map);
	}

	return -EINVAL;
}

static void
ctx_fw_data_init(void *cpu_ptr, void *priv)
{
	struct pvr_context *ctx = priv;

	memcpy(cpu_ptr, ctx->data, ctx->data_size);
}

/**
 * pvr_context_destroy_queues() - Destroy all queues attached to a context.
 * @ctx: Context to destroy queues on.
 *
 * Should be called when the last reference to a context object is dropped.
 * It releases all resources attached to the queues bound to this context.
 */
static void pvr_context_destroy_queues(struct pvr_context *ctx)
{
	switch (ctx->type) {
	case DRM_PVR_CTX_TYPE_RENDER:
		pvr_queue_destroy(ctx->queues.fragment);
		pvr_queue_destroy(ctx->queues.geometry);
		break;
	case DRM_PVR_CTX_TYPE_COMPUTE:
		pvr_queue_destroy(ctx->queues.compute);
		break;
	case DRM_PVR_CTX_TYPE_TRANSFER_FRAG:
		pvr_queue_destroy(ctx->queues.transfer);
		break;
	}
}

/**
 * pvr_context_create_queues() - Create all queues attached to a context.
 * @ctx: Context to create queues on.
 * @args: Context creation arguments passed by userspace.
 * @fw_ctx_map: CPU mapping of the FW context object.
 *
 * Return:
 *  * 0 on success, or
 *  * A negative error code otherwise.
 */
static int pvr_context_create_queues(struct pvr_context *ctx,
				     struct drm_pvr_ioctl_create_context_args *args,
				     void *fw_ctx_map)
{
	int err;

	switch (ctx->type) {
	case DRM_PVR_CTX_TYPE_RENDER:
		ctx->queues.geometry = pvr_queue_create(ctx, DRM_PVR_JOB_TYPE_GEOMETRY,
							args, fw_ctx_map);
		if (IS_ERR(ctx->queues.geometry)) {
			err = PTR_ERR(ctx->queues.geometry);
			ctx->queues.geometry = NULL;
			goto err_destroy_queues;
		}

		ctx->queues.fragment = pvr_queue_create(ctx, DRM_PVR_JOB_TYPE_FRAGMENT,
							args, fw_ctx_map);
		if (IS_ERR(ctx->queues.fragment)) {
			err = PTR_ERR(ctx->queues.fragment);
			ctx->queues.fragment = NULL;
			goto err_destroy_queues;
		}
		return 0;

	case DRM_PVR_CTX_TYPE_COMPUTE:
		ctx->queues.compute = pvr_queue_create(ctx, DRM_PVR_JOB_TYPE_COMPUTE,
						       args, fw_ctx_map);
		if (IS_ERR(ctx->queues.compute)) {
			err = PTR_ERR(ctx->queues.compute);
			ctx->queues.compute = NULL;
			goto err_destroy_queues;
		}
		return 0;

	case DRM_PVR_CTX_TYPE_TRANSFER_FRAG:
		ctx->queues.transfer = pvr_queue_create(ctx, DRM_PVR_JOB_TYPE_TRANSFER_FRAG,
							args, fw_ctx_map);
		if (IS_ERR(ctx->queues.transfer)) {
			err = PTR_ERR(ctx->queues.transfer);
			ctx->queues.transfer = NULL;
			goto err_destroy_queues;
		}
		return 0;
	}

	return -EINVAL;

err_destroy_queues:
	pvr_context_destroy_queues(ctx);
	return err;
}

/**
 * pvr_context_kill_queues() - Kill queues attached to context.
 * @ctx: Context to kill queues on.
 *
 * Killing the queues implies making them unusable for future jobs, while still
 * letting the currently submitted jobs a chance to finish. Queue resources will
 * stay around until pvr_context_destroy_queues() is called.
 */
static void pvr_context_kill_queues(struct pvr_context *ctx)
{
	switch (ctx->type) {
	case DRM_PVR_CTX_TYPE_RENDER:
		pvr_queue_kill(ctx->queues.fragment);
		pvr_queue_kill(ctx->queues.geometry);
		break;
	case DRM_PVR_CTX_TYPE_COMPUTE:
		pvr_queue_kill(ctx->queues.compute);
		break;
	case DRM_PVR_CTX_TYPE_TRANSFER_FRAG:
		pvr_queue_kill(ctx->queues.transfer);
		break;
	}
}

/**
 * pvr_context_create() - Create a context.
 * @pvr_file: File to attach the created context to.
 * @args: Context creation arguments.
 *
 * Return:
 *  * 0 on success, or
 *  * A negative error code on failure.
 */
int pvr_context_create(struct pvr_file *pvr_file, struct drm_pvr_ioctl_create_context_args *args)
{
	struct pvr_device *pvr_dev = pvr_file->pvr_dev;
	struct pvr_context *ctx;
	int ctx_size;
	int err;

	/* Context creation flags are currently unused and must be zero. */
	if (args->flags)
		return -EINVAL;

	ctx_size = get_fw_obj_size(args->type);
	if (ctx_size < 0)
		return ctx_size;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->data_size = ctx_size;
	ctx->type = args->type;
	ctx->flags = args->flags;
	ctx->pvr_dev = pvr_dev;
	kref_init(&ctx->ref_count);

	err = remap_priority(pvr_file, args->priority, &ctx->priority);
	if (err)
		goto err_free_ctx;

	ctx->vm_ctx = pvr_vm_context_lookup(pvr_file, args->vm_context_handle);
	if (IS_ERR(ctx->vm_ctx)) {
		err = PTR_ERR(ctx->vm_ctx);
		goto err_free_ctx;
	}

	ctx->data = kzalloc(ctx_size, GFP_KERNEL);
	if (!ctx->data) {
		err = -ENOMEM;
		goto err_put_vm;
	}

	err = pvr_context_create_queues(ctx, args, ctx->data);
	if (err)
		goto err_free_ctx_data;

	err = init_fw_objs(ctx, args, ctx->data);
	if (err)
		goto err_destroy_queues;

	err = pvr_fw_object_create(pvr_dev, ctx_size, PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
				   ctx_fw_data_init, ctx, &ctx->fw_obj);
	if (err)
		goto err_free_ctx_data;

	err = xa_alloc(&pvr_dev->ctx_ids, &ctx->ctx_id, ctx, xa_limit_32b, GFP_KERNEL);
	if (err)
		goto err_destroy_fw_obj;

	err = xa_alloc(&pvr_file->ctx_handles, &args->handle, ctx, xa_limit_32b, GFP_KERNEL);
	if (err) {
		/*
		 * It's possible that another thread could have taken a reference on the context at
		 * this point as it is in the ctx_ids xarray. Therefore instead of directly
		 * destroying the context, drop a reference instead.
		 */
		pvr_context_put(ctx);
		return err;
	}

	return 0;

err_destroy_fw_obj:
	pvr_fw_object_destroy(ctx->fw_obj);

err_destroy_queues:
	pvr_context_destroy_queues(ctx);

err_free_ctx_data:
	kfree(ctx->data);

err_put_vm:
	pvr_vm_context_put(ctx->vm_ctx);

err_free_ctx:
	kfree(ctx);
	return err;
}

static void
pvr_context_release(struct kref *ref_count)
{
	struct pvr_context *ctx =
		container_of(ref_count, struct pvr_context, ref_count);
	struct pvr_device *pvr_dev = ctx->pvr_dev;

	xa_erase(&pvr_dev->ctx_ids, ctx->ctx_id);
	pvr_context_destroy_queues(ctx);
	pvr_fw_object_destroy(ctx->fw_obj);
	kfree(ctx->data);
	pvr_vm_context_put(ctx->vm_ctx);
	kfree(ctx);
}

/**
 * pvr_context_put() - Release reference on context
 * @ctx: Target context.
 */
void
pvr_context_put(struct pvr_context *ctx)
{
	if (ctx)
		kref_put(&ctx->ref_count, pvr_context_release);
}

/**
 * pvr_context_destroy() - Destroy context
 * @pvr_file: Pointer to pvr_file structure.
 * @handle: Userspace context handle.
 *
 * Removes context from context list and drops initial reference. Context will
 * then be destroyed once all outstanding references are dropped.
 *
 * Return:
 *  * 0 on success, or
 *  * -%EINVAL if context not in context list.
 */
int
pvr_context_destroy(struct pvr_file *pvr_file, u32 handle)
{
	struct pvr_context *ctx = xa_erase(&pvr_file->ctx_handles, handle);

	if (!ctx)
		return -EINVAL;

	/* Make sure nothing can be queued to the queues after that point. */
	pvr_context_kill_queues(ctx);

	/* Release the reference held by the handle set. */
	pvr_context_put(ctx);

	return 0;
}

/**
 * pvr_destroy_contexts_for_file: Destroy any contexts associated with the given file
 * @pvr_file: Pointer to pvr_file structure.
 *
 * Removes all contexts associated with @pvr_file from the device context list and drops initial
 * references. Contexts will then be destroyed once all outstanding references are dropped.
 */
void pvr_destroy_contexts_for_file(struct pvr_file *pvr_file)
{
	struct pvr_context *ctx;
	unsigned long handle;

	xa_for_each(&pvr_file->ctx_handles, handle, ctx)
		pvr_context_destroy(pvr_file, handle);
}

/**
 * pvr_context_device_init() - Device level initialization for queue related resources.
 * @pvr_dev: The device to initialize.
 */
void pvr_context_device_init(struct pvr_device *pvr_dev)
{
	xa_init_flags(&pvr_dev->ctx_ids, XA_FLAGS_ALLOC1);
}

/**
 * pvr_context_device_fini() - Device level cleanup for queue related resources.
 * @pvr_dev: The device to cleanup.
 */
void pvr_context_device_fini(struct pvr_device *pvr_dev)
{
	WARN_ON(!xa_empty(&pvr_dev->ctx_ids));
	xa_destroy(&pvr_dev->ctx_ids);
}
