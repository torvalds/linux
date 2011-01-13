/*
 *  Galileo/Marvell GT641xx IRQ definitions.
 *
 *  Copyright (C) 2007  Yoichi Yuasa <yuasa@linux-mips.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef _ASM_IRQ_GT641XX_H
#define _ASM_IRQ_GT641XX_H

#ifndef GT641XX_IRQ_BASE
#define GT641XX_IRQ_BASE		8
#endif

#define GT641XX_MEMORY_OUT_OF_RANGE_IRQ		(GT641XX_IRQ_BASE + 1)
#define GT641XX_DMA_OUT_OF_RANGE_IRQ		(GT641XX_IRQ_BASE + 2)
#define GT641XX_CPU_ACCESS_OUT_OF_RANGE_IRQ	(GT641XX_IRQ_BASE + 3)
#define GT641XX_DMA0_IRQ			(GT641XX_IRQ_BASE + 4)
#define GT641XX_DMA1_IRQ			(GT641XX_IRQ_BASE + 5)
#define GT641XX_DMA2_IRQ			(GT641XX_IRQ_BASE + 6)
#define GT641XX_DMA3_IRQ			(GT641XX_IRQ_BASE + 7)
#define GT641XX_TIMER0_IRQ			(GT641XX_IRQ_BASE + 8)
#define GT641XX_TIMER1_IRQ			(GT641XX_IRQ_BASE + 9)
#define GT641XX_TIMER2_IRQ			(GT641XX_IRQ_BASE + 10)
#define GT641XX_TIMER3_IRQ			(GT641XX_IRQ_BASE + 11)
#define GT641XX_PCI_0_MASTER_READ_ERROR_IRQ	(GT641XX_IRQ_BASE + 12)
#define GT641XX_PCI_0_SLAVE_WRITE_ERROR_IRQ	(GT641XX_IRQ_BASE + 13)
#define GT641XX_PCI_0_MASTER_WRITE_ERROR_IRQ	(GT641XX_IRQ_BASE + 14)
#define GT641XX_PCI_0_SLAVE_READ_ERROR_IRQ	(GT641XX_IRQ_BASE + 15)
#define GT641XX_PCI_0_ADDRESS_ERROR_IRQ		(GT641XX_IRQ_BASE + 16)
#define GT641XX_MEMORY_ERROR_IRQ		(GT641XX_IRQ_BASE + 17)
#define GT641XX_PCI_0_MASTER_ABORT_IRQ		(GT641XX_IRQ_BASE + 18)
#define GT641XX_PCI_0_TARGET_ABORT_IRQ		(GT641XX_IRQ_BASE + 19)
#define GT641XX_PCI_0_RETRY_TIMEOUT_IRQ		(GT641XX_IRQ_BASE + 20)
#define GT641XX_CPU_INT0_IRQ			(GT641XX_IRQ_BASE + 21)
#define GT641XX_CPU_INT1_IRQ			(GT641XX_IRQ_BASE + 22)
#define GT641XX_CPU_INT2_IRQ			(GT641XX_IRQ_BASE + 23)
#define GT641XX_CPU_INT3_IRQ			(GT641XX_IRQ_BASE + 24)
#define GT641XX_CPU_INT4_IRQ			(GT641XX_IRQ_BASE + 25)
#define GT641XX_PCI_INT0_IRQ			(GT641XX_IRQ_BASE + 26)
#define GT641XX_PCI_INT1_IRQ			(GT641XX_IRQ_BASE + 27)
#define GT641XX_PCI_INT2_IRQ			(GT641XX_IRQ_BASE + 28)
#define GT641XX_PCI_INT3_IRQ			(GT641XX_IRQ_BASE + 29)

extern void gt641xx_irq_dispatch(void);
extern void gt641xx_irq_init(void);

#endif /* _ASM_IRQ_GT641XX_H */
