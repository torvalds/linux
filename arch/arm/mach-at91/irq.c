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
#include <linux/bitmap.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/setup.h>

#include <asm/exception.h>
#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include <mach/at91_aic.h>

void __iomem *at91_aic_base;
static struct irq_domain *at91_aic_domain;
static struct device_node *at91_aic_np;
static unsigned int n_irqs = NR_AIC_IRQS;
static unsigned long at91_aic_caps = 0;

/* AIC5 introduces a Source Select Register */
#define AT91_AIC_CAP_AIC5	(1 << 0)
#define has_aic5()		(at91_aic_caps & AT91_AIC_CAP_AIC5)

#ifdef CONFIG_PM

static unsigned long *wakeups;
static unsigned long *backups;

#define set_backup(bit) set_bit(bit, backups)
#define clear_backup(bit) clear_bit(bit, backups)

static int at91_aic_pm_init(void)
{
	backups = kzalloc(BITS_TO_LONGS(n_irqs) * sizeof(*backups), GFP_KERNEL);
	if (!backups)
		return -ENOMEM;

	wakeups = kzalloc(BITS_TO_LONGS(n_irqs) * sizeof(*backups), GFP_KERNEL);
	if (!wakeups) {
		kfree(backups);
		return -ENOMEM;
	}

	return 0;
}

static int at91_aic_set_wake(struct irq_data *d, unsigned value)
{
	if (unlikely(d->hwirq >= n_irqs))
		return -EINVAL;

	if (value)
		set_bit(d->hwirq, wakeups);
	else
		clear_bit(d->hwirq, wakeups);

	return 0;
}

void at91_irq_suspend(void)
{
	int i = 0, bit;

	if (has_aic5()) {
		/* disable enabled irqs */
		while ((bit = find_next_bit(backups, n_irqs, i)) < n_irqs) {
			at91_aic_write(AT91_AIC5_SSR,
				       bit & AT91_AIC5_INTSEL_MSK);
			at91_aic_write(AT91_AIC5_IDCR, 1);
			i = bit;
		}
		/* enable wakeup irqs */
		i = 0;
		while ((bit = find_next_bit(wakeups, n_irqs, i)) < n_irqs) {
			at91_aic_write(AT91_AIC5_SSR,
				       bit & AT91_AIC5_INTSEL_MSK);
			at91_aic_write(AT91_AIC5_IECR, 1);
			i = bit;
		}
	} else {
		at91_aic_write(AT91_AIC_IDCR, *backups);
		at91_aic_write(AT91_AIC_IECR, *wakeups);
	}
}

void at91_irq_resume(void)
{
	int i = 0, bit;

	if (has_aic5()) {
		/* disable wakeup irqs */
		while ((bit = find_next_bit(wakeups, n_irqs, i)) < n_irqs) {
			at91_aic_write(AT91_AIC5_SSR,
				       bit & AT91_AIC5_INTSEL_MSK);
			at91_aic_write(AT91_AIC5_IDCR, 1);
			i = bit;
		}
		/* enable irqs disabled for suspend */
		i = 0;
		while ((bit = find_next_bit(backups, n_irqs, i)) < n_irqs) {
			at91_aic_write(AT91_AIC5_SSR,
				       bit & AT91_AIC5_INTSEL_MSK);
			at91_aic_write(AT91_AIC5_IECR, 1);
			i = bit;
		}
	} else {
		at91_aic_write(AT91_AIC_IDCR, *wakeups);
		at91_aic_write(AT91_AIC_IECR, *backups);
	}
}

#else
static inline int at91_aic_pm_init(void)
{
	return 0;
}

#define set_backup(bit)
#define clear_backup(bit)
#define at91_aic_set_wake	NULL

#endif /* CONFIG_PM */

asmlinkage void __exception_irq_entry
at91_aic_handle_irq(struct pt_regs *regs)
{
	u32 irqnr;
	u32 irqstat;

	irqnr = at91_aic_read(AT91_AIC_IVR);
	irqstat = at91_aic_read(AT91_AIC_ISR);

	/*
	 * ISR value is 0 when there is no current interrupt or when there is
	 * a spurious interrupt
	 */
	if (!irqstat)
		at91_aic_write(AT91_AIC_EOICR, 0);
	else
		handle_IRQ(irqnr, regs);
}

asmlinkage void __exception_irq_entry
at91_aic5_handle_irq(struct pt_regs *regs)
{
	u32 irqnr;
	u32 irqstat;

	irqnr = at91_aic_read(AT91_AIC5_IVR);
	irqstat = at91_aic_read(AT91_AIC5_ISR);

	if (!irqstat)
		at91_aic_write(AT91_AIC5_EOICR, 0);
	else
		handle_IRQ(irqnr, regs);
}

static void at91_aic_mask_irq(struct irq_data *d)
{
	/* Disable interrupt on AIC */
	at91_aic_write(AT91_AIC_IDCR, 1 << d->hwirq);
	/* Update ISR cache */
	clear_backup(d->hwirq);
}

static void __maybe_unused at91_aic5_mask_irq(struct irq_data *d)
{
	/* Disable interrupt on AIC5 */
	at91_aic_write(AT91_AIC5_SSR, d->hwirq & AT91_AIC5_INTSEL_MSK);
	at91_aic_write(AT91_AIC5_IDCR, 1);
	/* Update ISR cache */
	clear_backup(d->hwirq);
}

static void at91_aic_unmask_irq(struct irq_data *d)
{
	/* Enable interrupt on AIC */
	at91_aic_write(AT91_AIC_IECR, 1 << d->hwirq);
	/* Update ISR cache */
	set_backup(d->hwirq);
}

static void __maybe_unused at91_aic5_unmask_irq(struct irq_data *d)
{
	/* Enable interrupt on AIC5 */
	at91_aic_write(AT91_AIC5_SSR, d->hwirq & AT91_AIC5_INTSEL_MSK);
	at91_aic_write(AT91_AIC5_IECR, 1);
	/* Update ISR cache */
	set_backup(d->hwirq);
}

static void at91_aic_eoi(struct irq_data *d)
{
	/*
	 * Mark end-of-interrupt on AIC, the controller doesn't care about
	 * the value written. Moreover it's a write-only register.
	 */
	at91_aic_write(AT91_AIC_EOICR, 0);
}

static void __maybe_unused at91_aic5_eoi(struct irq_data *d)
{
	at91_aic_write(AT91_AIC5_EOICR, 0);
}

unsigned long *at91_extern_irq;

#define is_extern_irq(hwirq) test_bit(hwirq, at91_extern_irq)

static int at91_aic_compute_srctype(struct irq_data *d, unsigned type)
{
	int srctype;

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
			srctype = -EINVAL;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		if ((d->hwirq == AT91_ID_FIQ) || is_extern_irq(d->hwirq))		/* only supported on external interrupts */
			srctype = AT91_AIC_SRCTYPE_FALLING;
		else
			srctype = -EINVAL;
		break;
	default:
		srctype = -EINVAL;
	}

	return srctype;
}

static int at91_aic_set_type(struct irq_data *d, unsigned type)
{
	unsigned int smr;
	int srctype;

	srctype = at91_aic_compute_srctype(d, type);
	if (srctype < 0)
		return srctype;

	if (has_aic5()) {
		at91_aic_write(AT91_AIC5_SSR,
			       d->hwirq & AT91_AIC5_INTSEL_MSK);
		smr = at91_aic_read(AT91_AIC5_SMR) & ~AT91_AIC_SRCTYPE;
		at91_aic_write(AT91_AIC5_SMR, smr | srctype);
	} else {
		smr = at91_aic_read(AT91_AIC_SMR(d->hwirq))
		      & ~AT91_AIC_SRCTYPE;
		at91_aic_write(AT91_AIC_SMR(d->hwirq), smr | srctype);
	}

	return 0;
}

static struct irq_chip at91_aic_chip = {
	.name		= "AIC",
	.irq_mask	= at91_aic_mask_irq,
	.irq_unmask	= at91_aic_unmask_irq,
	.irq_set_type	= at91_aic_set_type,
	.irq_set_wake	= at91_aic_set_wake,
	.irq_eoi	= at91_aic_eoi,
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

static void __init __maybe_unused at91_aic5_hw_init(unsigned int spu_vector)
{
	int i;

	/*
	 * Perform 8 End Of Interrupt Command to make sure AIC
	 * will not Lock out nIRQ
	 */
	for (i = 0; i < 8; i++)
		at91_aic_write(AT91_AIC5_EOICR, 0);

	/*
	 * Spurious Interrupt ID in Spurious Vector Register.
	 * When there is no current interrupt, the IRQ Vector Register
	 * reads the value stored in AIC_SPU
	 */
	at91_aic_write(AT91_AIC5_SPU, spu_vector);

	/* No debugging in AIC: Debug (Protect) Control Register */
	at91_aic_write(AT91_AIC5_DCR, 0);

	/* Disable and clear all interrupts initially */
	for (i = 0; i < n_irqs; i++) {
		at91_aic_write(AT91_AIC5_SSR, i & AT91_AIC5_INTSEL_MSK);
		at91_aic_write(AT91_AIC5_IDCR, 1);
		at91_aic_write(AT91_AIC5_ICCR, 1);
	}
}

#if defined(CONFIG_OF)
static unsigned int *at91_aic_irq_priorities;

static int at91_aic_irq_map(struct irq_domain *h, unsigned int virq,
							irq_hw_number_t hw)
{
	/* Put virq number in Source Vector Register */
	at91_aic_write(AT91_AIC_SVR(hw), virq);

	/* Active Low interrupt, with priority */
	at91_aic_write(AT91_AIC_SMR(hw),
		       AT91_AIC_SRCTYPE_LOW | at91_aic_irq_priorities[hw]);

	irq_set_chip_and_handler(virq, &at91_aic_chip, handle_fasteoi_irq);
	set_irq_flags(virq, IRQF_VALID | IRQF_PROBE);

	return 0;
}

static int at91_aic5_irq_map(struct irq_domain *h, unsigned int virq,
		irq_hw_number_t hw)
{
	at91_aic_write(AT91_AIC5_SSR, hw & AT91_AIC5_INTSEL_MSK);

	/* Put virq number in Source Vector Register */
	at91_aic_write(AT91_AIC5_SVR, virq);

	/* Active Low interrupt, with priority */
	at91_aic_write(AT91_AIC5_SMR,
		       AT91_AIC_SRCTYPE_LOW | at91_aic_irq_priorities[hw]);

	irq_set_chip_and_handler(virq, &at91_aic_chip, handle_fasteoi_irq);
	set_irq_flags(virq, IRQF_VALID | IRQF_PROBE);

	return 0;
}

static int at91_aic_irq_domain_xlate(struct irq_domain *d, struct device_node *ctrlr,
				const u32 *intspec, unsigned int intsize,
				irq_hw_number_t *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 3))
		return -EINVAL;
	if (WARN_ON(intspec[0] >= n_irqs))
		return -EINVAL;
	if (WARN_ON((intspec[2] < AT91_AIC_IRQ_MIN_PRIORITY)
		    || (intspec[2] > AT91_AIC_IRQ_MAX_PRIORITY)))
		return -EINVAL;

	*out_hwirq = intspec[0];
	*out_type = intspec[1] & IRQ_TYPE_SENSE_MASK;
	at91_aic_irq_priorities[*out_hwirq] = intspec[2];

	return 0;
}

static struct irq_domain_ops at91_aic_irq_ops = {
	.map	= at91_aic_irq_map,
	.xlate	= at91_aic_irq_domain_xlate,
};

int __init at91_aic_of_common_init(struct device_node *node,
				    struct device_node *parent)
{
	struct property *prop;
	const __be32 *p;
	u32 val;

	at91_extern_irq = kzalloc(BITS_TO_LONGS(n_irqs)
				  * sizeof(*at91_extern_irq), GFP_KERNEL);
	if (!at91_extern_irq)
		return -ENOMEM;

	if (at91_aic_pm_init()) {
		kfree(at91_extern_irq);
		return -ENOMEM;
	}

	at91_aic_irq_priorities = kzalloc(n_irqs
					  * sizeof(*at91_aic_irq_priorities),
					  GFP_KERNEL);
	if (!at91_aic_irq_priorities)
		return -ENOMEM;

	at91_aic_base = of_iomap(node, 0);
	at91_aic_np = node;

	at91_aic_domain = irq_domain_add_linear(at91_aic_np, n_irqs,
						&at91_aic_irq_ops, NULL);
	if (!at91_aic_domain)
		panic("Unable to add AIC irq domain (DT)\n");

	of_property_for_each_u32(node, "atmel,external-irqs", prop, p, val) {
		if (val >= n_irqs)
			pr_warn("AIC: external irq %d >= %d skip it\n",
				val, n_irqs);
		else
			set_bit(val, at91_extern_irq);
	}

	irq_set_default_host(at91_aic_domain);

	return 0;
}

int __init at91_aic_of_init(struct device_node *node,
				     struct device_node *parent)
{
	int err;

	err = at91_aic_of_common_init(node, parent);
	if (err)
		return err;

	at91_aic_hw_init(n_irqs);

	return 0;
}

int __init at91_aic5_of_init(struct device_node *node,
				     struct device_node *parent)
{
	int err;

	at91_aic_caps |= AT91_AIC_CAP_AIC5;
	n_irqs = NR_AIC5_IRQS;
	at91_aic_chip.irq_ack           = at91_aic5_mask_irq;
	at91_aic_chip.irq_mask		= at91_aic5_mask_irq;
	at91_aic_chip.irq_unmask	= at91_aic5_unmask_irq;
	at91_aic_chip.irq_eoi		= at91_aic5_eoi;
	at91_aic_irq_ops.map		= at91_aic5_irq_map;

	err = at91_aic_of_common_init(node, parent);
	if (err)
		return err;

	at91_aic5_hw_init(n_irqs);

	return 0;
}
#endif

/*
 * Initialize the AIC interrupt controller.
 */
void __init at91_aic_init(unsigned int *priority, unsigned int ext_irq_mask)
{
	unsigned int i;
	int irq_base;

	at91_extern_irq = kzalloc(BITS_TO_LONGS(n_irqs)
				  * sizeof(*at91_extern_irq), GFP_KERNEL);

	if (at91_aic_pm_init() || at91_extern_irq == NULL)
		panic("Unable to allocate bit maps\n");

	*at91_extern_irq = ext_irq_mask;

	at91_aic_base = ioremap(AT91_AIC, 512);
	if (!at91_aic_base)
		panic("Unable to ioremap AIC registers\n");

	/* Add irq domain for AIC */
	irq_base = irq_alloc_descs(-1, 0, n_irqs, 0);
	if (irq_base < 0) {
		WARN(1, "Cannot allocate irq_descs, assuming pre-allocated\n");
		irq_base = 0;
	}
	at91_aic_domain = irq_domain_add_legacy(at91_aic_np, n_irqs,
						irq_base, 0,
						&irq_domain_simple_ops, NULL);

	if (!at91_aic_domain)
		panic("Unable to add AIC irq domain\n");

	irq_set_default_host(at91_aic_domain);

	/*
	 * The IVR is used by macro get_irqnr_and_base to read and verify.
	 * The irq number is NR_AIC_IRQS when a spurious interrupt has occurred.
	 */
	for (i = 0; i < n_irqs; i++) {
		/* Put hardware irq number in Source Vector Register: */
		at91_aic_write(AT91_AIC_SVR(i), NR_IRQS_LEGACY + i);
		/* Active Low interrupt, with the specified priority */
		at91_aic_write(AT91_AIC_SMR(i), AT91_AIC_SRCTYPE_LOW | priority[i]);
		irq_set_chip_and_handler(NR_IRQS_LEGACY + i, &at91_aic_chip, handle_fasteoi_irq);
		set_irq_flags(i, IRQF_VALID | IRQF_PROBE);
	}

	at91_aic_hw_init(n_irqs);
}
