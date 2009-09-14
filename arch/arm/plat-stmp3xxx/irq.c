/*
 * Freescale STMP37XX/STMP378X common interrupt handling code
 *
 * Author: Vladislav Buzov <vbuzov@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/sysdev.h>

#include <mach/stmp3xxx.h>
#include <mach/platform.h>
#include <mach/regs-icoll.h>

void __init stmp3xxx_init_irq(struct irq_chip *chip)
{
	unsigned int i, lv;

	/* Reset the interrupt controller */
	stmp3xxx_reset_block(REGS_ICOLL_BASE + HW_ICOLL_CTRL, true);

	/* Disable all interrupts initially */
	for (i = 0; i < NR_REAL_IRQS; i++) {
		chip->mask(i);
		set_irq_chip(i, chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}

	/* Ensure vector is cleared */
	for (lv = 0; lv < 4; lv++)
		__raw_writel(1 << lv, REGS_ICOLL_BASE + HW_ICOLL_LEVELACK);
	__raw_writel(0, REGS_ICOLL_BASE + HW_ICOLL_VECTOR);

	/* Barrier */
	(void)__raw_readl(REGS_ICOLL_BASE + HW_ICOLL_STAT);
}

