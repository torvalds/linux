/*
 * arch/sh/mm/pmb.c
 *
 * Privileged Space Mapping Buffer (PMB) Support.
 *
 * Copyright (C) 2005 - 2011  Paul Mundt
 * Copyright (C) 2010  Matt Fleming
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/syscore_ops.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>
#include <asm/sizes.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>

struct pmb_entry;

struct pmb_entry {
	unsigned long vpn;
	unsigned long ppn;
	unsigned long flags;
	unsigned long size;

	raw_spinlock_t lock;

	/*
	 * 0 .. NR_PMB_ENTRIES for specific entry selection, or
	 * PMB_NO_ENTRY to search for a free one
	 */
	int entry;

	/* Adjacent entry link for contiguous multi-entry mappings */
	struct pmb_entry *link;
};

static struct {
	unsigned long size;
	int flag;
} pmb_sizes[] = {
	{ .size	= SZ_512M, .flag = PMB_SZ_512M, },
	{ .size = SZ_128M, .flag = PMB_SZ_128M, },
	{ .size = SZ_64M,  .flag = PMB_SZ_64M,  },
	{ .size = SZ_16M,  .flag = PMB_SZ_16M,  },
};

static void pmb_unmap_entry(struct pmb_entry *, int depth);

static DEFINE_RWLOCK(pmb_rwlock);
static struct pmb_entry pmb_entry_list[NR_PMB_ENTRIES];
static DECLARE_BITMAP(pmb_map, NR_PMB_ENTRIES);

static unsigned int pmb_iomapping_enabled;

static __always_inline unsigned long mk_pmb_entry(unsigned int entry)
{
	return (entry & PMB_E_MASK) << PMB_E_SHIFT;
}

static __always_inline unsigned long mk_pmb_addr(unsigned int entry)
{
	return mk_pmb_entry(entry) | PMB_ADDR;
}

static __always_inline unsigned long mk_pmb_data(unsigned int entry)
{
	return mk_pmb_entry(entry) | PMB_DATA;
}

static __always_inline unsigned int pmb_ppn_in_range(unsigned long ppn)
{
	return ppn >= __pa(memory_start) && ppn < __pa(memory_end);
}

/*
 * Ensure that the PMB entries match our cache configuration.
 *
 * When we are in 32-bit address extended mode, CCR.CB becomes
 * invalid, so care must be taken to manually adjust cacheable
 * translations.
 */
static __always_inline unsigned long pmb_cache_flags(void)
{
	unsigned long flags = 0;

#if defined(CONFIG_CACHE_OFF)
	flags |= PMB_WT | PMB_UB;
#elif defined(CONFIG_CACHE_WRITETHROUGH)
	flags |= PMB_C | PMB_WT | PMB_UB;
#elif defined(CONFIG_CACHE_WRITEBACK)
	flags |= PMB_C;
#endif

	return flags;
}

/*
 * Convert typical pgprot value to the PMB equivalent
 */
static inline unsigned long pgprot_to_pmb_flags(pgprot_t prot)
{
	unsigned long pmb_flags = 0;
	u64 flags = pgprot_val(prot);

	if (flags & _PAGE_CACHABLE)
		pmb_flags |= PMB_C;
	if (flags & _PAGE_WT)
		pmb_flags |= PMB_WT | PMB_UB;

	return pmb_flags;
}

static inline bool pmb_can_merge(struct pmb_entry *a, struct pmb_entry *b)
{
	return (b->vpn == (a->vpn + a->size)) &&
	       (b->ppn == (a->ppn + a->size)) &&
	       (b->flags == a->flags);
}

static bool pmb_mapping_exists(unsigned long vaddr, phys_addr_t phys,
			       unsigned long size)
{
	int i;

	read_lock(&pmb_rwlock);

	for (i = 0; i < ARRAY_SIZE(pmb_entry_list); i++) {
		struct pmb_entry *pmbe, *iter;
		unsigned long span;

		if (!test_bit(i, pmb_map))
			continue;

		pmbe = &pmb_entry_list[i];

		/*
		 * See if VPN and PPN are bounded by an existing mapping.
		 */
		if ((vaddr < pmbe->vpn) || (vaddr >= (pmbe->vpn + pmbe->size)))
			continue;
		if ((phys < pmbe->ppn) || (phys >= (pmbe->ppn + pmbe->size)))
			continue;

		/*
		 * Now see if we're in range of a simple mapping.
		 */
		if (size <= pmbe->size) {
			read_unlock(&pmb_rwlock);
			return true;
		}

		span = pmbe->size;

		/*
		 * Finally for sizes that involve compound mappings, walk
		 * the chain.
		 */
		for (iter = pmbe->link; iter; iter = iter->link)
			span += iter->size;

		/*
		 * Nothing else to do if the range requirements are met.
		 */
		if (size <= span) {
			read_unlock(&pmb_rwlock);
			return true;
		}
	}

	read_unlock(&pmb_rwlock);
	return false;
}

static bool pmb_size_valid(unsigned long size)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pmb_sizes); i++)
		if (pmb_sizes[i].size == size)
			return true;

	return false;
}

static inline bool pmb_addr_valid(unsigned long addr, unsigned long size)
{
	return (addr >= P1SEG && (addr + size - 1) < P3SEG);
}

static inline bool pmb_prot_valid(pgprot_t prot)
{
	return (pgprot_val(prot) & _PAGE_USER) == 0;
}

static int pmb_size_to_flags(unsigned long size)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pmb_sizes); i++)
		if (pmb_sizes[i].size == size)
			return pmb_sizes[i].flag;

	return 0;
}

static int pmb_alloc_entry(void)
{
	int pos;

	pos = find_first_zero_bit(pmb_map, NR_PMB_ENTRIES);
	if (pos >= 0 && pos < NR_PMB_ENTRIES)
		__set_bit(pos, pmb_map);
	else
		pos = -ENOSPC;

	return pos;
}

static struct pmb_entry *pmb_alloc(unsigned long vpn, unsigned long ppn,
				   unsigned long flags, int entry)
{
	struct pmb_entry *pmbe;
	unsigned long irqflags;
	void *ret = NULL;
	int pos;

	write_lock_irqsave(&pmb_rwlock, irqflags);

	if (entry == PMB_NO_ENTRY) {
		pos = pmb_alloc_entry();
		if (unlikely(pos < 0)) {
			ret = ERR_PTR(pos);
			goto out;
		}
	} else {
		if (__test_and_set_bit(entry, pmb_map)) {
			ret = ERR_PTR(-ENOSPC);
			goto out;
		}

		pos = entry;
	}

	write_unlock_irqrestore(&pmb_rwlock, irqflags);

	pmbe = &pmb_entry_list[pos];

	memset(pmbe, 0, sizeof(struct pmb_entry));

	raw_spin_lock_init(&pmbe->lock);

	pmbe->vpn	= vpn;
	pmbe->ppn	= ppn;
	pmbe->flags	= flags;
	pmbe->entry	= pos;

	return pmbe;

out:
	write_unlock_irqrestore(&pmb_rwlock, irqflags);
	return ret;
}

static void pmb_free(struct pmb_entry *pmbe)
{
	__clear_bit(pmbe->entry, pmb_map);

	pmbe->entry	= PMB_NO_ENTRY;
	pmbe->link	= NULL;
}

/*
 * Must be run uncached.
 */
static void __set_pmb_entry(struct pmb_entry *pmbe)
{
	unsigned long addr, data;

	addr = mk_pmb_addr(pmbe->entry);
	data = mk_pmb_data(pmbe->entry);

	jump_to_uncached();

	/* Set V-bit */
	__raw_writel(pmbe->vpn | PMB_V, addr);
	__raw_writel(pmbe->ppn | pmbe->flags | PMB_V, data);

	back_to_cached();
}

static void __clear_pmb_entry(struct pmb_entry *pmbe)
{
	unsigned long addr, data;
	unsigned long addr_val, data_val;

	addr = mk_pmb_addr(pmbe->entry);
	data = mk_pmb_data(pmbe->entry);

	addr_val = __raw_readl(addr);
	data_val = __raw_readl(data);

	/* Clear V-bit */
	writel_uncached(addr_val & ~PMB_V, addr);
	writel_uncached(data_val & ~PMB_V, data);
}

#ifdef CONFIG_PM
static void set_pmb_entry(struct pmb_entry *pmbe)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&pmbe->lock, flags);
	__set_pmb_entry(pmbe);
	raw_spin_unlock_irqrestore(&pmbe->lock, flags);
}
#endif /* CONFIG_PM */

int pmb_bolt_mapping(unsigned long vaddr, phys_addr_t phys,
		     unsigned long size, pgprot_t prot)
{
	struct pmb_entry *pmbp, *pmbe;
	unsigned long orig_addr, orig_size;
	unsigned long flags, pmb_flags;
	int i, mapped;

	if (size < SZ_16M)
		return -EINVAL;
	if (!pmb_addr_valid(vaddr, size))
		return -EFAULT;
	if (pmb_mapping_exists(vaddr, phys, size))
		return 0;

	orig_addr = vaddr;
	orig_size = size;

	flush_tlb_kernel_range(vaddr, vaddr + size);

	pmb_flags = pgprot_to_pmb_flags(prot);
	pmbp = NULL;

	do {
		for (i = mapped = 0; i < ARRAY_SIZE(pmb_sizes); i++) {
			if (size < pmb_sizes[i].size)
				continue;

			pmbe = pmb_alloc(vaddr, phys, pmb_flags |
					 pmb_sizes[i].flag, PMB_NO_ENTRY);
			if (IS_ERR(pmbe)) {
				pmb_unmap_entry(pmbp, mapped);
				return PTR_ERR(pmbe);
			}

			raw_spin_lock_irqsave(&pmbe->lock, flags);

			pmbe->size = pmb_sizes[i].size;

			__set_pmb_entry(pmbe);

			phys	+= pmbe->size;
			vaddr	+= pmbe->size;
			size	-= pmbe->size;

			/*
			 * Link adjacent entries that span multiple PMB
			 * entries for easier tear-down.
			 */
			if (likely(pmbp)) {
				raw_spin_lock_nested(&pmbp->lock,
						     SINGLE_DEPTH_NESTING);
				pmbp->link = pmbe;
				raw_spin_unlock(&pmbp->lock);
			}

			pmbp = pmbe;

			/*
			 * Instead of trying smaller sizes on every
			 * iteration (even if we succeed in allocating
			 * space), try using pmb_sizes[i].size again.
			 */
			i--;
			mapped++;

			raw_spin_unlock_irqrestore(&pmbe->lock, flags);
		}
	} while (size >= SZ_16M);

	flush_cache_vmap(orig_addr, orig_addr + orig_size);

	return 0;
}

void __iomem *pmb_remap_caller(phys_addr_t phys, unsigned long size,
			       pgprot_t prot, void *caller)
{
	unsigned long vaddr;
	phys_addr_t offset, last_addr;
	phys_addr_t align_mask;
	unsigned long aligned;
	struct vm_struct *area;
	int i, ret;

	if (!pmb_iomapping_enabled)
		return NULL;

	/*
	 * Small mappings need to go through the TLB.
	 */
	if (size < SZ_16M)
		return ERR_PTR(-EINVAL);
	if (!pmb_prot_valid(prot))
		return ERR_PTR(-EINVAL);

	for (i = 0; i < ARRAY_SIZE(pmb_sizes); i++)
		if (size >= pmb_sizes[i].size)
			break;

	last_addr = phys + size;
	align_mask = ~(pmb_sizes[i].size - 1);
	offset = phys & ~align_mask;
	phys &= align_mask;
	aligned = ALIGN(last_addr, pmb_sizes[i].size) - phys;

	/*
	 * XXX: This should really start from uncached_end, but this
	 * causes the MMU to reset, so for now we restrict it to the
	 * 0xb000...0xc000 range.
	 */
	area = __get_vm_area_caller(aligned, VM_IOREMAP, 0xb0000000,
				    P3SEG, caller);
	if (!area)
		return NULL;

	area->phys_addr = phys;
	vaddr = (unsigned long)area->addr;

	ret = pmb_bolt_mapping(vaddr, phys, size, prot);
	if (unlikely(ret != 0))
		return ERR_PTR(ret);

	return (void __iomem *)(offset + (char *)vaddr);
}

int pmb_unmap(void __iomem *addr)
{
	struct pmb_entry *pmbe = NULL;
	unsigned long vaddr = (unsigned long __force)addr;
	int i, found = 0;

	read_lock(&pmb_rwlock);

	for (i = 0; i < ARRAY_SIZE(pmb_entry_list); i++) {
		if (test_bit(i, pmb_map)) {
			pmbe = &pmb_entry_list[i];
			if (pmbe->vpn == vaddr) {
				found = 1;
				break;
			}
		}
	}

	read_unlock(&pmb_rwlock);

	if (found) {
		pmb_unmap_entry(pmbe, NR_PMB_ENTRIES);
		return 0;
	}

	return -EINVAL;
}

static void __pmb_unmap_entry(struct pmb_entry *pmbe, int depth)
{
	do {
		struct pmb_entry *pmblink = pmbe;

		/*
		 * We may be called before this pmb_entry has been
		 * entered into the PMB table via set_pmb_entry(), but
		 * that's OK because we've allocated a unique slot for
		 * this entry in pmb_alloc() (even if we haven't filled
		 * it yet).
		 *
		 * Therefore, calling __clear_pmb_entry() is safe as no
		 * other mapping can be using that slot.
		 */
		__clear_pmb_entry(pmbe);

		flush_cache_vunmap(pmbe->vpn, pmbe->vpn + pmbe->size);

		pmbe = pmblink->link;

		pmb_free(pmblink);
	} while (pmbe && --depth);
}

static void pmb_unmap_entry(struct pmb_entry *pmbe, int depth)
{
	unsigned long flags;

	if (unlikely(!pmbe))
		return;

	write_lock_irqsave(&pmb_rwlock, flags);
	__pmb_unmap_entry(pmbe, depth);
	write_unlock_irqrestore(&pmb_rwlock, flags);
}

static void __init pmb_notify(void)
{
	int i;

	pr_info("PMB: boot mappings:\n");

	read_lock(&pmb_rwlock);

	for (i = 0; i < ARRAY_SIZE(pmb_entry_list); i++) {
		struct pmb_entry *pmbe;

		if (!test_bit(i, pmb_map))
			continue;

		pmbe = &pmb_entry_list[i];

		pr_info("       0x%08lx -> 0x%08lx [ %4ldMB %2scached ]\n",
			pmbe->vpn >> PAGE_SHIFT, pmbe->ppn >> PAGE_SHIFT,
			pmbe->size >> 20, (pmbe->flags & PMB_C) ? "" : "un");
	}

	read_unlock(&pmb_rwlock);
}

/*
 * Sync our software copy of the PMB mappings with those in hardware. The
 * mappings in the hardware PMB were either set up by the bootloader or
 * very early on by the kernel.
 */
static void __init pmb_synchronize(void)
{
	struct pmb_entry *pmbp = NULL;
	int i, j;

	/*
	 * Run through the initial boot mappings, log the established
	 * ones, and blow away anything that falls outside of the valid
	 * PPN range. Specifically, we only care about existing mappings
	 * that impact the cached/uncached sections.
	 *
	 * Note that touching these can be a bit of a minefield; the boot
	 * loader can establish multi-page mappings with the same caching
	 * attributes, so we need to ensure that we aren't modifying a
	 * mapping that we're presently executing from, or may execute
	 * from in the case of straddling page boundaries.
	 *
	 * In the future we will have to tidy up after the boot loader by
	 * jumping between the cached and uncached mappings and tearing
	 * down alternating mappings while executing from the other.
	 */
	for (i = 0; i < NR_PMB_ENTRIES; i++) {
		unsigned long addr, data;
		unsigned long addr_val, data_val;
		unsigned long ppn, vpn, flags;
		unsigned long irqflags;
		unsigned int size;
		struct pmb_entry *pmbe;

		addr = mk_pmb_addr(i);
		data = mk_pmb_data(i);

		addr_val = __raw_readl(addr);
		data_val = __raw_readl(data);

		/*
		 * Skip over any bogus entries
		 */
		if (!(data_val & PMB_V) || !(addr_val & PMB_V))
			continue;

		ppn = data_val & PMB_PFN_MASK;
		vpn = addr_val & PMB_PFN_MASK;

		/*
		 * Only preserve in-range mappings.
		 */
		if (!pmb_ppn_in_range(ppn)) {
			/*
			 * Invalidate anything out of bounds.
			 */
			writel_uncached(addr_val & ~PMB_V, addr);
			writel_uncached(data_val & ~PMB_V, data);
			continue;
		}

		/*
		 * Update the caching attributes if necessary
		 */
		if (data_val & PMB_C) {
			data_val &= ~PMB_CACHE_MASK;
			data_val |= pmb_cache_flags();

			writel_uncached(data_val, data);
		}

		size = data_val & PMB_SZ_MASK;
		flags = size | (data_val & PMB_CACHE_MASK);

		pmbe = pmb_alloc(vpn, ppn, flags, i);
		if (IS_ERR(pmbe)) {
			WARN_ON_ONCE(1);
			continue;
		}

		raw_spin_lock_irqsave(&pmbe->lock, irqflags);

		for (j = 0; j < ARRAY_SIZE(pmb_sizes); j++)
			if (pmb_sizes[j].flag == size)
				pmbe->size = pmb_sizes[j].size;

		if (pmbp) {
			raw_spin_lock_nested(&pmbp->lock, SINGLE_DEPTH_NESTING);
			/*
			 * Compare the previous entry against the current one to
			 * see if the entries span a contiguous mapping. If so,
			 * setup the entry links accordingly. Compound mappings
			 * are later coalesced.
			 */
			if (pmb_can_merge(pmbp, pmbe))
				pmbp->link = pmbe;
			raw_spin_unlock(&pmbp->lock);
		}

		pmbp = pmbe;

		raw_spin_unlock_irqrestore(&pmbe->lock, irqflags);
	}
}

static void __init pmb_merge(struct pmb_entry *head)
{
	unsigned long span, newsize;
	struct pmb_entry *tail;
	int i = 1, depth = 0;

	span = newsize = head->size;

	tail = head->link;
	while (tail) {
		span += tail->size;

		if (pmb_size_valid(span)) {
			newsize = span;
			depth = i;
		}

		/* This is the end of the line.. */
		if (!tail->link)
			break;

		tail = tail->link;
		i++;
	}

	/*
	 * The merged page size must be valid.
	 */
	if (!depth || !pmb_size_valid(newsize))
		return;

	head->flags &= ~PMB_SZ_MASK;
	head->flags |= pmb_size_to_flags(newsize);

	head->size = newsize;

	__pmb_unmap_entry(head->link, depth);
	__set_pmb_entry(head);
}

static void __init pmb_coalesce(void)
{
	unsigned long flags;
	int i;

	write_lock_irqsave(&pmb_rwlock, flags);

	for (i = 0; i < ARRAY_SIZE(pmb_entry_list); i++) {
		struct pmb_entry *pmbe;

		if (!test_bit(i, pmb_map))
			continue;

		pmbe = &pmb_entry_list[i];

		/*
		 * We're only interested in compound mappings
		 */
		if (!pmbe->link)
			continue;

		/*
		 * Nothing to do if it already uses the largest possible
		 * page size.
		 */
		if (pmbe->size == SZ_512M)
			continue;

		pmb_merge(pmbe);
	}

	write_unlock_irqrestore(&pmb_rwlock, flags);
}

#ifdef CONFIG_UNCACHED_MAPPING
static void __init pmb_resize(void)
{
	int i;

	/*
	 * If the uncached mapping was constructed by the kernel, it will
	 * already be a reasonable size.
	 */
	if (uncached_size == SZ_16M)
		return;

	read_lock(&pmb_rwlock);

	for (i = 0; i < ARRAY_SIZE(pmb_entry_list); i++) {
		struct pmb_entry *pmbe;
		unsigned long flags;

		if (!test_bit(i, pmb_map))
			continue;

		pmbe = &pmb_entry_list[i];

		if (pmbe->vpn != uncached_start)
			continue;

		/*
		 * Found it, now resize it.
		 */
		raw_spin_lock_irqsave(&pmbe->lock, flags);

		pmbe->size = SZ_16M;
		pmbe->flags &= ~PMB_SZ_MASK;
		pmbe->flags |= pmb_size_to_flags(pmbe->size);

		uncached_resize(pmbe->size);

		__set_pmb_entry(pmbe);

		raw_spin_unlock_irqrestore(&pmbe->lock, flags);
	}

	read_unlock(&pmb_rwlock);
}
#endif

static int __init early_pmb(char *p)
{
	if (!p)
		return 0;

	if (strstr(p, "iomap"))
		pmb_iomapping_enabled = 1;

	return 0;
}
early_param("pmb", early_pmb);

void __init pmb_init(void)
{
	/* Synchronize software state */
	pmb_synchronize();

	/* Attempt to combine compound mappings */
	pmb_coalesce();

#ifdef CONFIG_UNCACHED_MAPPING
	/* Resize initial mappings, if necessary */
	pmb_resize();
#endif

	/* Log them */
	pmb_notify();

	writel_uncached(0, PMB_IRMCR);

	/* Flush out the TLB */
	local_flush_tlb_all();
	ctrl_barrier();
}

bool __in_29bit_mode(void)
{
        return (__raw_readl(PMB_PASCR) & PASCR_SE) == 0;
}

static int pmb_seq_show(struct seq_file *file, void *iter)
{
	int i;

	seq_printf(file, "V: Valid, C: Cacheable, WT: Write-Through\n"
			 "CB: Copy-Back, B: Buffered, UB: Unbuffered\n");
	seq_printf(file, "ety   vpn  ppn  size   flags\n");

	for (i = 0; i < NR_PMB_ENTRIES; i++) {
		unsigned long addr, data;
		unsigned int size;
		char *sz_str = NULL;

		addr = __raw_readl(mk_pmb_addr(i));
		data = __raw_readl(mk_pmb_data(i));

		size = data & PMB_SZ_MASK;
		sz_str = (size == PMB_SZ_16M)  ? " 16MB":
			 (size == PMB_SZ_64M)  ? " 64MB":
			 (size == PMB_SZ_128M) ? "128MB":
					         "512MB";

		/* 02: V 0x88 0x08 128MB C CB  B */
		seq_printf(file, "%02d: %c 0x%02lx 0x%02lx %s %c %s %s\n",
			   i, ((addr & PMB_V) && (data & PMB_V)) ? 'V' : ' ',
			   (addr >> 24) & 0xff, (data >> 24) & 0xff,
			   sz_str, (data & PMB_C) ? 'C' : ' ',
			   (data & PMB_WT) ? "WT" : "CB",
			   (data & PMB_UB) ? "UB" : " B");
	}

	return 0;
}

static int pmb_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmb_seq_show, NULL);
}

static const struct file_operations pmb_debugfs_fops = {
	.owner		= THIS_MODULE,
	.open		= pmb_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init pmb_debugfs_init(void)
{
	struct dentry *dentry;

	dentry = debugfs_create_file("pmb", S_IFREG | S_IRUGO,
				     arch_debugfs_dir, NULL, &pmb_debugfs_fops);
	if (!dentry)
		return -ENOMEM;

	return 0;
}
subsys_initcall(pmb_debugfs_init);

#ifdef CONFIG_PM
static void pmb_syscore_resume(void)
{
	struct pmb_entry *pmbe;
	int i;

	read_lock(&pmb_rwlock);

	for (i = 0; i < ARRAY_SIZE(pmb_entry_list); i++) {
		if (test_bit(i, pmb_map)) {
			pmbe = &pmb_entry_list[i];
			set_pmb_entry(pmbe);
		}
	}

	read_unlock(&pmb_rwlock);
}

static struct syscore_ops pmb_syscore_ops = {
	.resume = pmb_syscore_resume,
};

static int __init pmb_sysdev_init(void)
{
	register_syscore_ops(&pmb_syscore_ops);
	return 0;
}
subsys_initcall(pmb_sysdev_init);
#endif
