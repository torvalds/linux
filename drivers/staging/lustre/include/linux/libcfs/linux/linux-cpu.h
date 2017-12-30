// SPDX-License-Identifier: GPL-2.0
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
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/linux/linux-cpu.h
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
	unsigned int			cpt_spread_rotor;
};

/** descriptor for CPU partitions */
struct cfs_cpt_table {
	/* version, reserved for hotplug */
	unsigned int			ctb_version;
	/* spread rotor for NUMA allocator */
	unsigned int			ctb_spread_rotor;
	/* # of CPU partitions */
	unsigned int			ctb_nparts;
	/* partitions tables */
	struct cfs_cpu_partition	*ctb_parts;
	/* shadow HW CPU to CPU partition ID */
	int				*ctb_cpu2cpt;
	/* all cpus in this partition table */
	cpumask_t			*ctb_cpumask;
	/* all nodes in this partition table */
	nodemask_t			*ctb_nodemask;
};

#endif /* CONFIG_SMP */
#endif /* __LIBCFS_LINUX_CPU_H__ */
