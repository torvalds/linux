/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NUMA support for s390
 *
 * Declare the NUMA core code structures and functions.
 *
 * Copyright IBM Corp. 2015
 */

#ifndef _ASM_S390_NUMA_H
#define _ASM_S390_NUMA_H

#ifdef CONFIG_NUMA

#include <linux/numa.h>

void numa_setup(void);

#else

static inline void numa_setup(void) { }

#endif /* CONFIG_NUMA */

#endif /* _ASM_S390_NUMA_H */
