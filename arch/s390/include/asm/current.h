/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/current.h"
 */

#ifndef _S390_CURRENT_H
#define _S390_CURRENT_H

#include <asm/lowcore.h>
#include <asm/machine.h>

struct task_struct;

static __always_inline struct task_struct *get_current(void)
{
	unsigned long ptr, lc_current;

	lc_current = offsetof(struct lowcore, current_task);
	asm_inline(
		ALTERNATIVE("	lg	%[ptr],%[offzero](%%r0)\n",
			    "	lg	%[ptr],%[offalt](%%r0)\n",
			    ALT_FEATURE(MFEATURE_LOWCORE))
		: [ptr] "=d" (ptr)
		: [offzero] "i" (lc_current),
		  [offalt] "i" (lc_current + LOWCORE_ALT_ADDRESS));
	return (struct task_struct *)ptr;
}

#define current get_current()

#endif /* !(_S390_CURRENT_H) */
