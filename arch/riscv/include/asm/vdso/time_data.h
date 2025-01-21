/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RISCV_ASM_VDSO_TIME_DATA_H
#define __RISCV_ASM_VDSO_TIME_DATA_H

#include <linux/types.h>
#include <vdso/datapage.h>
#include <asm/hwprobe.h>

struct arch_vdso_time_data {
	/* Stash static answers to the hwprobe queries when all CPUs are selected. */
	__u64 all_cpu_hwprobe_values[RISCV_HWPROBE_MAX_KEY + 1];

	/* Boolean indicating all CPUs have the same static hwprobe values. */
	__u8 homogeneous_cpus;
};

#endif /* __RISCV_ASM_VDSO_TIME_DATA_H */
