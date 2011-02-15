/*
 * Handle interrupts from the SRM, assuming no additional weirdness.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/irq.h>

#include "proto.h"
#include "irq_impl.h"


/*
 * Is the palcode SMP safe? In other words: can we call cserve_ena/dis
 * at the same time in multiple CPUs? To be safe I added a spinlock
 * but it can be removed trivially if the palcode is robust against smp.
 */
DEFINE_SPINLOCK(srm_irq_lock);

static inline void
srm_enable_irq(unsigned int irq)
{
	spin_lock(&srm_irq_lock);
	cserve_ena(irq - 16);
	spin_unlock(&srm_irq_lock);
}

static void
srm_disable_irq(unsigned int irq)
{
	spin_lock(&srm_irq_lock);
	cserve_dis(irq - 16);
	spin_unlock(&srm_irq_lock);
}

/* Handle interrupts from the SRM, assuming no additional weirdness.  */
static struct irq_chip srm_irq_type = {
	.name		= "SRM",
	.unmask		= srm_enable_irq,
	.mask		= srm_disable_irq,
	.mask_ack	= srm_disable_irq,
};

void __init
init_srm_irqs(long max, unsigned long ignore_mask)
{
	long i;

	if (NR_IRQS <= 16)
		return;
	for (i = 16; i < max; ++i) {
		if (i < 64 && ((ignore_mask >> i) & 1))
			continue;
		set_irq_chip_and_handler(i, &srm_irq_type, handle_level_irq);
		irq_to_desc(i)->status |= IRQ_LEVEL;
	}
}

void 
srm_device_interrupt(unsigned long vector)
{
	int irq = (vector - 0x800) >> 4;
	handle_irq(irq);
}
