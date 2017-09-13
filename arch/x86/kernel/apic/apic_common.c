/*
 * Common functions shared between the various APIC flavours
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/irq.h>
#include <asm/apic.h>

int default_cpu_mask_to_apicid(const struct cpumask *msk, struct irq_data *irqd,
			       unsigned int *apicid)
{
	unsigned int cpu = cpumask_first(msk);

	if (cpu >= nr_cpu_ids)
		return -EINVAL;
	*apicid = per_cpu(x86_cpu_to_apicid, cpu);
	irq_data_update_effective_affinity(irqd, cpumask_of(cpu));
	return 0;
}

int flat_cpu_mask_to_apicid(const struct cpumask *mask, struct irq_data *irqd,
			    unsigned int *apicid)

{
	struct cpumask *effmsk = irq_data_get_effective_affinity_mask(irqd);
	unsigned long cpu_mask = cpumask_bits(mask)[0] & APIC_ALL_CPUS;

	if (!cpu_mask)
		return -EINVAL;
	*apicid = (unsigned int)cpu_mask;
	cpumask_bits(effmsk)[0] = cpu_mask;
	return 0;
}

bool default_check_apicid_used(physid_mask_t *map, int apicid)
{
	return physid_isset(apicid, *map);
}

void flat_vector_allocation_domain(int cpu, struct cpumask *retmask,
				   const struct cpumask *mask)
{
	/*
	 * Careful. Some cpus do not strictly honor the set of cpus
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

void default_vector_allocation_domain(int cpu, struct cpumask *retmask,
				      const struct cpumask *mask)
{
	cpumask_copy(retmask, cpumask_of(cpu));
}

void default_ioapic_phys_id_map(physid_mask_t *phys_map, physid_mask_t *retmap)
{
	*retmap = *phys_map;
}

int default_cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < nr_cpu_ids && cpu_present(mps_cpu))
		return (int)per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}
EXPORT_SYMBOL_GPL(default_cpu_present_to_apicid);

int default_check_phys_apicid_present(int phys_apicid)
{
	return physid_isset(phys_apicid, phys_cpu_present_map);
}

const struct cpumask *default_target_cpus(void)
{
#ifdef CONFIG_SMP
	return cpu_online_mask;
#else
	return cpumask_of(0);
#endif
}

const struct cpumask *online_target_cpus(void)
{
	return cpu_online_mask;
}

int default_apic_id_valid(int apicid)
{
	return (apicid < 255);
}
