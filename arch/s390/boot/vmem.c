// SPDX-License-Identifier: GPL-2.0
#include <linux/sched/task.h>
#include <linux/pgtable.h>
#include <linux/kasan.h>
#include <asm/pgalloc.h>
#include <asm/facility.h>
#include <asm/sections.h>
#include <asm/physmem_info.h>
#include <asm/maccess.h>
#include <asm/abs_lowcore.h>
#include "decompressor.h"
#include "boot.h"

unsigned long __bootdata_preserved(s390_invalid_asce);

#ifdef CONFIG_PROC_FS
atomic_long_t __bootdata_preserved(direct_pages_count[PG_DIRECT_MAP_MAX]);
#endif

#define init_mm			(*(struct mm_struct *)vmlinux.init_mm_off)
#define swapper_pg_dir		vmlinux.swapper_pg_dir_off
#define invalid_pg_dir		vmlinux.invalid_pg_dir_off

enum populate_mode {
	POPULATE_NONE,
	POPULATE_DIRECT,
	POPULATE_ABS_LOWCORE,
#ifdef CONFIG_KASAN
	POPULATE_KASAN_MAP_SHADOW,
	POPULATE_KASAN_ZERO_SHADOW,
	POPULATE_KASAN_SHALLOW
#endif
};

static void pgtable_populate(unsigned long addr, unsigned long end, enum populate_mode mode);

#ifdef CONFIG_KASAN

#define kasan_early_shadow_page	vmlinux.kasan_early_shadow_page_off
#define kasan_early_shadow_pte	((pte_t *)vmlinux.kasan_early_shadow_pte_off)
#define kasan_early_shadow_pmd	((pmd_t *)vmlinux.kasan_early_shadow_pmd_off)
#define kasan_early_shadow_pud	((pud_t *)vmlinux.kasan_early_shadow_pud_off)
#define kasan_early_shadow_p4d	((p4d_t *)vmlinux.kasan_early_shadow_p4d_off)
#define __sha(x)		((unsigned long)kasan_mem_to_shadow((void *)x))

static pte_t pte_z;

static inline void kasan_populate(unsigned long start, unsigned long end, enum populate_mode mode)
{
	start = PAGE_ALIGN_DOWN(__sha(start));
	end = PAGE_ALIGN(__sha(end));
	pgtable_populate(start, end, mode);
}

static void kasan_populate_shadow(void)
{
	pmd_t pmd_z = __pmd(__pa(kasan_early_shadow_pte) | _SEGMENT_ENTRY);
	pud_t pud_z = __pud(__pa(kasan_early_shadow_pmd) | _REGION3_ENTRY);
	p4d_t p4d_z = __p4d(__pa(kasan_early_shadow_pud) | _REGION2_ENTRY);
	unsigned long memgap_start = 0;
	unsigned long untracked_end;
	unsigned long start, end;
	int i;

	pte_z = __pte(__pa(kasan_early_shadow_page) | pgprot_val(PAGE_KERNEL_RO));
	if (!machine.has_nx)
		pte_z = clear_pte_bit(pte_z, __pgprot(_PAGE_NOEXEC));
	crst_table_init((unsigned long *)kasan_early_shadow_p4d, p4d_val(p4d_z));
	crst_table_init((unsigned long *)kasan_early_shadow_pud, pud_val(pud_z));
	crst_table_init((unsigned long *)kasan_early_shadow_pmd, pmd_val(pmd_z));
	memset64((u64 *)kasan_early_shadow_pte, pte_val(pte_z), PTRS_PER_PTE);

	/*
	 * Current memory layout:
	 * +- 0 -------------+	       +- shadow start -+
	 * |1:1 ident mapping|	      /|1/8 of ident map|
	 * |		     |	     / |		|
	 * +-end of ident map+	    /  +----------------+
	 * | ... gap ...     |	   /   |    kasan	|
	 * |		     |	  /    |  zero page	|
	 * +- vmalloc area  -+	 /     |   mapping	|
	 * | vmalloc_size    |	/      | (untracked)	|
	 * +- modules vaddr -+ /       +----------------+
	 * | 2Gb	     |/        |    unmapped	| allocated per module
	 * +- shadow start  -+	       +----------------+
	 * | 1/8 addr space  |	       | zero pg mapping| (untracked)
	 * +- shadow end ----+---------+- shadow end ---+
	 *
	 * Current memory layout (KASAN_VMALLOC):
	 * +- 0 -------------+	       +- shadow start -+
	 * |1:1 ident mapping|	      /|1/8 of ident map|
	 * |		     |	     / |		|
	 * +-end of ident map+	    /  +----------------+
	 * | ... gap ...     |	   /   | kasan zero page| (untracked)
	 * |		     |	  /    | mapping	|
	 * +- vmalloc area  -+	 /     +----------------+
	 * | vmalloc_size    |	/      |shallow populate|
	 * +- modules vaddr -+ /       +----------------+
	 * | 2Gb	     |/        |shallow populate|
	 * +- shadow start  -+	       +----------------+
	 * | 1/8 addr space  |	       | zero pg mapping| (untracked)
	 * +- shadow end ----+---------+- shadow end ---+
	 */

	for_each_physmem_usable_range(i, &start, &end) {
		kasan_populate(start, end, POPULATE_KASAN_MAP_SHADOW);
		if (memgap_start && physmem_info.info_source == MEM_DETECT_DIAG260)
			kasan_populate(memgap_start, start, POPULATE_KASAN_ZERO_SHADOW);
		memgap_start = end;
	}
	if (IS_ENABLED(CONFIG_KASAN_VMALLOC)) {
		untracked_end = VMALLOC_START;
		/* shallowly populate kasan shadow for vmalloc and modules */
		kasan_populate(VMALLOC_START, MODULES_END, POPULATE_KASAN_SHALLOW);
	} else {
		untracked_end = MODULES_VADDR;
	}
	/* populate kasan shadow for untracked memory */
	kasan_populate(ident_map_size, untracked_end, POPULATE_KASAN_ZERO_SHADOW);
	kasan_populate(MODULES_END, _REGION1_SIZE, POPULATE_KASAN_ZERO_SHADOW);
}

static bool kasan_pgd_populate_zero_shadow(pgd_t *pgd, unsigned long addr,
					   unsigned long end, enum populate_mode mode)
{
	if (mode == POPULATE_KASAN_ZERO_SHADOW &&
	    IS_ALIGNED(addr, PGDIR_SIZE) && end - addr >= PGDIR_SIZE) {
		pgd_populate(&init_mm, pgd, kasan_early_shadow_p4d);
		return true;
	}
	return false;
}

static bool kasan_p4d_populate_zero_shadow(p4d_t *p4d, unsigned long addr,
					   unsigned long end, enum populate_mode mode)
{
	if (mode == POPULATE_KASAN_ZERO_SHADOW &&
	    IS_ALIGNED(addr, P4D_SIZE) && end - addr >= P4D_SIZE) {
		p4d_populate(&init_mm, p4d, kasan_early_shadow_pud);
		return true;
	}
	return false;
}

static bool kasan_pud_populate_zero_shadow(pud_t *pud, unsigned long addr,
					   unsigned long end, enum populate_mode mode)
{
	if (mode == POPULATE_KASAN_ZERO_SHADOW &&
	    IS_ALIGNED(addr, PUD_SIZE) && end - addr >= PUD_SIZE) {
		pud_populate(&init_mm, pud, kasan_early_shadow_pmd);
		return true;
	}
	return false;
}

static bool kasan_pmd_populate_zero_shadow(pmd_t *pmd, unsigned long addr,
					   unsigned long end, enum populate_mode mode)
{
	if (mode == POPULATE_KASAN_ZERO_SHADOW &&
	    IS_ALIGNED(addr, PMD_SIZE) && end - addr >= PMD_SIZE) {
		pmd_populate(&init_mm, pmd, kasan_early_shadow_pte);
		return true;
	}
	return false;
}

static bool kasan_pte_populate_zero_shadow(pte_t *pte, enum populate_mode mode)
{
	pte_t entry;

	if (mode == POPULATE_KASAN_ZERO_SHADOW) {
		set_pte(pte, pte_z);
		return true;
	}
	return false;
}
#else

static inline void kasan_populate_shadow(void) {}

static inline bool kasan_pgd_populate_zero_shadow(pgd_t *pgd, unsigned long addr,
						  unsigned long end, enum populate_mode mode)
{
	return false;
}

static inline bool kasan_p4d_populate_zero_shadow(p4d_t *p4d, unsigned long addr,
						  unsigned long end, enum populate_mode mode)
{
	return false;
}

static inline bool kasan_pud_populate_zero_shadow(pud_t *pud, unsigned long addr,
						  unsigned long end, enum populate_mode mode)
{
	return false;
}

static inline bool kasan_pmd_populate_zero_shadow(pmd_t *pmd, unsigned long addr,
						  unsigned long end, enum populate_mode mode)
{
	return false;
}

static bool kasan_pte_populate_zero_shadow(pte_t *pte, enum populate_mode mode)
{
	return false;
}

#endif

/*
 * Mimic virt_to_kpte() in lack of init_mm symbol. Skip pmd NULL check though.
 */
static inline pte_t *__virt_to_kpte(unsigned long va)
{
	return pte_offset_kernel(pmd_offset(pud_offset(p4d_offset(pgd_offset_k(va), va), va), va), va);
}

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
	static void *pte_leftover;
	pte_t *pte;

	/*
	 * handling pte_leftovers this way helps to avoid memory fragmentation
	 * during POPULATE_KASAN_MAP_SHADOW when EDAT is off
	 */
	if (!pte_leftover) {
		pte_leftover = (void *)physmem_alloc_top_down(RR_VMEM, PAGE_SIZE, PAGE_SIZE);
		pte = pte_leftover + _PAGE_TABLE_SIZE;
	} else {
		pte = pte_leftover;
		pte_leftover = NULL;
	}

	memset64((u64 *)pte, _PAGE_INVALID, PTRS_PER_PTE);
	return pte;
}

static unsigned long _pa(unsigned long addr, unsigned long size, enum populate_mode mode)
{
	switch (mode) {
	case POPULATE_NONE:
		return -1;
	case POPULATE_DIRECT:
		return addr;
	case POPULATE_ABS_LOWCORE:
		return __abs_lowcore_pa(addr);
#ifdef CONFIG_KASAN
	case POPULATE_KASAN_MAP_SHADOW:
		addr = physmem_alloc_top_down(RR_VMEM, size, size);
		memset((void *)addr, 0, size);
		return addr;
#endif
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
	unsigned long pages = 0;
	pte_t *pte, entry;

	pte = pte_offset_kernel(pmd, addr);
	for (; addr < end; addr += PAGE_SIZE, pte++) {
		if (pte_none(*pte)) {
			if (kasan_pte_populate_zero_shadow(pte, mode))
				continue;
			entry = __pte(_pa(addr, PAGE_SIZE, mode));
			entry = set_pte_bit(entry, PAGE_KERNEL);
			if (!machine.has_nx)
				entry = clear_pte_bit(entry, __pgprot(_PAGE_NOEXEC));
			set_pte(pte, entry);
			pages++;
		}
	}
	if (mode == POPULATE_DIRECT)
		update_page_count(PG_DIRECT_MAP_4K, pages);
}

static void pgtable_pmd_populate(pud_t *pud, unsigned long addr, unsigned long end,
				 enum populate_mode mode)
{
	unsigned long next, pages = 0;
	pmd_t *pmd, entry;
	pte_t *pte;

	pmd = pmd_offset(pud, addr);
	for (; addr < end; addr = next, pmd++) {
		next = pmd_addr_end(addr, end);
		if (pmd_none(*pmd)) {
			if (kasan_pmd_populate_zero_shadow(pmd, addr, next, mode))
				continue;
			if (can_large_pmd(pmd, addr, next)) {
				entry = __pmd(_pa(addr, _SEGMENT_SIZE, mode));
				entry = set_pmd_bit(entry, SEGMENT_KERNEL);
				if (!machine.has_nx)
					entry = clear_pmd_bit(entry, __pgprot(_SEGMENT_ENTRY_NOEXEC));
				set_pmd(pmd, entry);
				pages++;
				continue;
			}
			pte = boot_pte_alloc();
			pmd_populate(&init_mm, pmd, pte);
		} else if (pmd_large(*pmd)) {
			continue;
		}
		pgtable_pte_populate(pmd, addr, next, mode);
	}
	if (mode == POPULATE_DIRECT)
		update_page_count(PG_DIRECT_MAP_1M, pages);
}

static void pgtable_pud_populate(p4d_t *p4d, unsigned long addr, unsigned long end,
				 enum populate_mode mode)
{
	unsigned long next, pages = 0;
	pud_t *pud, entry;
	pmd_t *pmd;

	pud = pud_offset(p4d, addr);
	for (; addr < end; addr = next, pud++) {
		next = pud_addr_end(addr, end);
		if (pud_none(*pud)) {
			if (kasan_pud_populate_zero_shadow(pud, addr, next, mode))
				continue;
			if (can_large_pud(pud, addr, next)) {
				entry = __pud(_pa(addr, _REGION3_SIZE, mode));
				entry = set_pud_bit(entry, REGION3_KERNEL);
				if (!machine.has_nx)
					entry = clear_pud_bit(entry, __pgprot(_REGION_ENTRY_NOEXEC));
				set_pud(pud, entry);
				pages++;
				continue;
			}
			pmd = boot_crst_alloc(_SEGMENT_ENTRY_EMPTY);
			pud_populate(&init_mm, pud, pmd);
		} else if (pud_large(*pud)) {
			continue;
		}
		pgtable_pmd_populate(pud, addr, next, mode);
	}
	if (mode == POPULATE_DIRECT)
		update_page_count(PG_DIRECT_MAP_2G, pages);
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
			if (kasan_p4d_populate_zero_shadow(p4d, addr, next, mode))
				continue;
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
			if (kasan_pgd_populate_zero_shadow(pgd, addr, next, mode))
				continue;
			p4d = boot_crst_alloc(_REGION2_ENTRY_EMPTY);
			pgd_populate(&init_mm, pgd, p4d);
		}
#ifdef CONFIG_KASAN
		if (mode == POPULATE_KASAN_SHALLOW)
			continue;
#endif
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
	pgtable_populate(0, sizeof(struct lowcore), POPULATE_DIRECT);
	for_each_physmem_usable_range(i, &start, &end)
		pgtable_populate(start, end, POPULATE_DIRECT);
	pgtable_populate(__abs_lowcore, __abs_lowcore + sizeof(struct lowcore),
			 POPULATE_ABS_LOWCORE);
	pgtable_populate(__memcpy_real_area, __memcpy_real_area + PAGE_SIZE,
			 POPULATE_NONE);
	memcpy_real_ptep = __virt_to_kpte(__memcpy_real_area);

	kasan_populate_shadow();

	S390_lowcore.kernel_asce = swapper_pg_dir | asce_bits;
	S390_lowcore.user_asce = s390_invalid_asce;

	__ctl_load(S390_lowcore.kernel_asce, 1, 1);
	__ctl_load(S390_lowcore.user_asce, 7, 7);
	__ctl_load(S390_lowcore.kernel_asce, 13, 13);

	init_mm.context.asce = S390_lowcore.kernel_asce;
}
