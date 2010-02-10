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
	if (!boot_pat_state)
		rdmsrl(MSR_IA32_CR_PAT, boot_pat_state);

	wrmsrl(MSR_IA32_CR_PAT, pat);

	if (boot_cpu)
		printk(KERN_INFO "x86 PAT enabled: cpu %d, old 0x%Lx, new 0x%Lx\n",
		       smp_processor_id(), boot_pat_state, pat);
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
static unsigned long pat_x_mtrr_type(u64 start, u64 end, unsigned long req_type)
{
	/*
	 * Look for MTRR hint to get the effective type in case where PAT
	 * request is for WB.
	 */
	if (req_type == _PAGE_CACHE_WB) {
		u8 mtrr_type;

		mtrr_type = mtrr_type_lookup(start, end);
		if (mtrr_type != MTRR_TYPE_WRBACK)
			return _PAGE_CACHE_UC_MINUS;

		return _PAGE_CACHE_WB;
	}

	return req_type;
}

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
 * For RAM pages, we use page flags to mark the pages with appropriate type.
 * Here we do two pass:
 * - Find the memtype of all the pages in the range, look for any conflicts
 * - In case of no conflicts, set the new memtype for pages in the range
 *
 * Caller must hold memtype_lock for atomicity.
 */
static int reserve_ram_pages_type(u64 start, u64 end, unsigned long req_type,
				  unsigned long *new_type)
{
	struct page *page;
	u64 pfn;

	if (req_type == _PAGE_CACHE_UC) {
		/* We do not support strong UC */
		WARN_ON_ONCE(1);
		req_type = _PAGE_CACHE_UC_MINUS;
	}

	for (pfn = (start >> PAGE_SHIFT); pfn < (end >> PAGE_SHIFT); ++pfn) {
		unsigned long type;

		page = pfn_to_page(pfn);
		type = get_page_memtype(page);
		if (type != -1) {
			printk(KERN_INFO "reserve_ram_pages_type failed "
				"0x%Lx-0x%Lx, track 0x%lx, req 0x%lx\n",
				start, end, type, req_type);
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
 * - _PAGE_CACHE_WB
 * - _PAGE_CACHE_WC
 * - _PAGE_CACHE_UC_MINUS
 * - _PAGE_CACHE_UC
 *
 * If new_type is NULL, function will return an error if it cannot reserve the
 * region with req_type. If new_type is non-NULL, function will return
 * available type in new_type in case of no error. In case of any error
 * it will return a negative return value.
 */
int reserve_memtype(u64 start, u64 end, unsigned long req_type,
		    unsigned long *new_type)
{
	struct memtype *new;
	unsigned long actual_type;
	int is_range_ram;
	int err = 0;

	BUG_ON(start >= end); /* end is exclusive */

	if (!pat_enabled) {
		/* This is identical to page table setting without PAT */
		if (new_type) {
			if (req_type == _PAGE_CACHE_WC)
				*new_type = _PAGE_CACHE_UC_MINUS;
			else
				*new_type = req_type & _PAGE_CACHE_MASK;
		}
		return 0;
	}

	/* Low ISA region is always mapped WB in page table. No need to track */
	if (x86_platform.is_untracked_pat_range(start, end)) {
		if (new_type)
			*new_type = _PAGE_CACHE_WB;
		return 0;
	}

	/*
	 * Call mtrr_lookup to get the type hint. This is an
	 * optimization for /dev/mem mmap'ers into WB memory (BIOS
	 * tools and ACPI tools). Use WB request for WB memory and use
	 * UC_MINUS otherwise.
	 */
	actual_type = pat_x_mtrr_type(start, end, req_type & _PAGE_CACHE_MASK);

	if (new_type)
		*new_type = actual_type;

	is_range_ram = pat_pagerange_is_ram(start, end);
	if (is_range_ram == 1) {

		spin_lock(&memtype_lock);
		err = reserve_ram_pages_type(start, end, req_type, new_type);
		spin_unlock(&memtype_lock);

		return err;
	} else if (is_range_ram < 0) {
		return -EINVAL;
	}

	new  = kmalloc(sizeof(struct memtype), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	new->start	= start;
	new->end	= end;
	new->type	= actual_type;

	spin_lock(&memtype_lock);

	err = rbt_memtype_check_insert(new, new_type);
	if (err) {
		printk(KERN_INFO "reserve_memtype failed 0x%Lx-0x%Lx, "
		       "track %s, req %s\n",
		       start, end, cattr_name(new->type), cattr_name(req_type));
		kfree(new);
		spin_unlock(&memtype_lock);

		return err;
	}

	spin_unlock(&memtype_lock);

	dprintk("reserve_memtype added 0x%Lx-0x%Lx, track %s, req %s, ret %s\n",
		start, end, cattr_name(new->type), cattr_name(req_type),
		new_type ? cattr_name(*new_type) : "-");

	return err;
}

int free_memtype(u64 start, u64 end)
{
	int err = -EINVAL;
	int is_range_ram;

	if (!pat_enabled)
		return 0;

	/* Low ISA region is always mapped WB. No need to track */
	if (x86_platform.is_untracked_pat_range(start, end))
		return 0;

	is_range_ram = pat_pagerange_is_ram(start, end);
	if (is_range_ram == 1) {

		spin_lock(&memtype_lock);
		err = free_ram_pages_type(start, end);
		spin_unlock(&memtype_lock);

		return err;
	} else if (is_range_ram < 0) {
		return -EINVAL;
	}

	spin_lock(&memtype_lock);
	err = rbt_memtype_erase(start, end);
	spin_unlock(&memtype_lock);

	if (err) {
		printk(KERN_INFO "%s:%d freeing invalid memtype %Lx-%Lx\n",
			current->comm, current->pid, start, end);
	}

	dprintk("free_memtype request 0x%Lx-0x%Lx\n", start, end);

	return err;
}


/**
 * lookup_memtype - Looksup the memory type for a physical address
 * @paddr: physical address of which memory type needs to be looked up
 *
 * Only to be called when PAT is enabled
 *
 * Returns _PAGE_CACHE_WB, _PAGE_CACHE_WC, _PAGE_CACHE_UC_MINUS or
 * _PAGE_CACHE_UC
 */
static unsigned long lookup_memtype(u64 paddr)
{
	int rettype = _PAGE_CACHE_WB;
	struct memtype *entry;

	if (x86_platform.is_untracked_pat_range(paddr, paddr + PAGE_SIZE))
		return rettype;

	if (pat_pagerange_is_ram(paddr, paddr + PAGE_SIZE)) {
		struct page *page;
		spin_lock(&memtype_lock);
		page = pfn_to_page(paddr >> PAGE_SHIFT);
		rettype = get_page_memtype(page);
		spin_unlock(&memtype_lock);
		/*
		 * -1 from get_page_memtype() implies RAM page is in its
		 * default state and not reserved, and hence of type WB
		 */
		if (rettype == -1)
			rettype = _PAGE_CACHE_WB;

		return rettype;
	}

	spin_lock(&memtype_lock);

	entry = rbt_memtype_lookup(paddr);
	if (entry != NULL)
		rettype = entry->type;
	else
		rettype = _PAGE_CACHE_UC_MINUS;

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
			unsigned long *type)
{
	resource_size_t size = end - start;
	unsigned long req_type = *type;
	unsigned long new_type;
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
	unsigned long flags = _PAGE_CACHE_WB;

	if (!range_is_allowed(pfn, size))
		return 0;

	if (file->f_flags & O_DSYNC)
		flags = _PAGE_CACHE_UC_MINUS;

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

	*vma_prot = __pgprot((pgprot_val(*vma_prot) & ~_PAGE_CACHE_MASK) |
			     flags);
	return 1;
}

/*
 * Change the memory type for the physial address range in kernel identity
 * mapping space if that range is a part of identity map.
 */
int kernel_map_sync_memtype(u64 base, unsigned long size, unsigned long flags)
{
	unsigned long id_sz;

	if (base >= __pa(high_memory))
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
	unsigned long want_flags = (pgprot_val(*vma_prot) & _PAGE_CACHE_MASK);
	unsigned long flags = want_flags;

	is_ram = pat_pagerange_is_ram(paddr, paddr + size);

	/*
	 * reserve_pfn_range() for RAM pages. We do not refcount to keep
	 * track of number of mappings of RAM pages. We can assert that
	 * the type requested matches the type of first page in the range.
	 */
	if (is_ram) {
		if (!pat_enabled)
			return 0;

		flags = lookup_memtype(paddr);
		if (want_flags != flags) {
			printk(KERN_WARNING
			"%s:%d map pfn RAM range req %s for %Lx-%Lx, got %s\n",
				current->comm, current->pid,
				cattr_name(want_flags),
				(unsigned long long)paddr,
				(unsigned long long)(paddr + size),
				cattr_name(flags));
			*vma_prot = __pgprot((pgprot_val(*vma_prot) &
					      (~_PAGE_CACHE_MASK)) |
					     flags);
		}
		return 0;
	}

	ret = reserve_memtype(paddr, paddr + size, want_flags, &flags);
	if (ret)
		return ret;

	if (flags != want_flags) {
		if (strict_prot ||
		    !is_new_memtype_allowed(paddr, size, want_flags, flags)) {
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
 */
int track_pfn_vma_copy(struct vm_area_struct *vma)
{
	resource_size_t paddr;
	unsigned long prot;
	unsigned long vma_size = vma->vm_end - vma->vm_start;
	pgprot_t pgprot;

	if (is_linear_pfn_mapping(vma)) {
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
 * track_pfn_vma_new is called when a _new_ pfn mapping is being established
 * for physical range indicated by pfn and size.
 *
 * prot is passed in as a parameter for the new mapping. If the vma has a
 * linear pfn mapping for the entire range reserve the entire vma range with
 * single reserve_pfn_range call.
 */
int track_pfn_vma_new(struct vm_area_struct *vma, pgprot_t *prot,
			unsigned long pfn, unsigned long size)
{
	unsigned long flags;
	resource_size_t paddr;
	unsigned long vma_size = vma->vm_end - vma->vm_start;

	if (is_linear_pfn_mapping(vma)) {
		/* reserve the whole chunk starting from vm_pgoff */
		paddr = (resource_size_t)vma->vm_pgoff << PAGE_SHIFT;
		return reserve_pfn_range(paddr, vma_size, prot, 0);
	}

	if (!pat_enabled)
		return 0;

	/* for vm_insert_pfn and friends, we set prot based on lookup */
	flags = lookup_memtype(pfn << PAGE_SHIFT);
	*prot = __pgprot((pgprot_val(vma->vm_page_prot) & (~_PAGE_CACHE_MASK)) |
			 flags);

	return 0;
}

/*
 * untrack_pfn_vma is called while unmapping a pfnmap for a region.
 * untrack can be called for a specific region indicated by pfn and size or
 * can be for the entire vma (in which case size can be zero).
 */
void untrack_pfn_vma(struct vm_area_struct *vma, unsigned long pfn,
			unsigned long size)
{
	resource_size_t paddr;
	unsigned long vma_size = vma->vm_end - vma->vm_start;

	if (is_linear_pfn_mapping(vma)) {
		/* free the whole chunk starting from vm_pgoff */
		paddr = (resource_size_t)vma->vm_pgoff << PAGE_SHIFT;
		free_pfn_range(paddr, vma_size);
		return;
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
