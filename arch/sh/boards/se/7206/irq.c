/*
 * linux/arch/sh/boards/se/7206/irq.c
 *
 * Copyright (C) 2005,2006 Yoshinori Sato
 *
 * Hitachi SolutionEngine Support.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/se7206.h>

#define INTSTS0 0x31800000
#define INTSTS1 0x31800002
#define INTMSK0 0x31800004
#define INTMSK1 0x31800006
#define INTSEL  0x31800008

/* shutdown is same as "disable" */
#define shutdown_se7206_irq disable_se7206_irq

static void disable_se7206_irq(unsigned int irq)
{
	unsigned short val;
	unsigned short mask = 0xffff ^ (0x0f << 4 * (3 - (IRQ0_IRQ - irq)));
	unsigned short msk0,msk1;

	/* Set the priority in IPR to 0 */
	val = ctrl_inw(INTC_IPR01);
	val &= mask;
	ctrl_outw(val, INTC_IPR01);
	/* FPGA mask set */
	msk0 = ctrl_inw(INTMSK0);
	msk1 = ctrl_inw(INTMSK1);

	switch (irq) {
	case IRQ0_IRQ:
		msk0 |= 0x0010;
		break;
	case IRQ1_IRQ:
		msk0 |= 0x000f;
		break;
	case IRQ2_IRQ:
		msk0 |= 0x0f00;
		msk1 |= 0x00ff;
		break;
	}
	ctrl_outw(msk0, INTMSK0);
	ctrl_outw(msk1, INTMSK1);
}

static void enable_se7206_irq(unsigned int irq)
{
	unsigned short val;
	unsigned short value = (0x0001 << 4 * (3 - (IRQ0_IRQ - irq)));
	unsigned short msk0,msk1;

	/* Set priority in IPR back to original value */
	val = ctrl_inw(INTC_IPR01);
	val |= value;
	ctrl_outw(val, INTC_IPR01);

	/* FPGA mask reset */
	msk0 = ctrl_inw(INTMSK0);
	msk1 = ctrl_inw(INTMSK1);

	switch (irq) {
	case IRQ0_IRQ:
		msk0 &= ~0x0010;
		break;
	case IRQ1_IRQ:
		msk0 &= ~0x000f;
		break;
	case IRQ2_IRQ:
		msk0 &= ~0x0f00;
		msk1 &= ~0x00ff;
		break;
	}
	ctrl_outw(msk0, INTMSK0);
	ctrl_outw(msk1, INTMSK1);
}

static unsigned int startup_se7206_irq(unsigned int irq)
{
	enable_se7206_irq(irq);
	return 0; /* never anything pending */
}

static void ack_se7206_irq(unsigned int irq)
{
	disable_se7206_irq(irq);
}

static void end_se7206_irq(unsigned int irq)
{
	unsigned short sts0,sts1;

	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_se7206_irq(irq);
	/* FPGA isr clear */
	sts0 = ctrl_inw(INTSTS0);
	sts1 = ctrl_inw(INTSTS1);

	switch (irq) {
	case IRQ0_IRQ:
		sts0 &= ~0x0010;
		break;
	case IRQ1_IRQ:
		sts0 &= ~0x000f;
		break;
	case IRQ2_IRQ:
		sts0 &= ~0x0f00;
		sts1 &= ~0x00ff;
		break;
	}
	ctrl_outw(sts0, INTSTS0);
	ctrl_outw(sts1, INTSTS1);
}

static struct hw_interrupt_type se7206_irq_type = {
	.typename =  "SE7206 FPGA-IRQ",
	.startup = startup_se7206_irq,
	.shutdown = shutdown_se7206_irq,
	.enable = enable_se7206_irq,
	.disable = disable_se7206_irq,
	.ack = ack_se7206_irq,
	.end = end_se7206_irq,
};

static void make_se7206_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_desc[irq].handler = &se7206_irq_type;
	disable_se7206_irq(irq);
}

/*
 * Initialize IRQ setting
 */
void __init init_se7206_IRQ(void)
{
	make_se7206_irq(IRQ0_IRQ); /* SMC91C111 */
	make_se7206_irq(IRQ1_IRQ); /* ATA */
	make_se7206_irq(IRQ3_IRQ); /* SLOT / PCM */
	ctrl_outw(inw(INTC_ICR1) | 0x000b ,INTC_ICR1 ) ; /* ICR1 */

	/* FPGA System register setup*/
	ctrl_outw(0x0000,INTSTS0); /* Clear INTSTS0 */
	ctrl_outw(0x0000,INTSTS1); /* Clear INTSTS1 */
	/* IRQ0=LAN, IRQ1=ATA, IRQ3=SLT,PCM */
	ctrl_outw(0x0001,INTSEL);
}

int se7206_irq_demux(int irq)
{
	return irq;
}
