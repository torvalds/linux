/*
 * Common functions shared between the various APIC flavours
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/irq.h>
#include <asm/apic.h>

#include "local.h"

u32 apic_default_calc_apicid(unsigned int cpu)
{
	return per_cpu(x86_cpu_to_apicid, cpu);
}

u32 apic_flat_calc_apicid(unsigned int cpu)
{
	return 1U << cpu;
}

bool default_check_apicid_used(physid_mask_t *map, u32 apicid)
{
	return physid_isset(apicid, *map);
}

void default_ioapic_phys_id_map(physid_mask_t *phys_map, physid_mask_t *retmap)
{
	*retmap = *phys_map;
}

u32 default_cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < nr_cpu_ids && cpu_present(mps_cpu))
		return (int)per_cpu(x86_cpu_to_apicid, mps_cpu);
	else
		return BAD_APICID;
}
EXPORT_SYMBOL_GPL(default_cpu_present_to_apicid);

bool default_apic_id_registered(void)
{
	return physid_isset(read_apic_id(), phys_cpu_present_map);
}

/*
 * Set up the logical destination ID when the APIC operates in logical
 * destination mode.
 */
void default_init_apic_ldr(void)
{
	unsigned long val;

	apic_write(APIC_DFR, APIC_DFR_FLAT);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val |= SET_APIC_LOGICAL_ID(1UL << smp_processor_id());
	apic_write(APIC_LDR, val);
}
