// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
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
	struct at91_clk_pms pms;
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

static int clk_utmi_save_context(struct clk_hw *hw)
{
	struct clk_utmi *utmi = to_clk_utmi(hw);

	utmi->pms.status = clk_utmi_is_prepared(hw);

	return 0;
}

static void clk_utmi_restore_context(struct clk_hw *hw)
{
	struct clk_utmi *utmi = to_clk_utmi(hw);

	if (utmi->pms.status)
		clk_utmi_prepare(hw);
}

static const struct clk_ops utmi_ops = {
	.prepare = clk_utmi_prepare,
	.unprepare = clk_utmi_unprepare,
	.is_prepared = clk_utmi_is_prepared,
	.recalc_rate = clk_utmi_recalc_rate,
	.save_context = clk_utmi_save_context,
	.restore_context = clk_utmi_restore_context,
};

static struct clk_hw * __init
at91_clk_register_utmi_internal(struct regmap *regmap_pmc,
				struct regmap *regmap_sfr,
				const char *name, const char *parent_name,
				struct clk_hw *parent_hw,
				const struct clk_ops *ops, unsigned long flags)
{
	struct clk_utmi *utmi;
	struct clk_hw *hw;
	struct clk_init_data init = {};
	int ret;

	if (!(parent_name || parent_hw))
		return ERR_PTR(-EINVAL);

	utmi = kzalloc(sizeof(*utmi), GFP_KERNEL);
	if (!utmi)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = ops;
	if (parent_hw)
		init.parent_hws = (const struct clk_hw **)&parent_hw;
	else
		init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = flags;

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

struct clk_hw * __init
at91_clk_register_utmi(struct regmap *regmap_pmc, struct regmap *regmap_sfr,
		       const char *name, const char *parent_name,
		       struct clk_hw *parent_hw)
{
	return at91_clk_register_utmi_internal(regmap_pmc, regmap_sfr, name,
			parent_name, parent_hw, &utmi_ops, CLK_SET_RATE_GATE);
}

static int clk_utmi_sama7g5_prepare(struct clk_hw *hw)
{
	struct clk_utmi *utmi = to_clk_utmi(hw);
	struct clk_hw *hw_parent;
	unsigned long parent_rate;
	unsigned int val;

	hw_parent = clk_hw_get_parent(hw);
	parent_rate = clk_hw_get_rate(hw_parent);

	switch (parent_rate) {
	case 16000000:
		val = 0;
		break;
	case 20000000:
		val = 2;
		break;
	case 24000000:
		val = 3;
		break;
	case 32000000:
		val = 5;
		break;
	default:
		pr_err("UTMICK: unsupported main_xtal rate\n");
		return -EINVAL;
	}

	regmap_write(utmi->regmap_pmc, AT91_PMC_XTALF, val);

	return 0;

}

static int clk_utmi_sama7g5_is_prepared(struct clk_hw *hw)
{
	struct clk_utmi *utmi = to_clk_utmi(hw);
	struct clk_hw *hw_parent;
	unsigned long parent_rate;
	unsigned int val;

	hw_parent = clk_hw_get_parent(hw);
	parent_rate = clk_hw_get_rate(hw_parent);

	regmap_read(utmi->regmap_pmc, AT91_PMC_XTALF, &val);
	switch (val & 0x7) {
	case 0:
		if (parent_rate == 16000000)
			return 1;
		break;
	case 2:
		if (parent_rate == 20000000)
			return 1;
		break;
	case 3:
		if (parent_rate == 24000000)
			return 1;
		break;
	case 5:
		if (parent_rate == 32000000)
			return 1;
		break;
	default:
		break;
	}

	return 0;
}

static int clk_utmi_sama7g5_save_context(struct clk_hw *hw)
{
	struct clk_utmi *utmi = to_clk_utmi(hw);

	utmi->pms.status = clk_utmi_sama7g5_is_prepared(hw);

	return 0;
}

static void clk_utmi_sama7g5_restore_context(struct clk_hw *hw)
{
	struct clk_utmi *utmi = to_clk_utmi(hw);

	if (utmi->pms.status)
		clk_utmi_sama7g5_prepare(hw);
}

static const struct clk_ops sama7g5_utmi_ops = {
	.prepare = clk_utmi_sama7g5_prepare,
	.is_prepared = clk_utmi_sama7g5_is_prepared,
	.recalc_rate = clk_utmi_recalc_rate,
	.save_context = clk_utmi_sama7g5_save_context,
	.restore_context = clk_utmi_sama7g5_restore_context,
};

struct clk_hw * __init
at91_clk_sama7g5_register_utmi(struct regmap *regmap_pmc, const char *name,
			       const char *parent_name, struct clk_hw *parent_hw)
{
	return at91_clk_register_utmi_internal(regmap_pmc, NULL, name,
			parent_name, parent_hw, &sama7g5_utmi_ops, 0);
}
