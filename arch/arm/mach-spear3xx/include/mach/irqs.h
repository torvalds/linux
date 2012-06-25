/*
 * arch/arm/mach-spear3xx/include/mach/irqs.h
 *
 * IRQ helper macros for SPEAr3xx machine family
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

/* FIXME: probe all these from DT */
#define SPEAR3XX_IRQ_INTRCOMM_RAS_ARM		1
#define SPEAR3XX_IRQ_GEN_RAS_1			28
#define SPEAR3XX_IRQ_GEN_RAS_2			29
#define SPEAR3XX_IRQ_GEN_RAS_3			30
#define SPEAR3XX_IRQ_VIC_END			32
#define SPEAR3XX_VIRQ_START			SPEAR3XX_IRQ_VIC_END

#define NR_IRQS			160

#endif /* __MACH_IRQS_H */
