// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2003-2005 Simtec Electronics
//   Ben Dooks <ben@simtec.co.uk>
//
// http://www.simtec.co.uk/products/EB2410ITX/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/mach/irq.h>

#include "regs-irq.h"
#include "irqs.h"

#include "bast.h"

#define irqdbf(x...)
#define irqdbf2(x...)

/* handle PC104 ISA interrupts from the system CPLD */

/* table of ISA irq nos to the relevant mask... zero means
 * the irq is not implemented
*/
static const unsigned char bast_pc104_irqmasks[] = {
	0,   /* 0 */
	0,   /* 1 */
	0,   /* 2 */
	1,   /* 3 */
	0,   /* 4 */
	2,   /* 5 */
	0,   /* 6 */
	4,   /* 7 */
	0,   /* 8 */
	0,   /* 9 */
	8,   /* 10 */
	0,   /* 11 */
	0,   /* 12 */
	0,   /* 13 */
	0,   /* 14 */
	0,   /* 15 */
};

static const unsigned char bast_pc104_irqs[] = { 3, 5, 7, 10 };

static void
bast_pc104_mask(struct irq_data *data)
{
	unsigned long temp;

	temp = __raw_readb(BAST_VA_PC104_IRQMASK);
	temp &= ~bast_pc104_irqmasks[data->irq];
	__raw_writeb(temp, BAST_VA_PC104_IRQMASK);
}

static void
bast_pc104_maskack(struct irq_data *data)
{
	struct irq_desc *desc = irq_to_desc(BAST_IRQ_ISA);

	bast_pc104_mask(data);
	desc->irq_data.chip->irq_ack(&desc->irq_data);
}

static void
bast_pc104_unmask(struct irq_data *data)
{
	unsigned long temp;

	temp = __raw_readb(BAST_VA_PC104_IRQMASK);
	temp |= bast_pc104_irqmasks[data->irq];
	__raw_writeb(temp, BAST_VA_PC104_IRQMASK);
}

static struct irq_chip  bast_pc104_chip = {
	.irq_mask	= bast_pc104_mask,
	.irq_unmask	= bast_pc104_unmask,
	.irq_ack	= bast_pc104_maskack
};

static void bast_irq_pc104_demux(struct irq_desc *desc)
{
	unsigned int stat;
	unsigned int irqno;
	int i;

	stat = __raw_readb(BAST_VA_PC104_IRQREQ) & 0xf;

	if (unlikely(stat == 0)) {
		/* ack if we get an irq with nothing (ie, startup) */
		desc->irq_data.chip->irq_ack(&desc->irq_data);
	} else {
		/* handle the IRQ */

		for (i = 0; stat != 0; i++, stat >>= 1) {
			if (stat & 1) {
				irqno = bast_pc104_irqs[i];
				generic_handle_irq(irqno);
			}
		}
	}
}

static __init int bast_irq_init(void)
{
	unsigned int i;

	if (machine_is_bast()) {
		printk(KERN_INFO "BAST PC104 IRQ routing, Copyright 2005 Simtec Electronics\n");

		/* zap all the IRQs */

		__raw_writeb(0x0, BAST_VA_PC104_IRQMASK);

		irq_set_chained_handler(BAST_IRQ_ISA, bast_irq_pc104_demux);

		/* register our IRQs */

		for (i = 0; i < 4; i++) {
			unsigned int irqno = bast_pc104_irqs[i];

			irq_set_chip_and_handler(irqno, &bast_pc104_chip,
						 handle_level_irq);
			irq_clear_status_flags(irqno, IRQ_NOREQUEST);
		}
	}

	return 0;
}

arch_initcall(bast_irq_init);
