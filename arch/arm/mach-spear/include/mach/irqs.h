/*
 * IRQ helper macros for spear machine family
 *
 * Copyright (C) 2009-2012 ST Microelectronics
 * Rajeev Kumar <rajeev-dlh.kumar@st.com>
 * Viresh Kumar <vireshk@kernel.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

#ifdef CONFIG_ARCH_SPEAR3XX
#define NR_IRQS			256
#endif

#ifdef CONFIG_ARCH_SPEAR6XX
/* IRQ definitions */
/* VIC 1 */
#define IRQ_VIC_END				64

/* GPIO pins virtual irqs */
#define VIRTUAL_IRQS				24
#define NR_IRQS					(IRQ_VIC_END + VIRTUAL_IRQS)
#endif

#ifdef CONFIG_ARCH_SPEAR13XX
#define IRQ_GIC_END			160
#define NR_IRQS				IRQ_GIC_END
#endif

#endif /* __MACH_IRQS_H */
