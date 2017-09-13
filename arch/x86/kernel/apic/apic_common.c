/*
 * Common functions shared between the various APIC flavours
 *
 * SPDX-License-Identifier: GPL-2.0
 */
#include <linux/irq.h>
#include <asm/apic.h>

int default_cpu_present_to_apicid(int mps_cpu)
{
	if (mps_cpu < nr_cpu_ids && cpu_present(mps_cpu))
		return (int)per_cpu(x86_bios_cpu_apicid, mps_cpu);
	else
		return BAD_APICID;
}

int default_check_phys_apicid_present(int phys_apicid)
{
	return physid_isset(phys_apicid, phys_cpu_present_map);
}
