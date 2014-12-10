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
 *  GPU. The GPU has loaded its state already and has stored away the gtt
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
#include "i915_trace.h"

/* This is a HW constraint. The value below is the largest known requirement
 * I've seen in a spec to date, and that was a workaround for a non-shipping
 * part. It should be safe to decrease this, but it's more future proof as is.
 */
#define GEN6_CONTEXT_ALIGN (64<<10)
#define GEN7_CONTEXT_ALIGN 4096

static size_t get_context_alignment(struct drm_device *dev)
{
	if (IS_GEN6(dev))
		return GEN6_CONTEXT_ALIGN;

	return GEN7_CONTEXT_ALIGN;
}

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
	case 8:
		ret = GEN8_CXT_TOTAL_SIZE;
		break;
	default:
		BUG();
	}

	return ret;
}

void i915_gem_context_free(struct kref *ctx_ref)
{
	struct intel_context *ctx = container_of(ctx_ref,
						 typeof(*ctx), ref);

	trace_i915_context_free(ctx);

	if (i915.enable_execlists)
		intel_lr_context_free(ctx);

	i915_ppgtt_put(ctx->ppgtt);

	if (ctx->legacy_hw_ctx.rcs_state)
		drm_gem_object_unreference(&ctx->legacy_hw_ctx.rcs_state->base);
	list_del(&ctx->link);
	kfree(ctx);
}

struct drm_i915_gem_object *
i915_gem_alloc_context_obj(struct drm_device *dev, size_t size)
{
	struct drm_i915_gem_object *obj;
	int ret;

	obj = i915_gem_alloc_object(dev, size);
	if (obj == NULL)
		return ERR_PTR(-ENOMEM);

	/*
	 * Try to make the context utilize L3 as well as LLC.
	 *
	 * On VLV we don't have L3 controls in the PTEs so we
	 * shouldn't touch the cache level, especially as that
	 * would make the object snooped which might have a
	 * negative performance impact.
	 */
	if (INTEL_INFO(dev)->gen >= 7 && !IS_VALLEYVIEW(dev)) {
		ret = i915_gem_object_set_cache_level(obj, I915_CACHE_L3_LLC);
		/* Failure shouldn't ever happen this early */
		if (WARN_ON(ret)) {
			drm_gem_object_unreference(&obj->base);
			return ERR_PTR(ret);
		}
	}

	return obj;
}

static struct intel_context *
__create_hw_context(struct drm_device *dev,
		    struct drm_i915_file_private *file_priv)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_context *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL)
		return ERR_PTR(-ENOMEM);

	kref_init(&ctx->ref);
	list_add_tail(&ctx->link, &dev_priv->context_list);

	if (dev_priv->hw_context_size) {
		struct drm_i915_gem_object *obj =
				i915_gem_alloc_context_obj(dev, dev_priv->hw_context_size);
		if (IS_ERR(obj)) {
			ret = PTR_ERR(obj);
			goto err_out;
		}
		ctx->legacy_hw_ctx.rcs_state = obj;
	}

	/* Default context will never have a file_priv */
	if (file_priv != NULL) {
		ret = idr_alloc(&file_priv->context_idr, ctx,
				DEFAULT_CONTEXT_HANDLE, 0, GFP_KERNEL);
		if (ret < 0)
			goto err_out;
	} else
		ret = DEFAULT_CONTEXT_HANDLE;

	ctx->file_priv = file_priv;
	ctx->user_handle = ret;
	/* NB: Mark all slices as needing a remap so that when the context first
	 * loads it will restore whatever remap state already exists. If there
	 * is no remap info, it will be a NOP. */
	ctx->remap_slice = (1 << NUM_L3_SLICES(dev)) - 1;

	return ctx;

err_out:
	i915_gem_context_unreference(ctx);
	return ERR_PTR(ret);
}

/**
 * The default context needs to exist per ring that uses contexts. It stores the
 * context state of the GPU for applications that don't utilize HW contexts, as
 * well as an idle case.
 */
static struct intel_context *
i915_gem_create_context(struct drm_device *dev,
			struct drm_i915_file_private *file_priv)
{
	const bool is_global_default_ctx = file_priv == NULL;
	struct intel_context *ctx;
	int ret = 0;

	BUG_ON(!mutex_is_locked(&dev->struct_mutex));

	ctx = __create_hw_context(dev, file_priv);
	if (IS_ERR(ctx))
		return ctx;

	if (is_global_default_ctx && ctx->legacy_hw_ctx.rcs_state) {
		/* We may need to do things with the shrinker which
		 * require us to immediately switch back to the default
		 * context. This can cause a problem as pinning the
		 * default context also requires GTT space which may not
		 * be available. To avoid this we always pin the default
		 * context.
		 */
		ret = i915_gem_obj_ggtt_pin(ctx->legacy_hw_ctx.rcs_state,
					    get_context_alignment(dev), 0);
		if (ret) {
			DRM_DEBUG_DRIVER("Couldn't pin %d\n", ret);
			goto err_destroy;
		}
	}

	if (USES_FULL_PPGTT(dev)) {
		struct i915_hw_ppgtt *ppgtt = i915_ppgtt_create(dev, file_priv);

		if (IS_ERR_OR_NULL(ppgtt)) {
			DRM_DEBUG_DRIVER("PPGTT setup failed (%ld)\n",
					 PTR_ERR(ppgtt));
			ret = PTR_ERR(ppgtt);
			goto err_unpin;
		}

		ctx->ppgtt = ppgtt;
	}

	trace_i915_context_create(ctx);

	return ctx;

err_unpin:
	if (is_global_default_ctx && ctx->legacy_hw_ctx.rcs_state)
		i915_gem_object_ggtt_unpin(ctx->legacy_hw_ctx.rcs_state);
err_destroy:
	i915_gem_context_unreference(ctx);
	return ERR_PTR(ret);
}

void i915_gem_context_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	/* In execlists mode we will unreference the context when the execlist
	 * queue is cleared and the requests destroyed.
	 */
	if (i915.enable_execlists)
		return;

	for (i = 0; i < I915_NUM_RINGS; i++) {
		struct intel_engine_cs *ring = &dev_priv->ring[i];
		struct intel_context *lctx = ring->last_context;

		if (lctx) {
			if (lctx->legacy_hw_ctx.rcs_state && i == RCS)
				i915_gem_object_ggtt_unpin(lctx->legacy_hw_ctx.rcs_state);

			i915_gem_context_unreference(lctx);
			ring->last_context = NULL;
		}
	}
}

int i915_gem_context_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_context *ctx;
	int i;

	/* Init should only be called once per module load. Eventually the
	 * restriction on the context_disabled check can be loosened. */
	if (WARN_ON(dev_priv->ring[RCS].default_context))
		return 0;

	if (i915.enable_execlists) {
		/* NB: intentionally left blank. We will allocate our own
		 * backing objects as we need them, thank you very much */
		dev_priv->hw_context_size = 0;
	} else if (HAS_HW_CONTEXTS(dev)) {
		dev_priv->hw_context_size = round_up(get_context_size(dev), 4096);
		if (dev_priv->hw_context_size > (1<<20)) {
			DRM_DEBUG_DRIVER("Disabling HW Contexts; invalid size %d\n",
					 dev_priv->hw_context_size);
			dev_priv->hw_context_size = 0;
		}
	}

	ctx = i915_gem_create_context(dev, NULL);
	if (IS_ERR(ctx)) {
		DRM_ERROR("Failed to create default global context (error %ld)\n",
			  PTR_ERR(ctx));
		return PTR_ERR(ctx);
	}

	for (i = 0; i < I915_NUM_RINGS; i++) {
		struct intel_engine_cs *ring = &dev_priv->ring[i];

		/* NB: RCS will hold a ref for all rings */
		ring->default_context = ctx;
	}

	DRM_DEBUG_DRIVER("%s context support initialized\n",
			i915.enable_execlists ? "LR" :
			dev_priv->hw_context_size ? "HW" : "fake");
	return 0;
}

void i915_gem_context_fini(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_context *dctx = dev_priv->ring[RCS].default_context;
	int i;

	if (dctx->legacy_hw_ctx.rcs_state) {
		/* The only known way to stop the gpu from accessing the hw context is
		 * to reset it. Do this as the very last operation to avoid confusing
		 * other code, leading to spurious errors. */
		intel_gpu_reset(dev);

		/* When default context is created and switched to, base object refcount
		 * will be 2 (+1 from object creation and +1 from do_switch()).
		 * i915_gem_context_fini() will be called after gpu_idle() has switched
		 * to default context. So we need to unreference the base object once
		 * to offset the do_switch part, so that i915_gem_context_unreference()
		 * can then free the base object correctly. */
		WARN_ON(!dev_priv->ring[RCS].last_context);
		if (dev_priv->ring[RCS].last_context == dctx) {
			/* Fake switch to NULL context */
			WARN_ON(dctx->legacy_hw_ctx.rcs_state->active);
			i915_gem_object_ggtt_unpin(dctx->legacy_hw_ctx.rcs_state);
			i915_gem_context_unreference(dctx);
			dev_priv->ring[RCS].last_context = NULL;
		}

		i915_gem_object_ggtt_unpin(dctx->legacy_hw_ctx.rcs_state);
	}

	for (i = 0; i < I915_NUM_RINGS; i++) {
		struct intel_engine_cs *ring = &dev_priv->ring[i];

		if (ring->last_context)
			i915_gem_context_unreference(ring->last_context);

		ring->default_context = NULL;
		ring->last_context = NULL;
	}

	i915_gem_context_unreference(dctx);
}

int i915_gem_context_enable(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *ring;
	int ret, i;

	BUG_ON(!dev_priv->ring[RCS].default_context);

	if (i915.enable_execlists) {
		for_each_ring(ring, dev_priv, i) {
			if (ring->init_context) {
				ret = ring->init_context(ring,
						ring->default_context);
				if (ret) {
					DRM_ERROR("ring init context: %d\n",
							ret);
					return ret;
				}
			}
		}

	} else
		for_each_ring(ring, dev_priv, i) {
			ret = i915_switch_context(ring, ring->default_context);
			if (ret)
				return ret;
		}

	return 0;
}

static int context_idr_cleanup(int id, void *p, void *data)
{
	struct intel_context *ctx = p;

	i915_gem_context_unreference(ctx);
	return 0;
}

int i915_gem_context_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct intel_context *ctx;

	idr_init(&file_priv->context_idr);

	mutex_lock(&dev->struct_mutex);
	ctx = i915_gem_create_context(dev, file_priv);
	mutex_unlock(&dev->struct_mutex);

	if (IS_ERR(ctx)) {
		idr_destroy(&file_priv->context_idr);
		return PTR_ERR(ctx);
	}

	return 0;
}

void i915_gem_context_close(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	idr_for_each(&file_priv->context_idr, context_idr_cleanup, NULL);
	idr_destroy(&file_priv->context_idr);
}

struct intel_context *
i915_gem_context_get(struct drm_i915_file_private *file_priv, u32 id)
{
	struct intel_context *ctx;

	ctx = (struct intel_context *)idr_find(&file_priv->context_idr, id);
	if (!ctx)
		return ERR_PTR(-ENOENT);

	return ctx;
}

static inline int
mi_set_context(struct intel_engine_cs *ring,
	       struct intel_context *new_context,
	       u32 hw_flags)
{
	u32 flags = hw_flags | MI_MM_SPACE_GTT;
	int ret;

	/* w/a: If Flush TLB Invalidation Mode is enabled, driver must do a TLB
	 * invalidation prior to MI_SET_CONTEXT. On GEN6 we don't set the value
	 * explicitly, so we rely on the value at ring init, stored in
	 * itlb_before_ctx_switch.
	 */
	if (IS_GEN6(ring->dev)) {
		ret = ring->flush(ring, I915_GEM_GPU_DOMAINS, 0);
		if (ret)
			return ret;
	}

	/* These flags are for resource streamer on HSW+ */
	if (!IS_HASWELL(ring->dev) && INTEL_INFO(ring->dev)->gen < 8)
		flags |= (MI_SAVE_EXT_STATE_EN | MI_RESTORE_EXT_STATE_EN);

	ret = intel_ring_begin(ring, 6);
	if (ret)
		return ret;

	/* WaProgramMiArbOnOffAroundMiSetContext:ivb,vlv,hsw,bdw,chv */
	if (INTEL_INFO(ring->dev)->gen >= 7)
		intel_ring_emit(ring, MI_ARB_ON_OFF | MI_ARB_DISABLE);
	else
		intel_ring_emit(ring, MI_NOOP);

	intel_ring_emit(ring, MI_NOOP);
	intel_ring_emit(ring, MI_SET_CONTEXT);
	intel_ring_emit(ring, i915_gem_obj_ggtt_offset(new_context->legacy_hw_ctx.rcs_state) |
			flags);
	/*
	 * w/a: MI_SET_CONTEXT must always be followed by MI_NOOP
	 * WaMiSetContext_Hang:snb,ivb,vlv
	 */
	intel_ring_emit(ring, MI_NOOP);

	if (INTEL_INFO(ring->dev)->gen >= 7)
		intel_ring_emit(ring, MI_ARB_ON_OFF | MI_ARB_ENABLE);
	else
		intel_ring_emit(ring, MI_NOOP);

	intel_ring_advance(ring);

	return ret;
}

static int do_switch(struct intel_engine_cs *ring,
		     struct intel_context *to)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	struct intel_context *from = ring->last_context;
	u32 hw_flags = 0;
	bool uninitialized = false;
	struct i915_vma *vma;
	int ret, i;

	if (from != NULL && ring == &dev_priv->ring[RCS]) {
		BUG_ON(from->legacy_hw_ctx.rcs_state == NULL);
		BUG_ON(!i915_gem_obj_is_pinned(from->legacy_hw_ctx.rcs_state));
	}

	if (from == to && !to->remap_slice)
		return 0;

	/* Trying to pin first makes error handling easier. */
	if (ring == &dev_priv->ring[RCS]) {
		ret = i915_gem_obj_ggtt_pin(to->legacy_hw_ctx.rcs_state,
					    get_context_alignment(ring->dev), 0);
		if (ret)
			return ret;
	}

	/*
	 * Pin can switch back to the default context if we end up calling into
	 * evict_everything - as a last ditch gtt defrag effort that also
	 * switches to the default context. Hence we need to reload from here.
	 */
	from = ring->last_context;

	if (to->ppgtt) {
		trace_switch_mm(ring, to);
		ret = to->ppgtt->switch_mm(to->ppgtt, ring);
		if (ret)
			goto unpin_out;
	}

	if (ring != &dev_priv->ring[RCS]) {
		if (from)
			i915_gem_context_unreference(from);
		goto done;
	}

	/*
	 * Clear this page out of any CPU caches for coherent swap-in/out. Note
	 * that thanks to write = false in this call and us not setting any gpu
	 * write domains when putting a context object onto the active list
	 * (when switching away from it), this won't block.
	 *
	 * XXX: We need a real interface to do this instead of trickery.
	 */
	ret = i915_gem_object_set_to_gtt_domain(to->legacy_hw_ctx.rcs_state, false);
	if (ret)
		goto unpin_out;

	vma = i915_gem_obj_to_ggtt(to->legacy_hw_ctx.rcs_state);
	if (!(vma->bound & GLOBAL_BIND)) {
		ret = i915_vma_bind(vma,
				    to->legacy_hw_ctx.rcs_state->cache_level,
				    GLOBAL_BIND);
		/* This shouldn't ever fail. */
		if (WARN_ONCE(ret, "GGTT context bind failed!"))
			goto unpin_out;
	}

	if (!to->legacy_hw_ctx.initialized || i915_gem_context_is_default(to))
		hw_flags |= MI_RESTORE_INHIBIT;

	ret = mi_set_context(ring, to, hw_flags);
	if (ret)
		goto unpin_out;

	for (i = 0; i < MAX_L3_SLICES; i++) {
		if (!(to->remap_slice & (1<<i)))
			continue;

		ret = i915_gem_l3_remap(ring, i);
		/* If it failed, try again next round */
		if (ret)
			DRM_DEBUG_DRIVER("L3 remapping failed\n");
		else
			to->remap_slice &= ~(1<<i);
	}

	/* The backing object for the context is done after switching to the
	 * *next* context. Therefore we cannot retire the previous context until
	 * the next context has already started running. In fact, the below code
	 * is a bit suboptimal because the retiring can occur simply after the
	 * MI_SET_CONTEXT instead of when the next seqno has completed.
	 */
	if (from != NULL) {
		from->legacy_hw_ctx.rcs_state->base.read_domains = I915_GEM_DOMAIN_INSTRUCTION;
		i915_vma_move_to_active(i915_gem_obj_to_ggtt(from->legacy_hw_ctx.rcs_state), ring);
		/* As long as MI_SET_CONTEXT is serializing, ie. it flushes the
		 * whole damn pipeline, we don't need to explicitly mark the
		 * object dirty. The only exception is that the context must be
		 * correct in case the object gets swapped out. Ideally we'd be
		 * able to defer doing this until we know the object would be
		 * swapped, but there is no way to do that yet.
		 */
		from->legacy_hw_ctx.rcs_state->dirty = 1;
		BUG_ON(i915_gem_request_get_ring(
			from->legacy_hw_ctx.rcs_state->last_read_req) != ring);

		/* obj is kept alive until the next request by its active ref */
		i915_gem_object_ggtt_unpin(from->legacy_hw_ctx.rcs_state);
		i915_gem_context_unreference(from);
	}

	uninitialized = !to->legacy_hw_ctx.initialized && from == NULL;
	to->legacy_hw_ctx.initialized = true;

done:
	i915_gem_context_reference(to);
	ring->last_context = to;

	if (uninitialized) {
		if (ring->init_context) {
			ret = ring->init_context(ring, to);
			if (ret)
				DRM_ERROR("ring init context: %d\n", ret);
		}
	}

	return 0;

unpin_out:
	if (ring->id == RCS)
		i915_gem_object_ggtt_unpin(to->legacy_hw_ctx.rcs_state);
	return ret;
}

/**
 * i915_switch_context() - perform a GPU context switch.
 * @ring: ring for which we'll execute the context switch
 * @to: the context to switch to
 *
 * The context life cycle is simple. The context refcount is incremented and
 * decremented by 1 and create and destroy. If the context is in use by the GPU,
 * it will have a refcount > 1. This allows us to destroy the context abstract
 * object while letting the normal object tracking destroy the backing BO.
 *
 * This function should not be used in execlists mode.  Instead the context is
 * switched by writing to the ELSP and requests keep a reference to their
 * context.
 */
int i915_switch_context(struct intel_engine_cs *ring,
			struct intel_context *to)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;

	WARN_ON(i915.enable_execlists);
	WARN_ON(!mutex_is_locked(&dev_priv->dev->struct_mutex));

	if (to->legacy_hw_ctx.rcs_state == NULL) { /* We have the fake context */
		if (to != ring->last_context) {
			i915_gem_context_reference(to);
			if (ring->last_context)
				i915_gem_context_unreference(ring->last_context);
			ring->last_context = to;
		}
		return 0;
	}

	return do_switch(ring, to);
}

static bool contexts_enabled(struct drm_device *dev)
{
	return i915.enable_execlists || to_i915(dev)->hw_context_size;
}

int i915_gem_context_create_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file)
{
	struct drm_i915_gem_context_create *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct intel_context *ctx;
	int ret;

	if (!contexts_enabled(dev))
		return -ENODEV;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ctx = i915_gem_create_context(dev, file_priv);
	mutex_unlock(&dev->struct_mutex);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	args->ctx_id = ctx->user_handle;
	DRM_DEBUG_DRIVER("HW context %d created\n", args->ctx_id);

	return 0;
}

int i915_gem_context_destroy_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file)
{
	struct drm_i915_gem_context_destroy *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct intel_context *ctx;
	int ret;

	if (args->ctx_id == DEFAULT_CONTEXT_HANDLE)
		return -ENOENT;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ctx = i915_gem_context_get(file_priv, args->ctx_id);
	if (IS_ERR(ctx)) {
		mutex_unlock(&dev->struct_mutex);
		return PTR_ERR(ctx);
	}

	idr_remove(&ctx->file_priv->context_idr, ctx->user_handle);
	i915_gem_context_unreference(ctx);
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG_DRIVER("HW context %d destroyed\n", args->ctx_id);
	return 0;
}
