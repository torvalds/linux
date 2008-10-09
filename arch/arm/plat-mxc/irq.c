/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <linux/irq.h>
#include <linux/io.h>
#include <mach/common.h>

#define AVIC_BASE		IO_ADDRESS(AVIC_BASE_ADDR)
#define AVIC_INTCNTL		(AVIC_BASE + 0x00)	/* int control reg */
#define AVIC_NIMASK		(AVIC_BASE + 0x04)	/* int mask reg */
#define AVIC_INTENNUM		(AVIC_BASE + 0x08)	/* int enable number reg */
#define AVIC_INTDISNUM		(AVIC_BASE + 0x0C)	/* int disable number reg */
#define AVIC_INTENABLEH		(AVIC_BASE + 0x10)	/* int enable reg high */
#define AVIC_INTENABLEL		(AVIC_BASE + 0x14)	/* int enable reg low */
#define AVIC_INTTYPEH		(AVIC_BASE + 0x18)	/* int type reg high */
#define AVIC_INTTYPEL		(AVIC_BASE + 0x1C)	/* int type reg low */
#define AVIC_NIPRIORITY7	(AVIC_BASE + 0x20)	/* norm int priority lvl7 */
#define AVIC_NIPRIORITY6	(AVIC_BASE + 0x24)	/* norm int priority lvl6 */
#define AVIC_NIPRIORITY5	(AVIC_BASE + 0x28)	/* norm int priority lvl5 */
#define AVIC_NIPRIORITY4	(AVIC_BASE + 0x2C)	/* norm int priority lvl4 */
#define AVIC_NIPRIORITY3	(AVIC_BASE + 0x30)	/* norm int priority lvl3 */
#define AVIC_NIPRIORITY2	(AVIC_BASE + 0x34)	/* norm int priority lvl2 */
#define AVIC_NIPRIORITY1	(AVIC_BASE + 0x38)	/* norm int priority lvl1 */
#define AVIC_NIPRIORITY0	(AVIC_BASE + 0x3C)	/* norm int priority lvl0 */
#define AVIC_NIVECSR		(AVIC_BASE + 0x40)	/* norm int vector/status */
#define AVIC_FIVECSR		(AVIC_BASE + 0x44)	/* fast int vector/status */
#define AVIC_INTSRCH		(AVIC_BASE + 0x48)	/* int source reg high */
#define AVIC_INTSRCL		(AVIC_BASE + 0x4C)	/* int source reg low */
#define AVIC_INTFRCH		(AVIC_BASE + 0x50)	/* int force reg high */
#define AVIC_INTFRCL		(AVIC_BASE + 0x54)	/* int force reg low */
#define AVIC_NIPNDH		(AVIC_BASE + 0x58)	/* norm int pending high */
#define AVIC_NIPNDL		(AVIC_BASE + 0x5C)	/* norm int pending low */
#define AVIC_FIPNDH		(AVIC_BASE + 0x60)	/* fast int pending high */
#define AVIC_FIPNDL		(AVIC_BASE + 0x64)	/* fast int pending low */

#define SYSTEM_PREV_REG		IO_ADDRESS(IIM_BASE_ADDR + 0x20)
#define SYSTEM_SREV_REG		IO_ADDRESS(IIM_BASE_ADDR + 0x24)
#define IIM_PROD_REV_SH		3
#define IIM_PROD_REV_LEN	5

/* Disable interrupt number "irq" in the AVIC */
static void mxc_mask_irq(unsigned int irq)
{
	__raw_writel(irq, AVIC_INTDISNUM);
}

/* Enable interrupt number "irq" in the AVIC */
static void mxc_unmask_irq(unsigned int irq)
{
	__raw_writel(irq, AVIC_INTENNUM);
}

static struct irq_chip mxc_avic_chip = {
	.ack = mxc_mask_irq,
	.mask = mxc_mask_irq,
	.unmask = mxc_unmask_irq,
};

/*
 * This function initializes the AVIC hardware and disables all the
 * interrupts. It registers the interrupt enable and disable functions
 * to the kernel for each interrupt source.
 */
void __init mxc_init_irq(void)
{
	int i;
	u32 reg;

	/* put the AVIC into the reset value with
	 * all interrupts disabled
	 */
	__raw_writel(0, AVIC_INTCNTL);
	__raw_writel(0x1f, AVIC_NIMASK);

	/* disable all interrupts */
	__raw_writel(0, AVIC_INTENABLEH);
	__raw_writel(0, AVIC_INTENABLEL);

	/* all IRQ no FIQ */
	__raw_writel(0, AVIC_INTTYPEH);
	__raw_writel(0, AVIC_INTTYPEL);
	for (i = 0; i < MXC_MAX_INT_LINES; i++) {
		set_irq_chip(i, &mxc_avic_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}

	/* Set WDOG2's interrupt the highest priority level (bit 28-31) */
	reg = __raw_readl(AVIC_NIPRIORITY6);
	reg |= (0xF << 28);
	__raw_writel(reg, AVIC_NIPRIORITY6);

	/* init architectures chained interrupt handler */
	mxc_register_gpios();

	printk(KERN_INFO "MXC IRQ initialized\n");
}
