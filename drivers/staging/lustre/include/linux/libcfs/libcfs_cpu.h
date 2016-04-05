/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Copyright (c) 2012, 2015 Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/libcfs_cpu.h
 *
 * CPU partition
 *   . CPU partition is virtual processing unit
 *
 *   . CPU partition can present 1-N cores, or 1-N NUMA nodes,
 *     in other words, CPU partition is a processors pool.
 *
 * CPU Partition Table (CPT)
 *   . a set of CPU partitions
 *
 *   . There are two modes for CPT: CFS_CPU_MODE_NUMA and CFS_CPU_MODE_SMP
 *
 *   . User can specify total number of CPU partitions while creating a
 *     CPT, ID of CPU partition is always start from 0.
 *
 *     Example: if there are 8 cores on the system, while creating a CPT
 *     with cpu_npartitions=4:
 *	      core[0, 1] = partition[0], core[2, 3] = partition[1]
 *	      core[4, 5] = partition[2], core[6, 7] = partition[3]
 *
 *	  cpu_npartitions=1:
 *	      core[0, 1, ... 7] = partition[0]
 *
 *   . User can also specify CPU partitions by string pattern
 *
 *     Examples: cpu_partitions="0[0,1], 1[2,3]"
 *	       cpu_partitions="N 0[0-3], 1[4-8]"
 *
 *     The first character "N" means following numbers are numa ID
 *
 *   . NUMA allocators, CPU affinity threads are built over CPU partitions,
 *     instead of HW CPUs or HW nodes.
 *
 *   . By default, Lustre modules should refer to the global cfs_cpt_table,
 *     instead of accessing HW CPUs directly, so concurrency of Lustre can be
 *     configured by cpu_npartitions of the global cfs_cpt_table
 *
 *   . If cpu_npartitions=1(all CPUs in one pool), lustre should work the
 *     same way as 2.2 or earlier versions
 *
 * Author: liang@whamcloud.com
 */

#ifndef __LIBCFS_CPU_H__
#define __LIBCFS_CPU_H__

/* any CPU partition */
#define CFS_CPT_ANY		(-1)

#ifdef CONFIG_SMP
/**
 * return cpumask of CPU partition \a cpt
 */
cpumask_t *cfs_cpt_cpumask(struct cfs_cpt_table *cptab, int cpt);
/**
 * print string information of cpt-table
 */
int cfs_cpt_table_print(struct cfs_cpt_table *cptab, char *buf, int len);
#else /* !CONFIG_SMP */
struct cfs_cpt_table {
	/* # of CPU partitions */
	int			ctb_nparts;
	/* cpu mask */
	cpumask_t		ctb_mask;
	/* node mask */
	nodemask_t		ctb_nodemask;
	/* version */
	__u64			ctb_version;
};

static inline cpumask_t *
cfs_cpt_cpumask(struct cfs_cpt_table *cptab, int cpt)
{
	return NULL;
}

static inline int
cfs_cpt_table_print(struct cfs_cpt_table *cptab, char *buf, int len)
{
	return 0;
}
#endif /* CONFIG_SMP */

extern struct cfs_cpt_table	*cfs_cpt_table;

/**
 * destroy a CPU partition table
 */
void cfs_cpt_table_free(struct cfs_cpt_table *cptab);
/**
 * create a cfs_cpt_table with \a ncpt number of partitions
 */
struct cfs_cpt_table *cfs_cpt_table_alloc(unsigned int ncpt);
/**
 * return total number of CPU partitions in \a cptab
 */
int
cfs_cpt_number(struct cfs_cpt_table *cptab);
/**
 * return number of HW cores or hyper-threadings in a CPU partition \a cpt
 */
int cfs_cpt_weight(struct cfs_cpt_table *cptab, int cpt);
/**
 * is there any online CPU in CPU partition \a cpt
 */
int cfs_cpt_online(struct cfs_cpt_table *cptab, int cpt);
/**
 * return nodemask of CPU partition \a cpt
 */
nodemask_t *cfs_cpt_nodemask(struct cfs_cpt_table *cptab, int cpt);
/**
 * shadow current HW processor ID to CPU-partition ID of \a cptab
 */
int cfs_cpt_current(struct cfs_cpt_table *cptab, int remap);
/**
 * shadow HW processor ID \a CPU to CPU-partition ID by \a cptab
 */
int cfs_cpt_of_cpu(struct cfs_cpt_table *cptab, int cpu);
/**
 * bind current thread on a CPU-partition \a cpt of \a cptab
 */
int cfs_cpt_bind(struct cfs_cpt_table *cptab, int cpt);
/**
 * add \a cpu to CPU partition @cpt of \a cptab, return 1 for success,
 * otherwise 0 is returned
 */
int cfs_cpt_set_cpu(struct cfs_cpt_table *cptab, int cpt, int cpu);
/**
 * remove \a cpu from CPU partition \a cpt of \a cptab
 */
void cfs_cpt_unset_cpu(struct cfs_cpt_table *cptab, int cpt, int cpu);
/**
 * add all cpus in \a mask to CPU partition \a cpt
 * return 1 if successfully set all CPUs, otherwise return 0
 */
int cfs_cpt_set_cpumask(struct cfs_cpt_table *cptab,
			int cpt, cpumask_t *mask);
/**
 * remove all cpus in \a mask from CPU partition \a cpt
 */
void cfs_cpt_unset_cpumask(struct cfs_cpt_table *cptab,
			   int cpt, cpumask_t *mask);
/**
 * add all cpus in NUMA node \a node to CPU partition \a cpt
 * return 1 if successfully set all CPUs, otherwise return 0
 */
int cfs_cpt_set_node(struct cfs_cpt_table *cptab, int cpt, int node);
/**
 * remove all cpus in NUMA node \a node from CPU partition \a cpt
 */
void cfs_cpt_unset_node(struct cfs_cpt_table *cptab, int cpt, int node);

/**
 * add all cpus in node mask \a mask to CPU partition \a cpt
 * return 1 if successfully set all CPUs, otherwise return 0
 */
int cfs_cpt_set_nodemask(struct cfs_cpt_table *cptab,
			 int cpt, nodemask_t *mask);
/**
 * remove all cpus in node mask \a mask from CPU partition \a cpt
 */
void cfs_cpt_unset_nodemask(struct cfs_cpt_table *cptab,
			    int cpt, nodemask_t *mask);
/**
 * unset all cpus for CPU partition \a cpt
 */
void cfs_cpt_clear(struct cfs_cpt_table *cptab, int cpt);
/**
 * convert partition id \a cpt to numa node id, if there are more than one
 * nodes in this partition, it might return a different node id each time.
 */
int cfs_cpt_spread_node(struct cfs_cpt_table *cptab, int cpt);

/**
 * return number of HTs in the same core of \a cpu
 */
int cfs_cpu_ht_nsiblings(int cpu);

/**
 * iterate over all CPU partitions in \a cptab
 */
#define cfs_cpt_for_each(i, cptab)	\
	for (i = 0; i < cfs_cpt_number(cptab); i++)

int  cfs_cpu_init(void);
void cfs_cpu_fini(void);

#endif /* __LIBCFS_CPU_H__ */
