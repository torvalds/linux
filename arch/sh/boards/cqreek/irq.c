/* $Id: irq.c,v 1.1.2.4 2002/11/04 20:33:56 lethal Exp $
 *
 * arch/sh/boards/cqreek/irq.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 *
 * CqREEK IDE/ISA Bridge Support.
 *
 */

#include <linux/irq.h>
#include <linux/init.h>

#include <asm/cqreek/cqreek.h>
#include <asm/io.h>
#include <asm/io_generic.h>
#include <asm/irq.h>
#include <asm/machvec.h>
#include <asm/machvec_init.h>
#include <asm/rtc.h>

struct cqreek_irq_data {
	unsigned short mask_port;	/* Port of Interrupt Mask Register */
	unsigned short stat_port;	/* Port of Interrupt Status Register */
	unsigned short bit;		/* Value of the bit */
};
static struct cqreek_irq_data cqreek_irq_data[NR_IRQS];

static void disable_cqreek_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned short mask;
	unsigned short mask_port = cqreek_irq_data[irq].mask_port;
	unsigned short bit = cqreek_irq_data[irq].bit;

	local_irq_save(flags);
	/* Disable IRQ */
	mask = inw(mask_port) & ~bit;
	outw_p(mask, mask_port);
	local_irq_restore(flags);
}

static void enable_cqreek_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned short mask;
	unsigned short mask_port = cqreek_irq_data[irq].mask_port;
	unsigned short bit = cqreek_irq_data[irq].bit;

	local_irq_save(flags);
	/* Enable IRQ */
	mask = inw(mask_port) | bit;
	outw_p(mask, mask_port);
	local_irq_restore(flags);
}

static void mask_and_ack_cqreek(unsigned int irq)
{
	unsigned short stat_port = cqreek_irq_data[irq].stat_port;
	unsigned short bit = cqreek_irq_data[irq].bit;

	disable_cqreek_irq(irq);
	/* Clear IRQ (it might be edge IRQ) */
	inw(stat_port);
	outw_p(bit, stat_port);
}

static void end_cqreek_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		enable_cqreek_irq(irq);
}

static unsigned int startup_cqreek_irq(unsigned int irq)
{ 
	enable_cqreek_irq(irq);
	return 0;
}

static void shutdown_cqreek_irq(unsigned int irq)
{
	disable_cqreek_irq(irq);
}

static struct hw_interrupt_type cqreek_irq_type = {
	.typename = "CqREEK-IRQ",
	.startup = startup_cqreek_irq,
	.shutdown = shutdown_cqreek_irq,
	.enable = enable_cqreek_irq,
	.disable = disable_cqreek_irq,
	.ack = mask_and_ack_cqreek,
	.end = end_cqreek_irq
};

int cqreek_has_ide, cqreek_has_isa;

/* XXX: This is just for test for my NE2000 ISA board
   What we really need is virtualized IRQ and demultiplexer like HP600 port */
void __init init_cqreek_IRQ(void)
{
	if (cqreek_has_ide) {
		cqreek_irq_data[14].mask_port = BRIDGE_IDE_INTR_MASK;
		cqreek_irq_data[14].stat_port = BRIDGE_IDE_INTR_STAT;
		cqreek_irq_data[14].bit = 1;

		irq_desc[14].handler = &cqreek_irq_type;
		irq_desc[14].status = IRQ_DISABLED;
		irq_desc[14].action = 0;
		irq_desc[14].depth = 1;

		disable_cqreek_irq(14);
	}

	if (cqreek_has_isa) {
		cqreek_irq_data[10].mask_port = BRIDGE_ISA_INTR_MASK;
		cqreek_irq_data[10].stat_port = BRIDGE_ISA_INTR_STAT;
		cqreek_irq_data[10].bit = (1 << 10);

		/* XXX: Err... we may need demultiplexer for ISA irq... */
		irq_desc[10].handler = &cqreek_irq_type;
		irq_desc[10].status = IRQ_DISABLED;
		irq_desc[10].action = 0;
		irq_desc[10].depth = 1;

		disable_cqreek_irq(10);
	}
}

