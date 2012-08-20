/*
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/exception.h>
#include <mach/mxs.h>
#include <mach/common.h>

#define HW_ICOLL_VECTOR				0x0000
#define HW_ICOLL_LEVELACK			0x0010
#define HW_ICOLL_CTRL				0x0020
#define HW_ICOLL_STAT_OFFSET			0x0070
#define HW_ICOLL_INTERRUPTn_SET(n)		(0x0124 + (n) * 0x10)
#define HW_ICOLL_INTERRUPTn_CLR(n)		(0x0128 + (n) * 0x10)
#define BM_ICOLL_INTERRUPTn_ENABLE		0x00000004
#define BV_ICOLL_LEVELACK_IRQLEVELACK__LEVEL0	0x1

static void __iomem *icoll_base = MXS_IO_ADDRESS(MXS_ICOLL_BASE_ADDR);

static void icoll_ack_irq(struct irq_data *d)
{
	/*
	 * The Interrupt Collector is able to prioritize irqs.
	 * Currently only level 0 is used. So acking can use
	 * BV_ICOLL_LEVELACK_IRQLEVELACK__LEVEL0 unconditionally.
	 */
	__raw_writel(BV_ICOLL_LEVELACK_IRQLEVELACK__LEVEL0,
			icoll_base + HW_ICOLL_LEVELACK);
}

static void icoll_mask_irq(struct irq_data *d)
{
	__raw_writel(BM_ICOLL_INTERRUPTn_ENABLE,
			icoll_base + HW_ICOLL_INTERRUPTn_CLR(d->irq));
}

static void icoll_unmask_irq(struct irq_data *d)
{
	__raw_writel(BM_ICOLL_INTERRUPTn_ENABLE,
			icoll_base + HW_ICOLL_INTERRUPTn_SET(d->irq));
}

static struct irq_chip mxs_icoll_chip = {
	.irq_ack = icoll_ack_irq,
	.irq_mask = icoll_mask_irq,
	.irq_unmask = icoll_unmask_irq,
};

asmlinkage void __exception_irq_entry icoll_handle_irq(struct pt_regs *regs)
{
	u32 irqnr;

	do {
		irqnr = __raw_readl(icoll_base + HW_ICOLL_STAT_OFFSET);
		if (irqnr != 0x7f) {
			__raw_writel(irqnr, icoll_base + HW_ICOLL_VECTOR);
			handle_IRQ(irqnr, regs);
			continue;
		}
		break;
	} while (1);
}

void __init icoll_init_irq(void)
{
	int i;

	/*
	 * Interrupt Collector reset, which initializes the priority
	 * for each irq to level 0.
	 */
	mxs_reset_block(icoll_base + HW_ICOLL_CTRL);

	for (i = 0; i < MXS_INTERNAL_IRQS; i++) {
		irq_set_chip_and_handler(i, &mxs_icoll_chip, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}
}
