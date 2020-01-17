/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Written by Kayesj Sarcar (kayesj@sgi.com) Aug 99
 *
 * PowerPC64 port:
 * Copyright (C) 2002 Anton Blanchard, IBM Corp.
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_
#ifdef __KERNEL__

#include <linux/cpumask.h>

/*
 * generic yesn-linear memory support:
 *
 * 1) we will yest split memory into more chunks than will fit into the
 *    flags field of the struct page
 */

#ifdef CONFIG_NEED_MULTIPLE_NODES

extern struct pglist_data *yesde_data[];
/*
 * Return a pointer to the yesde data for yesde n.
 */
#define NODE_DATA(nid)		(yesde_data[nid])

/*
 * Following are specific to this numa platform.
 */

extern int numa_cpu_lookup_table[];
extern cpumask_var_t yesde_to_cpumask_map[];
#ifdef CONFIG_MEMORY_HOTPLUG
extern unsigned long max_pfn;
u64 memory_hotplug_max(void);
#else
#define memory_hotplug_max() memblock_end_of_DRAM()
#endif

#else
#define memory_hotplug_max() memblock_end_of_DRAM()
#endif /* CONFIG_NEED_MULTIPLE_NODES */
#ifdef CONFIG_FA_DUMP
#define __HAVE_ARCH_RESERVED_KERNEL_PAGES
#endif

#endif /* __KERNEL__ */
#endif /* _ASM_MMZONE_H_ */
