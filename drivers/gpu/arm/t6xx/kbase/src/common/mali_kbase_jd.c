/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */


#if defined(CONFIG_DMA_SHARED_BUFFER)
#include <linux/dma-buf.h>
#endif /* defined(CONFIG_DMA_SHARED_BUFFER)*/
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_uku.h>
#include <kbase/src/common/mali_kbase_js_affinity.h>
#ifdef CONFIG_UMP
#include <linux/ump.h>
#endif /* CONFIG_UMP */

#define beenthere(f, a...)  OSK_PRINT_INFO(OSK_BASE_JD, "%s:" f, __func__, ##a)

/*
 * This is the kernel side of the API. Only entry points are:
 * - kbase_jd_submit(): Called from userspace to submit a single bag
 * - kbase_jd_done(): Called from interrupt context to track the
 *   completion of a job.
 * Callouts:
 * - to the job manager (enqueue a job)
 * - to the event subsystem (signals the completion/failure of bag/job-chains).
 */

static void *get_compat_pointer(const kbase_pointer *p)
{
#ifdef CONFIG_COMPAT
	if (is_compat_task())
	{
		return (void*)p->compat_value;
	}
	else
#endif
	{
		return p->value;
	}
}

/* Runs an atom, either by handing to the JS or by immediately running it in the case of soft-jobs
 *
 * Returns whether the JS needs a reschedule.
 *
 * Note that the caller must also check the atom status and
 * if it is KBASE_JD_ATOM_STATE_COMPLETED must call jd_done_nolock
 */
static int jd_run_atom(kbase_jd_atom *katom)
{
	kbase_context *kctx = katom->kctx;
	OSK_ASSERT(katom->status != KBASE_JD_ATOM_STATE_UNUSED);

	if ((katom->core_req & BASEP_JD_REQ_ATOM_TYPE) == BASE_JD_REQ_DEP)
	{
		/* Dependency only atom */
		katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
		return 0;
	}
	else if (katom->core_req & BASE_JD_REQ_SOFT_JOB)
	{
		/* Soft-job */
		if (kbase_process_soft_job(katom) == 0)
		{
			kbase_finish_soft_job(katom);
			katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
		}
		else
		{
			/* The job has not completed */
			kbasep_list_trace_add(2, kctx->kbdev, katom, &kctx->waiting_soft_jobs, KBASE_TRACE_LIST_ADD, KBASE_TRACE_LIST_WAITING_SOFT_JOBS);
			OSK_DLIST_PUSH_BACK(&kctx->waiting_soft_jobs, katom,
			                    kbase_jd_atom, dep_item[0]);
		}
		return 0;
	}
	katom->status = KBASE_JD_ATOM_STATE_IN_JS;
	/* Queue an action about whether we should try scheduling a context */
	return kbasep_js_add_job( kctx, katom );
}

#ifdef CONFIG_KDS
static void kds_dep_clear(void * callback_parameter, void * callback_extra_parameter)
{
	kbase_jd_atom * katom;
	kbase_jd_context * ctx;

	katom = (kbase_jd_atom*)callback_parameter;
	OSK_ASSERT(katom);
	ctx = &katom->kctx->jctx;

	mutex_lock(&ctx->lock);

	if (katom->kds_dep_satisfied)
	{
		/* KDS resource has already been satisfied (e.g. due to zapping) */
		goto out;
	}

	/* This atom's KDS dependency has now been met */
	katom->kds_dep_satisfied = MALI_TRUE;

	/* Check whether the atom's other dependencies were already met */
	if (!katom->dep_atom[0] && !katom->dep_atom[1])
	{
		/* katom dep complete, run it */
		mali_bool resched;

		resched = jd_run_atom(katom);

		if (katom->status == KBASE_JD_ATOM_STATE_COMPLETED)
		{
			/* The atom has already finished */
			resched |= jd_done_nolock(katom);
		}

		if (resched)
		{
			kbasep_js_try_schedule_head_ctx(katom->kctx->kbdev);
		}
	}
out:
	mutex_unlock(&ctx->lock);
}
#endif /* CONFIG_KDS */

#ifdef CONFIG_DMA_SHARED_BUFFER
static mali_error kbase_jd_umm_map(kbase_context * kctx, struct kbase_va_region * reg)
{
	struct sg_table * st;
	struct scatterlist * s;
	int i;
	osk_phy_addr * pa;
	mali_error err;

	OSK_ASSERT(NULL == reg->imported_metadata.umm.st);
	st = dma_buf_map_attachment(reg->imported_metadata.umm.dma_attachment, DMA_BIDIRECTIONAL);

	if (!st)
	{
		return MALI_ERROR_FUNCTION_FAILED;
	}

	/* save for later */
	reg->imported_metadata.umm.st = st;

	pa = kbase_get_phy_pages(reg);
	OSK_ASSERT(pa);

	for_each_sg(st->sgl, s, st->nents, i)
	{
		int j;
		size_t pages = PFN_DOWN(sg_dma_len(s));

		for (j = 0; j < pages; j++)
			*pa++ = sg_dma_address(s) + (j << PAGE_SHIFT);
	}

	err = kbase_mmu_insert_pages(kctx, reg->start_pfn, kbase_get_phy_pages(reg), reg->nr_alloc_pages, reg->flags | KBASE_REG_GPU_WR | KBASE_REG_GPU_RD);

	if (MALI_ERROR_NONE != err)
	{
		dma_buf_unmap_attachment(reg->imported_metadata.umm.dma_attachment, reg->imported_metadata.umm.st, DMA_BIDIRECTIONAL);
		reg->imported_metadata.umm.st = NULL;
	}

	return err;
}

static void kbase_jd_umm_unmap(kbase_context * kctx, struct kbase_va_region * reg)
{
	OSK_ASSERT(kctx);
	OSK_ASSERT(reg);
	OSK_ASSERT(reg->imported_metadata.umm.dma_attachment);
	OSK_ASSERT(reg->imported_metadata.umm.st);
	kbase_mmu_teardown_pages(kctx, reg->start_pfn, reg->nr_alloc_pages);
	dma_buf_unmap_attachment(reg->imported_metadata.umm.dma_attachment, reg->imported_metadata.umm.st, DMA_BIDIRECTIONAL);
	reg->imported_metadata.umm.st = NULL;
}
#endif /* CONFIG_DMA_SHARED_BUFFER */

void kbase_jd_free_external_resources(kbase_jd_atom *katom)
{
#ifdef CONFIG_KDS
	if (katom->kds_rset)
	{
		kds_resource_set_release(&katom->kds_rset);
	}
#endif /* CONFIG_KDS */
}

static void kbase_jd_post_external_resources(kbase_jd_atom * katom)
{
	OSK_ASSERT(katom);
	OSK_ASSERT(katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES);

#ifdef CONFIG_KDS
	if (katom->kds_rset)
	{
		/* Prevent the KDS resource from triggering the atom in case of zapping */
		katom->kds_dep_satisfied = MALI_TRUE;
	}
#endif /* CONFIG_KDS */

#if defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG)
	/* Lock also used in debug mode just for lock order checking */
	kbase_gpu_vm_lock(katom->kctx);
#endif /* defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG) */
	/* only roll back if extres is non-NULL */
	if (katom->extres)
	{
#ifdef CONFIG_DMA_SHARED_BUFFER
		u32 res_no;
		res_no = katom->nr_extres;
		while (res_no-- > 0)
		{
			base_external_resource * res;
			kbase_va_region * reg;

			res = &katom->extres[res_no];
			reg = kbase_region_tracker_find_region_enclosing_address(katom->kctx, res->ext_resource & ~BASE_EXT_RES_ACCESS_EXCLUSIVE);
			/* if reg wasn't found then it has been freed while the job ran */
			if (reg)
			{
				if (1 == reg->imported_metadata.umm.current_mapping_usage_count--)
				{
					/* last job using */
					kbase_jd_umm_unmap(katom->kctx, reg);
				}
			}
		}
#endif /* CONFIG_DMA_SHARED_BUFFER */
		kfree(katom->extres);
		katom->extres = NULL;
	}
#if defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG)
	/* Lock also used in debug mode just for lock order checking */
	kbase_gpu_vm_unlock(katom->kctx);
#endif /* defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG) */
}

#if defined(CONFIG_DMA_SHARED_BUFFER_USES_KDS) || defined(CONFIG_KDS)
static void add_kds_resource(struct kds_resource *kds_res, struct kds_resource ** kds_resources, u32 *kds_res_count,
                             unsigned long * kds_access_bitmap, mali_bool exclusive)
{
	u32 i;

	for(i = 0; i < *kds_res_count; i++)
	{
		if (kds_resources[i] == kds_res)
		{
			/* Duplicate resource, ignore */
			return;
		}
	}

	kds_resources[*kds_res_count] = kds_res;
	if (exclusive)
		osk_bitarray_set_bit(*kds_res_count, kds_access_bitmap);
	(*kds_res_count)++;
}
#endif /* defined(CONFIG_DMA_SHARED_BUFFER_USES_KDS) || defined(CONFIG_KDS) */

static mali_error kbase_jd_pre_external_resources(kbase_jd_atom * katom, const base_jd_atom_v2 *user_atom)
{
	mali_error err_ret_val = MALI_ERROR_FUNCTION_FAILED;
	u32 res_no;
#ifdef CONFIG_KDS
	u32 kds_res_count = 0;
	struct kds_resource ** kds_resources = NULL;
	unsigned long * kds_access_bitmap = NULL;
#endif /* CONFIG_KDS */

	OSK_ASSERT(katom);
	OSK_ASSERT(katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES);

	if (!katom->nr_extres)
	{
		/* no resources encoded, early out */
		return MALI_ERROR_FUNCTION_FAILED;
	}

	katom->extres = kmalloc(sizeof(base_external_resource)*katom->nr_extres, GFP_KERNEL);
	if (NULL == katom->extres)
	{
		err_ret_val = MALI_ERROR_OUT_OF_MEMORY;
		goto early_err_out;
	}

	if (ukk_copy_from_user(sizeof(base_external_resource)*katom->nr_extres,
	                       katom->extres,
	                       get_compat_pointer(&user_atom->extres_list)) != MALI_ERROR_NONE)
	{
		err_ret_val = MALI_ERROR_FUNCTION_FAILED;
		goto early_err_out;
	}

#ifdef CONFIG_KDS
	/* assume we have to wait for all */
	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		kds_resources = NULL;
	}
	else
	{
		OSK_ASSERT(0 != katom->nr_extres);
		kds_resources = kmalloc(sizeof(struct kds_resource *) * katom->nr_extres, GFP_KERNEL);
	}

	if (NULL == kds_resources)
	{
		err_ret_val = MALI_ERROR_OUT_OF_MEMORY;
		goto early_err_out;
	}

	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		kds_access_bitmap = NULL;
	}
	else
	{
		OSK_ASSERT(0 != katom->nr_extres);
		kds_access_bitmap = kzalloc(sizeof(unsigned long) * ((katom->nr_extres + OSK_BITS_PER_LONG - 1) / OSK_BITS_PER_LONG), GFP_KERNEL);
	}

	if (NULL == kds_access_bitmap)
	{
		err_ret_val = MALI_ERROR_OUT_OF_MEMORY;
		goto early_err_out;
	}
#endif /* CONFIG_KDS */

#if defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG)
	/* need to keep the GPU VM locked while we set up UMM buffers */
	/* Lock also used in debug mode just for lock order checking */
	kbase_gpu_vm_lock(katom->kctx);
#endif /* defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG) */

	for (res_no = 0; res_no < katom->nr_extres; res_no++)
	{
		base_external_resource * res;
		kbase_va_region * reg;

		res = &katom->extres[res_no];
		reg = kbase_region_tracker_find_region_enclosing_address(katom->kctx, res->ext_resource & ~BASE_EXT_RES_ACCESS_EXCLUSIVE);
		/* did we find a matching region object? */
		if (NULL == reg)
		{
			/* roll back */
			goto failed_loop;
		}

		/* decide what needs to happen for this resource */
		switch (reg->imported_type)
		{
			case BASE_TMEM_IMPORT_TYPE_UMP:
			{
#if defined(CONFIG_KDS) && defined(CONFIG_UMP)
				struct kds_resource * kds_res;
				kds_res = ump_dd_kds_resource_get(reg->imported_metadata.ump_handle);
				if (kds_res)
				{
					add_kds_resource(kds_res, kds_resources, &kds_res_count, kds_access_bitmap,
					                 katom->extres[res_no].ext_resource & BASE_EXT_RES_ACCESS_EXCLUSIVE);
				}
#endif /*defined(CONFIG_KDS) && defined(CONFIG_UMP)*/
				break;
			}
#ifdef CONFIG_DMA_SHARED_BUFFER
			case BASE_TMEM_IMPORT_TYPE_UMM:
			{
#ifdef CONFIG_DMA_SHARED_BUFFER_USES_KDS
				struct kds_resource *kds_res;
				kds_res = get_dma_buf_kds_resource(reg->imported_metadata.umm.dma_buf);
				if (kds_res)
				{
					add_kds_resource(kds_res, kds_resources, &kds_res_count, kds_access_bitmap,
					                 katom->extres[res_no].ext_resource & BASE_EXT_RES_ACCESS_EXCLUSIVE);
				}
#endif
				reg->imported_metadata.umm.current_mapping_usage_count++;
				if (1 == reg->imported_metadata.umm.current_mapping_usage_count)
				{
					/* use a local variable to not pollute err_ret_val
					 * with a potential success value as some other gotos depend
					 * on the default error code stored in err_ret_val */
					mali_error tmp;
					tmp = kbase_jd_umm_map(katom->kctx, reg);
					if (MALI_ERROR_NONE != tmp)
					{
						/* failed to map this buffer, roll back */
						err_ret_val = tmp;
						goto failed_loop;
					}
				}
				break;
			}
#endif
			default:
				goto failed_loop;
		}
	}
	/* successfully parsed the extres array */
#if defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG)
	/* drop the vm lock before we call into kds */
	/* Lock also used in debug mode just for lock order checking */
	kbase_gpu_vm_unlock(katom->kctx);
#endif /* defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG) */

#ifdef CONFIG_KDS
	if (kds_res_count)
	{
		/* We have resources to wait for with kds */
		katom->kds_dep_satisfied = MALI_FALSE;
		if (kds_async_waitall(&katom->kds_rset, KDS_FLAG_LOCKED_IGNORE, &katom->kctx->jctx.kds_cb,
		                      katom, NULL, kds_res_count, kds_access_bitmap, kds_resources))
		{
			goto failed_kds_setup;
		}
	}
	else
	{
		/* Nothing to wait for, so kds dep met */
		katom->kds_dep_satisfied = MALI_TRUE;
	}
	kfree(kds_resources);
	kfree(kds_access_bitmap);
#endif /* CONFIG_KDS */

	/* all done OK */
	return MALI_ERROR_NONE;


/* error handling section */

#ifdef CONFIG_KDS
failed_kds_setup:

#if defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG)
	/* lock before we unmap */
	/* Lock also used in debug mode just for lock order checking */
	kbase_gpu_vm_lock(katom->kctx);
#endif /* defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG) */
#endif /* CONFIG_KDS */

failed_loop:
#ifdef CONFIG_DMA_SHARED_BUFFER
	/* undo the loop work */
	while (res_no-- > 0)
	{
		base_external_resource * res;
		kbase_va_region * reg;

		res = &katom->extres[res_no];
		reg = kbase_region_tracker_find_region_enclosing_address(katom->kctx, res->ext_resource & ~BASE_EXT_RES_ACCESS_EXCLUSIVE);
		/* if reg wasn't found then it has been freed when we set up kds */
		if (reg)
		{
			reg->imported_metadata.umm.current_mapping_usage_count--;
			if (0 == reg->imported_metadata.umm.current_mapping_usage_count)
			{
				kbase_jd_umm_unmap(katom->kctx, reg);
			}
		}
	}
#endif /* CONFIG_DMA_SHARED_BUFFER */
#if defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG)
	/* Lock also used in debug mode just for lock order checking */
	kbase_gpu_vm_unlock(katom->kctx);
#endif /* defined(CONFIG_DMA_SHARED_BUFFER) || defined(CONFIG_MALI_DEBUG) */

early_err_out:
	if (katom->extres)
	{
		kfree(katom->extres);
		katom->extres = NULL;
	}
#ifdef CONFIG_KDS
	if (kds_resources)
	{
		kfree(kds_resources);
	}
	if (kds_access_bitmap)
	{
		kfree(kds_access_bitmap);
	}
#endif /* CONFIG_KDS */
	return err_ret_val;
}

STATIC INLINE void jd_resolve_dep(osk_dlist *out_list, kbase_jd_atom *katom, u8 d)
{
	u8 other_d = !d;

	while (!OSK_DLIST_IS_EMPTY(&katom->dep_head[d]))
	{
		int err_1;
		kbase_jd_atom *dep_atom;
		kbase_jd_atom *trace_atom;

		if (d == 0) {
			trace_atom = OSK_DLIST_FRONT(&katom->dep_head[d], kbase_jd_atom, dep_item[d]);
			kbasep_list_trace_add(3, trace_atom->kctx->kbdev, trace_atom, &katom->dep_head[d], KBASE_TRACE_LIST_DEL, KBASE_TRACE_LIST_DEP_HEAD_0);
		}
		dep_atom = OSK_DLIST_POP_FRONT(&katom->dep_head[d], kbase_jd_atom, dep_item[d], err_1);
		if (err_1 && (d == 0)) {
			kbasep_list_trace_dump(trace_atom->kctx->kbdev);
			BUG();
		}

		dep_atom->dep_atom[d] = NULL;

		if (katom->event_code != BASE_JD_EVENT_DONE)
		{
			/* Atom failed, so remove the other dependencies and immediately fail the atom */
			if (dep_atom->dep_atom[other_d])
			{
				int err;
				if (other_d == 0)
					kbasep_list_trace_add(4, dep_atom->kctx->kbdev, dep_atom, &dep_atom->dep_atom[other_d]->dep_head[other_d], KBASE_TRACE_LIST_DEL, KBASE_TRACE_LIST_DEP_HEAD_0);
				OSK_DLIST_REMOVE(&dep_atom->dep_atom[other_d]->dep_head[other_d], dep_atom, dep_item[other_d], err);
				if (err) {
					kbasep_list_trace_dump(dep_atom->kctx->kbdev);
					BUG();
				}
				dep_atom->dep_atom[other_d] = NULL;
			}

#ifdef CONFIG_KDS
			if (!dep_atom->kds_dep_satisfied)
			{
				/* Just set kds_dep_satisfied to true. If the callback happens after this then it will early out and
				 * do nothing. If the callback doesn't happen then kbase_jd_post_external_resources will clean up
				 */
				dep_atom->kds_dep_satisfied = MALI_TRUE;
			}
#endif

			dep_atom->event_code = katom->event_code;
			OSK_ASSERT(dep_atom->status != KBASE_JD_ATOM_STATE_UNUSED);
			dep_atom->status = KBASE_JD_ATOM_STATE_COMPLETED;

			kbasep_list_trace_add(5, dep_atom->kctx->kbdev, dep_atom, out_list, KBASE_TRACE_LIST_ADD, KBASE_TRACE_LIST_RUNNABLE_JOBS);
			OSK_DLIST_PUSH_FRONT(out_list, dep_atom, kbase_jd_atom, dep_item[0]);
		}
		else if (!dep_atom->dep_atom[other_d])
		{
#ifdef CONFIG_KDS
			if (dep_atom->kds_dep_satisfied)
#endif
			{
				kbasep_list_trace_add(6, dep_atom->kctx->kbdev, dep_atom, out_list, KBASE_TRACE_LIST_ADD, KBASE_TRACE_LIST_RUNNABLE_JOBS);
				OSK_DLIST_PUSH_FRONT(out_list, dep_atom, kbase_jd_atom, dep_item[0]);
			}
		}
	}
}
KBASE_EXPORT_TEST_API(jd_resolve_dep)

/*
 * Perform the necessary handling of an atom that has finished running
 * on the GPU.
 *
 * Note that if this is a soft-job that has had kbase_prepare_soft_job called on it then the caller
 * is responsible for calling kbase_finish_soft_job *before* calling this function.
 *
 * The caller must hold the kbase_jd_context.lock.
 */
mali_bool jd_done_nolock(kbase_jd_atom *katom)
{
	struct kbase_context *kctx = katom->kctx;
	osk_dlist completed_jobs;
	osk_dlist runnable_jobs;
	mali_bool need_to_try_schedule_context = MALI_FALSE;
	int i;

	OSK_DLIST_INIT(&completed_jobs);
	OSK_DLIST_INIT(&runnable_jobs);

	OSK_ASSERT(katom->status != KBASE_JD_ATOM_STATE_UNUSED);

	/* This is needed in case an atom is failed due to being invalid, this
	 * can happen *before* the jobs that the atom depends on have completed */
	for(i = 0; i < 2; i++)
	{
		if (katom->dep_atom[i]) {
			int err;
			if (i == 0)
				kbasep_list_trace_add(7, katom->kctx->kbdev, katom, &katom->dep_atom[i]->dep_head[i], KBASE_TRACE_LIST_DEL, KBASE_TRACE_LIST_DEP_HEAD_0);
			OSK_DLIST_REMOVE(&katom->dep_atom[i]->dep_head[i], katom, dep_item[i], err);
			if (err) {
				kbasep_list_trace_dump(katom->kctx->kbdev);
				BUG();
			}
			katom->dep_atom[i] = NULL;
		}
	}

	katom->status = KBASE_JD_ATOM_STATE_COMPLETED;
	kbasep_list_trace_add(8, katom->kctx->kbdev, katom, &completed_jobs, KBASE_TRACE_LIST_ADD, KBASE_TRACE_LIST_COMPLETED_JOBS);
	OSK_DLIST_PUSH_BACK(&completed_jobs, katom, kbase_jd_atom, dep_item[0]);

	while(!OSK_DLIST_IS_EMPTY(&completed_jobs))
	{
		int err;
		kbase_jd_atom *katom_aux = OSK_DLIST_BACK(&completed_jobs, kbase_jd_atom, dep_item[0]);
		kbasep_list_trace_add(9, katom_aux->kctx->kbdev, katom_aux, &completed_jobs, KBASE_TRACE_LIST_DEL, KBASE_TRACE_LIST_COMPLETED_JOBS);
		katom = OSK_DLIST_POP_BACK(&completed_jobs, kbase_jd_atom, dep_item[0], err);
		if (err) {
			kbasep_list_trace_dump(katom->kctx->kbdev);
			BUG();
		}
		OSK_ASSERT(katom->status == KBASE_JD_ATOM_STATE_COMPLETED);

		for(i = 0; i < 2; i++)
		{
			jd_resolve_dep(&runnable_jobs, katom, i);
		}

		while (!OSK_DLIST_IS_EMPTY(&runnable_jobs))
		{
			int err;
			kbase_jd_atom *node;
			kbase_jd_atom *katom_aux = OSK_DLIST_BACK(&runnable_jobs, kbase_jd_atom, dep_item[0]);
			kbasep_list_trace_add(10, katom_aux->kctx->kbdev, katom_aux, &runnable_jobs, KBASE_TRACE_LIST_DEL, KBASE_TRACE_LIST_RUNNABLE_JOBS);
			node = OSK_DLIST_POP_BACK(&runnable_jobs, kbase_jd_atom, dep_item[0], err);
			if (err) {
				kbasep_list_trace_dump(node->kctx->kbdev);
				BUG();
			}
			OSK_ASSERT(node->status != KBASE_JD_ATOM_STATE_UNUSED);

			if (katom->event_code == BASE_JD_EVENT_DONE)
			{
				need_to_try_schedule_context |= jd_run_atom(node);
			}
			else
			{
				node->event_code = katom->event_code;
				node->status = KBASE_JD_ATOM_STATE_COMPLETED;

				if (node->core_req & BASE_JD_REQ_SOFT_JOB)
				{
					kbase_finish_soft_job(node);
				}
			}

			if (node->status == KBASE_JD_ATOM_STATE_COMPLETED)
			{
				kbasep_list_trace_add(11, node->kctx->kbdev, node, &completed_jobs, KBASE_TRACE_LIST_ADD, KBASE_TRACE_LIST_COMPLETED_JOBS);
				OSK_DLIST_PUSH_BACK(&completed_jobs, node, kbase_jd_atom, dep_item[0]);
			}
		}

		if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES)
		{
			kbase_jd_post_external_resources(katom);
		}

		kbase_event_post(kctx, katom);

		/* Decrement and check the TOTAL number of jobs. This includes
		 * those not tracked by the scheduler: 'not ready to run' and
		 * 'dependency-only' jobs. */
		if (--kctx->jctx.job_nr == 0)
		{
			/* All events are safely queued now, and we can signal any waiter
			 * that we've got no more jobs (so we can be safely terminated) */
			wake_up(&kctx->jctx.zero_jobs_wait);
		}
	}

	return need_to_try_schedule_context;
}
KBASE_EXPORT_TEST_API(jd_done_nolock)

static mali_bool jd_submit_atom(kbase_context *kctx, const base_jd_atom_v2 *user_atom)
{
	kbase_jd_context *jctx = &kctx->jctx;
	kbase_jd_atom *katom;
	base_jd_core_req core_req;
	base_atom_id atom_number = user_atom->atom_number;
	int queued = 0;
	int i;
	mali_bool ret;

	katom = &jctx->atoms[atom_number];

	mutex_lock(&jctx->lock);
	while (katom->status != KBASE_JD_ATOM_STATE_UNUSED)
	{
		/* Atom number is already in use, wait for the atom to complete */
		mutex_unlock(&jctx->lock);
		if (wait_event_killable(katom->completed, katom->status == KBASE_JD_ATOM_STATE_UNUSED))
		{
			/* We're being killed so the result code doesn't really matter */
			return MALI_FALSE;
		}
		mutex_lock(&jctx->lock);
	}

	/* Update the TOTAL number of jobs. This includes those not tracked by
	 * the scheduler: 'not ready to run' and 'dependency-only' jobs. */
	jctx->job_nr++;

	core_req = user_atom->core_req;

	if (kbase_hw_has_issue(kctx->kbdev, BASE_HW_ISSUE_8987))
	{
		/* For this HW workaround, we scheduled differently on the 'ONLY_COMPUTE'
		 * flag, at the expense of ignoring the NSS flag.
		 *
		 * NOTE: We could allow the NSS flag still (and just ensure that we still
		 * submit on slot 2 when the NSS flag is set), but we don't because:
		 * - If we only have NSS contexts, the NSS jobs get all the cores, delaying
		 * a non-NSS context from getting cores for a long time.
		 * - A single compute context won't be subject to any timers anyway -
		 * only when there are >1 contexts (GLES *or* CL) will it get subject to
		 * timers.
		 */
		core_req &= ~((base_jd_core_req)BASE_JD_REQ_NSS);
	}

	katom->udata        = user_atom->udata;
	katom->kctx         = kctx;
	katom->nr_extres    = user_atom->nr_extres;
	katom->extres       = NULL;
	katom->device_nr    = user_atom->device_nr;
	katom->affinity     = 0;
	katom->jc           = user_atom->jc;
	katom->coreref_state= KBASE_ATOM_COREREF_STATE_NO_CORES_REQUESTED;
	katom->core_req     = core_req;
	katom->nice_prio    = user_atom->prio;
#ifdef CONFIG_KDS
	/* Start by assuming that the KDS dependencies are satisfied,
	 * kbase_jd_pre_external_resources will correct this if there are dependencies */
	katom->kds_dep_satisfied = MALI_TRUE;
	katom->kds_rset     = NULL;
#endif /* CONFIG_KDS */

	/* Add dependencies */
	for(i = 0; i < 2; i++)
	{
		int dep_atom_number = user_atom->pre_dep[i];
		katom->dep_atom[i] = NULL;
		if (dep_atom_number)
		{
			kbase_jd_atom *dep_atom = &jctx->atoms[dep_atom_number];

			if (dep_atom->status == KBASE_JD_ATOM_STATE_UNUSED ||
			    dep_atom->status == KBASE_JD_ATOM_STATE_COMPLETED)
			{
				if (dep_atom->event_code != BASE_JD_EVENT_DONE)
				{
					if (i == 1 && katom->dep_atom[0])
					{
						int err;
						/* Remove the previous dependency */
						kbasep_list_trace_add(12, katom->kctx->kbdev, katom, &katom->dep_atom[0]->dep_head[0], KBASE_TRACE_LIST_DEL, KBASE_TRACE_LIST_DEP_HEAD_0);
						OSK_DLIST_REMOVE(&katom->dep_atom[0]->dep_head[0], katom, dep_item[0], err);
						if (err) {
							kbasep_list_trace_dump(katom->kctx->kbdev);
							BUG();
						}
						katom->dep_atom[0] = NULL;
					}
					/* Atom has completed, propagate the error code if any */
					katom->event_code = dep_atom->event_code;
					katom->status = KBASE_JD_ATOM_STATE_QUEUED;
					ret = jd_done_nolock(katom);
					goto out;
				}
			}
			else
			{
				/* Atom is in progress, add this atom to the list */
				if (i == 0)
					kbasep_list_trace_add(13, katom->kctx->kbdev, katom, &dep_atom->dep_head[i], KBASE_TRACE_LIST_ADD, KBASE_TRACE_LIST_DEP_HEAD_0);
				OSK_DLIST_PUSH_BACK(&dep_atom->dep_head[i], katom, kbase_jd_atom, dep_item[i]);
				katom->dep_atom[i] = dep_atom;
				queued = 1;
			}
		}
	}

	/* These must occur after the above loop to ensure that an atom that
	 * depends on a previous atom with the same number behaves as expected */
	katom->event_code = BASE_JD_EVENT_DONE;
	katom->status = KBASE_JD_ATOM_STATE_QUEUED;

	/*
	 * If the priority is increased we need to check the caller has security caps to do this, if
	 * priority is decreased then this is ok as the result will have no negative impact on other
	 * processes running.
	 */
	if( 0 > katom->nice_prio)
	{
		mali_bool access_allowed;
		access_allowed = kbase_security_has_capability(kctx, KBASE_SEC_MODIFY_PRIORITY, KBASE_SEC_FLAG_NOAUDIT);
		if(!access_allowed)
		{
			/* For unprivileged processes - a negative priority is interpreted as zero */
			katom->nice_prio = 0;
		}
	}

	/* Scale priority range to use NICE range */
	if(katom->nice_prio)
	{
		/* Remove sign for calculation */
		int nice_priority = katom->nice_prio+128;
		/* Fixed point maths to scale from ..255 to 0..39 (NICE range with +20 offset) */
		katom->nice_prio = (((20<<16)/128)*nice_priority)>>16;
	}

	if (katom->core_req & BASE_JD_REQ_EXTERNAL_RESOURCES)
	{
		/* handle what we need to do to access the external resources */
		if (MALI_ERROR_NONE != kbase_jd_pre_external_resources(katom, user_atom))
		{
			/* setup failed (no access, bad resource, unknown resource types, etc.) */
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
			ret = jd_done_nolock(katom);
			goto out;
		}
	}

	/* Initialize the jobscheduler policy for this atom. Function will
	 * return error if the atom is malformed.
	 *
	 * Soft-jobs never enter the job scheduler but have their own initialize method.
	 *
	 * If either fail then we immediately complete the atom with an error.
	 */
	if ((katom->core_req & BASE_JD_REQ_SOFT_JOB) == 0)
	{
		kbasep_js_policy *js_policy = &(kctx->kbdev->js_data.policy);
		if (MALI_ERROR_NONE != kbasep_js_policy_init_job( js_policy, kctx, katom ))
		{
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
			ret = jd_done_nolock(katom);
			goto out;
		}
	}
	else
	{
		/* Soft-job */
		if (MALI_ERROR_NONE != kbase_prepare_soft_job(katom))
		{
			katom->event_code = BASE_JD_EVENT_JOB_INVALID;
			ret = jd_done_nolock(katom);
			goto out;
		}
	}

	if (queued)
	{
		ret = MALI_FALSE;
		goto out;
	}
#ifdef CONFIG_KDS
	if (!katom->kds_dep_satisfied)
	{
		/* Queue atom due to KDS dependency */
		ret = MALI_FALSE;
		goto out;
	}
#endif /* CONFIG_KDS */

	if (katom->core_req & BASE_JD_REQ_SOFT_JOB)
	{
		if (kbase_process_soft_job(katom) == 0)
		{
			kbase_finish_soft_job(katom);
			ret = jd_done_nolock(katom);
			goto out;
		}
		/* The job has not yet completed */
		kbasep_list_trace_add(14, kctx->kbdev, katom, &kctx->waiting_soft_jobs, KBASE_TRACE_LIST_ADD, KBASE_TRACE_LIST_WAITING_SOFT_JOBS);
		OSK_DLIST_PUSH_BACK(&kctx->waiting_soft_jobs, katom, kbase_jd_atom, dep_item[0]);
		ret = MALI_FALSE;
	}
	else if ((katom->core_req & BASEP_JD_REQ_ATOM_TYPE) != BASE_JD_REQ_DEP)
	{
		katom->status = KBASE_JD_ATOM_STATE_IN_JS;
		ret = kbasep_js_add_job( kctx, katom );
	}
	else
	{
		/* This is a pure dependency. Resolve it immediately */
		ret = jd_done_nolock(katom);
	}

out:
	mutex_unlock(&jctx->lock);
	return ret;
}

mali_error kbase_jd_submit(kbase_context *kctx, const kbase_uk_job_submit *submit_data)
{
	mali_error err = MALI_ERROR_NONE;
	int i;
	mali_bool need_to_try_schedule_context = MALI_FALSE;
	kbase_device *kbdev;
	void * user_addr;

	/*
	 * kbase_jd_submit isn't expected to fail and so all errors with the jobs
	 * are reported by immediately falling them (through event system)
	 */
	kbdev = kctx->kbdev;

	beenthere("%s", "Enter");

	if ((kctx->jctx.sched_info.ctx.flags & KBASE_CTX_FLAG_SUBMIT_DISABLED) != 0)
	{
		OSK_PRINT_ERROR(OSK_BASE_JD, "Attempt to submit to a context that has SUBMIT_DISABLED set on it");
		return MALI_ERROR_FUNCTION_FAILED;
	}

	if (submit_data->stride != sizeof(base_jd_atom_v2))
	{
		OSK_PRINT_ERROR(OSK_BASE_JD, "Stride passed to job_submit doesn't match kernel");
		return MALI_ERROR_FUNCTION_FAILED;
	}

	user_addr = get_compat_pointer(&submit_data->addr);
	
	for(i = 0; i < submit_data->nr_atoms; i++)
	{
		base_jd_atom_v2 user_atom;

		if (ukk_copy_from_user(sizeof(user_atom), &user_atom, user_addr) != MALI_ERROR_NONE)
		{
			err = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

		user_addr = (void*)((uintptr_t)user_addr + submit_data->stride);

		need_to_try_schedule_context |= jd_submit_atom(kctx, &user_atom);
	}

	if ( need_to_try_schedule_context )
	{
		kbasep_js_try_schedule_head_ctx( kbdev );
	}

	return err;
}
KBASE_EXPORT_TEST_API(kbase_jd_submit)

/**
 * This function:
 * - requeues the job from the runpool (if it was soft-stopped/removed from NEXT registers)
 * - removes it from the system if it finished/failed/was cancelled.
 * - resolves dependencies to add dependent jobs to the context, potentially starting them if necessary (which may add more references to the context)
 * - releases the reference to the context from the no-longer-running job.
 * - Handles retrying submission outside of IRQ context if it failed from within IRQ context.
 */
static void jd_done_worker(struct work_struct *data)
{
	kbase_jd_atom *katom = container_of(data, kbase_jd_atom, work);
	kbase_jd_context *jctx;
	kbase_context *kctx;
	kbasep_js_kctx_info *js_kctx_info;
	kbasep_js_policy *js_policy;
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	u64 cache_jc = katom->jc;
	kbasep_js_atom_retained_state katom_retained_state;

	/* Soft jobs should never reach this function */
	OSK_ASSERT( (katom->core_req & BASE_JD_REQ_SOFT_JOB) == 0);

	kctx = katom->kctx;
	jctx = &kctx->jctx;
	kbdev = kctx->kbdev;
	js_kctx_info = &kctx->jctx.sched_info;

	js_devdata = &kbdev->js_data;
	js_policy = &kbdev->js_data.policy;

	KBASE_TRACE_ADD( kbdev, JD_DONE_WORKER, kctx, katom, katom->jc, 0 );
	/*
	 * Begin transaction on JD context and JS context
	 */
	mutex_lock( &jctx->lock );
	mutex_lock( &js_kctx_info->ctx.jsctx_mutex );

	/* This worker only gets called on contexts that are scheduled *in*. This is
	 * because it only happens in response to an IRQ from a job that was
	 * running.
	 */
	OSK_ASSERT( js_kctx_info->ctx.is_scheduled != MALI_FALSE );

	/* Release cores this job was using (this might power down unused cores, and
	 * cause extra latency if a job submitted here - such as depenedent jobs -
	 * would use those cores) */
	kbasep_js_job_check_deref_cores(kbdev, katom);

	/* Retain state before the katom disappears */
	kbasep_js_atom_retained_state_copy( &katom_retained_state, katom );

	if ( !kbasep_js_has_atom_finished(&katom_retained_state) )
	{
		unsigned long flags;
		/* Requeue the atom on soft-stop / removed from NEXT registers */
		OSK_PRINT_INFO(OSK_BASE_JM, "JS: Soft Stopped/Removed from next %p on Ctx %p; Requeuing", kctx );

		mutex_lock( &js_devdata->runpool_mutex );
		kbasep_js_clear_job_retry_submit( katom );

		spin_lock_irqsave( &js_devdata->runpool_irq.lock, flags);
		kbasep_js_policy_enqueue_job( js_policy, katom );
		spin_unlock_irqrestore( &js_devdata->runpool_irq.lock, flags);

		/* A STOPPED/REMOVED job must cause a re-submit to happen, in case it
		 * was the last job left. Crucially, work items on work queues can run
		 * out of order e.g. on different CPUs, so being able to submit from
		 * the IRQ handler is not a good indication that we don't need to run
		 * jobs; the submitted job could be processed on the work-queue
		 * *before* the stopped job, even though it was submitted after. */
		{
			int tmp;
			OSK_ASSERT( kbasep_js_get_atom_retry_submit_slot( &katom_retained_state, &tmp ) != MALI_FALSE );
			CSTD_UNUSED( tmp );
		}

		mutex_unlock( &js_devdata->runpool_mutex );
		mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );
	}
	else
	{
		/* Remove the job from the system for all other reasons */
		mali_bool need_to_try_schedule_context;

		kbasep_js_remove_job( kbdev, kctx, katom );
		mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );
		/* jd_done_nolock() requires the jsctx_mutex lock to be dropped */

		need_to_try_schedule_context = jd_done_nolock(katom);

		/* This ctx is already scheduled in, so return value guarenteed FALSE */
		OSK_ASSERT( need_to_try_schedule_context == MALI_FALSE );
	}
	/* katom may have been freed now, do not use! */

	/*
	 * Transaction complete
	 */
	mutex_unlock( &jctx->lock );

	/* Job is now no longer running, so can now safely release the context
	 * reference, and handle any actions that were logged against the atom's retained state */
	kbasep_js_runpool_release_ctx_and_katom_retained_state( kbdev, kctx, &katom_retained_state );

	KBASE_TRACE_ADD( kbdev, JD_DONE_WORKER_END, kctx, katom, cache_jc, 0 );
}

/**
 * Work queue job cancel function
 * Only called as part of 'Zapping' a context (which occurs on termination)
 * Operates serially with the jd_done_worker() on the work queue.
 *
 * This can only be called on contexts that aren't scheduled.
 *
 * @note We don't need to release most of the resources that would occur on
 * kbase_jd_done() or jd_done_worker(), because the atoms here must not be
 * running (by virtue of only being called on contexts that aren't
 * scheduled). The only resources that are an exception to this are:
 * - those held by kbasep_js_job_check_ref_cores(), because these resources are
 *   held for non-running atoms as well as running atoms.
 */
static void jd_cancel_worker(struct work_struct *data)
{
	kbase_jd_atom *katom = container_of(data, kbase_jd_atom, work);
	kbase_jd_context *jctx;
	kbase_context *kctx;
	kbasep_js_kctx_info *js_kctx_info;
	mali_bool need_to_try_schedule_context;
	kbase_device *kbdev;

	/* Soft jobs should never reach this function */
	OSK_ASSERT( (katom->core_req & BASE_JD_REQ_SOFT_JOB) == 0);

	kctx = katom->kctx;
	kbdev = kctx->kbdev;
	jctx = &kctx->jctx;
	js_kctx_info = &kctx->jctx.sched_info;

	KBASE_TRACE_ADD( kbdev, JD_CANCEL_WORKER, kctx, katom, katom->jc, 0 );

	/* This only gets called on contexts that are scheduled out. Hence, we must
	 * make sure we don't de-ref the number of running jobs (there aren't
	 * any), nor must we try to schedule out the context (it's already
	 * scheduled out).
	 */
	OSK_ASSERT( js_kctx_info->ctx.is_scheduled == MALI_FALSE );

	/* Release cores this job was using (this might power down unused cores) */
	kbasep_js_job_check_deref_cores(kctx->kbdev, katom);

	/* Scheduler: Remove the job from the system */
	mutex_lock( &js_kctx_info->ctx.jsctx_mutex );
	kbasep_js_remove_cancelled_job( kbdev, kctx, katom );
	mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );

	mutex_lock(&jctx->lock);

	need_to_try_schedule_context = jd_done_nolock(katom);
	/* Because we're zapping, we're not adding any more jobs to this ctx, so no need to
	 * schedule the context. There's also no need for the jsctx_mutex to have been taken
	 * around this too. */
	OSK_ASSERT( need_to_try_schedule_context == MALI_FALSE );

	/* katom may have been freed now, do not use! */
	mutex_unlock(&jctx->lock);

}

/**
 * @brief Complete a job that has been removed from the Hardware
 *
 * This must be used whenever a job has been removed from the Hardware, e.g.:
 * - An IRQ indicates that the job finished (for both error and 'done' codes)
 * - The job was evicted from the JSn_HEAD_NEXT registers during a Soft/Hard stop.
 *
 * Some work is carried out immediately, and the rest is deferred onto a workqueue
 *
 * This can be called safely from atomic context.
 *
 * The caller must hold kbasep_js_device_data::runpool_irq::lock
 *
 */
void kbase_jd_done(kbase_jd_atom *katom, int slot_nr, ktime_t *end_timestamp, mali_bool start_new_jobs)
{
	kbase_context *kctx;
	kbase_device *kbdev;
	OSK_ASSERT(katom);
	kctx = katom->kctx;
	OSK_ASSERT(kctx);
	kbdev = kctx->kbdev;
	OSK_ASSERT(kbdev);

	KBASE_TRACE_ADD( kbdev, JD_DONE, kctx, katom, katom->jc, 0 );

	kbasep_js_job_done_slot_irq( katom, slot_nr, end_timestamp, start_new_jobs );

	OSK_ASSERT(0 == object_is_on_stack(&katom->work));
	INIT_WORK(&katom->work, jd_done_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}
KBASE_EXPORT_TEST_API(kbase_jd_done)


void kbase_jd_cancel(kbase_jd_atom *katom)
{
	kbase_context *kctx;
	kbasep_js_kctx_info *js_kctx_info;
	kbase_device *kbdev;
	OSK_ASSERT(NULL != katom);
	kctx = katom->kctx;
	OSK_ASSERT(NULL != kctx);

	js_kctx_info = &kctx->jctx.sched_info;
	kbdev = kctx->kbdev;

	KBASE_TRACE_ADD( kbdev, JD_CANCEL, kctx, katom, katom->jc, 0 );

	/* This should only be done from a context that is not scheduled */
	OSK_ASSERT( js_kctx_info->ctx.is_scheduled == MALI_FALSE );

	katom->event_code = BASE_JD_EVENT_JOB_CANCELLED;

	OSK_ASSERT(0 == object_is_on_stack(&katom->work));
	INIT_WORK(&katom->work, jd_cancel_worker);
	queue_work(kctx->jctx.job_done_wq, &katom->work);
}

void kbase_jd_flush_workqueues(kbase_context *kctx)
{
	kbase_device *kbdev;
	int i;

	OSK_ASSERT( kctx );

	kbdev = kctx->kbdev;
	OSK_ASSERT( kbdev );

	flush_workqueue( kctx->jctx.job_done_wq );

	/* Flush all workqueues, for simplicity */
	for (i = 0; i < kbdev->nr_hw_address_spaces; i++)
	{
		flush_workqueue( kbdev->as[i].pf_wq );
	}
}

typedef struct zap_reset_data
{
	/* The stages are:
	 * 1. The timer has never been called
	 * 2. The zap has timed out, all slots are soft-stopped - the GPU reset will happen.
	 *    The GPU has been reset when kbdev->reset_waitq is signalled
	 *
	 * (-1 - The timer has been cancelled)
	 */
	int             stage;
	kbase_device    *kbdev;
	struct hrtimer  timer;
	spinlock_t      lock;
} zap_reset_data;

static enum hrtimer_restart  zap_timeout_callback( struct hrtimer * timer )
{
	zap_reset_data *reset_data = container_of( timer, zap_reset_data, timer );
	kbase_device *kbdev = reset_data->kbdev;
	unsigned long flags;

	spin_lock_irqsave(&reset_data->lock, flags);

	if (reset_data->stage == -1)
	{
		goto out;
	}

	if (kbase_prepare_to_reset_gpu(kbdev))
	{
		OSK_PRINT_WARN(OSK_BASE_JD, "NOTE: GPU will now be reset as a workaround for a hardware issue");
		kbase_reset_gpu(kbdev);
	}

	reset_data->stage = 2;

out:
	spin_unlock_irqrestore(&reset_data->lock, flags);

	return HRTIMER_NORESTART;
}

void kbase_jd_zap_context(kbase_context *kctx)
{
	kbase_device *kbdev;
	zap_reset_data reset_data;
	unsigned long flags;
	kbase_jd_atom *katom;
	OSK_ASSERT(kctx);

	kbdev = kctx->kbdev;

	KBASE_TRACE_ADD( kbdev, JD_ZAP_CONTEXT, kctx, NULL, 0u, 0u );
	kbase_job_zap_context(kctx);

	mutex_lock(&kctx->jctx.lock);
	OSK_DLIST_FOREACH(&kctx->waiting_soft_jobs, kbase_jd_atom, dep_item[0], katom)
	{
		kbase_cancel_soft_job(katom);
	}
	mutex_unlock(&kctx->jctx.lock);

	hrtimer_init_on_stack(&reset_data.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL );
	reset_data.timer.function = zap_timeout_callback;

	spin_lock_init(&reset_data.lock);

	reset_data.kbdev = kbdev;
	reset_data.stage = 1;

	hrtimer_start(&reset_data.timer, HR_TIMER_DELAY_MSEC(ZAP_TIMEOUT), HRTIMER_MODE_REL);

	/* Wait for all jobs to finish, and for the context to be not-scheduled
	 * (due to kbase_job_zap_context(), we also guarentee it's not in the JS
	 * policy queue either */
	wait_event(kctx->jctx.zero_jobs_wait, kctx->jctx.job_nr == 0);
	wait_event(kctx->jctx.sched_info.ctx.is_scheduled_wait, kctx->jctx.sched_info.ctx.is_scheduled == MALI_FALSE);

	spin_lock_irqsave(&reset_data.lock, flags);
	if (reset_data.stage == 1)
	{
		/* The timer hasn't run yet - so cancel it */
		reset_data.stage = -1;
	}
	spin_unlock_irqrestore(&reset_data.lock, flags);

	hrtimer_cancel(&reset_data.timer);

	if (reset_data.stage == 2)
	{
		/* The reset has already started.
		 * Wait for the reset to complete
		 */
		wait_event(kbdev->reset_wait, atomic_read(&kbdev->reset_gpu) == KBASE_RESET_GPU_NOT_PENDING);
	}
	destroy_hrtimer_on_stack(&reset_data.timer);

	OSK_PRINT_INFO(OSK_BASE_JM, "Zap: Finished Context %p", kctx );

	/* Ensure that the signallers of the waitqs have finished */
	mutex_lock(&kctx->jctx.lock);
	mutex_lock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	mutex_unlock(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	mutex_unlock(&kctx->jctx.lock);
}
KBASE_EXPORT_TEST_API(kbase_jd_zap_context)

mali_error kbase_jd_init(kbase_context *kctx)
{
	int i;
	mali_error mali_err = MALI_ERROR_NONE;
#ifdef CONFIG_KDS
	int err;
#endif /* CONFIG_KDS */

	OSK_ASSERT(kctx);

	/* Simulate failure to create the workqueue */
	if(OSK_SIMULATE_FAILURE(OSK_BASE_JD))
	{
		kctx->jctx.job_done_wq = NULL;
		mali_err = MALI_ERROR_OUT_OF_MEMORY;
		goto out1;
	}
	kctx->jctx.job_done_wq = alloc_workqueue("mali_jd", 0, 1);
	if (NULL == kctx->jctx.job_done_wq)
	{
		mali_err = MALI_ERROR_OUT_OF_MEMORY;
		goto out1;
	}

	for (i = 0; i < BASE_JD_ATOM_COUNT; i++)
	{
		init_waitqueue_head(&kctx->jctx.atoms[i].completed);

		OSK_DLIST_INIT(&kctx->jctx.atoms[i].dep_head[0]);
		OSK_DLIST_INIT(&kctx->jctx.atoms[i].dep_head[1]);

		/* Catch userspace attempting to use an atom which doesn't exist as a pre-dependency */
		kctx->jctx.atoms[i].event_code = BASE_JD_EVENT_JOB_INVALID;
		kctx->jctx.atoms[i].status = KBASE_JD_ATOM_STATE_UNUSED;
	}

	mutex_init(&kctx->jctx.lock);

	init_waitqueue_head(&kctx->jctx.zero_jobs_wait);

	spin_lock_init(&kctx->jctx.tb_lock);

#ifdef CONFIG_KDS
	err = kds_callback_init(&kctx->jctx.kds_cb, 0, kds_dep_clear);
	if (0 != err)
	{
		mali_err = MALI_ERROR_FUNCTION_FAILED;
		goto out2;
	}
#endif /* CONFIG_KDS */

	kctx->jctx.job_nr       = 0;

	return MALI_ERROR_NONE;

#ifdef CONFIG_KDS
out2:
#endif /* CONFIG_KDS */
	destroy_workqueue(kctx->jctx.job_done_wq);
out1:
	return mali_err;
}
KBASE_EXPORT_TEST_API(kbase_jd_init)

void kbase_jd_exit(kbase_context *kctx)
{
	OSK_ASSERT(kctx);

#ifdef CONFIG_KDS
	kds_callback_term(&kctx->jctx.kds_cb);
#endif /* CONFIG_KDS */
	/* Work queue is emptied by this */
	destroy_workqueue(kctx->jctx.job_done_wq);
}
KBASE_EXPORT_TEST_API(kbase_jd_exit)
