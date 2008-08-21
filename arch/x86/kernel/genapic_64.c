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

#include <asm/smp.h>
#include <asm/ipi.h>
#include <asm/genapic.h>

#ifdef CONFIG_ACPI
#include <acpi/acpi_bus.h>
#endif

DEFINE_PER_CPU(int, x2apic_extra_bits);

struct genapic __read_mostly *genapic = &apic_flat;

static enum uv_system_type uv_system_type;

/*
 * Check the APIC IDs in bios_cpu_apicid and choose the APIC mode.
 */
void __init setup_apic_routing(void)
{
	if (uv_system_type == UV_NON_UNIQUE_APIC)
		genapic = &apic_x2apic_uv_x;
	else
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

	if (max_physical_apicid < 8)
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

int __init acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	if (!strcmp(oem_id, "SGI")) {
		if (!strcmp(oem_table_id, "UVL"))
			uv_system_type = UV_LEGACY_APIC;
		else if (!strcmp(oem_table_id, "UVX"))
			uv_system_type = UV_X2APIC;
		else if (!strcmp(oem_table_id, "UVH"))
			uv_system_type = UV_NON_UNIQUE_APIC;
	}
	return 0;
}

unsigned int read_apic_id(void)
{
	unsigned int id;

	WARN_ON(preemptible() && num_online_cpus() > 1);
	id = apic_read(APIC_ID);
	if (uv_system_type >= UV_X2APIC)
		id  |= __get_cpu_var(x2apic_extra_bits);
	return id;
}

enum uv_system_type get_uv_system_type(void)
{
	return uv_system_type;
}

int is_uv_system(void)
{
	return uv_system_type != UV_NONE;
}
EXPORT_SYMBOL_GPL(is_uv_system);
