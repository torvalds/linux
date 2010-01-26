/*
 * arch/sh/mm/pmb.c
 *
 * Privileged Space Mapping Buffer (PMB) Support.
 *
 * Copyright (C) 2005 - 2010  Paul Mundt
 * Copyright (C) 2010  Matt Fleming
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/err.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/io.h>
#include <asm/mmu_context.h>

#define NR_PMB_ENTRIES	16

static void __pmb_unmap(struct pmb_entry *);

static struct pmb_entry pmb_entry_list[NR_PMB_ENTRIES];
static unsigned long pmb_map;

static inline unsigned long mk_pmb_entry(unsigned int entry)
{
	return (entry & PMB_E_MASK) << PMB_E_SHIFT;
}

static inline unsigned long mk_pmb_addr(unsigned int entry)
{
	return mk_pmb_entry(entry) | PMB_ADDR;
}

static inline unsigned long mk_pmb_data(unsigned int entry)
{
	return mk_pmb_entry(entry) | PMB_DATA;
}

static int pmb_alloc_entry(void)
{
	unsigned int pos;

repeat:
	pos = find_first_zero_bit(&pmb_map, NR_PMB_ENTRIES);

	if (unlikely(pos > NR_PMB_ENTRIES))
		return -ENOSPC;

	if (test_and_set_bit(pos, &pmb_map))
		goto repeat;

	return pos;
}

static struct pmb_entry *pmb_alloc(unsigned long vpn, unsigned long ppn,
				   unsigned long flags, int entry)
{
	struct pmb_entry *pmbe;
	int pos;

	if (entry == PMB_NO_ENTRY) {
		pos = pmb_alloc_entry();
		if (pos < 0)
			return ERR_PTR(pos);
	} else {
		if (test_bit(entry, &pmb_map))
			return ERR_PTR(-ENOSPC);
		pos = entry;
	}

	pmbe = &pmb_entry_list[pos];
	if (!pmbe)
		return ERR_PTR(-ENOMEM);

	pmbe->vpn	= vpn;
	pmbe->ppn	= ppn;
	pmbe->flags	= flags;
	pmbe->entry	= pos;

	return pmbe;
}

static void pmb_free(struct pmb_entry *pmbe)
{
	int pos = pmbe->entry;

	pmbe->vpn	= 0;
	pmbe->ppn	= 0;
	pmbe->flags	= 0;
	pmbe->entry	= 0;

	clear_bit(pos, &pmb_map);
}

/*
 * Must be in P2 for __set_pmb_entry()
 */
static void __set_pmb_entry(unsigned long vpn, unsigned long ppn,
			    unsigned long flags, int pos)
{
	__raw_writel(vpn | PMB_V, mk_pmb_addr(pos));

#ifdef CONFIG_CACHE_WRITETHROUGH
	/*
	 * When we are in 32-bit address extended mode, CCR.CB becomes
	 * invalid, so care must be taken to manually adjust cacheable
	 * translations.
	 */
	if (likely(flags & PMB_C))
		flags |= PMB_WT;
#endif

	__raw_writel(ppn | flags | PMB_V, mk_pmb_data(pos));
}

static void set_pmb_entry(struct pmb_entry *pmbe)
{
	jump_to_uncached();
	__set_pmb_entry(pmbe->vpn, pmbe->ppn, pmbe->flags, pmbe->entry);
	back_to_cached();
}

static void clear_pmb_entry(struct pmb_entry *pmbe)
{
	unsigned int entry = pmbe->entry;
	unsigned long addr;

	if (unlikely(entry >= NR_PMB_ENTRIES))
		return;

	jump_to_uncached();

	/* Clear V-bit */
	addr = mk_pmb_addr(entry);
	__raw_writel(__raw_readl(addr) & ~PMB_V, addr);

	addr = mk_pmb_data(entry);
	__raw_writel(__raw_readl(addr) & ~PMB_V, addr);

	back_to_cached();
}


static struct {
	unsigned long size;
	int flag;
} pmb_sizes[] = {
	{ .size	= 0x20000000, .flag = PMB_SZ_512M, },
	{ .size = 0x08000000, .flag = PMB_SZ_128M, },
	{ .size = 0x04000000, .flag = PMB_SZ_64M,  },
	{ .size = 0x01000000, .flag = PMB_SZ_16M,  },
};

long pmb_remap(unsigned long vaddr, unsigned long phys,
	       unsigned long size, unsigned long flags)
{
	struct pmb_entry *pmbp, *pmbe;
	unsigned long wanted;
	int pmb_flags, i;
	long err;

	/* Convert typical pgprot value to the PMB equivalent */
	if (flags & _PAGE_CACHABLE) {
		if (flags & _PAGE_WT)
			pmb_flags = PMB_WT;
		else
			pmb_flags = PMB_C;
	} else
		pmb_flags = PMB_WT | PMB_UB;

	pmbp = NULL;
	wanted = size;

again:
	for (i = 0; i < ARRAY_SIZE(pmb_sizes); i++) {
		if (size < pmb_sizes[i].size)
			continue;

		pmbe = pmb_alloc(vaddr, phys, pmb_flags | pmb_sizes[i].flag,
				 PMB_NO_ENTRY);
		if (IS_ERR(pmbe)) {
			err = PTR_ERR(pmbe);
			goto out;
		}

		set_pmb_entry(pmbe);

		phys	+= pmb_sizes[i].size;
		vaddr	+= pmb_sizes[i].size;
		size	-= pmb_sizes[i].size;

		/*
		 * Link adjacent entries that span multiple PMB entries
		 * for easier tear-down.
		 */
		if (likely(pmbp))
			pmbp->link = pmbe;

		pmbp = pmbe;

		/*
		 * Instead of trying smaller sizes on every iteration
		 * (even if we succeed in allocating space), try using
		 * pmb_sizes[i].size again.
		 */
		i--;
	}

	if (size >= 0x1000000)
		goto again;

	return wanted - size;

out:
	if (pmbp)
		__pmb_unmap(pmbp);

	return err;
}

void pmb_unmap(unsigned long addr)
{
	struct pmb_entry *pmbe = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(pmb_entry_list); i++) {
		if (test_bit(i, &pmb_map)) {
			pmbe = &pmb_entry_list[i];
			if (pmbe->vpn == addr)
				break;
		}
	}

	if (unlikely(!pmbe))
		return;

	__pmb_unmap(pmbe);
}

static void __pmb_unmap(struct pmb_entry *pmbe)
{
	BUG_ON(!test_bit(pmbe->entry, &pmb_map));

	do {
		struct pmb_entry *pmblink = pmbe;

		/*
		 * We may be called before this pmb_entry has been
		 * entered into the PMB table via set_pmb_entry(), but
		 * that's OK because we've allocated a unique slot for
		 * this entry in pmb_alloc() (even if we haven't filled
		 * it yet).
		 *
		 * Therefore, calling clear_pmb_entry() is safe as no
		 * other mapping can be using that slot.
		 */
		clear_pmb_entry(pmbe);

		pmbe = pmblink->link;

		pmb_free(pmblink);
	} while (pmbe);
}

#ifdef CONFIG_PMB_LEGACY
static inline unsigned int pmb_ppn_in_range(unsigned long ppn)
{
	return ppn >= __MEMORY_START && ppn < __MEMORY_START + __MEMORY_SIZE;
}

static int pmb_apply_legacy_mappings(void)
{
	unsigned int applied = 0;
	int i;

	pr_info("PMB: Preserving legacy mappings:\n");

	/*
	 * The following entries are setup by the bootloader.
	 *
	 * Entry       VPN	   PPN	    V	SZ	C	UB
	 * --------------------------------------------------------
	 *   0      0xA0000000 0x00000000   1   64MB    0       0
	 *   1      0xA4000000 0x04000000   1   16MB    0       0
	 *   2      0xA6000000 0x08000000   1   16MB    0       0
	 *   9      0x88000000 0x48000000   1  128MB    1       1
	 *  10      0x90000000 0x50000000   1  128MB    1       1
	 *  11      0x98000000 0x58000000   1  128MB    1       1
	 *  13      0xA8000000 0x48000000   1  128MB    0       0
	 *  14      0xB0000000 0x50000000   1  128MB    0       0
	 *  15      0xB8000000 0x58000000   1  128MB    0       0
	 *
	 * The only entries the we need are the ones that map the kernel
	 * at the cached and uncached addresses.
	 */
	for (i = 0; i < PMB_ENTRY_MAX; i++) {
		unsigned long addr, data;
		unsigned long addr_val, data_val;
		unsigned long ppn, vpn;

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
		if (pmb_ppn_in_range(ppn)) {
			unsigned int size;
			char *sz_str = NULL;

			size = data_val & PMB_SZ_MASK;

			sz_str = (size == PMB_SZ_16M)  ? " 16MB":
				 (size == PMB_SZ_64M)  ? " 64MB":
				 (size == PMB_SZ_128M) ? "128MB":
							 "512MB";

			pr_info("\t0x%08lx -> 0x%08lx [ %s %scached ]\n",
				vpn >> PAGE_SHIFT, ppn >> PAGE_SHIFT, sz_str,
				(data_val & PMB_C) ? "" : "un");

			applied++;
		} else {
			/*
			 * Invalidate anything out of bounds.
			 */
			__raw_writel(addr_val & ~PMB_V, addr);
			__raw_writel(data_val & ~PMB_V, data);
		}
	}

	return (applied == 0);
}
#else
static inline int pmb_apply_legacy_mappings(void)
{
	return 1;
}
#endif

int pmb_init(void)
{
	int i;
	unsigned long addr, data;
	unsigned long ret;

	jump_to_uncached();

	/*
	 * Attempt to apply the legacy boot mappings if configured. If
	 * this is successful then we simply carry on with those and
	 * don't bother establishing additional memory mappings. Dynamic
	 * device mappings through pmb_remap() can still be bolted on
	 * after this.
	 */
	ret = pmb_apply_legacy_mappings();
	if (ret == 0) {
		back_to_cached();
		return 0;
	}

	/*
	 * Sync our software copy of the PMB mappings with those in
	 * hardware. The mappings in the hardware PMB were either set up
	 * by the bootloader or very early on by the kernel.
	 */
	for (i = 0; i < PMB_ENTRY_MAX; i++) {
		struct pmb_entry *pmbe;
		unsigned long vpn, ppn, flags;

		addr = PMB_DATA + (i << PMB_E_SHIFT);
		data = __raw_readl(addr);
		if (!(data & PMB_V))
			continue;

		if (data & PMB_C) {
#if defined(CONFIG_CACHE_WRITETHROUGH)
			data |= PMB_WT;
#elif defined(CONFIG_CACHE_WRITEBACK)
			data &= ~PMB_WT;
#else
			data &= ~(PMB_C | PMB_WT);
#endif
		}
		__raw_writel(data, addr);

		ppn = data & PMB_PFN_MASK;

		flags = data & (PMB_C | PMB_WT | PMB_UB);
		flags |= data & PMB_SZ_MASK;

		addr = PMB_ADDR + (i << PMB_E_SHIFT);
		data = __raw_readl(addr);

		vpn = data & PMB_PFN_MASK;

		pmbe = pmb_alloc(vpn, ppn, flags, i);
		WARN_ON(IS_ERR(pmbe));
	}

	__raw_writel(0, PMB_IRMCR);

	/* Flush out the TLB */
	i =  __raw_readl(MMUCR);
	i |= MMUCR_TI;
	__raw_writel(i, MMUCR);

	back_to_cached();

	return 0;
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
				     sh_debugfs_root, NULL, &pmb_debugfs_fops);
	if (!dentry)
		return -ENOMEM;
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	return 0;
}
postcore_initcall(pmb_debugfs_init);

#ifdef CONFIG_PM
static int pmb_sysdev_suspend(struct sys_device *dev, pm_message_t state)
{
	static pm_message_t prev_state;
	int i;

	/* Restore the PMB after a resume from hibernation */
	if (state.event == PM_EVENT_ON &&
	    prev_state.event == PM_EVENT_FREEZE) {
		struct pmb_entry *pmbe;
		for (i = 0; i < ARRAY_SIZE(pmb_entry_list); i++) {
			if (test_bit(i, &pmb_map)) {
				pmbe = &pmb_entry_list[i];
				set_pmb_entry(pmbe);
			}
		}
	}
	prev_state = state;
	return 0;
}

static int pmb_sysdev_resume(struct sys_device *dev)
{
	return pmb_sysdev_suspend(dev, PMSG_ON);
}

static struct sysdev_driver pmb_sysdev_driver = {
	.suspend = pmb_sysdev_suspend,
	.resume = pmb_sysdev_resume,
};

static int __init pmb_sysdev_init(void)
{
	return sysdev_driver_register(&cpu_sysdev_class, &pmb_sysdev_driver);
}
subsys_initcall(pmb_sysdev_init);
#endif
