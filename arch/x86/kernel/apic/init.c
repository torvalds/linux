// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) "APIC: " fmt

#include <asm/apic.h>

#include "local.h"

/*
 * Use DEFINE_STATIC_CALL_NULL() to avoid having to provide stub functions
 * for each callback. The callbacks are setup during boot and all except
 * wait_icr_idle() must be initialized before usage. The IPI wrappers
 * use static_call() and not static_call_cond() to catch any fails.
 */
#define DEFINE_APIC_CALL(__cb)						\
	DEFINE_STATIC_CALL_NULL(apic_call_##__cb, *apic->__cb)

DEFINE_APIC_CALL(eoi);
DEFINE_APIC_CALL(native_eoi);
DEFINE_APIC_CALL(icr_read);
DEFINE_APIC_CALL(icr_write);
DEFINE_APIC_CALL(read);
DEFINE_APIC_CALL(send_IPI);
DEFINE_APIC_CALL(send_IPI_mask);
DEFINE_APIC_CALL(send_IPI_mask_allbutself);
DEFINE_APIC_CALL(send_IPI_allbutself);
DEFINE_APIC_CALL(send_IPI_all);
DEFINE_APIC_CALL(send_IPI_self);
DEFINE_APIC_CALL(wait_icr_idle);
DEFINE_APIC_CALL(wakeup_secondary_cpu);
DEFINE_APIC_CALL(wakeup_secondary_cpu_64);
DEFINE_APIC_CALL(write);

EXPORT_STATIC_CALL_TRAMP_GPL(apic_call_send_IPI_mask);
EXPORT_STATIC_CALL_TRAMP_GPL(apic_call_send_IPI_self);

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

#define update_call(__cb)					\
	static_call_update(apic_call_##__cb, *apic->__cb)

static __init void update_static_calls(void)
{
	update_call(eoi);
	update_call(native_eoi);
	update_call(write);
	update_call(read);
	update_call(send_IPI);
	update_call(send_IPI_mask);
	update_call(send_IPI_mask_allbutself);
	update_call(send_IPI_allbutself);
	update_call(send_IPI_all);
	update_call(send_IPI_self);
	update_call(icr_read);
	update_call(icr_write);
	update_call(wait_icr_idle);
	update_call(wakeup_secondary_cpu);
	update_call(wakeup_secondary_cpu_64);
}

void __init apic_setup_apic_calls(void)
{
	/* Ensure that the default APIC has native_eoi populated */
	apic->native_eoi = apic->eoi;
	update_static_calls();
	pr_info("Static calls initialized\n");
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
	update_static_calls();

	pr_info("Switched APIC routing to: %s\n", driver->name);
}
