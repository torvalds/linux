// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqchip/riscv-aplic.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/printk.h>
#include <linux/smp.h>

#include "irq-riscv-aplic-main.h"

#define APLIC_DISABLE_IDELIVERY		0
#define APLIC_ENABLE_IDELIVERY		1
#define APLIC_DISABLE_ITHRESHOLD	1
#define APLIC_ENABLE_ITHRESHOLD		0

struct aplic_direct {
	struct aplic_priv	priv;
	struct irq_domain	*irqdomain;
	struct cpumask		lmask;
};

struct aplic_idc {
	unsigned int		hart_index;
	void __iomem		*regs;
	struct aplic_direct	*direct;
};

static unsigned int aplic_direct_parent_irq;
static DEFINE_PER_CPU(struct aplic_idc, aplic_idcs);

static void aplic_direct_irq_eoi(struct irq_data *d)
{
	/*
	 * The fasteoi_handler requires irq_eoi() callback hence
	 * provide a dummy handler.
	 */
}

#ifdef CONFIG_SMP
static int aplic_direct_set_affinity(struct irq_data *d, const struct cpumask *mask_val,
				     bool force)
{
	struct aplic_priv *priv = irq_data_get_irq_chip_data(d);
	struct aplic_direct *direct = container_of(priv, struct aplic_direct, priv);
	struct aplic_idc *idc;
	unsigned int cpu, val;
	void __iomem *target;

	if (force)
		cpu = cpumask_first_and(&direct->lmask, mask_val);
	else
		cpu = cpumask_first_and_and(&direct->lmask, mask_val, cpu_online_mask);

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	idc = per_cpu_ptr(&aplic_idcs, cpu);
	target = priv->regs + APLIC_TARGET_BASE + (d->hwirq - 1) * sizeof(u32);
	val = FIELD_PREP(APLIC_TARGET_HART_IDX, idc->hart_index);
	val |= FIELD_PREP(APLIC_TARGET_IPRIO, APLIC_DEFAULT_PRIORITY);
	writel(val, target);

	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	return IRQ_SET_MASK_OK_DONE;
}
#endif

static struct irq_chip aplic_direct_chip = {
	.name		= "APLIC-DIRECT",
	.irq_mask	= aplic_irq_mask,
	.irq_unmask	= aplic_irq_unmask,
	.irq_set_type	= aplic_irq_set_type,
	.irq_eoi	= aplic_direct_irq_eoi,
#ifdef CONFIG_SMP
	.irq_set_affinity = aplic_direct_set_affinity,
#endif
	.flags		= IRQCHIP_SET_TYPE_MASKED |
			  IRQCHIP_SKIP_SET_WAKE |
			  IRQCHIP_MASK_ON_SUSPEND,
};

static int aplic_direct_irqdomain_translate(struct irq_domain *d, struct irq_fwspec *fwspec,
					    unsigned long *hwirq, unsigned int *type)
{
	struct aplic_priv *priv = d->host_data;

	return aplic_irqdomain_translate(fwspec, priv->gsi_base, hwirq, type);
}

static int aplic_direct_irqdomain_alloc(struct irq_domain *domain, unsigned int virq,
					unsigned int nr_irqs, void *arg)
{
	struct aplic_priv *priv = domain->host_data;
	struct aplic_direct *direct = container_of(priv, struct aplic_direct, priv);
	struct irq_fwspec *fwspec = arg;
	irq_hw_number_t hwirq;
	unsigned int type;
	int i, ret;

	ret = aplic_irqdomain_translate(fwspec, priv->gsi_base, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, hwirq + i, &aplic_direct_chip,
				    priv, handle_fasteoi_irq, NULL, NULL);
		irq_set_affinity(virq + i, &direct->lmask);
	}

	return 0;
}

static const struct irq_domain_ops aplic_direct_irqdomain_ops = {
	.translate	= aplic_direct_irqdomain_translate,
	.alloc		= aplic_direct_irqdomain_alloc,
	.free		= irq_domain_free_irqs_top,
};

/*
 * To handle an APLIC direct interrupts, we just read the CLAIMI register
 * which will return highest priority pending interrupt and clear the
 * pending bit of the interrupt. This process is repeated until CLAIMI
 * register return zero value.
 */
static void aplic_direct_handle_irq(struct irq_desc *desc)
{
	struct aplic_idc *idc = this_cpu_ptr(&aplic_idcs);
	struct irq_domain *irqdomain = idc->direct->irqdomain;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	irq_hw_number_t hw_irq;
	int irq;

	chained_irq_enter(chip, desc);

	while ((hw_irq = readl(idc->regs + APLIC_IDC_CLAIMI))) {
		hw_irq = hw_irq >> APLIC_IDC_TOPI_ID_SHIFT;
		irq = irq_find_mapping(irqdomain, hw_irq);

		if (unlikely(irq <= 0)) {
			dev_warn_ratelimited(idc->direct->priv.dev,
					     "hw_irq %lu mapping not found\n", hw_irq);
		} else {
			generic_handle_irq(irq);
		}
	}

	chained_irq_exit(chip, desc);
}

static void aplic_idc_set_delivery(struct aplic_idc *idc, bool en)
{
	u32 de = (en) ? APLIC_ENABLE_IDELIVERY : APLIC_DISABLE_IDELIVERY;
	u32 th = (en) ? APLIC_ENABLE_ITHRESHOLD : APLIC_DISABLE_ITHRESHOLD;

	/* Priority must be less than threshold for interrupt triggering */
	writel(th, idc->regs + APLIC_IDC_ITHRESHOLD);

	/* Delivery must be set to 1 for interrupt triggering */
	writel(de, idc->regs + APLIC_IDC_IDELIVERY);
}

static int aplic_direct_dying_cpu(unsigned int cpu)
{
	if (aplic_direct_parent_irq)
		disable_percpu_irq(aplic_direct_parent_irq);

	return 0;
}

static int aplic_direct_starting_cpu(unsigned int cpu)
{
	if (aplic_direct_parent_irq) {
		enable_percpu_irq(aplic_direct_parent_irq,
				  irq_get_trigger_type(aplic_direct_parent_irq));
	}

	return 0;
}

static int aplic_direct_parse_parent_hwirq(struct device *dev, u32 index,
					   u32 *parent_hwirq, unsigned long *parent_hartid)
{
	struct of_phandle_args parent;
	int rc;

	/*
	 * Currently, only OF fwnode is supported so extend this
	 * function for ACPI support.
	 */
	if (!is_of_node(dev->fwnode))
		return -EINVAL;

	rc = of_irq_parse_one(to_of_node(dev->fwnode), index, &parent);
	if (rc)
		return rc;

	rc = riscv_of_parent_hartid(parent.np, parent_hartid);
	if (rc)
		return rc;

	*parent_hwirq = parent.args[0];
	return 0;
}

int aplic_direct_setup(struct device *dev, void __iomem *regs)
{
	int i, j, rc, cpu, current_cpu, setup_count = 0;
	struct aplic_direct *direct;
	struct irq_domain *domain;
	struct aplic_priv *priv;
	struct aplic_idc *idc;
	unsigned long hartid;
	u32 v, hwirq;

	direct = devm_kzalloc(dev, sizeof(*direct), GFP_KERNEL);
	if (!direct)
		return -ENOMEM;
	priv = &direct->priv;

	rc = aplic_setup_priv(priv, dev, regs);
	if (rc) {
		dev_err(dev, "failed to create APLIC context\n");
		return rc;
	}

	/* Setup per-CPU IDC and target CPU mask */
	current_cpu = get_cpu();
	for (i = 0; i < priv->nr_idcs; i++) {
		rc = aplic_direct_parse_parent_hwirq(dev, i, &hwirq, &hartid);
		if (rc) {
			dev_warn(dev, "parent irq for IDC%d not found\n", i);
			continue;
		}

		/*
		 * Skip interrupts other than external interrupts for
		 * current privilege level.
		 */
		if (hwirq != RV_IRQ_EXT)
			continue;

		cpu = riscv_hartid_to_cpuid(hartid);
		if (cpu < 0) {
			dev_warn(dev, "invalid cpuid for IDC%d\n", i);
			continue;
		}

		cpumask_set_cpu(cpu, &direct->lmask);

		idc = per_cpu_ptr(&aplic_idcs, cpu);
		idc->hart_index = i;
		idc->regs = priv->regs + APLIC_IDC_BASE + i * APLIC_IDC_SIZE;
		idc->direct = direct;

		aplic_idc_set_delivery(idc, true);

		/*
		 * Boot cpu might not have APLIC hart_index = 0 so check
		 * and update target registers of all interrupts.
		 */
		if (cpu == current_cpu && idc->hart_index) {
			v = FIELD_PREP(APLIC_TARGET_HART_IDX, idc->hart_index);
			v |= FIELD_PREP(APLIC_TARGET_IPRIO, APLIC_DEFAULT_PRIORITY);
			for (j = 1; j <= priv->nr_irqs; j++)
				writel(v, priv->regs + APLIC_TARGET_BASE + (j - 1) * sizeof(u32));
		}

		setup_count++;
	}
	put_cpu();

	/* Find parent domain and register chained handler */
	domain = irq_find_matching_fwnode(riscv_get_intc_hwnode(),
					  DOMAIN_BUS_ANY);
	if (!aplic_direct_parent_irq && domain) {
		aplic_direct_parent_irq = irq_create_mapping(domain, RV_IRQ_EXT);
		if (aplic_direct_parent_irq) {
			irq_set_chained_handler(aplic_direct_parent_irq,
						aplic_direct_handle_irq);

			/*
			 * Setup CPUHP notifier to enable parent
			 * interrupt on all CPUs
			 */
			cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
					  "irqchip/riscv/aplic:starting",
					  aplic_direct_starting_cpu,
					  aplic_direct_dying_cpu);
		}
	}

	/* Fail if we were not able to setup IDC for any CPU */
	if (!setup_count)
		return -ENODEV;

	/* Setup global config and interrupt delivery */
	aplic_init_hw_global(priv, false);

	/* Create irq domain instance for the APLIC */
	direct->irqdomain = irq_domain_create_linear(dev->fwnode, priv->nr_irqs + 1,
						     &aplic_direct_irqdomain_ops, priv);
	if (!direct->irqdomain) {
		dev_err(dev, "failed to create direct irq domain\n");
		return -ENOMEM;
	}

	/* Advertise the interrupt controller */
	dev_info(dev, "%d interrupts directly connected to %d CPUs\n",
		 priv->nr_irqs, priv->nr_idcs);

	return 0;
}
