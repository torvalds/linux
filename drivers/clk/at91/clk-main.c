// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/at91_pmc.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pmc.h"

#define SLOW_CLOCK_FREQ		32768
#define MAINF_DIV		16
#define MAINFRDY_TIMEOUT	(((MAINF_DIV + 1) * USEC_PER_SEC) / \
				 SLOW_CLOCK_FREQ)
#define MAINF_LOOP_MIN_WAIT	(USEC_PER_SEC / SLOW_CLOCK_FREQ)
#define MAINF_LOOP_MAX_WAIT	MAINFRDY_TIMEOUT

#define MOR_KEY_MASK		(0xff << 16)

#define clk_main_parent_select(s)	(((s) & \
					(AT91_PMC_MOSCEN | \
					AT91_PMC_OSCBYPASS)) ? 1 : 0)

struct clk_main_osc {
	struct clk_hw hw;
	struct regmap *regmap;
	struct at91_clk_pms pms;
};

#define to_clk_main_osc(hw) container_of(hw, struct clk_main_osc, hw)

struct clk_main_rc_osc {
	struct clk_hw hw;
	struct regmap *regmap;
	unsigned long frequency;
	unsigned long accuracy;
	struct at91_clk_pms pms;
};

#define to_clk_main_rc_osc(hw) container_of(hw, struct clk_main_rc_osc, hw)

struct clk_rm9200_main {
	struct clk_hw hw;
	struct regmap *regmap;
};

#define to_clk_rm9200_main(hw) container_of(hw, struct clk_rm9200_main, hw)

struct clk_sam9x5_main {
	struct clk_hw hw;
	struct regmap *regmap;
	struct at91_clk_pms pms;
	u8 parent;
};

#define to_clk_sam9x5_main(hw) container_of(hw, struct clk_sam9x5_main, hw)

static inline bool clk_main_osc_ready(struct regmap *regmap)
{
	unsigned int status;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return status & AT91_PMC_MOSCS;
}

static int clk_main_osc_prepare(struct clk_hw *hw)
{
	struct clk_main_osc *osc = to_clk_main_osc(hw);
	struct regmap *regmap = osc->regmap;
	u32 tmp;

	regmap_read(regmap, AT91_CKGR_MOR, &tmp);
	tmp &= ~MOR_KEY_MASK;

	if (tmp & AT91_PMC_OSCBYPASS)
		return 0;

	if (!(tmp & AT91_PMC_MOSCEN)) {
		tmp |= AT91_PMC_MOSCEN | AT91_PMC_KEY;
		regmap_write(regmap, AT91_CKGR_MOR, tmp);
	}

	while (!clk_main_osc_ready(regmap))
		cpu_relax();

	return 0;
}

static void clk_main_osc_unprepare(struct clk_hw *hw)
{
	struct clk_main_osc *osc = to_clk_main_osc(hw);
	struct regmap *regmap = osc->regmap;
	u32 tmp;

	regmap_read(regmap, AT91_CKGR_MOR, &tmp);
	if (tmp & AT91_PMC_OSCBYPASS)
		return;

	if (!(tmp & AT91_PMC_MOSCEN))
		return;

	tmp &= ~(AT91_PMC_KEY | AT91_PMC_MOSCEN);
	regmap_write(regmap, AT91_CKGR_MOR, tmp | AT91_PMC_KEY);
}

static int clk_main_osc_is_prepared(struct clk_hw *hw)
{
	struct clk_main_osc *osc = to_clk_main_osc(hw);
	struct regmap *regmap = osc->regmap;
	u32 tmp, status;

	regmap_read(regmap, AT91_CKGR_MOR, &tmp);
	if (tmp & AT91_PMC_OSCBYPASS)
		return 1;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return (status & AT91_PMC_MOSCS) && clk_main_parent_select(tmp);
}

static int clk_main_osc_save_context(struct clk_hw *hw)
{
	struct clk_main_osc *osc = to_clk_main_osc(hw);

	osc->pms.status = clk_main_osc_is_prepared(hw);

	return 0;
}

static void clk_main_osc_restore_context(struct clk_hw *hw)
{
	struct clk_main_osc *osc = to_clk_main_osc(hw);

	if (osc->pms.status)
		clk_main_osc_prepare(hw);
}

static const struct clk_ops main_osc_ops = {
	.prepare = clk_main_osc_prepare,
	.unprepare = clk_main_osc_unprepare,
	.is_prepared = clk_main_osc_is_prepared,
	.save_context = clk_main_osc_save_context,
	.restore_context = clk_main_osc_restore_context,
};

struct clk_hw * __init
at91_clk_register_main_osc(struct regmap *regmap,
			   const char *name,
			   const char *parent_name,
			   struct clk_parent_data *parent_data,
			   bool bypass)
{
	struct clk_main_osc *osc;
	struct clk_init_data init = {};
	struct clk_hw *hw;
	int ret;

	if (!name || !(parent_name || parent_data))
		return ERR_PTR(-EINVAL);

	osc = kzalloc(sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &main_osc_ops;
	if (parent_data)
		init.parent_data = (const struct clk_parent_data *)parent_data;
	else
		init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_IGNORE_UNUSED;

	osc->hw.init = &init;
	osc->regmap = regmap;

	if (bypass)
		regmap_update_bits(regmap,
				   AT91_CKGR_MOR, MOR_KEY_MASK |
				   AT91_PMC_OSCBYPASS,
				   AT91_PMC_OSCBYPASS | AT91_PMC_KEY);

	hw = &osc->hw;
	ret = clk_hw_register(NULL, &osc->hw);
	if (ret) {
		kfree(osc);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static bool clk_main_rc_osc_ready(struct regmap *regmap)
{
	unsigned int status;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return !!(status & AT91_PMC_MOSCRCS);
}

static int clk_main_rc_osc_prepare(struct clk_hw *hw)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);
	struct regmap *regmap = osc->regmap;
	unsigned int mor;

	regmap_read(regmap, AT91_CKGR_MOR, &mor);

	if (!(mor & AT91_PMC_MOSCRCEN))
		regmap_update_bits(regmap, AT91_CKGR_MOR,
				   MOR_KEY_MASK | AT91_PMC_MOSCRCEN,
				   AT91_PMC_MOSCRCEN | AT91_PMC_KEY);

	while (!clk_main_rc_osc_ready(regmap))
		cpu_relax();

	return 0;
}

static void clk_main_rc_osc_unprepare(struct clk_hw *hw)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);
	struct regmap *regmap = osc->regmap;
	unsigned int mor;

	regmap_read(regmap, AT91_CKGR_MOR, &mor);

	if (!(mor & AT91_PMC_MOSCRCEN))
		return;

	regmap_update_bits(regmap, AT91_CKGR_MOR,
			   MOR_KEY_MASK | AT91_PMC_MOSCRCEN, AT91_PMC_KEY);
}

static int clk_main_rc_osc_is_prepared(struct clk_hw *hw)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);
	struct regmap *regmap = osc->regmap;
	unsigned int mor, status;

	regmap_read(regmap, AT91_CKGR_MOR, &mor);
	regmap_read(regmap, AT91_PMC_SR, &status);

	return (mor & AT91_PMC_MOSCRCEN) && (status & AT91_PMC_MOSCRCS);
}

static unsigned long clk_main_rc_osc_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);

	return osc->frequency;
}

static unsigned long clk_main_rc_osc_recalc_accuracy(struct clk_hw *hw,
						     unsigned long parent_acc)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);

	return osc->accuracy;
}

static int clk_main_rc_osc_save_context(struct clk_hw *hw)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);

	osc->pms.status = clk_main_rc_osc_is_prepared(hw);

	return 0;
}

static void clk_main_rc_osc_restore_context(struct clk_hw *hw)
{
	struct clk_main_rc_osc *osc = to_clk_main_rc_osc(hw);

	if (osc->pms.status)
		clk_main_rc_osc_prepare(hw);
}

static const struct clk_ops main_rc_osc_ops = {
	.prepare = clk_main_rc_osc_prepare,
	.unprepare = clk_main_rc_osc_unprepare,
	.is_prepared = clk_main_rc_osc_is_prepared,
	.recalc_rate = clk_main_rc_osc_recalc_rate,
	.recalc_accuracy = clk_main_rc_osc_recalc_accuracy,
	.save_context = clk_main_rc_osc_save_context,
	.restore_context = clk_main_rc_osc_restore_context,
};

struct clk_hw * __init
at91_clk_register_main_rc_osc(struct regmap *regmap,
			      const char *name,
			      u32 frequency, u32 accuracy)
{
	struct clk_main_rc_osc *osc;
	struct clk_init_data init;
	struct clk_hw *hw;
	int ret;

	if (!name || !frequency)
		return ERR_PTR(-EINVAL);

	osc = kzalloc(sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &main_rc_osc_ops;
	init.parent_names = NULL;
	init.num_parents = 0;
	init.flags = CLK_IGNORE_UNUSED;

	osc->hw.init = &init;
	osc->regmap = regmap;
	osc->frequency = frequency;
	osc->accuracy = accuracy;

	hw = &osc->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(osc);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static int clk_main_probe_frequency(struct regmap *regmap)
{
	unsigned long prep_time, timeout;
	unsigned int mcfr;

	timeout = jiffies + usecs_to_jiffies(MAINFRDY_TIMEOUT);
	do {
		prep_time = jiffies;
		regmap_read(regmap, AT91_CKGR_MCFR, &mcfr);
		if (mcfr & AT91_PMC_MAINRDY)
			return 0;
		if (system_state < SYSTEM_RUNNING)
			udelay(MAINF_LOOP_MIN_WAIT);
		else
			usleep_range(MAINF_LOOP_MIN_WAIT, MAINF_LOOP_MAX_WAIT);
	} while (time_before(prep_time, timeout));

	return -ETIMEDOUT;
}

static unsigned long clk_main_recalc_rate(struct regmap *regmap,
					  unsigned long parent_rate)
{
	unsigned int mcfr;

	if (parent_rate)
		return parent_rate;

	pr_warn("Main crystal frequency not set, using approximate value\n");
	regmap_read(regmap, AT91_CKGR_MCFR, &mcfr);
	if (!(mcfr & AT91_PMC_MAINRDY))
		return 0;

	return ((mcfr & AT91_PMC_MAINF) * SLOW_CLOCK_FREQ) / MAINF_DIV;
}

static int clk_rm9200_main_prepare(struct clk_hw *hw)
{
	struct clk_rm9200_main *clkmain = to_clk_rm9200_main(hw);

	return clk_main_probe_frequency(clkmain->regmap);
}

static int clk_rm9200_main_is_prepared(struct clk_hw *hw)
{
	struct clk_rm9200_main *clkmain = to_clk_rm9200_main(hw);
	unsigned int status;

	regmap_read(clkmain->regmap, AT91_CKGR_MCFR, &status);

	return !!(status & AT91_PMC_MAINRDY);
}

static unsigned long clk_rm9200_main_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_rm9200_main *clkmain = to_clk_rm9200_main(hw);

	return clk_main_recalc_rate(clkmain->regmap, parent_rate);
}

static const struct clk_ops rm9200_main_ops = {
	.prepare = clk_rm9200_main_prepare,
	.is_prepared = clk_rm9200_main_is_prepared,
	.recalc_rate = clk_rm9200_main_recalc_rate,
};

struct clk_hw * __init
at91_clk_register_rm9200_main(struct regmap *regmap,
			      const char *name,
			      const char *parent_name,
			      struct clk_hw *parent_hw)
{
	struct clk_rm9200_main *clkmain;
	struct clk_init_data init = {};
	struct clk_hw *hw;
	int ret;

	if (!name)
		return ERR_PTR(-EINVAL);

	if (!(parent_name || parent_hw))
		return ERR_PTR(-EINVAL);

	clkmain = kzalloc(sizeof(*clkmain), GFP_KERNEL);
	if (!clkmain)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &rm9200_main_ops;
	if (parent_hw)
		init.parent_hws = (const struct clk_hw **)&parent_hw;
	else
		init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = 0;

	clkmain->hw.init = &init;
	clkmain->regmap = regmap;

	hw = &clkmain->hw;
	ret = clk_hw_register(NULL, &clkmain->hw);
	if (ret) {
		kfree(clkmain);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static inline bool clk_sam9x5_main_ready(struct regmap *regmap)
{
	unsigned int status;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return !!(status & AT91_PMC_MOSCSELS);
}

static int clk_sam9x5_main_prepare(struct clk_hw *hw)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);
	struct regmap *regmap = clkmain->regmap;

	while (!clk_sam9x5_main_ready(regmap))
		cpu_relax();

	return clk_main_probe_frequency(regmap);
}

static int clk_sam9x5_main_is_prepared(struct clk_hw *hw)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);

	return clk_sam9x5_main_ready(clkmain->regmap);
}

static unsigned long clk_sam9x5_main_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);

	return clk_main_recalc_rate(clkmain->regmap, parent_rate);
}

static int clk_sam9x5_main_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);
	struct regmap *regmap = clkmain->regmap;
	unsigned int tmp;

	if (index > 1)
		return -EINVAL;

	regmap_read(regmap, AT91_CKGR_MOR, &tmp);

	if (index && !(tmp & AT91_PMC_MOSCSEL))
		tmp = AT91_PMC_MOSCSEL;
	else if (!index && (tmp & AT91_PMC_MOSCSEL))
		tmp = 0;
	else
		return 0;

	regmap_update_bits(regmap, AT91_CKGR_MOR,
			   AT91_PMC_MOSCSEL | MOR_KEY_MASK,
			   tmp | AT91_PMC_KEY);

	while (!clk_sam9x5_main_ready(regmap))
		cpu_relax();

	return 0;
}

static u8 clk_sam9x5_main_get_parent(struct clk_hw *hw)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);
	unsigned int status;

	regmap_read(clkmain->regmap, AT91_CKGR_MOR, &status);

	return clk_main_parent_select(status);
}

static int clk_sam9x5_main_save_context(struct clk_hw *hw)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);

	clkmain->pms.status = clk_main_rc_osc_is_prepared(&clkmain->hw);
	clkmain->pms.parent = clk_sam9x5_main_get_parent(&clkmain->hw);

	return 0;
}

static void clk_sam9x5_main_restore_context(struct clk_hw *hw)
{
	struct clk_sam9x5_main *clkmain = to_clk_sam9x5_main(hw);
	int ret;

	ret = clk_sam9x5_main_set_parent(hw, clkmain->pms.parent);
	if (ret)
		return;

	if (clkmain->pms.status)
		clk_sam9x5_main_prepare(hw);
}

static const struct clk_ops sam9x5_main_ops = {
	.prepare = clk_sam9x5_main_prepare,
	.is_prepared = clk_sam9x5_main_is_prepared,
	.recalc_rate = clk_sam9x5_main_recalc_rate,
	.determine_rate = clk_hw_determine_rate_no_reparent,
	.set_parent = clk_sam9x5_main_set_parent,
	.get_parent = clk_sam9x5_main_get_parent,
	.save_context = clk_sam9x5_main_save_context,
	.restore_context = clk_sam9x5_main_restore_context,
};

struct clk_hw * __init
at91_clk_register_sam9x5_main(struct regmap *regmap,
			      const char *name,
			      const char **parent_names,
			      struct clk_hw **parent_hws,
			      int num_parents)
{
	struct clk_sam9x5_main *clkmain;
	struct clk_init_data init = {};
	unsigned int status;
	struct clk_hw *hw;
	int ret;

	if (!name)
		return ERR_PTR(-EINVAL);

	if (!(parent_hws || parent_names) || !num_parents)
		return ERR_PTR(-EINVAL);

	clkmain = kzalloc(sizeof(*clkmain), GFP_KERNEL);
	if (!clkmain)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &sam9x5_main_ops;
	if (parent_hws)
		init.parent_hws = (const struct clk_hw **)parent_hws;
	else
		init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_SET_PARENT_GATE;

	clkmain->hw.init = &init;
	clkmain->regmap = regmap;
	regmap_read(clkmain->regmap, AT91_CKGR_MOR, &status);
	clkmain->parent = clk_main_parent_select(status);

	hw = &clkmain->hw;
	ret = clk_hw_register(NULL, &clkmain->hw);
	if (ret) {
		kfree(clkmain);
		hw = ERR_PTR(ret);
	}

	return hw;
}
