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

int apollo_irq_startup(unsigned int irq)
{
	if (irq < 8)
		*(volatile unsigned char *)(pica+1) &= ~(1 << irq);
	else
		*(volatile unsigned char *)(picb+1) &= ~(1 << (irq - 8));
	return 0;
}

void apollo_irq_shutdown(unsigned int irq)
{
	if (irq < 8)
		*(volatile unsigned char *)(pica+1) |= (1 << irq);
	else
		*(volatile unsigned char *)(picb+1) |= (1 << (irq - 8));
}

static struct irq_controller apollo_irq_controller = {
	.name           = "apollo",
	.lock           = __SPIN_LOCK_UNLOCKED(apollo_irq_controller.lock),
	.startup        = apollo_irq_startup,
	.shutdown       = apollo_irq_shutdown,
};


void dn_init_IRQ(void)
{
	m68k_setup_user_interrupt(VEC_USER + 96, 16, dn_process_int);
	m68k_setup_irq_controller(&apollo_irq_controller, IRQ_APOLLO, 16);
}
