/*
 * Xtensa MX interrupt distributor
 *
 * Copyright (C) 2002 - 2013 Tensilica, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/of.h>

#include <asm/mxregs.h>

#define HW_IRQ_IPI_COUNT 2
#define HW_IRQ_MX_BASE 2
#define HW_IRQ_EXTERN_BASE 3

static DEFINE_PER_CPU(unsigned int, cached_irq_mask);

static int xtensa_mx_irq_map(struct irq_domain *d, unsigned int irq,
		irq_hw_number_t hw)
{
	if (hw < HW_IRQ_IPI_COUNT) {
		struct irq_chip *irq_chip = d->host_data;
		irq_set_chip_and_handler_name(irq, irq_chip,
				handle_percpu_irq, "ipi");
		irq_set_status_flags(irq, IRQ_LEVEL);
		return 0;
	}
	irqd_set_single_target(irq_desc_get_irq_data(irq_to_desc(irq)));
	return xtensa_irq_map(d, irq, hw);
}

/*
 * Device Tree IRQ specifier translation function which works with one or
 * two cell bindings. First cell value maps directly to the hwirq number.
 * Second cell if present specifies whether hwirq number is external (1) or
 * internal (0).
 */
static int xtensa_mx_irq_domain_xlate(struct irq_domain *d,
		struct device_node *ctrlr,
		const u32 *intspec, unsigned int intsize,
		unsigned long *out_hwirq, unsigned int *out_type)
{
	return xtensa_irq_domain_xlate(intspec, intsize,
			intspec[0], intspec[0] + HW_IRQ_EXTERN_BASE,
			out_hwirq, out_type);
}

static const struct irq_domain_ops xtensa_mx_irq_domain_ops = {
	.xlate = xtensa_mx_irq_domain_xlate,
	.map = xtensa_mx_irq_map,
};

void secondary_init_irq(void)
{
	__this_cpu_write(cached_irq_mask,
			XCHAL_INTTYPE_MASK_EXTERN_EDGE |
			XCHAL_INTTYPE_MASK_EXTERN_LEVEL);
	xtensa_set_sr(XCHAL_INTTYPE_MASK_EXTERN_EDGE |
			XCHAL_INTTYPE_MASK_EXTERN_LEVEL, intenable);
}

static void xtensa_mx_irq_mask(struct irq_data *d)
{
	unsigned int mask = 1u << d->hwirq;

	if (mask & (XCHAL_INTTYPE_MASK_EXTERN_EDGE |
		    XCHAL_INTTYPE_MASK_EXTERN_LEVEL)) {
		unsigned int ext_irq = xtensa_get_ext_irq_no(d->hwirq);

		if (ext_irq >= HW_IRQ_MX_BASE) {
			set_er(1u << (ext_irq - HW_IRQ_MX_BASE), MIENG);
			return;
		}
	}
	mask = __this_cpu_read(cached_irq_mask) & ~mask;
	__this_cpu_write(cached_irq_mask, mask);
	xtensa_set_sr(mask, intenable);
}

static void xtensa_mx_irq_unmask(struct irq_data *d)
{
	unsigned int mask = 1u << d->hwirq;

	if (mask & (XCHAL_INTTYPE_MASK_EXTERN_EDGE |
		    XCHAL_INTTYPE_MASK_EXTERN_LEVEL)) {
		unsigned int ext_irq = xtensa_get_ext_irq_no(d->hwirq);

		if (ext_irq >= HW_IRQ_MX_BASE) {
			set_er(1u << (ext_irq - HW_IRQ_MX_BASE), MIENGSET);
			return;
		}
	}
	mask |= __this_cpu_read(cached_irq_mask);
	__this_cpu_write(cached_irq_mask, mask);
	xtensa_set_sr(mask, intenable);
}

static void xtensa_mx_irq_enable(struct irq_data *d)
{
	xtensa_mx_irq_unmask(d);
}

static void xtensa_mx_irq_disable(struct irq_data *d)
{
	xtensa_mx_irq_mask(d);
}

static void xtensa_mx_irq_ack(struct irq_data *d)
{
	xtensa_set_sr(1 << d->hwirq, intclear);
}

static int xtensa_mx_irq_retrigger(struct irq_data *d)
{
	unsigned int mask = 1u << d->hwirq;

	if (WARN_ON(mask & ~XCHAL_INTTYPE_MASK_SOFTWARE))
		return 0;
	xtensa_set_sr(mask, intset);
	return 1;
}

static int xtensa_mx_irq_set_affinity(struct irq_data *d,
		const struct cpumask *dest, bool force)
{
	int cpu = cpumask_any_and(dest, cpu_online_mask);
	unsigned mask = 1u << cpu;

	set_er(mask, MIROUT(d->hwirq - HW_IRQ_MX_BASE));
	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	return 0;

}

static struct irq_chip xtensa_mx_irq_chip = {
	.name		= "xtensa-mx",
	.irq_enable	= xtensa_mx_irq_enable,
	.irq_disable	= xtensa_mx_irq_disable,
	.irq_mask	= xtensa_mx_irq_mask,
	.irq_unmask	= xtensa_mx_irq_unmask,
	.irq_ack	= xtensa_mx_irq_ack,
	.irq_retrigger	= xtensa_mx_irq_retrigger,
	.irq_set_affinity = xtensa_mx_irq_set_affinity,
};

static void __init xtensa_mx_init_common(struct irq_domain *root_domain)
{
	unsigned int i;

	irq_set_default_host(root_domain);
	secondary_init_irq();

	/* Initialize default IRQ routing to CPU 0 */
	for (i = 0; i < XCHAL_NUM_EXTINTERRUPTS; ++i)
		set_er(1, MIROUT(i));
}

int __init xtensa_mx_init_legacy(struct device_node *interrupt_parent)
{
	struct irq_domain *root_domain =
		irq_domain_add_legacy(NULL, NR_IRQS - 1, 1, 0,
				&xtensa_mx_irq_domain_ops,
				&xtensa_mx_irq_chip);
	xtensa_mx_init_common(root_domain);
	return 0;
}

static int __init xtensa_mx_init(struct device_node *np,
		struct device_node *interrupt_parent)
{
	struct irq_domain *root_domain =
		irq_domain_add_linear(np, NR_IRQS, &xtensa_mx_irq_domain_ops,
				&xtensa_mx_irq_chip);
	xtensa_mx_init_common(root_domain);
	return 0;
}
IRQCHIP_DECLARE(xtensa_mx_irq_chip, "cdns,xtensa-mx", xtensa_mx_init);
