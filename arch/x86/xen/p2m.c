/*
 * Xen leaves the responsibility for maintaining p2m mappings to the
 * guests themselves, but it must also access and update the p2m array
 * during suspend/resume when all the pages are reallocated.
 *
 * The p2m table is logically a flat array, but we implement it as a
 * three-level tree to allow the address space to be sparse.
 *
 *                               Xen
 *                                |
 *     p2m_top              p2m_top_mfn
 *       /  \                   /   \
 * p2m_mid p2m_mid	p2m_mid_mfn p2m_mid_mfn
 *    / \      / \         /           /
 *  p2m p2m p2m p2m p2m p2m p2m ...
 *
 * The p2m_mid_mfn pages are mapped by p2m_top_mfn_p.
 *
 * The p2m_top and p2m_top_mfn levels are limited to 1 page, so the
 * maximum representable pseudo-physical address space is:
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
 *
 * We also have the possibility of setting 1-1 mappings on certain regions, so
 * that:
 *  pfn_to_mfn(0xc0000)=0xc0000
 *
 * The benefit of this is, that we can assume for non-RAM regions (think
 * PCI BARs, or ACPI spaces), we can create mappings easily b/c we
 * get the PFN value to match the MFN.
 *
 * For this to work efficiently we have one new page p2m_identity and
 * allocate (via reserved_brk) any other pages we need to cover the sides
 * (1GB or 4MB boundary violations). All entries in p2m_identity are set to
 * INVALID_P2M_ENTRY type (Xen toolstack only recognizes that and MFNs,
 * no other fancy value).
 *
 * On lookup we spot that the entry points to p2m_identity and return the
 * identity value instead of dereferencing and returning INVALID_P2M_ENTRY.
 * If the entry points to an allocated page, we just proceed as before and
 * return the PFN.  If the PFN has IDENTITY_FRAME_BIT set we unmask that in
 * appropriate functions (pfn_to_mfn).
 *
 * The reason for having the IDENTITY_FRAME_BIT instead of just returning the
 * PFN is that we could find ourselves where pfn_to_mfn(pfn)==pfn for a
 * non-identity pfn. To protect ourselves against we elect to set (and get) the
 * IDENTITY_FRAME_BIT on all identity mapped PFNs.
 *
 * This simplistic diagram is used to explain the more subtle piece of code.
 * There is also a digram of the P2M at the end that can help.
 * Imagine your E820 looking as so:
 *
 *                    1GB                                           2GB
 * /-------------------+---------\/----\         /----------\    /---+-----\
 * | System RAM        | Sys RAM ||ACPI|         | reserved |    | Sys RAM |
 * \-------------------+---------/\----/         \----------/    \---+-----/
 *                               ^- 1029MB                       ^- 2001MB
 *
 * [1029MB = 263424 (0x40500), 2001MB = 512256 (0x7D100),
 *  2048MB = 524288 (0x80000)]
 *
 * And dom0_mem=max:3GB,1GB is passed in to the guest, meaning memory past 1GB
 * is actually not present (would have to kick the balloon driver to put it in).
 *
 * When we are told to set the PFNs for identity mapping (see patch: "xen/setup:
 * Set identity mapping for non-RAM E820 and E820 gaps.") we pass in the start
 * of the PFN and the end PFN (263424 and 512256 respectively). The first step
 * is to reserve_brk a top leaf page if the p2m[1] is missing. The top leaf page
 * covers 512^2 of page estate (1GB) and in case the start or end PFN is not
 * aligned on 512^2*PAGE_SIZE (1GB) we loop on aligned 1GB PFNs from start pfn
 * to end pfn.  We reserve_brk top leaf pages if they are missing (means they
 * point to p2m_mid_missing).
 *
 * With the E820 example above, 263424 is not 1GB aligned so we allocate a
 * reserve_brk page which will cover the PFNs estate from 0x40000 to 0x80000.
 * Each entry in the allocate page is "missing" (points to p2m_missing).
 *
 * Next stage is to determine if we need to do a more granular boundary check
 * on the 4MB (or 2MB depending on architecture) off the start and end pfn's.
 * We check if the start pfn and end pfn violate that boundary check, and if
 * so reserve_brk a middle (p2m[x][y]) leaf page. This way we have a much finer
 * granularity of setting which PFNs are missing and which ones are identity.
 * In our example 263424 and 512256 both fail the check so we reserve_brk two
 * pages. Populate them with INVALID_P2M_ENTRY (so they both have "missing"
 * values) and assign them to p2m[1][2] and p2m[1][488] respectively.
 *
 * At this point we would at minimum reserve_brk one page, but could be up to
 * three. Each call to set_phys_range_identity has at maximum a three page
 * cost. If we were to query the P2M at this stage, all those entries from
 * start PFN through end PFN (so 1029MB -> 2001MB) would return
 * INVALID_P2M_ENTRY ("missing").
 *
 * The next step is to walk from the start pfn to the end pfn setting
 * the IDENTITY_FRAME_BIT on each PFN. This is done in set_phys_range_identity.
 * If we find that the middle leaf is pointing to p2m_missing we can swap it
 * over to p2m_identity - this way covering 4MB (or 2MB) PFN space.  At this
 * point we do not need to worry about boundary aligment (so no need to
 * reserve_brk a middle page, figure out which PFNs are "missing" and which
 * ones are identity), as that has been done earlier.  If we find that the
 * middle leaf is not occupied by p2m_identity or p2m_missing, we dereference
 * that page (which covers 512 PFNs) and set the appropriate PFN with
 * IDENTITY_FRAME_BIT. In our example 263424 and 512256 end up there, and we
 * set from p2m[1][2][256->511] and p2m[1][488][0->256] with
 * IDENTITY_FRAME_BIT set.
 *
 * All other regions that are void (or not filled) either point to p2m_missing
 * (considered missing) or have the default value of INVALID_P2M_ENTRY (also
 * considered missing). In our case, p2m[1][2][0->255] and p2m[1][488][257->511]
 * contain the INVALID_P2M_ENTRY value and are considered "missing."
 *
 * This is what the p2m ends up looking (for the E820 above) with this
 * fabulous drawing:
 *
 *    p2m         /--------------\
 *  /-----\       | &mfn_list[0],|                           /-----------------\
 *  |  0  |------>| &mfn_list[1],|    /---------------\      | ~0, ~0, ..      |
 *  |-----|       |  ..., ~0, ~0 |    | ~0, ~0, [x]---+----->| IDENTITY [@256] |
 *  |  1  |---\   \--------------/    | [p2m_identity]+\     | IDENTITY [@257] |
 *  |-----|    \                      | [p2m_identity]+\\    | ....            |
 *  |  2  |--\  \-------------------->|  ...          | \\   \----------------/
 *  |-----|   \                       \---------------/  \\
 *  |  3  |\   \                                          \\  p2m_identity
 *  |-----| \   \-------------------->/---------------\   /-----------------\
 *  | ..  +->+                        | [p2m_identity]+-->| ~0, ~0, ~0, ... |
 *  \-----/ /                         | [p2m_identity]+-->| ..., ~0         |
 *         / /---------------\        | ....          |   \-----------------/
 *        /  | IDENTITY[@0]  |      /-+-[x], ~0, ~0.. |
 *       /   | IDENTITY[@256]|<----/  \---------------/
 *      /    | ~0, ~0, ....  |
 *     |     \---------------/
 *     |
 *     p2m_missing             p2m_missing
 * /------------------\     /------------\
 * | [p2m_mid_missing]+---->| ~0, ~0, ~0 |
 * | [p2m_mid_missing]+---->| ..., ~0    |
 * \------------------/     \------------/
 *
 * where ~0 is INVALID_P2M_ENTRY. IDENTITY is (PFN | IDENTITY_BIT)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#include <asm/cache.h>
#include <asm/setup.h>

#include <asm/xen/page.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>
#include <xen/grant_table.h>

#include "multicalls.h"
#include "xen-ops.h"

static void __init m2p_override_init(void);

unsigned long xen_max_p2m_pfn __read_mostly;

#define P2M_PER_PAGE		(PAGE_SIZE / sizeof(unsigned long))
#define P2M_MID_PER_PAGE	(PAGE_SIZE / sizeof(unsigned long *))
#define P2M_TOP_PER_PAGE	(PAGE_SIZE / sizeof(unsigned long **))

#define MAX_P2M_PFN		(P2M_TOP_PER_PAGE * P2M_MID_PER_PAGE * P2M_PER_PAGE)

/* Placeholders for holes in the address space */
static RESERVE_BRK_ARRAY(unsigned long, p2m_missing, P2M_PER_PAGE);
static RESERVE_BRK_ARRAY(unsigned long *, p2m_mid_missing, P2M_MID_PER_PAGE);
static RESERVE_BRK_ARRAY(unsigned long, p2m_mid_missing_mfn, P2M_MID_PER_PAGE);

static RESERVE_BRK_ARRAY(unsigned long **, p2m_top, P2M_TOP_PER_PAGE);
static RESERVE_BRK_ARRAY(unsigned long, p2m_top_mfn, P2M_TOP_PER_PAGE);
static RESERVE_BRK_ARRAY(unsigned long *, p2m_top_mfn_p, P2M_TOP_PER_PAGE);

static RESERVE_BRK_ARRAY(unsigned long, p2m_identity, P2M_PER_PAGE);

RESERVE_BRK(p2m_mid, PAGE_SIZE * (MAX_DOMAIN_PAGES / (P2M_PER_PAGE * P2M_MID_PER_PAGE)));
RESERVE_BRK(p2m_mid_mfn, PAGE_SIZE * (MAX_DOMAIN_PAGES / (P2M_PER_PAGE * P2M_MID_PER_PAGE)));

/* We might hit two boundary violations at the start and end, at max each
 * boundary violation will require three middle nodes. */
RESERVE_BRK(p2m_mid_identity, PAGE_SIZE * 2 * 3);

/* When we populate back during bootup, the amount of pages can vary. The
 * max we have is seen is 395979, but that does not mean it can't be more.
 * Some machines can have 3GB I/O holes even. With early_can_reuse_p2m_middle
 * it can re-use Xen provided mfn_list array, so we only need to allocate at
 * most three P2M top nodes. */
RESERVE_BRK(p2m_populated, PAGE_SIZE * 3);

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

static void p2m_top_init(unsigned long ***top)
{
	unsigned i;

	for (i = 0; i < P2M_TOP_PER_PAGE; i++)
		top[i] = p2m_mid_missing;
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

static void p2m_mid_init(unsigned long **mid)
{
	unsigned i;

	for (i = 0; i < P2M_MID_PER_PAGE; i++)
		mid[i] = p2m_missing;
}

static void p2m_mid_mfn_init(unsigned long *mid)
{
	unsigned i;

	for (i = 0; i < P2M_MID_PER_PAGE; i++)
		mid[i] = virt_to_mfn(p2m_missing);
}

static void p2m_init(unsigned long *p2m)
{
	unsigned i;

	for (i = 0; i < P2M_MID_PER_PAGE; i++)
		p2m[i] = INVALID_P2M_ENTRY;
}

/*
 * Build the parallel p2m_top_mfn and p2m_mid_mfn structures
 *
 * This is called both at boot time, and after resuming from suspend:
 * - At boot time we're called very early, and must use extend_brk()
 *   to allocate memory.
 *
 * - After resume we're called from within stop_machine, but the mfn
 *   tree should alreay be completely allocated.
 */
void __ref xen_build_mfn_list_list(void)
{
	unsigned long pfn;

	/* Pre-initialize p2m_top_mfn to be completely missing */
	if (p2m_top_mfn == NULL) {
		p2m_mid_missing_mfn = extend_brk(PAGE_SIZE, PAGE_SIZE);
		p2m_mid_mfn_init(p2m_mid_missing_mfn);

		p2m_top_mfn_p = extend_brk(PAGE_SIZE, PAGE_SIZE);
		p2m_top_mfn_p_init(p2m_top_mfn_p);

		p2m_top_mfn = extend_brk(PAGE_SIZE, PAGE_SIZE);
		p2m_top_mfn_init(p2m_top_mfn);
	} else {
		/* Reinitialise, mfn's all change after migration */
		p2m_mid_mfn_init(p2m_mid_missing_mfn);
	}

	for (pfn = 0; pfn < xen_max_p2m_pfn; pfn += P2M_PER_PAGE) {
		unsigned topidx = p2m_top_index(pfn);
		unsigned mididx = p2m_mid_index(pfn);
		unsigned long **mid;
		unsigned long *mid_mfn_p;

		mid = p2m_top[topidx];
		mid_mfn_p = p2m_top_mfn_p[topidx];

		/* Don't bother allocating any mfn mid levels if
		 * they're just missing, just update the stored mfn,
		 * since all could have changed over a migrate.
		 */
		if (mid == p2m_mid_missing) {
			BUG_ON(mididx);
			BUG_ON(mid_mfn_p != p2m_mid_missing_mfn);
			p2m_top_mfn[topidx] = virt_to_mfn(p2m_mid_missing_mfn);
			pfn += (P2M_MID_PER_PAGE - 1) * P2M_PER_PAGE;
			continue;
		}

		if (mid_mfn_p == p2m_mid_missing_mfn) {
			/*
			 * XXX boot-time only!  We should never find
			 * missing parts of the mfn tree after
			 * runtime.  extend_brk() will BUG if we call
			 * it too late.
			 */
			mid_mfn_p = extend_brk(PAGE_SIZE, PAGE_SIZE);
			p2m_mid_mfn_init(mid_mfn_p);

			p2m_top_mfn_p[topidx] = mid_mfn_p;
		}

		p2m_top_mfn[topidx] = virt_to_mfn(mid_mfn_p);
		mid_mfn_p[mididx] = virt_to_mfn(mid[mididx]);
	}
}

void xen_setup_mfn_list_list(void)
{
	BUG_ON(HYPERVISOR_shared_info == &xen_dummy_shared_info);

	HYPERVISOR_shared_info->arch.pfn_to_mfn_frame_list_list =
		virt_to_mfn(p2m_top_mfn);
	HYPERVISOR_shared_info->arch.max_pfn = xen_max_p2m_pfn;
}

/* Set up p2m_top to point to the domain-builder provided p2m pages */
void __init xen_build_dynamic_phys_to_machine(void)
{
	unsigned long *mfn_list = (unsigned long *)xen_start_info->mfn_list;
	unsigned long max_pfn = min(MAX_DOMAIN_PAGES, xen_start_info->nr_pages);
	unsigned long pfn;

	xen_max_p2m_pfn = max_pfn;

	p2m_missing = extend_brk(PAGE_SIZE, PAGE_SIZE);
	p2m_init(p2m_missing);

	p2m_mid_missing = extend_brk(PAGE_SIZE, PAGE_SIZE);
	p2m_mid_init(p2m_mid_missing);

	p2m_top = extend_brk(PAGE_SIZE, PAGE_SIZE);
	p2m_top_init(p2m_top);

	p2m_identity = extend_brk(PAGE_SIZE, PAGE_SIZE);
	p2m_init(p2m_identity);

	/*
	 * The domain builder gives us a pre-constructed p2m array in
	 * mfn_list for all the pages initially given to us, so we just
	 * need to graft that into our tree structure.
	 */
	for (pfn = 0; pfn < max_pfn; pfn += P2M_PER_PAGE) {
		unsigned topidx = p2m_top_index(pfn);
		unsigned mididx = p2m_mid_index(pfn);

		if (p2m_top[topidx] == p2m_mid_missing) {
			unsigned long **mid = extend_brk(PAGE_SIZE, PAGE_SIZE);
			p2m_mid_init(mid);

			p2m_top[topidx] = mid;
		}

		/*
		 * As long as the mfn_list has enough entries to completely
		 * fill a p2m page, pointing into the array is ok. But if
		 * not the entries beyond the last pfn will be undefined.
		 */
		if (unlikely(pfn + P2M_PER_PAGE > max_pfn)) {
			unsigned long p2midx;

			p2midx = max_pfn % P2M_PER_PAGE;
			for ( ; p2midx < P2M_PER_PAGE; p2midx++)
				mfn_list[pfn + p2midx] = INVALID_P2M_ENTRY;
		}
		p2m_top[topidx][mididx] = &mfn_list[pfn];
	}

	m2p_override_init();
}

unsigned long get_phys_to_machine(unsigned long pfn)
{
	unsigned topidx, mididx, idx;

	if (unlikely(pfn >= MAX_P2M_PFN))
		return INVALID_P2M_ENTRY;

	topidx = p2m_top_index(pfn);
	mididx = p2m_mid_index(pfn);
	idx = p2m_index(pfn);

	/*
	 * The INVALID_P2M_ENTRY is filled in both p2m_*identity
	 * and in p2m_*missing, so returning the INVALID_P2M_ENTRY
	 * would be wrong.
	 */
	if (p2m_top[topidx][mididx] == p2m_identity)
		return IDENTITY_FRAME(pfn);

	return p2m_top[topidx][mididx][idx];
}
EXPORT_SYMBOL_GPL(get_phys_to_machine);

static void *alloc_p2m_page(void)
{
	return (void *)__get_free_page(GFP_KERNEL | __GFP_REPEAT);
}

static void free_p2m_page(void *p)
{
	free_page((unsigned long)p);
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
	unsigned long ***top_p, **mid;
	unsigned long *top_mfn_p, *mid_mfn;

	topidx = p2m_top_index(pfn);
	mididx = p2m_mid_index(pfn);

	top_p = &p2m_top[topidx];
	mid = *top_p;

	if (mid == p2m_mid_missing) {
		/* Mid level is missing, allocate a new one */
		mid = alloc_p2m_page();
		if (!mid)
			return false;

		p2m_mid_init(mid);

		if (cmpxchg(top_p, p2m_mid_missing, mid) != p2m_mid_missing)
			free_p2m_page(mid);
	}

	top_mfn_p = &p2m_top_mfn[topidx];
	mid_mfn = p2m_top_mfn_p[topidx];

	BUG_ON(virt_to_mfn(mid_mfn) != *top_mfn_p);

	if (mid_mfn == p2m_mid_missing_mfn) {
		/* Separately check the mid mfn level */
		unsigned long missing_mfn;
		unsigned long mid_mfn_mfn;

		mid_mfn = alloc_p2m_page();
		if (!mid_mfn)
			return false;

		p2m_mid_mfn_init(mid_mfn);

		missing_mfn = virt_to_mfn(p2m_mid_missing_mfn);
		mid_mfn_mfn = virt_to_mfn(mid_mfn);
		if (cmpxchg(top_mfn_p, missing_mfn, mid_mfn_mfn) != missing_mfn)
			free_p2m_page(mid_mfn);
		else
			p2m_top_mfn_p[topidx] = mid_mfn;
	}

	if (p2m_top[topidx][mididx] == p2m_identity ||
	    p2m_top[topidx][mididx] == p2m_missing) {
		/* p2m leaf page is missing */
		unsigned long *p2m;
		unsigned long *p2m_orig = p2m_top[topidx][mididx];

		p2m = alloc_p2m_page();
		if (!p2m)
			return false;

		p2m_init(p2m);

		if (cmpxchg(&mid[mididx], p2m_orig, p2m) != p2m_orig)
			free_p2m_page(p2m);
		else
			mid_mfn[mididx] = virt_to_mfn(p2m);
	}

	return true;
}

static bool __init early_alloc_p2m_middle(unsigned long pfn, bool check_boundary)
{
	unsigned topidx, mididx, idx;
	unsigned long *p2m;
	unsigned long *mid_mfn_p;

	topidx = p2m_top_index(pfn);
	mididx = p2m_mid_index(pfn);
	idx = p2m_index(pfn);

	/* Pfff.. No boundary cross-over, lets get out. */
	if (!idx && check_boundary)
		return false;

	WARN(p2m_top[topidx][mididx] == p2m_identity,
		"P2M[%d][%d] == IDENTITY, should be MISSING (or alloced)!\n",
		topidx, mididx);

	/*
	 * Could be done by xen_build_dynamic_phys_to_machine..
	 */
	if (p2m_top[topidx][mididx] != p2m_missing)
		return false;

	/* Boundary cross-over for the edges: */
	p2m = extend_brk(PAGE_SIZE, PAGE_SIZE);

	p2m_init(p2m);

	p2m_top[topidx][mididx] = p2m;

	/* For save/restore we need to MFN of the P2M saved */

	mid_mfn_p = p2m_top_mfn_p[topidx];
	WARN(mid_mfn_p[mididx] != virt_to_mfn(p2m_missing),
		"P2M_TOP_P[%d][%d] != MFN of p2m_missing!\n",
		topidx, mididx);
	mid_mfn_p[mididx] = virt_to_mfn(p2m);

	return true;
}

static bool __init early_alloc_p2m(unsigned long pfn)
{
	unsigned topidx = p2m_top_index(pfn);
	unsigned long *mid_mfn_p;
	unsigned long **mid;

	mid = p2m_top[topidx];
	mid_mfn_p = p2m_top_mfn_p[topidx];
	if (mid == p2m_mid_missing) {
		mid = extend_brk(PAGE_SIZE, PAGE_SIZE);

		p2m_mid_init(mid);

		p2m_top[topidx] = mid;

		BUG_ON(mid_mfn_p != p2m_mid_missing_mfn);
	}
	/* And the save/restore P2M tables.. */
	if (mid_mfn_p == p2m_mid_missing_mfn) {
		mid_mfn_p = extend_brk(PAGE_SIZE, PAGE_SIZE);
		p2m_mid_mfn_init(mid_mfn_p);

		p2m_top_mfn_p[topidx] = mid_mfn_p;
		p2m_top_mfn[topidx] = virt_to_mfn(mid_mfn_p);
		/* Note: we don't set mid_mfn_p[midix] here,
		 * look in early_alloc_p2m_middle */
	}
	return true;
}

/*
 * Skim over the P2M tree looking at pages that are either filled with
 * INVALID_P2M_ENTRY or with 1:1 PFNs. If found, re-use that page and
 * replace the P2M leaf with a p2m_missing or p2m_identity.
 * Stick the old page in the new P2M tree location.
 */
bool __init early_can_reuse_p2m_middle(unsigned long set_pfn, unsigned long set_mfn)
{
	unsigned topidx;
	unsigned mididx;
	unsigned ident_pfns;
	unsigned inv_pfns;
	unsigned long *p2m;
	unsigned long *mid_mfn_p;
	unsigned idx;
	unsigned long pfn;

	/* We only look when this entails a P2M middle layer */
	if (p2m_index(set_pfn))
		return false;

	for (pfn = 0; pfn < MAX_DOMAIN_PAGES; pfn += P2M_PER_PAGE) {
		topidx = p2m_top_index(pfn);

		if (!p2m_top[topidx])
			continue;

		if (p2m_top[topidx] == p2m_mid_missing)
			continue;

		mididx = p2m_mid_index(pfn);
		p2m = p2m_top[topidx][mididx];
		if (!p2m)
			continue;

		if ((p2m == p2m_missing) || (p2m == p2m_identity))
			continue;

		if ((unsigned long)p2m == INVALID_P2M_ENTRY)
			continue;

		ident_pfns = 0;
		inv_pfns = 0;
		for (idx = 0; idx < P2M_PER_PAGE; idx++) {
			/* IDENTITY_PFNs are 1:1 */
			if (p2m[idx] == IDENTITY_FRAME(pfn + idx))
				ident_pfns++;
			else if (p2m[idx] == INVALID_P2M_ENTRY)
				inv_pfns++;
			else
				break;
		}
		if ((ident_pfns == P2M_PER_PAGE) || (inv_pfns == P2M_PER_PAGE))
			goto found;
	}
	return false;
found:
	/* Found one, replace old with p2m_identity or p2m_missing */
	p2m_top[topidx][mididx] = (ident_pfns ? p2m_identity : p2m_missing);
	/* And the other for save/restore.. */
	mid_mfn_p = p2m_top_mfn_p[topidx];
	/* NOTE: Even if it is a p2m_identity it should still be point to
	 * a page filled with INVALID_P2M_ENTRY entries. */
	mid_mfn_p[mididx] = virt_to_mfn(p2m_missing);

	/* Reset where we want to stick the old page in. */
	topidx = p2m_top_index(set_pfn);
	mididx = p2m_mid_index(set_pfn);

	/* This shouldn't happen */
	if (WARN_ON(p2m_top[topidx] == p2m_mid_missing))
		early_alloc_p2m(set_pfn);

	if (WARN_ON(p2m_top[topidx][mididx] != p2m_missing))
		return false;

	p2m_init(p2m);
	p2m_top[topidx][mididx] = p2m;
	mid_mfn_p = p2m_top_mfn_p[topidx];
	mid_mfn_p[mididx] = virt_to_mfn(p2m);

	return true;
}
bool __init early_set_phys_to_machine(unsigned long pfn, unsigned long mfn)
{
	if (unlikely(!__set_phys_to_machine(pfn, mfn)))  {
		if (!early_alloc_p2m(pfn))
			return false;

		if (early_can_reuse_p2m_middle(pfn, mfn))
			return __set_phys_to_machine(pfn, mfn);

		if (!early_alloc_p2m_middle(pfn, false /* boundary crossover OK!*/))
			return false;

		if (!__set_phys_to_machine(pfn, mfn))
			return false;
	}

	return true;
}
unsigned long __init set_phys_range_identity(unsigned long pfn_s,
				      unsigned long pfn_e)
{
	unsigned long pfn;

	if (unlikely(pfn_s >= MAX_P2M_PFN || pfn_e >= MAX_P2M_PFN))
		return 0;

	if (unlikely(xen_feature(XENFEAT_auto_translated_physmap)))
		return pfn_e - pfn_s;

	if (pfn_s > pfn_e)
		return 0;

	for (pfn = (pfn_s & ~(P2M_MID_PER_PAGE * P2M_PER_PAGE - 1));
		pfn < ALIGN(pfn_e, (P2M_MID_PER_PAGE * P2M_PER_PAGE));
		pfn += P2M_MID_PER_PAGE * P2M_PER_PAGE)
	{
		WARN_ON(!early_alloc_p2m(pfn));
	}

	early_alloc_p2m_middle(pfn_s, true);
	early_alloc_p2m_middle(pfn_e, true);

	for (pfn = pfn_s; pfn < pfn_e; pfn++)
		if (!__set_phys_to_machine(pfn, IDENTITY_FRAME(pfn)))
			break;

	if (!WARN((pfn - pfn_s) != (pfn_e - pfn_s),
		"Identity mapping failed. We are %ld short of 1-1 mappings!\n",
		(pfn_e - pfn_s) - (pfn - pfn_s)))
		printk(KERN_DEBUG "1-1 mapping on %lx->%lx\n", pfn_s, pfn);

	return pfn - pfn_s;
}

/* Try to install p2m mapping; fail if intermediate bits missing */
bool __set_phys_to_machine(unsigned long pfn, unsigned long mfn)
{
	unsigned topidx, mididx, idx;

	if (unlikely(xen_feature(XENFEAT_auto_translated_physmap))) {
		BUG_ON(pfn != mfn && mfn != INVALID_P2M_ENTRY);
		return true;
	}
	if (unlikely(pfn >= MAX_P2M_PFN)) {
		BUG_ON(mfn != INVALID_P2M_ENTRY);
		return true;
	}

	topidx = p2m_top_index(pfn);
	mididx = p2m_mid_index(pfn);
	idx = p2m_index(pfn);

	/* For sparse holes were the p2m leaf has real PFN along with
	 * PCI holes, stick in the PFN as the MFN value.
	 */
	if (mfn != INVALID_P2M_ENTRY && (mfn & IDENTITY_FRAME_BIT)) {
		if (p2m_top[topidx][mididx] == p2m_identity)
			return true;

		/* Swap over from MISSING to IDENTITY if needed. */
		if (p2m_top[topidx][mididx] == p2m_missing) {
			WARN_ON(cmpxchg(&p2m_top[topidx][mididx], p2m_missing,
				p2m_identity) != p2m_missing);
			return true;
		}
	}

	if (p2m_top[topidx][mididx] == p2m_missing)
		return mfn == INVALID_P2M_ENTRY;

	p2m_top[topidx][mididx][idx] = mfn;

	return true;
}

bool set_phys_to_machine(unsigned long pfn, unsigned long mfn)
{
	if (unlikely(!__set_phys_to_machine(pfn, mfn)))  {
		if (!alloc_p2m(pfn))
			return false;

		if (!__set_phys_to_machine(pfn, mfn))
			return false;
	}

	return true;
}

#define M2P_OVERRIDE_HASH_SHIFT	10
#define M2P_OVERRIDE_HASH	(1 << M2P_OVERRIDE_HASH_SHIFT)

static RESERVE_BRK_ARRAY(struct list_head, m2p_overrides, M2P_OVERRIDE_HASH);
static DEFINE_SPINLOCK(m2p_override_lock);

static void __init m2p_override_init(void)
{
	unsigned i;

	m2p_overrides = extend_brk(sizeof(*m2p_overrides) * M2P_OVERRIDE_HASH,
				   sizeof(unsigned long));

	for (i = 0; i < M2P_OVERRIDE_HASH; i++)
		INIT_LIST_HEAD(&m2p_overrides[i]);
}

static unsigned long mfn_hash(unsigned long mfn)
{
	return hash_long(mfn, M2P_OVERRIDE_HASH_SHIFT);
}

/* Add an MFN override for a particular page */
int m2p_add_override(unsigned long mfn, struct page *page,
		struct gnttab_map_grant_ref *kmap_op)
{
	unsigned long flags;
	unsigned long pfn;
	unsigned long uninitialized_var(address);
	unsigned level;
	pte_t *ptep = NULL;
	int ret = 0;

	pfn = page_to_pfn(page);
	if (!PageHighMem(page)) {
		address = (unsigned long)__va(pfn << PAGE_SHIFT);
		ptep = lookup_address(address, &level);
		if (WARN(ptep == NULL || level != PG_LEVEL_4K,
					"m2p_add_override: pfn %lx not mapped", pfn))
			return -EINVAL;
	}
	WARN_ON(PagePrivate(page));
	SetPagePrivate(page);
	set_page_private(page, mfn);
	page->index = pfn_to_mfn(pfn);

	if (unlikely(!set_phys_to_machine(pfn, FOREIGN_FRAME(mfn))))
		return -ENOMEM;

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
	ret = __get_user(pfn, &machine_to_phys_mapping[mfn]);
	if (ret == 0 && get_phys_to_machine(pfn) == mfn)
		set_phys_to_machine(pfn, FOREIGN_FRAME(mfn));

	return 0;
}
EXPORT_SYMBOL_GPL(m2p_add_override);
int m2p_remove_override(struct page *page,
		struct gnttab_map_grant_ref *kmap_op)
{
	unsigned long flags;
	unsigned long mfn;
	unsigned long pfn;
	unsigned long uninitialized_var(address);
	unsigned level;
	pte_t *ptep = NULL;
	int ret = 0;

	pfn = page_to_pfn(page);
	mfn = get_phys_to_machine(pfn);
	if (mfn == INVALID_P2M_ENTRY || !(mfn & FOREIGN_FRAME_BIT))
		return -EINVAL;

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
	WARN_ON(!PagePrivate(page));
	ClearPagePrivate(page);

	set_phys_to_machine(pfn, page->index);
	if (kmap_op != NULL) {
		if (!PageHighMem(page)) {
			struct multicall_space mcs;
			struct gnttab_unmap_grant_ref *unmap_op;

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
				printk(KERN_WARNING "m2p_remove_override: "
						"pfn %lx mfn %lx, failed to modify kernel mappings",
						pfn, mfn);
				return -1;
			}

			mcs = xen_mc_entry(
					sizeof(struct gnttab_unmap_grant_ref));
			unmap_op = mcs.args;
			unmap_op->host_addr = kmap_op->host_addr;
			unmap_op->handle = kmap_op->handle;
			unmap_op->dev_bus_addr = 0;

			MULTI_grant_table_op(mcs.mc,
					GNTTABOP_unmap_grant_ref, unmap_op, 1);

			xen_mc_issue(PARAVIRT_LAZY_MMU);

			set_pte_at(&init_mm, address, ptep,
					pfn_pte(pfn, PAGE_KERNEL));
			__flush_tlb_single(address);
			kmap_op->host_addr = 0;
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
	ret = __get_user(pfn, &machine_to_phys_mapping[mfn]);
	if (ret == 0 && get_phys_to_machine(pfn) == FOREIGN_FRAME(mfn) &&
			m2p_find_override(mfn) == NULL)
		set_phys_to_machine(pfn, mfn);

	return 0;
}
EXPORT_SYMBOL_GPL(m2p_remove_override);

struct page *m2p_find_override(unsigned long mfn)
{
	unsigned long flags;
	struct list_head *bucket = &m2p_overrides[mfn_hash(mfn)];
	struct page *p, *ret;

	ret = NULL;

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
	static const char * const level_name[] = { "top", "middle",
						"entry", "abnormal", "error"};
#define TYPE_IDENTITY 0
#define TYPE_MISSING 1
#define TYPE_PFN 2
#define TYPE_UNKNOWN 3
	static const char * const type_name[] = {
				[TYPE_IDENTITY] = "identity",
				[TYPE_MISSING] = "missing",
				[TYPE_PFN] = "pfn",
				[TYPE_UNKNOWN] = "abnormal"};
	unsigned long pfn, prev_pfn_type = 0, prev_pfn_level = 0;
	unsigned int uninitialized_var(prev_level);
	unsigned int uninitialized_var(prev_type);

	if (!p2m_top)
		return 0;

	for (pfn = 0; pfn < MAX_DOMAIN_PAGES; pfn++) {
		unsigned topidx = p2m_top_index(pfn);
		unsigned mididx = p2m_mid_index(pfn);
		unsigned idx = p2m_index(pfn);
		unsigned lvl, type;

		lvl = 4;
		type = TYPE_UNKNOWN;
		if (p2m_top[topidx] == p2m_mid_missing) {
			lvl = 0; type = TYPE_MISSING;
		} else if (p2m_top[topidx] == NULL) {
			lvl = 0; type = TYPE_UNKNOWN;
		} else if (p2m_top[topidx][mididx] == NULL) {
			lvl = 1; type = TYPE_UNKNOWN;
		} else if (p2m_top[topidx][mididx] == p2m_identity) {
			lvl = 1; type = TYPE_IDENTITY;
		} else if (p2m_top[topidx][mididx] == p2m_missing) {
			lvl = 1; type = TYPE_MISSING;
		} else if (p2m_top[topidx][mididx][idx] == 0) {
			lvl = 2; type = TYPE_UNKNOWN;
		} else if (p2m_top[topidx][mididx][idx] == IDENTITY_FRAME(pfn)) {
			lvl = 2; type = TYPE_IDENTITY;
		} else if (p2m_top[topidx][mididx][idx] == INVALID_P2M_ENTRY) {
			lvl = 2; type = TYPE_MISSING;
		} else if (p2m_top[topidx][mididx][idx] == pfn) {
			lvl = 2; type = TYPE_PFN;
		} else if (p2m_top[topidx][mididx][idx] != pfn) {
			lvl = 2; type = TYPE_PFN;
		}
		if (pfn == 0) {
			prev_level = lvl;
			prev_type = type;
		}
		if (pfn == MAX_DOMAIN_PAGES-1) {
			lvl = 3;
			type = TYPE_UNKNOWN;
		}
		if (prev_type != type) {
			seq_printf(m, " [0x%lx->0x%lx] %s\n",
				prev_pfn_type, pfn, type_name[prev_type]);
			prev_pfn_type = pfn;
			prev_type = type;
		}
		if (prev_level != lvl) {
			seq_printf(m, " [0x%lx->0x%lx] level %s\n",
				prev_pfn_level, pfn, level_name[prev_level]);
			prev_pfn_level = pfn;
			prev_level = lvl;
		}
	}
	return 0;
#undef TYPE_IDENTITY
#undef TYPE_MISSING
#undef TYPE_PFN
#undef TYPE_UNKNOWN
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
