/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000, 01, 02, 03 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 Kanoj Sarcar
 */
#ifndef __ASM_MACH_IP27_IRQ_H
#define __ASM_MACH_IP27_IRQ_H

#define NR_IRQS 256

#include_next <irq.h>

#define IP27_HUB_PEND0_IRQ	(MIPS_CPU_IRQ_BASE + 2)
#define IP27_HUB_PEND1_IRQ	(MIPS_CPU_IRQ_BASE + 3)
#define IP27_RT_TIMER_IRQ	(MIPS_CPU_IRQ_BASE + 4)

#define IP27_HUB_IRQ_BASE	(MIPS_CPU_IRQ_BASE + 8)
#define IP27_HUB_IRQ_COUNT	128

#endif /* __ASM_MACH_IP27_IRQ_H */
