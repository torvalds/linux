// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2020-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <backend/gpu/mali_kbase_instr_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <device/mali_kbase_device.h>
#include <mali_kbase_reset_gpu.h>
#include <mmu/mali_kbase_mmu.h>
#include <mali_kbase_ctx_sched.h>

/**
 * kbase_report_gpu_fault - Report a GPU fault of the device.
 *
 * @kbdev:    Kbase device pointer
 * @status:   Fault status
 * @as_nr:    Faulty address space
 * @as_valid: true if address space is valid
 *
 * This function is called from the interrupt handler when a GPU fault occurs.
 */
static void kbase_report_gpu_fault(struct kbase_device *kbdev, u32 status,
		u32 as_nr, bool as_valid)
{
	u64 address = (u64) kbase_reg_read(kbdev,
			GPU_CONTROL_REG(GPU_FAULTADDRESS_HI)) << 32;

	address |= kbase_reg_read(kbdev,
			GPU_CONTROL_REG(GPU_FAULTADDRESS_LO));

	/* Report GPU fault for all contexts in case either
	 * the address space is invalid or it's MCU address space.
	 */
	kbase_mmu_gpu_fault_interrupt(kbdev, status, as_nr, address, as_valid);
}

static void kbase_gpu_fault_interrupt(struct kbase_device *kbdev)
{
	const u32 status = kbase_reg_read(kbdev,
			GPU_CONTROL_REG(GPU_FAULTSTATUS));
	const bool as_valid = status & GPU_FAULTSTATUS_JASID_VALID_FLAG;
	const u32 as_nr = (status & GPU_FAULTSTATUS_JASID_MASK) >>
			GPU_FAULTSTATUS_JASID_SHIFT;
	bool bus_fault = (status & GPU_FAULTSTATUS_EXCEPTION_TYPE_MASK) ==
			GPU_FAULTSTATUS_EXCEPTION_TYPE_GPU_BUS_FAULT;

	if (bus_fault) {
		/* If as_valid, reset gpu when ASID is for MCU. */
		if (!as_valid || (as_nr == MCU_AS_NR)) {
			kbase_report_gpu_fault(kbdev, status, as_nr, as_valid);

			dev_err(kbdev->dev, "GPU bus fault triggering gpu-reset ...\n");
			if (kbase_prepare_to_reset_gpu(
				    kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR))
				kbase_reset_gpu(kbdev);
		} else {
			/* Handle Bus fault */
			if (kbase_mmu_bus_fault_interrupt(kbdev, status, as_nr))
				dev_warn(kbdev->dev,
					 "fail to handle GPU bus fault ...\n");
		}
	} else
		kbase_report_gpu_fault(kbdev, status, as_nr, as_valid);

}

void kbase_gpu_interrupt(struct kbase_device *kbdev, u32 val)
{
	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ, NULL, val);
	if (val & GPU_FAULT)
		kbase_gpu_fault_interrupt(kbdev);

	if (val & GPU_PROTECTED_FAULT) {
		struct kbase_csf_scheduler *scheduler = &kbdev->csf.scheduler;
		unsigned long flags;

		dev_err_ratelimited(kbdev->dev, "GPU fault in protected mode");

		/* Mask the protected fault interrupt to avoid the potential
		 * deluge of such interrupts. It will be unmasked on GPU reset.
		 */
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_MASK),
				GPU_IRQ_REG_ALL & ~GPU_PROTECTED_FAULT);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		kbase_csf_scheduler_spin_lock(kbdev, &flags);
		if (!WARN_ON(!kbase_csf_scheduler_protected_mode_in_use(
			    kbdev))) {
			struct base_gpu_queue_group_error const
				err_payload = { .error_type =
							BASE_GPU_QUEUE_GROUP_ERROR_FATAL,
						.payload = {
							.fatal_group = {
								.status =
									GPU_EXCEPTION_TYPE_SW_FAULT_0,
							} } };

			kbase_debug_csf_fault_notify(kbdev, scheduler->active_protm_grp->kctx,
						     DF_GPU_PROTECTED_FAULT);

			scheduler->active_protm_grp->faulted = true;
			kbase_csf_add_group_fatal_error(
				scheduler->active_protm_grp, &err_payload);
			kbase_event_wakeup(scheduler->active_protm_grp->kctx);
		}
		kbase_csf_scheduler_spin_unlock(kbdev, flags);

		if (kbase_prepare_to_reset_gpu(
			    kbdev, RESET_FLAGS_HWC_UNRECOVERABLE_ERROR))
			kbase_reset_gpu(kbdev);

		/* Defer the clearing to the GPU reset sequence */
		val &= ~GPU_PROTECTED_FAULT;
	}

	if (val & RESET_COMPLETED)
		kbase_pm_reset_done(kbdev);

	/* Defer clearing CLEAN_CACHES_COMPLETED to kbase_clean_caches_done.
	 * We need to acquire hwaccess_lock to avoid a race condition with
	 * kbase_gpu_cache_flush_and_busy_wait
	 */
	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, val & ~CLEAN_CACHES_COMPLETED);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), val & ~CLEAN_CACHES_COMPLETED);

#ifdef KBASE_PM_RUNTIME
	if (val & DOORBELL_MIRROR) {
		unsigned long flags;

		dev_dbg(kbdev->dev, "Doorbell mirror interrupt received");
		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
#ifdef CONFIG_MALI_BIFROST_DEBUG
		WARN_ON(!kbase_csf_scheduler_get_nr_active_csgs(kbdev));
#endif
		kbase_pm_disable_db_mirror_interrupt(kbdev);
		kbdev->pm.backend.exit_gpu_sleep_mode = true;
		kbase_csf_scheduler_invoke_tick(kbdev);
		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	}
#endif

	/* kbase_pm_check_transitions (called by kbase_pm_power_changed) must
	 * be called after the IRQ has been cleared. This is because it might
	 * trigger further power transitions and we don't want to miss the
	 * interrupt raised to notify us that these further transitions have
	 * finished. The same applies to kbase_clean_caches_done() - if another
	 * clean was queued, it might trigger another clean, which might
	 * generate another interrupt which shouldn't be missed.
	 */

	if (val & CLEAN_CACHES_COMPLETED)
		kbase_clean_caches_done(kbdev);

	if (val & (POWER_CHANGED_ALL | MCU_STATUS_GPU_IRQ)) {
		kbase_pm_power_changed(kbdev);
	} else if (val & CLEAN_CACHES_COMPLETED) {
		/* If cache line evict messages can be lost when shader cores
		 * power down then we need to flush the L2 cache before powering
		 * down cores. When the flush completes, the shaders' state
		 * machine needs to be re-invoked to proceed with powering down
		 * cores.
		 */
		if (kbdev->pm.backend.l2_always_on ||
			kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_TTRX_921))
			kbase_pm_power_changed(kbdev);
	}

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ_DONE, NULL, val);
}

#if !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
static bool kbase_is_register_accessible(u32 offset)
{
#ifdef CONFIG_MALI_BIFROST_DEBUG
	if (((offset >= MCU_SUBSYSTEM_BASE) && (offset < IPA_CONTROL_BASE)) ||
	    ((offset >= GPU_CONTROL_MCU_BASE) && (offset < USER_BASE))) {
		WARN(1, "Invalid register offset 0x%x", offset);
		return false;
	}
#endif

	return true;
}

void kbase_reg_write(struct kbase_device *kbdev, u32 offset, u32 value)
{
	if (WARN_ON(!kbdev->pm.backend.gpu_powered))
		return;

	if (WARN_ON(kbdev->dev == NULL))
		return;

	if (!kbase_is_register_accessible(offset))
		return;

	writel(value, kbdev->reg + offset);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	if (unlikely(kbdev->io_history.enabled))
		kbase_io_history_add(&kbdev->io_history, kbdev->reg + offset,
				     value, 1);
#endif /* CONFIG_DEBUG_FS */
	dev_dbg(kbdev->dev, "w: reg %08x val %08x", offset, value);
}
KBASE_EXPORT_TEST_API(kbase_reg_write);

u32 kbase_reg_read(struct kbase_device *kbdev, u32 offset)
{
	u32 val;

	if (WARN_ON(!kbdev->pm.backend.gpu_powered))
		return 0;

	if (WARN_ON(kbdev->dev == NULL))
		return 0;

	if (!kbase_is_register_accessible(offset))
		return 0;

	val = readl(kbdev->reg + offset);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	if (unlikely(kbdev->io_history.enabled))
		kbase_io_history_add(&kbdev->io_history, kbdev->reg + offset,
				     val, 0);
#endif /* CONFIG_DEBUG_FS */
	dev_dbg(kbdev->dev, "r: reg %08x val %08x", offset, val);

	return val;
}
KBASE_EXPORT_TEST_API(kbase_reg_read);
#endif /* !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI) */
