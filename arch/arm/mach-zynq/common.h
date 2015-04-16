/*
 * This file contains common function prototypes to avoid externs
 * in the c files.
 *
 *  Copyright (C) 2011 Xilinx
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MACH_ZYNQ_COMMON_H__
#define __MACH_ZYNQ_COMMON_H__

void zynq_secondary_startup(void);

extern int zynq_slcr_init(void);
extern int zynq_early_slcr_init(void);
extern void zynq_slcr_system_reset(void);
extern void zynq_slcr_cpu_stop(int cpu);
extern void zynq_slcr_cpu_start(int cpu);
extern bool zynq_slcr_cpu_state_read(int cpu);
extern void zynq_slcr_cpu_state_write(int cpu, bool die);
extern u32 zynq_slcr_get_device_id(void);

#ifdef CONFIG_SMP
extern char zynq_secondary_trampoline;
extern char zynq_secondary_trampoline_jump;
extern char zynq_secondary_trampoline_end;
extern int zynq_cpun_start(u32 address, int cpu);
extern struct smp_operations zynq_smp_ops __initdata;
#endif

extern void __iomem *zynq_scu_base;

void zynq_pm_late_init(void);

static inline void zynq_core_pm_init(void)
{
	/* A9 clock gating */
	asm volatile ("mrc  p15, 0, r12, c15, c0, 0\n"
		      "orr  r12, r12, #1\n"
		      "mcr  p15, 0, r12, c15, c0, 0\n"
		      : /* no outputs */
		      : /* no inputs */
		      : "r12");
}

#endif
