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
 * @file mali_kbase_instr.c
 * Base kernel instrumentation APIs for hardware revisions without BASE_HW_ISSUE_7115.
 */

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_midg_regmap.h>

#if (BASE_HW_ISSUE_7115 == 0)

static void kbase_instr_hwcnt_destroy(kbase_context * kctx);


static mali_error kbasep_instr_hwcnt_setup(kbase_context * kctx, kbase_uk_hwcnt_setup * setup)
{
	mali_error err = MALI_ERROR_NONE; /* let's be optimistic */
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	mali_bool access_allowed;
	u32 irq_mask;

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

	if ((setup->dump_buffer != 0ULL) && (NULL == kbdev->hwcnt_context))
	{
		/* Setup HW counters for the context */

		/* Enable interrupt */
		irq_mask = kbase_reg_read(kbdev,GPU_CONTROL_REG(GPU_IRQ_MASK),NULL);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask | PRFCNT_SAMPLE_COMPLETED, NULL);
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
#if BASE_HW_ISSUE_8186
			u32 val;
			/* Issue 8186 requires TILER_EN to be disabled before updating PRFCNT_CONFIG. We then restore the register contents */
			val = kbase_reg_read(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), kctx);
			if(0 != val)
			{
				kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), 0, kctx);
			}
			/* Now update PRFCNT_CONFIG with TILER_EN = 0 */
			kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), (kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT) | PRFCNT_CONFIG_MODE_MANUAL, kctx);
			/* Restore PRFCNT_TILER_EN */
			if(0 != val)
			{
				kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), val, kctx);
			}
#else
			kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), (kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT) | PRFCNT_CONFIG_MODE_MANUAL, kctx);
#endif
			/* Prevent the context to be scheduled out */
			kbasep_js_runpool_retain_ctx_nolock(kbdev, kctx);

			kbdev->hwcnt_is_setup = MALI_TRUE;
		}
		osk_spinlock_irq_unlock(&js_devdata->runpool_irq.lock);

		OSK_PRINT_INFO( OSK_BASE_CORE, "HW counters dumping set-up for context %p", kctx);
	}
	else if ((setup->dump_buffer == 0ULL) && (kctx == kbdev->hwcnt_context))
	{
		/* Disable HW counters for the context */

		/* disable interrupt */
		irq_mask = kbase_reg_read(kbdev,GPU_CONTROL_REG(GPU_IRQ_MASK),NULL);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask & ~PRFCNT_SAMPLE_COMPLETED, NULL);

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

		OSK_PRINT_INFO( OSK_BASE_CORE, "HW counters dumping disabled for context %p", kctx);
	}
	else
	{
		/* already in use or trying to disable while not owning */
		err = MALI_ERROR_FUNCTION_FAILED;
	}
	return err;
}
KBASE_EXPORT_TEST_API(kbase_destroy_context)

static mali_error kbasep_instr_hwcnt_dump(kbase_context * kctx)
{
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);

	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	/* Check it's the context previously set up and we're not already dumping */
	if (!(kbdev->hwcnt_context != kctx ||
	      MALI_TRUE == kbdev->hwcnt_in_progress ||
	      MALI_FALSE == kbdev->hwcnt_is_setup))
	{
		err = MALI_ERROR_NONE;

		/* Mark that we're dumping so the PF handler can signal that we faulted */
		kbdev->hwcnt_in_progress = MALI_TRUE;
		/* Start dumping */
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_PRFCNT_SAMPLE, kctx);
	}
	return err;
}

/**
 * @brief On dump complete, cleanup dump state.
 */

static mali_error kbasep_instr_hwcnt_dump_complete(kbase_device *kbdev)
{
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	OSK_ASSERT(NULL != kbdev);

	/* if hwcnt_in_progress is now MALI_FALSE we have faulted in some way */
	if (kbdev->hwcnt_in_progress)
	{
		/* success */
		err = MALI_ERROR_NONE;

		/* clear the mark for next time */
		kbdev->hwcnt_in_progress = MALI_FALSE;
	}

	/* reconfigure the dump address */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO), kbdev->hwcnt_addr & 0xFFFFFFFF, NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI), kbdev->hwcnt_addr >> 32,        NULL);

	return err;
}

/**
 * @brief Issue Cache Clean & Invalidate command to hardware
 */

static void kbasep_instr_hwcnt_cacheclean(kbase_device *kbdev)
{
	u32 irq_mask;

	OSK_ASSERT(NULL != kbdev);

	/* Enable interrupt */
	irq_mask = kbase_reg_read(kbdev,GPU_CONTROL_REG(GPU_IRQ_MASK),NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask | CLEAN_CACHES_COMPLETED, NULL);
	/* clean&invalidate the caches so we're sure the mmu tables for the dump buffer is valid */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_CLEAN_INV_CACHES, NULL);
	/* NOTE: PRLAM-5316 created as there is no way to know when the command has completed */
}

/**
 * @brief Issue Dump command to hardware
 */

mali_error kbase_instr_hwcnt_dump(kbase_context * kctx)
{
	mali_error err;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	while(1)
	{
		osk_spinlock_lock(&kbdev->hwcnt_lock);
		if(kbdev->hwcnt_state == KBASE_INSTR_STATE_DISABLED)
		{
			/* If the hw counters have been disabled - fail the dump request */
			osk_spinlock_unlock(&kbdev->hwcnt_lock);
			err = MALI_ERROR_FUNCTION_FAILED;
			break;
		}
		else if(kbdev->hwcnt_state != KBASE_INSTR_STATE_IDLE)
		{
			osk_spinlock_unlock(&kbdev->hwcnt_lock);
			/* Wait for idle state before retrying to obtain state change */
			osk_waitq_wait(&kbdev->hwcnt_waitqueue);
		}
		else
		{
			osk_waitq_clear(&kbdev->hwcnt_waitqueue);
			err = kbasep_instr_hwcnt_dump(kctx);
			if(MALI_ERROR_FUNCTION_FAILED == err)
			{
				/* Handle a failed dump request gracefully */
				osk_spinlock_unlock(&kbdev->hwcnt_lock);
				break;
			}
			kbdev->hwcnt_state = KBASE_INSTR_STATE_DUMPING;
			OSK_PRINT_INFO( OSK_BASE_CORE, "HW counters dumping done for context %p", kctx);
			osk_spinlock_unlock(&kbdev->hwcnt_lock);
			/* Wait for dump & cacheclean to complete */
			osk_waitq_wait(&kbdev->hwcnt_waitqueue);
			/* Detect dump failure */
			if(kbdev->hwcnt_state == KBASE_INSTR_STATE_ERROR)
			{
				err = MALI_ERROR_FUNCTION_FAILED;
				kbdev->hwcnt_state = KBASE_INSTR_STATE_IDLE;
				osk_waitq_set(&kbdev->hwcnt_waitqueue);
			}
			break;
		}
	}

	return err;
}

/**
 * @brief Initialize the Instrumentation hardware
 */

mali_error kbase_instr_hwcnt_setup(kbase_context * kctx, kbase_uk_hwcnt_setup * setup)
{
	mali_error err = MALI_ERROR_NONE;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);

	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	if(NULL == setup)
	{
		/* Bad parameter - abort */
		return MALI_ERROR_FUNCTION_FAILED;
	}

	osk_spinlock_lock(&kbdev->hwcnt_lock);

	if((setup->dump_buffer == 0ULL) && (kctx == kbdev->hwcnt_context))
	{
		/* Handle request to disable HW counters for the context */
		osk_spinlock_unlock(&kbdev->hwcnt_lock);
		kbase_instr_hwcnt_destroy(kctx);

		/* This context is not used for hwcnt anymore */
		kbase_pm_context_idle(kbdev);
	}
	else if((setup->dump_buffer != 0ULL) && (kbdev->hwcnt_state == KBASE_INSTR_STATE_DISABLED))
	{
		/* Precleaning so that state does not transition to IDLE */
		kbdev->hwcnt_state = KBASE_INSTR_STATE_PRECLEANING;
		osk_waitq_clear(&kbdev->hwcnt_waitqueue);

		osk_spinlock_unlock(&kbdev->hwcnt_lock);

		/* Mark this context as active as we don't want the GPU interrupts to be disabled */
		kbase_pm_context_active(kbdev);

		kbasep_instr_hwcnt_cacheclean(kbdev);

		/* Wait for cacheclean to complete */
		osk_waitq_wait(&kbdev->hwcnt_waitqueue);
		OSK_ASSERT(kbdev->hwcnt_state == KBASE_INSTR_STATE_CLEANED);
		
		/* Now setup the instrumentation hw registers */
		err = kbasep_instr_hwcnt_setup(kctx, setup);

		kbdev->hwcnt_state = KBASE_INSTR_STATE_IDLE;
		osk_waitq_set(&kbdev->hwcnt_waitqueue);
	}
	else
	{
		osk_spinlock_unlock(&kbdev->hwcnt_lock);
		err = MALI_ERROR_FUNCTION_FAILED;
	}

	return err;
}

/**
 * @brief Destory the Instrumentation context
 */

static void kbase_instr_hwcnt_destroy(kbase_context * kctx)
{
	mali_error err;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);

	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	while(1)
	{
		osk_spinlock_lock(&kbdev->hwcnt_lock);
		if(kbdev->hwcnt_state == KBASE_INSTR_STATE_IDLE)
		{
			/* kbase_instr_hwcnt_setup will need to be called to re-enable Instrumentation */
			kbdev->hwcnt_state = KBASE_INSTR_STATE_DISABLED;
			osk_waitq_clear(&kbdev->hwcnt_waitqueue);
			/* destroy instrumentation context */
			if(kbdev->hwcnt_context == kctx)
			{
				/* disable the use of the hw counters if the app didn't use the API correctly or crashed */
				kbase_uk_hwcnt_setup tmp;
				tmp.dump_buffer = 0ull;
				err = kbasep_instr_hwcnt_setup(kctx, &tmp);
				OSK_ASSERT(err != MALI_ERROR_FUNCTION_FAILED);
			}
			/* inform any other threads waiting for IDLE that state has changed to DISABLED */
			osk_waitq_set(&kbdev->hwcnt_waitqueue);
			osk_spinlock_unlock(&kbdev->hwcnt_lock);
			break;
		}
		else if(kbdev->hwcnt_state == KBASE_INSTR_STATE_DISABLED)
		{
			/* If Instrumentation was never enabled, do nothing */
			osk_spinlock_unlock(&kbdev->hwcnt_lock);
			break;
		}
		else
		{
			osk_spinlock_unlock(&kbdev->hwcnt_lock);
			osk_waitq_wait(&kbdev->hwcnt_waitqueue);
		}
	}
}

/**
 * @brief Dump complete interrupt received
 */

void kbase_instr_hwcnt_sample_done(kbase_device *kbdev)
{
	mali_error err;

	err = kbasep_instr_hwcnt_dump_complete(kbdev);
	/* Did the dump complete successfully? */
	if(err == MALI_ERROR_FUNCTION_FAILED)
	{
		/* Inform waiting thread of dump failure */
		kbdev->hwcnt_state = KBASE_INSTR_STATE_ERROR;
		osk_waitq_set(&kbdev->hwcnt_waitqueue);
	}
	else
	{
		/* Always clean and invalidate the cache after a successful dump */
		kbdev->hwcnt_state = KBASE_INSTR_STATE_POSTCLEANING;
		kbasep_instr_hwcnt_cacheclean(kbdev);
	}
}

/**
 * @brief Cache clean interrupt received
 */

void kbase_clean_caches_done(kbase_device *kbdev)
{
	u32 irq_mask;

	/* Disable interrupt */
	irq_mask = kbase_reg_read(kbdev,GPU_CONTROL_REG(GPU_IRQ_MASK),NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask & ~CLEAN_CACHES_COMPLETED, NULL);

	if(kbdev->hwcnt_state == KBASE_INSTR_STATE_PRECLEANING)
	{
		/* Don't return IDLE as we need kbase_instr_hwcnt_setup to continue rather than
		   allow access to another waiting thread */
		kbdev->hwcnt_state = KBASE_INSTR_STATE_CLEANED;
	}
	else
	{
		/* All finished and idle */
		kbdev->hwcnt_state = KBASE_INSTR_STATE_IDLE;
	}
	osk_waitq_set(&kbdev->hwcnt_waitqueue);
}
#endif /* BASE_HW_ISSUE_7115 */
