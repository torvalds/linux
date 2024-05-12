// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023 Rivos, Inc
 */

#include <linux/types.h>
#include <vdso/datapage.h>
#include <vdso/helpers.h>

extern int riscv_hwprobe(struct riscv_hwprobe *pairs, size_t pair_count,
			 size_t cpu_count, unsigned long *cpus,
			 unsigned int flags);

/* Add a prototype to avoid -Wmissing-prototypes warning. */
int __vdso_riscv_hwprobe(struct riscv_hwprobe *pairs, size_t pair_count,
			 size_t cpu_count, unsigned long *cpus,
			 unsigned int flags);

int __vdso_riscv_hwprobe(struct riscv_hwprobe *pairs, size_t pair_count,
			 size_t cpu_count, unsigned long *cpus,
			 unsigned int flags)
{
	const struct vdso_data *vd = __arch_get_vdso_data();
	const struct arch_vdso_data *avd = &vd->arch_data;
	bool all_cpus = !cpu_count && !cpus;
	struct riscv_hwprobe *p = pairs;
	struct riscv_hwprobe *end = pairs + pair_count;

	/*
	 * Defer to the syscall for exotic requests. The vdso has answers
	 * stashed away only for the "all cpus" case. If all CPUs are
	 * homogeneous, then this function can handle requests for arbitrary
	 * masks.
	 */
	if ((flags != 0) || (!all_cpus && !avd->homogeneous_cpus))
		return riscv_hwprobe(pairs, pair_count, cpu_count, cpus, flags);

	/* This is something we can handle, fill out the pairs. */
	while (p < end) {
		if (p->key <= RISCV_HWPROBE_MAX_KEY) {
			p->value = avd->all_cpu_hwprobe_values[p->key];

		} else {
			p->key = -1;
			p->value = 0;
		}

		p++;
	}

	return 0;
}
