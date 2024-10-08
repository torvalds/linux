// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_irq.h"
#include "i915_reg.h"
#include "intel_uncore.h"

void gen2_irq_reset(struct intel_uncore *uncore, struct i915_irq_regs regs)
{
	intel_uncore_write(uncore, regs.imr, 0xffffffff);
	intel_uncore_posting_read(uncore, regs.imr);

	intel_uncore_write(uncore, regs.ier, 0);

	/* IIR can theoretically queue up two events. Be paranoid. */
	intel_uncore_write(uncore, regs.iir, 0xffffffff);
	intel_uncore_posting_read(uncore, regs.iir);
	intel_uncore_write(uncore, regs.iir, 0xffffffff);
	intel_uncore_posting_read(uncore, regs.iir);
}

/*
 * We should clear IMR at preinstall/uninstall, and just check at postinstall.
 */
void gen2_assert_iir_is_zero(struct intel_uncore *uncore, i915_reg_t reg)
{
	struct xe_device *xe = container_of(uncore, struct xe_device, uncore);
	u32 val = intel_uncore_read(uncore, reg);

	if (val == 0)
		return;

	drm_WARN(&xe->drm, 1,
		 "Interrupt register 0x%x is not zero: 0x%08x\n",
		 i915_mmio_reg_offset(reg), val);
	intel_uncore_write(uncore, reg, 0xffffffff);
	intel_uncore_posting_read(uncore, reg);
	intel_uncore_write(uncore, reg, 0xffffffff);
	intel_uncore_posting_read(uncore, reg);
}

void gen2_irq_init(struct intel_uncore *uncore, struct i915_irq_regs regs,
		   u32 imr_val, u32 ier_val)
{
	gen2_assert_iir_is_zero(uncore, regs.iir);

	intel_uncore_write(uncore, regs.ier, ier_val);
	intel_uncore_write(uncore, regs.imr, imr_val);
	intel_uncore_posting_read(uncore, regs.imr);
}

bool intel_irqs_enabled(struct xe_device *xe)
{
	/*
	 * XXX: i915 has a racy handling of the irq.enabled, since it doesn't
	 * lock its transitions. Because of that, the irq.enabled sometimes
	 * is not read with the irq.lock in place.
	 * However, the most critical cases like vblank and page flips are
	 * properly using the locks.
	 * We cannot take the lock in here or run any kind of assert because
	 * of i915 inconsistency.
	 * But at this point the xe irq is better protected against races,
	 * although the full solution would be protecting the i915 side.
	 */
	return xe->irq.enabled;
}

void intel_synchronize_irq(struct xe_device *xe)
{
	synchronize_irq(to_pci_dev(xe->drm.dev)->irq);
}
