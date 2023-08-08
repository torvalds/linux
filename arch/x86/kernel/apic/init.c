// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) "APIC: " fmt

#include <asm/apic.h>

#include "local.h"

void __init apic_install_driver(struct apic *driver)
{
	if (apic == driver)
		return;

	apic = driver;

	if (IS_ENABLED(CONFIG_X86_X2APIC) && apic->x2apic_set_max_apicid)
		apic->max_apic_id = x2apic_max_apicid;

	pr_info("Switched APIC routing to: %s\n", driver->name);
}

#ifdef CONFIG_X86_64
void __init acpi_wake_cpu_handler_update(wakeup_cpu_handler handler)
{
	struct apic **drv;

	for (drv = __apicdrivers; drv < __apicdrivers_end; drv++)
		(*drv)->wakeup_secondary_cpu_64 = handler;
}
#endif

/*
 * Override the generic EOI implementation with an optimized version.
 * Only called during early boot when only one CPU is active and with
 * interrupts disabled, so we know this does not race with actual APIC driver
 * use.
 */
void __init apic_set_eoi_cb(void (*eoi)(void))
{
	struct apic **drv;

	for (drv = __apicdrivers; drv < __apicdrivers_end; drv++) {
		/* Should happen once for each apic */
		WARN_ON((*drv)->eoi == eoi);
		(*drv)->native_eoi = (*drv)->eoi;
		(*drv)->eoi = eoi;
	}
}
