/*
 * Copyright © 2011-2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

/*
 * This file implements HW context support. On gen5+ a HW context consists of an
 * opaque GPU object which is referenced at times of context saves and restores.
 * With RC6 enabled, the context is also referenced as the GPU enters and exists
 * from RC6 (GPU has it's own internal power context, except on gen5). Though
 * something like a context does exist for the media ring, the code only
 * supports contexts for the render ring.
 *
 * In software, there is a distinction between contexts created by the user,
 * and the default HW context. The default HW context is used by GPU clients
 * that do not request setup of their own hardware context. The default
 * context's state is never restored to help prevent programming errors. This
 * would happen if a client ran and piggy-backed off another clients GPU state.
 * The default context only exists to give the GPU some offset to load as the
 * current to invoke a save of the context we actually care about. In fact, the
 * code could likely be constructed, albeit in a more complicated fashion, to
 * never use the default context, though that limits the driver's ability to
 * swap out, and/or destroy other contexts.
 *
 * All other contexts are created as a request by the GPU client. These contexts
 * store GPU state, and thus allow GPU clients to not re-emit state (and
 * potentially query certain state) at any time. The kernel driver makes
 * certain that the appropriate commands are inserted.
 *
 * The context life cycle is semi-complicated in that context BOs may live
 * longer than the context itself because of the way the hardware, and object
 * tracking works. Below is a very crude representation of the state machine
 * describing the context life.
 *                                         refcount     pincount     active
 * S0: initial state                          0            0           0
 * S1: context created                        1            0           0
 * S2: context is currently running           2            1           X
 * S3: GPU referenced, but not current        2            0           1
 * S4: context is current, but destroyed      1            1           0
 * S5: like S3, but destroyed                 1            0           1
 *
 * The most common (but not all) transitions:
 * S0->S1: client creates a context
 * S1->S2: client submits execbuf with context
 * S2->S3: other clients submits execbuf with context
 * S3->S1: context object was retired
 * S3->S2: clients submits another execbuf
 * S2->S4: context destroy called with current context
 * S3->S5->S0: destroy path
 * S4->S5->S0: destroy path on current context
 *
 * There are two confusing terms used above:
 *  The "current context" means the context which is currently running on the
 *  GPU. The GPU has loaded it's state already and has stored away the gtt
 *  offset of the BO. The GPU is not actively referencing the data at this
 *  offset, but it will on the next context switch. The only way to avoid this
 *  is to do a GPU reset.
 *
 *  An "active context' is one which was previously the "current context" and is
 *  on the active list waiting for the next context switch to occur. Until this
 *  happens, the object must remain at the same gtt offset. It is therefore
 *  possible to destroy a context, but it is still active.
 *
 */

#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"

/* This is a HW constraint. The value below is the largest known requirement
 * I've seen in a spec to date, and that was a workaround for a non-shipping
 * part. It should be safe to decrease this, but it's more future proof as is.
 */
#define CONTEXT_ALIGN (64<<10)

static struct i915_hw_context *
i915_gem_context_get(struct drm_i915_file_private *file_priv, u32 id);
static int do_switch(struct i915_hw_context *to);

static int get_context_size(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;
	u32 reg;

	switch (INTEL_INFO(dev)->gen) {
	case 6:
		reg = I915_READ(CXT_SIZE);
		ret = GEN6_CXT_TOTAL_SIZE(reg) * 64;
		break;
	case 7:
		reg = I915_READ(GEN7_CXT_SIZE);
		if (IS_HASWELL(dev))
			ret = HSW_CXT_TOTAL_SIZE;
		else
			ret = GEN7_CXT_TOTAL_SIZE(reg) * 64;
		break;
	default:
		BUG();
	}

	return ret;
}

void i915_gem_context_free(struct kref *ctx_ref)
{
	struct i915_hw_context *ctx = container_of(ctx_ref,
						   typeof(*ctx), ref);

	drm_gem_object_unreference(&ctx->obj->base);
	kfree(ctx);
}

static struct i915_hw_context *
create_hw_context(struct drm_device *dev,
		  struct drm_i915_file_private *file_priv)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_context *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL)
		return ERR_PTR(-ENOMEM);

	kref_init(&ctx->ref);
	ctx->obj = i915_gem_alloc_object(dev, dev_priv->hw_context_size);
	if (ctx->obj == NULL) {
		kfree(ctx);
		DRM_DEBUG_DRIVER("Context object allocated failed\n");
		return ERR_PTR(-ENOMEM);
	}

	if (INTEL_INFO(dev)->gen >= 7) {
		ret = i915_gem_object_set_cache_level(ctx->obj,
						      I915_CACHE_LLC_MLC);
		/* Failure shouldn't ever happen this early */
		if (WARN_ON(ret))
			goto err_out;
	}

	/* The ring associated with the context object is handled by the normal
	 * object tracking code. We give an initial ring value simple to pass an
	 * assertion in the context switch code.
	 */
	ctx->ring = &dev_priv->ring[RCS];

	/* Default context will never have a file_priv */
	if (file_priv == NULL)
		return ctx;

	ret = idr_alloc(&file_priv->context_idr, ctx, DEFAULT_CONTEXT_ID + 1, 0,
			GFP_KERNEL);
	if (ret < 0)
		goto err_out;

	ctx->file_priv = file_priv;
	ctx->id = ret;

	return ctx;

err_out:
	i915_gem_context_unreference(ctx);
	return ERR_PTR(ret);
}

static inline bool is_default_context(struct i915_hw_context *ctx)
{
	return (ctx == ctx->ring->default_context);
}

/**
 * The default context needs to exist per ring that uses contexts. It stores the
 * context state of the GPU for applications that don't utilize HW contexts, as
 * well as an idle case.
 */
static int create_default_context(struct drm_i915_private *dev_priv)
{
	struct i915_hw_context *ctx;
	int ret;

	BUG_ON(!mutex_is_locked(&dev_priv->dev->struct_mutex));

	ctx = create_hw_context(dev_priv->dev, NULL);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	/* We may need to do things with the shrinker which require us to
	 * immediately switch back to the default context. This can cause a
	 * problem as pinning the default context also requires GTT space which
	 * may not be available. To avoid this we always pin the
	 * default context.
	 */
	dev_priv->ring[RCS].default_context = ctx;
	ret = i915_gem_object_pin(ctx->obj, CONTEXT_ALIGN, false, false);
	if (ret) {
		DRM_DEBUG_DRIVER("Couldn't pin %d\n", ret);
		goto err_destroy;
	}

	ret = do_switch(ctx);
	if (ret) {
		DRM_DEBUG_DRIVER("Switch failed %d\n", ret);
		goto err_unpin;
	}

	DRM_DEBUG_DRIVER("Default HW context loaded\n");
	return 0;

err_unpin:
	i915_gem_object_unpin(ctx->obj);
err_destroy:
	i915_gem_context_unreference(ctx);
	return ret;
}

void i915_gem_context_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!HAS_HW_CONTEXTS(dev)) {
		dev_priv->hw_contexts_disabled = true;
		DRM_DEBUG_DRIVER("Disabling HW Contexts; old hardware\n");
		return;
	}

	/* If called from reset, or thaw... we've been here already */
	if (dev_priv->hw_contexts_disabled ||
	    dev_priv->ring[RCS].default_context)
		return;

	dev_priv->hw_context_size = round_up(get_context_size(dev), 4096);

	if (dev_priv->hw_context_size > (1<<20)) {
		dev_priv->hw_contexts_disabled = true;
		DRM_DEBUG_DRIVER("Disabling HW Contexts; invalid size\n");
		return;
	}

	if (create_default_context(dev_priv)) {
		dev_priv->hw_contexts_disabled = true;
		DRM_DEBUG_DRIVER("Disabling HW Contexts; create failed\n");
		return;
	}

	DRM_DEBUG_DRIVER("HW context support initialized\n");
}

void i915_gem_context_fini(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct i915_hw_context *dctx = dev_priv->ring[RCS].default_context;

	if (dev_priv->hw_contexts_disabled)
		return;

	/* The only known way to stop the gpu from accessing the hw context is
	 * to reset it. Do this as the very last operation to avoid confusing
	 * other code, leading to spurious errors. */
	intel_gpu_reset(dev);

	i915_gem_object_unpin(dctx->obj);

	/* When default context is created and switched to, base object refcount
	 * will be 2 (+1 from object creation and +1 from do_switch()).
	 * i915_gem_context_fini() will be called after gpu_idle() has switched
	 * to default context. So we need to unreference the base object once
	 * to offset the do_switch part, so that i915_gem_context_unreference()
	 * can then free the base object correctly. */
	drm_gem_object_unreference(&dctx->obj->base);
	i915_gem_context_unreference(dctx);
}

static int context_idr_cleanup(int id, void *p, void *data)
{
	struct i915_hw_context *ctx = p;

	BUG_ON(id == DEFAULT_CONTEXT_ID);

	i915_gem_context_unreference(ctx);
	return 0;
}

void i915_gem_context_close(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	idr_for_each(&file_priv->context_idr, context_idr_cleanup, NULL);
	idr_destroy(&file_priv->context_idr);
}

static struct i915_hw_context *
i915_gem_context_get(struct drm_i915_file_private *file_priv, u32 id)
{
	return (struct i915_hw_context *)idr_find(&file_priv->context_idr, id);
}

static inline int
mi_set_context(struct intel_ring_buffer *ring,
	       struct i915_hw_context *new_context,
	       u32 hw_flags)
{
	int ret;

	/* w/a: If Flush TLB Invalidation Mode is enabled, driver must do a TLB
	 * invalidation prior to MI_SET_CONTEXT. On GEN6 we don't set the value
	 * explicitly, so we rely on the value at ring init, stored in
	 * itlb_before_ctx_switch.
	 */
	if (IS_GEN6(ring->dev) && ring->itlb_before_ctx_switch) {
		ret = ring->flush(ring, I915_GEM_GPU_DOMAINS, 0);
		if (ret)
			return ret;
	}

	ret = intel_ring_begin(ring, 6);
	if (ret)
		return ret;

	/* WaProgramMiArbOnOffAroundMiSetContext:ivb,vlv,hsw */
	if (IS_GEN7(ring->dev))
		intel_ring_emit(ring, MI_ARB_ON_OFF | MI_ARB_DISABLE);
	else
		intel_ring_emit(ring, MI_NOOP);

	intel_ring_emit(ring, MI_NOOP);
	intel_ring_emit(ring, MI_SET_CONTEXT);
	intel_ring_emit(ring, new_context->obj->gtt_offset |
			MI_MM_SPACE_GTT |
			MI_SAVE_EXT_STATE_EN |
			MI_RESTORE_EXT_STATE_EN |
			hw_flags);
	/* w/a: MI_SET_CONTEXT must always be followed by MI_NOOP */
	intel_ring_emit(ring, MI_NOOP);

	if (IS_GEN7(ring->dev))
		intel_ring_emit(ring, MI_ARB_ON_OFF | MI_ARB_ENABLE);
	else
		intel_ring_emit(ring, MI_NOOP);

	intel_ring_advance(ring);

	return ret;
}

static int do_switch(struct i915_hw_context *to)
{
	struct intel_ring_buffer *ring = to->ring;
	struct i915_hw_context *from = ring->last_context;
	u32 hw_flags = 0;
	int ret;

	BUG_ON(from != NULL && from->obj != NULL && from->obj->pin_count == 0);

	if (from == to)
		return 0;

	ret = i915_gem_object_pin(to->obj, CONTEXT_ALIGN, false, false);
	if (ret)
		return ret;

	/* Clear this page out of any CPU caches for coherent swap-in/out. Note
	 * that thanks to write = false in this call and us not setting any gpu
	 * write domains when putting a context object onto the active list
	 * (when switching away from it), this won't block.
	 * XXX: We need a real interface to do this instead of trickery. */
	ret = i915_gem_object_set_to_gtt_domain(to->obj, false);
	if (ret) {
		i915_gem_object_unpin(to->obj);
		return ret;
	}

	if (!to->obj->has_global_gtt_mapping)
		i915_gem_gtt_bind_object(to->obj, to->obj->cache_level);

	if (!to->is_initialized || is_default_context(to))
		hw_flags |= MI_RESTORE_INHIBIT;
	else if (WARN_ON_ONCE(from == to)) /* not yet expected */
		hw_flags |= MI_FORCE_RESTORE;

	ret = mi_set_context(ring, to, hw_flags);
	if (ret) {
		i915_gem_object_unpin(to->obj);
		return ret;
	}

	/* The backing object for the context is done after switching to the
	 * *next* context. Therefore we cannot retire the previous context until
	 * the next context has already started running. In fact, the below code
	 * is a bit suboptimal because the retiring can occur simply after the
	 * MI_SET_CONTEXT instead of when the next seqno has completed.
	 */
	if (from != NULL) {
		from->obj->base.read_domains = I915_GEM_DOMAIN_INSTRUCTION;
		i915_gem_object_move_to_active(from->obj, ring);
		/* As long as MI_SET_CONTEXT is serializing, ie. it flushes the
		 * whole damn pipeline, we don't need to explicitly mark the
		 * object dirty. The only exception is that the context must be
		 * correct in case the object gets swapped out. Ideally we'd be
		 * able to defer doing this until we know the object would be
		 * swapped, but there is no way to do that yet.
		 */
		from->obj->dirty = 1;
		BUG_ON(from->obj->ring != ring);

		ret = i915_add_request(ring, NULL, NULL);
		if (ret) {
			/* Too late, we've already scheduled a context switch.
			 * Try to undo the change so that the hw state is
			 * consistent with out tracking. In case of emergency,
			 * scream.
			 */
			WARN_ON(mi_set_context(ring, from, MI_RESTORE_INHIBIT));
			return ret;
		}

		i915_gem_object_unpin(from->obj);
		i915_gem_context_unreference(from);
	}

	i915_gem_context_reference(to);
	ring->last_context = to;
	to->is_initialized = true;

	return 0;
}

/**
 * i915_switch_context() - perform a GPU context switch.
 * @ring: ring for which we'll execute the context switch
 * @file_priv: file_priv associated with the context, may be NULL
 * @id: context id number
 * @seqno: sequence number by which the new context will be switched to
 * @flags:
 *
 * The context life cycle is simple. The context refcount is incremented and
 * decremented by 1 and create and destroy. If the context is in use by the GPU,
 * it will have a refoucnt > 1. This allows us to destroy the context abstract
 * object while letting the normal object tracking destroy the backing BO.
 */
int i915_switch_context(struct intel_ring_buffer *ring,
			struct drm_file *file,
			int to_id)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	struct i915_hw_context *to;

	if (dev_priv->hw_contexts_disabled)
		return 0;

	WARN_ON(!mutex_is_locked(&dev_priv->dev->struct_mutex));

	if (ring != &dev_priv->ring[RCS])
		return 0;

	if (to_id == DEFAULT_CONTEXT_ID) {
		to = ring->default_context;
	} else {
		if (file == NULL)
			return -EINVAL;

		to = i915_gem_context_get(file->driver_priv, to_id);
		if (to == NULL)
			return -ENOENT;
	}

	return do_switch(to);
}

int i915_gem_context_create_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_context_create *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_hw_context *ctx;
	int ret;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	if (dev_priv->hw_contexts_disabled)
		return -ENODEV;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ctx = create_hw_context(dev, file_priv);
	mutex_unlock(&dev->struct_mutex);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	args->ctx_id = ctx->id;
	DRM_DEBUG_DRIVER("HW context %d created\n", args->ctx_id);

	return 0;
}

int i915_gem_context_destroy_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file)
{
	struct drm_i915_gem_context_destroy *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_hw_context *ctx;
	int ret;

	if (!(dev->driver->driver_features & DRIVER_GEM))
		return -ENODEV;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ctx = i915_gem_context_get(file_priv, args->ctx_id);
	if (!ctx) {
		mutex_unlock(&dev->struct_mutex);
		return -ENOENT;
	}

	idr_remove(&ctx->file_priv->context_idr, ctx->id);
	i915_gem_context_unreference(ctx);
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG_DRIVER("HW context %d destroyed\n", args->ctx_id);
	return 0;
}
