/*
 * linux/arch/sh/boards/renesas/sh7763rdp/irq.c
 *
 * Renesas Solutions SH7763RDP Support.
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Copyright (C) 2008  Nobuhiro Iwamatsu <iwamatsu.nobuhiro@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <mach/sh7763rdp.h>

#define INTC_BASE		(0xFFD00000)
#define INTC_INT2PRI7   (INTC_BASE+0x4001C)
#define INTC_INT2MSKCR	(INTC_BASE+0x4003C)
#define INTC_INT2MSKCR1	(INTC_BASE+0x400D4)

/*
 * Initialize IRQ setting
 */
void __init init_sh7763rdp_IRQ(void)
{
	/* GPIO enabled */
	__raw_writel(1 << 25, INTC_INT2MSKCR);

	/* enable GPIO interrupts */
	__raw_writel((__raw_readl(INTC_INT2PRI7) & 0xFF00FFFF) | 0x000F0000,
		  INTC_INT2PRI7);

	/* USBH enabled */
	__raw_writel(1 << 17, INTC_INT2MSKCR1);

	/* GETHER enabled */
	__raw_writel(1 << 16, INTC_INT2MSKCR1);

	/* DMAC enabled */
	__raw_writel(1 << 8, INTC_INT2MSKCR);
}
