/*
 * Derived from include/asm-powerpc/iommu.h
 *
 * Copyright IBM Corporation, 2006-2007
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

#ifndef _ASM_X86_CALGARY_H
#define _ASM_X86_CALGARY_H

#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/timer.h>
#include <asm/types.h>

struct iommu_table {
	struct cal_chipset_ops *chip_ops; /* chipset specific funcs */
	unsigned long  it_base;      /* mapped address of tce table */
	unsigned long  it_hint;      /* Hint for next alloc */
	unsigned long *it_map;       /* A simple allocation bitmap for now */
	void __iomem  *bbar;         /* Bridge BAR */
	u64	       tar_val;      /* Table Address Register */
	struct timer_list watchdog_timer;
	spinlock_t     it_lock;      /* Protects it_map */
	unsigned int   it_size;      /* Size of iommu table in entries */
	unsigned char  it_busno;     /* Bus number this table belongs to */
};

struct cal_chipset_ops {
	void (*handle_quirks)(struct iommu_table *tbl, struct pci_dev *dev);
	void (*tce_cache_blast)(struct iommu_table *tbl);
	void (*dump_error_regs)(struct iommu_table *tbl);
};

#define TCE_TABLE_SIZE_UNSPECIFIED	~0
#define TCE_TABLE_SIZE_64K		0
#define TCE_TABLE_SIZE_128K		1
#define TCE_TABLE_SIZE_256K		2
#define TCE_TABLE_SIZE_512K		3
#define TCE_TABLE_SIZE_1M		4
#define TCE_TABLE_SIZE_2M		5
#define TCE_TABLE_SIZE_4M		6
#define TCE_TABLE_SIZE_8M		7

extern int use_calgary;

#ifdef CONFIG_CALGARY_IOMMU
extern int detect_calgary(void);
#else
static inline int detect_calgary(void) { return -ENODEV; }
#endif

#endif /* _ASM_X86_CALGARY_H */
