/*
 *
 * (C) COPYRIGHT 2014-2016 ARM Limited. All rights reserved.
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





/*
 * GPU backend instrumentation APIs.
 */

#include <mali_kbase.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_hwaccess_instr.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <backend/gpu/mali_kbase_instr_internal.h>

/**
 * kbasep_instr_hwcnt_cacheclean - Issue Cache Clean & Invalidate command to
 * hardware
 *
 * @kbdev: Kbase device
 */
static void kbasep_instr_hwcnt_cacheclean(struct kbase_device *kbdev)
{
	unsigned long flags;
	unsigned long pm_flags;
	u32 irq_mask;

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	KBASE_DEBUG_ASSERT(kbdev->hwcnt.backend.state ==
					KBASE_INSTR_STATE_REQUEST_CLEAN);

	/* Enable interrupt */
	spin_lock_irqsave(&kbdev->hwaccess_lock, pm_flags);
	irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
				irq_mask | CLEAN_CACHES_COMPLETED, NULL);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, pm_flags);

	/* clean&invalidate the caches so we're sure the mmu tables for the dump
	 * buffer is valid */
	KBASE_TRACE_ADD(kbdev, CORE_GPU_CLEAN_INV_CACHES, NULL, NULL, 0u, 0);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
					GPU_COMMAND_CLEAN_INV_CACHES, NULL);
	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_CLEANING;

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
}

int kbase_instr_hwcnt_enable_internal(struct kbase_device *kbdev,
					struct kbase_context *kctx,
					struct kbase_uk_hwcnt_setup *setup)
{
	unsigned long flags, pm_flags;
	int err = -EINVAL;
	u32 irq_mask;
	int ret;
	u64 shader_cores_needed;
	u32 prfcnt_config;

	shader_cores_needed = kbase_pm_get_present_cores(kbdev,
							KBASE_PM_CORE_SHADER);

	/* alignment failure */
	if ((setup->dump_buffer == 0ULL) || (setup->dump_buffer & (2048 - 1)))
		goto out_err;

	/* Override core availability policy to ensure all cores are available
	 */
	kbase_pm_ca_instr_enable(kbdev);

	/* Request the cores early on synchronously - we'll release them on any
	 * errors (e.g. instrumentation already active) */
	kbase_pm_request_cores_sync(kbdev, true, shader_cores_needed);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.backend.state != KBASE_INSTR_STATE_DISABLED) {
		/* Instrumentation is already enabled */
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		goto out_unrequest_cores;
	}

	/* Enable interrupt */
	spin_lock_irqsave(&kbdev->hwaccess_lock, pm_flags);
	irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), irq_mask |
						PRFCNT_SAMPLE_COMPLETED, NULL);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, pm_flags);

	/* In use, this context is the owner */
	kbdev->hwcnt.kctx = kctx;
	/* Remember the dump address so we can reprogram it later */
	kbdev->hwcnt.addr = setup->dump_buffer;

	/* Request the clean */
	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_REQUEST_CLEAN;
	kbdev->hwcnt.backend.triggered = 0;
	/* Clean&invalidate the caches so we're sure the mmu tables for the dump
	 * buffer is valid */
	ret = queue_work(kbdev->hwcnt.backend.cache_clean_wq,
					&kbdev->hwcnt.backend.cache_clean_work);
	KBASE_DEBUG_ASSERT(ret);

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	/* Wait for cacheclean to complete */
	wait_event(kbdev->hwcnt.backend.wait,
					kbdev->hwcnt.backend.triggered != 0);

	KBASE_DEBUG_ASSERT(kbdev->hwcnt.backend.state ==
							KBASE_INSTR_STATE_IDLE);

	kbase_pm_request_l2_caches(kbdev);

	/* Configure */
	prfcnt_config = kctx->as_nr << PRFCNT_CONFIG_AS_SHIFT;
#ifdef CONFIG_MALI_PRFCNT_SET_SECONDARY
	{
		u32 gpu_id = kbdev->gpu_props.props.raw_props.gpu_id;
		u32 product_id = (gpu_id & GPU_ID_VERSION_PRODUCT_ID)
			>> GPU_ID_VERSION_PRODUCT_ID_SHIFT;
		int arch_v6 = GPU_ID_IS_NEW_FORMAT(product_id);

		if (arch_v6)
			prfcnt_config |= 1 << PRFCNT_CONFIG_SETSELECT_SHIFT;
	}
#endif

	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG),
			prfcnt_config | PRFCNT_CONFIG_MODE_OFF, kctx);

	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO),
					setup->dump_buffer & 0xFFFFFFFF, kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI),
					setup->dump_buffer >> 32,        kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_JM_EN),
					setup->jm_bm,                    kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_SHADER_EN),
					setup->shader_bm,                kctx);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_MMU_L2_EN),
					setup->mmu_l2_bm,                kctx);
	/* Due to PRLAM-8186 we need to disable the Tiler before we enable the
	 * HW counter dump. */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8186))
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN), 0,
									kctx);
	else
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN),
							setup->tiler_bm, kctx);

	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG),
			prfcnt_config | PRFCNT_CONFIG_MODE_MANUAL, kctx);

	/* If HW has PRLAM-8186 we can now re-enable the tiler HW counters dump
	 */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8186))
		kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_TILER_EN),
							setup->tiler_bm, kctx);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_IDLE;
	kbdev->hwcnt.backend.triggered = 1;
	wake_up(&kbdev->hwcnt.backend.wait);

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	err = 0;

	dev_dbg(kbdev->dev, "HW counters dumping set-up for context %p", kctx);
	return err;
 out_unrequest_cores:
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_pm_unrequest_cores(kbdev, true, shader_cores_needed);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
 out_err:
	return err;
}

int kbase_instr_hwcnt_disable_internal(struct kbase_context *kctx)
{
	unsigned long flags, pm_flags;
	int err = -EINVAL;
	u32 irq_mask;
	struct kbase_device *kbdev = kctx->kbdev;

	while (1) {
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

		if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_DISABLED) {
			/* Instrumentation is not enabled */
			spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
			goto out;
		}

		if (kbdev->hwcnt.kctx != kctx) {
			/* Instrumentation has been setup for another context */
			spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
			goto out;
		}

		if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_IDLE)
			break;

		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

		/* Ongoing dump/setup - wait for its completion */
		wait_event(kbdev->hwcnt.backend.wait,
					kbdev->hwcnt.backend.triggered != 0);
	}

	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_DISABLED;
	kbdev->hwcnt.backend.triggered = 0;

	/* Disable interrupt */
	spin_lock_irqsave(&kbdev->hwaccess_lock, pm_flags);
	irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK), NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
				irq_mask & ~PRFCNT_SAMPLE_COMPLETED, NULL);

	/* Disable the counters */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_CONFIG), 0, kctx);

	kbdev->hwcnt.kctx = NULL;
	kbdev->hwcnt.addr = 0ULL;

	kbase_pm_ca_instr_disable(kbdev);

	kbase_pm_unrequest_cores(kbdev, true,
		kbase_pm_get_present_cores(kbdev, KBASE_PM_CORE_SHADER));

	kbase_pm_release_l2_caches(kbdev);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, pm_flags);
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	dev_dbg(kbdev->dev, "HW counters dumping disabled for context %p",
									kctx);

	err = 0;

 out:
	return err;
}

int kbase_instr_hwcnt_request_dump(struct kbase_context *kctx)
{
	unsigned long flags;
	int err = -EINVAL;
	struct kbase_device *kbdev = kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.kctx != kctx) {
		/* The instrumentation has been setup for another context */
		goto unlock;
	}

	if (kbdev->hwcnt.backend.state != KBASE_INSTR_STATE_IDLE) {
		/* HW counters are disabled or another dump is ongoing, or we're
		 * resetting */
		goto unlock;
	}

	kbdev->hwcnt.backend.triggered = 0;

	/* Mark that we're dumping - the PF handler can signal that we faulted
	 */
	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_DUMPING;

	/* Reconfigure the dump address */
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_LO),
					kbdev->hwcnt.addr & 0xFFFFFFFF, NULL);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(PRFCNT_BASE_HI),
					kbdev->hwcnt.addr >> 32, NULL);

	/* Start dumping */
	KBASE_TRACE_ADD(kbdev, CORE_GPU_PRFCNT_SAMPLE, NULL, NULL,
					kbdev->hwcnt.addr, 0);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
					GPU_COMMAND_PRFCNT_SAMPLE, kctx);

	dev_dbg(kbdev->dev, "HW counters dumping done for context %p", kctx);

	err = 0;

 unlock:
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_request_dump);

bool kbase_instr_hwcnt_dump_complete(struct kbase_context *kctx,
						bool * const success)
{
	unsigned long flags;
	bool complete = false;
	struct kbase_device *kbdev = kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_IDLE) {
		*success = true;
		complete = true;
	} else if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_FAULT) {
		*success = false;
		complete = true;
		kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_IDLE;
	}

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	return complete;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_dump_complete);

void kbasep_cache_clean_worker(struct work_struct *data)
{
	struct kbase_device *kbdev;
	unsigned long flags;

	kbdev = container_of(data, struct kbase_device,
						hwcnt.backend.cache_clean_work);

	mutex_lock(&kbdev->cacheclean_lock);
	kbasep_instr_hwcnt_cacheclean(kbdev);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	/* Wait for our condition, and any reset to complete */
	while (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_CLEANING) {
		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
		wait_event(kbdev->hwcnt.backend.cache_clean_wait,
				kbdev->hwcnt.backend.state !=
						KBASE_INSTR_STATE_CLEANING);
		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
	}
	KBASE_DEBUG_ASSERT(kbdev->hwcnt.backend.state ==
						KBASE_INSTR_STATE_CLEANED);

	/* All finished and idle */
	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_IDLE;
	kbdev->hwcnt.backend.triggered = 1;
	wake_up(&kbdev->hwcnt.backend.wait);

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	mutex_unlock(&kbdev->cacheclean_lock);
}

void kbase_instr_hwcnt_sample_done(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_FAULT) {
		kbdev->hwcnt.backend.triggered = 1;
		wake_up(&kbdev->hwcnt.backend.wait);
	} else if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_DUMPING) {
		int ret;
		/* Always clean and invalidate the cache after a successful dump
		 */
		kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_REQUEST_CLEAN;
		ret = queue_work(kbdev->hwcnt.backend.cache_clean_wq,
					&kbdev->hwcnt.backend.cache_clean_work);
		KBASE_DEBUG_ASSERT(ret);
	}

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
}

void kbase_clean_caches_done(struct kbase_device *kbdev)
{
	u32 irq_mask;

	if (kbdev->hwcnt.backend.state != KBASE_INSTR_STATE_DISABLED) {
		unsigned long flags;
		unsigned long pm_flags;

		spin_lock_irqsave(&kbdev->hwcnt.lock, flags);
		/* Disable interrupt */
		spin_lock_irqsave(&kbdev->hwaccess_lock, pm_flags);
		irq_mask = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
									NULL);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
				irq_mask & ~CLEAN_CACHES_COMPLETED, NULL);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, pm_flags);

		/* Wakeup... */
		if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_CLEANING) {
			/* Only wake if we weren't resetting */
			kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_CLEANED;
			wake_up(&kbdev->hwcnt.backend.cache_clean_wait);
		}

		spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	}
}

int kbase_instr_hwcnt_wait_for_dump(struct kbase_context *kctx)
{
	struct kbase_device *kbdev = kctx->kbdev;
	unsigned long flags;
	int err;

	/* Wait for dump & cacheclean to complete */
	wait_event(kbdev->hwcnt.backend.wait,
					kbdev->hwcnt.backend.triggered != 0);

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	if (kbdev->hwcnt.backend.state == KBASE_INSTR_STATE_FAULT) {
		err = -EINVAL;
		kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_IDLE;
	} else {
		/* Dump done */
		KBASE_DEBUG_ASSERT(kbdev->hwcnt.backend.state ==
							KBASE_INSTR_STATE_IDLE);
		err = 0;
	}

	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);

	return err;
}

int kbase_instr_hwcnt_clear(struct kbase_context *kctx)
{
	unsigned long flags;
	int err = -EINVAL;
	struct kbase_device *kbdev = kctx->kbdev;

	spin_lock_irqsave(&kbdev->hwcnt.lock, flags);

	/* Check it's the context previously set up and we're not already
	 * dumping */
	if (kbdev->hwcnt.kctx != kctx || kbdev->hwcnt.backend.state !=
							KBASE_INSTR_STATE_IDLE)
		goto out;

	/* Clear the counters */
	KBASE_TRACE_ADD(kbdev, CORE_GPU_PRFCNT_CLEAR, NULL, NULL, 0u, 0);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_COMMAND),
						GPU_COMMAND_PRFCNT_CLEAR, kctx);

	err = 0;

out:
	spin_unlock_irqrestore(&kbdev->hwcnt.lock, flags);
	return err;
}
KBASE_EXPORT_SYMBOL(kbase_instr_hwcnt_clear);

int kbase_instr_backend_init(struct kbase_device *kbdev)
{
	int ret = 0;

	kbdev->hwcnt.backend.state = KBASE_INSTR_STATE_DISABLED;

	init_waitqueue_head(&kbdev->hwcnt.backend.wait);
	init_waitqueue_head(&kbdev->hwcnt.backend.cache_clean_wait);
	INIT_WORK(&kbdev->hwcnt.backend.cache_clean_work,
						kbasep_cache_clean_worker);
	kbdev->hwcnt.backend.triggered = 0;

	kbdev->hwcnt.backend.cache_clean_wq =
			alloc_workqueue("Mali cache cleaning workqueue", 0, 1);
	if (NULL == kbdev->hwcnt.backend.cache_clean_wq)
		ret = -EINVAL;

	return ret;
}

void kbase_instr_backend_term(struct kbase_device *kbdev)
{
	destroy_workqueue(kbdev->hwcnt.backend.cache_clean_wq);
}

