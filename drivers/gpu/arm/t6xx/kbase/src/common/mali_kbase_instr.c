/*
 *
 * (C) COPYRIGHT 2011-2012 ARM Limited. All rights reserved.
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
 * Base kernel instrumentation APIs.
 */

#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_midg_regmap.h>

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
}

/**
 * @brief Enable HW counters collection
 *
 * Note: will wait for a cache clean to complete
 */
mali_error kbase_instr_hwcnt_enable(kbase_context * kctx, kbase_uk_hwcnt_setup * setup)
{
	unsigned long flags;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbasep_js_device_data *js_devdata;
	mali_bool access_allowed;
	u32 irq_mask;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);
	OSK_ASSERT(NULL != setup);

	js_devdata = &kbdev->js_data;
	OSK_ASSERT(NULL != js_devdata);

	/* Determine if the calling task has access to this capability */
	access_allowed = kbase_security_has_capability(kctx, KBASE_SEC_INSTR_HW_COUNTERS_COLLECT, KBASE_SEC_FLAG_NOAUDIT);
	if (MALI_FALSE == access_allowed)
	{
		goto out;
	}

	if ((setup->dump_buffer == 0ULL) ||
	    (setup->dump_buffer & (2048-1)))
	{
		/* alignment failure */
		goto out;
	}

	/* Mark the context as active so the GPU is kept turned on */
	kbase_pm_context_active(kbdev);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING)
	{
		/* GPU is being reset*/
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}


	if (kbdev->hwcnt.state != KBASE_INSTR_STATE_DISABLED)
	{
		/* Instrumentation is already enabled */
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		kbase_pm_context_idle(kbdev);
		goto out;
	}

	if ( MALI_ERROR_NONE != kbase_pm_request_cores(kbdev,
						   kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_SHADER ),
						   kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_TILER ) ) )
	{
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		kbase_pm_context_idle(kbdev);
		goto out;
	}

	/* Enable interrupt */
	irq_mask = kbase_reg_read(kbdev,GPU_CONTROL_REG(GPU_IRQ_MASK),NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask | PRFCNT_SAMPLE_COMPLETED, NULL);

	/* In use, this context is the owner */
	kbdev->hwcnt.kctx = kctx;
	/* Remember the dump address so we can reprogram it later */
	kbdev->hwcnt.addr = setup->dump_buffer;

	/* Precleaning so that state does not transition to IDLE */
	kbdev->hwcnt.state = KBASE_INSTR_STATE_PRECLEANING;
	kbdev->hwcnt.triggered = 0;

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	/* Clean&invalidate the caches so we're sure the mmu tables for the dump buffer is valid */
	kbasep_instr_hwcnt_cacheclean(kbdev);
	/* Wait for cacheclean to complete */
	wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);
	OSK_ASSERT(kbdev->hwcnt.state == KBASE_INSTR_STATE_CLEANED);

	/* Schedule the context in */
	kbasep_js_schedule_privileged_ctx(kbdev, kctx);

	/* Configure */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO),     setup->dump_buffer & 0xFFFFFFFF, kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI),     setup->dump_buffer >> 32,        kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_JM_EN),       setup->jm_bm,                    kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_SHADER_EN),   setup->shader_bm,                kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_L3_CACHE_EN), setup->l3_cache_bm,              kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_MMU_L2_EN),   setup->mmu_l2_bm,                kctx);
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8186))
	{
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), 0, kctx);
	}

	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), (kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT) | PRFCNT_CONFIG_MODE_MANUAL, kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN),    setup->tiler_bm,                 kctx);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING)
	{
		/* GPU is being reset*/
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}

	kbdev->hwcnt.state = KBASE_INSTR_STATE_IDLE;
	kbdev->hwcnt.triggered = 1;
	wake_up(&kbdev->hwcnt.wait);

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	err = MALI_ERROR_NONE;

	OSK_PRINT_INFO( OSK_BASE_CORE, "HW counters dumping set-up for context %p", kctx);

out:
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_enable)

/**
 * @brief Disable HW counters collection
 *
 * Note: might sleep, waiting for an ongoing dump to complete
 */
mali_error kbase_instr_hwcnt_disable(kbase_context * kctx)
{
	unsigned long flags;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	u32 irq_mask;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	while (1)
	{
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

		if (kbdev->hwcnt.state == KBASE_INSTR_STATE_DISABLED)
		{
			/* Instrumentation is not enabled */
			spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
			goto out;
		}

		if (kbdev->hwcnt.kctx != kctx)
		{
			/* Instrumentation has been setup for another context */
			spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
			goto out;
		}

		if (kbdev->hwcnt.state == KBASE_INSTR_STATE_IDLE)
		{
			break;
		}

		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

		/* Ongoing dump/setup - wait for its completion */
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);


	}

	kbdev->hwcnt.state = KBASE_INSTR_STATE_DISABLED;
	kbdev->hwcnt.triggered = 0;

	/* Disable interrupt */
	irq_mask = kbase_reg_read(kbdev,GPU_CONTROL_REG(GPU_IRQ_MASK),NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask & ~PRFCNT_SAMPLE_COMPLETED, NULL);

	/* Disable the counters */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), 0, kctx);

	kbdev->hwcnt.kctx = NULL;
	kbdev->hwcnt.addr = 0ULL;

	kbase_pm_unrequest_cores(kbdev,
						   kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_SHADER ),
						   kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_TILER ) );

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	/* Release the context, this implicitly (and indirectly) calls kbase_pm_context_idle */
	kbasep_js_release_privileged_ctx(kbdev, kctx);

	OSK_PRINT_INFO( OSK_BASE_CORE, "HW counters dumping disabled for context %p", kctx);

	err = MALI_ERROR_NONE;

out:
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_disable)

/**
 * @brief Configure HW counters collection
 */
mali_error kbase_instr_hwcnt_setup(kbase_context * kctx, kbase_uk_hwcnt_setup * setup)
{
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);

	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	if (NULL == setup)
	{
		/* Bad parameter - abort */
		goto out;
	}

	if (setup->dump_buffer != 0ULL)
	{
		/* Enable HW counters */
		err = kbase_instr_hwcnt_enable(kctx, setup);
	}
	else
	{
		/* Disable HW counters */
		err = kbase_instr_hwcnt_disable(kctx);
	}

out:
	return err;
}

/**
 * @brief Issue Dump command to hardware
 */
mali_error kbase_instr_hwcnt_dump_irq(kbase_context * kctx)
{
	unsigned long flags;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	OSK_ASSERT(kbdev->hwcnt.state != KBASE_INSTR_STATE_RESETTING);

	if (kbdev->hwcnt.kctx != kctx)
	{
		 /* The instrumentation has been setup for another context */
		goto unlock;
	}

	if (kbdev->hwcnt.state != KBASE_INSTR_STATE_IDLE)
	{
		/* HW counters are disabled or another dump is ongoing */
		goto unlock;
	}

	kbdev->hwcnt.triggered = 0;

	/* Mark that we're dumping - the PF handler can signal that we faulted */
	kbdev->hwcnt.state = KBASE_INSTR_STATE_DUMPING;

	/* Reconfigure the dump address */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO), kbdev->hwcnt.addr & 0xFFFFFFFF, NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI), kbdev->hwcnt.addr >> 32,        NULL);

	/* Start dumping */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_PRFCNT_SAMPLE, kctx);

	OSK_PRINT_INFO( OSK_BASE_CORE, "HW counters dumping done for context %p", kctx);

	err = MALI_ERROR_NONE;

unlock:
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_dump_irq)

/**
 * @brief Tell whether the HW counters dump has completed
 *
 * Notes:
 * - does not sleep
 * - success will be set to MALI_TRUE if the dump succeeded or
 *   MALI_FALSE on failure
 */
mali_bool kbase_instr_hwcnt_dump_complete(kbase_context * kctx, mali_bool *success)
{
	unsigned long flags;
	mali_bool complete = MALI_FALSE;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);
	OSK_ASSERT(NULL != success);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_IDLE)
	{
		*success = MALI_TRUE;
		complete = MALI_TRUE;
	}
	else if (kbdev->hwcnt.state == KBASE_INSTR_STATE_FAULT)
	{
		*success = MALI_FALSE;
		complete = MALI_TRUE;
		kbdev->hwcnt.state = KBASE_INSTR_STATE_IDLE;
	}

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	return complete;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_dump_complete)

/**
 * @brief Issue Dump command to hardware and wait for completion
 */
mali_error kbase_instr_hwcnt_dump(kbase_context * kctx)
{
	unsigned long flags;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	err = kbase_instr_hwcnt_dump_irq(kctx);
	if (MALI_ERROR_NONE != err)
	{
		 /* Can't dump HW counters */
		goto out;
	}

	/* Wait for dump & cacheclean to complete */
	wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING)
	{
		/* GPU is being reset*/
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_FAULT)
	{
		err = MALI_ERROR_FUNCTION_FAILED;
		kbdev->hwcnt.state = KBASE_INSTR_STATE_IDLE;
	}
	else
	{
		/* Dump done */
		OSK_ASSERT(kbdev->hwcnt.state == KBASE_INSTR_STATE_IDLE);
		err = MALI_ERROR_NONE;
	}

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
out:
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_dump)

/**
 * @brief Clear the HW counters
 */
mali_error kbase_instr_hwcnt_clear(kbase_context * kctx)
{
	unsigned long flags;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbase_device *kbdev;

	OSK_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	OSK_ASSERT(NULL != kbdev);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING)
	{
		/* GPU is being reset*/
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}

	/* Check it's the context previously set up and we're not already dumping */
	if (kbdev->hwcnt.kctx != kctx ||
	    kbdev->hwcnt.state != KBASE_INSTR_STATE_IDLE)
	{
		goto out;
	}

	/* Clear the counters */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_PRFCNT_CLEAR, kctx);

	err = MALI_ERROR_NONE;

out:
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_clear)

/**
 * @brief Dump complete interrupt received
 */
void kbase_instr_hwcnt_sample_done(kbase_device *kbdev)
{
	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_FAULT)
	{
		kbdev->hwcnt.triggered = 1;
		wake_up(&kbdev->hwcnt.wait);
	}
	else
	{
		/* Always clean and invalidate the cache after a successful dump */
		kbdev->hwcnt.state = KBASE_INSTR_STATE_POSTCLEANING;
		kbasep_instr_hwcnt_cacheclean(kbdev);
	}
}

/**
 * @brief Cache clean interrupt received
 */
void kbase_clean_caches_done(kbase_device *kbdev)
{
	u32 irq_mask;

	if (kbdev->hwcnt.state != KBASE_INSTR_STATE_DISABLED)
	{
		/* Disable interrupt */
		irq_mask = kbase_reg_read(kbdev,GPU_CONTROL_REG(GPU_IRQ_MASK),NULL);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask & ~CLEAN_CACHES_COMPLETED, NULL);

		if (kbdev->hwcnt.state == KBASE_INSTR_STATE_PRECLEANING)
		{
			/* Don't return IDLE as we need kbase_instr_hwcnt_setup to continue rather than
			   allow access to another waiting thread */
			kbdev->hwcnt.state = KBASE_INSTR_STATE_CLEANED;
		}
		else
		{
			/* All finished and idle */
			kbdev->hwcnt.state = KBASE_INSTR_STATE_IDLE;
		}

		kbdev->hwcnt.triggered = 1;
		wake_up(&kbdev->hwcnt.wait);

	}
}
