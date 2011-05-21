/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Generic APIC sub-arch probe layer.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/hardirq.h>
#include <linux/dmar.h>

#include <asm/smp.h>
#include <asm/apic.h>
#include <asm/ipi.h>
#include <asm/setup.h>

extern struct apic apic_flat;
extern struct apic apic_physflat;
extern struct apic apic_x2xpic_uv_x;
extern struct apic apic_x2apic_phys;
extern struct apic apic_x2apic_cluster;

struct apic __read_mostly *apic = &apic_flat;
EXPORT_SYMBOL_GPL(apic);

static struct apic *apic_probe[] __initdata = {
#ifdef CONFIG_X86_UV
	&apic_x2apic_uv_x,
#endif
#ifdef CONFIG_X86_X2APIC
	&apic_x2apic_phys,
	&apic_x2apic_cluster,
#endif
	&apic_physflat,
	NULL,
};

static int apicid_phys_pkg_id(int initial_apic_id, int index_msb)
{
	return hard_smp_processor_id() >> index_msb;
}

/*
 * Check the APIC IDs in bios_cpu_apicid and choose the APIC mode.
 */
void __init default_setup_apic_routing(void)
{
	struct apic **drv;

	enable_IR_x2apic();

	for (drv = __apicdrivers; drv < __apicdrivers_end; drv++) {
		if ((*drv)->probe && (*drv)->probe()) {
			if (apic != *drv) {
				apic = *drv;
				pr_info("Switched APIC routing to %s.\n",
					apic->name);
			}
			break;
		}
	}

	if (is_vsmp_box()) {
		/* need to update phys_pkg_id */
		apic->phys_pkg_id = apicid_phys_pkg_id;
	}
}

/* Same for both flat and physical. */

void apic_send_IPI_self(int vector)
{
	__default_send_IPI_shortcut(APIC_DEST_SELF, vector, APIC_DEST_PHYSICAL);
}

int __init default_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	struct apic **drv;

	for (drv = __apicdrivers; drv < __apicdrivers_end; drv++) {
		if ((*drv)->acpi_madt_oem_check(oem_id, oem_table_id)) {
			if (apic != *drv) {
				apic = *drv;
				pr_info("Setting APIC routing to %s.\n",
					apic->name);
			}
			return 1;
		}
	}
	return 0;
}
