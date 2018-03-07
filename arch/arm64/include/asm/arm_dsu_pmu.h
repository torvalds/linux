/*
 * ARM DynamIQ Shared Unit (DSU) PMU Low level register access routines.
 *
 * Copyright (C) ARM Limited, 2017.
 *
 * Author: Suzuki K Poulose <suzuki.poulose@arm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/build_bug.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/barrier.h>
#include <asm/sysreg.h>


#define CLUSTERPMCR_EL1			sys_reg(3, 0, 15, 5, 0)
#define CLUSTERPMCNTENSET_EL1		sys_reg(3, 0, 15, 5, 1)
#define CLUSTERPMCNTENCLR_EL1		sys_reg(3, 0, 15, 5, 2)
#define CLUSTERPMOVSSET_EL1		sys_reg(3, 0, 15, 5, 3)
#define CLUSTERPMOVSCLR_EL1		sys_reg(3, 0, 15, 5, 4)
#define CLUSTERPMSELR_EL1		sys_reg(3, 0, 15, 5, 5)
#define CLUSTERPMINTENSET_EL1		sys_reg(3, 0, 15, 5, 6)
#define CLUSTERPMINTENCLR_EL1		sys_reg(3, 0, 15, 5, 7)
#define CLUSTERPMCCNTR_EL1		sys_reg(3, 0, 15, 6, 0)
#define CLUSTERPMXEVTYPER_EL1		sys_reg(3, 0, 15, 6, 1)
#define CLUSTERPMXEVCNTR_EL1		sys_reg(3, 0, 15, 6, 2)
#define CLUSTERPMMDCR_EL1		sys_reg(3, 0, 15, 6, 3)
#define CLUSTERPMCEID0_EL1		sys_reg(3, 0, 15, 6, 4)
#define CLUSTERPMCEID1_EL1		sys_reg(3, 0, 15, 6, 5)

static inline u32 __dsu_pmu_read_pmcr(void)
{
	return read_sysreg_s(CLUSTERPMCR_EL1);
}

static inline void __dsu_pmu_write_pmcr(u32 val)
{
	write_sysreg_s(val, CLUSTERPMCR_EL1);
	isb();
}

static inline u32 __dsu_pmu_get_reset_overflow(void)
{
	u32 val = read_sysreg_s(CLUSTERPMOVSCLR_EL1);
	/* Clear the bit */
	write_sysreg_s(val, CLUSTERPMOVSCLR_EL1);
	isb();
	return val;
}

static inline void __dsu_pmu_select_counter(int counter)
{
	write_sysreg_s(counter, CLUSTERPMSELR_EL1);
	isb();
}

static inline u64 __dsu_pmu_read_counter(int counter)
{
	__dsu_pmu_select_counter(counter);
	return read_sysreg_s(CLUSTERPMXEVCNTR_EL1);
}

static inline void __dsu_pmu_write_counter(int counter, u64 val)
{
	__dsu_pmu_select_counter(counter);
	write_sysreg_s(val, CLUSTERPMXEVCNTR_EL1);
	isb();
}

static inline void __dsu_pmu_set_event(int counter, u32 event)
{
	__dsu_pmu_select_counter(counter);
	write_sysreg_s(event, CLUSTERPMXEVTYPER_EL1);
	isb();
}

static inline u64 __dsu_pmu_read_pmccntr(void)
{
	return read_sysreg_s(CLUSTERPMCCNTR_EL1);
}

static inline void __dsu_pmu_write_pmccntr(u64 val)
{
	write_sysreg_s(val, CLUSTERPMCCNTR_EL1);
	isb();
}

static inline void __dsu_pmu_disable_counter(int counter)
{
	write_sysreg_s(BIT(counter), CLUSTERPMCNTENCLR_EL1);
	isb();
}

static inline void __dsu_pmu_enable_counter(int counter)
{
	write_sysreg_s(BIT(counter), CLUSTERPMCNTENSET_EL1);
	isb();
}

static inline void __dsu_pmu_counter_interrupt_enable(int counter)
{
	write_sysreg_s(BIT(counter), CLUSTERPMINTENSET_EL1);
	isb();
}

static inline void __dsu_pmu_counter_interrupt_disable(int counter)
{
	write_sysreg_s(BIT(counter), CLUSTERPMINTENCLR_EL1);
	isb();
}


static inline u32 __dsu_pmu_read_pmceid(int n)
{
	switch (n) {
	case 0:
		return read_sysreg_s(CLUSTERPMCEID0_EL1);
	case 1:
		return read_sysreg_s(CLUSTERPMCEID1_EL1);
	default:
		BUILD_BUG();
		return 0;
	}
}
