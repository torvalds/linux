/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2024
 */

#ifndef __ASM_S390_ACCESS_REGS_H
#define __ASM_S390_ACCESS_REGS_H

#include <linux/instrumented.h>
#include <asm/sigcontext.h>

struct access_regs {
	unsigned int regs[NUM_ACRS];
};

static inline void save_access_regs(unsigned int *acrs)
{
	struct access_regs *regs = (struct access_regs *)acrs;

	instrument_write(regs, sizeof(*regs));
	asm volatile("stamy	0,15,%[regs]"
		     : [regs] "=QS" (*regs)
		     :
		     : "memory");
}

static inline void restore_access_regs(unsigned int *acrs)
{
	struct access_regs *regs = (struct access_regs *)acrs;

	instrument_read(regs, sizeof(*regs));
	asm volatile("lamy	0,15,%[regs]"
		     :
		     : [regs] "QS" (*regs)
		     : "memory");
}

#endif /* __ASM_S390_ACCESS_REGS_H */
