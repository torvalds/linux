// SPDX-License-Identifier: GPL-2.0

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
#include <linux/export.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <asm/cache.h>
#include <asm/setup.h>
#include <linux/uaccess.h>

#include <asm/xen/page.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>
#include <xen/balloon.h>
#include <xen/grant_table.h>

#include "multicalls.h"
#include "xen-ops.h"

#define P2M_MID_PER_PAGE	(PAGE_SIZE / sizeof(unsigned long *))
#define P2M_TOP_PER_PAGE	(PAGE_SIZE / sizeof(unsigned long **))

#define MAX_P2M_PFN	(P2M_TOP_PER_PAGE * P2M_MID_PER_PAGE * P2M_PER_PAGE)

#define PMDS_PER_MID_PAGE	(P2M_MID_PER_PAGE / PTRS_PER_PTE)

unsigned long *xen_p2m_addr __read_mostly;
EXPORT_SYMBOL_GPL(xen_p2m_addr);
unsigned long xen_p2m_size __read_mostly;
EXPORT_SYMBOL_GPL(xen_p2m_size);
unsigned long xen_max_p2m_pfn __read_mostly;
EXPORT_SYMBOL_GPL(xen_max_p2m_pfn);

#ifdef CONFIG_XEN_MEMORY_HOTPLUG_LIMIT
#define P2M_LIMIT CONFIG_XEN_MEMORY_HOTPLUG_LIMIT
#else
#define P2M_LIMIT 0
#endif

static DEFINE_SPINLOCK(p2m_update_lock);

static unsigned long *p2m_mid_missing_mfn;
static unsigned long *p2m_top_mfn;
static unsigned long **p2m_top_mfn_p;
static unsigned long *p2m_missing;
static unsigned long *p2m_identity;
static pte_t *p2m_missing_pte;
static pte_t *p2m_identity_pte;

/*
 * Hint at last populated PFN.
 *
 * Used to set HYPERVISOR_shared_info->arch.max_pfn so the toolstack
 * can avoid scanning the whole P2M (which may be sized to account for
 * hotplugged memory).
 */
static unsigned long xen_p2m_last_pfn;

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
	if (unlikely(!slab_is_available())) {
		void *ptr = memblock_alloc(PAGE_SIZE, PAGE_SIZE);

		if (!ptr)
			panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
			      __func__, PAGE_SIZE, PAGE_SIZE);

		return ptr;
	}

	return (void *)__get_free_page(GFP_KERNEL);
}

static void __ref free_p2m_page(void *p)
{
	if (unlikely(!slab_is_available())) {
		memblock_free((unsigned long)p, PAGE_SIZE);
		return;
	}

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

	if (xen_start_info->flags & SIF_VIRT_P2M_4TOOLS)
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
	BUG_ON(HYPERVISOR_shared_info == &xen_dummy_shared_info);

	if (xen_start_info->flags & SIF_VIRT_P2M_4TOOLS)
		HYPERVISOR_shared_info->arch.pfn_to_mfn_frame_list_list = ~0UL;
	else
		HYPERVISOR_shared_info->arch.pfn_to_mfn_frame_list_list =
			virt_to_mfn(p2m_top_mfn);
	HYPERVISOR_shared_info->arch.max_pfn = xen_p2m_last_pfn;
	HYPERVISOR_shared_info->arch.p2m_generation = 0;
	HYPERVISOR_shared_info->arch.p2m_vaddr = (unsigned long)xen_p2m_addr;
	HYPERVISOR_shared_info->arch.p2m_cr3 =
		xen_pfn_to_cr3(virt_to_mfn(swapper_pg_dir));
}

/* Set up p2m_top to point to the domain-builder provided p2m pages */
void __init xen_build_dynamic_phys_to_machine(void)
{
	unsigned long pfn;

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
			mfns = alloc_p2m_page();
			copy_page(mfns, xen_p2m_addr + pfn);
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
				(unsigned long)(p2m + pfn) + i * PMD_SIZE);
			set_pmd(pmdp, __pmd(__pa(ptep) | _KERNPG_TABLE));
		}
	}
}

void __init xen_vmalloc_p2m_tree(void)
{
	static struct vm_struct vm;
	unsigned long p2m_limit;

	xen_p2m_last_pfn = xen_max_p2m_pfn;

	p2m_limit = (phys_addr_t)P2M_LIMIT * 1024 * 1024 * 1024 / PAGE_SIZE;
	vm.flags = VM_ALLOC;
	vm.size = ALIGN(sizeof(unsigned long) * max(xen_max_p2m_pfn, p2m_limit),
			PMD_SIZE * PMDS_PER_MID_PAGE);
	vm_area_register_early(&vm, PMD_SIZE * PMDS_PER_MID_PAGE);
	pr_notice("p2m virtual area at %p, size is %lx\n", vm.addr, vm.size);

	xen_max_p2m_pfn = vm.size / sizeof(unsigned long);

	xen_rebuild_p2m_list(vm.addr);

	xen_p2m_addr = vm.addr;
	xen_p2m_size = xen_max_p2m_pfn;

	xen_inv_extra_mem();
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
 * pmd.
 */
static pte_t *alloc_p2m_pmd(unsigned long addr, pte_t *pte_pg)
{
	pte_t *ptechk;
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
			HYPERVISOR_shared_info->arch.p2m_generation++;
			wmb(); /* Tools are synchronizing via p2m_generation. */
			set_pmd(pmdp,
				__pmd(__pa(pte_newpg[i]) | _KERNPG_TABLE));
			wmb(); /* Tools are synchronizing via p2m_generation. */
			HYPERVISOR_shared_info->arch.p2m_generation++;
			pte_newpg[i] = NULL;
		}

		spin_unlock_irqrestore(&p2m_update_lock, flags);

		if (pte_newpg[i]) {
			paravirt_release_pte(__pa(pte_newpg[i]) >> PAGE_SHIFT);
			free_p2m_page(pte_newpg[i]);
		}

		vaddr += PMD_SIZE;
	}

	return lookup_address(addr, &level);
}

/*
 * Fully allocate the p2m structure for a given pfn.  We need to check
 * that both the top and mid levels are allocated, and make sure the
 * parallel mfn tree is kept in sync.  We may race with other cpus, so
 * the new pages are installed with cmpxchg; if we lose the race then
 * simply free the page we allocated and use the one that's there.
 */
int xen_alloc_p2m_entry(unsigned long pfn)
{
	unsigned topidx;
	unsigned long *top_mfn_p, *mid_mfn;
	pte_t *ptep, *pte_pg;
	unsigned int level;
	unsigned long flags;
	unsigned long addr = (unsigned long)(xen_p2m_addr + pfn);
	unsigned long p2m_pfn;

	ptep = lookup_address(addr, &level);
	BUG_ON(!ptep || level != PG_LEVEL_4K);
	pte_pg = (pte_t *)((unsigned long)ptep & ~(PAGE_SIZE - 1));

	if (pte_pg == p2m_missing_pte || pte_pg == p2m_identity_pte) {
		/* PMD level is missing, allocate a new one */
		ptep = alloc_p2m_pmd(addr, pte_pg);
		if (!ptep)
			return -ENOMEM;
	}

	if (p2m_top_mfn && pfn < MAX_P2M_PFN) {
		topidx = p2m_top_index(pfn);
		top_mfn_p = &p2m_top_mfn[topidx];
		mid_mfn = READ_ONCE(p2m_top_mfn_p[topidx]);

		BUG_ON(virt_to_mfn(mid_mfn) != *top_mfn_p);

		if (mid_mfn == p2m_mid_missing_mfn) {
			/* Separately check the mid mfn level */
			unsigned long missing_mfn;
			unsigned long mid_mfn_mfn;
			unsigned long old_mfn;

			mid_mfn = alloc_p2m_page();
			if (!mid_mfn)
				return -ENOMEM;

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

	p2m_pfn = pte_pfn(READ_ONCE(*ptep));
	if (p2m_pfn == PFN_DOWN(__pa(p2m_identity)) ||
	    p2m_pfn == PFN_DOWN(__pa(p2m_missing))) {
		/* p2m leaf page is missing */
		unsigned long *p2m;

		p2m = alloc_p2m_page();
		if (!p2m)
			return -ENOMEM;

		if (p2m_pfn == PFN_DOWN(__pa(p2m_missing)))
			p2m_init(p2m);
		else
			p2m_init_identity(p2m, pfn & ~(P2M_PER_PAGE - 1));

		spin_lock_irqsave(&p2m_update_lock, flags);

		if (pte_pfn(*ptep) == p2m_pfn) {
			HYPERVISOR_shared_info->arch.p2m_generation++;
			wmb(); /* Tools are synchronizing via p2m_generation. */
			set_pte(ptep,
				pfn_pte(PFN_DOWN(__pa(p2m)), PAGE_KERNEL));
			wmb(); /* Tools are synchronizing via p2m_generation. */
			HYPERVISOR_shared_info->arch.p2m_generation++;
			if (mid_mfn)
				mid_mfn[p2m_mid_index(pfn)] = virt_to_mfn(p2m);
			p2m = NULL;
		}

		spin_unlock_irqrestore(&p2m_update_lock, flags);

		if (p2m)
			free_p2m_page(p2m);
	}

	/* Expanded the p2m? */
	if (pfn > xen_p2m_last_pfn) {
		xen_p2m_last_pfn = pfn;
		HYPERVISOR_shared_info->arch.max_pfn = xen_p2m_last_pfn;
	}

	return 0;
}
EXPORT_SYMBOL(xen_alloc_p2m_entry);

unsigned long __init set_phys_range_identity(unsigned long pfn_s,
				      unsigned long pfn_e)
{
	unsigned long pfn;

	if (unlikely(pfn_s >= xen_p2m_size))
		return 0;

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

	/* Only invalid entries allowed above the highest p2m covered frame. */
	if (unlikely(pfn >= xen_p2m_size))
		return mfn == INVALID_P2M_ENTRY;

	/*
	 * The interface requires atomic updates on p2m elements.
	 * xen_safe_write_ulong() is using an atomic store via asm().
	 */
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
		int ret;

		ret = xen_alloc_p2m_entry(pfn);
		if (ret < 0)
			return false;

		return __set_phys_to_machine(pfn, mfn);
	}

	return true;
}

int set_foreign_p2m_mapping(struct gnttab_map_grant_ref *map_ops,
			    struct gnttab_map_grant_ref *kmap_ops,
			    struct page **pages, unsigned int count)
{
	int i, ret = 0;
	pte_t *pte;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 0;

	if (kmap_ops) {
		ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref,
						kmap_ops, count);
		if (ret)
			goto out;
	}

	for (i = 0; i < count; i++) {
		unsigned long mfn, pfn;
		struct gnttab_unmap_grant_ref unmap[2];
		int rc;

		/* Do not add to override if the map failed. */
		if (map_ops[i].status != GNTST_okay ||
		    (kmap_ops && kmap_ops[i].status != GNTST_okay))
			continue;

		if (map_ops[i].flags & GNTMAP_contains_pte) {
			pte = (pte_t *)(mfn_to_virt(PFN_DOWN(map_ops[i].host_addr)) +
				(map_ops[i].host_addr & ~PAGE_MASK));
			mfn = pte_mfn(*pte);
		} else {
			mfn = PFN_DOWN(map_ops[i].dev_bus_addr);
		}
		pfn = page_to_pfn(pages[i]);

		WARN(pfn_to_mfn(pfn) != INVALID_P2M_ENTRY, "page must be ballooned");

		if (likely(set_phys_to_machine(pfn, FOREIGN_FRAME(mfn))))
			continue;

		/*
		 * Signal an error for this slot. This in turn requires
		 * immediate unmapping.
		 */
		map_ops[i].status = GNTST_general_error;
		unmap[0].host_addr = map_ops[i].host_addr,
		unmap[0].handle = map_ops[i].handle;
		map_ops[i].handle = INVALID_GRANT_HANDLE;
		if (map_ops[i].flags & GNTMAP_device_map)
			unmap[0].dev_bus_addr = map_ops[i].dev_bus_addr;
		else
			unmap[0].dev_bus_addr = 0;

		if (kmap_ops) {
			kmap_ops[i].status = GNTST_general_error;
			unmap[1].host_addr = kmap_ops[i].host_addr,
			unmap[1].handle = kmap_ops[i].handle;
			kmap_ops[i].handle = INVALID_GRANT_HANDLE;
			if (kmap_ops[i].flags & GNTMAP_device_map)
				unmap[1].dev_bus_addr = kmap_ops[i].dev_bus_addr;
			else
				unmap[1].dev_bus_addr = 0;
		}

		/*
		 * Pre-populate both status fields, to be recognizable in
		 * the log message below.
		 */
		unmap[0].status = 1;
		unmap[1].status = 1;

		rc = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
					       unmap, 1 + !!kmap_ops);
		if (rc || unmap[0].status != GNTST_okay ||
		    unmap[1].status != GNTST_okay)
			pr_err_once("gnttab unmap failed: rc=%d st0=%d st1=%d\n",
				    rc, unmap[0].status, unmap[1].status);
	}

out:
	return ret;
}

int clear_foreign_p2m_mapping(struct gnttab_unmap_grant_ref *unmap_ops,
			      struct gnttab_unmap_grant_ref *kunmap_ops,
			      struct page **pages, unsigned int count)
{
	int i, ret = 0;

	if (xen_feature(XENFEAT_auto_translated_physmap))
		return 0;

	for (i = 0; i < count; i++) {
		unsigned long mfn = __pfn_to_mfn(page_to_pfn(pages[i]));
		unsigned long pfn = page_to_pfn(pages[i]);

		if (mfn != INVALID_P2M_ENTRY && (mfn & FOREIGN_FRAME_BIT))
			set_phys_to_machine(pfn, INVALID_P2M_ENTRY);
		else
			ret = -EINVAL;
	}
	if (kunmap_ops)
		ret = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
						kunmap_ops, count) ?: ret;

	return ret;
}

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

DEFINE_SHOW_ATTRIBUTE(p2m_dump);

static struct dentry *d_mmu_debug;

static int __init xen_p2m_debugfs(void)
{
	struct dentry *d_xen = xen_init_debugfs();

	d_mmu_debug = debugfs_create_dir("mmu", d_xen);

	debugfs_create_file("p2m", 0600, d_mmu_debug, NULL, &p2m_dump_fops);
	return 0;
}
fs_initcall(xen_p2m_debugfs);
#endif /* CONFIG_XEN_DEBUG_FS */
