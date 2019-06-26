// SPDX-License-Identifier: GPL-2.0
#include <linux/kasan.h>
#include <linux/sched/task.h>
#include <linux/memblock.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/kasan.h>
#include <asm/mem_detect.h>
#include <asm/processor.h>
#include <asm/sclp.h>
#include <asm/facility.h>
#include <asm/sections.h>
#include <asm/setup.h>

static unsigned long segment_pos __initdata;
static unsigned long segment_low __initdata;
static unsigned long pgalloc_pos __initdata;
static unsigned long pgalloc_low __initdata;
static unsigned long pgalloc_freeable __initdata;
static bool has_edat __initdata;
static bool has_nx __initdata;

#define __sha(x) ((unsigned long)kasan_mem_to_shadow((void *)x))

static pgd_t early_pg_dir[PTRS_PER_PGD] __initdata __aligned(PAGE_SIZE);

static void __init kasan_early_panic(const char *reason)
{
	sclp_early_printk("The Linux kernel failed to boot with the KernelAddressSanitizer:\n");
	sclp_early_printk(reason);
	disabled_wait();
}

static void * __init kasan_early_alloc_segment(void)
{
	segment_pos -= _SEGMENT_SIZE;

	if (segment_pos < segment_low)
		kasan_early_panic("out of memory during initialisation\n");

	return (void *)segment_pos;
}

static void * __init kasan_early_alloc_pages(unsigned int order)
{
	pgalloc_pos -= (PAGE_SIZE << order);

	if (pgalloc_pos < pgalloc_low)
		kasan_early_panic("out of memory during initialisation\n");

	return (void *)pgalloc_pos;
}

static void * __init kasan_early_crst_alloc(unsigned long val)
{
	unsigned long *table;

	table = kasan_early_alloc_pages(CRST_ALLOC_ORDER);
	if (table)
		crst_table_init(table, val);
	return table;
}

static pte_t * __init kasan_early_pte_alloc(void)
{
	static void *pte_leftover;
	pte_t *pte;

	BUILD_BUG_ON(_PAGE_TABLE_SIZE * 2 != PAGE_SIZE);

	if (!pte_leftover) {
		pte_leftover = kasan_early_alloc_pages(0);
		pte = pte_leftover + _PAGE_TABLE_SIZE;
	} else {
		pte = pte_leftover;
		pte_leftover = NULL;
	}
	memset64((u64 *)pte, _PAGE_INVALID, PTRS_PER_PTE);
	return pte;
}

enum populate_mode {
	POPULATE_ONE2ONE,
	POPULATE_MAP,
	POPULATE_ZERO_SHADOW
};
static void __init kasan_early_vmemmap_populate(unsigned long address,
						unsigned long end,
						enum populate_mode mode)
{
	unsigned long pgt_prot_zero, pgt_prot, sgt_prot;
	pgd_t *pg_dir;
	p4d_t *p4_dir;
	pud_t *pu_dir;
	pmd_t *pm_dir;
	pte_t *pt_dir;

	pgt_prot_zero = pgprot_val(PAGE_KERNEL_RO);
	if (!has_nx)
		pgt_prot_zero &= ~_PAGE_NOEXEC;
	pgt_prot = pgprot_val(PAGE_KERNEL_EXEC);
	sgt_prot = pgprot_val(SEGMENT_KERNEL_EXEC);

	while (address < end) {
		pg_dir = pgd_offset_k(address);
		if (pgd_none(*pg_dir)) {
			if (mode == POPULATE_ZERO_SHADOW &&
			    IS_ALIGNED(address, PGDIR_SIZE) &&
			    end - address >= PGDIR_SIZE) {
				pgd_populate(&init_mm, pg_dir,
						kasan_early_shadow_p4d);
				address = (address + PGDIR_SIZE) & PGDIR_MASK;
				continue;
			}
			p4_dir = kasan_early_crst_alloc(_REGION2_ENTRY_EMPTY);
			pgd_populate(&init_mm, pg_dir, p4_dir);
		}

		p4_dir = p4d_offset(pg_dir, address);
		if (p4d_none(*p4_dir)) {
			if (mode == POPULATE_ZERO_SHADOW &&
			    IS_ALIGNED(address, P4D_SIZE) &&
			    end - address >= P4D_SIZE) {
				p4d_populate(&init_mm, p4_dir,
						kasan_early_shadow_pud);
				address = (address + P4D_SIZE) & P4D_MASK;
				continue;
			}
			pu_dir = kasan_early_crst_alloc(_REGION3_ENTRY_EMPTY);
			p4d_populate(&init_mm, p4_dir, pu_dir);
		}

		pu_dir = pud_offset(p4_dir, address);
		if (pud_none(*pu_dir)) {
			if (mode == POPULATE_ZERO_SHADOW &&
			    IS_ALIGNED(address, PUD_SIZE) &&
			    end - address >= PUD_SIZE) {
				pud_populate(&init_mm, pu_dir,
						kasan_early_shadow_pmd);
				address = (address + PUD_SIZE) & PUD_MASK;
				continue;
			}
			pm_dir = kasan_early_crst_alloc(_SEGMENT_ENTRY_EMPTY);
			pud_populate(&init_mm, pu_dir, pm_dir);
		}

		pm_dir = pmd_offset(pu_dir, address);
		if (pmd_none(*pm_dir)) {
			if (mode == POPULATE_ZERO_SHADOW &&
			    IS_ALIGNED(address, PMD_SIZE) &&
			    end - address >= PMD_SIZE) {
				pmd_populate(&init_mm, pm_dir,
						kasan_early_shadow_pte);
				address = (address + PMD_SIZE) & PMD_MASK;
				continue;
			}
			/* the first megabyte of 1:1 is mapped with 4k pages */
			if (has_edat && address && end - address >= PMD_SIZE &&
			    mode != POPULATE_ZERO_SHADOW) {
				void *page;

				if (mode == POPULATE_ONE2ONE) {
					page = (void *)address;
				} else {
					page = kasan_early_alloc_segment();
					memset(page, 0, _SEGMENT_SIZE);
				}
				pmd_val(*pm_dir) = __pa(page) | sgt_prot;
				address = (address + PMD_SIZE) & PMD_MASK;
				continue;
			}

			pt_dir = kasan_early_pte_alloc();
			pmd_populate(&init_mm, pm_dir, pt_dir);
		} else if (pmd_large(*pm_dir)) {
			address = (address + PMD_SIZE) & PMD_MASK;
			continue;
		}

		pt_dir = pte_offset_kernel(pm_dir, address);
		if (pte_none(*pt_dir)) {
			void *page;

			switch (mode) {
			case POPULATE_ONE2ONE:
				page = (void *)address;
				pte_val(*pt_dir) = __pa(page) | pgt_prot;
				break;
			case POPULATE_MAP:
				page = kasan_early_alloc_pages(0);
				memset(page, 0, PAGE_SIZE);
				pte_val(*pt_dir) = __pa(page) | pgt_prot;
				break;
			case POPULATE_ZERO_SHADOW:
				page = kasan_early_shadow_page;
				pte_val(*pt_dir) = __pa(page) | pgt_prot_zero;
				break;
			}
		}
		address += PAGE_SIZE;
	}
}

static void __init kasan_set_pgd(pgd_t *pgd, unsigned long asce_type)
{
	unsigned long asce_bits;

	asce_bits = asce_type | _ASCE_TABLE_LENGTH;
	S390_lowcore.kernel_asce = (__pa(pgd) & PAGE_MASK) | asce_bits;
	S390_lowcore.user_asce = S390_lowcore.kernel_asce;

	__ctl_load(S390_lowcore.kernel_asce, 1, 1);
	__ctl_load(S390_lowcore.kernel_asce, 7, 7);
	__ctl_load(S390_lowcore.kernel_asce, 13, 13);
}

static void __init kasan_enable_dat(void)
{
	psw_t psw;

	psw.mask = __extract_psw();
	psw_bits(psw).dat = 1;
	psw_bits(psw).as = PSW_BITS_AS_HOME;
	__load_psw_mask(psw.mask);
}

static void __init kasan_early_detect_facilities(void)
{
	if (test_facility(8)) {
		has_edat = true;
		__ctl_set_bit(0, 23);
	}
	if (!noexec_disabled && test_facility(130)) {
		has_nx = true;
		__ctl_set_bit(0, 20);
	}
}

static unsigned long __init get_mem_detect_end(void)
{
	unsigned long start;
	unsigned long end;

	if (mem_detect.count) {
		__get_mem_detect_block(mem_detect.count - 1, &start, &end);
		return end;
	}
	return 0;
}

void __init kasan_early_init(void)
{
	unsigned long untracked_mem_end;
	unsigned long shadow_alloc_size;
	unsigned long initrd_end;
	unsigned long asce_type;
	unsigned long memsize;
	unsigned long vmax;
	unsigned long pgt_prot = pgprot_val(PAGE_KERNEL_RO);
	pte_t pte_z;
	pmd_t pmd_z = __pmd(__pa(kasan_early_shadow_pte) | _SEGMENT_ENTRY);
	pud_t pud_z = __pud(__pa(kasan_early_shadow_pmd) | _REGION3_ENTRY);
	p4d_t p4d_z = __p4d(__pa(kasan_early_shadow_pud) | _REGION2_ENTRY);

	kasan_early_detect_facilities();
	if (!has_nx)
		pgt_prot &= ~_PAGE_NOEXEC;
	pte_z = __pte(__pa(kasan_early_shadow_page) | pgt_prot);

	memsize = get_mem_detect_end();
	if (!memsize)
		kasan_early_panic("cannot detect physical memory size\n");
	/* respect mem= cmdline parameter */
	if (memory_end_set && memsize > memory_end)
		memsize = memory_end;
	memsize = min(memsize, KASAN_SHADOW_START);

	if (IS_ENABLED(CONFIG_KASAN_S390_4_LEVEL_PAGING)) {
		/* 4 level paging */
		BUILD_BUG_ON(!IS_ALIGNED(KASAN_SHADOW_START, P4D_SIZE));
		BUILD_BUG_ON(!IS_ALIGNED(KASAN_SHADOW_END, P4D_SIZE));
		crst_table_init((unsigned long *)early_pg_dir,
				_REGION2_ENTRY_EMPTY);
		untracked_mem_end = vmax = _REGION1_SIZE;
		asce_type = _ASCE_TYPE_REGION2;
	} else {
		/* 3 level paging */
		BUILD_BUG_ON(!IS_ALIGNED(KASAN_SHADOW_START, PUD_SIZE));
		BUILD_BUG_ON(!IS_ALIGNED(KASAN_SHADOW_END, PUD_SIZE));
		crst_table_init((unsigned long *)early_pg_dir,
				_REGION3_ENTRY_EMPTY);
		untracked_mem_end = vmax = _REGION2_SIZE;
		asce_type = _ASCE_TYPE_REGION3;
	}

	/* init kasan zero shadow */
	crst_table_init((unsigned long *)kasan_early_shadow_p4d,
				p4d_val(p4d_z));
	crst_table_init((unsigned long *)kasan_early_shadow_pud,
				pud_val(pud_z));
	crst_table_init((unsigned long *)kasan_early_shadow_pmd,
				pmd_val(pmd_z));
	memset64((u64 *)kasan_early_shadow_pte, pte_val(pte_z), PTRS_PER_PTE);

	shadow_alloc_size = memsize >> KASAN_SHADOW_SCALE_SHIFT;
	pgalloc_low = round_up((unsigned long)_end, _SEGMENT_SIZE);
	if (IS_ENABLED(CONFIG_BLK_DEV_INITRD)) {
		initrd_end =
		    round_up(INITRD_START + INITRD_SIZE, _SEGMENT_SIZE);
		pgalloc_low = max(pgalloc_low, initrd_end);
	}

	if (pgalloc_low + shadow_alloc_size > memsize)
		kasan_early_panic("out of memory during initialisation\n");

	if (has_edat) {
		segment_pos = round_down(memsize, _SEGMENT_SIZE);
		segment_low = segment_pos - shadow_alloc_size;
		pgalloc_pos = segment_low;
	} else {
		pgalloc_pos = memsize;
	}
	init_mm.pgd = early_pg_dir;
	/*
	 * Current memory layout:
	 * +- 0 -------------+	 +- shadow start -+
	 * | 1:1 ram mapping |	/| 1/8 ram	  |
	 * +- end of ram ----+ / +----------------+
	 * | ... gap ...     |/  |	kasan	  |
	 * +- shadow start --+	 |	zero	  |
	 * | 1/8 addr space  |	 |	page	  |
	 * +- shadow end    -+	 |	mapping	  |
	 * | ... gap ...     |\  |    (untracked) |
	 * +- modules vaddr -+ \ +----------------+
	 * | 2Gb	     |	\|	unmapped  | allocated per module
	 * +-----------------+	 +- shadow end ---+
	 */
	/* populate kasan shadow (for identity mapping and zero page mapping) */
	kasan_early_vmemmap_populate(__sha(0), __sha(memsize), POPULATE_MAP);
	if (IS_ENABLED(CONFIG_MODULES))
		untracked_mem_end = vmax - MODULES_LEN;
	kasan_early_vmemmap_populate(__sha(max_physmem_end),
				     __sha(untracked_mem_end),
				     POPULATE_ZERO_SHADOW);
	/* memory allocated for identity mapping structs will be freed later */
	pgalloc_freeable = pgalloc_pos;
	/* populate identity mapping */
	kasan_early_vmemmap_populate(0, memsize, POPULATE_ONE2ONE);
	kasan_set_pgd(early_pg_dir, asce_type);
	kasan_enable_dat();
	/* enable kasan */
	init_task.kasan_depth = 0;
	memblock_reserve(pgalloc_pos, memsize - pgalloc_pos);
	sclp_early_printk("KernelAddressSanitizer initialized\n");
}

void __init kasan_copy_shadow(pgd_t *pg_dir)
{
	/*
	 * At this point we are still running on early pages setup early_pg_dir,
	 * while swapper_pg_dir has just been initialized with identity mapping.
	 * Carry over shadow memory region from early_pg_dir to swapper_pg_dir.
	 */

	pgd_t *pg_dir_src;
	pgd_t *pg_dir_dst;
	p4d_t *p4_dir_src;
	p4d_t *p4_dir_dst;
	pud_t *pu_dir_src;
	pud_t *pu_dir_dst;

	pg_dir_src = pgd_offset_raw(early_pg_dir, KASAN_SHADOW_START);
	pg_dir_dst = pgd_offset_raw(pg_dir, KASAN_SHADOW_START);
	p4_dir_src = p4d_offset(pg_dir_src, KASAN_SHADOW_START);
	p4_dir_dst = p4d_offset(pg_dir_dst, KASAN_SHADOW_START);
	if (!p4d_folded(*p4_dir_src)) {
		/* 4 level paging */
		memcpy(p4_dir_dst, p4_dir_src,
		       (KASAN_SHADOW_SIZE >> P4D_SHIFT) * sizeof(p4d_t));
		return;
	}
	/* 3 level paging */
	pu_dir_src = pud_offset(p4_dir_src, KASAN_SHADOW_START);
	pu_dir_dst = pud_offset(p4_dir_dst, KASAN_SHADOW_START);
	memcpy(pu_dir_dst, pu_dir_src,
	       (KASAN_SHADOW_SIZE >> PUD_SHIFT) * sizeof(pud_t));
}

void __init kasan_free_early_identity(void)
{
	memblock_free(pgalloc_pos, pgalloc_freeable - pgalloc_pos);
}
