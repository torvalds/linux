/*
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#ifdef CONFIG_ISA_ARCV2

/*
 * ARCv2 based HS38 cores are in-order issue, but still weakly ordered
 * due to micro-arch buffering/queuing of load/store, cache hit vs. miss ...
 *
 * Explicit barrier provided by DMB instruction
 *  - Operand supports fine grained load/store/load+store semantics
 *  - Ensures that selected memory operation issued before it will complete
 *    before any subsequent memory operation of same type
 *  - DMB guarantees SMP as well as local barrier semantics
 *    (asm-generic/barrier.h ensures sane smp_*mb if not defined here, i.e.
 *    UP: barrier(), SMP: smp_*mb == *mb)
 *  - DSYNC provides DMB+completion_of_cache_bpu_maintenance_ops hence not needed
 *    in the general case. Plus it only provides full barrier.
 */

#define mb()	asm volatile("dmb 3\n" : : : "memory")
#define rmb()	asm volatile("dmb 1\n" : : : "memory")
#define wmb()	asm volatile("dmb 2\n" : : : "memory")

#endif

#ifdef CONFIG_ISA_ARCOMPACT

/*
 * ARCompact based cores (ARC700) only have SYNC instruction which is super
 * heavy weight as it flushes the pipeline as well.
 * There are no real SMP implementations of such cores.
 */

#define mb()	asm volatile("sync\n" : : : "memory")
#endif

#include <asm-generic/barrier.h>

#endif
