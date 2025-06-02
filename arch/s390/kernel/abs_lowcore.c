// SPDX-License-Identifier: GPL-2.0

#include <linux/pgtable.h>
#include <asm/abs_lowcore.h>
#include <asm/sections.h>

unsigned long __bootdata_preserved(__abs_lowcore);

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
