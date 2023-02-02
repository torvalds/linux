// SPDX-License-Identifier: GPL-2.0
#include <linux/sched/task.h>
#include <linux/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/facility.h>
#include <asm/sections.h>
#include <asm/physmem_info.h>
#include <asm/maccess.h>
#include <asm/abs_lowcore.h>
#include "decompressor.h"
#include "boot.h"

unsigned long __bootdata_preserved(s390_invalid_asce);

#define init_mm			(*(struct mm_struct *)vmlinux.init_mm_off)
#define swapper_pg_dir		vmlinux.swapper_pg_dir_off
#define invalid_pg_dir		vmlinux.invalid_pg_dir_off

/*
 * Mimic virt_to_kpte() in lack of init_mm symbol. Skip pmd NULL check though.
 */
static inline pte_t *__virt_to_kpte(unsigned long va)
{
	return pte_offset_kernel(pmd_offset(pud_offset(p4d_offset(pgd_offset_k(va), va), va), va), va);
}

enum populate_mode {
	POPULATE_NONE,
	POPULATE_ONE2ONE,
	POPULATE_ABS_LOWCORE,
};

static void *boot_crst_alloc(unsigned long val)
{
	unsigned long size = PAGE_SIZE << CRST_ALLOC_ORDER;
	unsigned long *table;

	table = (unsigned long *)physmem_alloc_top_down(RR_VMEM, size, size);
	crst_table_init(table, val);
	return table;
}

static pte_t *boot_pte_alloc(void)
{
	pte_t *pte;

	pte = (pte_t *)physmem_alloc_top_down(RR_VMEM, _PAGE_TABLE_SIZE, _PAGE_TABLE_SIZE);
	memset64((u64 *)pte, _PAGE_INVALID, PTRS_PER_PTE);
	return pte;
}

static unsigned long _pa(unsigned long addr, enum populate_mode mode)
{
	switch (mode) {
	case POPULATE_NONE:
		return -1;
	case POPULATE_ONE2ONE:
		return addr;
	case POPULATE_ABS_LOWCORE:
		return __abs_lowcore_pa(addr);
	default:
		return -1;
	}
}

static bool can_large_pud(pud_t *pu_dir, unsigned long addr, unsigned long end)
{
	return machine.has_edat2 &&
	       IS_ALIGNED(addr, PUD_SIZE) && (end - addr) >= PUD_SIZE;
}

static bool can_large_pmd(pmd_t *pm_dir, unsigned long addr, unsigned long end)
{
	return machine.has_edat1 &&
	       IS_ALIGNED(addr, PMD_SIZE) && (end - addr) >= PMD_SIZE;
}

static void pgtable_pte_populate(pmd_t *pmd, unsigned long addr, unsigned long end,
				 enum populate_mode mode)
{
	pte_t *pte, entry;

	pte = pte_offset_kernel(pmd, addr);
	for (; addr < end; addr += PAGE_SIZE, pte++) {
		if (pte_none(*pte)) {
			entry = __pte(_pa(addr, mode));
			entry = set_pte_bit(entry, PAGE_KERNEL_EXEC);
			set_pte(pte, entry);
		}
	}
}

static void pgtable_pmd_populate(pud_t *pud, unsigned long addr, unsigned long end,
				 enum populate_mode mode)
{
	unsigned long next;
	pmd_t *pmd, entry;
	pte_t *pte;

	pmd = pmd_offset(pud, addr);
	for (; addr < end; addr = next, pmd++) {
		next = pmd_addr_end(addr, end);
		if (pmd_none(*pmd)) {
			if (can_large_pmd(pmd, addr, next)) {
				entry = __pmd(_pa(addr, mode));
				entry = set_pmd_bit(entry, SEGMENT_KERNEL_EXEC);
				set_pmd(pmd, entry);
				continue;
			}
			pte = boot_pte_alloc();
			pmd_populate(&init_mm, pmd, pte);
		} else if (pmd_large(*pmd)) {
			continue;
		}
		pgtable_pte_populate(pmd, addr, next, mode);
	}
}

static void pgtable_pud_populate(p4d_t *p4d, unsigned long addr, unsigned long end,
				 enum populate_mode mode)
{
	unsigned long next;
	pud_t *pud, entry;
	pmd_t *pmd;

	pud = pud_offset(p4d, addr);
	for (; addr < end; addr = next, pud++) {
		next = pud_addr_end(addr, end);
		if (pud_none(*pud)) {
			if (can_large_pud(pud, addr, next)) {
				entry = __pud(_pa(addr, mode));
				entry = set_pud_bit(entry, REGION3_KERNEL_EXEC);
				set_pud(pud, entry);
				continue;
			}
			pmd = boot_crst_alloc(_SEGMENT_ENTRY_EMPTY);
			pud_populate(&init_mm, pud, pmd);
		} else if (pud_large(*pud)) {
			continue;
		}
		pgtable_pmd_populate(pud, addr, next, mode);
	}
}

static void pgtable_p4d_populate(pgd_t *pgd, unsigned long addr, unsigned long end,
				 enum populate_mode mode)
{
	unsigned long next;
	p4d_t *p4d;
	pud_t *pud;

	p4d = p4d_offset(pgd, addr);
	for (; addr < end; addr = next, p4d++) {
		next = p4d_addr_end(addr, end);
		if (p4d_none(*p4d)) {
			pud = boot_crst_alloc(_REGION3_ENTRY_EMPTY);
			p4d_populate(&init_mm, p4d, pud);
		}
		pgtable_pud_populate(p4d, addr, next, mode);
	}
}

static void pgtable_populate(unsigned long addr, unsigned long end, enum populate_mode mode)
{
	unsigned long next;
	pgd_t *pgd;
	p4d_t *p4d;

	pgd = pgd_offset(&init_mm, addr);
	for (; addr < end; addr = next, pgd++) {
		next = pgd_addr_end(addr, end);
		if (pgd_none(*pgd)) {
			p4d = boot_crst_alloc(_REGION2_ENTRY_EMPTY);
			pgd_populate(&init_mm, pgd, p4d);
		}
		pgtable_p4d_populate(pgd, addr, next, mode);
	}
}

void setup_vmem(unsigned long asce_limit)
{
	unsigned long start, end;
	unsigned long asce_type;
	unsigned long asce_bits;
	int i;

	if (asce_limit == _REGION1_SIZE) {
		asce_type = _REGION2_ENTRY_EMPTY;
		asce_bits = _ASCE_TYPE_REGION2 | _ASCE_TABLE_LENGTH;
	} else {
		asce_type = _REGION3_ENTRY_EMPTY;
		asce_bits = _ASCE_TYPE_REGION3 | _ASCE_TABLE_LENGTH;
	}
	s390_invalid_asce = invalid_pg_dir | _ASCE_TYPE_REGION3 | _ASCE_TABLE_LENGTH;

	crst_table_init((unsigned long *)swapper_pg_dir, asce_type);
	crst_table_init((unsigned long *)invalid_pg_dir, _REGION3_ENTRY_EMPTY);

	/*
	 * To allow prefixing the lowcore must be mapped with 4KB pages.
	 * To prevent creation of a large page at address 0 first map
	 * the lowcore and create the identity mapping only afterwards.
	 */
	pgtable_populate(0, sizeof(struct lowcore), POPULATE_ONE2ONE);
	for_each_physmem_usable_range(i, &start, &end)
		pgtable_populate(start, end, POPULATE_ONE2ONE);
	pgtable_populate(__abs_lowcore, __abs_lowcore + sizeof(struct lowcore),
			 POPULATE_ABS_LOWCORE);
	pgtable_populate(__memcpy_real_area, __memcpy_real_area + PAGE_SIZE,
			 POPULATE_NONE);
	memcpy_real_ptep = __virt_to_kpte(__memcpy_real_area);

	S390_lowcore.kernel_asce = swapper_pg_dir | asce_bits;
	S390_lowcore.user_asce = s390_invalid_asce;

	__ctl_load(S390_lowcore.kernel_asce, 1, 1);
	__ctl_load(S390_lowcore.user_asce, 7, 7);
	__ctl_load(S390_lowcore.kernel_asce, 13, 13);

	init_mm.context.asce = S390_lowcore.kernel_asce;
}
