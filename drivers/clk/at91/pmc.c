/*
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/at91_pmc.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>

#include <asm/proc-fns.h>

#include "pmc.h"

void __iomem *at91_pmc_base;
EXPORT_SYMBOL_GPL(at91_pmc_base);

void at91rm9200_idle(void)
{
	/*
	 * Disable the processor clock.  The processor will be automatically
	 * re-enabled by an interrupt or by a reset.
	 */
	at91_pmc_write(AT91_PMC_SCDR, AT91_PMC_PCK);
}

void at91sam9_idle(void)
{
	at91_pmc_write(AT91_PMC_SCDR, AT91_PMC_PCK);
	cpu_do_idle();
}

int of_at91_get_clk_range(struct device_node *np, const char *propname,
			  struct clk_range *range)
{
	u32 min, max;
	int ret;

	ret = of_property_read_u32_index(np, propname, 0, &min);
	if (ret)
		return ret;

	ret = of_property_read_u32_index(np, propname, 1, &max);
	if (ret)
		return ret;

	if (range) {
		range->min = min;
		range->max = max;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(of_at91_get_clk_range);

static void pmc_irq_mask(struct irq_data *d)
{
	struct at91_pmc *pmc = irq_data_get_irq_chip_data(d);

	pmc_write(pmc, AT91_PMC_IDR, 1 << d->hwirq);
}

static void pmc_irq_unmask(struct irq_data *d)
{
	struct at91_pmc *pmc = irq_data_get_irq_chip_data(d);

	pmc_write(pmc, AT91_PMC_IER, 1 << d->hwirq);
}

static int pmc_irq_set_type(struct irq_data *d, unsigned type)
{
	if (type != IRQ_TYPE_LEVEL_HIGH) {
		pr_warn("PMC: type not supported (support only IRQ_TYPE_LEVEL_HIGH type)\n");
		return -EINVAL;
	}

	return 0;
}

static struct irq_chip pmc_irq = {
	.name = "PMC",
	.irq_disable = pmc_irq_mask,
	.irq_mask = pmc_irq_mask,
	.irq_unmask = pmc_irq_unmask,
	.irq_set_type = pmc_irq_set_type,
};

static struct lock_class_key pmc_lock_class;

static int pmc_irq_map(struct irq_domain *h, unsigned int virq,
		       irq_hw_number_t hw)
{
	struct at91_pmc	*pmc = h->host_data;

	irq_set_lockdep_class(virq, &pmc_lock_class);

	irq_set_chip_and_handler(virq, &pmc_irq,
				 handle_level_irq);
	set_irq_flags(virq, IRQF_VALID);
	irq_set_chip_data(virq, pmc);

	return 0;
}

static int pmc_irq_domain_xlate(struct irq_domain *d,
				struct device_node *ctrlr,
				const u32 *intspec, unsigned int intsize,
				irq_hw_number_t *out_hwirq,
				unsigned int *out_type)
{
	struct at91_pmc *pmc = d->host_data;
	const struct at91_pmc_caps *caps = pmc->caps;

	if (WARN_ON(intsize < 1))
		return -EINVAL;

	*out_hwirq = intspec[0];

	if (!(caps->available_irqs & (1 << *out_hwirq)))
		return -EINVAL;

	*out_type = IRQ_TYPE_LEVEL_HIGH;

	return 0;
}

static struct irq_domain_ops pmc_irq_ops = {
	.map	= pmc_irq_map,
	.xlate	= pmc_irq_domain_xlate,
};

static irqreturn_t pmc_irq_handler(int irq, void *data)
{
	struct at91_pmc *pmc = (struct at91_pmc *)data;
	unsigned long sr;
	int n;

	sr = pmc_read(pmc, AT91_PMC_SR) & pmc_read(pmc, AT91_PMC_IMR);
	if (!sr)
		return IRQ_NONE;

	for_each_set_bit(n, &sr, BITS_PER_LONG)
		generic_handle_irq(irq_find_mapping(pmc->irqdomain, n));

	return IRQ_HANDLED;
}

static const struct at91_pmc_caps at91rm9200_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_LOCKB |
			  AT91_PMC_MCKRDY | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY | AT91_PMC_PCK2RDY |
			  AT91_PMC_PCK3RDY,
};

static const struct at91_pmc_caps at91sam9260_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_LOCKB |
			  AT91_PMC_MCKRDY | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY,
};

static const struct at91_pmc_caps at91sam9g45_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_MCKRDY |
			  AT91_PMC_LOCKU | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY,
};

static const struct at91_pmc_caps at91sam9n12_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_LOCKB |
			  AT91_PMC_MCKRDY | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY | AT91_PMC_MOSCSELS |
			  AT91_PMC_MOSCRCS | AT91_PMC_CFDEV,
};

static const struct at91_pmc_caps at91sam9x5_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_MCKRDY |
			  AT91_PMC_LOCKU | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY | AT91_PMC_MOSCSELS |
			  AT91_PMC_MOSCRCS | AT91_PMC_CFDEV,
};

static const struct at91_pmc_caps sama5d3_caps = {
	.available_irqs = AT91_PMC_MOSCS | AT91_PMC_LOCKA | AT91_PMC_MCKRDY |
			  AT91_PMC_LOCKU | AT91_PMC_PCK0RDY |
			  AT91_PMC_PCK1RDY | AT91_PMC_PCK2RDY |
			  AT91_PMC_MOSCSELS | AT91_PMC_MOSCRCS |
			  AT91_PMC_CFDEV,
};

static struct at91_pmc *__init at91_pmc_init(struct device_node *np,
					     void __iomem *regbase, int virq,
					     const struct at91_pmc_caps *caps)
{
	struct at91_pmc *pmc;

	if (!regbase || !virq ||  !caps)
		return NULL;

	at91_pmc_base = regbase;

	pmc = kzalloc(sizeof(*pmc), GFP_KERNEL);
	if (!pmc)
		return NULL;

	spin_lock_init(&pmc->lock);
	pmc->regbase = regbase;
	pmc->virq = virq;
	pmc->caps = caps;

	pmc->irqdomain = irq_domain_add_linear(np, 32, &pmc_irq_ops, pmc);

	if (!pmc->irqdomain)
		goto out_free_pmc;

	pmc_write(pmc, AT91_PMC_IDR, 0xffffffff);
	if (request_irq(pmc->virq, pmc_irq_handler, IRQF_SHARED, "pmc", pmc))
		goto out_remove_irqdomain;

	return pmc;

out_remove_irqdomain:
	irq_domain_remove(pmc->irqdomain);
out_free_pmc:
	kfree(pmc);

	return NULL;
}

static const struct of_device_id pmc_clk_ids[] __initconst = {
	/* Slow oscillator */
	{
		.compatible = "atmel,at91sam9260-clk-slow",
		.data = of_at91sam9260_clk_slow_setup,
	},
	/* Main clock */
	{
		.compatible = "atmel,at91rm9200-clk-main-osc",
		.data = of_at91rm9200_clk_main_osc_setup,
	},
	{
		.compatible = "atmel,at91sam9x5-clk-main-rc-osc",
		.data = of_at91sam9x5_clk_main_rc_osc_setup,
	},
	{
		.compatible = "atmel,at91rm9200-clk-main",
		.data = of_at91rm9200_clk_main_setup,
	},
	{
		.compatible = "atmel,at91sam9x5-clk-main",
		.data = of_at91sam9x5_clk_main_setup,
	},
	/* PLL clocks */
	{
		.compatible = "atmel,at91rm9200-clk-pll",
		.data = of_at91rm9200_clk_pll_setup,
	},
	{
		.compatible = "atmel,at91sam9g45-clk-pll",
		.data = of_at91sam9g45_clk_pll_setup,
	},
	{
		.compatible = "atmel,at91sam9g20-clk-pllb",
		.data = of_at91sam9g20_clk_pllb_setup,
	},
	{
		.compatible = "atmel,sama5d3-clk-pll",
		.data = of_sama5d3_clk_pll_setup,
	},
	{
		.compatible = "atmel,at91sam9x5-clk-plldiv",
		.data = of_at91sam9x5_clk_plldiv_setup,
	},
	/* Master clock */
	{
		.compatible = "atmel,at91rm9200-clk-master",
		.data = of_at91rm9200_clk_master_setup,
	},
	{
		.compatible = "atmel,at91sam9x5-clk-master",
		.data = of_at91sam9x5_clk_master_setup,
	},
	/* System clocks */
	{
		.compatible = "atmel,at91rm9200-clk-system",
		.data = of_at91rm9200_clk_sys_setup,
	},
	/* Peripheral clocks */
	{
		.compatible = "atmel,at91rm9200-clk-peripheral",
		.data = of_at91rm9200_clk_periph_setup,
	},
	{
		.compatible = "atmel,at91sam9x5-clk-peripheral",
		.data = of_at91sam9x5_clk_periph_setup,
	},
	/* Programmable clocks */
	{
		.compatible = "atmel,at91rm9200-clk-programmable",
		.data = of_at91rm9200_clk_prog_setup,
	},
	{
		.compatible = "atmel,at91sam9g45-clk-programmable",
		.data = of_at91sam9g45_clk_prog_setup,
	},
	{
		.compatible = "atmel,at91sam9x5-clk-programmable",
		.data = of_at91sam9x5_clk_prog_setup,
	},
	/* UTMI clock */
#if defined(CONFIG_HAVE_AT91_UTMI)
	{
		.compatible = "atmel,at91sam9x5-clk-utmi",
		.data = of_at91sam9x5_clk_utmi_setup,
	},
#endif
	/* USB clock */
#if defined(CONFIG_HAVE_AT91_USB_CLK)
	{
		.compatible = "atmel,at91rm9200-clk-usb",
		.data = of_at91rm9200_clk_usb_setup,
	},
	{
		.compatible = "atmel,at91sam9x5-clk-usb",
		.data = of_at91sam9x5_clk_usb_setup,
	},
	{
		.compatible = "atmel,at91sam9n12-clk-usb",
		.data = of_at91sam9n12_clk_usb_setup,
	},
#endif
	/* SMD clock */
#if defined(CONFIG_HAVE_AT91_SMD)
	{
		.compatible = "atmel,at91sam9x5-clk-smd",
		.data = of_at91sam9x5_clk_smd_setup,
	},
#endif
#if defined(CONFIG_HAVE_AT91_H32MX)
	{
		.compatible = "atmel,sama5d4-clk-h32mx",
		.data = of_sama5d4_clk_h32mx_setup,
	},
#endif
	{ /*sentinel*/ }
};

static void __init of_at91_pmc_setup(struct device_node *np,
				     const struct at91_pmc_caps *caps)
{
	struct at91_pmc *pmc;
	struct device_node *childnp;
	void (*clk_setup)(struct device_node *, struct at91_pmc *);
	const struct of_device_id *clk_id;
	void __iomem *regbase = of_iomap(np, 0);
	int virq;

	if (!regbase)
		return;

	virq = irq_of_parse_and_map(np, 0);
	if (!virq)
		return;

	pmc = at91_pmc_init(np, regbase, virq, caps);
	if (!pmc)
		return;
	for_each_child_of_node(np, childnp) {
		clk_id = of_match_node(pmc_clk_ids, childnp);
		if (!clk_id)
			continue;
		clk_setup = clk_id->data;
		clk_setup(childnp, pmc);
	}
}

static void __init of_at91rm9200_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &at91rm9200_caps);
}
CLK_OF_DECLARE(at91rm9200_clk_pmc, "atmel,at91rm9200-pmc",
	       of_at91rm9200_pmc_setup);

static void __init of_at91sam9260_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &at91sam9260_caps);
}
CLK_OF_DECLARE(at91sam9260_clk_pmc, "atmel,at91sam9260-pmc",
	       of_at91sam9260_pmc_setup);

static void __init of_at91sam9g45_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &at91sam9g45_caps);
}
CLK_OF_DECLARE(at91sam9g45_clk_pmc, "atmel,at91sam9g45-pmc",
	       of_at91sam9g45_pmc_setup);

static void __init of_at91sam9n12_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &at91sam9n12_caps);
}
CLK_OF_DECLARE(at91sam9n12_clk_pmc, "atmel,at91sam9n12-pmc",
	       of_at91sam9n12_pmc_setup);

static void __init of_at91sam9x5_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &at91sam9x5_caps);
}
CLK_OF_DECLARE(at91sam9x5_clk_pmc, "atmel,at91sam9x5-pmc",
	       of_at91sam9x5_pmc_setup);

static void __init of_sama5d3_pmc_setup(struct device_node *np)
{
	of_at91_pmc_setup(np, &sama5d3_caps);
}
CLK_OF_DECLARE(sama5d3_clk_pmc, "atmel,sama5d3-pmc",
	       of_sama5d3_pmc_setup);
