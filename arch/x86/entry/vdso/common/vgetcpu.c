// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2006 Andi Kleen, SUSE Labs.
 *
 * Fast user context implementation of getcpu()
 */

#include <linux/kernel.h>
#include <asm/segment.h>
#include <vdso/processor.h>

notrace long
__vdso_getcpu(unsigned *cpu, unsigned *node, void *unused)
{
	vdso_read_cpunode(cpu, node);

	return 0;
}

long getcpu(unsigned *cpu, unsigned *node, void *tcache)
	__attribute__((weak, alias("__vdso_getcpu")));
