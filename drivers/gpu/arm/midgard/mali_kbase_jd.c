/*
 *
 * (C) COPYRIGHT 2010-2015 ARM Limited. All rights reserved.
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
#ifdef CONFIG_UMP
#include <linux/ump.h>
#endif				/* CONFIG_UMP */
#include <linux/random.h>
#include <linux/version.h>
#include <linux/ratelimit.h>

#include <mali_kbase_jm.h>
#include <mali_kbase_hwaccess_jm.h>

#if defined(CONFIG_MALI_MIPE_ENABLED)
#include <mali_kbase_tlstream.h>
#endif

#define beenthere(kctx, f, a...)  dev_dbg(kctx->kbdev->dev, "%s:" f, __func__, ##a)

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
/* random32 was renamed to prandom_u32 in 3.8 */
#define prandom_u32 random32
#endif

/* Return whether katom will run on the GPU or not. Currently only soft jobs and
 * dependency-only atoms do not run on the GPU */
#define IS_GPU_ATOM(katom) (!((katom->core_req & BASE_JD_REQ_SOFT_JOB) ||  \
			((katom->core_req & BASEP_JD_REQ_ATOM_TYPE) ==    \
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
	if (kctx->is_compat)
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

	if ((katom->core_req & BASEP_JD_REQ_ATOM_TYPE) == BASE_JD_REQ_DEP) {
		/* Dependency only atom */
		katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
		return 0;
	} else if (katom->core_req & BASE_JD_REQ_SOFT_JOB) {
		/* Soft-job */
		if ((katom->core_req & BASEP_JD_REQ_ATOM_TYPE)
						  == BASE_JD_REQ_SOFT_REPLAY) {
			if (!kbase_replay_process(katom))
				katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
		} else if (kbase_process_soft_job(katom) == 0) {
			kbase_finish_soft_job(katom);
			katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
		} else {
			/* The job has not completed */
			list_add_tail(&katom->dep_item[0], &kctx->waiting_soft_jobs);
		}
		return 0;
	}

	katom->status = KBASE_JD_ATOM_STATE_IN_JS;
	/* Queue an action about whether we should try scheduling a context */
	return kbasep_js_add_job(kctx, katom);
}

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
	struct kbase_device *kbdev;

	katom = (struct kbase_jd_atom *)callback_parameter;
	KBASE_DEBUG_ASSERT(katom);
	ctx = &katom->kctx->jctx;
	kbdev = katom->kctx->kbdev;
	KBASE_DEBUG_ASSERT(kbdev);

	mutex_lock(&ctx->lock);

	/* KDS resource has already been satisfied (e.g. due to zapping) */
	if (katom->kds_dep_satisfied)
		goto out;

	/* This atom's KDS dependency has now been met */
	katom->kds_dep_satisfied = true;

	/* Check whether the atom's other dependencies were already met. If
	 * katom is a GPU atom then the job scheduler may be able to represent
	 * the dependencies, hence we may attempt to submit it before they are
	 * met. Other atoms must have had both dependencies resolved */
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
 out:
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

#ifdef CONFIG_DMA_SHARED_BUFFER
static int kbase_jd_umm_map(struct kbase_context *kctx, struct kbase_va_region *reg)
{
	struct sg_table *sgt;
	struct scatterlist *s;
	int i;
	phys_addr_t *pa;
	int err;
	size_t count = 0;
	struct kbase_mem_phy_alloc *alloc;

	alloc = reg->gpu_alloc;

	KBASE_DEBUG_ASSERT(alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM);
	KBASE_DEBUG_ASSERT(NULL == alloc->imported.umm.sgt);
	sgt = dma_buf_map_attachment(alloc->imported.umm.dma_attachment, DMA_BIDIRECTIONAL);

	if (IS_ERR_OR_NULL(sgt))
		return -EINVAL;

	/* save for later */
	alloc->imported.umm.sgt = sgt;

	pa = kbase_get_gpu_phy_pages(reg);
	KBASE_DEBUG_ASSERT(pa);

	for_each_sg(sgt->sgl, s, sgt->nents, i) {
		int j;
		size_t pages = PFN_UP(sg_dma_len(s));

		WARN_ONCE(sg_dma_len(s) & (PAGE_SIZE-1),
		"sg_dma_len(s)=%u is not a multiple of PAGE_SIZE\n",
		sg_dma_len(s));

		WARN_ONCE(sg_dma_address(s) & (PAGE_SIZE-1),
		"sg_dma_address(s)=%llx is not aligned to PAGE_SIZE\n",
		(unsigned long long) sg_dma_address(s));

		for (j = 0; (j < pages) && (count < reg->nr_pages); j++, count++)
			*pa++ = sg_dma_address(s) + (j << PAGE_SHIFT);
		WARN_ONCE(j < pages,
		"sg list from dma_buf_map_attachment > dma_buf->size=%zu\n",
		alloc->imported.umm.dma_buf->size);
	}

	if (WARN_ONCE(count < reg->nr_pages,
			"sg list from dma_buf_map_attachment < dma_buf->size=%zu\n",
			alloc->imported.umm.dma_buf->size)) {
		err = -EINVAL;
		goto out;
	}

	/* Update nents as we now have pages to map */
	alloc->nents = count;

	err = kbase_mmu_insert_pages(kctx, reg->start_pfn, kbase_get_gpu_phy_pages(reg), kbase_reg_current_backed_size(reg), reg->flags | KBASE_REG_GPU_WR | KBASE_REG_GPU_RD);

out:
	if (err) {
		dma_buf_unmap_attachment(alloc->imported.umm.dma_attachment, alloc->imported.umm.sgt, DMA_BIDIRECTIONAL);
		alloc->imported.umm.sgt = NULL;
	}

	return err;
}

static void kbase_jd_umm_unmap(struct kbase_context *kctx, struct kbase_mem_phy_alloc *alloc)
{
	KBASE_DEBUG_ASSERT(kctx);
	KBASE_DEBUG_ASSERT(alloc);
	KBASE_DEBUG_ASSERT(alloc->imported.umm.dma_attachment);
	KBASE_DEBUG_ASSERT(alloc->imported.umm.sgt);
	dma_buf_unmap_attachment(alloc->imported.umm.dma_attachment,
	    alloc->imported.umm.sgt, DMA_BIDIRECTIONAL);
	alloc->imported.umm.sgt = NULL;
	alloc->nents = 0;
}
#endif				/* CONFIG_DMA_SHARED_BUFFER */

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

	kbase_gpu_vm_lock(katom->kctx);
	/* only roll back if extres is non-NULL */
	if (katom->extres) {
		u32 res_no;

		res_no = katom->nr_extres;
		while (res_no-- > 0) {
			struct kbase_mem_phy_alloc *alloc;

			alloc = katom->extres[res_no].alloc;
#ifdef CONFIG_DMA_SHARED_BUFFER
			if (alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM) {
				alloc->imported.umm.current_mapping_usage_count--;

				if (0 == alloc->imported.umm.current_mapping_usage_count) {
					struct kbase_va_region *reg;

					reg = kbase_region_tracker_find_region_base_address(
							katom->kctx,
							katom->extres[res_no].gpu_address);

					if (reg && reg->gpu_alloc == alloc)
						kbase_mmu_teardown_pages(
								katom->kctx,
								reg->start_pfn,
								kbase_reg_current_backed_size(reg));

					kbase_jd_umm_unmap(katom->kctx, alloc);
				}
			}
#endif	/* CONFIG_DMA_SHARED_BUFFER */
			kbase_mem_phy_alloc_put(alloc);
		}
		kfree(katom->extres);
		katom->extres = NULL;
	}
	kbase_gpu_vm_unlock(katom->kctx);
}

#if (defined(CONFIG_KDS) && defined(CONFIG_UMP)) || defined(CONFIG_DMA_SHARED_BUFFER_USES_KDS)
static void add_kds_resource(struct kds_resource *kds_res, struct kds_resource **kds_resources, u32 *kds_res_count, unsigned long *kds_access_bitmap, bool exclusive)
{
	u32 i;

	for (i = 0; i < *kds_res_count; i++) {
		/* Duplicate resource, ignore */
		if (kds_resources[i] == kds_res)
			return;
	}

	kds_resources[*kds_res_count] = kds_res;
	if (exclusive)
		set_bit(*kds_res_count, kds_access_bitmap);
	(*kds_res_count)++;
}
#endif

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

	if (NULL == kds_resources) {
		err_ret_val = -ENOMEM;
		goto early_err_out;
	}

	KBASE_DEBUG_ASSERT(0 != katom->nr_extres);
	kds_access_bitmap = kzalloc(sizeof(unsigned long) * ((katom->nr_extres + BITS_PER_LONG - 1) / BITS_PER_LONG), GFP_KERNEL);

	if (NULL == kds_access_bitmap) {
		err_ret_val = -ENOMEM;
		goto early_err_out;
	}
#endif				/* CONFIG_KDS */

	/* need to keep the GPU VM locked while we set up UMM buffers */
	kbase_gpu_vm_lock(katom->kctx);
	for (res_no = 0; res_no < katom->nr_extres; res_no++) {
		struct base_external_resource *res;
		struct kbase_va_region *reg;

		res = &input_extres[res_no];
		reg = kbase_region_tracker_find_region_enclosing_address(katom->kctx,
				res->ext_resource & ~BASE_EXT_RES_ACCESS_EXCLUSIVE);
		/* did we find a matching region object? */
		if (NULL == reg || (reg->flags & KBASE_REG_FREE)) {
			/* roll back */
			goto failed_loop;
		}

		if (!(katom->core_req & BASE_JD_REQ_SOFT_JOB) &&
				(reg->flags & KBASE_REG_SECURE)) {
			katom->atom_flags |= KBASE_KATOM_FLAG_SECURE;
			if ((katom->core_req & BASE_JD_REQ_FS) == 0) {
				WARN_RATELIMIT(1, "Secure non-fragment jobs not supported");
				goto failed_loop;
			}
		}

		/* decide what needs to happen for this resource */
		switch (reg->gpu_alloc->type) {
		case BASE_MEM_IMPORT_TYPE_UMP:
			{
#if defined(CONFIG_KDS) && defined(CONFIG_UMP)
				struct kds_resource *kds_res;

				kds_res = ump_dd_kds_resource_get(reg->gpu_alloc->imported.ump_handle);
				if (kds_res)
					add_kds_resource(kds_res, kds_resources, &kds_res_count,
							kds_access_bitmap,
							res->ext_resource & BASE_EXT_RES_ACCESS_EXCLUSIVE);
#endif				/*defined(CONFIG_KDS) && defined(CONFIG_UMP) */
				break;
			}
#ifdef CONFIG_DMA_SHARED_BUFFER
		case BASE_MEM_IMPORT_TYPE_UMM:
			{
#ifdef CONFIG_DMA_SHARED_BUFFER_USES_KDS
				struct kds_resource *kds_res;

				kds_res = get_dma_buf_kds_resource(reg->gpu_alloc->imported.umm.dma_buf);
				if (kds_res)
					add_kds_resource(kds_res, kds_resources, &kds_res_count, kds_access_bitmap, res->ext_resource & BASE_EXT_RES_ACCESS_EXCLUSIVE);
#endif
				reg->gpu_alloc->imported.umm.current_mapping_usage_count++;
				if (1 == reg->gpu_alloc->imported.umm.current_mapping_usage_count) {
					/* use a local variable to not pollute err_ret_val
					 * with a potential success value as some other gotos depend
					 * on the default error code stored in err_ret_val */
					int tmp;

					tmp = kbase_jd_umm_map(katom->kctx, reg);
					if (tmp) {
						/* failed to map this buffer, roll back */
						err_ret_val = tmp;
						reg->gpu_alloc->imported.umm.current_mapping_usage_count--;
						goto failed_loop;
					}
				}
				break;
			}
#endif
		default:
			goto failed_loop;
		}

		/* finish with updating out array with the data we found */
		/* NOTE: It is important that this is the last thing we do (or
		 * at least not before the first write) as we overwrite elements
		 * as we loop and could be overwriting ourself, so no writes
		 * until the last read for an element.
		 * */
		katom->extres[res_no].gpu_address = reg->start_pfn << PAGE_SHIFT; /* save the start_pfn (as an address, not pfn) to use fast lookup later */
		katom->extres[res_no].alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);
	}
	/* successfully parsed the extres array */
	/* drop the vm lock before we call into kds */
	kbase_gpu_vm_unlock(katom->kctx);

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

	/* all done OK */
	return 0;

/* error handling section */

#ifdef CONFIG_KDS
 failed_kds_setup:

	/* lock before we unmap */
	kbase_gpu_vm_lock(katom->kctx);
#endif				/* CONFIG_KDS */

 failed_loop:
	/* undo the loop work */
	while (res_no-- > 0) {
		struct kbase_mem_phy_alloc *alloc = katom->extres[res_no].alloc;
#ifdef CONFIG_DMA_SHARED_BUFFER
		if (alloc->type == KBASE_MEM_TYPE_IMPORTED_UMM) {
			alloc->imported.umm.current_mapping_usage_count--;

			if (0 == alloc->imported.umm.current_mapping_usage_count) {
				struct kbase_va_region *reg;

				reg = kbase_region_tracker_find_region_base_address(
						katom->kctx,
						katom->extres[res_no].gpu_address);

				if (reg && reg->gpu_alloc == alloc)
					kbase_mmu_teardown_pages(katom->kctx,
							reg->start_pfn,
							kbase_reg_current_backed_size(reg));

				kbase_jd_umm_unmap(katom->kctx, alloc);
			}
		}
#endif				/* CONFIG_DMA_SHARED_BUFFER */
		kbase_mem_phy_alloc_put(alloc);
	}
	kbase_gpu_vm_unlock(katom->kctx);

 early_err_out:
	kfree(katom->extres);
	katom->extres = NULL;
#ifdef CONFIG_KDS
	kfree(kds_resources);
	kfree(kds_access_bitmap);
#endif				/* CONFIG_KDS */
	return err_ret_val;
}

static inline void jd_resolve_dep(struct list_head *out_list,
					struct kbase_jd_atom *katom,
					u8 d,
					bool ctx_is_dying)
{
	u8 other_d = !d;

	while (!list_empty(&katom->dep_head[d])) {
		struct kbase_jd_atom *dep_atom;
		u8 dep_type;

		dep_atom = list_entry(katom->dep_head[d].next,
				struct kbase_jd_atom, dep_item[d]);

		list_del(katom->dep_head[d].next);

		dep_type = kbase_jd_katom_dep_type(&dep_atom->dep[d]);
		kbase_jd_katom_dep_clear(&dep_atom->dep[d]);

		if (katom->event_code != BASE_JD_EVENT_DONE &&
			(dep_type != BASE_JD_DEP_TYPE_ORDER || ctx_is_dying)) {
			/* Atom failed, so remove the other dependencies and immediately fail the atom */
			if (kbase_jd_katom_dep_atom(&dep_atom->dep[other_d])) {
				list_del(&dep_atom->dep_item[other_d]);
				kbase_jd_katom_dep_clear(&dep_atom->dep[other_d]);
			}
#ifdef CONFIG_KDS
			if (!dep_atom->kds_dep_satisfied) {
				/* Just set kds_dep_satisfied to true. If the callback happens after this then it will early out and
				 * do nothing. If the callback doesn't happen then kbase_jd_post_external_resources will clean up
				 */
				dep_atom->kds_dep_satisfied = true;
			}
#endif

			dep_atom->event_code = katom->event_code;
			KBASE_DEBUG_ASSERT(dep_atom->status !=
						KBASE_JD_ATOM_STATE_UNUSED);
			dep_atom->status = KBASE_JD_ATOM_STATE_COMPLETED;

			list_add_tail(&dep_atom->dep_item[0], out_list);
		} else if (!kbase_jd_katom_dep_atom(&dep_atom->dep[other_d])) {
#ifdef CONFIG_KDS
			if (dep_atom->kds_dep_satisfied)
#endif
				list_add_tail(&dep_atom->dep_item[0], out_list);
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

			if ((dep_atom->core_req & BASEP_JD_REQ_ATOM_TYPE) ==
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
	struct kbasep_js_kctx_info *js_kctx_info = &kctx->jctx.sched_info;
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
	list_add_tail(&katom->dep_item[0], &completed_jobs);

	while (!list_empty(&completed_jobs)) {
		katom = list_entry(completed_jobs.prev, struct kbase_jd_atom, dep_item[0]);
		list_del(completed_jobs.prev);

		KBASE_DEBUG_ASSERT(katom->status == KBASE_JD_ATOM_STATE_COMPLETED);

		for (i = 0; i < 2; i++)
			jd_resolve_dep(&runnable_jobs, katom, i,
						js_kctx_info->ctx.is_dying);

		if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES)
			kbase_jd_post_external_resources(katom);

		while (!list_empty(&runnable_jobs)) {
			struct kbase_jd_atom *node;

			node = list_entry(runnable_jobs.next,
					struct kbase_jd_atom, dep_item[0]);

			list_del(runnable_jobs.next);

			KBASE_DEBUG_ASSERT(node->status != KBASE_JD_ATOM_STATE_UNUSED);

			if (node->status != KBASE_JD_ATOM_STATE_COMPLETED) {
				need_to_try_schedule_context |= jd_run_atom(node);
			} else {
				node->event_code = katom->event_code;

				if ((node->core_req & BASEP_JD_REQ_ATOM_TYPE)
						  == BASE_JD_REQ_SOFT_REPLAY) {
					if (kbase_replay_process(node))
						/* Don't complete this atom */
						continue;
				} else if (node->core_req &
							BASE_JD_REQ_SOFT_JOB) {
					/* If this is a fence wait then remove it from the list of sync waiters. */
					if (BASE_JD_REQ_SOFT_FENCE_WAIT == node->core_req)
						list_del(&node->dep_item[0]);

					kbase_finish_soft_job(node);
				}
				node->status = KBASE_JD_ATOM_STATE_COMPLETED;
			}

			if (node->status == KBASE_JD_ATOM_STATE_COMPLETED)
				list_add_tail(&node->dep_item[0], &completed_jobs);
		}

		/* Register a completed job as a disjoint event when the GPU
		 * is in a disjoint state (ie. being reset or replaying jobs).
		 */
		kbase_disjoint_event_potential(kctx->kbdev);
		if (completed_jobs_ctx)
			list_add_tail(&katom->dep_item[0], completed_jobs_ctx);
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

bool jd_submit_atom(struct kbase_context *kctx,
			 const struct base_jd_atom_v2 *user_atom,
			 struct kbase_jd_atom *katom)
{
	struct kbase_jd_context *jctx = &kctx->jctx;
	base_jd_core_req core_req;
	int queued = 0;
	int i;
	int sched_prio;
	bool ret;

	/* Update the TOTAL number of jobs. This includes those not tracked by
	 * the scheduler: 'not ready to run' and 'dependency-only' jobs. */
	jctx->job_nr++;

	core_req = user_atom->core_req;

	katom->start_timestamp.tv64 = 0;
	katom->time_spent_us = 0;
	katom->udata = user_atom->udata;
	katom->kctx = kctx;
	katom->nr_extres = user_atom->nr_extres;
	katom->extres = NULL;
	katom->device_nr = user_atom->device_nr;
	katom->affinity = 0;
	katom->jc = user_atom->jc;
	katom->coreref_state = KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED;
	katom->core_req = core_req;
	katom->atom_flags = 0;
	katom->retry_count = 0;
	katom->need_cache_flush_cores_retained = 0;
	katom->x_pre_dep = NULL;
	katom->x_post_dep = NULL;
#ifdef CONFIG_KDS
	/* Start by assuming that the KDS dependencies are satisfied,
	 * kbase_jd_pre_external_resources will correct this if there are dependencies */
	katom->kds_dep_satisfied = true;
	katom->kds_rset = NULL;
#endif				/* CONFIG_KDS */

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
#if defined(CONFIG_MALI_MIPE_ENABLED)
				kbase_tlstream_tl_new_atom(
						katom,
						kbase_jd_atom_id(kctx, katom));
				kbase_tlstream_tl_ret_atom_ctx(
						katom, kctx);
#endif
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
			 * only to the failed one, try to submit it throught
			 * the normal path
			 */
			if (dep_atom_type == BASE_JD_DEP_TYPE_ORDER &&
					dep_atom->event_code > BASE_JD_EVENT_ACTIVE) {
				continue;
			}

			if (i == 1 && kbase_jd_katom_dep_atom(&katom->dep[0])) {
				/* Remove the previous dependency */
				list_del(&katom->dep_item[0]);
				kbase_jd_katom_dep_clear(&katom->dep[0]);
			}

			/* Atom has completed, propagate the error code if any */
			katom->event_code = dep_atom->event_code;
			katom->status = KBASE_JD_ATOM_STATE_QUEUED;
#if defined(CONFIG_MALI_MIPE_ENABLED)
			kbase_tlstream_tl_new_atom(
					katom,
					kbase_jd_atom_id(kctx, katom));
			kbase_tlstream_tl_ret_atom_ctx(katom, kctx);
#endif
			if ((katom->core_req & BASEP_JD_REQ_ATOM_TYPE)
					 == BASE_JD_REQ_SOFT_REPLAY) {
				if (kbase_replay_process(katom)) {
					ret = false;
					goto out;
				}
			}
			ret = jd_done_nolock(katom, NULL);

			goto out;
		} else {
			/* Atom is in progress, add this atom to the list */
			list_add_tail(&katom->dep_item[i], &dep_atom->dep_head[i]);
			kbase_jd_katom_dep_set(&katom->dep[i], dep_atom, dep_atom_type);
			queued = 1;
		}
	}

	/* These must occur after the above loop to ensure that an atom that
	 * depends on a previous atom with the same number behaves as expected */
	katom->event_code = BASE_JD_EVENT_DONE;
	katom->status = KBASE_JD_ATOM_STATE_QUEUED;

#if defined(CONFIG_MALI_MIPE_ENABLED)
	kbase_tlstream_tl_new_atom(
			katom,
			kbase_jd_atom_id(kctx, katom));
	kbase_tlstream_tl_ret_atom_ctx(katom, kctx);
#endif

	/* Reject atoms with job chain = NULL, as these cause issues with soft-stop */
	if (!katom->jc && (katom->core_req & BASEP_JD_REQ_ATOM_TYPE) != BASE_JD_REQ_DEP) {
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

	/* For invalid priority, be most lenient and choose the default */
	sched_prio = kbasep_js_atom_prio_to_sched_prio(user_atom->prio);
	if (sched_prio == KBASE_JS_ATOM_SCHED_PRIO_INVALID)
		sched_prio = KBASE_JS_ATOM_SCHED_PRIO_DEFAULT;
	katom->sched_priority = sched_prio;

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
	trace_gpu_job_enqueue((u32)kctx, katom->work_id, kbasep_map_core_reqs_to_string(katom->core_req));
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

	if ((katom->core_req & BASEP_JD_REQ_ATOM_TYPE)
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
		/* The job has not yet completed */
		list_add_tail(&katom->dep_item[0], &kctx->waiting_soft_jobs);
		ret = false;
	} else if ((katom->core_req & BASEP_JD_REQ_ATOM_TYPE) != BASE_JD_REQ_DEP) {
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

#ifdef BASE_LEGACY_UK6_SUPPORT
int kbase_jd_submit(struct kbase_context *kctx,
		const struct kbase_uk_job_submit *submit_data,
		int uk6_atom)
#else
int kbase_jd_submit(struct kbase_context *kctx,
		const struct kbase_uk_job_submit *submit_data)
#endif /* BASE_LEGACY_UK6_SUPPORT */
{
	struct kbase_jd_context *jctx = &kctx->jctx;
	int err = 0;
	int i;
	bool need_to_try_schedule_context = false;
	struct kbase_device *kbdev;
	void __user *user_addr;

	/*
	 * kbase_jd_submit isn't expected to fail and so all errors with the jobs
	 * are reported by immediately falling them (through event system)
	 */
	kbdev = kctx->kbdev;

	beenthere(kctx, "%s", "Enter");

	if ((kctx->jctx.sched_info.ctx.flags & KBASE_CTX_FLAG_SUBMIT_DISABLED) != 0) {
		dev_err(kbdev->dev, "Attempt to submit to a context that has SUBMIT_DISABLED set on it");
		return -EINVAL;
	}

#ifdef BASE_LEGACY_UK6_SUPPORT
	if ((uk6_atom && submit_data->stride !=
			sizeof(struct base_jd_atom_v2_uk6)) ||
			submit_data->stride != sizeof(base_jd_atom_v2)) {
#else
	if (submit_data->stride != sizeof(base_jd_atom_v2)) {
#endif /* BASE_LEGACY_UK6_SUPPORT */
		dev_err(kbdev->dev, "Stride passed to job_submit doesn't match kernel");
		return -EINVAL;
	}

	user_addr = get_compat_pointer(kctx, &submit_data->addr);

	KBASE_TIMELINE_ATOMS_IN_FLIGHT(kctx, atomic_add_return(submit_data->nr_atoms, &kctx->timeline.jd_atoms_in_flight));

	for (i = 0; i < submit_data->nr_atoms; i++) {
		struct base_jd_atom_v2 user_atom;
		struct kbase_jd_atom *katom;

#ifdef BASE_LEGACY_UK6_SUPPORT
		if (uk6_atom) {
			struct base_jd_atom_v2_uk6 user_atom_v6;
			base_jd_dep_type dep_types[2] = {BASE_JD_DEP_TYPE_DATA, BASE_JD_DEP_TYPE_DATA};

			if (copy_from_user(&user_atom_v6, user_addr,
					sizeof(user_atom_v6))) {
				err = -EINVAL;
				KBASE_TIMELINE_ATOMS_IN_FLIGHT(kctx,
					atomic_sub_return(
					submit_data->nr_atoms - i,
					&kctx->timeline.jd_atoms_in_flight));
				break;
			}
			/* Convert from UK6 atom format to UK7 format */
			user_atom.jc = user_atom_v6.jc;
			user_atom.udata = user_atom_v6.udata;
			user_atom.extres_list = user_atom_v6.extres_list;
			user_atom.nr_extres = user_atom_v6.nr_extres;
			user_atom.core_req = user_atom_v6.core_req;

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
		if (copy_from_user(&user_atom, user_addr, sizeof(user_atom)) != 0) {
			err = -EINVAL;
			KBASE_TIMELINE_ATOMS_IN_FLIGHT(kctx, atomic_sub_return(submit_data->nr_atoms - i, &kctx->timeline.jd_atoms_in_flight));
			break;
		}
#ifdef BASE_LEGACY_UK6_SUPPORT
		}
#endif /* BASE_LEGACY_UK6_SUPPORT */

		user_addr = (void __user *)((uintptr_t) user_addr + submit_data->stride);

		mutex_lock(&jctx->lock);
#ifndef compiletime_assert
#define compiletime_assert_defined
#define compiletime_assert(x, msg) do { switch (0) { case 0: case (x):; } } \
while (false)
#endif
		compiletime_assert((1 << (8*sizeof(user_atom.atom_number))) ==
					BASE_JD_ATOM_COUNT,
			"BASE_JD_ATOM_COUNT and base_atom_id type out of sync");
		compiletime_assert(sizeof(user_atom.pre_dep[0].atom_id) ==
					sizeof(user_atom.atom_number),
			"BASE_JD_ATOM_COUNT and base_atom_id type out of sync");
#ifdef compiletime_assert_defined
#undef compiletime_assert
#undef compiletime_assert_defined
#endif
		katom = &jctx->atoms[user_atom.atom_number];

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
	union kbasep_js_policy *js_policy;
	struct kbase_device *kbdev;
	struct kbasep_js_device_data *js_devdata;
	u64 cache_jc = katom->jc;
	struct kbasep_js_atom_retained_state katom_retained_state;
	bool schedule = false;
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
	js_policy = &kbdev->js_data.policy;

	KBASE_TRACE_ADD(kbdev, JD_DONE_WORKER, kctx, katom, katom->jc, 0);

	kbase_backend_complete_wq(kbdev, katom);

	/*
	 * Begin transaction on JD context and JS context
	 */
	mutex_lock(&jctx->lock);
	mutex_lock(&js_devdata->queue_mutex);
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);

	/* This worker only gets called on contexts that are scheduled *in*. This is
	 * because it only happens in response to an IRQ from a job that was
	 * running.
	 */
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.is_scheduled);

	if (katom->event_code == BASE_JD_EVENT_STOPPED) {
		/* Atom has been promoted to stopped */
		unsigned long flags;

		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
		mutex_unlock(&js_devdata->queue_mutex);
		mutex_unlock(&jctx->lock);

		spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

		katom->status = KBASE_JD_ATOM_STATE_IN_JS;
		kbase_js_unpull(kctx, katom);

		spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

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

	if (!kbasep_js_has_atom_finished(&katom_retained_state)) {
		mutex_lock(&js_devdata->runpool_mutex);
		kbasep_js_clear_job_retry_submit(katom);
		/* An atom that has been hard-stopped might have previously
		 * been soft-stopped and has just finished before the hard-stop
		 * occurred. For this reason, clear the hard-stopped flag */
		katom->atom_flags &= ~(KBASE_KATOM_FLAG_BEEN_HARD_STOPPED);
		mutex_unlock(&js_devdata->runpool_mutex);
	}

	if (kbasep_js_has_atom_finished(&katom_retained_state))
		schedule = true;

	context_idle = kbase_js_complete_atom_wq(kctx, katom);

	KBASE_DEBUG_ASSERT(kbasep_js_has_atom_finished(&katom_retained_state));

	kbasep_js_remove_job(kbdev, kctx, katom);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_unlock(&js_devdata->queue_mutex);
	katom->atom_flags &= ~KBASE_KATOM_FLAG_HOLDING_CTX_REF;
	/* jd_done_nolock() requires the jsctx_mutex lock to be dropped */
	schedule |= jd_done_nolock(katom, &kctx->completed_jobs);

	/* katom may have been freed now, do not use! */

	if (context_idle) {
		unsigned long flags;

		mutex_lock(&js_devdata->queue_mutex);
		spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

		/* If kbase_sched() has scheduled this context back in then
		 * ctx_active will have been set after we marked it as inactive,
		 * and another pm reference will have been taken, so drop our
		 * reference. But do not call kbase_jm_idle_ctx(), as the
		 * context is active and fast-starting is allowed.
		 *
		 * If an atom has been fast-started then kctx->atoms_pulled will
		 * be non-zero but ctx_active will still be false (as the
		 * previous pm reference has been inherited). Do NOT drop our
		 * reference, as it has been re-used, and leave the context as
		 * active.
		 *
		 * If no new atoms have been started then ctx_active will still
		 * be false and atoms_pulled will be zero, so drop the reference
		 * and call kbase_jm_idle_ctx().
		 *
		 * As the checks are done under both the queue_mutex and
		 * runpool_irq.lock is should be impossible for this to race
		 * with the scheduler code.
		 */
		if (kctx->ctx_active || !atomic_read(&kctx->atoms_pulled)) {
			/* Calling kbase_jm_idle_ctx() here will ensure that
			 * atoms are not fast-started when we drop the
			 * runpool_irq.lock. This is not performed if ctx_active
			 * is set as in that case another pm reference has been
			 * taken and a fast-start would be valid.
			 */
			if (!kctx->ctx_active)
				kbase_jm_idle_ctx(kbdev, kctx);
			spin_unlock_irqrestore(&js_devdata->runpool_irq.lock,
					flags);

			kbase_pm_context_idle(kbdev);
		} else {
			kctx->ctx_active = true;
			spin_unlock_irqrestore(&js_devdata->runpool_irq.lock,
					flags);
		}
		mutex_unlock(&js_devdata->queue_mutex);
	}

	/*
	 * Transaction complete
	 */
	mutex_unlock(&jctx->lock);

	/* Job is now no longer running, so can now safely release the context
	 * reference, and handle any actions that were logged against the atom's retained state */

	kbasep_js_runpool_release_ctx_and_katom_retained_state(kbdev, kctx, &katom_retained_state);

	if (schedule)
		kbase_js_sched_all(kbdev);

	if (!atomic_dec_return(&kctx->work_count)) {
		/* If worker now idle then post all events that jd_done_nolock()
		 * has queued */
		mutex_lock(&jctx->lock);
		while (!list_empty(&kctx->completed_jobs)) {
			struct kbase_jd_atom *atom = list_entry(
					kctx->completed_jobs.next,
					struct kbase_jd_atom, dep_item[0]);
			list_del(kctx->completed_jobs.next);

			kbase_event_post(kctx, atom);
		}
		mutex_unlock(&jctx->lock);
	}

	kbase_backend_complete_wq_post_sched(kbdev, core_req, affinity,
			coreref_state);

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
	KBASE_DEBUG_ASSERT(!js_kctx_info->ctx.is_scheduled);

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
 * jd_evict_worker - Work queue job evict function
 * @data: a &struct work_struct
 *
 * Only called as part of evicting failed jobs. This is only called on jobs that
 * were never submitted to HW Access. Jobs that were submitted are handled
 * through kbase_jd_done_worker().
 * Operates serially with the kbase_jd_done_worker() on the work queue.
 *
 * We don't need to release most of the resources that would occur on
 * kbase_jd_done() or kbase_jd_done_worker(), because the atoms here must not be
 * running (by virtue of having not been submitted to HW Access).
 */
static void jd_evict_worker(struct work_struct *data)
{
	struct kbase_jd_atom *katom = container_of(data, struct kbase_jd_atom,
									work);
	struct kbase_jd_context *jctx;
	struct kbase_context *kctx;
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbase_device *kbdev;

	/* Soft jobs should never reach this function */
	KBASE_DEBUG_ASSERT((katom->core_req & BASE_JD_REQ_SOFT_JOB) == 0);

	kctx = katom->kctx;
	kbdev = kctx->kbdev;
	jctx = &kctx->jctx;
	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_TRACE_ADD(kbdev, JD_CANCEL_WORKER, kctx, katom, katom->jc, 0);

	/* Scheduler: Remove the job from the system */
	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	kbasep_js_remove_cancelled_job(kbdev, kctx, katom);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	mutex_lock(&jctx->lock);
	jd_done_nolock(katom, NULL);
	/* katom may have been freed now, do not use! */
	mutex_unlock(&jctx->lock);

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
 *   The caller must hold kbasep_js_device_data.runpool_irq.lock
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
	if (kbase_debug_job_fault_process(katom, katom->event_code))
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
	struct kbasep_js_kctx_info *js_kctx_info;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	KBASE_DEBUG_ASSERT(NULL != katom);
	kctx = katom->kctx;
	KBASE_DEBUG_ASSERT(NULL != kctx);

	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_TRACE_ADD(kbdev, JD_CANCEL, kctx, katom, katom->jc, 0);

	/* This should only be done from a context that is not scheduled */
	KBASE_DEBUG_ASSERT(!js_kctx_info->ctx.is_scheduled);

	WARN_ON(work_pending(&katom->work));

	katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;

	KBASE_DEBUG_ASSERT(0 == object_is_on_stack(&katom->work));
	INIT_WORK(&katom->work, jd_cancel_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}

void kbase_jd_evict(struct kbase_device *kbdev, struct kbase_jd_atom *katom)
{
	struct kbase_context *kctx;
	struct kbasep_js_kctx_info *js_kctx_info;

	KBASE_DEBUG_ASSERT(NULL != kbdev);
	KBASE_DEBUG_ASSERT(NULL != katom);
	kctx = katom->kctx;
	KBASE_DEBUG_ASSERT(NULL != kctx);

	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_TRACE_ADD(kbdev, JD_CANCEL, kctx, katom, katom->jc, 0);

	/* This should only be done from a context that is currently scheduled
	 */
	KBASE_DEBUG_ASSERT(js_kctx_info->ctx.is_scheduled);

	WARN_ON(work_pending(&katom->work));

	KBASE_DEBUG_ASSERT(0 == object_is_on_stack(&katom->work));
	INIT_WORK(&katom->work, jd_evict_worker);
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

	list_for_each_safe(entry, tmp, &kctx->waiting_soft_jobs) {
		katom = list_entry(entry, struct kbase_jd_atom, dep_item[0]);
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

	mutex_unlock(&kctx->jctx.lock);

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

	kctx->jctx.job_done_wq = alloc_workqueue("mali_jd", 0, 1);
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
