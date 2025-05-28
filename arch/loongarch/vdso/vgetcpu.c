// SPDX-License-Identifier: GPL-2.0-only
/*
 * Fast user context implementation of getcpu()
 */

#include <asm/vdso.h>
#include <linux/getcpu.h>

static __always_inline int read_cpu_id(void)
{
	int cpu_id;

	__asm__ __volatile__(
	"	rdtime.d $zero, %0\n"
	: "=r" (cpu_id)
	:
	: "memory");

	return cpu_id;
}

extern
int __vdso_getcpu(unsigned int *cpu, unsigned int *node, struct getcpu_cache *unused);
int __vdso_getcpu(unsigned int *cpu, unsigned int *node, struct getcpu_cache *unused)
{
	int cpu_id;

	cpu_id = read_cpu_id();

	if (cpu)
		*cpu = cpu_id;

	if (node)
		*node = vdso_u_arch_data.pdata[cpu_id].node;

	return 0;
}
