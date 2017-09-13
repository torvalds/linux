/*
 * Common functions shared between the various APIC flavours
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/irq.h>
#include <asm/apic.h>

u32 apic_default_calc_apicid(unsigned int cpu)
{
	return per_cpu(x86_cpu_to_apicid, cpu);
}

u32 apic_flat_calc_apicid(unsigned int cpu)
{
	return 1U << cpu;
}

bool default_check_apicid_used(physid_mask_t *map, int apicid)
{
	return physid_isset(apicid, *map);
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

int default_apic_id_valid(int apicid)
{
	return (apicid < 255);
}
