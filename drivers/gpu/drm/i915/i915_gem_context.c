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

#include <linux/log2.h>
#include <drm/drmP.h>
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_workarounds.h"

#define ALL_L3_SLICES(dev) (1 << NUM_L3_SLICES(dev)) - 1

static void lut_close(struct i915_gem_context *ctx)
{
	struct i915_lut_handle *lut, *ln;
	struct radix_tree_iter iter;
	void __rcu **slot;

	list_for_each_entry_safe(lut, ln, &ctx->handles_list, ctx_link) {
		list_del(&lut->obj_link);
		kmem_cache_free(ctx->i915->luts, lut);
	}

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &ctx->handles_vma, &iter, 0) {
		struct i915_vma *vma = rcu_dereference_raw(*slot);

		radix_tree_iter_delete(&ctx->handles_vma, &iter, slot);
		__i915_gem_object_release_unless_active(vma->obj);
	}
	rcu_read_unlock();
}

static void i915_gem_context_free(struct i915_gem_context *ctx)
{
	unsigned int n;

	lockdep_assert_held(&ctx->i915->drm.struct_mutex);
	GEM_BUG_ON(!i915_gem_context_is_closed(ctx));

	i915_ppgtt_put(ctx->ppgtt);

	for (n = 0; n < ARRAY_SIZE(ctx->__engine); n++) {
		struct intel_context *ce = &ctx->__engine[n];

		if (ce->ops)
			ce->ops->destroy(ce);
	}

	kfree(ctx->name);
	put_pid(ctx->pid);

	list_del(&ctx->link);

	ida_simple_remove(&ctx->i915->contexts.hw_ida, ctx->hw_id);
	kfree_rcu(ctx, rcu);
}

static void contexts_free(struct drm_i915_private *i915)
{
	struct llist_node *freed = llist_del_all(&i915->contexts.free_list);
	struct i915_gem_context *ctx, *cn;

	lockdep_assert_held(&i915->drm.struct_mutex);

	llist_for_each_entry_safe(ctx, cn, freed, free_link)
		i915_gem_context_free(ctx);
}

static void contexts_free_first(struct drm_i915_private *i915)
{
	struct i915_gem_context *ctx;
	struct llist_node *freed;

	lockdep_assert_held(&i915->drm.struct_mutex);

	freed = llist_del_first(&i915->contexts.free_list);
	if (!freed)
		return;

	ctx = container_of(freed, typeof(*ctx), free_link);
	i915_gem_context_free(ctx);
}

static void contexts_free_worker(struct work_struct *work)
{
	struct drm_i915_private *i915 =
		container_of(work, typeof(*i915), contexts.free_work);

	mutex_lock(&i915->drm.struct_mutex);
	contexts_free(i915);
	mutex_unlock(&i915->drm.struct_mutex);
}

void i915_gem_context_release(struct kref *ref)
{
	struct i915_gem_context *ctx = container_of(ref, typeof(*ctx), ref);
	struct drm_i915_private *i915 = ctx->i915;

	trace_i915_context_free(ctx);
	if (llist_add(&ctx->free_link, &i915->contexts.free_list))
		queue_work(i915->wq, &i915->contexts.free_work);
}

static void context_close(struct i915_gem_context *ctx)
{
	i915_gem_context_set_closed(ctx);

	/*
	 * The LUT uses the VMA as a backpointer to unref the object,
	 * so we need to clear the LUT before we close all the VMA (inside
	 * the ppgtt).
	 */
	lut_close(ctx);
	if (ctx->ppgtt)
		i915_ppgtt_close(&ctx->ppgtt->vm);

	ctx->file_priv = ERR_PTR(-EBADF);
	i915_gem_context_put(ctx);
}

static int assign_hw_id(struct drm_i915_private *dev_priv, unsigned *out)
{
	int ret;
	unsigned int max;

	if (INTEL_GEN(dev_priv) >= 11) {
		max = GEN11_MAX_CONTEXT_HW_ID;
	} else {
		/*
		 * When using GuC in proxy submission, GuC consumes the
		 * highest bit in the context id to indicate proxy submission.
		 */
		if (USES_GUC_SUBMISSION(dev_priv))
			max = MAX_GUC_CONTEXT_HW_ID;
		else
			max = MAX_CONTEXT_HW_ID;
	}


	ret = ida_simple_get(&dev_priv->contexts.hw_ida,
			     0, max, GFP_KERNEL);
	if (ret < 0) {
		/* Contexts are only released when no longer active.
		 * Flush any pending retires to hopefully release some
		 * stale contexts and try again.
		 */
		i915_retire_requests(dev_priv);
		ret = ida_simple_get(&dev_priv->contexts.hw_ida,
				     0, max, GFP_KERNEL);
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
	if (ppgtt && i915_vm_is_48bit(&ppgtt->vm))
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
	unsigned int n;
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
	list_add_tail(&ctx->link, &dev_priv->contexts.list);
	ctx->i915 = dev_priv;
	ctx->sched.priority = I915_PRIORITY_NORMAL;

	for (n = 0; n < ARRAY_SIZE(ctx->__engine); n++) {
		struct intel_context *ce = &ctx->__engine[n];

		ce->gem_context = ctx;
	}

	INIT_RADIX_TREE(&ctx->handles_vma, GFP_KERNEL);
	INIT_LIST_HEAD(&ctx->handles_list);

	/* Default context will never have a file_priv */
	ret = DEFAULT_CONTEXT_HANDLE;
	if (file_priv) {
		ret = idr_alloc(&file_priv->context_idr, ctx,
				DEFAULT_CONTEXT_HANDLE, 0, GFP_KERNEL);
		if (ret < 0)
			goto err_lut;
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

	/*
	 * GuC requires the ring to be placed in Non-WOPCM memory. If GuC is not
	 * present or not in use we still need a small bias as ring wraparound
	 * at offset 0 sometimes hangs. No idea why.
	 */
	if (USES_GUC(dev_priv))
		ctx->ggtt_offset_bias = dev_priv->guc.ggtt_pin_bias;
	else
		ctx->ggtt_offset_bias = I915_GTT_PAGE_SIZE;

	return ctx;

err_pid:
	put_pid(ctx->pid);
	idr_remove(&file_priv->context_idr, ctx->user_handle);
err_lut:
	context_close(ctx);
	return ERR_PTR(ret);
}

static void __destroy_hw_context(struct i915_gem_context *ctx,
				 struct drm_i915_file_private *file_priv)
{
	idr_remove(&file_priv->context_idr, ctx->user_handle);
	context_close(ctx);
}

static struct i915_gem_context *
i915_gem_create_context(struct drm_i915_private *dev_priv,
			struct drm_i915_file_private *file_priv)
{
	struct i915_gem_context *ctx;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	/* Reap the most stale context */
	contexts_free_first(dev_priv);

	ctx = __create_hw_context(dev_priv, file_priv);
	if (IS_ERR(ctx))
		return ctx;

	if (USES_FULL_PPGTT(dev_priv)) {
		struct i915_hw_ppgtt *ppgtt;

		ppgtt = i915_ppgtt_create(dev_priv, file_priv);
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
	if (!USES_GUC_SUBMISSION(to_i915(dev)))
		ctx->ring_size = 512 * PAGE_SIZE; /* Max ring buffer size */

	GEM_BUG_ON(i915_gem_context_is_kernel(ctx));
out:
	mutex_unlock(&dev->struct_mutex);
	return ctx;
}

struct i915_gem_context *
i915_gem_context_create_kernel(struct drm_i915_private *i915, int prio)
{
	struct i915_gem_context *ctx;

	ctx = i915_gem_create_context(i915, NULL);
	if (IS_ERR(ctx))
		return ctx;

	i915_gem_context_clear_bannable(ctx);
	ctx->sched.priority = prio;
	ctx->ring_size = PAGE_SIZE;

	GEM_BUG_ON(!i915_gem_context_is_kernel(ctx));

	return ctx;
}

static void
destroy_kernel_context(struct i915_gem_context **ctxp)
{
	struct i915_gem_context *ctx;

	/* Keep the context ref so that we can free it immediately ourselves */
	ctx = i915_gem_context_get(fetch_and_zero(ctxp));
	GEM_BUG_ON(!i915_gem_context_is_kernel(ctx));

	context_close(ctx);
	i915_gem_context_free(ctx);
}

static bool needs_preempt_context(struct drm_i915_private *i915)
{
	return HAS_LOGICAL_RING_PREEMPTION(i915);
}

int i915_gem_contexts_init(struct drm_i915_private *dev_priv)
{
	struct i915_gem_context *ctx;
	int ret;

	/* Reassure ourselves we are only called once */
	GEM_BUG_ON(dev_priv->kernel_context);
	GEM_BUG_ON(dev_priv->preempt_context);

	ret = intel_ctx_workarounds_init(dev_priv);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&dev_priv->contexts.list);
	INIT_WORK(&dev_priv->contexts.free_work, contexts_free_worker);
	init_llist_head(&dev_priv->contexts.free_list);

	/* Using the simple ida interface, the max is limited by sizeof(int) */
	BUILD_BUG_ON(MAX_CONTEXT_HW_ID > INT_MAX);
	BUILD_BUG_ON(GEN11_MAX_CONTEXT_HW_ID > INT_MAX);
	ida_init(&dev_priv->contexts.hw_ida);

	/* lowest priority; idle task */
	ctx = i915_gem_context_create_kernel(dev_priv, I915_PRIORITY_MIN);
	if (IS_ERR(ctx)) {
		DRM_ERROR("Failed to create default global context\n");
		return PTR_ERR(ctx);
	}
	/*
	 * For easy recognisablity, we want the kernel context to be 0 and then
	 * all user contexts will have non-zero hw_id.
	 */
	GEM_BUG_ON(ctx->hw_id);
	dev_priv->kernel_context = ctx;

	/* highest priority; preempting task */
	if (needs_preempt_context(dev_priv)) {
		ctx = i915_gem_context_create_kernel(dev_priv, INT_MAX);
		if (!IS_ERR(ctx))
			dev_priv->preempt_context = ctx;
		else
			DRM_ERROR("Failed to create preempt context; disabling preemption\n");
	}

	DRM_DEBUG_DRIVER("%s context support initialized\n",
			 DRIVER_CAPS(dev_priv)->has_logical_contexts ?
			 "logical" : "fake");
	return 0;
}

void i915_gem_contexts_lost(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	for_each_engine(engine, dev_priv, id)
		intel_engine_lost_context(engine);
}

void i915_gem_contexts_fini(struct drm_i915_private *i915)
{
	lockdep_assert_held(&i915->drm.struct_mutex);

	if (i915->preempt_context)
		destroy_kernel_context(&i915->preempt_context);
	destroy_kernel_context(&i915->kernel_context);

	/* Must free all deferred contexts (via flush_workqueue) first */
	ida_destroy(&i915->contexts.hw_ida);
}

static int context_idr_cleanup(int id, void *p, void *data)
{
	struct i915_gem_context *ctx = p;

	context_close(ctx);
	return 0;
}

int i915_gem_context_open(struct drm_i915_private *i915,
			  struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_gem_context *ctx;

	idr_init(&file_priv->context_idr);

	mutex_lock(&i915->drm.struct_mutex);
	ctx = i915_gem_create_context(i915, file_priv);
	mutex_unlock(&i915->drm.struct_mutex);
	if (IS_ERR(ctx)) {
		idr_destroy(&file_priv->context_idr);
		return PTR_ERR(ctx);
	}

	GEM_BUG_ON(i915_gem_context_is_kernel(ctx));

	return 0;
}

void i915_gem_context_close(struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	lockdep_assert_held(&file_priv->dev_priv->drm.struct_mutex);

	idr_for_each(&file_priv->context_idr, context_idr_cleanup, NULL);
	idr_destroy(&file_priv->context_idr);
}

static struct i915_request *
last_request_on_engine(struct i915_timeline *timeline,
		       struct intel_engine_cs *engine)
{
	struct i915_request *rq;

	GEM_BUG_ON(timeline == &engine->timeline);

	rq = i915_gem_active_raw(&timeline->last_request,
				 &engine->i915->drm.struct_mutex);
	if (rq && rq->engine == engine) {
		GEM_TRACE("last request for %s on engine %s: %llx:%d\n",
			  timeline->name, engine->name,
			  rq->fence.context, rq->fence.seqno);
		GEM_BUG_ON(rq->timeline != timeline);
		return rq;
	}

	return NULL;
}

static bool engine_has_kernel_context_barrier(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	const struct intel_context * const ce =
		to_intel_context(i915->kernel_context, engine);
	struct i915_timeline *barrier = ce->ring->timeline;
	struct intel_ring *ring;
	bool any_active = false;

	lockdep_assert_held(&i915->drm.struct_mutex);
	list_for_each_entry(ring, &i915->gt.active_rings, active_link) {
		struct i915_request *rq;

		rq = last_request_on_engine(ring->timeline, engine);
		if (!rq)
			continue;

		any_active = true;

		if (rq->hw_context == ce)
			continue;

		/*
		 * Was this request submitted after the previous
		 * switch-to-kernel-context?
		 */
		if (!i915_timeline_sync_is_later(barrier, &rq->fence)) {
			GEM_TRACE("%s needs barrier for %llx:%d\n",
				  ring->timeline->name,
				  rq->fence.context,
				  rq->fence.seqno);
			return false;
		}

		GEM_TRACE("%s has barrier after %llx:%d\n",
			  ring->timeline->name,
			  rq->fence.context,
			  rq->fence.seqno);
	}

	/*
	 * If any other timeline was still active and behind the last barrier,
	 * then our last switch-to-kernel-context must still be queued and
	 * will run last (leaving the engine in the kernel context when it
	 * eventually idles).
	 */
	if (any_active)
		return true;

	/* The engine is idle; check that it is idling in the kernel context. */
	return engine->last_retired_context == ce;
}

int i915_gem_switch_to_kernel_context(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	GEM_TRACE("awake?=%s\n", yesno(i915->gt.awake));

	lockdep_assert_held(&i915->drm.struct_mutex);
	GEM_BUG_ON(!i915->kernel_context);

	i915_retire_requests(i915);

	for_each_engine(engine, i915, id) {
		struct intel_ring *ring;
		struct i915_request *rq;

		GEM_BUG_ON(!to_intel_context(i915->kernel_context, engine));
		if (engine_has_kernel_context_barrier(engine))
			continue;

		GEM_TRACE("emit barrier on %s\n", engine->name);

		rq = i915_request_alloc(engine, i915->kernel_context);
		if (IS_ERR(rq))
			return PTR_ERR(rq);

		/* Queue this switch after all other activity */
		list_for_each_entry(ring, &i915->gt.active_rings, active_link) {
			struct i915_request *prev;

			prev = last_request_on_engine(ring->timeline, engine);
			if (!prev)
				continue;

			if (prev->gem_context == i915->kernel_context)
				continue;

			GEM_TRACE("add barrier on %s for %llx:%d\n",
				  engine->name,
				  prev->fence.context,
				  prev->fence.seqno);
			i915_sw_fence_await_sw_fence_gfp(&rq->submit,
							 &prev->submit,
							 I915_FENCE_GFP);
			i915_timeline_sync_set(rq->timeline, &prev->fence);
		}

		i915_request_add(rq);
	}

	return 0;
}

static bool client_is_banned(struct drm_i915_file_private *file_priv)
{
	return atomic_read(&file_priv->ban_score) >= I915_CLIENT_SCORE_BANNED;
}

int i915_gem_context_create_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_context_create *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_gem_context *ctx;
	int ret;

	if (!DRIVER_CAPS(dev_priv)->has_logical_contexts)
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

	ctx = i915_gem_context_lookup(file_priv, args->ctx_id);
	if (!ctx)
		return -ENOENT;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		goto out;

	__destroy_hw_context(ctx, file_priv);
	mutex_unlock(&dev->struct_mutex);

out:
	i915_gem_context_put(ctx);
	return 0;
}

int i915_gem_context_getparam_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct drm_i915_gem_context_param *args = data;
	struct i915_gem_context *ctx;
	int ret = 0;

	ctx = i915_gem_context_lookup(file_priv, args->ctx_id);
	if (!ctx)
		return -ENOENT;

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
			args->value = ctx->ppgtt->vm.total;
		else if (to_i915(dev)->mm.aliasing_ppgtt)
			args->value = to_i915(dev)->mm.aliasing_ppgtt->vm.total;
		else
			args->value = to_i915(dev)->ggtt.vm.total;
		break;
	case I915_CONTEXT_PARAM_NO_ERROR_CAPTURE:
		args->value = i915_gem_context_no_error_capture(ctx);
		break;
	case I915_CONTEXT_PARAM_BANNABLE:
		args->value = i915_gem_context_is_bannable(ctx);
		break;
	case I915_CONTEXT_PARAM_PRIORITY:
		args->value = ctx->sched.priority;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	i915_gem_context_put(ctx);
	return ret;
}

int i915_gem_context_setparam_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct drm_i915_gem_context_param *args = data;
	struct i915_gem_context *ctx;
	int ret;

	ctx = i915_gem_context_lookup(file_priv, args->ctx_id);
	if (!ctx)
		return -ENOENT;

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		goto out;

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

	case I915_CONTEXT_PARAM_PRIORITY:
		{
			s64 priority = args->value;

			if (args->size)
				ret = -EINVAL;
			else if (!(to_i915(dev)->caps.scheduler & I915_SCHEDULER_CAP_PRIORITY))
				ret = -ENODEV;
			else if (priority > I915_CONTEXT_MAX_USER_PRIORITY ||
				 priority < I915_CONTEXT_MIN_USER_PRIORITY)
				ret = -EINVAL;
			else if (priority > I915_CONTEXT_DEFAULT_PRIORITY &&
				 !capable(CAP_SYS_NICE))
				ret = -EPERM;
			else
				ctx->sched.priority = priority;
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&dev->struct_mutex);

out:
	i915_gem_context_put(ctx);
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

	ret = -ENOENT;
	rcu_read_lock();
	ctx = __i915_gem_context_lookup_rcu(file->driver_priv, args->ctx_id);
	if (!ctx)
		goto out;

	/*
	 * We opt for unserialised reads here. This may result in tearing
	 * in the extremely unlikely event of a GPU hang on this context
	 * as we are querying them. If we need that extra layer of protection,
	 * we should wrap the hangstats with a seqlock.
	 */

	if (capable(CAP_SYS_ADMIN))
		args->reset_count = i915_reset_count(&dev_priv->gpu_error);
	else
		args->reset_count = 0;

	args->batch_active = atomic_read(&ctx->guilty_count);
	args->batch_pending = atomic_read(&ctx->active_count);

	ret = 0;
out:
	rcu_read_unlock();
	return ret;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_context.c"
#include "selftests/i915_gem_context.c"
#endif
