// SPDX-License-Identifier: GPL-2.0

#include <linux/pgtable.h>
#include <asm/abs_lowcore.h>

#define ABS_LOWCORE_UNMAPPED	1
#define ABS_LOWCORE_LAP_ON	2
#define ABS_LOWCORE_IRQS_ON	4

unsigned long __bootdata_preserved(__abs_lowcore);
bool __ro_after_init abs_lowcore_mapped;

int abs_lowcore_map(int cpu, struct lowcore *lc, bool alloc)
{
	unsigned long addr = __abs_lowcore + (cpu * sizeof(struct lowcore));
	unsigned long phys = __pa(lc);
	int rc, i;

	for (i = 0; i < LC_PAGES; i++) {
		rc = __vmem_map_4k_page(addr, phys, PAGE_KERNEL, alloc);
		if (rc) {
			/*
			 * Do not unmap allocated page tables in case the
			 * allocation was not requested. In such a case the
			 * request is expected coming from an atomic context,
			 * while the unmap attempt might sleep.
			 */
			if (alloc) {
				for (--i; i >= 0; i--) {
					addr -= PAGE_SIZE;
					vmem_unmap_4k_page(addr);
				}
			}
			return rc;
		}
		addr += PAGE_SIZE;
		phys += PAGE_SIZE;
	}
	return 0;
}

void abs_lowcore_unmap(int cpu)
{
	unsigned long addr = __abs_lowcore + (cpu * sizeof(struct lowcore));
	int i;

	for (i = 0; i < LC_PAGES; i++) {
		vmem_unmap_4k_page(addr);
		addr += PAGE_SIZE;
	}
}

struct lowcore *get_abs_lowcore(unsigned long *flags)
{
	unsigned long irq_flags;
	union ctlreg0 cr0;
	int cpu;

	*flags = 0;
	cpu = get_cpu();
	if (abs_lowcore_mapped) {
		return ((struct lowcore *)__abs_lowcore) + cpu;
	} else {
		if (cpu != 0)
			panic("Invalid unmapped absolute lowcore access\n");
		local_irq_save(irq_flags);
		if (!irqs_disabled_flags(irq_flags))
			*flags |= ABS_LOWCORE_IRQS_ON;
		__ctl_store(cr0.val, 0, 0);
		if (cr0.lap) {
			*flags |= ABS_LOWCORE_LAP_ON;
			__ctl_clear_bit(0, 28);
		}
		*flags |= ABS_LOWCORE_UNMAPPED;
		return lowcore_ptr[0];
	}
}

void put_abs_lowcore(struct lowcore *lc, unsigned long flags)
{
	if (abs_lowcore_mapped) {
		if (flags)
			panic("Invalid mapped absolute lowcore release\n");
	} else {
		if (smp_processor_id() != 0)
			panic("Invalid mapped absolute lowcore access\n");
		if (!(flags & ABS_LOWCORE_UNMAPPED))
			panic("Invalid unmapped absolute lowcore release\n");
		if (flags & ABS_LOWCORE_LAP_ON)
			__ctl_set_bit(0, 28);
		if (flags & ABS_LOWCORE_IRQS_ON)
			local_irq_enable();
	}
	put_cpu();
}
