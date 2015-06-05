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
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/rbtree.h>

#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/x86_init.h>
#include <asm/pgtable.h>
#include <asm/fcntl.h>
#include <asm/e820.h>
#include <asm/mtrr.h>
#include <asm/page.h>
#include <asm/msr.h>
#include <asm/pat.h>
#include <asm/io.h>

#include "pat_internal.h"
#include "mm_internal.h"

#ifdef CONFIG_X86_PAT
int __read_mostly pat_enabled = 1;

static inline void pat_disable(const char *reason)
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


int pat_debug_enable;

static int __init pat_debug_setup(char *str)
{
	pat_debug_enable = 1;
	return 0;
}
__setup("debugpat", pat_debug_setup);

static u64 __read_mostly boot_pat_state;

#ifdef CONFIG_X86_PAT
/*
 * X86 PAT uses page flags WC and Uncached together to keep track of
 * memory type of pages that have backing page struct. X86 PAT supports 3
 * different memory types, _PAGE_CACHE_MODE_WB, _PAGE_CACHE_MODE_WC and
 * _PAGE_CACHE_MODE_UC_MINUS and fourth state where page's memory type has not
 * been changed from its default (value of -1 used to denote this).
 * Note we do not support _PAGE_CACHE_MODE_UC here.
 */

#define _PGMT_DEFAULT		0
#define _PGMT_WC		(1UL << PG_arch_1)
#define _PGMT_UC_MINUS		(1UL << PG_uncached)
#define _PGMT_WB		(1UL << PG_uncached | 1UL << PG_arch_1)
#define _PGMT_MASK		(1UL << PG_uncached | 1UL << PG_arch_1)
#define _PGMT_CLEAR_MASK	(~_PGMT_MASK)

static inline enum page_cache_mode get_page_memtype(struct page *pg)
{
	unsigned long pg_flags = pg->flags & _PGMT_MASK;

	if (pg_flags == _PGMT_DEFAULT)
		return -1;
	else if (pg_flags == _PGMT_WC)
		return _PAGE_CACHE_MODE_WC;
	else if (pg_flags == _PGMT_UC_MINUS)
		return _PAGE_CACHE_MODE_UC_MINUS;
	else
		return _PAGE_CACHE_MODE_WB;
}

static inline void set_page_memtype(struct page *pg,
				    enum page_cache_mode memtype)
{
	unsigned long memtype_flags;
	unsigned long old_flags;
	unsigned long new_flags;

	switch (memtype) {
	case _PAGE_CACHE_MODE_WC:
		memtype_flags = _PGMT_WC;
		break;
	case _PAGE_CACHE_MODE_UC_MINUS:
		memtype_flags = _PGMT_UC_MINUS;
		break;
	case _PAGE_CACHE_MODE_WB:
		memtype_flags = _PGMT_WB;
		break;
	default:
		memtype_flags = _PGMT_DEFAULT;
		break;
	}

	do {
		old_flags = pg->flags;
		new_flags = (old_flags & _PGMT_CLEAR_MASK) | memtype_flags;
	} while (cmpxchg(&pg->flags, old_flags, new_flags) != old_flags);
}
#else
static inline enum page_cache_mode get_page_memtype(struct page *pg)
{
	return -1;
}
static inline void set_page_memtype(struct page *pg,
				    enum page_cache_mode memtype)
{
}
#endif

enum {
	PAT_UC = 0,		/* uncached */
	PAT_WC = 1,		/* Write combining */
	PAT_WT = 4,		/* Write Through */
	PAT_WP = 5,		/* Write Protected */
	PAT_WB = 6,		/* Write Back (default) */
	PAT_UC_MINUS = 7,	/* UC, but can be overriden by MTRR */
};

#define CM(c) (_PAGE_CACHE_MODE_ ## c)

static enum page_cache_mode pat_get_cache_mode(unsigned pat_val, char *msg)
{
	enum page_cache_mode cache;
	char *cache_mode;

	switch (pat_val) {
	case PAT_UC:       cache = CM(UC);       cache_mode = "UC  "; break;
	case PAT_WC:       cache = CM(WC);       cache_mode = "WC  "; break;
	case PAT_WT:       cache = CM(WT);       cache_mode = "WT  "; break;
	case PAT_WP:       cache = CM(WP);       cache_mode = "WP  "; break;
	case PAT_WB:       cache = CM(WB);       cache_mode = "WB  "; break;
	case PAT_UC_MINUS: cache = CM(UC_MINUS); cache_mode = "UC- "; break;
	default:           cache = CM(WB);       cache_mode = "WB  "; break;
	}

	memcpy(msg, cache_mode, 4);

	return cache;
}

#undef CM

/*
 * Update the cache mode to pgprot translation tables according to PAT
 * configuration.
 * Using lower indices is preferred, so we start with highest index.
 */
void pat_init_cache_modes(void)
{
	int i;
	enum page_cache_mode cache;
	char pat_msg[33];
	u64 pat;

	rdmsrl(MSR_IA32_CR_PAT, pat);
	pat_msg[32] = 0;
	for (i = 7; i >= 0; i--) {
		cache = pat_get_cache_mode((pat >> (i * 8)) & 7,
					   pat_msg + 4 * i);
		update_cache_mode_entry(i, cache);
	}
	pr_info("PAT configuration [0-7]: %s\n", pat_msg);
}

#define PAT(x, y)	((u64)PAT_ ## y << ((x)*8))

void pat_init(void)
{
	u64 pat;
	bool boot_cpu = !boot_pat_state;

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
	if (!boot_pat_state) {
		rdmsrl(MSR_IA32_CR_PAT, boot_pat_state);
		if (!boot_pat_state) {
			pat_disable("PAT read returns always zero, disabled.");
			return;
		}
	}

	wrmsrl(MSR_IA32_CR_PAT, pat);

	if (boot_cpu)
		pat_init_cache_modes();
}

#undef PAT

static DEFINE_SPINLOCK(memtype_lock);	/* protects memtype accesses */

/*
 * Does intersection of PAT memory type and MTRR memory type and returns
 * the resulting memory type as PAT understands it.
 * (Type in pat and mtrr will not have same value)
 * The intersection is based on "Effective Memory Type" tables in IA-32
 * SDM vol 3a
 */
static unsigned long pat_x_mtrr_type(u64 start, u64 end,
				     enum page_cache_mode req_type)
{
	/*
	 * Look for MTRR hint to get the effective type in case where PAT
	 * request is for WB.
	 */
	if (req_type == _PAGE_CACHE_MODE_WB) {
		u8 mtrr_type;

		mtrr_type = mtrr_type_lookup(start, end);
		if (mtrr_type != MTRR_TYPE_WRBACK)
			return _PAGE_CACHE_MODE_UC_MINUS;

		return _PAGE_CACHE_MODE_WB;
	}

	return req_type;
}

struct pagerange_state {
	unsigned long		cur_pfn;
	int			ram;
	int			not_ram;
};

static int
pagerange_is_ram_callback(unsigned long initial_pfn, unsigned long total_nr_pages, void *arg)
{
	struct pagerange_state *state = arg;

	state->not_ram	|= initial_pfn > state->cur_pfn;
	state->ram	|= total_nr_pages > 0;
	state->cur_pfn	 = initial_pfn + total_nr_pages;

	return state->ram && state->not_ram;
}

static int pat_pagerange_is_ram(resource_size_t start, resource_size_t end)
{
	int ret = 0;
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long end_pfn = (end + PAGE_SIZE - 1) >> PAGE_SHIFT;
	struct pagerange_state state = {start_pfn, 0, 0};

	/*
	 * For legacy reasons, physical address range in the legacy ISA
	 * region is tracked as non-RAM. This will allow users of
	 * /dev/mem to map portions of legacy ISA region, even when
	 * some of those portions are listed(or not even listed) with
	 * different e820 types(RAM/reserved/..)
	 */
	if (start_pfn < ISA_END_ADDRESS >> PAGE_SHIFT)
		start_pfn = ISA_END_ADDRESS >> PAGE_SHIFT;

	if (start_pfn < end_pfn) {
		ret = walk_system_ram_range(start_pfn, end_pfn - start_pfn,
				&state, pagerange_is_ram_callback);
	}

	return (ret > 0) ? -1 : (state.ram ? 1 : 0);
}

/*
 * For RAM pages, we use page flags to mark the pages with appropriate type.
 * Here we do two pass:
 * - Find the memtype of all the pages in the range, look for any conflicts
 * - In case of no conflicts, set the new memtype for pages in the range
 */
static int reserve_ram_pages_type(u64 start, u64 end,
				  enum page_cache_mode req_type,
				  enum page_cache_mode *new_type)
{
	struct page *page;
	u64 pfn;

	if (req_type == _PAGE_CACHE_MODE_UC) {
		/* We do not support strong UC */
		WARN_ON_ONCE(1);
		req_type = _PAGE_CACHE_MODE_UC_MINUS;
	}

	for (pfn = (start >> PAGE_SHIFT); pfn < (end >> PAGE_SHIFT); ++pfn) {
		enum page_cache_mode type;

		page = pfn_to_page(pfn);
		type = get_page_memtype(page);
		if (type != -1) {
			pr_info("reserve_ram_pages_type failed [mem %#010Lx-%#010Lx], track 0x%x, req 0x%x\n",
				start, end - 1, type, req_type);
			if (new_type)
				*new_type = type;

			return -EBUSY;
		}
	}

	if (new_type)
		*new_type = req_type;

	for (pfn = (start >> PAGE_SHIFT); pfn < (end >> PAGE_SHIFT); ++pfn) {
		page = pfn_to_page(pfn);
		set_page_memtype(page, req_type);
	}
	return 0;
}

static int free_ram_pages_type(u64 start, u64 end)
{
	struct page *page;
	u64 pfn;

	for (pfn = (start >> PAGE_SHIFT); pfn < (end >> PAGE_SHIFT); ++pfn) {
		page = pfn_to_page(pfn);
		set_page_memtype(page, -1);
	}
	return 0;
}

/*
 * req_type typically has one of the:
 * - _PAGE_CACHE_MODE_WB
 * - _PAGE_CACHE_MODE_WC
 * - _PAGE_CACHE_MODE_UC_MINUS
 * - _PAGE_CACHE_MODE_UC
 *
 * If new_type is NULL, function will return an error if it cannot reserve the
 * region with req_type. If new_type is non-NULL, function will return
 * available type in new_type in case of no error. In case of any error
 * it will return a negative return value.
 */
int reserve_memtype(u64 start, u64 end, enum page_cache_mode req_type,
		    enum page_cache_mode *new_type)
{
	struct memtype *new;
	enum page_cache_mode actual_type;
	int is_range_ram;
	int err = 0;

	BUG_ON(start >= end); /* end is exclusive */

	if (!pat_enabled) {
		/* This is identical to page table setting without PAT */
		if (new_type) {
			if (req_type == _PAGE_CACHE_MODE_WC)
				*new_type = _PAGE_CACHE_MODE_UC_MINUS;
			else
				*new_type = req_type;
		}
		return 0;
	}

	/* Low ISA region is always mapped WB in page table. No need to track */
	if (x86_platform.is_untracked_pat_range(start, end)) {
		if (new_type)
			*new_type = _PAGE_CACHE_MODE_WB;
		return 0;
	}

	/*
	 * Call mtrr_lookup to get the type hint. This is an
	 * optimization for /dev/mem mmap'ers into WB memory (BIOS
	 * tools and ACPI tools). Use WB request for WB memory and use
	 * UC_MINUS otherwise.
	 */
	actual_type = pat_x_mtrr_type(start, end, req_type);

	if (new_type)
		*new_type = actual_type;

	is_range_ram = pat_pagerange_is_ram(start, end);
	if (is_range_ram == 1) {

		err = reserve_ram_pages_type(start, end, req_type, new_type);

		return err;
	} else if (is_range_ram < 0) {
		return -EINVAL;
	}

	new  = kzalloc(sizeof(struct memtype), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	new->start	= start;
	new->end	= end;
	new->type	= actual_type;

	spin_lock(&memtype_lock);

	err = rbt_memtype_check_insert(new, new_type);
	if (err) {
		printk(KERN_INFO "reserve_memtype failed [mem %#010Lx-%#010Lx], track %s, req %s\n",
		       start, end - 1,
		       cattr_name(new->type), cattr_name(req_type));
		kfree(new);
		spin_unlock(&memtype_lock);

		return err;
	}

	spin_unlock(&memtype_lock);

	dprintk("reserve_memtype added [mem %#010Lx-%#010Lx], track %s, req %s, ret %s\n",
		start, end - 1, cattr_name(new->type), cattr_name(req_type),
		new_type ? cattr_name(*new_type) : "-");

	return err;
}

int free_memtype(u64 start, u64 end)
{
	int err = -EINVAL;
	int is_range_ram;
	struct memtype *entry;

	if (!pat_enabled)
		return 0;

	/* Low ISA region is always mapped WB. No need to track */
	if (x86_platform.is_untracked_pat_range(start, end))
		return 0;

	is_range_ram = pat_pagerange_is_ram(start, end);
	if (is_range_ram == 1) {

		err = free_ram_pages_type(start, end);

		return err;
	} else if (is_range_ram < 0) {
		return -EINVAL;
	}

	spin_lock(&memtype_lock);
	entry = rbt_memtype_erase(start, end);
	spin_unlock(&memtype_lock);

	if (!entry) {
		printk(KERN_INFO "%s:%d freeing invalid memtype [mem %#010Lx-%#010Lx]\n",
		       current->comm, current->pid, start, end - 1);
		return -EINVAL;
	}

	kfree(entry);

	dprintk("free_memtype request [mem %#010Lx-%#010Lx]\n", start, end - 1);

	return 0;
}


/**
 * lookup_memtype - Looksup the memory type for a physical address
 * @paddr: physical address of which memory type needs to be looked up
 *
 * Only to be called when PAT is enabled
 *
 * Returns _PAGE_CACHE_MODE_WB, _PAGE_CACHE_MODE_WC, _PAGE_CACHE_MODE_UC_MINUS
 * or _PAGE_CACHE_MODE_UC
 */
static enum page_cache_mode lookup_memtype(u64 paddr)
{
	enum page_cache_mode rettype = _PAGE_CACHE_MODE_WB;
	struct memtype *entry;

	if (x86_platform.is_untracked_pat_range(paddr, paddr + PAGE_SIZE))
		return rettype;

	if (pat_pagerange_is_ram(paddr, paddr + PAGE_SIZE)) {
		struct page *page;
		page = pfn_to_page(paddr >> PAGE_SHIFT);
		rettype = get_page_memtype(page);
		/*
		 * -1 from get_page_memtype() implies RAM page is in its
		 * default state and not reserved, and hence of type WB
		 */
		if (rettype == -1)
			rettype = _PAGE_CACHE_MODE_WB;

		return rettype;
	}

	spin_lock(&memtype_lock);

	entry = rbt_memtype_lookup(paddr);
	if (entry != NULL)
		rettype = entry->type;
	else
		rettype = _PAGE_CACHE_MODE_UC_MINUS;

	spin_unlock(&memtype_lock);
	return rettype;
}

/**
 * io_reserve_memtype - Request a memory type mapping for a region of memory
 * @start: start (physical address) of the region
 * @end: end (physical address) of the region
 * @type: A pointer to memtype, with requested type. On success, requested
 * or any other compatible type that was available for the region is returned
 *
 * On success, returns 0
 * On failure, returns non-zero
 */
int io_reserve_memtype(resource_size_t start, resource_size_t end,
			enum page_cache_mode *type)
{
	resource_size_t size = end - start;
	enum page_cache_mode req_type = *type;
	enum page_cache_mode new_type;
	int ret;

	WARN_ON_ONCE(iomem_map_sanity_check(start, size));

	ret = reserve_memtype(start, end, req_type, &new_type);
	if (ret)
		goto out_err;

	if (!is_new_memtype_allowed(start, size, req_type, new_type))
		goto out_free;

	if (kernel_map_sync_memtype(start, size, new_type) < 0)
		goto out_free;

	*type = new_type;
	return 0;

out_free:
	free_memtype(start, end);
	ret = -EBUSY;
out_err:
	return ret;
}

/**
 * io_free_memtype - Release a memory type mapping for a region of memory
 * @start: start (physical address) of the region
 * @end: end (physical address) of the region
 */
void io_free_memtype(resource_size_t start, resource_size_t end)
{
	free_memtype(start, end);
}

pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				unsigned long size, pgprot_t vma_prot)
{
	return vma_prot;
}

#ifdef CONFIG_STRICT_DEVMEM
/* This check is done in drivers/char/mem.c in case of STRICT_DEVMEM */
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
			printk(KERN_INFO "Program %s tried to access /dev/mem between [mem %#010Lx-%#010Lx], PAT prevents it\n",
			       current->comm, from, to - 1);
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
	enum page_cache_mode pcm = _PAGE_CACHE_MODE_WB;

	if (!range_is_allowed(pfn, size))
		return 0;

	if (file->f_flags & O_DSYNC)
		pcm = _PAGE_CACHE_MODE_UC_MINUS;

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
		pcm = _PAGE_CACHE_MODE_UC;
	}
#endif

	*vma_prot = __pgprot((pgprot_val(*vma_prot) & ~_PAGE_CACHE_MASK) |
			     cachemode2protval(pcm));
	return 1;
}

/*
 * Change the memory type for the physial address range in kernel identity
 * mapping space if that range is a part of identity map.
 */
int kernel_map_sync_memtype(u64 base, unsigned long size,
			    enum page_cache_mode pcm)
{
	unsigned long id_sz;

	if (base > __pa(high_memory-1))
		return 0;

	/*
	 * some areas in the middle of the kernel identity range
	 * are not mapped, like the PCI space.
	 */
	if (!page_is_ram(base >> PAGE_SHIFT))
		return 0;

	id_sz = (__pa(high_memory-1) <= base + size) ?
				__pa(high_memory) - base :
				size;

	if (ioremap_change_attr((unsigned long)__va(base), id_sz, pcm) < 0) {
		printk(KERN_INFO "%s:%d ioremap_change_attr failed %s "
			"for [mem %#010Lx-%#010Lx]\n",
			current->comm, current->pid,
			cattr_name(pcm),
			base, (unsigned long long)(base + size-1));
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
	enum page_cache_mode want_pcm = pgprot2cachemode(*vma_prot);
	enum page_cache_mode pcm = want_pcm;

	is_ram = pat_pagerange_is_ram(paddr, paddr + size);

	/*
	 * reserve_pfn_range() for RAM pages. We do not refcount to keep
	 * track of number of mappings of RAM pages. We can assert that
	 * the type requested matches the type of first page in the range.
	 */
	if (is_ram) {
		if (!pat_enabled)
			return 0;

		pcm = lookup_memtype(paddr);
		if (want_pcm != pcm) {
			printk(KERN_WARNING "%s:%d map pfn RAM range req %s for [mem %#010Lx-%#010Lx], got %s\n",
				current->comm, current->pid,
				cattr_name(want_pcm),
				(unsigned long long)paddr,
				(unsigned long long)(paddr + size - 1),
				cattr_name(pcm));
			*vma_prot = __pgprot((pgprot_val(*vma_prot) &
					     (~_PAGE_CACHE_MASK)) |
					     cachemode2protval(pcm));
		}
		return 0;
	}

	ret = reserve_memtype(paddr, paddr + size, want_pcm, &pcm);
	if (ret)
		return ret;

	if (pcm != want_pcm) {
		if (strict_prot ||
		    !is_new_memtype_allowed(paddr, size, want_pcm, pcm)) {
			free_memtype(paddr, paddr + size);
			printk(KERN_ERR "%s:%d map pfn expected mapping type %s"
				" for [mem %#010Lx-%#010Lx], got %s\n",
				current->comm, current->pid,
				cattr_name(want_pcm),
				(unsigned long long)paddr,
				(unsigned long long)(paddr + size - 1),
				cattr_name(pcm));
			return -EINVAL;
		}
		/*
		 * We allow returning different type than the one requested in
		 * non strict case.
		 */
		*vma_prot = __pgprot((pgprot_val(*vma_prot) &
				      (~_PAGE_CACHE_MASK)) |
				     cachemode2protval(pcm));
	}

	if (kernel_map_sync_memtype(paddr, size, pcm) < 0) {
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
 * track_pfn_copy is called when vma that is covering the pfnmap gets
 * copied through copy_page_range().
 *
 * If the vma has a linear pfn mapping for the entire range, we get the prot
 * from pte and reserve the entire vma range with single reserve_pfn_range call.
 */
int track_pfn_copy(struct vm_area_struct *vma)
{
	resource_size_t paddr;
	unsigned long prot;
	unsigned long vma_size = vma->vm_end - vma->vm_start;
	pgprot_t pgprot;

	if (vma->vm_flags & VM_PAT) {
		/*
		 * reserve the whole chunk covered by vma. We need the
		 * starting address and protection from pte.
		 */
		if (follow_phys(vma, vma->vm_start, 0, &prot, &paddr)) {
			WARN_ON_ONCE(1);
			return -EINVAL;
		}
		pgprot = __pgprot(prot);
		return reserve_pfn_range(paddr, vma_size, &pgprot, 1);
	}

	return 0;
}

/*
 * prot is passed in as a parameter for the new mapping. If the vma has a
 * linear pfn mapping for the entire range reserve the entire vma range with
 * single reserve_pfn_range call.
 */
int track_pfn_remap(struct vm_area_struct *vma, pgprot_t *prot,
		    unsigned long pfn, unsigned long addr, unsigned long size)
{
	resource_size_t paddr = (resource_size_t)pfn << PAGE_SHIFT;
	enum page_cache_mode pcm;

	/* reserve the whole chunk starting from paddr */
	if (addr == vma->vm_start && size == (vma->vm_end - vma->vm_start)) {
		int ret;

		ret = reserve_pfn_range(paddr, size, prot, 0);
		if (!ret)
			vma->vm_flags |= VM_PAT;
		return ret;
	}

	if (!pat_enabled)
		return 0;

	/*
	 * For anything smaller than the vma size we set prot based on the
	 * lookup.
	 */
	pcm = lookup_memtype(paddr);

	/* Check memtype for the remaining pages */
	while (size > PAGE_SIZE) {
		size -= PAGE_SIZE;
		paddr += PAGE_SIZE;
		if (pcm != lookup_memtype(paddr))
			return -EINVAL;
	}

	*prot = __pgprot((pgprot_val(vma->vm_page_prot) & (~_PAGE_CACHE_MASK)) |
			 cachemode2protval(pcm));

	return 0;
}

int track_pfn_insert(struct vm_area_struct *vma, pgprot_t *prot,
		     unsigned long pfn)
{
	enum page_cache_mode pcm;

	if (!pat_enabled)
		return 0;

	/* Set prot based on lookup */
	pcm = lookup_memtype((resource_size_t)pfn << PAGE_SHIFT);
	*prot = __pgprot((pgprot_val(vma->vm_page_prot) & (~_PAGE_CACHE_MASK)) |
			 cachemode2protval(pcm));

	return 0;
}

/*
 * untrack_pfn is called while unmapping a pfnmap for a region.
 * untrack can be called for a specific region indicated by pfn and size or
 * can be for the entire vma (in which case pfn, size are zero).
 */
void untrack_pfn(struct vm_area_struct *vma, unsigned long pfn,
		 unsigned long size)
{
	resource_size_t paddr;
	unsigned long prot;

	if (!(vma->vm_flags & VM_PAT))
		return;

	/* free the chunk starting from pfn or the whole chunk */
	paddr = (resource_size_t)pfn << PAGE_SHIFT;
	if (!paddr && !size) {
		if (follow_phys(vma, vma->vm_start, 0, &prot, &paddr)) {
			WARN_ON_ONCE(1);
			return;
		}

		size = vma->vm_end - vma->vm_start;
	}
	free_pfn_range(paddr, size);
	vma->vm_flags &= ~VM_PAT;
}

pgprot_t pgprot_writecombine(pgprot_t prot)
{
	if (pat_enabled)
		return __pgprot(pgprot_val(prot) |
				cachemode2protval(_PAGE_CACHE_MODE_WC));
	else
		return pgprot_noncached(prot);
}
EXPORT_SYMBOL_GPL(pgprot_writecombine);

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_X86_PAT)

static struct memtype *memtype_get_idx(loff_t pos)
{
	struct memtype *print_entry;
	int ret;

	print_entry  = kzalloc(sizeof(struct memtype), GFP_KERNEL);
	if (!print_entry)
		return NULL;

	spin_lock(&memtype_lock);
	ret = rbt_memtype_copy_nth_element(print_entry, pos);
	spin_unlock(&memtype_lock);

	if (!ret) {
		return print_entry;
	} else {
		kfree(print_entry);
		return NULL;
	}
}

static void *memtype_seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos == 0) {
		++*pos;
		seq_puts(seq, "PAT memtype list:\n");
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

static const struct seq_operations memtype_seq_ops = {
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
	if (pat_enabled) {
		debugfs_create_file("pat_memtype_list", S_IRUSR,
				    arch_debugfs_dir, NULL, &memtype_fops);
	}
	return 0;
}

late_initcall(pat_memtype_list_init);

#endif /* CONFIG_DEBUG_FS && CONFIG_X86_PAT */
