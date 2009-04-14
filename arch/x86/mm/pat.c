/*
 * Handle caching attributes in page tables (PAT)
 *
 * Authors: Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *          Suresh B Siddha <suresh.b.siddha@intel.com>
 *
 * Loosely based on earlier PAT patchset from Eric Biederman and Andi Kleen.
 */

#include <linux/seq_file.h>
#include <linux/bootmem.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/fs.h>

#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <asm/fcntl.h>
#include <asm/e820.h>
#include <asm/mtrr.h>
#include <asm/page.h>
#include <asm/msr.h>
#include <asm/pat.h>
#include <asm/io.h>

#ifdef CONFIG_X86_PAT
int __read_mostly pat_enabled = 1;

void __cpuinit pat_disable(const char *reason)
{
	pat_enabled = 0;
	printk(KERN_INFO "%s\n", reason);
}

static int __init nopat(char *str)
{
	pat_disable("PAT support disabled.");
	return 0;
}
early_param("nopat", nopat);
#else
static inline void pat_disable(const char *reason)
{
	(void)reason;
}
#endif


static int debug_enable;

static int __init pat_debug_setup(char *str)
{
	debug_enable = 1;
	return 0;
}
__setup("debugpat", pat_debug_setup);

#define dprintk(fmt, arg...) \
	do { if (debug_enable) printk(KERN_INFO fmt, ##arg); } while (0)


static u64 __read_mostly boot_pat_state;

enum {
	PAT_UC = 0,		/* uncached */
	PAT_WC = 1,		/* Write combining */
	PAT_WT = 4,		/* Write Through */
	PAT_WP = 5,		/* Write Protected */
	PAT_WB = 6,		/* Write Back (default) */
	PAT_UC_MINUS = 7,	/* UC, but can be overriden by MTRR */
};

#define PAT(x, y)	((u64)PAT_ ## y << ((x)*8))

void pat_init(void)
{
	u64 pat;

	if (!pat_enabled)
		return;

	if (!cpu_has_pat) {
		if (!boot_pat_state) {
			pat_disable("PAT not supported by CPU.");
			return;
		} else {
			/*
			 * If this happens we are on a secondary CPU, but
			 * switched to PAT on the boot CPU. We have no way to
			 * undo PAT.
			 */
			printk(KERN_ERR "PAT enabled, "
			       "but not supported by secondary CPU\n");
			BUG();
		}
	}

	/* Set PWT to Write-Combining. All other bits stay the same */
	/*
	 * PTE encoding used in Linux:
	 *      PAT
	 *      |PCD
	 *      ||PWT
	 *      |||
	 *      000 WB		_PAGE_CACHE_WB
	 *      001 WC		_PAGE_CACHE_WC
	 *      010 UC-		_PAGE_CACHE_UC_MINUS
	 *      011 UC		_PAGE_CACHE_UC
	 * PAT bit unused
	 */
	pat = PAT(0, WB) | PAT(1, WC) | PAT(2, UC_MINUS) | PAT(3, UC) |
	      PAT(4, WB) | PAT(5, WC) | PAT(6, UC_MINUS) | PAT(7, UC);

	/* Boot CPU check */
	if (!boot_pat_state)
		rdmsrl(MSR_IA32_CR_PAT, boot_pat_state);

	wrmsrl(MSR_IA32_CR_PAT, pat);
	printk(KERN_INFO "x86 PAT enabled: cpu %d, old 0x%Lx, new 0x%Lx\n",
	       smp_processor_id(), boot_pat_state, pat);
}

#undef PAT

static char *cattr_name(unsigned long flags)
{
	switch (flags & _PAGE_CACHE_MASK) {
	case _PAGE_CACHE_UC:		return "uncached";
	case _PAGE_CACHE_UC_MINUS:	return "uncached-minus";
	case _PAGE_CACHE_WB:		return "write-back";
	case _PAGE_CACHE_WC:		return "write-combining";
	default:			return "broken";
	}
}

/*
 * The global memtype list keeps track of memory type for specific
 * physical memory areas. Conflicting memory types in different
 * mappings can cause CPU cache corruption. To avoid this we keep track.
 *
 * The list is sorted based on starting address and can contain multiple
 * entries for each address (this allows reference counting for overlapping
 * areas). All the aliases have the same cache attributes of course.
 * Zero attributes are represented as holes.
 *
 * Currently the data structure is a list because the number of mappings
 * are expected to be relatively small. If this should be a problem
 * it could be changed to a rbtree or similar.
 *
 * memtype_lock protects the whole list.
 */

struct memtype {
	u64			start;
	u64			end;
	unsigned long		type;
	struct list_head	nd;
};

static LIST_HEAD(memtype_list);
static DEFINE_SPINLOCK(memtype_lock);	/* protects memtype list */

/*
 * Does intersection of PAT memory type and MTRR memory type and returns
 * the resulting memory type as PAT understands it.
 * (Type in pat and mtrr will not have same value)
 * The intersection is based on "Effective Memory Type" tables in IA-32
 * SDM vol 3a
 */
static unsigned long pat_x_mtrr_type(u64 start, u64 end, unsigned long req_type)
{
	/*
	 * Look for MTRR hint to get the effective type in case where PAT
	 * request is for WB.
	 */
	if (req_type == _PAGE_CACHE_WB) {
		u8 mtrr_type;

		mtrr_type = mtrr_type_lookup(start, end);
		if (mtrr_type == MTRR_TYPE_UNCACHABLE)
			return _PAGE_CACHE_UC;
		if (mtrr_type == MTRR_TYPE_WRCOMB)
			return _PAGE_CACHE_WC;
	}

	return req_type;
}

static int
chk_conflict(struct memtype *new, struct memtype *entry, unsigned long *type)
{
	if (new->type != entry->type) {
		if (type) {
			new->type = entry->type;
			*type = entry->type;
		} else
			goto conflict;
	}

	 /* check overlaps with more than one entry in the list */
	list_for_each_entry_continue(entry, &memtype_list, nd) {
		if (new->end <= entry->start)
			break;
		else if (new->type != entry->type)
			goto conflict;
	}
	return 0;

 conflict:
	printk(KERN_INFO "%s:%d conflicting memory types "
	       "%Lx-%Lx %s<->%s\n", current->comm, current->pid, new->start,
	       new->end, cattr_name(new->type), cattr_name(entry->type));
	return -EBUSY;
}

static struct memtype *cached_entry;
static u64 cached_start;

static int pat_pagerange_is_ram(unsigned long start, unsigned long end)
{
	int ram_page = 0, not_rampage = 0;
	unsigned long page_nr;

	for (page_nr = (start >> PAGE_SHIFT); page_nr < (end >> PAGE_SHIFT);
	     ++page_nr) {
		/*
		 * For legacy reasons, physical address range in the legacy ISA
		 * region is tracked as non-RAM. This will allow users of
		 * /dev/mem to map portions of legacy ISA region, even when
		 * some of those portions are listed(or not even listed) with
		 * different e820 types(RAM/reserved/..)
		 */
		if (page_nr >= (ISA_END_ADDRESS >> PAGE_SHIFT) &&
		    page_is_ram(page_nr))
			ram_page = 1;
		else
			not_rampage = 1;

		if (ram_page == not_rampage)
			return -1;
	}

	return ram_page;
}

/*
 * For RAM pages, mark the pages as non WB memory type using
 * PageNonWB (PG_arch_1). We allow only one set_memory_uc() or
 * set_memory_wc() on a RAM page at a time before marking it as WB again.
 * This is ok, because only one driver will be owning the page and
 * doing set_memory_*() calls.
 *
 * For now, we use PageNonWB to track that the RAM page is being mapped
 * as non WB. In future, we will have to use one more flag
 * (or some other mechanism in page_struct) to distinguish between
 * UC and WC mapping.
 */
static int reserve_ram_pages_type(u64 start, u64 end, unsigned long req_type,
				  unsigned long *new_type)
{
	struct page *page;
	u64 pfn, end_pfn;

	for (pfn = (start >> PAGE_SHIFT); pfn < (end >> PAGE_SHIFT); ++pfn) {
		page = pfn_to_page(pfn);
		if (page_mapped(page) || PageNonWB(page))
			goto out;

		SetPageNonWB(page);
	}
	return 0;

out:
	end_pfn = pfn;
	for (pfn = (start >> PAGE_SHIFT); pfn < end_pfn; ++pfn) {
		page = pfn_to_page(pfn);
		ClearPageNonWB(page);
	}

	return -EINVAL;
}

static int free_ram_pages_type(u64 start, u64 end)
{
	struct page *page;
	u64 pfn, end_pfn;

	for (pfn = (start >> PAGE_SHIFT); pfn < (end >> PAGE_SHIFT); ++pfn) {
		page = pfn_to_page(pfn);
		if (page_mapped(page) || !PageNonWB(page))
			goto out;

		ClearPageNonWB(page);
	}
	return 0;

out:
	end_pfn = pfn;
	for (pfn = (start >> PAGE_SHIFT); pfn < end_pfn; ++pfn) {
		page = pfn_to_page(pfn);
		SetPageNonWB(page);
	}
	return -EINVAL;
}

/*
 * req_type typically has one of the:
 * - _PAGE_CACHE_WB
 * - _PAGE_CACHE_WC
 * - _PAGE_CACHE_UC_MINUS
 * - _PAGE_CACHE_UC
 *
 * req_type will have a special case value '-1', when requester want to inherit
 * the memory type from mtrr (if WB), existing PAT, defaulting to UC_MINUS.
 *
 * If new_type is NULL, function will return an error if it cannot reserve the
 * region with req_type. If new_type is non-NULL, function will return
 * available type in new_type in case of no error. In case of any error
 * it will return a negative return value.
 */
int reserve_memtype(u64 start, u64 end, unsigned long req_type,
		    unsigned long *new_type)
{
	struct memtype *new, *entry;
	unsigned long actual_type;
	struct list_head *where;
	int is_range_ram;
	int err = 0;

	BUG_ON(start >= end); /* end is exclusive */

	if (!pat_enabled) {
		/* This is identical to page table setting without PAT */
		if (new_type) {
			if (req_type == -1)
				*new_type = _PAGE_CACHE_WB;
			else
				*new_type = req_type & _PAGE_CACHE_MASK;
		}
		return 0;
	}

	/* Low ISA region is always mapped WB in page table. No need to track */
	if (is_ISA_range(start, end - 1)) {
		if (new_type)
			*new_type = _PAGE_CACHE_WB;
		return 0;
	}

	if (req_type == -1) {
		/*
		 * Call mtrr_lookup to get the type hint. This is an
		 * optimization for /dev/mem mmap'ers into WB memory (BIOS
		 * tools and ACPI tools). Use WB request for WB memory and use
		 * UC_MINUS otherwise.
		 */
		u8 mtrr_type = mtrr_type_lookup(start, end);

		if (mtrr_type == MTRR_TYPE_WRBACK)
			actual_type = _PAGE_CACHE_WB;
		else
			actual_type = _PAGE_CACHE_UC_MINUS;
	} else {
		actual_type = pat_x_mtrr_type(start, end,
					      req_type & _PAGE_CACHE_MASK);
	}

	if (new_type)
		*new_type = actual_type;

	is_range_ram = pat_pagerange_is_ram(start, end);
	if (is_range_ram == 1)
		return reserve_ram_pages_type(start, end, req_type,
					      new_type);
	else if (is_range_ram < 0)
		return -EINVAL;

	new  = kmalloc(sizeof(struct memtype), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	new->start	= start;
	new->end	= end;
	new->type	= actual_type;

	spin_lock(&memtype_lock);

	if (cached_entry && start >= cached_start)
		entry = cached_entry;
	else
		entry = list_entry(&memtype_list, struct memtype, nd);

	/* Search for existing mapping that overlaps the current range */
	where = NULL;
	list_for_each_entry_continue(entry, &memtype_list, nd) {
		if (end <= entry->start) {
			where = entry->nd.prev;
			cached_entry = list_entry(where, struct memtype, nd);
			break;
		} else if (start <= entry->start) { /* end > entry->start */
			err = chk_conflict(new, entry, new_type);
			if (!err) {
				dprintk("Overlap at 0x%Lx-0x%Lx\n",
					entry->start, entry->end);
				where = entry->nd.prev;
				cached_entry = list_entry(where,
							struct memtype, nd);
			}
			break;
		} else if (start < entry->end) { /* start > entry->start */
			err = chk_conflict(new, entry, new_type);
			if (!err) {
				dprintk("Overlap at 0x%Lx-0x%Lx\n",
					entry->start, entry->end);
				cached_entry = list_entry(entry->nd.prev,
							struct memtype, nd);

				/*
				 * Move to right position in the linked
				 * list to add this new entry
				 */
				list_for_each_entry_continue(entry,
							&memtype_list, nd) {
					if (start <= entry->start) {
						where = entry->nd.prev;
						break;
					}
				}
			}
			break;
		}
	}

	if (err) {
		printk(KERN_INFO "reserve_memtype failed 0x%Lx-0x%Lx, "
		       "track %s, req %s\n",
		       start, end, cattr_name(new->type), cattr_name(req_type));
		kfree(new);
		spin_unlock(&memtype_lock);

		return err;
	}

	cached_start = start;

	if (where)
		list_add(&new->nd, where);
	else
		list_add_tail(&new->nd, &memtype_list);

	spin_unlock(&memtype_lock);

	dprintk("reserve_memtype added 0x%Lx-0x%Lx, track %s, req %s, ret %s\n",
		start, end, cattr_name(new->type), cattr_name(req_type),
		new_type ? cattr_name(*new_type) : "-");

	return err;
}

int free_memtype(u64 start, u64 end)
{
	struct memtype *entry;
	int err = -EINVAL;
	int is_range_ram;

	if (!pat_enabled)
		return 0;

	/* Low ISA region is always mapped WB. No need to track */
	if (is_ISA_range(start, end - 1))
		return 0;

	is_range_ram = pat_pagerange_is_ram(start, end);
	if (is_range_ram == 1)
		return free_ram_pages_type(start, end);
	else if (is_range_ram < 0)
		return -EINVAL;

	spin_lock(&memtype_lock);
	list_for_each_entry(entry, &memtype_list, nd) {
		if (entry->start == start && entry->end == end) {
			if (cached_entry == entry || cached_start == start)
				cached_entry = NULL;

			list_del(&entry->nd);
			kfree(entry);
			err = 0;
			break;
		}
	}
	spin_unlock(&memtype_lock);

	if (err) {
		printk(KERN_INFO "%s:%d freeing invalid memtype %Lx-%Lx\n",
			current->comm, current->pid, start, end);
	}

	dprintk("free_memtype request 0x%Lx-0x%Lx\n", start, end);

	return err;
}


pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				unsigned long size, pgprot_t vma_prot)
{
	return vma_prot;
}

#ifdef CONFIG_STRICT_DEVMEM
/* This check is done in drivers/char/mem.c in case of STRICT_DEVMEM*/
static inline int range_is_allowed(unsigned long pfn, unsigned long size)
{
	return 1;
}
#else
/* This check is needed to avoid cache aliasing when PAT is enabled */
static inline int range_is_allowed(unsigned long pfn, unsigned long size)
{
	u64 from = ((u64)pfn) << PAGE_SHIFT;
	u64 to = from + size;
	u64 cursor = from;

	if (!pat_enabled)
		return 1;

	while (cursor < to) {
		if (!devmem_is_allowed(pfn)) {
			printk(KERN_INFO
		"Program %s tried to access /dev/mem between %Lx->%Lx.\n",
				current->comm, from, to);
			return 0;
		}
		cursor += PAGE_SIZE;
		pfn++;
	}
	return 1;
}
#endif /* CONFIG_STRICT_DEVMEM */

int phys_mem_access_prot_allowed(struct file *file, unsigned long pfn,
				unsigned long size, pgprot_t *vma_prot)
{
	u64 offset = ((u64) pfn) << PAGE_SHIFT;
	unsigned long flags = -1;
	int retval;

	if (!range_is_allowed(pfn, size))
		return 0;

	if (file->f_flags & O_SYNC) {
		flags = _PAGE_CACHE_UC_MINUS;
	}

#ifdef CONFIG_X86_32
	/*
	 * On the PPro and successors, the MTRRs are used to set
	 * memory types for physical addresses outside main memory,
	 * so blindly setting UC or PWT on those pages is wrong.
	 * For Pentiums and earlier, the surround logic should disable
	 * caching for the high addresses through the KEN pin, but
	 * we maintain the tradition of paranoia in this code.
	 */
	if (!pat_enabled &&
	    !(boot_cpu_has(X86_FEATURE_MTRR) ||
	      boot_cpu_has(X86_FEATURE_K6_MTRR) ||
	      boot_cpu_has(X86_FEATURE_CYRIX_ARR) ||
	      boot_cpu_has(X86_FEATURE_CENTAUR_MCR)) &&
	    (pfn << PAGE_SHIFT) >= __pa(high_memory)) {
		flags = _PAGE_CACHE_UC;
	}
#endif

	/*
	 * With O_SYNC, we can only take UC_MINUS mapping. Fail if we cannot.
	 *
	 * Without O_SYNC, we want to get
	 * - WB for WB-able memory and no other conflicting mappings
	 * - UC_MINUS for non-WB-able memory with no other conflicting mappings
	 * - Inherit from confliting mappings otherwise
	 */
	if (flags != -1) {
		retval = reserve_memtype(offset, offset + size, flags, NULL);
	} else {
		retval = reserve_memtype(offset, offset + size, -1, &flags);
	}

	if (retval < 0)
		return 0;

	if (((pfn < max_low_pfn_mapped) ||
	     (pfn >= (1UL<<(32 - PAGE_SHIFT)) && pfn < max_pfn_mapped)) &&
	    ioremap_change_attr((unsigned long)__va(offset), size, flags) < 0) {
		free_memtype(offset, offset + size);
		printk(KERN_INFO
		"%s:%d /dev/mem ioremap_change_attr failed %s for %Lx-%Lx\n",
			current->comm, current->pid,
			cattr_name(flags),
			offset, (unsigned long long)(offset + size));
		return 0;
	}

	*vma_prot = __pgprot((pgprot_val(*vma_prot) & ~_PAGE_CACHE_MASK) |
			     flags);
	return 1;
}

void map_devmem(unsigned long pfn, unsigned long size, pgprot_t vma_prot)
{
	unsigned long want_flags = (pgprot_val(vma_prot) & _PAGE_CACHE_MASK);
	u64 addr = (u64)pfn << PAGE_SHIFT;
	unsigned long flags;

	reserve_memtype(addr, addr + size, want_flags, &flags);
	if (flags != want_flags) {
		printk(KERN_INFO
		"%s:%d /dev/mem expected mapping type %s for %Lx-%Lx, got %s\n",
			current->comm, current->pid,
			cattr_name(want_flags),
			addr, (unsigned long long)(addr + size),
			cattr_name(flags));
	}
}

void unmap_devmem(unsigned long pfn, unsigned long size, pgprot_t vma_prot)
{
	u64 addr = (u64)pfn << PAGE_SHIFT;

	free_memtype(addr, addr + size);
}

/*
 * Change the memory type for the physial address range in kernel identity
 * mapping space if that range is a part of identity map.
 */
int kernel_map_sync_memtype(u64 base, unsigned long size, unsigned long flags)
{
	unsigned long id_sz;

	if (!pat_enabled || base >= __pa(high_memory))
		return 0;

	id_sz = (__pa(high_memory) < base + size) ?
				__pa(high_memory) - base :
				size;

	if (ioremap_change_attr((unsigned long)__va(base), id_sz, flags) < 0) {
		printk(KERN_INFO
			"%s:%d ioremap_change_attr failed %s "
			"for %Lx-%Lx\n",
			current->comm, current->pid,
			cattr_name(flags),
			base, (unsigned long long)(base + size));
		return -EINVAL;
	}
	return 0;
}

/*
 * Internal interface to reserve a range of physical memory with prot.
 * Reserved non RAM regions only and after successful reserve_memtype,
 * this func also keeps identity mapping (if any) in sync with this new prot.
 */
static int reserve_pfn_range(u64 paddr, unsigned long size, pgprot_t *vma_prot,
				int strict_prot)
{
	int is_ram = 0;
	int ret;
	unsigned long flags;
	unsigned long want_flags = (pgprot_val(*vma_prot) & _PAGE_CACHE_MASK);

	is_ram = pat_pagerange_is_ram(paddr, paddr + size);

	/*
	 * reserve_pfn_range() doesn't support RAM pages. Maintain the current
	 * behavior with RAM pages by returning success.
	 */
	if (is_ram != 0)
		return 0;

	ret = reserve_memtype(paddr, paddr + size, want_flags, &flags);
	if (ret)
		return ret;

	if (flags != want_flags) {
		if (strict_prot || !is_new_memtype_allowed(want_flags, flags)) {
			free_memtype(paddr, paddr + size);
			printk(KERN_ERR "%s:%d map pfn expected mapping type %s"
				" for %Lx-%Lx, got %s\n",
				current->comm, current->pid,
				cattr_name(want_flags),
				(unsigned long long)paddr,
				(unsigned long long)(paddr + size),
				cattr_name(flags));
			return -EINVAL;
		}
		/*
		 * We allow returning different type than the one requested in
		 * non strict case.
		 */
		*vma_prot = __pgprot((pgprot_val(*vma_prot) &
				      (~_PAGE_CACHE_MASK)) |
				     flags);
	}

	if (kernel_map_sync_memtype(paddr, size, flags) < 0) {
		free_memtype(paddr, paddr + size);
		return -EINVAL;
	}
	return 0;
}

/*
 * Internal interface to free a range of physical memory.
 * Frees non RAM regions only.
 */
static void free_pfn_range(u64 paddr, unsigned long size)
{
	int is_ram;

	is_ram = pat_pagerange_is_ram(paddr, paddr + size);
	if (is_ram == 0)
		free_memtype(paddr, paddr + size);
}

/*
 * track_pfn_vma_copy is called when vma that is covering the pfnmap gets
 * copied through copy_page_range().
 *
 * If the vma has a linear pfn mapping for the entire range, we get the prot
 * from pte and reserve the entire vma range with single reserve_pfn_range call.
 * Otherwise, we reserve the entire vma range, my ging through the PTEs page
 * by page to get physical address and protection.
 */
int track_pfn_vma_copy(struct vm_area_struct *vma)
{
	int retval = 0;
	unsigned long i, j;
	resource_size_t paddr;
	unsigned long prot;
	unsigned long vma_start = vma->vm_start;
	unsigned long vma_end = vma->vm_end;
	unsigned long vma_size = vma_end - vma_start;
	pgprot_t pgprot;

	if (!pat_enabled)
		return 0;

	if (is_linear_pfn_mapping(vma)) {
		/*
		 * reserve the whole chunk covered by vma. We need the
		 * starting address and protection from pte.
		 */
		if (follow_phys(vma, vma_start, 0, &prot, &paddr)) {
			WARN_ON_ONCE(1);
			return -EINVAL;
		}
		pgprot = __pgprot(prot);
		return reserve_pfn_range(paddr, vma_size, &pgprot, 1);
	}

	/* reserve entire vma page by page, using pfn and prot from pte */
	for (i = 0; i < vma_size; i += PAGE_SIZE) {
		if (follow_phys(vma, vma_start + i, 0, &prot, &paddr))
			continue;

		pgprot = __pgprot(prot);
		retval = reserve_pfn_range(paddr, PAGE_SIZE, &pgprot, 1);
		if (retval)
			goto cleanup_ret;
	}
	return 0;

cleanup_ret:
	/* Reserve error: Cleanup partial reservation and return error */
	for (j = 0; j < i; j += PAGE_SIZE) {
		if (follow_phys(vma, vma_start + j, 0, &prot, &paddr))
			continue;

		free_pfn_range(paddr, PAGE_SIZE);
	}

	return retval;
}

/*
 * track_pfn_vma_new is called when a _new_ pfn mapping is being established
 * for physical range indicated by pfn and size.
 *
 * prot is passed in as a parameter for the new mapping. If the vma has a
 * linear pfn mapping for the entire range reserve the entire vma range with
 * single reserve_pfn_range call.
 * Otherwise, we look t the pfn and size and reserve only the specified range
 * page by page.
 *
 * Note that this function can be called with caller trying to map only a
 * subrange/page inside the vma.
 */
int track_pfn_vma_new(struct vm_area_struct *vma, pgprot_t *prot,
			unsigned long pfn, unsigned long size)
{
	int retval = 0;
	unsigned long i, j;
	resource_size_t base_paddr;
	resource_size_t paddr;
	unsigned long vma_start = vma->vm_start;
	unsigned long vma_end = vma->vm_end;
	unsigned long vma_size = vma_end - vma_start;

	if (!pat_enabled)
		return 0;

	if (is_linear_pfn_mapping(vma)) {
		/* reserve the whole chunk starting from vm_pgoff */
		paddr = (resource_size_t)vma->vm_pgoff << PAGE_SHIFT;
		return reserve_pfn_range(paddr, vma_size, prot, 0);
	}

	/* reserve page by page using pfn and size */
	base_paddr = (resource_size_t)pfn << PAGE_SHIFT;
	for (i = 0; i < size; i += PAGE_SIZE) {
		paddr = base_paddr + i;
		retval = reserve_pfn_range(paddr, PAGE_SIZE, prot, 0);
		if (retval)
			goto cleanup_ret;
	}
	return 0;

cleanup_ret:
	/* Reserve error: Cleanup partial reservation and return error */
	for (j = 0; j < i; j += PAGE_SIZE) {
		paddr = base_paddr + j;
		free_pfn_range(paddr, PAGE_SIZE);
	}

	return retval;
}

/*
 * untrack_pfn_vma is called while unmapping a pfnmap for a region.
 * untrack can be called for a specific region indicated by pfn and size or
 * can be for the entire vma (in which case size can be zero).
 */
void untrack_pfn_vma(struct vm_area_struct *vma, unsigned long pfn,
			unsigned long size)
{
	unsigned long i;
	resource_size_t paddr;
	unsigned long prot;
	unsigned long vma_start = vma->vm_start;
	unsigned long vma_end = vma->vm_end;
	unsigned long vma_size = vma_end - vma_start;

	if (!pat_enabled)
		return;

	if (is_linear_pfn_mapping(vma)) {
		/* free the whole chunk starting from vm_pgoff */
		paddr = (resource_size_t)vma->vm_pgoff << PAGE_SHIFT;
		free_pfn_range(paddr, vma_size);
		return;
	}

	if (size != 0 && size != vma_size) {
		/* free page by page, using pfn and size */
		paddr = (resource_size_t)pfn << PAGE_SHIFT;
		for (i = 0; i < size; i += PAGE_SIZE) {
			paddr = paddr + i;
			free_pfn_range(paddr, PAGE_SIZE);
		}
	} else {
		/* free entire vma, page by page, using the pfn from pte */
		for (i = 0; i < vma_size; i += PAGE_SIZE) {
			if (follow_phys(vma, vma_start + i, 0, &prot, &paddr))
				continue;

			free_pfn_range(paddr, PAGE_SIZE);
		}
	}
}

pgprot_t pgprot_writecombine(pgprot_t prot)
{
	if (pat_enabled)
		return __pgprot(pgprot_val(prot) | _PAGE_CACHE_WC);
	else
		return pgprot_noncached(prot);
}
EXPORT_SYMBOL_GPL(pgprot_writecombine);

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_X86_PAT)

/* get Nth element of the linked list */
static struct memtype *memtype_get_idx(loff_t pos)
{
	struct memtype *list_node, *print_entry;
	int i = 1;

	print_entry  = kmalloc(sizeof(struct memtype), GFP_KERNEL);
	if (!print_entry)
		return NULL;

	spin_lock(&memtype_lock);
	list_for_each_entry(list_node, &memtype_list, nd) {
		if (pos == i) {
			*print_entry = *list_node;
			spin_unlock(&memtype_lock);
			return print_entry;
		}
		++i;
	}
	spin_unlock(&memtype_lock);
	kfree(print_entry);

	return NULL;
}

static void *memtype_seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos == 0) {
		++*pos;
		seq_printf(seq, "PAT memtype list:\n");
	}

	return memtype_get_idx(*pos);
}

static void *memtype_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return memtype_get_idx(*pos);
}

static void memtype_seq_stop(struct seq_file *seq, void *v)
{
}

static int memtype_seq_show(struct seq_file *seq, void *v)
{
	struct memtype *print_entry = (struct memtype *)v;

	seq_printf(seq, "%s @ 0x%Lx-0x%Lx\n", cattr_name(print_entry->type),
			print_entry->start, print_entry->end);
	kfree(print_entry);

	return 0;
}

static struct seq_operations memtype_seq_ops = {
	.start = memtype_seq_start,
	.next  = memtype_seq_next,
	.stop  = memtype_seq_stop,
	.show  = memtype_seq_show,
};

static int memtype_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &memtype_seq_ops);
}

static const struct file_operations memtype_fops = {
	.open    = memtype_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

static int __init pat_memtype_list_init(void)
{
	debugfs_create_file("pat_memtype_list", S_IRUSR, arch_debugfs_dir,
				NULL, &memtype_fops);
	return 0;
}

late_initcall(pat_memtype_list_init);

#endif /* CONFIG_DEBUG_FS && CONFIG_X86_PAT */
