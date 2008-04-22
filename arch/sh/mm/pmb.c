/*
 * arch/sh/mm/pmb.c
 *
 * Privileged Space Mapping Buffer (PMB) Support.
 *
 * Copyright (C) 2005, 2006, 2007 Paul Mundt
 *
 * P1/P2 Section mapping definitions from map32.h, which was:
 *
 *	Copyright 2003 (c) Lineo Solutions,Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
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

static struct kmem_cache *pmb_cache;
static unsigned long pmb_map;

static struct pmb_entry pmb_init_map[] = {
	/* vpn         ppn         flags (ub/sz/c/wt) */

	/* P1 Section Mappings */
	{ 0x80000000, 0x00000000, PMB_SZ_64M  | PMB_C, },
	{ 0x84000000, 0x04000000, PMB_SZ_64M  | PMB_C, },
	{ 0x88000000, 0x08000000, PMB_SZ_128M | PMB_C, },
	{ 0x90000000, 0x10000000, PMB_SZ_64M  | PMB_C, },
	{ 0x94000000, 0x14000000, PMB_SZ_64M  | PMB_C, },
	{ 0x98000000, 0x18000000, PMB_SZ_64M  | PMB_C, },

	/* P2 Section Mappings */
	{ 0xa0000000, 0x00000000, PMB_UB | PMB_SZ_64M  | PMB_WT, },
	{ 0xa4000000, 0x04000000, PMB_UB | PMB_SZ_64M  | PMB_WT, },
	{ 0xa8000000, 0x08000000, PMB_UB | PMB_SZ_128M | PMB_WT, },
	{ 0xb0000000, 0x10000000, PMB_UB | PMB_SZ_64M  | PMB_WT, },
	{ 0xb4000000, 0x14000000, PMB_UB | PMB_SZ_64M  | PMB_WT, },
	{ 0xb8000000, 0x18000000, PMB_UB | PMB_SZ_64M  | PMB_WT, },
};

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

static DEFINE_SPINLOCK(pmb_list_lock);
static struct pmb_entry *pmb_list;

static inline void pmb_list_add(struct pmb_entry *pmbe)
{
	struct pmb_entry **p, *tmp;

	p = &pmb_list;
	while ((tmp = *p) != NULL)
		p = &tmp->next;

	pmbe->next = tmp;
	*p = pmbe;
}

static inline void pmb_list_del(struct pmb_entry *pmbe)
{
	struct pmb_entry **p, *tmp;

	for (p = &pmb_list; (tmp = *p); p = &tmp->next)
		if (tmp == pmbe) {
			*p = tmp->next;
			return;
		}
}

struct pmb_entry *pmb_alloc(unsigned long vpn, unsigned long ppn,
			    unsigned long flags)
{
	struct pmb_entry *pmbe;

	pmbe = kmem_cache_alloc(pmb_cache, GFP_KERNEL);
	if (!pmbe)
		return ERR_PTR(-ENOMEM);

	pmbe->vpn	= vpn;
	pmbe->ppn	= ppn;
	pmbe->flags	= flags;

	spin_lock_irq(&pmb_list_lock);
	pmb_list_add(pmbe);
	spin_unlock_irq(&pmb_list_lock);

	return pmbe;
}

void pmb_free(struct pmb_entry *pmbe)
{
	spin_lock_irq(&pmb_list_lock);
	pmb_list_del(pmbe);
	spin_unlock_irq(&pmb_list_lock);

	kmem_cache_free(pmb_cache, pmbe);
}

/*
 * Must be in P2 for __set_pmb_entry()
 */
int __set_pmb_entry(unsigned long vpn, unsigned long ppn,
		    unsigned long flags, int *entry)
{
	unsigned int pos = *entry;

	if (unlikely(pos == PMB_NO_ENTRY))
		pos = find_first_zero_bit(&pmb_map, NR_PMB_ENTRIES);

repeat:
	if (unlikely(pos > NR_PMB_ENTRIES))
		return -ENOSPC;

	if (test_and_set_bit(pos, &pmb_map)) {
		pos = find_first_zero_bit(&pmb_map, NR_PMB_ENTRIES);
		goto repeat;
	}

	ctrl_outl(vpn | PMB_V, mk_pmb_addr(pos));

#ifdef CONFIG_CACHE_WRITETHROUGH
	/*
	 * When we are in 32-bit address extended mode, CCR.CB becomes
	 * invalid, so care must be taken to manually adjust cacheable
	 * translations.
	 */
	if (likely(flags & PMB_C))
		flags |= PMB_WT;
#endif

	ctrl_outl(ppn | flags | PMB_V, mk_pmb_data(pos));

	*entry = pos;

	return 0;
}

int __uses_jump_to_uncached set_pmb_entry(struct pmb_entry *pmbe)
{
	int ret;

	jump_to_uncached();
	ret = __set_pmb_entry(pmbe->vpn, pmbe->ppn, pmbe->flags, &pmbe->entry);
	back_to_cached();

	return ret;
}

void __uses_jump_to_uncached clear_pmb_entry(struct pmb_entry *pmbe)
{
	unsigned int entry = pmbe->entry;
	unsigned long addr;

	/*
	 * Don't allow clearing of wired init entries, P1 or P2 access
	 * without a corresponding mapping in the PMB will lead to reset
	 * by the TLB.
	 */
	if (unlikely(entry < ARRAY_SIZE(pmb_init_map) ||
		     entry >= NR_PMB_ENTRIES))
		return;

	jump_to_uncached();

	/* Clear V-bit */
	addr = mk_pmb_addr(entry);
	ctrl_outl(ctrl_inl(addr) & ~PMB_V, addr);

	addr = mk_pmb_data(entry);
	ctrl_outl(ctrl_inl(addr) & ~PMB_V, addr);

	back_to_cached();

	clear_bit(entry, &pmb_map);
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
	struct pmb_entry *pmbp;
	unsigned long wanted;
	int pmb_flags, i;

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
		struct pmb_entry *pmbe;
		int ret;

		if (size < pmb_sizes[i].size)
			continue;

		pmbe = pmb_alloc(vaddr, phys, pmb_flags | pmb_sizes[i].flag);
		if (IS_ERR(pmbe))
			return PTR_ERR(pmbe);

		ret = set_pmb_entry(pmbe);
		if (ret != 0) {
			pmb_free(pmbe);
			return -EBUSY;
		}

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
	}

	if (size >= 0x1000000)
		goto again;

	return wanted - size;
}

void pmb_unmap(unsigned long addr)
{
	struct pmb_entry **p, *pmbe;

	for (p = &pmb_list; (pmbe = *p); p = &pmbe->next)
		if (pmbe->vpn == addr)
			break;

	if (unlikely(!pmbe))
		return;

	WARN_ON(!test_bit(pmbe->entry, &pmb_map));

	do {
		struct pmb_entry *pmblink = pmbe;

		clear_pmb_entry(pmbe);
		pmbe = pmblink->link;

		pmb_free(pmblink);
	} while (pmbe);
}

static void pmb_cache_ctor(struct kmem_cache *cachep, void *pmb)
{
	struct pmb_entry *pmbe = pmb;

	memset(pmb, 0, sizeof(struct pmb_entry));

	pmbe->entry = PMB_NO_ENTRY;
}

static int __uses_jump_to_uncached pmb_init(void)
{
	unsigned int nr_entries = ARRAY_SIZE(pmb_init_map);
	unsigned int entry, i;

	BUG_ON(unlikely(nr_entries >= NR_PMB_ENTRIES));

	pmb_cache = kmem_cache_create("pmb", sizeof(struct pmb_entry), 0,
				      SLAB_PANIC, pmb_cache_ctor);

	jump_to_uncached();

	/*
	 * Ordering is important, P2 must be mapped in the PMB before we
	 * can set PMB.SE, and P1 must be mapped before we jump back to
	 * P1 space.
	 */
	for (entry = 0; entry < nr_entries; entry++) {
		struct pmb_entry *pmbe = pmb_init_map + entry;

		__set_pmb_entry(pmbe->vpn, pmbe->ppn, pmbe->flags, &entry);
	}

	ctrl_outl(0, PMB_IRMCR);

	/* PMB.SE and UB[7] */
	ctrl_outl((1 << 31) | (1 << 7), PMB_PASCR);

	/* Flush out the TLB */
	i =  ctrl_inl(MMUCR);
	i |= MMUCR_TI;
	ctrl_outl(i, MMUCR);

	back_to_cached();

	return 0;
}
arch_initcall(pmb_init);

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

		addr = ctrl_inl(mk_pmb_addr(i));
		data = ctrl_inl(mk_pmb_data(i));

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
	.release	= seq_release,
};

static int __init pmb_debugfs_init(void)
{
	struct dentry *dentry;

	dentry = debugfs_create_file("pmb", S_IFREG | S_IRUGO,
				     sh_debugfs_root, NULL, &pmb_debugfs_fops);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	return 0;
}
postcore_initcall(pmb_debugfs_init);
