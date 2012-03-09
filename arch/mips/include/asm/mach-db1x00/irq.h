/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 by Ralf Baechle
 */
#ifndef __ASM_MACH_GENERIC_IRQ_H
#define __ASM_MACH_GENERIC_IRQ_H


#ifdef NR_IRQS
#undef NR_IRQS
#endif

#ifndef MIPS_CPU_IRQ_BASE
#define MIPS_CPU_IRQ_BASE 0
#endif

/* 8 (MIPS) + 128 (au1300) + 16 (cpld) */
#define NR_IRQS 152

#endif /* __ASM_MACH_GENERIC_IRQ_H */
