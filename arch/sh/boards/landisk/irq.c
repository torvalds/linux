/*
 * arch/sh/boards/landisk/irq.c
 *
 * Copyright (C) 2001  Ian da Silva, Jeremy Siegel
 * Based largely on io_se.c.
 *
 * I/O routine for I-O Data Device, Inc. LANDISK.
 *
 * Initial version only to support LAN access; some
 * placeholder code from io_landisk.c left in with the
 * expectation of later SuperIO and PCMCIA access.
 */
/*
 * modified by kogiidena
 * 2005.03.03
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/landisk/iodata_landisk.h>

static void enable_landisk_irq(unsigned int irq);
static void disable_landisk_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_landisk_irq disable_landisk_irq

static void ack_landisk_irq(unsigned int irq);
static void end_landisk_irq(unsigned int irq);

static unsigned int startup_landisk_irq(unsigned int irq)
{
	enable_landisk_irq(irq);
	return 0;		/* never anything pending */
}

static void disable_landisk_irq(unsigned int irq)
{
	unsigned char val;
	unsigned char mask = 0xff ^ (0x01 << (irq - 5));

	/* Set the priority in IPR to 0 */
	val = ctrl_inb(PA_IMASK);
	val &= mask;
	ctrl_outb(val, PA_IMASK);
}

static void enable_landisk_irq(unsigned int irq)
{
	unsigned char val;
	unsigned char value = (0x01 << (irq - 5));

	/* Set priority in IPR back to original value */
	val = ctrl_inb(PA_IMASK);
	val |= value;
	ctrl_outb(val, PA_IMASK);
}

static void ack_landisk_irq(unsigned int irq)
{
	disable_landisk_irq(irq);
}

static void end_landisk_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		enable_landisk_irq(irq);
}

static struct hw_interrupt_type landisk_irq_type = {
	.typename = "LANDISK IRQ",
	.startup = startup_landisk_irq,
	.shutdown = shutdown_landisk_irq,
	.enable = enable_landisk_irq,
	.disable = disable_landisk_irq,
	.ack = ack_landisk_irq,
	.end = end_landisk_irq
};

static void make_landisk_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].chip = &landisk_irq_type;
	disable_landisk_irq(irq);
}

/*
 * Initialize IRQ setting
 */
void __init init_landisk_IRQ(void)
{
	int i;

	for (i = 5; i < 14; i++)
		make_landisk_irq(i);
}
