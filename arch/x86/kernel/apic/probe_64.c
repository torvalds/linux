// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2004 James Cleverdon, IBM.
 *
 * Generic APIC sub-arch probe layer.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */
#include <linux/thread_info.h>
#include <asm/apic.h>

#include "local.h"

static __init void apic_install_driver(struct apic *driver)
{
	if (apic == driver)
		return;

	apic = driver;

	if (IS_ENABLED(CONFIG_X86_X2APIC) && apic->x2apic_set_max_apicid)
		apic->max_apic_id = x2apic_max_apicid;

	pr_info("Switched APIC routing to %s:\n", apic->name);
}

/* Select the appropriate APIC driver */
void __init x86_64_probe_apic(void)
{
	struct apic **drv;

	enable_IR_x2apic();

	for (drv = __apicdrivers; drv < __apicdrivers_end; drv++) {
		if ((*drv)->probe && (*drv)->probe()) {
			apic_install_driver(*drv);
			break;
		}
	}
}

int __init default_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	struct apic **drv;

	for (drv = __apicdrivers; drv < __apicdrivers_end; drv++) {
		if ((*drv)->acpi_madt_oem_check(oem_id, oem_table_id)) {
			apic_install_driver(*drv);
			return 1;
		}
	}
	return 0;
}
