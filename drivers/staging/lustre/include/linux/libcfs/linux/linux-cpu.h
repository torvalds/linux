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
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/linux/linux-mem.h
 *
 * Basic library routines.
 *
 * Author: liang@whamcloud.com
 */

#ifndef __LIBCFS_LINUX_CPU_H__
#define __LIBCFS_LINUX_CPU_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <linux/libcfs/libcfs.h> instead
#endif

#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/topology.h>

#ifdef CONFIG_SMP

#define HAVE_LIBCFS_CPT

/** virtual processing unit */
struct cfs_cpu_partition {
	/* CPUs mask for this partition */
	cpumask_t			*cpt_cpumask;
	/* nodes mask for this partition */
	nodemask_t			*cpt_nodemask;
	/* spread rotor for NUMA allocator */
	unsigned			cpt_spread_rotor;
};

/** descriptor for CPU partitions */
struct cfs_cpt_table {
	/* version, reserved for hotplug */
	unsigned			ctb_version;
	/* spread rotor for NUMA allocator */
	unsigned			ctb_spread_rotor;
	/* # of CPU partitions */
	unsigned			ctb_nparts;
	/* partitions tables */
	struct cfs_cpu_partition	*ctb_parts;
	/* shadow HW CPU to CPU partition ID */
	int				*ctb_cpu2cpt;
	/* all cpus in this partition table */
	cpumask_t			*ctb_cpumask;
	/* all nodes in this partition table */
	nodemask_t			*ctb_nodemask;
};

/**
 * comment out definitions for compatible layer
 *
 * typedef cpumask_t			   cfs_cpumask_t;
 *
 * #define cfs_cpu_current()		   smp_processor_id()
 * #define cfs_cpu_online(i)		   cpu_online(i)
 * #define cfs_cpu_online_num()		num_online_cpus()
 * #define cfs_cpu_online_for_each(i)	  for_each_online_cpu(i)
 * #define cfs_cpu_possible_num()	      num_possible_cpus()
 * #define cfs_cpu_possible_for_each(i)	for_each_possible_cpu(i)
 *
 * #ifdef CONFIG_CPUMASK_SIZE
 * #define cfs_cpu_mask_size()		 cpumask_size()
 * #else
 * #define cfs_cpu_mask_size()		 sizeof(cfs_cpumask_t)
 * #endif
 *
 * #define cfs_cpu_mask_set(i, mask)	   cpu_set(i, mask)
 * #define cfs_cpu_mask_unset(i, mask)	 cpu_clear(i, mask)
 * #define cfs_cpu_mask_isset(i, mask)	 cpu_isset(i, mask)
 * #define cfs_cpu_mask_clear(mask)	    cpus_clear(mask)
 * #define cfs_cpu_mask_empty(mask)	    cpus_empty(mask)
 * #define cfs_cpu_mask_weight(mask)	   cpus_weight(mask)
 * #define cfs_cpu_mask_first(mask)	    first_cpu(mask)
 * #define cfs_cpu_mask_any_online(mask)      (any_online_cpu(mask) != NR_CPUS)
 * #define cfs_cpu_mask_for_each(i, mask)      for_each_cpu_mask(i, mask)
 * #define cfs_cpu_mask_bind(t, mask)	  set_cpus_allowed(t, mask)
 *
 * #ifdef HAVE_CPUMASK_COPY
 * #define cfs_cpu_mask_copy(dst, src)	 cpumask_copy(dst, src)
 * #else
 * #define cfs_cpu_mask_copy(dst, src)	 memcpy(dst, src, sizeof(*src))
 * #endif
 *
 * static inline void
 * cfs_cpu_mask_of_online(cfs_cpumask_t *mask)
 * {
 * cfs_cpu_mask_copy(mask, &cpu_online_map);
 * }
 *
 * #ifdef CONFIG_NUMA
 *
 * #define CFS_NODE_NR			 MAX_NUMNODES
 *
 * typedef nodemask_t			  cfs_node_mask_t;
 *
 * #define cfs_node_of_cpu(cpu)		cpu_to_node(cpu)
 * #define cfs_node_online(i)		  node_online(i)
 * #define cfs_node_online_num()	       num_online_nodes()
 * #define cfs_node_online_for_each(i)	 for_each_online_node(i)
 * #define cfs_node_possible_num()	     num_possible_nodes()
 * #define cfs_node_possible_for_each(i)       for_each_node(i)
 *
 * static inline void cfs_node_to_cpumask(int node, cfs_cpumask_t *mask)
 * {
 * #if defined(HAVE_NODE_TO_CPUMASK)
 *      *mask = node_to_cpumask(node);
 * #elif defined(HAVE_CPUMASK_OF_NODE)
 *      cfs_cpu_mask_copy(mask, cpumask_of_node(node));
 * #else
 * # error "Needs node_to_cpumask or cpumask_of_node"
 * #endif
 * }
 *
 * #define cfs_node_mask_set(i, mask)	  node_set(i, mask)
 * #define cfs_node_mask_unset(i, mask)	node_clear(i, mask)
 * #define cfs_node_mask_isset(i, mask)	node_isset(i, mask)
 * #define cfs_node_mask_clear(mask)	   nodes_reset(mask)
 * #define cfs_node_mask_empty(mask)	   nodes_empty(mask)
 * #define cfs_node_mask_weight(mask)	  nodes_weight(mask)
 * #define cfs_node_mask_for_each(i, mask)     for_each_node_mask(i, mask)
 * #define cfs_node_mask_copy(dst, src)	memcpy(dst, src, sizeof(*src))
 *
 * static inline void
 * cfs_node_mask_of_online(cfs_node_mask_t *mask)
 * {
 *       cfs_node_mask_copy(mask, &node_online_map);
 * }
 *
 * #endif
 */

#endif /* CONFIG_SMP */
#endif /* __LIBCFS_LINUX_CPU_H__ */
