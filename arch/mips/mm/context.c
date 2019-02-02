// SPDX-License-Identifier: GPL-2.0
#include <linux/mmu_context.h>

void get_new_mmu_context(struct mm_struct *mm)
{
	unsigned int cpu;
	u64 asid;

	cpu = smp_processor_id();
	asid = asid_cache(cpu);

	if (!((asid += cpu_asid_inc()) & cpu_asid_mask(&cpu_data[cpu]))) {
		if (cpu_has_vtag_icache)
			flush_icache_all();
		local_flush_tlb_all();	/* start new asid cycle */
	}

	cpu_context(cpu, mm) = asid_cache(cpu) = asid;
}
