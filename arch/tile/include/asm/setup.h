/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */
#ifndef _ASM_TILE_SETUP_H
#define _ASM_TILE_SETUP_H


#include <linux/pfn.h>
#include <linux/init.h>
#include <uapi/asm/setup.h>

/*
 * Reserved space for vmalloc and iomap - defined in asm/page.h
 */
#define MAXMEM_PFN	PFN_DOWN(MAXMEM)

void early_panic(const char *fmt, ...);
void warn_early_printk(void);
void __init disable_early_printk(void);

/* Init-time routine to do tile-specific per-cpu setup. */
void setup_cpu(int boot);

/* User-level DMA management functions */
void grant_dma_mpls(void);
void restrict_dma_mpls(void);

#ifdef CONFIG_HARDWALL
/* User-level network management functions */
void reset_network_state(void);
struct task_struct;
void hardwall_switch_tasks(struct task_struct *prev, struct task_struct *next);
void hardwall_deactivate_all(struct task_struct *task);
int hardwall_ipi_valid(int cpu);

/* Hook hardwall code into changes in affinity. */
#define arch_set_cpus_allowed(p, new_mask) do { \
	if (!cpumask_equal(&p->cpus_allowed, new_mask)) \
		hardwall_deactivate_all(p); \
} while (0)
#endif

#endif /* _ASM_TILE_SETUP_H */
