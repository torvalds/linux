/*
 * arch/arm/include/asm/bL_switcher.h
 *
 * Created by:  Nicolas Pitre, April 2012
 * Copyright:   (C) 2012  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef BL_ENTRY_H
#define BL_ENTRY_H

#define BL_CPUS_PER_CLUSTER	4
#define BL_NR_CLUSTERS		2

/* Definitions for bL_cluster_sync_struct */
#define CPU_DOWN		0
#define CPU_COMING_UP		1
#define CPU_UP			2
#define CPU_GOING_DOWN		3

#define CLUSTER_DOWN		0
#define CLUSTER_UP		1
#define CLUSTER_GOING_DOWN	2

#define INBOUND_NOT_COMING_UP	0
#define INBOUND_COMING_UP	1

#define BL_VLOCK_STRUCT_SIZE	8

#ifndef __ASSEMBLY__

#include <linux/mm.h>
#include <linux/types.h>

/* Synchronisation structures for coordinating safe cluster setup/teardown: */

struct bL_cluster_sync_struct {
	s8	cpus[BL_CPUS_PER_CLUSTER];	/* individual CPU states */
	s8	cluster;	/* cluster state */
	s8	inbound;	/* inbound-side state */
	s8	first_man;	/* CPU index of elected first man */
};

struct bL_sync_struct {
	struct bL_cluster_sync_struct clusters[BL_NR_CLUSTERS];
};

/* How much physical memory to reserve for the synchronisation structure: */
#define BL_SYNC_MEM_RESERVE PAGE_ALIGN(sizeof(struct bL_cluster_sync_struct))

extern unsigned long bL_sync_phys;	/* physical address of *bL_sync */

struct bL_vlock_struct {
	unsigned int voting_owner;
	unsigned char voting_offset[BL_CPUS_PER_CLUSTER];
};

struct bL_firstman_vlock_struct {
	struct bL_vlock_struct clusters[BL_NR_CLUSTERS];
};

extern unsigned long bL_vlock_phys;

#define BL_VLOCK_MEM_RESERVE PAGE_ALIGN(sizeof(struct bL_vlock_struct))

void __bL_cpu_going_down(unsigned int cpu, unsigned int cluster);
void __bL_cpu_down(unsigned int cpu, unsigned int cluster);
void __bL_outbound_leave_critical(unsigned int cluster, int state);
bool __bL_outbound_enter_critical(unsigned int this_cpu, unsigned int cluster);
bool __bL_cluster_state(unsigned int cluster);

int __init bL_cluster_sync_reserve(void);
unsigned int bL_running_cluster_num_cpus(unsigned int cpu);
void bL_update_cluster_state(unsigned int value, unsigned int cluster);
void bL_update_cpu_state(unsigned int value, unsigned int cpu,
			 unsigned int cluster);
/*
 * CPU/cluster power operations for higher subsystems to use.
 * This is the "public" API whereas the above is meant to be used
 * only in the implementation of this API.
 */
struct bL_power_ops {
	void (*power_up)(unsigned int cpu, unsigned int cluster);
	void (*power_down)(unsigned int cpu, unsigned int cluster);
	void (*power_up_setup)(void);
	void (*inbound_setup)(unsigned int cpu, unsigned int cluster);
};

int __init bL_cluster_sync_init(const struct bL_power_ops *ops);

/*
 * Platform specific code should use this symbol to set up seconary
 * entry location for processors to use when released from reset.
 */
extern void bl_entry_point(void);

/*
 * This is used to indicate where the given CPU from given cluster should
 * branch once it is ready to re-enter the kernel using ptr, or NULL if it
 * should be gated.  A gated CPU is held in a WFE loop until its vector
 * becomes non NULL.
 */
void bL_set_entry_vector(unsigned cpu, unsigned cluster, void *ptr);

#endif /* ! __ASSEMBLY__ */
#endif
