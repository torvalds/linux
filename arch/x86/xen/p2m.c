/*
 * Xen leaves the responsibility for maintaining p2m mappings to the
 * guests themselves, but it must also access and update the p2m array
 * during suspend/resume when all the pages are reallocated.
 *
 * The logical flat p2m table is mapped to a linear kernel memory area.
 * For accesses by Xen a three-level tree linked via mfns only is set up to
 * allow the address space to be sparse.
 *
 *               Xen
 *                |
 *          p2m_top_mfn
 *              /   \
 * p2m_mid_mfn p2m_mid_mfn
 *         /           /
 *  p2m p2m p2m ...
 *
 * The p2m_mid_mfn pages are mapped by p2m_top_mfn_p.
 *
 * The p2m_top_mfn level is limited to 1 page, so the maximum representable
 * pseudo-physical address space is:
 *  P2M_TOP_PER_PAGE * P2M_MID_PER_PAGE * P2M_PER_PAGE pages
 *
 * P2M_PER_PAGE depends on the architecture, as a mfn is always
 * unsigned long (8 bytes on 64-bit, 4 bytes on 32), leading to
 * 512 and 1024 entries respectively.
 *
 * In short, these structures contain the Machine Frame Number (MFN) of the PFN.
 *
 * However not all entries are filled with MFNs. Specifically for all other
 * leaf entries, or for the top  root, or middle one, for which there is a void
 * entry, we assume it is  "missing". So (for example)
 *  pfn_to_mfn(0x90909090)=INVALID_P2M_ENTRY.
 * We have a dedicated page p2m_missing with all entries being
 * INVALID_P2M_ENTRY. This page may be referenced multiple times in the p2m
 * list/tree in case there are multiple areas with P2M_PER_PAGE invalid pfns.
 *
 * We also have the possibility of setting 1-1 mappings on certain regions, so
 * that:
 *  pfn_to_mfn(0xc0000)=0xc0000
 *
 * The benefit of this is, that we can assume for non-RAM regions (think
 * PCI BARs, or ACPI spaces), we can create mappings easily because we
 * get the PFN value to match the MFN.
 *
 * For this to work efficiently we have one new page p2m_identity. All entries
 * in p2m_identity are set to INVALID_P2M_ENTRY type (Xen toolstack only
 * recognizes that and MFNs, no other fancy value).
 *
 * On lookup we spot that the entry points to p2m_identity and return the
 * identity value instead of dereferencing and returning INVALID_P2M_ENTRY.
 * If the entry points to an allocated page, we just proceed as before and
 * return the PFN. If the PFN has IDENTITY_FRAME_BIT set we unmask that in
 * appropriate functions (pfn_to_mfn).
 *
 * The reason for having the IDENTITY_FRAME_BIT instead of just returning the
 * PFN is that we could find ourselves where pfn_to_mfn(pfn)==pfn for a
 * non-identity pfn. To protect ourselves against we elect to set (and get) the
 * IDENTITY_FRAME_BIT on all identity mapped PFNs.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/bootmem.h>
#include <linux/slab.h>

#include <asm/cache.h>
#include <asm/setup.h>
#include <asm/uaccess.h>

#include <asm/xen/page.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>
#include <xen/balloon.h>
#include <xen/grant_table.h>

#include "p2m.h"
#include "multicalls.h"
#include "xen-ops.h"

#define PMDS_PER_MID_PAGE	(P2M_MID_PER_PAGE / PTRS_PER_PTE)

static void __init m2p_override_init(void);

unsigned long *xen_p2m_addr __read_mostly;
EXPORT_SYMBOL_GPL(xen_p2m_addr);
unsigned long xen_p2m_size __read_mostly;
EXPORT_SYMBOL_GPL(xen_p2m_size);
unsigned long xen_max_p2m_pfn __read_mostly;
EXPORT_SYMBOL_GPL(xen_max_p2m_pfn);

static DEFINE_SPINLOCK(p2m_update_lock);

static unsigned long *p2m_mid_missing_mfn;
static unsigned long *p2m_top_mfn;
static unsigned long **p2m_top_mfn_p;
static unsigned long *p2m_missing;
static unsigned long *p2m_identity;
static pte_t *p2m_missing_pte;
static pte_t *p2m_identity_pte;

static inline unsigned p2m_top_index(unsigned long pfn)
{
	BUG_ON(pfn >= MAX_P2M_PFN);
	return pfn / (P2M_MID_PER_PAGE * P2M_PER_PAGE);
}

static inline unsigned p2m_mid_index(unsigned long pfn)
{
	return (pfn / P2M_PER_PAGE) % P2M_MID_PER_PAGE;
}

static inline unsigned p2m_index(unsigned long pfn)
{
	return pfn % P2M_PER_PAGE;
}

static void p2m_top_mfn_init(unsigned long *top)
{
	unsigned i;

	for (i = 0; i < P2M_TOP_PER_PAGE; i++)
		top[i] = virt_to_mfn(p2m_mid_missing_mfn);
}

static void p2m_top_mfn_p_init(unsigned long **top)
{
	unsigned i;

	for (i = 0; i < P2M_TOP_PER_PAGE; i++)
		top[i] = p2m_mid_missing_mfn;
}

static void p2m_mid_mfn_init(unsigned long *mid, unsigned long *leaf)
{
	unsigned i;

	for (i = 0; i < P2M_MID_PER_PAGE; i++)
		mid[i] = virt_to_mfn(leaf);
}

static void p2m_init(unsigned long *p2m)
{
	unsigned i;

	for (i = 0; i < P2M_PER_PAGE; i++)
		p2m[i] = INVALID_P2M_ENTRY;
}

static void p2m_init_identity(unsigned long *p2m, unsigned long pfn)
{
	unsigned i;

	for (i = 0; i < P2M_PER_PAGE; i++)
		p2m[i] = IDENTITY_FRAME(pfn + i);
}

static void * __ref alloc_p2m_page(void)
{
	if (unlikely(!slab_is_available()))
		return alloc_bootmem_align(PAGE_SIZE, PAGE_SIZE);

	return (void *)__get_free_page(GFP_KERNEL | __GFP_REPEAT);
}

/* Only to be called in case of a race for a page just allocated! */
static void free_p2m_page(void *p)
{
	BUG_ON(!slab_is_available());
	free_page((unsigned long)p);
}

/*
 * Build the parallel p2m_top_mfn and p2m_mid_mfn structures
 *
 * This is called both at boot time, and after resuming from suspend:
 * - At boot time we're called rather early, and must use alloc_bootmem*()
 *   to allocate memory.
 *
 * - After resume we're called from within stop_machine, but the mfn
 *   tree should already be completely allocated.
 */
void __ref xen_build_mfn_list_list(void)
{
	unsigned long pfn, mfn;
	pte_t *ptep;
	unsigned int level, topidx, mididx;
	unsigned long *mid_mfn_p;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return;

	/* Pre-initialize p2m_top_mfn to be completely missing */
	if (p2m_top_mfn == NULL) {
		p2m_mid_missing_mfn = alloc_p2m_page();
		p2m_mid_mfn_init(p2m_mid_missing_mfn, p2m_missing);

		p2m_top_mfn_p = alloc_p2m_page();
		p2m_top_mfn_p_init(p2m_top_mfn_p);

		p2m_top_mfn = alloc_p2m_page();
		p2m_top_mfn_init(p2m_top_mfn);
	} else {
		/* Reinitialise, mfn's all change after migration */
		p2m_mid_mfn_init(p2m_mid_missing_mfn, p2m_missing);
	}

	for (pfn = 0; pfn < xen_max_p2m_pfn && pfn < MAX_P2M_PFN;
	     pfn += P2M_PER_PAGE) {
		topidx = p2m_top_index(pfn);
		mididx = p2m_mid_index(pfn);

		mid_mfn_p = p2m_top_mfn_p[topidx];
		ptep = lookup_address((unsigned long)(xen_p2m_addr + pfn),
				      &level);
		BUG_ON(!ptep || level != PG_LEVEL_4K);
		mfn = pte_mfn(*ptep);
		ptep = (pte_t *)((unsigned long)ptep & ~(PAGE_SIZE - 1));

		/* Don't bother allocating any mfn mid levels if
		 * they're just missing, just update the stored mfn,
		 * since all could have changed over a migrate.
		 */
		if (ptep == p2m_missing_pte || ptep == p2m_identity_pte) {
			BUG_ON(mididx);
			BUG_ON(mid_mfn_p != p2m_mid_missing_mfn);
			p2m_top_mfn[topidx] = virt_to_mfn(p2m_mid_missing_mfn);
			pfn += (P2M_MID_PER_PAGE - 1) * P2M_PER_PAGE;
			continue;
		}

		if (mid_mfn_p == p2m_mid_missing_mfn) {
			mid_mfn_p = alloc_p2m_page();
			p2m_mid_mfn_init(mid_mfn_p, p2m_missing);

			p2m_top_mfn_p[topidx] = mid_mfn_p;
		}

		p2m_top_mfn[topidx] = virt_to_mfn(mid_mfn_p);
		mid_mfn_p[mididx] = mfn;
	}
}

void xen_setup_mfn_list_list(void)
{
	if (xen_feature(XENFEAT_auto_translated_physmap))
		return;

	BUG_ON(HYPERVISOR_shared_info == &xen_dummy_shared_info);

	HYPERVISOR_shared_info->arch.pfn_to_mfn_frame_list_list =
		virt_to_mfn(p2m_top_mfn);
	HYPERVISOR_shared_info->arch.max_pfn = xen_max_p2m_pfn;
}

/* Set up p2m_top to point to the domain-builder provided p2m pages */
void __init xen_build_dynamic_phys_to_machine(void)
{
	unsigned long pfn;

	 if (xen_feature(XENFEAT_auto_translated_physmap))
		return;

	xen_p2m_addr = (unsigned long *)xen_start_info->mfn_list;
	xen_p2m_size = ALIGN(xen_start_info->nr_pages, P2M_PER_PAGE);

	for (pfn = xen_start_info->nr_pages; pfn < xen_p2m_size; pfn++)
		xen_p2m_addr[pfn] = INVALID_P2M_ENTRY;

	xen_max_p2m_pfn = xen_p2m_size;
}

#define P2M_TYPE_IDENTITY	0
#define P2M_TYPE_MISSING	1
#define P2M_TYPE_PFN		2
#define P2M_TYPE_UNKNOWN	3

static int xen_p2m_elem_type(unsigned long pfn)
{
	unsigned long mfn;

	if (pfn >= xen_p2m_size)
		return P2M_TYPE_IDENTITY;

	mfn = xen_p2m_addr[pfn];

	if (mfn == INVALID_P2M_ENTRY)
		return P2M_TYPE_MISSING;

	if (mfn & IDENTITY_FRAME_BIT)
		return P2M_TYPE_IDENTITY;

	return P2M_TYPE_PFN;
}

static void __init xen_rebuild_p2m_list(unsigned long *p2m)
{
	unsigned int i, chunk;
	unsigned long pfn;
	unsigned long *mfns;
	pte_t *ptep;
	pmd_t *pmdp;
	int type;

	p2m_missing = alloc_p2m_page();
	p2m_init(p2m_missing);
	p2m_identity = alloc_p2m_page();
	p2m_init(p2m_identity);

	p2m_missing_pte = alloc_p2m_page();
	paravirt_alloc_pte(&init_mm, __pa(p2m_missing_pte) >> PAGE_SHIFT);
	p2m_identity_pte = alloc_p2m_page();
	paravirt_alloc_pte(&init_mm, __pa(p2m_identity_pte) >> PAGE_SHIFT);
	for (i = 0; i < PTRS_PER_PTE; i++) {
		set_pte(p2m_missing_pte + i,
			pfn_pte(PFN_DOWN(__pa(p2m_missing)), PAGE_KERNEL_RO));
		set_pte(p2m_identity_pte + i,
			pfn_pte(PFN_DOWN(__pa(p2m_identity)), PAGE_KERNEL_RO));
	}

	for (pfn = 0; pfn < xen_max_p2m_pfn; pfn += chunk) {
		/*
		 * Try to map missing/identity PMDs or p2m-pages if possible.
		 * We have to respect the structure of the mfn_list_list
		 * which will be built just afterwards.
		 * Chunk size to test is one p2m page if we are in the middle
		 * of a mfn_list_list mid page and the complete mid page area
		 * if we are at index 0 of the mid page. Please note that a
		 * mid page might cover more than one PMD, e.g. on 32 bit PAE
		 * kernels.
		 */
		chunk = (pfn & (P2M_PER_PAGE * P2M_MID_PER_PAGE - 1)) ?
			P2M_PER_PAGE : P2M_PER_PAGE * P2M_MID_PER_PAGE;

		type = xen_p2m_elem_type(pfn);
		i = 0;
		if (type != P2M_TYPE_PFN)
			for (i = 1; i < chunk; i++)
				if (xen_p2m_elem_type(pfn + i) != type)
					break;
		if (i < chunk)
			/* Reset to minimal chunk size. */
			chunk = P2M_PER_PAGE;

		if (type == P2M_TYPE_PFN || i < chunk) {
			/* Use initial p2m page contents. */
#ifdef CONFIG_X86_64
			mfns = alloc_p2m_page();
			copy_page(mfns, xen_p2m_addr + pfn);
#else
			mfns = xen_p2m_addr + pfn;
#endif
			ptep = populate_extra_pte((unsigned long)(p2m + pfn));
			set_pte(ptep,
				pfn_pte(PFN_DOWN(__pa(mfns)), PAGE_KERNEL));
			continue;
		}

		if (chunk == P2M_PER_PAGE) {
			/* Map complete missing or identity p2m-page. */
			mfns = (type == P2M_TYPE_MISSING) ?
				p2m_missing : p2m_identity;
			ptep = populate_extra_pte((unsigned long)(p2m + pfn));
			set_pte(ptep,
				pfn_pte(PFN_DOWN(__pa(mfns)), PAGE_KERNEL_RO));
			continue;
		}

		/* Complete missing or identity PMD(s) can be mapped. */
		ptep = (type == P2M_TYPE_MISSING) ?
			p2m_missing_pte : p2m_identity_pte;
		for (i = 0; i < PMDS_PER_MID_PAGE; i++) {
			pmdp = populate_extra_pmd(
				(unsigned long)(p2m + pfn + i * PTRS_PER_PTE));
			set_pmd(pmdp, __pmd(__pa(ptep) | _KERNPG_TABLE));
		}
	}
}

void __init xen_vmalloc_p2m_tree(void)
{
	static struct vm_struct vm;

	vm.flags = VM_ALLOC;
	vm.size = ALIGN(sizeof(unsigned long) * xen_max_p2m_pfn,
			PMD_SIZE * PMDS_PER_MID_PAGE);
	vm_area_register_early(&vm, PMD_SIZE * PMDS_PER_MID_PAGE);
	pr_notice("p2m virtual area at %p, size is %lx\n", vm.addr, vm.size);

	xen_max_p2m_pfn = vm.size / sizeof(unsigned long);

	xen_rebuild_p2m_list(vm.addr);

	xen_p2m_addr = vm.addr;
	xen_p2m_size = xen_max_p2m_pfn;

	xen_inv_extra_mem();

	m2p_override_init();
}

unsigned long get_phys_to_machine(unsigned long pfn)
{
	pte_t *ptep;
	unsigned int level;

	if (unlikely(pfn >= xen_p2m_size)) {
		if (pfn < xen_max_p2m_pfn)
			return xen_chk_extra_mem(pfn);

		return IDENTITY_FRAME(pfn);
	}

	ptep = lookup_address((unsigned long)(xen_p2m_addr + pfn), &level);
	BUG_ON(!ptep || level != PG_LEVEL_4K);

	/*
	 * The INVALID_P2M_ENTRY is filled in both p2m_*identity
	 * and in p2m_*missing, so returning the INVALID_P2M_ENTRY
	 * would be wrong.
	 */
	if (pte_pfn(*ptep) == PFN_DOWN(__pa(p2m_identity)))
		return IDENTITY_FRAME(pfn);

	return xen_p2m_addr[pfn];
}
EXPORT_SYMBOL_GPL(get_phys_to_machine);

/*
 * Allocate new pmd(s). It is checked whether the old pmd is still in place.
 * If not, nothing is changed. This is okay as the only reason for allocating
 * a new pmd is to replace p2m_missing_pte or p2m_identity_pte by a individual
 * pmd. In case of PAE/x86-32 there are multiple pmds to allocate!
 */
static pte_t *alloc_p2m_pmd(unsigned long addr, pte_t *ptep, pte_t *pte_pg)
{
	pte_t *ptechk;
	pte_t *pteret = ptep;
	pte_t *pte_newpg[PMDS_PER_MID_PAGE];
	pmd_t *pmdp;
	unsigned int level;
	unsigned long flags;
	unsigned long vaddr;
	int i;

	/* Do all allocations first to bail out in error case. */
	for (i = 0; i < PMDS_PER_MID_PAGE; i++) {
		pte_newpg[i] = alloc_p2m_page();
		if (!pte_newpg[i]) {
			for (i--; i >= 0; i--)
				free_p2m_page(pte_newpg[i]);

			return NULL;
		}
	}

	vaddr = addr & ~(PMD_SIZE * PMDS_PER_MID_PAGE - 1);

	for (i = 0; i < PMDS_PER_MID_PAGE; i++) {
		copy_page(pte_newpg[i], pte_pg);
		paravirt_alloc_pte(&init_mm, __pa(pte_newpg[i]) >> PAGE_SHIFT);

		pmdp = lookup_pmd_address(vaddr);
		BUG_ON(!pmdp);

		spin_lock_irqsave(&p2m_update_lock, flags);

		ptechk = lookup_address(vaddr, &level);
		if (ptechk == pte_pg) {
			set_pmd(pmdp,
				__pmd(__pa(pte_newpg[i]) | _KERNPG_TABLE));
			if (vaddr == (addr & ~(PMD_SIZE - 1)))
				pteret = pte_offset_kernel(pmdp, addr);
			pte_newpg[i] = NULL;
		}

		spin_unlock_irqrestore(&p2m_update_lock, flags);

		if (pte_newpg[i]) {
			paravirt_release_pte(__pa(pte_newpg[i]) >> PAGE_SHIFT);
			free_p2m_page(pte_newpg[i]);
		}

		vaddr += PMD_SIZE;
	}

	return pteret;
}

/*
 * Fully allocate the p2m structure for a given pfn.  We need to check
 * that both the top and mid levels are allocated, and make sure the
 * parallel mfn tree is kept in sync.  We may race with other cpus, so
 * the new pages are installed with cmpxchg; if we lose the race then
 * simply free the page we allocated and use the one that's there.
 */
static bool alloc_p2m(unsigned long pfn)
{
	unsigned topidx, mididx;
	unsigned long *top_mfn_p, *mid_mfn;
	pte_t *ptep, *pte_pg;
	unsigned int level;
	unsigned long flags;
	unsigned long addr = (unsigned long)(xen_p2m_addr + pfn);
	unsigned long p2m_pfn;

	topidx = p2m_top_index(pfn);
	mididx = p2m_mid_index(pfn);

	ptep = lookup_address(addr, &level);
	BUG_ON(!ptep || level != PG_LEVEL_4K);
	pte_pg = (pte_t *)((unsigned long)ptep & ~(PAGE_SIZE - 1));

	if (pte_pg == p2m_missing_pte || pte_pg == p2m_identity_pte) {
		/* PMD level is missing, allocate a new one */
		ptep = alloc_p2m_pmd(addr, ptep, pte_pg);
		if (!ptep)
			return false;
	}

	if (p2m_top_mfn) {
		top_mfn_p = &p2m_top_mfn[topidx];
		mid_mfn = ACCESS_ONCE(p2m_top_mfn_p[topidx]);

		BUG_ON(virt_to_mfn(mid_mfn) != *top_mfn_p);

		if (mid_mfn == p2m_mid_missing_mfn) {
			/* Separately check the mid mfn level */
			unsigned long missing_mfn;
			unsigned long mid_mfn_mfn;
			unsigned long old_mfn;

			mid_mfn = alloc_p2m_page();
			if (!mid_mfn)
				return false;

			p2m_mid_mfn_init(mid_mfn, p2m_missing);

			missing_mfn = virt_to_mfn(p2m_mid_missing_mfn);
			mid_mfn_mfn = virt_to_mfn(mid_mfn);
			old_mfn = cmpxchg(top_mfn_p, missing_mfn, mid_mfn_mfn);
			if (old_mfn != missing_mfn) {
				free_p2m_page(mid_mfn);
				mid_mfn = mfn_to_virt(old_mfn);
			} else {
				p2m_top_mfn_p[topidx] = mid_mfn;
			}
		}
	} else {
		mid_mfn = NULL;
	}

	p2m_pfn = pte_pfn(ACCESS_ONCE(*ptep));
	if (p2m_pfn == PFN_DOWN(__pa(p2m_identity)) ||
	    p2m_pfn == PFN_DOWN(__pa(p2m_missing))) {
		/* p2m leaf page is missing */
		unsigned long *p2m;

		p2m = alloc_p2m_page();
		if (!p2m)
			return false;

		if (p2m_pfn == PFN_DOWN(__pa(p2m_missing)))
			p2m_init(p2m);
		else
			p2m_init_identity(p2m, pfn);

		spin_lock_irqsave(&p2m_update_lock, flags);

		if (pte_pfn(*ptep) == p2m_pfn) {
			set_pte(ptep,
				pfn_pte(PFN_DOWN(__pa(p2m)), PAGE_KERNEL));
			if (mid_mfn)
				mid_mfn[mididx] = virt_to_mfn(p2m);
			p2m = NULL;
		}

		spin_unlock_irqrestore(&p2m_update_lock, flags);

		if (p2m)
			free_p2m_page(p2m);
	}

	return true;
}

unsigned long __init set_phys_range_identity(unsigned long pfn_s,
				      unsigned long pfn_e)
{
	unsigned long pfn;

	if (unlikely(pfn_s >= xen_p2m_size))
		return 0;

	if (unlikely(xen_feature(XENFEAT_auto_translated_physmap)))
		return pfn_e - pfn_s;

	if (pfn_s > pfn_e)
		return 0;

	if (pfn_e > xen_p2m_size)
		pfn_e = xen_p2m_size;

	for (pfn = pfn_s; pfn < pfn_e; pfn++)
		xen_p2m_addr[pfn] = IDENTITY_FRAME(pfn);

	return pfn - pfn_s;
}

bool __set_phys_to_machine(unsigned long pfn, unsigned long mfn)
{
	pte_t *ptep;
	unsigned int level;

	/* don't track P2M changes in autotranslate guests */
	if (unlikely(xen_feature(XENFEAT_auto_translated_physmap)))
		return true;

	if (unlikely(pfn >= xen_p2m_size)) {
		BUG_ON(mfn != INVALID_P2M_ENTRY);
		return true;
	}

	if (likely(!xen_safe_write_ulong(xen_p2m_addr + pfn, mfn)))
		return true;

	ptep = lookup_address((unsigned long)(xen_p2m_addr + pfn), &level);
	BUG_ON(!ptep || level != PG_LEVEL_4K);

	if (pte_pfn(*ptep) == PFN_DOWN(__pa(p2m_missing)))
		return mfn == INVALID_P2M_ENTRY;

	if (pte_pfn(*ptep) == PFN_DOWN(__pa(p2m_identity)))
		return mfn == IDENTITY_FRAME(pfn);

	return false;
}

bool set_phys_to_machine(unsigned long pfn, unsigned long mfn)
{
	if (unlikely(!__set_phys_to_machine(pfn, mfn))) {
		if (!alloc_p2m(pfn))
			return false;

		return __set_phys_to_machine(pfn, mfn);
	}

	return true;
}

#define M2P_OVERRIDE_HASH_SHIFT	10
#define M2P_OVERRIDE_HASH	(1 << M2P_OVERRIDE_HASH_SHIFT)

static struct list_head *m2p_overrides;
static DEFINE_SPINLOCK(m2p_override_lock);

static void __init m2p_override_init(void)
{
	unsigned i;

	m2p_overrides = alloc_bootmem_align(
				sizeof(*m2p_overrides) * M2P_OVERRIDE_HASH,
				sizeof(unsigned long));

	for (i = 0; i < M2P_OVERRIDE_HASH; i++)
		INIT_LIST_HEAD(&m2p_overrides[i]);
}

static unsigned long mfn_hash(unsigned long mfn)
{
	return hash_long(mfn, M2P_OVERRIDE_HASH_SHIFT);
}

/* Add an MFN override for a particular page */
static int m2p_add_override(unsigned long mfn, struct page *page,
			    struct gnttab_map_grant_ref *kmap_op)
{
	unsigned long flags;
	unsigned long pfn;
	unsigned long uninitialized_var(address);
	unsigned level;
	pte_t *ptep = NULL;

	pfn = page_to_pfn(page);
	if (!PageHighMem(page)) {
		address = (unsigned long)__va(pfn << PAGE_SHIFT);
		ptep = lookup_address(address, &level);
		if (WARN(ptep == NULL || level != PG_LEVEL_4K,
			 "m2p_add_override: pfn %lx not mapped", pfn))
			return -EINVAL;
	}

	if (kmap_op != NULL) {
		if (!PageHighMem(page)) {
			struct multicall_space mcs =
				xen_mc_entry(sizeof(*kmap_op));

			MULTI_grant_table_op(mcs.mc,
					GNTTABOP_map_grant_ref, kmap_op, 1);

			xen_mc_issue(PARAVIRT_LAZY_MMU);
		}
	}
	spin_lock_irqsave(&m2p_override_lock, flags);
	list_add(&page->lru,  &m2p_overrides[mfn_hash(mfn)]);
	spin_unlock_irqrestore(&m2p_override_lock, flags);

	/* p2m(m2p(mfn)) == mfn: the mfn is already present somewhere in
	 * this domain. Set the FOREIGN_FRAME_BIT in the p2m for the other
	 * pfn so that the following mfn_to_pfn(mfn) calls will return the
	 * pfn from the m2p_override (the backend pfn) instead.
	 * We need to do this because the pages shared by the frontend
	 * (xen-blkfront) can be already locked (lock_page, called by
	 * do_read_cache_page); when the userspace backend tries to use them
	 * with direct_IO, mfn_to_pfn returns the pfn of the frontend, so
	 * do_blockdev_direct_IO is going to try to lock the same pages
	 * again resulting in a deadlock.
	 * As a side effect get_user_pages_fast might not be safe on the
	 * frontend pages while they are being shared with the backend,
	 * because mfn_to_pfn (that ends up being called by GUPF) will
	 * return the backend pfn rather than the frontend pfn. */
	pfn = mfn_to_pfn_no_overrides(mfn);
	if (__pfn_to_mfn(pfn) == mfn)
		set_phys_to_machine(pfn, FOREIGN_FRAME(mfn));

	return 0;
}

int set_foreign_p2m_mapping(struct gnttab_map_grant_ref *map_ops,
			    struct gnttab_map_grant_ref *kmap_ops,
			    struct page **pages, unsigned int count)
{
	int i, ret = 0;
	bool lazy = false;
	pte_t *pte;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 0;

	if (kmap_ops &&
	    !in_interrupt() &&
	    paravirt_get_lazy_mode() == PARAVIRT_LAZY_NONE) {
		arch_enter_lazy_mmu_mode();
		lazy = true;
	}

	for (i = 0; i < count; i++) {
		unsigned long mfn, pfn;

		/* Do not add to override if the map failed. */
		if (map_ops[i].status)
			continue;

		if (map_ops[i].flags & GNTMAP_contains_pte) {
			pte = (pte_t *)(mfn_to_virt(PFN_DOWN(map_ops[i].host_addr)) +
				(map_ops[i].host_addr & ~PAGE_MASK));
			mfn = pte_mfn(*pte);
		} else {
			mfn = PFN_DOWN(map_ops[i].dev_bus_addr);
		}
		pfn = page_to_pfn(pages[i]);

		WARN_ON(PagePrivate(pages[i]));
		SetPagePrivate(pages[i]);
		set_page_private(pages[i], mfn);
		pages[i]->index = pfn_to_mfn(pfn);

		if (unlikely(!set_phys_to_machine(pfn, FOREIGN_FRAME(mfn)))) {
			ret = -ENOMEM;
			goto out;
		}

		if (kmap_ops) {
			ret = m2p_add_override(mfn, pages[i], &kmap_ops[i]);
			if (ret)
				goto out;
		}
	}

out:
	if (lazy)
		arch_leave_lazy_mmu_mode();

	return ret;
}
EXPORT_SYMBOL_GPL(set_foreign_p2m_mapping);

static struct page *m2p_find_override(unsigned long mfn)
{
	unsigned long flags;
	struct list_head *bucket;
	struct page *p, *ret;

	if (unlikely(!m2p_overrides))
		return NULL;

	ret = NULL;
	bucket = &m2p_overrides[mfn_hash(mfn)];

	spin_lock_irqsave(&m2p_override_lock, flags);

	list_for_each_entry(p, bucket, lru) {
		if (page_private(p) == mfn) {
			ret = p;
			break;
		}
	}

	spin_unlock_irqrestore(&m2p_override_lock, flags);

	return ret;
}

static int m2p_remove_override(struct page *page,
			       struct gnttab_map_grant_ref *kmap_op,
			       unsigned long mfn)
{
	unsigned long flags;
	unsigned long pfn;
	unsigned long uninitialized_var(address);
	unsigned level;
	pte_t *ptep = NULL;

	pfn = page_to_pfn(page);

	if (!PageHighMem(page)) {
		address = (unsigned long)__va(pfn << PAGE_SHIFT);
		ptep = lookup_address(address, &level);

		if (WARN(ptep == NULL || level != PG_LEVEL_4K,
			 "m2p_remove_override: pfn %lx not mapped", pfn))
			return -EINVAL;
	}

	spin_lock_irqsave(&m2p_override_lock, flags);
	list_del(&page->lru);
	spin_unlock_irqrestore(&m2p_override_lock, flags);

	if (kmap_op != NULL) {
		if (!PageHighMem(page)) {
			struct multicall_space mcs;
			struct gnttab_unmap_and_replace *unmap_op;
			struct page *scratch_page = get_balloon_scratch_page();
			unsigned long scratch_page_address = (unsigned long)
				__va(page_to_pfn(scratch_page) << PAGE_SHIFT);

			/*
			 * It might be that we queued all the m2p grant table
			 * hypercalls in a multicall, then m2p_remove_override
			 * get called before the multicall has actually been
			 * issued. In this case handle is going to -1 because
			 * it hasn't been modified yet.
			 */
			if (kmap_op->handle == -1)
				xen_mc_flush();
			/*
			 * Now if kmap_op->handle is negative it means that the
			 * hypercall actually returned an error.
			 */
			if (kmap_op->handle == GNTST_general_error) {
				pr_warn("m2p_remove_override: pfn %lx mfn %lx, failed to modify kernel mappings",
					pfn, mfn);
				put_balloon_scratch_page();
				return -1;
			}

			xen_mc_batch();

			mcs = __xen_mc_entry(
				sizeof(struct gnttab_unmap_and_replace));
			unmap_op = mcs.args;
			unmap_op->host_addr = kmap_op->host_addr;
			unmap_op->new_addr = scratch_page_address;
			unmap_op->handle = kmap_op->handle;

			MULTI_grant_table_op(mcs.mc,
				GNTTABOP_unmap_and_replace, unmap_op, 1);

			mcs = __xen_mc_entry(0);
			MULTI_update_va_mapping(mcs.mc, scratch_page_address,
					pfn_pte(page_to_pfn(scratch_page),
					PAGE_KERNEL_RO), 0);

			xen_mc_issue(PARAVIRT_LAZY_MMU);

			kmap_op->host_addr = 0;
			put_balloon_scratch_page();
		}
	}

	/* p2m(m2p(mfn)) == FOREIGN_FRAME(mfn): the mfn is already present
	 * somewhere in this domain, even before being added to the
	 * m2p_override (see comment above in m2p_add_override).
	 * If there are no other entries in the m2p_override corresponding
	 * to this mfn, then remove the FOREIGN_FRAME_BIT from the p2m for
	 * the original pfn (the one shared by the frontend): the backend
	 * cannot do any IO on this page anymore because it has been
	 * unshared. Removing the FOREIGN_FRAME_BIT from the p2m entry of
	 * the original pfn causes mfn_to_pfn(mfn) to return the frontend
	 * pfn again. */
	mfn &= ~FOREIGN_FRAME_BIT;
	pfn = mfn_to_pfn_no_overrides(mfn);
	if (__pfn_to_mfn(pfn) == FOREIGN_FRAME(mfn) &&
			m2p_find_override(mfn) == NULL)
		set_phys_to_machine(pfn, mfn);

	return 0;
}

int clear_foreign_p2m_mapping(struct gnttab_unmap_grant_ref *unmap_ops,
			      struct gnttab_map_grant_ref *kmap_ops,
			      struct page **pages, unsigned int count)
{
	int i, ret = 0;
	bool lazy = false;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 0;

	if (kmap_ops &&
	    !in_interrupt() &&
	    paravirt_get_lazy_mode() == PARAVIRT_LAZY_NONE) {
		arch_enter_lazy_mmu_mode();
		lazy = true;
	}

	for (i = 0; i < count; i++) {
		unsigned long mfn = __pfn_to_mfn(page_to_pfn(pages[i]));
		unsigned long pfn = page_to_pfn(pages[i]);

		if (mfn == INVALID_P2M_ENTRY || !(mfn & FOREIGN_FRAME_BIT)) {
			ret = -EINVAL;
			goto out;
		}

		set_page_private(pages[i], INVALID_P2M_ENTRY);
		WARN_ON(!PagePrivate(pages[i]));
		ClearPagePrivate(pages[i]);
		set_phys_to_machine(pfn, pages[i]->index);

		if (kmap_ops)
			ret = m2p_remove_override(pages[i], &kmap_ops[i], mfn);
		if (ret)
			goto out;
	}

out:
	if (lazy)
		arch_leave_lazy_mmu_mode();
	return ret;
}
EXPORT_SYMBOL_GPL(clear_foreign_p2m_mapping);

unsigned long m2p_find_override_pfn(unsigned long mfn, unsigned long pfn)
{
	struct page *p = m2p_find_override(mfn);
	unsigned long ret = pfn;

	if (p)
		ret = page_to_pfn(p);

	return ret;
}
EXPORT_SYMBOL_GPL(m2p_find_override_pfn);

#ifdef CONFIG_XEN_DEBUG_FS
#include <linux/debugfs.h>
#include "debugfs.h"
static int p2m_dump_show(struct seq_file *m, void *v)
{
	static const char * const type_name[] = {
				[P2M_TYPE_IDENTITY] = "identity",
				[P2M_TYPE_MISSING] = "missing",
				[P2M_TYPE_PFN] = "pfn",
				[P2M_TYPE_UNKNOWN] = "abnormal"};
	unsigned long pfn, first_pfn;
	int type, prev_type;

	prev_type = xen_p2m_elem_type(0);
	first_pfn = 0;

	for (pfn = 0; pfn < xen_p2m_size; pfn++) {
		type = xen_p2m_elem_type(pfn);
		if (type != prev_type) {
			seq_printf(m, " [0x%lx->0x%lx] %s\n", first_pfn, pfn,
				   type_name[prev_type]);
			prev_type = type;
			first_pfn = pfn;
		}
	}
	seq_printf(m, " [0x%lx->0x%lx] %s\n", first_pfn, pfn,
		   type_name[prev_type]);
	return 0;
}

static int p2m_dump_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, p2m_dump_show, NULL);
}

static const struct file_operations p2m_dump_fops = {
	.open		= p2m_dump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct dentry *d_mmu_debug;

static int __init xen_p2m_debugfs(void)
{
	struct dentry *d_xen = xen_init_debugfs();

	if (d_xen == NULL)
		return -ENOMEM;

	d_mmu_debug = debugfs_create_dir("mmu", d_xen);

	debugfs_create_file("p2m", 0600, d_mmu_debug, NULL, &p2m_dump_fops);
	return 0;
}
fs_initcall(xen_p2m_debugfs);
#endif /* CONFIG_XEN_DEBUG_FS */
