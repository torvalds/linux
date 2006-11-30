/*
 * linux/arch/arm/mach-at91rm9200/irq.c
 *
 *  Copyright (C) 2004 SAN People
 *  Copyright (C) 2004 ATMEL
 *  Copyright (C) Rick Bronson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>


static void at91_aic_mask_irq(unsigned int irq)
{
	/* Disable interrupt on AIC */
	at91_sys_write(AT91_AIC_IDCR, 1 << irq);
}

static void at91_aic_unmask_irq(unsigned int irq)
{
	/* Enable interrupt on AIC */
	at91_sys_write(AT91_AIC_IECR, 1 << irq);
}

unsigned int at91_extern_irq;

#define is_extern_irq(irq) ((1 << (irq)) & at91_extern_irq)

static int at91_aic_set_type(unsigned irq, unsigned type)
{
	unsigned int smr, srctype;

	switch (type) {
	case IRQT_HIGH:
		srctype = AT91_AIC_SRCTYPE_HIGH;
		break;
	case IRQT_RISING:
		srctype = AT91_AIC_SRCTYPE_RISING;
		break;
	case IRQT_LOW:
		if ((irq == AT91_ID_FIQ) || is_extern_irq(irq))		/* only supported on external interrupts */
			srctype = AT91_AIC_SRCTYPE_LOW;
		else
			return -EINVAL;
		break;
	case IRQT_FALLING:
		if ((irq == AT91_ID_FIQ) || is_extern_irq(irq))		/* only supported on external interrupts */
			srctype = AT91_AIC_SRCTYPE_FALLING;
		else
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	smr = at91_sys_read(AT91_AIC_SMR(irq)) & ~AT91_AIC_SRCTYPE;
	at91_sys_write(AT91_AIC_SMR(irq), smr | srctype);
	return 0;
}

#ifdef CONFIG_PM

static u32 wakeups;
static u32 backups;

static int at91_aic_set_wake(unsigned irq, unsigned value)
{
	if (unlikely(irq >= 32))
		return -EINVAL;

	if (value)
		wakeups |= (1 << irq);
	else
		wakeups &= ~(1 << irq);

	return 0;
}

void at91_irq_suspend(void)
{
	backups = at91_sys_read(AT91_AIC_IMR);
	at91_sys_write(AT91_AIC_IDCR, backups);
	at91_sys_write(AT91_AIC_IECR, wakeups);
}

void at91_irq_resume(void)
{
	at91_sys_write(AT91_AIC_IDCR, wakeups);
	at91_sys_write(AT91_AIC_IECR, backups);
}

#else
#define at91_aic_set_wake	NULL
#endif

static struct irq_chip at91_aic_chip = {
	.name		= "AIC",
	.ack		= at91_aic_mask_irq,
	.mask		= at91_aic_mask_irq,
	.unmask		= at91_aic_unmask_irq,
	.set_type	= at91_aic_set_type,
	.set_wake	= at91_aic_set_wake,
};

/*
 * Initialize the AIC interrupt controller.
 */
void __init at91_aic_init(unsigned int priority[NR_AIC_IRQS])
{
	unsigned int i;

	/*
	 * The IVR is used by macro get_irqnr_and_base to read and verify.
	 * The irq number is NR_AIC_IRQS when a spurious interrupt has occurred.
	 */
	for (i = 0; i < NR_AIC_IRQS; i++) {
		/* Put irq number in Source Vector Register: */
		at91_sys_write(AT91_AIC_SVR(i), i);
		/* Active Low interrupt, with the specified priority */
		at91_sys_write(AT91_AIC_SMR(i), AT91_AIC_SRCTYPE_LOW | priority[i]);

		set_irq_chip(i, &at91_aic_chip);
		set_irq_handler(i, do_level_IRQ);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);

		/* Perform 8 End Of Interrupt Command to make sure AIC will not Lock out nIRQ */
		if (i < 8)
			at91_sys_write(AT91_AIC_EOICR, 0);
	}

	/*
	 * Spurious Interrupt ID in Spurious Vector Register is NR_AIC_IRQS
	 * When there is no current interrupt, the IRQ Vector Register reads the value stored in AIC_SPU
	 */
	at91_sys_write(AT91_AIC_SPU, NR_AIC_IRQS);

	/* No debugging in AIC: Debug (Protect) Control Register */
	at91_sys_write(AT91_AIC_DCR, 0);

	/* Disable and clear all interrupts initially */
	at91_sys_write(AT91_AIC_IDCR, 0xFFFFFFFF);
	at91_sys_write(AT91_AIC_ICCR, 0xFFFFFFFF);
}
