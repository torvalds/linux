// SPDX-License-Identifier: GPL-2.0
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/traps.h>
#include <asm/apollohw.h>

unsigned int apollo_irq_startup(struct irq_data *data)
{
	unsigned int irq = data->irq;

	if (irq < 8)
		*(volatile unsigned char *)(pica+1) &= ~(1 << irq);
	else
		*(volatile unsigned char *)(picb+1) &= ~(1 << (irq - 8));
	return 0;
}

void apollo_irq_shutdown(struct irq_data *data)
{
	unsigned int irq = data->irq;

	if (irq < 8)
		*(volatile unsigned char *)(pica+1) |= (1 << irq);
	else
		*(volatile unsigned char *)(picb+1) |= (1 << (irq - 8));
}

void apollo_irq_eoi(struct irq_data *data)
{
	*(volatile unsigned char *)(pica) = 0x20;
	*(volatile unsigned char *)(picb) = 0x20;
}

static struct irq_chip apollo_irq_chip = {
	.name           = "apollo",
	.irq_startup    = apollo_irq_startup,
	.irq_shutdown   = apollo_irq_shutdown,
	.irq_eoi	= apollo_irq_eoi,
};


void __init dn_init_IRQ(void)
{
	m68k_setup_user_interrupt(VEC_USER + 96, 16);
	m68k_setup_irq_controller(&apollo_irq_chip, handle_fasteoi_irq,
				  IRQ_APOLLO, 16);
}
