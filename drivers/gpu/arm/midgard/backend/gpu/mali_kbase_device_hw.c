/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
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
 *
 */
#include <mali_kbase.h>
#include <backend/gpu/mali_kbase_instr_internal.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

#include <backend/gpu/mali_kbase_device_internal.h>

#if !defined(CONFIG_MALI_NO_MALI)
void kbase_reg_write(struct kbase_device *kbdev, u16 offset, u32 value,
						struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);
	KBASE_DEBUG_ASSERT(kctx == NULL || kctx->as_nr != KBASEP_AS_NR_INVALID);
	KBASE_DEBUG_ASSERT(kbdev->dev != NULL);
	dev_dbg(kbdev->dev, "w: reg %04x val %08x", offset, value);
	writel(value, kbdev->reg + offset);
	if (kctx && kctx->jctx.tb)
		kbase_device_trace_register_access(kctx, REG_WRITE, offset,
									value);
}

KBASE_EXPORT_TEST_API(kbase_reg_write);

u32 kbase_reg_read(struct kbase_device *kbdev, u16 offset,
						struct kbase_context *kctx)
{
	u32 val;
	KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);
	KBASE_DEBUG_ASSERT(kctx == NULL || kctx->as_nr != KBASEP_AS_NR_INVALID);
	KBASE_DEBUG_ASSERT(kbdev->dev != NULL);
	val = readl(kbdev->reg + offset);
	dev_dbg(kbdev->dev, "r: reg %04x val %08x", offset, val);
	if (kctx && kctx->jctx.tb)
		kbase_device_trace_register_access(kctx, REG_READ, offset, val);
	return val;
}

KBASE_EXPORT_TEST_API(kbase_reg_read);
#endif /* !defined(CONFIG_MALI_NO_MALI) */

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
	u32 status;
	u64 address;

	status = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_FAULTSTATUS), NULL);
	address = (u64) kbase_reg_read(kbdev,
			GPU_CONTROL_REG(GPU_FAULTADDRESS_HI), NULL) << 32;
	address |= kbase_reg_read(kbdev,
			GPU_CONTROL_REG(GPU_FAULTADDRESS_LO), NULL);

	dev_warn(kbdev->dev, "GPU Fault 0x%08x (%s) at 0x%016llx",
			status & 0xFF,
			kbase_exception_name(kbdev, status),
			address);
	if (multiple)
		dev_warn(kbdev->dev, "There were multiple GPU faults - some have not been reported\n");
}

void kbase_gpu_interrupt(struct kbase_device *kbdev, u32 val)
{
	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ, NULL, NULL, 0u, val);
	if (val & GPU_FAULT)
		kbase_report_gpu_fault(kbdev, val & MULTIPLE_GPU_FAULTS);

	if (val & RESET_COMPLETED)
		kbase_pm_reset_done(kbdev);

	if (val & PRFCNT_SAMPLE_COMPLETED)
		kbase_instr_hwcnt_sample_done(kbdev);

	if (val & CLEAN_CACHES_COMPLETED)
		kbase_clean_caches_done(kbdev);

	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ_CLEAR, NULL, NULL, 0u, val);
	kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), val, NULL);

	/* kbase_pm_check_transitions must be called after the IRQ has been
	 * cleared. This is because it might trigger further power transitions
	 * and we don't want to miss the interrupt raised to notify us that
	 * these further transitions have finished.
	 */
	if (val & POWER_CHANGED_ALL)
		kbase_pm_power_changed(kbdev);

	KBASE_TRACE_ADD(kbdev, CORE_GPU_IRQ_DONE, NULL, NULL, 0u, val);
}
