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
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <soc/at91/atmel-sfr.h>

#include "pmc.h"

/*
 * The purpose of this clock is to generate a 480 MHz signal. A different
 * rate can't be configured.
 */
#define UTMI_RATE	480000000

struct clk_utmi {
	struct clk_hw hw;
	struct regmap *regmap_pmc;
	struct regmap *regmap_sfr;
};

#define to_clk_utmi(hw) container_of(hw, struct clk_utmi, hw)

static inline bool clk_utmi_ready(struct regmap *regmap)
{
	unsigned int status;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return status & AT91_PMC_LOCKU;
}

static int clk_utmi_prepare(struct clk_hw *hw)
{
	struct clk_hw *hw_parent;
	struct clk_utmi *utmi = to_clk_utmi(hw);
	unsigned int uckr = AT91_PMC_UPLLEN | AT91_PMC_UPLLCOUNT |
			    AT91_PMC_BIASEN;
	unsigned int utmi_ref_clk_freq;
	unsigned long parent_rate;

	/*
	 * If mainck rate is different from 12 MHz, we have to configure the
	 * FREQ field of the SFR_UTMICKTRIM register to generate properly
	 * the utmi clock.
	 */
	hw_parent = clk_hw_get_parent(hw);
	parent_rate = clk_hw_get_rate(hw_parent);

	switch (parent_rate) {
	case 12000000:
		utmi_ref_clk_freq = 0;
		break;
	case 16000000:
		utmi_ref_clk_freq = 1;
		break;
	case 24000000:
		utmi_ref_clk_freq = 2;
		break;
	/*
	 * Not supported on SAMA5D2 but it's not an issue since MAINCK
	 * maximum value is 24 MHz.
	 */
	case 48000000:
		utmi_ref_clk_freq = 3;
		break;
	default:
		pr_err("UTMICK: unsupported mainck rate\n");
		return -EINVAL;
	}

	if (utmi->regmap_sfr) {
		regmap_update_bits(utmi->regmap_sfr, AT91_SFR_UTMICKTRIM,
				   AT91_UTMICKTRIM_FREQ, utmi_ref_clk_freq);
	} else if (utmi_ref_clk_freq) {
		pr_err("UTMICK: sfr node required\n");
		return -EINVAL;
	}

	regmap_update_bits(utmi->regmap_pmc, AT91_CKGR_UCKR, uckr, uckr);

	while (!clk_utmi_ready(utmi->regmap_pmc))
		cpu_relax();

	return 0;
}

static int clk_utmi_is_prepared(struct clk_hw *hw)
{
	struct clk_utmi *utmi = to_clk_utmi(hw);

	return clk_utmi_ready(utmi->regmap_pmc);
}

static void clk_utmi_unprepare(struct clk_hw *hw)
{
	struct clk_utmi *utmi = to_clk_utmi(hw);

	regmap_update_bits(utmi->regmap_pmc, AT91_CKGR_UCKR,
			   AT91_PMC_UPLLEN, 0);
}

static unsigned long clk_utmi_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	/* UTMI clk rate is fixed. */
	return UTMI_RATE;
}

static const struct clk_ops utmi_ops = {
	.prepare = clk_utmi_prepare,
	.unprepare = clk_utmi_unprepare,
	.is_prepared = clk_utmi_is_prepared,
	.recalc_rate = clk_utmi_recalc_rate,
};

static struct clk_hw * __init
at91_clk_register_utmi(struct regmap *regmap_pmc, struct regmap *regmap_sfr,
		       const char *name, const char *parent_name)
{
	struct clk_utmi *utmi;
	struct clk_hw *hw;
	struct clk_init_data init = {};
	int ret;

	utmi = kzalloc(sizeof(*utmi), GFP_KERNEL);
	if (!utmi)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &utmi_ops;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;
	init.flags = CLK_SET_RATE_GATE;

	utmi->hw.init = &init;
	utmi->regmap_pmc = regmap_pmc;
	utmi->regmap_sfr = regmap_sfr;

	hw = &utmi->hw;
	ret = clk_hw_register(NULL, &utmi->hw);
	if (ret) {
		kfree(utmi);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static void __init of_at91sam9x5_clk_utmi_setup(struct device_node *np)
{
	struct clk_hw *hw;
	const char *parent_name;
	const char *name = np->name;
	struct regmap *regmap_pmc, *regmap_sfr;

	parent_name = of_clk_get_parent_name(np, 0);

	of_property_read_string(np, "clock-output-names", &name);

	regmap_pmc = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap_pmc))
		return;

	/*
	 * If the device supports different mainck rates, this value has to be
	 * set in the UTMI Clock Trimming register.
	 * - 9x5: mainck supports several rates but it is indicated that a
	 *   12 MHz is needed in case of USB.
	 * - sama5d3 and sama5d2: mainck supports several rates. Configuring
	 *   the FREQ field of the UTMI Clock Trimming register is mandatory.
	 * - sama5d4: mainck is at 12 MHz.
	 *
	 * We only need to retrieve sama5d3 or sama5d2 sfr regmap.
	 */
	regmap_sfr = syscon_regmap_lookup_by_compatible("atmel,sama5d3-sfr");
	if (IS_ERR(regmap_sfr)) {
		regmap_sfr = syscon_regmap_lookup_by_compatible("atmel,sama5d2-sfr");
		if (IS_ERR(regmap_sfr))
			regmap_sfr = NULL;
	}

	hw = at91_clk_register_utmi(regmap_pmc, regmap_sfr, name, parent_name);
	if (IS_ERR(hw))
		return;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
	return;
}
CLK_OF_DECLARE(at91sam9x5_clk_utmi, "atmel,at91sam9x5-clk-utmi",
	       of_at91sam9x5_clk_utmi_setup);
