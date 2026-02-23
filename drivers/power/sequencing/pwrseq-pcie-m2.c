// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@oss.qualcomm.com>
 */

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pwrseq/provider.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

struct pwrseq_pcie_m2_pdata {
	const struct pwrseq_target_data **targets;
};

struct pwrseq_pcie_m2_ctx {
	struct pwrseq_device *pwrseq;
	struct device_node *of_node;
	const struct pwrseq_pcie_m2_pdata *pdata;
	struct regulator_bulk_data *regs;
	size_t num_vregs;
	struct notifier_block nb;
};

static int pwrseq_pcie_m2_m_vregs_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return regulator_bulk_enable(ctx->num_vregs, ctx->regs);
}

static int pwrseq_pcie_m2_m_vregs_disable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return regulator_bulk_disable(ctx->num_vregs, ctx->regs);
}

static const struct pwrseq_unit_data pwrseq_pcie_m2_vregs_unit_data = {
	.name = "regulators-enable",
	.enable = pwrseq_pcie_m2_m_vregs_enable,
	.disable = pwrseq_pcie_m2_m_vregs_disable,
};

static const struct pwrseq_unit_data *pwrseq_pcie_m2_m_unit_deps[] = {
	&pwrseq_pcie_m2_vregs_unit_data,
	NULL
};

static const struct pwrseq_unit_data pwrseq_pcie_m2_m_pcie_unit_data = {
	.name = "pcie-enable",
	.deps = pwrseq_pcie_m2_m_unit_deps,
};

static const struct pwrseq_target_data pwrseq_pcie_m2_m_pcie_target_data = {
	.name = "pcie",
	.unit = &pwrseq_pcie_m2_m_pcie_unit_data,
};

static const struct pwrseq_target_data *pwrseq_pcie_m2_m_targets[] = {
	&pwrseq_pcie_m2_m_pcie_target_data,
	NULL
};

static const struct pwrseq_pcie_m2_pdata pwrseq_pcie_m2_m_of_data = {
	.targets = pwrseq_pcie_m2_m_targets,
};

static int pwrseq_pcie_m2_match(struct pwrseq_device *pwrseq,
				 struct device *dev)
{
	struct pwrseq_pcie_m2_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);
	struct device_node *endpoint __free(device_node) = NULL;

	/*
	 * Traverse the 'remote-endpoint' nodes and check if the remote node's
	 * parent matches the OF node of 'dev'.
	 */
	for_each_endpoint_of_node(ctx->of_node, endpoint) {
		struct device_node *remote __free(device_node) =
				of_graph_get_remote_port_parent(endpoint);
		if (remote && (remote == dev_of_node(dev)))
			return PWRSEQ_MATCH_OK;
	}

	return PWRSEQ_NO_MATCH;
}

static void pwrseq_pcie_m2_free_regulators(void *data)
{
	struct pwrseq_pcie_m2_ctx *ctx = data;

	regulator_bulk_free(ctx->num_vregs, ctx->regs);
}

static int pwrseq_pcie_m2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwrseq_pcie_m2_ctx *ctx;
	struct pwrseq_config config = {};
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->of_node = of_node_get(dev->of_node);
	ctx->pdata = device_get_match_data(dev);
	if (!ctx->pdata)
		return dev_err_probe(dev, -ENODEV,
				     "Failed to obtain platform data\n");

	/*
	 * Currently, of_regulator_bulk_get_all() is the only regulator API that
	 * allows to get all supplies in the devicetree node without manually
	 * specifying them.
	 */
	ret = of_regulator_bulk_get_all(dev, dev_of_node(dev), &ctx->regs);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "Failed to get all regulators\n");

	ctx->num_vregs = ret;

	ret = devm_add_action_or_reset(dev, pwrseq_pcie_m2_free_regulators, ctx);
	if (ret)
		return ret;

	config.parent = dev;
	config.owner = THIS_MODULE;
	config.drvdata = ctx;
	config.match = pwrseq_pcie_m2_match;
	config.targets = ctx->pdata->targets;

	ctx->pwrseq = devm_pwrseq_device_register(dev, &config);
	if (IS_ERR(ctx->pwrseq))
		return dev_err_probe(dev, PTR_ERR(ctx->pwrseq),
				     "Failed to register the power sequencer\n");

	return 0;
}

static const struct of_device_id pwrseq_pcie_m2_of_match[] = {
	{
		.compatible = "pcie-m2-m-connector",
		.data = &pwrseq_pcie_m2_m_of_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, pwrseq_pcie_m2_of_match);

static struct platform_driver pwrseq_pcie_m2_driver = {
	.driver = {
		.name = "pwrseq-pcie-m2",
		.of_match_table = pwrseq_pcie_m2_of_match,
	},
	.probe = pwrseq_pcie_m2_probe,
};
module_platform_driver(pwrseq_pcie_m2_driver);

MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@oss.qualcomm.com>");
MODULE_DESCRIPTION("Power Sequencing driver for PCIe M.2 connector");
MODULE_LICENSE("GPL");
