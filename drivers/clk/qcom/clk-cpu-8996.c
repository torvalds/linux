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

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <soc/qcom/kryo-l2-accessors.h>

#include "clk-alpha-pll.h"
#include "clk-regmap.h"

enum _pmux_input {
	DIV_2_INDEX = 0,
	PLL_INDEX,
	ACD_INDEX,
	ALT_INDEX,
	NUM_OF_PMUX_INPUTS
};

#define DIV_2_THRESHOLD		600000000
#define PWRCL_REG_OFFSET 0x0
#define PERFCL_REG_OFFSET 0x80000
#define MUX_OFFSET	0x40
#define ALT_PLL_OFFSET	0x100
#define SSSCTL_OFFSET 0x160

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
	[PLL_OFF_ALPHA_VAL_U] = 0x0c,
	[PLL_OFF_USER_CTL] = 0x10,
	[PLL_OFF_USER_CTL_U] = 0x14,
	[PLL_OFF_CONFIG_CTL] = 0x18,
	[PLL_OFF_TEST_CTL] = 0x20,
	[PLL_OFF_TEST_CTL_U] = 0x24,
	[PLL_OFF_STATUS] = 0x28,
};

/* PLLs */

static const struct alpha_pll_config hfpll_config = {
	.l = 60,
	.config_ctl_val = 0x200d4aa8,
	.config_ctl_hi_val = 0x006,
	.pre_div_mask = BIT(12),
	.post_div_mask = 0x3 << 8,
	.post_div_val = 0x1 << 8,
	.main_output_mask = BIT(0),
	.early_output_mask = BIT(3),
};

static struct clk_alpha_pll perfcl_pll = {
	.offset = PERFCL_REG_OFFSET,
	.regs = prim_pll_regs,
	.flags = SUPPORTS_DYNAMIC_UPDATE | SUPPORTS_FSM_MODE,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "perfcl_pll",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_huayra_ops,
	},
};

static struct clk_alpha_pll pwrcl_pll = {
	.offset = PWRCL_REG_OFFSET,
	.regs = prim_pll_regs,
	.flags = SUPPORTS_DYNAMIC_UPDATE | SUPPORTS_FSM_MODE,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pwrcl_pll",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_huayra_ops,
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

static struct clk_alpha_pll perfcl_alt_pll = {
	.offset = PERFCL_REG_OFFSET + ALT_PLL_OFFSET,
	.regs = alt_pll_regs,
	.vco_table = alt_pll_vco_modes,
	.num_vco = ARRAY_SIZE(alt_pll_vco_modes),
	.flags = SUPPORTS_OFFLINE_REQ | SUPPORTS_FSM_MODE,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "perfcl_alt_pll",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_hwfsm_ops,
	},
};

static struct clk_alpha_pll pwrcl_alt_pll = {
	.offset = PWRCL_REG_OFFSET + ALT_PLL_OFFSET,
	.regs = alt_pll_regs,
	.vco_table = alt_pll_vco_modes,
	.num_vco = ARRAY_SIZE(alt_pll_vco_modes),
	.flags = SUPPORTS_OFFLINE_REQ | SUPPORTS_FSM_MODE,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "pwrcl_alt_pll",
		.parent_names = (const char *[]){ "xo" },
		.num_parents = 1,
		.ops = &clk_alpha_pll_hwfsm_ops,
	},
};

struct clk_cpu_8996_mux {
	u32	reg;
	u8	shift;
	u8	width;
	struct notifier_block nb;
	struct clk_hw	*pll;
	struct clk_hw	*pll_div_2;
	struct clk_regmap clkr;
};

static int cpu_clk_notifier_cb(struct notifier_block *nb, unsigned long event,
			       void *data);

#define to_clk_cpu_8996_mux_nb(_nb) \
	container_of(_nb, struct clk_cpu_8996_mux, nb)

static inline struct clk_cpu_8996_mux *to_clk_cpu_8996_mux_hw(struct clk_hw *hw)
{
	return container_of(to_clk_regmap(hw), struct clk_cpu_8996_mux, clkr);
}

static u8 clk_cpu_8996_mux_get_parent(struct clk_hw *hw)
{
	struct clk_regmap *clkr = to_clk_regmap(hw);
	struct clk_cpu_8996_mux *cpuclk = to_clk_cpu_8996_mux_hw(hw);
	u32 mask = GENMASK(cpuclk->width - 1, 0);
	u32 val;

	regmap_read(clkr->regmap, cpuclk->reg, &val);
	val >>= cpuclk->shift;

	return val & mask;
}

static int clk_cpu_8996_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_regmap *clkr = to_clk_regmap(hw);
	struct clk_cpu_8996_mux *cpuclk = to_clk_cpu_8996_mux_hw(hw);
	u32 mask = GENMASK(cpuclk->width + cpuclk->shift - 1, cpuclk->shift);
	u32 val;

	val = index;
	val <<= cpuclk->shift;

	return regmap_update_bits(clkr->regmap, cpuclk->reg, mask, val);
}

static int clk_cpu_8996_mux_determine_rate(struct clk_hw *hw,
					   struct clk_rate_request *req)
{
	struct clk_cpu_8996_mux *cpuclk = to_clk_cpu_8996_mux_hw(hw);
	struct clk_hw *parent = cpuclk->pll;

	if (cpuclk->pll_div_2 && req->rate < DIV_2_THRESHOLD) {
		if (req->rate < (DIV_2_THRESHOLD / 2))
			return -EINVAL;

		parent = cpuclk->pll_div_2;
	}

	req->best_parent_rate = clk_hw_round_rate(parent, req->rate);
	req->best_parent_hw = parent;

	return 0;
}

static const struct clk_ops clk_cpu_8996_mux_ops = {
	.set_parent = clk_cpu_8996_mux_set_parent,
	.get_parent = clk_cpu_8996_mux_get_parent,
	.determine_rate = clk_cpu_8996_mux_determine_rate,
};

static struct clk_cpu_8996_mux pwrcl_smux = {
	.reg = PWRCL_REG_OFFSET + MUX_OFFSET,
	.shift = 2,
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "pwrcl_smux",
		.parent_names = (const char *[]){
			"xo",
			"pwrcl_pll_main",
		},
		.num_parents = 2,
		.ops = &clk_cpu_8996_mux_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_cpu_8996_mux perfcl_smux = {
	.reg = PERFCL_REG_OFFSET + MUX_OFFSET,
	.shift = 2,
	.width = 2,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "perfcl_smux",
		.parent_names = (const char *[]){
			"xo",
			"perfcl_pll_main",
		},
		.num_parents = 2,
		.ops = &clk_cpu_8996_mux_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_cpu_8996_mux pwrcl_pmux = {
	.reg = PWRCL_REG_OFFSET + MUX_OFFSET,
	.shift = 0,
	.width = 2,
	.pll = &pwrcl_pll.clkr.hw,
	.pll_div_2 = &pwrcl_smux.clkr.hw,
	.nb.notifier_call = cpu_clk_notifier_cb,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "pwrcl_pmux",
		.parent_names = (const char *[]){
			"pwrcl_smux",
			"pwrcl_pll",
			"pwrcl_pll_acd",
			"pwrcl_alt_pll",
		},
		.num_parents = 4,
		.ops = &clk_cpu_8996_mux_ops,
		/* CPU clock is critical and should never be gated */
		.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
	},
};

static struct clk_cpu_8996_mux perfcl_pmux = {
	.reg = PERFCL_REG_OFFSET + MUX_OFFSET,
	.shift = 0,
	.width = 2,
	.pll = &perfcl_pll.clkr.hw,
	.pll_div_2 = &perfcl_smux.clkr.hw,
	.nb.notifier_call = cpu_clk_notifier_cb,
	.clkr.hw.init = &(struct clk_init_data) {
		.name = "perfcl_pmux",
		.parent_names = (const char *[]){
			"perfcl_smux",
			"perfcl_pll",
			"perfcl_pll_acd",
			"perfcl_alt_pll",
		},
		.num_parents = 4,
		.ops = &clk_cpu_8996_mux_ops,
		/* CPU clock is critical and should never be gated */
		.flags = CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
	},
};

static const struct regmap_config cpu_msm8996_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= 0x80210,
	.fast_io		= true,
	.val_format_endian	= REGMAP_ENDIAN_LITTLE,
};

static struct clk_regmap *cpu_msm8996_clks[] = {
	&perfcl_pll.clkr,
	&pwrcl_pll.clkr,
	&perfcl_alt_pll.clkr,
	&pwrcl_alt_pll.clkr,
	&perfcl_smux.clkr,
	&pwrcl_smux.clkr,
	&perfcl_pmux.clkr,
	&pwrcl_pmux.clkr,
};

static int qcom_cpu_clk_msm8996_register_clks(struct device *dev,
					      struct regmap *regmap)
{
	int i, ret;

	perfcl_smux.pll = clk_hw_register_fixed_factor(dev, "perfcl_pll_main",
						       "perfcl_pll",
						       CLK_SET_RATE_PARENT,
						       1, 2);
	if (IS_ERR(perfcl_smux.pll)) {
		dev_err(dev, "Failed to initialize perfcl_pll_main\n");
		return PTR_ERR(perfcl_smux.pll);
	}

	pwrcl_smux.pll = clk_hw_register_fixed_factor(dev, "pwrcl_pll_main",
						      "pwrcl_pll",
						      CLK_SET_RATE_PARENT,
						      1, 2);
	if (IS_ERR(pwrcl_smux.pll)) {
		dev_err(dev, "Failed to initialize pwrcl_pll_main\n");
		clk_hw_unregister(perfcl_smux.pll);
		return PTR_ERR(pwrcl_smux.pll);
	}

	for (i = 0; i < ARRAY_SIZE(cpu_msm8996_clks); i++) {
		ret = devm_clk_register_regmap(dev, cpu_msm8996_clks[i]);
		if (ret) {
			clk_hw_unregister(perfcl_smux.pll);
			clk_hw_unregister(pwrcl_smux.pll);
			return ret;
		}
	}

	clk_alpha_pll_configure(&perfcl_pll, regmap, &hfpll_config);
	clk_alpha_pll_configure(&pwrcl_pll, regmap, &hfpll_config);
	clk_alpha_pll_configure(&perfcl_alt_pll, regmap, &altpll_config);
	clk_alpha_pll_configure(&pwrcl_alt_pll, regmap, &altpll_config);

	/* Enable alt PLLs */
	clk_prepare_enable(pwrcl_alt_pll.clkr.hw.clk);
	clk_prepare_enable(perfcl_alt_pll.clkr.hw.clk);

	clk_notifier_register(pwrcl_pmux.clkr.hw.clk, &pwrcl_pmux.nb);
	clk_notifier_register(perfcl_pmux.clkr.hw.clk, &perfcl_pmux.nb);

	return ret;
}

static int qcom_cpu_clk_msm8996_unregister_clks(void)
{
	int ret = 0;

	ret = clk_notifier_unregister(pwrcl_pmux.clkr.hw.clk, &pwrcl_pmux.nb);
	if (ret)
		return ret;

	ret = clk_notifier_unregister(perfcl_pmux.clkr.hw.clk, &perfcl_pmux.nb);
	if (ret)
		return ret;

	clk_hw_unregister(perfcl_smux.pll);
	clk_hw_unregister(pwrcl_smux.pll);

	return 0;
}

#define CPU_AFINITY_MASK 0xFFF
#define PWRCL_CPU_REG_MASK 0x3
#define PERFCL_CPU_REG_MASK 0x103

#define L2ACDCR_REG 0x580ULL
#define L2ACDTD_REG 0x581ULL
#define L2ACDDVMRC_REG 0x584ULL
#define L2ACDSSCR_REG 0x589ULL

static DEFINE_SPINLOCK(qcom_clk_acd_lock);
static void __iomem *base;

static void qcom_cpu_clk_msm8996_acd_init(void __iomem *base)
{
	u64 hwid;
	unsigned long flags;

	spin_lock_irqsave(&qcom_clk_acd_lock, flags);

	hwid = read_cpuid_mpidr() & CPU_AFINITY_MASK;

	kryo_l2_set_indirect_reg(L2ACDTD_REG, 0x00006a11);
	kryo_l2_set_indirect_reg(L2ACDDVMRC_REG, 0x000e0f0f);
	kryo_l2_set_indirect_reg(L2ACDSSCR_REG, 0x00000601);

	if (PWRCL_CPU_REG_MASK == (hwid | PWRCL_CPU_REG_MASK)) {
		writel(0xf, base + PWRCL_REG_OFFSET + SSSCTL_OFFSET);
		kryo_l2_set_indirect_reg(L2ACDCR_REG, 0x002c5ffd);
	}

	if (PERFCL_CPU_REG_MASK == (hwid | PERFCL_CPU_REG_MASK)) {
		kryo_l2_set_indirect_reg(L2ACDCR_REG, 0x002c5ffd);
		writel(0xf, base + PERFCL_REG_OFFSET + SSSCTL_OFFSET);
	}

	spin_unlock_irqrestore(&qcom_clk_acd_lock, flags);
}

static int cpu_clk_notifier_cb(struct notifier_block *nb, unsigned long event,
			       void *data)
{
	struct clk_cpu_8996_mux *cpuclk = to_clk_cpu_8996_mux_nb(nb);
	struct clk_notifier_data *cnd = data;
	int ret;

	switch (event) {
	case PRE_RATE_CHANGE:
		ret = clk_cpu_8996_mux_set_parent(&cpuclk->clkr.hw, ALT_INDEX);
		qcom_cpu_clk_msm8996_acd_init(base);
		break;
	case POST_RATE_CHANGE:
		if (cnd->new_rate < DIV_2_THRESHOLD)
			ret = clk_cpu_8996_mux_set_parent(&cpuclk->clkr.hw,
							  DIV_2_INDEX);
		else
			ret = clk_cpu_8996_mux_set_parent(&cpuclk->clkr.hw,
							  ACD_INDEX);
		break;
	default:
		ret = 0;
		break;
	}

	return notifier_from_errno(ret);
};

static int qcom_cpu_clk_msm8996_driver_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	struct clk_hw_onecell_data *data;
	struct device *dev = &pdev->dev;
	int ret;

	data = devm_kzalloc(dev, struct_size(data, hws, 2), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &cpu_msm8996_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = qcom_cpu_clk_msm8996_register_clks(dev, regmap);
	if (ret)
		return ret;

	qcom_cpu_clk_msm8996_acd_init(base);

	data->hws[0] = &pwrcl_pmux.clkr.hw;
	data->hws[1] = &perfcl_pmux.clkr.hw;
	data->num = 2;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, data);
}

static int qcom_cpu_clk_msm8996_driver_remove(struct platform_device *pdev)
{
	return qcom_cpu_clk_msm8996_unregister_clks();
}

static const struct of_device_id qcom_cpu_clk_msm8996_match_table[] = {
	{ .compatible = "qcom,msm8996-apcc" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_cpu_clk_msm8996_match_table);

static struct platform_driver qcom_cpu_clk_msm8996_driver = {
	.probe = qcom_cpu_clk_msm8996_driver_probe,
	.remove = qcom_cpu_clk_msm8996_driver_remove,
	.driver = {
		.name = "qcom-msm8996-apcc",
		.of_match_table = qcom_cpu_clk_msm8996_match_table,
	},
};
module_platform_driver(qcom_cpu_clk_msm8996_driver);

MODULE_DESCRIPTION("QCOM MSM8996 CPU Clock Driver");
MODULE_LICENSE("GPL v2");
