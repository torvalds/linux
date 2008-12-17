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
#include <linux/init.h>
#include <linux/hardirq.h>
#include <asm/smp.h>
#include <asm/ipi.h>
#include <asm/genapic.h>
#include <mach_apicdef.h>

#ifdef CONFIG_ACPI
#include <acpi/acpi_bus.h>
#endif

static int flat_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 1;
}

static const struct cpumask *flat_target_cpus(void)
{
	return cpu_online_mask;
}

static void flat_vector_allocation_domain(int cpu, struct cpumask *retmask)
{
	/* Careful. Some cpus do not strictly honor the set of cpus
	 * specified in the interrupt destination when using lowest
	 * priority interrupt delivery mode.
	 *
	 * In particular there was a hyperthreading cpu observed to
	 * deliver interrupts to the wrong hyperthread when only one
	 * hyperthread was specified in the interrupt desitination.
	 */
	cpumask_clear(retmask);
	cpumask_bits(retmask)[0] = APIC_ALL_CPUS;
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static void flat_init_apic_ldr(void)
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

static inline void _flat_send_IPI_mask(unsigned long mask, int vector)
{
	unsigned long flags;

	local_irq_save(flags);
	__send_IPI_dest_field(mask, vector, APIC_DEST_LOGICAL);
	local_irq_restore(flags);
}

static void flat_send_IPI_mask(const struct cpumask *cpumask, int vector)
{
	unsigned long mask = cpumask_bits(cpumask)[0];

	_flat_send_IPI_mask(mask, vector);
}

static void flat_send_IPI_mask_allbutself(const struct cpumask *cpumask,
					  int vector)
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
		__send_IPI_shortcut(APIC_DEST_ALLBUT, vector,APIC_DEST_LOGICAL);
	}
}

static void flat_send_IPI_all(int vector)
{
	if (vector == NMI_VECTOR)
		flat_send_IPI_mask(cpu_online_mask, vector);
	else
		__send_IPI_shortcut(APIC_DEST_ALLINC, vector, APIC_DEST_LOGICAL);
}

static unsigned int get_apic_id(unsigned long x)
{
	unsigned int id;

	id = (((x)>>24) & 0xFFu);
	return id;
}

static unsigned long set_apic_id(unsigned int id)
{
	unsigned long x;

	x = ((id & 0xFFu)<<24);
	return x;
}

static unsigned int read_xapic_id(void)
{
	unsigned int id;

	id = get_apic_id(apic_read(APIC_ID));
	return id;
}

static int flat_apic_id_registered(void)
{
	return physid_isset(read_xapic_id(), phys_cpu_present_map);
}

static unsigned int flat_cpu_mask_to_apicid(const struct cpumask *cpumask)
{
	return cpumask_bits(cpumask)[0] & APIC_ALL_CPUS;
}

static unsigned int flat_cpu_mask_to_apicid_and(const struct cpumask *cpumask,
						const struct cpumask *andmask)
{
	unsigned long mask1 = cpumask_bits(cpumask)[0] & APIC_ALL_CPUS;
	unsigned long mask2 = cpumask_bits(andmask)[0] & APIC_ALL_CPUS;

	return mask1 & mask2;
}

static unsigned int phys_pkg_id(int index_msb)
{
	return hard_smp_processor_id() >> index_msb;
}

struct genapic apic_flat =  {
	.name = "flat",
	.acpi_madt_oem_check = flat_acpi_madt_oem_check,
	.int_delivery_mode = dest_LowestPrio,
	.int_dest_mode = (APIC_DEST_LOGICAL != 0),
	.target_cpus = flat_target_cpus,
	.vector_allocation_domain = flat_vector_allocation_domain,
	.apic_id_registered = flat_apic_id_registered,
	.init_apic_ldr = flat_init_apic_ldr,
	.send_IPI_all = flat_send_IPI_all,
	.send_IPI_allbutself = flat_send_IPI_allbutself,
	.send_IPI_mask = flat_send_IPI_mask,
	.send_IPI_mask_allbutself = flat_send_IPI_mask_allbutself,
	.send_IPI_self = apic_send_IPI_self,
	.cpu_mask_to_apicid = flat_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and = flat_cpu_mask_to_apicid_and,
	.phys_pkg_id = phys_pkg_id,
	.get_apic_id = get_apic_id,
	.set_apic_id = set_apic_id,
	.apic_id_mask = (0xFFu<<24),
};

/*
 * Physflat mode is used when there are more than 8 CPUs on a AMD system.
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
	if (acpi_gbl_FADT.header.revision > FADT2_REVISION_ID &&
		(acpi_gbl_FADT.flags & ACPI_FADT_APIC_PHYSICAL)) {
		printk(KERN_DEBUG "system APIC only can use physical flat");
		return 1;
	}
#endif

	return 0;
}

static const struct cpumask *physflat_target_cpus(void)
{
	return cpu_online_mask;
}

static void physflat_vector_allocation_domain(int cpu, struct cpumask *retmask)
{
	cpumask_clear(retmask);
	cpumask_set_cpu(cpu, retmask);
}

static void physflat_send_IPI_mask(const struct cpumask *cpumask, int vector)
{
	send_IPI_mask_sequence(cpumask, vector);
}

static void physflat_send_IPI_mask_allbutself(const struct cpumask *cpumask,
					      int vector)
{
	send_IPI_mask_allbutself(cpumask, vector);
}

static void physflat_send_IPI_allbutself(int vector)
{
	send_IPI_mask_allbutself(cpu_online_mask, vector);
}

static void physflat_send_IPI_all(int vector)
{
	physflat_send_IPI_mask(cpu_online_mask, vector);
}

static unsigned int physflat_cpu_mask_to_apicid(const struct cpumask *cpumask)
{
	int cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	cpu = cpumask_first(cpumask);
	if ((unsigned)cpu < nr_cpu_ids)
		return per_cpu(x86_cpu_to_apicid, cpu);
	else
		return BAD_APICID;
}

static unsigned int
physflat_cpu_mask_to_apicid_and(const struct cpumask *cpumask,
				const struct cpumask *andmask)
{
	int cpu;

	/*
	 * We're using fixed IRQ delivery, can only return one phys APIC ID.
	 * May as well be the first.
	 */
	for_each_cpu_and(cpu, cpumask, andmask)
		if (cpumask_test_cpu(cpu, cpu_online_mask))
			break;
	if (cpu < nr_cpu_ids)
		return per_cpu(x86_cpu_to_apicid, cpu);
	return BAD_APICID;
}

struct genapic apic_physflat =  {
	.name = "physical flat",
	.acpi_madt_oem_check = physflat_acpi_madt_oem_check,
	.int_delivery_mode = dest_Fixed,
	.int_dest_mode = (APIC_DEST_PHYSICAL != 0),
	.target_cpus = physflat_target_cpus,
	.vector_allocation_domain = physflat_vector_allocation_domain,
	.apic_id_registered = flat_apic_id_registered,
	.init_apic_ldr = flat_init_apic_ldr,/*not needed, but shouldn't hurt*/
	.send_IPI_all = physflat_send_IPI_all,
	.send_IPI_allbutself = physflat_send_IPI_allbutself,
	.send_IPI_mask = physflat_send_IPI_mask,
	.send_IPI_mask_allbutself = physflat_send_IPI_mask_allbutself,
	.send_IPI_self = apic_send_IPI_self,
	.cpu_mask_to_apicid = physflat_cpu_mask_to_apicid,
	.cpu_mask_to_apicid_and = physflat_cpu_mask_to_apicid_and,
	.phys_pkg_id = phys_pkg_id,
	.get_apic_id = get_apic_id,
	.set_apic_id = set_apic_id,
	.apic_id_mask = (0xFFu<<24),
};
