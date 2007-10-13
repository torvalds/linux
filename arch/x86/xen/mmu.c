/*
 * Xen mmu operations
 *
 * This file contains the various mmu fetch and update operations.
 * The most important job they must perform is the mapping between the
 * domain's pfn and the overall machine mfns.
 *
 * Xen allows guests to directly update the pagetable, in a controlled
 * fashion.  In other words, the guest modifies the same pagetable
 * that the CPU actually uses, which eliminates the overhead of having
 * a separate shadow pagetable.
 *
 * In order to allow this, it falls on the guest domain to map its
 * notion of a "physical" pfn - which is just a domain-local linear
 * address - into a real "machine address" which the CPU's MMU can
 * use.
 *
 * A pgd_t/pmd_t/pte_t will typically contain an mfn, and so can be
 * inserted directly into the pagetable.  When creating a new
 * pte/pmd/pgd, it converts the passed pfn into an mfn.  Conversely,
 * when reading the content back with __(pgd|pmd|pte)_val, it converts
 * the mfn back into a pfn.
 *
 * The other constraint is that all pages which make up a pagetable
 * must be mapped read-only in the guest.  This prevents uncontrolled
 * guest updates to the pagetable.  Xen strictly enforces this, and
 * will disallow any pagetable update which will end up mapping a
 * pagetable page RW, and will disallow using any writable page as a
 * pagetable.
 *
 * Naively, when loading %cr3 with the base of a new pagetable, Xen
 * would need to validate the whole pagetable before going on.
 * Naturally, this is quite slow.  The solution is to "pin" a
 * pagetable, which enforces all the constraints on the pagetable even
 * when it is not actively in use.  This menas that Xen can be assured
 * that it is still valid when you do load it into %cr3, and doesn't
 * need to revalidate it.
 *
 * Jeremy Fitzhardinge <jeremy@xensource.com>, XenSource Inc, 2007
 */
#include <linux/sched.h>
#include <linux/highmem.h>
#include <linux/bug.h>
#include <linux/sched.h>

#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/paravirt.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>

#include <xen/page.h>
#include <xen/interface/xen.h>

#include "multicalls.h"
#include "mmu.h"

xmaddr_t arbitrary_virt_to_machine(unsigned long address)
{
	pte_t *pte = lookup_address(address);
	unsigned offset = address & PAGE_MASK;

	BUG_ON(pte == NULL);

	return XMADDR((pte_mfn(*pte) << PAGE_SHIFT) + offset);
}

void make_lowmem_page_readonly(void *vaddr)
{
	pte_t *pte, ptev;
	unsigned long address = (unsigned long)vaddr;

	pte = lookup_address(address);
	BUG_ON(pte == NULL);

	ptev = pte_wrprotect(*pte);

	if (HYPERVISOR_update_va_mapping(address, ptev, 0))
		BUG();
}

void make_lowmem_page_readwrite(void *vaddr)
{
	pte_t *pte, ptev;
	unsigned long address = (unsigned long)vaddr;

	pte = lookup_address(address);
	BUG_ON(pte == NULL);

	ptev = pte_mkwrite(*pte);

	if (HYPERVISOR_update_va_mapping(address, ptev, 0))
		BUG();
}


void xen_set_pmd(pmd_t *ptr, pmd_t val)
{
	struct multicall_space mcs;
	struct mmu_update *u;

	preempt_disable();

	mcs = xen_mc_entry(sizeof(*u));
	u = mcs.args;
	u->ptr = virt_to_machine(ptr).maddr;
	u->val = pmd_val_ma(val);
	MULTI_mmu_update(mcs.mc, u, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

/*
 * Associate a virtual page frame with a given physical page frame
 * and protection flags for that frame.
 */
void set_pte_mfn(unsigned long vaddr, unsigned long mfn, pgprot_t flags)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = swapper_pg_dir + pgd_index(vaddr);
	if (pgd_none(*pgd)) {
		BUG();
		return;
	}
	pud = pud_offset(pgd, vaddr);
	if (pud_none(*pud)) {
		BUG();
		return;
	}
	pmd = pmd_offset(pud, vaddr);
	if (pmd_none(*pmd)) {
		BUG();
		return;
	}
	pte = pte_offset_kernel(pmd, vaddr);
	/* <mfn,flags> stored as-is, to permit clearing entries */
	xen_set_pte(pte, mfn_pte(mfn, flags));

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

void xen_set_pte_at(struct mm_struct *mm, unsigned long addr,
		    pte_t *ptep, pte_t pteval)
{
	if (mm == current->mm || mm == &init_mm) {
		if (xen_get_lazy_mode() == PARAVIRT_LAZY_MMU) {
			struct multicall_space mcs;
			mcs = xen_mc_entry(0);

			MULTI_update_va_mapping(mcs.mc, addr, pteval, 0);
			xen_mc_issue(PARAVIRT_LAZY_MMU);
			return;
		} else
			if (HYPERVISOR_update_va_mapping(addr, pteval, 0) == 0)
				return;
	}
	xen_set_pte(ptep, pteval);
}

#ifdef CONFIG_X86_PAE
void xen_set_pud(pud_t *ptr, pud_t val)
{
	struct multicall_space mcs;
	struct mmu_update *u;

	preempt_disable();

	mcs = xen_mc_entry(sizeof(*u));
	u = mcs.args;
	u->ptr = virt_to_machine(ptr).maddr;
	u->val = pud_val_ma(val);
	MULTI_mmu_update(mcs.mc, u, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

void xen_set_pte(pte_t *ptep, pte_t pte)
{
	ptep->pte_high = pte.pte_high;
	smp_wmb();
	ptep->pte_low = pte.pte_low;
}

void xen_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	set_64bit((u64 *)ptep, pte_val_ma(pte));
}

void xen_pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	ptep->pte_low = 0;
	smp_wmb();		/* make sure low gets written first */
	ptep->pte_high = 0;
}

void xen_pmd_clear(pmd_t *pmdp)
{
	xen_set_pmd(pmdp, __pmd(0));
}

unsigned long long xen_pte_val(pte_t pte)
{
	unsigned long long ret = 0;

	if (pte.pte_low) {
		ret = ((unsigned long long)pte.pte_high << 32) | pte.pte_low;
		ret = machine_to_phys(XMADDR(ret)).paddr | 1;
	}

	return ret;
}

unsigned long long xen_pmd_val(pmd_t pmd)
{
	unsigned long long ret = pmd.pmd;
	if (ret)
		ret = machine_to_phys(XMADDR(ret)).paddr | 1;
	return ret;
}

unsigned long long xen_pgd_val(pgd_t pgd)
{
	unsigned long long ret = pgd.pgd;
	if (ret)
		ret = machine_to_phys(XMADDR(ret)).paddr | 1;
	return ret;
}

pte_t xen_make_pte(unsigned long long pte)
{
	if (pte & 1)
		pte = phys_to_machine(XPADDR(pte)).maddr;

	return (pte_t){ pte, pte >> 32 };
}

pmd_t xen_make_pmd(unsigned long long pmd)
{
	if (pmd & 1)
		pmd = phys_to_machine(XPADDR(pmd)).maddr;

	return (pmd_t){ pmd };
}

pgd_t xen_make_pgd(unsigned long long pgd)
{
	if (pgd & _PAGE_PRESENT)
		pgd = phys_to_machine(XPADDR(pgd)).maddr;

	return (pgd_t){ pgd };
}
#else  /* !PAE */
void xen_set_pte(pte_t *ptep, pte_t pte)
{
	*ptep = pte;
}

unsigned long xen_pte_val(pte_t pte)
{
	unsigned long ret = pte.pte_low;

	if (ret & _PAGE_PRESENT)
		ret = machine_to_phys(XMADDR(ret)).paddr;

	return ret;
}

unsigned long xen_pgd_val(pgd_t pgd)
{
	unsigned long ret = pgd.pgd;
	if (ret)
		ret = machine_to_phys(XMADDR(ret)).paddr | 1;
	return ret;
}

pte_t xen_make_pte(unsigned long pte)
{
	if (pte & _PAGE_PRESENT)
		pte = phys_to_machine(XPADDR(pte)).maddr;

	return (pte_t){ pte };
}

pgd_t xen_make_pgd(unsigned long pgd)
{
	if (pgd & _PAGE_PRESENT)
		pgd = phys_to_machine(XPADDR(pgd)).maddr;

	return (pgd_t){ pgd };
}
#endif	/* CONFIG_X86_PAE */



/*
  (Yet another) pagetable walker.  This one is intended for pinning a
  pagetable.  This means that it walks a pagetable and calls the
  callback function on each page it finds making up the page table,
  at every level.  It walks the entire pagetable, but it only bothers
  pinning pte pages which are below pte_limit.  In the normal case
  this will be TASK_SIZE, but at boot we need to pin up to
  FIXADDR_TOP.  But the important bit is that we don't pin beyond
  there, because then we start getting into Xen's ptes.
*/
static int pgd_walk(pgd_t *pgd_base, int (*func)(struct page *, unsigned),
		    unsigned long limit)
{
	pgd_t *pgd = pgd_base;
	int flush = 0;
	unsigned long addr = 0;
	unsigned long pgd_next;

	BUG_ON(limit > FIXADDR_TOP);

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 0;

	for (; addr != FIXADDR_TOP; pgd++, addr = pgd_next) {
		pud_t *pud;
		unsigned long pud_limit, pud_next;

		pgd_next = pud_limit = pgd_addr_end(addr, FIXADDR_TOP);

		if (!pgd_val(*pgd))
			continue;

		pud = pud_offset(pgd, 0);

		if (PTRS_PER_PUD > 1) /* not folded */
			flush |= (*func)(virt_to_page(pud), 0);

		for (; addr != pud_limit; pud++, addr = pud_next) {
			pmd_t *pmd;
			unsigned long pmd_limit;

			pud_next = pud_addr_end(addr, pud_limit);

			if (pud_next < limit)
				pmd_limit = pud_next;
			else
				pmd_limit = limit;

			if (pud_none(*pud))
				continue;

			pmd = pmd_offset(pud, 0);

			if (PTRS_PER_PMD > 1) /* not folded */
				flush |= (*func)(virt_to_page(pmd), 0);

			for (; addr != pmd_limit; pmd++) {
				addr += (PAGE_SIZE * PTRS_PER_PTE);
				if ((pmd_limit-1) < (addr-1)) {
					addr = pmd_limit;
					break;
				}

				if (pmd_none(*pmd))
					continue;

				flush |= (*func)(pmd_page(*pmd), 0);
			}
		}
	}

	flush |= (*func)(virt_to_page(pgd_base), UVMF_TLB_FLUSH);

	return flush;
}

static int pin_page(struct page *page, unsigned flags)
{
	unsigned pgfl = test_and_set_bit(PG_pinned, &page->flags);
	int flush;

	if (pgfl)
		flush = 0;		/* already pinned */
	else if (PageHighMem(page))
		/* kmaps need flushing if we found an unpinned
		   highpage */
		flush = 1;
	else {
		void *pt = lowmem_page_address(page);
		unsigned long pfn = page_to_pfn(page);
		struct multicall_space mcs = __xen_mc_entry(0);

		flush = 0;

		MULTI_update_va_mapping(mcs.mc, (unsigned long)pt,
					pfn_pte(pfn, PAGE_KERNEL_RO),
					flags);
	}

	return flush;
}

/* This is called just after a mm has been created, but it has not
   been used yet.  We need to make sure that its pagetable is all
   read-only, and can be pinned. */
void xen_pgd_pin(pgd_t *pgd)
{
	struct multicall_space mcs;
	struct mmuext_op *op;

	xen_mc_batch();

	if (pgd_walk(pgd, pin_page, TASK_SIZE)) {
		/* re-enable interrupts for kmap_flush_unused */
		xen_mc_issue(0);
		kmap_flush_unused();
		xen_mc_batch();
	}

	mcs = __xen_mc_entry(sizeof(*op));
	op = mcs.args;

#ifdef CONFIG_X86_PAE
	op->cmd = MMUEXT_PIN_L3_TABLE;
#else
	op->cmd = MMUEXT_PIN_L2_TABLE;
#endif
	op->arg1.mfn = pfn_to_mfn(PFN_DOWN(__pa(pgd)));
	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(0);
}

/* The init_mm pagetable is really pinned as soon as its created, but
   that's before we have page structures to store the bits.  So do all
   the book-keeping now. */
static __init int mark_pinned(struct page *page, unsigned flags)
{
	SetPagePinned(page);
	return 0;
}

void __init xen_mark_init_mm_pinned(void)
{
	pgd_walk(init_mm.pgd, mark_pinned, FIXADDR_TOP);
}

static int unpin_page(struct page *page, unsigned flags)
{
	unsigned pgfl = test_and_clear_bit(PG_pinned, &page->flags);

	if (pgfl && !PageHighMem(page)) {
		void *pt = lowmem_page_address(page);
		unsigned long pfn = page_to_pfn(page);
		struct multicall_space mcs = __xen_mc_entry(0);

		MULTI_update_va_mapping(mcs.mc, (unsigned long)pt,
					pfn_pte(pfn, PAGE_KERNEL),
					flags);
	}

	return 0;		/* never need to flush on unpin */
}

/* Release a pagetables pages back as normal RW */
static void xen_pgd_unpin(pgd_t *pgd)
{
	struct mmuext_op *op;
	struct multicall_space mcs;

	xen_mc_batch();

	mcs = __xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = MMUEXT_UNPIN_TABLE;
	op->arg1.mfn = pfn_to_mfn(PFN_DOWN(__pa(pgd)));

	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	pgd_walk(pgd, unpin_page, TASK_SIZE);

	xen_mc_issue(0);
}

void xen_activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	spin_lock(&next->page_table_lock);
	xen_pgd_pin(next->pgd);
	spin_unlock(&next->page_table_lock);
}

void xen_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm)
{
	spin_lock(&mm->page_table_lock);
	xen_pgd_pin(mm->pgd);
	spin_unlock(&mm->page_table_lock);
}


#ifdef CONFIG_SMP
/* Another cpu may still have their %cr3 pointing at the pagetable, so
   we need to repoint it somewhere else before we can unpin it. */
static void drop_other_mm_ref(void *info)
{
	struct mm_struct *mm = info;

	if (__get_cpu_var(cpu_tlbstate).active_mm == mm)
		leave_mm(smp_processor_id());
}

static void drop_mm_ref(struct mm_struct *mm)
{
	if (current->active_mm == mm) {
		if (current->mm == mm)
			load_cr3(swapper_pg_dir);
		else
			leave_mm(smp_processor_id());
	}

	if (!cpus_empty(mm->cpu_vm_mask))
		xen_smp_call_function_mask(mm->cpu_vm_mask, drop_other_mm_ref,
					   mm, 1);
}
#else
static void drop_mm_ref(struct mm_struct *mm)
{
	if (current->active_mm == mm)
		load_cr3(swapper_pg_dir);
}
#endif

/*
 * While a process runs, Xen pins its pagetables, which means that the
 * hypervisor forces it to be read-only, and it controls all updates
 * to it.  This means that all pagetable updates have to go via the
 * hypervisor, which is moderately expensive.
 *
 * Since we're pulling the pagetable down, we switch to use init_mm,
 * unpin old process pagetable and mark it all read-write, which
 * allows further operations on it to be simple memory accesses.
 *
 * The only subtle point is that another CPU may be still using the
 * pagetable because of lazy tlb flushing.  This means we need need to
 * switch all CPUs off this pagetable before we can unpin it.
 */
void xen_exit_mmap(struct mm_struct *mm)
{
	get_cpu();		/* make sure we don't move around */
	drop_mm_ref(mm);
	put_cpu();

	spin_lock(&mm->page_table_lock);

	/* pgd may not be pinned in the error exit path of execve */
	if (PagePinned(virt_to_page(mm->pgd)))
		xen_pgd_unpin(mm->pgd);
	spin_unlock(&mm->page_table_lock);
}
