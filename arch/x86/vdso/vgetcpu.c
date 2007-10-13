/*
 * Copyright 2006 Andi Kleen, SUSE Labs.
 * Subject to the GNU Public License, v.2
 *
 * Fast user context implementation of getcpu()
 */

#include <linux/kernel.h>
#include <linux/getcpu.h>
#include <linux/jiffies.h>
#include <linux/time.h>
#include <asm/vsyscall.h>
#include <asm/vgtod.h>
#include "vextern.h"

long __vdso_getcpu(unsigned *cpu, unsigned *node, struct getcpu_cache *tcache)
{
	unsigned int dummy, p;
	unsigned long j = 0;

	/* Fast cache - only recompute value once per jiffies and avoid
	   relatively costly rdtscp/cpuid otherwise.
	   This works because the scheduler usually keeps the process
	   on the same CPU and this syscall doesn't guarantee its
	   results anyways.
	   We do this here because otherwise user space would do it on
	   its own in a likely inferior way (no access to jiffies).
	   If you don't like it pass NULL. */
	if (tcache && tcache->blob[0] == (j = *vdso_jiffies)) {
		p = tcache->blob[1];
	} else if (*vdso_vgetcpu_mode == VGETCPU_RDTSCP) {
		/* Load per CPU data from RDTSCP */
		rdtscp(dummy, dummy, p);
	} else {
		/* Load per CPU data from GDT */
		asm("lsl %1,%0" : "=r" (p) : "r" (__PER_CPU_SEG));
	}
	if (tcache) {
		tcache->blob[0] = j;
		tcache->blob[1] = p;
	}
	if (cpu)
		*cpu = p & 0xfff;
	if (node)
		*node = p >> 12;
	return 0;
}

long getcpu(unsigned *cpu, unsigned *node, struct getcpu_cache *tcache)
	__attribute__((weak, alias("__vdso_getcpu")));
