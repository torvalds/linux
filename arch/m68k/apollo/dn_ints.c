#include <linux/interrupt.h>

#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/apollohw.h>

void dn_process_int(unsigned int irq, struct pt_regs *fp)
{
	__m68k_handle_int(irq, fp);

	*(volatile unsigned char *)(pica)=0x20;
	*(volatile unsigned char *)(picb)=0x20;
}

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

static struct irq_chip apollo_irq_chip = {
	.name           = "apollo",
	.irq_startup    = apollo_irq_startup,
	.irq_shutdown   = apollo_irq_shutdown,
};


void __init dn_init_IRQ(void)
{
	m68k_setup_user_interrupt(VEC_USER + 96, 16, dn_process_int);
	m68k_setup_irq_chip(&apollo_irq_chip, IRQ_APOLLO, 16);
}
