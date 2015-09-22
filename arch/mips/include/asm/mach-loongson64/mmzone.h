/*
 * Copyright (C) 2010 Loongson Inc. & Lemote Inc. &
 *                    Institute of Computing Technology
 * Author:  Xiang Gao, gaoxiang@ict.ac.cn
 *          Huacai Chen, chenhc@lemote.com
 *          Xiaofu Meng, Shuangshuang Zhang
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _ASM_MACH_MMZONE_H
#define _ASM_MACH_MMZONE_H

#include <boot_param.h>
#define NODE_ADDRSPACE_SHIFT 44
#define NODE0_ADDRSPACE_OFFSET 0x000000000000UL
#define NODE1_ADDRSPACE_OFFSET 0x100000000000UL
#define NODE2_ADDRSPACE_OFFSET 0x200000000000UL
#define NODE3_ADDRSPACE_OFFSET 0x300000000000UL

#define pa_to_nid(addr)  (((addr) & 0xf00000000000) >> NODE_ADDRSPACE_SHIFT)

#define LEVELS_PER_SLICE 128

struct slice_data {
	unsigned long irq_enable_mask[2];
	int level_to_irq[LEVELS_PER_SLICE];
};

struct hub_data {
	cpumask_t	h_cpus;
	unsigned long slice_map;
	unsigned long irq_alloc_mask[2];
	struct slice_data slice[2];
};

struct node_data {
	struct pglist_data pglist;
	struct hub_data hub;
	cpumask_t cpumask;
};

extern struct node_data *__node_data[];

#define NODE_DATA(n)		(&__node_data[(n)]->pglist)
#define hub_data(n)		(&__node_data[(n)]->hub)

extern void setup_zero_pages(void);
extern void __init prom_init_numa_memory(void);

#endif /* _ASM_MACH_MMZONE_H */
