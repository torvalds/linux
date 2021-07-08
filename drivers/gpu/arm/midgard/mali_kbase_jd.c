/*
 *
 * (C) COPYRIGHT 2010-2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





#if defined(CONFIG_DMA_SHARED_BUFFER)
#include <linux/dma-buf.h>
#endif				/* defined(CONFIG_DMA_SHARED_BUFFER) */
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <mali_kbase.h>
#include <mali_kbase_uku.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/ratelimit.h>
#include <linux/nospec.h>

#include <mali_kbase_jm.h>
#include <mali_kbase_hwaccess_jm.h>
#include <mali_kbase_tlstream.h>

#include "mali_kbase_dma_fence.h"

#define beenthere(kctx, f, a...)  dev_dbg(kctx->kbdev->dev, "%s:" f, __func__, ##a)

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
/* random32 was renamed to prandom_u32 in 3.8 */
#define prandom_u32 random32
#endif

/* Return whether katom will run on the GPU or not. Currently only soft jobs and
 * dependency-only atoms do not run on the GPU */
#define IS_GPU_ATOM(katom) (!((katom->core_req & BASE_JD_REQ_SOFT_JOB) ||  \
			((katom->core_req & BASE_JD_REQ_ATOM_TYPE) ==    \
							BASE_JD_REQ_DEP)))
/*
 * This is the kernel side of the API. Only entry points are:
 * - kbase_jd_submit(): Called from userspace to submit a single bag
 * - kbase_jd_done(): Called from interrupt context to track the
 *   completion of a job.
 * Callouts:
 * - to the job manager (enqueue a job)
 * - to the event subsystem (signals the completion/failure of bag/job-chains).
 */

static void __user *
get_compat_pointer(struct kbase_context *kctx, const union kbase_pointer *p)
{
#ifdef CONFIG_COMPAT
	if (kbase_ctx_flag(kctx, KCTX_COMPAT))
		return compat_ptr(p->compat_value);
#endif
	return p->value;
}

/* Runs an atom, either by handing to the JS or by immediately running it in the case of soft-jobs
 *
 * Returns whether the JS needs a reschedule.
 *
 * Note that the caller must also check the atom status and
 * if it is KBASE_JD_ATOM_STATE_COMPLETED must call jd_done_nolock
 */
static int jd_run_atom(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;

	KBASE_DEBUG_ASSERT(katom->status != KBASE_JD_ATOM_STATE_UNUSED);

	if ((katom->core_req & BASE_JD_REQ_ATOM_TYPE) == BASE_JD_REQ_DEP) {
		/* Dependency only atom */
		katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
		return 0;
	} else if (katom->core_req & BASE_JD_REQ_SOFT_JOB) {
		/* Soft-job */
		if (katom->will_fail_event_code) {
			katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
			return 0;
		}
		if ((katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE)
						  == BASE_JD_REQ_SOFT_REPLAY) {
			if (!kbase_replay_process(katom))
				katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
		} else if (kbase_process_soft_job(katom) == 0) {
			kbase_finish_soft_job(katom);
			katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
		}
		return 0;
	}

	katom->status = KBASE_JD_ATOM_STATE_IN_JS;
	/* Queue an action about whether we should try scheduling a context */
	return kbasep_js_add_job(kctx, katom);
}

#if defined(CONFIG_KDS) || defined(CONFIG_MALI_DMA_FENCE)
void kbase_jd_dep_clear_locked(struct kbase_jd_atom *katom)
{
	struct kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(katom);
	kbdev = katom->kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev);

	/* Check whether the atom's other dependencies were already met. If
	 * katom is a GPU atom then the job scheduler may be able to represent
	 * the dependencies, hence we may attempt to submit it before they are
	 * met. Other atoms must have had both dependencies resolved.
	 */
	if (IS_GPU_ATOM(katom) ||
			(!kbase_jd_katom_dep_atom(&katom->dep[0]) &&
			!kbase_jd_katom_dep_atom(&katom->dep[1]))) {
		/* katom dep complete, attempt to run it */
		bool resched = false;

		resched = jd_run_atom(katom);

		if (katom->status == KBASE_JD_ATOM_STATE_COMPLETED) {
			/* The atom has already finished */
			resched |= jd_done_nolock(katom, NULL);
		}

		if (resched)
			kbase_js_sched_all(kbdev);
	}
}
#endif

#ifdef CONFIG_KDS

/* Add the katom to the kds waiting list.
 * Atoms must be added to the waiting list after a successful call to kds_async_waitall.
 * The caller must hold the kbase_jd_context.lock */

static void kbase_jd_kds_waiters_add(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx;

	KBASE_DEBUG_ASSERT(katom);

	kctx = katom->kctx;

	list_add_tail(&katom->node, &kctx->waiting_kds_resource);
}

/* Remove the katom from the kds waiting list.
 * Atoms must be removed from the waiting list before a call to kds_resource_set_release_sync.
 * The supplied katom must first have been added to the list with a call to kbase_jd_kds_waiters_add.
 * The caller must hold the kbase_jd_context.lock */

static void kbase_jd_kds_waiters_remove(struct kbase_jd_atom *katom)
{
	KBASE_DEBUG_ASSERT(katom);
	list_del(&katom->node);
}

static void kds_dep_clear(void *callback_parameter, void *callback_extra_parameter)
{
	struct kbase_jd_atom *katom;
	struct kbase_jd_context *ctx;

	katom = (struct kbase_jd_atom *)callback_parameter;
	KBASE_DEBUG_ASSERT(katom);

	ctx = &katom->kctx->jctx;

	/* If KDS resource has already been satisfied (e.g. due to zapping)
	 * do nothing.
	 */
	mutex_lock(&ctx->lock);
	if (!katom->kds_dep_satisfied) {
		katom->kds_dep_satisfied = true;
		kbase_jd_dep_clear_locked(katom);
	}
	mutex_unlock(&ctx->lock);
}

static void kbase_cancel_kds_wait_job(struct kbase_jd_atom *katom)
{
	KBASE_DEBUG_ASSERT(katom);

	/* Prevent job_done_nolock from being called twice on an atom when
	 *  there is a race between job completion and cancellation */

	if (katom->status == KBASE_JD_ATOM_STATE_QUEUED) {
		/* Wait was cancelled - zap the atom */
		katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;
		if (jd_done_nolock(katom, NULL))
			kbase_js_sched_all(katom->kctx->kbdev);
	}
}
#endif				/* CONFIG_KDS */

void kbase_jd_free_external_resources(struct kbase_jd_atom *katom)
{
#ifdef CONFIG_KDS
	if (katom->kds_rset) {
		struct kbase_jd_context *jctx = &katom->kctx->jctx;

		/*
		 * As the atom is no longer waiting, remove it from
		 * the waiting list.
		 */

		mutex_lock(&jctx->lock);
		kbase_jd_kds_waiters_remove(katom);
		mutex_unlock(&jctx->lock);

		/* Release the kds resource or cancel if zapping */
		kds_resource_set_release_sync(&katom->kds_rset);
	}
#endif				/* CONFIG_KDS */

#ifdef CONFIG_MALI_DMA_FENCE
	/* Flush dma-fence workqueue to ensure that any callbacks that may have
	 * been queued are done before continuing.
	 * Any successfully completed atom would have had all it's callbacks
	 * completed before the atom was run, so only flush for failed atoms.
	 */
	if (katom->event_code != BASE_JD_EVENT_DONE)
		flush_workqueue(katom->kctx->dma_fence.wq);
#endif /* CONFIG_MALI_DMA_FENCE */
}

static void kbase_jd_post_external_resources(struct kbase_jd_atom *katom)
{
	KBASE_DEBUG_ASSERT(katom);
	KBASE_DEBUG_ASSERT(katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES);

#ifdef CONFIG_KDS
	/* Prevent the KDS resource from triggering the atom in case of zapping */
	if (katom->kds_rset)
		katom->kds_dep_satisfied = true;
#endif				/* CONFIG_KDS */

#ifdef CONFIG_MALI_DMA_FENCE
	kbase_dma_fence_signal(katom);
#endif /* CONFIG_MALI_DMA_FENCE */

	kbase_gpu_vm_lock(katom->kctx);
	/* only roll back if extres is non-NULL */
	if (katom->extres) {
		u32 res_no;

		res_no = katom->nr_extres;
		while (res_no-- > 0) {
			struct kbase_mem_phy_alloc *alloc = katom->extres[res_no].alloc;
			struct kbase_va_region *reg;

			reg = kbase_region_tracker_find_region_base_address(
					katom->kctx,
					katom->extres[res_no].gpu_address);
			kbase_unmap_external_resource(katom->kctx, reg, alloc);
		}
		kfree(katom->extres);
		katom->extres = NULL;
	}
	kbase_gpu_vm_unlock(katom->kctx);
}

/*
 * Set up external resources needed by this job.
 *
 * jctx.lock must be held when this is called.
 */

static int kbase_jd_pre_external_resources(struct kbase_jd_atom *katom, const struct base_jd_atom_v2 *user_atom)
{
	int err_ret_val = -EINVAL;
	u32 res_no;
#ifdef CONFIG_KDS
	u32 kds_res_count = 0;
	struct kds_resource **kds_resources = NULL;
	unsigned long *kds_access_bitmap = NULL;
#endif				/* CONFIG_KDS */
#ifdef CONFIG_MALI_DMA_FENCE
	struct kbase_dma_fence_resv_info info = {
		.dma_fence_resv_count = 0,
	};
#ifdef CONFIG_SYNC
	/*
	 * When both dma-buf fence and Android native sync is enabled, we
	 * disable dma-buf fence for contexts that are using Android native
	 * fences.
	 */
	const bool implicit_sync = !kbase_ctx_flag(katom->kctx,
						   KCTX_NO_IMPLICIT_SYNC);
#else /* CONFIG_SYNC */
	const bool implicit_sync = true;
#endif /* CONFIG_SYNC */
#endif /* CONFIG_MALI_DMA_FENCE */
	struct base_external_resource *input_extres;

	KBASE_DEBUG_ASSERT(katom);
	KBASE_DEBUG_ASSERT(katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES);

	/* no resources encoded, early out */
	if (!katom->nr_extres)
		return -EINVAL;

	katom->extres = kmalloc_array(katom->nr_extres, sizeof(*katom->extres), GFP_KERNEL);
	if (NULL == katom->extres) {
		err_ret_val = -ENOMEM;
		goto early_err_out;
	}

	/* copy user buffer to the end of our real buffer.
	 * Make sure the struct sizes haven't changed in a way
	 * we don't support */
	BUILD_BUG_ON(sizeof(*input_extres) > sizeof(*katom->extres));
	input_extres = (struct base_external_resource *)
			(((unsigned char *)katom->extres) +
			(sizeof(*katom->extres) - sizeof(*input_extres)) *
			katom->nr_extres);

	if (copy_from_user(input_extres,
			get_compat_pointer(katom->kctx, &user_atom->extres_list),
			sizeof(*input_extres) * katom->nr_extres) != 0) {
		err_ret_val = -EINVAL;
		goto early_err_out;
	}
#ifdef CONFIG_KDS
	/* assume we have to wait for all */
	KBASE_DEBUG_ASSERT(0 != katom->nr_extres);
	kds_resources = kmalloc_array(katom->nr_extres, sizeof(struct kds_resource *), GFP_KERNEL);

	if (!kds_resources) {
		err_ret_val = -ENOMEM;
		goto early_err_out;
	}

	KBASE_DEBUG_ASSERT(0 != katom->nr_extres);
	kds_access_bitmap = kcalloc(BITS_TO_LONGS(katom->nr_extres),
				    sizeof(unsigned long),
				    GFP_KERNEL);
	if (!kds_access_bitmap) {
		err_ret_val = -ENOMEM;
		goto early_err_out;
	}
#endif				/* CONFIG_KDS */

#ifdef CONFIG_MALI_DMA_FENCE
	if (implicit_sync) {
		info.resv_objs = kmalloc_array(katom->nr_extres,
					sizeof(struct reservation_object *),
					GFP_KERNEL);
		if (!info.resv_objs) {
			err_ret_val = -ENOMEM;
			goto early_err_out;
		}

		info.dma_fence_excl_bitmap =
				kcalloc(BITS_TO_LONGS(katom->nr_extres),
					sizeof(unsigned long), GFP_KERNEL);
		if (!info.dma_fence_excl_bitmap) {
			err_ret_val = -ENOMEM;
			goto early_err_out;
		}
	}
#endif /* CONFIG_MALI_DMA_FENCE */

	/* Take the processes mmap lock */
	down_read(&current->mm->mmap_lock);

	/* need to keep the GPU VM locked while we set up UMM buffers */
	kbase_gpu_vm_lock(katom->kctx);
	for (res_no = 0; res_no < katom->nr_extres; res_no++) {
		struct base_external_resource *res;
		struct kbase_va_region *reg;
		struct kbase_mem_phy_alloc *alloc;
		bool exclusive;

		res = &input_extres[res_no];
		exclusive = (res->ext_resource & BASE_EXT_RES_ACCESS_EXCLUSIVE)
				? true : false;
		reg = kbase_region_tracker_find_region_enclosing_address(
				katom->kctx,
				res->ext_resource & ~BASE_EXT_RES_ACCESS_EXCLUSIVE);
		/* did we find a matching region object? */
		if (NULL == reg || (reg->flags & KBASE_REG_FREE)) {
			/* roll back */
			goto failed_loop;
		}

		if (!(katom->core_req & BASE_JD_REQ_SOFT_JOB) &&
				(reg->flags & KBASE_REG_SECURE)) {
			katom->atom_flags |= KBASE_KATOM_FLAG_PROTECTED;
		}

		alloc = kbase_map_external_resource(katom->kctx, reg,
				current->mm
#ifdef CONFIG_KDS
				, &kds_res_count, kds_resources,
				kds_access_bitmap, exclusive
#endif
				);
		if (!alloc) {
			err_ret_val = -EINVAL;
			goto failed_loop;
		}

#ifdef CONFIG_MALI_DMA_FENCE
		if (implicit_sync &&
		    reg->gpu_alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM) {
			struct reservation_object *resv;

			resv = reg->gpu_alloc->imported.umm.dma_buf->resv;
			if (resv)
				kbase_dma_fence_add_reservation(resv, &info,
								exclusive);
		}
#endif /* CONFIG_MALI_DMA_FENCE */

		/* finish with updating out array with the data we found */
		/* NOTE: It is important that this is the last thing we do (or
		 * at least not before the first write) as we overwrite elements
		 * as we loop and could be overwriting ourself, so no writes
		 * until the last read for an element.
		 * */
		katom->extres[res_no].gpu_address = reg->start_pfn << PAGE_SHIFT; /* save the start_pfn (as an address, not pfn) to use fast lookup later */
		katom->extres[res_no].alloc = alloc;
	}
	/* successfully parsed the extres array */
	/* drop the vm lock before we call into kds */
	kbase_gpu_vm_unlock(katom->kctx);

	/* Release the processes mmap lock */
	up_read(&current->mm->mmap_lock);

#ifdef CONFIG_KDS
	if (kds_res_count) {
		int wait_failed;

		/* We have resources to wait for with kds */
		katom->kds_dep_satisfied = false;

		wait_failed = kds_async_waitall(&katom->kds_rset,
				&katom->kctx->jctx.kds_cb, katom, NULL,
				kds_res_count, kds_access_bitmap,
				kds_resources);

		if (wait_failed)
			goto failed_kds_setup;
		else
			kbase_jd_kds_waiters_add(katom);
	} else {
		/* Nothing to wait for, so kds dep met */
		katom->kds_dep_satisfied = true;
	}
	kfree(kds_resources);
	kfree(kds_access_bitmap);
#endif				/* CONFIG_KDS */

#ifdef CONFIG_MALI_DMA_FENCE
	if (implicit_sync) {
		if (info.dma_fence_resv_count) {
			int ret;

			ret = kbase_dma_fence_wait(katom, &info);
			if (ret < 0)
				goto failed_dma_fence_setup;
		}

		kfree(info.resv_objs);
		kfree(info.dma_fence_excl_bitmap);
	}
#endif /* CONFIG_MALI_DMA_FENCE */

	/* all done OK */
	return 0;

/* error handling section */

#ifdef CONFIG_MALI_DMA_FENCE
failed_dma_fence_setup:
#ifdef CONFIG_KDS
	/* If we are here, dma_fence setup failed but KDS didn't.
	 * Revert KDS setup if any.
	 */
	if (kds_res_count) {
		mutex_unlock(&katom->kctx->jctx.lock);
		kds_resource_set_release_sync(&katom->kds_rset);
		mutex_lock(&katom->kctx->jctx.lock);

		kbase_jd_kds_waiters_remove(katom);
		katom->kds_dep_satisfied = true;
	}
#endif /* CONFIG_KDS */
#endif /* CONFIG_MALI_DMA_FENCE */
#ifdef CONFIG_KDS
failed_kds_setup:
#endif
#if defined(CONFIG_KDS) || defined(CONFIG_MALI_DMA_FENCE)
	/* Lock the processes mmap lock */
	down_read(&current->mm->mmap_lock);

	/* lock before we unmap */
	kbase_gpu_vm_lock(katom->kctx);
#endif

 failed_loop:
	/* undo the loop work */
	while (res_no-- > 0) {
		struct kbase_mem_phy_alloc *alloc = katom->extres[res_no].alloc;

		kbase_unmap_external_resource(katom->kctx, NULL, alloc);
	}
	kbase_gpu_vm_unlock(katom->kctx);

	/* Release the processes mmap lock */
	up_read(&current->mm->mmap_lock);

 early_err_out:
	kfree(katom->extres);
	katom->extres = NULL;
#ifdef CONFIG_KDS
	kfree(kds_resources);
	kfree(kds_access_bitmap);
#endif				/* CONFIG_KDS */
#ifdef CONFIG_MALI_DMA_FENCE
	if (implicit_sync) {
		kfree(info.resv_objs);
		kfree(info.dma_fence_excl_bitmap);
	}
#endif
	return err_ret_val;
}

static inline void jd_resolve_dep(struct list_head *out_list,
					struct kbase_jd_atom *katom,
					u8 d, bool ctx_is_dying)
{
	u8 other_d = !d;

	while (!list_empty(&katom->dep_head[d])) {
		struct kbase_jd_atom *dep_atom;
		struct kbase_jd_atom *other_dep_atom;
		u8 dep_type;

		dep_atom = list_entry(katom->dep_head[d].next,
				struct kbase_jd_atom, dep_item[d]);
		list_del(katom->dep_head[d].next);

		dep_type = kbase_jd_katom_dep_type(&dep_atom->dep[d]);
		kbase_jd_katom_dep_clear(&dep_atom->dep[d]);

		if (katom->event_code != BASE_JD_EVENT_DONE &&
			(dep_type != BASE_JD_DEP_TYPE_ORDER)) {
#ifdef CONFIG_KDS
			if (!dep_atom->kds_dep_satisfied) {
				/* Just set kds_dep_satisfied to true. If the callback happens after this then it will early out and
				 * do nothing. If the callback doesn't happen then kbase_jd_post_external_resources will clean up
				 */
				dep_atom->kds_dep_satisfied = true;
			}
#endif

#ifdef CONFIG_MALI_DMA_FENCE
			kbase_dma_fence_cancel_callbacks(dep_atom);
#endif

			dep_atom->event_code = katom->event_code;
			KBASE_DEBUG_ASSERT(dep_atom->status !=
						KBASE_JD_ATOM_STATE_UNUSED);

			if ((dep_atom->core_req & BASE_JD_REQ_SOFT_REPLAY)
					!= BASE_JD_REQ_SOFT_REPLAY) {
				dep_atom->will_fail_event_code =
					dep_atom->event_code;
			} else {
				dep_atom->status =
					KBASE_JD_ATOM_STATE_COMPLETED;
			}
		}
		other_dep_atom = (struct kbase_jd_atom *)
			kbase_jd_katom_dep_atom(&dep_atom->dep[other_d]);

		if (!dep_atom->in_jd_list && (!other_dep_atom ||
				(IS_GPU_ATOM(dep_atom) && !ctx_is_dying &&
				!dep_atom->will_fail_event_code &&
				!other_dep_atom->will_fail_event_code))) {
			bool dep_satisfied = true;
#ifdef CONFIG_MALI_DMA_FENCE
			int dep_count;

			dep_count = kbase_fence_dep_count_read(dep_atom);
			if (likely(dep_count == -1)) {
				dep_satisfied = true;
			} else {
				/*
				 * There are either still active callbacks, or
				 * all fences for this @dep_atom has signaled,
				 * but the worker that will queue the atom has
				 * not yet run.
				 *
				 * Wait for the fences to signal and the fence
				 * worker to run and handle @dep_atom. If
				 * @dep_atom was completed due to error on
				 * @katom, then the fence worker will pick up
				 * the complete status and error code set on
				 * @dep_atom above.
				 */
				dep_satisfied = false;
			}
#endif /* CONFIG_MALI_DMA_FENCE */

#ifdef CONFIG_KDS
			dep_satisfied = dep_satisfied && dep_atom->kds_dep_satisfied;
#endif

			if (dep_satisfied) {
				dep_atom->in_jd_list = true;
				list_add_tail(&dep_atom->jd_item, out_list);
			}
		}
	}
}

KBASE_EXPORT_TEST_API(jd_resolve_dep);

#if MALI_CUSTOMER_RELEASE == 0
static void jd_force_failure(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	kbdev->force_replay_count++;

	if (kbdev->force_replay_count >= kbdev->force_replay_limit) {
		kbdev->force_replay_count = 0;
		katom->event_code = BASE_JD_EVENT_FORCE_REPLAY;

		if (kbdev->force_replay_random)
			kbdev->force_replay_limit =
			   (prandom_u32() % KBASEP_FORCE_REPLAY_RANDOM_LIMIT) + 1;

		dev_info(kbdev->dev, "force_replay : promoting to error\n");
	}
}

/** Test to see if atom should be forced to fail.
 *
 * This function will check if an atom has a replay job as a dependent. If so
 * then it will be considered for forced failure. */
static void jd_check_force_failure(struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	int i;

	if ((kbdev->force_replay_limit == KBASEP_FORCE_REPLAY_DISABLED) ||
	    (katom->core_req & BASEP_JD_REQ_EVENT_NEVER))
		return;

	for (i = 1; i < BASE_JD_ATOM_COUNT; i++) {
		if (kbase_jd_katom_dep_atom(&kctx->jctx.atoms[i].dep[0]) == katom ||
		    kbase_jd_katom_dep_atom(&kctx->jctx.atoms[i].dep[1]) == katom) {
			struct kbase_jd_atom *dep_atom = &kctx->jctx.atoms[i];

			if ((dep_atom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE) ==
						     BASE_JD_REQ_SOFT_REPLAY &&
			    (dep_atom->core_req & kbdev->force_replay_core_req)
					     == kbdev->force_replay_core_req) {
				jd_force_failure(kbdev, katom);
				return;
			}
		}
	}
}
#endif

/**
 * is_dep_valid - Validate that a dependency is valid for early dependency
 *                submission
 * @katom: Dependency atom to validate
 *
 * A dependency is valid if any of the following are true :
 * - It does not exist (a non-existent dependency does not block submission)
 * - It is in the job scheduler
 * - It has completed, does not have a failure event code, and has not been
 *   marked to fail in the future
 *
 * Return: true if valid, false otherwise
 */
static bool is_dep_valid(struct kbase_jd_atom *katom)
{
	/* If there's no dependency then this is 'valid' from the perspective of
	 * early dependency submission */
	if (!katom)
		return true;

	/* Dependency must have reached the job scheduler */
	if (katom->status < KBASE_JD_ATOM_STATE_IN_JS)
		return false;

	/* If dependency has completed and has failed or will fail then it is
	 * not valid */
	if (katom->status >= KBASE_JD_ATOM_STATE_HW_COMPLETED &&
			(katom->event_code != BASE_JD_EVENT_DONE ||
			katom->will_fail_event_code))
		return false;

	return true;
}

static void jd_try_submitting_deps(struct list_head *out_list,
		struct kbase_jd_atom *node)
{
	int i;

	for (i = 0; i < 2; i++) {
		struct list_head *pos;

		list_for_each(pos, &node->dep_head[i]) {
			struct kbase_jd_atom *dep_atom = list_entry(pos,
					struct kbase_jd_atom, dep_item[i]);

			if (IS_GPU_ATOM(dep_atom) && !dep_atom->in_jd_list) {
				/*Check if atom deps look sane*/
				bool dep0_valid = is_dep_valid(
						dep_atom->dep[0].atom);
				bool dep1_valid = is_dep_valid(
						dep_atom->dep[1].atom);
				bool dep_satisfied = true;
#ifdef CONFIG_MALI_DMA_FENCE
				int dep_count;

				dep_count = kbase_fence_dep_count_read(
								dep_atom);
				if (likely(dep_count == -1)) {
					dep_satisfied = true;
				} else {
				/*
				 * There are either still active callbacks, or
				 * all fences for this @dep_atom has signaled,
				 * but the worker that will queue the atom has
				 * not yet run.
				 *
				 * Wait for the fences to signal and the fence
				 * worker to run and handle @dep_atom. If
				 * @dep_atom was completed due to error on
				 * @katom, then the fence worker will pick up
				 * the complete status and error code set on
				 * @dep_atom above.
				 */
					dep_satisfied = false;
				}
#endif /* CONFIG_MALI_DMA_FENCE */
#ifdef CONFIG_KDS
				dep_satisfied = dep_satisfied &&
						dep_atom->kds_dep_satisfied;
#endif

				if (dep0_valid && dep1_valid && dep_satisfied) {
					dep_atom->in_jd_list = true;
					list_add(&dep_atom->jd_item, out_list);
				}
			}
		}
	}
}

/*
 * Perform the necessary handling of an atom that has finished running
 * on the GPU.
 *
 * Note that if this is a soft-job that has had kbase_prepare_soft_job called on it then the caller
 * is responsible for calling kbase_finish_soft_job *before* calling this function.
 *
 * The caller must hold the kbase_jd_context.lock.
 */
bool jd_done_nolock(struct kbase_jd_atom *katom,
		struct list_head *completed_jobs_ctx)
{
	struct kbase_context *kctx = katom->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	struct list_head completed_jobs;
	struct list_head runnable_jobs;
	bool need_to_try_schedule_context = false;
	int i;

	INIT_LIST_HEAD(&completed_jobs);
	INIT_LIST_HEAD(&runnable_jobs);

	KBASE_DEBUG_ASSERT(katom->status != KBASE_JD_ATOM_STATE_UNUSED);

#if MALI_CUSTOMER_RELEASE == 0
	jd_check_force_failure(katom);
#endif

	/* This is needed in case an atom is failed due to being invalid, this
	 * can happen *before* the jobs that the atom depends on have completed */
	for (i = 0; i < 2; i++) {
		if (kbase_jd_katom_dep_atom(&katom->dep[i])) {
			list_del(&katom->dep_item[i]);
			kbase_jd_katom_dep_clear(&katom->dep[i]);
		}
	}

	/* With PRLAM-10817 or PRLAM-10959 the last tile of a fragment job being soft-stopped can fail with
	 * BASE_JD_EVENT_TILE_RANGE_FAULT.
	 *
	 * So here if the fragment job failed with TILE_RANGE_FAULT and it has been soft-stopped, then we promote the
	 * error code to BASE_JD_EVENT_DONE
	 */

	if ((kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_10817) || kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_10959)) &&
		  katom->event_code == BASE_JD_EVENT_TILE_RANGE_FAULT) {
		if ((katom->core_req & BASE_JD_REQ_FS) && (katom->atom_flags & KBASE_KATOM_FLAG_BEEN_SOFT_STOPPPED)) {
			/* Promote the failure to job done */
			katom->event_code = BASE_JD_EVENT_DONE;
			katom->atom_flags = katom->atom_flags & (~KBASE_KATOM_FLAG_BEEN_SOFT_STOPPPED);
		}
	}

	katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
	list_add_tail(&katom->jd_item, &completed_jobs);

	while (!list_empty(&completed_jobs)) {
		katom = list_entry(completed_jobs.prev, struct kbase_jd_atom, jd_item);
		list_del(completed_jobs.prev);
		KBASE_DEBUG_ASSERT(katom->status == KBASE_JD_ATOM_STATE_COMPLETED);

		for (i = 0; i < 2; i++)
			jd_resolve_dep(&runnable_jobs, katom, i,
					kbase_ctx_flag(kctx, KCTX_DYING));

		if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES)
			kbase_jd_post_external_resources(katom);

		while (!list_empty(&runnable_jobs)) {
			struct kbase_jd_atom *node;

			node = list_entry(runnable_jobs.next,
					struct kbase_jd_atom, jd_item);
			list_del(runnable_jobs.next);
			node->in_jd_list = false;

			KBASE_DEBUG_ASSERT(node->status != KBASE_JD_ATOM_STATE_UNUSED);

			if (node->status != KBASE_JD_ATOM_STATE_COMPLETED &&
					!kbase_ctx_flag(kctx, KCTX_DYING)) {
				need_to_try_schedule_context |= jd_run_atom(node);
			} else {
				node->event_code = katom->event_code;

				if ((node->core_req &
					BASE_JD_REQ_SOFT_JOB_TYPE) ==
					BASE_JD_REQ_SOFT_REPLAY) {
					if (kbase_replay_process(node))
						/* Don't complete this atom */
						continue;
				} else if (node->core_req &
							BASE_JD_REQ_SOFT_JOB) {
					/* If this is a fence wait soft job
					 * then remove it from the list of sync
					 * waiters.
					 */
					if (BASE_JD_REQ_SOFT_FENCE_WAIT == node->core_req)
						kbasep_remove_waiting_soft_job(node);

					kbase_finish_soft_job(node);
				}
				node->status = KBASE_JD_ATOM_STATE_COMPLETED;
			}

			if (node->status == KBASE_JD_ATOM_STATE_COMPLETED) {
				list_add_tail(&node->jd_item, &completed_jobs);
			} else if (node->status == KBASE_JD_ATOM_STATE_IN_JS &&
					!node->will_fail_event_code) {
				/* Node successfully submitted, try submitting
				 * dependencies as they may now be representable
				 * in JS */
				jd_try_submitting_deps(&runnable_jobs, node);
			}
		}

		/* Register a completed job as a disjoint event when the GPU
		 * is in a disjoint state (ie. being reset or replaying jobs).
		 */
		kbase_disjoint_event_potential(kctx->kbdev);
		if (completed_jobs_ctx)
			list_add_tail(&katom->jd_item, completed_jobs_ctx);
		else
			kbase_event_post(kctx, katom);

		/* Decrement and check the TOTAL number of jobs. This includes
		 * those not tracked by the scheduler: 'not ready to run' and
		 * 'dependency-only' jobs. */
		if (--kctx->jctx.job_nr == 0)
			wake_up(&kctx->jctx.zero_jobs_wait);	/* All events are safely queued now, and we can signal any waiter
								 * that we've got no more jobs (so we can be safely terminated) */
	}

	return need_to_try_schedule_context;
}

KBASE_EXPORT_TEST_API(jd_done_nolock);

#ifdef CONFIG_GPU_TRACEPOINTS
enum {
	CORE_REQ_DEP_ONLY,
	CORE_REQ_SOFT,
	CORE_REQ_COMPUTE,
	CORE_REQ_FRAGMENT,
	CORE_REQ_VERTEX,
	CORE_REQ_TILER,
	CORE_REQ_FRAGMENT_VERTEX,
	CORE_REQ_FRAGMENT_VERTEX_TILER,
	CORE_REQ_FRAGMENT_TILER,
	CORE_REQ_VERTEX_TILER,
	CORE_REQ_UNKNOWN
};
static const char * const core_req_strings[] = {
	"Dependency Only Job",
	"Soft Job",
	"Compute Shader Job",
	"Fragment Shader Job",
	"Vertex/Geometry Shader Job",
	"Tiler Job",
	"Fragment Shader + Vertex/Geometry Shader Job",
	"Fragment Shader + Vertex/Geometry Shader Job + Tiler Job",
	"Fragment Shader + Tiler Job",
	"Vertex/Geometry Shader Job + Tiler Job",
	"Unknown Job"
};
static const char *kbasep_map_core_reqs_to_string(base_jd_core_req core_req)
{
	if (core_req & BASE_JD_REQ_SOFT_JOB)
		return core_req_strings[CORE_REQ_SOFT];
	if (core_req & BASE_JD_REQ_ONLY_COMPUTE)
		return core_req_strings[CORE_REQ_COMPUTE];
	switch (core_req & (BASE_JD_REQ_FS | BASE_JD_REQ_CS | BASE_JD_REQ_T)) {
	case BASE_JD_REQ_DEP:
		return core_req_strings[CORE_REQ_DEP_ONLY];
	case BASE_JD_REQ_FS:
		return core_req_strings[CORE_REQ_FRAGMENT];
	case BASE_JD_REQ_CS:
		return core_req_strings[CORE_REQ_VERTEX];
	case BASE_JD_REQ_T:
		return core_req_strings[CORE_REQ_TILER];
	case (BASE_JD_REQ_FS | BASE_JD_REQ_CS):
		return core_req_strings[CORE_REQ_FRAGMENT_VERTEX];
	case (BASE_JD_REQ_FS | BASE_JD_REQ_T):
		return core_req_strings[CORE_REQ_FRAGMENT_TILER];
	case (BASE_JD_REQ_CS | BASE_JD_REQ_T):
		return core_req_strings[CORE_REQ_VERTEX_TILER];
	case (BASE_JD_REQ_FS | BASE_JD_REQ_CS | BASE_JD_REQ_T):
		return core_req_strings[CORE_REQ_FRAGMENT_VERTEX_TILER];
	}
	return core_req_strings[CORE_REQ_UNKNOWN];
}
#endif

bool jd_submit_atom(struct kbase_context *kctx, const struct base_jd_atom_v2 *user_atom, struct kbase_jd_atom *katom)
{
	struct kbase_jd_context *jctx = &kctx->jctx;
	int queued = 0;
	int i;
	int sched_prio;
	bool ret;
	bool will_fail = false;

	/* Update the TOTAL number of jobs. This includes those not tracked by
	 * the scheduler: 'not ready to run' and 'dependency-only' jobs. */
	jctx->job_nr++;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	katom->start_timestamp.tv64 = 0;
#else
	katom->start_timestamp = 0;
#endif
	katom->udata = user_atom->udata;
	katom->kctx = kctx;
	katom->nr_extres = user_atom->nr_extres;
	katom->extres = NULL;
	katom->device_nr = user_atom->device_nr;
	katom->affinity = 0;
	katom->jc = user_atom->jc;
	katom->coreref_state = KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED;
	katom->core_req = user_atom->core_req;
	katom->atom_flags = 0;
	katom->retry_count = 0;
	katom->need_cache_flush_cores_retained = 0;
	katom->pre_dep = NULL;
	katom->post_dep = NULL;
	katom->x_pre_dep = NULL;
	katom->x_post_dep = NULL;
	katom->will_fail_event_code = BASE_JD_EVENT_NOT_STARTED;

	/* Implicitly sets katom->protected_state.enter as well. */
	katom->protected_state.exit = KBASE_ATOM_EXIT_PROTECTED_CHECK;

	katom->age = kctx->age_count++;

	INIT_LIST_HEAD(&katom->jd_item);
#ifdef CONFIG_KDS
	/* Start by assuming that the KDS dependencies are satisfied,
	 * kbase_jd_pre_external_resources will correct this if there are dependencies */
	katom->kds_dep_satisfied = true;
	katom->kds_rset = NULL;
#endif				/* CONFIG_KDS */
#ifdef CONFIG_MALI_DMA_FENCE
	kbase_fence_dep_count_set(katom, -1);
#endif

	/* Don't do anything if there is a mess up with dependencies.
	   This is done in a separate cycle to check both the dependencies at ones, otherwise
	   it will be extra complexity to deal with 1st dependency ( just added to the list )
	   if only the 2nd one has invalid config.
	 */
	for (i = 0; i < 2; i++) {
		int dep_atom_number = user_atom->pre_dep[i].atom_id;
		base_jd_dep_type dep_atom_type = user_atom->pre_dep[i].dependency_type;

		if (dep_atom_number) {
			if (dep_atom_type != BASE_JD_DEP_TYPE_ORDER &&
					dep_atom_type != BASE_JD_DEP_TYPE_DATA) {
				katom->event_code = BASE_JD_EVENT_JOB_CONFIG_FAULT;
				katom->status = KBASE_JD_ATOM_STATE_COMPLETED;

				/* Wrong dependency setup. Atom will be sent
				 * back to user space. Do not record any
				 * dependencies. */
				KBASE_TLSTREAM_TL_NEW_ATOM(
						katom,
						kbase_jd_atom_id(kctx, katom));
				KBASE_TLSTREAM_TL_RET_ATOM_CTX(
						katom, kctx);
				KBASE_TLSTREAM_TL_ATTRIB_ATOM_STATE(katom,
						TL_ATOM_STATE_IDLE);

				ret = jd_done_nolock(katom, NULL);
				goto out;
			}
		}
	}

	/* Add dependencies */
	for (i = 0; i < 2; i++) {
		int dep_atom_number = user_atom->pre_dep[i].atom_id;
		base_jd_dep_type dep_atom_type;
		struct kbase_jd_atom *dep_atom = &jctx->atoms[dep_atom_number];

		dep_atom_type = user_atom->pre_dep[i].dependency_type;
		kbase_jd_katom_dep_clear(&katom->dep[i]);

		if (!dep_atom_number)
			continue;

		if (dep_atom->status == KBASE_JD_ATOM_STATE_UNUSED ||
				dep_atom->status == KBASE_JD_ATOM_STATE_COMPLETED) {

			if (dep_atom->event_code == BASE_JD_EVENT_DONE)
				continue;
			/* don't stop this atom if it has an order dependency
			 * only to the failed one, try to submit it through
			 * the normal path
			 */
			if (dep_atom_type == BASE_JD_DEP_TYPE_ORDER &&
					dep_atom->event_code > BASE_JD_EVENT_ACTIVE) {
				continue;
			}

			/* Atom has completed, propagate the error code if any */
			katom->event_code = dep_atom->event_code;
			katom->status = KBASE_JD_ATOM_STATE_QUEUED;

			/* This atom is going through soft replay or
			 * will be sent back to user space. Do not record any
			 * dependencies. */
			KBASE_TLSTREAM_TL_NEW_ATOM(
					katom,
					kbase_jd_atom_id(kctx, katom));
			KBASE_TLSTREAM_TL_RET_ATOM_CTX(katom, kctx);
			KBASE_TLSTREAM_TL_ATTRIB_ATOM_STATE(katom,
					TL_ATOM_STATE_IDLE);

			if ((katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE)
					 == BASE_JD_REQ_SOFT_REPLAY) {
				if (kbase_replay_process(katom)) {
					ret = false;
					goto out;
				}
			}
			will_fail = true;

		} else {
			/* Atom is in progress, add this atom to the list */
			list_add_tail(&katom->dep_item[i], &dep_atom->dep_head[i]);
			kbase_jd_katom_dep_set(&katom->dep[i], dep_atom, dep_atom_type);
			queued = 1;
		}
	}

	if (will_fail) {
		if (!queued) {
			ret = jd_done_nolock(katom, NULL);

			goto out;
		} else {
			katom->will_fail_event_code = katom->event_code;
			ret = false;

			goto out;
		}
	} else {
		/* These must occur after the above loop to ensure that an atom
		 * that depends on a previous atom with the same number behaves
		 * as expected */
		katom->event_code = BASE_JD_EVENT_DONE;
		katom->status = KBASE_JD_ATOM_STATE_QUEUED;
	}

	/* For invalid priority, be most lenient and choose the default */
	sched_prio = kbasep_js_atom_prio_to_sched_prio(user_atom->prio);
	if (sched_prio == KBASE_JS_ATOM_SCHED_PRIO_INVALID)
		sched_prio = KBASE_JS_ATOM_SCHED_PRIO_DEFAULT;
	katom->sched_priority = sched_prio;

	/* Create a new atom recording all dependencies it was set up with. */
	KBASE_TLSTREAM_TL_NEW_ATOM(
			katom,
			kbase_jd_atom_id(kctx, katom));
	KBASE_TLSTREAM_TL_ATTRIB_ATOM_STATE(katom, TL_ATOM_STATE_IDLE);
	KBASE_TLSTREAM_TL_ATTRIB_ATOM_PRIORITY(katom, katom->sched_priority);
	KBASE_TLSTREAM_TL_RET_ATOM_CTX(katom, kctx);
	for (i = 0; i < 2; i++)
		if (BASE_JD_DEP_TYPE_INVALID != kbase_jd_katom_dep_type(
					&katom->dep[i])) {
			KBASE_TLSTREAM_TL_DEP_ATOM_ATOM(
					(void *)kbase_jd_katom_dep_atom(
						&katom->dep[i]),
					(void *)katom);
		} else if (BASE_JD_DEP_TYPE_INVALID !=
				user_atom->pre_dep[i].dependency_type) {
			/* Resolved dependency. */
			int dep_atom_number =
				user_atom->pre_dep[i].atom_id;
			struct kbase_jd_atom *dep_atom =
				&jctx->atoms[dep_atom_number];

			KBASE_TLSTREAM_TL_RDEP_ATOM_ATOM(
					(void *)dep_atom,
					(void *)katom);
		}

	/* Reject atoms with job chain = NULL, as these cause issues with soft-stop */
	if (!katom->jc && (katom->core_req & BASE_JD_REQ_ATOM_TYPE) != BASE_JD_REQ_DEP) {
		dev_warn(kctx->kbdev->dev, "Rejecting atom with jc = NULL");
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		ret = jd_done_nolock(katom, NULL);
		goto out;
	}

	/* Reject atoms with an invalid device_nr */
	if ((katom->core_req & BASE_JD_REQ_SPECIFIC_COHERENT_GROUP) &&
	    (katom->device_nr >= kctx->kbdev->gpu_props.num_core_groups)) {
		dev_warn(kctx->kbdev->dev,
				"Rejecting atom with invalid device_nr %d",
				katom->device_nr);
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		ret = jd_done_nolock(katom, NULL);
		goto out;
	}

	/* Reject atoms with invalid core requirements */
	if ((katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES) &&
			(katom->core_req & BASE_JD_REQ_EVENT_COALESCE)) {
		dev_warn(kctx->kbdev->dev,
				"Rejecting atom with invalid core requirements");
		katom->event_code = BASE_JD_EVENT_JOB_INVALID;
		katom->core_req &= ~BASE_JD_REQ_EVENT_COALESCE;
		ret = jd_done_nolock(katom, NULL);
		goto out;
	}

	if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES) {
		/* handle what we need to do to access the external resources */
		if (kbase_jd_pre_external_resources(katom, user_atom) != 0) {
			/* setup failed (no access, bad resource, unknown resource types, etc.) */
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
			ret = jd_done_nolock(katom, NULL);
			goto out;
		}
	}

	/* Validate the atom. Function will return error if the atom is
	 * malformed.
	 *
	 * Soft-jobs never enter the job scheduler but have their own initialize method.
	 *
	 * If either fail then we immediately complete the atom with an error.
	 */
	if ((katom->core_req & BASE_JD_REQ_SOFT_JOB) == 0) {
		if (!kbase_js_is_atom_valid(kctx->kbdev, katom)) {
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
			ret = jd_done_nolock(katom, NULL);
			goto out;
		}
	} else {
		/* Soft-job */
		if (kbase_prepare_soft_job(katom) != 0) {
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
			ret = jd_done_nolock(katom, NULL);
			goto out;
		}
	}

#ifdef CONFIG_GPU_TRACEPOINTS
	katom->work_id = atomic_inc_return(&jctx->work_id);
	trace_gpu_job_enqueue((u32)kctx->id, katom->work_id,
			kbasep_map_core_reqs_to_string(katom->core_req));
#endif

	if (queued && !IS_GPU_ATOM(katom)) {
		ret = false;
		goto out;
	}
#ifdef CONFIG_KDS
	if (!katom->kds_dep_satisfied) {
		/* Queue atom due to KDS dependency */
		ret = false;
		goto out;
	}
#endif				/* CONFIG_KDS */


#ifdef CONFIG_MALI_DMA_FENCE
	if (kbase_fence_dep_count_read(katom) != -1) {
		ret = false;
		goto out;
	}
#endif /* CONFIG_MALI_DMA_FENCE */

	if ((katom->core_req & BASE_JD_REQ_SOFT_JOB_TYPE)
						  == BASE_JD_REQ_SOFT_REPLAY) {
		if (kbase_replay_process(katom))
			ret = false;
		else
			ret = jd_done_nolock(katom, NULL);

		goto out;
	} else if (katom->core_req & BASE_JD_REQ_SOFT_JOB) {
		if (kbase_process_soft_job(katom) == 0) {
			kbase_finish_soft_job(katom);
			ret = jd_done_nolock(katom, NULL);
			goto out;
		}

		ret = false;
	} else if ((katom->core_req & BASE_JD_REQ_ATOM_TYPE) != BASE_JD_REQ_DEP) {
		katom->status = KBASE_JD_ATOM_STATE_IN_JS;
		ret = kbasep_js_add_job(kctx, katom);
		/* If job was cancelled then resolve immediately */
		if (katom->event_code == BASE_JD_EVENT_JOB_CANCELLED)
			ret = jd_done_nolock(katom, NULL);
	} else {
		/* This is a pure dependency. Resolve it immediately */
		ret = jd_done_nolock(katom, NULL);
	}

 out:
	return ret;
}

int kbase_jd_submit(struct kbase_context *kctx,
		void __user *user_addr, u32 nr_atoms, u32 stride,
		bool uk6_atom)
{
	struct kbase_jd_context *jctx = &kctx->jctx;
	int err = 0;
	int i;
	bool need_to_try_schedule_context = false;
	struct kbase_device *kbdev;
	u32 latest_flush;

	/*
	 * kbase_jd_submit isn't expected to fail and so all errors with the
	 * jobs are reported by immediately failing them (through event system)
	 */
	kbdev = kctx->kbdev;

	beenthere(kctx, "%s", "Enter");

	if (kbase_ctx_flag(kctx, KCTX_SUBMIT_DISABLED)) {
		dev_err(kbdev->dev, "Attempt to submit to a context that has SUBMIT_DISABLED set on it");
		return -EINVAL;
	}

	if (stride != sizeof(base_jd_atom_v2)) {
		dev_err(kbdev->dev, "Stride passed to job_submit doesn't match kernel");
		return -EINVAL;
	}

	KBASE_TIMELINE_ATOMS_IN_FLIGHT(kctx, atomic_add_return(nr_atoms,
				&kctx->timeline.jd_atoms_in_flight));

	/* All atoms submitted in this call have the same flush ID */
	latest_flush = kbase_backend_get_current_flush_id(kbdev);

	for (i = 0; i < nr_atoms; i++) {
		struct base_jd_atom_v2 user_atom;
		struct kbase_jd_atom *katom;

#ifdef BASE_LEGACY_UK6_SUPPORT
		BUILD_BUG_ON(sizeof(struct base_jd_atom_v2_uk6) !=
				sizeof(base_jd_atom_v2));

		if (uk6_atom) {
			struct base_jd_atom_v2_uk6 user_atom_v6;
			base_jd_dep_type dep_types[2] = {BASE_JD_DEP_TYPE_DATA, BASE_JD_DEP_TYPE_DATA};

			if (copy_from_user(&user_atom_v6, user_addr,
					sizeof(user_atom_v6))) {
				err = -EINVAL;
				KBASE_TIMELINE_ATOMS_IN_FLIGHT(kctx,
					atomic_sub_return(
					nr_atoms - i,
					&kctx->timeline.jd_atoms_in_flight));
				break;
			}
			/* Convert from UK6 atom format to UK7 format */
			user_atom.jc = user_atom_v6.jc;
			user_atom.udata = user_atom_v6.udata;
			user_atom.extres_list = user_atom_v6.extres_list;
			user_atom.nr_extres = user_atom_v6.nr_extres;
			user_atom.core_req = (u32)(user_atom_v6.core_req & 0x7fff);

			/* atom number 0 is used for no dependency atoms */
			if (!user_atom_v6.pre_dep[0])
				dep_types[0] = BASE_JD_DEP_TYPE_INVALID;

			base_jd_atom_dep_set(&user_atom.pre_dep[0],
					user_atom_v6.pre_dep[0],
					dep_types[0]);

			/* atom number 0 is used for no dependency atoms */
			if (!user_atom_v6.pre_dep[1])
				dep_types[1] = BASE_JD_DEP_TYPE_INVALID;

			base_jd_atom_dep_set(&user_atom.pre_dep[1],
					user_atom_v6.pre_dep[1],
					dep_types[1]);

			user_atom.atom_number = user_atom_v6.atom_number;
			user_atom.prio = user_atom_v6.prio;
			user_atom.device_nr = user_atom_v6.device_nr;
		} else {
#endif /* BASE_LEGACY_UK6_SUPPORT */
			if (copy_from_user(&user_atom, user_addr,
						sizeof(user_atom)) != 0) {
				err = -EINVAL;
				KBASE_TIMELINE_ATOMS_IN_FLIGHT(kctx,
					atomic_sub_return(nr_atoms - i,
					&kctx->timeline.jd_atoms_in_flight));
				break;
			}
#ifdef BASE_LEGACY_UK6_SUPPORT
		}
#endif

#ifdef BASE_LEGACY_UK10_2_SUPPORT
		if (KBASE_API_VERSION(10, 3) > kctx->api_version)
			user_atom.core_req = (u32)(user_atom.compat_core_req
					      & 0x7fff);
#endif /* BASE_LEGACY_UK10_2_SUPPORT */

		user_addr = (void __user *)((uintptr_t) user_addr + stride);

		mutex_lock(&jctx->lock);
#ifndef compiletime_assert
#define compiletime_assert_defined
#define compiletime_assert(x, msg) do { switch (0) { case 0: case (x):; } } \
while (false)
#endif
		compiletime_assert((1 << (8*sizeof(user_atom.atom_number))) >=
					BASE_JD_ATOM_COUNT,
			"BASE_JD_ATOM_COUNT and base_atom_id type out of sync");
		compiletime_assert(sizeof(user_atom.pre_dep[0].atom_id) ==
					sizeof(user_atom.atom_number),
			"BASE_JD_ATOM_COUNT and base_atom_id type out of sync");
#ifdef compiletime_assert_defined
#undef compiletime_assert
#undef compiletime_assert_defined
#endif
		if (user_atom.atom_number >= BASE_JD_ATOM_COUNT) {
			err = -EINVAL;
			break;
		}
		user_atom.atom_number =
			array_index_nospec(user_atom.atom_number,
					   BASE_JD_ATOM_COUNT);
		katom = &jctx->atoms[user_atom.atom_number];

		/* Record the flush ID for the cache flush optimisation */
		katom->flush_id = latest_flush;

		while (katom->status != KBASE_JD_ATOM_STATE_UNUSED) {
			/* Atom number is already in use, wait for the atom to
			 * complete
			 */
			mutex_unlock(&jctx->lock);

			/* This thread will wait for the atom to complete. Due
			 * to thread scheduling we are not sure that the other
			 * thread that owns the atom will also schedule the
			 * context, so we force the scheduler to be active and
			 * hence eventually schedule this context at some point
			 * later.
			 */
			kbase_js_sched_all(kbdev);

			if (wait_event_killable(katom->completed,
					katom->status ==
					KBASE_JD_ATOM_STATE_UNUSED) != 0) {
				/* We're being killed so the result code
				 * doesn't really matter
				 */
				return 0;
			}
			mutex_lock(&jctx->lock);
		}

		need_to_try_schedule_context |=
				       jd_submit_atom(kctx, &user_atom, katom);

		/* Register a completed job as a disjoint event when the GPU is in a disjoint state
		 * (ie. being reset or replaying jobs).
		 */
		kbase_disjoint_event_potential(kbdev);

		mutex_unlock(&jctx->lock);
	}

	if (need_to_try_schedule_context)
		kbase_js_sched_all(kbdev);

	return err;
}

KBASE_EXPORT_TEST_API(kbase_jd_submit);

void kbase_jd_done_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom = container_of(data, struct kbase_jd_atom, work);
	struct kbase_jd_context *jctx;
	struct kbase_context *kctx;
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbase_device *kbdev;
	struct kbasep_js_device_data *js_devdata;
	u64 cache_jc = katom->jc;
	struct kbasep_js_atom_retained_state katom_retained_state;
	bool context_idle;
	base_jd_core_req core_req = katom->core_req;
	u64 affinity = katom->affinity;
	enum kbase_atom_coreref_state coreref_state = katom->coreref_state;

	/* Soft jobs should never reach this function */
	KBASE_DEBUG_ASSERT((katom->core_req & BASE_JD_REQ_SOFT_JOB) == 0);

	kctx = katom->kctx;
	jctx = &kctx->jctx;
	kbdev = kctx->kbdev;
	js_kctx_info = &kctx->jctx.sched_info;
	js_devdata = &kbdev->js_data;

	KBASE_TRACE_ADD(kbdev, JD_DONE_WORKER, kctx, katom, katom->jc, 0);

	kbase_backend_complete_wq(kbdev, katom);

	/*
	 * Begin transaction on JD context and JS context
	 */
	mutex_lock(&jctx->lock);
	KBASE_TLSTREAM_TL_ATTRIB_ATOM_STATE(katom, TL_ATOM_STATE_DONE);
	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);

	/* This worker only gets called on contexts that are scheduled *in*. This is
	 * because it only happens in response to an IRQ from a job that was
	 * running.
	 */
	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	if (katom->event_code == BASE_JD_EVENT_STOPPED) {
		/* Atom has been promoted to stopped */
		unsigned long flags;

		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		katom->status = KBASE_JD_ATOM_STATE_IN_JS;
		kbase_js_unpull(kctx, katom);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&jctx->lock);

		return;
	}

	if (katom->event_code != BASE_JD_EVENT_DONE)
		dev_err(kbdev->dev,
			"t6xx: GPU fault 0x%02lx from job slot %d\n",
					(unsigned long)katom->event_code,
								katom->slot_nr);

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8316))
		kbase_as_poking_timer_release_atom(kbdev, kctx, katom);

	/* Retain state before the katom disappears */
	kbasep_js_atom_retained_state_copy(&katom_retained_state, katom);

	context_idle = kbase_js_complete_atom_wq(kctx, katom);

	KBASE_DEBUG_ASSERT(kbasep_js_has_atom_finished(&katom_retained_state));

	kbasep_js_remove_job(kbdev, kctx, katom);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_unlock(&js_devdata->queue_mutex);
	katom->atom_flags &= ~KBASE_KATOM_FLAG_HOLDING_CTX_REF;
	/* jd_done_nolock() requires the jsctx_mutex lock to be dropped */
	jd_done_nolock(katom, &kctx->completed_jobs);

	/* katom may have been freed now, do not use! */

	if (context_idle) {
		unsigned long flags;

		context_idle = false;
		mutex_lock(&js_devdata->queue_mutex);
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

		/* If kbase_sched() has scheduled this context back in then
		 * KCTX_ACTIVE will have been set after we marked it as
		 * inactive, and another pm reference will have been taken, so
		 * drop our reference. But do not call kbase_jm_idle_ctx(), as
		 * the context is active and fast-starting is allowed.
		 *
		 * If an atom has been fast-started then kctx->atoms_pulled will
		 * be non-zero but KCTX_ACTIVE will still be false (as the
		 * previous pm reference has been inherited). Do NOT drop our
		 * reference, as it has been re-used, and leave the context as
		 * active.
		 *
		 * If no new atoms have been started then KCTX_ACTIVE will still
		 * be false and atoms_pulled will be zero, so drop the reference
		 * and call kbase_jm_idle_ctx().
		 *
		 * As the checks are done under both the queue_mutex and
		 * hwaccess_lock is should be impossible for this to race
		 * with the scheduler code.
		 */
		if (kbase_ctx_flag(kctx, KCTX_ACTIVE) ||
		    !atomic_read(&kctx->atoms_pulled)) {
			/* Calling kbase_jm_idle_ctx() here will ensure that
			 * atoms are not fast-started when we drop the
			 * hwaccess_lock. This is not performed if
			 * KCTX_ACTIVE is set as in that case another pm
			 * reference has been taken and a fast-start would be
			 * valid.
			 */
			if (!kbase_ctx_flag(kctx, KCTX_ACTIVE))
				kbase_jm_idle_ctx(kbdev, kctx);
			context_idle = true;
		} else {
			kbase_ctx_flag_set(kctx, KCTX_ACTIVE);
		}
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		mutex_unlock(&js_devdata->queue_mutex);
	}

	/*
	 * Transaction complete
	 */
	mutex_unlock(&jctx->lock);

	/* Job is now no longer running, so can now safely release the context
	 * reference, and handle any actions that were logged against the atom's retained state */

	kbasep_js_runpool_release_ctx_and_katom_retained_state(kbdev, kctx, &katom_retained_state);

	kbase_js_sched_all(kbdev);

	if (!atomic_dec_return(&kctx->work_count)) {
		/* If worker now idle then post all events that jd_done_nolock()
		 * has queued */
		mutex_lock(&jctx->lock);
		while (!list_empty(&kctx->completed_jobs)) {
			struct kbase_jd_atom *atom = list_entry(
					kctx->completed_jobs.next,
					struct kbase_jd_atom, jd_item);
			list_del(kctx->completed_jobs.next);

			kbase_event_post(kctx, atom);
		}
		mutex_unlock(&jctx->lock);
	}

	kbase_backend_complete_wq_post_sched(kbdev, core_req, affinity,
			coreref_state);

	if (context_idle)
		kbase_pm_context_idle(kbdev);

	KBASE_TRACE_ADD(kbdev, JD_DONE_WORKER_END, kctx, NULL, cache_jc, 0);
}

/**
 * jd_cancel_worker - Work queue job cancel function.
 * @data: a &struct work_struct
 *
 * Only called as part of 'Zapping' a context (which occurs on termination).
 * Operates serially with the kbase_jd_done_worker() on the work queue.
 *
 * This can only be called on contexts that aren't scheduled.
 *
 * We don't need to release most of the resources that would occur on
 * kbase_jd_done() or kbase_jd_done_worker(), because the atoms here must not be
 * running (by virtue of only being called on contexts that aren't
 * scheduled).
 */
static void jd_cancel_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom = container_of(data, struct kbase_jd_atom, work);
	struct kbase_jd_context *jctx;
	struct kbase_context *kctx;
	struct kbasep_js_kctx_info *js_kctx_info;
	bool need_to_try_schedule_context;
	bool attr_state_changed;
	struct kbase_device *kbdev;

	/* Soft jobs should never reach this function */
	KBASE_DEBUG_ASSERT((katom->core_req & BASE_JD_REQ_SOFT_JOB) == 0);

	kctx = katom->kctx;
	kbdev = kctx->kbdev;
	jctx = &kctx->jctx;
	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_TRACE_ADD(kbdev, JD_CANCEL_WORKER, kctx, katom, katom->jc, 0);

	/* This only gets called on contexts that are scheduled out. Hence, we must
	 * make sure we don't de-ref the number of running jobs (there aren't
	 * any), nor must we try to schedule out the context (it's already
	 * scheduled out).
	 */
	KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	/* Scheduler: Remove the job from the system */
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	attr_state_changed = kbasep_js_remove_cancelled_job(kbdev, kctx, katom);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	mutex_lock(&jctx->lock);

	need_to_try_schedule_context = jd_done_nolock(katom, NULL);
	/* Because we're zapping, we're not adding any more jobs to this ctx, so no need to
	 * schedule the context. There's also no need for the jsctx_mutex to have been taken
	 * around this too. */
	KBASE_DEBUG_ASSERT(!need_to_try_schedule_context);

	/* katom may have been freed now, do not use! */
	mutex_unlock(&jctx->lock);

	if (attr_state_changed)
		kbase_js_sched_all(kbdev);
}

/**
 * kbase_jd_done - Complete a job that has been removed from the Hardware
 * @katom: atom which has been completed
 * @slot_nr: slot the atom was on
 * @end_timestamp: completion time
 * @done_code: completion code
 *
 * This must be used whenever a job has been removed from the Hardware, e.g.:
 * An IRQ indicates that the job finished (for both error and 'done' codes), or
 * the job was evicted from the JS_HEAD_NEXT registers during a Soft/Hard stop.
 *
 * Some work is carried out immediately, and the rest is deferred onto a
 * workqueue
 *
 * Context:
 *   This can be called safely from atomic context.
 *   The caller must hold kbdev->hwaccess_lock
 */
void kbase_jd_done(struct kbase_jd_atom *katom, int slot_nr,
		ktime_t *end_timestamp, kbasep_js_atom_done_code done_code)
{
	struct kbase_context *kctx;
	struct kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(katom);
	kctx = katom->kctx;
	KBASE_DEBUG_ASSERT(kctx);
	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev);

	if (done_code & KBASE_JS_ATOM_DONE_EVICTED_FROM_NEXT)
		katom->event_code = BASE_JD_EVENT_REMOVED_FROM_NEXT;

	KBASE_TRACE_ADD(kbdev, JD_DONE, kctx, katom, katom->jc, 0);

	kbase_job_check_leave_disjoint(kbdev, katom);

	katom->slot_nr = slot_nr;

	atomic_inc(&kctx->work_count);

#ifdef CONFIG_DEBUG_FS
	/* a failed job happened and is waiting for dumping*/
	if (!katom->will_fail_event_code &&
			kbase_debug_job_fault_process(katom, katom->event_code))
		return;
#endif

	WARN_ON(work_pending(&katom->work));
	KBASE_DEBUG_ASSERT(0 == object_is_on_stack(&katom->work));
	INIT_WORK(&katom->work, kbase_jd_done_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}

KBASE_EXPORT_TEST_API(kbase_jd_done);

void kbase_jd_cancel(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	KBASE_DEBUG_ASSERT(NULL != katom);
	kctx = katom->kctx;
	KBASE_DEBUG_ASSERT(NULL != kctx);

	KBASE_TRACE_ADD(kbdev, JD_CANCEL, kctx, katom, katom->jc, 0);

	/* This should only be done from a context that is not scheduled */
	KBASE_DEBUG_ASSERT(!kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	WARN_ON(work_pending(&katom->work));

	katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;

	KBASE_DEBUG_ASSERT(0 == object_is_on_stack(&katom->work));
	INIT_WORK(&katom->work, jd_cancel_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}


void kbase_jd_zap_context(struct kbase_context *kctx)
{
	struct kbase_jd_atom *katom;
	struct list_head *entry, *tmp;
	struct kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(kctx);

	kbdev = kctx->kbdev;

	KBASE_TRACE_ADD(kbdev, JD_ZAP_CONTEXT, kctx, NULL, 0u, 0u);

	kbase_js_zap_context(kctx);

	mutex_lock(&kctx->jctx.lock);

	/*
	 * While holding the struct kbase_jd_context lock clean up jobs which are known to kbase but are
	 * queued outside the job scheduler.
	 */

	del_timer_sync(&kctx->soft_job_timeout);
	list_for_each_safe(entry, tmp, &kctx->waiting_soft_jobs) {
		katom = list_entry(entry, struct kbase_jd_atom, queue);
		kbase_cancel_soft_job(katom);
	}


#ifdef CONFIG_KDS

	/* For each job waiting on a kds resource, cancel the wait and force the job to
	 * complete early, this is done so that we don't leave jobs outstanding waiting
	 * on kds resources which may never be released when contexts are zapped, resulting
	 * in a hang.
	 *
	 * Note that we can safely iterate over the list as the struct kbase_jd_context lock is held,
	 * this prevents items being removed when calling job_done_nolock in kbase_cancel_kds_wait_job.
	 */

	list_for_each(entry, &kctx->waiting_kds_resource) {
		katom = list_entry(entry, struct kbase_jd_atom, node);

		kbase_cancel_kds_wait_job(katom);
	}
#endif

#ifdef CONFIG_MALI_DMA_FENCE
	kbase_dma_fence_cancel_all_atoms(kctx);
#endif

	mutex_unlock(&kctx->jctx.lock);

#ifdef CONFIG_MALI_DMA_FENCE
	/* Flush dma-fence workqueue to ensure that any callbacks that may have
	 * been queued are done before continuing.
	 */
	flush_workqueue(kctx->dma_fence.wq);
#endif

	kbase_jm_wait_for_zero_jobs(kctx);
}

KBASE_EXPORT_TEST_API(kbase_jd_zap_context);

int kbase_jd_init(struct kbase_context *kctx)
{
	int i;
	int mali_err = 0;
#ifdef CONFIG_KDS
	int err;
#endif				/* CONFIG_KDS */

	KBASE_DEBUG_ASSERT(kctx);

	kctx->jctx.job_done_wq = alloc_workqueue("mali_jd",
			WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (NULL == kctx->jctx.job_done_wq) {
		mali_err = -ENOMEM;
		goto out1;
	}

	for (i = 0; i < BASE_JD_ATOM_COUNT; i++) {
		init_waitqueue_head(&kctx->jctx.atoms[i].completed);

		INIT_LIST_HEAD(&kctx->jctx.atoms[i].dep_head[0]);
		INIT_LIST_HEAD(&kctx->jctx.atoms[i].dep_head[1]);

		/* Catch userspace attempting to use an atom which doesn't exist as a pre-dependency */
		kctx->jctx.atoms[i].event_code = BASE_JD_EVENT_JOB_INVALID;
		kctx->jctx.atoms[i].status = KBASE_JD_ATOM_STATE_UNUSED;

#if defined(CONFIG_MALI_DMA_FENCE) || defined(CONFIG_SYNC_FILE)
		kctx->jctx.atoms[i].dma_fence.context =
						dma_fence_context_alloc(1);
		atomic_set(&kctx->jctx.atoms[i].dma_fence.seqno, 0);
		INIT_LIST_HEAD(&kctx->jctx.atoms[i].dma_fence.callbacks);
#endif
	}

	mutex_init(&kctx->jctx.lock);

	init_waitqueue_head(&kctx->jctx.zero_jobs_wait);

	spin_lock_init(&kctx->jctx.tb_lock);

#ifdef CONFIG_KDS
	err = kds_callback_init(&kctx->jctx.kds_cb, 0, kds_dep_clear);
	if (0 != err) {
		mali_err = -EINVAL;
		goto out2;
	}
#endif				/* CONFIG_KDS */

	kctx->jctx.job_nr = 0;
	INIT_LIST_HEAD(&kctx->completed_jobs);
	atomic_set(&kctx->work_count, 0);

	return 0;

#ifdef CONFIG_KDS
 out2:
	destroy_workqueue(kctx->jctx.job_done_wq);
#endif				/* CONFIG_KDS */
 out1:
	return mali_err;
}

KBASE_EXPORT_TEST_API(kbase_jd_init);

void kbase_jd_exit(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx);

#ifdef CONFIG_KDS
	kds_callback_term(&kctx->jctx.kds_cb);
#endif				/* CONFIG_KDS */
	/* Work queue is emptied by this */
	destroy_workqueue(kctx->jctx.job_done_wq);
}

KBASE_EXPORT_TEST_API(kbase_jd_exit);
