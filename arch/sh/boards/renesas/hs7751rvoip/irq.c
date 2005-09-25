/*
 * linux/arch/sh/boards/renesas/hs7751rvoip/irq.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Renesas Technology Sales HS7751RVoIP Support.
 *
 * Modified for HS7751RVoIP by
 * Atom Create Engineering Co., Ltd. 2002.
 * Lineo uSolutions, Inc. 2003.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/hs7751rvoip/hs7751rvoip.h>

static int mask_pos[] = {8, 9, 10, 11, 12, 13, 0, 1, 2, 3, 4, 5, 6, 7};

static void enable_hs7751rvoip_irq(unsigned int irq);
static void disable_hs7751rvoip_irq(unsigned int irq);

/* shutdown is same as "disable" */
#define shutdown_hs7751rvoip_irq disable_hs7751rvoip_irq

static void ack_hs7751rvoip_irq(unsigned int irq);
static void end_hs7751rvoip_irq(unsigned int irq);

static unsigned int startup_hs7751rvoip_irq(unsigned int irq)
{
	enable_hs7751rvoip_irq(irq);
	return 0; /* never anything pending */
}

static void disable_hs7751rvoip_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned short val;
	unsigned short mask = 0xffff ^ (0x0001 << mask_pos[irq]);

	/* Set the priority in IPR to 0 */
	local_irq_save(flags);
	val = ctrl_inw(IRLCNTR3);
	val &= mask;
	ctrl_outw(val, IRLCNTR3);
	local_irq_restore(flags);
}

static void enable_hs7751rvoip_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned short val;
	unsigned short value = (0x0001 << mask_pos[irq]);

	/* Set priority in IPR back to original value */
	local_irq_save(flags);
	val = ctrl_inw(IRLCNTR3);
	val |= value;
	ctrl_outw(val, IRLCNTR3);
	local_irq_restore(flags);
}

static void ack_hs7751rvoip_irq(unsigned int irq)
{
	disable_hs7751rvoip_irq(irq);
}

static void end_hs7751rvoip_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_hs7751rvoip_irq(irq);
}

static struct hw_interrupt_type hs7751rvoip_irq_type = {
	.typename =  "HS7751RVoIP IRQ",
	.startup = startup_hs7751rvoip_irq,
	.shutdown = shutdown_hs7751rvoip_irq,
	.enable = enable_hs7751rvoip_irq,
	.disable = disable_hs7751rvoip_irq,
	.ack = ack_hs7751rvoip_irq,
	.end = end_hs7751rvoip_irq,
};

static void make_hs7751rvoip_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &hs7751rvoip_irq_type;
	disable_hs7751rvoip_irq(irq);
}

/*
 * Initialize IRQ setting
 */
void __init init_hs7751rvoip_IRQ(void)
{
	int i;

	/* IRL0=ON HOOK1
	 * IRL1=OFF HOOK1
	 * IRL2=ON HOOK2
	 * IRL3=OFF HOOK2
	 * IRL4=Ringing Detection
	 * IRL5=CODEC
	 * IRL6=Ethernet
	 * IRL7=Ethernet Hub
	 * IRL8=USB Communication
	 * IRL9=USB Connection
	 * IRL10=USB DMA
	 * IRL11=CF Card
	 * IRL12=PCMCIA
	 * IRL13=PCI Slot
	 */
	ctrl_outw(0x9876, IRLCNTR1);
	ctrl_outw(0xdcba, IRLCNTR2);
	ctrl_outw(0x0050, IRLCNTR4);
	ctrl_outw(0x4321, IRLCNTR5);

	for (i=0; i<14; i++)
		make_hs7751rvoip_irq(i);
}
