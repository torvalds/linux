// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023 Rivos, Inc
 */

#include <linux/string.h>
#include <linux/types.h>
#include <vdso/datapage.h>
#include <vdso/helpers.h>

extern int riscv_hwprobe(struct riscv_hwprobe *pairs, size_t pair_count,
			 size_t cpusetsize, unsigned long *cpus,
			 unsigned int flags);

static int riscv_vdso_get_values(struct riscv_hwprobe *pairs, size_t pair_count,
				 size_t cpusetsize, unsigned long *cpus,
				 unsigned int flags)
{
	const struct vdso_data *vd = __arch_get_vdso_data();
	const struct arch_vdso_data *avd = &vd->arch_data;
	bool all_cpus = !cpusetsize && !cpus;
	struct riscv_hwprobe *p = pairs;
	struct riscv_hwprobe *end = pairs + pair_count;

	/*
	 * Defer to the syscall for exotic requests. The vdso has answers
	 * stashed away only for the "all cpus" case. If all CPUs are
	 * homogeneous, then this function can handle requests for arbitrary
	 * masks.
	 */
	if ((flags != 0) || (!all_cpus && !avd->homogeneous_cpus))
		return riscv_hwprobe(pairs, pair_count, cpusetsize, cpus, flags);

	/* This is something we can handle, fill out the pairs. */
	while (p < end) {
		if (riscv_hwprobe_key_is_valid(p->key)) {
			p->value = avd->all_cpu_hwprobe_values[p->key];

		} else {
			p->key = -1;
			p->value = 0;
		}

		p++;
	}

	return 0;
}

static int riscv_vdso_get_cpus(struct riscv_hwprobe *pairs, size_t pair_count,
			       size_t cpusetsize, unsigned long *cpus,
			       unsigned int flags)
{
	const struct vdso_data *vd = __arch_get_vdso_data();
	const struct arch_vdso_data *avd = &vd->arch_data;
	struct riscv_hwprobe *p = pairs;
	struct riscv_hwprobe *end = pairs + pair_count;
	unsigned char *c = (unsigned char *)cpus;
	bool empty_cpus = true;
	bool clear_all = false;
	int i;

	if (!cpusetsize || !cpus)
		return -EINVAL;

	for (i = 0; i < cpusetsize; i++) {
		if (c[i]) {
			empty_cpus = false;
			break;
		}
	}

	if (empty_cpus || flags != RISCV_HWPROBE_WHICH_CPUS || !avd->homogeneous_cpus)
		return riscv_hwprobe(pairs, pair_count, cpusetsize, cpus, flags);

	while (p < end) {
		if (riscv_hwprobe_key_is_valid(p->key)) {
			struct riscv_hwprobe t = {
				.key = p->key,
				.value = avd->all_cpu_hwprobe_values[p->key],
			};

			if (!riscv_hwprobe_pair_cmp(&t, p))
				clear_all = true;
		} else {
			clear_all = true;
			p->key = -1;
			p->value = 0;
		}
		p++;
	}

	if (clear_all) {
		for (i = 0; i < cpusetsize; i++)
			c[i] = 0;
	}

	return 0;
}

/* Add a prototype to avoid -Wmissing-prototypes warning. */
int __vdso_riscv_hwprobe(struct riscv_hwprobe *pairs, size_t pair_count,
			 size_t cpusetsize, unsigned long *cpus,
			 unsigned int flags);

int __vdso_riscv_hwprobe(struct riscv_hwprobe *pairs, size_t pair_count,
			 size_t cpusetsize, unsigned long *cpus,
			 unsigned int flags)
{
	if (flags & RISCV_HWPROBE_WHICH_CPUS)
		return riscv_vdso_get_cpus(pairs, pair_count, cpusetsize,
					   cpus, flags);

	return riscv_vdso_get_values(pairs, pair_count, cpusetsize,
				     cpus, flags);
}
