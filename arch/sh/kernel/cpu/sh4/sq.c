/*
 * arch/sh/kernel/cpu/sq.c
 *
 * General management API for SH-4 integrated Store Queues
 *
 * Copyright (C) 2001, 2002, 2003, 2004  Paul Mundt
 * Copyright (C) 2001, 2002  M. R. Brown
 *
 * Some of this code has been adopted directly from the old arch/sh/mm/sq.c
 * hack that was part of the LinuxDC project. For all intents and purposes,
 * this is a completely new interface that really doesn't have much in common
 * with the old zone-based approach at all. In fact, it's only listed here for
 * general completeness.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>

#include <asm/io.h>
#include <asm/page.h>
#include <asm/mmu_context.h>
#include <asm/cpu/sq.h>

static LIST_HEAD(sq_mapping_list);
static DEFINE_SPINLOCK(sq_mapping_lock);

/**
 * sq_flush - Flush (prefetch) the store queue cache
 * @addr: the store queue address to flush
 *
 * Executes a prefetch instruction on the specified store queue cache,
 * so that the cached data is written to physical memory.
 */
inline void sq_flush(void *addr)
{
	__asm__ __volatile__ ("pref @%0" : : "r" (addr) : "memory");
}

/**
 * sq_flush_range - Flush (prefetch) a specific SQ range
 * @start: the store queue address to start flushing from
 * @len: the length to flush
 *
 * Flushes the store queue cache from @start to @start + @len in a
 * linear fashion.
 */
void sq_flush_range(unsigned long start, unsigned int len)
{
	volatile unsigned long *sq = (unsigned long *)start;
	unsigned long dummy;

	/* Flush the queues */
	for (len >>= 5; len--; sq += 8)
		sq_flush((void *)sq);

	/* Wait for completion */
	dummy = ctrl_inl(P4SEG_STORE_QUE);

	ctrl_outl(0, P4SEG_STORE_QUE + 0);
	ctrl_outl(0, P4SEG_STORE_QUE + 8);
}

static struct sq_mapping *__sq_alloc_mapping(unsigned long virt, unsigned long phys, unsigned long size, const char *name)
{
	struct sq_mapping *map;

	if (virt + size > SQ_ADDRMAX)
		return ERR_PTR(-ENOSPC);

	map = kmalloc(sizeof(struct sq_mapping), GFP_KERNEL);
	if (!map)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&map->list);

	map->sq_addr	= virt;
	map->addr	= phys;
	map->size	= size + 1;
	map->name	= name;

	list_add(&map->list, &sq_mapping_list);

	return map;
}

static unsigned long __sq_get_next_addr(void)
{
	if (!list_empty(&sq_mapping_list)) {
		struct list_head *pos, *tmp;

		/*
		 * Read one off the list head, as it will have the highest
		 * mapped allocation. Set the next one up right above it.
		 *
		 * This is somewhat sub-optimal, as we don't look at
		 * gaps between allocations or anything lower then the
		 * highest-level allocation.
		 *
		 * However, in the interest of performance and the general
		 * lack of desire to do constant list rebalancing, we don't
		 * worry about it.
		 */
		list_for_each_safe(pos, tmp, &sq_mapping_list) {
			struct sq_mapping *entry;

			entry = list_entry(pos, typeof(*entry), list);

			return entry->sq_addr + entry->size;
		}
	}

	return P4SEG_STORE_QUE;
}

/**
 * __sq_remap - Perform a translation from the SQ to a phys addr
 * @map: sq mapping containing phys and store queue addresses.
 *
 * Maps the store queue address specified in the mapping to the physical
 * address specified in the mapping.
 */
static struct sq_mapping *__sq_remap(struct sq_mapping *map)
{
	unsigned long flags, pteh, ptel;
	struct vm_struct *vma;
	pgprot_t pgprot;

	/*
	 * Without an MMU (or with it turned off), this is much more
	 * straightforward, as we can just load up each queue's QACR with
	 * the physical address appropriately masked.
	 */

	ctrl_outl(((map->addr >> 26) << 2) & 0x1c, SQ_QACR0);
	ctrl_outl(((map->addr >> 26) << 2) & 0x1c, SQ_QACR1);

#ifdef CONFIG_MMU
	/*
	 * With an MMU on the other hand, things are slightly more involved.
	 * Namely, we have to have a direct mapping between the SQ addr and
	 * the associated physical address in the UTLB by way of setting up
	 * a virt<->phys translation by hand. We do this by simply specifying
	 * the SQ addr in UTLB.VPN and the associated physical address in
	 * UTLB.PPN.
	 *
	 * Notably, even though this is a special case translation, and some
	 * of the configuration bits are meaningless, we're still required
	 * to have a valid ASID context in PTEH.
	 *
	 * We could also probably get by without explicitly setting PTEA, but
	 * we do it here just for good measure.
	 */
	spin_lock_irqsave(&sq_mapping_lock, flags);

	pteh = map->sq_addr;
	ctrl_outl((pteh & MMU_VPN_MASK) | get_asid(), MMU_PTEH);

	ptel = map->addr & PAGE_MASK;
	ctrl_outl(((ptel >> 28) & 0xe) | (ptel & 0x1), MMU_PTEA);

	pgprot = pgprot_noncached(PAGE_KERNEL);

	ptel &= _PAGE_FLAGS_HARDWARE_MASK;
	ptel |= pgprot_val(pgprot);
	ctrl_outl(ptel, MMU_PTEL);

	__asm__ __volatile__ ("ldtlb" : : : "memory");

	spin_unlock_irqrestore(&sq_mapping_lock, flags);

	/*
	 * Next, we need to map ourselves in the kernel page table, so that
	 * future accesses after a TLB flush will be handled when we take a
	 * page fault.
	 *
	 * Theoretically we could just do this directly and not worry about
	 * setting up the translation by hand ahead of time, but for the
	 * cases where we want a one-shot SQ mapping followed by a quick
	 * writeout before we hit the TLB flush, we do it anyways. This way
	 * we at least save ourselves the initial page fault overhead.
	 */
	vma = __get_vm_area(map->size, VM_ALLOC, map->sq_addr, SQ_ADDRMAX);
	if (!vma)
		return ERR_PTR(-ENOMEM);

	vma->phys_addr = map->addr;

	if (remap_area_pages((unsigned long)vma->addr, vma->phys_addr,
			     map->size, pgprot_val(pgprot))) {
		vunmap(vma->addr);
		return NULL;
	}
#endif /* CONFIG_MMU */

	return map;
}

/**
 * sq_remap - Map a physical address through the Store Queues
 * @phys: Physical address of mapping.
 * @size: Length of mapping.
 * @name: User invoking mapping.
 *
 * Remaps the physical address @phys through the next available store queue
 * address of @size length. @name is logged at boot time as well as through
 * the procfs interface.
 *
 * A pre-allocated and filled sq_mapping pointer is returned, and must be
 * cleaned up with a call to sq_unmap() when the user is done with the
 * mapping.
 */
struct sq_mapping *sq_remap(unsigned long phys, unsigned int size, const char *name)
{
	struct sq_mapping *map;
	unsigned long virt, end;
	unsigned int psz;

	/* Don't allow wraparound or zero size */
	end = phys + size - 1;
	if (!size || end < phys)
		return NULL;
	/* Don't allow anyone to remap normal memory.. */
	if (phys < virt_to_phys(high_memory))
		return NULL;

	phys &= PAGE_MASK;

	size  = PAGE_ALIGN(end + 1) - phys;
	virt  = __sq_get_next_addr();
	psz   = (size + (PAGE_SIZE - 1)) / PAGE_SIZE;
	map   = __sq_alloc_mapping(virt, phys, size, name);

	printk("sqremap: %15s  [%4d page%s]  va 0x%08lx   pa 0x%08lx\n",
	       map->name ? map->name : "???",
	       psz, psz == 1 ? " " : "s",
	       map->sq_addr, map->addr);

	return __sq_remap(map);
}

/**
 * sq_unmap - Unmap a Store Queue allocation
 * @map: Pre-allocated Store Queue mapping.
 *
 * Unmaps the store queue allocation @map that was previously created by
 * sq_remap(). Also frees up the pte that was previously inserted into
 * the kernel page table and discards the UTLB translation.
 */
void sq_unmap(struct sq_mapping *map)
{
	if (map->sq_addr > (unsigned long)high_memory)
		vfree((void *)(map->sq_addr & PAGE_MASK));

	list_del(&map->list);
	kfree(map);
}

/**
 * sq_clear - Clear a store queue range
 * @addr: Address to start clearing from.
 * @len: Length to clear.
 *
 * A quick zero-fill implementation for clearing out memory that has been
 * remapped through the store queues.
 */
void sq_clear(unsigned long addr, unsigned int len)
{
	int i;

	/* Clear out both queues linearly */
	for (i = 0; i < 8; i++) {
		ctrl_outl(0, addr + i + 0);
		ctrl_outl(0, addr + i + 8);
	}

	sq_flush_range(addr, len);
}

/**
 * sq_vma_unmap - Unmap a VMA range
 * @area: VMA containing range.
 * @addr: Start of range.
 * @len: Length of range.
 *
 * Searches the sq_mapping_list for a mapping matching the sq addr @addr,
 * and subsequently frees up the entry. Further cleanup is done by generic
 * code.
 */
static void sq_vma_unmap(struct vm_area_struct *area,
			 unsigned long addr, size_t len)
{
	struct list_head *pos, *tmp;

	list_for_each_safe(pos, tmp, &sq_mapping_list) {
		struct sq_mapping *entry;

		entry = list_entry(pos, typeof(*entry), list);

		if (entry->sq_addr == addr) {
			/*
			 * We could probably get away without doing the tlb flush
			 * here, as generic code should take care of most of this
			 * when unmapping the rest of the VMA range for us. Leave
			 * it in for added sanity for the time being..
			 */
			__flush_tlb_page(get_asid(), entry->sq_addr & PAGE_MASK);

			list_del(&entry->list);
			kfree(entry);

			return;
		}
	}
}

/**
 * sq_vma_sync - Sync a VMA range
 * @area: VMA containing range.
 * @start: Start of range.
 * @len: Length of range.
 * @flags: Additional flags.
 *
 * Synchronizes an sq mapped range by flushing the store queue cache for
 * the duration of the mapping.
 *
 * Used internally for user mappings, which must use msync() to prefetch
 * the store queue cache.
 */
static int sq_vma_sync(struct vm_area_struct *area,
		       unsigned long start, size_t len, unsigned int flags)
{
	sq_flush_range(start, len);

	return 0;
}

static struct vm_operations_struct sq_vma_ops = {
	.unmap	= sq_vma_unmap,
	.sync	= sq_vma_sync,
};

/**
 * sq_mmap - mmap() for /dev/cpu/sq
 * @file: unused.
 * @vma: VMA to remap.
 *
 * Remap the specified vma @vma through the store queues, and setup associated
 * information for the new mapping. Also build up the page tables for the new
 * area.
 */
static int sq_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long size = vma->vm_end - vma->vm_start;
	struct sq_mapping *map;

	/*
	 * We're not interested in any arbitrary virtual address that has
	 * been stuck in the VMA, as we already know what addresses we
	 * want. Save off the size, and reposition the VMA to begin at
	 * the next available sq address.
	 */
	vma->vm_start = __sq_get_next_addr();
	vma->vm_end   = vma->vm_start + size;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	vma->vm_flags |= VM_IO | VM_RESERVED;

	map = __sq_alloc_mapping(vma->vm_start, offset, size, "Userspace");

	if (io_remap_pfn_range(vma, map->sq_addr, map->addr >> PAGE_SHIFT,
				size, vma->vm_page_prot))
		return -EAGAIN;

	vma->vm_ops = &sq_vma_ops;

	return 0;
}

#ifdef CONFIG_PROC_FS
static int sq_mapping_read_proc(char *buf, char **start, off_t off,
				int len, int *eof, void *data)
{
	struct list_head *pos;
	char *p = buf;

	list_for_each_prev(pos, &sq_mapping_list) {
		struct sq_mapping *entry;

		entry = list_entry(pos, typeof(*entry), list);

		p += sprintf(p, "%08lx-%08lx [%08lx]: %s\n", entry->sq_addr,
			     entry->sq_addr + entry->size - 1, entry->addr,
			     entry->name);
	}

	return p - buf;
}
#endif

static struct file_operations sq_fops = {
	.owner		= THIS_MODULE,
	.mmap		= sq_mmap,
};

static struct miscdevice sq_dev = {
	.minor		= STORE_QUEUE_MINOR,
	.name		= "sq",
	.devfs_name	= "cpu/sq",
	.fops		= &sq_fops,
};

static int __init sq_api_init(void)
{
	printk(KERN_NOTICE "sq: Registering store queue API.\n");

#ifdef CONFIG_PROC_FS
	create_proc_read_entry("sq_mapping", 0, 0, sq_mapping_read_proc, 0);
#endif

	return misc_register(&sq_dev);
}

static void __exit sq_api_exit(void)
{
	misc_deregister(&sq_dev);
}

module_init(sq_api_init);
module_exit(sq_api_exit);

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>, M. R. Brown <mrbrown@0xd6.org>");
MODULE_DESCRIPTION("Simple API for SH-4 integrated Store Queues");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(STORE_QUEUE_MINOR);

EXPORT_SYMBOL(sq_remap);
EXPORT_SYMBOL(sq_unmap);
EXPORT_SYMBOL(sq_clear);
EXPORT_SYMBOL(sq_flush);
EXPORT_SYMBOL(sq_flush_range);

