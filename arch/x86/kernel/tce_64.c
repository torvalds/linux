/*
 * This file manages the translation entries for the IBM Calgary IOMMU.
 *
 * Derived from arch/powerpc/platforms/pseries/iommu.c
 *
 * Copyright (C) IBM Corporation, 2006
 *
 * Author: Jon Mason <jdmason@us.ibm.com>
 * Author: Muli Ben-Yehuda <muli@il.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/bootmem.h>
#include <asm/tce.h>
#include <asm/calgary.h>
#include <asm/proto.h>
#include <asm/cacheflush.h>

/* flush a tce at 'tceaddr' to main memory */
static inline void flush_tce(void* tceaddr)
{
	/* a single tce can't cross a cache line */
	if (boot_cpu_has(X86_FEATURE_CLFLUSH))
		clflush(tceaddr);
	else
		wbinvd();
}

void tce_build(struct iommu_table *tbl, unsigned long index,
	unsigned int npages, unsigned long uaddr, int direction)
{
	u64* tp;
	u64 t;
	u64 rpn;

	t = (1 << TCE_READ_SHIFT);
	if (direction != DMA_TO_DEVICE)
		t |= (1 << TCE_WRITE_SHIFT);

	tp = ((u64*)tbl->it_base) + index;

	while (npages--) {
		rpn = (virt_to_bus((void*)uaddr)) >> PAGE_SHIFT;
		t &= ~TCE_RPN_MASK;
		t |= (rpn << TCE_RPN_SHIFT);

		*tp = cpu_to_be64(t);
		flush_tce(tp);

		uaddr += PAGE_SIZE;
		tp++;
	}
}

void tce_free(struct iommu_table *tbl, long index, unsigned int npages)
{
	u64* tp;

	tp  = ((u64*)tbl->it_base) + index;

	while (npages--) {
		*tp = cpu_to_be64(0);
		flush_tce(tp);
		tp++;
	}
}

static inline unsigned int table_size_to_number_of_entries(unsigned char size)
{
	/*
	 * size is the order of the table, 0-7
	 * smallest table is 8K entries, so shift result by 13 to
	 * multiply by 8K
	 */
	return (1 << size) << 13;
}

static int tce_table_setparms(struct pci_dev *dev, struct iommu_table *tbl)
{
	unsigned int bitmapsz;
	unsigned long bmppages;
	int ret;

	tbl->it_busno = dev->bus->number;

	/* set the tce table size - measured in entries */
	tbl->it_size = table_size_to_number_of_entries(specified_table_size);

	/*
	 * number of bytes needed for the bitmap size in number of
	 * entries; we need one bit per entry
	 */
	bitmapsz = tbl->it_size / BITS_PER_BYTE;
	bmppages = __get_free_pages(GFP_KERNEL, get_order(bitmapsz));
	if (!bmppages) {
		printk(KERN_ERR "Calgary: cannot allocate bitmap\n");
		ret = -ENOMEM;
		goto done;
	}

	tbl->it_map = (unsigned long*)bmppages;

	memset(tbl->it_map, 0, bitmapsz);

	tbl->it_hint = 0;

	spin_lock_init(&tbl->it_lock);

	return 0;

done:
	return ret;
}

int __init build_tce_table(struct pci_dev *dev, void __iomem *bbar)
{
	struct iommu_table *tbl;
	int ret;

	if (pci_iommu(dev->bus)) {
		printk(KERN_ERR "Calgary: dev %p has sysdata->iommu %p\n",
		       dev, pci_iommu(dev->bus));
		BUG();
	}

	tbl = kzalloc(sizeof(struct iommu_table), GFP_KERNEL);
	if (!tbl) {
		printk(KERN_ERR "Calgary: error allocating iommu_table\n");
		ret = -ENOMEM;
		goto done;
	}

	ret = tce_table_setparms(dev, tbl);
	if (ret)
		goto free_tbl;

	tbl->bbar = bbar;

	set_pci_iommu(dev->bus, tbl);

	return 0;

free_tbl:
	kfree(tbl);
done:
	return ret;
}

void * __init alloc_tce_table(void)
{
	unsigned int size;

	size = table_size_to_number_of_entries(specified_table_size);
	size *= TCE_ENTRY_SIZE;

	return __alloc_bootmem_low(size, size, 0);
}

void __init free_tce_table(void *tbl)
{
	unsigned int size;

	if (!tbl)
		return;

	size = table_size_to_number_of_entries(specified_table_size);
	size *= TCE_ENTRY_SIZE;

	free_bootmem(__pa(tbl), size);
}
