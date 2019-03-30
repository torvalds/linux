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
#include <drm/i915_drm.h>
#include "i915_drv.h"
#include "i915_globals.h"
#include "i915_trace.h"
#include "i915_user_extensions.h"
#include "intel_lrc_reg.h"
#include "intel_workarounds.h"

#define I915_CONTEXT_CREATE_FLAGS_SINGLE_TIMELINE (1 << 1)
#define I915_CONTEXT_PARAM_VM 0x9

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
	struct i915_lut_handle *lut, *ln;
	struct radix_tree_iter iter;
	void __rcu **slot;

	list_for_each_entry_safe(lut, ln, &ctx->handles_list, ctx_link) {
		list_del(&lut->obj_link);
		i915_lut_handle_free(lut);
	}
	INIT_LIST_HEAD(&ctx->handles_list);

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &ctx->handles_vma, &iter, 0) {
		struct i915_vma *vma = rcu_dereference_raw(*slot);

		radix_tree_iter_delete(&ctx->handles_vma, &iter, slot);

		vma->open_count--;
		__i915_gem_object_release_unless_active(vma->obj);
	}
	rcu_read_unlock();
}

static inline int new_hw_id(struct drm_i915_private *i915, gfp_t gfp)
{
	unsigned int max;

	lockdep_assert_held(&i915->contexts.mutex);

	if (INTEL_GEN(i915) >= 11)
		max = GEN11_MAX_CONTEXT_HW_ID;
	else if (USES_GUC_SUBMISSION(i915))
		/*
		 * When using GuC in proxy submission, GuC consumes the
		 * highest bit in the context id to indicate proxy submission.
		 */
		max = MAX_GUC_CONTEXT_HW_ID;
	else
		max = MAX_CONTEXT_HW_ID;

	return ida_simple_get(&i915->contexts.hw_ida, 0, max, gfp);
}

static int steal_hw_id(struct drm_i915_private *i915)
{
	struct i915_gem_context *ctx, *cn;
	LIST_HEAD(pinned);
	int id = -ENOSPC;

	lockdep_assert_held(&i915->contexts.mutex);

	list_for_each_entry_safe(ctx, cn,
				 &i915->contexts.hw_id_list, hw_id_link) {
		if (atomic_read(&ctx->hw_id_pin_count)) {
			list_move_tail(&ctx->hw_id_link, &pinned);
			continue;
		}

		GEM_BUG_ON(!ctx->hw_id); /* perma-pinned kernel context */
		list_del_init(&ctx->hw_id_link);
		id = ctx->hw_id;
		break;
	}

	/*
	 * Remember how far we got up on the last repossesion scan, so the
	 * list is kept in a "least recently scanned" order.
	 */
	list_splice_tail(&pinned, &i915->contexts.hw_id_list);
	return id;
}

static int assign_hw_id(struct drm_i915_private *i915, unsigned int *out)
{
	int ret;

	lockdep_assert_held(&i915->contexts.mutex);

	/*
	 * We prefer to steal/stall ourselves and our users over that of the
	 * entire system. That may be a little unfair to our users, and
	 * even hurt high priority clients. The choice is whether to oomkill
	 * something else, or steal a context id.
	 */
	ret = new_hw_id(i915, GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	if (unlikely(ret < 0)) {
		ret = steal_hw_id(i915);
		if (ret < 0) /* once again for the correct errno code */
			ret = new_hw_id(i915, GFP_KERNEL);
		if (ret < 0)
			return ret;
	}

	*out = ret;
	return 0;
}

static void release_hw_id(struct i915_gem_context *ctx)
{
	struct drm_i915_private *i915 = ctx->i915;

	if (list_empty(&ctx->hw_id_link))
		return;

	mutex_lock(&i915->contexts.mutex);
	if (!list_empty(&ctx->hw_id_link)) {
		ida_simple_remove(&i915->contexts.hw_ida, ctx->hw_id);
		list_del_init(&ctx->hw_id_link);
	}
	mutex_unlock(&i915->contexts.mutex);
}

static void i915_gem_context_free(struct i915_gem_context *ctx)
{
	struct intel_context *it, *n;

	lockdep_assert_held(&ctx->i915->drm.struct_mutex);
	GEM_BUG_ON(!i915_gem_context_is_closed(ctx));
	GEM_BUG_ON(!list_empty(&ctx->active_engines));

	release_hw_id(ctx);
	i915_ppgtt_put(ctx->ppgtt);

	rbtree_postorder_for_each_entry_safe(it, n, &ctx->hw_contexts, node)
		intel_context_put(it);

	if (ctx->timeline)
		i915_timeline_put(ctx->timeline);

	kfree(ctx->name);
	put_pid(ctx->pid);

	list_del(&ctx->link);
	mutex_destroy(&ctx->mutex);

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
	 * This context will never again be assinged to HW, so we can
	 * reuse its ID for the next context.
	 */
	release_hw_id(ctx);

	/*
	 * The LUT uses the VMA as a backpointer to unref the object,
	 * so we need to clear the LUT before we close all the VMA (inside
	 * the ppgtt).
	 */
	lut_close(ctx);

	ctx->file_priv = ERR_PTR(-EBADF);
	i915_gem_context_put(ctx);
}

static u32 default_desc_template(const struct drm_i915_private *i915,
				 const struct i915_hw_ppgtt *ppgtt)
{
	u32 address_mode;
	u32 desc;

	desc = GEN8_CTX_VALID | GEN8_CTX_PRIVILEGE;

	address_mode = INTEL_LEGACY_32B_CONTEXT;
	if (ppgtt && i915_vm_is_4lvl(&ppgtt->vm))
		address_mode = INTEL_LEGACY_64B_CONTEXT;
	desc |= address_mode << GEN8_CTX_ADDRESSING_MODE_SHIFT;

	if (IS_GEN(i915, 8))
		desc |= GEN8_CTX_L3LLC_COHERENT;

	/* TODO: WaDisableLiteRestore when we start using semaphore
	 * signalling between Command Streamers
	 * ring->ctx_desc_template |= GEN8_CTX_FORCE_RESTORE;
	 */

	return desc;
}

static struct i915_gem_context *
__create_context(struct drm_i915_private *dev_priv)
{
	struct i915_gem_context *ctx;
	int i;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	kref_init(&ctx->ref);
	list_add_tail(&ctx->link, &dev_priv->contexts.list);
	ctx->i915 = dev_priv;
	ctx->sched.priority = I915_USER_PRIORITY(I915_PRIORITY_NORMAL);
	INIT_LIST_HEAD(&ctx->active_engines);
	mutex_init(&ctx->mutex);

	ctx->hw_contexts = RB_ROOT;
	spin_lock_init(&ctx->hw_contexts_lock);

	INIT_RADIX_TREE(&ctx->handles_vma, GFP_KERNEL);
	INIT_LIST_HEAD(&ctx->handles_list);
	INIT_LIST_HEAD(&ctx->hw_id_link);

	/* NB: Mark all slices as needing a remap so that when the context first
	 * loads it will restore whatever remap state already exists. If there
	 * is no remap info, it will be a NOP. */
	ctx->remap_slice = ALL_L3_SLICES(dev_priv);

	i915_gem_context_set_bannable(ctx);
	i915_gem_context_set_recoverable(ctx);

	ctx->ring_size = 4 * PAGE_SIZE;
	ctx->desc_template =
		default_desc_template(dev_priv, dev_priv->mm.aliasing_ppgtt);

	for (i = 0; i < ARRAY_SIZE(ctx->hang_timestamp); i++)
		ctx->hang_timestamp[i] = jiffies - CONTEXT_FAST_HANG_JIFFIES;

	return ctx;
}

static struct i915_hw_ppgtt *
__set_ppgtt(struct i915_gem_context *ctx, struct i915_hw_ppgtt *ppgtt)
{
	struct i915_hw_ppgtt *old = ctx->ppgtt;

	ctx->ppgtt = i915_ppgtt_get(ppgtt);
	ctx->desc_template = default_desc_template(ctx->i915, ppgtt);

	return old;
}

static void __assign_ppgtt(struct i915_gem_context *ctx,
			   struct i915_hw_ppgtt *ppgtt)
{
	if (ppgtt == ctx->ppgtt)
		return;

	ppgtt = __set_ppgtt(ctx, ppgtt);
	if (ppgtt)
		i915_ppgtt_put(ppgtt);
}

static struct i915_gem_context *
i915_gem_create_context(struct drm_i915_private *dev_priv, unsigned int flags)
{
	struct i915_gem_context *ctx;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	BUILD_BUG_ON(I915_CONTEXT_CREATE_FLAGS_SINGLE_TIMELINE &
		     ~I915_CONTEXT_CREATE_FLAGS_UNKNOWN);
	if (flags & I915_CONTEXT_CREATE_FLAGS_SINGLE_TIMELINE &&
	    !HAS_EXECLISTS(dev_priv))
		return ERR_PTR(-EINVAL);

	/* Reap the most stale context */
	contexts_free_first(dev_priv);

	ctx = __create_context(dev_priv);
	if (IS_ERR(ctx))
		return ctx;

	if (HAS_FULL_PPGTT(dev_priv)) {
		struct i915_hw_ppgtt *ppgtt;

		ppgtt = i915_ppgtt_create(dev_priv);
		if (IS_ERR(ppgtt)) {
			DRM_DEBUG_DRIVER("PPGTT setup failed (%ld)\n",
					 PTR_ERR(ppgtt));
			context_close(ctx);
			return ERR_CAST(ppgtt);
		}

		__assign_ppgtt(ctx, ppgtt);
		i915_ppgtt_put(ppgtt);
	}

	if (flags & I915_CONTEXT_CREATE_FLAGS_SINGLE_TIMELINE) {
		struct i915_timeline *timeline;

		timeline = i915_timeline_create(dev_priv, NULL);
		if (IS_ERR(timeline)) {
			context_close(ctx);
			return ERR_CAST(timeline);
		}

		ctx->timeline = timeline;
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

	ctx = i915_gem_create_context(to_i915(dev), 0);
	if (IS_ERR(ctx))
		goto out;

	ret = i915_gem_context_pin_hw_id(ctx);
	if (ret) {
		context_close(ctx);
		ctx = ERR_PTR(ret);
		goto out;
	}

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

struct i915_gem_context *
i915_gem_context_create_kernel(struct drm_i915_private *i915, int prio)
{
	struct i915_gem_context *ctx;
	int err;

	ctx = i915_gem_create_context(i915, 0);
	if (IS_ERR(ctx))
		return ctx;

	err = i915_gem_context_pin_hw_id(ctx);
	if (err) {
		destroy_kernel_context(&ctx);
		return ERR_PTR(err);
	}

	i915_gem_context_clear_bannable(ctx);
	ctx->sched.priority = I915_USER_PRIORITY(prio);
	ctx->ring_size = PAGE_SIZE;

	GEM_BUG_ON(!i915_gem_context_is_kernel(ctx));

	return ctx;
}

static void init_contexts(struct drm_i915_private *i915)
{
	mutex_init(&i915->contexts.mutex);
	INIT_LIST_HEAD(&i915->contexts.list);

	/* Using the simple ida interface, the max is limited by sizeof(int) */
	BUILD_BUG_ON(MAX_CONTEXT_HW_ID > INT_MAX);
	BUILD_BUG_ON(GEN11_MAX_CONTEXT_HW_ID > INT_MAX);
	ida_init(&i915->contexts.hw_ida);
	INIT_LIST_HEAD(&i915->contexts.hw_id_list);

	INIT_WORK(&i915->contexts.free_work, contexts_free_worker);
	init_llist_head(&i915->contexts.free_list);
}

static bool needs_preempt_context(struct drm_i915_private *i915)
{
	return HAS_LOGICAL_RING_PREEMPTION(i915);
}

int i915_gem_contexts_init(struct drm_i915_private *dev_priv)
{
	struct i915_gem_context *ctx;

	/* Reassure ourselves we are only called once */
	GEM_BUG_ON(dev_priv->kernel_context);
	GEM_BUG_ON(dev_priv->preempt_context);

	intel_engine_init_ctx_wa(dev_priv->engine[RCS0]);
	init_contexts(dev_priv);

	/* lowest priority; idle task */
	ctx = i915_gem_context_create_kernel(dev_priv, I915_PRIORITY_MIN);
	if (IS_ERR(ctx)) {
		DRM_ERROR("Failed to create default global context\n");
		return PTR_ERR(ctx);
	}
	/*
	 * For easy recognisablity, we want the kernel context to be 0 and then
	 * all user contexts will have non-zero hw_id. Kernel contexts are
	 * permanently pinned, so that we never suffer a stall and can
	 * use them from any allocation context (e.g. for evicting other
	 * contexts and from inside the shrinker).
	 */
	GEM_BUG_ON(ctx->hw_id);
	GEM_BUG_ON(!atomic_read(&ctx->hw_id_pin_count));
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
	GEM_BUG_ON(!list_empty(&i915->contexts.hw_id_list));
	ida_destroy(&i915->contexts.hw_ida);
}

static int context_idr_cleanup(int id, void *p, void *data)
{
	context_close(p);
	return 0;
}

static int vm_idr_cleanup(int id, void *p, void *data)
{
	i915_ppgtt_put(p);
	return 0;
}

static int gem_context_register(struct i915_gem_context *ctx,
				struct drm_i915_file_private *fpriv)
{
	int ret;

	ctx->file_priv = fpriv;
	if (ctx->ppgtt)
		ctx->ppgtt->vm.file = fpriv;

	ctx->pid = get_task_pid(current, PIDTYPE_PID);
	ctx->name = kasprintf(GFP_KERNEL, "%s[%d]",
			      current->comm, pid_nr(ctx->pid));
	if (!ctx->name) {
		ret = -ENOMEM;
		goto err_pid;
	}

	/* And finally expose ourselves to userspace via the idr */
	mutex_lock(&fpriv->context_idr_lock);
	ret = idr_alloc(&fpriv->context_idr, ctx, 0, 0, GFP_KERNEL);
	mutex_unlock(&fpriv->context_idr_lock);
	if (ret >= 0)
		goto out;

	kfree(fetch_and_zero(&ctx->name));
err_pid:
	put_pid(fetch_and_zero(&ctx->pid));
out:
	return ret;
}

int i915_gem_context_open(struct drm_i915_private *i915,
			  struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_gem_context *ctx;
	int err;

	mutex_init(&file_priv->context_idr_lock);
	mutex_init(&file_priv->vm_idr_lock);

	idr_init(&file_priv->context_idr);
	idr_init_base(&file_priv->vm_idr, 1);

	mutex_lock(&i915->drm.struct_mutex);
	ctx = i915_gem_create_context(i915, 0);
	mutex_unlock(&i915->drm.struct_mutex);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto err;
	}

	err = gem_context_register(ctx, file_priv);
	if (err < 0)
		goto err_ctx;

	GEM_BUG_ON(i915_gem_context_is_kernel(ctx));
	GEM_BUG_ON(err > 0);

	return 0;

err_ctx:
	mutex_lock(&i915->drm.struct_mutex);
	context_close(ctx);
	mutex_unlock(&i915->drm.struct_mutex);
err:
	idr_destroy(&file_priv->vm_idr);
	idr_destroy(&file_priv->context_idr);
	mutex_destroy(&file_priv->vm_idr_lock);
	mutex_destroy(&file_priv->context_idr_lock);
	return err;
}

void i915_gem_context_close(struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;

	lockdep_assert_held(&file_priv->dev_priv->drm.struct_mutex);

	idr_for_each(&file_priv->context_idr, context_idr_cleanup, NULL);
	idr_destroy(&file_priv->context_idr);
	mutex_destroy(&file_priv->context_idr_lock);

	idr_for_each(&file_priv->vm_idr, vm_idr_cleanup, NULL);
	idr_destroy(&file_priv->vm_idr);
	mutex_destroy(&file_priv->vm_idr_lock);
}

int i915_gem_vm_create_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct drm_i915_gem_vm_control *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_hw_ppgtt *ppgtt;
	int err;

	if (!HAS_FULL_PPGTT(i915))
		return -ENODEV;

	if (args->flags)
		return -EINVAL;

	ppgtt = i915_ppgtt_create(i915);
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

	err = idr_alloc(&file_priv->vm_idr, ppgtt, 0, 0, GFP_KERNEL);
	if (err < 0)
		goto err_unlock;

	GEM_BUG_ON(err == 0); /* reserved for default/unassigned ppgtt */
	ppgtt->user_handle = err;

	mutex_unlock(&file_priv->vm_idr_lock);

	args->vm_id = err;
	return 0;

err_unlock:
	mutex_unlock(&file_priv->vm_idr_lock);
err_put:
	i915_ppgtt_put(ppgtt);
	return err;
}

int i915_gem_vm_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct drm_i915_gem_vm_control *args = data;
	struct i915_hw_ppgtt *ppgtt;
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

	ppgtt = idr_remove(&file_priv->vm_idr, id);
	if (ppgtt) {
		GEM_BUG_ON(ppgtt->user_handle != id);
		ppgtt->user_handle = 0;
	}

	mutex_unlock(&file_priv->vm_idr_lock);
	if (!ppgtt)
		return -ENOENT;

	i915_ppgtt_put(ppgtt);
	return 0;
}

static struct i915_request *
last_request_on_engine(struct i915_timeline *timeline,
		       struct intel_engine_cs *engine)
{
	struct i915_request *rq;

	GEM_BUG_ON(timeline == &engine->timeline);

	rq = i915_active_request_raw(&timeline->last_request,
				     &engine->i915->drm.struct_mutex);
	if (rq && rq->engine->mask & engine->mask) {
		GEM_TRACE("last request on engine %s: %llx:%llu\n",
			  engine->name, rq->fence.context, rq->fence.seqno);
		GEM_BUG_ON(rq->timeline != timeline);
		return rq;
	}

	return NULL;
}

struct context_barrier_task {
	struct i915_active base;
	void (*task)(void *data);
	void *data;
};

static void cb_retire(struct i915_active *base)
{
	struct context_barrier_task *cb = container_of(base, typeof(*cb), base);

	if (cb->task)
		cb->task(cb->data);

	i915_active_fini(&cb->base);
	kfree(cb);
}

I915_SELFTEST_DECLARE(static unsigned long context_barrier_inject_fault);
static int context_barrier_task(struct i915_gem_context *ctx,
				unsigned long engines,
				int (*emit)(struct i915_request *rq, void *data),
				void (*task)(void *data),
				void *data)
{
	struct drm_i915_private *i915 = ctx->i915;
	struct context_barrier_task *cb;
	struct intel_context *ce, *next;
	intel_wakeref_t wakeref;
	int err = 0;

	lockdep_assert_held(&i915->drm.struct_mutex);
	GEM_BUG_ON(!task);

	cb = kmalloc(sizeof(*cb), GFP_KERNEL);
	if (!cb)
		return -ENOMEM;

	i915_active_init(i915, &cb->base, cb_retire);
	i915_active_acquire(&cb->base);

	wakeref = intel_runtime_pm_get(i915);
	rbtree_postorder_for_each_entry_safe(ce, next, &ctx->hw_contexts, node) {
		struct intel_engine_cs *engine = ce->engine;
		struct i915_request *rq;

		if (!(engine->mask & engines))
			continue;

		if (I915_SELFTEST_ONLY(context_barrier_inject_fault &
				       engine->mask)) {
			err = -ENXIO;
			break;
		}

		rq = i915_request_alloc(engine, ctx);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			break;
		}

		err = 0;
		if (emit)
			err = emit(rq, data);
		if (err == 0)
			err = i915_active_ref(&cb->base, rq->fence.context, rq);

		i915_request_add(rq);
		if (err)
			break;
	}
	intel_runtime_pm_put(i915, wakeref);

	cb->task = err ? NULL : task; /* caller needs to unwind instead */
	cb->data = data;

	i915_active_release(&cb->base);

	return err;
}

int i915_gem_switch_to_kernel_context(struct drm_i915_private *i915,
				      unsigned long mask)
{
	struct intel_engine_cs *engine;

	GEM_TRACE("awake?=%s\n", yesno(i915->gt.awake));

	lockdep_assert_held(&i915->drm.struct_mutex);
	GEM_BUG_ON(!i915->kernel_context);

	/* Inoperable, so presume the GPU is safely pointing into the void! */
	if (i915_terminally_wedged(i915))
		return 0;

	for_each_engine_masked(engine, i915, mask, mask) {
		struct intel_ring *ring;
		struct i915_request *rq;

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

			GEM_TRACE("add barrier on %s for %llx:%lld\n",
				  engine->name,
				  prev->fence.context,
				  prev->fence.seqno);
			i915_sw_fence_await_sw_fence_gfp(&rq->submit,
							 &prev->submit,
							 I915_FENCE_GFP);
		}

		i915_request_add(rq);
	}

	return 0;
}

static int get_ppgtt(struct drm_i915_file_private *file_priv,
		     struct i915_gem_context *ctx,
		     struct drm_i915_gem_context_param *args)
{
	struct i915_hw_ppgtt *ppgtt;
	int ret;

	return -EINVAL; /* nothing to see here; please move along */

	if (!ctx->ppgtt)
		return -ENODEV;

	/* XXX rcu acquire? */
	ret = mutex_lock_interruptible(&ctx->i915->drm.struct_mutex);
	if (ret)
		return ret;

	ppgtt = i915_ppgtt_get(ctx->ppgtt);
	mutex_unlock(&ctx->i915->drm.struct_mutex);

	ret = mutex_lock_interruptible(&file_priv->vm_idr_lock);
	if (ret)
		goto err_put;

	if (!ppgtt->user_handle) {
		ret = idr_alloc(&file_priv->vm_idr, ppgtt, 0, 0, GFP_KERNEL);
		GEM_BUG_ON(!ret);
		if (ret < 0)
			goto err_unlock;

		ppgtt->user_handle = ret;
		i915_ppgtt_get(ppgtt);
	}

	args->size = 0;
	args->value = ppgtt->user_handle;

	ret = 0;
err_unlock:
	mutex_unlock(&file_priv->vm_idr_lock);
err_put:
	i915_ppgtt_put(ppgtt);
	return ret;
}

static void set_ppgtt_barrier(void *data)
{
	struct i915_hw_ppgtt *old = data;

	if (INTEL_GEN(old->vm.i915) < 8)
		gen6_ppgtt_unpin_all(old);

	i915_ppgtt_put(old);
}

static int emit_ppgtt_update(struct i915_request *rq, void *data)
{
	struct i915_hw_ppgtt *ppgtt = rq->gem_context->ppgtt;
	struct intel_engine_cs *engine = rq->engine;
	u32 *cs;
	int i;

	if (i915_vm_is_4lvl(&ppgtt->vm)) {
		const dma_addr_t pd_daddr = px_dma(&ppgtt->pml4);

		cs = intel_ring_begin(rq, 6);
		if (IS_ERR(cs))
			return PTR_ERR(cs);

		*cs++ = MI_LOAD_REGISTER_IMM(2);

		*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_UDW(engine, 0));
		*cs++ = upper_32_bits(pd_daddr);
		*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_LDW(engine, 0));
		*cs++ = lower_32_bits(pd_daddr);

		*cs++ = MI_NOOP;
		intel_ring_advance(rq, cs);
	} else if (HAS_LOGICAL_RING_CONTEXTS(engine->i915)) {
		cs = intel_ring_begin(rq, 4 * GEN8_3LVL_PDPES + 2);
		if (IS_ERR(cs))
			return PTR_ERR(cs);

		*cs++ = MI_LOAD_REGISTER_IMM(2 * GEN8_3LVL_PDPES);
		for (i = GEN8_3LVL_PDPES; i--; ) {
			const dma_addr_t pd_daddr = i915_page_dir_dma_addr(ppgtt, i);

			*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_UDW(engine, i));
			*cs++ = upper_32_bits(pd_daddr);
			*cs++ = i915_mmio_reg_offset(GEN8_RING_PDP_LDW(engine, i));
			*cs++ = lower_32_bits(pd_daddr);
		}
		*cs++ = MI_NOOP;
		intel_ring_advance(rq, cs);
	} else {
		/* ppGTT is not part of the legacy context image */
		gen6_ppgtt_pin(ppgtt);
	}

	return 0;
}

static int set_ppgtt(struct drm_i915_file_private *file_priv,
		     struct i915_gem_context *ctx,
		     struct drm_i915_gem_context_param *args)
{
	struct i915_hw_ppgtt *ppgtt, *old;
	int err;

	return -EINVAL; /* nothing to see here; please move along */

	if (args->size)
		return -EINVAL;

	if (!ctx->ppgtt)
		return -ENODEV;

	if (upper_32_bits(args->value))
		return -ENOENT;

	err = mutex_lock_interruptible(&file_priv->vm_idr_lock);
	if (err)
		return err;

	ppgtt = idr_find(&file_priv->vm_idr, args->value);
	if (ppgtt) {
		GEM_BUG_ON(ppgtt->user_handle != args->value);
		i915_ppgtt_get(ppgtt);
	}
	mutex_unlock(&file_priv->vm_idr_lock);
	if (!ppgtt)
		return -ENOENT;

	err = mutex_lock_interruptible(&ctx->i915->drm.struct_mutex);
	if (err)
		goto out;

	if (ppgtt == ctx->ppgtt)
		goto unlock;

	/* Teardown the existing obj:vma cache, it will have to be rebuilt. */
	lut_close(ctx);

	old = __set_ppgtt(ctx, ppgtt);

	/*
	 * We need to flush any requests using the current ppgtt before
	 * we release it as the requests do not hold a reference themselves,
	 * only indirectly through the context.
	 */
	err = context_barrier_task(ctx, ALL_ENGINES,
				   emit_ppgtt_update,
				   set_ppgtt_barrier,
				   old);
	if (err) {
		ctx->ppgtt = old;
		ctx->desc_template = default_desc_template(ctx->i915, old);
		i915_ppgtt_put(ppgtt);
	}

unlock:
	mutex_unlock(&ctx->i915->drm.struct_mutex);

out:
	i915_ppgtt_put(ppgtt);
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
		 (CTX_R_PWR_CLK_STATE + 1) * 4;

	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = lower_32_bits(offset);
	*cs++ = upper_32_bits(offset);
	*cs++ = gen8_make_rpcs(rq->i915, &sseu);

	intel_ring_advance(rq, cs);

	return 0;
}

static int
gen8_modify_rpcs(struct intel_context *ce, struct intel_sseu sseu)
{
	struct drm_i915_private *i915 = ce->engine->i915;
	struct i915_request *rq, *prev;
	intel_wakeref_t wakeref;
	int ret;

	lockdep_assert_held(&ce->pin_mutex);

	/*
	 * If the context is not idle, we have to submit an ordered request to
	 * modify its context image via the kernel context (writing to our own
	 * image, or into the registers directory, does not stick). Pristine
	 * and idle contexts will be configured on pinning.
	 */
	if (!intel_context_is_pinned(ce))
		return 0;

	/* Submitting requests etc needs the hw awake. */
	wakeref = intel_runtime_pm_get(i915);

	rq = i915_request_alloc(ce->engine, i915->kernel_context);
	if (IS_ERR(rq)) {
		ret = PTR_ERR(rq);
		goto out_put;
	}

	/* Queue this switch after all other activity by this context. */
	prev = i915_active_request_raw(&ce->ring->timeline->last_request,
				       &i915->drm.struct_mutex);
	if (prev && !i915_request_completed(prev)) {
		ret = i915_request_await_dma_fence(rq, &prev->fence);
		if (ret < 0)
			goto out_add;
	}

	/* Order all following requests to be after. */
	ret = i915_timeline_set_barrier(ce->ring->timeline, rq);
	if (ret)
		goto out_add;

	ret = gen8_emit_rpcs_config(rq, ce, sseu);
	if (ret)
		goto out_add;

	/*
	 * Guarantee context image and the timeline remains pinned until the
	 * modifying request is retired by setting the ce activity tracker.
	 *
	 * But we only need to take one pin on the account of it. Or in other
	 * words transfer the pinned ce object to tracked active request.
	 */
	if (!i915_active_request_isset(&ce->active_tracker))
		__intel_context_pin(ce);
	__i915_active_request_set(&ce->active_tracker, rq);

out_add:
	i915_request_add(rq);
out_put:
	intel_runtime_pm_put(i915, wakeref);

	return ret;
}

static int
__i915_gem_context_reconfigure_sseu(struct i915_gem_context *ctx,
				    struct intel_engine_cs *engine,
				    struct intel_sseu sseu)
{
	struct intel_context *ce;
	int ret = 0;

	GEM_BUG_ON(INTEL_GEN(ctx->i915) < 8);
	GEM_BUG_ON(engine->id != RCS0);

	ce = intel_context_pin_lock(ctx, engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	/* Nothing to do if unmodified. */
	if (!memcmp(&ce->sseu, &sseu, sizeof(sseu)))
		goto unlock;

	ret = gen8_modify_rpcs(ce, sseu);
	if (!ret)
		ce->sseu = sseu;

unlock:
	intel_context_pin_unlock(ce);
	return ret;
}

static int
i915_gem_context_reconfigure_sseu(struct i915_gem_context *ctx,
				  struct intel_engine_cs *engine,
				  struct intel_sseu sseu)
{
	int ret;

	ret = mutex_lock_interruptible(&ctx->i915->drm.struct_mutex);
	if (ret)
		return ret;

	ret = __i915_gem_context_reconfigure_sseu(ctx, engine, sseu);

	mutex_unlock(&ctx->i915->drm.struct_mutex);

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
	struct intel_engine_cs *engine;
	struct intel_sseu sseu;
	int ret;

	if (args->size < sizeof(user_sseu))
		return -EINVAL;

	if (!IS_GEN(i915, 11))
		return -ENODEV;

	if (copy_from_user(&user_sseu, u64_to_user_ptr(args->value),
			   sizeof(user_sseu)))
		return -EFAULT;

	if (user_sseu.flags || user_sseu.rsvd)
		return -EINVAL;

	engine = intel_engine_lookup_user(i915,
					  user_sseu.engine_class,
					  user_sseu.engine_instance);
	if (!engine)
		return -EINVAL;

	/* Only render engine supports RPCS configuration. */
	if (engine->class != RENDER_CLASS)
		return -ENODEV;

	ret = user_to_context_sseu(i915, &user_sseu, &sseu);
	if (ret)
		return ret;

	ret = i915_gem_context_reconfigure_sseu(ctx, engine, sseu);
	if (ret)
		return ret;

	args->size = sizeof(user_sseu);

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
		{
			s64 priority = args->value;

			if (args->size)
				ret = -EINVAL;
			else if (!(ctx->i915->caps.scheduler & I915_SCHEDULER_CAP_PRIORITY))
				ret = -ENODEV;
			else if (priority > I915_CONTEXT_MAX_USER_PRIORITY ||
				 priority < I915_CONTEXT_MIN_USER_PRIORITY)
				ret = -EINVAL;
			else if (priority > I915_CONTEXT_DEFAULT_PRIORITY &&
				 !capable(CAP_SYS_NICE))
				ret = -EPERM;
			else
				ctx->sched.priority =
					I915_USER_PRIORITY(priority);
		}
		break;

	case I915_CONTEXT_PARAM_SSEU:
		ret = set_sseu(ctx, args);
		break;

	case I915_CONTEXT_PARAM_VM:
		ret = set_ppgtt(fpriv, ctx, args);
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

static const i915_user_extension_fn create_extensions[] = {
	[I915_CONTEXT_CREATE_EXT_SETPARAM] = create_setparam,
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

	if (!DRIVER_CAPS(i915)->has_logical_contexts)
		return -ENODEV;

	if (args->flags & I915_CONTEXT_CREATE_FLAGS_UNKNOWN)
		return -EINVAL;

	ret = i915_terminally_wedged(i915);
	if (ret)
		return ret;

	ext_data.fpriv = file->driver_priv;
	if (client_is_banned(ext_data.fpriv)) {
		DRM_DEBUG("client %s[%d] banned from creating ctx\n",
			  current->comm,
			  pid_nr(get_task_pid(current, PIDTYPE_PID)));
		return -EIO;
	}

	ret = i915_mutex_lock_interruptible(dev);
	if (ret)
		return ret;

	ext_data.ctx = i915_gem_create_context(i915, args->flags);
	mutex_unlock(&dev->struct_mutex);
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

	ret = gem_context_register(ext_data.ctx, ext_data.fpriv);
	if (ret < 0)
		goto err_ctx;

	args->ctx_id = ret;
	DRM_DEBUG("HW context %d created\n", args->ctx_id);

	return 0;

err_ctx:
	mutex_lock(&dev->struct_mutex);
	context_close(ext_data.ctx);
	mutex_unlock(&dev->struct_mutex);
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

	if (mutex_lock_interruptible(&file_priv->context_idr_lock))
		return -EINTR;

	ctx = idr_remove(&file_priv->context_idr, args->ctx_id);
	mutex_unlock(&file_priv->context_idr_lock);
	if (!ctx)
		return -ENOENT;

	mutex_lock(&dev->struct_mutex);
	context_close(ctx);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int get_sseu(struct i915_gem_context *ctx,
		    struct drm_i915_gem_context_param *args)
{
	struct drm_i915_gem_context_param_sseu user_sseu;
	struct intel_engine_cs *engine;
	struct intel_context *ce;

	if (args->size == 0)
		goto out;
	else if (args->size < sizeof(user_sseu))
		return -EINVAL;

	if (copy_from_user(&user_sseu, u64_to_user_ptr(args->value),
			   sizeof(user_sseu)))
		return -EFAULT;

	if (user_sseu.flags || user_sseu.rsvd)
		return -EINVAL;

	engine = intel_engine_lookup_user(ctx->i915,
					  user_sseu.engine_class,
					  user_sseu.engine_instance);
	if (!engine)
		return -EINVAL;

	ce = intel_context_pin_lock(ctx, engine); /* serialises with set_sseu */
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	user_sseu.slice_mask = ce->sseu.slice_mask;
	user_sseu.subslice_mask = ce->sseu.subslice_mask;
	user_sseu.min_eus_per_subslice = ce->sseu.min_eus_per_subslice;
	user_sseu.max_eus_per_subslice = ce->sseu.max_eus_per_subslice;

	intel_context_pin_unlock(ce);

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
		if (ctx->ppgtt)
			args->value = ctx->ppgtt->vm.total;
		else if (to_i915(dev)->mm.aliasing_ppgtt)
			args->value = to_i915(dev)->mm.aliasing_ppgtt->vm.total;
		else
			args->value = to_i915(dev)->ggtt.vm.total;
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

int __i915_gem_context_pin_hw_id(struct i915_gem_context *ctx)
{
	struct drm_i915_private *i915 = ctx->i915;
	int err = 0;

	mutex_lock(&i915->contexts.mutex);

	GEM_BUG_ON(i915_gem_context_is_closed(ctx));

	if (list_empty(&ctx->hw_id_link)) {
		GEM_BUG_ON(atomic_read(&ctx->hw_id_pin_count));

		err = assign_hw_id(i915, &ctx->hw_id);
		if (err)
			goto out_unlock;

		list_add_tail(&ctx->hw_id_link, &i915->contexts.hw_id_list);
	}

	GEM_BUG_ON(atomic_read(&ctx->hw_id_pin_count) == ~0u);
	atomic_inc(&ctx->hw_id_pin_count);

out_unlock:
	mutex_unlock(&i915->contexts.mutex);
	return err;
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
