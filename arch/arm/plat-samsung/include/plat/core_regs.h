/*
 * Copyright 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/


#ifndef __PLAT_CORE_TEST_H
#define __PLAT_CORE_TEST_H

#include <linux/kernel.h>
#include <linux/types.h>

struct register_type {
	const char *name;
	u32 (*read_reg)(void);
};

struct core_register {
	struct register_type *reg;
	u32 val;
};

static inline u32 armv7_sctlr_read(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(val));
	return val;
}

static inline u32 armv7_actlr_read(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c1, c0, 1" : "=r"(val));
	return val;
}

static inline u32 armv7_prrr_read(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c10, c2, 0" : "=r"(val));
	return val;
}

static inline u32 armv7_nmrr_read(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c10, c2, 1" : "=r"(val));
	return val;
}

static inline u32 armv7_l2ctlr_read(void)
{
	u32 val;
	asm volatile("mrc p15, 1, %0, c9, c0, 2" : "=r"(val));
	return val;
}

static inline u32 armv7_l2ectlr_read(void)
{
	u32 val;
	asm volatile("mrc p15, 1, %0, c9, c0, 3" : "=r"(val));
	return val;
}

static inline u32 armv7_l2actlr_read(void)
{
	u32 val;
	asm volatile("mrc p15, 1, %0, c15, c0, 0" : "=r"(val));
	return val;
}

static inline u32 armv7_l2pfr_read(void)
{
	u32 val;
	asm volatile("mrc p15, 1, %0, c15, c0, 3" : "=r"(val));
	return val;
}

static inline u32 read_mpidr(void)
{
	u32 id;
	asm volatile ("mrc\tp15, 0, %0, c0, c0, 5" : "=r" (id));
	return id;
}

struct register_type reg_sctlr = {
	.name = "SCTLR",
	.read_reg = armv7_sctlr_read
};

struct register_type reg_actlr = {
	.name = "ACTLR",
	.read_reg = armv7_actlr_read
};

struct register_type reg_prrr = {
	.name = "PRRR",
	.read_reg = armv7_prrr_read
};

struct register_type reg_nmrr = {
	.name = "NMRR",
	.read_reg = armv7_nmrr_read
};

struct register_type reg_l2ctlr = {
	.name = "L2CTLR",
	.read_reg = armv7_l2ctlr_read
};

struct register_type reg_l2ectlr = {
	.name = "L2ECTLR",
	.read_reg = armv7_l2ectlr_read
};

struct register_type reg_l2actlr = {
	.name = "L2ACTLR",
	.read_reg = armv7_l2actlr_read
};

struct register_type reg_l2pfr = {
	.name = "L2PFR",
	.read_reg = armv7_l2pfr_read
};

#ifdef CONFIG_SOC_EXYNOS5410
#include <mach/exynos5410_core_regs.h>
#endif
#endif
