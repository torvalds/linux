// SPDX-License-Identifier: GPL-2.0+
/*
 *  Copyright (C) 2019 Microchip Technology Inc.
 *
 */

#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/at91_pmc.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pmc.h"

#define PMC_PLL_CTRL0	0xc
#define		PMC_PLL_CTRL0_DIV_MSK		GENMASK(7, 0)
#define		PMC_PLL_CTRL0_ENPLL		BIT(28)
#define		PMC_PLL_CTRL0_ENPLLCK		BIT(29)
#define		PMC_PLL_CTRL0_ENLOCK		BIT(31)

#define PMC_PLL_CTRL1	0x10
#define		PMC_PLL_CTRL1_FRACR_MSK		GENMASK(21, 0)
#define		PMC_PLL_CTRL1_MUL_MSK		GENMASK(30, 24)

#define PMC_PLL_ACR	0x18
#define		PMC_PLL_ACR_DEFAULT		0x1b040010UL
#define		PMC_PLL_ACR_UTMIVR		BIT(12)
#define		PMC_PLL_ACR_UTMIBG		BIT(13)
#define		PMC_PLL_ACR_LOOP_FILTER_MSK	GENMASK(31, 24)

#define PMC_PLL_UPDT	0x1c
#define		PMC_PLL_UPDT_UPDATE		BIT(8)

#define PMC_PLL_ISR0	0xec

#define PLL_DIV_MAX		(FIELD_GET(PMC_PLL_CTRL0_DIV_MSK, UINT_MAX) + 1)
#define UPLL_DIV		2
#define PLL_MUL_MAX		(FIELD_GET(PMC_PLL_CTRL1_MUL_MSK, UINT_MAX) + 1)

#define PLL_MAX_ID		1

struct sam9x60_pll {
	struct clk_hw hw;
	struct regmap *regmap;
	spinlock_t *lock;
	const struct clk_pll_characteristics *characteristics;
	u32 frac;
	u8 id;
	u8 div;
	u16 mul;
};

#define to_sam9x60_pll(hw) container_of(hw, struct sam9x60_pll, hw)

static inline bool sam9x60_pll_ready(struct regmap *regmap, int id)
{
	unsigned int status;

	regmap_read(regmap, PMC_PLL_ISR0, &status);

	return !!(status & BIT(id));
}

static int sam9x60_pll_prepare(struct clk_hw *hw)
{
	struct sam9x60_pll *pll = to_sam9x60_pll(hw);
	struct regmap *regmap = pll->regmap;
	unsigned long flags;
	u8 div;
	u16 mul;
	u32 val;

	spin_lock_irqsave(pll->lock, flags);
	regmap_write(regmap, PMC_PLL_UPDT, pll->id);

	regmap_read(regmap, PMC_PLL_CTRL0, &val);
	div = FIELD_GET(PMC_PLL_CTRL0_DIV_MSK, val);

	regmap_read(regmap, PMC_PLL_CTRL1, &val);
	mul = FIELD_GET(PMC_PLL_CTRL1_MUL_MSK, val);

	if (sam9x60_pll_ready(regmap, pll->id) &&
	    (div == pll->div && mul == pll->mul)) {
		spin_unlock_irqrestore(pll->lock, flags);
		return 0;
	}

	/* Recommended value for PMC_PLL_ACR */
	val = PMC_PLL_ACR_DEFAULT;
	regmap_write(regmap, PMC_PLL_ACR, val);

	regmap_write(regmap, PMC_PLL_CTRL1,
		     FIELD_PREP(PMC_PLL_CTRL1_MUL_MSK, pll->mul));

	if (pll->characteristics->upll) {
		/* Enable the UTMI internal bandgap */
		val |= PMC_PLL_ACR_UTMIBG;
		regmap_write(regmap, PMC_PLL_ACR, val);

		udelay(10);

		/* Enable the UTMI internal regulator */
		val |= PMC_PLL_ACR_UTMIVR;
		regmap_write(regmap, PMC_PLL_ACR, val);

		udelay(10);
	}

	regmap_update_bits(regmap, PMC_PLL_UPDT,
			   PMC_PLL_UPDT_UPDATE, PMC_PLL_UPDT_UPDATE);

	regmap_write(regmap, PMC_PLL_CTRL0,
		     PMC_PLL_CTRL0_ENLOCK | PMC_PLL_CTRL0_ENPLL |
		     PMC_PLL_CTRL0_ENPLLCK | pll->div);

	regmap_update_bits(regmap, PMC_PLL_UPDT,
			   PMC_PLL_UPDT_UPDATE, PMC_PLL_UPDT_UPDATE);

	while (!sam9x60_pll_ready(regmap, pll->id))
		cpu_relax();

	spin_unlock_irqrestore(pll->lock, flags);

	return 0;
}

static int sam9x60_pll_is_prepared(struct clk_hw *hw)
{
	struct sam9x60_pll *pll = to_sam9x60_pll(hw);

	return sam9x60_pll_ready(pll->regmap, pll->id);
}

static void sam9x60_pll_unprepare(struct clk_hw *hw)
{
	struct sam9x60_pll *pll = to_sam9x60_pll(hw);
	unsigned long flags;

	spin_lock_irqsave(pll->lock, flags);

	regmap_write(pll->regmap, PMC_PLL_UPDT, pll->id);

	regmap_update_bits(pll->regmap, PMC_PLL_CTRL0,
			   PMC_PLL_CTRL0_ENPLLCK, 0);

	regmap_update_bits(pll->regmap, PMC_PLL_UPDT,
			   PMC_PLL_UPDT_UPDATE, PMC_PLL_UPDT_UPDATE);

	regmap_update_bits(pll->regmap, PMC_PLL_CTRL0, PMC_PLL_CTRL0_ENPLL, 0);

	if (pll->characteristics->upll)
		regmap_update_bits(pll->regmap, PMC_PLL_ACR,
				   PMC_PLL_ACR_UTMIBG | PMC_PLL_ACR_UTMIVR, 0);

	regmap_update_bits(pll->regmap, PMC_PLL_UPDT,
			   PMC_PLL_UPDT_UPDATE, PMC_PLL_UPDT_UPDATE);

	spin_unlock_irqrestore(pll->lock, flags);
}

static unsigned long sam9x60_pll_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct sam9x60_pll *pll = to_sam9x60_pll(hw);

	return (parent_rate * (pll->mul + 1)) / (pll->div + 1);
}

static long sam9x60_pll_get_best_div_mul(struct sam9x60_pll *pll,
					 unsigned long rate,
					 unsigned long parent_rate,
					 bool update)
{
	const struct clk_pll_characteristics *characteristics =
							pll->characteristics;
	unsigned long bestremainder = ULONG_MAX;
	unsigned long maxdiv, mindiv, tmpdiv;
	long bestrate = -ERANGE;
	unsigned long bestdiv = 0;
	unsigned long bestmul = 0;
	unsigned long bestfrac = 0;

	if (rate < characteristics->output[0].min ||
	    rate > characteristics->output[0].max)
		return -ERANGE;

	if (!pll->characteristics->upll) {
		mindiv = parent_rate / rate;
		if (mindiv < 2)
			mindiv = 2;

		maxdiv = DIV_ROUND_UP(parent_rate * PLL_MUL_MAX, rate);
		if (maxdiv > PLL_DIV_MAX)
			maxdiv = PLL_DIV_MAX;
	} else {
		mindiv = maxdiv = UPLL_DIV;
	}

	for (tmpdiv = mindiv; tmpdiv <= maxdiv; tmpdiv++) {
		unsigned long remainder;
		unsigned long tmprate;
		unsigned long tmpmul;
		unsigned long tmpfrac = 0;

		/*
		 * Calculate the multiplier associated with the current
		 * divider that provide the closest rate to the requested one.
		 */
		tmpmul = mult_frac(rate, tmpdiv, parent_rate);
		tmprate = mult_frac(parent_rate, tmpmul, tmpdiv);
		remainder = rate - tmprate;

		if (remainder) {
			tmpfrac = DIV_ROUND_CLOSEST_ULL((u64)remainder * tmpdiv * (1 << 22),
							parent_rate);

			tmprate += DIV_ROUND_CLOSEST_ULL((u64)tmpfrac * parent_rate,
							 tmpdiv * (1 << 22));

			if (tmprate > rate)
				remainder = tmprate - rate;
			else
				remainder = rate - tmprate;
		}

		/*
		 * Compare the remainder with the best remainder found until
		 * now and elect a new best multiplier/divider pair if the
		 * current remainder is smaller than the best one.
		 */
		if (remainder < bestremainder) {
			bestremainder = remainder;
			bestdiv = tmpdiv;
			bestmul = tmpmul;
			bestrate = tmprate;
			bestfrac = tmpfrac;
		}

		/* We've found a perfect match!  */
		if (!remainder)
			break;
	}

	/* Check if bestrate is a valid output rate  */
	if (bestrate < characteristics->output[0].min &&
	    bestrate > characteristics->output[0].max)
		return -ERANGE;

	if (update) {
		pll->div = bestdiv - 1;
		pll->mul = bestmul - 1;
		pll->frac = bestfrac;
	}

	return bestrate;
}

static long sam9x60_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	struct sam9x60_pll *pll = to_sam9x60_pll(hw);

	return sam9x60_pll_get_best_div_mul(pll, rate, *parent_rate, false);
}

static int sam9x60_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct sam9x60_pll *pll = to_sam9x60_pll(hw);

	return sam9x60_pll_get_best_div_mul(pll, rate, parent_rate, true);
}

static const struct clk_ops pll_ops = {
	.prepare = sam9x60_pll_prepare,
	.unprepare = sam9x60_pll_unprepare,
	.is_prepared = sam9x60_pll_is_prepared,
	.recalc_rate = sam9x60_pll_recalc_rate,
	.round_rate = sam9x60_pll_round_rate,
	.set_rate = sam9x60_pll_set_rate,
};

struct clk_hw * __init
sam9x60_clk_register_pll(struct regmap *regmap, spinlock_t *lock,
			 const char *name, const char *parent_name, u8 id,
			 const struct clk_pll_characteristics *characteristics)
{
	struct sam9x60_pll *pll;
	struct clk_hw *hw;
	struct clk_init_data init;
	unsigned int pllr;
	int ret;

	if (id > PLL_MAX_ID)
		return ERR_PTR(-EINVAL);

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &pll_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_SET_RATE_GATE;

	pll->id = id;
	pll->hw.init = &init;
	pll->characteristics = characteristics;
	pll->regmap = regmap;
	pll->lock = lock;

	regmap_write(regmap, PMC_PLL_UPDT, id);
	regmap_read(regmap, PMC_PLL_CTRL0, &pllr);
	pll->div = FIELD_GET(PMC_PLL_CTRL0_DIV_MSK, pllr);
	regmap_read(regmap, PMC_PLL_CTRL1, &pllr);
	pll->mul = FIELD_GET(PMC_PLL_CTRL1_MUL_MSK, pllr);

	hw = &pll->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(pll);
		hw = ERR_PTR(ret);
	}

	return hw;
}

