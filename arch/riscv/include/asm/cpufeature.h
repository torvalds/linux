/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022-2023 Rivos, Inc
 */

#ifndef _ASM_CPUFEATURE_H
#define _ASM_CPUFEATURE_H

/*
 * These are probed via a device_initcall(), via either the SBI or directly
 * from the corresponding CSRs.
 */
struct riscv_cpuinfo {
	unsigned long mvendorid;
	unsigned long marchid;
	unsigned long mimpid;
};

DECLARE_PER_CPU(struct riscv_cpuinfo, riscv_cpuinfo);

DECLARE_PER_CPU(long, misaligned_access_speed);

#endif
