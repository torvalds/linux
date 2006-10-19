/*
 * Renesas Solutions Highlander R7780RP-1 Support.
 *
 * Copyright (C) 2002  Atom Create Engineering Co., Ltd.
 * Copyright (C) 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/r7780rp.h>

#ifdef CONFIG_SH_R7780MP
static int mask_pos[] = {12, 11, 9, 14, 15, 8, 13, 6, 5, 4, 3, 2, 0, 0, 1, 0};
#else
static int mask_pos[] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 5, 6, 4, 0, 1, 2, 0};
#endif

static void enable_r7780rp_irq(unsigned int irq)
{
	/* Set priority in IPR back to original value */
	ctrl_outw(ctrl_inw(IRLCNTR1) | (1 << mask_pos[irq]), IRLCNTR1);
}

static void disable_r7780rp_irq(unsigned int irq)
{
	/* Set the priority in IPR to 0 */
	ctrl_outw(ctrl_inw(IRLCNTR1) & (0xffff ^ (1 << mask_pos[irq])),
		  IRLCNTR1);
}

static struct irq_chip r7780rp_irq_chip __read_mostly = {
	.name		= "r7780rp",
	.mask		= disable_r7780rp_irq,
	.unmask		= enable_r7780rp_irq,
	.mask_ack	= disable_r7780rp_irq,
};

/*
 * Initialize IRQ setting
 */
void __init init_r7780rp_IRQ(void)
{
	int i;

	for (i = 0; i < 15; i++) {
		disable_irq_nosync(i);
		set_irq_chip_and_handler(i, &r7780rp_irq_chip,
					 handle_level_irq);
		enable_r7780rp_irq(i);
	}
}
