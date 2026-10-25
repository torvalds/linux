// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026, Linaro Ltd.
 */

#include <dt-bindings/media/video-interfaces.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "phy-qcom-mipi-csi2.h"

static int
phy_qcom_mipi_csi2_set_clock_rates(struct mipi_csi2phy_device *csi2phy,
				   s64 link_freq)
{
	struct device *dev = csi2phy->dev;
	unsigned long opp_rate = link_freq / 4;
	struct dev_pm_opp *opp;
	long timer_rate;
	int i, pstate;
	int ret;

	opp = dev_pm_opp_find_freq_ceil(dev, &opp_rate);
	if (IS_ERR(opp)) {
		dev_err(csi2phy->dev, "Couldn't find ceiling for %lld Hz\n",
			link_freq);
		return PTR_ERR(opp);
	}

	pstate = 0;
	for (i = 0; i < csi2phy->pd_list->num_pds; i++) {
		unsigned int perf;

		if (!csi2phy->soc_cfg->genpds[i].scaled)
			continue;

		perf = dev_pm_opp_get_required_pstate(opp, pstate);
		pstate += 1;

		ret = dev_pm_genpd_set_performance_state(csi2phy->pd_list->pd_devs[i], perf);
		if (ret) {
			dev_err(csi2phy->dev, "Couldn't set perf state %u\n",
				perf);
			dev_pm_opp_put(opp);
			goto unset_pstate;
		}
	}
	dev_pm_opp_put(opp);

	ret = dev_pm_opp_set_rate(dev, opp_rate);
	if (ret) {
		dev_err(csi2phy->dev, "dev_pm_opp_set_rate() fail\n");
		goto unset_opp_rate;
	}

	timer_rate = clk_round_rate(csi2phy->timer_clk, link_freq / 4);
	if (timer_rate <= 0) {
		ret = -ENODEV;
		goto unset_opp_rate;
	}

	ret = clk_set_rate(csi2phy->timer_clk, timer_rate);
	if (ret)
		goto unset_opp_rate;

	csi2phy->timer_clk_rate = timer_rate;

	return 0;

unset_opp_rate:
	dev_pm_opp_set_rate(dev, 0);

unset_pstate:
	while (i--) {
		if (!csi2phy->soc_cfg->genpds[i].scaled)
			continue;

		dev_pm_genpd_set_performance_state(csi2phy->pd_list->pd_devs[i], 0);
	}

	return ret;
}

static int phy_qcom_mipi_csi2_configure(struct phy *phy,
					union phy_configure_opts *opts)
{
	struct mipi_csi2phy_device *csi2phy = phy_get_drvdata(phy);
	struct phy_configure_opts_mipi_dphy *dphy_cfg = &opts->mipi_dphy;
	struct mipi_csi2phy_stream_cfg *stream_cfg = &csi2phy->stream_cfg;
	int ret;

	ret = phy_mipi_dphy_config_validate(dphy_cfg);
	if (ret)
		return ret;

	if (dphy_cfg->lanes < 1 || dphy_cfg->lanes > CSI2_MAX_DATA_LANES)
		return -EINVAL;

	if (dphy_cfg->lanes != stream_cfg->num_data_lanes)
		return -EINVAL;

	stream_cfg->link_freq = dphy_cfg->hs_clk_rate;

	return 0;
}

static int phy_qcom_mipi_csi2_set_mode(struct phy *phy,
				       enum phy_mode mode, int submode)
{
	struct mipi_csi2phy_device *csi2phy = phy_get_drvdata(phy);

	if (mode != PHY_MODE_MIPI_DPHY)
		return -EOPNOTSUPP;

	csi2phy->phy_mode = mode;
	return 0;
}

static int phy_qcom_mipi_csi2_power_on(struct phy *phy)
{
	struct mipi_csi2phy_device *csi2phy = phy_get_drvdata(phy);
	const struct mipi_csi2phy_hw_ops *ops = csi2phy->soc_cfg->ops;
	int i, ret;

	ret = regulator_bulk_enable(csi2phy->soc_cfg->num_supplies,
				    csi2phy->supplies);
	if (ret)
		return ret;

	ret = pm_runtime_resume_and_get(csi2phy->dev);
	if (ret < 0)
		goto disable_regulators;

	ret = phy_qcom_mipi_csi2_set_clock_rates(csi2phy, csi2phy->stream_cfg.link_freq);
	if (ret)
		goto poweroff_phy;

	ret = clk_bulk_prepare_enable(csi2phy->soc_cfg->num_clk,
				      csi2phy->clks);
	if (ret) {
		dev_err(csi2phy->dev, "failed to enable clocks, %d\n", ret);
		goto unset_rate;
	}

	ops->reset(csi2phy);

	ops->hw_version_read(csi2phy);

	ret = ops->lanes_enable(csi2phy, &csi2phy->stream_cfg);
	if (ret)
		goto unset_clocks;

	return 0;

unset_clocks:
	clk_bulk_disable_unprepare(csi2phy->soc_cfg->num_clk,
				   csi2phy->clks);

unset_rate:
	dev_pm_opp_set_rate(csi2phy->dev, 0);

	for (i = 0; i < csi2phy->pd_list->num_pds; i++) {
		if (!csi2phy->soc_cfg->genpds[i].scaled)
			continue;

		dev_pm_genpd_set_performance_state(csi2phy->pd_list->pd_devs[i], 0);
	}

poweroff_phy:
	pm_runtime_put_sync(csi2phy->dev);

disable_regulators:
	regulator_bulk_disable(csi2phy->soc_cfg->num_supplies,
			       csi2phy->supplies);

	return ret;
}

static int phy_qcom_mipi_csi2_power_off(struct phy *phy)
{
	struct mipi_csi2phy_device *csi2phy = phy_get_drvdata(phy);
	const struct mipi_csi2phy_hw_ops *ops = csi2phy->soc_cfg->ops;
	int i;

	ops->lanes_disable(csi2phy, &csi2phy->stream_cfg);

	clk_bulk_disable_unprepare(csi2phy->soc_cfg->num_clk,
				   csi2phy->clks);

	dev_pm_opp_set_rate(csi2phy->dev, 0);

	for (i = 0; i < csi2phy->pd_list->num_pds; i++) {
		if (!csi2phy->soc_cfg->genpds[i].scaled)
			continue;

		dev_pm_genpd_set_performance_state(csi2phy->pd_list->pd_devs[i], 0);
	}

	pm_runtime_put_sync(csi2phy->dev);

	regulator_bulk_disable(csi2phy->soc_cfg->num_supplies,
			       csi2phy->supplies);

	return 0;
}

static const struct phy_ops phy_qcom_mipi_csi2_ops = {
	.configure	= phy_qcom_mipi_csi2_configure,
	.set_mode	= phy_qcom_mipi_csi2_set_mode,
	.power_on	= phy_qcom_mipi_csi2_power_on,
	.power_off	= phy_qcom_mipi_csi2_power_off,
	.owner		= THIS_MODULE,
};

static int phy_qcom_mipi_csi2_attach_pm_domains(struct mipi_csi2phy_device *csi2phy)
{
	struct dev_pm_domain_attach_data pd_data = { 0 };
	const char **pd_names;
	int i;

	pd_names = devm_kzalloc(csi2phy->dev,
				sizeof(char *) * csi2phy->soc_cfg->num_genpds,
				GFP_KERNEL);
	if (!pd_names)
		return -ENOMEM;

	for (i = 0; i < csi2phy->soc_cfg->num_genpds; i++)
		pd_names[i] = csi2phy->soc_cfg->genpds[i].name;

	pd_data.pd_names = pd_names;
	pd_data.num_pd_names = csi2phy->soc_cfg->num_genpds;

	return devm_pm_domain_attach_list(csi2phy->dev, &pd_data,
					  &csi2phy->pd_list);
}

static int phy_qcom_mipi_csi2_parse_routing(struct mipi_csi2phy_device *csi2phy)
{
	struct mipi_csi2phy_stream_cfg *stream_cfg = &csi2phy->stream_cfg;
	u32 lane_polarities[CSI2_MAX_DATA_LANES + 1];
	u32 data_lanes[CSI2_MAX_DATA_LANES];
	struct device *dev = csi2phy->dev;
	struct fwnode_handle *ep;
	int num_polarities;
	int num_data_lanes;
	u32 bus_type;
	int i, ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 1, 0);
	if (ep) {
		fwnode_handle_put(ep);
		dev_err(dev, "DPHY split mode is not supported\n");
		return -EOPNOTSUPP;
	}

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0, 0);
	if (!ep) {
		dev_err(dev, "Missing port@0\n");
		return -ENODEV;
	}

	/* bus-type is optional, D-PHY is the default */
	bus_type = MEDIA_BUS_TYPE_CSI2_DPHY;
	fwnode_property_read_u32(ep, "bus-type", &bus_type);

	if (bus_type != MEDIA_BUS_TYPE_CSI2_DPHY) {
		ret = -EOPNOTSUPP;
		dev_err(dev, "Unsupported bus-type %u\n", bus_type);
		goto out_put;
	}

	num_data_lanes = fwnode_property_count_u32(ep, "data-lanes");
	if (num_data_lanes < 1 || num_data_lanes > CSI2_MAX_DATA_LANES) {
		ret = -EINVAL;
		dev_err(dev, "Invalid data-lanes count: %d\n", num_data_lanes);
		goto out_put;
	}
	stream_cfg->num_data_lanes = num_data_lanes;

	ret = fwnode_property_read_u32_array(ep, "data-lanes", data_lanes,
					     stream_cfg->num_data_lanes);
	if (ret) {
		dev_err(dev, "Failed to read data-lanes: %d\n", ret);
		goto out_put;
	}

	/* lane-polarities: optional, up to num_data_lanes + 1 entries */
	memset(lane_polarities, 0x00, sizeof(lane_polarities));
	num_polarities = fwnode_property_count_u32(ep, "lane-polarities");
	if (num_polarities > 0) {
		if (num_polarities != stream_cfg->num_data_lanes + 1) {
			ret = -EINVAL;
			dev_err(dev, "clock+data-lane %d/polarities %d mismatch\n",
				stream_cfg->num_data_lanes + 1, num_polarities);
			goto out_put;
		}

		ret = fwnode_property_read_u32_array(ep, "lane-polarities", lane_polarities,
						     num_polarities);
		if (ret) {
			dev_err(dev, "Failed to read lane-polarities: %d\n", ret);
			goto out_put;
		}
	}

	csi2phy->stream_cfg.lane_cfg.clk.pos = CSI2_DEFAULT_CLK_LANE;
	csi2phy->stream_cfg.lane_cfg.clk.pol = lane_polarities[0];

	for (i = 0; i < csi2phy->stream_cfg.num_data_lanes; i++) {
		if (data_lanes[i] < 1 || data_lanes[i] > CSI2_MAX_DATA_LANES) {
			dev_err(dev, "Invalid lane %d\n", data_lanes[i]);
			ret = -EINVAL;
			goto out_put;
		}

		/* Convert data-lanes = <1 2 3 4> to bit positions */
		csi2phy->stream_cfg.lane_cfg.data[i].pos = data_lanes[i] - 1;
		csi2phy->stream_cfg.lane_cfg.data[i].pol = lane_polarities[i + 1];
	}

	ret = 0;

out_put:
	fwnode_handle_put(ep);

	return ret;
}

static int phy_qcom_mipi_csi2_probe(struct platform_device *pdev)
{
	unsigned int i, num_clk, num_supplies;
	struct mipi_csi2phy_device *csi2phy;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct phy *generic_phy;
	int ret;

	csi2phy = devm_kzalloc(dev, sizeof(*csi2phy), GFP_KERNEL);
	if (!csi2phy)
		return -ENOMEM;

	csi2phy->dev = dev;
	dev_set_drvdata(dev, csi2phy);

	csi2phy->soc_cfg = device_get_match_data(&pdev->dev);

	if (!csi2phy->soc_cfg)
		return -EINVAL;

	num_clk = csi2phy->soc_cfg->num_clk;

	ret = phy_qcom_mipi_csi2_parse_routing(csi2phy);
	if (ret)
		return ret;

	ret = phy_qcom_mipi_csi2_attach_pm_domains(csi2phy);
	if (ret < 0 || csi2phy->pd_list == NULL) {
		if (ret == 0)
			ret = -ENODEV;
		return dev_err_probe(dev, ret, "Failed to attach power-domain list\n");
	}

	ret = devm_pm_runtime_enable(dev);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to enable pm runtime\n");

	ret = devm_clk_bulk_get_all(dev, &csi2phy->clks);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get clocks\n");

	if (num_clk != ret) {
		return dev_err_probe(dev, -ENODEV, "clock count %d expected %d\n",
				     ret, num_clk);
	}

	for (i = 0; i < num_clk; i++) {
		if (!csi2phy->clks[i].id)
			return dev_err_probe(dev, -EINVAL, "Missing clock-names\n");

		if (!strcmp(csi2phy->clks[i].id, "timer")) {
			csi2phy->timer_clk = csi2phy->clks[i].clk;
			break;
		}
	}
	if (!csi2phy->timer_clk)
		return dev_err_probe(dev, -ENODEV, "no timer clock\n");

	ret = devm_pm_opp_set_clkname(dev, "core");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set opp clkname\n");

	ret = devm_pm_opp_of_add_table(dev);
	if (ret)
		return dev_err_probe(dev, ret, "invalid OPP table in device tree\n");

	num_supplies = csi2phy->soc_cfg->num_supplies;
	csi2phy->supplies = devm_kzalloc(dev, sizeof(*csi2phy->supplies) * num_supplies,
					 GFP_KERNEL);
	if (!csi2phy->supplies)
		return -ENOMEM;

	for (i = 0; i < num_supplies; i++)
		csi2phy->supplies[i].supply = csi2phy->soc_cfg->supply_names[i];

	ret = devm_regulator_bulk_get(dev, num_supplies, csi2phy->supplies);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get regulator supplies\n");

	csi2phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi2phy->base))
		return PTR_ERR(csi2phy->base);

	generic_phy = devm_phy_create(dev, NULL, &phy_qcom_mipi_csi2_ops);
	if (IS_ERR(generic_phy)) {
		ret = PTR_ERR(generic_phy);
		return dev_err_probe(dev, ret, "failed to create phy\n");
	}
	csi2phy->phy = generic_phy;

	phy_set_drvdata(generic_phy, csi2phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (!IS_ERR(phy_provider))
		dev_dbg(dev, "Registered MIPI CSI2 PHY device\n");

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id phy_qcom_mipi_csi2_of_match_table[] = {
	{ .compatible	= "qcom,x1e80100-csi2-phy", .data = &mipi_csi2_dphy_4nm_x1e },
	{ }
};
MODULE_DEVICE_TABLE(of, phy_qcom_mipi_csi2_of_match_table);

static struct platform_driver phy_qcom_mipi_csi2_driver = {
	.probe		= phy_qcom_mipi_csi2_probe,
	.driver = {
		.name	= "qcom-mipi-csi2-phy",
		.of_match_table = phy_qcom_mipi_csi2_of_match_table,
	},
};

module_platform_driver(phy_qcom_mipi_csi2_driver);

MODULE_DESCRIPTION("Qualcomm MIPI CSI2 PHY driver");
MODULE_AUTHOR("Bryan O'Donoghue <bod@kernel.org>");
MODULE_LICENSE("GPL");
