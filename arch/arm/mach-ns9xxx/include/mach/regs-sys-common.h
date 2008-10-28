/*
 * arch/arm/mach-ns9xxx/include/mach/regs-sys-common.h
 *
 * Copyright (C) 2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef __ASM_ARCH_REGSSYSCOMMON_H
#define __ASM_ARCH_REGSSYSCOMMON_H
#include <mach/hardware.h>

/* Interrupt Vector Address Register Level x */
#define SYS_IVA(x)	__REG2(0xa09000c4, (x))

/* Interrupt Configuration registers */
#define SYS_IC(x)	__REG2(0xa0900144, (x))

/* ISRADDR */
#define SYS_ISRADDR     __REG(0xa0900164)

/* Interrupt Status Active */
#define SYS_ISA		__REG(0xa0900168)

/* Interrupt Status Raw */
#define SYS_ISR		__REG(0xa090016c)

#endif /* ifndef __ASM_ARCH_REGSSYSCOMMON_H */
