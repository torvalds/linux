/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __IP30_COMMON_H
#define __IP30_COMMON_H

/*
 * Power Switch is wired via BaseIO BRIDGE slot #6.
 *
 * ACFail is wired via BaseIO BRIDGE slot #7.
 */
#define IP30_POWER_IRQ		HEART_L2_INT_POWER_BTN

#define IP30_HEART_L0_IRQ	(MIPS_CPU_IRQ_BASE + 2)
#define IP30_HEART_L1_IRQ	(MIPS_CPU_IRQ_BASE + 3)
#define IP30_HEART_L2_IRQ	(MIPS_CPU_IRQ_BASE + 4)
#define IP30_HEART_TIMER_IRQ	(MIPS_CPU_IRQ_BASE + 5)
#define IP30_HEART_ERR_IRQ	(MIPS_CPU_IRQ_BASE + 6)

extern void __init ip30_install_ipi(void);
extern struct plat_smp_ops ip30_smp_ops;
extern void __init ip30_per_cpu_init(void);

#endif /* __IP30_COMMON_H */
