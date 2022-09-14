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

static __always_inline const struct vdso_pcpu_data *get_pcpu_data(void)
{
	return (struct vdso_pcpu_data *)(get_vdso_base() - VDSO_DATA_SIZE);
}

int __vdso_getcpu(unsigned int *cpu, unsigned int *node, struct getcpu_cache *unused)
{
	int cpu_id;
	const struct vdso_pcpu_data *data;

	cpu_id = read_cpu_id();

	if (cpu)
		*cpu = cpu_id;

	if (node) {
		data = get_pcpu_data();
		*node = data[cpu_id].node;
	}

	return 0;
}
