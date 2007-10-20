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

#include <asm/smp.h>
#include <asm/ipi.h>
#include <asm/genapic.h>

#ifdef CONFIG_ACPI
#include <acpi/acpi_bus.h>
#endif

/*
 * which logical CPU number maps to which CPU (physical APIC ID)
 *
 * The following static array is used during kernel startup
 * and the x86_cpu_to_apicid_ptr contains the address of the
 * array during this time.  Is it zeroed when the per_cpu
 * data area is removed.
 */
u8 x86_cpu_to_apicid_init[NR_CPUS] __initdata
					= { [0 ... NR_CPUS-1] = BAD_APICID };
void *x86_cpu_to_apicid_ptr;
DEFINE_PER_CPU(u8, x86_cpu_to_apicid) = BAD_APICID;
EXPORT_PER_CPU_SYMBOL(x86_cpu_to_apicid);

struct genapic __read_mostly *genapic = &apic_flat;

/*
 * Check the APIC IDs in bios_cpu_apicid and choose the APIC mode.
 */
void __init setup_apic_routing(void)
{
#ifdef CONFIG_ACPI
	/*
	 * Quirk: some x86_64 machines can only use physical APIC mode
	 * regardless of how many processors are present (x86_64 ES7000
	 * is an example).
	 */
	if (acpi_gbl_FADT.header.revision > FADT2_REVISION_ID &&
			(acpi_gbl_FADT.flags & ACPI_FADT_APIC_PHYSICAL))
		genapic = &apic_physflat;
	else
#endif

	if (cpus_weight(cpu_possible_map) <= 8)
		genapic = &apic_flat;
	else
		genapic = &apic_physflat;

	printk(KERN_INFO "Setting APIC routing to %s\n", genapic->name);
}

/* Same for both flat and physical. */

void send_IPI_self(int vector)
{
	__send_IPI_shortcut(APIC_DEST_SELF, vector, APIC_DEST_PHYSICAL);
}
