/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Flat APIC subarch code.
 *
 * Hacked for x86-64 by James Cleverdon from i386 architecture code by
 * Martin Bligh, Andi Kleen, James Bottomley, John Stultz, and
 * James Cleverdon.
 */
#include <linux/errno.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/hardirq.h>
#include <linux/export.h>
#include <asm/smp.h>
#include <asm/apic.h>
#include <asm/ipi.h>

#include <linux/acpi.h>

static struct apic apic_physflat;
static struct apic apic_flat;

struct apic *apic __ro_after_init = &apic_flat;
EXPORT_SYMBOL_GPL(apic);

static int flat_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 1;
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
void flat_init_apic_ldr(void)
{
	unsigned long val;
	unsigned long num, id;

	num = smp_processor_id();
	id = 1UL << num;
	apic_write(APIC_DFR, APIC_DFR_FLAT);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(id);
	apic_write(APIC_LDR, val);
}

static void _flat_send_IPI_mask(unsigned long mask, int vector)
{
	unsigned long flags;

	local_irq_save(flags);
	__default_send_IPI_dest_field(mask, vector, apic->dest_logical);
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
		clear_bit(cpu, &mask);

	_flat_send_IPI_mask(mask, vector);
}

static void flat_send_IPI_allbutself(int vector)
{
	int cpu = smp_processor_id();
#ifdef	CONFIG_HOTPLUG_CPU
	int hotplug = 1;
#else
	int hotplug = 0;
#endif
	if (hotplug || vector == NMI_VECTOR) {
		if (!cpumask_equal(cpu_online_mask, cpumask_of(cpu))) {
			unsigned long mask = cpumask_bits(cpu_online_mask)[0];

			if (cpu < BITS_PER_LONG)
				clear_bit(cpu, &mask);

			_flat_send_IPI_mask(mask, vector);
		}
	} else if (num_online_cpus() > 1) {
		__default_send_IPI_shortcut(APIC_DEST_ALLBUT,
					    vector, apic->dest_logical);
	}
}

static void flat_send_IPI_all(int vector)
{
	if (vector == NMI_VECTOR) {
		flat_send_IPI_mask(cpu_online_mask, vector);
	} else {
		__default_send_IPI_shortcut(APIC_DEST_ALLINC,
					    vector, apic->dest_logical);
	}
}

static unsigned int flat_get_apic_id(unsigned long x)
{
	return (x >> 24) & 0xFF;
}

static u32 set_apic_id(unsigned int id)
{
	return (id & 0xFF) << 24;
}

static unsigned int read_xapic_id(void)
{
	return flat_get_apic_id(apic_read(APIC_ID));
}

static int flat_apic_id_registered(void)
{
	return physid_isset(read_xapic_id(), phys_cpu_present_map);
}

static int flat_phys_pkg_id(int initial_apic_id, int index_msb)
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
	.apic_id_valid			= default_apic_id_valid,
	.apic_id_registered		= flat_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	.irq_dest_mode			= 1, /* logical */

	.disable_esr			= 0,
	.dest_logical			= APIC_DEST_LOGICAL,
	.check_apicid_used		= NULL,

	.init_apic_ldr			= flat_init_apic_ldr,

	.ioapic_phys_id_map		= NULL,
	.setup_apic_routing		= NULL,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.apicid_to_cpu_present		= NULL,
	.check_phys_apicid_present	= default_check_phys_apicid_present,
	.phys_pkg_id			= flat_phys_pkg_id,

	.get_apic_id			= flat_get_apic_id,
	.set_apic_id			= set_apic_id,

	.calc_dest_apicid		= apic_flat_calc_apicid,

	.send_IPI			= default_send_IPI_single,
	.send_IPI_mask			= flat_send_IPI_mask,
	.send_IPI_mask_allbutself	= flat_send_IPI_mask_allbutself,
	.send_IPI_allbutself		= flat_send_IPI_allbutself,
	.send_IPI_all			= flat_send_IPI_all,
	.send_IPI_self			= apic_send_IPI_self,

	.inquire_remote_apic		= default_inquire_remote_apic,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.eoi_write			= native_apic_mem_write,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= native_apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_apic_wait_icr_idle,
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

static void physflat_send_IPI_allbutself(int vector)
{
	default_send_IPI_mask_allbutself_phys(cpu_online_mask, vector);
}

static void physflat_send_IPI_all(int vector)
{
	default_send_IPI_mask_sequence_phys(cpu_online_mask, vector);
}

static int physflat_probe(void)
{
	if (apic == &apic_physflat || num_possible_cpus() > 8)
		return 1;

	return 0;
}

static struct apic apic_physflat __ro_after_init = {

	.name				= "physical flat",
	.probe				= physflat_probe,
	.acpi_madt_oem_check		= physflat_acpi_madt_oem_check,
	.apic_id_valid			= default_apic_id_valid,
	.apic_id_registered		= flat_apic_id_registered,

	.irq_delivery_mode		= dest_Fixed,
	.irq_dest_mode			= 0, /* physical */

	.disable_esr			= 0,
	.dest_logical			= 0,
	.check_apicid_used		= NULL,

	/* not needed, but shouldn't hurt: */
	.init_apic_ldr			= flat_init_apic_ldr,

	.ioapic_phys_id_map		= NULL,
	.setup_apic_routing		= NULL,
	.cpu_present_to_apicid		= default_cpu_present_to_apicid,
	.apicid_to_cpu_present		= NULL,
	.check_phys_apicid_present	= default_check_phys_apicid_present,
	.phys_pkg_id			= flat_phys_pkg_id,

	.get_apic_id			= flat_get_apic_id,
	.set_apic_id			= set_apic_id,

	.calc_dest_apicid		= apic_default_calc_apicid,

	.send_IPI			= default_send_IPI_single_phys,
	.send_IPI_mask			= default_send_IPI_mask_sequence_phys,
	.send_IPI_mask_allbutself	= default_send_IPI_mask_allbutself_phys,
	.send_IPI_allbutself		= physflat_send_IPI_allbutself,
	.send_IPI_all			= physflat_send_IPI_all,
	.send_IPI_self			= apic_send_IPI_self,

	.inquire_remote_apic		= default_inquire_remote_apic,

	.read				= native_apic_mem_read,
	.write				= native_apic_mem_write,
	.eoi_write			= native_apic_mem_write,
	.icr_read			= native_apic_icr_read,
	.icr_write			= native_apic_icr_write,
	.wait_icr_idle			= native_apic_wait_icr_idle,
	.safe_wait_icr_idle		= native_safe_apic_wait_icr_idle,
};

/*
 * We need to check for physflat first, so this order is important.
 */
apic_drivers(apic_physflat, apic_flat);
