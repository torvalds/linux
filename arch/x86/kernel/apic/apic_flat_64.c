// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2004 James Cleverdon, IBM.
 *
 * Flat APIC subarch code.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/acpi.h>

#include <asm/jailhouse_para.h>
#include <asm/apic.h>

#include "local.h"

static struct apic apic_physflat;
static struct apic apic_flat;

struct apic *apic __ro_after_init = &apic_flat;
EXPORT_SYMBOL_GPL(apic);

static int flat_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 1;
}

static void _flat_send_IPI_mask(unsigned long mask, int vector)
{
	unsigned long flags;

	local_irq_save(flags);
	__default_send_IPI_dest_field(mask, vector, APIC_DEST_LOGICAL);
	local_irq_restore(flags);
}

static void flat_send_IPI_mask(const struct cpumask *cpumask, int vector)
{
	unsigned long mask = cpumask_bits(cpumask)[0];

	_flat_send_IPI_mask(mask, vector);
}

static void
flat_send_IPI_mask_allbutself(const struct cpumask *cpumask, int vector)
{
	unsigned long mask = cpumask_bits(cpumask)[0];
	int cpu = smp_processor_id();

	if (cpu < BITS_PER_LONG)
		__clear_bit(cpu, &mask);

	_flat_send_IPI_mask(mask, vector);
}

static u32 flat_get_apic_id(u32 x)
{
	return (x >> 24) & 0xFF;
}

static u32 set_apic_id(u32 id)
{
	return (id & 0xFF) << 24;
}

static u32 flat_phys_pkg_id(u32 initial_apic_id, int index_msb)
{
	return initial_apic_id >> index_msb;
}

static int flat_probe(void)
{
	return 1;
}

static struct apic apic_flat __ro_after_init = {
	.name				= "flat",
	.probe				= flat_probe,
	.acpi_madt_oem_check		= flat_acpi_madt_oem_check,
	.apic_id_registered		= default_apic_id_registered,

	.dest_mode_logical		= true,

	.disable_esr			= 0,

	.init_apic_ldr			= default_init_apic_ldr,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.phys_pkg_id			= flat_phys_pkg_id,

	.max_apic_id			= 0xFE,
	.get_apic_id			= flat_get_apic_id,
	.set_apic_id			= set_apic_id,

	.calc_dest_apicid		= apic_flat_calc_apicid,

	.send_IPI			= default_send_IPI_single,
	.send_IPI_mask			= flat_send_IPI_mask,
	.send_IPI_mask_allbutself	= flat_send_IPI_mask_allbutself,
	.send_IPI_allbutself		= default_send_IPI_allbutself,
	.send_IPI_all			= default_send_IPI_all,
	.send_IPI_self			= default_send_IPI_self,
	.nmi_to_offline_cpu		= true,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.eoi				= native_apic_mem_eoi,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= apic_mem_wait_icr_idle,
	.safe_wait_icr_idle		= apic_mem_wait_icr_idle_timeout,
};

/*
 * Physflat mode is used when there are more than 8 CPUs on a system.
 * We cannot use logical delivery in this case because the mask
 * overflows, so use physical mode.
 */
static int physflat_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
#ifdef CONFIG_ACPI
	/*
	 * Quirk: some x86_64 machines can only use physical APIC mode
	 * regardless of how many processors are present (x86_64 ES7000
	 * is an example).
	 */
	if (acpi_gbl_FADT.header.revision >= FADT2_REVISION_ID &&
		(acpi_gbl_FADT.flags & ACPI_FADT_APIC_PHYSICAL)) {
		printk(KERN_DEBUG "system APIC only can use physical flat");
		return 1;
	}

	if (!strncmp(oem_id, "IBM", 3) && !strncmp(oem_table_id, "EXA", 3)) {
		printk(KERN_DEBUG "IBM Summit detected, will use apic physical");
		return 1;
	}
#endif

	return 0;
}

static int physflat_probe(void)
{
	return apic == &apic_physflat || num_possible_cpus() > 8 || jailhouse_paravirt();
}

static struct apic apic_physflat __ro_after_init = {

	.name				= "physical flat",
	.probe				= physflat_probe,
	.acpi_madt_oem_check		= physflat_acpi_madt_oem_check,
	.apic_id_registered		= default_apic_id_registered,

	.dest_mode_logical		= false,

	.disable_esr			= 0,

	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.phys_pkg_id			= flat_phys_pkg_id,

	.max_apic_id			= 0xFE,
	.get_apic_id			= flat_get_apic_id,
	.set_apic_id			= set_apic_id,

	.calc_dest_apicid		= apic_default_calc_apicid,

	.send_IPI			= default_send_IPI_single_phys,
	.send_IPI_mask			= default_send_IPI_mask_sequence_phys,
	.send_IPI_mask_allbutself	= default_send_IPI_mask_allbutself_phys,
	.send_IPI_allbutself		= default_send_IPI_allbutself,
	.send_IPI_all			= default_send_IPI_all,
	.send_IPI_self			= default_send_IPI_self,
	.nmi_to_offline_cpu		= true,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.eoi				= native_apic_mem_eoi,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= apic_mem_wait_icr_idle,
	.safe_wait_icr_idle		= apic_mem_wait_icr_idle_timeout,
};

/*
 * We need to check for physflat first, so this order is important.
 */
apic_drivers(apic_physflat, apic_flat);
