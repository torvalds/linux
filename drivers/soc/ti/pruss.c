// SPDX-License-Identifier: GPL-2.0-only
/*
 * PRU-ICSS platform driver for various TI SoCs
 *
 * Copyright (C) 2014-2020 Texas Instruments Incorporated - http://www.ti.com/
 * Author(s):
 *	Suman Anna <s-anna@ti.com>
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/pruss_driver.h>

/**
 * struct pruss_private_data - PRUSS driver private data
 * @has_no_sharedram: flag to indicate the absence of PRUSS Shared Data RAM
 */
struct pruss_private_data {
	bool has_no_sharedram;
};

static int pruss_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev_of_node(dev);
	struct device_node *child;
	struct pruss *pruss;
	struct resource res;
	int ret, i, index;
	const struct pruss_private_data *data;
	const char *mem_names[PRUSS_MEM_MAX] = { "dram0", "dram1", "shrdram2" };

	data = of_device_get_match_data(&pdev->dev);
	if (IS_ERR(data)) {
		dev_err(dev, "missing private data\n");
		return -ENODEV;
	}

	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "failed to set the DMA coherent mask");
		return ret;
	}

	pruss = devm_kzalloc(dev, sizeof(*pruss), GFP_KERNEL);
	if (!pruss)
		return -ENOMEM;

	pruss->dev = dev;

	child = of_get_child_by_name(np, "memories");
	if (!child) {
		dev_err(dev, "%pOF is missing its 'memories' node\n", child);
		return -ENODEV;
	}

	for (i = 0; i < PRUSS_MEM_MAX; i++) {
		/*
		 * On AM437x one of two PRUSS units don't contain Shared RAM,
		 * skip it
		 */
		if (data && data->has_no_sharedram && i == PRUSS_MEM_SHRD_RAM2)
			continue;

		index = of_property_match_string(child, "reg-names",
						 mem_names[i]);
		if (index < 0) {
			of_node_put(child);
			return index;
		}

		if (of_address_to_resource(child, index, &res)) {
			of_node_put(child);
			return -EINVAL;
		}

		pruss->mem_regions[i].va = devm_ioremap(dev, res.start,
							resource_size(&res));
		if (!pruss->mem_regions[i].va) {
			dev_err(dev, "failed to parse and map memory resource %d %s\n",
				i, mem_names[i]);
			of_node_put(child);
			return -ENOMEM;
		}
		pruss->mem_regions[i].pa = res.start;
		pruss->mem_regions[i].size = resource_size(&res);

		dev_dbg(dev, "memory %8s: pa %pa size 0x%zx va %pK\n",
			mem_names[i], &pruss->mem_regions[i].pa,
			pruss->mem_regions[i].size, pruss->mem_regions[i].va);
	}
	of_node_put(child);

	platform_set_drvdata(pdev, pruss);

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "couldn't enable module\n");
		pm_runtime_put_noidle(dev);
		goto rpm_disable;
	}

	child = of_get_child_by_name(np, "cfg");
	if (!child) {
		dev_err(dev, "%pOF is missing its 'cfg' node\n", child);
		ret = -ENODEV;
		goto rpm_put;
	}

	pruss->cfg_regmap = syscon_node_to_regmap(child);
	of_node_put(child);
	if (IS_ERR(pruss->cfg_regmap)) {
		ret = -ENODEV;
		goto rpm_put;
	}

	ret = devm_of_platform_populate(dev);
	if (ret) {
		dev_err(dev, "failed to register child devices\n");
		goto rpm_put;
	}

	return 0;

rpm_put:
	pm_runtime_put_sync(dev);
rpm_disable:
	pm_runtime_disable(dev);
	return ret;
}

static int pruss_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	devm_of_platform_depopulate(dev);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return 0;
}

/* instance-specific driver private data */
static const struct pruss_private_data am437x_pruss1_data = {
	.has_no_sharedram = false,
};

static const struct pruss_private_data am437x_pruss0_data = {
	.has_no_sharedram = true,
};

static const struct of_device_id pruss_of_match[] = {
	{ .compatible = "ti,am3356-pruss" },
	{ .compatible = "ti,am4376-pruss0", .data = &am437x_pruss0_data, },
	{ .compatible = "ti,am4376-pruss1", .data = &am437x_pruss1_data, },
	{ .compatible = "ti,am5728-pruss" },
	{ .compatible = "ti,k2g-pruss" },
	{ .compatible = "ti,am654-icssg" },
	{ .compatible = "ti,j721e-icssg" },
	{},
};
MODULE_DEVICE_TABLE(of, pruss_of_match);

static struct platform_driver pruss_driver = {
	.driver = {
		.name = "pruss",
		.of_match_table = pruss_of_match,
	},
	.probe  = pruss_probe,
	.remove = pruss_remove,
};
module_platform_driver(pruss_driver);

MODULE_AUTHOR("Suman Anna <s-anna@ti.com>");
MODULE_DESCRIPTION("PRU-ICSS Subsystem Driver");
MODULE_LICENSE("GPL v2");
