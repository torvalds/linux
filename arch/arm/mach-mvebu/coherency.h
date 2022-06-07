/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-mvebu/include/mach/coherency.h
 *
 * Coherency fabric (Aurora) support for Armada 370 and XP platforms.
 *
 * Copyright (C) 2012 Marvell
 */

#ifndef __MACH_370_XP_COHERENCY_H
#define __MACH_370_XP_COHERENCY_H

extern void __iomem *coherency_base;	/* for coherency_ll.S */
extern unsigned long coherency_phys_base;
int set_cpu_coherent(void);

int coherency_init(void);
int coherency_available(void);

#endif	/* __MACH_370_XP_COHERENCY_H */
