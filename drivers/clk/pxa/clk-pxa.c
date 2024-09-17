// SPDX-License-Identifier: GPL-2.0-only
/*
 * Marvell PXA family clocks
 *
 * Copyright (C) 2014 Robert Jarzmik
 *
 * Common clock code for PXA clocks ("CKEN" type clocks + DT)
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/soc/pxa/smemc.h>

#include <dt-bindings/clock/pxa-clock.h>
#include "clk-pxa.h"

#define KHz 1000
#define MHz (1000 * 1000)

#define MDREFR_K0DB4	(1 << 29)	/* SDCLK0 Divide by 4 Control/Status */
#define MDREFR_K2FREE	(1 << 25)	/* SDRAM Free-Running Control */
#define MDREFR_K1FREE	(1 << 24)	/* SDRAM Free-Running Control */
#define MDREFR_K0FREE	(1 << 23)	/* SDRAM Free-Running Control */
#define MDREFR_SLFRSH	(1 << 22)	/* SDRAM Self-Refresh Control/Status */
#define MDREFR_APD	(1 << 20)	/* SDRAM/SSRAM Auto-Power-Down Enable */
#define MDREFR_K2DB2	(1 << 19)	/* SDCLK2 Divide by 2 Control/Status */
#define MDREFR_K2RUN	(1 << 18)	/* SDCLK2 Run Control/Status */
#define MDREFR_K1DB2	(1 << 17)	/* SDCLK1 Divide by 2 Control/Status */
#define MDREFR_K1RUN	(1 << 16)	/* SDCLK1 Run Control/Status */
#define MDREFR_E1PIN	(1 << 15)	/* SDCKE1 Level Control/Status */
#define MDREFR_K0DB2	(1 << 14)	/* SDCLK0 Divide by 2 Control/Status */
#define MDREFR_K0RUN	(1 << 13)	/* SDCLK0 Run Control/Status */
#define MDREFR_E0PIN	(1 << 12)	/* SDCKE0 Level Control/Status */
#define MDREFR_DB2_MASK	(MDREFR_K2DB2 | MDREFR_K1DB2)
#define MDREFR_DRI_MASK	0xFFF

static DEFINE_SPINLOCK(pxa_clk_lock);

static struct clk *pxa_clocks[CLK_MAX];
static struct clk_onecell_data onecell_data = {
	.clks = pxa_clocks,
	.clk_num = CLK_MAX,
};

struct pxa_clk {
	struct clk_hw hw;
	struct clk_fixed_factor lp;
	struct clk_fixed_factor hp;
	struct clk_gate gate;
	bool (*is_in_low_power)(void);
};

#define to_pxa_clk(_hw) container_of(_hw, struct pxa_clk, hw)

static unsigned long cken_recalc_rate(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	struct pxa_clk *pclk = to_pxa_clk(hw);
	struct clk_fixed_factor *fix;

	if (!pclk->is_in_low_power || pclk->is_in_low_power())
		fix = &pclk->lp;
	else
		fix = &pclk->hp;
	__clk_hw_set_clk(&fix->hw, hw);
	return clk_fixed_factor_ops.recalc_rate(&fix->hw, parent_rate);
}

static const struct clk_ops cken_rate_ops = {
	.recalc_rate = cken_recalc_rate,
};

static u8 cken_get_parent(struct clk_hw *hw)
{
	struct pxa_clk *pclk = to_pxa_clk(hw);

	if (!pclk->is_in_low_power)
		return 0;
	return pclk->is_in_low_power() ? 0 : 1;
}

static const struct clk_ops cken_mux_ops = {
	.get_parent = cken_get_parent,
	.set_parent = dummy_clk_set_parent,
};

void __init clkdev_pxa_register(int ckid, const char *con_id,
				const char *dev_id, struct clk *clk)
{
	if (!IS_ERR(clk) && (ckid != CLK_NONE))
		pxa_clocks[ckid] = clk;
	if (!IS_ERR(clk))
		clk_register_clkdev(clk, con_id, dev_id);
}

int __init clk_pxa_cken_init(const struct desc_clk_cken *clks,
			     int nb_clks, void __iomem *clk_regs)
{
	int i;
	struct pxa_clk *pxa_clk;
	struct clk *clk;

	for (i = 0; i < nb_clks; i++) {
		pxa_clk = kzalloc(sizeof(*pxa_clk), GFP_KERNEL);
		if (!pxa_clk)
			return -ENOMEM;
		pxa_clk->is_in_low_power = clks[i].is_in_low_power;
		pxa_clk->lp = clks[i].lp;
		pxa_clk->hp = clks[i].hp;
		pxa_clk->gate = clks[i].gate;
		pxa_clk->gate.reg = clk_regs + clks[i].cken_reg;
		pxa_clk->gate.lock = &pxa_clk_lock;
		clk = clk_register_composite(NULL, clks[i].name,
					     clks[i].parent_names, 2,
					     &pxa_clk->hw, &cken_mux_ops,
					     &pxa_clk->hw, &cken_rate_ops,
					     &pxa_clk->gate.hw, &clk_gate_ops,
					     clks[i].flags);
		clkdev_pxa_register(clks[i].ckid, clks[i].con_id,
				    clks[i].dev_id, clk);
	}
	return 0;
}

void __init clk_pxa_dt_common_init(struct device_node *np)
{
	of_clk_add_provider(np, of_clk_src_onecell_get, &onecell_data);
}

void pxa2xx_core_turbo_switch(bool on)
{
	unsigned long flags;
	unsigned int unused, clkcfg;

	local_irq_save(flags);

	asm("mrc p14, 0, %0, c6, c0, 0" : "=r" (clkcfg));
	clkcfg &= ~CLKCFG_TURBO & ~CLKCFG_HALFTURBO;
	if (on)
		clkcfg |= CLKCFG_TURBO;
	clkcfg |= CLKCFG_FCS;

	asm volatile(
	"	b	2f\n"
	"	.align	5\n"
	"1:	mcr	p14, 0, %1, c6, c0, 0\n"
	"	b	3f\n"
	"2:	b	1b\n"
	"3:	nop\n"
		: "=&r" (unused) : "r" (clkcfg));

	local_irq_restore(flags);
}

void pxa2xx_cpll_change(struct pxa2xx_freq *freq,
			u32 (*mdrefr_dri)(unsigned int),
			void __iomem *cccr)
{
	unsigned int clkcfg = freq->clkcfg;
	unsigned int unused, preset_mdrefr, postset_mdrefr;
	unsigned long flags;
	void __iomem *mdrefr = pxa_smemc_get_mdrefr();

	local_irq_save(flags);

	/* Calculate the next MDREFR.  If we're slowing down the SDRAM clock
	 * we need to preset the smaller DRI before the change.	 If we're
	 * speeding up we need to set the larger DRI value after the change.
	 */
	preset_mdrefr = postset_mdrefr = readl(mdrefr);
	if ((preset_mdrefr & MDREFR_DRI_MASK) > mdrefr_dri(freq->membus_khz)) {
		preset_mdrefr = (preset_mdrefr & ~MDREFR_DRI_MASK);
		preset_mdrefr |= mdrefr_dri(freq->membus_khz);
	}
	postset_mdrefr =
		(postset_mdrefr & ~MDREFR_DRI_MASK) |
		mdrefr_dri(freq->membus_khz);

	/* If we're dividing the memory clock by two for the SDRAM clock, this
	 * must be set prior to the change.  Clearing the divide must be done
	 * after the change.
	 */
	if (freq->div2) {
		preset_mdrefr  |= MDREFR_DB2_MASK;
		postset_mdrefr |= MDREFR_DB2_MASK;
	} else {
		postset_mdrefr &= ~MDREFR_DB2_MASK;
	}

	/* Set new the CCCR and prepare CLKCFG */
	writel(freq->cccr, cccr);

	asm volatile(
	"	ldr	r4, [%1]\n"
	"	b	2f\n"
	"	.align	5\n"
	"1:	str	%3, [%1]		/* preset the MDREFR */\n"
	"	mcr	p14, 0, %2, c6, c0, 0	/* set CLKCFG[FCS] */\n"
	"	str	%4, [%1]		/* postset the MDREFR */\n"
	"	b	3f\n"
	"2:	b	1b\n"
	"3:	nop\n"
	     : "=&r" (unused)
	     : "r" (mdrefr), "r" (clkcfg), "r" (preset_mdrefr),
	       "r" (postset_mdrefr)
	     : "r4", "r5");

	local_irq_restore(flags);
}

int pxa2xx_determine_rate(struct clk_rate_request *req,
			  struct pxa2xx_freq *freqs, int nb_freqs)
{
	int i, closest_below = -1, closest_above = -1;
	unsigned long rate;

	for (i = 0; i < nb_freqs; i++) {
		rate = freqs[i].cpll;
		if (rate == req->rate)
			break;
		if (rate < req->min_rate)
			continue;
		if (rate > req->max_rate)
			continue;
		if (rate <= req->rate)
			closest_below = i;
		if ((rate >= req->rate) && (closest_above == -1))
			closest_above = i;
	}

	req->best_parent_hw = NULL;

	if (i < nb_freqs) {
		rate = req->rate;
	} else if (closest_below >= 0) {
		rate = freqs[closest_below].cpll;
	} else if (closest_above >= 0) {
		rate = freqs[closest_above].cpll;
	} else {
		pr_debug("%s(rate=%lu) no match\n", __func__, req->rate);
		return -EINVAL;
	}

	pr_debug("%s(rate=%lu) rate=%lu\n", __func__, req->rate, rate);
	req->rate = rate;

	return 0;
}
