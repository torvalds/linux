// SPDX-License-Identifier: GPL-2.0-only
/*
 * clk-flexgen.c
 *
 * Copyright (C) ST-Microelectronics SA 2013
 * Author:  Maxime Coquelin <maxime.coquelin@st.com> for ST-Microelectronics.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/of_address.h>

struct clkgen_clk_out {
	const char *name;
	unsigned long flags;
};

struct clkgen_data {
	unsigned long flags;
	bool mode;
	const struct clkgen_clk_out *outputs;
	const unsigned int outputs_nb;
};

struct flexgen {
	struct clk_hw hw;

	/* Crossbar */
	struct clk_mux mux;
	/* Pre-divisor's gate */
	struct clk_gate pgate;
	/* Pre-divisor */
	struct clk_divider pdiv;
	/* Final divisor's gate */
	struct clk_gate fgate;
	/* Final divisor */
	struct clk_divider fdiv;
	/* Asynchronous mode control */
	struct clk_gate sync;
	/* hw control flags */
	bool control_mode;
};

#define to_flexgen(_hw) container_of(_hw, struct flexgen, hw)
#define to_clk_gate(_hw) container_of(_hw, struct clk_gate, hw)

static int flexgen_enable(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *pgate_hw = &flexgen->pgate.hw;
	struct clk_hw *fgate_hw = &flexgen->fgate.hw;

	__clk_hw_set_clk(pgate_hw, hw);
	__clk_hw_set_clk(fgate_hw, hw);

	clk_gate_ops.enable(pgate_hw);

	clk_gate_ops.enable(fgate_hw);

	pr_debug("%s: flexgen output enabled\n", clk_hw_get_name(hw));
	return 0;
}

static void flexgen_disable(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *fgate_hw = &flexgen->fgate.hw;

	/* disable only the final gate */
	__clk_hw_set_clk(fgate_hw, hw);

	clk_gate_ops.disable(fgate_hw);

	pr_debug("%s: flexgen output disabled\n", clk_hw_get_name(hw));
}

static int flexgen_is_enabled(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *fgate_hw = &flexgen->fgate.hw;

	__clk_hw_set_clk(fgate_hw, hw);

	if (!clk_gate_ops.is_enabled(fgate_hw))
		return 0;

	return 1;
}

static u8 flexgen_get_parent(struct clk_hw *hw)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *mux_hw = &flexgen->mux.hw;

	__clk_hw_set_clk(mux_hw, hw);

	return clk_mux_ops.get_parent(mux_hw);
}

static int flexgen_set_parent(struct clk_hw *hw, u8 index)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *mux_hw = &flexgen->mux.hw;

	__clk_hw_set_clk(mux_hw, hw);

	return clk_mux_ops.set_parent(mux_hw, index);
}

static inline unsigned long
clk_best_div(unsigned long parent_rate, unsigned long rate)
{
	return parent_rate / rate + ((rate > (2*(parent_rate % rate))) ? 0 : 1);
}

static int flexgen_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	unsigned long div;

	/* Round div according to exact prate and wished rate */
	div = clk_best_div(req->best_parent_rate, req->rate);

	if (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) {
		req->best_parent_rate = req->rate * div;
		return 0;
	}

	req->rate = req->best_parent_rate / div;
	return 0;
}

static unsigned long flexgen_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *pdiv_hw = &flexgen->pdiv.hw;
	struct clk_hw *fdiv_hw = &flexgen->fdiv.hw;
	unsigned long mid_rate;

	__clk_hw_set_clk(pdiv_hw, hw);
	__clk_hw_set_clk(fdiv_hw, hw);

	mid_rate = clk_divider_ops.recalc_rate(pdiv_hw, parent_rate);

	return clk_divider_ops.recalc_rate(fdiv_hw, mid_rate);
}

static int flexgen_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct flexgen *flexgen = to_flexgen(hw);
	struct clk_hw *pdiv_hw = &flexgen->pdiv.hw;
	struct clk_hw *fdiv_hw = &flexgen->fdiv.hw;
	struct clk_hw *sync_hw = &flexgen->sync.hw;
	struct clk_gate *config = to_clk_gate(sync_hw);
	unsigned long div = 0;
	int ret = 0;
	u32 reg;

	__clk_hw_set_clk(pdiv_hw, hw);
	__clk_hw_set_clk(fdiv_hw, hw);

	if (flexgen->control_mode) {
		reg = readl(config->reg);
		reg &= ~BIT(config->bit_idx);
		writel(reg, config->reg);
	}

	div = clk_best_div(parent_rate, rate);

	/*
	* pdiv is mainly targeted for low freq results, while fdiv
	* should be used for div <= 64. The other way round can
	* lead to 'duty cycle' issues.
	*/

	if (div <= 64) {
		clk_divider_ops.set_rate(pdiv_hw, parent_rate, parent_rate);
		ret = clk_divider_ops.set_rate(fdiv_hw, rate, rate * div);
	} else {
		clk_divider_ops.set_rate(fdiv_hw, parent_rate, parent_rate);
		ret = clk_divider_ops.set_rate(pdiv_hw, rate, rate * div);
	}

	return ret;
}

static const struct clk_ops flexgen_ops = {
	.enable = flexgen_enable,
	.disable = flexgen_disable,
	.is_enabled = flexgen_is_enabled,
	.get_parent = flexgen_get_parent,
	.set_parent = flexgen_set_parent,
	.determine_rate = flexgen_determine_rate,
	.recalc_rate = flexgen_recalc_rate,
	.set_rate = flexgen_set_rate,
};

static struct clk *clk_register_flexgen(const char *name,
				const char **parent_names, u8 num_parents,
				void __iomem *reg, spinlock_t *lock, u32 idx,
				unsigned long flexgen_flags, bool mode) {
	struct flexgen *fgxbar;
	struct clk *clk;
	struct clk_init_data init;
	u32  xbar_shift;
	void __iomem *xbar_reg, *fdiv_reg;

	fgxbar = kzalloc(sizeof(struct flexgen), GFP_KERNEL);
	if (!fgxbar)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &flexgen_ops;
	init.flags = CLK_GET_RATE_NOCACHE | flexgen_flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	xbar_reg = reg + 0x18 + (idx & ~0x3);
	xbar_shift = (idx % 4) * 0x8;
	fdiv_reg = reg + 0x164 + idx * 4;

	/* Crossbar element config */
	fgxbar->mux.lock = lock;
	fgxbar->mux.mask = BIT(6) - 1;
	fgxbar->mux.reg = xbar_reg;
	fgxbar->mux.shift = xbar_shift;
	fgxbar->mux.table = NULL;


	/* Pre-divider's gate config (in xbar register)*/
	fgxbar->pgate.lock = lock;
	fgxbar->pgate.reg = xbar_reg;
	fgxbar->pgate.bit_idx = xbar_shift + 6;

	/* Pre-divider config */
	fgxbar->pdiv.lock = lock;
	fgxbar->pdiv.reg = reg + 0x58 + idx * 4;
	fgxbar->pdiv.width = 10;

	/* Final divider's gate config */
	fgxbar->fgate.lock = lock;
	fgxbar->fgate.reg = fdiv_reg;
	fgxbar->fgate.bit_idx = 6;

	/* Final divider config */
	fgxbar->fdiv.lock = lock;
	fgxbar->fdiv.reg = fdiv_reg;
	fgxbar->fdiv.width = 6;

	/* Final divider sync config */
	fgxbar->sync.lock = lock;
	fgxbar->sync.reg = fdiv_reg;
	fgxbar->sync.bit_idx = 7;

	fgxbar->control_mode = mode;

	fgxbar->hw.init = &init;

	clk = clk_register(NULL, &fgxbar->hw);
	if (IS_ERR(clk))
		kfree(fgxbar);
	else
		pr_debug("%s: parent %s rate %u\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			(unsigned int)clk_get_rate(clk));
	return clk;
}

static const char ** __init flexgen_get_parents(struct device_node *np,
						       int *num_parents)
{
	const char **parents;
	unsigned int nparents;

	nparents = of_clk_get_parent_count(np);
	if (WARN_ON(!nparents))
		return NULL;

	parents = kcalloc(nparents, sizeof(const char *), GFP_KERNEL);
	if (!parents)
		return NULL;

	*num_parents = of_clk_parent_fill(np, parents, nparents);

	return parents;
}

static const struct clkgen_data clkgen_audio = {
	.flags = CLK_SET_RATE_PARENT,
};

static const struct clkgen_data clkgen_video = {
	.flags = CLK_SET_RATE_PARENT,
	.mode = 1,
};

static const struct clkgen_clk_out clkgen_stih407_a0_clk_out[] = {
	/* This clk needs to be on so that memory interface is accessible */
	{ .name = "clk-ic-lmi0", .flags = CLK_IS_CRITICAL },
};

static const struct clkgen_data clkgen_stih407_a0 = {
	.outputs = clkgen_stih407_a0_clk_out,
	.outputs_nb = ARRAY_SIZE(clkgen_stih407_a0_clk_out),
};

static const struct clkgen_clk_out clkgen_stih410_a0_clk_out[] = {
	/* Those clks need to be on so that memory interface is accessible */
	{ .name = "clk-ic-lmi0", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-ic-lmi1", .flags = CLK_IS_CRITICAL },
};

static const struct clkgen_data clkgen_stih410_a0 = {
	.outputs = clkgen_stih410_a0_clk_out,
	.outputs_nb = ARRAY_SIZE(clkgen_stih410_a0_clk_out),
};

static const struct clkgen_clk_out clkgen_stih407_c0_clk_out[] = {
	{ .name = "clk-icn-gpu", },
	{ .name = "clk-fdma", },
	{ .name = "clk-nand", },
	{ .name = "clk-hva", },
	{ .name = "clk-proc-stfe", },
	{ .name = "clk-proc-tp", },
	{ .name = "clk-rx-icn-dmu", },
	{ .name = "clk-rx-icn-hva", },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-icn-cpu", .flags = CLK_IS_CRITICAL },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-tx-icn-dmu", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-mmc-0", },
	{ .name = "clk-mmc-1", },
	{ .name = "clk-jpegdec", },
	/* This clk needs to be on to keep A9 running */
	{ .name = "clk-ext2fa9", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-ic-bdisp-0", },
	{ .name = "clk-ic-bdisp-1", },
	{ .name = "clk-pp-dmu", },
	{ .name = "clk-vid-dmu", },
	{ .name = "clk-dss-lpc", },
	{ .name = "clk-st231-aud-0", },
	{ .name = "clk-st231-gp-1", },
	{ .name = "clk-st231-dmu", },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-icn-lmi", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-tx-icn-disp-1", },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-icn-sbc", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-stfe-frc2", },
	{ .name = "clk-eth-phy", },
	{ .name = "clk-eth-ref-phyclk", },
	{ .name = "clk-flash-promip", },
	{ .name = "clk-main-disp", },
	{ .name = "clk-aux-disp", },
	{ .name = "clk-compo-dvp", },
};

static const struct clkgen_data clkgen_stih407_c0 = {
	.outputs = clkgen_stih407_c0_clk_out,
	.outputs_nb = ARRAY_SIZE(clkgen_stih407_c0_clk_out),
};

static const struct clkgen_clk_out clkgen_stih410_c0_clk_out[] = {
	{ .name = "clk-icn-gpu", },
	{ .name = "clk-fdma", },
	{ .name = "clk-nand", },
	{ .name = "clk-hva", },
	{ .name = "clk-proc-stfe", },
	{ .name = "clk-proc-tp", },
	{ .name = "clk-rx-icn-dmu", },
	{ .name = "clk-rx-icn-hva", },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-icn-cpu", .flags = CLK_IS_CRITICAL },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-tx-icn-dmu", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-mmc-0", },
	{ .name = "clk-mmc-1", },
	{ .name = "clk-jpegdec", },
	/* This clk needs to be on to keep A9 running */
	{ .name = "clk-ext2fa9", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-ic-bdisp-0", },
	{ .name = "clk-ic-bdisp-1", },
	{ .name = "clk-pp-dmu", },
	{ .name = "clk-vid-dmu", },
	{ .name = "clk-dss-lpc", },
	{ .name = "clk-st231-aud-0", },
	{ .name = "clk-st231-gp-1", },
	{ .name = "clk-st231-dmu", },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-icn-lmi", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-tx-icn-disp-1", },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-icn-sbc", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-stfe-frc2", },
	{ .name = "clk-eth-phy", },
	{ .name = "clk-eth-ref-phyclk", },
	{ .name = "clk-flash-promip", },
	{ .name = "clk-main-disp", },
	{ .name = "clk-aux-disp", },
	{ .name = "clk-compo-dvp", },
	{ .name = "clk-tx-icn-hades", },
	{ .name = "clk-rx-icn-hades", },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-icn-reg-16", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-pp-hades", },
	{ .name = "clk-clust-hades", },
	{ .name = "clk-hwpe-hades", },
	{ .name = "clk-fc-hades", },
};

static const struct clkgen_data clkgen_stih410_c0 = {
	.outputs = clkgen_stih410_c0_clk_out,
	.outputs_nb = ARRAY_SIZE(clkgen_stih410_c0_clk_out),
};

static const struct clkgen_clk_out clkgen_stih418_c0_clk_out[] = {
	{ .name = "clk-icn-gpu", },
	{ .name = "clk-fdma", },
	{ .name = "clk-nand", },
	{ .name = "clk-hva", },
	{ .name = "clk-proc-stfe", },
	{ .name = "clk-tp", },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-rx-icn-dmu", .flags = CLK_IS_CRITICAL },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-rx-icn-hva", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-icn-cpu", .flags = CLK_IS_CRITICAL },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-tx-icn-dmu", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-mmc-0", },
	{ .name = "clk-mmc-1", },
	{ .name = "clk-jpegdec", },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-icn-reg", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-proc-bdisp-0", },
	{ .name = "clk-proc-bdisp-1", },
	{ .name = "clk-pp-dmu", },
	{ .name = "clk-vid-dmu", },
	{ .name = "clk-dss-lpc", },
	{ .name = "clk-st231-aud-0", },
	{ .name = "clk-st231-gp-1", },
	{ .name = "clk-st231-dmu", },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-icn-lmi", .flags = CLK_IS_CRITICAL },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-tx-icn-1", .flags = CLK_IS_CRITICAL },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-icn-sbc", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-stfe-frc2", },
	{ .name = "clk-eth-phyref", },
	{ .name = "clk-eth-ref-phyclk", },
	{ .name = "clk-flash-promip", },
	{ .name = "clk-main-disp", },
	{ .name = "clk-aux-disp", },
	{ .name = "clk-compo-dvp", },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-tx-icn-hades", .flags = CLK_IS_CRITICAL },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-rx-icn-hades", .flags = CLK_IS_CRITICAL },
	/* This clk needs to be on to keep bus interconnect alive */
	{ .name = "clk-icn-reg-16", .flags = CLK_IS_CRITICAL },
	{ .name = "clk-pp-hevc", },
	{ .name = "clk-clust-hevc", },
	{ .name = "clk-hwpe-hevc", },
	{ .name = "clk-fc-hevc", },
	{ .name = "clk-proc-mixer", },
	{ .name = "clk-proc-sc", },
	{ .name = "clk-avsp-hevc", },
};

static const struct clkgen_data clkgen_stih418_c0 = {
	.outputs = clkgen_stih418_c0_clk_out,
	.outputs_nb = ARRAY_SIZE(clkgen_stih418_c0_clk_out),
};

static const struct clkgen_clk_out clkgen_stih407_d0_clk_out[] = {
	{ .name = "clk-pcm-0", },
	{ .name = "clk-pcm-1", },
	{ .name = "clk-pcm-2", },
	{ .name = "clk-spdiff", },
};

static const struct clkgen_data clkgen_stih407_d0 = {
	.flags = CLK_SET_RATE_PARENT,
	.outputs = clkgen_stih407_d0_clk_out,
	.outputs_nb = ARRAY_SIZE(clkgen_stih407_d0_clk_out),
};

static const struct clkgen_clk_out clkgen_stih410_d0_clk_out[] = {
	{ .name = "clk-pcm-0", },
	{ .name = "clk-pcm-1", },
	{ .name = "clk-pcm-2", },
	{ .name = "clk-spdiff", },
	{ .name = "clk-pcmr10-master", },
	{ .name = "clk-usb2-phy", },
};

static const struct clkgen_data clkgen_stih410_d0 = {
	.flags = CLK_SET_RATE_PARENT,
	.outputs = clkgen_stih410_d0_clk_out,
	.outputs_nb = ARRAY_SIZE(clkgen_stih410_d0_clk_out),
};

static const struct clkgen_clk_out clkgen_stih407_d2_clk_out[] = {
	{ .name = "clk-pix-main-disp", },
	{ .name = "clk-pix-pip", },
	{ .name = "clk-pix-gdp1", },
	{ .name = "clk-pix-gdp2", },
	{ .name = "clk-pix-gdp3", },
	{ .name = "clk-pix-gdp4", },
	{ .name = "clk-pix-aux-disp", },
	{ .name = "clk-denc", },
	{ .name = "clk-pix-hddac", },
	{ .name = "clk-hddac", },
	{ .name = "clk-sddac", },
	{ .name = "clk-pix-dvo", },
	{ .name = "clk-dvo", },
	{ .name = "clk-pix-hdmi", },
	{ .name = "clk-tmds-hdmi", },
	{ .name = "clk-ref-hdmiphy", },
};

static const struct clkgen_data clkgen_stih407_d2 = {
	.outputs = clkgen_stih407_d2_clk_out,
	.outputs_nb = ARRAY_SIZE(clkgen_stih407_d2_clk_out),
	.flags = CLK_SET_RATE_PARENT,
	.mode = 1,
};

static const struct clkgen_clk_out clkgen_stih418_d2_clk_out[] = {
	{ .name = "clk-pix-main-disp", },
	{ .name = "", },
	{ .name = "", },
	{ .name = "", },
	{ .name = "", },
	{ .name = "clk-tmds-hdmi-div2", },
	{ .name = "clk-pix-aux-disp", },
	{ .name = "clk-denc", },
	{ .name = "clk-pix-hddac", },
	{ .name = "clk-hddac", },
	{ .name = "clk-sddac", },
	{ .name = "clk-pix-dvo", },
	{ .name = "clk-dvo", },
	{ .name = "clk-pix-hdmi", },
	{ .name = "clk-tmds-hdmi", },
	{ .name = "clk-ref-hdmiphy", },
	{ .name = "", }, { .name = "", }, { .name = "", }, { .name = "", },
	{ .name = "", }, { .name = "", }, { .name = "", }, { .name = "", },
	{ .name = "", }, { .name = "", }, { .name = "", }, { .name = "", },
	{ .name = "", }, { .name = "", }, { .name = "", }, { .name = "", },
	{ .name = "", }, { .name = "", }, { .name = "", }, { .name = "", },
	{ .name = "", }, { .name = "", }, { .name = "", }, { .name = "", },
	{ .name = "", }, { .name = "", }, { .name = "", }, { .name = "", },
	{ .name = "", }, { .name = "", }, { .name = "", },
	{ .name = "clk-vp9", },
};

static const struct clkgen_data clkgen_stih418_d2 = {
	.outputs = clkgen_stih418_d2_clk_out,
	.outputs_nb = ARRAY_SIZE(clkgen_stih418_d2_clk_out),
	.flags = CLK_SET_RATE_PARENT,
	.mode = 1,
};

static const struct clkgen_clk_out clkgen_stih407_d3_clk_out[] = {
	{ .name = "clk-stfe-frc1", },
	{ .name = "clk-tsout-0", },
	{ .name = "clk-tsout-1", },
	{ .name = "clk-mchi", },
	{ .name = "clk-vsens-compo", },
	{ .name = "clk-frc1-remote", },
	{ .name = "clk-lpc-0", },
	{ .name = "clk-lpc-1", },
};

static const struct clkgen_data clkgen_stih407_d3 = {
	.outputs = clkgen_stih407_d3_clk_out,
	.outputs_nb = ARRAY_SIZE(clkgen_stih407_d3_clk_out),
};

static const struct of_device_id flexgen_of_match[] = {
	{
		.compatible = "st,flexgen-audio",
		.data = &clkgen_audio,
	},
	{
		.compatible = "st,flexgen-video",
		.data = &clkgen_video,
	},
	{
		.compatible = "st,flexgen-stih407-a0",
		.data = &clkgen_stih407_a0,
	},
	{
		.compatible = "st,flexgen-stih410-a0",
		.data = &clkgen_stih410_a0,
	},
	{
		.compatible = "st,flexgen-stih407-c0",
		.data = &clkgen_stih407_c0,
	},
	{
		.compatible = "st,flexgen-stih410-c0",
		.data = &clkgen_stih410_c0,
	},
	{
		.compatible = "st,flexgen-stih418-c0",
		.data = &clkgen_stih418_c0,
	},
	{
		.compatible = "st,flexgen-stih407-d0",
		.data = &clkgen_stih407_d0,
	},
	{
		.compatible = "st,flexgen-stih410-d0",
		.data = &clkgen_stih410_d0,
	},
	{
		.compatible = "st,flexgen-stih407-d2",
		.data = &clkgen_stih407_d2,
	},
	{
		.compatible = "st,flexgen-stih418-d2",
		.data = &clkgen_stih418_d2,
	},
	{
		.compatible = "st,flexgen-stih407-d3",
		.data = &clkgen_stih407_d3,
	},
	{}
};

static void __init st_of_flexgen_setup(struct device_node *np)
{
	struct device_node *pnode;
	void __iomem *reg;
	struct clk_onecell_data *clk_data;
	const char **parents;
	int num_parents, i;
	spinlock_t *rlock = NULL;
	const struct of_device_id *match;
	struct clkgen_data *data = NULL;
	unsigned long flex_flags = 0;
	int ret;
	bool clk_mode = 0;
	const char *clk_name;

	pnode = of_get_parent(np);
	if (!pnode)
		return;

	reg = of_iomap(pnode, 0);
	of_node_put(pnode);
	if (!reg)
		return;

	parents = flexgen_get_parents(np, &num_parents);
	if (!parents) {
		iounmap(reg);
		return;
	}

	match = of_match_node(flexgen_of_match, np);
	if (match) {
		data = (struct clkgen_data *)match->data;
		flex_flags = data->flags;
		clk_mode = data->mode;
	}

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		goto err;

	/* First try to get output information from the compatible data */
	if (!data || !data->outputs_nb || !data->outputs) {
		ret = of_property_count_strings(np, "clock-output-names");
		if (ret <= 0) {
			pr_err("%s: Failed to get number of output clocks (%d)",
					__func__, clk_data->clk_num);
			goto err;
		}
		clk_data->clk_num = ret;
	} else
		clk_data->clk_num = data->outputs_nb;

	clk_data->clks = kcalloc(clk_data->clk_num, sizeof(struct clk *),
			GFP_KERNEL);
	if (!clk_data->clks)
		goto err;

	rlock = kzalloc(sizeof(spinlock_t), GFP_KERNEL);
	if (!rlock)
		goto err;

	spin_lock_init(rlock);

	for (i = 0; i < clk_data->clk_num; i++) {
		struct clk *clk;

		if (!data || !data->outputs_nb || !data->outputs) {
			if (of_property_read_string_index(np,
							  "clock-output-names",
							  i, &clk_name))
				break;
			flex_flags &= ~CLK_IS_CRITICAL;
			of_clk_detect_critical(np, i, &flex_flags);
		} else {
			clk_name = data->outputs[i].name;
			flex_flags = data->flags | data->outputs[i].flags;
		}

		/*
		 * If we read an empty clock name then the output is unused
		 */
		if (*clk_name == '\0')
			continue;

		clk = clk_register_flexgen(clk_name, parents, num_parents,
					   reg, rlock, i, flex_flags, clk_mode);

		if (IS_ERR(clk))
			goto err;

		clk_data->clks[i] = clk;
	}

	kfree(parents);
	of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);

	return;

err:
	iounmap(reg);
	if (clk_data)
		kfree(clk_data->clks);
	kfree(clk_data);
	kfree(parents);
	kfree(rlock);
}
CLK_OF_DECLARE(flexgen, "st,flexgen", st_of_flexgen_setup);
