// SPDX-License-Identifier: GPL-2.0
/*
 *
 * (C) COPYRIGHT 2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_fault.h>
#include <backend/gpu/mali_kbase_instr_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <device/mali_kbase_device.h>
#include <mali_kbase_reset_gpu.h>
#include <mmu/mali_kbase_mmu.h>

/**
 * kbase_report_gpu_fault - Report a GPU fault.
 * @kbdev:    Kbase device pointer
 * @multiple: Zero if only GPU_FAULT was raised, non-zero if MULTIPLE_GPU_FAULTS
 *            was also set
 *
 * This function is called from the interrupt handler when a GPU fault occurs.
 * It reports the details of the fault using dev_warn().
 */
static void kbase_report_gpu_fault(struct kbase_device *kbdev, int multiple)
{
	u32 status = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTSTATUS));
	u64 address = (u64) kbase_reg_read(kbdev,
			GPU_CONTROL_REG(GPU_FAULTADDRESS_HI)) << 32;

	address |= kbase_reg_read(kbdev,
			GPU_CONTROL_REG(GPU_FAULTADDRESS_LO));

	dev_warn(kbdev->dev, "GPU Fault 0x%08x (%s) at 0x%016llx",
		status,
		kbase_gpu_exception_name(status & 0xFF),
		address);
	if (multiple)
		dev_warn(kbdev->dev, "There were multiple GPU faults - some have not been reported\n");
}

void kbase_gpu_interrupt(struct kbase_device *kbdev, u32 val)
{
	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ, NULL, val);
	if (val & GPU_FAULT)
		kbase_report_gpu_fault(kbdev, val & MULTIPLE_GPU_FAULTS);

	if (val & RESET_COMPLETED)
		kbase_pm_reset_done(kbdev);

	if (val & PRFCNT_SAMPLE_COMPLETED)
		kbase_instr_hwcnt_sample_done(kbdev);

	KBASE_KTRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, val);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), val);

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

	if (val & POWER_CHANGED_ALL) {
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
