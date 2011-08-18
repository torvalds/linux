#include <linux/interrupt.h>
#ifdef CONFIG_GENERIC_HARDIRQS
#include <linux/irq.h>
#else
#include <asm/irq.h>
#endif

#include <asm/traps.h>
#include <asm/apollohw.h>

#ifndef CONFIG_GENERIC_HARDIRQS
void dn_process_int(unsigned int irq, struct pt_regs *fp)
{
	do_IRQ(irq, fp);

	*(volatile unsigned char *)(pica)=0x20;
	*(volatile unsigned char *)(picb)=0x20;
}
#endif

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

#ifdef CONFIG_GENERIC_HARDIRQS
void apollo_irq_eoi(struct irq_data *data)
{
	*(volatile unsigned char *)(pica) = 0x20;
	*(volatile unsigned char *)(picb) = 0x20;
}
#endif

static struct irq_chip apollo_irq_chip = {
	.name           = "apollo",
	.irq_startup    = apollo_irq_startup,
	.irq_shutdown   = apollo_irq_shutdown,
#ifdef CONFIG_GENERIC_HARDIRQS
	.irq_eoi	= apollo_irq_eoi,
#endif
};


void __init dn_init_IRQ(void)
{
#ifdef CONFIG_GENERIC_HARDIRQS
	m68k_setup_user_interrupt(VEC_USER + 96, 16, NULL);
#else
	m68k_setup_user_interrupt(VEC_USER + 96, 16, dn_process_int);
#endif
	m68k_setup_irq_controller(&apollo_irq_chip, handle_fasteoi_irq,
				  IRQ_APOLLO, 16);
}
