/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_instr_7115.c
 * Base kernel instrumentation APIs for hardware revisions with BASE_HW_ISSUE_7115.
 */

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_midg_regmap.h>

#if BASE_HW_ISSUE_7115

mali_error kbase_instr_hwcnt_setup(kbase_context * kctx, kbase_uk_hwcnt_setup * setup)
{
	mali_error err = MALI_ERROR_NONE; /* let's be optimistic */
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	mali_bool access_allowed;

	OSK_ASSERT(NULL != kctx);
	OSK_ASSERT(NULL != setup);

	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	js_devdata = &kbdev->js_data;
	OSK_ASSERT(NULL != js_devdata);

	/* Determine if the calling task has access to this capability */
	access_allowed = kbase_security_has_capability(kctx, KBASE_SEC_INSTR_HW_COUNTERS_COLLECT, KBASE_SEC_FLAG_NOAUDIT);
	if (MALI_FALSE == access_allowed )
	{
		return MALI_ERROR_FUNCTION_FAILED;
	}

	if (setup->dump_buffer & (2048-1))
	{
		/* alignment failure */
		return MALI_ERROR_FUNCTION_FAILED;
	}

	/* dumping in progress without hardware context ? */
	OSK_ASSERT(((kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_STATUS), kctx) & GPU_STATUS_PRFCNT_ACTIVE)) == 0);

	/* Mark this context as active while we set the hwcnt up */
	kbase_pm_context_active(kbdev);

	osk_spinlock_lock(&kbdev->hwcnt_lock);

	if ((setup->dump_buffer != 0ULL) && (NULL == kbdev->hwcnt_context))
	{
		/* Setup HW counters for the context */

		/* clean&invalidate the caches so we're sure the mmu tables for the dump buffer is valid */
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_CLEAN_INV_CACHES, kctx);
		/* NOTE: PRLAM-5316 created as there is no way to know when the command has completed */

		/* configure */
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO),     setup->dump_buffer & 0xFFFFFFFF, kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI),     setup->dump_buffer >> 32,        kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_JM_EN),       setup->jm_bm,                    kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_SHADER_EN),   setup->shader_bm,                kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN),    setup->tiler_bm,                 kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_L3_CACHE_EN), setup->l3_cache_bm,              kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_MMU_L2_EN),   setup->mmu_l2_bm,                kctx);

		/* mark as in use and that this context is the owner */
		kbdev->hwcnt_context = kctx;
		/* remember the dump address so we can reprogram it later */
		kbdev->hwcnt_addr = setup->dump_buffer;

		osk_spinlock_irq_lock(&js_devdata->runpool_irq.lock);
		if (kctx->as_nr != KBASEP_AS_NR_INVALID)
		{
			/* Setup the base address */
			kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), (kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT) | PRFCNT_CONFIG_MODE_MANUAL, kctx);
			/* Prevent the context to be scheduled out */
			kbasep_js_runpool_retain_ctx_nolock(kbdev, kctx);

			kbdev->hwcnt_is_setup = MALI_TRUE;
		}
		osk_spinlock_irq_unlock(&js_devdata->runpool_irq.lock);
		osk_spinlock_unlock(&kbdev->hwcnt_lock);

		/* Mark this context as active as we don't want the GPU interrupts to be disabled while thye hwcnt are enabled */
		kbase_pm_context_active(kbdev);

		OSK_PRINT_INFO( OSK_BASE_CORE, "HW counters dumping set-up for context %p", kctx);
	}
	else if ((setup->dump_buffer == 0ULL) && (kctx == kbdev->hwcnt_context))
	{
		/* Disable HW counters for the context */
		if (MALI_TRUE == kbdev->hwcnt_is_setup)
		{
			/* Disable the counters */
			kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), 0, kctx);

			kbdev->hwcnt_is_setup = MALI_FALSE;

			/* We need to release the spinlock while calling runpool_release */
			osk_spinlock_unlock(&kbdev->hwcnt_lock);
			/* Release the context */
			kbasep_js_runpool_release_ctx(kbdev, kctx);
			osk_spinlock_lock(&kbdev->hwcnt_lock);
		}

		kbdev->hwcnt_context = NULL;
		kbdev->hwcnt_addr = 0ULL;

		osk_spinlock_unlock(&kbdev->hwcnt_lock);
		/* This context will not be used for hwcnt anymore */
		kbase_pm_context_idle(kbdev);

		OSK_PRINT_INFO( OSK_BASE_CORE, "HW counters dumping disabled for context %p", kctx);
	}
	else
	{
		/* already in use or trying to disable while not owning */
		err = MALI_ERROR_FUNCTION_FAILED;
		osk_spinlock_unlock(&kbdev->hwcnt_lock);
	}

	/* We don't need to keep this context alive (except if hwcnt have been enabled) */
	kbase_pm_context_idle(kbdev);

	return err;
}
KBASE_EXPORT_TEST_API(kbase_destroy_context)

mali_error kbase_instr_hwcnt_dump(kbase_context * kctx)
{
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);

	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	osk_spinlock_lock(&kbdev->hwcnt_lock);
	{
		/* Check it's the context previously set up and we're not already dumping */
		if (kbdev->hwcnt_context != kctx ||
		    MALI_TRUE == kbdev->hwcnt_in_progress ||
		    MALI_FALSE == kbdev->hwcnt_is_setup)
		{
			osk_spinlock_unlock(&kbdev->hwcnt_lock);
			goto out;
		}

		/* Mark that we're dumping so the PF handler can signal that we faulted */
		kbdev->hwcnt_in_progress = MALI_TRUE;
	}
	osk_spinlock_unlock(&kbdev->hwcnt_lock);
#define WAIT_HI_LOOPS 10
#define WAIT_LOW_LOOPS 10000000ULL

	/* Start dumping */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_PRFCNT_SAMPLE, kctx);

	if (kbase_device_has_feature(kbdev, KBASE_FEATURE_DELAYED_PERF_WRITE_STATUS))
	{
		u64 i;
		/* the model needs a few cycles to set the bit to indicate it's dumping */
		for (i = 0; i < WAIT_HI_LOOPS; i++)
		{
			if ((kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_STATUS), kctx) & GPU_STATUS_PRFCNT_ACTIVE))
			{
				break;
			}
		}
		if (WAIT_HI_LOOPS == i)
		{
			OSK_PRINT_WARN(OSK_BASE_CORE, "Bit didn't go high, failed to dump hardware counters\n");
			goto out;
		}
	}

	{
		u64 i;
		for ( i = 0; i < WAIT_LOW_LOOPS; i++)
		{
			if ( 0 == (kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_STATUS), kctx) & GPU_STATUS_PRFCNT_ACTIVE))
			{
				break;
			}
		}
		if (WAIT_LOW_LOOPS == i)
		{
			OSK_PRINT_WARN(OSK_BASE_CORE, "Dump active bit stuck high, failed to dump hardware counters\n");
			goto out;
		}
	}

	osk_spinlock_lock(&kbdev->hwcnt_lock);
	{
		/* if hwcnt_in_progress is now MALI_FALSE we have faulted in some way */
		if (kbdev->hwcnt_in_progress)
		{
			/* clear cache to commit the samples to main memory */
			kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_CLEAN_CACHES, kctx);
			/* NOTE: PRLAM-5316 created as there is no way to know that the cache clear has completed */

			/* success */
			err = MALI_ERROR_NONE;

			/* clear the mark for next time */
			kbdev->hwcnt_in_progress = MALI_FALSE;
		}

		/* reconfigure the dump address */
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO), kbdev->hwcnt_addr & 0xFFFFFFFF, kctx);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI), kbdev->hwcnt_addr >> 32,        kctx);
	}

	osk_spinlock_unlock(&kbdev->hwcnt_lock);

	OSK_PRINT_INFO( OSK_BASE_CORE, "HW counters dumping done for context %p", kctx);

out:
	return err;
}

void kbase_instr_hwcnt_sample_done(kbase_device *kbdev)
{
	/* Dummy func, BASE_HW_ISSUE_7115 does not support interrupt on sample done */
}

void kbase_clean_caches_done(kbase_device *kbdev)
{
	/* Dummy func, BASE_HW_ISSUE_7115 does not support interrupt on cache clean done */
}
#endif /* BASE_HW_ISSUE_7115 */
