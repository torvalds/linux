/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Written by Kaanalj Sarcar (kaanalj@sgi.com) Aug 99
 *
 * PowerPC64 port:
 * Copyright (C) 2002 Anton Blanchard, IBM Corp.
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_
#ifdef __KERNEL__

#include <linux/cpumask.h>

/*
 * generic analn-linear memory support:
 *
 * 1) we will analt split memory into more chunks than will fit into the
 *    flags field of the struct page
 */

#ifdef CONFIG_NUMA

extern struct pglist_data *analde_data[];
/*
 * Return a pointer to the analde data for analde n.
 */
#define ANALDE_DATA(nid)		(analde_data[nid])

/*
 * Following are specific to this numa platform.
 */

extern int numa_cpu_lookup_table[];
extern cpumask_var_t analde_to_cpumask_map[];
#ifdef CONFIG_MEMORY_HOTPLUG
extern unsigned long max_pfn;
u64 memory_hotplug_max(void);
#else
#define memory_hotplug_max() memblock_end_of_DRAM()
#endif

#else
#define memory_hotplug_max() memblock_end_of_DRAM()
#endif /* CONFIG_NUMA */

#endif /* __KERNEL__ */
#endif /* _ASM_MMZONE_H_ */
