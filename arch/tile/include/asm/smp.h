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

#ifndef _ASM_TILE_SMP_H
#define _ASM_TILE_SMP_H

#ifdef CONFIG_SMP

#include <asm/processor.h>
#include <linux/cpumask.h>
#include <linux/irqreturn.h>
#include <hv/hypervisor.h>

/* Set up this tile to support receiving hypervisor messages */
void init_messaging(void);

/* Set up this tile to support receiving device interrupts and IPIs. */
void init_per_tile_IRQs(void);

/* Send a message to processors specified in mask */
void send_IPI_many(const struct cpumask *mask, int tag);

/* Send a message to all but the sending processor */
void send_IPI_allbutself(int tag);

/* Send a message to a specific processor */
void send_IPI_single(int dest, int tag);

/* Process an IPI message */
void evaluate_message(int tag);

/* Boot a secondary cpu */
void online_secondary(void);

/* Topology of the supervisor tile grid, and coordinates of boot processor */
extern HV_Topology smp_topology;

/* Accessors for grid size */
#define smp_height		(smp_topology.height)
#define smp_width		(smp_topology.width)

/* Convenience functions for converting cpu <-> coords. */
static inline int cpu_x(int cpu)
{
	return cpu % smp_width;
}
static inline int cpu_y(int cpu)
{
	return cpu / smp_width;
}
static inline int xy_to_cpu(int x, int y)
{
	return y * smp_width + x;
}

/* Hypervisor message tags sent via the tile send_IPI*() routines. */
#define MSG_TAG_START_CPU		1
#define MSG_TAG_STOP_CPU		2
#define MSG_TAG_CALL_FUNCTION_MANY	3
#define MSG_TAG_CALL_FUNCTION_SINGLE	4

/* Hook for the generic smp_call_function_many() routine. */
static inline void arch_send_call_function_ipi_mask(struct cpumask *mask)
{
	send_IPI_many(mask, MSG_TAG_CALL_FUNCTION_MANY);
}

/* Hook for the generic smp_call_function_single() routine. */
static inline void arch_send_call_function_single_ipi(int cpu)
{
	send_IPI_single(cpu, MSG_TAG_CALL_FUNCTION_SINGLE);
}

/* Print out the boot string describing which cpus were disabled. */
void print_disabled_cpus(void);

#else /* !CONFIG_SMP */

#define smp_master_cpu		0
#define smp_height		1
#define smp_width		1
#define cpu_x(cpu)		0
#define cpu_y(cpu)		0
#define xy_to_cpu(x, y)		0

#endif /* !CONFIG_SMP */


/* Which cpus may be used as the lotar in a page table entry. */
extern struct cpumask cpu_lotar_map;
#define cpu_is_valid_lotar(cpu) cpumask_test_cpu((cpu), &cpu_lotar_map)

#if CHIP_HAS_CBOX_HOME_MAP()
/* Which processors are used for hash-for-home mapping */
extern struct cpumask hash_for_home_map;
#endif

/* Which cpus can have their cache flushed by hv_flush_remote(). */
extern struct cpumask cpu_cacheable_map;
#define cpu_cacheable(cpu) cpumask_test_cpu((cpu), &cpu_cacheable_map)

/* Convert an HV_LOTAR value into a cpu. */
static inline int hv_lotar_to_cpu(HV_LOTAR lotar)
{
	return HV_LOTAR_X(lotar) + (HV_LOTAR_Y(lotar) * smp_width);
}

/*
 * Extension of <linux/cpumask.h> functionality when you just want
 * to express a mask or suppression or inclusion region without
 * being too concerned about exactly which cpus are valid in that region.
 */
int bitmap_parselist_crop(const char *bp, unsigned long *maskp, int nmaskbits);

#define cpulist_parse_crop(buf, dst) \
			__cpulist_parse_crop((buf), (dst), NR_CPUS)
static inline int __cpulist_parse_crop(const char *buf, struct cpumask *dstp,
					int nbits)
{
	return bitmap_parselist_crop(buf, cpumask_bits(dstp), nbits);
}

/* Initialize the IPI subsystem. */
void ipi_init(void);

/* Function for start-cpu message to cause us to jump to. */
extern unsigned long start_cpu_function_addr;

#endif /* _ASM_TILE_SMP_H */
