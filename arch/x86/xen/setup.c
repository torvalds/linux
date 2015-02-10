/*
 * Machine specific setup for xen
 *
 * Jeremy Fitzhardinge <jeremy@xensource.com>, XenSource Inc, 2007
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pm.h>
#include <linux/memblock.h>
#include <linux/cpuidle.h>
#include <linux/cpufreq.h>

#include <asm/elf.h>
#include <asm/vdso.h>
#include <asm/e820.h>
#include <asm/setup.h>
#include <asm/acpi.h>
#include <asm/numa.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/page.h>
#include <xen/interface/callback.h>
#include <xen/interface/memory.h>
#include <xen/interface/physdev.h>
#include <xen/features.h>
#include "xen-ops.h"
#include "vdso.h"
#include "p2m.h"
#include "mmu.h"

/* These are code, but not functions.  Defined in entry.S */
extern const char xen_hypervisor_callback[];
extern const char xen_failsafe_callback[];
#ifdef CONFIG_X86_64
extern asmlinkage void nmi(void);
#endif
extern void xen_sysenter_target(void);
extern void xen_syscall_target(void);
extern void xen_syscall32_target(void);

/* Amount of extra memory space we add to the e820 ranges */
struct xen_memory_region xen_extra_mem[XEN_EXTRA_MEM_MAX_REGIONS] __initdata;

/* Number of pages released from the initial allocation. */
unsigned long xen_released_pages;

/*
 * Buffer used to remap identity mapped pages. We only need the virtual space.
 * The physical page behind this address is remapped as needed to different
 * buffer pages.
 */
#define REMAP_SIZE	(P2M_PER_PAGE - 3)
static struct {
	unsigned long	next_area_mfn;
	unsigned long	target_pfn;
	unsigned long	size;
	unsigned long	mfns[REMAP_SIZE];
} xen_remap_buf __initdata __aligned(PAGE_SIZE);
static unsigned long xen_remap_mfn __initdata = INVALID_P2M_ENTRY;

/* 
 * The maximum amount of extra memory compared to the base size.  The
 * main scaling factor is the size of struct page.  At extreme ratios
 * of base:extra, all the base memory can be filled with page
 * structures for the extra memory, leaving no space for anything
 * else.
 * 
 * 10x seems like a reasonable balance between scaling flexibility and
 * leaving a practically usable system.
 */
#define EXTRA_MEM_RATIO		(10)

static void __init xen_add_extra_mem(u64 start, u64 size)
{
	int i;

	for (i = 0; i < XEN_EXTRA_MEM_MAX_REGIONS; i++) {
		/* Add new region. */
		if (xen_extra_mem[i].size == 0) {
			xen_extra_mem[i].start = start;
			xen_extra_mem[i].size  = size;
			break;
		}
		/* Append to existing region. */
		if (xen_extra_mem[i].start + xen_extra_mem[i].size == start) {
			xen_extra_mem[i].size += size;
			break;
		}
	}
	if (i == XEN_EXTRA_MEM_MAX_REGIONS)
		printk(KERN_WARNING "Warning: not enough extra memory regions\n");

	memblock_reserve(start, size);
}

static void __init xen_del_extra_mem(u64 start, u64 size)
{
	int i;
	u64 start_r, size_r;

	for (i = 0; i < XEN_EXTRA_MEM_MAX_REGIONS; i++) {
		start_r = xen_extra_mem[i].start;
		size_r = xen_extra_mem[i].size;

		/* Start of region. */
		if (start_r == start) {
			BUG_ON(size > size_r);
			xen_extra_mem[i].start += size;
			xen_extra_mem[i].size -= size;
			break;
		}
		/* End of region. */
		if (start_r + size_r == start + size) {
			BUG_ON(size > size_r);
			xen_extra_mem[i].size -= size;
			break;
		}
		/* Mid of region. */
		if (start > start_r && start < start_r + size_r) {
			BUG_ON(start + size > start_r + size_r);
			xen_extra_mem[i].size = start - start_r;
			/* Calling memblock_reserve() again is okay. */
			xen_add_extra_mem(start + size, start_r + size_r -
					  (start + size));
			break;
		}
	}
	memblock_free(start, size);
}

/*
 * Called during boot before the p2m list can take entries beyond the
 * hypervisor supplied p2m list. Entries in extra mem are to be regarded as
 * invalid.
 */
unsigned long __ref xen_chk_extra_mem(unsigned long pfn)
{
	int i;
	unsigned long addr = PFN_PHYS(pfn);

	for (i = 0; i < XEN_EXTRA_MEM_MAX_REGIONS; i++) {
		if (addr >= xen_extra_mem[i].start &&
		    addr < xen_extra_mem[i].start + xen_extra_mem[i].size)
			return INVALID_P2M_ENTRY;
	}

	return IDENTITY_FRAME(pfn);
}

/*
 * Mark all pfns of extra mem as invalid in p2m list.
 */
void __init xen_inv_extra_mem(void)
{
	unsigned long pfn, pfn_s, pfn_e;
	int i;

	for (i = 0; i < XEN_EXTRA_MEM_MAX_REGIONS; i++) {
		pfn_s = PFN_DOWN(xen_extra_mem[i].start);
		pfn_e = PFN_UP(xen_extra_mem[i].start + xen_extra_mem[i].size);
		for (pfn = pfn_s; pfn < pfn_e; pfn++)
			set_phys_to_machine(pfn, INVALID_P2M_ENTRY);
	}
}

/*
 * Finds the next RAM pfn available in the E820 map after min_pfn.
 * This function updates min_pfn with the pfn found and returns
 * the size of that range or zero if not found.
 */
static unsigned long __init xen_find_pfn_range(
	const struct e820entry *list, size_t map_size,
	unsigned long *min_pfn)
{
	const struct e820entry *entry;
	unsigned int i;
	unsigned long done = 0;

	for (i = 0, entry = list; i < map_size; i++, entry++) {
		unsigned long s_pfn;
		unsigned long e_pfn;

		if (entry->type != E820_RAM)
			continue;

		e_pfn = PFN_DOWN(entry->addr + entry->size);

		/* We only care about E820 after this */
		if (e_pfn < *min_pfn)
			continue;

		s_pfn = PFN_UP(entry->addr);

		/* If min_pfn falls within the E820 entry, we want to start
		 * at the min_pfn PFN.
		 */
		if (s_pfn <= *min_pfn) {
			done = e_pfn - *min_pfn;
		} else {
			done = e_pfn - s_pfn;
			*min_pfn = s_pfn;
		}
		break;
	}

	return done;
}

static int __init xen_free_mfn(unsigned long mfn)
{
	struct xen_memory_reservation reservation = {
		.address_bits = 0,
		.extent_order = 0,
		.domid        = DOMID_SELF
	};

	set_xen_guest_handle(reservation.extent_start, &mfn);
	reservation.nr_extents = 1;

	return HYPERVISOR_memory_op(XENMEM_decrease_reservation, &reservation);
}

/*
 * This releases a chunk of memory and then does the identity map. It's used
 * as a fallback if the remapping fails.
 */
static void __init xen_set_identity_and_release_chunk(unsigned long start_pfn,
	unsigned long end_pfn, unsigned long nr_pages, unsigned long *identity,
	unsigned long *released)
{
	unsigned long len = 0;
	unsigned long pfn, end;
	int ret;

	WARN_ON(start_pfn > end_pfn);

	end = min(end_pfn, nr_pages);
	for (pfn = start_pfn; pfn < end; pfn++) {
		unsigned long mfn = pfn_to_mfn(pfn);

		/* Make sure pfn exists to start with */
		if (mfn == INVALID_P2M_ENTRY || mfn_to_pfn(mfn) != pfn)
			continue;

		ret = xen_free_mfn(mfn);
		WARN(ret != 1, "Failed to release pfn %lx err=%d\n", pfn, ret);

		if (ret == 1) {
			if (!__set_phys_to_machine(pfn, INVALID_P2M_ENTRY))
				break;
			len++;
		} else
			break;
	}

	/* Need to release pages first */
	*released += len;
	*identity += set_phys_range_identity(start_pfn, end_pfn);
}

/*
 * Helper function to update the p2m and m2p tables and kernel mapping.
 */
static void __init xen_update_mem_tables(unsigned long pfn, unsigned long mfn)
{
	struct mmu_update update = {
		.ptr = ((unsigned long long)mfn << PAGE_SHIFT) | MMU_MACHPHYS_UPDATE,
		.val = pfn
	};

	/* Update p2m */
	if (!set_phys_to_machine(pfn, mfn)) {
		WARN(1, "Failed to set p2m mapping for pfn=%ld mfn=%ld\n",
		     pfn, mfn);
		BUG();
	}

	/* Update m2p */
	if (HYPERVISOR_mmu_update(&update, 1, NULL, DOMID_SELF) < 0) {
		WARN(1, "Failed to set m2p mapping for mfn=%ld pfn=%ld\n",
		     mfn, pfn);
		BUG();
	}

	/* Update kernel mapping, but not for highmem. */
	if ((pfn << PAGE_SHIFT) >= __pa(high_memory))
		return;

	if (HYPERVISOR_update_va_mapping((unsigned long)__va(pfn << PAGE_SHIFT),
					 mfn_pte(mfn, PAGE_KERNEL), 0)) {
		WARN(1, "Failed to update kernel mapping for mfn=%ld pfn=%ld\n",
		      mfn, pfn);
		BUG();
	}
}

/*
 * This function updates the p2m and m2p tables with an identity map from
 * start_pfn to start_pfn+size and prepares remapping the underlying RAM of the
 * original allocation at remap_pfn. The information needed for remapping is
 * saved in the memory itself to avoid the need for allocating buffers. The
 * complete remap information is contained in a list of MFNs each containing
 * up to REMAP_SIZE MFNs and the start target PFN for doing the remap.
 * This enables us to preserve the original mfn sequence while doing the
 * remapping at a time when the memory management is capable of allocating
 * virtual and physical memory in arbitrary amounts, see 'xen_remap_memory' and
 * its callers.
 */
static void __init xen_do_set_identity_and_remap_chunk(
        unsigned long start_pfn, unsigned long size, unsigned long remap_pfn)
{
	unsigned long buf = (unsigned long)&xen_remap_buf;
	unsigned long mfn_save, mfn;
	unsigned long ident_pfn_iter, remap_pfn_iter;
	unsigned long ident_end_pfn = start_pfn + size;
	unsigned long left = size;
	unsigned long ident_cnt = 0;
	unsigned int i, chunk;

	WARN_ON(size == 0);

	BUG_ON(xen_feature(XENFEAT_auto_translated_physmap));

	mfn_save = virt_to_mfn(buf);

	for (ident_pfn_iter = start_pfn, remap_pfn_iter = remap_pfn;
	     ident_pfn_iter < ident_end_pfn;
	     ident_pfn_iter += REMAP_SIZE, remap_pfn_iter += REMAP_SIZE) {
		chunk = (left < REMAP_SIZE) ? left : REMAP_SIZE;

		/* Map first pfn to xen_remap_buf */
		mfn = pfn_to_mfn(ident_pfn_iter);
		set_pte_mfn(buf, mfn, PAGE_KERNEL);

		/* Save mapping information in page */
		xen_remap_buf.next_area_mfn = xen_remap_mfn;
		xen_remap_buf.target_pfn = remap_pfn_iter;
		xen_remap_buf.size = chunk;
		for (i = 0; i < chunk; i++)
			xen_remap_buf.mfns[i] = pfn_to_mfn(ident_pfn_iter + i);

		/* Put remap buf into list. */
		xen_remap_mfn = mfn;

		/* Set identity map */
		ident_cnt += set_phys_range_identity(ident_pfn_iter,
			ident_pfn_iter + chunk);

		left -= chunk;
	}

	/* Restore old xen_remap_buf mapping */
	set_pte_mfn(buf, mfn_save, PAGE_KERNEL);
}

/*
 * This function takes a contiguous pfn range that needs to be identity mapped
 * and:
 *
 *  1) Finds a new range of pfns to use to remap based on E820 and remap_pfn.
 *  2) Calls the do_ function to actually do the mapping/remapping work.
 *
 * The goal is to not allocate additional memory but to remap the existing
 * pages. In the case of an error the underlying memory is simply released back
 * to Xen and not remapped.
 */
static unsigned long __init xen_set_identity_and_remap_chunk(
        const struct e820entry *list, size_t map_size, unsigned long start_pfn,
	unsigned long end_pfn, unsigned long nr_pages, unsigned long remap_pfn,
	unsigned long *identity, unsigned long *released)
{
	unsigned long pfn;
	unsigned long i = 0;
	unsigned long n = end_pfn - start_pfn;

	while (i < n) {
		unsigned long cur_pfn = start_pfn + i;
		unsigned long left = n - i;
		unsigned long size = left;
		unsigned long remap_range_size;

		/* Do not remap pages beyond the current allocation */
		if (cur_pfn >= nr_pages) {
			/* Identity map remaining pages */
			*identity += set_phys_range_identity(cur_pfn,
				cur_pfn + size);
			break;
		}
		if (cur_pfn + size > nr_pages)
			size = nr_pages - cur_pfn;

		remap_range_size = xen_find_pfn_range(list, map_size,
						      &remap_pfn);
		if (!remap_range_size) {
			pr_warning("Unable to find available pfn range, not remapping identity pages\n");
			xen_set_identity_and_release_chunk(cur_pfn,
				cur_pfn + left, nr_pages, identity, released);
			break;
		}
		/* Adjust size to fit in current e820 RAM region */
		if (size > remap_range_size)
			size = remap_range_size;

		xen_do_set_identity_and_remap_chunk(cur_pfn, size, remap_pfn);

		/* Update variables to reflect new mappings. */
		i += size;
		remap_pfn += size;
		*identity += size;
	}

	/*
	 * If the PFNs are currently mapped, the VA mapping also needs
	 * to be updated to be 1:1.
	 */
	for (pfn = start_pfn; pfn <= max_pfn_mapped && pfn < end_pfn; pfn++)
		(void)HYPERVISOR_update_va_mapping(
			(unsigned long)__va(pfn << PAGE_SHIFT),
			mfn_pte(pfn, PAGE_KERNEL_IO), 0);

	return remap_pfn;
}

static void __init xen_set_identity_and_remap(
	const struct e820entry *list, size_t map_size, unsigned long nr_pages,
	unsigned long *released)
{
	phys_addr_t start = 0;
	unsigned long identity = 0;
	unsigned long last_pfn = nr_pages;
	const struct e820entry *entry;
	unsigned long num_released = 0;
	int i;

	/*
	 * Combine non-RAM regions and gaps until a RAM region (or the
	 * end of the map) is reached, then set the 1:1 map and
	 * remap the memory in those non-RAM regions.
	 *
	 * The combined non-RAM regions are rounded to a whole number
	 * of pages so any partial pages are accessible via the 1:1
	 * mapping.  This is needed for some BIOSes that put (for
	 * example) the DMI tables in a reserved region that begins on
	 * a non-page boundary.
	 */
	for (i = 0, entry = list; i < map_size; i++, entry++) {
		phys_addr_t end = entry->addr + entry->size;
		if (entry->type == E820_RAM || i == map_size - 1) {
			unsigned long start_pfn = PFN_DOWN(start);
			unsigned long end_pfn = PFN_UP(end);

			if (entry->type == E820_RAM)
				end_pfn = PFN_UP(entry->addr);

			if (start_pfn < end_pfn)
				last_pfn = xen_set_identity_and_remap_chunk(
						list, map_size, start_pfn,
						end_pfn, nr_pages, last_pfn,
						&identity, &num_released);
			start = end;
		}
	}

	*released = num_released;

	pr_info("Set %ld page(s) to 1-1 mapping\n", identity);
	pr_info("Released %ld page(s)\n", num_released);
}

/*
 * Remap the memory prepared in xen_do_set_identity_and_remap_chunk().
 * The remap information (which mfn remap to which pfn) is contained in the
 * to be remapped memory itself in a linked list anchored at xen_remap_mfn.
 * This scheme allows to remap the different chunks in arbitrary order while
 * the resulting mapping will be independant from the order.
 */
void __init xen_remap_memory(void)
{
	unsigned long buf = (unsigned long)&xen_remap_buf;
	unsigned long mfn_save, mfn, pfn;
	unsigned long remapped = 0;
	unsigned int i;
	unsigned long pfn_s = ~0UL;
	unsigned long len = 0;

	mfn_save = virt_to_mfn(buf);

	while (xen_remap_mfn != INVALID_P2M_ENTRY) {
		/* Map the remap information */
		set_pte_mfn(buf, xen_remap_mfn, PAGE_KERNEL);

		BUG_ON(xen_remap_mfn != xen_remap_buf.mfns[0]);

		pfn = xen_remap_buf.target_pfn;
		for (i = 0; i < xen_remap_buf.size; i++) {
			mfn = xen_remap_buf.mfns[i];
			xen_update_mem_tables(pfn, mfn);
			remapped++;
			pfn++;
		}
		if (pfn_s == ~0UL || pfn == pfn_s) {
			pfn_s = xen_remap_buf.target_pfn;
			len += xen_remap_buf.size;
		} else if (pfn_s + len == xen_remap_buf.target_pfn) {
			len += xen_remap_buf.size;
		} else {
			xen_del_extra_mem(PFN_PHYS(pfn_s), PFN_PHYS(len));
			pfn_s = xen_remap_buf.target_pfn;
			len = xen_remap_buf.size;
		}

		mfn = xen_remap_mfn;
		xen_remap_mfn = xen_remap_buf.next_area_mfn;
	}

	if (pfn_s != ~0UL && len)
		xen_del_extra_mem(PFN_PHYS(pfn_s), PFN_PHYS(len));

	set_pte_mfn(buf, mfn_save, PAGE_KERNEL);

	pr_info("Remapped %ld page(s)\n", remapped);
}

static unsigned long __init xen_get_max_pages(void)
{
	unsigned long max_pages = MAX_DOMAIN_PAGES;
	domid_t domid = DOMID_SELF;
	int ret;

	/*
	 * For the initial domain we use the maximum reservation as
	 * the maximum page.
	 *
	 * For guest domains the current maximum reservation reflects
	 * the current maximum rather than the static maximum. In this
	 * case the e820 map provided to us will cover the static
	 * maximum region.
	 */
	if (xen_initial_domain()) {
		ret = HYPERVISOR_memory_op(XENMEM_maximum_reservation, &domid);
		if (ret > 0)
			max_pages = ret;
	}

	return min(max_pages, MAX_DOMAIN_PAGES);
}

static void xen_align_and_add_e820_region(u64 start, u64 size, int type)
{
	u64 end = start + size;

	/* Align RAM regions to page boundaries. */
	if (type == E820_RAM) {
		start = PAGE_ALIGN(start);
		end &= ~((u64)PAGE_SIZE - 1);
	}

	e820_add_region(start, end - start, type);
}

void xen_ignore_unusable(struct e820entry *list, size_t map_size)
{
	struct e820entry *entry;
	unsigned int i;

	for (i = 0, entry = list; i < map_size; i++, entry++) {
		if (entry->type == E820_UNUSABLE)
			entry->type = E820_RAM;
	}
}

/**
 * machine_specific_memory_setup - Hook for machine specific memory setup.
 **/
char * __init xen_memory_setup(void)
{
	static struct e820entry map[E820MAX] __initdata;

	unsigned long max_pfn = xen_start_info->nr_pages;
	unsigned long long mem_end;
	int rc;
	struct xen_memory_map memmap;
	unsigned long max_pages;
	unsigned long extra_pages = 0;
	int i;
	int op;

	max_pfn = min(MAX_DOMAIN_PAGES, max_pfn);
	mem_end = PFN_PHYS(max_pfn);

	memmap.nr_entries = E820MAX;
	set_xen_guest_handle(memmap.buffer, map);

	op = xen_initial_domain() ?
		XENMEM_machine_memory_map :
		XENMEM_memory_map;
	rc = HYPERVISOR_memory_op(op, &memmap);
	if (rc == -ENOSYS) {
		BUG_ON(xen_initial_domain());
		memmap.nr_entries = 1;
		map[0].addr = 0ULL;
		map[0].size = mem_end;
		/* 8MB slack (to balance backend allocations). */
		map[0].size += 8ULL << 20;
		map[0].type = E820_RAM;
		rc = 0;
	}
	BUG_ON(rc);
	BUG_ON(memmap.nr_entries == 0);

	/*
	 * Xen won't allow a 1:1 mapping to be created to UNUSABLE
	 * regions, so if we're using the machine memory map leave the
	 * region as RAM as it is in the pseudo-physical map.
	 *
	 * UNUSABLE regions in domUs are not handled and will need
	 * a patch in the future.
	 */
	if (xen_initial_domain())
		xen_ignore_unusable(map, memmap.nr_entries);

	/* Make sure the Xen-supplied memory map is well-ordered. */
	sanitize_e820_map(map, memmap.nr_entries, &memmap.nr_entries);

	max_pages = xen_get_max_pages();
	if (max_pages > max_pfn)
		extra_pages += max_pages - max_pfn;

	/*
	 * Set identity map on non-RAM pages and prepare remapping the
	 * underlying RAM.
	 */
	xen_set_identity_and_remap(map, memmap.nr_entries, max_pfn,
				   &xen_released_pages);

	extra_pages += xen_released_pages;

	/*
	 * Clamp the amount of extra memory to a EXTRA_MEM_RATIO
	 * factor the base size.  On non-highmem systems, the base
	 * size is the full initial memory allocation; on highmem it
	 * is limited to the max size of lowmem, so that it doesn't
	 * get completely filled.
	 *
	 * In principle there could be a problem in lowmem systems if
	 * the initial memory is also very large with respect to
	 * lowmem, but we won't try to deal with that here.
	 */
	extra_pages = min(EXTRA_MEM_RATIO * min(max_pfn, PFN_DOWN(MAXMEM)),
			  extra_pages);
	i = 0;
	while (i < memmap.nr_entries) {
		u64 addr = map[i].addr;
		u64 size = map[i].size;
		u32 type = map[i].type;

		if (type == E820_RAM) {
			if (addr < mem_end) {
				size = min(size, mem_end - addr);
			} else if (extra_pages) {
				size = min(size, (u64)extra_pages * PAGE_SIZE);
				extra_pages -= size / PAGE_SIZE;
				xen_add_extra_mem(addr, size);
				xen_max_p2m_pfn = PFN_DOWN(addr + size);
			} else
				type = E820_UNUSABLE;
		}

		xen_align_and_add_e820_region(addr, size, type);

		map[i].addr += size;
		map[i].size -= size;
		if (map[i].size == 0)
			i++;
	}

	/*
	 * Set the rest as identity mapped, in case PCI BARs are
	 * located here.
	 *
	 * PFNs above MAX_P2M_PFN are considered identity mapped as
	 * well.
	 */
	set_phys_range_identity(map[i-1].addr / PAGE_SIZE, ~0ul);

	/*
	 * In domU, the ISA region is normal, usable memory, but we
	 * reserve ISA memory anyway because too many things poke
	 * about in there.
	 */
	e820_add_region(ISA_START_ADDRESS, ISA_END_ADDRESS - ISA_START_ADDRESS,
			E820_RESERVED);

	/*
	 * Reserve Xen bits:
	 *  - mfn_list
	 *  - xen_start_info
	 * See comment above "struct start_info" in <xen/interface/xen.h>
	 * We tried to make the the memblock_reserve more selective so
	 * that it would be clear what region is reserved. Sadly we ran
	 * in the problem wherein on a 64-bit hypervisor with a 32-bit
	 * initial domain, the pt_base has the cr3 value which is not
	 * neccessarily where the pagetable starts! As Jan put it: "
	 * Actually, the adjustment turns out to be correct: The page
	 * tables for a 32-on-64 dom0 get allocated in the order "first L1",
	 * "first L2", "first L3", so the offset to the page table base is
	 * indeed 2. When reading xen/include/public/xen.h's comment
	 * very strictly, this is not a violation (since there nothing is said
	 * that the first thing in the page table space is pointed to by
	 * pt_base; I admit that this seems to be implied though, namely
	 * do I think that it is implied that the page table space is the
	 * range [pt_base, pt_base + nt_pt_frames), whereas that
	 * range here indeed is [pt_base - 2, pt_base - 2 + nt_pt_frames),
	 * which - without a priori knowledge - the kernel would have
	 * difficulty to figure out)." - so lets just fall back to the
	 * easy way and reserve the whole region.
	 */
	memblock_reserve(__pa(xen_start_info->mfn_list),
			 xen_start_info->pt_base - xen_start_info->mfn_list);

	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);

	return "Xen";
}

/*
 * Machine specific memory setup for auto-translated guests.
 */
char * __init xen_auto_xlated_memory_setup(void)
{
	static struct e820entry map[E820MAX] __initdata;

	struct xen_memory_map memmap;
	int i;
	int rc;

	memmap.nr_entries = E820MAX;
	set_xen_guest_handle(memmap.buffer, map);

	rc = HYPERVISOR_memory_op(XENMEM_memory_map, &memmap);
	if (rc < 0)
		panic("No memory map (%d)\n", rc);

	sanitize_e820_map(map, ARRAY_SIZE(map), &memmap.nr_entries);

	for (i = 0; i < memmap.nr_entries; i++)
		e820_add_region(map[i].addr, map[i].size, map[i].type);

	memblock_reserve(__pa(xen_start_info->mfn_list),
			 xen_start_info->pt_base - xen_start_info->mfn_list);

	return "Xen";
}

/*
 * Set the bit indicating "nosegneg" library variants should be used.
 * We only need to bother in pure 32-bit mode; compat 32-bit processes
 * can have un-truncated segments, so wrapping around is allowed.
 */
static void __init fiddle_vdso(void)
{
#ifdef CONFIG_X86_32
	/*
	 * This could be called before selected_vdso32 is initialized, so
	 * just fiddle with both possible images.  vdso_image_32_syscall
	 * can't be selected, since it only exists on 64-bit systems.
	 */
	u32 *mask;
	mask = vdso_image_32_int80.data +
		vdso_image_32_int80.sym_VDSO32_NOTE_MASK;
	*mask |= 1 << VDSO_NOTE_NONEGSEG_BIT;
	mask = vdso_image_32_sysenter.data +
		vdso_image_32_sysenter.sym_VDSO32_NOTE_MASK;
	*mask |= 1 << VDSO_NOTE_NONEGSEG_BIT;
#endif
}

static int register_callback(unsigned type, const void *func)
{
	struct callback_register callback = {
		.type = type,
		.address = XEN_CALLBACK(__KERNEL_CS, func),
		.flags = CALLBACKF_mask_events,
	};

	return HYPERVISOR_callback_op(CALLBACKOP_register, &callback);
}

void xen_enable_sysenter(void)
{
	int ret;
	unsigned sysenter_feature;

#ifdef CONFIG_X86_32
	sysenter_feature = X86_FEATURE_SEP;
#else
	sysenter_feature = X86_FEATURE_SYSENTER32;
#endif

	if (!boot_cpu_has(sysenter_feature))
		return;

	ret = register_callback(CALLBACKTYPE_sysenter, xen_sysenter_target);
	if(ret != 0)
		setup_clear_cpu_cap(sysenter_feature);
}

void xen_enable_syscall(void)
{
#ifdef CONFIG_X86_64
	int ret;

	ret = register_callback(CALLBACKTYPE_syscall, xen_syscall_target);
	if (ret != 0) {
		printk(KERN_ERR "Failed to set syscall callback: %d\n", ret);
		/* Pretty fatal; 64-bit userspace has no other
		   mechanism for syscalls. */
	}

	if (boot_cpu_has(X86_FEATURE_SYSCALL32)) {
		ret = register_callback(CALLBACKTYPE_syscall32,
					xen_syscall32_target);
		if (ret != 0)
			setup_clear_cpu_cap(X86_FEATURE_SYSCALL32);
	}
#endif /* CONFIG_X86_64 */
}

void __init xen_pvmmu_arch_setup(void)
{
	HYPERVISOR_vm_assist(VMASST_CMD_enable, VMASST_TYPE_4gb_segments);
	HYPERVISOR_vm_assist(VMASST_CMD_enable, VMASST_TYPE_writable_pagetables);

	HYPERVISOR_vm_assist(VMASST_CMD_enable,
			     VMASST_TYPE_pae_extended_cr3);

	if (register_callback(CALLBACKTYPE_event, xen_hypervisor_callback) ||
	    register_callback(CALLBACKTYPE_failsafe, xen_failsafe_callback))
		BUG();

	xen_enable_sysenter();
	xen_enable_syscall();
}

/* This function is not called for HVM domains */
void __init xen_arch_setup(void)
{
	xen_panic_handler_init();
	if (!xen_feature(XENFEAT_auto_translated_physmap))
		xen_pvmmu_arch_setup();

#ifdef CONFIG_ACPI
	if (!(xen_start_info->flags & SIF_INITDOMAIN)) {
		printk(KERN_INFO "ACPI in unprivileged domain disabled\n");
		disable_acpi();
	}
#endif

	memcpy(boot_command_line, xen_start_info->cmd_line,
	       MAX_GUEST_CMDLINE > COMMAND_LINE_SIZE ?
	       COMMAND_LINE_SIZE : MAX_GUEST_CMDLINE);

	/* Set up idle, making sure it calls safe_halt() pvop */
	disable_cpuidle();
	disable_cpufreq();
	WARN_ON(xen_set_default_idle());
	fiddle_vdso();
#ifdef CONFIG_NUMA
	numa_off = 1;
#endif
}
