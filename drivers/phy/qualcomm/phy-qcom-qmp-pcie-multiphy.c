// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "phy-qcom-qmp.h"

#define PHY_INIT_COMPLETE_TIMEOUT_US		10000

enum qmp_pcie_glymur_link_mode {
	QMP_PCIE_GLYMUR_MODE_X8,
	QMP_PCIE_GLYMUR_MODE_X4X4,
};

enum qphy_reg_layout {
	QPHY_PCS_STATUS,
	QPHY_LAYOUT_SIZE
};

static const unsigned int pciephy_v8_50_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_PCS_STATUS]		= QPHY_V8_50_PCS_STATUS1,
};

struct qmp_pcie_offsets {
	u16 pcs;
};

struct qmp_phy_cfg {
	const struct qmp_pcie_offsets *offsets;
	const char * const *reg_names;
	int num_regs;
	const char * const *pd_names;
	int num_pds;
	const char * const *nocsr_reset_list;
	int num_nocsr_resets;
	const char * const *vreg_list;
	int num_vregs;
	const unsigned int *regs;
	unsigned int phy_status;
	const char * const *clk_list;
	int num_clks;
	const char * const *pipe_clk_list;
	int num_pipe_clks;
};

struct qmp_pcie {
	struct device *dev;
	const struct qmp_phy_cfg *cfg;
	void __iomem **base;
	struct clk_bulk_data *clks;
	struct clk_bulk_data *pipe_clks;
	struct reset_control_bulk_data *nocsr_resets;
	struct regulator_bulk_data *vregs;
	struct device **pd_devs;
};

struct qmp_pcie_multiphy {
	struct phy **phys;
	const struct qmp_pcie_link_mode_cfg *mode_cfg;
	int num_pipe_outputs;
	struct clk_fixed_rate *pipe_out_clks;
};

struct qmp_pcie_link_mode_cfg {
	const struct qmp_phy_cfg * const *cfgs;
	u32 num_phys;
};

struct qmp_pcie_match_data {
	const struct qmp_pcie_link_mode_cfg *mode_cfgs;
	u32 num_modes;
};

static const char * const glymur_pciephy_clk_l[] = {
	"aux", "cfg_ahb", "ref", "rchng", "phy_b_aux",
};

static const char * const glymur_pciephy_a_clk_l[] = {
	"aux", "cfg_ahb", "ref", "rchng",
};

static const char * const glymur_pciephy_b_clk_l[] = {
	"phy_b_aux", "cfg_ahb_b", "ref", "rchng_b",
};

static const char * const glymur_pciephy_pipeclk_l[] = {
	"pipe",
};

static const char * const glymur_pipephy_b_pipeclk_l[] = {
	"pipe_b", "pipediv2_b",
};

static const char * const glymur_vreg_l[] = {
	"vdda-phy", "vdda-pll", "vdda-refgen0p9", "vdda-refgen1p2",
};

static const char * const glymur_pciephy_a_reg_l[] = {
	"port_a",
};

static const char * const glymur_pciephy_b_reg_l[] = {
	"port_b",
};

static const char * const glymur_pciephy_reg_l[] = {
	"port_a", "port_b",
};

static const char * const glymur_pciephy_a_pd_l[] = {
	"port_a",
};

static const char * const glymur_pciephy_b_pd_l[] = {
	"port_b",
};

static const char * const glymur_pciephy_pd_l[] = {
	"port_a", "port_b",
};

static const char * const glymur_pciephy_a_nocsr_reset_l[] = {
	"port_a_nocsr",
};

static const char * const glymur_pciephy_nocsr_reset_l[] = {
	"port_a_nocsr", "port_b_nocsr",
};

static const char * const glymur_pciephy_b_nocsr_reset_l[] = {
	"port_b_nocsr",
};

static const struct qmp_pcie_offsets glymur_pcie_offsets_v8_50 = {
	.pcs		= 0x9000,
};

static const struct qmp_phy_cfg glymur_qmp_gen5x4_pciephy_a_cfg = {
	.offsets		= &glymur_pcie_offsets_v8_50,
	.reg_names		= glymur_pciephy_a_reg_l,
	.num_regs		= ARRAY_SIZE(glymur_pciephy_a_reg_l),
	.pd_names		= glymur_pciephy_a_pd_l,
	.num_pds		= ARRAY_SIZE(glymur_pciephy_a_pd_l),
	.nocsr_reset_list	= glymur_pciephy_a_nocsr_reset_l,
	.num_nocsr_resets	= ARRAY_SIZE(glymur_pciephy_a_nocsr_reset_l),
	.vreg_list		= glymur_vreg_l,
	.num_vregs		= ARRAY_SIZE(glymur_vreg_l),
	.regs			= pciephy_v8_50_regs_layout,
	.phy_status		= PHYSTATUS_4_20,
	.pipe_clk_list		= glymur_pciephy_pipeclk_l,
	.num_pipe_clks		= ARRAY_SIZE(glymur_pciephy_pipeclk_l),
	.clk_list		= glymur_pciephy_a_clk_l,
	.num_clks		= ARRAY_SIZE(glymur_pciephy_a_clk_l),
};

static const struct qmp_phy_cfg glymur_qmp_gen5x4_pciephy_b_cfg = {
	.offsets		= &glymur_pcie_offsets_v8_50,
	.reg_names		= glymur_pciephy_b_reg_l,
	.num_regs		= ARRAY_SIZE(glymur_pciephy_b_reg_l),
	.pd_names		= glymur_pciephy_b_pd_l,
	.num_pds		= ARRAY_SIZE(glymur_pciephy_b_pd_l),
	.nocsr_reset_list	= glymur_pciephy_b_nocsr_reset_l,
	.num_nocsr_resets	= ARRAY_SIZE(glymur_pciephy_b_nocsr_reset_l),
	.vreg_list		= glymur_vreg_l,
	.num_vregs		= ARRAY_SIZE(glymur_vreg_l),
	.regs			= pciephy_v8_50_regs_layout,
	.phy_status		= PHYSTATUS_4_20,
	.pipe_clk_list		= glymur_pipephy_b_pipeclk_l,
	.num_pipe_clks		= ARRAY_SIZE(glymur_pipephy_b_pipeclk_l),
	.clk_list		= glymur_pciephy_b_clk_l,
	.num_clks		= ARRAY_SIZE(glymur_pciephy_b_clk_l),
};

static const struct qmp_phy_cfg glymur_qmp_gen5x8_pciephy_cfg = {
	.offsets		= &glymur_pcie_offsets_v8_50,
	.reg_names		= glymur_pciephy_reg_l,
	.num_regs		= ARRAY_SIZE(glymur_pciephy_reg_l),
	.pd_names		= glymur_pciephy_pd_l,
	.num_pds		= ARRAY_SIZE(glymur_pciephy_pd_l),
	.nocsr_reset_list	= glymur_pciephy_nocsr_reset_l,
	.num_nocsr_resets	= ARRAY_SIZE(glymur_pciephy_nocsr_reset_l),
	.vreg_list		= glymur_vreg_l,
	.num_vregs		= ARRAY_SIZE(glymur_vreg_l),
	.regs			= pciephy_v8_50_regs_layout,
	.phy_status		= PHYSTATUS_4_20,
	.pipe_clk_list		= glymur_pciephy_pipeclk_l,
	.num_pipe_clks		= ARRAY_SIZE(glymur_pciephy_pipeclk_l),
	.clk_list		= glymur_pciephy_clk_l,
	.num_clks		= ARRAY_SIZE(glymur_pciephy_clk_l),
};

static const struct qmp_phy_cfg * const glymur_qmp_gen5x8_mode_x8_cfgs[] = {
	&glymur_qmp_gen5x8_pciephy_cfg,
};

static const struct qmp_phy_cfg * const glymur_qmp_gen5x8_mode_x4x4_cfgs[] = {
	&glymur_qmp_gen5x4_pciephy_a_cfg,
	&glymur_qmp_gen5x4_pciephy_b_cfg,
};

static const struct qmp_pcie_link_mode_cfg glymur_qmp_gen5x8_mode_cfgs[] = {
	[QMP_PCIE_GLYMUR_MODE_X8] = {
		.cfgs		= glymur_qmp_gen5x8_mode_x8_cfgs,
		.num_phys	= ARRAY_SIZE(glymur_qmp_gen5x8_mode_x8_cfgs),
	},
	[QMP_PCIE_GLYMUR_MODE_X4X4] = {
		.cfgs		= glymur_qmp_gen5x8_mode_x4x4_cfgs,
		.num_phys	= ARRAY_SIZE(glymur_qmp_gen5x8_mode_x4x4_cfgs),
	},
};

static const struct qmp_pcie_match_data glymur_qmp_gen5x8_match_data = {
	.mode_cfgs	= glymur_qmp_gen5x8_mode_cfgs,
	.num_modes	= ARRAY_SIZE(glymur_qmp_gen5x8_mode_cfgs),
};

static int qmp_pcie_pd_power_on(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	int i, ret;

	for (i = 0; i < cfg->num_pds; i++) {
		ret = pm_runtime_resume_and_get(qmp->pd_devs[i]);
		if (ret < 0) {
			dev_err(qmp->dev, "failed to power on %s domain: %d\n",
				cfg->pd_names[i], ret);
			goto err_power_off;
		}
	}

	return 0;

err_power_off:
	while (--i >= 0)
		pm_runtime_put(qmp->pd_devs[i]);

	return ret;
}

static void qmp_pcie_pd_power_off(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	int i;

	for (i = cfg->num_pds - 1; i >= 0; i--)
		pm_runtime_put(qmp->pd_devs[i]);
}

static int qmp_pcie_init(struct phy *phy)
{
	struct qmp_pcie *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	int ret;

	ret = qmp_pcie_pd_power_on(qmp);
	if (ret)
		return ret;

	ret = regulator_bulk_enable(cfg->num_vregs, qmp->vregs);
	if (ret) {
		dev_err(qmp->dev, "failed to enable regulators: %d\n", ret);
		goto err_pd_power_off;
	}

	ret = reset_control_bulk_assert(qmp->cfg->num_nocsr_resets, qmp->nocsr_resets);
	if (ret) {
		dev_err(qmp->dev, "no-csr reset assert failed: %d\n", ret);
		goto err_disable_regulators;
	}

	usleep_range(200, 300);

	ret = clk_bulk_prepare_enable(qmp->cfg->num_clks, qmp->clks);
	if (ret)
		goto err_disable_regulators;

	return 0;

err_disable_regulators:
	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);
err_pd_power_off:
	qmp_pcie_pd_power_off(qmp);

	return ret;
}

static int qmp_pcie_exit(struct phy *phy)
{
	struct qmp_pcie *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;

	reset_control_bulk_assert(qmp->cfg->num_nocsr_resets, qmp->nocsr_resets);

	clk_bulk_disable_unprepare(qmp->cfg->num_clks, qmp->clks);
	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);
	qmp_pcie_pd_power_off(qmp);

	return 0;
}

static int qmp_pcie_power_on(struct phy *phy)
{
	struct qmp_pcie *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	const struct qmp_pcie_offsets *offs = cfg->offsets;
	void __iomem *status;
	unsigned int val;
	int i, ret;

	ret = clk_bulk_prepare_enable(qmp->cfg->num_pipe_clks, qmp->pipe_clks);
	if (ret)
		return ret;

	ret = reset_control_bulk_deassert(qmp->cfg->num_nocsr_resets, qmp->nocsr_resets);
	if (ret) {
		dev_err(qmp->dev, "no-csr reset deassert failed: %d\n", ret);
		goto err_disable_pipe_clk;
	}

	for (i = 0; i < cfg->num_regs; i++) {
		status = qmp->base[i] + offs->pcs + cfg->regs[QPHY_PCS_STATUS];
		ret = readl_poll_timeout(status, val, !(val & cfg->phy_status), 200,
					 PHY_INIT_COMPLETE_TIMEOUT_US);
		if (ret) {
			dev_err(qmp->dev, "PHY power on timed-out (%s): %d\n",
				cfg->reg_names[i], ret);
			goto err_disable_pipe_clk;
		}
	}

	return 0;

err_disable_pipe_clk:
	clk_bulk_disable_unprepare(qmp->cfg->num_pipe_clks, qmp->pipe_clks);

	return ret;
}

static int qmp_pcie_power_off(struct phy *phy)
{
	struct qmp_pcie *qmp = phy_get_drvdata(phy);

	clk_bulk_disable_unprepare(qmp->cfg->num_pipe_clks, qmp->pipe_clks);

	return 0;
}

static int qmp_pcie_enable(struct phy *phy)
{
	int ret;

	ret = qmp_pcie_init(phy);
	if (ret)
		return ret;

	ret = qmp_pcie_power_on(phy);
	if (ret)
		qmp_pcie_exit(phy);

	return ret;
}

static int qmp_pcie_disable(struct phy *phy)
{
	int ret;

	ret = qmp_pcie_power_off(phy);
	if (ret)
		return ret;

	return qmp_pcie_exit(phy);
}

static const struct phy_ops qmp_pcie_phy_ops = {
	.power_on	= qmp_pcie_enable,
	.power_off	= qmp_pcie_disable,
	.owner		= THIS_MODULE,
};

static void qmp_pcie_pd_detach(void *data)
{
	struct qmp_pcie *qmp = data;
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	int i;

	for (i = 0; i < cfg->num_pds; i++) {
		if (!IS_ERR_OR_NULL(qmp->pd_devs[i]))
			dev_pm_domain_detach(qmp->pd_devs[i], true);
	}
}

static int qmp_pcie_pd_init(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;
	int i, ret;

	if (!cfg->num_pds)
		return 0;

	qmp->pd_devs = devm_kcalloc(dev, cfg->num_pds, sizeof(*qmp->pd_devs),
				    GFP_KERNEL);
	if (!qmp->pd_devs)
		return -ENOMEM;

	for (i = 0; i < cfg->num_pds; i++) {
		qmp->pd_devs[i] = dev_pm_domain_attach_by_name(dev,
							       cfg->pd_names[i]);
		if (IS_ERR_OR_NULL(qmp->pd_devs[i])) {
			ret = PTR_ERR(qmp->pd_devs[i]) ? : -ENODATA;
			goto err_detach;
		}
	}

	return devm_add_action_or_reset(dev, qmp_pcie_pd_detach, qmp);

err_detach:
	while (--i >= 0)
		dev_pm_domain_detach(qmp->pd_devs[i], false);

	return ret;
}

static int qmp_pcie_vreg_init(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;
	int i;

	qmp->vregs = devm_kcalloc(dev, cfg->num_vregs, sizeof(*qmp->vregs),
				  GFP_KERNEL);
	if (!qmp->vregs)
		return -ENOMEM;

	for (i = 0; i < cfg->num_vregs; i++)
		qmp->vregs[i].supply = cfg->vreg_list[i];

	return devm_regulator_bulk_get(dev, cfg->num_vregs, qmp->vregs);
}

static int qmp_pcie_reset_init(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;
	int i, ret;

	qmp->nocsr_resets = devm_kcalloc(dev, cfg->num_nocsr_resets,
					 sizeof(*qmp->nocsr_resets),
					 GFP_KERNEL);
	if (!qmp->nocsr_resets)
		return -ENOMEM;

	for (i = 0; i < cfg->num_nocsr_resets; i++)
		qmp->nocsr_resets[i].id = cfg->nocsr_reset_list[i];

	ret = devm_reset_control_bulk_get_exclusive(dev,
						    cfg->num_nocsr_resets,
						    qmp->nocsr_resets);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get no-csr resets\n");

	return 0;
}

static int qmp_pcie_clk_init(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;
	int i, ret;

	qmp->clks = devm_kcalloc(dev, cfg->num_clks, sizeof(*qmp->clks),
				 GFP_KERNEL);
	if (!qmp->clks)
		return -ENOMEM;

	for (i = 0; i < cfg->num_clks; i++)
		qmp->clks[i].id = cfg->clk_list[i];

	ret = devm_clk_bulk_get_optional(dev, cfg->num_clks, qmp->clks);
	if (ret)
		return ret;

	qmp->pipe_clks = devm_kcalloc(dev, cfg->num_pipe_clks,
				      sizeof(*qmp->pipe_clks), GFP_KERNEL);
	if (!qmp->pipe_clks)
		return -ENOMEM;

	for (i = 0; i < cfg->num_pipe_clks; i++)
		qmp->pipe_clks[i].id = cfg->pipe_clk_list[i];

	return devm_clk_bulk_get_optional(dev, cfg->num_pipe_clks,
					  qmp->pipe_clks);
}

static int __phy_pipe_clk_register(struct device *dev, struct device_node *np,
				   int idx, struct clk_fixed_rate *fixed)
{
	struct clk_init_data init = { };
	int ret;

	ret = of_property_read_string_index(np, "clock-output-names", idx,
					    &init.name);
	if (ret) {
		dev_err(dev, "%pOFn: No clock-output-names\n", np);
		return ret;
	}

	init.ops = &clk_fixed_rate_ops;

	fixed->fixed_rate = 125000000;

	fixed->hw.init = &init;

	return devm_clk_hw_register(dev, &fixed->hw);
}

static struct clk_hw *
qmp_pcie_multiphy_clk_hw_get(struct of_phandle_args *clkspec, void *data)
{
	struct qmp_pcie_multiphy *qmp_data = data;
	unsigned int idx = 0;

	if (clkspec->args_count)
		idx = clkspec->args[0];

	if (idx < (unsigned int)qmp_data->num_pipe_outputs)
		return &qmp_data->pipe_out_clks[idx].hw;

	return ERR_PTR(-EINVAL);
}

static int qmp_pcie_multiphy_register_clocks(struct device *dev,
					     struct device_node *np,
					     struct qmp_pcie_multiphy *qmp_data)
{
	int num_pipe_outputs;
	int i, ret;

	num_pipe_outputs = of_property_count_strings(np, "clock-output-names");

	qmp_data->num_pipe_outputs = num_pipe_outputs;
	qmp_data->pipe_out_clks = devm_kcalloc(dev, num_pipe_outputs,
					       sizeof(*qmp_data->pipe_out_clks),
					       GFP_KERNEL);
	if (!qmp_data->pipe_out_clks)
		return -ENOMEM;

	for (i = 0; i < num_pipe_outputs; i++) {
		ret = __phy_pipe_clk_register(dev, np, i,
					      &qmp_data->pipe_out_clks[i]);
		if (ret)
			return ret;
	}

	return devm_of_clk_add_hw_provider(dev, qmp_pcie_multiphy_clk_hw_get, qmp_data);
}

static int qmp_pcie_get_mmio(struct qmp_pcie *qmp)
{
	struct platform_device *pdev = to_platform_device(qmp->dev);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;
	void __iomem *base;
	int i;

	qmp->base = devm_kcalloc(dev, cfg->num_regs, sizeof(*qmp->base),
				 GFP_KERNEL);
	if (!qmp->base)
		return -ENOMEM;

	for (i = 0; i < cfg->num_regs; i++) {
		base = devm_platform_ioremap_resource_byname(pdev, cfg->reg_names[i]);
		if (IS_ERR(base))
			return PTR_ERR(base);

		qmp->base[i] = base;
	}

	return 0;
}

static int qmp_pcie_read_link_mode(struct device *dev, unsigned int *link_mode)
{
	struct regmap *map;
	unsigned int args[1];

	map = syscon_regmap_lookup_by_phandle_args(dev->of_node, "qcom,link-mode",
						   ARRAY_SIZE(args), args);
	if (IS_ERR(map))
		return PTR_ERR(map);

	return regmap_read(map, args[0], link_mode);
}

static struct phy *qmp_pcie_multiphy_xlate(struct device *dev,
					   const struct of_phandle_args *args)
{
	struct qmp_pcie_multiphy *qmp_data = dev_get_drvdata(dev);
	unsigned int idx;

	if (!qmp_data || args->args_count < 1)
		return ERR_PTR(-EINVAL);

	idx = args->args[0];

	if (idx < (unsigned int)qmp_data->mode_cfg->num_phys)
		return qmp_data->phys[idx] ?: ERR_PTR(-EINVAL);

	return ERR_PTR(-EINVAL);
}

static int qmp_pcie_probe_phy(struct qmp_pcie *qmp, struct device_node *np,
			      struct phy **out_phy)
{
	int ret;

	ret = qmp_pcie_get_mmio(qmp);
	if (ret)
		return ret;

	ret = qmp_pcie_clk_init(qmp);
	if (ret)
		return ret;

	ret = qmp_pcie_reset_init(qmp);
	if (ret)
		return ret;

	ret = qmp_pcie_vreg_init(qmp);
	if (ret)
		return ret;

	ret = qmp_pcie_pd_init(qmp);
	if (ret)
		return ret;

	*out_phy = devm_phy_create(qmp->dev, np, &qmp_pcie_phy_ops);
	if (IS_ERR(*out_phy))
		return PTR_ERR(*out_phy);

	phy_set_drvdata(*out_phy, qmp);

	return 0;
}

static int qmp_pcie_multiphy_probe(struct platform_device *pdev)
{
	const struct qmp_pcie_match_data *match_data;
	struct qmp_pcie_multiphy *qmp_data;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	unsigned int link_mode;
	struct qmp_pcie *qmp;
	struct phy **phys;
	int phy_index;
	int ret;

	qmp_data = devm_kzalloc(dev, sizeof(*qmp_data), GFP_KERNEL);

	match_data = of_device_get_match_data(dev);
	if (!match_data)
		return -EINVAL;

	if (!qmp_data)
		return -ENOMEM;

	ret = qmp_pcie_read_link_mode(dev, &link_mode);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read qcom,link-mode\n");

	if (link_mode >= match_data->num_modes)
		return dev_err_probe(dev, -EINVAL, "invalid qcom,link-mode: %u\n",
				     link_mode);

	qmp_data->mode_cfg = &match_data->mode_cfgs[link_mode];

	qmp = devm_kcalloc(dev, qmp_data->mode_cfg->num_phys, sizeof(*qmp), GFP_KERNEL);
	if (!qmp)
		return -ENOMEM;

	phys = devm_kcalloc(dev, qmp_data->mode_cfg->num_phys, sizeof(*phys), GFP_KERNEL);
	if (!phys)
		return -ENOMEM;

	qmp_data->phys = phys;
	dev_set_drvdata(dev, qmp_data);

	for (phy_index = 0; phy_index < qmp_data->mode_cfg->num_phys; phy_index++) {
		qmp[phy_index].dev = dev;
		qmp[phy_index].cfg = qmp_data->mode_cfg->cfgs[phy_index];
		ret = qmp_pcie_probe_phy(&qmp[phy_index], dev->of_node, &phys[phy_index]);
		if (ret)
			return ret;
	}

	ret = qmp_pcie_multiphy_register_clocks(dev, dev->of_node, qmp_data);
	if (ret)
		return ret;

	phy_provider = devm_of_phy_provider_register(dev, qmp_pcie_multiphy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id qmp_pcie_multiphy_of_match_table[] = {
	{
		.compatible = "qcom,glymur-qmp-gen5x8-pcie-phy",
		.data = &glymur_qmp_gen5x8_match_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, qmp_pcie_multiphy_of_match_table);

static struct platform_driver qmp_pcie_multiphy_driver = {
	.probe		= qmp_pcie_multiphy_probe,
	.driver = {
		.name	= "qcom-qmp-pcie-multiphy",
		.of_match_table = qmp_pcie_multiphy_of_match_table,
	},
};
module_platform_driver(qmp_pcie_multiphy_driver);

MODULE_AUTHOR("Qiang Yu <qiang.yu@oss.qualcomm.com>");
MODULE_DESCRIPTION("Qualcomm QMP PCIe Multi-PHY driver for Glymur");
MODULE_LICENSE("GPL");
