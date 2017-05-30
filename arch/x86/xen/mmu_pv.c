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
#include <linux/sched/mm.h>
#include <linux/highmem.h>
#include <linux/debugfs.h>
#include <linux/bug.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/crash_dump.h>
#ifdef CONFIG_KEXEC_CORE
#include <linux/kexec.h>
#endif

#include <trace/events/xen.h>

#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/mmu_context.h>
#include <asm/setup.h>
#include <asm/paravirt.h>
#include <asm/e820/api.h>
#include <asm/linkage.h>
#include <asm/page.h>
#include <asm/init.h>
#include <asm/pat.h>
#include <asm/smp.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>

#include <xen/xen.h>
#include <xen/page.h>
#include <xen/interface/xen.h>
#include <xen/interface/hvm/hvm_op.h>
#include <xen/interface/version.h>
#include <xen/interface/memory.h>
#include <xen/hvc-console.h>

#include "multicalls.h"
#include "mmu.h"
#include "debugfs.h"

#ifdef CONFIG_X86_32
/*
 * Identity map, in addition to plain kernel map.  This needs to be
 * large enough to allocate page table pages to allocate the rest.
 * Each page can map 2MB.
 */
#define LEVEL1_IDENT_ENTRIES	(PTRS_PER_PTE * 4)
static RESERVE_BRK_ARRAY(pte_t, level1_ident_pgt, LEVEL1_IDENT_ENTRIES);
#endif
#ifdef CONFIG_X86_64
/* l3 pud for userspace vsyscall mapping */
static pud_t level3_user_vsyscall[PTRS_PER_PUD] __page_aligned_bss;
#endif /* CONFIG_X86_64 */

/*
 * Note about cr3 (pagetable base) values:
 *
 * xen_cr3 contains the current logical cr3 value; it contains the
 * last set cr3.  This may not be the current effective cr3, because
 * its update may be being lazily deferred.  However, a vcpu looking
 * at its own cr3 can use this value knowing that it everything will
 * be self-consistent.
 *
 * xen_current_cr3 contains the actual vcpu cr3; it is set once the
 * hypercall to set the vcpu cr3 is complete (so it may be a little
 * out of date, but it will never be set early).  If one vcpu is
 * looking at another vcpu's cr3 value, it should use this variable.
 */
DEFINE_PER_CPU(unsigned long, xen_cr3);	 /* cr3 stored as physaddr */
DEFINE_PER_CPU(unsigned long, xen_current_cr3);	 /* actual vcpu cr3 */

static phys_addr_t xen_pt_base, xen_pt_size __initdata;

/*
 * Just beyond the highest usermode address.  STACK_TOP_MAX has a
 * redzone above it, so round it up to a PGD boundary.
 */
#define USER_LIMIT	((STACK_TOP_MAX + PGDIR_SIZE - 1) & PGDIR_MASK)

void make_lowmem_page_readonly(void *vaddr)
{
	pte_t *pte, ptev;
	unsigned long address = (unsigned long)vaddr;
	unsigned int level;

	pte = lookup_address(address, &level);
	if (pte == NULL)
		return;		/* vaddr missing */

	ptev = pte_wrprotect(*pte);

	if (HYPERVISOR_update_va_mapping(address, ptev, 0))
		BUG();
}

void make_lowmem_page_readwrite(void *vaddr)
{
	pte_t *pte, ptev;
	unsigned long address = (unsigned long)vaddr;
	unsigned int level;

	pte = lookup_address(address, &level);
	if (pte == NULL)
		return;		/* vaddr missing */

	ptev = pte_mkwrite(*pte);

	if (HYPERVISOR_update_va_mapping(address, ptev, 0))
		BUG();
}


static bool xen_page_pinned(void *ptr)
{
	struct page *page = virt_to_page(ptr);

	return PagePinned(page);
}

void xen_set_domain_pte(pte_t *ptep, pte_t pteval, unsigned domid)
{
	struct multicall_space mcs;
	struct mmu_update *u;

	trace_xen_mmu_set_domain_pte(ptep, pteval, domid);

	mcs = xen_mc_entry(sizeof(*u));
	u = mcs.args;

	/* ptep might be kmapped when using 32-bit HIGHPTE */
	u->ptr = virt_to_machine(ptep).maddr;
	u->val = pte_val_ma(pteval);

	MULTI_mmu_update(mcs.mc, mcs.args, 1, NULL, domid);

	xen_mc_issue(PARAVIRT_LAZY_MMU);
}
EXPORT_SYMBOL_GPL(xen_set_domain_pte);

static void xen_extend_mmu_update(const struct mmu_update *update)
{
	struct multicall_space mcs;
	struct mmu_update *u;

	mcs = xen_mc_extend_args(__HYPERVISOR_mmu_update, sizeof(*u));

	if (mcs.mc != NULL) {
		mcs.mc->args[1]++;
	} else {
		mcs = __xen_mc_entry(sizeof(*u));
		MULTI_mmu_update(mcs.mc, mcs.args, 1, NULL, DOMID_SELF);
	}

	u = mcs.args;
	*u = *update;
}

static void xen_extend_mmuext_op(const struct mmuext_op *op)
{
	struct multicall_space mcs;
	struct mmuext_op *u;

	mcs = xen_mc_extend_args(__HYPERVISOR_mmuext_op, sizeof(*u));

	if (mcs.mc != NULL) {
		mcs.mc->args[1]++;
	} else {
		mcs = __xen_mc_entry(sizeof(*u));
		MULTI_mmuext_op(mcs.mc, mcs.args, 1, NULL, DOMID_SELF);
	}

	u = mcs.args;
	*u = *op;
}

static void xen_set_pmd_hyper(pmd_t *ptr, pmd_t val)
{
	struct mmu_update u;

	preempt_disable();

	xen_mc_batch();

	/* ptr may be ioremapped for 64-bit pagetable setup */
	u.ptr = arbitrary_virt_to_machine(ptr).maddr;
	u.val = pmd_val_ma(val);
	xen_extend_mmu_update(&u);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

static void xen_set_pmd(pmd_t *ptr, pmd_t val)
{
	trace_xen_mmu_set_pmd(ptr, val);

	/* If page is not pinned, we can just update the entry
	   directly */
	if (!xen_page_pinned(ptr)) {
		*ptr = val;
		return;
	}

	xen_set_pmd_hyper(ptr, val);
}

/*
 * Associate a virtual page frame with a given physical page frame
 * and protection flags for that frame.
 */
void set_pte_mfn(unsigned long vaddr, unsigned long mfn, pgprot_t flags)
{
	set_pte_vaddr(vaddr, mfn_pte(mfn, flags));
}

static bool xen_batched_set_pte(pte_t *ptep, pte_t pteval)
{
	struct mmu_update u;

	if (paravirt_get_lazy_mode() != PARAVIRT_LAZY_MMU)
		return false;

	xen_mc_batch();

	u.ptr = virt_to_machine(ptep).maddr | MMU_NORMAL_PT_UPDATE;
	u.val = pte_val_ma(pteval);
	xen_extend_mmu_update(&u);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	return true;
}

static inline void __xen_set_pte(pte_t *ptep, pte_t pteval)
{
	if (!xen_batched_set_pte(ptep, pteval)) {
		/*
		 * Could call native_set_pte() here and trap and
		 * emulate the PTE write but with 32-bit guests this
		 * needs two traps (one for each of the two 32-bit
		 * words in the PTE) so do one hypercall directly
		 * instead.
		 */
		struct mmu_update u;

		u.ptr = virt_to_machine(ptep).maddr | MMU_NORMAL_PT_UPDATE;
		u.val = pte_val_ma(pteval);
		HYPERVISOR_mmu_update(&u, 1, NULL, DOMID_SELF);
	}
}

static void xen_set_pte(pte_t *ptep, pte_t pteval)
{
	trace_xen_mmu_set_pte(ptep, pteval);
	__xen_set_pte(ptep, pteval);
}

static void xen_set_pte_at(struct mm_struct *mm, unsigned long addr,
		    pte_t *ptep, pte_t pteval)
{
	trace_xen_mmu_set_pte_at(mm, addr, ptep, pteval);
	__xen_set_pte(ptep, pteval);
}

pte_t xen_ptep_modify_prot_start(struct mm_struct *mm,
				 unsigned long addr, pte_t *ptep)
{
	/* Just return the pte as-is.  We preserve the bits on commit */
	trace_xen_mmu_ptep_modify_prot_start(mm, addr, ptep, *ptep);
	return *ptep;
}

void xen_ptep_modify_prot_commit(struct mm_struct *mm, unsigned long addr,
				 pte_t *ptep, pte_t pte)
{
	struct mmu_update u;

	trace_xen_mmu_ptep_modify_prot_commit(mm, addr, ptep, pte);
	xen_mc_batch();

	u.ptr = virt_to_machine(ptep).maddr | MMU_PT_UPDATE_PRESERVE_AD;
	u.val = pte_val_ma(pte);
	xen_extend_mmu_update(&u);

	xen_mc_issue(PARAVIRT_LAZY_MMU);
}

/* Assume pteval_t is equivalent to all the other *val_t types. */
static pteval_t pte_mfn_to_pfn(pteval_t val)
{
	if (val & _PAGE_PRESENT) {
		unsigned long mfn = (val & PTE_PFN_MASK) >> PAGE_SHIFT;
		unsigned long pfn = mfn_to_pfn(mfn);

		pteval_t flags = val & PTE_FLAGS_MASK;
		if (unlikely(pfn == ~0))
			val = flags & ~_PAGE_PRESENT;
		else
			val = ((pteval_t)pfn << PAGE_SHIFT) | flags;
	}

	return val;
}

static pteval_t pte_pfn_to_mfn(pteval_t val)
{
	if (val & _PAGE_PRESENT) {
		unsigned long pfn = (val & PTE_PFN_MASK) >> PAGE_SHIFT;
		pteval_t flags = val & PTE_FLAGS_MASK;
		unsigned long mfn;

		mfn = __pfn_to_mfn(pfn);

		/*
		 * If there's no mfn for the pfn, then just create an
		 * empty non-present pte.  Unfortunately this loses
		 * information about the original pfn, so
		 * pte_mfn_to_pfn is asymmetric.
		 */
		if (unlikely(mfn == INVALID_P2M_ENTRY)) {
			mfn = 0;
			flags = 0;
		} else
			mfn &= ~(FOREIGN_FRAME_BIT | IDENTITY_FRAME_BIT);
		val = ((pteval_t)mfn << PAGE_SHIFT) | flags;
	}

	return val;
}

__visible pteval_t xen_pte_val(pte_t pte)
{
	pteval_t pteval = pte.pte;

	return pte_mfn_to_pfn(pteval);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_pte_val);

__visible pgdval_t xen_pgd_val(pgd_t pgd)
{
	return pte_mfn_to_pfn(pgd.pgd);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_pgd_val);

__visible pte_t xen_make_pte(pteval_t pte)
{
	pte = pte_pfn_to_mfn(pte);

	return native_make_pte(pte);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_make_pte);

__visible pgd_t xen_make_pgd(pgdval_t pgd)
{
	pgd = pte_pfn_to_mfn(pgd);
	return native_make_pgd(pgd);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_make_pgd);

__visible pmdval_t xen_pmd_val(pmd_t pmd)
{
	return pte_mfn_to_pfn(pmd.pmd);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_pmd_val);

static void xen_set_pud_hyper(pud_t *ptr, pud_t val)
{
	struct mmu_update u;

	preempt_disable();

	xen_mc_batch();

	/* ptr may be ioremapped for 64-bit pagetable setup */
	u.ptr = arbitrary_virt_to_machine(ptr).maddr;
	u.val = pud_val_ma(val);
	xen_extend_mmu_update(&u);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

static void xen_set_pud(pud_t *ptr, pud_t val)
{
	trace_xen_mmu_set_pud(ptr, val);

	/* If page is not pinned, we can just update the entry
	   directly */
	if (!xen_page_pinned(ptr)) {
		*ptr = val;
		return;
	}

	xen_set_pud_hyper(ptr, val);
}

#ifdef CONFIG_X86_PAE
static void xen_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	trace_xen_mmu_set_pte_atomic(ptep, pte);
	set_64bit((u64 *)ptep, native_pte_val(pte));
}

static void xen_pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	trace_xen_mmu_pte_clear(mm, addr, ptep);
	if (!xen_batched_set_pte(ptep, native_make_pte(0)))
		native_pte_clear(mm, addr, ptep);
}

static void xen_pmd_clear(pmd_t *pmdp)
{
	trace_xen_mmu_pmd_clear(pmdp);
	set_pmd(pmdp, __pmd(0));
}
#endif	/* CONFIG_X86_PAE */

__visible pmd_t xen_make_pmd(pmdval_t pmd)
{
	pmd = pte_pfn_to_mfn(pmd);
	return native_make_pmd(pmd);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_make_pmd);

#if CONFIG_PGTABLE_LEVELS == 4
__visible pudval_t xen_pud_val(pud_t pud)
{
	return pte_mfn_to_pfn(pud.pud);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_pud_val);

__visible pud_t xen_make_pud(pudval_t pud)
{
	pud = pte_pfn_to_mfn(pud);

	return native_make_pud(pud);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_make_pud);

static pgd_t *xen_get_user_pgd(pgd_t *pgd)
{
	pgd_t *pgd_page = (pgd_t *)(((unsigned long)pgd) & PAGE_MASK);
	unsigned offset = pgd - pgd_page;
	pgd_t *user_ptr = NULL;

	if (offset < pgd_index(USER_LIMIT)) {
		struct page *page = virt_to_page(pgd_page);
		user_ptr = (pgd_t *)page->private;
		if (user_ptr)
			user_ptr += offset;
	}

	return user_ptr;
}

static void __xen_set_p4d_hyper(p4d_t *ptr, p4d_t val)
{
	struct mmu_update u;

	u.ptr = virt_to_machine(ptr).maddr;
	u.val = p4d_val_ma(val);
	xen_extend_mmu_update(&u);
}

/*
 * Raw hypercall-based set_p4d, intended for in early boot before
 * there's a page structure.  This implies:
 *  1. The only existing pagetable is the kernel's
 *  2. It is always pinned
 *  3. It has no user pagetable attached to it
 */
static void __init xen_set_p4d_hyper(p4d_t *ptr, p4d_t val)
{
	preempt_disable();

	xen_mc_batch();

	__xen_set_p4d_hyper(ptr, val);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

static void xen_set_p4d(p4d_t *ptr, p4d_t val)
{
	pgd_t *user_ptr = xen_get_user_pgd((pgd_t *)ptr);
	pgd_t pgd_val;

	trace_xen_mmu_set_p4d(ptr, (p4d_t *)user_ptr, val);

	/* If page is not pinned, we can just update the entry
	   directly */
	if (!xen_page_pinned(ptr)) {
		*ptr = val;
		if (user_ptr) {
			WARN_ON(xen_page_pinned(user_ptr));
			pgd_val.pgd = p4d_val_ma(val);
			*user_ptr = pgd_val;
		}
		return;
	}

	/* If it's pinned, then we can at least batch the kernel and
	   user updates together. */
	xen_mc_batch();

	__xen_set_p4d_hyper(ptr, val);
	if (user_ptr)
		__xen_set_p4d_hyper((p4d_t *)user_ptr, val);

	xen_mc_issue(PARAVIRT_LAZY_MMU);
}
#endif	/* CONFIG_PGTABLE_LEVELS == 4 */

static int xen_pmd_walk(struct mm_struct *mm, pmd_t *pmd,
		int (*func)(struct mm_struct *mm, struct page *, enum pt_level),
		bool last, unsigned long limit)
{
	int i, nr, flush = 0;

	nr = last ? pmd_index(limit) + 1 : PTRS_PER_PMD;
	for (i = 0; i < nr; i++) {
		if (!pmd_none(pmd[i]))
			flush |= (*func)(mm, pmd_page(pmd[i]), PT_PTE);
	}
	return flush;
}

static int xen_pud_walk(struct mm_struct *mm, pud_t *pud,
		int (*func)(struct mm_struct *mm, struct page *, enum pt_level),
		bool last, unsigned long limit)
{
	int i, nr, flush = 0;

	nr = last ? pud_index(limit) + 1 : PTRS_PER_PUD;
	for (i = 0; i < nr; i++) {
		pmd_t *pmd;

		if (pud_none(pud[i]))
			continue;

		pmd = pmd_offset(&pud[i], 0);
		if (PTRS_PER_PMD > 1)
			flush |= (*func)(mm, virt_to_page(pmd), PT_PMD);
		flush |= xen_pmd_walk(mm, pmd, func,
				last && i == nr - 1, limit);
	}
	return flush;
}

static int xen_p4d_walk(struct mm_struct *mm, p4d_t *p4d,
		int (*func)(struct mm_struct *mm, struct page *, enum pt_level),
		bool last, unsigned long limit)
{
	int i, nr, flush = 0;

	nr = last ? p4d_index(limit) + 1 : PTRS_PER_P4D;
	for (i = 0; i < nr; i++) {
		pud_t *pud;

		if (p4d_none(p4d[i]))
			continue;

		pud = pud_offset(&p4d[i], 0);
		if (PTRS_PER_PUD > 1)
			flush |= (*func)(mm, virt_to_page(pud), PT_PUD);
		flush |= xen_pud_walk(mm, pud, func,
				last && i == nr - 1, limit);
	}
	return flush;
}

/*
 * (Yet another) pagetable walker.  This one is intended for pinning a
 * pagetable.  This means that it walks a pagetable and calls the
 * callback function on each page it finds making up the page table,
 * at every level.  It walks the entire pagetable, but it only bothers
 * pinning pte pages which are below limit.  In the normal case this
 * will be STACK_TOP_MAX, but at boot we need to pin up to
 * FIXADDR_TOP.
 *
 * For 32-bit the important bit is that we don't pin beyond there,
 * because then we start getting into Xen's ptes.
 *
 * For 64-bit, we must skip the Xen hole in the middle of the address
 * space, just after the big x86-64 virtual hole.
 */
static int __xen_pgd_walk(struct mm_struct *mm, pgd_t *pgd,
			  int (*func)(struct mm_struct *mm, struct page *,
				      enum pt_level),
			  unsigned long limit)
{
	int i, nr, flush = 0;
	unsigned hole_low, hole_high;

	/* The limit is the last byte to be touched */
	limit--;
	BUG_ON(limit >= FIXADDR_TOP);

	/*
	 * 64-bit has a great big hole in the middle of the address
	 * space, which contains the Xen mappings.  On 32-bit these
	 * will end up making a zero-sized hole and so is a no-op.
	 */
	hole_low = pgd_index(USER_LIMIT);
	hole_high = pgd_index(PAGE_OFFSET);

	nr = pgd_index(limit) + 1;
	for (i = 0; i < nr; i++) {
		p4d_t *p4d;

		if (i >= hole_low && i < hole_high)
			continue;

		if (pgd_none(pgd[i]))
			continue;

		p4d = p4d_offset(&pgd[i], 0);
		if (PTRS_PER_P4D > 1)
			flush |= (*func)(mm, virt_to_page(p4d), PT_P4D);
		flush |= xen_p4d_walk(mm, p4d, func, i == nr - 1, limit);
	}

	/* Do the top level last, so that the callbacks can use it as
	   a cue to do final things like tlb flushes. */
	flush |= (*func)(mm, virt_to_page(pgd), PT_PGD);

	return flush;
}

static int xen_pgd_walk(struct mm_struct *mm,
			int (*func)(struct mm_struct *mm, struct page *,
				    enum pt_level),
			unsigned long limit)
{
	return __xen_pgd_walk(mm, mm->pgd, func, limit);
}

/* If we're using split pte locks, then take the page's lock and
   return a pointer to it.  Otherwise return NULL. */
static spinlock_t *xen_pte_lock(struct page *page, struct mm_struct *mm)
{
	spinlock_t *ptl = NULL;

#if USE_SPLIT_PTE_PTLOCKS
	ptl = ptlock_ptr(page);
	spin_lock_nest_lock(ptl, &mm->page_table_lock);
#endif

	return ptl;
}

static void xen_pte_unlock(void *v)
{
	spinlock_t *ptl = v;
	spin_unlock(ptl);
}

static void xen_do_pin(unsigned level, unsigned long pfn)
{
	struct mmuext_op op;

	op.cmd = level;
	op.arg1.mfn = pfn_to_mfn(pfn);

	xen_extend_mmuext_op(&op);
}

static int xen_pin_page(struct mm_struct *mm, struct page *page,
			enum pt_level level)
{
	unsigned pgfl = TestSetPagePinned(page);
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
		spinlock_t *ptl;

		flush = 0;

		/*
		 * We need to hold the pagetable lock between the time
		 * we make the pagetable RO and when we actually pin
		 * it.  If we don't, then other users may come in and
		 * attempt to update the pagetable by writing it,
		 * which will fail because the memory is RO but not
		 * pinned, so Xen won't do the trap'n'emulate.
		 *
		 * If we're using split pte locks, we can't hold the
		 * entire pagetable's worth of locks during the
		 * traverse, because we may wrap the preempt count (8
		 * bits).  The solution is to mark RO and pin each PTE
		 * page while holding the lock.  This means the number
		 * of locks we end up holding is never more than a
		 * batch size (~32 entries, at present).
		 *
		 * If we're not using split pte locks, we needn't pin
		 * the PTE pages independently, because we're
		 * protected by the overall pagetable lock.
		 */
		ptl = NULL;
		if (level == PT_PTE)
			ptl = xen_pte_lock(page, mm);

		MULTI_update_va_mapping(mcs.mc, (unsigned long)pt,
					pfn_pte(pfn, PAGE_KERNEL_RO),
					level == PT_PGD ? UVMF_TLB_FLUSH : 0);

		if (ptl) {
			xen_do_pin(MMUEXT_PIN_L1_TABLE, pfn);

			/* Queue a deferred unlock for when this batch
			   is completed. */
			xen_mc_callback(xen_pte_unlock, ptl);
		}
	}

	return flush;
}

/* This is called just after a mm has been created, but it has not
   been used yet.  We need to make sure that its pagetable is all
   read-only, and can be pinned. */
static void __xen_pgd_pin(struct mm_struct *mm, pgd_t *pgd)
{
	trace_xen_mmu_pgd_pin(mm, pgd);

	xen_mc_batch();

	if (__xen_pgd_walk(mm, pgd, xen_pin_page, USER_LIMIT)) {
		/* re-enable interrupts for flushing */
		xen_mc_issue(0);

		kmap_flush_unused();

		xen_mc_batch();
	}

#ifdef CONFIG_X86_64
	{
		pgd_t *user_pgd = xen_get_user_pgd(pgd);

		xen_do_pin(MMUEXT_PIN_L4_TABLE, PFN_DOWN(__pa(pgd)));

		if (user_pgd) {
			xen_pin_page(mm, virt_to_page(user_pgd), PT_PGD);
			xen_do_pin(MMUEXT_PIN_L4_TABLE,
				   PFN_DOWN(__pa(user_pgd)));
		}
	}
#else /* CONFIG_X86_32 */
#ifdef CONFIG_X86_PAE
	/* Need to make sure unshared kernel PMD is pinnable */
	xen_pin_page(mm, pgd_page(pgd[pgd_index(TASK_SIZE)]),
		     PT_PMD);
#endif
	xen_do_pin(MMUEXT_PIN_L3_TABLE, PFN_DOWN(__pa(pgd)));
#endif /* CONFIG_X86_64 */
	xen_mc_issue(0);
}

static void xen_pgd_pin(struct mm_struct *mm)
{
	__xen_pgd_pin(mm, mm->pgd);
}

/*
 * On save, we need to pin all pagetables to make sure they get their
 * mfns turned into pfns.  Search the list for any unpinned pgds and pin
 * them (unpinned pgds are not currently in use, probably because the
 * process is under construction or destruction).
 *
 * Expected to be called in stop_machine() ("equivalent to taking
 * every spinlock in the system"), so the locking doesn't really
 * matter all that much.
 */
void xen_mm_pin_all(void)
{
	struct page *page;

	spin_lock(&pgd_lock);

	list_for_each_entry(page, &pgd_list, lru) {
		if (!PagePinned(page)) {
			__xen_pgd_pin(&init_mm, (pgd_t *)page_address(page));
			SetPageSavePinned(page);
		}
	}

	spin_unlock(&pgd_lock);
}

/*
 * The init_mm pagetable is really pinned as soon as its created, but
 * that's before we have page structures to store the bits.  So do all
 * the book-keeping now.
 */
static int __init xen_mark_pinned(struct mm_struct *mm, struct page *page,
				  enum pt_level level)
{
	SetPagePinned(page);
	return 0;
}

static void __init xen_mark_init_mm_pinned(void)
{
	xen_pgd_walk(&init_mm, xen_mark_pinned, FIXADDR_TOP);
}

static int xen_unpin_page(struct mm_struct *mm, struct page *page,
			  enum pt_level level)
{
	unsigned pgfl = TestClearPagePinned(page);

	if (pgfl && !PageHighMem(page)) {
		void *pt = lowmem_page_address(page);
		unsigned long pfn = page_to_pfn(page);
		spinlock_t *ptl = NULL;
		struct multicall_space mcs;

		/*
		 * Do the converse to pin_page.  If we're using split
		 * pte locks, we must be holding the lock for while
		 * the pte page is unpinned but still RO to prevent
		 * concurrent updates from seeing it in this
		 * partially-pinned state.
		 */
		if (level == PT_PTE) {
			ptl = xen_pte_lock(page, mm);

			if (ptl)
				xen_do_pin(MMUEXT_UNPIN_TABLE, pfn);
		}

		mcs = __xen_mc_entry(0);

		MULTI_update_va_mapping(mcs.mc, (unsigned long)pt,
					pfn_pte(pfn, PAGE_KERNEL),
					level == PT_PGD ? UVMF_TLB_FLUSH : 0);

		if (ptl) {
			/* unlock when batch completed */
			xen_mc_callback(xen_pte_unlock, ptl);
		}
	}

	return 0;		/* never need to flush on unpin */
}

/* Release a pagetables pages back as normal RW */
static void __xen_pgd_unpin(struct mm_struct *mm, pgd_t *pgd)
{
	trace_xen_mmu_pgd_unpin(mm, pgd);

	xen_mc_batch();

	xen_do_pin(MMUEXT_UNPIN_TABLE, PFN_DOWN(__pa(pgd)));

#ifdef CONFIG_X86_64
	{
		pgd_t *user_pgd = xen_get_user_pgd(pgd);

		if (user_pgd) {
			xen_do_pin(MMUEXT_UNPIN_TABLE,
				   PFN_DOWN(__pa(user_pgd)));
			xen_unpin_page(mm, virt_to_page(user_pgd), PT_PGD);
		}
	}
#endif

#ifdef CONFIG_X86_PAE
	/* Need to make sure unshared kernel PMD is unpinned */
	xen_unpin_page(mm, pgd_page(pgd[pgd_index(TASK_SIZE)]),
		       PT_PMD);
#endif

	__xen_pgd_walk(mm, pgd, xen_unpin_page, USER_LIMIT);

	xen_mc_issue(0);
}

static void xen_pgd_unpin(struct mm_struct *mm)
{
	__xen_pgd_unpin(mm, mm->pgd);
}

/*
 * On resume, undo any pinning done at save, so that the rest of the
 * kernel doesn't see any unexpected pinned pagetables.
 */
void xen_mm_unpin_all(void)
{
	struct page *page;

	spin_lock(&pgd_lock);

	list_for_each_entry(page, &pgd_list, lru) {
		if (PageSavePinned(page)) {
			BUG_ON(!PagePinned(page));
			__xen_pgd_unpin(&init_mm, (pgd_t *)page_address(page));
			ClearPageSavePinned(page);
		}
	}

	spin_unlock(&pgd_lock);
}

static void xen_activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	spin_lock(&next->page_table_lock);
	xen_pgd_pin(next);
	spin_unlock(&next->page_table_lock);
}

static void xen_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm)
{
	spin_lock(&mm->page_table_lock);
	xen_pgd_pin(mm);
	spin_unlock(&mm->page_table_lock);
}


#ifdef CONFIG_SMP
/* Another cpu may still have their %cr3 pointing at the pagetable, so
   we need to repoint it somewhere else before we can unpin it. */
static void drop_other_mm_ref(void *info)
{
	struct mm_struct *mm = info;
	struct mm_struct *active_mm;

	active_mm = this_cpu_read(cpu_tlbstate.active_mm);

	if (active_mm == mm && this_cpu_read(cpu_tlbstate.state) != TLBSTATE_OK)
		leave_mm(smp_processor_id());

	/* If this cpu still has a stale cr3 reference, then make sure
	   it has been flushed. */
	if (this_cpu_read(xen_current_cr3) == __pa(mm->pgd))
		load_cr3(swapper_pg_dir);
}

static void xen_drop_mm_ref(struct mm_struct *mm)
{
	cpumask_var_t mask;
	unsigned cpu;

	if (current->active_mm == mm) {
		if (current->mm == mm)
			load_cr3(swapper_pg_dir);
		else
			leave_mm(smp_processor_id());
	}

	/* Get the "official" set of cpus referring to our pagetable. */
	if (!alloc_cpumask_var(&mask, GFP_ATOMIC)) {
		for_each_online_cpu(cpu) {
			if (!cpumask_test_cpu(cpu, mm_cpumask(mm))
			    && per_cpu(xen_current_cr3, cpu) != __pa(mm->pgd))
				continue;
			smp_call_function_single(cpu, drop_other_mm_ref, mm, 1);
		}
		return;
	}
	cpumask_copy(mask, mm_cpumask(mm));

	/* It's possible that a vcpu may have a stale reference to our
	   cr3, because its in lazy mode, and it hasn't yet flushed
	   its set of pending hypercalls yet.  In this case, we can
	   look at its actual current cr3 value, and force it to flush
	   if needed. */
	for_each_online_cpu(cpu) {
		if (per_cpu(xen_current_cr3, cpu) == __pa(mm->pgd))
			cpumask_set_cpu(cpu, mask);
	}

	if (!cpumask_empty(mask))
		smp_call_function_many(mask, drop_other_mm_ref, mm, 1);
	free_cpumask_var(mask);
}
#else
static void xen_drop_mm_ref(struct mm_struct *mm)
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
static void xen_exit_mmap(struct mm_struct *mm)
{
	get_cpu();		/* make sure we don't move around */
	xen_drop_mm_ref(mm);
	put_cpu();

	spin_lock(&mm->page_table_lock);

	/* pgd may not be pinned in the error exit path of execve */
	if (xen_page_pinned(mm->pgd))
		xen_pgd_unpin(mm);

	spin_unlock(&mm->page_table_lock);
}

static void xen_post_allocator_init(void);

static void __init pin_pagetable_pfn(unsigned cmd, unsigned long pfn)
{
	struct mmuext_op op;

	op.cmd = cmd;
	op.arg1.mfn = pfn_to_mfn(pfn);
	if (HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF))
		BUG();
}

#ifdef CONFIG_X86_64
static void __init xen_cleanhighmap(unsigned long vaddr,
				    unsigned long vaddr_end)
{
	unsigned long kernel_end = roundup((unsigned long)_brk_end, PMD_SIZE) - 1;
	pmd_t *pmd = level2_kernel_pgt + pmd_index(vaddr);

	/* NOTE: The loop is more greedy than the cleanup_highmap variant.
	 * We include the PMD passed in on _both_ boundaries. */
	for (; vaddr <= vaddr_end && (pmd < (level2_kernel_pgt + PTRS_PER_PMD));
			pmd++, vaddr += PMD_SIZE) {
		if (pmd_none(*pmd))
			continue;
		if (vaddr < (unsigned long) _text || vaddr > kernel_end)
			set_pmd(pmd, __pmd(0));
	}
	/* In case we did something silly, we should crash in this function
	 * instead of somewhere later and be confusing. */
	xen_mc_flush();
}

/*
 * Make a page range writeable and free it.
 */
static void __init xen_free_ro_pages(unsigned long paddr, unsigned long size)
{
	void *vaddr = __va(paddr);
	void *vaddr_end = vaddr + size;

	for (; vaddr < vaddr_end; vaddr += PAGE_SIZE)
		make_lowmem_page_readwrite(vaddr);

	memblock_free(paddr, size);
}

static void __init xen_cleanmfnmap_free_pgtbl(void *pgtbl, bool unpin)
{
	unsigned long pa = __pa(pgtbl) & PHYSICAL_PAGE_MASK;

	if (unpin)
		pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, PFN_DOWN(pa));
	ClearPagePinned(virt_to_page(__va(pa)));
	xen_free_ro_pages(pa, PAGE_SIZE);
}

static void __init xen_cleanmfnmap_pmd(pmd_t *pmd, bool unpin)
{
	unsigned long pa;
	pte_t *pte_tbl;
	int i;

	if (pmd_large(*pmd)) {
		pa = pmd_val(*pmd) & PHYSICAL_PAGE_MASK;
		xen_free_ro_pages(pa, PMD_SIZE);
		return;
	}

	pte_tbl = pte_offset_kernel(pmd, 0);
	for (i = 0; i < PTRS_PER_PTE; i++) {
		if (pte_none(pte_tbl[i]))
			continue;
		pa = pte_pfn(pte_tbl[i]) << PAGE_SHIFT;
		xen_free_ro_pages(pa, PAGE_SIZE);
	}
	set_pmd(pmd, __pmd(0));
	xen_cleanmfnmap_free_pgtbl(pte_tbl, unpin);
}

static void __init xen_cleanmfnmap_pud(pud_t *pud, bool unpin)
{
	unsigned long pa;
	pmd_t *pmd_tbl;
	int i;

	if (pud_large(*pud)) {
		pa = pud_val(*pud) & PHYSICAL_PAGE_MASK;
		xen_free_ro_pages(pa, PUD_SIZE);
		return;
	}

	pmd_tbl = pmd_offset(pud, 0);
	for (i = 0; i < PTRS_PER_PMD; i++) {
		if (pmd_none(pmd_tbl[i]))
			continue;
		xen_cleanmfnmap_pmd(pmd_tbl + i, unpin);
	}
	set_pud(pud, __pud(0));
	xen_cleanmfnmap_free_pgtbl(pmd_tbl, unpin);
}

static void __init xen_cleanmfnmap_p4d(p4d_t *p4d, bool unpin)
{
	unsigned long pa;
	pud_t *pud_tbl;
	int i;

	if (p4d_large(*p4d)) {
		pa = p4d_val(*p4d) & PHYSICAL_PAGE_MASK;
		xen_free_ro_pages(pa, P4D_SIZE);
		return;
	}

	pud_tbl = pud_offset(p4d, 0);
	for (i = 0; i < PTRS_PER_PUD; i++) {
		if (pud_none(pud_tbl[i]))
			continue;
		xen_cleanmfnmap_pud(pud_tbl + i, unpin);
	}
	set_p4d(p4d, __p4d(0));
	xen_cleanmfnmap_free_pgtbl(pud_tbl, unpin);
}

/*
 * Since it is well isolated we can (and since it is perhaps large we should)
 * also free the page tables mapping the initial P->M table.
 */
static void __init xen_cleanmfnmap(unsigned long vaddr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	unsigned int i;
	bool unpin;

	unpin = (vaddr == 2 * PGDIR_SIZE);
	vaddr &= PMD_MASK;
	pgd = pgd_offset_k(vaddr);
	p4d = p4d_offset(pgd, 0);
	for (i = 0; i < PTRS_PER_P4D; i++) {
		if (p4d_none(p4d[i]))
			continue;
		xen_cleanmfnmap_p4d(p4d + i, unpin);
	}
	if (IS_ENABLED(CONFIG_X86_5LEVEL)) {
		set_pgd(pgd, __pgd(0));
		xen_cleanmfnmap_free_pgtbl(p4d, unpin);
	}
}

static void __init xen_pagetable_p2m_free(void)
{
	unsigned long size;
	unsigned long addr;

	size = PAGE_ALIGN(xen_start_info->nr_pages * sizeof(unsigned long));

	/* No memory or already called. */
	if ((unsigned long)xen_p2m_addr == xen_start_info->mfn_list)
		return;

	/* using __ka address and sticking INVALID_P2M_ENTRY! */
	memset((void *)xen_start_info->mfn_list, 0xff, size);

	addr = xen_start_info->mfn_list;
	/*
	 * We could be in __ka space.
	 * We roundup to the PMD, which means that if anybody at this stage is
	 * using the __ka address of xen_start_info or
	 * xen_start_info->shared_info they are in going to crash. Fortunatly
	 * we have already revectored in xen_setup_kernel_pagetable and in
	 * xen_setup_shared_info.
	 */
	size = roundup(size, PMD_SIZE);

	if (addr >= __START_KERNEL_map) {
		xen_cleanhighmap(addr, addr + size);
		size = PAGE_ALIGN(xen_start_info->nr_pages *
				  sizeof(unsigned long));
		memblock_free(__pa(addr), size);
	} else {
		xen_cleanmfnmap(addr);
	}
}

static void __init xen_pagetable_cleanhighmap(void)
{
	unsigned long size;
	unsigned long addr;

	/* At this stage, cleanup_highmap has already cleaned __ka space
	 * from _brk_limit way up to the max_pfn_mapped (which is the end of
	 * the ramdisk). We continue on, erasing PMD entries that point to page
	 * tables - do note that they are accessible at this stage via __va.
	 * For good measure we also round up to the PMD - which means that if
	 * anybody is using __ka address to the initial boot-stack - and try
	 * to use it - they are going to crash. The xen_start_info has been
	 * taken care of already in xen_setup_kernel_pagetable. */
	addr = xen_start_info->pt_base;
	size = roundup(xen_start_info->nr_pt_frames * PAGE_SIZE, PMD_SIZE);

	xen_cleanhighmap(addr, addr + size);
	xen_start_info->pt_base = (unsigned long)__va(__pa(xen_start_info->pt_base));
#ifdef DEBUG
	/* This is superfluous and is not necessary, but you know what
	 * lets do it. The MODULES_VADDR -> MODULES_END should be clear of
	 * anything at this stage. */
	xen_cleanhighmap(MODULES_VADDR, roundup(MODULES_VADDR, PUD_SIZE) - 1);
#endif
}
#endif

static void __init xen_pagetable_p2m_setup(void)
{
	xen_vmalloc_p2m_tree();

#ifdef CONFIG_X86_64
	xen_pagetable_p2m_free();

	xen_pagetable_cleanhighmap();
#endif
	/* And revector! Bye bye old array */
	xen_start_info->mfn_list = (unsigned long)xen_p2m_addr;
}

static void __init xen_pagetable_init(void)
{
	paging_init();
	xen_post_allocator_init();

	xen_pagetable_p2m_setup();

	/* Allocate and initialize top and mid mfn levels for p2m structure */
	xen_build_mfn_list_list();

	/* Remap memory freed due to conflicts with E820 map */
	xen_remap_memory();

	xen_setup_shared_info();
}
static void xen_write_cr2(unsigned long cr2)
{
	this_cpu_read(xen_vcpu)->arch.cr2 = cr2;
}

static unsigned long xen_read_cr2(void)
{
	return this_cpu_read(xen_vcpu)->arch.cr2;
}

unsigned long xen_read_cr2_direct(void)
{
	return this_cpu_read(xen_vcpu_info.arch.cr2);
}

static void xen_flush_tlb(void)
{
	struct mmuext_op *op;
	struct multicall_space mcs;

	trace_xen_mmu_flush_tlb(0);

	preempt_disable();

	mcs = xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = MMUEXT_TLB_FLUSH_LOCAL;
	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

static void xen_flush_tlb_single(unsigned long addr)
{
	struct mmuext_op *op;
	struct multicall_space mcs;

	trace_xen_mmu_flush_tlb_single(addr);

	preempt_disable();

	mcs = xen_mc_entry(sizeof(*op));
	op = mcs.args;
	op->cmd = MMUEXT_INVLPG_LOCAL;
	op->arg1.linear_addr = addr & PAGE_MASK;
	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

static void xen_flush_tlb_others(const struct cpumask *cpus,
				 struct mm_struct *mm, unsigned long start,
				 unsigned long end)
{
	struct {
		struct mmuext_op op;
#ifdef CONFIG_SMP
		DECLARE_BITMAP(mask, num_processors);
#else
		DECLARE_BITMAP(mask, NR_CPUS);
#endif
	} *args;
	struct multicall_space mcs;

	trace_xen_mmu_flush_tlb_others(cpus, mm, start, end);

	if (cpumask_empty(cpus))
		return;		/* nothing to do */

	mcs = xen_mc_entry(sizeof(*args));
	args = mcs.args;
	args->op.arg2.vcpumask = to_cpumask(args->mask);

	/* Remove us, and any offline CPUS. */
	cpumask_and(to_cpumask(args->mask), cpus, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), to_cpumask(args->mask));

	args->op.cmd = MMUEXT_TLB_FLUSH_MULTI;
	if (end != TLB_FLUSH_ALL && (end - start) <= PAGE_SIZE) {
		args->op.cmd = MMUEXT_INVLPG_MULTI;
		args->op.arg1.linear_addr = start;
	}

	MULTI_mmuext_op(mcs.mc, &args->op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);
}

static unsigned long xen_read_cr3(void)
{
	return this_cpu_read(xen_cr3);
}

static void set_current_cr3(void *v)
{
	this_cpu_write(xen_current_cr3, (unsigned long)v);
}

static void __xen_write_cr3(bool kernel, unsigned long cr3)
{
	struct mmuext_op op;
	unsigned long mfn;

	trace_xen_mmu_write_cr3(kernel, cr3);

	if (cr3)
		mfn = pfn_to_mfn(PFN_DOWN(cr3));
	else
		mfn = 0;

	WARN_ON(mfn == 0 && kernel);

	op.cmd = kernel ? MMUEXT_NEW_BASEPTR : MMUEXT_NEW_USER_BASEPTR;
	op.arg1.mfn = mfn;

	xen_extend_mmuext_op(&op);

	if (kernel) {
		this_cpu_write(xen_cr3, cr3);

		/* Update xen_current_cr3 once the batch has actually
		   been submitted. */
		xen_mc_callback(set_current_cr3, (void *)cr3);
	}
}
static void xen_write_cr3(unsigned long cr3)
{
	BUG_ON(preemptible());

	xen_mc_batch();  /* disables interrupts */

	/* Update while interrupts are disabled, so its atomic with
	   respect to ipis */
	this_cpu_write(xen_cr3, cr3);

	__xen_write_cr3(true, cr3);

#ifdef CONFIG_X86_64
	{
		pgd_t *user_pgd = xen_get_user_pgd(__va(cr3));
		if (user_pgd)
			__xen_write_cr3(false, __pa(user_pgd));
		else
			__xen_write_cr3(false, 0);
	}
#endif

	xen_mc_issue(PARAVIRT_LAZY_CPU);  /* interrupts restored */
}

#ifdef CONFIG_X86_64
/*
 * At the start of the day - when Xen launches a guest, it has already
 * built pagetables for the guest. We diligently look over them
 * in xen_setup_kernel_pagetable and graft as appropriate them in the
 * init_level4_pgt and its friends. Then when we are happy we load
 * the new init_level4_pgt - and continue on.
 *
 * The generic code starts (start_kernel) and 'init_mem_mapping' sets
 * up the rest of the pagetables. When it has completed it loads the cr3.
 * N.B. that baremetal would start at 'start_kernel' (and the early
 * #PF handler would create bootstrap pagetables) - so we are running
 * with the same assumptions as what to do when write_cr3 is executed
 * at this point.
 *
 * Since there are no user-page tables at all, we have two variants
 * of xen_write_cr3 - the early bootup (this one), and the late one
 * (xen_write_cr3). The reason we have to do that is that in 64-bit
 * the Linux kernel and user-space are both in ring 3 while the
 * hypervisor is in ring 0.
 */
static void __init xen_write_cr3_init(unsigned long cr3)
{
	BUG_ON(preemptible());

	xen_mc_batch();  /* disables interrupts */

	/* Update while interrupts are disabled, so its atomic with
	   respect to ipis */
	this_cpu_write(xen_cr3, cr3);

	__xen_write_cr3(true, cr3);

	xen_mc_issue(PARAVIRT_LAZY_CPU);  /* interrupts restored */
}
#endif

static int xen_pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = mm->pgd;
	int ret = 0;

	BUG_ON(PagePinned(virt_to_page(pgd)));

#ifdef CONFIG_X86_64
	{
		struct page *page = virt_to_page(pgd);
		pgd_t *user_pgd;

		BUG_ON(page->private != 0);

		ret = -ENOMEM;

		user_pgd = (pgd_t *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
		page->private = (unsigned long)user_pgd;

		if (user_pgd != NULL) {
#ifdef CONFIG_X86_VSYSCALL_EMULATION
			user_pgd[pgd_index(VSYSCALL_ADDR)] =
				__pgd(__pa(level3_user_vsyscall) | _PAGE_TABLE);
#endif
			ret = 0;
		}

		BUG_ON(PagePinned(virt_to_page(xen_get_user_pgd(pgd))));
	}
#endif
	return ret;
}

static void xen_pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
#ifdef CONFIG_X86_64
	pgd_t *user_pgd = xen_get_user_pgd(pgd);

	if (user_pgd)
		free_page((unsigned long)user_pgd);
#endif
}

/*
 * Init-time set_pte while constructing initial pagetables, which
 * doesn't allow RO page table pages to be remapped RW.
 *
 * If there is no MFN for this PFN then this page is initially
 * ballooned out so clear the PTE (as in decrease_reservation() in
 * drivers/xen/balloon.c).
 *
 * Many of these PTE updates are done on unpinned and writable pages
 * and doing a hypercall for these is unnecessary and expensive.  At
 * this point it is not possible to tell if a page is pinned or not,
 * so always write the PTE directly and rely on Xen trapping and
 * emulating any updates as necessary.
 */
__visible pte_t xen_make_pte_init(pteval_t pte)
{
#ifdef CONFIG_X86_64
	unsigned long pfn;

	/*
	 * Pages belonging to the initial p2m list mapped outside the default
	 * address range must be mapped read-only. This region contains the
	 * page tables for mapping the p2m list, too, and page tables MUST be
	 * mapped read-only.
	 */
	pfn = (pte & PTE_PFN_MASK) >> PAGE_SHIFT;
	if (xen_start_info->mfn_list < __START_KERNEL_map &&
	    pfn >= xen_start_info->first_p2m_pfn &&
	    pfn < xen_start_info->first_p2m_pfn + xen_start_info->nr_p2m_frames)
		pte &= ~_PAGE_RW;
#endif
	pte = pte_pfn_to_mfn(pte);
	return native_make_pte(pte);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_make_pte_init);

static void __init xen_set_pte_init(pte_t *ptep, pte_t pte)
{
#ifdef CONFIG_X86_32
	/* If there's an existing pte, then don't allow _PAGE_RW to be set */
	if (pte_mfn(pte) != INVALID_P2M_ENTRY
	    && pte_val_ma(*ptep) & _PAGE_PRESENT)
		pte = __pte_ma(((pte_val_ma(*ptep) & _PAGE_RW) | ~_PAGE_RW) &
			       pte_val_ma(pte));
#endif
	native_set_pte(ptep, pte);
}

/* Early in boot, while setting up the initial pagetable, assume
   everything is pinned. */
static void __init xen_alloc_pte_init(struct mm_struct *mm, unsigned long pfn)
{
#ifdef CONFIG_FLATMEM
	BUG_ON(mem_map);	/* should only be used early */
#endif
	make_lowmem_page_readonly(__va(PFN_PHYS(pfn)));
	pin_pagetable_pfn(MMUEXT_PIN_L1_TABLE, pfn);
}

/* Used for pmd and pud */
static void __init xen_alloc_pmd_init(struct mm_struct *mm, unsigned long pfn)
{
#ifdef CONFIG_FLATMEM
	BUG_ON(mem_map);	/* should only be used early */
#endif
	make_lowmem_page_readonly(__va(PFN_PHYS(pfn)));
}

/* Early release_pte assumes that all pts are pinned, since there's
   only init_mm and anything attached to that is pinned. */
static void __init xen_release_pte_init(unsigned long pfn)
{
	pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, pfn);
	make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
}

static void __init xen_release_pmd_init(unsigned long pfn)
{
	make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
}

static inline void __pin_pagetable_pfn(unsigned cmd, unsigned long pfn)
{
	struct multicall_space mcs;
	struct mmuext_op *op;

	mcs = __xen_mc_entry(sizeof(*op));
	op = mcs.args;
	op->cmd = cmd;
	op->arg1.mfn = pfn_to_mfn(pfn);

	MULTI_mmuext_op(mcs.mc, mcs.args, 1, NULL, DOMID_SELF);
}

static inline void __set_pfn_prot(unsigned long pfn, pgprot_t prot)
{
	struct multicall_space mcs;
	unsigned long addr = (unsigned long)__va(pfn << PAGE_SHIFT);

	mcs = __xen_mc_entry(0);
	MULTI_update_va_mapping(mcs.mc, (unsigned long)addr,
				pfn_pte(pfn, prot), 0);
}

/* This needs to make sure the new pte page is pinned iff its being
   attached to a pinned pagetable. */
static inline void xen_alloc_ptpage(struct mm_struct *mm, unsigned long pfn,
				    unsigned level)
{
	bool pinned = PagePinned(virt_to_page(mm->pgd));

	trace_xen_mmu_alloc_ptpage(mm, pfn, level, pinned);

	if (pinned) {
		struct page *page = pfn_to_page(pfn);

		SetPagePinned(page);

		if (!PageHighMem(page)) {
			xen_mc_batch();

			__set_pfn_prot(pfn, PAGE_KERNEL_RO);

			if (level == PT_PTE && USE_SPLIT_PTE_PTLOCKS)
				__pin_pagetable_pfn(MMUEXT_PIN_L1_TABLE, pfn);

			xen_mc_issue(PARAVIRT_LAZY_MMU);
		} else {
			/* make sure there are no stray mappings of
			   this page */
			kmap_flush_unused();
		}
	}
}

static void xen_alloc_pte(struct mm_struct *mm, unsigned long pfn)
{
	xen_alloc_ptpage(mm, pfn, PT_PTE);
}

static void xen_alloc_pmd(struct mm_struct *mm, unsigned long pfn)
{
	xen_alloc_ptpage(mm, pfn, PT_PMD);
}

/* This should never happen until we're OK to use struct page */
static inline void xen_release_ptpage(unsigned long pfn, unsigned level)
{
	struct page *page = pfn_to_page(pfn);
	bool pinned = PagePinned(page);

	trace_xen_mmu_release_ptpage(pfn, level, pinned);

	if (pinned) {
		if (!PageHighMem(page)) {
			xen_mc_batch();

			if (level == PT_PTE && USE_SPLIT_PTE_PTLOCKS)
				__pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, pfn);

			__set_pfn_prot(pfn, PAGE_KERNEL);

			xen_mc_issue(PARAVIRT_LAZY_MMU);
		}
		ClearPagePinned(page);
	}
}

static void xen_release_pte(unsigned long pfn)
{
	xen_release_ptpage(pfn, PT_PTE);
}

static void xen_release_pmd(unsigned long pfn)
{
	xen_release_ptpage(pfn, PT_PMD);
}

#if CONFIG_PGTABLE_LEVELS >= 4
static void xen_alloc_pud(struct mm_struct *mm, unsigned long pfn)
{
	xen_alloc_ptpage(mm, pfn, PT_PUD);
}

static void xen_release_pud(unsigned long pfn)
{
	xen_release_ptpage(pfn, PT_PUD);
}
#endif

void __init xen_reserve_top(void)
{
#ifdef CONFIG_X86_32
	unsigned long top = HYPERVISOR_VIRT_START;
	struct xen_platform_parameters pp;

	if (HYPERVISOR_xen_version(XENVER_platform_parameters, &pp) == 0)
		top = pp.virt_start;

	reserve_top_address(-top);
#endif	/* CONFIG_X86_32 */
}

/*
 * Like __va(), but returns address in the kernel mapping (which is
 * all we have until the physical memory mapping has been set up.
 */
static void * __init __ka(phys_addr_t paddr)
{
#ifdef CONFIG_X86_64
	return (void *)(paddr + __START_KERNEL_map);
#else
	return __va(paddr);
#endif
}

/* Convert a machine address to physical address */
static unsigned long __init m2p(phys_addr_t maddr)
{
	phys_addr_t paddr;

	maddr &= PTE_PFN_MASK;
	paddr = mfn_to_pfn(maddr >> PAGE_SHIFT) << PAGE_SHIFT;

	return paddr;
}

/* Convert a machine address to kernel virtual */
static void * __init m2v(phys_addr_t maddr)
{
	return __ka(m2p(maddr));
}

/* Set the page permissions on an identity-mapped pages */
static void __init set_page_prot_flags(void *addr, pgprot_t prot,
				       unsigned long flags)
{
	unsigned long pfn = __pa(addr) >> PAGE_SHIFT;
	pte_t pte = pfn_pte(pfn, prot);

	if (HYPERVISOR_update_va_mapping((unsigned long)addr, pte, flags))
		BUG();
}
static void __init set_page_prot(void *addr, pgprot_t prot)
{
	return set_page_prot_flags(addr, prot, UVMF_NONE);
}
#ifdef CONFIG_X86_32
static void __init xen_map_identity_early(pmd_t *pmd, unsigned long max_pfn)
{
	unsigned pmdidx, pteidx;
	unsigned ident_pte;
	unsigned long pfn;

	level1_ident_pgt = extend_brk(sizeof(pte_t) * LEVEL1_IDENT_ENTRIES,
				      PAGE_SIZE);

	ident_pte = 0;
	pfn = 0;
	for (pmdidx = 0; pmdidx < PTRS_PER_PMD && pfn < max_pfn; pmdidx++) {
		pte_t *pte_page;

		/* Reuse or allocate a page of ptes */
		if (pmd_present(pmd[pmdidx]))
			pte_page = m2v(pmd[pmdidx].pmd);
		else {
			/* Check for free pte pages */
			if (ident_pte == LEVEL1_IDENT_ENTRIES)
				break;

			pte_page = &level1_ident_pgt[ident_pte];
			ident_pte += PTRS_PER_PTE;

			pmd[pmdidx] = __pmd(__pa(pte_page) | _PAGE_TABLE);
		}

		/* Install mappings */
		for (pteidx = 0; pteidx < PTRS_PER_PTE; pteidx++, pfn++) {
			pte_t pte;

			if (pfn > max_pfn_mapped)
				max_pfn_mapped = pfn;

			if (!pte_none(pte_page[pteidx]))
				continue;

			pte = pfn_pte(pfn, PAGE_KERNEL_EXEC);
			pte_page[pteidx] = pte;
		}
	}

	for (pteidx = 0; pteidx < ident_pte; pteidx += PTRS_PER_PTE)
		set_page_prot(&level1_ident_pgt[pteidx], PAGE_KERNEL_RO);

	set_page_prot(pmd, PAGE_KERNEL_RO);
}
#endif
void __init xen_setup_machphys_mapping(void)
{
	struct xen_machphys_mapping mapping;

	if (HYPERVISOR_memory_op(XENMEM_machphys_mapping, &mapping) == 0) {
		machine_to_phys_mapping = (unsigned long *)mapping.v_start;
		machine_to_phys_nr = mapping.max_mfn + 1;
	} else {
		machine_to_phys_nr = MACH2PHYS_NR_ENTRIES;
	}
#ifdef CONFIG_X86_32
	WARN_ON((machine_to_phys_mapping + (machine_to_phys_nr - 1))
		< machine_to_phys_mapping);
#endif
}

#ifdef CONFIG_X86_64
static void __init convert_pfn_mfn(void *v)
{
	pte_t *pte = v;
	int i;

	/* All levels are converted the same way, so just treat them
	   as ptes. */
	for (i = 0; i < PTRS_PER_PTE; i++)
		pte[i] = xen_make_pte(pte[i].pte);
}
static void __init check_pt_base(unsigned long *pt_base, unsigned long *pt_end,
				 unsigned long addr)
{
	if (*pt_base == PFN_DOWN(__pa(addr))) {
		set_page_prot_flags((void *)addr, PAGE_KERNEL, UVMF_INVLPG);
		clear_page((void *)addr);
		(*pt_base)++;
	}
	if (*pt_end == PFN_DOWN(__pa(addr))) {
		set_page_prot_flags((void *)addr, PAGE_KERNEL, UVMF_INVLPG);
		clear_page((void *)addr);
		(*pt_end)--;
	}
}
/*
 * Set up the initial kernel pagetable.
 *
 * We can construct this by grafting the Xen provided pagetable into
 * head_64.S's preconstructed pagetables.  We copy the Xen L2's into
 * level2_ident_pgt, and level2_kernel_pgt.  This means that only the
 * kernel has a physical mapping to start with - but that's enough to
 * get __va working.  We need to fill in the rest of the physical
 * mapping once some sort of allocator has been set up.
 */
void __init xen_setup_kernel_pagetable(pgd_t *pgd, unsigned long max_pfn)
{
	pud_t *l3;
	pmd_t *l2;
	unsigned long addr[3];
	unsigned long pt_base, pt_end;
	unsigned i;

	/* max_pfn_mapped is the last pfn mapped in the initial memory
	 * mappings. Considering that on Xen after the kernel mappings we
	 * have the mappings of some pages that don't exist in pfn space, we
	 * set max_pfn_mapped to the last real pfn mapped. */
	if (xen_start_info->mfn_list < __START_KERNEL_map)
		max_pfn_mapped = xen_start_info->first_p2m_pfn;
	else
		max_pfn_mapped = PFN_DOWN(__pa(xen_start_info->mfn_list));

	pt_base = PFN_DOWN(__pa(xen_start_info->pt_base));
	pt_end = pt_base + xen_start_info->nr_pt_frames;

	/* Zap identity mapping */
	init_level4_pgt[0] = __pgd(0);

	/* Pre-constructed entries are in pfn, so convert to mfn */
	/* L4[272] -> level3_ident_pgt  */
	/* L4[511] -> level3_kernel_pgt */
	convert_pfn_mfn(init_level4_pgt);

	/* L3_i[0] -> level2_ident_pgt */
	convert_pfn_mfn(level3_ident_pgt);
	/* L3_k[510] -> level2_kernel_pgt */
	/* L3_k[511] -> level2_fixmap_pgt */
	convert_pfn_mfn(level3_kernel_pgt);

	/* L3_k[511][506] -> level1_fixmap_pgt */
	convert_pfn_mfn(level2_fixmap_pgt);

	/* We get [511][511] and have Xen's version of level2_kernel_pgt */
	l3 = m2v(pgd[pgd_index(__START_KERNEL_map)].pgd);
	l2 = m2v(l3[pud_index(__START_KERNEL_map)].pud);

	addr[0] = (unsigned long)pgd;
	addr[1] = (unsigned long)l3;
	addr[2] = (unsigned long)l2;
	/* Graft it onto L4[272][0]. Note that we creating an aliasing problem:
	 * Both L4[272][0] and L4[511][510] have entries that point to the same
	 * L2 (PMD) tables. Meaning that if you modify it in __va space
	 * it will be also modified in the __ka space! (But if you just
	 * modify the PMD table to point to other PTE's or none, then you
	 * are OK - which is what cleanup_highmap does) */
	copy_page(level2_ident_pgt, l2);
	/* Graft it onto L4[511][510] */
	copy_page(level2_kernel_pgt, l2);

	/* Copy the initial P->M table mappings if necessary. */
	i = pgd_index(xen_start_info->mfn_list);
	if (i && i < pgd_index(__START_KERNEL_map))
		init_level4_pgt[i] = ((pgd_t *)xen_start_info->pt_base)[i];

	/* Make pagetable pieces RO */
	set_page_prot(init_level4_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_ident_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_kernel_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_user_vsyscall, PAGE_KERNEL_RO);
	set_page_prot(level2_ident_pgt, PAGE_KERNEL_RO);
	set_page_prot(level2_kernel_pgt, PAGE_KERNEL_RO);
	set_page_prot(level2_fixmap_pgt, PAGE_KERNEL_RO);
	set_page_prot(level1_fixmap_pgt, PAGE_KERNEL_RO);

	/* Pin down new L4 */
	pin_pagetable_pfn(MMUEXT_PIN_L4_TABLE,
			  PFN_DOWN(__pa_symbol(init_level4_pgt)));

	/* Unpin Xen-provided one */
	pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, PFN_DOWN(__pa(pgd)));

	/*
	 * At this stage there can be no user pgd, and no page structure to
	 * attach it to, so make sure we just set kernel pgd.
	 */
	xen_mc_batch();
	__xen_write_cr3(true, __pa(init_level4_pgt));
	xen_mc_issue(PARAVIRT_LAZY_CPU);

	/* We can't that easily rip out L3 and L2, as the Xen pagetables are
	 * set out this way: [L4], [L1], [L2], [L3], [L1], [L1] ...  for
	 * the initial domain. For guests using the toolstack, they are in:
	 * [L4], [L3], [L2], [L1], [L1], order .. So for dom0 we can only
	 * rip out the [L4] (pgd), but for guests we shave off three pages.
	 */
	for (i = 0; i < ARRAY_SIZE(addr); i++)
		check_pt_base(&pt_base, &pt_end, addr[i]);

	/* Our (by three pages) smaller Xen pagetable that we are using */
	xen_pt_base = PFN_PHYS(pt_base);
	xen_pt_size = (pt_end - pt_base) * PAGE_SIZE;
	memblock_reserve(xen_pt_base, xen_pt_size);

	/* Revector the xen_start_info */
	xen_start_info = (struct start_info *)__va(__pa(xen_start_info));
}

/*
 * Read a value from a physical address.
 */
static unsigned long __init xen_read_phys_ulong(phys_addr_t addr)
{
	unsigned long *vaddr;
	unsigned long val;

	vaddr = early_memremap_ro(addr, sizeof(val));
	val = *vaddr;
	early_memunmap(vaddr, sizeof(val));
	return val;
}

/*
 * Translate a virtual address to a physical one without relying on mapped
 * page tables. Don't rely on big pages being aligned in (guest) physical
 * space!
 */
static phys_addr_t __init xen_early_virt_to_phys(unsigned long vaddr)
{
	phys_addr_t pa;
	pgd_t pgd;
	pud_t pud;
	pmd_t pmd;
	pte_t pte;

	pa = read_cr3();
	pgd = native_make_pgd(xen_read_phys_ulong(pa + pgd_index(vaddr) *
						       sizeof(pgd)));
	if (!pgd_present(pgd))
		return 0;

	pa = pgd_val(pgd) & PTE_PFN_MASK;
	pud = native_make_pud(xen_read_phys_ulong(pa + pud_index(vaddr) *
						       sizeof(pud)));
	if (!pud_present(pud))
		return 0;
	pa = pud_val(pud) & PTE_PFN_MASK;
	if (pud_large(pud))
		return pa + (vaddr & ~PUD_MASK);

	pmd = native_make_pmd(xen_read_phys_ulong(pa + pmd_index(vaddr) *
						       sizeof(pmd)));
	if (!pmd_present(pmd))
		return 0;
	pa = pmd_val(pmd) & PTE_PFN_MASK;
	if (pmd_large(pmd))
		return pa + (vaddr & ~PMD_MASK);

	pte = native_make_pte(xen_read_phys_ulong(pa + pte_index(vaddr) *
						       sizeof(pte)));
	if (!pte_present(pte))
		return 0;
	pa = pte_pfn(pte) << PAGE_SHIFT;

	return pa | (vaddr & ~PAGE_MASK);
}

/*
 * Find a new area for the hypervisor supplied p2m list and relocate the p2m to
 * this area.
 */
void __init xen_relocate_p2m(void)
{
	phys_addr_t size, new_area, pt_phys, pmd_phys, pud_phys, p4d_phys;
	unsigned long p2m_pfn, p2m_pfn_end, n_frames, pfn, pfn_end;
	int n_pte, n_pt, n_pmd, n_pud, n_p4d, idx_pte, idx_pt, idx_pmd, idx_pud, idx_p4d;
	pte_t *pt;
	pmd_t *pmd;
	pud_t *pud;
	p4d_t *p4d = NULL;
	pgd_t *pgd;
	unsigned long *new_p2m;
	int save_pud;

	size = PAGE_ALIGN(xen_start_info->nr_pages * sizeof(unsigned long));
	n_pte = roundup(size, PAGE_SIZE) >> PAGE_SHIFT;
	n_pt = roundup(size, PMD_SIZE) >> PMD_SHIFT;
	n_pmd = roundup(size, PUD_SIZE) >> PUD_SHIFT;
	n_pud = roundup(size, P4D_SIZE) >> P4D_SHIFT;
	if (PTRS_PER_P4D > 1)
		n_p4d = roundup(size, PGDIR_SIZE) >> PGDIR_SHIFT;
	else
		n_p4d = 0;
	n_frames = n_pte + n_pt + n_pmd + n_pud + n_p4d;

	new_area = xen_find_free_area(PFN_PHYS(n_frames));
	if (!new_area) {
		xen_raw_console_write("Can't find new memory area for p2m needed due to E820 map conflict\n");
		BUG();
	}

	/*
	 * Setup the page tables for addressing the new p2m list.
	 * We have asked the hypervisor to map the p2m list at the user address
	 * PUD_SIZE. It may have done so, or it may have used a kernel space
	 * address depending on the Xen version.
	 * To avoid any possible virtual address collision, just use
	 * 2 * PUD_SIZE for the new area.
	 */
	p4d_phys = new_area;
	pud_phys = p4d_phys + PFN_PHYS(n_p4d);
	pmd_phys = pud_phys + PFN_PHYS(n_pud);
	pt_phys = pmd_phys + PFN_PHYS(n_pmd);
	p2m_pfn = PFN_DOWN(pt_phys) + n_pt;

	pgd = __va(read_cr3());
	new_p2m = (unsigned long *)(2 * PGDIR_SIZE);
	idx_p4d = 0;
	save_pud = n_pud;
	do {
		if (n_p4d > 0) {
			p4d = early_memremap(p4d_phys, PAGE_SIZE);
			clear_page(p4d);
			n_pud = min(save_pud, PTRS_PER_P4D);
		}
		for (idx_pud = 0; idx_pud < n_pud; idx_pud++) {
			pud = early_memremap(pud_phys, PAGE_SIZE);
			clear_page(pud);
			for (idx_pmd = 0; idx_pmd < min(n_pmd, PTRS_PER_PUD);
				 idx_pmd++) {
				pmd = early_memremap(pmd_phys, PAGE_SIZE);
				clear_page(pmd);
				for (idx_pt = 0; idx_pt < min(n_pt, PTRS_PER_PMD);
					 idx_pt++) {
					pt = early_memremap(pt_phys, PAGE_SIZE);
					clear_page(pt);
					for (idx_pte = 0;
						 idx_pte < min(n_pte, PTRS_PER_PTE);
						 idx_pte++) {
						set_pte(pt + idx_pte,
								pfn_pte(p2m_pfn, PAGE_KERNEL));
						p2m_pfn++;
					}
					n_pte -= PTRS_PER_PTE;
					early_memunmap(pt, PAGE_SIZE);
					make_lowmem_page_readonly(__va(pt_phys));
					pin_pagetable_pfn(MMUEXT_PIN_L1_TABLE,
							PFN_DOWN(pt_phys));
					set_pmd(pmd + idx_pt,
							__pmd(_PAGE_TABLE | pt_phys));
					pt_phys += PAGE_SIZE;
				}
				n_pt -= PTRS_PER_PMD;
				early_memunmap(pmd, PAGE_SIZE);
				make_lowmem_page_readonly(__va(pmd_phys));
				pin_pagetable_pfn(MMUEXT_PIN_L2_TABLE,
						PFN_DOWN(pmd_phys));
				set_pud(pud + idx_pmd, __pud(_PAGE_TABLE | pmd_phys));
				pmd_phys += PAGE_SIZE;
			}
			n_pmd -= PTRS_PER_PUD;
			early_memunmap(pud, PAGE_SIZE);
			make_lowmem_page_readonly(__va(pud_phys));
			pin_pagetable_pfn(MMUEXT_PIN_L3_TABLE, PFN_DOWN(pud_phys));
			if (n_p4d > 0)
				set_p4d(p4d + idx_pud, __p4d(_PAGE_TABLE | pud_phys));
			else
				set_pgd(pgd + 2 + idx_pud, __pgd(_PAGE_TABLE | pud_phys));
			pud_phys += PAGE_SIZE;
		}
		if (n_p4d > 0) {
			save_pud -= PTRS_PER_P4D;
			early_memunmap(p4d, PAGE_SIZE);
			make_lowmem_page_readonly(__va(p4d_phys));
			pin_pagetable_pfn(MMUEXT_PIN_L4_TABLE, PFN_DOWN(p4d_phys));
			set_pgd(pgd + 2 + idx_p4d, __pgd(_PAGE_TABLE | p4d_phys));
			p4d_phys += PAGE_SIZE;
		}
	} while (++idx_p4d < n_p4d);

	/* Now copy the old p2m info to the new area. */
	memcpy(new_p2m, xen_p2m_addr, size);
	xen_p2m_addr = new_p2m;

	/* Release the old p2m list and set new list info. */
	p2m_pfn = PFN_DOWN(xen_early_virt_to_phys(xen_start_info->mfn_list));
	BUG_ON(!p2m_pfn);
	p2m_pfn_end = p2m_pfn + PFN_DOWN(size);

	if (xen_start_info->mfn_list < __START_KERNEL_map) {
		pfn = xen_start_info->first_p2m_pfn;
		pfn_end = xen_start_info->first_p2m_pfn +
			  xen_start_info->nr_p2m_frames;
		set_pgd(pgd + 1, __pgd(0));
	} else {
		pfn = p2m_pfn;
		pfn_end = p2m_pfn_end;
	}

	memblock_free(PFN_PHYS(pfn), PAGE_SIZE * (pfn_end - pfn));
	while (pfn < pfn_end) {
		if (pfn == p2m_pfn) {
			pfn = p2m_pfn_end;
			continue;
		}
		make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
		pfn++;
	}

	xen_start_info->mfn_list = (unsigned long)xen_p2m_addr;
	xen_start_info->first_p2m_pfn =  PFN_DOWN(new_area);
	xen_start_info->nr_p2m_frames = n_frames;
}

#else	/* !CONFIG_X86_64 */
static RESERVE_BRK_ARRAY(pmd_t, initial_kernel_pmd, PTRS_PER_PMD);
static RESERVE_BRK_ARRAY(pmd_t, swapper_kernel_pmd, PTRS_PER_PMD);

static void __init xen_write_cr3_init(unsigned long cr3)
{
	unsigned long pfn = PFN_DOWN(__pa(swapper_pg_dir));

	BUG_ON(read_cr3() != __pa(initial_page_table));
	BUG_ON(cr3 != __pa(swapper_pg_dir));

	/*
	 * We are switching to swapper_pg_dir for the first time (from
	 * initial_page_table) and therefore need to mark that page
	 * read-only and then pin it.
	 *
	 * Xen disallows sharing of kernel PMDs for PAE
	 * guests. Therefore we must copy the kernel PMD from
	 * initial_page_table into a new kernel PMD to be used in
	 * swapper_pg_dir.
	 */
	swapper_kernel_pmd =
		extend_brk(sizeof(pmd_t) * PTRS_PER_PMD, PAGE_SIZE);
	copy_page(swapper_kernel_pmd, initial_kernel_pmd);
	swapper_pg_dir[KERNEL_PGD_BOUNDARY] =
		__pgd(__pa(swapper_kernel_pmd) | _PAGE_PRESENT);
	set_page_prot(swapper_kernel_pmd, PAGE_KERNEL_RO);

	set_page_prot(swapper_pg_dir, PAGE_KERNEL_RO);
	xen_write_cr3(cr3);
	pin_pagetable_pfn(MMUEXT_PIN_L3_TABLE, pfn);

	pin_pagetable_pfn(MMUEXT_UNPIN_TABLE,
			  PFN_DOWN(__pa(initial_page_table)));
	set_page_prot(initial_page_table, PAGE_KERNEL);
	set_page_prot(initial_kernel_pmd, PAGE_KERNEL);

	pv_mmu_ops.write_cr3 = &xen_write_cr3;
}

/*
 * For 32 bit domains xen_start_info->pt_base is the pgd address which might be
 * not the first page table in the page table pool.
 * Iterate through the initial page tables to find the real page table base.
 */
static phys_addr_t xen_find_pt_base(pmd_t *pmd)
{
	phys_addr_t pt_base, paddr;
	unsigned pmdidx;

	pt_base = min(__pa(xen_start_info->pt_base), __pa(pmd));

	for (pmdidx = 0; pmdidx < PTRS_PER_PMD; pmdidx++)
		if (pmd_present(pmd[pmdidx]) && !pmd_large(pmd[pmdidx])) {
			paddr = m2p(pmd[pmdidx].pmd);
			pt_base = min(pt_base, paddr);
		}

	return pt_base;
}

void __init xen_setup_kernel_pagetable(pgd_t *pgd, unsigned long max_pfn)
{
	pmd_t *kernel_pmd;

	kernel_pmd = m2v(pgd[KERNEL_PGD_BOUNDARY].pgd);

	xen_pt_base = xen_find_pt_base(kernel_pmd);
	xen_pt_size = xen_start_info->nr_pt_frames * PAGE_SIZE;

	initial_kernel_pmd =
		extend_brk(sizeof(pmd_t) * PTRS_PER_PMD, PAGE_SIZE);

	max_pfn_mapped = PFN_DOWN(xen_pt_base + xen_pt_size + 512 * 1024);

	copy_page(initial_kernel_pmd, kernel_pmd);

	xen_map_identity_early(initial_kernel_pmd, max_pfn);

	copy_page(initial_page_table, pgd);
	initial_page_table[KERNEL_PGD_BOUNDARY] =
		__pgd(__pa(initial_kernel_pmd) | _PAGE_PRESENT);

	set_page_prot(initial_kernel_pmd, PAGE_KERNEL_RO);
	set_page_prot(initial_page_table, PAGE_KERNEL_RO);
	set_page_prot(empty_zero_page, PAGE_KERNEL_RO);

	pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, PFN_DOWN(__pa(pgd)));

	pin_pagetable_pfn(MMUEXT_PIN_L3_TABLE,
			  PFN_DOWN(__pa(initial_page_table)));
	xen_write_cr3(__pa(initial_page_table));

	memblock_reserve(xen_pt_base, xen_pt_size);
}
#endif	/* CONFIG_X86_64 */

void __init xen_reserve_special_pages(void)
{
	phys_addr_t paddr;

	memblock_reserve(__pa(xen_start_info), PAGE_SIZE);
	if (xen_start_info->store_mfn) {
		paddr = PFN_PHYS(mfn_to_pfn(xen_start_info->store_mfn));
		memblock_reserve(paddr, PAGE_SIZE);
	}
	if (!xen_initial_domain()) {
		paddr = PFN_PHYS(mfn_to_pfn(xen_start_info->console.domU.mfn));
		memblock_reserve(paddr, PAGE_SIZE);
	}
}

void __init xen_pt_check_e820(void)
{
	if (xen_is_e820_reserved(xen_pt_base, xen_pt_size)) {
		xen_raw_console_write("Xen hypervisor allocated page table memory conflicts with E820 map\n");
		BUG();
	}
}

static unsigned char dummy_mapping[PAGE_SIZE] __page_aligned_bss;

static void xen_set_fixmap(unsigned idx, phys_addr_t phys, pgprot_t prot)
{
	pte_t pte;

	phys >>= PAGE_SHIFT;

	switch (idx) {
	case FIX_BTMAP_END ... FIX_BTMAP_BEGIN:
	case FIX_RO_IDT:
#ifdef CONFIG_X86_32
	case FIX_WP_TEST:
# ifdef CONFIG_HIGHMEM
	case FIX_KMAP_BEGIN ... FIX_KMAP_END:
# endif
#elif defined(CONFIG_X86_VSYSCALL_EMULATION)
	case VSYSCALL_PAGE:
#endif
	case FIX_TEXT_POKE0:
	case FIX_TEXT_POKE1:
	case FIX_GDT_REMAP_BEGIN ... FIX_GDT_REMAP_END:
		/* All local page mappings */
		pte = pfn_pte(phys, prot);
		break;

#ifdef CONFIG_X86_LOCAL_APIC
	case FIX_APIC_BASE:	/* maps dummy local APIC */
		pte = pfn_pte(PFN_DOWN(__pa(dummy_mapping)), PAGE_KERNEL);
		break;
#endif

#ifdef CONFIG_X86_IO_APIC
	case FIX_IO_APIC_BASE_0 ... FIX_IO_APIC_BASE_END:
		/*
		 * We just don't map the IO APIC - all access is via
		 * hypercalls.  Keep the address in the pte for reference.
		 */
		pte = pfn_pte(PFN_DOWN(__pa(dummy_mapping)), PAGE_KERNEL);
		break;
#endif

	case FIX_PARAVIRT_BOOTMAP:
		/* This is an MFN, but it isn't an IO mapping from the
		   IO domain */
		pte = mfn_pte(phys, prot);
		break;

	default:
		/* By default, set_fixmap is used for hardware mappings */
		pte = mfn_pte(phys, prot);
		break;
	}

	__native_set_fixmap(idx, pte);

#ifdef CONFIG_X86_VSYSCALL_EMULATION
	/* Replicate changes to map the vsyscall page into the user
	   pagetable vsyscall mapping. */
	if (idx == VSYSCALL_PAGE) {
		unsigned long vaddr = __fix_to_virt(idx);
		set_pte_vaddr_pud(level3_user_vsyscall, vaddr, pte);
	}
#endif
}

static void __init xen_post_allocator_init(void)
{
	pv_mmu_ops.set_pte = xen_set_pte;
	pv_mmu_ops.set_pmd = xen_set_pmd;
	pv_mmu_ops.set_pud = xen_set_pud;
#if CONFIG_PGTABLE_LEVELS >= 4
	pv_mmu_ops.set_p4d = xen_set_p4d;
#endif

	/* This will work as long as patching hasn't happened yet
	   (which it hasn't) */
	pv_mmu_ops.alloc_pte = xen_alloc_pte;
	pv_mmu_ops.alloc_pmd = xen_alloc_pmd;
	pv_mmu_ops.release_pte = xen_release_pte;
	pv_mmu_ops.release_pmd = xen_release_pmd;
#if CONFIG_PGTABLE_LEVELS >= 4
	pv_mmu_ops.alloc_pud = xen_alloc_pud;
	pv_mmu_ops.release_pud = xen_release_pud;
#endif
	pv_mmu_ops.make_pte = PV_CALLEE_SAVE(xen_make_pte);

#ifdef CONFIG_X86_64
	pv_mmu_ops.write_cr3 = &xen_write_cr3;
	SetPagePinned(virt_to_page(level3_user_vsyscall));
#endif
	xen_mark_init_mm_pinned();
}

static void xen_leave_lazy_mmu(void)
{
	preempt_disable();
	xen_mc_flush();
	paravirt_leave_lazy_mmu();
	preempt_enable();
}

static const struct pv_mmu_ops xen_mmu_ops __initconst = {
	.read_cr2 = xen_read_cr2,
	.write_cr2 = xen_write_cr2,

	.read_cr3 = xen_read_cr3,
	.write_cr3 = xen_write_cr3_init,

	.flush_tlb_user = xen_flush_tlb,
	.flush_tlb_kernel = xen_flush_tlb,
	.flush_tlb_single = xen_flush_tlb_single,
	.flush_tlb_others = xen_flush_tlb_others,

	.pte_update = paravirt_nop,

	.pgd_alloc = xen_pgd_alloc,
	.pgd_free = xen_pgd_free,

	.alloc_pte = xen_alloc_pte_init,
	.release_pte = xen_release_pte_init,
	.alloc_pmd = xen_alloc_pmd_init,
	.release_pmd = xen_release_pmd_init,

	.set_pte = xen_set_pte_init,
	.set_pte_at = xen_set_pte_at,
	.set_pmd = xen_set_pmd_hyper,

	.ptep_modify_prot_start = __ptep_modify_prot_start,
	.ptep_modify_prot_commit = __ptep_modify_prot_commit,

	.pte_val = PV_CALLEE_SAVE(xen_pte_val),
	.pgd_val = PV_CALLEE_SAVE(xen_pgd_val),

	.make_pte = PV_CALLEE_SAVE(xen_make_pte_init),
	.make_pgd = PV_CALLEE_SAVE(xen_make_pgd),

#ifdef CONFIG_X86_PAE
	.set_pte_atomic = xen_set_pte_atomic,
	.pte_clear = xen_pte_clear,
	.pmd_clear = xen_pmd_clear,
#endif	/* CONFIG_X86_PAE */
	.set_pud = xen_set_pud_hyper,

	.make_pmd = PV_CALLEE_SAVE(xen_make_pmd),
	.pmd_val = PV_CALLEE_SAVE(xen_pmd_val),

#if CONFIG_PGTABLE_LEVELS >= 4
	.pud_val = PV_CALLEE_SAVE(xen_pud_val),
	.make_pud = PV_CALLEE_SAVE(xen_make_pud),
	.set_p4d = xen_set_p4d_hyper,

	.alloc_pud = xen_alloc_pmd_init,
	.release_pud = xen_release_pmd_init,
#endif	/* CONFIG_PGTABLE_LEVELS == 4 */

	.activate_mm = xen_activate_mm,
	.dup_mmap = xen_dup_mmap,
	.exit_mmap = xen_exit_mmap,

	.lazy_mode = {
		.enter = paravirt_enter_lazy_mmu,
		.leave = xen_leave_lazy_mmu,
		.flush = paravirt_flush_lazy_mmu,
	},

	.set_fixmap = xen_set_fixmap,
};

void __init xen_init_mmu_ops(void)
{
	x86_init.paging.pagetable_init = xen_pagetable_init;

	pv_mmu_ops = xen_mmu_ops;

	memset(dummy_mapping, 0xff, PAGE_SIZE);
}

/* Protected by xen_reservation_lock. */
#define MAX_CONTIG_ORDER 9 /* 2MB */
static unsigned long discontig_frames[1<<MAX_CONTIG_ORDER];

#define VOID_PTE (mfn_pte(0, __pgprot(0)))
static void xen_zap_pfn_range(unsigned long vaddr, unsigned int order,
				unsigned long *in_frames,
				unsigned long *out_frames)
{
	int i;
	struct multicall_space mcs;

	xen_mc_batch();
	for (i = 0; i < (1UL<<order); i++, vaddr += PAGE_SIZE) {
		mcs = __xen_mc_entry(0);

		if (in_frames)
			in_frames[i] = virt_to_mfn(vaddr);

		MULTI_update_va_mapping(mcs.mc, vaddr, VOID_PTE, 0);
		__set_phys_to_machine(virt_to_pfn(vaddr), INVALID_P2M_ENTRY);

		if (out_frames)
			out_frames[i] = virt_to_pfn(vaddr);
	}
	xen_mc_issue(0);
}

/*
 * Update the pfn-to-mfn mappings for a virtual address range, either to
 * point to an array of mfns, or contiguously from a single starting
 * mfn.
 */
static void xen_remap_exchanged_ptes(unsigned long vaddr, int order,
				     unsigned long *mfns,
				     unsigned long first_mfn)
{
	unsigned i, limit;
	unsigned long mfn;

	xen_mc_batch();

	limit = 1u << order;
	for (i = 0; i < limit; i++, vaddr += PAGE_SIZE) {
		struct multicall_space mcs;
		unsigned flags;

		mcs = __xen_mc_entry(0);
		if (mfns)
			mfn = mfns[i];
		else
			mfn = first_mfn + i;

		if (i < (limit - 1))
			flags = 0;
		else {
			if (order == 0)
				flags = UVMF_INVLPG | UVMF_ALL;
			else
				flags = UVMF_TLB_FLUSH | UVMF_ALL;
		}

		MULTI_update_va_mapping(mcs.mc, vaddr,
				mfn_pte(mfn, PAGE_KERNEL), flags);

		set_phys_to_machine(virt_to_pfn(vaddr), mfn);
	}

	xen_mc_issue(0);
}

/*
 * Perform the hypercall to exchange a region of our pfns to point to
 * memory with the required contiguous alignment.  Takes the pfns as
 * input, and populates mfns as output.
 *
 * Returns a success code indicating whether the hypervisor was able to
 * satisfy the request or not.
 */
static int xen_exchange_memory(unsigned long extents_in, unsigned int order_in,
			       unsigned long *pfns_in,
			       unsigned long extents_out,
			       unsigned int order_out,
			       unsigned long *mfns_out,
			       unsigned int address_bits)
{
	long rc;
	int success;

	struct xen_memory_exchange exchange = {
		.in = {
			.nr_extents   = extents_in,
			.extent_order = order_in,
			.extent_start = pfns_in,
			.domid        = DOMID_SELF
		},
		.out = {
			.nr_extents   = extents_out,
			.extent_order = order_out,
			.extent_start = mfns_out,
			.address_bits = address_bits,
			.domid        = DOMID_SELF
		}
	};

	BUG_ON(extents_in << order_in != extents_out << order_out);

	rc = HYPERVISOR_memory_op(XENMEM_exchange, &exchange);
	success = (exchange.nr_exchanged == extents_in);

	BUG_ON(!success && ((exchange.nr_exchanged != 0) || (rc == 0)));
	BUG_ON(success && (rc != 0));

	return success;
}

int xen_create_contiguous_region(phys_addr_t pstart, unsigned int order,
				 unsigned int address_bits,
				 dma_addr_t *dma_handle)
{
	unsigned long *in_frames = discontig_frames, out_frame;
	unsigned long  flags;
	int            success;
	unsigned long vstart = (unsigned long)phys_to_virt(pstart);

	/*
	 * Currently an auto-translated guest will not perform I/O, nor will
	 * it require PAE page directories below 4GB. Therefore any calls to
	 * this function are redundant and can be ignored.
	 */

	if (unlikely(order > MAX_CONTIG_ORDER))
		return -ENOMEM;

	memset((void *) vstart, 0, PAGE_SIZE << order);

	spin_lock_irqsave(&xen_reservation_lock, flags);

	/* 1. Zap current PTEs, remembering MFNs. */
	xen_zap_pfn_range(vstart, order, in_frames, NULL);

	/* 2. Get a new contiguous memory extent. */
	out_frame = virt_to_pfn(vstart);
	success = xen_exchange_memory(1UL << order, 0, in_frames,
				      1, order, &out_frame,
				      address_bits);

	/* 3. Map the new extent in place of old pages. */
	if (success)
		xen_remap_exchanged_ptes(vstart, order, NULL, out_frame);
	else
		xen_remap_exchanged_ptes(vstart, order, in_frames, 0);

	spin_unlock_irqrestore(&xen_reservation_lock, flags);

	*dma_handle = virt_to_machine(vstart).maddr;
	return success ? 0 : -ENOMEM;
}
EXPORT_SYMBOL_GPL(xen_create_contiguous_region);

void xen_destroy_contiguous_region(phys_addr_t pstart, unsigned int order)
{
	unsigned long *out_frames = discontig_frames, in_frame;
	unsigned long  flags;
	int success;
	unsigned long vstart;

	if (unlikely(order > MAX_CONTIG_ORDER))
		return;

	vstart = (unsigned long)phys_to_virt(pstart);
	memset((void *) vstart, 0, PAGE_SIZE << order);

	spin_lock_irqsave(&xen_reservation_lock, flags);

	/* 1. Find start MFN of contiguous extent. */
	in_frame = virt_to_mfn(vstart);

	/* 2. Zap current PTEs. */
	xen_zap_pfn_range(vstart, order, NULL, out_frames);

	/* 3. Do the exchange for non-contiguous MFNs. */
	success = xen_exchange_memory(1, order, &in_frame, 1UL << order,
					0, out_frames, 0);

	/* 4. Map new pages in place of old pages. */
	if (success)
		xen_remap_exchanged_ptes(vstart, order, out_frames, 0);
	else
		xen_remap_exchanged_ptes(vstart, order, NULL, in_frame);

	spin_unlock_irqrestore(&xen_reservation_lock, flags);
}
EXPORT_SYMBOL_GPL(xen_destroy_contiguous_region);

#ifdef CONFIG_KEXEC_CORE
phys_addr_t paddr_vmcoreinfo_note(void)
{
	if (xen_pv_domain())
		return virt_to_machine(&vmcoreinfo_note).maddr;
	else
		return __pa_symbol(&vmcoreinfo_note);
}
#endif /* CONFIG_KEXEC_CORE */
