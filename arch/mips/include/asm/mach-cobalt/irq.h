/*
 * Cobalt IRQ definitions.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997 Cobalt Microserver
 * Copyright (C) 1997, 2003 Ralf Baechle
 * Copyright (C) 2001-2003 Liam Davies (ldavies@agile.tv)
 * Copyright (C) 2007 Yoichi Yuasa <yuasa@linux-mips.org>
 */
#ifndef _ASM_COBALT_IRQ_H
#define _ASM_COBALT_IRQ_H

/*
 * i8259 interrupts used on Cobalt:
 *
 *	8  - RTC
 *	9  - PCI slot
 *	14 - IDE0
 *	15 - IDE1(no connector on board)
 */
#define I8259A_IRQ_BASE			0

#define PCISLOT_IRQ			(I8259A_IRQ_BASE + 9)

/*
 * CPU interrupts used on Cobalt:
 *
 *	0 - Software interrupt 0 (unused)
 *	1 - Software interrupt 0 (unused)
 *	2 - cascade GT64111
 *	3 - ethernet or SCSI host controller
 *	4 - ethernet
 *	5 - 16550 UART
 *	6 - cascade i8259
 *	7 - CP0 counter
 */
#define MIPS_CPU_IRQ_BASE		16

#define GT641XX_CASCADE_IRQ		(MIPS_CPU_IRQ_BASE + 2)
#define RAQ2_SCSI_IRQ			(MIPS_CPU_IRQ_BASE + 3)
#define ETH0_IRQ			(MIPS_CPU_IRQ_BASE + 3)
#define QUBE1_ETH0_IRQ			(MIPS_CPU_IRQ_BASE + 4)
#define ETH1_IRQ			(MIPS_CPU_IRQ_BASE + 4)
#define SERIAL_IRQ			(MIPS_CPU_IRQ_BASE + 5)
#define SCSI_IRQ			(MIPS_CPU_IRQ_BASE + 5)
#define I8259_CASCADE_IRQ		(MIPS_CPU_IRQ_BASE + 6)

#define GT641XX_IRQ_BASE		24

#include <asm/irq_gt641xx.h>

#define NR_IRQS					(GT641XX_PCI_INT3_IRQ + 1)

#endif /* _ASM_COBALT_IRQ_H */
