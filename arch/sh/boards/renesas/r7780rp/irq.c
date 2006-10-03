/*
 * linux/arch/sh/boards/renesas/r7780rp/irq.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Renesas Solutions Highlander R7780RP-1 Support.
 *
 * Modified for R7780RP-1 by
 * Atom Create Engineering Co., Ltd. 2002.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/r7780rp/r7780rp.h>

#ifdef CONFIG_SH_R7780MP
static int mask_pos[] = {12, 11, 9, 14, 15, 8, 13, 6, 5, 4, 3, 2, 0, 0, 1, 0};
#else
static int mask_pos[] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 5, 6, 4, 0, 1, 2, 0};
#endif

static void enable_r7780rp_irq(unsigned int irq);
static void disable_r7780rp_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_r7780rp_irq disable_r7780rp_irq

static void ack_r7780rp_irq(unsigned int irq);
static void end_r7780rp_irq(unsigned int irq);

static unsigned int startup_r7780rp_irq(unsigned int irq)
{
	enable_r7780rp_irq(irq);
	return 0; /* never anything pending */
}

static void disable_r7780rp_irq(unsigned int irq)
{
	unsigned short val;
	unsigned short mask = 0xffff ^ (0x0001 << mask_pos[irq]);

	/* Set the priority in IPR to 0 */
	val = ctrl_inw(IRLCNTR1);
	val &= mask;
	ctrl_outw(val, IRLCNTR1);
}

static void enable_r7780rp_irq(unsigned int irq)
{
	unsigned short val;
	unsigned short value = (0x0001 << mask_pos[irq]);

	/* Set priority in IPR back to original value */
	val = ctrl_inw(IRLCNTR1);
	val |= value;
	ctrl_outw(val, IRLCNTR1);
}

static void ack_r7780rp_irq(unsigned int irq)
{
	disable_r7780rp_irq(irq);
}

static void end_r7780rp_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_r7780rp_irq(irq);
}

static struct hw_interrupt_type r7780rp_irq_type = {
	.typename = "R7780RP-IRQ",
	.startup = startup_r7780rp_irq,
	.shutdown = shutdown_r7780rp_irq,
	.enable = enable_r7780rp_irq,
	.disable = disable_r7780rp_irq,
	.ack = ack_r7780rp_irq,
	.end = end_r7780rp_irq,
};

static void make_r7780rp_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].chip = &r7780rp_irq_type;
	disable_r7780rp_irq(irq);
}

/*
 * Initialize IRQ setting
 */
void __init init_r7780rp_IRQ(void)
{
	int i;

	/* IRL0=PCI Slot #A
	 * IRL1=PCI Slot #B
	 * IRL2=PCI Slot #C
	 * IRL3=PCI Slot #D
	 * IRL4=CF Card
	 * IRL5=CF Card Insert
	 * IRL6=M66596
	 * IRL7=SD Card
	 * IRL8=Touch Panel
	 * IRL9=SCI
	 * IRL10=Serial
	 * IRL11=Extention #A
	 * IRL11=Extention #B
	 * IRL12=Debug LAN
	 * IRL13=Push Switch
	 * IRL14=ZiggBee IO
	 */

	for (i=0; i<15; i++)
		make_r7780rp_irq(i);
}
