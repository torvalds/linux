// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Birger Koblitz <mail@birger-koblitz.de>
 * Copyright (C) 2020 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2020 John Crispin <john@phrozen.org>
 */

#include <linux/of_irq.h>
#include <linux/irqchip.h>
#include <linux/spinlock.h>
#include <linux/of_address.h>
#include <linux/irqchip/chained_irq.h>

/* Global Interrupt Mask Register */
#define RTL_ICTL_GIMR		0x00
/* Global Interrupt Status Register */
#define RTL_ICTL_GISR		0x04
/* Interrupt Routing Registers */
#define RTL_ICTL_IRR0		0x08
#define RTL_ICTL_IRR1		0x0c
#define RTL_ICTL_IRR2		0x10
#define RTL_ICTL_IRR3		0x14

#define RTL_ICTL_NUM_INPUTS	32

#define REG(cpu, x)		(realtek_ictl_base[cpu] + x)

struct realtek_ictl_output {
	struct fwnode_handle	*fwnode;
	struct irq_domain	*domain;
	unsigned int		parent_irq;
	unsigned int		parent_hwirq;
	unsigned int		index;
	u32			mask;
};

static DEFINE_RAW_SPINLOCK(irq_lock);
static void __iomem *realtek_ictl_base[NR_CPUS];

/*
 * IRR0-IRR3 store 4 bits per interrupt, but Realtek uses inverted numbering,
 * placing IRQ 31 in the first four bits. A routing value of '0' means the
 * interrupt is left disconnected. Routing values {1..15} connect to output
 * lines {0..14}.
 */
#define IRR_OFFSET(idx)		(4 * (3 - (idx * 4) / 32))
#define IRR_SHIFT(idx)		((idx * 4) % 32)

static inline void enable_gimr(unsigned int cpu, unsigned int hw_irq)
{
	u32 gimr;

	gimr = readl(REG(cpu, RTL_ICTL_GIMR));
	gimr |= BIT(hw_irq);
	writel(gimr, REG(cpu, RTL_ICTL_GIMR));
}

static inline void disable_gimr(unsigned int cpu, unsigned int hw_irq)
{
	u32 gimr;

	gimr = readl(REG(cpu, RTL_ICTL_GIMR));
	gimr &= ~BIT(hw_irq);
	writel(gimr, REG(cpu, RTL_ICTL_GIMR));
}

static void write_irr(unsigned int cpu, int hw_irq, u32 value)
{
	void __iomem *irr0 = REG(cpu, RTL_ICTL_IRR0);
	unsigned int offset = IRR_OFFSET(hw_irq);
	unsigned int shift = IRR_SHIFT(hw_irq);
	u32 irr;

	irr = readl(irr0 + offset) & ~(0xf << shift);
	irr |= (value & 0xf) << shift;
	writel(irr, irr0 + offset);
}

static void realtek_ictl_unmask_irq(struct irq_data *i)
{
	unsigned int cpu;

	guard(raw_spinlock)(&irq_lock);
	for_each_cpu(cpu, irq_data_get_effective_affinity_mask(i))
		enable_gimr(cpu, i->hwirq);
}

static void realtek_ictl_mask_irq(struct irq_data *i)
{
	unsigned int cpu;

	guard(raw_spinlock)(&irq_lock);
	for_each_cpu(cpu, irq_data_get_effective_affinity_mask(i))
		disable_gimr(cpu, i->hwirq);
}

static int realtek_ictl_irq_affinity(struct irq_data *i, const struct cpumask *dest, bool force)
{
	if (!irqd_irq_masked(i))
		realtek_ictl_mask_irq(i);

	irq_data_update_effective_affinity(i, dest);

	if (!irqd_irq_masked(i))
		realtek_ictl_unmask_irq(i);

	return IRQ_SET_MASK_OK;
}

static struct irq_chip realtek_ictl_irq = {
	.name			= "realtek-rtl-intc",
	.irq_mask		= realtek_ictl_mask_irq,
	.irq_unmask		= realtek_ictl_unmask_irq,
	.irq_set_affinity	= realtek_ictl_irq_affinity,
};

static int intc_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw_irq)
{
	struct realtek_ictl_output *output = d->host_data;
	unsigned int cpu;

	irq_set_chip_and_handler(irq, &realtek_ictl_irq, handle_level_irq);

	guard(raw_spinlock_irqsave)(&irq_lock);
	output->mask |= BIT(hw_irq);
	for_each_present_cpu(cpu)
		write_irr(cpu, hw_irq, output->parent_hwirq - 1);

	return 0;
}

static int intc_select(struct irq_domain *d, struct irq_fwspec *fwspec,
		       enum irq_domain_bus_token bus_token)
{
	struct realtek_ictl_output *output = d->host_data;
	unsigned int index = 0;

	if (fwspec->fwnode != output->fwnode)
		return false;

	if (fwspec->param_count == 2)
		index = fwspec->param[1];

	return index == output->index;
}

static const struct irq_domain_ops irq_domain_ops = {
	.map	= intc_map,
	.select	= intc_select,
	.xlate	= irq_domain_xlate_onecell,
};

static void realtek_irq_dispatch(struct irq_desc *desc)
{
	struct realtek_ictl_output *output = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int cpu = smp_processor_id();
	unsigned long pending;
	unsigned int hw_irq;

	chained_irq_enter(chip, desc);
	pending = readl(REG(cpu, RTL_ICTL_GIMR)) & readl(REG(cpu, RTL_ICTL_GISR)) & output->mask;

	if (unlikely(!pending)) {
		spurious_interrupt();
		goto out;
	}

	for_each_set_bit(hw_irq, &pending, RTL_ICTL_NUM_INPUTS)
		generic_handle_domain_irq(output->domain, hw_irq);

out:
	chained_irq_exit(chip, desc);
}

static int __init realtek_setup_parents(struct device_node *node)
{
	int p, cnt, err, parent_irq, num_parents = of_irq_count(node);
	struct realtek_ictl_output *output;
	struct irq_data *parent_data;
	struct of_phandle_args oirq;
	struct irq_domain *domain;

	cnt = max(1, num_parents);
	output = kcalloc(cnt, sizeof(*output), GFP_KERNEL);
	if (!output)
		return -ENOMEM;

	for (p = 0; p < cnt; p++) {
		if (WARN_ON(!num_parents)) {
			/*
			 * If DT contains no parent interrupts, assume MIPS IRQ 2 (HW0) is
			 * connected to the first output. This is the case for all known hardware.
			 */
			oirq.np = of_find_compatible_node(NULL, NULL,
							  "mti,cpu-interrupt-controller");
			if (!oirq.np) {
				err = -EINVAL;
				goto err_out;
			}

			oirq.args_count = 1;
			oirq.args[0] = 2;
			parent_irq = irq_create_of_mapping(&oirq);
			of_node_put(oirq.np);
		} else {
			parent_irq = of_irq_get(node, p);
		}

		if (parent_irq <= 0) {
			err = parent_irq ? parent_irq : -ENODEV;
			goto err_out;
		}

		parent_data = irq_get_irq_data(parent_irq);
		if (!parent_data) {
			err = -EINVAL;
			goto err_out;
		}

		domain = irq_domain_create_linear(of_fwnode_handle(node), RTL_ICTL_NUM_INPUTS,
						  &irq_domain_ops, &output[p]);
		if (!domain) {
			err = -ENOMEM;
			goto err_out;
		}

		output[p].domain = domain;
		output[p].fwnode = of_fwnode_handle(node);
		output[p].index = p;
		output[p].parent_irq = parent_irq;
		output[p].parent_hwirq = irqd_to_hwirq(parent_data);
		irq_set_chained_handler_and_data(parent_irq, realtek_irq_dispatch, &output[p]);
	}

	return 0;

err_out:
	while (p--) {
		irq_set_chained_handler_and_data(output[p].parent_irq, NULL, NULL);
		irq_domain_remove(output[p].domain);
	}

	kfree(output);

	return err;
}

static int __init realtek_rtl_of_init(struct device_node *node, struct device_node *parent)
{
	unsigned int cpu;

	for_each_present_cpu(cpu) {
		realtek_ictl_base[cpu] = of_iomap(node, cpu);
		if (!realtek_ictl_base[cpu])
			return -ENXIO;

		/* Disable all cascaded interrupts and clear routing */
		for (unsigned int hw_irq = 0; hw_irq < RTL_ICTL_NUM_INPUTS; hw_irq++) {
			disable_gimr(cpu, hw_irq);
			write_irr(cpu, hw_irq, 0);
		}
	}

	return realtek_setup_parents(node);
}

IRQCHIP_DECLARE(realtek_rtl_intc, "realtek,rtl-intc", realtek_rtl_of_init);
