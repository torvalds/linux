/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2013 Texas Instruments, Inc.
 *	Cyril Chemparathy <cyril@ti.com>
 *	Santosh Shilimkar <santosh.shillimkar@ti.com>
 */

#ifndef __KEYSTONE_H__
#define __KEYSTONE_H__

#define KEYSTONE_MON_CPU_UP_IDX		0x00

#ifndef __ASSEMBLER__

extern const struct smp_operations keystone_smp_ops;
extern void secondary_startup(void);
extern u32 keystone_cpu_smc(u32 command, u32 cpu, u32 addr);
extern int keystone_pm_runtime_init(void);

#endif /* __ASSEMBLER__ */
#endif /* __KEYSTONE_H__ */
