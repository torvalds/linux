/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2011-2012 Intel Corporation
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
#include <linux/nospec.h>

#include <drm/i915_drm.h>

#include "gt/gen6_ppgtt.h"
#include "gt/intel_context.h"
#include "gt/intel_engine_heartbeat.h"
#include "gt/intel_engine_pm.h"
#include "gt/intel_engine_user.h"
#include "gt/intel_lrc_reg.h"
#include "gt/intel_ring.h"

#include "i915_gem_context.h"
#include "i915_globals.h"
#include "i915_trace.h"
#include "i915_user_extensions.h"

#define ALL_L3_SLICES(dev) (1 << NUM_L3_SLICES(dev)) - 1

static struct i915_global_gem_context {
	struct i915_global base;
	struct kmem_cache *slab_luts;
} global;

struct i915_lut_handle *i915_lut_handle_alloc(void)
{
	return kmem_cache_alloc(global.slab_luts, GFP_KERNEL);
}

void i915_lut_handle_free(struct i915_lut_handle *lut)
{
	return kmem_cache_free(global.slab_luts, lut);
}

static void lut_close(struct i915_gem_context *ctx)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	lockdep_assert_held(&ctx->mutex);

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &ctx->handles_vma, &iter, 0) {
		struct i915_vma *vma = rcu_dereference_raw(*slot);
		struct drm_i915_gem_object *obj = vma->obj;
		struct i915_lut_handle *lut;

		if (!kref_get_unless_zero(&obj->base.refcount))
			continue;

		rcu_read_unlock();
		i915_gem_object_lock(obj);
		list_for_each_entry(lut, &obj->lut_list, obj_link) {
			if (lut->ctx != ctx)
				continue;

			if (lut->handle != iter.index)
				continue;

			list_del(&lut->obj_link);
			break;
		}
		i915_gem_object_unlock(obj);
		rcu_read_lock();

		if (&lut->obj_link != &obj->lut_list) {
			i915_lut_handle_free(lut);
			radix_tree_iter_delete(&ctx->handles_vma, &iter, slot);
			if (atomic_dec_and_test(&vma->open_count) &&
			    !i915_vma_is_ggtt(vma))
				i915_vma_close(vma);
			i915_gem_object_put(obj);
		}

		i915_gem_object_put(obj);
	}
	rcu_read_unlock();
}

static struct intel_context *
lookup_user_engine(struct i915_gem_context *ctx,
		   unsigned long flags,
		   const struct i915_engine_class_instance *ci)
#define LOOKUP_USER_INDEX BIT(0)
{
	int idx;

	if (!!(flags & LOOKUP_USER_INDEX) != i915_gem_context_user_engines(ctx))
		return ERR_PTR(-EINVAL);

	if (!i915_gem_context_user_engines(ctx)) {
		struct intel_engine_cs *engine;

		engine = intel_engine_lookup_user(ctx->i915,
						  ci->engine_class,
						  ci->engine_instance);
		if (!engine)
			return ERR_PTR(-EINVAL);

		idx = engine->legacy_idx;
	} else {
		idx = ci->engine_instance;
	}

	return i915_gem_context_get_engine(ctx, idx);
}

static struct i915_address_space *
context_get_vm_rcu(struct i915_gem_context *ctx)
{
	GEM_BUG_ON(!rcu_access_pointer(ctx->vm));

	do {
		struct i915_address_space *vm;

		/*
		 * We do not allow downgrading from full-ppgtt [to a shared
		 * global gtt], so ctx->vm cannot become NULL.
		 */
		vm = rcu_dereference(ctx->vm);
		if (!kref_get_unless_zero(&vm->ref))
			continue;

		/*
		 * This ppgtt may have be reallocated between
		 * the read and the kref, and reassigned to a third
		 * context. In order to avoid inadvertent sharing
		 * of this ppgtt with that third context (and not
		 * src), we have to confirm that we have the same
		 * ppgtt after passing through the strong memory
		 * barrier implied by a successful
		 * kref_get_unless_zero().
		 *
		 * Once we have acquired the current ppgtt of ctx,
		 * we no longer care if it is released from ctx, as
		 * it cannot be reallocated elsewhere.
		 */

		if (vm == rcu_access_pointer(ctx->vm))
			return rcu_pointer_handoff(vm);

		i915_vm_put(vm);
	} while (1);
}

static void intel_context_set_gem(struct intel_context *ce,
				  struct i915_gem_context *ctx)
{
	GEM_BUG_ON(rcu_access_pointer(ce->gem_context));
	RCU_INIT_POINTER(ce->gem_context, ctx);

	if (!test_bit(CONTEXT_ALLOC_BIT, &ce->flags))
		ce->ring = __intel_context_ring_size(SZ_16K);

	if (rcu_access_pointer(ctx->vm)) {
		struct i915_address_space *vm;

		rcu_read_lock();
		vm = context_get_vm_rcu(ctx); /* hmm */
		rcu_read_unlock();

		i915_vm_put(ce->vm);
		ce->vm = vm;
	}

	GEM_BUG_ON(ce->timeline);
	if (ctx->timeline)
		ce->timeline = intel_timeline_get(ctx->timeline);

	if (ctx->sched.priority >= I915_PRIORITY_NORMAL &&
	    intel_engine_has_semaphores(ce->engine))
		__set_bit(CONTEXT_USE_SEMAPHORES, &ce->flags);
}

static void __free_engines(struct i915_gem_engines *e, unsigned int count)
{
	while (count--) {
		if (!e->engines[count])
			continue;

		RCU_INIT_POINTER(e->engines[count]->gem_context, NULL);
		intel_context_put(e->engines[count]);
	}
	kfree(e);
}

static void free_engines(struct i915_gem_engines *e)
{
	__free_engines(e, e->num_engines);
}

static void free_engines_rcu(struct rcu_head *rcu)
{
	free_engines(container_of(rcu, struct i915_gem_engines, rcu));
}

static struct i915_gem_engines *default_engines(struct i915_gem_context *ctx)
{
	const struct intel_gt *gt = &ctx->i915->gt;
	struct intel_engine_cs *engine;
	struct i915_gem_engines *e;
	enum intel_engine_id id;

	e = kzalloc(struct_size(e, engines, I915_NUM_ENGINES), GFP_KERNEL);
	if (!e)
		return ERR_PTR(-ENOMEM);

	init_rcu_head(&e->rcu);
	for_each_engine(engine, gt, id) {
		struct intel_context *ce;

		if (engine->legacy_idx == INVALID_ENGINE)
			continue;

		GEM_BUG_ON(engine->legacy_idx >= I915_NUM_ENGINES);
		GEM_BUG_ON(e->engines[engine->legacy_idx]);

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			__free_engines(e, e->num_engines + 1);
			return ERR_CAST(ce);
		}

		intel_context_set_gem(ce, ctx);

		e->engines[engine->legacy_idx] = ce;
		e->num_engines = max(e->num_engines, engine->legacy_idx);
	}
	e->num_engines++;

	return e;
}

static void i915_gem_context_free(struct i915_gem_context *ctx)
{
	GEM_BUG_ON(!i915_gem_context_is_closed(ctx));

	spin_lock(&ctx->i915->gem.contexts.lock);
	list_del(&ctx->link);
	spin_unlock(&ctx->i915->gem.contexts.lock);

	free_engines(rcu_access_pointer(ctx->engines));
	mutex_destroy(&ctx->engines_mutex);

	if (ctx->timeline)
		intel_timeline_put(ctx->timeline);

	put_pid(ctx->pid);
	mutex_destroy(&ctx->mutex);

	kfree_rcu(ctx, rcu);
}

static void contexts_free_all(struct llist_node *list)
{
	struct i915_gem_context *ctx, *cn;

	llist_for_each_entry_safe(ctx, cn, list, free_link)
		i915_gem_context_free(ctx);
}

static void contexts_flush_free(struct i915_gem_contexts *gc)
{
	contexts_free_all(llist_del_all(&gc->free_list));
}

static void contexts_free_worker(struct work_struct *work)
{
	struct i915_gem_contexts *gc =
		container_of(work, typeof(*gc), free_work);

	contexts_flush_free(gc);
}

void i915_gem_context_release(struct kref *ref)
{
	struct i915_gem_context *ctx = container_of(ref, typeof(*ctx), ref);
	struct i915_gem_contexts *gc = &ctx->i915->gem.contexts;

	trace_i915_context_free(ctx);
	if (llist_add(&ctx->free_link, &gc->free_list))
		schedule_work(&gc->free_work);
}

static inline struct i915_gem_engines *
__context_engines_static(const struct i915_gem_context *ctx)
{
	return rcu_dereference_protected(ctx->engines, true);
}

static bool __reset_engine(struct intel_engine_cs *engine)
{
	struct intel_gt *gt = engine->gt;
	bool success = false;

	if (!intel_has_reset_engine(gt))
		return false;

	if (!test_and_set_bit(I915_RESET_ENGINE + engine->id,
			      &gt->reset.flags)) {
		success = intel_engine_reset(engine, NULL) == 0;
		clear_and_wake_up_bit(I915_RESET_ENGINE + engine->id,
				      &gt->reset.flags);
	}

	return success;
}

static void __reset_context(struct i915_gem_context *ctx,
			    struct intel_engine_cs *engine)
{
	intel_gt_handle_error(engine->gt, engine->mask, 0,
			      "context closure in %s", ctx->name);
}

static bool __cancel_engine(struct intel_engine_cs *engine)
{
	/*
	 * Send a "high priority pulse" down the engine to cause the
	 * current request to be momentarily preempted. (If it fails to
	 * be preempted, it will be reset). As we have marked our context
	 * as banned, any incomplete request, including any running, will
	 * be skipped following the preemption.
	 *
	 * If there is no hangchecking (one of the reasons why we try to
	 * cancel the context) and no forced preemption, there may be no
	 * means by which we reset the GPU and evict the persistent hog.
	 * Ergo if we are unable to inject a preemptive pulse that can
	 * kill the banned context, we fallback to doing a local reset
	 * instead.
	 */
	if (IS_ACTIVE(CONFIG_DRM_I915_PREEMPT_TIMEOUT) &&
	    !intel_engine_pulse(engine))
		return true;

	/* If we are unable to send a pulse, try resetting this engine. */
	return __reset_engine(engine);
}

static struct intel_engine_cs *__active_engine(struct i915_request *rq)
{
	struct intel_engine_cs *engine, *locked;

	/*
	 * Serialise with __i915_request_submit() so that it sees
	 * is-banned?, or we know the request is already inflight.
	 */
	locked = READ_ONCE(rq->engine);
	spin_lock_irq(&locked->active.lock);
	while (unlikely(locked != (engine = READ_ONCE(rq->engine)))) {
		spin_unlock(&locked->active.lock);
		spin_lock(&engine->active.lock);
		locked = engine;
	}

	engine = NULL;
	if (i915_request_is_active(rq) && !rq->fence.error)
		engine = rq->engine;

	spin_unlock_irq(&locked->active.lock);

	return engine;
}

static struct intel_engine_cs *active_engine(struct intel_context *ce)
{
	struct intel_engine_cs *engine = NULL;
	struct i915_request *rq;

	if (!ce->timeline)
		return NULL;

	mutex_lock(&ce->timeline->mutex);
	list_for_each_entry_reverse(rq, &ce->timeline->requests, link) {
		if (i915_request_completed(rq))
			break;

		/* Check with the backend if the request is inflight */
		engine = __active_engine(rq);
		if (engine)
			break;
	}
	mutex_unlock(&ce->timeline->mutex);

	return engine;
}

static void kill_context(struct i915_gem_context *ctx)
{
	struct i915_gem_engines_iter it;
	struct intel_context *ce;

	/*
	 * Map the user's engine back to the actual engines; one virtual
	 * engine will be mapped to multiple engines, and using ctx->engine[]
	 * the same engine may be have multiple instances in the user's map.
	 * However, we only care about pending requests, so only include
	 * engines on which there are incomplete requests.
	 */
	for_each_gem_engine(ce, __context_engines_static(ctx), it) {
		struct intel_engine_cs *engine;

		if (intel_context_set_banned(ce))
			continue;

		/*
		 * Check the current active state of this context; if we
		 * are currently executing on the GPU we need to evict
		 * ourselves. On the other hand, if we haven't yet been
		 * submitted to the GPU or if everything is complete,
		 * we have nothing to do.
		 */
		engine = active_engine(ce);

		/* First attempt to gracefully cancel the context */
		if (engine && !__cancel_engine(engine))
			/*
			 * If we are unable to send a preemptive pulse to bump
			 * the context from the GPU, we have to resort to a full
			 * reset. We hope the collateral damage is worth it.
			 */
			__reset_context(ctx, engine);
	}
}

static void set_closed_name(struct i915_gem_context *ctx)
{
	char *s;

	/* Replace '[]' with '<>' to indicate closed in debug prints */

	s = strrchr(ctx->name, '[');
	if (!s)
		return;

	*s = '<';

	s = strchr(s + 1, ']');
	if (s)
		*s = '>';
}

static void context_close(struct i915_gem_context *ctx)
{
	struct i915_address_space *vm;

	i915_gem_context_set_closed(ctx);
	set_closed_name(ctx);

	mutex_lock(&ctx->mutex);

	vm = i915_gem_context_vm(ctx);
	if (vm)
		i915_vm_close(vm);

	ctx->file_priv = ERR_PTR(-EBADF);

	/*
	 * The LUT uses the VMA as a backpointer to unref the object,
	 * so we need to clear the LUT before we close all the VMA (inside
	 * the ppgtt).
	 */
	lut_close(ctx);

	mutex_unlock(&ctx->mutex);

	/*
	 * If the user has disabled hangchecking, we can not be sure that
	 * the batches will ever complete after the context is closed,
	 * keeping the context and all resources pinned forever. So in this
	 * case we opt to forcibly kill off all remaining requests on
	 * context close.
	 */
	if (!i915_gem_context_is_persistent(ctx) ||
	    !i915_modparams.enable_hangcheck)
		kill_context(ctx);

	i915_gem_context_put(ctx);
}

static int __context_set_persistence(struct i915_gem_context *ctx, bool state)
{
	if (i915_gem_context_is_persistent(ctx) == state)
		return 0;

	if (state) {
		/*
		 * Only contexts that are short-lived [that will expire or be
		 * reset] are allowed to survive past termination. We require
		 * hangcheck to ensure that the persistent requests are healthy.
		 */
		if (!i915_modparams.enable_hangcheck)
			return -EINVAL;

		i915_gem_context_set_persistence(ctx);
	} else {
		/* To cancel a context we use "preempt-to-idle" */
		if (!(ctx->i915->caps.scheduler & I915_SCHEDULER_CAP_PREEMPTION))
			return -ENODEV;

		i915_gem_context_clear_persistence(ctx);
	}

	return 0;
}

static struct i915_gem_context *
__create_context(struct drm_i915_private *i915)
{
	struct i915_gem_context *ctx;
	struct i915_gem_engines *e;
	int err;
	int i;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	kref_init(&ctx->ref);
	ctx->i915 = i915;
	ctx->sched.priority = I915_USER_PRIORITY(I915_PRIORITY_NORMAL);
	mutex_init(&ctx->mutex);

	mutex_init(&ctx->engines_mutex);
	e = default_engines(ctx);
	if (IS_ERR(e)) {
		err = PTR_ERR(e);
		goto err_free;
	}
	RCU_INIT_POINTER(ctx->engines, e);

	INIT_RADIX_TREE(&ctx->handles_vma, GFP_KERNEL);

	/* NB: Mark all slices as needing a remap so that when the context first
	 * loads it will restore whatever remap state already exists. If there
	 * is no remap info, it will be a NOP. */
	ctx->remap_slice = ALL_L3_SLICES(i915);

	i915_gem_context_set_bannable(ctx);
	i915_gem_context_set_recoverable(ctx);
	__context_set_persistence(ctx, true /* cgroup hook? */);

	for (i = 0; i < ARRAY_SIZE(ctx->hang_timestamp); i++)
		ctx->hang_timestamp[i] = jiffies - CONTEXT_FAST_HANG_JIFFIES;

	spin_lock(&i915->gem.contexts.lock);
	list_add_tail(&ctx->link, &i915->gem.contexts.list);
	spin_unlock(&i915->gem.contexts.lock);

	return ctx;

err_free:
	kfree(ctx);
	return ERR_PTR(err);
}

static void
context_apply_all(struct i915_gem_context *ctx,
		  void (*fn)(struct intel_context *ce, void *data),
		  void *data)
{
	struct i915_gem_engines_iter it;
	struct intel_context *ce;

	for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it)
		fn(ce, data);
	i915_gem_context_unlock_engines(ctx);
}

static void __apply_ppgtt(struct intel_context *ce, void *vm)
{
	i915_vm_put(ce->vm);
	ce->vm = i915_vm_get(vm);
}

static struct i915_address_space *
__set_ppgtt(struct i915_gem_context *ctx, struct i915_address_space *vm)
{
	struct i915_address_space *old = i915_gem_context_vm(ctx);

	GEM_BUG_ON(old && i915_vm_is_4lvl(vm) != i915_vm_is_4lvl(old));

	rcu_assign_pointer(ctx->vm, i915_vm_open(vm));
	context_apply_all(ctx, __apply_ppgtt, vm);

	return old;
}

static void __assign_ppgtt(struct i915_gem_context *ctx,
			   struct i915_address_space *vm)
{
	if (vm == rcu_access_pointer(ctx->vm))
		return;

	vm = __set_ppgtt(ctx, vm);
	if (vm)
		i915_vm_close(vm);
}

static void __set_timeline(struct intel_timeline **dst,
			   struct intel_timeline *src)
{
	struct intel_timeline *old = *dst;

	*dst = src ? intel_timeline_get(src) : NULL;

	if (old)
		intel_timeline_put(old);
}

static void __apply_timeline(struct intel_context *ce, void *timeline)
{
	__set_timeline(&ce->timeline, timeline);
}

static void __assign_timeline(struct i915_gem_context *ctx,
			      struct intel_timeline *timeline)
{
	__set_timeline(&ctx->timeline, timeline);
	context_apply_all(ctx, __apply_timeline, timeline);
}

static struct i915_gem_context *
i915_gem_create_context(struct drm_i915_private *i915, unsigned int flags)
{
	struct i915_gem_context *ctx;

	if (flags & I915_CONTEXT_CREATE_FLAGS_SINGLE_TIMELINE &&
	    !HAS_EXECLISTS(i915))
		return ERR_PTR(-EINVAL);

	/* Reap the stale contexts */
	contexts_flush_free(&i915->gem.contexts);

	ctx = __create_context(i915);
	if (IS_ERR(ctx))
		return ctx;

	if (HAS_FULL_PPGTT(i915)) {
		struct i915_ppgtt *ppgtt;

		ppgtt = i915_ppgtt_create(&i915->gt);
		if (IS_ERR(ppgtt)) {
			DRM_DEBUG_DRIVER("PPGTT setup failed (%ld)\n",
					 PTR_ERR(ppgtt));
			context_close(ctx);
			return ERR_CAST(ppgtt);
		}

		mutex_lock(&ctx->mutex);
		__assign_ppgtt(ctx, &ppgtt->vm);
		mutex_unlock(&ctx->mutex);

		i915_vm_put(&ppgtt->vm);
	}

	if (flags & I915_CONTEXT_CREATE_FLAGS_SINGLE_TIMELINE) {
		struct intel_timeline *timeline;

		timeline = intel_timeline_create(&i915->gt, NULL);
		if (IS_ERR(timeline)) {
			context_close(ctx);
			return ERR_CAST(timeline);
		}

		__assign_timeline(ctx, timeline);
		intel_timeline_put(timeline);
	}

	trace_i915_context_create(ctx);

	return ctx;
}

static void init_contexts(struct i915_gem_contexts *gc)
{
	spin_lock_init(&gc->lock);
	INIT_LIST_HEAD(&gc->list);

	INIT_WORK(&gc->free_work, contexts_free_worker);
	init_llist_head(&gc->free_list);
}

void i915_gem_init__contexts(struct drm_i915_private *i915)
{
	init_contexts(&i915->gem.contexts);
	DRM_DEBUG_DRIVER("%s context support initialized\n",
			 DRIVER_CAPS(i915)->has_logical_contexts ?
			 "logical" : "fake");
}

void i915_gem_driver_release__contexts(struct drm_i915_private *i915)
{
	flush_work(&i915->gem.contexts.free_work);
}

static int vm_idr_cleanup(int id, void *p, void *data)
{
	i915_vm_put(p);
	return 0;
}

static int gem_context_register(struct i915_gem_context *ctx,
				struct drm_i915_file_private *fpriv,
				u32 *id)
{
	struct i915_address_space *vm;
	int ret;

	ctx->file_priv = fpriv;

	mutex_lock(&ctx->mutex);
	vm = i915_gem_context_vm(ctx);
	if (vm)
		WRITE_ONCE(vm->file, fpriv); /* XXX */
	mutex_unlock(&ctx->mutex);

	ctx->pid = get_task_pid(current, PIDTYPE_PID);
	snprintf(ctx->name, sizeof(ctx->name), "%s[%d]",
		 current->comm, pid_nr(ctx->pid));

	/* And finally expose ourselves to userspace via the idr */
	ret = xa_alloc(&fpriv->context_xa, id, ctx, xa_limit_32b, GFP_KERNEL);
	if (ret)
		put_pid(fetch_and_zero(&ctx->pid));

	return ret;
}

int i915_gem_context_open(struct drm_i915_private *i915,
			  struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_gem_context *ctx;
	int err;
	u32 id;

	xa_init_flags(&file_priv->context_xa, XA_FLAGS_ALLOC);

	mutex_init(&file_priv->vm_idr_lock);
	idr_init_base(&file_priv->vm_idr, 1);

	ctx = i915_gem_create_context(i915, 0);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto err;
	}

	err = gem_context_register(ctx, file_priv, &id);
	if (err < 0)
		goto err_ctx;

	GEM_BUG_ON(id);
	return 0;

err_ctx:
	context_close(ctx);
err:
	idr_destroy(&file_priv->vm_idr);
	xa_destroy(&file_priv->context_xa);
	mutex_destroy(&file_priv->vm_idr_lock);
	return err;
}

void i915_gem_context_close(struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct drm_i915_private *i915 = file_priv->dev_priv;
	struct i915_gem_context *ctx;
	unsigned long idx;

	xa_for_each(&file_priv->context_xa, idx, ctx)
		context_close(ctx);
	xa_destroy(&file_priv->context_xa);

	idr_for_each(&file_priv->vm_idr, vm_idr_cleanup, NULL);
	idr_destroy(&file_priv->vm_idr);
	mutex_destroy(&file_priv->vm_idr_lock);

	contexts_flush_free(&i915->gem.contexts);
}

int i915_gem_vm_create_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct drm_i915_gem_vm_control *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_ppgtt *ppgtt;
	int err;

	if (!HAS_FULL_PPGTT(i915))
		return -ENODEV;

	if (args->flags)
		return -EINVAL;

	ppgtt = i915_ppgtt_create(&i915->gt);
	if (IS_ERR(ppgtt))
		return PTR_ERR(ppgtt);

	ppgtt->vm.file = file_priv;

	if (args->extensions) {
		err = i915_user_extensions(u64_to_user_ptr(args->extensions),
					   NULL, 0,
					   ppgtt);
		if (err)
			goto err_put;
	}

	err = mutex_lock_interruptible(&file_priv->vm_idr_lock);
	if (err)
		goto err_put;

	err = idr_alloc(&file_priv->vm_idr, &ppgtt->vm, 0, 0, GFP_KERNEL);
	if (err < 0)
		goto err_unlock;

	GEM_BUG_ON(err == 0); /* reserved for invalid/unassigned ppgtt */

	mutex_unlock(&file_priv->vm_idr_lock);

	args->vm_id = err;
	return 0;

err_unlock:
	mutex_unlock(&file_priv->vm_idr_lock);
err_put:
	i915_vm_put(&ppgtt->vm);
	return err;
}

int i915_gem_vm_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct drm_i915_gem_vm_control *args = data;
	struct i915_address_space *vm;
	int err;
	u32 id;

	if (args->flags)
		return -EINVAL;

	if (args->extensions)
		return -EINVAL;

	id = args->vm_id;
	if (!id)
		return -ENOENT;

	err = mutex_lock_interruptible(&file_priv->vm_idr_lock);
	if (err)
		return err;

	vm = idr_remove(&file_priv->vm_idr, id);

	mutex_unlock(&file_priv->vm_idr_lock);
	if (!vm)
		return -ENOENT;

	i915_vm_put(vm);
	return 0;
}

struct context_barrier_task {
	struct i915_active base;
	void (*task)(void *data);
	void *data;
};

__i915_active_call
static void cb_retire(struct i915_active *base)
{
	struct context_barrier_task *cb = container_of(base, typeof(*cb), base);

	if (cb->task)
		cb->task(cb->data);

	i915_active_fini(&cb->base);
	kfree(cb);
}

I915_SELFTEST_DECLARE(static intel_engine_mask_t context_barrier_inject_fault);
static int context_barrier_task(struct i915_gem_context *ctx,
				intel_engine_mask_t engines,
				bool (*skip)(struct intel_context *ce, void *data),
				int (*emit)(struct i915_request *rq, void *data),
				void (*task)(void *data),
				void *data)
{
	struct context_barrier_task *cb;
	struct i915_gem_engines_iter it;
	struct intel_context *ce;
	int err = 0;

	GEM_BUG_ON(!task);

	cb = kmalloc(sizeof(*cb), GFP_KERNEL);
	if (!cb)
		return -ENOMEM;

	i915_active_init(&cb->base, NULL, cb_retire);
	err = i915_active_acquire(&cb->base);
	if (err) {
		kfree(cb);
		return err;
	}

	for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it) {
		struct i915_request *rq;

		if (I915_SELFTEST_ONLY(context_barrier_inject_fault &
				       ce->engine->mask)) {
			err = -ENXIO;
			break;
		}

		if (!(ce->engine->mask & engines))
			continue;

		if (skip && skip(ce, data))
			continue;

		rq = intel_context_create_request(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			break;
		}

		err = 0;
		if (emit)
			err = emit(rq, data);
		if (err == 0)
			err = i915_active_add_request(&cb->base, rq);

		i915_request_add(rq);
		if (err)
			break;
	}
	i915_gem_context_unlock_engines(ctx);

	cb->task = err ? NULL : task; /* caller needs to unwind instead */
	cb->data = data;

	i915_active_release(&cb->base);

	return err;
}

static int get_ppgtt(struct drm_i915_file_private *file_priv,
		     struct i915_gem_context *ctx,
		     struct drm_i915_gem_context_param *args)
{
	struct i915_address_space *vm;
	int ret;

	if (!rcu_access_pointer(ctx->vm))
		return -ENODEV;

	rcu_read_lock();
	vm = context_get_vm_rcu(ctx);
	rcu_read_unlock();

	ret = mutex_lock_interruptible(&file_priv->vm_idr_lock);
	if (ret)
		goto err_put;

	ret = idr_alloc(&file_priv->vm_idr, vm, 0, 0, GFP_KERNEL);
	GEM_BUG_ON(!ret);
	if (ret < 0)
		goto err_unlock;

	i915_vm_open(vm);

	args->size = 0;
	args->value = ret;

	ret = 0;
err_unlock:
	mutex_unlock(&file_priv->vm_idr_lock);
err_put:
	i915_vm_put(vm);
	return ret;
}

static void set_ppgtt_barrier(void *data)
{
	struct i915_address_space *old = data;

	if (INTEL_GEN(old->i915) < 8)
		gen6_ppgtt_unpin_all(i915_vm_to_ppgtt(old));

	i915_vm_close(old);
}

static int emit_ppgtt_update(struct i915_request *rq, void *data)
{
	struct i915_address_space *vm = rq->context->vm;
	struct intel_engine_cs *engine = rq->engine;
	u32 base = engine->mmio_base;
	u32 *cs;
	int i;

	if (i915_vm_is_4lvl(vm)) {
		struct i915_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
		const dma_addr_t pd_daddr = px_dma(ppgtt->pd);

		cs = intel_ring_begin(rq, 6);
		if (IS_ERR(cs))
			return PTR_ERR(cs);

		*cs++ = MI_LOAD_REGISTER_IMM(2);

		*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_UDW(base, 0));
		*cs++ = upper_32_bits(pd_daddr);
		*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_LDW(base, 0));
		*cs++ = lower_32_bits(pd_daddr);

		*cs++ = MI_NOOP;
		intel_ring_advance(rq, cs);
	} else if (HAS_LOGICAL_RING_CONTEXTS(engine->i915)) {
		struct i915_ppgtt *ppgtt = i915_vm_to_ppgtt(vm);
		int err;

		/* Magic required to prevent forcewake errors! */
		err = engine->emit_flush(rq, EMIT_INVALIDATE);
		if (err)
			return err;

		cs = intel_ring_begin(rq, 4 * GEN8_3LVL_PDPES + 2);
		if (IS_ERR(cs))
			return PTR_ERR(cs);

		*cs++ = MI_LOAD_REGISTER_IMM(2 * GEN8_3LVL_PDPES) | MI_LRI_FORCE_POSTED;
		for (i = GEN8_3LVL_PDPES; i--; ) {
			const dma_addr_t pd_daddr = i915_page_dir_dma_addr(ppgtt, i);

			*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_UDW(base, i));
			*cs++ = upper_32_bits(pd_daddr);
			*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_LDW(base, i));
			*cs++ = lower_32_bits(pd_daddr);
		}
		*cs++ = MI_NOOP;
		intel_ring_advance(rq, cs);
	}

	return 0;
}

static bool skip_ppgtt_update(struct intel_context *ce, void *data)
{
	if (!test_bit(CONTEXT_ALLOC_BIT, &ce->flags))
		return true;

	if (HAS_LOGICAL_RING_CONTEXTS(ce->engine->i915))
		return false;

	if (!atomic_read(&ce->pin_count))
		return true;

	/* ppGTT is not part of the legacy context image */
	if (gen6_ppgtt_pin(i915_vm_to_ppgtt(ce->vm)))
		return true;

	return false;
}

static int set_ppgtt(struct drm_i915_file_private *file_priv,
		     struct i915_gem_context *ctx,
		     struct drm_i915_gem_context_param *args)
{
	struct i915_address_space *vm, *old;
	int err;

	if (args->size)
		return -EINVAL;

	if (!rcu_access_pointer(ctx->vm))
		return -ENODEV;

	if (upper_32_bits(args->value))
		return -ENOENT;

	rcu_read_lock();
	vm = idr_find(&file_priv->vm_idr, args->value);
	if (vm && !kref_get_unless_zero(&vm->ref))
		vm = NULL;
	rcu_read_unlock();
	if (!vm)
		return -ENOENT;

	err = mutex_lock_interruptible(&ctx->mutex);
	if (err)
		goto out;

	if (i915_gem_context_is_closed(ctx)) {
		err = -ENOENT;
		goto unlock;
	}

	if (vm == rcu_access_pointer(ctx->vm))
		goto unlock;

	/* Teardown the existing obj:vma cache, it will have to be rebuilt. */
	lut_close(ctx);

	old = __set_ppgtt(ctx, vm);

	/*
	 * We need to flush any requests using the current ppgtt before
	 * we release it as the requests do not hold a reference themselves,
	 * only indirectly through the context.
	 */
	err = context_barrier_task(ctx, ALL_ENGINES,
				   skip_ppgtt_update,
				   emit_ppgtt_update,
				   set_ppgtt_barrier,
				   old);
	if (err) {
		i915_vm_close(__set_ppgtt(ctx, old));
		i915_vm_close(old);
	}

unlock:
	mutex_unlock(&ctx->mutex);
out:
	i915_vm_put(vm);
	return err;
}

static int gen8_emit_rpcs_config(struct i915_request *rq,
				 struct intel_context *ce,
				 struct intel_sseu sseu)
{
	u64 offset;
	u32 *cs;

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	offset = i915_ggtt_offset(ce->state) +
		 LRC_STATE_PN * PAGE_SIZE +
		 CTX_R_PWR_CLK_STATE * 4;

	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = lower_32_bits(offset);
	*cs++ = upper_32_bits(offset);
	*cs++ = intel_sseu_make_rpcs(rq->i915, &sseu);

	intel_ring_advance(rq, cs);

	return 0;
}

static int
gen8_modify_rpcs(struct intel_context *ce, struct intel_sseu sseu)
{
	struct i915_request *rq;
	int ret;

	lockdep_assert_held(&ce->pin_mutex);

	/*
	 * If the context is not idle, we have to submit an ordered request to
	 * modify its context image via the kernel context (writing to our own
	 * image, or into the registers directory, does not stick). Pristine
	 * and idle contexts will be configured on pinning.
	 */
	if (!intel_context_pin_if_active(ce))
		return 0;

	rq = intel_engine_create_kernel_request(ce->engine);
	if (IS_ERR(rq)) {
		ret = PTR_ERR(rq);
		goto out_unpin;
	}

	/* Serialise with the remote context */
	ret = intel_context_prepare_remote_request(ce, rq);
	if (ret == 0)
		ret = gen8_emit_rpcs_config(rq, ce, sseu);

	i915_request_add(rq);
out_unpin:
	intel_context_unpin(ce);
	return ret;
}

static int
intel_context_reconfigure_sseu(struct intel_context *ce, struct intel_sseu sseu)
{
	int ret;

	GEM_BUG_ON(INTEL_GEN(ce->engine->i915) < 8);

	ret = intel_context_lock_pinned(ce);
	if (ret)
		return ret;

	/* Nothing to do if unmodified. */
	if (!memcmp(&ce->sseu, &sseu, sizeof(sseu)))
		goto unlock;

	ret = gen8_modify_rpcs(ce, sseu);
	if (!ret)
		ce->sseu = sseu;

unlock:
	intel_context_unlock_pinned(ce);
	return ret;
}

static int
user_to_context_sseu(struct drm_i915_private *i915,
		     const struct drm_i915_gem_context_param_sseu *user,
		     struct intel_sseu *context)
{
	const struct sseu_dev_info *device = &RUNTIME_INFO(i915)->sseu;

	/* No zeros in any field. */
	if (!user->slice_mask || !user->subslice_mask ||
	    !user->min_eus_per_subslice || !user->max_eus_per_subslice)
		return -EINVAL;

	/* Max > min. */
	if (user->max_eus_per_subslice < user->min_eus_per_subslice)
		return -EINVAL;

	/*
	 * Some future proofing on the types since the uAPI is wider than the
	 * current internal implementation.
	 */
	if (overflows_type(user->slice_mask, context->slice_mask) ||
	    overflows_type(user->subslice_mask, context->subslice_mask) ||
	    overflows_type(user->min_eus_per_subslice,
			   context->min_eus_per_subslice) ||
	    overflows_type(user->max_eus_per_subslice,
			   context->max_eus_per_subslice))
		return -EINVAL;

	/* Check validity against hardware. */
	if (user->slice_mask & ~device->slice_mask)
		return -EINVAL;

	if (user->subslice_mask & ~device->subslice_mask[0])
		return -EINVAL;

	if (user->max_eus_per_subslice > device->max_eus_per_subslice)
		return -EINVAL;

	context->slice_mask = user->slice_mask;
	context->subslice_mask = user->subslice_mask;
	context->min_eus_per_subslice = user->min_eus_per_subslice;
	context->max_eus_per_subslice = user->max_eus_per_subslice;

	/* Part specific restrictions. */
	if (IS_GEN(i915, 11)) {
		unsigned int hw_s = hweight8(device->slice_mask);
		unsigned int hw_ss_per_s = hweight8(device->subslice_mask[0]);
		unsigned int req_s = hweight8(context->slice_mask);
		unsigned int req_ss = hweight8(context->subslice_mask);

		/*
		 * Only full subslice enablement is possible if more than one
		 * slice is turned on.
		 */
		if (req_s > 1 && req_ss != hw_ss_per_s)
			return -EINVAL;

		/*
		 * If more than four (SScount bitfield limit) subslices are
		 * requested then the number has to be even.
		 */
		if (req_ss > 4 && (req_ss & 1))
			return -EINVAL;

		/*
		 * If only one slice is enabled and subslice count is below the
		 * device full enablement, it must be at most half of the all
		 * available subslices.
		 */
		if (req_s == 1 && req_ss < hw_ss_per_s &&
		    req_ss > (hw_ss_per_s / 2))
			return -EINVAL;

		/* ABI restriction - VME use case only. */

		/* All slices or one slice only. */
		if (req_s != 1 && req_s != hw_s)
			return -EINVAL;

		/*
		 * Half subslices or full enablement only when one slice is
		 * enabled.
		 */
		if (req_s == 1 &&
		    (req_ss != hw_ss_per_s && req_ss != (hw_ss_per_s / 2)))
			return -EINVAL;

		/* No EU configuration changes. */
		if ((user->min_eus_per_subslice !=
		     device->max_eus_per_subslice) ||
		    (user->max_eus_per_subslice !=
		     device->max_eus_per_subslice))
			return -EINVAL;
	}

	return 0;
}

static int set_sseu(struct i915_gem_context *ctx,
		    struct drm_i915_gem_context_param *args)
{
	struct drm_i915_private *i915 = ctx->i915;
	struct drm_i915_gem_context_param_sseu user_sseu;
	struct intel_context *ce;
	struct intel_sseu sseu;
	unsigned long lookup;
	int ret;

	if (args->size < sizeof(user_sseu))
		return -EINVAL;

	if (!IS_GEN(i915, 11))
		return -ENODEV;

	if (copy_from_user(&user_sseu, u64_to_user_ptr(args->value),
			   sizeof(user_sseu)))
		return -EFAULT;

	if (user_sseu.rsvd)
		return -EINVAL;

	if (user_sseu.flags & ~(I915_CONTEXT_SSEU_FLAG_ENGINE_INDEX))
		return -EINVAL;

	lookup = 0;
	if (user_sseu.flags & I915_CONTEXT_SSEU_FLAG_ENGINE_INDEX)
		lookup |= LOOKUP_USER_INDEX;

	ce = lookup_user_engine(ctx, lookup, &user_sseu.engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	/* Only render engine supports RPCS configuration. */
	if (ce->engine->class != RENDER_CLASS) {
		ret = -ENODEV;
		goto out_ce;
	}

	ret = user_to_context_sseu(i915, &user_sseu, &sseu);
	if (ret)
		goto out_ce;

	ret = intel_context_reconfigure_sseu(ce, sseu);
	if (ret)
		goto out_ce;

	args->size = sizeof(user_sseu);

out_ce:
	intel_context_put(ce);
	return ret;
}

struct set_engines {
	struct i915_gem_context *ctx;
	struct i915_gem_engines *engines;
};

static int
set_engines__load_balance(struct i915_user_extension __user *base, void *data)
{
	struct i915_context_engines_load_balance __user *ext =
		container_of_user(base, typeof(*ext), base);
	const struct set_engines *set = data;
	struct intel_engine_cs *stack[16];
	struct intel_engine_cs **siblings;
	struct intel_context *ce;
	u16 num_siblings, idx;
	unsigned int n;
	int err;

	if (!HAS_EXECLISTS(set->ctx->i915))
		return -ENODEV;

	if (USES_GUC_SUBMISSION(set->ctx->i915))
		return -ENODEV; /* not implement yet */

	if (get_user(idx, &ext->engine_index))
		return -EFAULT;

	if (idx >= set->engines->num_engines) {
		DRM_DEBUG("Invalid placement value, %d >= %d\n",
			  idx, set->engines->num_engines);
		return -EINVAL;
	}

	idx = array_index_nospec(idx, set->engines->num_engines);
	if (set->engines->engines[idx]) {
		DRM_DEBUG("Invalid placement[%d], already occupied\n", idx);
		return -EEXIST;
	}

	if (get_user(num_siblings, &ext->num_siblings))
		return -EFAULT;

	err = check_user_mbz(&ext->flags);
	if (err)
		return err;

	err = check_user_mbz(&ext->mbz64);
	if (err)
		return err;

	siblings = stack;
	if (num_siblings > ARRAY_SIZE(stack)) {
		siblings = kmalloc_array(num_siblings,
					 sizeof(*siblings),
					 GFP_KERNEL);
		if (!siblings)
			return -ENOMEM;
	}

	for (n = 0; n < num_siblings; n++) {
		struct i915_engine_class_instance ci;

		if (copy_from_user(&ci, &ext->engines[n], sizeof(ci))) {
			err = -EFAULT;
			goto out_siblings;
		}

		siblings[n] = intel_engine_lookup_user(set->ctx->i915,
						       ci.engine_class,
						       ci.engine_instance);
		if (!siblings[n]) {
			DRM_DEBUG("Invalid sibling[%d]: { class:%d, inst:%d }\n",
				  n, ci.engine_class, ci.engine_instance);
			err = -EINVAL;
			goto out_siblings;
		}
	}

	ce = intel_execlists_create_virtual(siblings, n);
	if (IS_ERR(ce)) {
		err = PTR_ERR(ce);
		goto out_siblings;
	}

	intel_context_set_gem(ce, set->ctx);

	if (cmpxchg(&set->engines->engines[idx], NULL, ce)) {
		intel_context_put(ce);
		err = -EEXIST;
		goto out_siblings;
	}

out_siblings:
	if (siblings != stack)
		kfree(siblings);

	return err;
}

static int
set_engines__bond(struct i915_user_extension __user *base, void *data)
{
	struct i915_context_engines_bond __user *ext =
		container_of_user(base, typeof(*ext), base);
	const struct set_engines *set = data;
	struct i915_engine_class_instance ci;
	struct intel_engine_cs *virtual;
	struct intel_engine_cs *master;
	u16 idx, num_bonds;
	int err, n;

	if (get_user(idx, &ext->virtual_index))
		return -EFAULT;

	if (idx >= set->engines->num_engines) {
		DRM_DEBUG("Invalid index for virtual engine: %d >= %d\n",
			  idx, set->engines->num_engines);
		return -EINVAL;
	}

	idx = array_index_nospec(idx, set->engines->num_engines);
	if (!set->engines->engines[idx]) {
		DRM_DEBUG("Invalid engine at %d\n", idx);
		return -EINVAL;
	}
	virtual = set->engines->engines[idx]->engine;

	err = check_user_mbz(&ext->flags);
	if (err)
		return err;

	for (n = 0; n < ARRAY_SIZE(ext->mbz64); n++) {
		err = check_user_mbz(&ext->mbz64[n]);
		if (err)
			return err;
	}

	if (copy_from_user(&ci, &ext->master, sizeof(ci)))
		return -EFAULT;

	master = intel_engine_lookup_user(set->ctx->i915,
					  ci.engine_class, ci.engine_instance);
	if (!master) {
		DRM_DEBUG("Unrecognised master engine: { class:%u, instance:%u }\n",
			  ci.engine_class, ci.engine_instance);
		return -EINVAL;
	}

	if (get_user(num_bonds, &ext->num_bonds))
		return -EFAULT;

	for (n = 0; n < num_bonds; n++) {
		struct intel_engine_cs *bond;

		if (copy_from_user(&ci, &ext->engines[n], sizeof(ci)))
			return -EFAULT;

		bond = intel_engine_lookup_user(set->ctx->i915,
						ci.engine_class,
						ci.engine_instance);
		if (!bond) {
			DRM_DEBUG("Unrecognised engine[%d] for bonding: { class:%d, instance: %d }\n",
				  n, ci.engine_class, ci.engine_instance);
			return -EINVAL;
		}

		/*
		 * A non-virtual engine has no siblings to choose between; and
		 * a submit fence will always be directed to the one engine.
		 */
		if (intel_engine_is_virtual(virtual)) {
			err = intel_virtual_engine_attach_bond(virtual,
							       master,
							       bond);
			if (err)
				return err;
		}
	}

	return 0;
}

static const i915_user_extension_fn set_engines__extensions[] = {
	[I915_CONTEXT_ENGINES_EXT_LOAD_BALANCE] = set_engines__load_balance,
	[I915_CONTEXT_ENGINES_EXT_BOND] = set_engines__bond,
};

static int
set_engines(struct i915_gem_context *ctx,
	    const struct drm_i915_gem_context_param *args)
{
	struct i915_context_param_engines __user *user =
		u64_to_user_ptr(args->value);
	struct set_engines set = { .ctx = ctx };
	unsigned int num_engines, n;
	u64 extensions;
	int err;

	if (!args->size) { /* switch back to legacy user_ring_map */
		if (!i915_gem_context_user_engines(ctx))
			return 0;

		set.engines = default_engines(ctx);
		if (IS_ERR(set.engines))
			return PTR_ERR(set.engines);

		goto replace;
	}

	BUILD_BUG_ON(!IS_ALIGNED(sizeof(*user), sizeof(*user->engines)));
	if (args->size < sizeof(*user) ||
	    !IS_ALIGNED(args->size, sizeof(*user->engines))) {
		DRM_DEBUG("Invalid size for engine array: %d\n",
			  args->size);
		return -EINVAL;
	}

	/*
	 * Note that I915_EXEC_RING_MASK limits execbuf to only using the
	 * first 64 engines defined here.
	 */
	num_engines = (args->size - sizeof(*user)) / sizeof(*user->engines);

	set.engines = kmalloc(struct_size(set.engines, engines, num_engines),
			      GFP_KERNEL);
	if (!set.engines)
		return -ENOMEM;

	init_rcu_head(&set.engines->rcu);
	for (n = 0; n < num_engines; n++) {
		struct i915_engine_class_instance ci;
		struct intel_engine_cs *engine;
		struct intel_context *ce;

		if (copy_from_user(&ci, &user->engines[n], sizeof(ci))) {
			__free_engines(set.engines, n);
			return -EFAULT;
		}

		if (ci.engine_class == (u16)I915_ENGINE_CLASS_INVALID &&
		    ci.engine_instance == (u16)I915_ENGINE_CLASS_INVALID_NONE) {
			set.engines->engines[n] = NULL;
			continue;
		}

		engine = intel_engine_lookup_user(ctx->i915,
						  ci.engine_class,
						  ci.engine_instance);
		if (!engine) {
			DRM_DEBUG("Invalid engine[%d]: { class:%d, instance:%d }\n",
				  n, ci.engine_class, ci.engine_instance);
			__free_engines(set.engines, n);
			return -ENOENT;
		}

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			__free_engines(set.engines, n);
			return PTR_ERR(ce);
		}

		intel_context_set_gem(ce, ctx);

		set.engines->engines[n] = ce;
	}
	set.engines->num_engines = num_engines;

	err = -EFAULT;
	if (!get_user(extensions, &user->extensions))
		err = i915_user_extensions(u64_to_user_ptr(extensions),
					   set_engines__extensions,
					   ARRAY_SIZE(set_engines__extensions),
					   &set);
	if (err) {
		free_engines(set.engines);
		return err;
	}

replace:
	mutex_lock(&ctx->engines_mutex);
	if (args->size)
		i915_gem_context_set_user_engines(ctx);
	else
		i915_gem_context_clear_user_engines(ctx);
	set.engines = rcu_replace_pointer(ctx->engines, set.engines, 1);
	mutex_unlock(&ctx->engines_mutex);

	call_rcu(&set.engines->rcu, free_engines_rcu);

	return 0;
}

static struct i915_gem_engines *
__copy_engines(struct i915_gem_engines *e)
{
	struct i915_gem_engines *copy;
	unsigned int n;

	copy = kmalloc(struct_size(e, engines, e->num_engines), GFP_KERNEL);
	if (!copy)
		return ERR_PTR(-ENOMEM);

	init_rcu_head(&copy->rcu);
	for (n = 0; n < e->num_engines; n++) {
		if (e->engines[n])
			copy->engines[n] = intel_context_get(e->engines[n]);
		else
			copy->engines[n] = NULL;
	}
	copy->num_engines = n;

	return copy;
}

static int
get_engines(struct i915_gem_context *ctx,
	    struct drm_i915_gem_context_param *args)
{
	struct i915_context_param_engines __user *user;
	struct i915_gem_engines *e;
	size_t n, count, size;
	int err = 0;

	err = mutex_lock_interruptible(&ctx->engines_mutex);
	if (err)
		return err;

	e = NULL;
	if (i915_gem_context_user_engines(ctx))
		e = __copy_engines(i915_gem_context_engines(ctx));
	mutex_unlock(&ctx->engines_mutex);
	if (IS_ERR_OR_NULL(e)) {
		args->size = 0;
		return PTR_ERR_OR_ZERO(e);
	}

	count = e->num_engines;

	/* Be paranoid in case we have an impedance mismatch */
	if (!check_struct_size(user, engines, count, &size)) {
		err = -EINVAL;
		goto err_free;
	}
	if (overflows_type(size, args->size)) {
		err = -EINVAL;
		goto err_free;
	}

	if (!args->size) {
		args->size = size;
		goto err_free;
	}

	if (args->size < size) {
		err = -EINVAL;
		goto err_free;
	}

	user = u64_to_user_ptr(args->value);
	if (!access_ok(user, size)) {
		err = -EFAULT;
		goto err_free;
	}

	if (put_user(0, &user->extensions)) {
		err = -EFAULT;
		goto err_free;
	}

	for (n = 0; n < count; n++) {
		struct i915_engine_class_instance ci = {
			.engine_class = I915_ENGINE_CLASS_INVALID,
			.engine_instance = I915_ENGINE_CLASS_INVALID_NONE,
		};

		if (e->engines[n]) {
			ci.engine_class = e->engines[n]->engine->uabi_class;
			ci.engine_instance = e->engines[n]->engine->uabi_instance;
		}

		if (copy_to_user(&user->engines[n], &ci, sizeof(ci))) {
			err = -EFAULT;
			goto err_free;
		}
	}

	args->size = size;

err_free:
	free_engines(e);
	return err;
}

static int
set_persistence(struct i915_gem_context *ctx,
		const struct drm_i915_gem_context_param *args)
{
	if (args->size)
		return -EINVAL;

	return __context_set_persistence(ctx, args->value);
}

static void __apply_priority(struct intel_context *ce, void *arg)
{
	struct i915_gem_context *ctx = arg;

	if (!intel_engine_has_semaphores(ce->engine))
		return;

	if (ctx->sched.priority >= I915_PRIORITY_NORMAL)
		intel_context_set_use_semaphores(ce);
	else
		intel_context_clear_use_semaphores(ce);
}

static int set_priority(struct i915_gem_context *ctx,
			const struct drm_i915_gem_context_param *args)
{
	s64 priority = args->value;

	if (args->size)
		return -EINVAL;

	if (!(ctx->i915->caps.scheduler & I915_SCHEDULER_CAP_PRIORITY))
		return -ENODEV;

	if (priority > I915_CONTEXT_MAX_USER_PRIORITY ||
	    priority < I915_CONTEXT_MIN_USER_PRIORITY)
		return -EINVAL;

	if (priority > I915_CONTEXT_DEFAULT_PRIORITY &&
	    !capable(CAP_SYS_NICE))
		return -EPERM;

	ctx->sched.priority = I915_USER_PRIORITY(priority);
	context_apply_all(ctx, __apply_priority, ctx);

	return 0;
}

static int ctx_setparam(struct drm_i915_file_private *fpriv,
			struct i915_gem_context *ctx,
			struct drm_i915_gem_context_param *args)
{
	int ret = 0;

	switch (args->param) {
	case I915_CONTEXT_PARAM_NO_ZEROMAP:
		if (args->size)
			ret = -EINVAL;
		else if (args->value)
			set_bit(UCONTEXT_NO_ZEROMAP, &ctx->user_flags);
		else
			clear_bit(UCONTEXT_NO_ZEROMAP, &ctx->user_flags);
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

	case I915_CONTEXT_PARAM_RECOVERABLE:
		if (args->size)
			ret = -EINVAL;
		else if (args->value)
			i915_gem_context_set_recoverable(ctx);
		else
			i915_gem_context_clear_recoverable(ctx);
		break;

	case I915_CONTEXT_PARAM_PRIORITY:
		ret = set_priority(ctx, args);
		break;

	case I915_CONTEXT_PARAM_SSEU:
		ret = set_sseu(ctx, args);
		break;

	case I915_CONTEXT_PARAM_VM:
		ret = set_ppgtt(fpriv, ctx, args);
		break;

	case I915_CONTEXT_PARAM_ENGINES:
		ret = set_engines(ctx, args);
		break;

	case I915_CONTEXT_PARAM_PERSISTENCE:
		ret = set_persistence(ctx, args);
		break;

	case I915_CONTEXT_PARAM_BAN_PERIOD:
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

struct create_ext {
	struct i915_gem_context *ctx;
	struct drm_i915_file_private *fpriv;
};

static int create_setparam(struct i915_user_extension __user *ext, void *data)
{
	struct drm_i915_gem_context_create_ext_setparam local;
	const struct create_ext *arg = data;

	if (copy_from_user(&local, ext, sizeof(local)))
		return -EFAULT;

	if (local.param.ctx_id)
		return -EINVAL;

	return ctx_setparam(arg->fpriv, arg->ctx, &local.param);
}

static int clone_engines(struct i915_gem_context *dst,
			 struct i915_gem_context *src)
{
	struct i915_gem_engines *e = i915_gem_context_lock_engines(src);
	struct i915_gem_engines *clone;
	bool user_engines;
	unsigned long n;

	clone = kmalloc(struct_size(e, engines, e->num_engines), GFP_KERNEL);
	if (!clone)
		goto err_unlock;

	init_rcu_head(&clone->rcu);
	for (n = 0; n < e->num_engines; n++) {
		struct intel_engine_cs *engine;

		if (!e->engines[n]) {
			clone->engines[n] = NULL;
			continue;
		}
		engine = e->engines[n]->engine;

		/*
		 * Virtual engines are singletons; they can only exist
		 * inside a single context, because they embed their
		 * HW context... As each virtual context implies a single
		 * timeline (each engine can only dequeue a single request
		 * at any time), it would be surprising for two contexts
		 * to use the same engine. So let's create a copy of
		 * the virtual engine instead.
		 */
		if (intel_engine_is_virtual(engine))
			clone->engines[n] =
				intel_execlists_clone_virtual(engine);
		else
			clone->engines[n] = intel_context_create(engine);
		if (IS_ERR_OR_NULL(clone->engines[n])) {
			__free_engines(clone, n);
			goto err_unlock;
		}

		intel_context_set_gem(clone->engines[n], dst);
	}
	clone->num_engines = n;

	user_engines = i915_gem_context_user_engines(src);
	i915_gem_context_unlock_engines(src);

	/* Serialised by constructor */
	free_engines(__context_engines_static(dst));
	RCU_INIT_POINTER(dst->engines, clone);
	if (user_engines)
		i915_gem_context_set_user_engines(dst);
	else
		i915_gem_context_clear_user_engines(dst);
	return 0;

err_unlock:
	i915_gem_context_unlock_engines(src);
	return -ENOMEM;
}

static int clone_flags(struct i915_gem_context *dst,
		       struct i915_gem_context *src)
{
	dst->user_flags = src->user_flags;
	return 0;
}

static int clone_schedattr(struct i915_gem_context *dst,
			   struct i915_gem_context *src)
{
	dst->sched = src->sched;
	return 0;
}

static int clone_sseu(struct i915_gem_context *dst,
		      struct i915_gem_context *src)
{
	struct i915_gem_engines *e = i915_gem_context_lock_engines(src);
	struct i915_gem_engines *clone;
	unsigned long n;
	int err;

	/* no locking required; sole access under constructor*/
	clone = __context_engines_static(dst);
	if (e->num_engines != clone->num_engines) {
		err = -EINVAL;
		goto unlock;
	}

	for (n = 0; n < e->num_engines; n++) {
		struct intel_context *ce = e->engines[n];

		if (clone->engines[n]->engine->class != ce->engine->class) {
			/* Must have compatible engine maps! */
			err = -EINVAL;
			goto unlock;
		}

		/* serialises with set_sseu */
		err = intel_context_lock_pinned(ce);
		if (err)
			goto unlock;

		clone->engines[n]->sseu = ce->sseu;
		intel_context_unlock_pinned(ce);
	}

	err = 0;
unlock:
	i915_gem_context_unlock_engines(src);
	return err;
}

static int clone_timeline(struct i915_gem_context *dst,
			  struct i915_gem_context *src)
{
	if (src->timeline)
		__assign_timeline(dst, src->timeline);

	return 0;
}

static int clone_vm(struct i915_gem_context *dst,
		    struct i915_gem_context *src)
{
	struct i915_address_space *vm;
	int err = 0;

	if (!rcu_access_pointer(src->vm))
		return 0;

	rcu_read_lock();
	vm = context_get_vm_rcu(src);
	rcu_read_unlock();

	if (!mutex_lock_interruptible(&dst->mutex)) {
		__assign_ppgtt(dst, vm);
		mutex_unlock(&dst->mutex);
	} else {
		err = -EINTR;
	}

	i915_vm_put(vm);
	return err;
}

static int create_clone(struct i915_user_extension __user *ext, void *data)
{
	static int (* const fn[])(struct i915_gem_context *dst,
				  struct i915_gem_context *src) = {
#define MAP(x, y) [ilog2(I915_CONTEXT_CLONE_##x)] = y
		MAP(ENGINES, clone_engines),
		MAP(FLAGS, clone_flags),
		MAP(SCHEDATTR, clone_schedattr),
		MAP(SSEU, clone_sseu),
		MAP(TIMELINE, clone_timeline),
		MAP(VM, clone_vm),
#undef MAP
	};
	struct drm_i915_gem_context_create_ext_clone local;
	const struct create_ext *arg = data;
	struct i915_gem_context *dst = arg->ctx;
	struct i915_gem_context *src;
	int err, bit;

	if (copy_from_user(&local, ext, sizeof(local)))
		return -EFAULT;

	BUILD_BUG_ON(GENMASK(BITS_PER_TYPE(local.flags) - 1, ARRAY_SIZE(fn)) !=
		     I915_CONTEXT_CLONE_UNKNOWN);

	if (local.flags & I915_CONTEXT_CLONE_UNKNOWN)
		return -EINVAL;

	if (local.rsvd)
		return -EINVAL;

	rcu_read_lock();
	src = __i915_gem_context_lookup_rcu(arg->fpriv, local.clone_id);
	rcu_read_unlock();
	if (!src)
		return -ENOENT;

	GEM_BUG_ON(src == dst);

	for (bit = 0; bit < ARRAY_SIZE(fn); bit++) {
		if (!(local.flags & BIT(bit)))
			continue;

		err = fn[bit](dst, src);
		if (err)
			return err;
	}

	return 0;
}

static const i915_user_extension_fn create_extensions[] = {
	[I915_CONTEXT_CREATE_EXT_SETPARAM] = create_setparam,
	[I915_CONTEXT_CREATE_EXT_CLONE] = create_clone,
};

static bool client_is_banned(struct drm_i915_file_private *file_priv)
{
	return atomic_read(&file_priv->ban_score) >= I915_CLIENT_SCORE_BANNED;
}

int i915_gem_context_create_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct drm_i915_gem_context_create_ext *args = data;
	struct create_ext ext_data;
	int ret;
	u32 id;

	if (!DRIVER_CAPS(i915)->has_logical_contexts)
		return -ENODEV;

	if (args->flags & I915_CONTEXT_CREATE_FLAGS_UNKNOWN)
		return -EINVAL;

	ret = intel_gt_terminally_wedged(&i915->gt);
	if (ret)
		return ret;

	ext_data.fpriv = file->driver_priv;
	if (client_is_banned(ext_data.fpriv)) {
		DRM_DEBUG("client %s[%d] banned from creating ctx\n",
			  current->comm, task_pid_nr(current));
		return -EIO;
	}

	ext_data.ctx = i915_gem_create_context(i915, args->flags);
	if (IS_ERR(ext_data.ctx))
		return PTR_ERR(ext_data.ctx);

	if (args->flags & I915_CONTEXT_CREATE_FLAGS_USE_EXTENSIONS) {
		ret = i915_user_extensions(u64_to_user_ptr(args->extensions),
					   create_extensions,
					   ARRAY_SIZE(create_extensions),
					   &ext_data);
		if (ret)
			goto err_ctx;
	}

	ret = gem_context_register(ext_data.ctx, ext_data.fpriv, &id);
	if (ret < 0)
		goto err_ctx;

	args->ctx_id = id;
	DRM_DEBUG("HW context %d created\n", args->ctx_id);

	return 0;

err_ctx:
	context_close(ext_data.ctx);
	return ret;
}

int i915_gem_context_destroy_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file)
{
	struct drm_i915_gem_context_destroy *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_gem_context *ctx;

	if (args->pad != 0)
		return -EINVAL;

	if (!args->ctx_id)
		return -ENOENT;

	ctx = xa_erase(&file_priv->context_xa, args->ctx_id);
	if (!ctx)
		return -ENOENT;

	context_close(ctx);
	return 0;
}

static int get_sseu(struct i915_gem_context *ctx,
		    struct drm_i915_gem_context_param *args)
{
	struct drm_i915_gem_context_param_sseu user_sseu;
	struct intel_context *ce;
	unsigned long lookup;
	int err;

	if (args->size == 0)
		goto out;
	else if (args->size < sizeof(user_sseu))
		return -EINVAL;

	if (copy_from_user(&user_sseu, u64_to_user_ptr(args->value),
			   sizeof(user_sseu)))
		return -EFAULT;

	if (user_sseu.rsvd)
		return -EINVAL;

	if (user_sseu.flags & ~(I915_CONTEXT_SSEU_FLAG_ENGINE_INDEX))
		return -EINVAL;

	lookup = 0;
	if (user_sseu.flags & I915_CONTEXT_SSEU_FLAG_ENGINE_INDEX)
		lookup |= LOOKUP_USER_INDEX;

	ce = lookup_user_engine(ctx, lookup, &user_sseu.engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	err = intel_context_lock_pinned(ce); /* serialises with set_sseu */
	if (err) {
		intel_context_put(ce);
		return err;
	}

	user_sseu.slice_mask = ce->sseu.slice_mask;
	user_sseu.subslice_mask = ce->sseu.subslice_mask;
	user_sseu.min_eus_per_subslice = ce->sseu.min_eus_per_subslice;
	user_sseu.max_eus_per_subslice = ce->sseu.max_eus_per_subslice;

	intel_context_unlock_pinned(ce);
	intel_context_put(ce);

	if (copy_to_user(u64_to_user_ptr(args->value), &user_sseu,
			 sizeof(user_sseu)))
		return -EFAULT;

out:
	args->size = sizeof(user_sseu);

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

	switch (args->param) {
	case I915_CONTEXT_PARAM_NO_ZEROMAP:
		args->size = 0;
		args->value = test_bit(UCONTEXT_NO_ZEROMAP, &ctx->user_flags);
		break;

	case I915_CONTEXT_PARAM_GTT_SIZE:
		args->size = 0;
		rcu_read_lock();
		if (rcu_access_pointer(ctx->vm))
			args->value = rcu_dereference(ctx->vm)->total;
		else
			args->value = to_i915(dev)->ggtt.vm.total;
		rcu_read_unlock();
		break;

	case I915_CONTEXT_PARAM_NO_ERROR_CAPTURE:
		args->size = 0;
		args->value = i915_gem_context_no_error_capture(ctx);
		break;

	case I915_CONTEXT_PARAM_BANNABLE:
		args->size = 0;
		args->value = i915_gem_context_is_bannable(ctx);
		break;

	case I915_CONTEXT_PARAM_RECOVERABLE:
		args->size = 0;
		args->value = i915_gem_context_is_recoverable(ctx);
		break;

	case I915_CONTEXT_PARAM_PRIORITY:
		args->size = 0;
		args->value = ctx->sched.priority >> I915_USER_PRIORITY_SHIFT;
		break;

	case I915_CONTEXT_PARAM_SSEU:
		ret = get_sseu(ctx, args);
		break;

	case I915_CONTEXT_PARAM_VM:
		ret = get_ppgtt(file_priv, ctx, args);
		break;

	case I915_CONTEXT_PARAM_ENGINES:
		ret = get_engines(ctx, args);
		break;

	case I915_CONTEXT_PARAM_PERSISTENCE:
		args->size = 0;
		args->value = i915_gem_context_is_persistent(ctx);
		break;

	case I915_CONTEXT_PARAM_BAN_PERIOD:
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

	ret = ctx_setparam(file_priv, ctx, args);

	i915_gem_context_put(ctx);
	return ret;
}

int i915_gem_context_reset_stats_ioctl(struct drm_device *dev,
				       void *data, struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
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
		args->reset_count = i915_reset_count(&i915->gpu_error);
	else
		args->reset_count = 0;

	args->batch_active = atomic_read(&ctx->guilty_count);
	args->batch_pending = atomic_read(&ctx->active_count);

	ret = 0;
out:
	rcu_read_unlock();
	return ret;
}

/* GEM context-engines iterator: for_each_gem_engine() */
struct intel_context *
i915_gem_engines_iter_next(struct i915_gem_engines_iter *it)
{
	const struct i915_gem_engines *e = it->engines;
	struct intel_context *ctx;

	do {
		if (it->idx >= e->num_engines)
			return NULL;

		ctx = e->engines[it->idx++];
	} while (!ctx);

	return ctx;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_context.c"
#include "selftests/i915_gem_context.c"
#endif

static void i915_global_gem_context_shrink(void)
{
	kmem_cache_shrink(global.slab_luts);
}

static void i915_global_gem_context_exit(void)
{
	kmem_cache_destroy(global.slab_luts);
}

static struct i915_global_gem_context global = { {
	.shrink = i915_global_gem_context_shrink,
	.exit = i915_global_gem_context_exit,
} };

int __init i915_global_gem_context_init(void)
{
	global.slab_luts = KMEM_CACHE(i915_lut_handle, 0);
	if (!global.slab_luts)
		return -ENOMEM;

	i915_global_register(&global.base);
	return 0;
}
