/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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





/**
 * @file mali_kbase_instr.c
 * Base kernel instrumentation APIs.
 */

#include <mali_kbase.h>
#include <mali_midg_regmap.h>

/**
 * @brief Issue Cache Clean & Invalidate command to hardware
 */
static void kbasep_instr_hwcnt_cacheclean(kbase_device *kbdev)
{
	unsigned long flags;
	unsigned long pm_flags;
	u32 irq_mask;

	KBASE_DEBUG_ASSERT(NULL != kbdev);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	/* Wait for any reset to complete */
	while (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING) {
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.cache_clean_wait,
		           kbdev->hwcnt.state != KBASE_INSTR_STATE_RESETTING);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}
	KBASE_DEBUG_ASSERT(kbdev->hwcnt.state == KBASE_INSTR_STATE_REQUEST_CLEAN);

	/* Enable interrupt */
	spin_lock_irqsave(&kbdev->pm.power_change_lock, pm_flags);
	irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask | CLEAN_CACHES_COMPLETED, NULL);
	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, pm_flags);

	/* clean&invalidate the caches so we're sure the mmu tables for the dump buffer is valid */
	KBASE_TRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, NULL, 0u, 0);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_CLEAN_INV_CACHES, NULL);
	kbdev->hwcnt.state = KBASE_INSTR_STATE_CLEANING;

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
}

STATIC mali_error kbase_instr_hwcnt_enable_internal(kbase_device *kbdev, kbase_context *kctx, kbase_uk_hwcnt_setup *setup)
{
	unsigned long flags, pm_flags;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbasep_js_device_data *js_devdata;
	u32 irq_mask;
	int ret;
	u64 shader_cores_needed;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	KBASE_DEBUG_ASSERT(NULL != kbdev);
	KBASE_DEBUG_ASSERT(NULL != setup);
	KBASE_DEBUG_ASSERT(NULL == kbdev->hwcnt.suspended_kctx);

	shader_cores_needed = kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_SHADER);

	js_devdata = &kbdev->js_data;

	/* alignment failure */
	if ((setup->dump_buffer == 0ULL) || (setup->dump_buffer & (2048 - 1)))
		goto out_err;

	/* Override core availability policy to ensure all cores are available */
	kbase_pm_ca_instr_enable(kbdev);

	/* Mark the context as active so the GPU is kept turned on */
	/* A suspend won't happen here, because we're in a syscall from a userspace
	 * thread. */
	kbase_pm_context_active(kbdev);

	/* Request the cores early on synchronously - we'll release them on any errors
	 * (e.g. instrumentation already active) */
	kbase_pm_request_cores_sync(kbdev, MALI_TRUE, shader_cores_needed);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING) {
		/* GPU is being reset */
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}

	if (kbdev->hwcnt.state != KBASE_INSTR_STATE_DISABLED) {
		/* Instrumentation is already enabled */
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		goto out_unrequest_cores;
	}

	/* Enable interrupt */
	spin_lock_irqsave(&kbdev->pm.power_change_lock, pm_flags);
	irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask | PRFCNT_SAMPLE_COMPLETED, NULL);
	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, pm_flags);

	/* In use, this context is the owner */
	kbdev->hwcnt.kctx = kctx;
	/* Remember the dump address so we can reprogram it later */
	kbdev->hwcnt.addr = setup->dump_buffer;
	/* Remember all the settings for suspend/resume */
	if (&kbdev->hwcnt.suspended_state != setup)
		memcpy(&kbdev->hwcnt.suspended_state, setup, sizeof(kbdev->hwcnt.suspended_state));

	/* Request the clean */
	kbdev->hwcnt.state = KBASE_INSTR_STATE_REQUEST_CLEAN;
	kbdev->hwcnt.triggered = 0;
	/* Clean&invalidate the caches so we're sure the mmu tables for the dump buffer is valid */
	ret = queue_work(kbdev->hwcnt.cache_clean_wq, &kbdev->hwcnt.cache_clean_work);
	KBASE_DEBUG_ASSERT(ret);

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	/* Wait for cacheclean to complete */
	wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);

	KBASE_DEBUG_ASSERT(kbdev->hwcnt.state == KBASE_INSTR_STATE_IDLE);

	/* Schedule the context in */
	kbasep_js_schedule_privileged_ctx(kbdev, kctx);

	/* Configure */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), (kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT) | PRFCNT_CONFIG_MODE_OFF, kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO),     setup->dump_buffer & 0xFFFFFFFF, kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI),     setup->dump_buffer >> 32,        kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_JM_EN),       setup->jm_bm,                    kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_SHADER_EN),   setup->shader_bm,                kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_L3_CACHE_EN), setup->l3_cache_bm,              kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_MMU_L2_EN),   setup->mmu_l2_bm,                kctx);
	/* Due to PRLAM-8186 we need to disable the Tiler before we enable the HW counter dump. */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8186))
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), 0, kctx);
	else
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), setup->tiler_bm, kctx);

	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), (kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT) | PRFCNT_CONFIG_MODE_MANUAL, kctx);

	/* If HW has PRLAM-8186 we can now re-enable the tiler HW counters dump */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8186))
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), setup->tiler_bm, kctx);
	
	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING) {
		/* GPU is being reset */
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}

	kbdev->hwcnt.state = KBASE_INSTR_STATE_IDLE;
	kbdev->hwcnt.triggered = 1;
	wake_up(&kbdev->hwcnt.wait);

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	err = MALI_ERROR_NONE;

	KBASE_LOG(1, kbdev->dev, "HW counters dumping set-up for context %p", kctx);
	return err;
 out_unrequest_cores:
	kbase_pm_unrequest_cores(kbdev, MALI_TRUE, shader_cores_needed);
	kbase_pm_context_idle(kbdev);
 out_err:
	return err;
}

/**
 * @brief Enable HW counters collection
 *
 * Note: will wait for a cache clean to complete
 */
mali_error kbase_instr_hwcnt_enable(kbase_context *kctx, kbase_uk_hwcnt_setup *setup)
{
	kbase_device *kbdev;
	mali_bool access_allowed;
	kbdev = kctx->kbdev;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	/* Determine if the calling task has access to this capability */
	access_allowed = kbase_security_has_capability(kctx, KBASE_SEC_INSTR_HW_COUNTERS_COLLECT, KBASE_SEC_FLAG_NOAUDIT);
	if (MALI_FALSE == access_allowed)
		return MALI_ERROR_FUNCTION_FAILED;

	return kbase_instr_hwcnt_enable_internal(kbdev, kctx, setup);
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_enable)

/**
 * @brief Disable HW counters collection
 *
 * Note: might sleep, waiting for an ongoing dump to complete
 */
mali_error kbase_instr_hwcnt_disable(kbase_context *kctx)
{
	unsigned long flags, pm_flags;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	u32 irq_mask;
	kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(NULL != kbdev);

	while (1) {
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

		if (kbdev->hwcnt.state == KBASE_INSTR_STATE_DISABLED) {
			/* Instrumentation is not enabled */
			spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
			goto out;
		}

		if (kbdev->hwcnt.kctx != kctx) {
			/* Instrumentation has been setup for another context */
			spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
			goto out;
		}

		if (kbdev->hwcnt.state == KBASE_INSTR_STATE_IDLE)
			break;

		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

		/* Ongoing dump/setup - wait for its completion */
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);

	}

	kbdev->hwcnt.state = KBASE_INSTR_STATE_DISABLED;
	kbdev->hwcnt.triggered = 0;

	/* Disable interrupt */
	spin_lock_irqsave(&kbdev->pm.power_change_lock, pm_flags);
	irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask & ~PRFCNT_SAMPLE_COMPLETED, NULL);
	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, pm_flags);

	/* Disable the counters */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), 0, kctx);

	kbdev->hwcnt.kctx = NULL;
	kbdev->hwcnt.addr = 0ULL;

	kbase_pm_ca_instr_disable(kbdev);

	kbase_pm_unrequest_cores(kbdev, MALI_TRUE, kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_SHADER));

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	/* Release the context. This had its own Power Manager Active reference */
	kbasep_js_release_privileged_ctx(kbdev, kctx);

	/* Also release our Power Manager Active reference */
	kbase_pm_context_idle(kbdev);

	KBASE_LOG(1, kbdev->dev, "HW counters dumping disabled for context %p", kctx);

	err = MALI_ERROR_NONE;

 out:
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_disable)

/**
 * @brief Configure HW counters collection
 */
mali_error kbase_instr_hwcnt_setup(kbase_context *kctx, kbase_uk_hwcnt_setup *setup)
{
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(NULL != kctx);

	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(NULL != kbdev);

	if (NULL == setup) {
		/* Bad parameter - abort */
		goto out;
	}

	if (setup->dump_buffer != 0ULL) {
		/* Enable HW counters */
		err = kbase_instr_hwcnt_enable(kctx, setup);
	} else {
		/* Disable HW counters */
		err = kbase_instr_hwcnt_disable(kctx);
	}

 out:
	return err;
}

/**
 * @brief Issue Dump command to hardware
 *
 * Notes:
 * - does not sleep
 */
mali_error kbase_instr_hwcnt_dump_irq(kbase_context *kctx)
{
	unsigned long flags;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(NULL != kbdev);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.kctx != kctx) {
		/* The instrumentation has been setup for another context */
		goto unlock;
	}

	if (kbdev->hwcnt.state != KBASE_INSTR_STATE_IDLE) {
		/* HW counters are disabled or another dump is ongoing, or we're resetting */
		goto unlock;
	}

	kbdev->hwcnt.triggered = 0;

	/* Mark that we're dumping - the PF handler can signal that we faulted */
	kbdev->hwcnt.state = KBASE_INSTR_STATE_DUMPING;

	/* Reconfigure the dump address */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO), kbdev->hwcnt.addr & 0xFFFFFFFF, NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI), kbdev->hwcnt.addr >> 32, NULL);

	/* Start dumping */
	KBASE_TRACE_ADD(kbdev, CORE_GPU_PRFCNT_SAMPLE, NULL, NULL, kbdev->hwcnt.addr, 0);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_PRFCNT_SAMPLE, kctx);

	KBASE_LOG(1, kbdev->dev, "HW counters dumping done for context %p", kctx);

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
mali_bool kbase_instr_hwcnt_dump_complete(kbase_context *kctx, mali_bool * const success)
{
	unsigned long flags;
	mali_bool complete = MALI_FALSE;
	kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(NULL != kbdev);
	KBASE_DEBUG_ASSERT(NULL != success);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_IDLE) {
		*success = MALI_TRUE;
		complete = MALI_TRUE;
	} else if (kbdev->hwcnt.state == KBASE_INSTR_STATE_FAULT) {
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
mali_error kbase_instr_hwcnt_dump(kbase_context *kctx)
{
	unsigned long flags;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(NULL != kbdev);

	err = kbase_instr_hwcnt_dump_irq(kctx);
	if (MALI_ERROR_NONE != err) {
		/* Can't dump HW counters */
		goto out;
	}

	/* Wait for dump & cacheclean to complete */
	wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING) {
		/* GPU is being reset */
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_FAULT) {
		err = MALI_ERROR_FUNCTION_FAILED;
		kbdev->hwcnt.state = KBASE_INSTR_STATE_IDLE;
	} else {
		/* Dump done */
		KBASE_DEBUG_ASSERT(kbdev->hwcnt.state == KBASE_INSTR_STATE_IDLE);
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
mali_error kbase_instr_hwcnt_clear(kbase_context *kctx)
{
	unsigned long flags;
	mali_error err = MALI_ERROR_FUNCTION_FAILED;
	kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(NULL != kctx);
	kbdev = kctx->kbdev;
	KBASE_DEBUG_ASSERT(NULL != kbdev);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING) {
		/* GPU is being reset */
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.wait, kbdev->hwcnt.triggered != 0);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}

	/* Check it's the context previously set up and we're not already dumping */
	if (kbdev->hwcnt.kctx != kctx || kbdev->hwcnt.state != KBASE_INSTR_STATE_IDLE)
		goto out;

	/* Clear the counters */
	KBASE_TRACE_ADD(kbdev, CORE_GPU_PRFCNT_CLEAR, NULL, NULL, 0u, 0);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND), GPU_COMMAND_PRFCNT_CLEAR, kctx);

	err = MALI_ERROR_NONE;

 out:
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_clear)

/**
 * Workqueue for handling cache cleaning
 */
void kbasep_cache_clean_worker(struct work_struct *data)
{
	kbase_device *kbdev;
	unsigned long flags;

	kbdev = container_of(data, kbase_device, hwcnt.cache_clean_work);

	mutex_lock(&kbdev->cacheclean_lock);
	kbasep_instr_hwcnt_cacheclean(kbdev);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	/* Wait for our condition, and any reset to complete */
	while (kbdev->hwcnt.state == KBASE_INSTR_STATE_RESETTING
		   || kbdev->hwcnt.state == KBASE_INSTR_STATE_CLEANING) {
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.cache_clean_wait,
		           (kbdev->hwcnt.state != KBASE_INSTR_STATE_RESETTING
		            && kbdev->hwcnt.state != KBASE_INSTR_STATE_CLEANING));
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}
	KBASE_DEBUG_ASSERT(kbdev->hwcnt.state == KBASE_INSTR_STATE_CLEANED);

	/* All finished and idle */
	kbdev->hwcnt.state = KBASE_INSTR_STATE_IDLE;
	kbdev->hwcnt.triggered = 1;
	wake_up(&kbdev->hwcnt.wait);

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	mutex_unlock(&kbdev->cacheclean_lock);
}

/**
 * @brief Dump complete interrupt received
 */
void kbase_instr_hwcnt_sample_done(kbase_device *kbdev)
{
	unsigned long flags;
	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.state == KBASE_INSTR_STATE_FAULT) {
		kbdev->hwcnt.triggered = 1;
		wake_up(&kbdev->hwcnt.wait);
	} else if (kbdev->hwcnt.state == KBASE_INSTR_STATE_DUMPING) {
		int ret;
		/* Always clean and invalidate the cache after a successful dump */
		kbdev->hwcnt.state = KBASE_INSTR_STATE_REQUEST_CLEAN;
		ret = queue_work(kbdev->hwcnt.cache_clean_wq, &kbdev->hwcnt.cache_clean_work);
		KBASE_DEBUG_ASSERT(ret);
	}
	/* NOTE: In the state KBASE_INSTR_STATE_RESETTING, We're in a reset,
	 * and the instrumentation state hasn't been restored yet -
	 * kbasep_reset_timeout_worker() will do the rest of the work */

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
}

/**
 * @brief Cache clean interrupt received
 */
void kbase_clean_caches_done(kbase_device *kbdev)
{
	u32 irq_mask;

	if (kbdev->hwcnt.state != KBASE_INSTR_STATE_DISABLED) {
		unsigned long flags;
		unsigned long pm_flags;
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
		/* Disable interrupt */
		spin_lock_irqsave(&kbdev->pm.power_change_lock, pm_flags);
		irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), NULL);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask & ~CLEAN_CACHES_COMPLETED, NULL);
		spin_unlock_irqrestore(&kbdev->pm.power_change_lock, pm_flags);

		/* Wakeup... */
		if (kbdev->hwcnt.state == KBASE_INSTR_STATE_CLEANING) {
			/* Only wake if we weren't resetting */
			kbdev->hwcnt.state = KBASE_INSTR_STATE_CLEANED;
			wake_up(&kbdev->hwcnt.cache_clean_wait);
		}
		/* NOTE: In the state KBASE_INSTR_STATE_RESETTING, We're in a reset,
		 * and the instrumentation state hasn't been restored yet -
		 * kbasep_reset_timeout_worker() will do the rest of the work */

		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	}
}


/* Disable instrumentation and wait for any existing dump to complete
 * It's assumed that there's only one privileged context
 * Safe to do this without lock when doing an OS suspend, because it only
 * changes in response to user-space IOCTLs */
void kbase_instr_hwcnt_suspend(kbase_device *kbdev)
{
	kbase_context *kctx;
	KBASE_DEBUG_ASSERT(kbdev);
	KBASE_DEBUG_ASSERT(!kbdev->hwcnt.suspended_kctx);

	kctx = kbdev->hwcnt.kctx;
	kbdev->hwcnt.suspended_kctx = kctx;

	/* Relevant state was saved into hwcnt.suspended_state when enabling the
	 * counters */

	if (kctx)
	{
		KBASE_DEBUG_ASSERT(kctx->jctx.sched_info.ctx.flags & KBASE_CTX_FLAG_PRIVILEGED);
		kbase_instr_hwcnt_disable(kctx);
	}
}

void kbase_instr_hwcnt_resume(kbase_device *kbdev)
{
	kbase_context *kctx;
	KBASE_DEBUG_ASSERT(kbdev);

	kctx = kbdev->hwcnt.suspended_kctx;
	kbdev->hwcnt.suspended_kctx = NULL;

	if (kctx)
	{
		mali_error err;
		err = kbase_instr_hwcnt_enable_internal(kbdev, kctx, &kbdev->hwcnt.suspended_state);
		WARN(err != MALI_ERROR_NONE,
		     "Failed to restore instrumented hardware counters on resume\n");
	}
}

