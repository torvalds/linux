// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/mm/mmu.c
 *
 * Copyright (C) 1995-2005 Russell King
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/cache.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kexec.h>
#include <linux/libfdt.h>
#include <linux/mman.h>
#include <linux/nodemask.h>
#include <linux/memblock.h>
#include <linux/memremap.h>
#include <linux/memory.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/set_memory.h>
#include <linux/kfence.h>
#include <linux/pkeys.h>
#include <linux/mm_inline.h>
#include <linux/pagewalk.h>
#include <linux/stop_machine.h>

#include <asm/barrier.h>
#include <asm/cputype.h>
#include <asm/fixmap.h>
#include <asm/kasan.h>
#include <asm/kernel-pgtable.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <linux/sizes.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <asm/ptdump.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>
#include <asm/kfence.h>

#define NO_BLOCK_MAPPINGS	BIT(0)
#define NO_CONT_MAPPINGS	BIT(1)
#define NO_EXEC_MAPPINGS	BIT(2)	/* assumes FEAT_HPDS is not used */

DEFINE_STATIC_KEY_FALSE(arm64_ptdump_lock_key);

u64 kimage_voffset __ro_after_init;
EXPORT_SYMBOL(kimage_voffset);

u32 __boot_cpu_mode[] = { BOOT_CPU_MODE_EL2, BOOT_CPU_MODE_EL1 };

static bool rodata_is_rw __ro_after_init = true;

/*
 * The booting CPU updates the failed status @__early_cpu_boot_status,
 * with MMU turned off.
 */
long __section(".mmuoff.data.write") __early_cpu_boot_status;

/*
 * Empty_zero_page is a special page that is used for zero-initialized data
 * and COW.
 */
unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)] __page_aligned_bss;
EXPORT_SYMBOL(empty_zero_page);

static DEFINE_SPINLOCK(swapper_pgdir_lock);
static DEFINE_MUTEX(fixmap_lock);

void noinstr set_swapper_pgd(pgd_t *pgdp, pgd_t pgd)
{
	pgd_t *fixmap_pgdp;

	/*
	 * Don't bother with the fixmap if swapper_pg_dir is still mapped
	 * writable in the kernel mapping.
	 */
	if (rodata_is_rw) {
		WRITE_ONCE(*pgdp, pgd);
		dsb(ishst);
		isb();
		return;
	}

	spin_lock(&swapper_pgdir_lock);
	fixmap_pgdp = pgd_set_fixmap(__pa_symbol(pgdp));
	WRITE_ONCE(*fixmap_pgdp, pgd);
	/*
	 * We need dsb(ishst) here to ensure the page-table-walker sees
	 * our new entry before set_p?d() returns. The fixmap's
	 * flush_tlb_kernel_range() via clear_fixmap() does this for us.
	 */
	pgd_clear_fixmap();
	spin_unlock(&swapper_pgdir_lock);
}

pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
			      unsigned long size, pgprot_t vma_prot)
{
	if (!pfn_is_map_memory(pfn))
		return pgprot_noncached(vma_prot);
	else if (file->f_flags & O_SYNC)
		return pgprot_writecombine(vma_prot);
	return vma_prot;
}
EXPORT_SYMBOL(phys_mem_access_prot);

static phys_addr_t __init early_pgtable_alloc(enum pgtable_type pgtable_type)
{
	phys_addr_t phys;

	phys = memblock_phys_alloc_range(PAGE_SIZE, PAGE_SIZE, 0,
					 MEMBLOCK_ALLOC_NOLEAKTRACE);
	if (!phys)
		panic("Failed to allocate page table page\n");

	return phys;
}

bool pgattr_change_is_safe(pteval_t old, pteval_t new)
{
	/*
	 * The following mapping attributes may be updated in live
	 * kernel mappings without the need for break-before-make.
	 */
	pteval_t mask = PTE_PXN | PTE_RDONLY | PTE_WRITE | PTE_NG |
			PTE_SWBITS_MASK;

	/* creating or taking down mappings is always safe */
	if (!pte_valid(__pte(old)) || !pte_valid(__pte(new)))
		return true;

	/* A live entry's pfn should not change */
	if (pte_pfn(__pte(old)) != pte_pfn(__pte(new)))
		return false;

	/* live contiguous mappings may not be manipulated at all */
	if ((old | new) & PTE_CONT)
		return false;

	/* Transitioning from Non-Global to Global is unsafe */
	if (old & ~new & PTE_NG)
		return false;

	/*
	 * Changing the memory type between Normal and Normal-Tagged is safe
	 * since Tagged is considered a permission attribute from the
	 * mismatched attribute aliases perspective.
	 */
	if (((old & PTE_ATTRINDX_MASK) == PTE_ATTRINDX(MT_NORMAL) ||
	     (old & PTE_ATTRINDX_MASK) == PTE_ATTRINDX(MT_NORMAL_TAGGED)) &&
	    ((new & PTE_ATTRINDX_MASK) == PTE_ATTRINDX(MT_NORMAL) ||
	     (new & PTE_ATTRINDX_MASK) == PTE_ATTRINDX(MT_NORMAL_TAGGED)))
		mask |= PTE_ATTRINDX_MASK;

	return ((old ^ new) & ~mask) == 0;
}

static void init_clear_pgtable(void *table)
{
	clear_page(table);

	/* Ensure the zeroing is observed by page table walks. */
	dsb(ishst);
}

static void init_pte(pte_t *ptep, unsigned long addr, unsigned long end,
		     phys_addr_t phys, pgprot_t prot)
{
	do {
		pte_t old_pte = __ptep_get(ptep);

		/*
		 * Required barriers to make this visible to the table walker
		 * are deferred to the end of alloc_init_cont_pte().
		 */
		__set_pte_nosync(ptep, pfn_pte(__phys_to_pfn(phys), prot));

		/*
		 * After the PTE entry has been populated once, we
		 * only allow updates to the permission attributes.
		 */
		BUG_ON(!pgattr_change_is_safe(pte_val(old_pte),
					      pte_val(__ptep_get(ptep))));

		phys += PAGE_SIZE;
	} while (ptep++, addr += PAGE_SIZE, addr != end);
}

static void alloc_init_cont_pte(pmd_t *pmdp, unsigned long addr,
				unsigned long end, phys_addr_t phys,
				pgprot_t prot,
				phys_addr_t (*pgtable_alloc)(enum pgtable_type),
				int flags)
{
	unsigned long next;
	pmd_t pmd = READ_ONCE(*pmdp);
	pte_t *ptep;

	BUG_ON(pmd_sect(pmd));
	if (pmd_none(pmd)) {
		pmdval_t pmdval = PMD_TYPE_TABLE | PMD_TABLE_UXN | PMD_TABLE_AF;
		phys_addr_t pte_phys;

		if (flags & NO_EXEC_MAPPINGS)
			pmdval |= PMD_TABLE_PXN;
		BUG_ON(!pgtable_alloc);
		pte_phys = pgtable_alloc(TABLE_PTE);
		ptep = pte_set_fixmap(pte_phys);
		init_clear_pgtable(ptep);
		ptep += pte_index(addr);
		__pmd_populate(pmdp, pte_phys, pmdval);
	} else {
		BUG_ON(pmd_bad(pmd));
		ptep = pte_set_fixmap_offset(pmdp, addr);
	}

	do {
		pgprot_t __prot = prot;

		next = pte_cont_addr_end(addr, end);

		/* use a contiguous mapping if the range is suitably aligned */
		if ((((addr | next | phys) & ~CONT_PTE_MASK) == 0) &&
		    (flags & NO_CONT_MAPPINGS) == 0)
			__prot = __pgprot(pgprot_val(prot) | PTE_CONT);

		init_pte(ptep, addr, next, phys, __prot);

		ptep += pte_index(next) - pte_index(addr);
		phys += next - addr;
	} while (addr = next, addr != end);

	/*
	 * Note: barriers and maintenance necessary to clear the fixmap slot
	 * ensure that all previous pgtable writes are visible to the table
	 * walker.
	 */
	pte_clear_fixmap();
}

static void init_pmd(pmd_t *pmdp, unsigned long addr, unsigned long end,
		     phys_addr_t phys, pgprot_t prot,
		     phys_addr_t (*pgtable_alloc)(enum pgtable_type), int flags)
{
	unsigned long next;

	do {
		pmd_t old_pmd = READ_ONCE(*pmdp);

		next = pmd_addr_end(addr, end);

		/* try section mapping first */
		if (((addr | next | phys) & ~PMD_MASK) == 0 &&
		    (flags & NO_BLOCK_MAPPINGS) == 0) {
			pmd_set_huge(pmdp, phys, prot);

			/*
			 * After the PMD entry has been populated once, we
			 * only allow updates to the permission attributes.
			 */
			BUG_ON(!pgattr_change_is_safe(pmd_val(old_pmd),
						      READ_ONCE(pmd_val(*pmdp))));
		} else {
			alloc_init_cont_pte(pmdp, addr, next, phys, prot,
					    pgtable_alloc, flags);

			BUG_ON(pmd_val(old_pmd) != 0 &&
			       pmd_val(old_pmd) != READ_ONCE(pmd_val(*pmdp)));
		}
		phys += next - addr;
	} while (pmdp++, addr = next, addr != end);
}

static void alloc_init_cont_pmd(pud_t *pudp, unsigned long addr,
				unsigned long end, phys_addr_t phys,
				pgprot_t prot,
				phys_addr_t (*pgtable_alloc)(enum pgtable_type),
				int flags)
{
	unsigned long next;
	pud_t pud = READ_ONCE(*pudp);
	pmd_t *pmdp;

	/*
	 * Check for initial section mappings in the pgd/pud.
	 */
	BUG_ON(pud_sect(pud));
	if (pud_none(pud)) {
		pudval_t pudval = PUD_TYPE_TABLE | PUD_TABLE_UXN | PUD_TABLE_AF;
		phys_addr_t pmd_phys;

		if (flags & NO_EXEC_MAPPINGS)
			pudval |= PUD_TABLE_PXN;
		BUG_ON(!pgtable_alloc);
		pmd_phys = pgtable_alloc(TABLE_PMD);
		pmdp = pmd_set_fixmap(pmd_phys);
		init_clear_pgtable(pmdp);
		pmdp += pmd_index(addr);
		__pud_populate(pudp, pmd_phys, pudval);
	} else {
		BUG_ON(pud_bad(pud));
		pmdp = pmd_set_fixmap_offset(pudp, addr);
	}

	do {
		pgprot_t __prot = prot;

		next = pmd_cont_addr_end(addr, end);

		/* use a contiguous mapping if the range is suitably aligned */
		if ((((addr | next | phys) & ~CONT_PMD_MASK) == 0) &&
		    (flags & NO_CONT_MAPPINGS) == 0)
			__prot = __pgprot(pgprot_val(prot) | PTE_CONT);

		init_pmd(pmdp, addr, next, phys, __prot, pgtable_alloc, flags);

		pmdp += pmd_index(next) - pmd_index(addr);
		phys += next - addr;
	} while (addr = next, addr != end);

	pmd_clear_fixmap();
}

static void alloc_init_pud(p4d_t *p4dp, unsigned long addr, unsigned long end,
			   phys_addr_t phys, pgprot_t prot,
			   phys_addr_t (*pgtable_alloc)(enum pgtable_type),
			   int flags)
{
	unsigned long next;
	p4d_t p4d = READ_ONCE(*p4dp);
	pud_t *pudp;

	if (p4d_none(p4d)) {
		p4dval_t p4dval = P4D_TYPE_TABLE | P4D_TABLE_UXN | P4D_TABLE_AF;
		phys_addr_t pud_phys;

		if (flags & NO_EXEC_MAPPINGS)
			p4dval |= P4D_TABLE_PXN;
		BUG_ON(!pgtable_alloc);
		pud_phys = pgtable_alloc(TABLE_PUD);
		pudp = pud_set_fixmap(pud_phys);
		init_clear_pgtable(pudp);
		pudp += pud_index(addr);
		__p4d_populate(p4dp, pud_phys, p4dval);
	} else {
		BUG_ON(p4d_bad(p4d));
		pudp = pud_set_fixmap_offset(p4dp, addr);
	}

	do {
		pud_t old_pud = READ_ONCE(*pudp);

		next = pud_addr_end(addr, end);

		/*
		 * For 4K granule only, attempt to put down a 1GB block
		 */
		if (pud_sect_supported() &&
		   ((addr | next | phys) & ~PUD_MASK) == 0 &&
		    (flags & NO_BLOCK_MAPPINGS) == 0) {
			pud_set_huge(pudp, phys, prot);

			/*
			 * After the PUD entry has been populated once, we
			 * only allow updates to the permission attributes.
			 */
			BUG_ON(!pgattr_change_is_safe(pud_val(old_pud),
						      READ_ONCE(pud_val(*pudp))));
		} else {
			alloc_init_cont_pmd(pudp, addr, next, phys, prot,
					    pgtable_alloc, flags);

			BUG_ON(pud_val(old_pud) != 0 &&
			       pud_val(old_pud) != READ_ONCE(pud_val(*pudp)));
		}
		phys += next - addr;
	} while (pudp++, addr = next, addr != end);

	pud_clear_fixmap();
}

static void alloc_init_p4d(pgd_t *pgdp, unsigned long addr, unsigned long end,
			   phys_addr_t phys, pgprot_t prot,
			   phys_addr_t (*pgtable_alloc)(enum pgtable_type),
			   int flags)
{
	unsigned long next;
	pgd_t pgd = READ_ONCE(*pgdp);
	p4d_t *p4dp;

	if (pgd_none(pgd)) {
		pgdval_t pgdval = PGD_TYPE_TABLE | PGD_TABLE_UXN | PGD_TABLE_AF;
		phys_addr_t p4d_phys;

		if (flags & NO_EXEC_MAPPINGS)
			pgdval |= PGD_TABLE_PXN;
		BUG_ON(!pgtable_alloc);
		p4d_phys = pgtable_alloc(TABLE_P4D);
		p4dp = p4d_set_fixmap(p4d_phys);
		init_clear_pgtable(p4dp);
		p4dp += p4d_index(addr);
		__pgd_populate(pgdp, p4d_phys, pgdval);
	} else {
		BUG_ON(pgd_bad(pgd));
		p4dp = p4d_set_fixmap_offset(pgdp, addr);
	}

	do {
		p4d_t old_p4d = READ_ONCE(*p4dp);

		next = p4d_addr_end(addr, end);

		alloc_init_pud(p4dp, addr, next, phys, prot,
			       pgtable_alloc, flags);

		BUG_ON(p4d_val(old_p4d) != 0 &&
		       p4d_val(old_p4d) != READ_ONCE(p4d_val(*p4dp)));

		phys += next - addr;
	} while (p4dp++, addr = next, addr != end);

	p4d_clear_fixmap();
}

static void __create_pgd_mapping_locked(pgd_t *pgdir, phys_addr_t phys,
					unsigned long virt, phys_addr_t size,
					pgprot_t prot,
					phys_addr_t (*pgtable_alloc)(enum pgtable_type),
					int flags)
{
	unsigned long addr, end, next;
	pgd_t *pgdp = pgd_offset_pgd(pgdir, virt);

	/*
	 * If the virtual and physical address don't have the same offset
	 * within a page, we cannot map the region as the caller expects.
	 */
	if (WARN_ON((phys ^ virt) & ~PAGE_MASK))
		return;

	phys &= PAGE_MASK;
	addr = virt & PAGE_MASK;
	end = PAGE_ALIGN(virt + size);

	do {
		next = pgd_addr_end(addr, end);
		alloc_init_p4d(pgdp, addr, next, phys, prot, pgtable_alloc,
			       flags);
		phys += next - addr;
	} while (pgdp++, addr = next, addr != end);
}

static void __create_pgd_mapping(pgd_t *pgdir, phys_addr_t phys,
				 unsigned long virt, phys_addr_t size,
				 pgprot_t prot,
				 phys_addr_t (*pgtable_alloc)(enum pgtable_type),
				 int flags)
{
	mutex_lock(&fixmap_lock);
	__create_pgd_mapping_locked(pgdir, phys, virt, size, prot,
				    pgtable_alloc, flags);
	mutex_unlock(&fixmap_lock);
}

#define INVALID_PHYS_ADDR	(-1ULL)

static phys_addr_t __pgd_pgtable_alloc(struct mm_struct *mm, gfp_t gfp,
				       enum pgtable_type pgtable_type)
{
	/* Page is zeroed by init_clear_pgtable() so don't duplicate effort. */
	struct ptdesc *ptdesc = pagetable_alloc(gfp & ~__GFP_ZERO, 0);
	phys_addr_t pa;

	if (!ptdesc)
		return INVALID_PHYS_ADDR;

	pa = page_to_phys(ptdesc_page(ptdesc));

	switch (pgtable_type) {
	case TABLE_PTE:
		BUG_ON(!pagetable_pte_ctor(mm, ptdesc));
		break;
	case TABLE_PMD:
		BUG_ON(!pagetable_pmd_ctor(mm, ptdesc));
		break;
	case TABLE_PUD:
		pagetable_pud_ctor(ptdesc);
		break;
	case TABLE_P4D:
		pagetable_p4d_ctor(ptdesc);
		break;
	}

	return pa;
}

static phys_addr_t
try_pgd_pgtable_alloc_init_mm(enum pgtable_type pgtable_type, gfp_t gfp)
{
	return __pgd_pgtable_alloc(&init_mm, gfp, pgtable_type);
}

static phys_addr_t __maybe_unused
pgd_pgtable_alloc_init_mm(enum pgtable_type pgtable_type)
{
	phys_addr_t pa;

	pa = __pgd_pgtable_alloc(&init_mm, GFP_PGTABLE_KERNEL, pgtable_type);
	BUG_ON(pa == INVALID_PHYS_ADDR);
	return pa;
}

static phys_addr_t
pgd_pgtable_alloc_special_mm(enum pgtable_type pgtable_type)
{
	phys_addr_t pa;

	pa = __pgd_pgtable_alloc(NULL, GFP_PGTABLE_KERNEL, pgtable_type);
	BUG_ON(pa == INVALID_PHYS_ADDR);
	return pa;
}

static void split_contpte(pte_t *ptep)
{
	int i;

	ptep = PTR_ALIGN_DOWN(ptep, sizeof(*ptep) * CONT_PTES);
	for (i = 0; i < CONT_PTES; i++, ptep++)
		__set_pte(ptep, pte_mknoncont(__ptep_get(ptep)));
}

static int split_pmd(pmd_t *pmdp, pmd_t pmd, gfp_t gfp, bool to_cont)
{
	pmdval_t tableprot = PMD_TYPE_TABLE | PMD_TABLE_UXN | PMD_TABLE_AF;
	unsigned long pfn = pmd_pfn(pmd);
	pgprot_t prot = pmd_pgprot(pmd);
	phys_addr_t pte_phys;
	pte_t *ptep;
	int i;

	pte_phys = try_pgd_pgtable_alloc_init_mm(TABLE_PTE, gfp);
	if (pte_phys == INVALID_PHYS_ADDR)
		return -ENOMEM;
	ptep = (pte_t *)phys_to_virt(pte_phys);

	if (pgprot_val(prot) & PMD_SECT_PXN)
		tableprot |= PMD_TABLE_PXN;

	prot = __pgprot((pgprot_val(prot) & ~PTE_TYPE_MASK) | PTE_TYPE_PAGE);
	prot = __pgprot(pgprot_val(prot) & ~PTE_CONT);
	if (to_cont)
		prot = __pgprot(pgprot_val(prot) | PTE_CONT);

	for (i = 0; i < PTRS_PER_PTE; i++, ptep++, pfn++)
		__set_pte(ptep, pfn_pte(pfn, prot));

	/*
	 * Ensure the pte entries are visible to the table walker by the time
	 * the pmd entry that points to the ptes is visible.
	 */
	dsb(ishst);
	__pmd_populate(pmdp, pte_phys, tableprot);

	return 0;
}

static void split_contpmd(pmd_t *pmdp)
{
	int i;

	pmdp = PTR_ALIGN_DOWN(pmdp, sizeof(*pmdp) * CONT_PMDS);
	for (i = 0; i < CONT_PMDS; i++, pmdp++)
		set_pmd(pmdp, pmd_mknoncont(pmdp_get(pmdp)));
}

static int split_pud(pud_t *pudp, pud_t pud, gfp_t gfp, bool to_cont)
{
	pudval_t tableprot = PUD_TYPE_TABLE | PUD_TABLE_UXN | PUD_TABLE_AF;
	unsigned int step = PMD_SIZE >> PAGE_SHIFT;
	unsigned long pfn = pud_pfn(pud);
	pgprot_t prot = pud_pgprot(pud);
	phys_addr_t pmd_phys;
	pmd_t *pmdp;
	int i;

	pmd_phys = try_pgd_pgtable_alloc_init_mm(TABLE_PMD, gfp);
	if (pmd_phys == INVALID_PHYS_ADDR)
		return -ENOMEM;
	pmdp = (pmd_t *)phys_to_virt(pmd_phys);

	if (pgprot_val(prot) & PMD_SECT_PXN)
		tableprot |= PUD_TABLE_PXN;

	prot = __pgprot((pgprot_val(prot) & ~PMD_TYPE_MASK) | PMD_TYPE_SECT);
	prot = __pgprot(pgprot_val(prot) & ~PTE_CONT);
	if (to_cont)
		prot = __pgprot(pgprot_val(prot) | PTE_CONT);

	for (i = 0; i < PTRS_PER_PMD; i++, pmdp++, pfn += step)
		set_pmd(pmdp, pfn_pmd(pfn, prot));

	/*
	 * Ensure the pmd entries are visible to the table walker by the time
	 * the pud entry that points to the pmds is visible.
	 */
	dsb(ishst);
	__pud_populate(pudp, pmd_phys, tableprot);

	return 0;
}

static int split_kernel_leaf_mapping_locked(unsigned long addr)
{
	pgd_t *pgdp, pgd;
	p4d_t *p4dp, p4d;
	pud_t *pudp, pud;
	pmd_t *pmdp, pmd;
	pte_t *ptep, pte;
	int ret = 0;

	/*
	 * PGD: If addr is PGD aligned then addr already describes a leaf
	 * boundary. If not present then there is nothing to split.
	 */
	if (ALIGN_DOWN(addr, PGDIR_SIZE) == addr)
		goto out;
	pgdp = pgd_offset_k(addr);
	pgd = pgdp_get(pgdp);
	if (!pgd_present(pgd))
		goto out;

	/*
	 * P4D: If addr is P4D aligned then addr already describes a leaf
	 * boundary. If not present then there is nothing to split.
	 */
	if (ALIGN_DOWN(addr, P4D_SIZE) == addr)
		goto out;
	p4dp = p4d_offset(pgdp, addr);
	p4d = p4dp_get(p4dp);
	if (!p4d_present(p4d))
		goto out;

	/*
	 * PUD: If addr is PUD aligned then addr already describes a leaf
	 * boundary. If not present then there is nothing to split. Otherwise,
	 * if we have a pud leaf, split to contpmd.
	 */
	if (ALIGN_DOWN(addr, PUD_SIZE) == addr)
		goto out;
	pudp = pud_offset(p4dp, addr);
	pud = pudp_get(pudp);
	if (!pud_present(pud))
		goto out;
	if (pud_leaf(pud)) {
		ret = split_pud(pudp, pud, GFP_PGTABLE_KERNEL, true);
		if (ret)
			goto out;
	}

	/*
	 * CONTPMD: If addr is CONTPMD aligned then addr already describes a
	 * leaf boundary. If not present then there is nothing to split.
	 * Otherwise, if we have a contpmd leaf, split to pmd.
	 */
	if (ALIGN_DOWN(addr, CONT_PMD_SIZE) == addr)
		goto out;
	pmdp = pmd_offset(pudp, addr);
	pmd = pmdp_get(pmdp);
	if (!pmd_present(pmd))
		goto out;
	if (pmd_leaf(pmd)) {
		if (pmd_cont(pmd))
			split_contpmd(pmdp);
		/*
		 * PMD: If addr is PMD aligned then addr already describes a
		 * leaf boundary. Otherwise, split to contpte.
		 */
		if (ALIGN_DOWN(addr, PMD_SIZE) == addr)
			goto out;
		ret = split_pmd(pmdp, pmd, GFP_PGTABLE_KERNEL, true);
		if (ret)
			goto out;
	}

	/*
	 * CONTPTE: If addr is CONTPTE aligned then addr already describes a
	 * leaf boundary. If not present then there is nothing to split.
	 * Otherwise, if we have a contpte leaf, split to pte.
	 */
	if (ALIGN_DOWN(addr, CONT_PTE_SIZE) == addr)
		goto out;
	ptep = pte_offset_kernel(pmdp, addr);
	pte = __ptep_get(ptep);
	if (!pte_present(pte))
		goto out;
	if (pte_cont(pte))
		split_contpte(ptep);

out:
	return ret;
}

static inline bool force_pte_mapping(void)
{
	const bool bbml2 = system_capabilities_finalized() ?
		system_supports_bbml2_noabort() : cpu_supports_bbml2_noabort();

	if (debug_pagealloc_enabled())
		return true;
	if (bbml2)
		return false;
	return rodata_full || arm64_kfence_can_set_direct_map() || is_realm_world();
}

static inline bool split_leaf_mapping_possible(void)
{
	/*
	 * !BBML2_NOABORT systems should never run into scenarios where we would
	 * have to split. So exit early and let calling code detect it and raise
	 * a warning.
	 */
	if (!system_supports_bbml2_noabort())
		return false;
	return !force_pte_mapping();
}

static DEFINE_MUTEX(pgtable_split_lock);

int split_kernel_leaf_mapping(unsigned long start, unsigned long end)
{
	int ret;

	/*
	 * Exit early if the region is within a pte-mapped area or if we can't
	 * split. For the latter case, the permission change code will raise a
	 * warning if not already pte-mapped.
	 */
	if (!split_leaf_mapping_possible() || is_kfence_address((void *)start))
		return 0;

	/*
	 * Ensure start and end are at least page-aligned since this is the
	 * finest granularity we can split to.
	 */
	if (start != PAGE_ALIGN(start) || end != PAGE_ALIGN(end))
		return -EINVAL;

	mutex_lock(&pgtable_split_lock);
	arch_enter_lazy_mmu_mode();

	/*
	 * The split_kernel_leaf_mapping_locked() may sleep, it is not a
	 * problem for ARM64 since ARM64's lazy MMU implementation allows
	 * sleeping.
	 *
	 * Optimize for the common case of splitting out a single page from a
	 * larger mapping. Here we can just split on the "least aligned" of
	 * start and end and this will guarantee that there must also be a split
	 * on the more aligned address since the both addresses must be in the
	 * same contpte block and it must have been split to ptes.
	 */
	if (end - start == PAGE_SIZE) {
		start = __ffs(start) < __ffs(end) ? start : end;
		ret = split_kernel_leaf_mapping_locked(start);
	} else {
		ret = split_kernel_leaf_mapping_locked(start);
		if (!ret)
			ret = split_kernel_leaf_mapping_locked(end);
	}

	arch_leave_lazy_mmu_mode();
	mutex_unlock(&pgtable_split_lock);
	return ret;
}

static int split_to_ptes_pud_entry(pud_t *pudp, unsigned long addr,
				   unsigned long next, struct mm_walk *walk)
{
	gfp_t gfp = *(gfp_t *)walk->private;
	pud_t pud = pudp_get(pudp);
	int ret = 0;

	if (pud_leaf(pud))
		ret = split_pud(pudp, pud, gfp, false);

	return ret;
}

static int split_to_ptes_pmd_entry(pmd_t *pmdp, unsigned long addr,
				   unsigned long next, struct mm_walk *walk)
{
	gfp_t gfp = *(gfp_t *)walk->private;
	pmd_t pmd = pmdp_get(pmdp);
	int ret = 0;

	if (pmd_leaf(pmd)) {
		if (pmd_cont(pmd))
			split_contpmd(pmdp);
		ret = split_pmd(pmdp, pmd, gfp, false);

		/*
		 * We have split the pmd directly to ptes so there is no need to
		 * visit each pte to check if they are contpte.
		 */
		walk->action = ACTION_CONTINUE;
	}

	return ret;
}

static int split_to_ptes_pte_entry(pte_t *ptep, unsigned long addr,
				   unsigned long next, struct mm_walk *walk)
{
	pte_t pte = __ptep_get(ptep);

	if (pte_cont(pte))
		split_contpte(ptep);

	return 0;
}

static const struct mm_walk_ops split_to_ptes_ops = {
	.pud_entry	= split_to_ptes_pud_entry,
	.pmd_entry	= split_to_ptes_pmd_entry,
	.pte_entry	= split_to_ptes_pte_entry,
};

static int range_split_to_ptes(unsigned long start, unsigned long end, gfp_t gfp)
{
	int ret;

	arch_enter_lazy_mmu_mode();
	ret = walk_kernel_page_table_range_lockless(start, end,
					&split_to_ptes_ops, NULL, &gfp);
	arch_leave_lazy_mmu_mode();

	return ret;
}

static bool linear_map_requires_bbml2 __initdata;

u32 idmap_kpti_bbml2_flag;

static void __init init_idmap_kpti_bbml2_flag(void)
{
	WRITE_ONCE(idmap_kpti_bbml2_flag, 1);
	/* Must be visible to other CPUs before stop_machine() is called. */
	smp_mb();
}

static int __init linear_map_split_to_ptes(void *__unused)
{
	/*
	 * Repainting the linear map must be done by CPU0 (the boot CPU) because
	 * that's the only CPU that we know supports BBML2. The other CPUs will
	 * be held in a waiting area with the idmap active.
	 */
	if (!smp_processor_id()) {
		unsigned long lstart = _PAGE_OFFSET(vabits_actual);
		unsigned long lend = PAGE_END;
		unsigned long kstart = (unsigned long)lm_alias(_stext);
		unsigned long kend = (unsigned long)lm_alias(__init_begin);
		int ret;

		/*
		 * Wait for all secondary CPUs to be put into the waiting area.
		 */
		smp_cond_load_acquire(&idmap_kpti_bbml2_flag, VAL == num_online_cpus());

		/*
		 * Walk all of the linear map [lstart, lend), except the kernel
		 * linear map alias [kstart, kend), and split all mappings to
		 * PTE. The kernel alias remains static throughout runtime so
		 * can continue to be safely mapped with large mappings.
		 */
		ret = range_split_to_ptes(lstart, kstart, GFP_ATOMIC);
		if (!ret)
			ret = range_split_to_ptes(kend, lend, GFP_ATOMIC);
		if (ret)
			panic("Failed to split linear map\n");
		flush_tlb_kernel_range(lstart, lend);

		/*
		 * Relies on dsb in flush_tlb_kernel_range() to avoid reordering
		 * before any page table split operations.
		 */
		WRITE_ONCE(idmap_kpti_bbml2_flag, 0);
	} else {
		typedef void (wait_split_fn)(void);
		extern wait_split_fn wait_linear_map_split_to_ptes;
		wait_split_fn *wait_fn;

		wait_fn = (void *)__pa_symbol(wait_linear_map_split_to_ptes);

		/*
		 * At least one secondary CPU doesn't support BBML2 so cannot
		 * tolerate the size of the live mappings changing. So have the
		 * secondary CPUs wait for the boot CPU to make the changes
		 * with the idmap active and init_mm inactive.
		 */
		cpu_install_idmap();
		wait_fn();
		cpu_uninstall_idmap();
	}

	return 0;
}

void __init linear_map_maybe_split_to_ptes(void)
{
	if (linear_map_requires_bbml2 && !system_supports_bbml2_noabort()) {
		init_idmap_kpti_bbml2_flag();
		stop_machine(linear_map_split_to_ptes, NULL, cpu_online_mask);
	}
}

/*
 * This function can only be used to modify existing table entries,
 * without allocating new levels of table. Note that this permits the
 * creation of new section or page entries.
 */
void __init create_mapping_noalloc(phys_addr_t phys, unsigned long virt,
				   phys_addr_t size, pgprot_t prot)
{
	if (virt < PAGE_OFFSET) {
		pr_warn("BUG: not creating mapping for %pa at 0x%016lx - outside kernel range\n",
			&phys, virt);
		return;
	}
	__create_pgd_mapping(init_mm.pgd, phys, virt, size, prot, NULL,
			     NO_CONT_MAPPINGS);
}

void __init create_pgd_mapping(struct mm_struct *mm, phys_addr_t phys,
			       unsigned long virt, phys_addr_t size,
			       pgprot_t prot, bool page_mappings_only)
{
	int flags = 0;

	BUG_ON(mm == &init_mm);

	if (page_mappings_only)
		flags = NO_BLOCK_MAPPINGS | NO_CONT_MAPPINGS;

	__create_pgd_mapping(mm->pgd, phys, virt, size, prot,
			     pgd_pgtable_alloc_special_mm, flags);
}

static void update_mapping_prot(phys_addr_t phys, unsigned long virt,
				phys_addr_t size, pgprot_t prot)
{
	if (virt < PAGE_OFFSET) {
		pr_warn("BUG: not updating mapping for %pa at 0x%016lx - outside kernel range\n",
			&phys, virt);
		return;
	}

	__create_pgd_mapping(init_mm.pgd, phys, virt, size, prot, NULL,
			     NO_CONT_MAPPINGS);

	/* flush the TLBs after updating live kernel mappings */
	flush_tlb_kernel_range(virt, virt + size);
}

static void __init __map_memblock(pgd_t *pgdp, phys_addr_t start,
				  phys_addr_t end, pgprot_t prot, int flags)
{
	__create_pgd_mapping(pgdp, start, __phys_to_virt(start), end - start,
			     prot, early_pgtable_alloc, flags);
}

void __init mark_linear_text_alias_ro(void)
{
	/*
	 * Remove the write permissions from the linear alias of .text/.rodata
	 */
	update_mapping_prot(__pa_symbol(_text), (unsigned long)lm_alias(_text),
			    (unsigned long)__init_begin - (unsigned long)_text,
			    PAGE_KERNEL_RO);
}

#ifdef CONFIG_KFENCE

bool __ro_after_init kfence_early_init = !!CONFIG_KFENCE_SAMPLE_INTERVAL;

/* early_param() will be parsed before map_mem() below. */
static int __init parse_kfence_early_init(char *arg)
{
	int val;

	if (get_option(&arg, &val))
		kfence_early_init = !!val;
	return 0;
}
early_param("kfence.sample_interval", parse_kfence_early_init);

static phys_addr_t __init arm64_kfence_alloc_pool(void)
{
	phys_addr_t kfence_pool;

	if (!kfence_early_init)
		return 0;

	kfence_pool = memblock_phys_alloc(KFENCE_POOL_SIZE, PAGE_SIZE);
	if (!kfence_pool) {
		pr_err("failed to allocate kfence pool\n");
		kfence_early_init = false;
		return 0;
	}

	/* Temporarily mark as NOMAP. */
	memblock_mark_nomap(kfence_pool, KFENCE_POOL_SIZE);

	return kfence_pool;
}

static void __init arm64_kfence_map_pool(phys_addr_t kfence_pool, pgd_t *pgdp)
{
	if (!kfence_pool)
		return;

	/* KFENCE pool needs page-level mapping. */
	__map_memblock(pgdp, kfence_pool, kfence_pool + KFENCE_POOL_SIZE,
			pgprot_tagged(PAGE_KERNEL),
			NO_BLOCK_MAPPINGS | NO_CONT_MAPPINGS);
	memblock_clear_nomap(kfence_pool, KFENCE_POOL_SIZE);
	__kfence_pool = phys_to_virt(kfence_pool);
}

bool arch_kfence_init_pool(void)
{
	unsigned long start = (unsigned long)__kfence_pool;
	unsigned long end = start + KFENCE_POOL_SIZE;
	int ret;

	/* Exit early if we know the linear map is already pte-mapped. */
	if (!split_leaf_mapping_possible())
		return true;

	/* Kfence pool is already pte-mapped for the early init case. */
	if (kfence_early_init)
		return true;

	mutex_lock(&pgtable_split_lock);
	ret = range_split_to_ptes(start, end, GFP_PGTABLE_KERNEL);
	mutex_unlock(&pgtable_split_lock);

	/*
	 * Since the system supports bbml2_noabort, tlb invalidation is not
	 * required here; the pgtable mappings have been split to pte but larger
	 * entries may safely linger in the TLB.
	 */

	return !ret;
}
#else /* CONFIG_KFENCE */

static inline phys_addr_t arm64_kfence_alloc_pool(void) { return 0; }
static inline void arm64_kfence_map_pool(phys_addr_t kfence_pool, pgd_t *pgdp) { }

#endif /* CONFIG_KFENCE */

static void __init map_mem(pgd_t *pgdp)
{
	static const u64 direct_map_end = _PAGE_END(VA_BITS_MIN);
	phys_addr_t kernel_start = __pa_symbol(_text);
	phys_addr_t kernel_end = __pa_symbol(__init_begin);
	phys_addr_t start, end;
	phys_addr_t early_kfence_pool;
	int flags = NO_EXEC_MAPPINGS;
	u64 i;

	/*
	 * Setting hierarchical PXNTable attributes on table entries covering
	 * the linear region is only possible if it is guaranteed that no table
	 * entries at any level are being shared between the linear region and
	 * the vmalloc region. Check whether this is true for the PGD level, in
	 * which case it is guaranteed to be true for all other levels as well.
	 * (Unless we are running with support for LPA2, in which case the
	 * entire reduced VA space is covered by a single pgd_t which will have
	 * been populated without the PXNTable attribute by the time we get here.)
	 */
	BUILD_BUG_ON(pgd_index(direct_map_end - 1) == pgd_index(direct_map_end) &&
		     pgd_index(_PAGE_OFFSET(VA_BITS_MIN)) != PTRS_PER_PGD - 1);

	early_kfence_pool = arm64_kfence_alloc_pool();

	linear_map_requires_bbml2 = !force_pte_mapping() && can_set_direct_map();

	if (force_pte_mapping())
		flags |= NO_BLOCK_MAPPINGS | NO_CONT_MAPPINGS;

	/*
	 * Take care not to create a writable alias for the
	 * read-only text and rodata sections of the kernel image.
	 * So temporarily mark them as NOMAP to skip mappings in
	 * the following for-loop
	 */
	memblock_mark_nomap(kernel_start, kernel_end - kernel_start);

	/* map all the memory banks */
	for_each_mem_range(i, &start, &end) {
		if (start >= end)
			break;
		/*
		 * The linear map must allow allocation tags reading/writing
		 * if MTE is present. Otherwise, it has the same attributes as
		 * PAGE_KERNEL.
		 */
		__map_memblock(pgdp, start, end, pgprot_tagged(PAGE_KERNEL),
			       flags);
	}

	/*
	 * Map the linear alias of the [_text, __init_begin) interval
	 * as non-executable now, and remove the write permission in
	 * mark_linear_text_alias_ro() below (which will be called after
	 * alternative patching has completed). This makes the contents
	 * of the region accessible to subsystems such as hibernate,
	 * but protects it from inadvertent modification or execution.
	 * Note that contiguous mappings cannot be remapped in this way,
	 * so we should avoid them here.
	 */
	__map_memblock(pgdp, kernel_start, kernel_end,
		       PAGE_KERNEL, NO_CONT_MAPPINGS);
	memblock_clear_nomap(kernel_start, kernel_end - kernel_start);
	arm64_kfence_map_pool(early_kfence_pool, pgdp);
}

void mark_rodata_ro(void)
{
	unsigned long section_size;

	/*
	 * mark .rodata as read only. Use __init_begin rather than __end_rodata
	 * to cover NOTES and EXCEPTION_TABLE.
	 */
	section_size = (unsigned long)__init_begin - (unsigned long)__start_rodata;
	WRITE_ONCE(rodata_is_rw, false);
	update_mapping_prot(__pa_symbol(__start_rodata), (unsigned long)__start_rodata,
			    section_size, PAGE_KERNEL_RO);
	/* mark the range between _text and _stext as read only. */
	update_mapping_prot(__pa_symbol(_text), (unsigned long)_text,
			    (unsigned long)_stext - (unsigned long)_text,
			    PAGE_KERNEL_RO);
}

static void __init declare_vma(struct vm_struct *vma,
			       void *va_start, void *va_end,
			       unsigned long vm_flags)
{
	phys_addr_t pa_start = __pa_symbol(va_start);
	unsigned long size = va_end - va_start;

	BUG_ON(!PAGE_ALIGNED(pa_start));
	BUG_ON(!PAGE_ALIGNED(size));

	if (!(vm_flags & VM_NO_GUARD))
		size += PAGE_SIZE;

	vma->addr	= va_start;
	vma->phys_addr	= pa_start;
	vma->size	= size;
	vma->flags	= VM_MAP | vm_flags;
	vma->caller	= __builtin_return_address(0);

	vm_area_add_early(vma);
}

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
#define KPTI_NG_TEMP_VA		(-(1UL << PMD_SHIFT))

static phys_addr_t kpti_ng_temp_alloc __initdata;

static phys_addr_t __init kpti_ng_pgd_alloc(enum pgtable_type type)
{
	kpti_ng_temp_alloc -= PAGE_SIZE;
	return kpti_ng_temp_alloc;
}

static int __init __kpti_install_ng_mappings(void *__unused)
{
	typedef void (kpti_remap_fn)(int, int, phys_addr_t, unsigned long);
	extern kpti_remap_fn idmap_kpti_install_ng_mappings;
	kpti_remap_fn *remap_fn;

	int cpu = smp_processor_id();
	int levels = CONFIG_PGTABLE_LEVELS;
	int order = order_base_2(levels);
	u64 kpti_ng_temp_pgd_pa = 0;
	pgd_t *kpti_ng_temp_pgd;
	u64 alloc = 0;

	if (levels == 5 && !pgtable_l5_enabled())
		levels = 4;
	else if (levels == 4 && !pgtable_l4_enabled())
		levels = 3;

	remap_fn = (void *)__pa_symbol(idmap_kpti_install_ng_mappings);

	if (!cpu) {
		alloc = __get_free_pages(GFP_ATOMIC | __GFP_ZERO, order);
		kpti_ng_temp_pgd = (pgd_t *)(alloc + (levels - 1) * PAGE_SIZE);
		kpti_ng_temp_alloc = kpti_ng_temp_pgd_pa = __pa(kpti_ng_temp_pgd);

		//
		// Create a minimal page table hierarchy that permits us to map
		// the swapper page tables temporarily as we traverse them.
		//
		// The physical pages are laid out as follows:
		//
		// +--------+-/-------+-/------ +-/------ +-\\\--------+
		// :  PTE[] : | PMD[] : | PUD[] : | P4D[] : ||| PGD[]  :
		// +--------+-\-------+-\------ +-\------ +-///--------+
		//      ^
		// The first page is mapped into this hierarchy at a PMD_SHIFT
		// aligned virtual address, so that we can manipulate the PTE
		// level entries while the mapping is active. The first entry
		// covers the PTE[] page itself, the remaining entries are free
		// to be used as a ad-hoc fixmap.
		//
		__create_pgd_mapping_locked(kpti_ng_temp_pgd, __pa(alloc),
					    KPTI_NG_TEMP_VA, PAGE_SIZE, PAGE_KERNEL,
					    kpti_ng_pgd_alloc, 0);
	}

	cpu_install_idmap();
	remap_fn(cpu, num_online_cpus(), kpti_ng_temp_pgd_pa, KPTI_NG_TEMP_VA);
	cpu_uninstall_idmap();

	if (!cpu) {
		free_pages(alloc, order);
		arm64_use_ng_mappings = true;
	}

	return 0;
}

void __init kpti_install_ng_mappings(void)
{
	/* Check whether KPTI is going to be used */
	if (!arm64_kernel_unmapped_at_el0())
		return;

	/*
	 * We don't need to rewrite the page-tables if either we've done
	 * it already or we have KASLR enabled and therefore have not
	 * created any global mappings at all.
	 */
	if (arm64_use_ng_mappings)
		return;

	init_idmap_kpti_bbml2_flag();
	stop_machine(__kpti_install_ng_mappings, NULL, cpu_online_mask);
}

static pgprot_t __init kernel_exec_prot(void)
{
	return rodata_enabled ? PAGE_KERNEL_ROX : PAGE_KERNEL_EXEC;
}

static int __init map_entry_trampoline(void)
{
	int i;

	if (!arm64_kernel_unmapped_at_el0())
		return 0;

	pgprot_t prot = kernel_exec_prot();
	phys_addr_t pa_start = __pa_symbol(__entry_tramp_text_start);

	/* The trampoline is always mapped and can therefore be global */
	pgprot_val(prot) &= ~PTE_NG;

	/* Map only the text into the trampoline page table */
	memset(tramp_pg_dir, 0, PGD_SIZE);
	__create_pgd_mapping(tramp_pg_dir, pa_start, TRAMP_VALIAS,
			     entry_tramp_text_size(), prot,
			     pgd_pgtable_alloc_init_mm, NO_BLOCK_MAPPINGS);

	/* Map both the text and data into the kernel page table */
	for (i = 0; i < DIV_ROUND_UP(entry_tramp_text_size(), PAGE_SIZE); i++)
		__set_fixmap(FIX_ENTRY_TRAMP_TEXT1 - i,
			     pa_start + i * PAGE_SIZE, prot);

	if (IS_ENABLED(CONFIG_RELOCATABLE))
		__set_fixmap(FIX_ENTRY_TRAMP_TEXT1 - i,
			     pa_start + i * PAGE_SIZE, PAGE_KERNEL_RO);

	return 0;
}
core_initcall(map_entry_trampoline);
#endif

/*
 * Declare the VMA areas for the kernel
 */
static void __init declare_kernel_vmas(void)
{
	static struct vm_struct vmlinux_seg[KERNEL_SEGMENT_COUNT];

	declare_vma(&vmlinux_seg[0], _text, _etext, VM_NO_GUARD);
	declare_vma(&vmlinux_seg[1], __start_rodata, __inittext_begin, VM_NO_GUARD);
	declare_vma(&vmlinux_seg[2], __inittext_begin, __inittext_end, VM_NO_GUARD);
	declare_vma(&vmlinux_seg[3], __initdata_begin, __initdata_end, VM_NO_GUARD);
	declare_vma(&vmlinux_seg[4], _data, _end, 0);
}

void __pi_map_range(phys_addr_t *pte, u64 start, u64 end, phys_addr_t pa,
		    pgprot_t prot, int level, pte_t *tbl, bool may_use_cont,
		    u64 va_offset);

static u8 idmap_ptes[IDMAP_LEVELS - 1][PAGE_SIZE] __aligned(PAGE_SIZE) __ro_after_init,
	  kpti_bbml2_ptes[IDMAP_LEVELS - 1][PAGE_SIZE] __aligned(PAGE_SIZE) __ro_after_init;

static void __init create_idmap(void)
{
	phys_addr_t start = __pa_symbol(__idmap_text_start);
	phys_addr_t end   = __pa_symbol(__idmap_text_end);
	phys_addr_t ptep  = __pa_symbol(idmap_ptes);

	__pi_map_range(&ptep, start, end, start, PAGE_KERNEL_ROX,
		       IDMAP_ROOT_LEVEL, (pte_t *)idmap_pg_dir, false,
		       __phys_to_virt(ptep) - ptep);

	if (linear_map_requires_bbml2 ||
	    (IS_ENABLED(CONFIG_UNMAP_KERNEL_AT_EL0) && !arm64_use_ng_mappings)) {
		phys_addr_t pa = __pa_symbol(&idmap_kpti_bbml2_flag);

		/*
		 * The KPTI G-to-nG conversion code needs a read-write mapping
		 * of its synchronization flag in the ID map. This is also used
		 * when splitting the linear map to ptes if a secondary CPU
		 * doesn't support bbml2.
		 */
		ptep = __pa_symbol(kpti_bbml2_ptes);
		__pi_map_range(&ptep, pa, pa + sizeof(u32), pa, PAGE_KERNEL,
			       IDMAP_ROOT_LEVEL, (pte_t *)idmap_pg_dir, false,
			       __phys_to_virt(ptep) - ptep);
	}
}

void __init paging_init(void)
{
	map_mem(swapper_pg_dir);

	memblock_allow_resize();

	create_idmap();
	declare_kernel_vmas();
}

#ifdef CONFIG_MEMORY_HOTPLUG
static void free_hotplug_page_range(struct page *page, size_t size,
				    struct vmem_altmap *altmap)
{
	if (altmap) {
		vmem_altmap_free(altmap, size >> PAGE_SHIFT);
	} else {
		WARN_ON(PageReserved(page));
		__free_pages(page, get_order(size));
	}
}

static void free_hotplug_pgtable_page(struct page *page)
{
	free_hotplug_page_range(page, PAGE_SIZE, NULL);
}

static bool pgtable_range_aligned(unsigned long start, unsigned long end,
				  unsigned long floor, unsigned long ceiling,
				  unsigned long mask)
{
	start &= mask;
	if (start < floor)
		return false;

	if (ceiling) {
		ceiling &= mask;
		if (!ceiling)
			return false;
	}

	if (end - 1 > ceiling - 1)
		return false;
	return true;
}

static void unmap_hotplug_pte_range(pmd_t *pmdp, unsigned long addr,
				    unsigned long end, bool free_mapped,
				    struct vmem_altmap *altmap)
{
	pte_t *ptep, pte;

	do {
		ptep = pte_offset_kernel(pmdp, addr);
		pte = __ptep_get(ptep);
		if (pte_none(pte))
			continue;

		WARN_ON(!pte_present(pte));
		__pte_clear(&init_mm, addr, ptep);
		flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
		if (free_mapped)
			free_hotplug_page_range(pte_page(pte),
						PAGE_SIZE, altmap);
	} while (addr += PAGE_SIZE, addr < end);
}

static void unmap_hotplug_pmd_range(pud_t *pudp, unsigned long addr,
				    unsigned long end, bool free_mapped,
				    struct vmem_altmap *altmap)
{
	unsigned long next;
	pmd_t *pmdp, pmd;

	do {
		next = pmd_addr_end(addr, end);
		pmdp = pmd_offset(pudp, addr);
		pmd = READ_ONCE(*pmdp);
		if (pmd_none(pmd))
			continue;

		WARN_ON(!pmd_present(pmd));
		if (pmd_sect(pmd)) {
			pmd_clear(pmdp);

			/*
			 * One TLBI should be sufficient here as the PMD_SIZE
			 * range is mapped with a single block entry.
			 */
			flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
			if (free_mapped)
				free_hotplug_page_range(pmd_page(pmd),
							PMD_SIZE, altmap);
			continue;
		}
		WARN_ON(!pmd_table(pmd));
		unmap_hotplug_pte_range(pmdp, addr, next, free_mapped, altmap);
	} while (addr = next, addr < end);
}

static void unmap_hotplug_pud_range(p4d_t *p4dp, unsigned long addr,
				    unsigned long end, bool free_mapped,
				    struct vmem_altmap *altmap)
{
	unsigned long next;
	pud_t *pudp, pud;

	do {
		next = pud_addr_end(addr, end);
		pudp = pud_offset(p4dp, addr);
		pud = READ_ONCE(*pudp);
		if (pud_none(pud))
			continue;

		WARN_ON(!pud_present(pud));
		if (pud_sect(pud)) {
			pud_clear(pudp);

			/*
			 * One TLBI should be sufficient here as the PUD_SIZE
			 * range is mapped with a single block entry.
			 */
			flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
			if (free_mapped)
				free_hotplug_page_range(pud_page(pud),
							PUD_SIZE, altmap);
			continue;
		}
		WARN_ON(!pud_table(pud));
		unmap_hotplug_pmd_range(pudp, addr, next, free_mapped, altmap);
	} while (addr = next, addr < end);
}

static void unmap_hotplug_p4d_range(pgd_t *pgdp, unsigned long addr,
				    unsigned long end, bool free_mapped,
				    struct vmem_altmap *altmap)
{
	unsigned long next;
	p4d_t *p4dp, p4d;

	do {
		next = p4d_addr_end(addr, end);
		p4dp = p4d_offset(pgdp, addr);
		p4d = READ_ONCE(*p4dp);
		if (p4d_none(p4d))
			continue;

		WARN_ON(!p4d_present(p4d));
		unmap_hotplug_pud_range(p4dp, addr, next, free_mapped, altmap);
	} while (addr = next, addr < end);
}

static void unmap_hotplug_range(unsigned long addr, unsigned long end,
				bool free_mapped, struct vmem_altmap *altmap)
{
	unsigned long next;
	pgd_t *pgdp, pgd;

	/*
	 * altmap can only be used as vmemmap mapping backing memory.
	 * In case the backing memory itself is not being freed, then
	 * altmap is irrelevant. Warn about this inconsistency when
	 * encountered.
	 */
	WARN_ON(!free_mapped && altmap);

	do {
		next = pgd_addr_end(addr, end);
		pgdp = pgd_offset_k(addr);
		pgd = READ_ONCE(*pgdp);
		if (pgd_none(pgd))
			continue;

		WARN_ON(!pgd_present(pgd));
		unmap_hotplug_p4d_range(pgdp, addr, next, free_mapped, altmap);
	} while (addr = next, addr < end);
}

static void free_empty_pte_table(pmd_t *pmdp, unsigned long addr,
				 unsigned long end, unsigned long floor,
				 unsigned long ceiling)
{
	pte_t *ptep, pte;
	unsigned long i, start = addr;

	do {
		ptep = pte_offset_kernel(pmdp, addr);
		pte = __ptep_get(ptep);

		/*
		 * This is just a sanity check here which verifies that
		 * pte clearing has been done by earlier unmap loops.
		 */
		WARN_ON(!pte_none(pte));
	} while (addr += PAGE_SIZE, addr < end);

	if (!pgtable_range_aligned(start, end, floor, ceiling, PMD_MASK))
		return;

	/*
	 * Check whether we can free the pte page if the rest of the
	 * entries are empty. Overlap with other regions have been
	 * handled by the floor/ceiling check.
	 */
	ptep = pte_offset_kernel(pmdp, 0UL);
	for (i = 0; i < PTRS_PER_PTE; i++) {
		if (!pte_none(__ptep_get(&ptep[i])))
			return;
	}

	pmd_clear(pmdp);
	__flush_tlb_kernel_pgtable(start);
	free_hotplug_pgtable_page(virt_to_page(ptep));
}

static void free_empty_pmd_table(pud_t *pudp, unsigned long addr,
				 unsigned long end, unsigned long floor,
				 unsigned long ceiling)
{
	pmd_t *pmdp, pmd;
	unsigned long i, next, start = addr;

	do {
		next = pmd_addr_end(addr, end);
		pmdp = pmd_offset(pudp, addr);
		pmd = READ_ONCE(*pmdp);
		if (pmd_none(pmd))
			continue;

		WARN_ON(!pmd_present(pmd) || !pmd_table(pmd) || pmd_sect(pmd));
		free_empty_pte_table(pmdp, addr, next, floor, ceiling);
	} while (addr = next, addr < end);

	if (CONFIG_PGTABLE_LEVELS <= 2)
		return;

	if (!pgtable_range_aligned(start, end, floor, ceiling, PUD_MASK))
		return;

	/*
	 * Check whether we can free the pmd page if the rest of the
	 * entries are empty. Overlap with other regions have been
	 * handled by the floor/ceiling check.
	 */
	pmdp = pmd_offset(pudp, 0UL);
	for (i = 0; i < PTRS_PER_PMD; i++) {
		if (!pmd_none(READ_ONCE(pmdp[i])))
			return;
	}

	pud_clear(pudp);
	__flush_tlb_kernel_pgtable(start);
	free_hotplug_pgtable_page(virt_to_page(pmdp));
}

static void free_empty_pud_table(p4d_t *p4dp, unsigned long addr,
				 unsigned long end, unsigned long floor,
				 unsigned long ceiling)
{
	pud_t *pudp, pud;
	unsigned long i, next, start = addr;

	do {
		next = pud_addr_end(addr, end);
		pudp = pud_offset(p4dp, addr);
		pud = READ_ONCE(*pudp);
		if (pud_none(pud))
			continue;

		WARN_ON(!pud_present(pud) || !pud_table(pud) || pud_sect(pud));
		free_empty_pmd_table(pudp, addr, next, floor, ceiling);
	} while (addr = next, addr < end);

	if (!pgtable_l4_enabled())
		return;

	if (!pgtable_range_aligned(start, end, floor, ceiling, P4D_MASK))
		return;

	/*
	 * Check whether we can free the pud page if the rest of the
	 * entries are empty. Overlap with other regions have been
	 * handled by the floor/ceiling check.
	 */
	pudp = pud_offset(p4dp, 0UL);
	for (i = 0; i < PTRS_PER_PUD; i++) {
		if (!pud_none(READ_ONCE(pudp[i])))
			return;
	}

	p4d_clear(p4dp);
	__flush_tlb_kernel_pgtable(start);
	free_hotplug_pgtable_page(virt_to_page(pudp));
}

static void free_empty_p4d_table(pgd_t *pgdp, unsigned long addr,
				 unsigned long end, unsigned long floor,
				 unsigned long ceiling)
{
	p4d_t *p4dp, p4d;
	unsigned long i, next, start = addr;

	do {
		next = p4d_addr_end(addr, end);
		p4dp = p4d_offset(pgdp, addr);
		p4d = READ_ONCE(*p4dp);
		if (p4d_none(p4d))
			continue;

		WARN_ON(!p4d_present(p4d));
		free_empty_pud_table(p4dp, addr, next, floor, ceiling);
	} while (addr = next, addr < end);

	if (!pgtable_l5_enabled())
		return;

	if (!pgtable_range_aligned(start, end, floor, ceiling, PGDIR_MASK))
		return;

	/*
	 * Check whether we can free the p4d page if the rest of the
	 * entries are empty. Overlap with other regions have been
	 * handled by the floor/ceiling check.
	 */
	p4dp = p4d_offset(pgdp, 0UL);
	for (i = 0; i < PTRS_PER_P4D; i++) {
		if (!p4d_none(READ_ONCE(p4dp[i])))
			return;
	}

	pgd_clear(pgdp);
	__flush_tlb_kernel_pgtable(start);
	free_hotplug_pgtable_page(virt_to_page(p4dp));
}

static void free_empty_tables(unsigned long addr, unsigned long end,
			      unsigned long floor, unsigned long ceiling)
{
	unsigned long next;
	pgd_t *pgdp, pgd;

	do {
		next = pgd_addr_end(addr, end);
		pgdp = pgd_offset_k(addr);
		pgd = READ_ONCE(*pgdp);
		if (pgd_none(pgd))
			continue;

		WARN_ON(!pgd_present(pgd));
		free_empty_p4d_table(pgdp, addr, next, floor, ceiling);
	} while (addr = next, addr < end);
}
#endif

void __meminit vmemmap_set_pmd(pmd_t *pmdp, void *p, int node,
			       unsigned long addr, unsigned long next)
{
	pmd_set_huge(pmdp, __pa(p), __pgprot(PROT_SECT_NORMAL));
}

int __meminit vmemmap_check_pmd(pmd_t *pmdp, int node,
				unsigned long addr, unsigned long next)
{
	vmemmap_verify((pte_t *)pmdp, node, addr, next);

	return pmd_sect(READ_ONCE(*pmdp));
}

int __meminit vmemmap_populate(unsigned long start, unsigned long end, int node,
		struct vmem_altmap *altmap)
{
	WARN_ON((start < VMEMMAP_START) || (end > VMEMMAP_END));
	/* [start, end] should be within one section */
	WARN_ON_ONCE(end - start > PAGES_PER_SECTION * sizeof(struct page));

	if (!IS_ENABLED(CONFIG_ARM64_4K_PAGES) ||
	    (end - start < PAGES_PER_SECTION * sizeof(struct page)))
		return vmemmap_populate_basepages(start, end, node, altmap);
	else
		return vmemmap_populate_hugepages(start, end, node, altmap);
}

#ifdef CONFIG_MEMORY_HOTPLUG
void vmemmap_free(unsigned long start, unsigned long end,
		struct vmem_altmap *altmap)
{
	WARN_ON((start < VMEMMAP_START) || (end > VMEMMAP_END));

	unmap_hotplug_range(start, end, true, altmap);
	free_empty_tables(start, end, VMEMMAP_START, VMEMMAP_END);
}
#endif /* CONFIG_MEMORY_HOTPLUG */

int pud_set_huge(pud_t *pudp, phys_addr_t phys, pgprot_t prot)
{
	pud_t new_pud = pfn_pud(__phys_to_pfn(phys), mk_pud_sect_prot(prot));

	/* Only allow permission changes for now */
	if (!pgattr_change_is_safe(READ_ONCE(pud_val(*pudp)),
				   pud_val(new_pud)))
		return 0;

	VM_BUG_ON(phys & ~PUD_MASK);
	set_pud(pudp, new_pud);
	return 1;
}

int pmd_set_huge(pmd_t *pmdp, phys_addr_t phys, pgprot_t prot)
{
	pmd_t new_pmd = pfn_pmd(__phys_to_pfn(phys), mk_pmd_sect_prot(prot));

	/* Only allow permission changes for now */
	if (!pgattr_change_is_safe(READ_ONCE(pmd_val(*pmdp)),
				   pmd_val(new_pmd)))
		return 0;

	VM_BUG_ON(phys & ~PMD_MASK);
	set_pmd(pmdp, new_pmd);
	return 1;
}

#ifndef __PAGETABLE_P4D_FOLDED
void p4d_clear_huge(p4d_t *p4dp)
{
}
#endif

int pud_clear_huge(pud_t *pudp)
{
	if (!pud_sect(READ_ONCE(*pudp)))
		return 0;
	pud_clear(pudp);
	return 1;
}

int pmd_clear_huge(pmd_t *pmdp)
{
	if (!pmd_sect(READ_ONCE(*pmdp)))
		return 0;
	pmd_clear(pmdp);
	return 1;
}

static int __pmd_free_pte_page(pmd_t *pmdp, unsigned long addr,
			       bool acquire_mmap_lock)
{
	pte_t *table;
	pmd_t pmd;

	pmd = READ_ONCE(*pmdp);

	if (!pmd_table(pmd)) {
		VM_WARN_ON(1);
		return 1;
	}

	/* See comment in pud_free_pmd_page for static key logic */
	table = pte_offset_kernel(pmdp, addr);
	pmd_clear(pmdp);
	__flush_tlb_kernel_pgtable(addr);
	if (static_branch_unlikely(&arm64_ptdump_lock_key) && acquire_mmap_lock) {
		mmap_read_lock(&init_mm);
		mmap_read_unlock(&init_mm);
	}

	pte_free_kernel(NULL, table);
	return 1;
}

int pmd_free_pte_page(pmd_t *pmdp, unsigned long addr)
{
	/* If ptdump is walking the pagetables, acquire init_mm.mmap_lock */
	return __pmd_free_pte_page(pmdp, addr, /* acquire_mmap_lock = */ true);
}

int pud_free_pmd_page(pud_t *pudp, unsigned long addr)
{
	pmd_t *table;
	pmd_t *pmdp;
	pud_t pud;
	unsigned long next, end;

	pud = READ_ONCE(*pudp);

	if (!pud_table(pud)) {
		VM_WARN_ON(1);
		return 1;
	}

	table = pmd_offset(pudp, addr);

	/*
	 * Our objective is to prevent ptdump from reading a PMD table which has
	 * been freed. In this race, if pud_free_pmd_page observes the key on
	 * (which got flipped by ptdump) then the mmap lock sequence here will,
	 * as a result of the mmap write lock/unlock sequence in ptdump, give
	 * us the correct synchronization. If not, this means that ptdump has
	 * yet not started walking the pagetables - the sequence of barriers
	 * issued by __flush_tlb_kernel_pgtable() guarantees that ptdump will
	 * observe an empty PUD.
	 */
	pud_clear(pudp);
	__flush_tlb_kernel_pgtable(addr);
	if (static_branch_unlikely(&arm64_ptdump_lock_key)) {
		mmap_read_lock(&init_mm);
		mmap_read_unlock(&init_mm);
	}

	pmdp = table;
	next = addr;
	end = addr + PUD_SIZE;
	do {
		if (pmd_present(pmdp_get(pmdp)))
			/*
			 * PMD has been isolated, so ptdump won't see it. No
			 * need to acquire init_mm.mmap_lock.
			 */
			__pmd_free_pte_page(pmdp, next, /* acquire_mmap_lock = */ false);
	} while (pmdp++, next += PMD_SIZE, next != end);

	pmd_free(NULL, table);
	return 1;
}

#ifdef CONFIG_MEMORY_HOTPLUG
static void __remove_pgd_mapping(pgd_t *pgdir, unsigned long start, u64 size)
{
	unsigned long end = start + size;

	WARN_ON(pgdir != init_mm.pgd);
	WARN_ON((start < PAGE_OFFSET) || (end > PAGE_END));

	unmap_hotplug_range(start, end, false, NULL);
	free_empty_tables(start, end, PAGE_OFFSET, PAGE_END);
}

struct range arch_get_mappable_range(void)
{
	struct range mhp_range;
	phys_addr_t start_linear_pa = __pa(_PAGE_OFFSET(vabits_actual));
	phys_addr_t end_linear_pa = __pa(PAGE_END - 1);

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE)) {
		/*
		 * Check for a wrap, it is possible because of randomized linear
		 * mapping the start physical address is actually bigger than
		 * the end physical address. In this case set start to zero
		 * because [0, end_linear_pa] range must still be able to cover
		 * all addressable physical addresses.
		 */
		if (start_linear_pa > end_linear_pa)
			start_linear_pa = 0;
	}

	WARN_ON(start_linear_pa > end_linear_pa);

	/*
	 * Linear mapping region is the range [PAGE_OFFSET..(PAGE_END - 1)]
	 * accommodating both its ends but excluding PAGE_END. Max physical
	 * range which can be mapped inside this linear mapping range, must
	 * also be derived from its end points.
	 */
	mhp_range.start = start_linear_pa;
	mhp_range.end =  end_linear_pa;

	return mhp_range;
}

int arch_add_memory(int nid, u64 start, u64 size,
		    struct mhp_params *params)
{
	int ret, flags = NO_EXEC_MAPPINGS;

	VM_BUG_ON(!mhp_range_allowed(start, size, true));

	if (force_pte_mapping())
		flags |= NO_BLOCK_MAPPINGS | NO_CONT_MAPPINGS;

	__create_pgd_mapping(swapper_pg_dir, start, __phys_to_virt(start),
			     size, params->pgprot, pgd_pgtable_alloc_init_mm,
			     flags);

	memblock_clear_nomap(start, size);

	ret = __add_pages(nid, start >> PAGE_SHIFT, size >> PAGE_SHIFT,
			   params);
	if (ret)
		__remove_pgd_mapping(swapper_pg_dir,
				     __phys_to_virt(start), size);
	else {
		/* Address of hotplugged memory can be smaller */
		max_pfn = max(max_pfn, PFN_UP(start + size));
		max_low_pfn = max_pfn;
	}

	return ret;
}

void arch_remove_memory(u64 start, u64 size, struct vmem_altmap *altmap)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;

	__remove_pages(start_pfn, nr_pages, altmap);
	__remove_pgd_mapping(swapper_pg_dir, __phys_to_virt(start), size);
}

/*
 * This memory hotplug notifier helps prevent boot memory from being
 * inadvertently removed as it blocks pfn range offlining process in
 * __offline_pages(). Hence this prevents both offlining as well as
 * removal process for boot memory which is initially always online.
 * In future if and when boot memory could be removed, this notifier
 * should be dropped and free_hotplug_page_range() should handle any
 * reserved pages allocated during boot.
 */
static int prevent_bootmem_remove_notifier(struct notifier_block *nb,
					   unsigned long action, void *data)
{
	struct mem_section *ms;
	struct memory_notify *arg = data;
	unsigned long end_pfn = arg->start_pfn + arg->nr_pages;
	unsigned long pfn = arg->start_pfn;

	if ((action != MEM_GOING_OFFLINE) && (action != MEM_OFFLINE))
		return NOTIFY_OK;

	for (; pfn < end_pfn; pfn += PAGES_PER_SECTION) {
		unsigned long start = PFN_PHYS(pfn);
		unsigned long end = start + (1UL << PA_SECTION_SHIFT);

		ms = __pfn_to_section(pfn);
		if (!early_section(ms))
			continue;

		if (action == MEM_GOING_OFFLINE) {
			/*
			 * Boot memory removal is not supported. Prevent
			 * it via blocking any attempted offline request
			 * for the boot memory and just report it.
			 */
			pr_warn("Boot memory [%lx %lx] offlining attempted\n", start, end);
			return NOTIFY_BAD;
		} else if (action == MEM_OFFLINE) {
			/*
			 * This should have never happened. Boot memory
			 * offlining should have been prevented by this
			 * very notifier. Probably some memory removal
			 * procedure might have changed which would then
			 * require further debug.
			 */
			pr_err("Boot memory [%lx %lx] offlined\n", start, end);

			/*
			 * Core memory hotplug does not process a return
			 * code from the notifier for MEM_OFFLINE events.
			 * The error condition has been reported. Return
			 * from here as if ignored.
			 */
			return NOTIFY_DONE;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block prevent_bootmem_remove_nb = {
	.notifier_call = prevent_bootmem_remove_notifier,
};

/*
 * This ensures that boot memory sections on the platform are online
 * from early boot. Memory sections could not be prevented from being
 * offlined, unless for some reason they are not online to begin with.
 * This helps validate the basic assumption on which the above memory
 * event notifier works to prevent boot memory section offlining and
 * its possible removal.
 */
static void validate_bootmem_online(void)
{
	phys_addr_t start, end, addr;
	struct mem_section *ms;
	u64 i;

	/*
	 * Scanning across all memblock might be expensive
	 * on some big memory systems. Hence enable this
	 * validation only with DEBUG_VM.
	 */
	if (!IS_ENABLED(CONFIG_DEBUG_VM))
		return;

	for_each_mem_range(i, &start, &end) {
		for (addr = start; addr < end; addr += (1UL << PA_SECTION_SHIFT)) {
			ms = __pfn_to_section(PHYS_PFN(addr));

			/*
			 * All memory ranges in the system at this point
			 * should have been marked as early sections.
			 */
			WARN_ON(!early_section(ms));

			/*
			 * Memory notifier mechanism here to prevent boot
			 * memory offlining depends on the fact that each
			 * early section memory on the system is initially
			 * online. Otherwise a given memory section which
			 * is already offline will be overlooked and can
			 * be removed completely. Call out such sections.
			 */
			if (!online_section(ms))
				pr_err("Boot memory [%llx %llx] is offline, can be removed\n",
					addr, addr + (1UL << PA_SECTION_SHIFT));
		}
	}
}

static int __init prevent_bootmem_remove_init(void)
{
	int ret = 0;

	if (!IS_ENABLED(CONFIG_MEMORY_HOTREMOVE))
		return ret;

	validate_bootmem_online();
	ret = register_memory_notifier(&prevent_bootmem_remove_nb);
	if (ret)
		pr_err("%s: Notifier registration failed %d\n", __func__, ret);

	return ret;
}
early_initcall(prevent_bootmem_remove_init);
#endif

pte_t modify_prot_start_ptes(struct vm_area_struct *vma, unsigned long addr,
			     pte_t *ptep, unsigned int nr)
{
	pte_t pte = get_and_clear_ptes(vma->vm_mm, addr, ptep, nr);

	if (alternative_has_cap_unlikely(ARM64_WORKAROUND_2645198)) {
		/*
		 * Break-before-make (BBM) is required for all user space mappings
		 * when the permission changes from executable to non-executable
		 * in cases where cpu is affected with errata #2645198.
		 */
		if (pte_accessible(vma->vm_mm, pte) && pte_user_exec(pte))
			__flush_tlb_range(vma, addr, nr * PAGE_SIZE,
					  PAGE_SIZE, true, 3);
	}

	return pte;
}

pte_t ptep_modify_prot_start(struct vm_area_struct *vma, unsigned long addr, pte_t *ptep)
{
	return modify_prot_start_ptes(vma, addr, ptep, 1);
}

void modify_prot_commit_ptes(struct vm_area_struct *vma, unsigned long addr,
			     pte_t *ptep, pte_t old_pte, pte_t pte,
			     unsigned int nr)
{
	set_ptes(vma->vm_mm, addr, ptep, pte, nr);
}

void ptep_modify_prot_commit(struct vm_area_struct *vma, unsigned long addr, pte_t *ptep,
			     pte_t old_pte, pte_t pte)
{
	modify_prot_commit_ptes(vma, addr, ptep, old_pte, pte, 1);
}

/*
 * Atomically replaces the active TTBR1_EL1 PGD with a new VA-compatible PGD,
 * avoiding the possibility of conflicting TLB entries being allocated.
 */
void __cpu_replace_ttbr1(pgd_t *pgdp, bool cnp)
{
	typedef void (ttbr_replace_func)(phys_addr_t);
	extern ttbr_replace_func idmap_cpu_replace_ttbr1;
	ttbr_replace_func *replace_phys;
	unsigned long daif;

	/* phys_to_ttbr() zeros lower 2 bits of ttbr with 52-bit PA */
	phys_addr_t ttbr1 = phys_to_ttbr(virt_to_phys(pgdp));

	if (cnp)
		ttbr1 |= TTBR_CNP_BIT;

	replace_phys = (void *)__pa_symbol(idmap_cpu_replace_ttbr1);

	cpu_install_idmap();

	/*
	 * We really don't want to take *any* exceptions while TTBR1 is
	 * in the process of being replaced so mask everything.
	 */
	daif = local_daif_save();
	replace_phys(ttbr1);
	local_daif_restore(daif);

	cpu_uninstall_idmap();
}

#ifdef CONFIG_ARCH_HAS_PKEYS
int arch_set_user_pkey_access(struct task_struct *tsk, int pkey, unsigned long init_val)
{
	u64 new_por;
	u64 old_por;

	if (!system_supports_poe())
		return -ENOSPC;

	/*
	 * This code should only be called with valid 'pkey'
	 * values originating from in-kernel users.  Complain
	 * if a bad value is observed.
	 */
	if (WARN_ON_ONCE(pkey >= arch_max_pkey()))
		return -EINVAL;

	/* Set the bits we need in POR:  */
	new_por = POE_RWX;
	if (init_val & PKEY_DISABLE_WRITE)
		new_por &= ~POE_W;
	if (init_val & PKEY_DISABLE_ACCESS)
		new_por &= ~POE_RW;
	if (init_val & PKEY_DISABLE_READ)
		new_por &= ~POE_R;
	if (init_val & PKEY_DISABLE_EXECUTE)
		new_por &= ~POE_X;

	/* Shift the bits in to the correct place in POR for pkey: */
	new_por = POR_ELx_PERM_PREP(pkey, new_por);

	/* Get old POR and mask off any old bits in place: */
	old_por = read_sysreg_s(SYS_POR_EL0);
	old_por &= ~(POE_MASK << POR_ELx_PERM_SHIFT(pkey));

	/* Write old part along with new part: */
	write_sysreg_s(old_por | new_por, SYS_POR_EL0);

	return 0;
}
#endif
