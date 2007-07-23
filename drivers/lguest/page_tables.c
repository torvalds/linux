/* Shadow page table operations.
 * Copyright (C) Rusty Russell IBM Corporation 2006.
 * GPL v2 and any later version */
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/percpu.h>
#include <asm/tlbflush.h>
#include "lg.h"

#define PTES_PER_PAGE_SHIFT 10
#define PTES_PER_PAGE (1 << PTES_PER_PAGE_SHIFT)
#define SWITCHER_PGD_INDEX (PTES_PER_PAGE - 1)

static DEFINE_PER_CPU(spte_t *, switcher_pte_pages);
#define switcher_pte_page(cpu) per_cpu(switcher_pte_pages, cpu)

static unsigned vaddr_to_pgd_index(unsigned long vaddr)
{
	return vaddr >> (PAGE_SHIFT + PTES_PER_PAGE_SHIFT);
}

/* These access the shadow versions (ie. the ones used by the CPU). */
static spgd_t *spgd_addr(struct lguest *lg, u32 i, unsigned long vaddr)
{
	unsigned int index = vaddr_to_pgd_index(vaddr);

	if (index >= SWITCHER_PGD_INDEX) {
		kill_guest(lg, "attempt to access switcher pages");
		index = 0;
	}
	return &lg->pgdirs[i].pgdir[index];
}

static spte_t *spte_addr(struct lguest *lg, spgd_t spgd, unsigned long vaddr)
{
	spte_t *page = __va(spgd.pfn << PAGE_SHIFT);
	BUG_ON(!(spgd.flags & _PAGE_PRESENT));
	return &page[(vaddr >> PAGE_SHIFT) % PTES_PER_PAGE];
}

/* These access the guest versions. */
static unsigned long gpgd_addr(struct lguest *lg, unsigned long vaddr)
{
	unsigned int index = vaddr >> (PAGE_SHIFT + PTES_PER_PAGE_SHIFT);
	return lg->pgdirs[lg->pgdidx].cr3 + index * sizeof(gpgd_t);
}

static unsigned long gpte_addr(struct lguest *lg,
			       gpgd_t gpgd, unsigned long vaddr)
{
	unsigned long gpage = gpgd.pfn << PAGE_SHIFT;
	BUG_ON(!(gpgd.flags & _PAGE_PRESENT));
	return gpage + ((vaddr>>PAGE_SHIFT) % PTES_PER_PAGE) * sizeof(gpte_t);
}

/* Do a virtual -> physical mapping on a user page. */
static unsigned long get_pfn(unsigned long virtpfn, int write)
{
	struct page *page;
	unsigned long ret = -1UL;

	down_read(&current->mm->mmap_sem);
	if (get_user_pages(current, current->mm, virtpfn << PAGE_SHIFT,
			   1, write, 1, &page, NULL) == 1)
		ret = page_to_pfn(page);
	up_read(&current->mm->mmap_sem);
	return ret;
}

static spte_t gpte_to_spte(struct lguest *lg, gpte_t gpte, int write)
{
	spte_t spte;
	unsigned long pfn;

	/* We ignore the global flag. */
	spte.flags = (gpte.flags & ~_PAGE_GLOBAL);
	pfn = get_pfn(gpte.pfn, write);
	if (pfn == -1UL) {
		kill_guest(lg, "failed to get page %u", gpte.pfn);
		/* Must not put_page() bogus page on cleanup. */
		spte.flags = 0;
	}
	spte.pfn = pfn;
	return spte;
}

static void release_pte(spte_t pte)
{
	if (pte.flags & _PAGE_PRESENT)
		put_page(pfn_to_page(pte.pfn));
}

static void check_gpte(struct lguest *lg, gpte_t gpte)
{
	if ((gpte.flags & (_PAGE_PWT|_PAGE_PSE)) || gpte.pfn >= lg->pfn_limit)
		kill_guest(lg, "bad page table entry");
}

static void check_gpgd(struct lguest *lg, gpgd_t gpgd)
{
	if ((gpgd.flags & ~_PAGE_TABLE) || gpgd.pfn >= lg->pfn_limit)
		kill_guest(lg, "bad page directory entry");
}

/* FIXME: We hold reference to pages, which prevents them from being
   swapped.  It'd be nice to have a callback when Linux wants to swap out. */

/* We fault pages in, which allows us to update accessed/dirty bits.
 * Return true if we got page. */
int demand_page(struct lguest *lg, unsigned long vaddr, int errcode)
{
	gpgd_t gpgd;
	spgd_t *spgd;
	unsigned long gpte_ptr;
	gpte_t gpte;
	spte_t *spte;

	gpgd = mkgpgd(lgread_u32(lg, gpgd_addr(lg, vaddr)));
	if (!(gpgd.flags & _PAGE_PRESENT))
		return 0;

	spgd = spgd_addr(lg, lg->pgdidx, vaddr);
	if (!(spgd->flags & _PAGE_PRESENT)) {
		/* Get a page of PTEs for them. */
		unsigned long ptepage = get_zeroed_page(GFP_KERNEL);
		/* FIXME: Steal from self in this case? */
		if (!ptepage) {
			kill_guest(lg, "out of memory allocating pte page");
			return 0;
		}
		check_gpgd(lg, gpgd);
		spgd->raw.val = (__pa(ptepage) | gpgd.flags);
	}

	gpte_ptr = gpte_addr(lg, gpgd, vaddr);
	gpte = mkgpte(lgread_u32(lg, gpte_ptr));

	/* No page? */
	if (!(gpte.flags & _PAGE_PRESENT))
		return 0;

	/* Write to read-only page? */
	if ((errcode & 2) && !(gpte.flags & _PAGE_RW))
		return 0;

	/* User access to a non-user page? */
	if ((errcode & 4) && !(gpte.flags & _PAGE_USER))
		return 0;

	check_gpte(lg, gpte);
	gpte.flags |= _PAGE_ACCESSED;
	if (errcode & 2)
		gpte.flags |= _PAGE_DIRTY;

	/* We're done with the old pte. */
	spte = spte_addr(lg, *spgd, vaddr);
	release_pte(*spte);

	/* We don't make it writable if this isn't a write: later
	 * write will fault so we can set dirty bit in guest. */
	if (gpte.flags & _PAGE_DIRTY)
		*spte = gpte_to_spte(lg, gpte, 1);
	else {
		gpte_t ro_gpte = gpte;
		ro_gpte.flags &= ~_PAGE_RW;
		*spte = gpte_to_spte(lg, ro_gpte, 0);
	}

	/* Now we update dirty/accessed on guest. */
	lgwrite_u32(lg, gpte_ptr, gpte.raw.val);
	return 1;
}

/* This is much faster than the full demand_page logic. */
static int page_writable(struct lguest *lg, unsigned long vaddr)
{
	spgd_t *spgd;
	unsigned long flags;

	spgd = spgd_addr(lg, lg->pgdidx, vaddr);
	if (!(spgd->flags & _PAGE_PRESENT))
		return 0;

	flags = spte_addr(lg, *spgd, vaddr)->flags;
	return (flags & (_PAGE_PRESENT|_PAGE_RW)) == (_PAGE_PRESENT|_PAGE_RW);
}

void pin_page(struct lguest *lg, unsigned long vaddr)
{
	if (!page_writable(lg, vaddr) && !demand_page(lg, vaddr, 2))
		kill_guest(lg, "bad stack page %#lx", vaddr);
}

static void release_pgd(struct lguest *lg, spgd_t *spgd)
{
	if (spgd->flags & _PAGE_PRESENT) {
		unsigned int i;
		spte_t *ptepage = __va(spgd->pfn << PAGE_SHIFT);
		for (i = 0; i < PTES_PER_PAGE; i++)
			release_pte(ptepage[i]);
		free_page((long)ptepage);
		spgd->raw.val = 0;
	}
}

static void flush_user_mappings(struct lguest *lg, int idx)
{
	unsigned int i;
	for (i = 0; i < vaddr_to_pgd_index(lg->page_offset); i++)
		release_pgd(lg, lg->pgdirs[idx].pgdir + i);
}

void guest_pagetable_flush_user(struct lguest *lg)
{
	flush_user_mappings(lg, lg->pgdidx);
}

static unsigned int find_pgdir(struct lguest *lg, unsigned long pgtable)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(lg->pgdirs); i++)
		if (lg->pgdirs[i].cr3 == pgtable)
			break;
	return i;
}

static unsigned int new_pgdir(struct lguest *lg,
			      unsigned long cr3,
			      int *blank_pgdir)
{
	unsigned int next;

	next = random32() % ARRAY_SIZE(lg->pgdirs);
	if (!lg->pgdirs[next].pgdir) {
		lg->pgdirs[next].pgdir = (spgd_t *)get_zeroed_page(GFP_KERNEL);
		if (!lg->pgdirs[next].pgdir)
			next = lg->pgdidx;
		else
			/* There are no mappings: you'll need to re-pin */
			*blank_pgdir = 1;
	}
	lg->pgdirs[next].cr3 = cr3;
	/* Release all the non-kernel mappings. */
	flush_user_mappings(lg, next);

	return next;
}

void guest_new_pagetable(struct lguest *lg, unsigned long pgtable)
{
	int newpgdir, repin = 0;

	newpgdir = find_pgdir(lg, pgtable);
	if (newpgdir == ARRAY_SIZE(lg->pgdirs))
		newpgdir = new_pgdir(lg, pgtable, &repin);
	lg->pgdidx = newpgdir;
	if (repin)
		pin_stack_pages(lg);
}

static void release_all_pagetables(struct lguest *lg)
{
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(lg->pgdirs); i++)
		if (lg->pgdirs[i].pgdir)
			for (j = 0; j < SWITCHER_PGD_INDEX; j++)
				release_pgd(lg, lg->pgdirs[i].pgdir + j);
}

void guest_pagetable_clear_all(struct lguest *lg)
{
	release_all_pagetables(lg);
	pin_stack_pages(lg);
}

static void do_set_pte(struct lguest *lg, int idx,
		       unsigned long vaddr, gpte_t gpte)
{
	spgd_t *spgd = spgd_addr(lg, idx, vaddr);
	if (spgd->flags & _PAGE_PRESENT) {
		spte_t *spte = spte_addr(lg, *spgd, vaddr);
		release_pte(*spte);
		if (gpte.flags & (_PAGE_DIRTY | _PAGE_ACCESSED)) {
			check_gpte(lg, gpte);
			*spte = gpte_to_spte(lg, gpte, gpte.flags&_PAGE_DIRTY);
		} else
			spte->raw.val = 0;
	}
}

void guest_set_pte(struct lguest *lg,
		   unsigned long cr3, unsigned long vaddr, gpte_t gpte)
{
	/* Kernel mappings must be changed on all top levels. */
	if (vaddr >= lg->page_offset) {
		unsigned int i;
		for (i = 0; i < ARRAY_SIZE(lg->pgdirs); i++)
			if (lg->pgdirs[i].pgdir)
				do_set_pte(lg, i, vaddr, gpte);
	} else {
		int pgdir = find_pgdir(lg, cr3);
		if (pgdir != ARRAY_SIZE(lg->pgdirs))
			do_set_pte(lg, pgdir, vaddr, gpte);
	}
}

void guest_set_pmd(struct lguest *lg, unsigned long cr3, u32 idx)
{
	int pgdir;

	if (idx >= SWITCHER_PGD_INDEX)
		return;

	pgdir = find_pgdir(lg, cr3);
	if (pgdir < ARRAY_SIZE(lg->pgdirs))
		release_pgd(lg, lg->pgdirs[pgdir].pgdir + idx);
}

int init_guest_pagetable(struct lguest *lg, unsigned long pgtable)
{
	/* We assume this in flush_user_mappings, so check now */
	if (vaddr_to_pgd_index(lg->page_offset) >= SWITCHER_PGD_INDEX)
		return -EINVAL;
	lg->pgdidx = 0;
	lg->pgdirs[lg->pgdidx].cr3 = pgtable;
	lg->pgdirs[lg->pgdidx].pgdir = (spgd_t*)get_zeroed_page(GFP_KERNEL);
	if (!lg->pgdirs[lg->pgdidx].pgdir)
		return -ENOMEM;
	return 0;
}

void free_guest_pagetable(struct lguest *lg)
{
	unsigned int i;

	release_all_pagetables(lg);
	for (i = 0; i < ARRAY_SIZE(lg->pgdirs); i++)
		free_page((long)lg->pgdirs[i].pgdir);
}

/* Caller must be preempt-safe */
void map_switcher_in_guest(struct lguest *lg, struct lguest_pages *pages)
{
	spte_t *switcher_pte_page = __get_cpu_var(switcher_pte_pages);
	spgd_t switcher_pgd;
	spte_t regs_pte;

	/* Since switcher less that 4MB, we simply mug top pte page. */
	switcher_pgd.pfn = __pa(switcher_pte_page) >> PAGE_SHIFT;
	switcher_pgd.flags = _PAGE_KERNEL;
	lg->pgdirs[lg->pgdidx].pgdir[SWITCHER_PGD_INDEX] = switcher_pgd;

	/* Map our regs page over stack page. */
	regs_pte.pfn = __pa(lg->regs_page) >> PAGE_SHIFT;
	regs_pte.flags = _PAGE_KERNEL;
	switcher_pte_page[(unsigned long)pages/PAGE_SIZE%PTES_PER_PAGE]
		= regs_pte;
}

static void free_switcher_pte_pages(void)
{
	unsigned int i;

	for_each_possible_cpu(i)
		free_page((long)switcher_pte_page(i));
}

static __init void populate_switcher_pte_page(unsigned int cpu,
					      struct page *switcher_page[],
					      unsigned int pages)
{
	unsigned int i;
	spte_t *pte = switcher_pte_page(cpu);

	for (i = 0; i < pages; i++) {
		pte[i].pfn = page_to_pfn(switcher_page[i]);
		pte[i].flags = _PAGE_PRESENT|_PAGE_ACCESSED;
	}

	/* We only map this CPU's pages, so guest can't see others. */
	i = pages + cpu*2;

	/* First page (regs) is rw, second (state) is ro. */
	pte[i].pfn = page_to_pfn(switcher_page[i]);
	pte[i].flags = _PAGE_PRESENT|_PAGE_ACCESSED|_PAGE_RW;
	pte[i+1].pfn = page_to_pfn(switcher_page[i+1]);
	pte[i+1].flags = _PAGE_PRESENT|_PAGE_ACCESSED;
}

__init int init_pagetables(struct page **switcher_page, unsigned int pages)
{
	unsigned int i;

	for_each_possible_cpu(i) {
		switcher_pte_page(i) = (spte_t *)get_zeroed_page(GFP_KERNEL);
		if (!switcher_pte_page(i)) {
			free_switcher_pte_pages();
			return -ENOMEM;
		}
		populate_switcher_pte_page(i, switcher_page, pages);
	}
	return 0;
}

void free_pagetables(void)
{
	free_switcher_pte_pages();
}
