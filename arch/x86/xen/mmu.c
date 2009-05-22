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
#include <linux/debugfs.h>
#include <linux/bug.h>
#include <linux/module.h>

#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/mmu_context.h>
#include <asm/setup.h>
#include <asm/paravirt.h>
#include <asm/linkage.h>

#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>

#include <xen/page.h>
#include <xen/interface/xen.h>
#include <xen/interface/version.h>
#include <xen/hvc-console.h>

#include "multicalls.h"
#include "mmu.h"
#include "debugfs.h"

#define MMU_UPDATE_HISTO	30

#ifdef CONFIG_XEN_DEBUG_FS

static struct {
	u32 pgd_update;
	u32 pgd_update_pinned;
	u32 pgd_update_batched;

	u32 pud_update;
	u32 pud_update_pinned;
	u32 pud_update_batched;

	u32 pmd_update;
	u32 pmd_update_pinned;
	u32 pmd_update_batched;

	u32 pte_update;
	u32 pte_update_pinned;
	u32 pte_update_batched;

	u32 mmu_update;
	u32 mmu_update_extended;
	u32 mmu_update_histo[MMU_UPDATE_HISTO];

	u32 prot_commit;
	u32 prot_commit_batched;

	u32 set_pte_at;
	u32 set_pte_at_batched;
	u32 set_pte_at_pinned;
	u32 set_pte_at_current;
	u32 set_pte_at_kernel;
} mmu_stats;

static u8 zero_stats;

static inline void check_zero(void)
{
	if (unlikely(zero_stats)) {
		memset(&mmu_stats, 0, sizeof(mmu_stats));
		zero_stats = 0;
	}
}

#define ADD_STATS(elem, val)			\
	do { check_zero(); mmu_stats.elem += (val); } while(0)

#else  /* !CONFIG_XEN_DEBUG_FS */

#define ADD_STATS(elem, val)	do { (void)(val); } while(0)

#endif /* CONFIG_XEN_DEBUG_FS */


/*
 * Identity map, in addition to plain kernel map.  This needs to be
 * large enough to allocate page table pages to allocate the rest.
 * Each page can map 2MB.
 */
static pte_t level1_ident_pgt[PTRS_PER_PTE * 4] __page_aligned_bss;

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


/*
 * Just beyond the highest usermode address.  STACK_TOP_MAX has a
 * redzone above it, so round it up to a PGD boundary.
 */
#define USER_LIMIT	((STACK_TOP_MAX + PGDIR_SIZE - 1) & PGDIR_MASK)


#define P2M_ENTRIES_PER_PAGE	(PAGE_SIZE / sizeof(unsigned long))
#define TOP_ENTRIES		(MAX_DOMAIN_PAGES / P2M_ENTRIES_PER_PAGE)

/* Placeholder for holes in the address space */
static unsigned long p2m_missing[P2M_ENTRIES_PER_PAGE] __page_aligned_data =
		{ [ 0 ... P2M_ENTRIES_PER_PAGE-1 ] = ~0UL };

 /* Array of pointers to pages containing p2m entries */
static unsigned long *p2m_top[TOP_ENTRIES] __page_aligned_data =
		{ [ 0 ... TOP_ENTRIES - 1] = &p2m_missing[0] };

/* Arrays of p2m arrays expressed in mfns used for save/restore */
static unsigned long p2m_top_mfn[TOP_ENTRIES] __page_aligned_bss;

static unsigned long p2m_top_mfn_list[TOP_ENTRIES / P2M_ENTRIES_PER_PAGE]
	__page_aligned_bss;

static inline unsigned p2m_top_index(unsigned long pfn)
{
	BUG_ON(pfn >= MAX_DOMAIN_PAGES);
	return pfn / P2M_ENTRIES_PER_PAGE;
}

static inline unsigned p2m_index(unsigned long pfn)
{
	return pfn % P2M_ENTRIES_PER_PAGE;
}

/* Build the parallel p2m_top_mfn structures */
static void __init xen_build_mfn_list_list(void)
{
	unsigned pfn, idx;

	for (pfn = 0; pfn < MAX_DOMAIN_PAGES; pfn += P2M_ENTRIES_PER_PAGE) {
		unsigned topidx = p2m_top_index(pfn);

		p2m_top_mfn[topidx] = virt_to_mfn(p2m_top[topidx]);
	}

	for (idx = 0; idx < ARRAY_SIZE(p2m_top_mfn_list); idx++) {
		unsigned topidx = idx * P2M_ENTRIES_PER_PAGE;
		p2m_top_mfn_list[idx] = virt_to_mfn(&p2m_top_mfn[topidx]);
	}
}

void xen_setup_mfn_list_list(void)
{
	BUG_ON(HYPERVISOR_shared_info == &xen_dummy_shared_info);

	HYPERVISOR_shared_info->arch.pfn_to_mfn_frame_list_list =
		virt_to_mfn(p2m_top_mfn_list);
	HYPERVISOR_shared_info->arch.max_pfn = xen_start_info->nr_pages;
}

/* Set up p2m_top to point to the domain-builder provided p2m pages */
void __init xen_build_dynamic_phys_to_machine(void)
{
	unsigned long *mfn_list = (unsigned long *)xen_start_info->mfn_list;
	unsigned long max_pfn = min(MAX_DOMAIN_PAGES, xen_start_info->nr_pages);
	unsigned pfn;

	for (pfn = 0; pfn < max_pfn; pfn += P2M_ENTRIES_PER_PAGE) {
		unsigned topidx = p2m_top_index(pfn);

		p2m_top[topidx] = &mfn_list[pfn];
	}

	xen_build_mfn_list_list();
}

unsigned long get_phys_to_machine(unsigned long pfn)
{
	unsigned topidx, idx;

	if (unlikely(pfn >= MAX_DOMAIN_PAGES))
		return INVALID_P2M_ENTRY;

	topidx = p2m_top_index(pfn);
	idx = p2m_index(pfn);
	return p2m_top[topidx][idx];
}
EXPORT_SYMBOL_GPL(get_phys_to_machine);

/* install a  new p2m_top page */
bool install_p2mtop_page(unsigned long pfn, unsigned long *p)
{
	unsigned topidx = p2m_top_index(pfn);
	unsigned long **pfnp, *mfnp;
	unsigned i;

	pfnp = &p2m_top[topidx];
	mfnp = &p2m_top_mfn[topidx];

	for (i = 0; i < P2M_ENTRIES_PER_PAGE; i++)
		p[i] = INVALID_P2M_ENTRY;

	if (cmpxchg(pfnp, p2m_missing, p) == p2m_missing) {
		*mfnp = virt_to_mfn(p);
		return true;
	}

	return false;
}

static void alloc_p2m(unsigned long pfn)
{
	unsigned long *p;

	p = (void *)__get_free_page(GFP_KERNEL | __GFP_NOFAIL);
	BUG_ON(p == NULL);

	if (!install_p2mtop_page(pfn, p))
		free_page((unsigned long)p);
}

/* Try to install p2m mapping; fail if intermediate bits missing */
bool __set_phys_to_machine(unsigned long pfn, unsigned long mfn)
{
	unsigned topidx, idx;

	if (unlikely(pfn >= MAX_DOMAIN_PAGES)) {
		BUG_ON(mfn != INVALID_P2M_ENTRY);
		return true;
	}

	topidx = p2m_top_index(pfn);
	if (p2m_top[topidx] == p2m_missing) {
		if (mfn == INVALID_P2M_ENTRY)
			return true;
		return false;
	}

	idx = p2m_index(pfn);
	p2m_top[topidx][idx] = mfn;

	return true;
}

void set_phys_to_machine(unsigned long pfn, unsigned long mfn)
{
	if (unlikely(xen_feature(XENFEAT_auto_translated_physmap))) {
		BUG_ON(pfn != mfn && mfn != INVALID_P2M_ENTRY);
		return;
	}

	if (unlikely(!__set_phys_to_machine(pfn, mfn)))  {
		alloc_p2m(pfn);

		if (!__set_phys_to_machine(pfn, mfn))
			BUG();
	}
}

unsigned long arbitrary_virt_to_mfn(void *vaddr)
{
	xmaddr_t maddr = arbitrary_virt_to_machine(vaddr);

	return PFN_DOWN(maddr.maddr);
}

xmaddr_t arbitrary_virt_to_machine(void *vaddr)
{
	unsigned long address = (unsigned long)vaddr;
	unsigned int level;
	pte_t *pte;
	unsigned offset;

	/*
	 * if the PFN is in the linear mapped vaddr range, we can just use
	 * the (quick) virt_to_machine() p2m lookup
	 */
	if (virt_addr_valid(vaddr))
		return virt_to_machine(vaddr);

	/* otherwise we have to do a (slower) full page-table walk */

	pte = lookup_address(address, &level);
	BUG_ON(pte == NULL);
	offset = address & ~PAGE_MASK;
	return XMADDR(((phys_addr_t)pte_mfn(*pte) << PAGE_SHIFT) + offset);
}

void make_lowmem_page_readonly(void *vaddr)
{
	pte_t *pte, ptev;
	unsigned long address = (unsigned long)vaddr;
	unsigned int level;

	pte = lookup_address(address, &level);
	BUG_ON(pte == NULL);

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
	BUG_ON(pte == NULL);

	ptev = pte_mkwrite(*pte);

	if (HYPERVISOR_update_va_mapping(address, ptev, 0))
		BUG();
}


static bool xen_page_pinned(void *ptr)
{
	struct page *page = virt_to_page(ptr);

	return PagePinned(page);
}

static void xen_extend_mmu_update(const struct mmu_update *update)
{
	struct multicall_space mcs;
	struct mmu_update *u;

	mcs = xen_mc_extend_args(__HYPERVISOR_mmu_update, sizeof(*u));

	if (mcs.mc != NULL) {
		ADD_STATS(mmu_update_extended, 1);
		ADD_STATS(mmu_update_histo[mcs.mc->args[1]], -1);

		mcs.mc->args[1]++;

		if (mcs.mc->args[1] < MMU_UPDATE_HISTO)
			ADD_STATS(mmu_update_histo[mcs.mc->args[1]], 1);
		else
			ADD_STATS(mmu_update_histo[0], 1);
	} else {
		ADD_STATS(mmu_update, 1);
		mcs = __xen_mc_entry(sizeof(*u));
		MULTI_mmu_update(mcs.mc, mcs.args, 1, NULL, DOMID_SELF);
		ADD_STATS(mmu_update_histo[1], 1);
	}

	u = mcs.args;
	*u = *update;
}

void xen_set_pmd_hyper(pmd_t *ptr, pmd_t val)
{
	struct mmu_update u;

	preempt_disable();

	xen_mc_batch();

	/* ptr may be ioremapped for 64-bit pagetable setup */
	u.ptr = arbitrary_virt_to_machine(ptr).maddr;
	u.val = pmd_val_ma(val);
	xen_extend_mmu_update(&u);

	ADD_STATS(pmd_update_batched, paravirt_get_lazy_mode() == PARAVIRT_LAZY_MMU);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

void xen_set_pmd(pmd_t *ptr, pmd_t val)
{
	ADD_STATS(pmd_update, 1);

	/* If page is not pinned, we can just update the entry
	   directly */
	if (!xen_page_pinned(ptr)) {
		*ptr = val;
		return;
	}

	ADD_STATS(pmd_update_pinned, 1);

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

void xen_set_pte_at(struct mm_struct *mm, unsigned long addr,
		    pte_t *ptep, pte_t pteval)
{
	/* updates to init_mm may be done without lock */
	if (mm == &init_mm)
		preempt_disable();

	ADD_STATS(set_pte_at, 1);
//	ADD_STATS(set_pte_at_pinned, xen_page_pinned(ptep));
	ADD_STATS(set_pte_at_current, mm == current->mm);
	ADD_STATS(set_pte_at_kernel, mm == &init_mm);

	if (mm == current->mm || mm == &init_mm) {
		if (paravirt_get_lazy_mode() == PARAVIRT_LAZY_MMU) {
			struct multicall_space mcs;
			mcs = xen_mc_entry(0);

			MULTI_update_va_mapping(mcs.mc, addr, pteval, 0);
			ADD_STATS(set_pte_at_batched, 1);
			xen_mc_issue(PARAVIRT_LAZY_MMU);
			goto out;
		} else
			if (HYPERVISOR_update_va_mapping(addr, pteval, 0) == 0)
				goto out;
	}
	xen_set_pte(ptep, pteval);

out:
	if (mm == &init_mm)
		preempt_enable();
}

pte_t xen_ptep_modify_prot_start(struct mm_struct *mm,
				 unsigned long addr, pte_t *ptep)
{
	/* Just return the pte as-is.  We preserve the bits on commit */
	return *ptep;
}

void xen_ptep_modify_prot_commit(struct mm_struct *mm, unsigned long addr,
				 pte_t *ptep, pte_t pte)
{
	struct mmu_update u;

	xen_mc_batch();

	u.ptr = arbitrary_virt_to_machine(ptep).maddr | MMU_PT_UPDATE_PRESERVE_AD;
	u.val = pte_val_ma(pte);
	xen_extend_mmu_update(&u);

	ADD_STATS(prot_commit, 1);
	ADD_STATS(prot_commit_batched, paravirt_get_lazy_mode() == PARAVIRT_LAZY_MMU);

	xen_mc_issue(PARAVIRT_LAZY_MMU);
}

/* Assume pteval_t is equivalent to all the other *val_t types. */
static pteval_t pte_mfn_to_pfn(pteval_t val)
{
	if (val & _PAGE_PRESENT) {
		unsigned long mfn = (val & PTE_PFN_MASK) >> PAGE_SHIFT;
		pteval_t flags = val & PTE_FLAGS_MASK;
		val = ((pteval_t)mfn_to_pfn(mfn) << PAGE_SHIFT) | flags;
	}

	return val;
}

static pteval_t pte_pfn_to_mfn(pteval_t val)
{
	if (val & _PAGE_PRESENT) {
		unsigned long pfn = (val & PTE_PFN_MASK) >> PAGE_SHIFT;
		pteval_t flags = val & PTE_FLAGS_MASK;
		val = ((pteval_t)pfn_to_mfn(pfn) << PAGE_SHIFT) | flags;
	}

	return val;
}

pteval_t xen_pte_val(pte_t pte)
{
	return pte_mfn_to_pfn(pte.pte);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_pte_val);

pgdval_t xen_pgd_val(pgd_t pgd)
{
	return pte_mfn_to_pfn(pgd.pgd);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_pgd_val);

pte_t xen_make_pte(pteval_t pte)
{
	pte = pte_pfn_to_mfn(pte);
	return native_make_pte(pte);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_make_pte);

pgd_t xen_make_pgd(pgdval_t pgd)
{
	pgd = pte_pfn_to_mfn(pgd);
	return native_make_pgd(pgd);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_make_pgd);

pmdval_t xen_pmd_val(pmd_t pmd)
{
	return pte_mfn_to_pfn(pmd.pmd);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_pmd_val);

void xen_set_pud_hyper(pud_t *ptr, pud_t val)
{
	struct mmu_update u;

	preempt_disable();

	xen_mc_batch();

	/* ptr may be ioremapped for 64-bit pagetable setup */
	u.ptr = arbitrary_virt_to_machine(ptr).maddr;
	u.val = pud_val_ma(val);
	xen_extend_mmu_update(&u);

	ADD_STATS(pud_update_batched, paravirt_get_lazy_mode() == PARAVIRT_LAZY_MMU);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

void xen_set_pud(pud_t *ptr, pud_t val)
{
	ADD_STATS(pud_update, 1);

	/* If page is not pinned, we can just update the entry
	   directly */
	if (!xen_page_pinned(ptr)) {
		*ptr = val;
		return;
	}

	ADD_STATS(pud_update_pinned, 1);

	xen_set_pud_hyper(ptr, val);
}

void xen_set_pte(pte_t *ptep, pte_t pte)
{
	ADD_STATS(pte_update, 1);
//	ADD_STATS(pte_update_pinned, xen_page_pinned(ptep));
	ADD_STATS(pte_update_batched, paravirt_get_lazy_mode() == PARAVIRT_LAZY_MMU);

#ifdef CONFIG_X86_PAE
	ptep->pte_high = pte.pte_high;
	smp_wmb();
	ptep->pte_low = pte.pte_low;
#else
	*ptep = pte;
#endif
}

#ifdef CONFIG_X86_PAE
void xen_set_pte_atomic(pte_t *ptep, pte_t pte)
{
	set_64bit((u64 *)ptep, native_pte_val(pte));
}

void xen_pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	ptep->pte_low = 0;
	smp_wmb();		/* make sure low gets written first */
	ptep->pte_high = 0;
}

void xen_pmd_clear(pmd_t *pmdp)
{
	set_pmd(pmdp, __pmd(0));
}
#endif	/* CONFIG_X86_PAE */

pmd_t xen_make_pmd(pmdval_t pmd)
{
	pmd = pte_pfn_to_mfn(pmd);
	return native_make_pmd(pmd);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_make_pmd);

#if PAGETABLE_LEVELS == 4
pudval_t xen_pud_val(pud_t pud)
{
	return pte_mfn_to_pfn(pud.pud);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_pud_val);

pud_t xen_make_pud(pudval_t pud)
{
	pud = pte_pfn_to_mfn(pud);

	return native_make_pud(pud);
}
PV_CALLEE_SAVE_REGS_THUNK(xen_make_pud);

pgd_t *xen_get_user_pgd(pgd_t *pgd)
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

static void __xen_set_pgd_hyper(pgd_t *ptr, pgd_t val)
{
	struct mmu_update u;

	u.ptr = virt_to_machine(ptr).maddr;
	u.val = pgd_val_ma(val);
	xen_extend_mmu_update(&u);
}

/*
 * Raw hypercall-based set_pgd, intended for in early boot before
 * there's a page structure.  This implies:
 *  1. The only existing pagetable is the kernel's
 *  2. It is always pinned
 *  3. It has no user pagetable attached to it
 */
void __init xen_set_pgd_hyper(pgd_t *ptr, pgd_t val)
{
	preempt_disable();

	xen_mc_batch();

	__xen_set_pgd_hyper(ptr, val);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

void xen_set_pgd(pgd_t *ptr, pgd_t val)
{
	pgd_t *user_ptr = xen_get_user_pgd(ptr);

	ADD_STATS(pgd_update, 1);

	/* If page is not pinned, we can just update the entry
	   directly */
	if (!xen_page_pinned(ptr)) {
		*ptr = val;
		if (user_ptr) {
			WARN_ON(xen_page_pinned(user_ptr));
			*user_ptr = val;
		}
		return;
	}

	ADD_STATS(pgd_update_pinned, 1);
	ADD_STATS(pgd_update_batched, paravirt_get_lazy_mode() == PARAVIRT_LAZY_MMU);

	/* If it's pinned, then we can at least batch the kernel and
	   user updates together. */
	xen_mc_batch();

	__xen_set_pgd_hyper(ptr, val);
	if (user_ptr)
		__xen_set_pgd_hyper(user_ptr, val);

	xen_mc_issue(PARAVIRT_LAZY_MMU);
}
#endif	/* PAGETABLE_LEVELS == 4 */

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
	int flush = 0;
	unsigned hole_low, hole_high;
	unsigned pgdidx_limit, pudidx_limit, pmdidx_limit;
	unsigned pgdidx, pudidx, pmdidx;

	/* The limit is the last byte to be touched */
	limit--;
	BUG_ON(limit >= FIXADDR_TOP);

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 0;

	/*
	 * 64-bit has a great big hole in the middle of the address
	 * space, which contains the Xen mappings.  On 32-bit these
	 * will end up making a zero-sized hole and so is a no-op.
	 */
	hole_low = pgd_index(USER_LIMIT);
	hole_high = pgd_index(PAGE_OFFSET);

	pgdidx_limit = pgd_index(limit);
#if PTRS_PER_PUD > 1
	pudidx_limit = pud_index(limit);
#else
	pudidx_limit = 0;
#endif
#if PTRS_PER_PMD > 1
	pmdidx_limit = pmd_index(limit);
#else
	pmdidx_limit = 0;
#endif

	for (pgdidx = 0; pgdidx <= pgdidx_limit; pgdidx++) {
		pud_t *pud;

		if (pgdidx >= hole_low && pgdidx < hole_high)
			continue;

		if (!pgd_val(pgd[pgdidx]))
			continue;

		pud = pud_offset(&pgd[pgdidx], 0);

		if (PTRS_PER_PUD > 1) /* not folded */
			flush |= (*func)(mm, virt_to_page(pud), PT_PUD);

		for (pudidx = 0; pudidx < PTRS_PER_PUD; pudidx++) {
			pmd_t *pmd;

			if (pgdidx == pgdidx_limit &&
			    pudidx > pudidx_limit)
				goto out;

			if (pud_none(pud[pudidx]))
				continue;

			pmd = pmd_offset(&pud[pudidx], 0);

			if (PTRS_PER_PMD > 1) /* not folded */
				flush |= (*func)(mm, virt_to_page(pmd), PT_PMD);

			for (pmdidx = 0; pmdidx < PTRS_PER_PMD; pmdidx++) {
				struct page *pte;

				if (pgdidx == pgdidx_limit &&
				    pudidx == pudidx_limit &&
				    pmdidx > pmdidx_limit)
					goto out;

				if (pmd_none(pmd[pmdidx]))
					continue;

				pte = pmd_page(pmd[pmdidx]);
				flush |= (*func)(mm, pte, PT_PTE);
			}
		}
	}

out:
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

#if USE_SPLIT_PTLOCKS
	ptl = __pte_lockptr(page);
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
	struct mmuext_op *op;
	struct multicall_space mcs;

	mcs = __xen_mc_entry(sizeof(*op));
	op = mcs.args;
	op->cmd = level;
	op->arg1.mfn = pfn_to_mfn(pfn);
	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);
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
	vm_unmap_aliases();

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
	unsigned long flags;
	struct page *page;

	spin_lock_irqsave(&pgd_lock, flags);

	list_for_each_entry(page, &pgd_list, lru) {
		if (!PagePinned(page)) {
			__xen_pgd_pin(&init_mm, (pgd_t *)page_address(page));
			SetPageSavePinned(page);
		}
	}

	spin_unlock_irqrestore(&pgd_lock, flags);
}

/*
 * The init_mm pagetable is really pinned as soon as its created, but
 * that's before we have page structures to store the bits.  So do all
 * the book-keeping now.
 */
static __init int xen_mark_pinned(struct mm_struct *mm, struct page *page,
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
	unsigned long flags;
	struct page *page;

	spin_lock_irqsave(&pgd_lock, flags);

	list_for_each_entry(page, &pgd_list, lru) {
		if (PageSavePinned(page)) {
			BUG_ON(!PagePinned(page));
			__xen_pgd_unpin(&init_mm, (pgd_t *)page_address(page));
			ClearPageSavePinned(page);
		}
	}

	spin_unlock_irqrestore(&pgd_lock, flags);
}

void xen_activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	spin_lock(&next->page_table_lock);
	xen_pgd_pin(next);
	spin_unlock(&next->page_table_lock);
}

void xen_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm)
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

	active_mm = percpu_read(cpu_tlbstate.active_mm);

	if (active_mm == mm)
		leave_mm(smp_processor_id());

	/* If this cpu still has a stale cr3 reference, then make sure
	   it has been flushed. */
	if (percpu_read(xen_current_cr3) == __pa(mm->pgd)) {
		load_cr3(swapper_pg_dir);
		arch_flush_lazy_cpu_mode();
	}
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
		arch_flush_lazy_cpu_mode();
	}

	/* Get the "official" set of cpus referring to our pagetable. */
	if (!alloc_cpumask_var(&mask, GFP_ATOMIC)) {
		for_each_online_cpu(cpu) {
			if (!cpumask_test_cpu(cpu, &mm->cpu_vm_mask)
			    && per_cpu(xen_current_cr3, cpu) != __pa(mm->pgd))
				continue;
			smp_call_function_single(cpu, drop_other_mm_ref, mm, 1);
		}
		return;
	}
	cpumask_copy(mask, &mm->cpu_vm_mask);

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
void xen_exit_mmap(struct mm_struct *mm)
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

static __init void xen_pagetable_setup_start(pgd_t *base)
{
}

static __init void xen_pagetable_setup_done(pgd_t *base)
{
	xen_setup_shared_info();
}

static void xen_write_cr2(unsigned long cr2)
{
	percpu_read(xen_vcpu)->arch.cr2 = cr2;
}

static unsigned long xen_read_cr2(void)
{
	return percpu_read(xen_vcpu)->arch.cr2;
}

unsigned long xen_read_cr2_direct(void)
{
	return percpu_read(xen_vcpu_info.arch.cr2);
}

static void xen_flush_tlb(void)
{
	struct mmuext_op *op;
	struct multicall_space mcs;

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
				 struct mm_struct *mm, unsigned long va)
{
	struct {
		struct mmuext_op op;
		DECLARE_BITMAP(mask, NR_CPUS);
	} *args;
	struct multicall_space mcs;

	if (cpumask_empty(cpus))
		return;		/* nothing to do */

	mcs = xen_mc_entry(sizeof(*args));
	args = mcs.args;
	args->op.arg2.vcpumask = to_cpumask(args->mask);

	/* Remove us, and any offline CPUS. */
	cpumask_and(to_cpumask(args->mask), cpus, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), to_cpumask(args->mask));

	if (va == TLB_FLUSH_ALL) {
		args->op.cmd = MMUEXT_TLB_FLUSH_MULTI;
	} else {
		args->op.cmd = MMUEXT_INVLPG_MULTI;
		args->op.arg1.linear_addr = va;
	}

	MULTI_mmuext_op(mcs.mc, &args->op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);
}

static unsigned long xen_read_cr3(void)
{
	return percpu_read(xen_cr3);
}

static void set_current_cr3(void *v)
{
	percpu_write(xen_current_cr3, (unsigned long)v);
}

static void __xen_write_cr3(bool kernel, unsigned long cr3)
{
	struct mmuext_op *op;
	struct multicall_space mcs;
	unsigned long mfn;

	if (cr3)
		mfn = pfn_to_mfn(PFN_DOWN(cr3));
	else
		mfn = 0;

	WARN_ON(mfn == 0 && kernel);

	mcs = __xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = kernel ? MMUEXT_NEW_BASEPTR : MMUEXT_NEW_USER_BASEPTR;
	op->arg1.mfn = mfn;

	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	if (kernel) {
		percpu_write(xen_cr3, cr3);

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
	percpu_write(xen_cr3, cr3);

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
			user_pgd[pgd_index(VSYSCALL_START)] =
				__pgd(__pa(level3_user_vsyscall) | _PAGE_TABLE);
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

#ifdef CONFIG_HIGHPTE
static void *xen_kmap_atomic_pte(struct page *page, enum km_type type)
{
	pgprot_t prot = PAGE_KERNEL;

	if (PagePinned(page))
		prot = PAGE_KERNEL_RO;

	if (0 && PageHighMem(page))
		printk("mapping highpte %lx type %d prot %s\n",
		       page_to_pfn(page), type,
		       (unsigned long)pgprot_val(prot) & _PAGE_RW ? "WRITE" : "READ");

	return kmap_atomic_prot(page, type, prot);
}
#endif

#ifdef CONFIG_X86_32
static __init pte_t mask_rw_pte(pte_t *ptep, pte_t pte)
{
	/* If there's an existing pte, then don't allow _PAGE_RW to be set */
	if (pte_val_ma(*ptep) & _PAGE_PRESENT)
		pte = __pte_ma(((pte_val_ma(*ptep) & _PAGE_RW) | ~_PAGE_RW) &
			       pte_val_ma(pte));

	return pte;
}

/* Init-time set_pte while constructing initial pagetables, which
   doesn't allow RO pagetable pages to be remapped RW */
static __init void xen_set_pte_init(pte_t *ptep, pte_t pte)
{
	pte = mask_rw_pte(ptep, pte);

	xen_set_pte(ptep, pte);
}
#endif

static void pin_pagetable_pfn(unsigned cmd, unsigned long pfn)
{
	struct mmuext_op op;
	op.cmd = cmd;
	op.arg1.mfn = pfn_to_mfn(pfn);
	if (HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF))
		BUG();
}

/* Early in boot, while setting up the initial pagetable, assume
   everything is pinned. */
static __init void xen_alloc_pte_init(struct mm_struct *mm, unsigned long pfn)
{
#ifdef CONFIG_FLATMEM
	BUG_ON(mem_map);	/* should only be used early */
#endif
	make_lowmem_page_readonly(__va(PFN_PHYS(pfn)));
	pin_pagetable_pfn(MMUEXT_PIN_L1_TABLE, pfn);
}

/* Used for pmd and pud */
static __init void xen_alloc_pmd_init(struct mm_struct *mm, unsigned long pfn)
{
#ifdef CONFIG_FLATMEM
	BUG_ON(mem_map);	/* should only be used early */
#endif
	make_lowmem_page_readonly(__va(PFN_PHYS(pfn)));
}

/* Early release_pte assumes that all pts are pinned, since there's
   only init_mm and anything attached to that is pinned. */
static __init void xen_release_pte_init(unsigned long pfn)
{
	pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, pfn);
	make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
}

static __init void xen_release_pmd_init(unsigned long pfn)
{
	make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
}

/* This needs to make sure the new pte page is pinned iff its being
   attached to a pinned pagetable. */
static void xen_alloc_ptpage(struct mm_struct *mm, unsigned long pfn, unsigned level)
{
	struct page *page = pfn_to_page(pfn);

	if (PagePinned(virt_to_page(mm->pgd))) {
		SetPagePinned(page);

		vm_unmap_aliases();
		if (!PageHighMem(page)) {
			make_lowmem_page_readonly(__va(PFN_PHYS((unsigned long)pfn)));
			if (level == PT_PTE && USE_SPLIT_PTLOCKS)
				pin_pagetable_pfn(MMUEXT_PIN_L1_TABLE, pfn);
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
static void xen_release_ptpage(unsigned long pfn, unsigned level)
{
	struct page *page = pfn_to_page(pfn);

	if (PagePinned(page)) {
		if (!PageHighMem(page)) {
			if (level == PT_PTE && USE_SPLIT_PTLOCKS)
				pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, pfn);
			make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
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

#if PAGETABLE_LEVELS == 4
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
static void *__ka(phys_addr_t paddr)
{
#ifdef CONFIG_X86_64
	return (void *)(paddr + __START_KERNEL_map);
#else
	return __va(paddr);
#endif
}

/* Convert a machine address to physical address */
static unsigned long m2p(phys_addr_t maddr)
{
	phys_addr_t paddr;

	maddr &= PTE_PFN_MASK;
	paddr = mfn_to_pfn(maddr >> PAGE_SHIFT) << PAGE_SHIFT;

	return paddr;
}

/* Convert a machine address to kernel virtual */
static void *m2v(phys_addr_t maddr)
{
	return __ka(m2p(maddr));
}

static void set_page_prot(void *addr, pgprot_t prot)
{
	unsigned long pfn = __pa(addr) >> PAGE_SHIFT;
	pte_t pte = pfn_pte(pfn, prot);

	if (HYPERVISOR_update_va_mapping((unsigned long)addr, pte, 0))
		BUG();
}

static __init void xen_map_identity_early(pmd_t *pmd, unsigned long max_pfn)
{
	unsigned pmdidx, pteidx;
	unsigned ident_pte;
	unsigned long pfn;

	ident_pte = 0;
	pfn = 0;
	for (pmdidx = 0; pmdidx < PTRS_PER_PMD && pfn < max_pfn; pmdidx++) {
		pte_t *pte_page;

		/* Reuse or allocate a page of ptes */
		if (pmd_present(pmd[pmdidx]))
			pte_page = m2v(pmd[pmdidx].pmd);
		else {
			/* Check for free pte pages */
			if (ident_pte == ARRAY_SIZE(level1_ident_pgt))
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

#ifdef CONFIG_X86_64
static void convert_pfn_mfn(void *v)
{
	pte_t *pte = v;
	int i;

	/* All levels are converted the same way, so just treat them
	   as ptes. */
	for (i = 0; i < PTRS_PER_PTE; i++)
		pte[i] = xen_make_pte(pte[i].pte);
}

/*
 * Set up the inital kernel pagetable.
 *
 * We can construct this by grafting the Xen provided pagetable into
 * head_64.S's preconstructed pagetables.  We copy the Xen L2's into
 * level2_ident_pgt, level2_kernel_pgt and level2_fixmap_pgt.  This
 * means that only the kernel has a physical mapping to start with -
 * but that's enough to get __va working.  We need to fill in the rest
 * of the physical mapping once some sort of allocator has been set
 * up.
 */
__init pgd_t *xen_setup_kernel_pagetable(pgd_t *pgd,
					 unsigned long max_pfn)
{
	pud_t *l3;
	pmd_t *l2;

	/* Zap identity mapping */
	init_level4_pgt[0] = __pgd(0);

	/* Pre-constructed entries are in pfn, so convert to mfn */
	convert_pfn_mfn(init_level4_pgt);
	convert_pfn_mfn(level3_ident_pgt);
	convert_pfn_mfn(level3_kernel_pgt);

	l3 = m2v(pgd[pgd_index(__START_KERNEL_map)].pgd);
	l2 = m2v(l3[pud_index(__START_KERNEL_map)].pud);

	memcpy(level2_ident_pgt, l2, sizeof(pmd_t) * PTRS_PER_PMD);
	memcpy(level2_kernel_pgt, l2, sizeof(pmd_t) * PTRS_PER_PMD);

	l3 = m2v(pgd[pgd_index(__START_KERNEL_map + PMD_SIZE)].pgd);
	l2 = m2v(l3[pud_index(__START_KERNEL_map + PMD_SIZE)].pud);
	memcpy(level2_fixmap_pgt, l2, sizeof(pmd_t) * PTRS_PER_PMD);

	/* Set up identity map */
	xen_map_identity_early(level2_ident_pgt, max_pfn);

	/* Make pagetable pieces RO */
	set_page_prot(init_level4_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_ident_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_kernel_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_user_vsyscall, PAGE_KERNEL_RO);
	set_page_prot(level2_kernel_pgt, PAGE_KERNEL_RO);
	set_page_prot(level2_fixmap_pgt, PAGE_KERNEL_RO);

	/* Pin down new L4 */
	pin_pagetable_pfn(MMUEXT_PIN_L4_TABLE,
			  PFN_DOWN(__pa_symbol(init_level4_pgt)));

	/* Unpin Xen-provided one */
	pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, PFN_DOWN(__pa(pgd)));

	/* Switch over */
	pgd = init_level4_pgt;

	/*
	 * At this stage there can be no user pgd, and no page
	 * structure to attach it to, so make sure we just set kernel
	 * pgd.
	 */
	xen_mc_batch();
	__xen_write_cr3(true, __pa(pgd));
	xen_mc_issue(PARAVIRT_LAZY_CPU);

	reserve_early(__pa(xen_start_info->pt_base),
		      __pa(xen_start_info->pt_base +
			   xen_start_info->nr_pt_frames * PAGE_SIZE),
		      "XEN PAGETABLES");

	return pgd;
}
#else	/* !CONFIG_X86_64 */
static pmd_t level2_kernel_pgt[PTRS_PER_PMD] __page_aligned_bss;

__init pgd_t *xen_setup_kernel_pagetable(pgd_t *pgd,
					 unsigned long max_pfn)
{
	pmd_t *kernel_pmd;

	max_pfn_mapped = PFN_DOWN(__pa(xen_start_info->pt_base) +
				  xen_start_info->nr_pt_frames * PAGE_SIZE +
				  512*1024);

	kernel_pmd = m2v(pgd[KERNEL_PGD_BOUNDARY].pgd);
	memcpy(level2_kernel_pgt, kernel_pmd, sizeof(pmd_t) * PTRS_PER_PMD);

	xen_map_identity_early(level2_kernel_pgt, max_pfn);

	memcpy(swapper_pg_dir, pgd, sizeof(pgd_t) * PTRS_PER_PGD);
	set_pgd(&swapper_pg_dir[KERNEL_PGD_BOUNDARY],
			__pgd(__pa(level2_kernel_pgt) | _PAGE_PRESENT));

	set_page_prot(level2_kernel_pgt, PAGE_KERNEL_RO);
	set_page_prot(swapper_pg_dir, PAGE_KERNEL_RO);
	set_page_prot(empty_zero_page, PAGE_KERNEL_RO);

	pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, PFN_DOWN(__pa(pgd)));

	xen_write_cr3(__pa(swapper_pg_dir));

	pin_pagetable_pfn(MMUEXT_PIN_L3_TABLE, PFN_DOWN(__pa(swapper_pg_dir)));

	reserve_early(__pa(xen_start_info->pt_base),
		      __pa(xen_start_info->pt_base +
			   xen_start_info->nr_pt_frames * PAGE_SIZE),
		      "XEN PAGETABLES");

	return swapper_pg_dir;
}
#endif	/* CONFIG_X86_64 */

static void xen_set_fixmap(unsigned idx, phys_addr_t phys, pgprot_t prot)
{
	pte_t pte;

	phys >>= PAGE_SHIFT;

	switch (idx) {
	case FIX_BTMAP_END ... FIX_BTMAP_BEGIN:
#ifdef CONFIG_X86_F00F_BUG
	case FIX_F00F_IDT:
#endif
#ifdef CONFIG_X86_32
	case FIX_WP_TEST:
	case FIX_VDSO:
# ifdef CONFIG_HIGHMEM
	case FIX_KMAP_BEGIN ... FIX_KMAP_END:
# endif
#else
	case VSYSCALL_LAST_PAGE ... VSYSCALL_FIRST_PAGE:
#endif
#ifdef CONFIG_X86_LOCAL_APIC
	case FIX_APIC_BASE:	/* maps dummy local APIC */
#endif
	case FIX_TEXT_POKE0:
	case FIX_TEXT_POKE1:
		/* All local page mappings */
		pte = pfn_pte(phys, prot);
		break;

	default:
		pte = mfn_pte(phys, prot);
		break;
	}

	__native_set_fixmap(idx, pte);

#ifdef CONFIG_X86_64
	/* Replicate changes to map the vsyscall page into the user
	   pagetable vsyscall mapping. */
	if (idx >= VSYSCALL_LAST_PAGE && idx <= VSYSCALL_FIRST_PAGE) {
		unsigned long vaddr = __fix_to_virt(idx);
		set_pte_vaddr_pud(level3_user_vsyscall, vaddr, pte);
	}
#endif
}

__init void xen_post_allocator_init(void)
{
	pv_mmu_ops.set_pte = xen_set_pte;
	pv_mmu_ops.set_pmd = xen_set_pmd;
	pv_mmu_ops.set_pud = xen_set_pud;
#if PAGETABLE_LEVELS == 4
	pv_mmu_ops.set_pgd = xen_set_pgd;
#endif

	/* This will work as long as patching hasn't happened yet
	   (which it hasn't) */
	pv_mmu_ops.alloc_pte = xen_alloc_pte;
	pv_mmu_ops.alloc_pmd = xen_alloc_pmd;
	pv_mmu_ops.release_pte = xen_release_pte;
	pv_mmu_ops.release_pmd = xen_release_pmd;
#if PAGETABLE_LEVELS == 4
	pv_mmu_ops.alloc_pud = xen_alloc_pud;
	pv_mmu_ops.release_pud = xen_release_pud;
#endif

#ifdef CONFIG_X86_64
	SetPagePinned(virt_to_page(level3_user_vsyscall));
#endif
	xen_mark_init_mm_pinned();
}

const struct pv_mmu_ops xen_mmu_ops __initdata = {
	.pagetable_setup_start = xen_pagetable_setup_start,
	.pagetable_setup_done = xen_pagetable_setup_done,

	.read_cr2 = xen_read_cr2,
	.write_cr2 = xen_write_cr2,

	.read_cr3 = xen_read_cr3,
	.write_cr3 = xen_write_cr3,

	.flush_tlb_user = xen_flush_tlb,
	.flush_tlb_kernel = xen_flush_tlb,
	.flush_tlb_single = xen_flush_tlb_single,
	.flush_tlb_others = xen_flush_tlb_others,

	.pte_update = paravirt_nop,
	.pte_update_defer = paravirt_nop,

	.pgd_alloc = xen_pgd_alloc,
	.pgd_free = xen_pgd_free,

	.alloc_pte = xen_alloc_pte_init,
	.release_pte = xen_release_pte_init,
	.alloc_pmd = xen_alloc_pmd_init,
	.alloc_pmd_clone = paravirt_nop,
	.release_pmd = xen_release_pmd_init,

#ifdef CONFIG_HIGHPTE
	.kmap_atomic_pte = xen_kmap_atomic_pte,
#endif

#ifdef CONFIG_X86_64
	.set_pte = xen_set_pte,
#else
	.set_pte = xen_set_pte_init,
#endif
	.set_pte_at = xen_set_pte_at,
	.set_pmd = xen_set_pmd_hyper,

	.ptep_modify_prot_start = __ptep_modify_prot_start,
	.ptep_modify_prot_commit = __ptep_modify_prot_commit,

	.pte_val = PV_CALLEE_SAVE(xen_pte_val),
	.pgd_val = PV_CALLEE_SAVE(xen_pgd_val),

	.make_pte = PV_CALLEE_SAVE(xen_make_pte),
	.make_pgd = PV_CALLEE_SAVE(xen_make_pgd),

#ifdef CONFIG_X86_PAE
	.set_pte_atomic = xen_set_pte_atomic,
	.pte_clear = xen_pte_clear,
	.pmd_clear = xen_pmd_clear,
#endif	/* CONFIG_X86_PAE */
	.set_pud = xen_set_pud_hyper,

	.make_pmd = PV_CALLEE_SAVE(xen_make_pmd),
	.pmd_val = PV_CALLEE_SAVE(xen_pmd_val),

#if PAGETABLE_LEVELS == 4
	.pud_val = PV_CALLEE_SAVE(xen_pud_val),
	.make_pud = PV_CALLEE_SAVE(xen_make_pud),
	.set_pgd = xen_set_pgd_hyper,

	.alloc_pud = xen_alloc_pmd_init,
	.release_pud = xen_release_pmd_init,
#endif	/* PAGETABLE_LEVELS == 4 */

	.activate_mm = xen_activate_mm,
	.dup_mmap = xen_dup_mmap,
	.exit_mmap = xen_exit_mmap,

	.lazy_mode = {
		.enter = paravirt_enter_lazy_mmu,
		.leave = xen_leave_lazy,
	},

	.set_fixmap = xen_set_fixmap,
};


#ifdef CONFIG_XEN_DEBUG_FS

static struct dentry *d_mmu_debug;

static int __init xen_mmu_debugfs(void)
{
	struct dentry *d_xen = xen_init_debugfs();

	if (d_xen == NULL)
		return -ENOMEM;

	d_mmu_debug = debugfs_create_dir("mmu", d_xen);

	debugfs_create_u8("zero_stats", 0644, d_mmu_debug, &zero_stats);

	debugfs_create_u32("pgd_update", 0444, d_mmu_debug, &mmu_stats.pgd_update);
	debugfs_create_u32("pgd_update_pinned", 0444, d_mmu_debug,
			   &mmu_stats.pgd_update_pinned);
	debugfs_create_u32("pgd_update_batched", 0444, d_mmu_debug,
			   &mmu_stats.pgd_update_pinned);

	debugfs_create_u32("pud_update", 0444, d_mmu_debug, &mmu_stats.pud_update);
	debugfs_create_u32("pud_update_pinned", 0444, d_mmu_debug,
			   &mmu_stats.pud_update_pinned);
	debugfs_create_u32("pud_update_batched", 0444, d_mmu_debug,
			   &mmu_stats.pud_update_pinned);

	debugfs_create_u32("pmd_update", 0444, d_mmu_debug, &mmu_stats.pmd_update);
	debugfs_create_u32("pmd_update_pinned", 0444, d_mmu_debug,
			   &mmu_stats.pmd_update_pinned);
	debugfs_create_u32("pmd_update_batched", 0444, d_mmu_debug,
			   &mmu_stats.pmd_update_pinned);

	debugfs_create_u32("pte_update", 0444, d_mmu_debug, &mmu_stats.pte_update);
//	debugfs_create_u32("pte_update_pinned", 0444, d_mmu_debug,
//			   &mmu_stats.pte_update_pinned);
	debugfs_create_u32("pte_update_batched", 0444, d_mmu_debug,
			   &mmu_stats.pte_update_pinned);

	debugfs_create_u32("mmu_update", 0444, d_mmu_debug, &mmu_stats.mmu_update);
	debugfs_create_u32("mmu_update_extended", 0444, d_mmu_debug,
			   &mmu_stats.mmu_update_extended);
	xen_debugfs_create_u32_array("mmu_update_histo", 0444, d_mmu_debug,
				     mmu_stats.mmu_update_histo, 20);

	debugfs_create_u32("set_pte_at", 0444, d_mmu_debug, &mmu_stats.set_pte_at);
	debugfs_create_u32("set_pte_at_batched", 0444, d_mmu_debug,
			   &mmu_stats.set_pte_at_batched);
	debugfs_create_u32("set_pte_at_current", 0444, d_mmu_debug,
			   &mmu_stats.set_pte_at_current);
	debugfs_create_u32("set_pte_at_kernel", 0444, d_mmu_debug,
			   &mmu_stats.set_pte_at_kernel);

	debugfs_create_u32("prot_commit", 0444, d_mmu_debug, &mmu_stats.prot_commit);
	debugfs_create_u32("prot_commit_batched", 0444, d_mmu_debug,
			   &mmu_stats.prot_commit_batched);

	return 0;
}
fs_initcall(xen_mmu_debugfs);

#endif	/* CONFIG_XEN_DEBUG_FS */
