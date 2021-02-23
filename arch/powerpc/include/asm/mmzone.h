/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Written by Kanoj Sarcar (kanoj@sgi.com) Aug 99
 *
 * PowerPC64 port:
 * Copyright (C) 2002 Anton Blanchard, IBM Corp.
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_
#ifdef __KERNEL__

#include <linux/cpumask.h>

/*
 * generic non-linear memory support:
 *
 * 1) we will not split memory into more chunks than will fit into the
 *    flags field of the struct page
 */

#ifdef CONFIG_NEED_MULTIPLE_NODES

extern struct pglist_data *node_data[];
/*
 * Return a pointer to the node data for node n.
 */
#define NODE_DATA(nid)		(node_data[nid])

/*
 * Following are specific to this numa platform.
 */

extern int numa_cpu_lookup_table[];
extern cpumask_var_t node_to_cpumask_map[];
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

#ifdef CONFIG_MEMORY_HOTPLUG
extern int create_section_mapping(unsigned long start, unsigned long end,
				  int nid, pgprot_t prot);
#endif

#endif /* __KERNEL__ */
#endif /* _ASM_MMZONE_H_ */
