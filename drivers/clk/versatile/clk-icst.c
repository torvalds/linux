/*
 * Driver for the ICST307 VCO clock found in the ARM Reference designs.
 * We wrap the custom interface from <asm/hardware/icst.h> into the generic
 * clock framework.
 *
 * TODO: when all ARM reference designs are migrated to generic clocks, the
 * ICST clock code from the ARM tree should probably be merged into this
 * file.
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/clk-provider.h>

#include "clk-icst.h"

/**
 * struct clk_icst - ICST VCO clock wrapper
 * @hw: corresponding clock hardware entry
 * @params: parameters for this ICST instance
 * @rate: current rate
 * @setvco: function to commit ICST settings to hardware
 */
struct clk_icst {
	struct clk_hw hw;
	const struct icst_params *params;
	unsigned long rate;
	struct icst_vco (*getvco)(void);
	void (*setvco)(struct icst_vco);
};

#define to_icst(_hw) container_of(_hw, struct clk_icst, hw)

static unsigned long icst_recalc_rate(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	struct clk_icst *icst = to_icst(hw);
	struct icst_vco vco;

	vco = icst->getvco();
	icst->rate = icst_hz(icst->params, vco);
	return icst->rate;
}

static long icst_round_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long *prate)
{
	struct clk_icst *icst = to_icst(hw);
	struct icst_vco vco;

	vco = icst_hz_to_vco(icst->params, rate);
	return icst_hz(icst->params, vco);
}

static int icst_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate)
{
	struct clk_icst *icst = to_icst(hw);
	struct icst_vco vco;

	vco = icst_hz_to_vco(icst->params, rate);
	icst->rate = icst_hz(icst->params, vco);
	icst->setvco(vco);
	return 0;
}

static const struct clk_ops icst_ops = {
	.recalc_rate = icst_recalc_rate,
	.round_rate = icst_round_rate,
	.set_rate = icst_set_rate,
};

struct clk * __init icst_clk_register(struct device *dev,
				      const struct clk_icst_desc *desc)
{
	struct clk *clk;
	struct clk_icst *icst;
	struct clk_init_data init;

	icst = kzalloc(sizeof(struct clk_icst), GFP_KERNEL);
	if (!icst) {
		pr_err("could not allocate ICST clock!\n");
		return ERR_PTR(-ENOMEM);
	}
	init.name = "icst";
	init.ops = &icst_ops;
	init.flags = CLK_IS_ROOT;
	init.parent_names = NULL;
	init.num_parents = 0;
	icst->hw.init = &init;
	icst->params = desc->params;
	icst->getvco = desc->getvco;
	icst->setvco = desc->setvco;

	clk = clk_register(dev, &icst->hw);
	if (IS_ERR(clk))
		kfree(icst);

	return clk;
}
