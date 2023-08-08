// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) "APIC: " fmt

#include <asm/apic.h>

#include "local.h"

/* The container for function call overrides */
struct apic_override __x86_apic_override __initdata;

#define apply_override(__cb)					\
	if (__x86_apic_override.__cb)				\
		apic->__cb = __x86_apic_override.__cb

static __init void restore_override_callbacks(void)
{
	apply_override(eoi);
	apply_override(native_eoi);
	apply_override(write);
	apply_override(read);
	apply_override(send_IPI);
	apply_override(send_IPI_mask);
	apply_override(send_IPI_mask_allbutself);
	apply_override(send_IPI_allbutself);
	apply_override(send_IPI_all);
	apply_override(send_IPI_self);
	apply_override(icr_read);
	apply_override(icr_write);
	apply_override(wakeup_secondary_cpu);
	apply_override(wakeup_secondary_cpu_64);
}

void __init apic_setup_apic_calls(void)
{
	/* Ensure that the default APIC has native_eoi populated */
	apic->native_eoi = apic->eoi;
}

void __init apic_install_driver(struct apic *driver)
{
	if (apic == driver)
		return;

	apic = driver;

	if (IS_ENABLED(CONFIG_X86_X2APIC) && apic->x2apic_set_max_apicid)
		apic->max_apic_id = x2apic_max_apicid;

	/* Copy the original eoi() callback as KVM/HyperV might overwrite it */
	if (!apic->native_eoi)
		apic->native_eoi = apic->eoi;

	/* Apply any already installed callback overrides */
	restore_override_callbacks();

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
		(*drv)->eoi = eoi;
	}
}
