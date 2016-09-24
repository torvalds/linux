/*
 * Copyright IBM Corp. 2011
 * Author(s): Jan Glauber <jang@linux.vnet.ibm.com>
 */
#include <linux/hugetlb.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/facility.h>
#include <asm/pgtable.h>
#include <asm/page.h>

static inline unsigned long sske_frame(unsigned long addr, unsigned char skey)
{
	asm volatile(".insn rrf,0xb22b0000,%[skey],%[addr],9,0"
		     : [addr] "+a" (addr) : [skey] "d" (skey));
	return addr;
}

void __storage_key_init_range(unsigned long start, unsigned long end)
{
	unsigned long boundary, size;

	if (!PAGE_DEFAULT_KEY)
		return;
	while (start < end) {
		if (MACHINE_HAS_EDAT1) {
			/* set storage keys for a 1MB frame */
			size = 1UL << 20;
			boundary = (start + size) & ~(size - 1);
			if (boundary <= end) {
				do {
					start = sske_frame(start, PAGE_DEFAULT_KEY);
				} while (start < boundary);
				continue;
			}
		}
		page_set_storage_key(start, PAGE_DEFAULT_KEY, 0);
		start += PAGE_SIZE;
	}
}

#ifdef CONFIG_PROC_FS
atomic_long_t direct_pages_count[PG_DIRECT_MAP_MAX];

void arch_report_meminfo(struct seq_file *m)
{
	seq_printf(m, "DirectMap4k:    %8lu kB\n",
		   atomic_long_read(&direct_pages_count[PG_DIRECT_MAP_4K]) << 2);
	seq_printf(m, "DirectMap1M:    %8lu kB\n",
		   atomic_long_read(&direct_pages_count[PG_DIRECT_MAP_1M]) << 10);
	seq_printf(m, "DirectMap2G:    %8lu kB\n",
		   atomic_long_read(&direct_pages_count[PG_DIRECT_MAP_2G]) << 21);
}
#endif /* CONFIG_PROC_FS */

static void pgt_set(unsigned long *old, unsigned long new, unsigned long addr,
		    unsigned long dtt)
{
	unsigned long table, mask;

	mask = 0;
	if (MACHINE_HAS_EDAT2) {
		switch (dtt) {
		case CRDTE_DTT_REGION3:
			mask = ~(PTRS_PER_PUD * sizeof(pud_t) - 1);
			break;
		case CRDTE_DTT_SEGMENT:
			mask = ~(PTRS_PER_PMD * sizeof(pmd_t) - 1);
			break;
		case CRDTE_DTT_PAGE:
			mask = ~(PTRS_PER_PTE * sizeof(pte_t) - 1);
			break;
		}
		table = (unsigned long)old & mask;
		crdte(*old, new, table, dtt, addr, S390_lowcore.kernel_asce);
	} else if (MACHINE_HAS_IDTE) {
		cspg(old, *old, new);
	} else {
		csp((unsigned int *)old + 1, *old, new);
	}
}

struct cpa {
	unsigned int set_ro	: 1;
	unsigned int clear_ro	: 1;
};

static int walk_pte_level(pmd_t *pmdp, unsigned long addr, unsigned long end,
			  struct cpa cpa)
{
	pte_t *ptep, new;

	ptep = pte_offset(pmdp, addr);
	do {
		if (pte_none(*ptep))
			return -EINVAL;
		if (cpa.set_ro)
			new = pte_wrprotect(*ptep);
		else if (cpa.clear_ro)
			new = pte_mkwrite(pte_mkdirty(*ptep));
		pgt_set((unsigned long *)ptep, pte_val(new), addr, CRDTE_DTT_PAGE);
		ptep++;
		addr += PAGE_SIZE;
		cond_resched();
	} while (addr < end);
	return 0;
}

static int split_pmd_page(pmd_t *pmdp, unsigned long addr)
{
	unsigned long pte_addr, prot;
	pte_t *pt_dir, *ptep;
	pmd_t new;
	int i, ro;

	pt_dir = vmem_pte_alloc();
	if (!pt_dir)
		return -ENOMEM;
	pte_addr = pmd_pfn(*pmdp) << PAGE_SHIFT;
	ro = !!(pmd_val(*pmdp) & _SEGMENT_ENTRY_PROTECT);
	prot = pgprot_val(ro ? PAGE_KERNEL_RO : PAGE_KERNEL);
	ptep = pt_dir;
	for (i = 0; i < PTRS_PER_PTE; i++) {
		pte_val(*ptep) = pte_addr | prot;
		pte_addr += PAGE_SIZE;
		ptep++;
	}
	pmd_val(new) = __pa(pt_dir) | _SEGMENT_ENTRY;
	pgt_set((unsigned long *)pmdp, pmd_val(new), addr, CRDTE_DTT_SEGMENT);
	update_page_count(PG_DIRECT_MAP_4K, PTRS_PER_PTE);
	update_page_count(PG_DIRECT_MAP_1M, -1);
	return 0;
}

static void modify_pmd_page(pmd_t *pmdp, unsigned long addr, struct cpa cpa)
{
	pmd_t new;

	if (cpa.set_ro)
		new = pmd_wrprotect(*pmdp);
	else if (cpa.clear_ro)
		new = pmd_mkwrite(pmd_mkdirty(*pmdp));
	pgt_set((unsigned long *)pmdp, pmd_val(new), addr, CRDTE_DTT_SEGMENT);
}

static int walk_pmd_level(pud_t *pudp, unsigned long addr, unsigned long end,
			  struct cpa cpa)
{
	unsigned long next;
	pmd_t *pmdp;
	int rc = 0;

	pmdp = pmd_offset(pudp, addr);
	do {
		if (pmd_none(*pmdp))
			return -EINVAL;
		next = pmd_addr_end(addr, end);
		if (pmd_large(*pmdp)) {
			if (addr & ~PMD_MASK || addr + PMD_SIZE > next) {
				rc = split_pmd_page(pmdp, addr);
				if (rc)
					return rc;
				continue;
			}
			modify_pmd_page(pmdp, addr, cpa);
		} else {
			rc = walk_pte_level(pmdp, addr, next, cpa);
			if (rc)
				return rc;
		}
		pmdp++;
		addr = next;
		cond_resched();
	} while (addr < end);
	return rc;
}

static int split_pud_page(pud_t *pudp, unsigned long addr)
{
	unsigned long pmd_addr, prot;
	pmd_t *pm_dir, *pmdp;
	pud_t new;
	int i, ro;

	pm_dir = vmem_pmd_alloc();
	if (!pm_dir)
		return -ENOMEM;
	pmd_addr = pud_pfn(*pudp) << PAGE_SHIFT;
	ro = !!(pud_val(*pudp) & _REGION_ENTRY_PROTECT);
	prot = pgprot_val(ro ? SEGMENT_KERNEL_RO : SEGMENT_KERNEL);
	pmdp = pm_dir;
	for (i = 0; i < PTRS_PER_PMD; i++) {
		pmd_val(*pmdp) = pmd_addr | prot;
		pmd_addr += PMD_SIZE;
		pmdp++;
	}
	pud_val(new) = __pa(pm_dir) | _REGION3_ENTRY;
	pgt_set((unsigned long *)pudp, pud_val(new), addr, CRDTE_DTT_REGION3);
	update_page_count(PG_DIRECT_MAP_1M, PTRS_PER_PMD);
	update_page_count(PG_DIRECT_MAP_2G, -1);
	return 0;
}

static void modify_pud_page(pud_t *pudp, unsigned long addr, struct cpa cpa)
{
	pud_t new;

	if (cpa.set_ro)
		new = pud_wrprotect(*pudp);
	else if (cpa.clear_ro)
		new = pud_mkwrite(pud_mkdirty(*pudp));
	pgt_set((unsigned long *)pudp, pud_val(new), addr, CRDTE_DTT_REGION3);
}

static int walk_pud_level(pgd_t *pgd, unsigned long addr, unsigned long end,
			  struct cpa cpa)
{
	unsigned long next;
	pud_t *pudp;
	int rc = 0;

	pudp = pud_offset(pgd, addr);
	do {
		if (pud_none(*pudp))
			return -EINVAL;
		next = pud_addr_end(addr, end);
		if (pud_large(*pudp)) {
			if (addr & ~PUD_MASK || addr + PUD_SIZE > next) {
				rc = split_pud_page(pudp, addr);
				if (rc)
					break;
				continue;
			}
			modify_pud_page(pudp, addr, cpa);
		} else {
			rc = walk_pmd_level(pudp, addr, next, cpa);
		}
		pudp++;
		addr = next;
		cond_resched();
	} while (addr < end && !rc);
	return rc;
}

static DEFINE_MUTEX(cpa_mutex);

static int change_page_attr(unsigned long addr, unsigned long end,
			    struct cpa cpa)
{
	unsigned long next;
	int rc = -EINVAL;
	pgd_t *pgdp;

	if (addr == end)
		return 0;
	if (end >= MODULES_END)
		return -EINVAL;
	mutex_lock(&cpa_mutex);
	pgdp = pgd_offset_k(addr);
	do {
		if (pgd_none(*pgdp))
			break;
		next = pgd_addr_end(addr, end);
		rc = walk_pud_level(pgdp, addr, next, cpa);
		if (rc)
			break;
		cond_resched();
	} while (pgdp++, addr = next, addr < end && !rc);
	mutex_unlock(&cpa_mutex);
	return rc;
}

int set_memory_ro(unsigned long addr, int numpages)
{
	struct cpa cpa = {
		.set_ro = 1,
	};

	addr &= PAGE_MASK;
	return change_page_attr(addr, addr + numpages * PAGE_SIZE, cpa);
}

int set_memory_rw(unsigned long addr, int numpages)
{
	struct cpa cpa = {
		.clear_ro = 1,
	};

	addr &= PAGE_MASK;
	return change_page_attr(addr, addr + numpages * PAGE_SIZE, cpa);
}

/* not possible */
int set_memory_nx(unsigned long addr, int numpages)
{
	return 0;
}

int set_memory_x(unsigned long addr, int numpages)
{
	return 0;
}

#ifdef CONFIG_DEBUG_PAGEALLOC

static void ipte_range(pte_t *pte, unsigned long address, int nr)
{
	int i;

	if (test_facility(13)) {
		__ptep_ipte_range(address, nr - 1, pte);
		return;
	}
	for (i = 0; i < nr; i++) {
		__ptep_ipte(address, pte);
		address += PAGE_SIZE;
		pte++;
	}
}

void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	unsigned long address;
	int nr, i, j;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	for (i = 0; i < numpages;) {
		address = page_to_phys(page + i);
		pgd = pgd_offset_k(address);
		pud = pud_offset(pgd, address);
		pmd = pmd_offset(pud, address);
		pte = pte_offset_kernel(pmd, address);
		nr = (unsigned long)pte >> ilog2(sizeof(long));
		nr = PTRS_PER_PTE - (nr & (PTRS_PER_PTE - 1));
		nr = min(numpages - i, nr);
		if (enable) {
			for (j = 0; j < nr; j++) {
				pte_val(*pte) = address | pgprot_val(PAGE_KERNEL);
				address += PAGE_SIZE;
				pte++;
			}
		} else {
			ipte_range(pte, address, nr);
		}
		i += nr;
	}
}

#ifdef CONFIG_HIBERNATION
bool kernel_page_present(struct page *page)
{
	unsigned long addr;
	int cc;

	addr = page_to_phys(page);
	asm volatile(
		"	lra	%1,0(%1)\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (cc), "+a" (addr) : : "cc");
	return cc == 0;
}
#endif /* CONFIG_HIBERNATION */

#endif /* CONFIG_DEBUG_PAGEALLOC */
