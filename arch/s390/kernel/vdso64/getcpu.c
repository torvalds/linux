// SPDX-License-Identifier: GPL-2.0
/* Copyright IBM Corp. 2020 */

#include <linux/compiler.h>
#include <linux/getcpu.h>
#include <asm/timex.h>
#include "vdso.h"

int __s390_vdso_getcpu(unsigned *cpu, unsigned *node, struct getcpu_cache *unused)
{
	__u16 todval[8];

	/* CPU number is stored in the programmable field of the TOD clock */
	get_tod_clock_ext((char *)todval);
	if (cpu)
		*cpu = todval[7];
	/* NUMA node is always zero */
	if (node)
		*node = 0;
	return 0;
}
