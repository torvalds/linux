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

#define ALL_L3_SLICES(dev) (1 << NUM_L3_SLICES(dev)) - 1

void i915_gem_context_free(struct kref *ctx_ref)
{
	struct i915_gem_context *ctx = container_of(ctx_ref, typeof(*ctx), ref);
	int i;

	lockdep_assert_held(&ctx->i915->drm.struct_mutex);
	trace_i915_context_free(ctx);
	GEM_BUG_ON(!i915_gem_context_is_closed(ctx));

	i915_ppgtt_put(ctx->ppgtt);

	for (i = 0; i < I915_NUM_ENGINES; i++) {
		struct intel_context *ce = &ctx->engine[i];

		if (!ce->state)
			continue;

		WARN_ON(ce->pin_count);
		if (ce->ring)
			intel_ring_free(ce->ring);

		__i915_gem_object_release_unless_active(ce->state->obj);
	}

	kfree(ctx->name);
	put_pid(ctx->pid);
	list_del(&ctx->link);

	ida_simple_remove(&ctx->i915->context_hw_ida, ctx->hw_id);
	kfree(ctx);
}

static void context_close(struct i915_gem_context *ctx)
{
	i915_gem_context_set_closed(ctx);
	if (ctx->ppgtt)
		i915_ppgtt_close(&ctx->ppgtt->base);
	ctx->file_priv = ERR_PTR(-EBADF);
	i915_gem_context_put(ctx);
}

static int assign_hw_id(struct drm_i915_private *dev_priv, unsigned *out)
{
	int ret;

	ret = ida_simple_get(&dev_priv->context_hw_ida,
			     0, MAX_CONTEXT_HW_ID, GFP_KERNEL);
	if (ret < 0) {
		/* Contexts are only released when no longer active.
		 * Flush any pending retires to hopefully release some
		 * stale contexts and try again.
		 */
		i915_gem_retire_requests(dev_priv);
		ret = ida_simple_get(&dev_priv->context_hw_ida,
				     0, MAX_CONTEXT_HW_ID, GFP_KERNEL);
		if (ret < 0)
			return ret;
	}

	*out = ret;
	return 0;
}

static u32 default_desc_template(const struct drm_i915_private *i915,
				 const struct i915_hw_ppgtt *ppgtt)
{
	u32 address_mode;
	u32 desc;

	desc = GEN8_CTX_VALID | GEN8_CTX_PRIVILEGE;

	address_mode = INTEL_LEGACY_32B_CONTEXT;
	if (ppgtt && i915_vm_is_48bit(&ppgtt->base))
		address_mode = INTEL_LEGACY_64B_CONTEXT;
	desc |= address_mode << GEN8_CTX_ADDRESSING_MODE_SHIFT;

	if (IS_GEN8(i915))
		desc |= GEN8_CTX_L3LLC_COHERENT;

	/* TODO: WaDisableLiteRestore when we start using semaphore
	 * signalling between Command Streamers
	 * ring->ctx_desc_template |= GEN8_CTX_FORCE_RESTORE;
	 */

	return desc;
}

static struct i915_gem_context *
__create_hw_context(struct drm_i915_private *dev_priv,
		    struct drm_i915_file_private *file_priv)
{
	struct i915_gem_context *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL)
		return ERR_PTR(-ENOMEM);

	ret = assign_hw_id(dev_priv, &ctx->hw_id);
	if (ret) {
		kfree(ctx);
		return ERR_PTR(ret);
	}

	kref_init(&ctx->ref);
	list_add_tail(&ctx->link, &dev_priv->context_list);
	ctx->i915 = dev_priv;
	ctx->priority = I915_PRIORITY_NORMAL;

	/* Default context will never have a file_priv */
	ret = DEFAULT_CONTEXT_HANDLE;
	if (file_priv) {
		ret = idr_alloc(&file_priv->context_idr, ctx,
				DEFAULT_CONTEXT_HANDLE, 0, GFP_KERNEL);
		if (ret < 0)
			goto err_out;
	}
	ctx->user_handle = ret;

	ctx->file_priv = file_priv;
	if (file_priv) {
		ctx->pid = get_task_pid(current, PIDTYPE_PID);
		ctx->name = kasprintf(GFP_KERNEL, "%s[%d]/%x",
				      current->comm,
				      pid_nr(ctx->pid),
				      ctx->user_handle);
		if (!ctx->name) {
			ret = -ENOMEM;
			goto err_pid;
		}
	}

	/* NB: Mark all slices as needing a remap so that when the context first
	 * loads it will restore whatever remap state already exists. If there
	 * is no remap info, it will be a NOP. */
	ctx->remap_slice = ALL_L3_SLICES(dev_priv);

	i915_gem_context_set_bannable(ctx);
	ctx->ring_size = 4 * PAGE_SIZE;
	ctx->desc_template =
		default_desc_template(dev_priv, dev_priv->mm.aliasing_ppgtt);

	/* GuC requires the ring to be placed above GUC_WOPCM_TOP. If GuC is not
	 * present or not in use we still need a small bias as ring wraparound
	 * at offset 0 sometimes hangs. No idea why.
	 */
	if (HAS_GUC(dev_priv) && i915.enable_guc_loading)
		ctx->ggtt_offset_bias = GUC_WOPCM_TOP;
	else
		ctx->ggtt_offset_bias = I915_GTT_PAGE_SIZE;

	return ctx;

err_pid:
	put_pid(ctx->pid);
	idr_remove(&file_priv->context_idr, ctx->user_handle);
err_out:
	context_close(ctx);
	return ERR_PTR(ret);
}

static void __destroy_hw_context(struct i915_gem_context *ctx,
				 struct drm_i915_file_private *file_priv)
{
	idr_remove(&file_priv->context_idr, ctx->user_handle);
	context_close(ctx);
}

/**
 * The default context needs to exist per ring that uses contexts. It stores the
 * context state of the GPU for applications that don't utilize HW contexts, as
 * well as an idle case.
 */
static struct i915_gem_context *
i915_gem_create_context(struct drm_i915_private *dev_priv,
			struct drm_i915_file_private *file_priv)
{
	struct i915_gem_context *ctx;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	ctx = __create_hw_context(dev_priv, file_priv);
	if (IS_ERR(ctx))
		return ctx;

	if (USES_FULL_PPGTT(dev_priv)) {
		struct i915_hw_ppgtt *ppgtt;

		ppgtt = i915_ppgtt_create(dev_priv, file_priv, ctx->name);
		if (IS_ERR(ppgtt)) {
			DRM_DEBUG_DRIVER("PPGTT setup failed (%ld)\n",
					 PTR_ERR(ppgtt));
			__destroy_hw_context(ctx, file_priv);
			return ERR_CAST(ppgtt);
		}

		ctx->ppgtt = ppgtt;
		ctx->desc_template = default_desc_template(dev_priv, ppgtt);
	}

	trace_i915_context_create(ctx);

	return ctx;
}

/**
 * i915_gem_context_create_gvt - create a GVT GEM context
 * @dev: drm device *
 *
 * This function is used to create a GVT specific GEM context.
 *
 * Returns:
 * pointer to i915_gem_context on success, error pointer if failed
 *
 */
struct i915_gem_context *
i915_gem_context_create_gvt(struct drm_device *dev)
{
	struct i915_gem_context *ctx;
	int ret;

	if (!IS_ENABLED(CONFIG_DRM_I915_GVT))
		return ERR_PTR(-ENODEV);

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ERR_PTR(ret);

	ctx = __create_hw_context(to_i915(dev), NULL);
	if (IS_ERR(ctx))
		goto out;

	ctx->file_priv = ERR_PTR(-EBADF);
	i915_gem_context_set_closed(ctx); /* not user accessible */
	i915_gem_context_clear_bannable(ctx);
	i915_gem_context_set_force_single_submission(ctx);
	if (!i915.enable_guc_submission)
		ctx->ring_size = 512 * PAGE_SIZE; /* Max ring buffer size */

	GEM_BUG_ON(i915_gem_context_is_kernel(ctx));
out:
	mutex_unlock(&dev->struct_mutex);
	return ctx;
}

int i915_gem_context_init(struct drm_i915_private *dev_priv)
{
	struct i915_gem_context *ctx;

	/* Init should only be called once per module load. Eventually the
	 * restriction on the context_disabled check can be loosened. */
	if (WARN_ON(dev_priv->kernel_context))
		return 0;

	if (intel_vgpu_active(dev_priv) &&
	    HAS_LOGICAL_RING_CONTEXTS(dev_priv)) {
		if (!i915.enable_execlists) {
			DRM_INFO("Only EXECLIST mode is supported in vgpu.\n");
			return -EINVAL;
		}
	}

	/* Using the simple ida interface, the max is limited by sizeof(int) */
	BUILD_BUG_ON(MAX_CONTEXT_HW_ID > INT_MAX);
	ida_init(&dev_priv->context_hw_ida);

	ctx = i915_gem_create_context(dev_priv, NULL);
	if (IS_ERR(ctx)) {
		DRM_ERROR("Failed to create default global context (error %ld)\n",
			  PTR_ERR(ctx));
		return PTR_ERR(ctx);
	}

	/* For easy recognisablity, we want the kernel context to be 0 and then
	 * all user contexts will have non-zero hw_id.
	 */
	GEM_BUG_ON(ctx->hw_id);

	i915_gem_context_clear_bannable(ctx);
	ctx->priority = I915_PRIORITY_MIN; /* lowest priority; idle task */
	dev_priv->kernel_context = ctx;

	GEM_BUG_ON(!i915_gem_context_is_kernel(ctx));

	DRM_DEBUG_DRIVER("%s context support initialized\n",
			 dev_priv->engine[RCS]->context_size ? "logical" :
			 "fake");
	return 0;
}

void i915_gem_context_lost(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	for_each_engine(engine, dev_priv, id) {
		engine->legacy_active_context = NULL;

		if (!engine->last_retired_context)
			continue;

		engine->context_unpin(engine, engine->last_retired_context);
		engine->last_retired_context = NULL;
	}

	/* Force the GPU state to be restored on enabling */
	if (!i915.enable_execlists) {
		struct i915_gem_context *ctx;

		list_for_each_entry(ctx, &dev_priv->context_list, link) {
			if (!i915_gem_context_is_default(ctx))
				continue;

			for_each_engine(engine, dev_priv, id)
				ctx->engine[engine->id].initialised = false;

			ctx->remap_slice = ALL_L3_SLICES(dev_priv);
		}

		for_each_engine(engine, dev_priv, id) {
			struct intel_context *kce =
				&dev_priv->kernel_context->engine[engine->id];

			kce->initialised = true;
		}
	}
}

void i915_gem_context_fini(struct drm_i915_private *dev_priv)
{
	struct i915_gem_context *dctx = dev_priv->kernel_context;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	GEM_BUG_ON(!i915_gem_context_is_kernel(dctx));

	context_close(dctx);
	dev_priv->kernel_context = NULL;

	ida_destroy(&dev_priv->context_hw_ida);
}

static int context_idr_cleanup(int id, void *p, void *data)
{
	struct i915_gem_context *ctx = p;

	context_close(ctx);
	return 0;
}

int i915_gem_context_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_gem_context *ctx;

	idr_init(&file_priv->context_idr);

	mutex_lock(&dev->struct_mutex);
	ctx = i915_gem_create_context(to_i915(dev), file_priv);
	mutex_unlock(&dev->struct_mutex);

	GEM_BUG_ON(i915_gem_context_is_kernel(ctx));

	if (IS_ERR(ctx)) {
		idr_destroy(&file_priv->context_idr);
		return PTR_ERR(ctx);
	}

	return 0;
}

void i915_gem_context_close(struct drm_device *dev, struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	lockdep_assert_held(&dev->struct_mutex);

	idr_for_each(&file_priv->context_idr, context_idr_cleanup, NULL);
	idr_destroy(&file_priv->context_idr);
}

static inline int
mi_set_context(struct drm_i915_gem_request *req, u32 flags)
{
	struct drm_i915_private *dev_priv = req->i915;
	struct intel_engine_cs *engine = req->engine;
	enum intel_engine_id id;
	const int num_rings =
		/* Use an extended w/a on gen7 if signalling from other rings */
		(i915.semaphores && INTEL_GEN(dev_priv) == 7) ?
		INTEL_INFO(dev_priv)->num_rings - 1 :
		0;
	int len;
	u32 *cs;

	flags |= MI_MM_SPACE_GTT;
	if (IS_HASWELL(dev_priv) || INTEL_GEN(dev_priv) >= 8)
		/* These flags are for resource streamer on HSW+ */
		flags |= HSW_MI_RS_SAVE_STATE_EN | HSW_MI_RS_RESTORE_STATE_EN;
	else
		flags |= MI_SAVE_EXT_STATE_EN | MI_RESTORE_EXT_STATE_EN;

	len = 4;
	if (INTEL_GEN(dev_priv) >= 7)
		len += 2 + (num_rings ? 4*num_rings + 6 : 0);

	cs = intel_ring_begin(req, len);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/* WaProgramMiArbOnOffAroundMiSetContext:ivb,vlv,hsw,bdw,chv */
	if (INTEL_GEN(dev_priv) >= 7) {
		*cs++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;
		if (num_rings) {
			struct intel_engine_cs *signaller;

			*cs++ = MI_LOAD_REGISTER_IMM(num_rings);
			for_each_engine(signaller, dev_priv, id) {
				if (signaller == engine)
					continue;

				*cs++ = i915_mmio_reg_offset(
					   RING_PSMI_CTL(signaller->mmio_base));
				*cs++ = _MASKED_BIT_ENABLE(
						GEN6_PSMI_SLEEP_MSG_DISABLE);
			}
		}
	}

	*cs++ = MI_NOOP;
	*cs++ = MI_SET_CONTEXT;
	*cs++ = i915_ggtt_offset(req->ctx->engine[RCS].state) | flags;
	/*
	 * w/a: MI_SET_CONTEXT must always be followed by MI_NOOP
	 * WaMiSetContext_Hang:snb,ivb,vlv
	 */
	*cs++ = MI_NOOP;

	if (INTEL_GEN(dev_priv) >= 7) {
		if (num_rings) {
			struct intel_engine_cs *signaller;
			i915_reg_t last_reg = {}; /* keep gcc quiet */

			*cs++ = MI_LOAD_REGISTER_IMM(num_rings);
			for_each_engine(signaller, dev_priv, id) {
				if (signaller == engine)
					continue;

				last_reg = RING_PSMI_CTL(signaller->mmio_base);
				*cs++ = i915_mmio_reg_offset(last_reg);
				*cs++ = _MASKED_BIT_DISABLE(
						GEN6_PSMI_SLEEP_MSG_DISABLE);
			}

			/* Insert a delay before the next switch! */
			*cs++ = MI_STORE_REGISTER_MEM | MI_SRM_LRM_GLOBAL_GTT;
			*cs++ = i915_mmio_reg_offset(last_reg);
			*cs++ = i915_ggtt_offset(engine->scratch);
			*cs++ = MI_NOOP;
		}
		*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	}

	intel_ring_advance(req, cs);

	return 0;
}

static int remap_l3(struct drm_i915_gem_request *req, int slice)
{
	u32 *cs, *remap_info = req->i915->l3_parity.remap_info[slice];
	int i;

	if (!remap_info)
		return 0;

	cs = intel_ring_begin(req, GEN7_L3LOG_SIZE/4 * 2 + 2);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/*
	 * Note: We do not worry about the concurrent register cacheline hang
	 * here because no other code should access these registers other than
	 * at initialization time.
	 */
	*cs++ = MI_LOAD_REGISTER_IMM(GEN7_L3LOG_SIZE/4);
	for (i = 0; i < GEN7_L3LOG_SIZE/4; i++) {
		*cs++ = i915_mmio_reg_offset(GEN7_L3LOG(slice, i));
		*cs++ = remap_info[i];
	}
	*cs++ = MI_NOOP;
	intel_ring_advance(req, cs);

	return 0;
}

static inline bool skip_rcs_switch(struct i915_hw_ppgtt *ppgtt,
				   struct intel_engine_cs *engine,
				   struct i915_gem_context *to)
{
	if (to->remap_slice)
		return false;

	if (!to->engine[RCS].initialised)
		return false;

	if (ppgtt && (intel_engine_flag(engine) & ppgtt->pd_dirty_rings))
		return false;

	return to == engine->legacy_active_context;
}

static bool
needs_pd_load_pre(struct i915_hw_ppgtt *ppgtt,
		  struct intel_engine_cs *engine,
		  struct i915_gem_context *to)
{
	if (!ppgtt)
		return false;

	/* Always load the ppgtt on first use */
	if (!engine->legacy_active_context)
		return true;

	/* Same context without new entries, skip */
	if (engine->legacy_active_context == to &&
	    !(intel_engine_flag(engine) & ppgtt->pd_dirty_rings))
		return false;

	if (engine->id != RCS)
		return true;

	if (INTEL_GEN(engine->i915) < 8)
		return true;

	return false;
}

static bool
needs_pd_load_post(struct i915_hw_ppgtt *ppgtt,
		   struct i915_gem_context *to,
		   u32 hw_flags)
{
	if (!ppgtt)
		return false;

	if (!IS_GEN8(to->i915))
		return false;

	if (hw_flags & MI_RESTORE_INHIBIT)
		return true;

	return false;
}

static int do_rcs_switch(struct drm_i915_gem_request *req)
{
	struct i915_gem_context *to = req->ctx;
	struct intel_engine_cs *engine = req->engine;
	struct i915_hw_ppgtt *ppgtt = to->ppgtt ?: req->i915->mm.aliasing_ppgtt;
	struct i915_gem_context *from = engine->legacy_active_context;
	u32 hw_flags;
	int ret, i;

	GEM_BUG_ON(engine->id != RCS);

	if (skip_rcs_switch(ppgtt, engine, to))
		return 0;

	if (needs_pd_load_pre(ppgtt, engine, to)) {
		/* Older GENs and non render rings still want the load first,
		 * "PP_DCLV followed by PP_DIR_BASE register through Load
		 * Register Immediate commands in Ring Buffer before submitting
		 * a context."*/
		trace_switch_mm(engine, to);
		ret = ppgtt->switch_mm(ppgtt, req);
		if (ret)
			return ret;
	}

	if (!to->engine[RCS].initialised || i915_gem_context_is_default(to))
		/* NB: If we inhibit the restore, the context is not allowed to
		 * die because future work may end up depending on valid address
		 * space. This means we must enforce that a page table load
		 * occur when this occurs. */
		hw_flags = MI_RESTORE_INHIBIT;
	else if (ppgtt && intel_engine_flag(engine) & ppgtt->pd_dirty_rings)
		hw_flags = MI_FORCE_RESTORE;
	else
		hw_flags = 0;

	if (to != from || (hw_flags & MI_FORCE_RESTORE)) {
		ret = mi_set_context(req, hw_flags);
		if (ret)
			return ret;

		engine->legacy_active_context = to;
	}

	/* GEN8 does *not* require an explicit reload if the PDPs have been
	 * setup, and we do not wish to move them.
	 */
	if (needs_pd_load_post(ppgtt, to, hw_flags)) {
		trace_switch_mm(engine, to);
		ret = ppgtt->switch_mm(ppgtt, req);
		/* The hardware context switch is emitted, but we haven't
		 * actually changed the state - so it's probably safe to bail
		 * here. Still, let the user know something dangerous has
		 * happened.
		 */
		if (ret)
			return ret;
	}

	if (ppgtt)
		ppgtt->pd_dirty_rings &= ~intel_engine_flag(engine);

	for (i = 0; i < MAX_L3_SLICES; i++) {
		if (!(to->remap_slice & (1<<i)))
			continue;

		ret = remap_l3(req, i);
		if (ret)
			return ret;

		to->remap_slice &= ~(1<<i);
	}

	if (!to->engine[RCS].initialised) {
		if (engine->init_context) {
			ret = engine->init_context(req);
			if (ret)
				return ret;
		}
		to->engine[RCS].initialised = true;
	}

	return 0;
}

/**
 * i915_switch_context() - perform a GPU context switch.
 * @req: request for which we'll execute the context switch
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
int i915_switch_context(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *engine = req->engine;

	lockdep_assert_held(&req->i915->drm.struct_mutex);
	if (i915.enable_execlists)
		return 0;

	if (!req->ctx->engine[engine->id].state) {
		struct i915_gem_context *to = req->ctx;
		struct i915_hw_ppgtt *ppgtt =
			to->ppgtt ?: req->i915->mm.aliasing_ppgtt;

		if (needs_pd_load_pre(ppgtt, engine, to)) {
			int ret;

			trace_switch_mm(engine, to);
			ret = ppgtt->switch_mm(ppgtt, req);
			if (ret)
				return ret;

			ppgtt->pd_dirty_rings &= ~intel_engine_flag(engine);
		}

		return 0;
	}

	return do_rcs_switch(req);
}

static bool engine_has_kernel_context(struct intel_engine_cs *engine)
{
	struct i915_gem_timeline *timeline;

	list_for_each_entry(timeline, &engine->i915->gt.timelines, link) {
		struct intel_timeline *tl;

		if (timeline == &engine->i915->gt.global_timeline)
			continue;

		tl = &timeline->engine[engine->id];
		if (i915_gem_active_peek(&tl->last_request,
					 &engine->i915->drm.struct_mutex))
			return false;
	}

	return (!engine->last_retired_context ||
		i915_gem_context_is_kernel(engine->last_retired_context));
}

int i915_gem_switch_to_kernel_context(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	struct i915_gem_timeline *timeline;
	enum intel_engine_id id;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	i915_gem_retire_requests(dev_priv);

	for_each_engine(engine, dev_priv, id) {
		struct drm_i915_gem_request *req;
		int ret;

		if (engine_has_kernel_context(engine))
			continue;

		req = i915_gem_request_alloc(engine, dev_priv->kernel_context);
		if (IS_ERR(req))
			return PTR_ERR(req);

		/* Queue this switch after all other activity */
		list_for_each_entry(timeline, &dev_priv->gt.timelines, link) {
			struct drm_i915_gem_request *prev;
			struct intel_timeline *tl;

			tl = &timeline->engine[engine->id];
			prev = i915_gem_active_raw(&tl->last_request,
						   &dev_priv->drm.struct_mutex);
			if (prev)
				i915_sw_fence_await_sw_fence_gfp(&req->submit,
								 &prev->submit,
								 GFP_KERNEL);
		}

		ret = i915_switch_context(req);
		i915_add_request(req);
		if (ret)
			return ret;
	}

	return 0;
}

static bool client_is_banned(struct drm_i915_file_private *file_priv)
{
	return file_priv->context_bans > I915_MAX_CLIENT_CONTEXT_BANS;
}

int i915_gem_context_create_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_context_create *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_gem_context *ctx;
	int ret;

	if (!dev_priv->engine[RCS]->context_size)
		return -ENODEV;

	if (args->pad != 0)
		return -EINVAL;

	if (client_is_banned(file_priv)) {
		DRM_DEBUG("client %s[%d] banned from creating ctx\n",
			  current->comm,
			  pid_nr(get_task_pid(current, PIDTYPE_PID)));

		return -EIO;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ctx = i915_gem_create_context(dev_priv, file_priv);
	mutex_unlock(&dev->struct_mutex);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	GEM_BUG_ON(i915_gem_context_is_kernel(ctx));

	args->ctx_id = ctx->user_handle;
	DRM_DEBUG("HW context %d created\n", args->ctx_id);

	return 0;
}

int i915_gem_context_destroy_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file)
{
	struct drm_i915_gem_context_destroy *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_gem_context *ctx;
	int ret;

	if (args->pad != 0)
		return -EINVAL;

	if (args->ctx_id == DEFAULT_CONTEXT_HANDLE)
		return -ENOENT;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ctx = i915_gem_context_lookup(file_priv, args->ctx_id);
	if (IS_ERR(ctx)) {
		mutex_unlock(&dev->struct_mutex);
		return PTR_ERR(ctx);
	}

	__destroy_hw_context(ctx, file_priv);
	mutex_unlock(&dev->struct_mutex);

	DRM_DEBUG("HW context %d destroyed\n", args->ctx_id);
	return 0;
}

int i915_gem_context_getparam_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct drm_i915_gem_context_param *args = data;
	struct i915_gem_context *ctx;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ctx = i915_gem_context_lookup(file_priv, args->ctx_id);
	if (IS_ERR(ctx)) {
		mutex_unlock(&dev->struct_mutex);
		return PTR_ERR(ctx);
	}

	args->size = 0;
	switch (args->param) {
	case I915_CONTEXT_PARAM_BAN_PERIOD:
		ret = -EINVAL;
		break;
	case I915_CONTEXT_PARAM_NO_ZEROMAP:
		args->value = ctx->flags & CONTEXT_NO_ZEROMAP;
		break;
	case I915_CONTEXT_PARAM_GTT_SIZE:
		if (ctx->ppgtt)
			args->value = ctx->ppgtt->base.total;
		else if (to_i915(dev)->mm.aliasing_ppgtt)
			args->value = to_i915(dev)->mm.aliasing_ppgtt->base.total;
		else
			args->value = to_i915(dev)->ggtt.base.total;
		break;
	case I915_CONTEXT_PARAM_NO_ERROR_CAPTURE:
		args->value = i915_gem_context_no_error_capture(ctx);
		break;
	case I915_CONTEXT_PARAM_BANNABLE:
		args->value = i915_gem_context_is_bannable(ctx);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int i915_gem_context_setparam_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct drm_i915_gem_context_param *args = data;
	struct i915_gem_context *ctx;
	int ret;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ctx = i915_gem_context_lookup(file_priv, args->ctx_id);
	if (IS_ERR(ctx)) {
		mutex_unlock(&dev->struct_mutex);
		return PTR_ERR(ctx);
	}

	switch (args->param) {
	case I915_CONTEXT_PARAM_BAN_PERIOD:
		ret = -EINVAL;
		break;
	case I915_CONTEXT_PARAM_NO_ZEROMAP:
		if (args->size) {
			ret = -EINVAL;
		} else {
			ctx->flags &= ~CONTEXT_NO_ZEROMAP;
			ctx->flags |= args->value ? CONTEXT_NO_ZEROMAP : 0;
		}
		break;
	case I915_CONTEXT_PARAM_NO_ERROR_CAPTURE:
		if (args->size)
			ret = -EINVAL;
		else if (args->value)
			i915_gem_context_set_no_error_capture(ctx);
		else
			i915_gem_context_clear_no_error_capture(ctx);
		break;
	case I915_CONTEXT_PARAM_BANNABLE:
		if (args->size)
			ret = -EINVAL;
		else if (!capable(CAP_SYS_ADMIN) && !args->value)
			ret = -EPERM;
		else if (args->value)
			i915_gem_context_set_bannable(ctx);
		else
			i915_gem_context_clear_bannable(ctx);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

int i915_gem_context_reset_stats_ioctl(struct drm_device *dev,
				       void *data, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_reset_stats *args = data;
	struct i915_gem_context *ctx;
	int ret;

	if (args->flags || args->pad)
		return -EINVAL;

	if (args->ctx_id == DEFAULT_CONTEXT_HANDLE && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ctx = i915_gem_context_lookup(file->driver_priv, args->ctx_id);
	if (IS_ERR(ctx)) {
		mutex_unlock(&dev->struct_mutex);
		return PTR_ERR(ctx);
	}

	if (capable(CAP_SYS_ADMIN))
		args->reset_count = i915_reset_count(&dev_priv->gpu_error);
	else
		args->reset_count = 0;

	args->batch_active = ctx->guilty_count;
	args->batch_pending = ctx->active_count;

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_context.c"
#include "selftests/i915_gem_context.c"
#endif
