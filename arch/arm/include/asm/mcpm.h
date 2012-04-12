/*
 * arch/arm/include/asm/mcpm.h
 *
 * Created by:  Nicolas Pitre, April 2012
 * Copyright:   (C) 2012-2013  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MCPM_H
#define MCPM_H

/*
 * Maximum number of possible clusters / CPUs per cluster.
 *
 * This should be sufficient for quite a while, while keeping the
 * (assembly) code simpler.  When this starts to grow then we'll have
 * to consider dynamic allocation.
 */
#define MAX_CPUS_PER_CLUSTER	4
#define MAX_NR_CLUSTERS		2

#ifndef __ASSEMBLY__

/*
 * Platform specific code should use this symbol to set up secondary
 * entry location for processors to use when released from reset.
 */
extern void mcpm_entry_point(void);

/*
 * This is used to indicate where the given CPU from given cluster should
 * branch once it is ready to re-enter the kernel using ptr, or NULL if it
 * should be gated.  A gated CPU is held in a WFE loop until its vector
 * becomes non NULL.
 */
void mcpm_set_entry_vector(unsigned cpu, unsigned cluster, void *ptr);

#endif /* ! __ASSEMBLY__ */
#endif
