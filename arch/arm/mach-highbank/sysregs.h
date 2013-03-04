/*
 * Copyright 2011 Calxeda, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _MACH_HIGHBANK__SYSREGS_H_
#define _MACH_HIGHBANK__SYSREGS_H_

#include <linux/io.h>
#include <linux/smp.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>
#include "core.h"

extern void __iomem *sregs_base;

#define HB_SREG_A9_PWR_REQ		0xf00
#define HB_SREG_A9_BOOT_STAT		0xf04
#define HB_SREG_A9_BOOT_DATA		0xf08

#define HB_PWR_SUSPEND			0
#define HB_PWR_SOFT_RESET		1
#define HB_PWR_HARD_RESET		2
#define HB_PWR_SHUTDOWN			3

#define SREG_CPU_PWR_CTRL(c)		(0x200 + ((c) * 4))

static inline void highbank_set_core_pwr(void)
{
	int cpu = MPIDR_AFFINITY_LEVEL(cpu_logical_map(smp_processor_id()), 0);
	if (scu_base_addr)
		scu_power_mode(scu_base_addr, SCU_PM_POWEROFF);
	else
		writel_relaxed(1, sregs_base + SREG_CPU_PWR_CTRL(cpu));
}

static inline void highbank_clear_core_pwr(void)
{
	int cpu = MPIDR_AFFINITY_LEVEL(cpu_logical_map(smp_processor_id()), 0);
	if (scu_base_addr)
		scu_power_mode(scu_base_addr, SCU_PM_NORMAL);
	else
		writel_relaxed(0, sregs_base + SREG_CPU_PWR_CTRL(cpu));
}

static inline void highbank_set_pwr_suspend(void)
{
	writel(HB_PWR_SUSPEND, sregs_base + HB_SREG_A9_PWR_REQ);
	highbank_set_core_pwr();
}

static inline void highbank_set_pwr_shutdown(void)
{
	writel(HB_PWR_SHUTDOWN, sregs_base + HB_SREG_A9_PWR_REQ);
	highbank_set_core_pwr();
}

static inline void highbank_set_pwr_soft_reset(void)
{
	writel(HB_PWR_SOFT_RESET, sregs_base + HB_SREG_A9_PWR_REQ);
	highbank_set_core_pwr();
}

static inline void highbank_set_pwr_hard_reset(void)
{
	writel(HB_PWR_HARD_RESET, sregs_base + HB_SREG_A9_PWR_REQ);
	highbank_set_core_pwr();
}

static inline void highbank_clear_pwr_request(void)
{
	writel(~0UL, sregs_base + HB_SREG_A9_PWR_REQ);
	highbank_clear_core_pwr();
}

#endif
