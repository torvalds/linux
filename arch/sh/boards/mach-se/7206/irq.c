// SPDX-License-Identifier: GPL-2.0
/*
 * linux/arch/sh/boards/se/7206/irq.c
 *
 * Copyright (C) 2005,2006 Yoshinori Sato
 *
 * Hitachi SolutionEngine Support.
 *
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <mach-se/mach/se7206.h>

#define INTSTS0 0x31800000
#define INTSTS1 0x31800002
#define INTMSK0 0x31800004
#define INTMSK1 0x31800006
#define INTSEL  0x31800008

#define IRQ0_IRQ 64
#define IRQ1_IRQ 65
#define IRQ3_IRQ 67

#define INTC_IPR01 0xfffe0818
#define INTC_ICR1  0xfffe0802

static void disable_se7206_irq(struct irq_data *data)
{
	unsigned int irq = data->irq;
	unsigned short val;
	unsigned short mask = 0xffff ^ (0x0f << 4 * (3 - (IRQ0_IRQ - irq)));
	unsigned short msk0,msk1;

	/* Set the priority in IPR to 0 */
	val = __raw_readw(INTC_IPR01);
	val &= mask;
	__raw_writew(val, INTC_IPR01);
	/* FPGA mask set */
	msk0 = __raw_readw(INTMSK0);
	msk1 = __raw_readw(INTMSK1);

	switch (irq) {
	case IRQ0_IRQ:
		msk0 |= 0x0010;
		break;
	case IRQ1_IRQ:
		msk0 |= 0x000f;
		break;
	case IRQ3_IRQ:
		msk0 |= 0x0f00;
		msk1 |= 0x00ff;
		break;
	}
	__raw_writew(msk0, INTMSK0);
	__raw_writew(msk1, INTMSK1);
}

static void enable_se7206_irq(struct irq_data *data)
{
	unsigned int irq = data->irq;
	unsigned short val;
	unsigned short value = (0x0001 << 4 * (3 - (IRQ0_IRQ - irq)));
	unsigned short msk0,msk1;

	/* Set priority in IPR back to original value */
	val = __raw_readw(INTC_IPR01);
	val |= value;
	__raw_writew(val, INTC_IPR01);

	/* FPGA mask reset */
	msk0 = __raw_readw(INTMSK0);
	msk1 = __raw_readw(INTMSK1);

	switch (irq) {
	case IRQ0_IRQ:
		msk0 &= ~0x0010;
		break;
	case IRQ1_IRQ:
		msk0 &= ~0x000f;
		break;
	case IRQ3_IRQ:
		msk0 &= ~0x0f00;
		msk1 &= ~0x00ff;
		break;
	}
	__raw_writew(msk0, INTMSK0);
	__raw_writew(msk1, INTMSK1);
}

static void eoi_se7206_irq(struct irq_data *data)
{
	unsigned short sts0,sts1;
	unsigned int irq = data->irq;

	if (!irqd_irq_disabled(data) && !irqd_irq_inprogress(data))
		enable_se7206_irq(data);
	/* FPGA isr clear */
	sts0 = __raw_readw(INTSTS0);
	sts1 = __raw_readw(INTSTS1);

	switch (irq) {
	case IRQ0_IRQ:
		sts0 &= ~0x0010;
		break;
	case IRQ1_IRQ:
		sts0 &= ~0x000f;
		break;
	case IRQ3_IRQ:
		sts0 &= ~0x0f00;
		sts1 &= ~0x00ff;
		break;
	}
	__raw_writew(sts0, INTSTS0);
	__raw_writew(sts1, INTSTS1);
}

static struct irq_chip se7206_irq_chip __read_mostly = {
	.name		= "SE7206-FPGA",
	.irq_mask	= disable_se7206_irq,
	.irq_unmask	= enable_se7206_irq,
	.irq_eoi	= eoi_se7206_irq,
};

static void make_se7206_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	irq_set_chip_and_handler_name(irq, &se7206_irq_chip,
				      handle_level_irq, "level");
	disable_se7206_irq(irq_get_irq_data(irq));
}

/*
 * Initialize IRQ setting
 */
void __init init_se7206_IRQ(void)
{
	make_se7206_irq(IRQ0_IRQ); /* SMC91C111 */
	make_se7206_irq(IRQ1_IRQ); /* ATA */
	make_se7206_irq(IRQ3_IRQ); /* SLOT / PCM */

	__raw_writew(__raw_readw(INTC_ICR1) | 0x000b, INTC_ICR1); /* ICR1 */

	/* FPGA System register setup*/
	__raw_writew(0x0000,INTSTS0); /* Clear INTSTS0 */
	__raw_writew(0x0000,INTSTS1); /* Clear INTSTS1 */

	/* IRQ0=LAN, IRQ1=ATA, IRQ3=SLT,PCM */
	__raw_writew(0x0001,INTSEL);
}
