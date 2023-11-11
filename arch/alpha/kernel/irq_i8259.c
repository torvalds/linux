// SPDX-License-Identifier: GPL-2.0
/*
 *      linux/arch/alpha/kernel/irq_i8259.c
 *
 * This is the 'legacy' 8259A Programmable Interrupt Controller,
 * present in the majority of PC/AT boxes.
 *
 * Started hacking from linux-2.3.30pre6/arch/i386/kernel/i8259.c.
 */

#include <linux/init.h>
#include <linux/cache.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/io.h>

#include "proto.h"
#include "irq_impl.h"


/* Note mask bit is true for DISABLED irqs.  */
static unsigned int cached_irq_mask = 0xffff;
static DEFINE_SPINLOCK(i8259_irq_lock);

static inline void
i8259_update_irq_hw(unsigned int irq, unsigned long mask)
{
	int port = 0x21;
	if (irq & 8) mask >>= 8;
	if (irq & 8) port = 0xA1;
	outb(mask, port);
}

inline void
i8259a_enable_irq(struct irq_data *d)
{
	spin_lock(&i8259_irq_lock);
	i8259_update_irq_hw(d->irq, cached_irq_mask &= ~(1 << d->irq));
	spin_unlock(&i8259_irq_lock);
}

static inline void
__i8259a_disable_irq(unsigned int irq)
{
	i8259_update_irq_hw(irq, cached_irq_mask |= 1 << irq);
}

void
i8259a_disable_irq(struct irq_data *d)
{
	spin_lock(&i8259_irq_lock);
	__i8259a_disable_irq(d->irq);
	spin_unlock(&i8259_irq_lock);
}

void
i8259a_mask_and_ack_irq(struct irq_data *d)
{
	unsigned int irq = d->irq;

	spin_lock(&i8259_irq_lock);
	__i8259a_disable_irq(irq);

	/* Ack the interrupt making it the lowest priority.  */
	if (irq >= 8) {
		outb(0xE0 | (irq - 8), 0xa0);   /* ack the slave */
		irq = 2;
	}
	outb(0xE0 | irq, 0x20);			/* ack the master */
	spin_unlock(&i8259_irq_lock);
}

struct irq_chip i8259a_irq_type = {
	.name		= "XT-PIC",
	.irq_unmask	= i8259a_enable_irq,
	.irq_mask	= i8259a_disable_irq,
	.irq_mask_ack	= i8259a_mask_and_ack_irq,
};

void __init
init_i8259a_irqs(void)
{
	long i;

	outb(0xff, 0x21);	/* mask all of 8259A-1 */
	outb(0xff, 0xA1);	/* mask all of 8259A-2 */

	for (i = 0; i < 16; i++) {
		irq_set_chip_and_handler(i, &i8259a_irq_type, handle_level_irq);
	}

	if (request_irq(2, no_action, 0, "cascade", NULL))
		pr_err("Failed to request irq 2 (cascade)\n");
}


#if defined(CONFIG_ALPHA_GENERIC)
# define IACK_SC	alpha_mv.iack_sc
#elif defined(CONFIG_ALPHA_APECS)
# define IACK_SC	APECS_IACK_SC
#elif defined(CONFIG_ALPHA_LCA)
# define IACK_SC	LCA_IACK_SC
#elif defined(CONFIG_ALPHA_CIA)
# define IACK_SC	CIA_IACK_SC
#elif defined(CONFIG_ALPHA_PYXIS)
# define IACK_SC	PYXIS_IACK_SC
#elif defined(CONFIG_ALPHA_TITAN)
# define IACK_SC	TITAN_IACK_SC
#elif defined(CONFIG_ALPHA_TSUNAMI)
# define IACK_SC	TSUNAMI_IACK_SC
#elif defined(CONFIG_ALPHA_IRONGATE)
# define IACK_SC        IRONGATE_IACK_SC
#endif
/* Note that CONFIG_ALPHA_POLARIS is intentionally left out here, since
   sys_rx164 wants to use isa_no_iack_sc_device_interrupt for some reason.  */

#if defined(IACK_SC)
void
isa_device_interrupt(unsigned long vector)
{
	/*
	 * Generate a PCI interrupt acknowledge cycle.  The PIC will
	 * respond with the interrupt vector of the highest priority
	 * interrupt that is pending.  The PALcode sets up the
	 * interrupts vectors such that irq level L generates vector L.
	 */
	int j = *(vuip) IACK_SC;
	j &= 0xff;
	handle_irq(j);
}
#endif

#if defined(CONFIG_ALPHA_GENERIC) || !defined(IACK_SC)
void
isa_no_iack_sc_device_interrupt(unsigned long vector)
{
	unsigned long pic;

	/*
	 * It seems to me that the probability of two or more *device*
	 * interrupts occurring at almost exactly the same time is
	 * pretty low.  So why pay the price of checking for
	 * additional interrupts here if the common case can be
	 * handled so much easier?
	 */
	/* 
	 *  The first read of gives you *all* interrupting lines.
	 *  Therefore, read the mask register and and out those lines
	 *  not enabled.  Note that some documentation has 21 and a1 
	 *  write only.  This is not true.
	 */
	pic = inb(0x20) | (inb(0xA0) << 8);	/* read isr */
	pic &= 0xFFFB;				/* mask out cascade & hibits */

	while (pic) {
		int j = ffz(~pic);
		pic &= pic - 1;
		handle_irq(j);
	}
}
#endif
