/*
 * linux/arch/sh/boards/renesas/rts7751r2d/irq.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Renesas Technology Sales RTS7751R2D Support.
 *
 * Modified for RTS7751R2D by
 * Atom Create Engineering Co., Ltd. 2002.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/rts7751r2d.h>

#if defined(CONFIG_RTS7751R2D_REV11)
static int mask_pos[] = {11, 9, 8, 12, 10, 6, 5, 4, 7, 14, 13, 0, 0, 0, 0};
#else
static int mask_pos[] = {6, 11, 9, 8, 12, 10, 5, 4, 7, 14, 13, 0, 0, 0, 0};
#endif

extern int voyagergx_irq_demux(int irq);
extern void setup_voyagergx_irq(void);

static void enable_rts7751r2d_irq(unsigned int irq)
{
	/* Set priority in IPR back to original value */
	ctrl_outw(ctrl_inw(IRLCNTR1) | (1 << mask_pos[irq]), IRLCNTR1);
}

static void disable_rts7751r2d_irq(unsigned int irq)
{
	/* Set the priority in IPR to 0 */
	ctrl_outw(ctrl_inw(IRLCNTR1) & (0xffff ^ (1 << mask_pos[irq])),
		  IRLCNTR1);
}

int rts7751r2d_irq_demux(int irq)
{
	return voyagergx_irq_demux(irq);
}

static struct irq_chip rts7751r2d_irq_chip __read_mostly = {
	.name		= "rts7751r2d",
	.mask		= disable_rts7751r2d_irq,
	.unmask		= enable_rts7751r2d_irq,
	.mask_ack	= disable_rts7751r2d_irq,
};

/*
 * Initialize IRQ setting
 */
void __init init_rts7751r2d_IRQ(void)
{
	int i;

	/* IRL0=KEY Input
	 * IRL1=Ethernet
	 * IRL2=CF Card
	 * IRL3=CF Card Insert
	 * IRL4=PCMCIA
	 * IRL5=VOYAGER
	 * IRL6=RTC Alarm
	 * IRL7=RTC Timer
	 * IRL8=SD Card
	 * IRL9=PCI Slot #1
	 * IRL10=PCI Slot #2
	 * IRL11=Extention #0
	 * IRL12=Extention #1
	 * IRL13=Extention #2
	 * IRL14=Extention #3
	 */

	for (i=0; i<15; i++) {
		disable_irq_nosync(i);
		set_irq_chip_and_handler_name(i, &rts7751r2d_irq_chip,
					      handle_level_irq, "level");
		enable_rts7751r2d_irq(i);
	}

	setup_voyagergx_irq();
}
