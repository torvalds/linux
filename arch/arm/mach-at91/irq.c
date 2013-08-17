/*
 * linux/arch/arm/mach-at91/irq.c
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
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <linux/err.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

void __iomem *at91_aic_base;
static struct irq_domain *at91_aic_domain;
static struct device_node *at91_aic_np;

static void at91_aic_mask_irq(struct irq_data *d)
{
	/* Disable interrupt on AIC */
	at91_aic_write(AT91_AIC_IDCR, 1 << d->hwirq);
}

static void at91_aic_unmask_irq(struct irq_data *d)
{
	/* Enable interrupt on AIC */
	at91_aic_write(AT91_AIC_IECR, 1 << d->hwirq);
}

unsigned int at91_extern_irq;

#define is_extern_irq(hwirq) ((1 << (hwirq)) & at91_extern_irq)

static int at91_aic_set_type(struct irq_data *d, unsigned type)
{
	unsigned int smr, srctype;

	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
		srctype = AT91_AIC_SRCTYPE_HIGH;
		break;
	case IRQ_TYPE_EDGE_RISING:
		srctype = AT91_AIC_SRCTYPE_RISING;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		if ((d->hwirq == AT91_ID_FIQ) || is_extern_irq(d->hwirq))		/* only supported on external interrupts */
			srctype = AT91_AIC_SRCTYPE_LOW;
		else
			return -EINVAL;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		if ((d->hwirq == AT91_ID_FIQ) || is_extern_irq(d->hwirq))		/* only supported on external interrupts */
			srctype = AT91_AIC_SRCTYPE_FALLING;
		else
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	smr = at91_aic_read(AT91_AIC_SMR(d->hwirq)) & ~AT91_AIC_SRCTYPE;
	at91_aic_write(AT91_AIC_SMR(d->hwirq), smr | srctype);
	return 0;
}

#ifdef CONFIG_PM

static u32 wakeups;
static u32 backups;

static int at91_aic_set_wake(struct irq_data *d, unsigned value)
{
	if (unlikely(d->hwirq >= NR_AIC_IRQS))
		return -EINVAL;

	if (value)
		wakeups |= (1 << d->hwirq);
	else
		wakeups &= ~(1 << d->hwirq);

	return 0;
}

void at91_irq_suspend(void)
{
	backups = at91_aic_read(AT91_AIC_IMR);
	at91_aic_write(AT91_AIC_IDCR, backups);
	at91_aic_write(AT91_AIC_IECR, wakeups);
}

void at91_irq_resume(void)
{
	at91_aic_write(AT91_AIC_IDCR, wakeups);
	at91_aic_write(AT91_AIC_IECR, backups);
}

#else
#define at91_aic_set_wake	NULL
#endif

static struct irq_chip at91_aic_chip = {
	.name		= "AIC",
	.irq_ack	= at91_aic_mask_irq,
	.irq_mask	= at91_aic_mask_irq,
	.irq_unmask	= at91_aic_unmask_irq,
	.irq_set_type	= at91_aic_set_type,
	.irq_set_wake	= at91_aic_set_wake,
};

static void __init at91_aic_hw_init(unsigned int spu_vector)
{
	int i;

	/*
	 * Perform 8 End Of Interrupt Command to make sure AIC
	 * will not Lock out nIRQ
	 */
	for (i = 0; i < 8; i++)
		at91_aic_write(AT91_AIC_EOICR, 0);

	/*
	 * Spurious Interrupt ID in Spurious Vector Register.
	 * When there is no current interrupt, the IRQ Vector Register
	 * reads the value stored in AIC_SPU
	 */
	at91_aic_write(AT91_AIC_SPU, spu_vector);

	/* No debugging in AIC: Debug (Protect) Control Register */
	at91_aic_write(AT91_AIC_DCR, 0);

	/* Disable and clear all interrupts initially */
	at91_aic_write(AT91_AIC_IDCR, 0xFFFFFFFF);
	at91_aic_write(AT91_AIC_ICCR, 0xFFFFFFFF);
}

#if defined(CONFIG_OF)
static int at91_aic_irq_map(struct irq_domain *h, unsigned int virq,
							irq_hw_number_t hw)
{
	/* Put virq number in Source Vector Register */
	at91_aic_write(AT91_AIC_SVR(hw), virq);

	/* Active Low interrupt, without priority */
	at91_aic_write(AT91_AIC_SMR(hw), AT91_AIC_SRCTYPE_LOW);

	irq_set_chip_and_handler(virq, &at91_aic_chip, handle_level_irq);
	set_irq_flags(virq, IRQF_VALID | IRQF_PROBE);

	return 0;
}

static struct irq_domain_ops at91_aic_irq_ops = {
	.map	= at91_aic_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

int __init at91_aic_of_init(struct device_node *node,
				     struct device_node *parent)
{
	at91_aic_base = of_iomap(node, 0);
	at91_aic_np = node;

	at91_aic_domain = irq_domain_add_linear(at91_aic_np, NR_AIC_IRQS,
						&at91_aic_irq_ops, NULL);
	if (!at91_aic_domain)
		panic("Unable to add AIC irq domain (DT)\n");

	irq_set_default_host(at91_aic_domain);

	at91_aic_hw_init(NR_AIC_IRQS);

	return 0;
}
#endif

/*
 * Initialize the AIC interrupt controller.
 */
void __init at91_aic_init(unsigned int priority[NR_AIC_IRQS])
{
	unsigned int i;
	int irq_base;

	at91_aic_base = ioremap(AT91_AIC, 512);
	if (!at91_aic_base)
		panic("Unable to ioremap AIC registers\n");

	/* Add irq domain for AIC */
	irq_base = irq_alloc_descs(-1, 0, NR_AIC_IRQS, 0);
	if (irq_base < 0) {
		WARN(1, "Cannot allocate irq_descs, assuming pre-allocated\n");
		irq_base = 0;
	}
	at91_aic_domain = irq_domain_add_legacy(at91_aic_np, NR_AIC_IRQS,
						irq_base, 0,
						&irq_domain_simple_ops, NULL);

	if (!at91_aic_domain)
		panic("Unable to add AIC irq domain\n");

	irq_set_default_host(at91_aic_domain);

	/*
	 * The IVR is used by macro get_irqnr_and_base to read and verify.
	 * The irq number is NR_AIC_IRQS when a spurious interrupt has occurred.
	 */
	for (i = 0; i < NR_AIC_IRQS; i++) {
		/* Put hardware irq number in Source Vector Register: */
		at91_aic_write(AT91_AIC_SVR(i), i);
		/* Active Low interrupt, with the specified priority */
		at91_aic_write(AT91_AIC_SMR(i), AT91_AIC_SRCTYPE_LOW | priority[i]);

		irq_set_chip_and_handler(i, &at91_aic_chip, handle_level_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}

	at91_aic_hw_init(NR_AIC_IRQS);
}
