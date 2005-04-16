/*
 * arch/ppc/platforms/adir_pic.c
 *
 * Interrupt controller support for SBS Adirondack
 *
 * By Michael Sokolov <msokolov@ivan.Harhan.ORG>
 * based on the K2 and SCM versions by Matt Porter <mporter@mvista.com>
 */

#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/i8259.h>
#include "adir.h"

static void adir_onboard_pic_enable(unsigned int irq);
static void adir_onboard_pic_disable(unsigned int irq);

__init static void
adir_onboard_pic_init(void)
{
	volatile u_short *maskreg = (volatile u_short *) ADIR_PROCA_INT_MASK;

	/* Disable all Adirondack onboard interrupts */
	out_be16(maskreg, 0xFFFF);
}

static int
adir_onboard_pic_get_irq(void)
{
	volatile u_short *statreg = (volatile u_short *) ADIR_PROCA_INT_STAT;
	int irq;
	u_short int_status, int_test;

	int_status = in_be16(statreg);
	for (irq = 0, int_test = 1; irq < 16; irq++, int_test <<= 1) {
		if (int_status & int_test)
			break;
	}

	if (irq == 16)
		return -1;

	return (irq+16);
}

static void
adir_onboard_pic_enable(unsigned int irq)
{
	volatile u_short *maskreg = (volatile u_short *) ADIR_PROCA_INT_MASK;

	/* Change irq to Adirondack onboard native value */
	irq -= 16;

	/* Enable requested irq number */
	out_be16(maskreg, in_be16(maskreg) & ~(1 << irq));
}

static void
adir_onboard_pic_disable(unsigned int irq)
{
	volatile u_short *maskreg = (volatile u_short *) ADIR_PROCA_INT_MASK;

	/* Change irq to Adirondack onboard native value */
	irq -= 16;

	/* Disable requested irq number */
	out_be16(maskreg, in_be16(maskreg) | (1 << irq));
}

static struct hw_interrupt_type adir_onboard_pic = {
	" ADIR PIC ",
	NULL,
	NULL,
	adir_onboard_pic_enable,		/* unmask */
	adir_onboard_pic_disable,		/* mask */
	adir_onboard_pic_disable,		/* mask and ack */
	NULL,
	NULL
};

static struct irqaction noop_action = {
	.handler	= no_action,
	.flags          = SA_INTERRUPT,
	.mask           = CPU_MASK_NONE,
	.name           = "82c59 primary cascade",
};

/*
 * Linux interrupt values are assigned as follows:
 *
 * 	0-15		VT82C686 8259 interrupts
 * 	16-31		Adirondack CPLD interrupts
 */
__init void
adir_init_IRQ(void)
{
	int	i;

	/* Initialize the cascaded 8259's on the VT82C686 */
	for (i=0; i<16; i++)
		irq_desc[i].handler = &i8259_pic;
	i8259_init(NULL);

	/* Initialize Adirondack CPLD PIC and enable 8259 interrupt cascade */
	for (i=16; i<32; i++)
		irq_desc[i].handler = &adir_onboard_pic;
	adir_onboard_pic_init();

	/* Enable 8259 interrupt cascade */
	setup_irq(ADIR_IRQ_VT82C686_INTR, &noop_action);
}

int
adir_get_irq(struct pt_regs *regs)
{
	int	irq;

	if ((irq = adir_onboard_pic_get_irq()) < 0)
		return irq;

	if (irq == ADIR_IRQ_VT82C686_INTR)
		irq = i8259_irq(regs);

	return irq;
}
