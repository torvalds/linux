/*
 * arch/arm/mach-spear6xx/include/mach/irqs.h
 *
 * IRQ helper macros for SPEAr6xx machine family
 *
 * Copyright (C) 2009 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

/* IRQ definitions */
/* VIC 1 */
/* FIXME: probe this from DT */
#define IRQ_CPU_GPT1_1				16

#define IRQ_VIC_END				64

/* GPIO pins virtual irqs */
#define VIRTUAL_IRQS				24
#define NR_IRQS					(IRQ_VIC_END + VIRTUAL_IRQS)

#endif	/* __MACH_IRQS_H */
