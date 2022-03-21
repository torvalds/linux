// SPDX-License-Identifier: GPL-2.0-only
/*
 * PRU-ICSS platform driver for various TI SoCs
 *
 * Copyright (C) 2014-2020 Texas Instruments Incorporated - http://www.ti.com/
 * Author(s):
 *	Suman Anna <s-anna@ti.com>
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/clk-provider.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/pruss_driver.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/**
 * struct pruss_private_data - PRUSS driver private data
 * @has_no_sharedram: flag to indicate the absence of PRUSS Shared Data RAM
 * @has_core_mux_clock: flag to indicate the presence of PRUSS core clock
 */
struct pruss_private_data {
	bool has_no_sharedram;
	bool has_core_mux_clock;
};

static void pruss_of_free_clk_provider(void *data)
{
	struct device_node *clk_mux_np = data;

	of_clk_del_provider(clk_mux_np);
	of_node_put(clk_mux_np);
}

static int pruss_clk_mux_setup(struct pruss *pruss, struct clk *clk_mux,
			       char *mux_name, struct device_node *clks_np)
{
	struct device_node *clk_mux_np;
	struct device *dev = pruss->dev;
	char *clk_mux_name;
	unsigned int num_parents;
	const char **parent_names;
	void __iomem *reg;
	u32 reg_offset;
	int ret;

	clk_mux_np = of_get_child_by_name(clks_np, mux_name);
	if (!clk_mux_np) {
		dev_err(dev, "%pOF is missing its '%s' node\n", clks_np,
			mux_name);
		return -ENODEV;
	}

	num_parents = of_clk_get_parent_count(clk_mux_np);
	if (num_parents < 1) {
		dev_err(dev, "mux-clock %pOF must have parents\n", clk_mux_np);
		ret = -EINVAL;
		goto put_clk_mux_np;
	}

	parent_names = devm_kcalloc(dev, sizeof(*parent_names), num_parents,
				    GFP_KERNEL);
	if (!parent_names) {
		ret = -ENOMEM;
		goto put_clk_mux_np;
	}

	of_clk_parent_fill(clk_mux_np, parent_names, num_parents);

	clk_mux_name = devm_kasprintf(dev, GFP_KERNEL, "%s.%pOFn",
				      dev_name(dev), clk_mux_np);
	if (!clk_mux_name) {
		ret = -ENOMEM;
		goto put_clk_mux_np;
	}

	ret = of_property_read_u32(clk_mux_np, "reg", &reg_offset);
	if (ret)
		goto put_clk_mux_np;

	reg = pruss->cfg_base + reg_offset;

	clk_mux = clk_register_mux(NULL, clk_mux_name, parent_names,
				   num_parents, 0, reg, 0, 1, 0, NULL);
	if (IS_ERR(clk_mux)) {
		ret = PTR_ERR(clk_mux);
		goto put_clk_mux_np;
	}

	ret = devm_add_action_or_reset(dev, (void(*)(void *))clk_unregister_mux,
				       clk_mux);
	if (ret) {
		dev_err(dev, "failed to add clkmux unregister action %d", ret);
		goto put_clk_mux_np;
	}

	ret = of_clk_add_provider(clk_mux_np, of_clk_src_simple_get, clk_mux);
	if (ret)
		goto put_clk_mux_np;

	ret = devm_add_action_or_reset(dev, pruss_of_free_clk_provider,
				       clk_mux_np);
	if (ret) {
		dev_err(dev, "failed to add clkmux free action %d", ret);
		goto put_clk_mux_np;
	}

	return 0;

put_clk_mux_np:
	of_node_put(clk_mux_np);
	return ret;
}

static int pruss_clk_init(struct pruss *pruss, struct device_node *cfg_node)
{
	const struct pruss_private_data *data;
	struct device_node *clks_np;
	struct device *dev = pruss->dev;
	int ret = 0;

	data = of_device_get_match_data(dev);
	if (IS_ERR(data))
		return -ENODEV;

	clks_np = of_get_child_by_name(cfg_node, "clocks");
	if (!clks_np) {
		dev_err(dev, "%pOF is missing its 'clocks' node\n", cfg_node);
		return -ENODEV;
	}

	if (data && data->has_core_mux_clock) {
		ret = pruss_clk_mux_setup(pruss, pruss->core_clk_mux,
					  "coreclk-mux", clks_np);
		if (ret) {
			dev_err(dev, "failed to setup coreclk-mux\n");
			goto put_clks_node;
		}
	}

	ret = pruss_clk_mux_setup(pruss, pruss->iep_clk_mux, "iepclk-mux",
				  clks_np);
	if (ret) {
		dev_err(dev, "failed to setup iepclk-mux\n");
		goto put_clks_node;
	}

put_clks_node:
	of_node_put(clks_np);

	return ret;
}

static struct regmap_config regmap_conf = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
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

	if (of_address_to_resource(child, 0, &res)) {
		ret = -ENOMEM;
		goto node_put;
	}

	pruss->cfg_base = devm_ioremap(dev, res.start, resource_size(&res));
	if (!pruss->cfg_base) {
		ret = -ENOMEM;
		goto node_put;
	}

	regmap_conf.name = kasprintf(GFP_KERNEL, "%pOFn@%llx", child,
				     (u64)res.start);
	regmap_conf.max_register = resource_size(&res) - 4;

	pruss->cfg_regmap = devm_regmap_init_mmio(dev, pruss->cfg_base,
						  &regmap_conf);
	kfree(regmap_conf.name);
	if (IS_ERR(pruss->cfg_regmap)) {
		dev_err(dev, "regmap_init_mmio failed for cfg, ret = %ld\n",
			PTR_ERR(pruss->cfg_regmap));
		ret = PTR_ERR(pruss->cfg_regmap);
		goto node_put;
	}

	ret = pruss_clk_init(pruss, child);
	if (ret) {
		dev_err(dev, "failed to setup coreclk-mux\n");
		goto node_put;
	}

	ret = devm_of_platform_populate(dev);
	if (ret) {
		dev_err(dev, "failed to register child devices\n");
		goto node_put;
	}

	of_node_put(child);

	return 0;

node_put:
	of_node_put(child);
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

static const struct pruss_private_data am65x_j721e_pruss_data = {
	.has_core_mux_clock = true,
};

static const struct of_device_id pruss_of_match[] = {
	{ .compatible = "ti,am3356-pruss" },
	{ .compatible = "ti,am4376-pruss0", .data = &am437x_pruss0_data, },
	{ .compatible = "ti,am4376-pruss1", .data = &am437x_pruss1_data, },
	{ .compatible = "ti,am5728-pruss" },
	{ .compatible = "ti,k2g-pruss" },
	{ .compatible = "ti,am654-icssg", .data = &am65x_j721e_pruss_data, },
	{ .compatible = "ti,j721e-icssg", .data = &am65x_j721e_pruss_data, },
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
