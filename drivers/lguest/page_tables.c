/*P:700 The pagetable code, on the other hand, still shows the scars of
 * previous encounters.  It's functional, and as neat as it can be in the
 * circumstances, but be wary, for these things are subtle and break easily.
 * The Guest provides a virtual to physical mapping, but we can neither trust
 * it nor use it: we verify and convert it here to point the hardware to the
 * actual Guest pages when running the Guest. :*/

/* Copyright (C) Rusty Russell IBM Corporation 2006.
 * GPL v2 and any later version */
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/percpu.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include "lg.h"

/*M:008 We hold reference to pages, which prevents them from being swapped.
 * It'd be nice to have a callback in the "struct mm_struct" when Linux wants
 * to swap out.  If we had this, and a shrinker callback to trim PTE pages, we
 * could probably consider launching Guests as non-root. :*/

/*H:300
 * The Page Table Code
 *
 * We use two-level page tables for the Guest.  If you're not entirely
 * comfortable with virtual addresses, physical addresses and page tables then
 * I recommend you review arch/x86/lguest/boot.c's "Page Table Handling" (with
 * diagrams!).
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


/* 1024 entries in a page table page maps 1024 pages: 4MB.  The Switcher is
 * conveniently placed at the top 4MB, so it uses a separate, complete PTE
 * page.  */
#define SWITCHER_PGD_INDEX (PTRS_PER_PGD - 1)

/* We actually need a separate PTE page for each CPU.  Remember that after the
 * Switcher code itself comes two pages for each CPU, and we don't want this
 * CPU's guest to see the pages of any other CPU. */
static DEFINE_PER_CPU(pte_t *, switcher_pte_pages);
#define switcher_pte_page(cpu) per_cpu(switcher_pte_pages, cpu)

/*H:320 The page table code is curly enough to need helper functions to keep it
 * clear and clean.
 *
 * There are two functions which return pointers to the shadow (aka "real")
 * page tables.
 *
 * spgd_addr() takes the virtual address and returns a pointer to the top-level
 * page directory entry (PGD) for that address.  Since we keep track of several
 * page tables, the "i" argument tells us which one we're interested in (it's
 * usually the current one). */
static pgd_t *spgd_addr(struct lg_cpu *cpu, u32 i, unsigned long vaddr)
{
	unsigned int index = pgd_index(vaddr);

	/* We kill any Guest trying to touch the Switcher addresses. */
	if (index >= SWITCHER_PGD_INDEX) {
		kill_guest(cpu, "attempt to access switcher pages");
		index = 0;
	}
	/* Return a pointer index'th pgd entry for the i'th page table. */
	return &cpu->lg->pgdirs[i].pgdir[index];
}

/* This routine then takes the page directory entry returned above, which
 * contains the address of the page table entry (PTE) page.  It then returns a
 * pointer to the PTE entry for the given address. */
static pte_t *spte_addr(pgd_t spgd, unsigned long vaddr)
{
	pte_t *page = __va(pgd_pfn(spgd) << PAGE_SHIFT);
	/* You should never call this if the PGD entry wasn't valid */
	BUG_ON(!(pgd_flags(spgd) & _PAGE_PRESENT));
	return &page[(vaddr >> PAGE_SHIFT) % PTRS_PER_PTE];
}

/* These two functions just like the above two, except they access the Guest
 * page tables.  Hence they return a Guest address. */
static unsigned long gpgd_addr(struct lg_cpu *cpu, unsigned long vaddr)
{
	unsigned int index = vaddr >> (PGDIR_SHIFT);
	return cpu->lg->pgdirs[cpu->cpu_pgd].gpgdir + index * sizeof(pgd_t);
}

static unsigned long gpte_addr(pgd_t gpgd, unsigned long vaddr)
{
	unsigned long gpage = pgd_pfn(gpgd) << PAGE_SHIFT;
	BUG_ON(!(pgd_flags(gpgd) & _PAGE_PRESENT));
	return gpage + ((vaddr>>PAGE_SHIFT) % PTRS_PER_PTE) * sizeof(pte_t);
}

/*H:350 This routine takes a page number given by the Guest and converts it to
 * an actual, physical page number.  It can fail for several reasons: the
 * virtual address might not be mapped by the Launcher, the write flag is set
 * and the page is read-only, or the write flag was set and the page was
 * shared so had to be copied, but we ran out of memory.
 *
 * This holds a reference to the page, so release_pte() is careful to
 * put that back. */
static unsigned long get_pfn(unsigned long virtpfn, int write)
{
	struct page *page;
	/* This value indicates failure. */
	unsigned long ret = -1UL;

	/* get_user_pages() is a complex interface: it gets the "struct
	 * vm_area_struct" and "struct page" assocated with a range of pages.
	 * It also needs the task's mmap_sem held, and is not very quick.
	 * It returns the number of pages it got. */
	down_read(&current->mm->mmap_sem);
	if (get_user_pages(current, current->mm, virtpfn << PAGE_SHIFT,
			   1, write, 1, &page, NULL) == 1)
		ret = page_to_pfn(page);
	up_read(&current->mm->mmap_sem);
	return ret;
}

/*H:340 Converting a Guest page table entry to a shadow (ie. real) page table
 * entry can be a little tricky.  The flags are (almost) the same, but the
 * Guest PTE contains a virtual page number: the CPU needs the real page
 * number. */
static pte_t gpte_to_spte(struct lg_cpu *cpu, pte_t gpte, int write)
{
	unsigned long pfn, base, flags;

	/* The Guest sets the global flag, because it thinks that it is using
	 * PGE.  We only told it to use PGE so it would tell us whether it was
	 * flushing a kernel mapping or a userspace mapping.  We don't actually
	 * use the global bit, so throw it away. */
	flags = (pte_flags(gpte) & ~_PAGE_GLOBAL);

	/* The Guest's pages are offset inside the Launcher. */
	base = (unsigned long)cpu->lg->mem_base / PAGE_SIZE;

	/* We need a temporary "unsigned long" variable to hold the answer from
	 * get_pfn(), because it returns 0xFFFFFFFF on failure, which wouldn't
	 * fit in spte.pfn.  get_pfn() finds the real physical number of the
	 * page, given the virtual number. */
	pfn = get_pfn(base + pte_pfn(gpte), write);
	if (pfn == -1UL) {
		kill_guest(cpu, "failed to get page %lu", pte_pfn(gpte));
		/* When we destroy the Guest, we'll go through the shadow page
		 * tables and release_pte() them.  Make sure we don't think
		 * this one is valid! */
		flags = 0;
	}
	/* Now we assemble our shadow PTE from the page number and flags. */
	return pfn_pte(pfn, __pgprot(flags));
}

/*H:460 And to complete the chain, release_pte() looks like this: */
static void release_pte(pte_t pte)
{
	/* Remember that get_user_pages() took a reference to the page, in
	 * get_pfn()?  We have to put it back now. */
	if (pte_flags(pte) & _PAGE_PRESENT)
		put_page(pfn_to_page(pte_pfn(pte)));
}
/*:*/

static void check_gpte(struct lg_cpu *cpu, pte_t gpte)
{
	if ((pte_flags(gpte) & (_PAGE_PWT|_PAGE_PSE))
	    || pte_pfn(gpte) >= cpu->lg->pfn_limit)
		kill_guest(cpu, "bad page table entry");
}

static void check_gpgd(struct lg_cpu *cpu, pgd_t gpgd)
{
	if ((pgd_flags(gpgd) & ~_PAGE_TABLE) ||
	   (pgd_pfn(gpgd) >= cpu->lg->pfn_limit))
		kill_guest(cpu, "bad page directory entry");
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
 * true.  Otherwise, it was a real fault and we need to tell the Guest. */
int demand_page(struct lg_cpu *cpu, unsigned long vaddr, int errcode)
{
	pgd_t gpgd;
	pgd_t *spgd;
	unsigned long gpte_ptr;
	pte_t gpte;
	pte_t *spte;

	/* First step: get the top-level Guest page table entry. */
	gpgd = lgread(cpu, gpgd_addr(cpu, vaddr), pgd_t);
	/* Toplevel not present?  We can't map it in. */
	if (!(pgd_flags(gpgd) & _PAGE_PRESENT))
		return 0;

	/* Now look at the matching shadow entry. */
	spgd = spgd_addr(cpu, cpu->cpu_pgd, vaddr);
	if (!(pgd_flags(*spgd) & _PAGE_PRESENT)) {
		/* No shadow entry: allocate a new shadow PTE page. */
		unsigned long ptepage = get_zeroed_page(GFP_KERNEL);
		/* This is not really the Guest's fault, but killing it is
		 * simple for this corner case. */
		if (!ptepage) {
			kill_guest(cpu, "out of memory allocating pte page");
			return 0;
		}
		/* We check that the Guest pgd is OK. */
		check_gpgd(cpu, gpgd);
		/* And we copy the flags to the shadow PGD entry.  The page
		 * number in the shadow PGD is the page we just allocated. */
		*spgd = __pgd(__pa(ptepage) | pgd_flags(gpgd));
	}

	/* OK, now we look at the lower level in the Guest page table: keep its
	 * address, because we might update it later. */
	gpte_ptr = gpte_addr(gpgd, vaddr);
	gpte = lgread(cpu, gpte_ptr, pte_t);

	/* If this page isn't in the Guest page tables, we can't page it in. */
	if (!(pte_flags(gpte) & _PAGE_PRESENT))
		return 0;

	/* Check they're not trying to write to a page the Guest wants
	 * read-only (bit 2 of errcode == write). */
	if ((errcode & 2) && !(pte_flags(gpte) & _PAGE_RW))
		return 0;

	/* User access to a kernel-only page? (bit 3 == user access) */
	if ((errcode & 4) && !(pte_flags(gpte) & _PAGE_USER))
		return 0;

	/* Check that the Guest PTE flags are OK, and the page number is below
	 * the pfn_limit (ie. not mapping the Launcher binary). */
	check_gpte(cpu, gpte);

	/* Add the _PAGE_ACCESSED and (for a write) _PAGE_DIRTY flag */
	gpte = pte_mkyoung(gpte);
	if (errcode & 2)
		gpte = pte_mkdirty(gpte);

	/* Get the pointer to the shadow PTE entry we're going to set. */
	spte = spte_addr(*spgd, vaddr);
	/* If there was a valid shadow PTE entry here before, we release it.
	 * This can happen with a write to a previously read-only entry. */
	release_pte(*spte);

	/* If this is a write, we insist that the Guest page is writable (the
	 * final arg to gpte_to_spte()). */
	if (pte_dirty(gpte))
		*spte = gpte_to_spte(cpu, gpte, 1);
	else
		/* If this is a read, don't set the "writable" bit in the page
		 * table entry, even if the Guest says it's writable.  That way
		 * we will come back here when a write does actually occur, so
		 * we can update the Guest's _PAGE_DIRTY flag. */
		*spte = gpte_to_spte(cpu, pte_wrprotect(gpte), 0);

	/* Finally, we write the Guest PTE entry back: we've set the
	 * _PAGE_ACCESSED and maybe the _PAGE_DIRTY flags. */
	lgwrite(cpu, gpte_ptr, pte_t, gpte);

	/* The fault is fixed, the page table is populated, the mapping
	 * manipulated, the result returned and the code complete.  A small
	 * delay and a trace of alliteration are the only indications the Guest
	 * has that a page fault occurred at all. */
	return 1;
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
 * mapped by the shadow page tables, and is it writable? */
static int page_writable(struct lg_cpu *cpu, unsigned long vaddr)
{
	pgd_t *spgd;
	unsigned long flags;

	/* Look at the current top level entry: is it present? */
	spgd = spgd_addr(cpu, cpu->cpu_pgd, vaddr);
	if (!(pgd_flags(*spgd) & _PAGE_PRESENT))
		return 0;

	/* Check the flags on the pte entry itself: it must be present and
	 * writable. */
	flags = pte_flags(*(spte_addr(*spgd, vaddr)));

	return (flags & (_PAGE_PRESENT|_PAGE_RW)) == (_PAGE_PRESENT|_PAGE_RW);
}

/* So, when pin_stack_pages() asks us to pin a page, we check if it's already
 * in the page tables, and if not, we call demand_page() with error code 2
 * (meaning "write"). */
void pin_page(struct lg_cpu *cpu, unsigned long vaddr)
{
	if (!page_writable(cpu, vaddr) && !demand_page(cpu, vaddr, 2))
		kill_guest(cpu, "bad stack page %#lx", vaddr);
}

/*H:450 If we chase down the release_pgd() code, it looks like this: */
static void release_pgd(struct lguest *lg, pgd_t *spgd)
{
	/* If the entry's not present, there's nothing to release. */
	if (pgd_flags(*spgd) & _PAGE_PRESENT) {
		unsigned int i;
		/* Converting the pfn to find the actual PTE page is easy: turn
		 * the page number into a physical address, then convert to a
		 * virtual address (easy for kernel pages like this one). */
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

/*H:445 We saw flush_user_mappings() twice: once from the flush_user_mappings()
 * hypercall and once in new_pgdir() when we re-used a top-level pgdir page.
 * It simply releases every PTE page from 0 up to the Guest's kernel address. */
static void flush_user_mappings(struct lguest *lg, int idx)
{
	unsigned int i;
	/* Release every pgd entry up to the kernel's address. */
	for (i = 0; i < pgd_index(lg->kernel_address); i++)
		release_pgd(lg, lg->pgdirs[idx].pgdir + i);
}

/*H:440 (v) Flushing (throwing away) page tables,
 *
 * The Guest has a hypercall to throw away the page tables: it's used when a
 * large number of mappings have been changed. */
void guest_pagetable_flush_user(struct lg_cpu *cpu)
{
	/* Drop the userspace part of the current page table. */
	flush_user_mappings(cpu->lg, cpu->cpu_pgd);
}
/*:*/

/* We walk down the guest page tables to get a guest-physical address */
unsigned long guest_pa(struct lg_cpu *cpu, unsigned long vaddr)
{
	pgd_t gpgd;
	pte_t gpte;

	/* First step: get the top-level Guest page table entry. */
	gpgd = lgread(cpu, gpgd_addr(cpu, vaddr), pgd_t);
	/* Toplevel not present?  We can't map it in. */
	if (!(pgd_flags(gpgd) & _PAGE_PRESENT))
		kill_guest(cpu, "Bad address %#lx", vaddr);

	gpte = lgread(cpu, gpte_addr(gpgd, vaddr), pte_t);
	if (!(pte_flags(gpte) & _PAGE_PRESENT))
		kill_guest(cpu, "Bad address %#lx", vaddr);

	return pte_pfn(gpte) * PAGE_SIZE | (vaddr & ~PAGE_MASK);
}

/* We keep several page tables.  This is a simple routine to find the page
 * table (if any) corresponding to this top-level address the Guest has given
 * us. */
static unsigned int find_pgdir(struct lguest *lg, unsigned long pgtable)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(lg->pgdirs); i++)
		if (lg->pgdirs[i].gpgdir == pgtable)
			break;
	return i;
}

/*H:435 And this is us, creating the new page directory.  If we really do
 * allocate a new one (and so the kernel parts are not there), we set
 * blank_pgdir. */
static unsigned int new_pgdir(struct lg_cpu *cpu,
			      unsigned long gpgdir,
			      int *blank_pgdir)
{
	unsigned int next;

	/* We pick one entry at random to throw out.  Choosing the Least
	 * Recently Used might be better, but this is easy. */
	next = random32() % ARRAY_SIZE(cpu->lg->pgdirs);
	/* If it's never been allocated at all before, try now. */
	if (!cpu->lg->pgdirs[next].pgdir) {
		cpu->lg->pgdirs[next].pgdir =
					(pgd_t *)get_zeroed_page(GFP_KERNEL);
		/* If the allocation fails, just keep using the one we have */
		if (!cpu->lg->pgdirs[next].pgdir)
			next = cpu->cpu_pgd;
		else
			/* This is a blank page, so there are no kernel
			 * mappings: caller must map the stack! */
			*blank_pgdir = 1;
	}
	/* Record which Guest toplevel this shadows. */
	cpu->lg->pgdirs[next].gpgdir = gpgdir;
	/* Release all the non-kernel mappings. */
	flush_user_mappings(cpu->lg, next);

	return next;
}

/*H:430 (iv) Switching page tables
 *
 * Now we've seen all the page table setting and manipulation, let's see what
 * what happens when the Guest changes page tables (ie. changes the top-level
 * pgdir).  This occurs on almost every context switch. */
void guest_new_pagetable(struct lg_cpu *cpu, unsigned long pgtable)
{
	int newpgdir, repin = 0;

	/* Look to see if we have this one already. */
	newpgdir = find_pgdir(cpu->lg, pgtable);
	/* If not, we allocate or mug an existing one: if it's a fresh one,
	 * repin gets set to 1. */
	if (newpgdir == ARRAY_SIZE(cpu->lg->pgdirs))
		newpgdir = new_pgdir(cpu, pgtable, &repin);
	/* Change the current pgd index to the new one. */
	cpu->cpu_pgd = newpgdir;
	/* If it was completely blank, we map in the Guest kernel stack */
	if (repin)
		pin_stack_pages(cpu);
}

/*H:470 Finally, a routine which throws away everything: all PGD entries in all
 * the shadow page tables, including the Guest's kernel mappings.  This is used
 * when we destroy the Guest. */
static void release_all_pagetables(struct lguest *lg)
{
	unsigned int i, j;

	/* Every shadow pagetable this Guest has */
	for (i = 0; i < ARRAY_SIZE(lg->pgdirs); i++)
		if (lg->pgdirs[i].pgdir)
			/* Every PGD entry except the Switcher at the top */
			for (j = 0; j < SWITCHER_PGD_INDEX; j++)
				release_pgd(lg, lg->pgdirs[i].pgdir + j);
}

/* We also throw away everything when a Guest tells us it's changed a kernel
 * mapping.  Since kernel mappings are in every page table, it's easiest to
 * throw them all away.  This traps the Guest in amber for a while as
 * everything faults back in, but it's rare. */
void guest_pagetable_clear_all(struct lg_cpu *cpu)
{
	release_all_pagetables(cpu->lg);
	/* We need the Guest kernel stack mapped again. */
	pin_stack_pages(cpu);
}
/*:*/
/*M:009 Since we throw away all mappings when a kernel mapping changes, our
 * performance sucks for guests using highmem.  In fact, a guest with
 * PAGE_OFFSET 0xc0000000 (the default) and more than about 700MB of RAM is
 * usually slower than a Guest with less memory.
 *
 * This, of course, cannot be fixed.  It would take some kind of... well, I
 * don't know, but the term "puissant code-fu" comes to mind. :*/

/*H:420 This is the routine which actually sets the page table entry for then
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
static void do_set_pte(struct lg_cpu *cpu, int idx,
		       unsigned long vaddr, pte_t gpte)
{
	/* Look up the matching shadow page directory entry. */
	pgd_t *spgd = spgd_addr(cpu, idx, vaddr);

	/* If the top level isn't present, there's no entry to update. */
	if (pgd_flags(*spgd) & _PAGE_PRESENT) {
		/* Otherwise, we start by releasing the existing entry. */
		pte_t *spte = spte_addr(*spgd, vaddr);
		release_pte(*spte);

		/* If they're setting this entry as dirty or accessed, we might
		 * as well put that entry they've given us in now.  This shaves
		 * 10% off a copy-on-write micro-benchmark. */
		if (pte_flags(gpte) & (_PAGE_DIRTY | _PAGE_ACCESSED)) {
			check_gpte(cpu, gpte);
			*spte = gpte_to_spte(cpu, gpte,
					     pte_flags(gpte) & _PAGE_DIRTY);
		} else
			/* Otherwise kill it and we can demand_page() it in
			 * later. */
			*spte = __pte(0);
	}
}

/*H:410 Updating a PTE entry is a little trickier.
 *
 * We keep track of several different page tables (the Guest uses one for each
 * process, so it makes sense to cache at least a few).  Each of these have
 * identical kernel parts: ie. every mapping above PAGE_OFFSET is the same for
 * all processes.  So when the page table above that address changes, we update
 * all the page tables, not just the current one.  This is rare.
 *
 * The benefit is that when we have to track a new page table, we can copy keep
 * all the kernel mappings.  This speeds up context switch immensely. */
void guest_set_pte(struct lg_cpu *cpu,
		   unsigned long gpgdir, unsigned long vaddr, pte_t gpte)
{
	/* Kernel mappings must be changed on all top levels.  Slow, but
	 * doesn't happen often. */
	if (vaddr >= cpu->lg->kernel_address) {
		unsigned int i;
		for (i = 0; i < ARRAY_SIZE(cpu->lg->pgdirs); i++)
			if (cpu->lg->pgdirs[i].pgdir)
				do_set_pte(cpu, i, vaddr, gpte);
	} else {
		/* Is this page table one we have a shadow for? */
		int pgdir = find_pgdir(cpu->lg, gpgdir);
		if (pgdir != ARRAY_SIZE(cpu->lg->pgdirs))
			/* If so, do the update. */
			do_set_pte(cpu, pgdir, vaddr, gpte);
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
 * So with that in mind here's our code to to update a (top-level) PGD entry:
 */
void guest_set_pmd(struct lguest *lg, unsigned long gpgdir, u32 idx)
{
	int pgdir;

	/* The kernel seems to try to initialize this early on: we ignore its
	 * attempts to map over the Switcher. */
	if (idx >= SWITCHER_PGD_INDEX)
		return;

	/* If they're talking about a page table we have a shadow for... */
	pgdir = find_pgdir(lg, gpgdir);
	if (pgdir < ARRAY_SIZE(lg->pgdirs))
		/* ... throw it away. */
		release_pgd(lg, lg->pgdirs[pgdir].pgdir + idx);
}

/*H:500 (vii) Setting up the page tables initially.
 *
 * When a Guest is first created, the Launcher tells us where the toplevel of
 * its first page table is.  We set some things up here: */
int init_guest_pagetable(struct lguest *lg, unsigned long pgtable)
{
	/* We start on the first shadow page table, and give it a blank PGD
	 * page. */
	lg->pgdirs[0].gpgdir = pgtable;
	lg->pgdirs[0].pgdir = (pgd_t *)get_zeroed_page(GFP_KERNEL);
	if (!lg->pgdirs[0].pgdir)
		return -ENOMEM;
	lg->cpus[0].cpu_pgd = 0;
	return 0;
}

/* When the Guest calls LHCALL_LGUEST_INIT we do more setup. */
void page_table_guest_data_init(struct lg_cpu *cpu)
{
	/* We get the kernel address: above this is all kernel memory. */
	if (get_user(cpu->lg->kernel_address,
		     &cpu->lg->lguest_data->kernel_address)
	    /* We tell the Guest that it can't use the top 4MB of virtual
	     * addresses used by the Switcher. */
	    || put_user(4U*1024*1024, &cpu->lg->lguest_data->reserve_mem)
	    || put_user(cpu->lg->pgdirs[0].gpgdir, &cpu->lg->lguest_data->pgdir))
		kill_guest(cpu, "bad guest page %p", cpu->lg->lguest_data);

	/* In flush_user_mappings() we loop from 0 to
	 * "pgd_index(lg->kernel_address)".  This assumes it won't hit the
	 * Switcher mappings, so check that now. */
	if (pgd_index(cpu->lg->kernel_address) >= SWITCHER_PGD_INDEX)
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

/*H:480 (vi) Mapping the Switcher when the Guest is about to run.
 *
 * The Switcher and the two pages for this CPU need to be visible in the
 * Guest (and not the pages for other CPUs).  We have the appropriate PTE pages
 * for each CPU already set up, we just need to hook them in now we know which
 * Guest is about to run on this CPU. */
void map_switcher_in_guest(struct lg_cpu *cpu, struct lguest_pages *pages)
{
	pte_t *switcher_pte_page = __get_cpu_var(switcher_pte_pages);
	pgd_t switcher_pgd;
	pte_t regs_pte;
	unsigned long pfn;

	/* Make the last PGD entry for this Guest point to the Switcher's PTE
	 * page for this CPU (with appropriate flags). */
	switcher_pgd = __pgd(__pa(switcher_pte_page) | __PAGE_KERNEL);

	cpu->lg->pgdirs[cpu->cpu_pgd].pgdir[SWITCHER_PGD_INDEX] = switcher_pgd;

	/* We also change the Switcher PTE page.  When we're running the Guest,
	 * we want the Guest's "regs" page to appear where the first Switcher
	 * page for this CPU is.  This is an optimization: when the Switcher
	 * saves the Guest registers, it saves them into the first page of this
	 * CPU's "struct lguest_pages": if we make sure the Guest's register
	 * page is already mapped there, we don't have to copy them out
	 * again. */
	pfn = __pa(cpu->regs_page) >> PAGE_SHIFT;
	regs_pte = pfn_pte(pfn, __pgprot(__PAGE_KERNEL));
	switcher_pte_page[(unsigned long)pages/PAGE_SIZE%PTRS_PER_PTE] = regs_pte;
}
/*:*/

static void free_switcher_pte_pages(void)
{
	unsigned int i;

	for_each_possible_cpu(i)
		free_page((long)switcher_pte_page(i));
}

/*H:520 Setting up the Switcher PTE page for given CPU is fairly easy, given
 * the CPU number and the "struct page"s for the Switcher code itself.
 *
 * Currently the Switcher is less than a page long, so "pages" is always 1. */
static __init void populate_switcher_pte_page(unsigned int cpu,
					      struct page *switcher_page[],
					      unsigned int pages)
{
	unsigned int i;
	pte_t *pte = switcher_pte_page(cpu);

	/* The first entries are easy: they map the Switcher code. */
	for (i = 0; i < pages; i++) {
		pte[i] = mk_pte(switcher_page[i],
				__pgprot(_PAGE_PRESENT|_PAGE_ACCESSED));
	}

	/* The only other thing we map is this CPU's pair of pages. */
	i = pages + cpu*2;

	/* First page (Guest registers) is writable from the Guest */
	pte[i] = pfn_pte(page_to_pfn(switcher_page[i]),
			 __pgprot(_PAGE_PRESENT|_PAGE_ACCESSED|_PAGE_RW));

	/* The second page contains the "struct lguest_ro_state", and is
	 * read-only. */
	pte[i+1] = pfn_pte(page_to_pfn(switcher_page[i+1]),
			   __pgprot(_PAGE_PRESENT|_PAGE_ACCESSED));
}

/* We've made it through the page table code.  Perhaps our tired brains are
 * still processing the details, or perhaps we're simply glad it's over.
 *
 * If nothing else, note that all this complexity in juggling shadow page
 * tables in sync with the Guest's page tables is for one reason: for most
 * Guests this page table dance determines how bad performance will be.  This
 * is why Xen uses exotic direct Guest pagetable manipulation, and why both
 * Intel and AMD have implemented shadow page table support directly into
 * hardware.
 *
 * There is just one file remaining in the Host. */

/*H:510 At boot or module load time, init_pagetables() allocates and populates
 * the Switcher PTE page for each CPU. */
__init int init_pagetables(struct page **switcher_page, unsigned int pages)
{
	unsigned int i;

	for_each_possible_cpu(i) {
		switcher_pte_page(i) = (pte_t *)get_zeroed_page(GFP_KERNEL);
		if (!switcher_pte_page(i)) {
			free_switcher_pte_pages();
			return -ENOMEM;
		}
		populate_switcher_pte_page(i, switcher_page, pages);
	}
	return 0;
}
/*:*/

/* Cleaning up simply involves freeing the PTE page for each CPU. */
void free_pagetables(void)
{
	free_switcher_pte_pages();
}
