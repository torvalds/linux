// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

/*
 * Each of the CPU clusters (Power and Perf) on msm8996 are
 * clocked via 2 PLLs, a primary and alternate. There are also
 * 2 Mux'es, a primary and secondary all connected together
 * as shown below
 *
 *                              +-------+
 *               XO             |       |
 *           +------------------>0      |
 *               SYS_APCS_AUX   |       |
 *           +------------------>3      |
 *                              |       |
 *                    PLL/2     | SMUX  +----+
 *                      +------->1      |    |
 *                      |       |       |    |
 *                      |       +-------+    |    +-------+
 *                      |                    +---->0      |
 *                      |                         |       |
 * +---------------+    |             +----------->1      | CPU clk
 * |Primary PLL    +----+ PLL_EARLY   |           |       +------>
 * |               +------+-----------+    +------>2 PMUX |
 * +---------------+      |                |      |       |
 *                        |   +------+     |   +-->3      |
 *                        +--^+  ACD +-----+   |  +-------+
 * +---------------+          +------+         |
 * |Alt PLL        |                           |
 * |               +---------------------------+
 * +---------------+         PLL_EARLY
 *
 * The primary PLL is what drives the CPU clk, except for times
 * when we are reprogramming the PLL itself (for rate changes) when
 * we temporarily switch to an alternate PLL.
 *
 * The primary PLL operates on a single VCO range, between 600MHz
 * and 3GHz. However the CPUs do support OPPs with frequencies
 * between 300MHz and 600MHz. In order to support running the CPUs
 * at those frequencies we end up having to lock the PLL at twice
 * the rate and drive the CPU clk via the PLL/2 output and SMUX.
 *
 * So for frequencies above 600MHz we follow the following path
 *  Primary PLL --> PLL_EARLY --> PMUX(1) --> CPU clk
 * and for frequencies between 300MHz and 600MHz we follow
 *  Primary PLL --> PLL/2 --> SMUX(1) --> PMUX(0) --> CPU clk
 *
 * ACD stands for Adaptive Clock Distribution and is used to
 * detect voltage droops.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <soc/qcom/kryo-l2-accessors.h>

#include <asm/cputype.h>

#include "clk-alpha-pll.h"
#include "clk-regmap.h"
#include "clk-regmap-mux.h"

enum _pmux_input {
	SMUX_INDEX = 0,
	PLL_INDEX,
	ACD_INDEX,
	ALT_INDEX,
	NUM_OF_PMUX_INPUTS
};

#define DIV_2_THRESHOLD		600000000
#define PWRCL_REG_OFFSET 0x0
#define PERFCL_REG_OFFSET 0x80000
#define MUX_OFFSET	0x40
#define CLK_CTL_OFFSET 0x44
#define CLK_CTL_AUTO_CLK_SEL BIT(8)
#define ALT_PLL_OFFSET	0x100
#define SSSCTL_OFFSET 0x160
#define PSCTL_OFFSET 0x164

#define PMUX_MASK	0x3
#define MUX_AUTO_CLK_SEL_ALWAYS_ON_MASK GENMASK(5, 4)
#define MUX_AUTO_CLK_SEL_ALWAYS_ON_GPLL0_SEL \
	FIELD_PREP(MUX_AUTO_CLK_SEL_ALWAYS_ON_MASK, 0x03)

static const u8 prim_pll_regs[PLL_OFF_MAX_REGS] = {
	[PLL_OFF_L_VAL] = 0x04,
	[PLL_OFF_ALPHA_VAL] = 0x08,
	[PLL_OFF_USER_CTL] = 0x10,
	[PLL_OFF_CONFIG_CTL] = 0x18,
	[PLL_OFF_CONFIG_CTL_U] = 0x1c,
	[PLL_OFF_TEST_CTL] = 0x20,
	[PLL_OFF_TEST_CTL_U] = 0x24,
	[PLL_OFF_STATUS] = 0x28,
};

static const u8 alt_pll_regs[PLL_OFF_MAX_REGS] = {
	[PLL_OFF_L_VAL] = 0x04,
	[PLL_OFF_ALPHA_VAL] = 0x08,
	[PLL_OFF_USER_CTL] = 0x10,
	[PLL_OFF_CONFIG_CTL] = 0x18,
	[PLL_OFF_TEST_CTL] = 0x20,
	[PLL_OFF_STATUS] = 0x28,
};

/* PLLs */

static const struct alpha_pll_config hfpll_config = {
	.l = 54,
	.config_ctl_val = 0x200d4828,
	.config_ctl_hi_val = 0x006,
	.test_ctl_val = 0x1c000000,
	.test_ctl_hi_val = 0x00004000,
	.pre_div_mask = BIT(12),
	.post_div_mask = 0x3 << 8,
	.post_div_val = 0x1 << 8,
	.main_output_mask = BIT(0),
	.early_output_mask = BIT(3),
};

static const struct clk_parent_data pll_parent[] = {
	{ .fw_name = "xo" },
};

static struct clk_alpha_pll pwrcl_pll = {
	.offset = PWRCL_REG_OFFSET,
	.regs = prim_pll_regs,
	.flags = SUPPORTS_DYNAMIC_UPDATE | SUPPORTS_FSM_MODE,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pwrcl_pll",
		.parent_data = pll_parent,
		.num_parents = ARRAY_SIZE(pll_parent),
		.ops = &clk_alpha_pll_hwfsm_ops,
	},
};

static struct clk_alpha_pll perfcl_pll = {
	.offset = PERFCL_REG_OFFSET,
	.regs = prim_pll_regs,
	.flags = SUPPORTS_DYNAMIC_UPDATE | SUPPORTS_FSM_MODE,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "perfcl_pll",
		.parent_data = pll_parent,
		.num_parents = ARRAY_SIZE(pll_parent),
		.ops = &clk_alpha_pll_hwfsm_ops,
	},
};

static struct clk_fixed_factor pwrcl_pll_postdiv = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "pwrcl_pll_postdiv",
		.parent_data = &(const struct clk_parent_data){
			.hw = &pwrcl_pll.clkr.hw
		},
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor perfcl_pll_postdiv = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "perfcl_pll_postdiv",
		.parent_data = &(const struct clk_parent_data){
			.hw = &perfcl_pll.clkr.hw
		},
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor perfcl_pll_acd = {
	.mult = 1,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "perfcl_pll_acd",
		.parent_data = &(const struct clk_parent_data){
			.hw = &perfcl_pll.clkr.hw
		},
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_fixed_factor pwrcl_pll_acd = {
	.mult = 1,
	.div = 1,
	.hw.init = &(struct clk_init_data){
		.name = "pwrcl_pll_acd",
		.parent_data = &(const struct clk_parent_data){
			.hw = &pwrcl_pll.clkr.hw
		},
		.num_parents = 1,
		.ops = &clk_fixed_factor_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct pll_vco alt_pll_vco_modes[] = {
	VCO(3,  250000000,  500000000),
	VCO(2,  500000000,  750000000),
	VCO(1,  750000000, 1000000000),
	VCO(0, 1000000000, 2150400000),
};

static const struct alpha_pll_config altpll_config = {
	.l = 16,
	.vco_val = 0x3 << 20,
	.vco_mask = 0x3 << 20,
	.config_ctl_val = 0x4001051b,
	.post_div_mask = 0x3 << 8,
	.post_div_val = 0x1 << 8,
	.main_output_mask = BIT(0),
	.early_output_mask = BIT(3),
};

static struct clk_alpha_pll pwrcl_alt_pll = {
	.offset = PWRCL_REG_OFFSET + ALT_PLL_OFFSET,
	.regs = alt_pll_regs,
	.vco_table = alt_pll_vco_modes,
	.num_vco = ARRAY_SIZE(alt_pll_vco_modes),
	.flags = SUPPORTS_OFFLINE_REQ | SUPPORTS_FSM_MODE,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "pwrcl_alt_pll",
		.parent_data = pll_parent,
		.num_parents = ARRAY_SIZE(pll_parent),
		.ops = &clk_alpha_pll_hwfsm_ops,
	},
};

static struct clk_alpha_pll perfcl_alt_pll = {
	.offset = PERFCL_REG_OFFSET + ALT_PLL_OFFSET,
	.regs = alt_pll_regs,
	.vco_table = alt_pll_vco_modes,
	.num_vco = ARRAY_SIZE(alt_pll_vco_modes),
	.flags = SUPPORTS_OFFLINE_REQ | SUPPORTS_FSM_MODE,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "perfcl_alt_pll",
		.parent_data = pll_parent,
		.num_parents = ARRAY_SIZE(pll_parent),
		.ops = &clk_alpha_pll_hwfsm_ops,
	},
};

struct clk_cpu_8996_pmux {
	u32	reg;
	struct notifier_block nb;
	struct clk_regmap clkr;
};

static int cpu_clk_notifier_cb(struct notifier_block *nb, unsigned long event,
			       void *data);

#define to_clk_cpu_8996_pmux_nb(_nb) \
	container_of(_nb, struct clk_cpu_8996_pmux, nb)

static inline struct clk_cpu_8996_pmux *to_clk_cpu_8996_pmux_hw(struct clk_hw *hw)
{
	return container_of(to_clk_regmap(hw), struct clk_cpu_8996_pmux, clkr);
}

static u8 clk_cpu_8996_pmux_get_parent(struct clk_hw *hw)
{
	struct clk_regmap *clkr = to_clk_regmap(hw);
	struct clk_cpu_8996_pmux *cpuclk = to_clk_cpu_8996_pmux_hw(hw);
	u32 val;

	regmap_read(clkr->regmap, cpuclk->reg, &val);

	return FIELD_GET(PMUX_MASK, val);
}

static int clk_cpu_8996_pmux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_regmap *clkr = to_clk_regmap(hw);
	struct clk_cpu_8996_pmux *cpuclk = to_clk_cpu_8996_pmux_hw(hw);
	u32 val;

	val = FIELD_PREP(PMUX_MASK, index);

	return regmap_update_bits(clkr->regmap, cpuclk->reg, PMUX_MASK, val);
}

static int clk_cpu_8996_pmux_determine_rate(struct clk_hw *hw,
					   struct clk_rate_request *req)
{
	struct clk_hw *parent;

	if (req->rate < (DIV_2_THRESHOLD / 2))
		return -EINVAL;

	if (req->rate < DIV_2_THRESHOLD)
		parent = clk_hw_get_parent_by_index(hw, SMUX_INDEX);
	else
		parent = clk_hw_get_parent_by_index(hw, ACD_INDEX);
	if (!parent)
		return -EINVAL;

	req->best_parent_rate = clk_hw_round_rate(parent, req->rate);
	req->best_parent_hw = parent;

	return 0;
}

static const struct clk_ops clk_cpu_8996_pmux_ops = {
	.set_parent = clk_cpu_8996_pmux_set_parent,
	.get_parent = clk_cpu_8996_pmux_get_parent,
	.determine_rate = clk_cpu_8996_pmux_determine_rate,
};

static const struct parent_map smux_parent_map[] = {
	{ .cfg = 0, }, /* xo */
	{ .cfg = 1, }, /* pll */
	{ .cfg = 3, }, /* sys_apcs_aux */
};

static const struct clk_parent_data pwrcl_smux_parents[] = {
	{ .fw_name = "xo" },
	{ .hw = &pwrcl_pll_postdiv.hw },
	{ .fw_name = "sys_apcs_aux" },
};

static const struct clk_parent_data perfcl_smux_parents[] = {
	{ .fw_name = "xo" },
	{ .hw = &perfcl_pll_postdiv.hw },
	{ .fw_name = "sys_apcs_aux" },
};

static struct clk_regmap_mux pwrcl_smux = {
	.reg = PWRCL_REG_OFFSET + MUX_OFFSET,
	.shift = 2,
	.width = 2,
	.parent_map = smux_parent_map,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "pwrcl_smux",
		.parent_data = pwrcl_smux_parents,
		.num_parents = ARRAY_SIZE(pwrcl_smux_parents),
		.ops = &clk_regmap_mux_closest_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap_mux perfcl_smux = {
	.reg = PERFCL_REG_OFFSET + MUX_OFFSET,
	.shift = 2,
	.width = 2,
	.parent_map = smux_parent_map,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "perfcl_smux",
		.parent_data = perfcl_smux_parents,
		.num_parents = ARRAY_SIZE(perfcl_smux_parents),
		.ops = &clk_regmap_mux_closest_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_hw *pwrcl_pmux_parents[] = {
	[SMUX_INDEX] = &pwrcl_smux.clkr.hw,
	[PLL_INDEX] = &pwrcl_pll.clkr.hw,
	[ACD_INDEX] = &pwrcl_pll_acd.hw,
	[ALT_INDEX] = &pwrcl_alt_pll.clkr.hw,
};

static const struct clk_hw *perfcl_pmux_parents[] = {
	[SMUX_INDEX] = &perfcl_smux.clkr.hw,
	[PLL_INDEX] = &perfcl_pll.clkr.hw,
	[ACD_INDEX] = &perfcl_pll_acd.hw,
	[ALT_INDEX] = &perfcl_alt_pll.clkr.hw,
};

static struct clk_cpu_8996_pmux pwrcl_pmux = {
	.reg = PWRCL_REG_OFFSET + MUX_OFFSET,
	.nb.notifier_call = cpu_clk_notifier_cb,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "pwrcl_pmux",
		.parent_hws = pwrcl_pmux_parents,
		.num_parents = ARRAY_SIZE(pwrcl_pmux_parents),
		.ops = &clk_cpu_8996_pmux_ops,
		/* CPU clock is critical and should never be gated */
		.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
	},
};

static struct clk_cpu_8996_pmux perfcl_pmux = {
	.reg = PERFCL_REG_OFFSET + MUX_OFFSET,
	.nb.notifier_call = cpu_clk_notifier_cb,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "perfcl_pmux",
		.parent_hws = perfcl_pmux_parents,
		.num_parents = ARRAY_SIZE(perfcl_pmux_parents),
		.ops = &clk_cpu_8996_pmux_ops,
		/* CPU clock is critical and should never be gated */
		.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
	},
};

static const struct regmap_config cpu_msm8996_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= 0x80210,
	.val_format_endian	= REGMAP_ENDIAN_LITTLE,
};

static struct clk_hw *cpu_msm8996_hw_clks[] = {
	&pwrcl_pll_postdiv.hw,
	&perfcl_pll_postdiv.hw,
	&pwrcl_pll_acd.hw,
	&perfcl_pll_acd.hw,
};

static struct clk_regmap *cpu_msm8996_clks[] = {
	&pwrcl_pll.clkr,
	&perfcl_pll.clkr,
	&pwrcl_alt_pll.clkr,
	&perfcl_alt_pll.clkr,
	&pwrcl_smux.clkr,
	&perfcl_smux.clkr,
	&pwrcl_pmux.clkr,
	&perfcl_pmux.clkr,
};

static void qcom_cpu_clk_msm8996_acd_init(struct regmap *regmap);

static int qcom_cpu_clk_msm8996_register_clks(struct device *dev,
					      struct regmap *regmap)
{
	int i, ret;

	/* Select GPLL0 for 300MHz for both clusters */
	regmap_write(regmap, PERFCL_REG_OFFSET + MUX_OFFSET, 0xc);
	regmap_write(regmap, PWRCL_REG_OFFSET + MUX_OFFSET, 0xc);

	/* Ensure write goes through before PLLs are reconfigured */
	udelay(5);

	/* Set the auto clock sel always-on source to GPLL0/2 (300MHz) */
	regmap_update_bits(regmap, PWRCL_REG_OFFSET + MUX_OFFSET,
			   MUX_AUTO_CLK_SEL_ALWAYS_ON_MASK,
			   MUX_AUTO_CLK_SEL_ALWAYS_ON_GPLL0_SEL);
	regmap_update_bits(regmap, PERFCL_REG_OFFSET + MUX_OFFSET,
			   MUX_AUTO_CLK_SEL_ALWAYS_ON_MASK,
			   MUX_AUTO_CLK_SEL_ALWAYS_ON_GPLL0_SEL);

	clk_alpha_pll_configure(&pwrcl_pll, regmap, &hfpll_config);
	clk_alpha_pll_configure(&perfcl_pll, regmap, &hfpll_config);
	clk_alpha_pll_configure(&pwrcl_alt_pll, regmap, &altpll_config);
	clk_alpha_pll_configure(&perfcl_alt_pll, regmap, &altpll_config);

	/* Wait for PLL(s) to lock */
	udelay(50);

	/* Enable auto clock selection for both clusters */
	regmap_update_bits(regmap, PWRCL_REG_OFFSET + CLK_CTL_OFFSET,
			   CLK_CTL_AUTO_CLK_SEL, CLK_CTL_AUTO_CLK_SEL);
	regmap_update_bits(regmap, PERFCL_REG_OFFSET + CLK_CTL_OFFSET,
			   CLK_CTL_AUTO_CLK_SEL, CLK_CTL_AUTO_CLK_SEL);

	/* Ensure write goes through before muxes are switched */
	udelay(5);

	qcom_cpu_clk_msm8996_acd_init(regmap);

	/* Pulse swallower and soft-start settings */
	regmap_write(regmap, PWRCL_REG_OFFSET + PSCTL_OFFSET, 0x00030005);
	regmap_write(regmap, PERFCL_REG_OFFSET + PSCTL_OFFSET, 0x00030005);

	/* Switch clusters to use the ACD leg */
	regmap_write(regmap, PWRCL_REG_OFFSET + MUX_OFFSET, 0x32);
	regmap_write(regmap, PERFCL_REG_OFFSET + MUX_OFFSET, 0x32);

	for (i = 0; i < ARRAY_SIZE(cpu_msm8996_hw_clks); i++) {
		ret = devm_clk_hw_register(dev, cpu_msm8996_hw_clks[i]);
		if (ret)
			return ret;
	}

	for (i = 0; i < ARRAY_SIZE(cpu_msm8996_clks); i++) {
		ret = devm_clk_register_regmap(dev, cpu_msm8996_clks[i]);
		if (ret)
			return ret;
	}

	/* Enable alt PLLs */
	clk_prepare_enable(pwrcl_alt_pll.clkr.hw.clk);
	clk_prepare_enable(perfcl_alt_pll.clkr.hw.clk);

	devm_clk_notifier_register(dev, pwrcl_pmux.clkr.hw.clk, &pwrcl_pmux.nb);
	devm_clk_notifier_register(dev, perfcl_pmux.clkr.hw.clk, &perfcl_pmux.nb);

	return ret;
}

#define CPU_CLUSTER_AFFINITY_MASK 0xf00
#define PWRCL_AFFINITY_MASK 0x000
#define PERFCL_AFFINITY_MASK 0x100

#define L2ACDCR_REG 0x580ULL
#define L2ACDTD_REG 0x581ULL
#define L2ACDDVMRC_REG 0x584ULL
#define L2ACDSSCR_REG 0x589ULL

static DEFINE_SPINLOCK(qcom_clk_acd_lock);

static void qcom_cpu_clk_msm8996_acd_init(struct regmap *regmap)
{
	u64 hwid;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&qcom_clk_acd_lock, flags);

	val = kryo_l2_get_indirect_reg(L2ACDTD_REG);
	if (val == 0x00006a11)
		goto out;

	kryo_l2_set_indirect_reg(L2ACDTD_REG, 0x00006a11);
	kryo_l2_set_indirect_reg(L2ACDDVMRC_REG, 0x000e0f0f);
	kryo_l2_set_indirect_reg(L2ACDSSCR_REG, 0x00000601);

	kryo_l2_set_indirect_reg(L2ACDCR_REG, 0x002c5ffd);

	hwid = read_cpuid_mpidr();
	if ((hwid & CPU_CLUSTER_AFFINITY_MASK) == PWRCL_AFFINITY_MASK)
		regmap_write(regmap, PWRCL_REG_OFFSET + SSSCTL_OFFSET, 0xf);
	else
		regmap_write(regmap, PERFCL_REG_OFFSET + SSSCTL_OFFSET, 0xf);

out:
	spin_unlock_irqrestore(&qcom_clk_acd_lock, flags);
}

static int cpu_clk_notifier_cb(struct notifier_block *nb, unsigned long event,
			       void *data)
{
	struct clk_cpu_8996_pmux *cpuclk = to_clk_cpu_8996_pmux_nb(nb);
	struct clk_notifier_data *cnd = data;

	switch (event) {
	case PRE_RATE_CHANGE:
		qcom_cpu_clk_msm8996_acd_init(cpuclk->clkr.regmap);

		/*
		 * Avoid overvolting. clk_core_set_rate_nolock() walks from top
		 * to bottom, so it will change the rate of the PLL before
		 * chaging the parent of PMUX. This can result in pmux getting
		 * clocked twice the expected rate.
		 *
		 * Manually switch to PLL/2 here.
		 */
		if (cnd->new_rate < DIV_2_THRESHOLD &&
		    cnd->old_rate > DIV_2_THRESHOLD)
			clk_cpu_8996_pmux_set_parent(&cpuclk->clkr.hw, SMUX_INDEX);

		break;
	case ABORT_RATE_CHANGE:
		/* Revert manual change */
		if (cnd->new_rate < DIV_2_THRESHOLD &&
		    cnd->old_rate > DIV_2_THRESHOLD)
			clk_cpu_8996_pmux_set_parent(&cpuclk->clkr.hw, ACD_INDEX);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
};

static int qcom_cpu_clk_msm8996_driver_probe(struct platform_device *pdev)
{
	static void __iomem *base;
	struct regmap *regmap;
	struct clk_hw_onecell_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	data = devm_kzalloc(dev, struct_size(data, hws, 2), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->num = 2;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &cpu_msm8996_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = qcom_cpu_clk_msm8996_register_clks(dev, regmap);
	if (ret)
		return ret;

	data->hws[0] = &pwrcl_pmux.clkr.hw;
	data->hws[1] = &perfcl_pmux.clkr.hw;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, data);
}

static const struct of_device_id qcom_cpu_clk_msm8996_match_table[] = {
	{ .compatible = "qcom,msm8996-apcc" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_cpu_clk_msm8996_match_table);

static struct platform_driver qcom_cpu_clk_msm8996_driver = {
	.probe = qcom_cpu_clk_msm8996_driver_probe,
	.driver = {
		.name = "qcom-msm8996-apcc",
		.of_match_table = qcom_cpu_clk_msm8996_match_table,
	},
};
module_platform_driver(qcom_cpu_clk_msm8996_driver);

MODULE_DESCRIPTION("QCOM MSM8996 CPU Clock Driver");
MODULE_LICENSE("GPL v2");
