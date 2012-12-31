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
struct kbase_context *kbase_create_context(kbase_device *kbdev)
{
	struct kbase_context *kctx;
	struct kbase_va_region *pmem_reg;
	struct kbase_va_region *tmem_reg;
	osk_error osk_err;
	mali_error mali_err;

	OSK_ASSERT(kbdev != NULL);

	/* zero-inited as lot of code assume it's zero'ed out on create */
	kctx = osk_calloc(sizeof(*kctx));
	if (!kctx)
		goto out;

	kctx->kbdev = kbdev;
	kctx->as_nr = KBASEP_AS_NR_INVALID;

	if (kbase_mem_usage_init(&kctx->usage, kctx->kbdev->memdev.per_process_memory_limit >> OSK_PAGE_SHIFT))
	{
		goto free_kctx;
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

	osk_err = osk_mutex_init(&kctx->reg_lock, OSK_LOCK_ORDER_MEM_REG);
	if (OSK_ERR_NONE != osk_err)
		goto free_event;

	OSK_DLIST_INIT(&kctx->reg_list);

	osk_err = osk_phy_allocator_init(&kctx->pgd_allocator, 0, 0, NULL);
	if (OSK_ERR_NONE != osk_err)
		goto free_region_lock;

	mali_err = kbase_mmu_init(kctx);
	if(MALI_ERROR_NONE != mali_err)
		goto free_phy;

	kctx->pgd = kbase_mmu_alloc_pgd(kctx);
	if (!kctx->pgd)
		goto free_mmu;
	
	if (kbase_create_os_context(&kctx->osctx))
		goto free_pgd;

#if (BASE_HW_ISSUE_6787 && BASE_HW_ISSUE_6315)
	osk_err = osk_phy_allocator_init(&kctx->nulljob_allocator, 0, 0, NULL);
	if (OSK_ERR_NONE != osk_err)
		goto free_context;
	if (1 !=  osk_phy_pages_alloc(&kctx->nulljob_allocator, 1, &kctx->nulljob_pa))
		goto free_nulljob_allocinit;
	kctx->nulljob_va = osk_kmap(kctx->nulljob_pa);
	if (NULL == kctx->nulljob_va)
		goto free_nulljob_alloc;
	/* NOTE: we use page 1 of the reserved address region */
	mali_err = kbase_mmu_insert_pages(kctx, (u64)1, &kctx->nulljob_pa, 1, KBASE_REG_CPU_RW|KBASE_REG_GPU_RW);
	if(MALI_ERROR_NONE != mali_err)
		goto free_nulljob_kmap;
#endif

	/* Make sure page 0 is not used... */
	pmem_reg = kbase_alloc_free_region(kctx, 1,
	                                   KBASE_REG_ZONE_TMEM_BASE - 1, KBASE_REG_ZONE_PMEM);
	tmem_reg = kbase_alloc_free_region(kctx, KBASE_REG_ZONE_TMEM_BASE,
	                                   KBASE_REG_ZONE_TMEM_SIZE, KBASE_REG_ZONE_TMEM);

	if (!pmem_reg || !tmem_reg)
	{
		if (pmem_reg)
			kbase_free_alloced_region(pmem_reg);
		if (tmem_reg)
			kbase_free_alloced_region(tmem_reg);

		kbase_destroy_context(kctx);
		return NULL;
	}

	OSK_DLIST_PUSH_FRONT(&kctx->reg_list, pmem_reg, struct kbase_va_region, link);
	OSK_DLIST_PUSH_BACK(&kctx->reg_list, tmem_reg, struct kbase_va_region, link);

	return kctx;
#if (BASE_HW_ISSUE_6787 && BASE_HW_ISSUE_6315)
free_nulljob_kmap:
	osk_kunmap(kctx->nulljob_pa, kctx->nulljob_va);
free_nulljob_alloc:
	osk_phy_pages_free(&kctx->nulljob_allocator, 1, &kctx->nulljob_pa);
free_nulljob_allocinit:
	osk_phy_allocator_term(&kctx->nulljob_allocator);
free_context:
	kbase_destroy_os_context(&kctx->osctx);
#endif /* (BASE_HW_ISSUE_6787 && BASE_HW_ISSUE_6315) */
free_pgd:
	kbase_mmu_free_pgd(kctx);
free_mmu:
	kbase_mmu_term(kctx);
free_phy:
	osk_phy_allocator_term(&kctx->pgd_allocator);
free_region_lock:
	osk_mutex_term(&kctx->reg_lock);
free_event:
	kbase_event_cleanup(kctx);
free_jd:
	/* Safe to call this one even when didn't initialize (assuming kctx was sufficiently zeroed) */
	kbasep_js_kctx_term(kctx);
	kbase_jd_exit(kctx);
free_memctx:
	kbase_mem_usage_term(&kctx->usage);
free_kctx:
	osk_free(kctx);
out:
	return NULL;
	
}
KBASE_EXPORT_TEST_API(kbase_create_context)

/**
 * @brief Destroy a kernel base context.
 *
 * Destroy a kernel base context. Calls kbase_destroy_os_context() to
 * free OS specific structures. Will release all outstanding regions.
 */
void kbase_destroy_context(struct kbase_context *kctx)
{
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);

	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	KBASE_TRACE_ADD( kbdev, CORE_CTX_DESTROY, kctx, NULL, 0u, 0u );

	/* Ensure the core is powered up for the destroy process */
	kbase_pm_context_active(kbdev);

	if(kbdev->hwcnt_context == kctx)
	{
		/* disable the use of the hw counters if the app didn't use the API correctly or crashed */
		kbase_uk_hwcnt_setup tmp;

		KBASE_TRACE_ADD( kbdev, CORE_CTX_HWINSTR_TERM, kctx, NULL, 0u, 0u );
		OSK_PRINT_WARN(OSK_BASE_CTX,
					   "The privileged process asking for instrumentation forgot to disable it "
					   "before exiting. Will end instrumentation for them" );
		tmp.dump_buffer = 0ull;
		kbase_instr_hwcnt_setup(kctx, &tmp);
	}

	kbase_jd_zap_context(kctx);
	kbase_event_cleanup(kctx);

	kbase_gpu_vm_lock(kctx);

	/* MMU is disabled as part of scheduling out the context */
	kbase_mmu_free_pgd(kctx);
	osk_phy_allocator_term(&kctx->pgd_allocator);
	OSK_DLIST_EMPTY_LIST(&kctx->reg_list, struct kbase_va_region,
	                     link, kbase_free_alloced_region);
	kbase_destroy_os_context(&kctx->osctx);
	kbase_gpu_vm_unlock(kctx);

	/* Safe to call this one even when didn't initialize (assuming kctx was sufficiently zeroed) */
	kbasep_js_kctx_term(kctx);

	kbase_jd_exit(kctx);
	osk_mutex_term(&kctx->reg_lock);

	kbase_pm_context_idle(kbdev);
#if (BASE_HW_ISSUE_6787 && BASE_HW_ISSUE_6315)
	osk_kunmap(kctx->nulljob_pa, kctx->nulljob_va);
	osk_phy_pages_free(&kctx->nulljob_allocator, 1, &kctx->nulljob_pa);
	osk_phy_allocator_term(&kctx->nulljob_allocator);
#endif /* (BASE_HW_ISSUE_6787 && BASE_HW_ISSUE_6315) */

	kbase_mmu_term(kctx);

	kbase_mem_usage_term(&kctx->usage);
	osk_free(kctx);
}

mali_error kbase_instr_hwcnt_clear(kbase_context * kctx)
{
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);

	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	osk_spinlock_lock(&kbdev->hwcnt_lock);

	/* Check it's the context previously set up and we're not already dumping */
	if (kbdev->hwcnt_context != kctx ||
	    MALI_TRUE == kbdev->hwcnt_in_progress)
	{
		goto out;
	}

	/* Clear the counters */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_PRFCNT_CLEAR, kctx);

	err = MALI_ERROR_NONE;

out:
	osk_spinlock_unlock(&kbdev->hwcnt_lock);
	return err;
}

