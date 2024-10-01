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

#include <linux/highmem.h>
#include <linux/log2.h>
#include <linux/nospec.h>

#include <drm/drm_cache.h>
#include <drm/drm_syncobj.h>

#include "gt/gen6_ppgtt.h"
#include "gt/intel_context.h"
#include "gt/intel_context_param.h"
#include "gt/intel_engine_heartbeat.h"
#include "gt/intel_engine_user.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_ring.h"

#include "pxp/intel_pxp.h"

#include "i915_file_private.h"
#include "i915_gem_context.h"
#include "i915_trace.h"
#include "i915_user_extensions.h"

#define ALL_L3_SLICES(dev) (1 << NUM_L3_SLICES(dev)) - 1

static struct kmem_cache *slab_luts;

struct i915_lut_handle *i915_lut_handle_alloc(void)
{
	return kmem_cache_alloc(slab_luts, GFP_KERNEL);
}

void i915_lut_handle_free(struct i915_lut_handle *lut)
{
	return kmem_cache_free(slab_luts, lut);
}

static void lut_close(struct i915_gem_context *ctx)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	mutex_lock(&ctx->lut_mutex);
	rcu_read_lock();
	radix_tree_for_each_slot(slot, &ctx->handles_vma, &iter, 0) {
		struct i915_vma *vma = rcu_dereference_raw(*slot);
		struct drm_i915_gem_object *obj = vma->obj;
		struct i915_lut_handle *lut;

		if (!kref_get_unless_zero(&obj->base.refcount))
			continue;

		spin_lock(&obj->lut_lock);
		list_for_each_entry(lut, &obj->lut_list, obj_link) {
			if (lut->ctx != ctx)
				continue;

			if (lut->handle != iter.index)
				continue;

			list_del(&lut->obj_link);
			break;
		}
		spin_unlock(&obj->lut_lock);

		if (&lut->obj_link != &obj->lut_list) {
			i915_lut_handle_free(lut);
			radix_tree_iter_delete(&ctx->handles_vma, &iter, slot);
			i915_vma_close(vma);
			i915_gem_object_put(obj);
		}

		i915_gem_object_put(obj);
	}
	rcu_read_unlock();
	mutex_unlock(&ctx->lut_mutex);
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

static int validate_priority(struct drm_i915_private *i915,
			     const struct drm_i915_gem_context_param *args)
{
	s64 priority = args->value;

	if (args->size)
		return -EINVAL;

	if (!(i915->caps.scheduler & I915_SCHEDULER_CAP_PRIORITY))
		return -ENODEV;

	if (priority > I915_CONTEXT_MAX_USER_PRIORITY ||
	    priority < I915_CONTEXT_MIN_USER_PRIORITY)
		return -EINVAL;

	if (priority > I915_CONTEXT_DEFAULT_PRIORITY &&
	    !capable(CAP_SYS_NICE))
		return -EPERM;

	return 0;
}

static void proto_context_close(struct drm_i915_private *i915,
				struct i915_gem_proto_context *pc)
{
	int i;

	if (pc->pxp_wakeref)
		intel_runtime_pm_put(&i915->runtime_pm, pc->pxp_wakeref);
	if (pc->vm)
		i915_vm_put(pc->vm);
	if (pc->user_engines) {
		for (i = 0; i < pc->num_user_engines; i++)
			kfree(pc->user_engines[i].siblings);
		kfree(pc->user_engines);
	}
	kfree(pc);
}

static int proto_context_set_persistence(struct drm_i915_private *i915,
					 struct i915_gem_proto_context *pc,
					 bool persist)
{
	if (persist) {
		/*
		 * Only contexts that are short-lived [that will expire or be
		 * reset] are allowed to survive past termination. We require
		 * hangcheck to ensure that the persistent requests are healthy.
		 */
		if (!i915->params.enable_hangcheck)
			return -EINVAL;

		pc->user_flags |= BIT(UCONTEXT_PERSISTENCE);
	} else {
		/* To cancel a context we use "preempt-to-idle" */
		if (!(i915->caps.scheduler & I915_SCHEDULER_CAP_PREEMPTION))
			return -ENODEV;

		/*
		 * If the cancel fails, we then need to reset, cleanly!
		 *
		 * If the per-engine reset fails, all hope is lost! We resort
		 * to a full GPU reset in that unlikely case, but realistically
		 * if the engine could not reset, the full reset does not fare
		 * much better. The damage has been done.
		 *
		 * However, if we cannot reset an engine by itself, we cannot
		 * cleanup a hanging persistent context without causing
		 * colateral damage, and we should not pretend we can by
		 * exposing the interface.
		 */
		if (!intel_has_reset_engine(to_gt(i915)))
			return -ENODEV;

		pc->user_flags &= ~BIT(UCONTEXT_PERSISTENCE);
	}

	return 0;
}

static int proto_context_set_protected(struct drm_i915_private *i915,
				       struct i915_gem_proto_context *pc,
				       bool protected)
{
	int ret = 0;

	if (!protected) {
		pc->uses_protected_content = false;
	} else if (!intel_pxp_is_enabled(&to_gt(i915)->pxp)) {
		ret = -ENODEV;
	} else if ((pc->user_flags & BIT(UCONTEXT_RECOVERABLE)) ||
		   !(pc->user_flags & BIT(UCONTEXT_BANNABLE))) {
		ret = -EPERM;
	} else {
		pc->uses_protected_content = true;

		/*
		 * protected context usage requires the PXP session to be up,
		 * which in turn requires the device to be active.
		 */
		pc->pxp_wakeref = intel_runtime_pm_get(&i915->runtime_pm);

		if (!intel_pxp_is_active(&to_gt(i915)->pxp))
			ret = intel_pxp_start(&to_gt(i915)->pxp);
	}

	return ret;
}

static struct i915_gem_proto_context *
proto_context_create(struct drm_i915_private *i915, unsigned int flags)
{
	struct i915_gem_proto_context *pc, *err;

	pc = kzalloc(sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return ERR_PTR(-ENOMEM);

	pc->num_user_engines = -1;
	pc->user_engines = NULL;
	pc->user_flags = BIT(UCONTEXT_BANNABLE) |
			 BIT(UCONTEXT_RECOVERABLE);
	if (i915->params.enable_hangcheck)
		pc->user_flags |= BIT(UCONTEXT_PERSISTENCE);
	pc->sched.priority = I915_PRIORITY_NORMAL;

	if (flags & I915_CONTEXT_CREATE_FLAGS_SINGLE_TIMELINE) {
		if (!HAS_EXECLISTS(i915)) {
			err = ERR_PTR(-EINVAL);
			goto proto_close;
		}
		pc->single_timeline = true;
	}

	return pc;

proto_close:
	proto_context_close(i915, pc);
	return err;
}

static int proto_context_register_locked(struct drm_i915_file_private *fpriv,
					 struct i915_gem_proto_context *pc,
					 u32 *id)
{
	int ret;
	void *old;

	lockdep_assert_held(&fpriv->proto_context_lock);

	ret = xa_alloc(&fpriv->context_xa, id, NULL, xa_limit_32b, GFP_KERNEL);
	if (ret)
		return ret;

	old = xa_store(&fpriv->proto_context_xa, *id, pc, GFP_KERNEL);
	if (xa_is_err(old)) {
		xa_erase(&fpriv->context_xa, *id);
		return xa_err(old);
	}
	WARN_ON(old);

	return 0;
}

static int proto_context_register(struct drm_i915_file_private *fpriv,
				  struct i915_gem_proto_context *pc,
				  u32 *id)
{
	int ret;

	mutex_lock(&fpriv->proto_context_lock);
	ret = proto_context_register_locked(fpriv, pc, id);
	mutex_unlock(&fpriv->proto_context_lock);

	return ret;
}

static struct i915_address_space *
i915_gem_vm_lookup(struct drm_i915_file_private *file_priv, u32 id)
{
	struct i915_address_space *vm;

	xa_lock(&file_priv->vm_xa);
	vm = xa_load(&file_priv->vm_xa, id);
	if (vm)
		kref_get(&vm->ref);
	xa_unlock(&file_priv->vm_xa);

	return vm;
}

static int set_proto_ctx_vm(struct drm_i915_file_private *fpriv,
			    struct i915_gem_proto_context *pc,
			    const struct drm_i915_gem_context_param *args)
{
	struct drm_i915_private *i915 = fpriv->dev_priv;
	struct i915_address_space *vm;

	if (args->size)
		return -EINVAL;

	if (!HAS_FULL_PPGTT(i915))
		return -ENODEV;

	if (upper_32_bits(args->value))
		return -ENOENT;

	vm = i915_gem_vm_lookup(fpriv, args->value);
	if (!vm)
		return -ENOENT;

	if (pc->vm)
		i915_vm_put(pc->vm);
	pc->vm = vm;

	return 0;
}

struct set_proto_ctx_engines {
	struct drm_i915_private *i915;
	unsigned num_engines;
	struct i915_gem_proto_engine *engines;
};

static int
set_proto_ctx_engines_balance(struct i915_user_extension __user *base,
			      void *data)
{
	struct i915_context_engines_load_balance __user *ext =
		container_of_user(base, typeof(*ext), base);
	const struct set_proto_ctx_engines *set = data;
	struct drm_i915_private *i915 = set->i915;
	struct intel_engine_cs **siblings;
	u16 num_siblings, idx;
	unsigned int n;
	int err;

	if (!HAS_EXECLISTS(i915))
		return -ENODEV;

	if (get_user(idx, &ext->engine_index))
		return -EFAULT;

	if (idx >= set->num_engines) {
		drm_dbg(&i915->drm, "Invalid placement value, %d >= %d\n",
			idx, set->num_engines);
		return -EINVAL;
	}

	idx = array_index_nospec(idx, set->num_engines);
	if (set->engines[idx].type != I915_GEM_ENGINE_TYPE_INVALID) {
		drm_dbg(&i915->drm,
			"Invalid placement[%d], already occupied\n", idx);
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

	if (num_siblings == 0)
		return 0;

	siblings = kmalloc_array(num_siblings, sizeof(*siblings), GFP_KERNEL);
	if (!siblings)
		return -ENOMEM;

	for (n = 0; n < num_siblings; n++) {
		struct i915_engine_class_instance ci;

		if (copy_from_user(&ci, &ext->engines[n], sizeof(ci))) {
			err = -EFAULT;
			goto err_siblings;
		}

		siblings[n] = intel_engine_lookup_user(i915,
						       ci.engine_class,
						       ci.engine_instance);
		if (!siblings[n]) {
			drm_dbg(&i915->drm,
				"Invalid sibling[%d]: { class:%d, inst:%d }\n",
				n, ci.engine_class, ci.engine_instance);
			err = -EINVAL;
			goto err_siblings;
		}
	}

	if (num_siblings == 1) {
		set->engines[idx].type = I915_GEM_ENGINE_TYPE_PHYSICAL;
		set->engines[idx].engine = siblings[0];
		kfree(siblings);
	} else {
		set->engines[idx].type = I915_GEM_ENGINE_TYPE_BALANCED;
		set->engines[idx].num_siblings = num_siblings;
		set->engines[idx].siblings = siblings;
	}

	return 0;

err_siblings:
	kfree(siblings);

	return err;
}

static int
set_proto_ctx_engines_bond(struct i915_user_extension __user *base, void *data)
{
	struct i915_context_engines_bond __user *ext =
		container_of_user(base, typeof(*ext), base);
	const struct set_proto_ctx_engines *set = data;
	struct drm_i915_private *i915 = set->i915;
	struct i915_engine_class_instance ci;
	struct intel_engine_cs *master;
	u16 idx, num_bonds;
	int err, n;

	if (GRAPHICS_VER(i915) >= 12 && !IS_TIGERLAKE(i915) &&
	    !IS_ROCKETLAKE(i915) && !IS_ALDERLAKE_S(i915)) {
		drm_dbg(&i915->drm,
			"Bonding not supported on this platform\n");
		return -ENODEV;
	}

	if (get_user(idx, &ext->virtual_index))
		return -EFAULT;

	if (idx >= set->num_engines) {
		drm_dbg(&i915->drm,
			"Invalid index for virtual engine: %d >= %d\n",
			idx, set->num_engines);
		return -EINVAL;
	}

	idx = array_index_nospec(idx, set->num_engines);
	if (set->engines[idx].type == I915_GEM_ENGINE_TYPE_INVALID) {
		drm_dbg(&i915->drm, "Invalid engine at %d\n", idx);
		return -EINVAL;
	}

	if (set->engines[idx].type != I915_GEM_ENGINE_TYPE_PHYSICAL) {
		drm_dbg(&i915->drm,
			"Bonding with virtual engines not allowed\n");
		return -EINVAL;
	}

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

	master = intel_engine_lookup_user(i915,
					  ci.engine_class,
					  ci.engine_instance);
	if (!master) {
		drm_dbg(&i915->drm,
			"Unrecognised master engine: { class:%u, instance:%u }\n",
			ci.engine_class, ci.engine_instance);
		return -EINVAL;
	}

	if (intel_engine_uses_guc(master)) {
		DRM_DEBUG("bonding extension not supported with GuC submission");
		return -ENODEV;
	}

	if (get_user(num_bonds, &ext->num_bonds))
		return -EFAULT;

	for (n = 0; n < num_bonds; n++) {
		struct intel_engine_cs *bond;

		if (copy_from_user(&ci, &ext->engines[n], sizeof(ci)))
			return -EFAULT;

		bond = intel_engine_lookup_user(i915,
						ci.engine_class,
						ci.engine_instance);
		if (!bond) {
			drm_dbg(&i915->drm,
				"Unrecognised engine[%d] for bonding: { class:%d, instance: %d }\n",
				n, ci.engine_class, ci.engine_instance);
			return -EINVAL;
		}
	}

	return 0;
}

static int
set_proto_ctx_engines_parallel_submit(struct i915_user_extension __user *base,
				      void *data)
{
	struct i915_context_engines_parallel_submit __user *ext =
		container_of_user(base, typeof(*ext), base);
	const struct set_proto_ctx_engines *set = data;
	struct drm_i915_private *i915 = set->i915;
	struct i915_engine_class_instance prev_engine;
	u64 flags;
	int err = 0, n, i, j;
	u16 slot, width, num_siblings;
	struct intel_engine_cs **siblings = NULL;
	intel_engine_mask_t prev_mask;

	if (get_user(slot, &ext->engine_index))
		return -EFAULT;

	if (get_user(width, &ext->width))
		return -EFAULT;

	if (get_user(num_siblings, &ext->num_siblings))
		return -EFAULT;

	if (!intel_uc_uses_guc_submission(&to_gt(i915)->uc) &&
	    num_siblings != 1) {
		drm_dbg(&i915->drm, "Only 1 sibling (%d) supported in non-GuC mode\n",
			num_siblings);
		return -EINVAL;
	}

	if (slot >= set->num_engines) {
		drm_dbg(&i915->drm, "Invalid placement value, %d >= %d\n",
			slot, set->num_engines);
		return -EINVAL;
	}

	if (set->engines[slot].type != I915_GEM_ENGINE_TYPE_INVALID) {
		drm_dbg(&i915->drm,
			"Invalid placement[%d], already occupied\n", slot);
		return -EINVAL;
	}

	if (get_user(flags, &ext->flags))
		return -EFAULT;

	if (flags) {
		drm_dbg(&i915->drm, "Unknown flags 0x%02llx", flags);
		return -EINVAL;
	}

	for (n = 0; n < ARRAY_SIZE(ext->mbz64); n++) {
		err = check_user_mbz(&ext->mbz64[n]);
		if (err)
			return err;
	}

	if (width < 2) {
		drm_dbg(&i915->drm, "Width (%d) < 2\n", width);
		return -EINVAL;
	}

	if (num_siblings < 1) {
		drm_dbg(&i915->drm, "Number siblings (%d) < 1\n",
			num_siblings);
		return -EINVAL;
	}

	siblings = kmalloc_array(num_siblings * width,
				 sizeof(*siblings),
				 GFP_KERNEL);
	if (!siblings)
		return -ENOMEM;

	/* Create contexts / engines */
	for (i = 0; i < width; ++i) {
		intel_engine_mask_t current_mask = 0;

		for (j = 0; j < num_siblings; ++j) {
			struct i915_engine_class_instance ci;

			n = i * num_siblings + j;
			if (copy_from_user(&ci, &ext->engines[n], sizeof(ci))) {
				err = -EFAULT;
				goto out_err;
			}

			siblings[n] =
				intel_engine_lookup_user(i915, ci.engine_class,
							 ci.engine_instance);
			if (!siblings[n]) {
				drm_dbg(&i915->drm,
					"Invalid sibling[%d]: { class:%d, inst:%d }\n",
					n, ci.engine_class, ci.engine_instance);
				err = -EINVAL;
				goto out_err;
			}

			/*
			 * We don't support breadcrumb handshake on these
			 * classes
			 */
			if (siblings[n]->class == RENDER_CLASS ||
			    siblings[n]->class == COMPUTE_CLASS) {
				err = -EINVAL;
				goto out_err;
			}

			if (n) {
				if (prev_engine.engine_class !=
				    ci.engine_class) {
					drm_dbg(&i915->drm,
						"Mismatched class %d, %d\n",
						prev_engine.engine_class,
						ci.engine_class);
					err = -EINVAL;
					goto out_err;
				}
			}

			prev_engine = ci;
			current_mask |= siblings[n]->logical_mask;
		}

		if (i > 0) {
			if (current_mask != prev_mask << 1) {
				drm_dbg(&i915->drm,
					"Non contiguous logical mask 0x%x, 0x%x\n",
					prev_mask, current_mask);
				err = -EINVAL;
				goto out_err;
			}
		}
		prev_mask = current_mask;
	}

	set->engines[slot].type = I915_GEM_ENGINE_TYPE_PARALLEL;
	set->engines[slot].num_siblings = num_siblings;
	set->engines[slot].width = width;
	set->engines[slot].siblings = siblings;

	return 0;

out_err:
	kfree(siblings);

	return err;
}

static const i915_user_extension_fn set_proto_ctx_engines_extensions[] = {
	[I915_CONTEXT_ENGINES_EXT_LOAD_BALANCE] = set_proto_ctx_engines_balance,
	[I915_CONTEXT_ENGINES_EXT_BOND] = set_proto_ctx_engines_bond,
	[I915_CONTEXT_ENGINES_EXT_PARALLEL_SUBMIT] =
		set_proto_ctx_engines_parallel_submit,
};

static int set_proto_ctx_engines(struct drm_i915_file_private *fpriv,
			         struct i915_gem_proto_context *pc,
			         const struct drm_i915_gem_context_param *args)
{
	struct drm_i915_private *i915 = fpriv->dev_priv;
	struct set_proto_ctx_engines set = { .i915 = i915 };
	struct i915_context_param_engines __user *user =
		u64_to_user_ptr(args->value);
	unsigned int n;
	u64 extensions;
	int err;

	if (pc->num_user_engines >= 0) {
		drm_dbg(&i915->drm, "Cannot set engines twice");
		return -EINVAL;
	}

	if (args->size < sizeof(*user) ||
	    !IS_ALIGNED(args->size - sizeof(*user), sizeof(*user->engines))) {
		drm_dbg(&i915->drm, "Invalid size for engine array: %d\n",
			args->size);
		return -EINVAL;
	}

	set.num_engines = (args->size - sizeof(*user)) / sizeof(*user->engines);
	/* RING_MASK has no shift so we can use it directly here */
	if (set.num_engines > I915_EXEC_RING_MASK + 1)
		return -EINVAL;

	set.engines = kmalloc_array(set.num_engines, sizeof(*set.engines), GFP_KERNEL);
	if (!set.engines)
		return -ENOMEM;

	for (n = 0; n < set.num_engines; n++) {
		struct i915_engine_class_instance ci;
		struct intel_engine_cs *engine;

		if (copy_from_user(&ci, &user->engines[n], sizeof(ci))) {
			kfree(set.engines);
			return -EFAULT;
		}

		memset(&set.engines[n], 0, sizeof(set.engines[n]));

		if (ci.engine_class == (u16)I915_ENGINE_CLASS_INVALID &&
		    ci.engine_instance == (u16)I915_ENGINE_CLASS_INVALID_NONE)
			continue;

		engine = intel_engine_lookup_user(i915,
						  ci.engine_class,
						  ci.engine_instance);
		if (!engine) {
			drm_dbg(&i915->drm,
				"Invalid engine[%d]: { class:%d, instance:%d }\n",
				n, ci.engine_class, ci.engine_instance);
			kfree(set.engines);
			return -ENOENT;
		}

		set.engines[n].type = I915_GEM_ENGINE_TYPE_PHYSICAL;
		set.engines[n].engine = engine;
	}

	err = -EFAULT;
	if (!get_user(extensions, &user->extensions))
		err = i915_user_extensions(u64_to_user_ptr(extensions),
					   set_proto_ctx_engines_extensions,
					   ARRAY_SIZE(set_proto_ctx_engines_extensions),
					   &set);
	if (err) {
		kfree(set.engines);
		return err;
	}

	pc->num_user_engines = set.num_engines;
	pc->user_engines = set.engines;

	return 0;
}

static int set_proto_ctx_sseu(struct drm_i915_file_private *fpriv,
			      struct i915_gem_proto_context *pc,
			      struct drm_i915_gem_context_param *args)
{
	struct drm_i915_private *i915 = fpriv->dev_priv;
	struct drm_i915_gem_context_param_sseu user_sseu;
	struct intel_sseu *sseu;
	int ret;

	if (args->size < sizeof(user_sseu))
		return -EINVAL;

	if (GRAPHICS_VER(i915) != 11)
		return -ENODEV;

	if (copy_from_user(&user_sseu, u64_to_user_ptr(args->value),
			   sizeof(user_sseu)))
		return -EFAULT;

	if (user_sseu.rsvd)
		return -EINVAL;

	if (user_sseu.flags & ~(I915_CONTEXT_SSEU_FLAG_ENGINE_INDEX))
		return -EINVAL;

	if (!!(user_sseu.flags & I915_CONTEXT_SSEU_FLAG_ENGINE_INDEX) != (pc->num_user_engines >= 0))
		return -EINVAL;

	if (pc->num_user_engines >= 0) {
		int idx = user_sseu.engine.engine_instance;
		struct i915_gem_proto_engine *pe;

		if (idx >= pc->num_user_engines)
			return -EINVAL;

		idx = array_index_nospec(idx, pc->num_user_engines);
		pe = &pc->user_engines[idx];

		/* Only render engine supports RPCS configuration. */
		if (pe->engine->class != RENDER_CLASS)
			return -EINVAL;

		sseu = &pe->sseu;
	} else {
		/* Only render engine supports RPCS configuration. */
		if (user_sseu.engine.engine_class != I915_ENGINE_CLASS_RENDER)
			return -EINVAL;

		/* There is only one render engine */
		if (user_sseu.engine.engine_instance != 0)
			return -EINVAL;

		sseu = &pc->legacy_rcs_sseu;
	}

	ret = i915_gem_user_to_context_sseu(to_gt(i915), &user_sseu, sseu);
	if (ret)
		return ret;

	args->size = sizeof(user_sseu);

	return 0;
}

static int set_proto_ctx_param(struct drm_i915_file_private *fpriv,
			       struct i915_gem_proto_context *pc,
			       struct drm_i915_gem_context_param *args)
{
	int ret = 0;

	switch (args->param) {
	case I915_CONTEXT_PARAM_NO_ERROR_CAPTURE:
		if (args->size)
			ret = -EINVAL;
		else if (args->value)
			pc->user_flags |= BIT(UCONTEXT_NO_ERROR_CAPTURE);
		else
			pc->user_flags &= ~BIT(UCONTEXT_NO_ERROR_CAPTURE);
		break;

	case I915_CONTEXT_PARAM_BANNABLE:
		if (args->size)
			ret = -EINVAL;
		else if (!capable(CAP_SYS_ADMIN) && !args->value)
			ret = -EPERM;
		else if (args->value)
			pc->user_flags |= BIT(UCONTEXT_BANNABLE);
		else if (pc->uses_protected_content)
			ret = -EPERM;
		else
			pc->user_flags &= ~BIT(UCONTEXT_BANNABLE);
		break;

	case I915_CONTEXT_PARAM_RECOVERABLE:
		if (args->size)
			ret = -EINVAL;
		else if (!args->value)
			pc->user_flags &= ~BIT(UCONTEXT_RECOVERABLE);
		else if (pc->uses_protected_content)
			ret = -EPERM;
		else
			pc->user_flags |= BIT(UCONTEXT_RECOVERABLE);
		break;

	case I915_CONTEXT_PARAM_PRIORITY:
		ret = validate_priority(fpriv->dev_priv, args);
		if (!ret)
			pc->sched.priority = args->value;
		break;

	case I915_CONTEXT_PARAM_SSEU:
		ret = set_proto_ctx_sseu(fpriv, pc, args);
		break;

	case I915_CONTEXT_PARAM_VM:
		ret = set_proto_ctx_vm(fpriv, pc, args);
		break;

	case I915_CONTEXT_PARAM_ENGINES:
		ret = set_proto_ctx_engines(fpriv, pc, args);
		break;

	case I915_CONTEXT_PARAM_PERSISTENCE:
		if (args->size)
			ret = -EINVAL;
		else
			ret = proto_context_set_persistence(fpriv->dev_priv, pc,
							    args->value);
		break;

	case I915_CONTEXT_PARAM_PROTECTED_CONTENT:
		ret = proto_context_set_protected(fpriv->dev_priv, pc,
						  args->value);
		break;

	case I915_CONTEXT_PARAM_NO_ZEROMAP:
	case I915_CONTEXT_PARAM_BAN_PERIOD:
	case I915_CONTEXT_PARAM_RINGSIZE:
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int intel_context_set_gem(struct intel_context *ce,
				 struct i915_gem_context *ctx,
				 struct intel_sseu sseu)
{
	int ret = 0;

	GEM_BUG_ON(rcu_access_pointer(ce->gem_context));
	RCU_INIT_POINTER(ce->gem_context, ctx);

	GEM_BUG_ON(intel_context_is_pinned(ce));
	ce->ring_size = SZ_16K;

	i915_vm_put(ce->vm);
	ce->vm = i915_gem_context_get_eb_vm(ctx);

	if (ctx->sched.priority >= I915_PRIORITY_NORMAL &&
	    intel_engine_has_timeslices(ce->engine) &&
	    intel_engine_has_semaphores(ce->engine))
		__set_bit(CONTEXT_USE_SEMAPHORES, &ce->flags);

	if (CONFIG_DRM_I915_REQUEST_TIMEOUT &&
	    ctx->i915->params.request_timeout_ms) {
		unsigned int timeout_ms = ctx->i915->params.request_timeout_ms;

		intel_context_set_watchdog_us(ce, (u64)timeout_ms * 1000);
	}

	/* A valid SSEU has no zero fields */
	if (sseu.slice_mask && !WARN_ON(ce->engine->class != RENDER_CLASS))
		ret = intel_context_reconfigure_sseu(ce, sseu);

	return ret;
}

static void __unpin_engines(struct i915_gem_engines *e, unsigned int count)
{
	while (count--) {
		struct intel_context *ce = e->engines[count], *child;

		if (!ce || !test_bit(CONTEXT_PERMA_PIN, &ce->flags))
			continue;

		for_each_child(ce, child)
			intel_context_unpin(child);
		intel_context_unpin(ce);
	}
}

static void unpin_engines(struct i915_gem_engines *e)
{
	__unpin_engines(e, e->num_engines);
}

static void __free_engines(struct i915_gem_engines *e, unsigned int count)
{
	while (count--) {
		if (!e->engines[count])
			continue;

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
	struct i915_gem_engines *engines =
		container_of(rcu, struct i915_gem_engines, rcu);

	i915_sw_fence_fini(&engines->fence);
	free_engines(engines);
}

static void accumulate_runtime(struct i915_drm_client *client,
			       struct i915_gem_engines *engines)
{
	struct i915_gem_engines_iter it;
	struct intel_context *ce;

	if (!client)
		return;

	/* Transfer accumulated runtime to the parent GEM context. */
	for_each_gem_engine(ce, engines, it) {
		unsigned int class = ce->engine->uabi_class;

		GEM_BUG_ON(class >= ARRAY_SIZE(client->past_runtime));
		atomic64_add(intel_context_get_total_runtime_ns(ce),
			     &client->past_runtime[class]);
	}
}

static int
engines_notify(struct i915_sw_fence *fence, enum i915_sw_fence_notify state)
{
	struct i915_gem_engines *engines =
		container_of(fence, typeof(*engines), fence);
	struct i915_gem_context *ctx = engines->ctx;

	switch (state) {
	case FENCE_COMPLETE:
		if (!list_empty(&engines->link)) {
			unsigned long flags;

			spin_lock_irqsave(&ctx->stale.lock, flags);
			list_del(&engines->link);
			spin_unlock_irqrestore(&ctx->stale.lock, flags);
		}
		accumulate_runtime(ctx->client, engines);
		i915_gem_context_put(ctx);

		break;

	case FENCE_FREE:
		init_rcu_head(&engines->rcu);
		call_rcu(&engines->rcu, free_engines_rcu);
		break;
	}

	return NOTIFY_DONE;
}

static struct i915_gem_engines *alloc_engines(unsigned int count)
{
	struct i915_gem_engines *e;

	e = kzalloc(struct_size(e, engines, count), GFP_KERNEL);
	if (!e)
		return NULL;

	i915_sw_fence_init(&e->fence, engines_notify);
	return e;
}

static struct i915_gem_engines *default_engines(struct i915_gem_context *ctx,
						struct intel_sseu rcs_sseu)
{
	const struct intel_gt *gt = to_gt(ctx->i915);
	struct intel_engine_cs *engine;
	struct i915_gem_engines *e, *err;
	enum intel_engine_id id;

	e = alloc_engines(I915_NUM_ENGINES);
	if (!e)
		return ERR_PTR(-ENOMEM);

	for_each_engine(engine, gt, id) {
		struct intel_context *ce;
		struct intel_sseu sseu = {};
		int ret;

		if (engine->legacy_idx == INVALID_ENGINE)
			continue;

		GEM_BUG_ON(engine->legacy_idx >= I915_NUM_ENGINES);
		GEM_BUG_ON(e->engines[engine->legacy_idx]);

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = ERR_CAST(ce);
			goto free_engines;
		}

		e->engines[engine->legacy_idx] = ce;
		e->num_engines = max(e->num_engines, engine->legacy_idx + 1);

		if (engine->class == RENDER_CLASS)
			sseu = rcs_sseu;

		ret = intel_context_set_gem(ce, ctx, sseu);
		if (ret) {
			err = ERR_PTR(ret);
			goto free_engines;
		}

	}

	return e;

free_engines:
	free_engines(e);
	return err;
}

static int perma_pin_contexts(struct intel_context *ce)
{
	struct intel_context *child;
	int i = 0, j = 0, ret;

	GEM_BUG_ON(!intel_context_is_parent(ce));

	ret = intel_context_pin(ce);
	if (unlikely(ret))
		return ret;

	for_each_child(ce, child) {
		ret = intel_context_pin(child);
		if (unlikely(ret))
			goto unwind;
		++i;
	}

	set_bit(CONTEXT_PERMA_PIN, &ce->flags);

	return 0;

unwind:
	intel_context_unpin(ce);
	for_each_child(ce, child) {
		if (j++ < i)
			intel_context_unpin(child);
		else
			break;
	}

	return ret;
}

static struct i915_gem_engines *user_engines(struct i915_gem_context *ctx,
					     unsigned int num_engines,
					     struct i915_gem_proto_engine *pe)
{
	struct i915_gem_engines *e, *err;
	unsigned int n;

	e = alloc_engines(num_engines);
	if (!e)
		return ERR_PTR(-ENOMEM);
	e->num_engines = num_engines;

	for (n = 0; n < num_engines; n++) {
		struct intel_context *ce, *child;
		int ret;

		switch (pe[n].type) {
		case I915_GEM_ENGINE_TYPE_PHYSICAL:
			ce = intel_context_create(pe[n].engine);
			break;

		case I915_GEM_ENGINE_TYPE_BALANCED:
			ce = intel_engine_create_virtual(pe[n].siblings,
							 pe[n].num_siblings, 0);
			break;

		case I915_GEM_ENGINE_TYPE_PARALLEL:
			ce = intel_engine_create_parallel(pe[n].siblings,
							  pe[n].num_siblings,
							  pe[n].width);
			break;

		case I915_GEM_ENGINE_TYPE_INVALID:
		default:
			GEM_WARN_ON(pe[n].type != I915_GEM_ENGINE_TYPE_INVALID);
			continue;
		}

		if (IS_ERR(ce)) {
			err = ERR_CAST(ce);
			goto free_engines;
		}

		e->engines[n] = ce;

		ret = intel_context_set_gem(ce, ctx, pe->sseu);
		if (ret) {
			err = ERR_PTR(ret);
			goto free_engines;
		}
		for_each_child(ce, child) {
			ret = intel_context_set_gem(child, ctx, pe->sseu);
			if (ret) {
				err = ERR_PTR(ret);
				goto free_engines;
			}
		}

		/*
		 * XXX: Must be done after calling intel_context_set_gem as that
		 * function changes the ring size. The ring is allocated when
		 * the context is pinned. If the ring size is changed after
		 * allocation we have a mismatch of the ring size and will cause
		 * the context to hang. Presumably with a bit of reordering we
		 * could move the perma-pin step to the backend function
		 * intel_engine_create_parallel.
		 */
		if (pe[n].type == I915_GEM_ENGINE_TYPE_PARALLEL) {
			ret = perma_pin_contexts(ce);
			if (ret) {
				err = ERR_PTR(ret);
				goto free_engines;
			}
		}
	}

	return e;

free_engines:
	free_engines(e);
	return err;
}

static void i915_gem_context_release_work(struct work_struct *work)
{
	struct i915_gem_context *ctx = container_of(work, typeof(*ctx),
						    release_work);
	struct i915_address_space *vm;

	trace_i915_context_free(ctx);
	GEM_BUG_ON(!i915_gem_context_is_closed(ctx));

	spin_lock(&ctx->i915->gem.contexts.lock);
	list_del(&ctx->link);
	spin_unlock(&ctx->i915->gem.contexts.lock);

	if (ctx->syncobj)
		drm_syncobj_put(ctx->syncobj);

	vm = ctx->vm;
	if (vm)
		i915_vm_put(vm);

	if (ctx->pxp_wakeref)
		intel_runtime_pm_put(&ctx->i915->runtime_pm, ctx->pxp_wakeref);

	if (ctx->client)
		i915_drm_client_put(ctx->client);

	mutex_destroy(&ctx->engines_mutex);
	mutex_destroy(&ctx->lut_mutex);

	put_pid(ctx->pid);
	mutex_destroy(&ctx->mutex);

	kfree_rcu(ctx, rcu);
}

void i915_gem_context_release(struct kref *ref)
{
	struct i915_gem_context *ctx = container_of(ref, typeof(*ctx), ref);

	queue_work(ctx->i915->wq, &ctx->release_work);
}

static inline struct i915_gem_engines *
__context_engines_static(const struct i915_gem_context *ctx)
{
	return rcu_dereference_protected(ctx->engines, true);
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
	return intel_engine_pulse(engine) == 0;
}

static struct intel_engine_cs *active_engine(struct intel_context *ce)
{
	struct intel_engine_cs *engine = NULL;
	struct i915_request *rq;

	if (intel_context_has_inflight(ce))
		return intel_context_inflight(ce);

	if (!ce->timeline)
		return NULL;

	/*
	 * rq->link is only SLAB_TYPESAFE_BY_RCU, we need to hold a reference
	 * to the request to prevent it being transferred to a new timeline
	 * (and onto a new timeline->requests list).
	 */
	rcu_read_lock();
	list_for_each_entry_reverse(rq, &ce->timeline->requests, link) {
		bool found;

		/* timeline is already completed upto this point? */
		if (!i915_request_get_rcu(rq))
			break;

		/* Check with the backend if the request is inflight */
		found = true;
		if (likely(rcu_access_pointer(rq->timeline) == ce->timeline))
			found = i915_request_active_engine(rq, &engine);

		i915_request_put(rq);
		if (found)
			break;
	}
	rcu_read_unlock();

	return engine;
}

static void
kill_engines(struct i915_gem_engines *engines, bool exit, bool persistent)
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
	for_each_gem_engine(ce, engines, it) {
		struct intel_engine_cs *engine;

		if ((exit || !persistent) && intel_context_revoke(ce))
			continue; /* Already marked. */

		/*
		 * Check the current active state of this context; if we
		 * are currently executing on the GPU we need to evict
		 * ourselves. On the other hand, if we haven't yet been
		 * submitted to the GPU or if everything is complete,
		 * we have nothing to do.
		 */
		engine = active_engine(ce);

		/* First attempt to gracefully cancel the context */
		if (engine && !__cancel_engine(engine) && (exit || !persistent))
			/*
			 * If we are unable to send a preemptive pulse to bump
			 * the context from the GPU, we have to resort to a full
			 * reset. We hope the collateral damage is worth it.
			 */
			__reset_context(engines->ctx, engine);
	}
}

static void kill_context(struct i915_gem_context *ctx)
{
	struct i915_gem_engines *pos, *next;

	spin_lock_irq(&ctx->stale.lock);
	GEM_BUG_ON(!i915_gem_context_is_closed(ctx));
	list_for_each_entry_safe(pos, next, &ctx->stale.engines, link) {
		if (!i915_sw_fence_await(&pos->fence)) {
			list_del_init(&pos->link);
			continue;
		}

		spin_unlock_irq(&ctx->stale.lock);

		kill_engines(pos, !ctx->i915->params.enable_hangcheck,
			     i915_gem_context_is_persistent(ctx));

		spin_lock_irq(&ctx->stale.lock);
		GEM_BUG_ON(i915_sw_fence_signaled(&pos->fence));
		list_safe_reset_next(pos, next, link);
		list_del_init(&pos->link); /* decouple from FENCE_COMPLETE */

		i915_sw_fence_complete(&pos->fence);
	}
	spin_unlock_irq(&ctx->stale.lock);
}

static void engines_idle_release(struct i915_gem_context *ctx,
				 struct i915_gem_engines *engines)
{
	struct i915_gem_engines_iter it;
	struct intel_context *ce;

	INIT_LIST_HEAD(&engines->link);

	engines->ctx = i915_gem_context_get(ctx);

	for_each_gem_engine(ce, engines, it) {
		int err;

		/* serialises with execbuf */
		set_bit(CONTEXT_CLOSED_BIT, &ce->flags);
		if (!intel_context_pin_if_active(ce))
			continue;

		/* Wait until context is finally scheduled out and retired */
		err = i915_sw_fence_await_active(&engines->fence,
						 &ce->active,
						 I915_ACTIVE_AWAIT_BARRIER);
		intel_context_unpin(ce);
		if (err)
			goto kill;
	}

	spin_lock_irq(&ctx->stale.lock);
	if (!i915_gem_context_is_closed(ctx))
		list_add_tail(&engines->link, &ctx->stale.engines);
	spin_unlock_irq(&ctx->stale.lock);

kill:
	if (list_empty(&engines->link)) /* raced, already closed */
		kill_engines(engines, true,
			     i915_gem_context_is_persistent(ctx));

	i915_sw_fence_commit(&engines->fence);
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
	struct i915_drm_client *client;

	/* Flush any concurrent set_engines() */
	mutex_lock(&ctx->engines_mutex);
	unpin_engines(__context_engines_static(ctx));
	engines_idle_release(ctx, rcu_replace_pointer(ctx->engines, NULL, 1));
	i915_gem_context_set_closed(ctx);
	mutex_unlock(&ctx->engines_mutex);

	mutex_lock(&ctx->mutex);

	set_closed_name(ctx);

	/*
	 * The LUT uses the VMA as a backpointer to unref the object,
	 * so we need to clear the LUT before we close all the VMA (inside
	 * the ppgtt).
	 */
	lut_close(ctx);

	ctx->file_priv = ERR_PTR(-EBADF);

	client = ctx->client;
	if (client) {
		spin_lock(&client->ctx_lock);
		list_del_rcu(&ctx->client_link);
		spin_unlock(&client->ctx_lock);
	}

	mutex_unlock(&ctx->mutex);

	/*
	 * If the user has disabled hangchecking, we can not be sure that
	 * the batches will ever complete after the context is closed,
	 * keeping the context and all resources pinned forever. So in this
	 * case we opt to forcibly kill off all remaining requests on
	 * context close.
	 */
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
		if (!ctx->i915->params.enable_hangcheck)
			return -EINVAL;

		i915_gem_context_set_persistence(ctx);
	} else {
		/* To cancel a context we use "preempt-to-idle" */
		if (!(ctx->i915->caps.scheduler & I915_SCHEDULER_CAP_PREEMPTION))
			return -ENODEV;

		/*
		 * If the cancel fails, we then need to reset, cleanly!
		 *
		 * If the per-engine reset fails, all hope is lost! We resort
		 * to a full GPU reset in that unlikely case, but realistically
		 * if the engine could not reset, the full reset does not fare
		 * much better. The damage has been done.
		 *
		 * However, if we cannot reset an engine by itself, we cannot
		 * cleanup a hanging persistent context without causing
		 * colateral damage, and we should not pretend we can by
		 * exposing the interface.
		 */
		if (!intel_has_reset_engine(to_gt(ctx->i915)))
			return -ENODEV;

		i915_gem_context_clear_persistence(ctx);
	}

	return 0;
}

static struct i915_gem_context *
i915_gem_create_context(struct drm_i915_private *i915,
			const struct i915_gem_proto_context *pc)
{
	struct i915_gem_context *ctx;
	struct i915_address_space *vm = NULL;
	struct i915_gem_engines *e;
	int err;
	int i;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	kref_init(&ctx->ref);
	ctx->i915 = i915;
	ctx->sched = pc->sched;
	mutex_init(&ctx->mutex);
	INIT_LIST_HEAD(&ctx->link);
	INIT_WORK(&ctx->release_work, i915_gem_context_release_work);

	spin_lock_init(&ctx->stale.lock);
	INIT_LIST_HEAD(&ctx->stale.engines);

	if (pc->vm) {
		vm = i915_vm_get(pc->vm);
	} else if (HAS_FULL_PPGTT(i915)) {
		struct i915_ppgtt *ppgtt;

		ppgtt = i915_ppgtt_create(to_gt(i915), 0);
		if (IS_ERR(ppgtt)) {
			drm_dbg(&i915->drm, "PPGTT setup failed (%ld)\n",
				PTR_ERR(ppgtt));
			err = PTR_ERR(ppgtt);
			goto err_ctx;
		}
		vm = &ppgtt->vm;
	}
	if (vm)
		ctx->vm = vm;

	mutex_init(&ctx->engines_mutex);
	if (pc->num_user_engines >= 0) {
		i915_gem_context_set_user_engines(ctx);
		e = user_engines(ctx, pc->num_user_engines, pc->user_engines);
	} else {
		i915_gem_context_clear_user_engines(ctx);
		e = default_engines(ctx, pc->legacy_rcs_sseu);
	}
	if (IS_ERR(e)) {
		err = PTR_ERR(e);
		goto err_vm;
	}
	RCU_INIT_POINTER(ctx->engines, e);

	INIT_RADIX_TREE(&ctx->handles_vma, GFP_KERNEL);
	mutex_init(&ctx->lut_mutex);

	/* NB: Mark all slices as needing a remap so that when the context first
	 * loads it will restore whatever remap state already exists. If there
	 * is no remap info, it will be a NOP. */
	ctx->remap_slice = ALL_L3_SLICES(i915);

	ctx->user_flags = pc->user_flags;

	for (i = 0; i < ARRAY_SIZE(ctx->hang_timestamp); i++)
		ctx->hang_timestamp[i] = jiffies - CONTEXT_FAST_HANG_JIFFIES;

	if (pc->single_timeline) {
		err = drm_syncobj_create(&ctx->syncobj,
					 DRM_SYNCOBJ_CREATE_SIGNALED,
					 NULL);
		if (err)
			goto err_engines;
	}

	if (pc->uses_protected_content) {
		ctx->pxp_wakeref = intel_runtime_pm_get(&i915->runtime_pm);
		ctx->uses_protected_content = true;
	}

	trace_i915_context_create(ctx);

	return ctx;

err_engines:
	free_engines(e);
err_vm:
	if (ctx->vm)
		i915_vm_put(ctx->vm);
err_ctx:
	kfree(ctx);
	return ERR_PTR(err);
}

static void init_contexts(struct i915_gem_contexts *gc)
{
	spin_lock_init(&gc->lock);
	INIT_LIST_HEAD(&gc->list);
}

void i915_gem_init__contexts(struct drm_i915_private *i915)
{
	init_contexts(&i915->gem.contexts);
}

/*
 * Note that this implicitly consumes the ctx reference, by placing
 * the ctx in the context_xa.
 */
static void gem_context_register(struct i915_gem_context *ctx,
				 struct drm_i915_file_private *fpriv,
				 u32 id)
{
	struct drm_i915_private *i915 = ctx->i915;
	void *old;

	ctx->file_priv = fpriv;

	ctx->pid = get_task_pid(current, PIDTYPE_PID);
	ctx->client = i915_drm_client_get(fpriv->client);

	snprintf(ctx->name, sizeof(ctx->name), "%s[%d]",
		 current->comm, pid_nr(ctx->pid));

	spin_lock(&ctx->client->ctx_lock);
	list_add_tail_rcu(&ctx->client_link, &ctx->client->ctx_list);
	spin_unlock(&ctx->client->ctx_lock);

	spin_lock(&i915->gem.contexts.lock);
	list_add_tail(&ctx->link, &i915->gem.contexts.list);
	spin_unlock(&i915->gem.contexts.lock);

	/* And finally expose ourselves to userspace via the idr */
	old = xa_store(&fpriv->context_xa, id, ctx, GFP_KERNEL);
	WARN_ON(old);
}

int i915_gem_context_open(struct drm_i915_private *i915,
			  struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_gem_proto_context *pc;
	struct i915_gem_context *ctx;
	int err;

	mutex_init(&file_priv->proto_context_lock);
	xa_init_flags(&file_priv->proto_context_xa, XA_FLAGS_ALLOC);

	/* 0 reserved for the default context */
	xa_init_flags(&file_priv->context_xa, XA_FLAGS_ALLOC1);

	/* 0 reserved for invalid/unassigned ppgtt */
	xa_init_flags(&file_priv->vm_xa, XA_FLAGS_ALLOC1);

	pc = proto_context_create(i915, 0);
	if (IS_ERR(pc)) {
		err = PTR_ERR(pc);
		goto err;
	}

	ctx = i915_gem_create_context(i915, pc);
	proto_context_close(i915, pc);
	if (IS_ERR(ctx)) {
		err = PTR_ERR(ctx);
		goto err;
	}

	gem_context_register(ctx, file_priv, 0);

	return 0;

err:
	xa_destroy(&file_priv->vm_xa);
	xa_destroy(&file_priv->context_xa);
	xa_destroy(&file_priv->proto_context_xa);
	mutex_destroy(&file_priv->proto_context_lock);
	return err;
}

void i915_gem_context_close(struct drm_file *file)
{
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_gem_proto_context *pc;
	struct i915_address_space *vm;
	struct i915_gem_context *ctx;
	unsigned long idx;

	xa_for_each(&file_priv->proto_context_xa, idx, pc)
		proto_context_close(file_priv->dev_priv, pc);
	xa_destroy(&file_priv->proto_context_xa);
	mutex_destroy(&file_priv->proto_context_lock);

	xa_for_each(&file_priv->context_xa, idx, ctx)
		context_close(ctx);
	xa_destroy(&file_priv->context_xa);

	xa_for_each(&file_priv->vm_xa, idx, vm)
		i915_vm_put(vm);
	xa_destroy(&file_priv->vm_xa);
}

int i915_gem_vm_create_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct drm_i915_gem_vm_control *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_ppgtt *ppgtt;
	u32 id;
	int err;

	if (!HAS_FULL_PPGTT(i915))
		return -ENODEV;

	if (args->flags)
		return -EINVAL;

	ppgtt = i915_ppgtt_create(to_gt(i915), 0);
	if (IS_ERR(ppgtt))
		return PTR_ERR(ppgtt);

	if (args->extensions) {
		err = i915_user_extensions(u64_to_user_ptr(args->extensions),
					   NULL, 0,
					   ppgtt);
		if (err)
			goto err_put;
	}

	err = xa_alloc(&file_priv->vm_xa, &id, &ppgtt->vm,
		       xa_limit_32b, GFP_KERNEL);
	if (err)
		goto err_put;

	GEM_BUG_ON(id == 0); /* reserved for invalid/unassigned ppgtt */
	args->vm_id = id;
	return 0;

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

	if (args->flags)
		return -EINVAL;

	if (args->extensions)
		return -EINVAL;

	vm = xa_erase(&file_priv->vm_xa, args->vm_id);
	if (!vm)
		return -ENOENT;

	i915_vm_put(vm);
	return 0;
}

static int get_ppgtt(struct drm_i915_file_private *file_priv,
		     struct i915_gem_context *ctx,
		     struct drm_i915_gem_context_param *args)
{
	struct i915_address_space *vm;
	int err;
	u32 id;

	if (!i915_gem_context_has_full_ppgtt(ctx))
		return -ENODEV;

	vm = ctx->vm;
	GEM_BUG_ON(!vm);

	/*
	 * Get a reference for the allocated handle.  Once the handle is
	 * visible in the vm_xa table, userspace could try to close it
	 * from under our feet, so we need to hold the extra reference
	 * first.
	 */
	i915_vm_get(vm);

	err = xa_alloc(&file_priv->vm_xa, &id, vm, xa_limit_32b, GFP_KERNEL);
	if (err) {
		i915_vm_put(vm);
		return err;
	}

	GEM_BUG_ON(id == 0); /* reserved for invalid/unassigned ppgtt */
	args->value = id;
	args->size = 0;

	return err;
}

int
i915_gem_user_to_context_sseu(struct intel_gt *gt,
			      const struct drm_i915_gem_context_param_sseu *user,
			      struct intel_sseu *context)
{
	const struct sseu_dev_info *device = &gt->info.sseu;
	struct drm_i915_private *i915 = gt->i915;
	unsigned int dev_subslice_mask = intel_sseu_get_hsw_subslices(device, 0);

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

	if (user->subslice_mask & ~dev_subslice_mask)
		return -EINVAL;

	if (user->max_eus_per_subslice > device->max_eus_per_subslice)
		return -EINVAL;

	context->slice_mask = user->slice_mask;
	context->subslice_mask = user->subslice_mask;
	context->min_eus_per_subslice = user->min_eus_per_subslice;
	context->max_eus_per_subslice = user->max_eus_per_subslice;

	/* Part specific restrictions. */
	if (GRAPHICS_VER(i915) == 11) {
		unsigned int hw_s = hweight8(device->slice_mask);
		unsigned int hw_ss_per_s = hweight8(dev_subslice_mask);
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

	if (GRAPHICS_VER(i915) != 11)
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

	ret = i915_gem_user_to_context_sseu(ce->engine->gt, &user_sseu, &sseu);
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

static int
set_persistence(struct i915_gem_context *ctx,
		const struct drm_i915_gem_context_param *args)
{
	if (args->size)
		return -EINVAL;

	return __context_set_persistence(ctx, args->value);
}

static int set_priority(struct i915_gem_context *ctx,
			const struct drm_i915_gem_context_param *args)
{
	struct i915_gem_engines_iter it;
	struct intel_context *ce;
	int err;

	err = validate_priority(ctx->i915, args);
	if (err)
		return err;

	ctx->sched.priority = args->value;

	for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it) {
		if (!intel_engine_has_timeslices(ce->engine))
			continue;

		if (ctx->sched.priority >= I915_PRIORITY_NORMAL &&
		    intel_engine_has_semaphores(ce->engine))
			intel_context_set_use_semaphores(ce);
		else
			intel_context_clear_use_semaphores(ce);
	}
	i915_gem_context_unlock_engines(ctx);

	return 0;
}

static int get_protected(struct i915_gem_context *ctx,
			 struct drm_i915_gem_context_param *args)
{
	args->size = 0;
	args->value = i915_gem_context_uses_protected_content(ctx);

	return 0;
}

static int ctx_setparam(struct drm_i915_file_private *fpriv,
			struct i915_gem_context *ctx,
			struct drm_i915_gem_context_param *args)
{
	int ret = 0;

	switch (args->param) {
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
		else if (i915_gem_context_uses_protected_content(ctx))
			ret = -EPERM; /* can't clear this for protected contexts */
		else
			i915_gem_context_clear_bannable(ctx);
		break;

	case I915_CONTEXT_PARAM_RECOVERABLE:
		if (args->size)
			ret = -EINVAL;
		else if (!args->value)
			i915_gem_context_clear_recoverable(ctx);
		else if (i915_gem_context_uses_protected_content(ctx))
			ret = -EPERM; /* can't set this for protected contexts */
		else
			i915_gem_context_set_recoverable(ctx);
		break;

	case I915_CONTEXT_PARAM_PRIORITY:
		ret = set_priority(ctx, args);
		break;

	case I915_CONTEXT_PARAM_SSEU:
		ret = set_sseu(ctx, args);
		break;

	case I915_CONTEXT_PARAM_PERSISTENCE:
		ret = set_persistence(ctx, args);
		break;

	case I915_CONTEXT_PARAM_PROTECTED_CONTENT:
	case I915_CONTEXT_PARAM_NO_ZEROMAP:
	case I915_CONTEXT_PARAM_BAN_PERIOD:
	case I915_CONTEXT_PARAM_RINGSIZE:
	case I915_CONTEXT_PARAM_VM:
	case I915_CONTEXT_PARAM_ENGINES:
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

struct create_ext {
	struct i915_gem_proto_context *pc;
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

	return set_proto_ctx_param(arg->fpriv, arg->pc, &local.param);
}

static int invalid_ext(struct i915_user_extension __user *ext, void *data)
{
	return -EINVAL;
}

static const i915_user_extension_fn create_extensions[] = {
	[I915_CONTEXT_CREATE_EXT_SETPARAM] = create_setparam,
	[I915_CONTEXT_CREATE_EXT_CLONE] = invalid_ext,
};

static bool client_is_banned(struct drm_i915_file_private *file_priv)
{
	return atomic_read(&file_priv->ban_score) >= I915_CLIENT_SCORE_BANNED;
}

static inline struct i915_gem_context *
__context_lookup(struct drm_i915_file_private *file_priv, u32 id)
{
	struct i915_gem_context *ctx;

	rcu_read_lock();
	ctx = xa_load(&file_priv->context_xa, id);
	if (ctx && !kref_get_unless_zero(&ctx->ref))
		ctx = NULL;
	rcu_read_unlock();

	return ctx;
}

static struct i915_gem_context *
finalize_create_context_locked(struct drm_i915_file_private *file_priv,
			       struct i915_gem_proto_context *pc, u32 id)
{
	struct i915_gem_context *ctx;
	void *old;

	lockdep_assert_held(&file_priv->proto_context_lock);

	ctx = i915_gem_create_context(file_priv->dev_priv, pc);
	if (IS_ERR(ctx))
		return ctx;

	/*
	 * One for the xarray and one for the caller.  We need to grab
	 * the reference *prior* to making the ctx visble to userspace
	 * in gem_context_register(), as at any point after that
	 * userspace can try to race us with another thread destroying
	 * the context under our feet.
	 */
	i915_gem_context_get(ctx);

	gem_context_register(ctx, file_priv, id);

	old = xa_erase(&file_priv->proto_context_xa, id);
	GEM_BUG_ON(old != pc);
	proto_context_close(file_priv->dev_priv, pc);

	return ctx;
}

struct i915_gem_context *
i915_gem_context_lookup(struct drm_i915_file_private *file_priv, u32 id)
{
	struct i915_gem_proto_context *pc;
	struct i915_gem_context *ctx;

	ctx = __context_lookup(file_priv, id);
	if (ctx)
		return ctx;

	mutex_lock(&file_priv->proto_context_lock);
	/* Try one more time under the lock */
	ctx = __context_lookup(file_priv, id);
	if (!ctx) {
		pc = xa_load(&file_priv->proto_context_xa, id);
		if (!pc)
			ctx = ERR_PTR(-ENOENT);
		else
			ctx = finalize_create_context_locked(file_priv, pc, id);
	}
	mutex_unlock(&file_priv->proto_context_lock);

	return ctx;
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

	ret = intel_gt_terminally_wedged(to_gt(i915));
	if (ret)
		return ret;

	ext_data.fpriv = file->driver_priv;
	if (client_is_banned(ext_data.fpriv)) {
		drm_dbg(&i915->drm,
			"client %s[%d] banned from creating ctx\n",
			current->comm, task_pid_nr(current));
		return -EIO;
	}

	ext_data.pc = proto_context_create(i915, args->flags);
	if (IS_ERR(ext_data.pc))
		return PTR_ERR(ext_data.pc);

	if (args->flags & I915_CONTEXT_CREATE_FLAGS_USE_EXTENSIONS) {
		ret = i915_user_extensions(u64_to_user_ptr(args->extensions),
					   create_extensions,
					   ARRAY_SIZE(create_extensions),
					   &ext_data);
		if (ret)
			goto err_pc;
	}

	if (GRAPHICS_VER(i915) > 12) {
		struct i915_gem_context *ctx;

		/* Get ourselves a context ID */
		ret = xa_alloc(&ext_data.fpriv->context_xa, &id, NULL,
			       xa_limit_32b, GFP_KERNEL);
		if (ret)
			goto err_pc;

		ctx = i915_gem_create_context(i915, ext_data.pc);
		if (IS_ERR(ctx)) {
			ret = PTR_ERR(ctx);
			goto err_pc;
		}

		proto_context_close(i915, ext_data.pc);
		gem_context_register(ctx, ext_data.fpriv, id);
	} else {
		ret = proto_context_register(ext_data.fpriv, ext_data.pc, &id);
		if (ret < 0)
			goto err_pc;
	}

	args->ctx_id = id;
	drm_dbg(&i915->drm, "HW context %d created\n", args->ctx_id);

	return 0;

err_pc:
	proto_context_close(i915, ext_data.pc);
	return ret;
}

int i915_gem_context_destroy_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file)
{
	struct drm_i915_gem_context_destroy *args = data;
	struct drm_i915_file_private *file_priv = file->driver_priv;
	struct i915_gem_proto_context *pc;
	struct i915_gem_context *ctx;

	if (args->pad != 0)
		return -EINVAL;

	if (!args->ctx_id)
		return -ENOENT;

	/* We need to hold the proto-context lock here to prevent races
	 * with finalize_create_context_locked().
	 */
	mutex_lock(&file_priv->proto_context_lock);
	ctx = xa_erase(&file_priv->context_xa, args->ctx_id);
	pc = xa_erase(&file_priv->proto_context_xa, args->ctx_id);
	mutex_unlock(&file_priv->proto_context_lock);

	if (!ctx && !pc)
		return -ENOENT;
	GEM_WARN_ON(ctx && pc);

	if (pc)
		proto_context_close(file_priv->dev_priv, pc);

	if (ctx)
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
	struct i915_address_space *vm;
	int ret = 0;

	ctx = i915_gem_context_lookup(file_priv, args->ctx_id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	switch (args->param) {
	case I915_CONTEXT_PARAM_GTT_SIZE:
		args->size = 0;
		vm = i915_gem_context_get_eb_vm(ctx);
		args->value = vm->total;
		i915_vm_put(vm);

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
		args->value = ctx->sched.priority;
		break;

	case I915_CONTEXT_PARAM_SSEU:
		ret = get_sseu(ctx, args);
		break;

	case I915_CONTEXT_PARAM_VM:
		ret = get_ppgtt(file_priv, ctx, args);
		break;

	case I915_CONTEXT_PARAM_PERSISTENCE:
		args->size = 0;
		args->value = i915_gem_context_is_persistent(ctx);
		break;

	case I915_CONTEXT_PARAM_PROTECTED_CONTENT:
		ret = get_protected(ctx, args);
		break;

	case I915_CONTEXT_PARAM_NO_ZEROMAP:
	case I915_CONTEXT_PARAM_BAN_PERIOD:
	case I915_CONTEXT_PARAM_ENGINES:
	case I915_CONTEXT_PARAM_RINGSIZE:
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
	struct i915_gem_proto_context *pc;
	struct i915_gem_context *ctx;
	int ret = 0;

	mutex_lock(&file_priv->proto_context_lock);
	ctx = __context_lookup(file_priv, args->ctx_id);
	if (!ctx) {
		pc = xa_load(&file_priv->proto_context_xa, args->ctx_id);
		if (pc) {
			/* Contexts should be finalized inside
			 * GEM_CONTEXT_CREATE starting with graphics
			 * version 13.
			 */
			WARN_ON(GRAPHICS_VER(file_priv->dev_priv) > 12);
			ret = set_proto_ctx_param(file_priv, pc, args);
		} else {
			ret = -ENOENT;
		}
	}
	mutex_unlock(&file_priv->proto_context_lock);

	if (ctx) {
		ret = ctx_setparam(file_priv, ctx, args);
		i915_gem_context_put(ctx);
	}

	return ret;
}

int i915_gem_context_reset_stats_ioctl(struct drm_device *dev,
				       void *data, struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct drm_i915_reset_stats *args = data;
	struct i915_gem_context *ctx;

	if (args->flags || args->pad)
		return -EINVAL;

	ctx = i915_gem_context_lookup(file->driver_priv, args->ctx_id);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

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

	i915_gem_context_put(ctx);
	return 0;
}

/* GEM context-engines iterator: for_each_gem_engine() */
struct intel_context *
i915_gem_engines_iter_next(struct i915_gem_engines_iter *it)
{
	const struct i915_gem_engines *e = it->engines;
	struct intel_context *ctx;

	if (unlikely(!e))
		return NULL;

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

void i915_gem_context_module_exit(void)
{
	kmem_cache_destroy(slab_luts);
}

int __init i915_gem_context_module_init(void)
{
	slab_luts = KMEM_CACHE(i915_lut_handle, 0);
	if (!slab_luts)
		return -ENOMEM;

	return 0;
}
