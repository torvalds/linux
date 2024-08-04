// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include "i915_irq.h"
#include "i915_reg.h"
#include "intel_uncore.h"

void gen3_irq_reset(struct intel_uncore *uncore, i915_reg_t imr,
		    i915_reg_t iir, i915_reg_t ier)
{
	intel_uncore_write(uncore, imr, 0xffffffff);
	intel_uncore_posting_read(uncore, imr);

	intel_uncore_write(uncore, ier, 0);

	/* IIR can theoretically queue up two events. Be paranoid. */
	intel_uncore_write(uncore, iir, 0xffffffff);
	intel_uncore_posting_read(uncore, iir);
	intel_uncore_write(uncore, iir, 0xffffffff);
	intel_uncore_posting_read(uncore, iir);
}

/*
 * We should clear IMR at preinstall/uninstall, and just check at postinstall.
 */
void gen3_assert_iir_is_zero(struct intel_uncore *uncore, i915_reg_t reg)
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

void gen3_irq_init(struct intel_uncore *uncore,
		   i915_reg_t imr, u32 imr_val,
		   i915_reg_t ier, u32 ier_val,
		   i915_reg_t iir)
{
	gen3_assert_iir_is_zero(uncore, iir);

	intel_uncore_write(uncore, ier, ier_val);
	intel_uncore_write(uncore, imr, imr_val);
	intel_uncore_posting_read(uncore, imr);
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
