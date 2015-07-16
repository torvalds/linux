/*P:700
 * The pagetable code, on the other hand, still shows the scars of
 * previous encounters.  It's functional, and as neat as it can be in the
 * circumstances, but be wary, for these things are subtle and break easily.
 * The Guest provides a virtual to physical mapping, but we can neither trust
 * it nor use it: we verify and convert it here then point the CPU to the
 * converted Guest pages when running the Guest.
:*/

/* Copyright (C) Rusty Russell IBM Corporation 2013.
 * GPL v2 and any later version */
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/percpu.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include "lg.h"

/*M:008
 * We hold reference to pages, which prevents them from being swapped.
 * It'd be nice to have a callback in the "struct mm_struct" when Linux wants
 * to swap out.  If we had this, and a shrinker callback to trim PTE pages, we
 * could probably consider launching Guests as non-root.
:*/

/*H:300
 * The Page Table Code
 *
 * We use two-level page tables for the Guest, or three-level with PAE.  If
 * you're not entirely comfortable with virtual addresses, physical addresses
 * and page tables then I recommend you review arch/x86/lguest/boot.c's "Page
 * Table Handling" (with diagrams!).
 *
 * The Guest keeps page tables, but we maintain the actual ones here: these are
 * called "shadow" page tables.  Which is a very Guest-centric name: these are
 * the real page tables the CPU uses, although we keep them up to date to
 * reflect the Guest's.  (See what I mean about weird naming?  Since when do
 * shadows reflect anything?)
 *
 * Anyway, this is the most complicated part of the Host code.  There are seven
 * parts to this:
 *  (i) Looking up a page table entry when the Guest faults,
 *  (ii) Making sure the Guest stack is mapped,
 *  (iii) Setting up a page table entry when the Guest tells us one has changed,
 *  (iv) Switching page tables,
 *  (v) Flushing (throwing away) page tables,
 *  (vi) Mapping the Switcher when the Guest is about to run,
 *  (vii) Setting up the page tables initially.
:*/

/*
 * The Switcher uses the complete top PTE page.  That's 1024 PTE entries (4MB)
 * or 512 PTE entries with PAE (2MB).
 */
#define SWITCHER_PGD_INDEX (PTRS_PER_PGD - 1)

/*
 * For PAE we need the PMD index as well. We use the last 2MB, so we
 * will need the last pmd entry of the last pmd page.
 */
#ifdef CONFIG_X86_PAE
#define CHECK_GPGD_MASK		_PAGE_PRESENT
#else
#define CHECK_GPGD_MASK		_PAGE_TABLE
#endif

/*H:320
 * The page table code is curly enough to need helper functions to keep it
 * clear and clean.  The kernel itself provides many of them; one advantage
 * of insisting that the Guest and Host use the same CONFIG_X86_PAE setting.
 *
 * There are two functions which return pointers to the shadow (aka "real")
 * page tables.
 *
 * spgd_addr() takes the virtual address and returns a pointer to the top-level
 * page directory entry (PGD) for that address.  Since we keep track of several
 * page tables, the "i" argument tells us which one we're interested in (it's
 * usually the current one).
 */
static pgd_t *spgd_addr(struct lg_cpu *cpu, u32 i, unsigned long vaddr)
{
	unsigned int index = pgd_index(vaddr);

	/* Return a pointer index'th pgd entry for the i'th page table. */
	return &cpu->lg->pgdirs[i].pgdir[index];
}

#ifdef CONFIG_X86_PAE
/*
 * This routine then takes the PGD entry given above, which contains the
 * address of the PMD page.  It then returns a pointer to the PMD entry for the
 * given address.
 */
static pmd_t *spmd_addr(struct lg_cpu *cpu, pgd_t spgd, unsigned long vaddr)
{
	unsigned int index = pmd_index(vaddr);
	pmd_t *page;

	/* You should never call this if the PGD entry wasn't valid */
	BUG_ON(!(pgd_flags(spgd) & _PAGE_PRESENT));
	page = __va(pgd_pfn(spgd) << PAGE_SHIFT);

	return &page[index];
}
#endif

/*
 * This routine then takes the page directory entry returned above, which
 * contains the address of the page table entry (PTE) page.  It then returns a
 * pointer to the PTE entry for the given address.
 */
static pte_t *spte_addr(struct lg_cpu *cpu, pgd_t spgd, unsigned long vaddr)
{
#ifdef CONFIG_X86_PAE
	pmd_t *pmd = spmd_addr(cpu, spgd, vaddr);
	pte_t *page = __va(pmd_pfn(*pmd) << PAGE_SHIFT);

	/* You should never call this if the PMD entry wasn't valid */
	BUG_ON(!(pmd_flags(*pmd) & _PAGE_PRESENT));
#else
	pte_t *page = __va(pgd_pfn(spgd) << PAGE_SHIFT);
	/* You should never call this if the PGD entry wasn't valid */
	BUG_ON(!(pgd_flags(spgd) & _PAGE_PRESENT));
#endif

	return &page[pte_index(vaddr)];
}

/*
 * These functions are just like the above, except they access the Guest
 * page tables.  Hence they return a Guest address.
 */
static unsigned long gpgd_addr(struct lg_cpu *cpu, unsigned long vaddr)
{
	unsigned int index = vaddr >> (PGDIR_SHIFT);
	return cpu->lg->pgdirs[cpu->cpu_pgd].gpgdir + index * sizeof(pgd_t);
}

#ifdef CONFIG_X86_PAE
/* Follow the PGD to the PMD. */
static unsigned long gpmd_addr(pgd_t gpgd, unsigned long vaddr)
{
	unsigned long gpage = pgd_pfn(gpgd) << PAGE_SHIFT;
	BUG_ON(!(pgd_flags(gpgd) & _PAGE_PRESENT));
	return gpage + pmd_index(vaddr) * sizeof(pmd_t);
}

/* Follow the PMD to the PTE. */
static unsigned long gpte_addr(struct lg_cpu *cpu,
			       pmd_t gpmd, unsigned long vaddr)
{
	unsigned long gpage = pmd_pfn(gpmd) << PAGE_SHIFT;

	BUG_ON(!(pmd_flags(gpmd) & _PAGE_PRESENT));
	return gpage + pte_index(vaddr) * sizeof(pte_t);
}
#else
/* Follow the PGD to the PTE (no mid-level for !PAE). */
static unsigned long gpte_addr(struct lg_cpu *cpu,
				pgd_t gpgd, unsigned long vaddr)
{
	unsigned long gpage = pgd_pfn(gpgd) << PAGE_SHIFT;

	BUG_ON(!(pgd_flags(gpgd) & _PAGE_PRESENT));
	return gpage + pte_index(vaddr) * sizeof(pte_t);
}
#endif
/*:*/

/*M:007
 * get_pfn is slow: we could probably try to grab batches of pages here as
 * an optimization (ie. pre-faulting).
:*/

/*H:350
 * This routine takes a page number given by the Guest and converts it to
 * an actual, physical page number.  It can fail for several reasons: the
 * virtual address might not be mapped by the Launcher, the write flag is set
 * and the page is read-only, or the write flag was set and the page was
 * shared so had to be copied, but we ran out of memory.
 *
 * This holds a reference to the page, so release_pte() is careful to put that
 * back.
 */
static unsigned long get_pfn(unsigned long virtpfn, int write)
{
	struct page *page;

	/* gup me one page at this address please! */
	if (get_user_pages_fast(virtpfn << PAGE_SHIFT, 1, write, &page) == 1)
		return page_to_pfn(page);

	/* This value indicates failure. */
	return -1UL;
}

/*H:340
 * Converting a Guest page table entry to a shadow (ie. real) page table
 * entry can be a little tricky.  The flags are (almost) the same, but the
 * Guest PTE contains a virtual page number: the CPU needs the real page
 * number.
 */
static pte_t gpte_to_spte(struct lg_cpu *cpu, pte_t gpte, int write)
{
	unsigned long pfn, base, flags;

	/*
	 * The Guest sets the global flag, because it thinks that it is using
	 * PGE.  We only told it to use PGE so it would tell us whether it was
	 * flushing a kernel mapping or a userspace mapping.  We don't actually
	 * use the global bit, so throw it away.
	 */
	flags = (pte_flags(gpte) & ~_PAGE_GLOBAL);

	/* The Guest's pages are offset inside the Launcher. */
	base = (unsigned long)cpu->lg->mem_base / PAGE_SIZE;

	/*
	 * We need a temporary "unsigned long" variable to hold the answer from
	 * get_pfn(), because it returns 0xFFFFFFFF on failure, which wouldn't
	 * fit in spte.pfn.  get_pfn() finds the real physical number of the
	 * page, given the virtual number.
	 */
	pfn = get_pfn(base + pte_pfn(gpte), write);
	if (pfn == -1UL) {
		kill_guest(cpu, "failed to get page %lu", pte_pfn(gpte));
		/*
		 * When we destroy the Guest, we'll go through the shadow page
		 * tables and release_pte() them.  Make sure we don't think
		 * this one is valid!
		 */
		flags = 0;
	}
	/* Now we assemble our shadow PTE from the page number and flags. */
	return pfn_pte(pfn, __pgprot(flags));
}

/*H:460 And to complete the chain, release_pte() looks like this: */
static void release_pte(pte_t pte)
{
	/*
	 * Remember that get_user_pages_fast() took a reference to the page, in
	 * get_pfn()?  We have to put it back now.
	 */
	if (pte_flags(pte) & _PAGE_PRESENT)
		put_page(pte_page(pte));
}
/*:*/

static bool gpte_in_iomem(struct lg_cpu *cpu, pte_t gpte)
{
	/* We don't handle large pages. */
	if (pte_flags(gpte) & _PAGE_PSE)
		return false;

	return (pte_pfn(gpte) >= cpu->lg->pfn_limit
		&& pte_pfn(gpte) < cpu->lg->device_limit);
}

static bool check_gpte(struct lg_cpu *cpu, pte_t gpte)
{
	if ((pte_flags(gpte) & _PAGE_PSE) ||
	    pte_pfn(gpte) >= cpu->lg->pfn_limit) {
		kill_guest(cpu, "bad page table entry");
		return false;
	}
	return true;
}

static bool check_gpgd(struct lg_cpu *cpu, pgd_t gpgd)
{
	if ((pgd_flags(gpgd) & ~CHECK_GPGD_MASK) ||
	    (pgd_pfn(gpgd) >= cpu->lg->pfn_limit)) {
		kill_guest(cpu, "bad page directory entry");
		return false;
	}
	return true;
}

#ifdef CONFIG_X86_PAE
static bool check_gpmd(struct lg_cpu *cpu, pmd_t gpmd)
{
	if ((pmd_flags(gpmd) & ~_PAGE_TABLE) ||
	    (pmd_pfn(gpmd) >= cpu->lg->pfn_limit)) {
		kill_guest(cpu, "bad page middle directory entry");
		return false;
	}
	return true;
}
#endif

/*H:331
 * This is the core routine to walk the shadow page tables and find the page
 * table entry for a specific address.
 *
 * If allocate is set, then we allocate any missing levels, setting the flags
 * on the new page directory and mid-level directories using the arguments
 * (which are copied from the Guest's page table entries).
 */
static pte_t *find_spte(struct lg_cpu *cpu, unsigned long vaddr, bool allocate,
			int pgd_flags, int pmd_flags)
{
	pgd_t *spgd;
	/* Mid level for PAE. */
#ifdef CONFIG_X86_PAE
	pmd_t *spmd;
#endif

	/* Get top level entry. */
	spgd = spgd_addr(cpu, cpu->cpu_pgd, vaddr);
	if (!(pgd_flags(*spgd) & _PAGE_PRESENT)) {
		/* No shadow entry: allocate a new shadow PTE page. */
		unsigned long ptepage;

		/* If they didn't want us to allocate anything, stop. */
		if (!allocate)
			return NULL;

		ptepage = get_zeroed_page(GFP_KERNEL);
		/*
		 * This is not really the Guest's fault, but killing it is
		 * simple for this corner case.
		 */
		if (!ptepage) {
			kill_guest(cpu, "out of memory allocating pte page");
			return NULL;
		}
		/*
		 * And we copy the flags to the shadow PGD entry.  The page
		 * number in the shadow PGD is the page we just allocated.
		 */
		set_pgd(spgd, __pgd(__pa(ptepage) | pgd_flags));
	}

	/*
	 * Intel's Physical Address Extension actually uses three levels of
	 * page tables, so we need to look in the mid-level.
	 */
#ifdef CONFIG_X86_PAE
	/* Now look at the mid-level shadow entry. */
	spmd = spmd_addr(cpu, *spgd, vaddr);

	if (!(pmd_flags(*spmd) & _PAGE_PRESENT)) {
		/* No shadow entry: allocate a new shadow PTE page. */
		unsigned long ptepage;

		/* If they didn't want us to allocate anything, stop. */
		if (!allocate)
			return NULL;

		ptepage = get_zeroed_page(GFP_KERNEL);

		/*
		 * This is not really the Guest's fault, but killing it is
		 * simple for this corner case.
		 */
		if (!ptepage) {
			kill_guest(cpu, "out of memory allocating pmd page");
			return NULL;
		}

		/*
		 * And we copy the flags to the shadow PMD entry.  The page
		 * number in the shadow PMD is the page we just allocated.
		 */
		set_pmd(spmd, __pmd(__pa(ptepage) | pmd_flags));
	}
#endif

	/* Get the pointer to the shadow PTE entry we're going to set. */
	return spte_addr(cpu, *spgd, vaddr);
}

/*H:330
 * (i) Looking up a page table entry when the Guest faults.
 *
 * We saw this call in run_guest(): when we see a page fault in the Guest, we
 * come here.  That's because we only set up the shadow page tables lazily as
 * they're needed, so we get page faults all the time and quietly fix them up
 * and return to the Guest without it knowing.
 *
 * If we fixed up the fault (ie. we mapped the address), this routine returns
 * true.  Otherwise, it was a real fault and we need to tell the Guest.
 *
 * There's a corner case: they're trying to access memory between
 * pfn_limit and device_limit, which is I/O memory.  In this case, we
 * return false and set @iomem to the physical address, so the the
 * Launcher can handle the instruction manually.
 */
bool demand_page(struct lg_cpu *cpu, unsigned long vaddr, int errcode,
		 unsigned long *iomem)
{
	unsigned long gpte_ptr;
	pte_t gpte;
	pte_t *spte;
	pmd_t gpmd;
	pgd_t gpgd;

	*iomem = 0;

	/* We never demand page the Switcher, so trying is a mistake. */
	if (vaddr >= switcher_addr)
		return false;

	/* First step: get the top-level Guest page table entry. */
	if (unlikely(cpu->linear_pages)) {
		/* Faking up a linear mapping. */
		gpgd = __pgd(CHECK_GPGD_MASK);
	} else {
		gpgd = lgread(cpu, gpgd_addr(cpu, vaddr), pgd_t);
		/* Toplevel not present?  We can't map it in. */
		if (!(pgd_flags(gpgd) & _PAGE_PRESENT))
			return false;

		/* 
		 * This kills the Guest if it has weird flags or tries to
		 * refer to a "physical" address outside the bounds.
		 */
		if (!check_gpgd(cpu, gpgd))
			return false;
	}

	/* This "mid-level" entry is only used for non-linear, PAE mode. */
	gpmd = __pmd(_PAGE_TABLE);

#ifdef CONFIG_X86_PAE
	if (likely(!cpu->linear_pages)) {
		gpmd = lgread(cpu, gpmd_addr(gpgd, vaddr), pmd_t);
		/* Middle level not present?  We can't map it in. */
		if (!(pmd_flags(gpmd) & _PAGE_PRESENT))
			return false;

		/* 
		 * This kills the Guest if it has weird flags or tries to
		 * refer to a "physical" address outside the bounds.
		 */
		if (!check_gpmd(cpu, gpmd))
			return false;
	}

	/*
	 * OK, now we look at the lower level in the Guest page table: keep its
	 * address, because we might update it later.
	 */
	gpte_ptr = gpte_addr(cpu, gpmd, vaddr);
#else
	/*
	 * OK, now we look at the lower level in the Guest page table: keep its
	 * address, because we might update it later.
	 */
	gpte_ptr = gpte_addr(cpu, gpgd, vaddr);
#endif

	if (unlikely(cpu->linear_pages)) {
		/* Linear?  Make up a PTE which points to same page. */
		gpte = __pte((vaddr & PAGE_MASK) | _PAGE_RW | _PAGE_PRESENT);
	} else {
		/* Read the actual PTE value. */
		gpte = lgread(cpu, gpte_ptr, pte_t);
	}

	/* If this page isn't in the Guest page tables, we can't page it in. */
	if (!(pte_flags(gpte) & _PAGE_PRESENT))
		return false;

	/*
	 * Check they're not trying to write to a page the Guest wants
	 * read-only (bit 2 of errcode == write).
	 */
	if ((errcode & 2) && !(pte_flags(gpte) & _PAGE_RW))
		return false;

	/* User access to a kernel-only page? (bit 3 == user access) */
	if ((errcode & 4) && !(pte_flags(gpte) & _PAGE_USER))
		return false;

	/* If they're accessing io memory, we expect a fault. */
	if (gpte_in_iomem(cpu, gpte)) {
		*iomem = (pte_pfn(gpte) << PAGE_SHIFT) | (vaddr & ~PAGE_MASK);
		return false;
	}

	/*
	 * Check that the Guest PTE flags are OK, and the page number is below
	 * the pfn_limit (ie. not mapping the Launcher binary).
	 */
	if (!check_gpte(cpu, gpte))
		return false;

	/* Add the _PAGE_ACCESSED and (for a write) _PAGE_DIRTY flag */
	gpte = pte_mkyoung(gpte);
	if (errcode & 2)
		gpte = pte_mkdirty(gpte);

	/* Get the pointer to the shadow PTE entry we're going to set. */
	spte = find_spte(cpu, vaddr, true, pgd_flags(gpgd), pmd_flags(gpmd));
	if (!spte)
		return false;

	/*
	 * If there was a valid shadow PTE entry here before, we release it.
	 * This can happen with a write to a previously read-only entry.
	 */
	release_pte(*spte);

	/*
	 * If this is a write, we insist that the Guest page is writable (the
	 * final arg to gpte_to_spte()).
	 */
	if (pte_dirty(gpte))
		*spte = gpte_to_spte(cpu, gpte, 1);
	else
		/*
		 * If this is a read, don't set the "writable" bit in the page
		 * table entry, even if the Guest says it's writable.  That way
		 * we will come back here when a write does actually occur, so
		 * we can update the Guest's _PAGE_DIRTY flag.
		 */
		set_pte(spte, gpte_to_spte(cpu, pte_wrprotect(gpte), 0));

	/*
	 * Finally, we write the Guest PTE entry back: we've set the
	 * _PAGE_ACCESSED and maybe the _PAGE_DIRTY flags.
	 */
	if (likely(!cpu->linear_pages))
		lgwrite(cpu, gpte_ptr, pte_t, gpte);

	/*
	 * The fault is fixed, the page table is populated, the mapping
	 * manipulated, the result returned and the code complete.  A small
	 * delay and a trace of alliteration are the only indications the Guest
	 * has that a page fault occurred at all.
	 */
	return true;
}

/*H:360
 * (ii) Making sure the Guest stack is mapped.
 *
 * Remember that direct traps into the Guest need a mapped Guest kernel stack.
 * pin_stack_pages() calls us here: we could simply call demand_page(), but as
 * we've seen that logic is quite long, and usually the stack pages are already
 * mapped, so it's overkill.
 *
 * This is a quick version which answers the question: is this virtual address
 * mapped by the shadow page tables, and is it writable?
 */
static bool page_writable(struct lg_cpu *cpu, unsigned long vaddr)
{
	pte_t *spte;
	unsigned long flags;

	/* You can't put your stack in the Switcher! */
	if (vaddr >= switcher_addr)
		return false;

	/* If there's no shadow PTE, it's not writable. */
	spte = find_spte(cpu, vaddr, false, 0, 0);
	if (!spte)
		return false;

	/*
	 * Check the flags on the pte entry itself: it must be present and
	 * writable.
	 */
	flags = pte_flags(*spte);
	return (flags & (_PAGE_PRESENT|_PAGE_RW)) == (_PAGE_PRESENT|_PAGE_RW);
}

/*
 * So, when pin_stack_pages() asks us to pin a page, we check if it's already
 * in the page tables, and if not, we call demand_page() with error code 2
 * (meaning "write").
 */
void pin_page(struct lg_cpu *cpu, unsigned long vaddr)
{
	unsigned long iomem;

	if (!page_writable(cpu, vaddr) && !demand_page(cpu, vaddr, 2, &iomem))
		kill_guest(cpu, "bad stack page %#lx", vaddr);
}
/*:*/

#ifdef CONFIG_X86_PAE
static void release_pmd(pmd_t *spmd)
{
	/* If the entry's not present, there's nothing to release. */
	if (pmd_flags(*spmd) & _PAGE_PRESENT) {
		unsigned int i;
		pte_t *ptepage = __va(pmd_pfn(*spmd) << PAGE_SHIFT);
		/* For each entry in the page, we might need to release it. */
		for (i = 0; i < PTRS_PER_PTE; i++)
			release_pte(ptepage[i]);
		/* Now we can free the page of PTEs */
		free_page((long)ptepage);
		/* And zero out the PMD entry so we never release it twice. */
		set_pmd(spmd, __pmd(0));
	}
}

static void release_pgd(pgd_t *spgd)
{
	/* If the entry's not present, there's nothing to release. */
	if (pgd_flags(*spgd) & _PAGE_PRESENT) {
		unsigned int i;
		pmd_t *pmdpage = __va(pgd_pfn(*spgd) << PAGE_SHIFT);

		for (i = 0; i < PTRS_PER_PMD; i++)
			release_pmd(&pmdpage[i]);

		/* Now we can free the page of PMDs */
		free_page((long)pmdpage);
		/* And zero out the PGD entry so we never release it twice. */
		set_pgd(spgd, __pgd(0));
	}
}

#else /* !CONFIG_X86_PAE */
/*H:450
 * If we chase down the release_pgd() code, the non-PAE version looks like
 * this.  The PAE version is almost identical, but instead of calling
 * release_pte it calls release_pmd(), which looks much like this.
 */
static void release_pgd(pgd_t *spgd)
{
	/* If the entry's not present, there's nothing to release. */
	if (pgd_flags(*spgd) & _PAGE_PRESENT) {
		unsigned int i;
		/*
		 * Converting the pfn to find the actual PTE page is easy: turn
		 * the page number into a physical address, then convert to a
		 * virtual address (easy for kernel pages like this one).
		 */
		pte_t *ptepage = __va(pgd_pfn(*spgd) << PAGE_SHIFT);
		/* For each entry in the page, we might need to release it. */
		for (i = 0; i < PTRS_PER_PTE; i++)
			release_pte(ptepage[i]);
		/* Now we can free the page of PTEs */
		free_page((long)ptepage);
		/* And zero out the PGD entry so we never release it twice. */
		*spgd = __pgd(0);
	}
}
#endif

/*H:445
 * We saw flush_user_mappings() twice: once from the flush_user_mappings()
 * hypercall and once in new_pgdir() when we re-used a top-level pgdir page.
 * It simply releases every PTE page from 0 up to the Guest's kernel address.
 */
static void flush_user_mappings(struct lguest *lg, int idx)
{
	unsigned int i;
	/* Release every pgd entry up to the kernel's address. */
	for (i = 0; i < pgd_index(lg->kernel_address); i++)
		release_pgd(lg->pgdirs[idx].pgdir + i);
}

/*H:440
 * (v) Flushing (throwing away) page tables,
 *
 * The Guest has a hypercall to throw away the page tables: it's used when a
 * large number of mappings have been changed.
 */
void guest_pagetable_flush_user(struct lg_cpu *cpu)
{
	/* Drop the userspace part of the current page table. */
	flush_user_mappings(cpu->lg, cpu->cpu_pgd);
}
/*:*/

/* We walk down the guest page tables to get a guest-physical address */
bool __guest_pa(struct lg_cpu *cpu, unsigned long vaddr, unsigned long *paddr)
{
	pgd_t gpgd;
	pte_t gpte;
#ifdef CONFIG_X86_PAE
	pmd_t gpmd;
#endif

	/* Still not set up?  Just map 1:1. */
	if (unlikely(cpu->linear_pages)) {
		*paddr = vaddr;
		return true;
	}

	/* First step: get the top-level Guest page table entry. */
	gpgd = lgread(cpu, gpgd_addr(cpu, vaddr), pgd_t);
	/* Toplevel not present?  We can't map it in. */
	if (!(pgd_flags(gpgd) & _PAGE_PRESENT))
		goto fail;

#ifdef CONFIG_X86_PAE
	gpmd = lgread(cpu, gpmd_addr(gpgd, vaddr), pmd_t);
	if (!(pmd_flags(gpmd) & _PAGE_PRESENT))
		goto fail;
	gpte = lgread(cpu, gpte_addr(cpu, gpmd, vaddr), pte_t);
#else
	gpte = lgread(cpu, gpte_addr(cpu, gpgd, vaddr), pte_t);
#endif
	if (!(pte_flags(gpte) & _PAGE_PRESENT))
		goto fail;

	*paddr = pte_pfn(gpte) * PAGE_SIZE | (vaddr & ~PAGE_MASK);
	return true;

fail:
	*paddr = -1UL;
	return false;
}

/*
 * This is the version we normally use: kills the Guest if it uses a
 * bad address
 */
unsigned long guest_pa(struct lg_cpu *cpu, unsigned long vaddr)
{
	unsigned long paddr;

	if (!__guest_pa(cpu, vaddr, &paddr))
		kill_guest(cpu, "Bad address %#lx", vaddr);
	return paddr;
}

/*
 * We keep several page tables.  This is a simple routine to find the page
 * table (if any) corresponding to this top-level address the Guest has given
 * us.
 */
static unsigned int find_pgdir(struct lguest *lg, unsigned long pgtable)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(lg->pgdirs); i++)
		if (lg->pgdirs[i].pgdir && lg->pgdirs[i].gpgdir == pgtable)
			break;
	return i;
}

/*H:435
 * And this is us, creating the new page directory.  If we really do
 * allocate a new one (and so the kernel parts are not there), we set
 * blank_pgdir.
 */
static unsigned int new_pgdir(struct lg_cpu *cpu,
			      unsigned long gpgdir,
			      int *blank_pgdir)
{
	unsigned int next;

	/*
	 * We pick one entry at random to throw out.  Choosing the Least
	 * Recently Used might be better, but this is easy.
	 */
	next = prandom_u32() % ARRAY_SIZE(cpu->lg->pgdirs);
	/* If it's never been allocated at all before, try now. */
	if (!cpu->lg->pgdirs[next].pgdir) {
		cpu->lg->pgdirs[next].pgdir =
					(pgd_t *)get_zeroed_page(GFP_KERNEL);
		/* If the allocation fails, just keep using the one we have */
		if (!cpu->lg->pgdirs[next].pgdir)
			next = cpu->cpu_pgd;
		else {
			/*
			 * This is a blank page, so there are no kernel
			 * mappings: caller must map the stack!
			 */
			*blank_pgdir = 1;
		}
	}
	/* Record which Guest toplevel this shadows. */
	cpu->lg->pgdirs[next].gpgdir = gpgdir;
	/* Release all the non-kernel mappings. */
	flush_user_mappings(cpu->lg, next);

	/* This hasn't run on any CPU at all. */
	cpu->lg->pgdirs[next].last_host_cpu = -1;

	return next;
}

/*H:501
 * We do need the Switcher code mapped at all times, so we allocate that
 * part of the Guest page table here.  We map the Switcher code immediately,
 * but defer mapping of the guest register page and IDT/LDT etc page until
 * just before we run the guest in map_switcher_in_guest().
 *
 * We *could* do this setup in map_switcher_in_guest(), but at that point
 * we've interrupts disabled, and allocating pages like that is fraught: we
 * can't sleep if we need to free up some memory.
 */
static bool allocate_switcher_mapping(struct lg_cpu *cpu)
{
	int i;

	for (i = 0; i < TOTAL_SWITCHER_PAGES; i++) {
		pte_t *pte = find_spte(cpu, switcher_addr + i * PAGE_SIZE, true,
				       CHECK_GPGD_MASK, _PAGE_TABLE);
		if (!pte)
			return false;

		/*
		 * Map the switcher page if not already there.  It might
		 * already be there because we call allocate_switcher_mapping()
		 * in guest_set_pgd() just in case it did discard our Switcher
		 * mapping, but it probably didn't.
		 */
		if (i == 0 && !(pte_flags(*pte) & _PAGE_PRESENT)) {
			/* Get a reference to the Switcher page. */
			get_page(lg_switcher_pages[0]);
			/* Create a read-only, exectuable, kernel-style PTE */
			set_pte(pte,
				mk_pte(lg_switcher_pages[0], PAGE_KERNEL_RX));
		}
	}
	cpu->lg->pgdirs[cpu->cpu_pgd].switcher_mapped = true;
	return true;
}

/*H:470
 * Finally, a routine which throws away everything: all PGD entries in all
 * the shadow page tables, including the Guest's kernel mappings.  This is used
 * when we destroy the Guest.
 */
static void release_all_pagetables(struct lguest *lg)
{
	unsigned int i, j;

	/* Every shadow pagetable this Guest has */
	for (i = 0; i < ARRAY_SIZE(lg->pgdirs); i++) {
		if (!lg->pgdirs[i].pgdir)
			continue;

		/* Every PGD entry. */
		for (j = 0; j < PTRS_PER_PGD; j++)
			release_pgd(lg->pgdirs[i].pgdir + j);
		lg->pgdirs[i].switcher_mapped = false;
		lg->pgdirs[i].last_host_cpu = -1;
	}
}

/*
 * We also throw away everything when a Guest tells us it's changed a kernel
 * mapping.  Since kernel mappings are in every page table, it's easiest to
 * throw them all away.  This traps the Guest in amber for a while as
 * everything faults back in, but it's rare.
 */
void guest_pagetable_clear_all(struct lg_cpu *cpu)
{
	release_all_pagetables(cpu->lg);
	/* We need the Guest kernel stack mapped again. */
	pin_stack_pages(cpu);
	/* And we need Switcher allocated. */
	if (!allocate_switcher_mapping(cpu))
		kill_guest(cpu, "Cannot populate switcher mapping");
}

/*H:430
 * (iv) Switching page tables
 *
 * Now we've seen all the page table setting and manipulation, let's see
 * what happens when the Guest changes page tables (ie. changes the top-level
 * pgdir).  This occurs on almost every context switch.
 */
void guest_new_pagetable(struct lg_cpu *cpu, unsigned long pgtable)
{
	int newpgdir, repin = 0;

	/*
	 * The very first time they call this, we're actually running without
	 * any page tables; we've been making it up.  Throw them away now.
	 */
	if (unlikely(cpu->linear_pages)) {
		release_all_pagetables(cpu->lg);
		cpu->linear_pages = false;
		/* Force allocation of a new pgdir. */
		newpgdir = ARRAY_SIZE(cpu->lg->pgdirs);
	} else {
		/* Look to see if we have this one already. */
		newpgdir = find_pgdir(cpu->lg, pgtable);
	}

	/*
	 * If not, we allocate or mug an existing one: if it's a fresh one,
	 * repin gets set to 1.
	 */
	if (newpgdir == ARRAY_SIZE(cpu->lg->pgdirs))
		newpgdir = new_pgdir(cpu, pgtable, &repin);
	/* Change the current pgd index to the new one. */
	cpu->cpu_pgd = newpgdir;
	/*
	 * If it was completely blank, we map in the Guest kernel stack and
	 * the Switcher.
	 */
	if (repin)
		pin_stack_pages(cpu);

	if (!cpu->lg->pgdirs[cpu->cpu_pgd].switcher_mapped) {
		if (!allocate_switcher_mapping(cpu))
			kill_guest(cpu, "Cannot populate switcher mapping");
	}
}
/*:*/

/*M:009
 * Since we throw away all mappings when a kernel mapping changes, our
 * performance sucks for guests using highmem.  In fact, a guest with
 * PAGE_OFFSET 0xc0000000 (the default) and more than about 700MB of RAM is
 * usually slower than a Guest with less memory.
 *
 * This, of course, cannot be fixed.  It would take some kind of... well, I
 * don't know, but the term "puissant code-fu" comes to mind.
:*/

/*H:420
 * This is the routine which actually sets the page table entry for then
 * "idx"'th shadow page table.
 *
 * Normally, we can just throw out the old entry and replace it with 0: if they
 * use it demand_page() will put the new entry in.  We need to do this anyway:
 * The Guest expects _PAGE_ACCESSED to be set on its PTE the first time a page
 * is read from, and _PAGE_DIRTY when it's written to.
 *
 * But Avi Kivity pointed out that most Operating Systems (Linux included) set
 * these bits on PTEs immediately anyway.  This is done to save the CPU from
 * having to update them, but it helps us the same way: if they set
 * _PAGE_ACCESSED then we can put a read-only PTE entry in immediately, and if
 * they set _PAGE_DIRTY then we can put a writable PTE entry in immediately.
 */
static void __guest_set_pte(struct lg_cpu *cpu, int idx,
		       unsigned long vaddr, pte_t gpte)
{
	/* Look up the matching shadow page directory entry. */
	pgd_t *spgd = spgd_addr(cpu, idx, vaddr);
#ifdef CONFIG_X86_PAE
	pmd_t *spmd;
#endif

	/* If the top level isn't present, there's no entry to update. */
	if (pgd_flags(*spgd) & _PAGE_PRESENT) {
#ifdef CONFIG_X86_PAE
		spmd = spmd_addr(cpu, *spgd, vaddr);
		if (pmd_flags(*spmd) & _PAGE_PRESENT) {
#endif
			/* Otherwise, start by releasing the existing entry. */
			pte_t *spte = spte_addr(cpu, *spgd, vaddr);
			release_pte(*spte);

			/*
			 * If they're setting this entry as dirty or accessed,
			 * we might as well put that entry they've given us in
			 * now.  This shaves 10% off a copy-on-write
			 * micro-benchmark.
			 */
			if ((pte_flags(gpte) & (_PAGE_DIRTY | _PAGE_ACCESSED))
			    && !gpte_in_iomem(cpu, gpte)) {
				if (!check_gpte(cpu, gpte))
					return;
				set_pte(spte,
					gpte_to_spte(cpu, gpte,
						pte_flags(gpte) & _PAGE_DIRTY));
			} else {
				/*
				 * Otherwise kill it and we can demand_page()
				 * it in later.
				 */
				set_pte(spte, __pte(0));
			}
#ifdef CONFIG_X86_PAE
		}
#endif
	}
}

/*H:410
 * Updating a PTE entry is a little trickier.
 *
 * We keep track of several different page tables (the Guest uses one for each
 * process, so it makes sense to cache at least a few).  Each of these have
 * identical kernel parts: ie. every mapping above PAGE_OFFSET is the same for
 * all processes.  So when the page table above that address changes, we update
 * all the page tables, not just the current one.  This is rare.
 *
 * The benefit is that when we have to track a new page table, we can keep all
 * the kernel mappings.  This speeds up context switch immensely.
 */
void guest_set_pte(struct lg_cpu *cpu,
		   unsigned long gpgdir, unsigned long vaddr, pte_t gpte)
{
	/* We don't let you remap the Switcher; we need it to get back! */
	if (vaddr >= switcher_addr) {
		kill_guest(cpu, "attempt to set pte into Switcher pages");
		return;
	}

	/*
	 * Kernel mappings must be changed on all top levels.  Slow, but doesn't
	 * happen often.
	 */
	if (vaddr >= cpu->lg->kernel_address) {
		unsigned int i;
		for (i = 0; i < ARRAY_SIZE(cpu->lg->pgdirs); i++)
			if (cpu->lg->pgdirs[i].pgdir)
				__guest_set_pte(cpu, i, vaddr, gpte);
	} else {
		/* Is this page table one we have a shadow for? */
		int pgdir = find_pgdir(cpu->lg, gpgdir);
		if (pgdir != ARRAY_SIZE(cpu->lg->pgdirs))
			/* If so, do the update. */
			__guest_set_pte(cpu, pgdir, vaddr, gpte);
	}
}

/*H:400
 * (iii) Setting up a page table entry when the Guest tells us one has changed.
 *
 * Just like we did in interrupts_and_traps.c, it makes sense for us to deal
 * with the other side of page tables while we're here: what happens when the
 * Guest asks for a page table to be updated?
 *
 * We already saw that demand_page() will fill in the shadow page tables when
 * needed, so we can simply remove shadow page table entries whenever the Guest
 * tells us they've changed.  When the Guest tries to use the new entry it will
 * fault and demand_page() will fix it up.
 *
 * So with that in mind here's our code to update a (top-level) PGD entry:
 */
void guest_set_pgd(struct lguest *lg, unsigned long gpgdir, u32 idx)
{
	int pgdir;

	if (idx > PTRS_PER_PGD) {
		kill_guest(&lg->cpus[0], "Attempt to set pgd %u/%u",
			   idx, PTRS_PER_PGD);
		return;
	}

	/* If they're talking about a page table we have a shadow for... */
	pgdir = find_pgdir(lg, gpgdir);
	if (pgdir < ARRAY_SIZE(lg->pgdirs)) {
		/* ... throw it away. */
		release_pgd(lg->pgdirs[pgdir].pgdir + idx);
		/* That might have been the Switcher mapping, remap it. */
		if (!allocate_switcher_mapping(&lg->cpus[0])) {
			kill_guest(&lg->cpus[0],
				   "Cannot populate switcher mapping");
		}
		lg->pgdirs[pgdir].last_host_cpu = -1;
	}
}

#ifdef CONFIG_X86_PAE
/* For setting a mid-level, we just throw everything away.  It's easy. */
void guest_set_pmd(struct lguest *lg, unsigned long pmdp, u32 idx)
{
	guest_pagetable_clear_all(&lg->cpus[0]);
}
#endif

/*H:500
 * (vii) Setting up the page tables initially.
 *
 * When a Guest is first created, set initialize a shadow page table which
 * we will populate on future faults.  The Guest doesn't have any actual
 * pagetables yet, so we set linear_pages to tell demand_page() to fake it
 * for the moment.
 *
 * We do need the Switcher to be mapped at all times, so we allocate that
 * part of the Guest page table here.
 */
int init_guest_pagetable(struct lguest *lg)
{
	struct lg_cpu *cpu = &lg->cpus[0];
	int allocated = 0;

	/* lg (and lg->cpus[]) starts zeroed: this allocates a new pgdir */
	cpu->cpu_pgd = new_pgdir(cpu, 0, &allocated);
	if (!allocated)
		return -ENOMEM;

	/* We start with a linear mapping until the initialize. */
	cpu->linear_pages = true;

	/* Allocate the page tables for the Switcher. */
	if (!allocate_switcher_mapping(cpu)) {
		release_all_pagetables(lg);
		return -ENOMEM;
	}

	return 0;
}

/*H:508 When the Guest calls LHCALL_LGUEST_INIT we do more setup. */
void page_table_guest_data_init(struct lg_cpu *cpu)
{
	/*
	 * We tell the Guest that it can't use the virtual addresses
	 * used by the Switcher.  This trick is equivalent to 4GB -
	 * switcher_addr.
	 */
	u32 top = ~switcher_addr + 1;

	/* We get the kernel address: above this is all kernel memory. */
	if (get_user(cpu->lg->kernel_address,
		     &cpu->lg->lguest_data->kernel_address)
		/*
		 * We tell the Guest that it can't use the top virtual
		 * addresses (used by the Switcher).
		 */
	    || put_user(top, &cpu->lg->lguest_data->reserve_mem)) {
		kill_guest(cpu, "bad guest page %p", cpu->lg->lguest_data);
		return;
	}

	/*
	 * In flush_user_mappings() we loop from 0 to
	 * "pgd_index(lg->kernel_address)".  This assumes it won't hit the
	 * Switcher mappings, so check that now.
	 */
	if (cpu->lg->kernel_address >= switcher_addr)
		kill_guest(cpu, "bad kernel address %#lx",
				 cpu->lg->kernel_address);
}

/* When a Guest dies, our cleanup is fairly simple. */
void free_guest_pagetable(struct lguest *lg)
{
	unsigned int i;

	/* Throw away all page table pages. */
	release_all_pagetables(lg);
	/* Now free the top levels: free_page() can handle 0 just fine. */
	for (i = 0; i < ARRAY_SIZE(lg->pgdirs); i++)
		free_page((long)lg->pgdirs[i].pgdir);
}

/*H:481
 * This clears the Switcher mappings for cpu #i.
 */
static void remove_switcher_percpu_map(struct lg_cpu *cpu, unsigned int i)
{
	unsigned long base = switcher_addr + PAGE_SIZE + i * PAGE_SIZE*2;
	pte_t *pte;

	/* Clear the mappings for both pages. */
	pte = find_spte(cpu, base, false, 0, 0);
	release_pte(*pte);
	set_pte(pte, __pte(0));

	pte = find_spte(cpu, base + PAGE_SIZE, false, 0, 0);
	release_pte(*pte);
	set_pte(pte, __pte(0));
}

/*H:480
 * (vi) Mapping the Switcher when the Guest is about to run.
 *
 * The Switcher and the two pages for this CPU need to be visible in the Guest
 * (and not the pages for other CPUs).
 *
 * The pages for the pagetables have all been allocated before: we just need
 * to make sure the actual PTEs are up-to-date for the CPU we're about to run
 * on.
 */
void map_switcher_in_guest(struct lg_cpu *cpu, struct lguest_pages *pages)
{
	unsigned long base;
	struct page *percpu_switcher_page, *regs_page;
	pte_t *pte;
	struct pgdir *pgdir = &cpu->lg->pgdirs[cpu->cpu_pgd];

	/* Switcher page should always be mapped by now! */
	BUG_ON(!pgdir->switcher_mapped);

	/* 
	 * Remember that we have two pages for each Host CPU, so we can run a
	 * Guest on each CPU without them interfering.  We need to make sure
	 * those pages are mapped correctly in the Guest, but since we usually
	 * run on the same CPU, we cache that, and only update the mappings
	 * when we move.
	 */
	if (pgdir->last_host_cpu == raw_smp_processor_id())
		return;

	/* -1 means unknown so we remove everything. */
	if (pgdir->last_host_cpu == -1) {
		unsigned int i;
		for_each_possible_cpu(i)
			remove_switcher_percpu_map(cpu, i);
	} else {
		/* We know exactly what CPU mapping to remove. */
		remove_switcher_percpu_map(cpu, pgdir->last_host_cpu);
	}

	/*
	 * When we're running the Guest, we want the Guest's "regs" page to
	 * appear where the first Switcher page for this CPU is.  This is an
	 * optimization: when the Switcher saves the Guest registers, it saves
	 * them into the first page of this CPU's "struct lguest_pages": if we
	 * make sure the Guest's register page is already mapped there, we
	 * don't have to copy them out again.
	 */
	/* Find the shadow PTE for this regs page. */
	base = switcher_addr + PAGE_SIZE
		+ raw_smp_processor_id() * sizeof(struct lguest_pages);
	pte = find_spte(cpu, base, false, 0, 0);
	regs_page = pfn_to_page(__pa(cpu->regs_page) >> PAGE_SHIFT);
	get_page(regs_page);
	set_pte(pte, mk_pte(regs_page, __pgprot(__PAGE_KERNEL & ~_PAGE_GLOBAL)));

	/*
	 * We map the second page of the struct lguest_pages read-only in
	 * the Guest: the IDT, GDT and other things it's not supposed to
	 * change.
	 */
	pte = find_spte(cpu, base + PAGE_SIZE, false, 0, 0);
	percpu_switcher_page
		= lg_switcher_pages[1 + raw_smp_processor_id()*2 + 1];
	get_page(percpu_switcher_page);
	set_pte(pte, mk_pte(percpu_switcher_page,
			    __pgprot(__PAGE_KERNEL_RO & ~_PAGE_GLOBAL)));

	pgdir->last_host_cpu = raw_smp_processor_id();
}

/*H:490
 * We've made it through the page table code.  Perhaps our tired brains are
 * still processing the details, or perhaps we're simply glad it's over.
 *
 * If nothing else, note that all this complexity in juggling shadow page tables
 * in sync with the Guest's page tables is for one reason: for most Guests this
 * page table dance determines how bad performance will be.  This is why Xen
 * uses exotic direct Guest pagetable manipulation, and why both Intel and AMD
 * have implemented shadow page table support directly into hardware.
 *
 * There is just one file remaining in the Host.
 */
