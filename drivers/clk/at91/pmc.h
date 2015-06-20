/*
 * drivers/clk/at91/pmc.h
 *
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __PMC_H_
#define __PMC_H_

#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/spinlock.h>

struct clk_range {
	unsigned long min;
	unsigned long max;
};

#define CLK_RANGE(MIN, MAX) {.min = MIN, .max = MAX,}

struct at91_pmc_caps {
	u32 available_irqs;
};

struct at91_pmc {
	void __iomem *regbase;
	int virq;
	spinlock_t lock;
	const struct at91_pmc_caps *caps;
	struct irq_domain *irqdomain;
	u32 imr;
};

static inline void pmc_lock(struct at91_pmc *pmc)
{
	spin_lock(&pmc->lock);
}

static inline void pmc_unlock(struct at91_pmc *pmc)
{
	spin_unlock(&pmc->lock);
}

static inline u32 pmc_read(struct at91_pmc *pmc, int offset)
{
	return readl(pmc->regbase + offset);
}

static inline void pmc_write(struct at91_pmc *pmc, int offset, u32 value)
{
	writel(value, pmc->regbase + offset);
}

int of_at91_get_clk_range(struct device_node *np, const char *propname,
			  struct clk_range *range);

extern void __init of_at91sam9260_clk_slow_setup(struct device_node *np,
						 struct at91_pmc *pmc);

extern void __init of_at91rm9200_clk_main_osc_setup(struct device_node *np,
						    struct at91_pmc *pmc);
extern void __init of_at91sam9x5_clk_main_rc_osc_setup(struct device_node *np,
						       struct at91_pmc *pmc);
extern void __init of_at91rm9200_clk_main_setup(struct device_node *np,
						struct at91_pmc *pmc);
extern void __init of_at91sam9x5_clk_main_setup(struct device_node *np,
						struct at91_pmc *pmc);

extern void __init of_at91rm9200_clk_pll_setup(struct device_node *np,
					       struct at91_pmc *pmc);
extern void __init of_at91sam9g45_clk_pll_setup(struct device_node *np,
						struct at91_pmc *pmc);
extern void __init of_at91sam9g20_clk_pllb_setup(struct device_node *np,
						 struct at91_pmc *pmc);
extern void __init of_sama5d3_clk_pll_setup(struct device_node *np,
					    struct at91_pmc *pmc);
extern void __init of_at91sam9x5_clk_plldiv_setup(struct device_node *np,
						  struct at91_pmc *pmc);

extern void __init of_at91rm9200_clk_master_setup(struct device_node *np,
						  struct at91_pmc *pmc);
extern void __init of_at91sam9x5_clk_master_setup(struct device_node *np,
						  struct at91_pmc *pmc);

extern void __init of_at91rm9200_clk_sys_setup(struct device_node *np,
					       struct at91_pmc *pmc);

extern void __init of_at91rm9200_clk_periph_setup(struct device_node *np,
						  struct at91_pmc *pmc);
extern void __init of_at91sam9x5_clk_periph_setup(struct device_node *np,
						  struct at91_pmc *pmc);

extern void __init of_at91rm9200_clk_prog_setup(struct device_node *np,
						struct at91_pmc *pmc);
extern void __init of_at91sam9g45_clk_prog_setup(struct device_node *np,
						 struct at91_pmc *pmc);
extern void __init of_at91sam9x5_clk_prog_setup(struct device_node *np,
						struct at91_pmc *pmc);

#if defined(CONFIG_HAVE_AT91_UTMI)
extern void __init of_at91sam9x5_clk_utmi_setup(struct device_node *np,
						struct at91_pmc *pmc);
#endif

#if defined(CONFIG_HAVE_AT91_USB_CLK)
extern void __init of_at91rm9200_clk_usb_setup(struct device_node *np,
					       struct at91_pmc *pmc);
extern void __init of_at91sam9x5_clk_usb_setup(struct device_node *np,
					       struct at91_pmc *pmc);
extern void __init of_at91sam9n12_clk_usb_setup(struct device_node *np,
						struct at91_pmc *pmc);
#endif

#if defined(CONFIG_HAVE_AT91_SMD)
extern void __init of_at91sam9x5_clk_smd_setup(struct device_node *np,
					       struct at91_pmc *pmc);
#endif

#if defined(CONFIG_HAVE_AT91_H32MX)
extern void __init of_sama5d4_clk_h32mx_setup(struct device_node *np,
					      struct at91_pmc *pmc);
#endif

#endif /* __PMC_H_ */
