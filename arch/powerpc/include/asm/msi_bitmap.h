/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _POWERPC_SYSDEV_MSI_BITMAP_H
#define _POWERPC_SYSDEV_MSI_BITMAP_H

/*
 * Copyright 2008, Michael Ellerman, IBM Corporation.
 */

#include <linux/of.h>
#include <asm/irq.h>

struct msi_bitmap {
	struct device_node	*of_node;
	unsigned long		*bitmap;
	spinlock_t		lock;
	unsigned int		irq_count;
	bool		 	bitmap_from_slab;
};

int msi_bitmap_alloc_hwirqs(struct msi_bitmap *bmp, int num);
void msi_bitmap_free_hwirqs(struct msi_bitmap *bmp, unsigned int offset,
			    unsigned int num);
void msi_bitmap_reserve_hwirq(struct msi_bitmap *bmp, unsigned int hwirq);

int msi_bitmap_reserve_dt_hwirqs(struct msi_bitmap *bmp);

int msi_bitmap_alloc(struct msi_bitmap *bmp, unsigned int irq_count,
		     struct device_node *of_node);
void msi_bitmap_free(struct msi_bitmap *bmp);

#endif /* _POWERPC_SYSDEV_MSI_BITMAP_H */
