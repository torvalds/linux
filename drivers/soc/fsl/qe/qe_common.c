/*
 * Common CPM code
 *
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Copyright 2007-2008,2010 Freescale Semiconductor, Inc.
 *
 * Some parts derived from commproc.c/cpm2_common.c, which is:
 * Copyright (c) 1997 Dan error_act (dmalek@jlc.net)
 * Copyright (c) 1999-2001 Dan Malek <dan@embeddedalley.com>
 * Copyright (c) 2000 MontaVista Software, Inc (source@mvista.com)
 * 2006 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */
#include <linux/genalloc.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/of_device.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <soc/fsl/qe/qe.h>

static struct gen_pool *muram_pool;
static spinlock_t cpm_muram_lock;
static u8 __iomem *muram_vbase;
static phys_addr_t muram_pbase;

struct muram_block {
	struct list_head head;
	unsigned long start;
	int size;
};

static LIST_HEAD(muram_block_list);

/* max address size we deal with */
#define OF_MAX_ADDR_CELLS	4
#define GENPOOL_OFFSET		(4096 * 8)

int cpm_muram_init(void)
{
	struct device_node *np;
	struct resource r;
	u32 zero[OF_MAX_ADDR_CELLS] = {};
	resource_size_t max = 0;
	int i = 0;
	int ret = 0;

	if (muram_pbase)
		return 0;

	spin_lock_init(&cpm_muram_lock);
	np = of_find_compatible_node(NULL, NULL, "fsl,cpm-muram-data");
	if (!np) {
		/* try legacy bindings */
		np = of_find_node_by_name(NULL, "data-only");
		if (!np) {
			pr_err("Cannot find CPM muram data node");
			ret = -ENODEV;
			goto out_muram;
		}
	}

	muram_pool = gen_pool_create(0, -1);
	muram_pbase = of_translate_address(np, zero);
	if (muram_pbase == (phys_addr_t)OF_BAD_ADDR) {
		pr_err("Cannot translate zero through CPM muram node");
		ret = -ENODEV;
		goto out_pool;
	}

	while (of_address_to_resource(np, i++, &r) == 0) {
		if (r.end > max)
			max = r.end;
		ret = gen_pool_add(muram_pool, r.start - muram_pbase +
				   GENPOOL_OFFSET, resource_size(&r), -1);
		if (ret) {
			pr_err("QE: couldn't add muram to pool!\n");
			goto out_pool;
		}
	}

	muram_vbase = ioremap(muram_pbase, max - muram_pbase + 1);
	if (!muram_vbase) {
		pr_err("Cannot map QE muram");
		ret = -ENOMEM;
		goto out_pool;
	}
	goto out_muram;
out_pool:
	gen_pool_destroy(muram_pool);
out_muram:
	of_node_put(np);
	return ret;
}

/*
 * cpm_muram_alloc - allocate the requested size worth of multi-user ram
 * @size: number of bytes to allocate
 * @align: requested alignment, in bytes
 *
 * This function returns an offset into the muram area.
 * Use cpm_dpram_addr() to get the virtual address of the area.
 * Use cpm_muram_free() to free the allocation.
 */
unsigned long cpm_muram_alloc(unsigned long size, unsigned long align)
{
	unsigned long start;
	unsigned long flags;
	struct genpool_data_align muram_pool_data;

	spin_lock_irqsave(&cpm_muram_lock, flags);
	muram_pool_data.align = align;
	start = cpm_muram_alloc_common(size, gen_pool_first_fit_align,
				       &muram_pool_data);
	spin_unlock_irqrestore(&cpm_muram_lock, flags);
	return start;
}
EXPORT_SYMBOL(cpm_muram_alloc);

/**
 * cpm_muram_free - free a chunk of multi-user ram
 * @offset: The beginning of the chunk as returned by cpm_muram_alloc().
 */
int cpm_muram_free(unsigned long offset)
{
	unsigned long flags;
	int size;
	struct muram_block *tmp;

	size = 0;
	spin_lock_irqsave(&cpm_muram_lock, flags);
	list_for_each_entry(tmp, &muram_block_list, head) {
		if (tmp->start == offset) {
			size = tmp->size;
			list_del(&tmp->head);
			kfree(tmp);
			break;
		}
	}
	gen_pool_free(muram_pool, offset + GENPOOL_OFFSET, size);
	spin_unlock_irqrestore(&cpm_muram_lock, flags);
	return size;
}
EXPORT_SYMBOL(cpm_muram_free);

/*
 * cpm_muram_alloc_fixed - reserve a specific region of multi-user ram
 * @offset: offset of allocation start address
 * @size: number of bytes to allocate
 * This function returns an offset into the muram area
 * Use cpm_dpram_addr() to get the virtual address of the area.
 * Use cpm_muram_free() to free the allocation.
 */
unsigned long cpm_muram_alloc_fixed(unsigned long offset, unsigned long size)
{
	unsigned long start;
	unsigned long flags;
	struct genpool_data_fixed muram_pool_data_fixed;

	spin_lock_irqsave(&cpm_muram_lock, flags);
	muram_pool_data_fixed.offset = offset + GENPOOL_OFFSET;
	start = cpm_muram_alloc_common(size, gen_pool_fixed_alloc,
				       &muram_pool_data_fixed);
	spin_unlock_irqrestore(&cpm_muram_lock, flags);
	return start;
}
EXPORT_SYMBOL(cpm_muram_alloc_fixed);

/*
 * cpm_muram_alloc_common - cpm_muram_alloc common code
 * @size: number of bytes to allocate
 * @algo: algorithm for alloc.
 * @data: data for genalloc's algorithm.
 *
 * This function returns an offset into the muram area.
 */
unsigned long cpm_muram_alloc_common(unsigned long size, genpool_algo_t algo,
				     void *data)
{
	struct muram_block *entry;
	unsigned long start;

	start = gen_pool_alloc_algo(muram_pool, size, algo, data);
	if (!start)
		goto out2;
	start = start - GENPOOL_OFFSET;
	memset_io(cpm_muram_addr(start), 0, size);
	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		goto out1;
	entry->start = start;
	entry->size = size;
	list_add(&entry->head, &muram_block_list);

	return start;
out1:
	gen_pool_free(muram_pool, start, size);
out2:
	return (unsigned long)-ENOMEM;
}

/**
 * cpm_muram_addr - turn a muram offset into a virtual address
 * @offset: muram offset to convert
 */
void __iomem *cpm_muram_addr(unsigned long offset)
{
	return muram_vbase + offset;
}
EXPORT_SYMBOL(cpm_muram_addr);

unsigned long cpm_muram_offset(void __iomem *addr)
{
	return addr - (void __iomem *)muram_vbase;
}
EXPORT_SYMBOL(cpm_muram_offset);

/**
 * cpm_muram_dma - turn a muram virtual address into a DMA address
 * @offset: virtual address from cpm_muram_addr() to convert
 */
dma_addr_t cpm_muram_dma(void __iomem *addr)
{
	return muram_pbase + ((u8 __iomem *)addr - muram_vbase);
}
EXPORT_SYMBOL(cpm_muram_dma);
