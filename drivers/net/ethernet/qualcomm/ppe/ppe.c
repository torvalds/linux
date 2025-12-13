// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

/* PPE platform device probe, DTSI parser and PPE clock initializations. */

#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "ppe.h"
#include "ppe_config.h"
#include "ppe_debugfs.h"

#define PPE_PORT_MAX		8
#define PPE_CLK_RATE		353000000

/* ICC clocks for enabling PPE device. The avg_bw and peak_bw with value 0
 * will be updated by the clock rate of PPE.
 */
static const struct icc_bulk_data ppe_icc_data[] = {
	{
		.name = "ppe",
		.avg_bw = 0,
		.peak_bw = 0,
	},
	{
		.name = "ppe_cfg",
		.avg_bw = 0,
		.peak_bw = 0,
	},
	{
		.name = "qos_gen",
		.avg_bw = 6000,
		.peak_bw = 6000,
	},
	{
		.name = "timeout_ref",
		.avg_bw = 6000,
		.peak_bw = 6000,
	},
	{
		.name = "nssnoc_memnoc",
		.avg_bw = 533333,
		.peak_bw = 533333,
	},
	{
		.name = "memnoc_nssnoc",
		.avg_bw = 533333,
		.peak_bw = 533333,
	},
	{
		.name = "memnoc_nssnoc_1",
		.avg_bw = 533333,
		.peak_bw = 533333,
	},
};

static const struct regmap_range ppe_readable_ranges[] = {
	regmap_reg_range(0x0, 0x1ff),		/* Global */
	regmap_reg_range(0x400, 0x5ff),		/* LPI CSR */
	regmap_reg_range(0x1000, 0x11ff),	/* GMAC0 */
	regmap_reg_range(0x1200, 0x13ff),	/* GMAC1 */
	regmap_reg_range(0x1400, 0x15ff),	/* GMAC2 */
	regmap_reg_range(0x1600, 0x17ff),	/* GMAC3 */
	regmap_reg_range(0x1800, 0x19ff),	/* GMAC4 */
	regmap_reg_range(0x1a00, 0x1bff),	/* GMAC5 */
	regmap_reg_range(0xb000, 0xefff),	/* PRX CSR */
	regmap_reg_range(0xf000, 0x1efff),	/* IPE */
	regmap_reg_range(0x20000, 0x5ffff),	/* PTX CSR */
	regmap_reg_range(0x60000, 0x9ffff),	/* IPE L2 CSR */
	regmap_reg_range(0xb0000, 0xeffff),	/* IPO CSR */
	regmap_reg_range(0x100000, 0x17ffff),	/* IPE PC */
	regmap_reg_range(0x180000, 0x1bffff),	/* PRE IPO CSR */
	regmap_reg_range(0x1d0000, 0x1dffff),	/* Tunnel parser */
	regmap_reg_range(0x1e0000, 0x1effff),	/* Ingress parse */
	regmap_reg_range(0x200000, 0x2fffff),	/* IPE L3 */
	regmap_reg_range(0x300000, 0x3fffff),	/* IPE tunnel */
	regmap_reg_range(0x400000, 0x4fffff),	/* Scheduler */
	regmap_reg_range(0x500000, 0x503fff),	/* XGMAC0 */
	regmap_reg_range(0x504000, 0x507fff),	/* XGMAC1 */
	regmap_reg_range(0x508000, 0x50bfff),	/* XGMAC2 */
	regmap_reg_range(0x50c000, 0x50ffff),	/* XGMAC3 */
	regmap_reg_range(0x510000, 0x513fff),	/* XGMAC4 */
	regmap_reg_range(0x514000, 0x517fff),	/* XGMAC5 */
	regmap_reg_range(0x600000, 0x6fffff),	/* BM */
	regmap_reg_range(0x800000, 0x9fffff),	/* QM */
	regmap_reg_range(0xb00000, 0xbef800),	/* EDMA */
};

static const struct regmap_access_table ppe_reg_table = {
	.yes_ranges = ppe_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(ppe_readable_ranges),
};

static const struct regmap_config regmap_config_ipq9574 = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.rd_table = &ppe_reg_table,
	.wr_table = &ppe_reg_table,
	.max_register = 0xbef800,
	.fast_io = true,
};

static int ppe_clock_init_and_reset(struct ppe_device *ppe_dev)
{
	unsigned long ppe_rate = ppe_dev->clk_rate;
	struct device *dev = ppe_dev->dev;
	struct reset_control *rstc;
	struct clk_bulk_data *clks;
	struct clk *clk;
	int ret, i;

	for (i = 0; i < ppe_dev->num_icc_paths; i++) {
		ppe_dev->icc_paths[i].name = ppe_icc_data[i].name;
		ppe_dev->icc_paths[i].avg_bw = ppe_icc_data[i].avg_bw ? :
					       Bps_to_icc(ppe_rate);

		/* PPE does not have an explicit peak bandwidth requirement,
		 * so set the peak bandwidth to be equal to the average
		 * bandwidth.
		 */
		ppe_dev->icc_paths[i].peak_bw = ppe_icc_data[i].peak_bw ? :
						Bps_to_icc(ppe_rate);
	}

	ret = devm_of_icc_bulk_get(dev, ppe_dev->num_icc_paths,
				   ppe_dev->icc_paths);
	if (ret)
		return ret;

	ret = icc_bulk_set_bw(ppe_dev->num_icc_paths, ppe_dev->icc_paths);
	if (ret)
		return ret;

	/* The PPE clocks have a common parent clock. Setting the clock
	 * rate of "ppe" ensures the clock rate of all PPE clocks is
	 * configured to the same rate.
	 */
	clk = devm_clk_get(dev, "ppe");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = clk_set_rate(clk, ppe_rate);
	if (ret)
		return ret;

	ret = devm_clk_bulk_get_all_enabled(dev, &clks);
	if (ret < 0)
		return ret;

	/* Reset the PPE. */
	rstc = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(rstc))
		return PTR_ERR(rstc);

	ret = reset_control_assert(rstc);
	if (ret)
		return ret;

	/* The delay 10 ms of assert is necessary for resetting PPE. */
	usleep_range(10000, 11000);

	return reset_control_deassert(rstc);
}

static int qcom_ppe_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ppe_device *ppe_dev;
	void __iomem *base;
	int ret, num_icc;

	num_icc = ARRAY_SIZE(ppe_icc_data);
	ppe_dev = devm_kzalloc(dev, struct_size(ppe_dev, icc_paths, num_icc),
			       GFP_KERNEL);
	if (!ppe_dev)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base), "PPE ioremap failed\n");

	ppe_dev->regmap = devm_regmap_init_mmio(dev, base, &regmap_config_ipq9574);
	if (IS_ERR(ppe_dev->regmap))
		return dev_err_probe(dev, PTR_ERR(ppe_dev->regmap),
				     "PPE initialize regmap failed\n");
	ppe_dev->dev = dev;
	ppe_dev->clk_rate = PPE_CLK_RATE;
	ppe_dev->num_ports = PPE_PORT_MAX;
	ppe_dev->num_icc_paths = num_icc;

	ret = ppe_clock_init_and_reset(ppe_dev);
	if (ret)
		return dev_err_probe(dev, ret, "PPE clock config failed\n");

	ret = ppe_hw_config(ppe_dev);
	if (ret)
		return dev_err_probe(dev, ret, "PPE HW config failed\n");

	ppe_debugfs_setup(ppe_dev);
	platform_set_drvdata(pdev, ppe_dev);

	return 0;
}

static void qcom_ppe_remove(struct platform_device *pdev)
{
	struct ppe_device *ppe_dev;

	ppe_dev = platform_get_drvdata(pdev);
	ppe_debugfs_teardown(ppe_dev);
}

static const struct of_device_id qcom_ppe_of_match[] = {
	{ .compatible = "qcom,ipq9574-ppe" },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_ppe_of_match);

static struct platform_driver qcom_ppe_driver = {
	.driver = {
		.name = "qcom_ppe",
		.of_match_table = qcom_ppe_of_match,
	},
	.probe	= qcom_ppe_probe,
	.remove = qcom_ppe_remove,
};
module_platform_driver(qcom_ppe_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. IPQ PPE driver");
