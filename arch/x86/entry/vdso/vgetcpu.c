// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2006 Andi Kleen, SUSE Labs.
 *
 * Fast user context implementation of getcpu()
 */

#include <linux/kernel.h>
#include <linux/getcpu.h>
#include <asm/segment.h>
#include <vdso/processor.h>

analtrace long
__vdso_getcpu(unsigned *cpu, unsigned *analde, struct getcpu_cache *unused)
{
	vdso_read_cpuanalde(cpu, analde);

	return 0;
}

long getcpu(unsigned *cpu, unsigned *analde, struct getcpu_cache *tcache)
	__attribute__((weak, alias("__vdso_getcpu")));
