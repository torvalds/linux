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



/**
 * @file mali_kbase_context.c
 * Base kernel context APIs
 */

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_midg_regmap.h>

/**
 * @brief Create a kernel base context.
 *
 * Allocate and init a kernel base context. Calls
 * kbase_create_os_context() to setup OS specific structures.
 */
kbase_context *kbase_create_context(kbase_device *kbdev)
{
	kbase_context *kctx;
	mali_error mali_err;

	OSK_ASSERT(kbdev != NULL);

	/* zero-inited as lot of code assume it's zero'ed out on create */
	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		kctx = NULL;
	}
	else
	{
		kctx = vzalloc(sizeof(*kctx));
	}

	if (!kctx)
		goto out;

	kctx->kbdev = kbdev;
	kctx->as_nr = KBASEP_AS_NR_INVALID;
	atomic_set(&kctx->setup_complete, 0);
	atomic_set(&kctx->setup_in_progress, 0);
	kctx->keep_gpu_powered = MALI_FALSE;

	spin_lock_init(&kctx->mm_update_lock);
	kctx->process_mm = NULL;
	atomic_set(&kctx->nonmapped_pages, 0);

	if (MALI_ERROR_NONE != kbase_mem_allocator_init(&kctx->osalloc, 16384))
	{
		goto free_kctx;
	}

	kctx->pgd_allocator = &kctx->osalloc;

	if (kbase_mem_usage_init(&kctx->usage, kctx->kbdev->memdev.per_process_memory_limit >> PAGE_SHIFT))
	{
		goto free_allocator;
	}

	if (kbase_jd_init(kctx))
		goto free_memctx;

	mali_err = kbasep_js_kctx_init( kctx );
	if ( MALI_ERROR_NONE != mali_err )
	{
		goto free_jd; /* safe to call kbasep_js_kctx_term  in this case */
	}

	mali_err = kbase_event_init(kctx);
	if (MALI_ERROR_NONE != mali_err)
		goto free_jd;

	mutex_init(&kctx->reg_lock);

	OSK_DLIST_INIT(&kctx->waiting_soft_jobs);

	mali_err = kbase_mmu_init(kctx);
	if(MALI_ERROR_NONE != mali_err)
		goto free_event;

	kctx->pgd = kbase_mmu_alloc_pgd(kctx);
	if (!kctx->pgd)
		goto free_mmu;
	
	if (kbase_create_os_context(&kctx->osctx))
		goto free_pgd;

	/* Make sure page 0 is not used... */
	if(kbase_region_tracker_init(kctx))
		goto free_osctx;

	return kctx;

free_osctx:
	kbase_destroy_os_context(&kctx->osctx);
free_pgd:
	kbase_mmu_free_pgd(kctx);
free_mmu:
	kbase_mmu_term(kctx);
free_event:
	kbase_event_cleanup(kctx);
free_jd:
	/* Safe to call this one even when didn't initialize (assuming kctx was sufficiently zeroed) */
	kbasep_js_kctx_term(kctx);
	kbase_jd_exit(kctx);
free_memctx:
	kbase_mem_usage_term(&kctx->usage);
free_allocator:
	kbase_mem_allocator_term(&kctx->osalloc);
free_kctx:
	vfree(kctx);
out:
	return NULL;
	
}
KBASE_EXPORT_SYMBOL(kbase_create_context)

/**
 * @brief Destroy a kernel base context.
 *
 * Destroy a kernel base context. Calls kbase_destroy_os_context() to
 * free OS specific structures. Will release all outstanding regions.
 */
void kbase_destroy_context(kbase_context *kctx)
{
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);

	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	KBASE_TRACE_ADD( kbdev, CORE_CTX_DESTROY, kctx, NULL, 0u, 0u );

	/* Ensure the core is powered up for the destroy process */
	kbase_pm_context_active(kbdev);

	if(kbdev->hwcnt.kctx == kctx)
	{
		/* disable the use of the hw counters if the app didn't use the API correctly or crashed */
		KBASE_TRACE_ADD( kbdev, CORE_CTX_HWINSTR_TERM, kctx, NULL, 0u, 0u );
		OSK_PRINT_WARN(OSK_BASE_CTX,
		               "The privileged process asking for instrumentation forgot to disable it "
		               "before exiting. Will end instrumentation for them" );
		kbase_instr_hwcnt_disable(kctx);
	}

	kbase_jd_zap_context(kctx);
	kbase_event_cleanup(kctx);

	kbase_gpu_vm_lock(kctx);

	/* MMU is disabled as part of scheduling out the context */
	kbase_mmu_free_pgd(kctx);
	kbase_region_tracker_term(kctx);
	kbase_destroy_os_context(&kctx->osctx);
	kbase_gpu_vm_unlock(kctx);

	/* Safe to call this one even when didn't initialize (assuming kctx was sufficiently zeroed) */
	kbasep_js_kctx_term(kctx);

	kbase_jd_exit(kctx);

	kbase_pm_context_idle(kbdev);

	kbase_mmu_term(kctx);

	kbase_mem_usage_term(&kctx->usage);

	if (kctx->keep_gpu_powered)
	{
		kbase_pm_context_idle(kbdev);
	}
	WARN_ON(atomic_read(&kctx->nonmapped_pages) != 0);

	kbase_mem_allocator_term(&kctx->osalloc);
	vfree(kctx);
}
KBASE_EXPORT_SYMBOL(kbase_destroy_context)

/**
 * Set creation flags on a context
 */
mali_error kbase_context_set_create_flags(kbase_context *kctx, u32 flags)
{
	mali_error err = MALI_ERROR_NONE;
	kbasep_js_kctx_info *js_kctx_info;
	OSK_ASSERT(NULL != kctx);

	if (OSK_SIMULATE_FAILURE(OSK_BASE_CTX))
	{
		return MALI_ERROR_FUNCTION_FAILED;
	}

	js_kctx_info = &kctx->jctx.sched_info;

	/* Validate flags */
	if ( flags != (flags & BASE_CONTEXT_CREATE_KERNEL_FLAGS) )
	{
		err = MALI_ERROR_FUNCTION_FAILED;
		goto out;
	}

	mutex_lock( &js_kctx_info->ctx.jsctx_mutex );

	/* Translate the flags */
	if ( (flags & BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED) == 0 )
	{
		js_kctx_info->ctx.flags &= ~((u32)KBASE_CTX_FLAG_SUBMIT_DISABLED);
	}

	if ( (flags & BASE_CONTEXT_HINT_ONLY_COMPUTE) != 0 )
	{
		js_kctx_info->ctx.flags |= (u32)KBASE_CTX_FLAG_HINT_ONLY_COMPUTE;
	}

	/* Latch the initial attributes into the Job Scheduler */
	kbasep_js_ctx_attr_set_initial_attrs( kctx->kbdev, kctx );
	
	mutex_unlock( &js_kctx_info->ctx.jsctx_mutex );
out:
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_context_set_create_flags)
