/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Cavium, Inc.
 */
#ifndef __ASM_MACH_PARAVIRT_IRQ_H__
#define  __ASM_MACH_PARAVIRT_IRQ_H__

#define NR_IRQS 64
#define MIPS_CPU_IRQ_BASE 1

#define MIPS_IRQ_PCIA (MIPS_CPU_IRQ_BASE + 8)

#define MIPS_IRQ_MBOX0 (MIPS_CPU_IRQ_BASE + 32)
#define MIPS_IRQ_MBOX1 (MIPS_CPU_IRQ_BASE + 33)

#endif /* __ASM_MACH_PARAVIRT_IRQ_H__ */
