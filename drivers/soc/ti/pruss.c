// SPDX-License-Identifier: GPL-2.0-only
/*
 * PRU-ICSS platform driver for various TI SoCs
 *
 * Copyright (C) 2014-2020 Texas Instruments Incorporated - http://www.ti.com/
 * Author(s):
 *	Suman Anna <s-anna@ti.com>
 *	Andrew F. Davis <afd@ti.com>
 *	Tero Kristo <t-kristo@ti.com>
 */

#include <linux/clk-provider.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pruss_driver.h>
#include <linux/regmap.h>
#include <linux/remoteproc.h>
#include <linux/slab.h>
#include "pruss.h"

/**
 * struct pruss_private_data - PRUSS driver private data
 * @has_no_sharedram: flag to indicate the absence of PRUSS Shared Data RAM
 * @has_core_mux_clock: flag to indicate the presence of PRUSS core clock
 */
struct pruss_private_data {
	bool has_no_sharedram;
	bool has_core_mux_clock;
};

/**
 * pruss_get() - get the pruss for a given PRU remoteproc
 * @rproc: remoteproc handle of a PRU instance
 *
 * Finds the parent pruss device for a PRU given the @rproc handle of the
 * PRU remote processor. This function increments the pruss device's refcount,
 * so always use pruss_put() to decrement it back once pruss isn't needed
 * anymore.
 *
 * This API doesn't check if @rproc is valid or not. It is expected the caller
 * will have done a pru_rproc_get() on @rproc, before calling this API to make
 * sure that @rproc is valid.
 *
 * Return: pruss handle on success, and an ERR_PTR on failure using one
 * of the following error values
 *    -EINVAL if invalid parameter
 *    -ENODEV if PRU device or PRUSS device is not found
 */
struct pruss *pruss_get(struct rproc *rproc)
{
	struct pruss *pruss;
	struct device *dev;
	struct platform_device *ppdev;

	if (IS_ERR_OR_NULL(rproc))
		return ERR_PTR(-EINVAL);

	dev = &rproc->dev;

	/* make sure it is PRU rproc */
	if (!dev->parent || !is_pru_rproc(dev->parent))
		return ERR_PTR(-ENODEV);

	ppdev = to_platform_device(dev->parent->parent);
	pruss = platform_get_drvdata(ppdev);
	if (!pruss)
		return ERR_PTR(-ENODEV);

	get_device(pruss->dev);

	return pruss;
}
EXPORT_SYMBOL_GPL(pruss_get);

/**
 * pruss_put() - decrement pruss device's usecount
 * @pruss: pruss handle
 *
 * Complimentary function for pruss_get(). Needs to be called
 * after the PRUSS is used, and only if the pruss_get() succeeds.
 */
void pruss_put(struct pruss *pruss)
{
	if (IS_ERR_OR_NULL(pruss))
		return;

	put_device(pruss->dev);
}
EXPORT_SYMBOL_GPL(pruss_put);

/**
 * pruss_request_mem_region() - request a memory resource
 * @pruss: the pruss instance
 * @mem_id: the memory resource id
 * @region: pointer to memory region structure to be filled in
 *
 * This function allows a client driver to request a memory resource,
 * and if successful, will let the client driver own the particular
 * memory region until released using the pruss_release_mem_region()
 * API.
 *
 * Return: 0 if requested memory region is available (in such case pointer to
 * memory region is returned via @region), an error otherwise
 */
int pruss_request_mem_region(struct pruss *pruss, enum pruss_mem mem_id,
			     struct pruss_mem_region *region)
{
	if (!pruss || !region || mem_id >= PRUSS_MEM_MAX)
		return -EINVAL;

	mutex_lock(&pruss->lock);

	if (pruss->mem_in_use[mem_id]) {
		mutex_unlock(&pruss->lock);
		return -EBUSY;
	}

	*region = pruss->mem_regions[mem_id];
	pruss->mem_in_use[mem_id] = region;

	mutex_unlock(&pruss->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(pruss_request_mem_region);

/**
 * pruss_release_mem_region() - release a memory resource
 * @pruss: the pruss instance
 * @region: the memory region to release
 *
 * This function is the complimentary function to
 * pruss_request_mem_region(), and allows the client drivers to
 * release back a memory resource.
 *
 * Return: 0 on success, an error code otherwise
 */
int pruss_release_mem_region(struct pruss *pruss,
			     struct pruss_mem_region *region)
{
	int id;

	if (!pruss || !region)
		return -EINVAL;

	mutex_lock(&pruss->lock);

	/* find out the memory region being released */
	for (id = 0; id < PRUSS_MEM_MAX; id++) {
		if (pruss->mem_in_use[id] == region)
			break;
	}

	if (id == PRUSS_MEM_MAX) {
		mutex_unlock(&pruss->lock);
		return -EINVAL;
	}

	pruss->mem_in_use[id] = NULL;

	mutex_unlock(&pruss->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(pruss_release_mem_region);

/**
 * pruss_cfg_get_gpmux() - get the current GPMUX value for a PRU device
 * @pruss: pruss instance
 * @pru_id: PRU identifier (0-1)
 * @mux: pointer to store the current mux value into
 *
 * Return: 0 on success, or an error code otherwise
 */
int pruss_cfg_get_gpmux(struct pruss *pruss, enum pruss_pru_id pru_id, u8 *mux)
{
	int ret;
	u32 val;

	if (pru_id >= PRUSS_NUM_PRUS || !mux)
		return -EINVAL;

	ret = pruss_cfg_read(pruss, PRUSS_CFG_GPCFG(pru_id), &val);
	if (!ret)
		*mux = (u8)((val & PRUSS_GPCFG_PRU_MUX_SEL_MASK) >>
			    PRUSS_GPCFG_PRU_MUX_SEL_SHIFT);
	return ret;
}
EXPORT_SYMBOL_GPL(pruss_cfg_get_gpmux);

/**
 * pruss_cfg_set_gpmux() - set the GPMUX value for a PRU device
 * @pruss: pruss instance
 * @pru_id: PRU identifier (0-1)
 * @mux: new mux value for PRU
 *
 * Return: 0 on success, or an error code otherwise
 */
int pruss_cfg_set_gpmux(struct pruss *pruss, enum pruss_pru_id pru_id, u8 mux)
{
	if (mux >= PRUSS_GP_MUX_SEL_MAX ||
	    pru_id >= PRUSS_NUM_PRUS)
		return -EINVAL;

	return pruss_cfg_update(pruss, PRUSS_CFG_GPCFG(pru_id),
				PRUSS_GPCFG_PRU_MUX_SEL_MASK,
				(u32)mux << PRUSS_GPCFG_PRU_MUX_SEL_SHIFT);
}
EXPORT_SYMBOL_GPL(pruss_cfg_set_gpmux);

/**
 * pruss_cfg_gpimode() - set the GPI mode of the PRU
 * @pruss: the pruss instance handle
 * @pru_id: id of the PRU core within the PRUSS
 * @mode: GPI mode to set
 *
 * Sets the GPI mode for a given PRU by programming the
 * corresponding PRUSS_CFG_GPCFGx register
 *
 * Return: 0 on success, or an error code otherwise
 */
int pruss_cfg_gpimode(struct pruss *pruss, enum pruss_pru_id pru_id,
		      enum pruss_gpi_mode mode)
{
	if (pru_id >= PRUSS_NUM_PRUS || mode >= PRUSS_GPI_MODE_MAX)
		return -EINVAL;

	return pruss_cfg_update(pruss, PRUSS_CFG_GPCFG(pru_id),
				PRUSS_GPCFG_PRU_GPI_MODE_MASK,
				mode << PRUSS_GPCFG_PRU_GPI_MODE_SHIFT);
}
EXPORT_SYMBOL_GPL(pruss_cfg_gpimode);

/**
 * pruss_cfg_miirt_enable() - Enable/disable MII RT Events
 * @pruss: the pruss instance
 * @enable: enable/disable
 *
 * Enable/disable the MII RT Events for the PRUSS.
 *
 * Return: 0 on success, or an error code otherwise
 */
int pruss_cfg_miirt_enable(struct pruss *pruss, bool enable)
{
	u32 set = enable ? PRUSS_MII_RT_EVENT_EN : 0;

	return pruss_cfg_update(pruss, PRUSS_CFG_MII_RT,
				PRUSS_MII_RT_EVENT_EN, set);
}
EXPORT_SYMBOL_GPL(pruss_cfg_miirt_enable);

/**
 * pruss_cfg_xfr_enable() - Enable/disable XIN XOUT shift functionality
 * @pruss: the pruss instance
 * @pru_type: PRU core type identifier
 * @enable: enable/disable
 *
 * Return: 0 on success, or an error code otherwise
 */
int pruss_cfg_xfr_enable(struct pruss *pruss, enum pru_type pru_type,
			 bool enable)
{
	u32 mask, set;

	switch (pru_type) {
	case PRU_TYPE_PRU:
		mask = PRUSS_SPP_XFER_SHIFT_EN;
		break;
	case PRU_TYPE_RTU:
		mask = PRUSS_SPP_RTU_XFR_SHIFT_EN;
		break;
	default:
		return -EINVAL;
	}

	set = enable ? mask : 0;

	return pruss_cfg_update(pruss, PRUSS_CFG_SPP, mask, set);
}
EXPORT_SYMBOL_GPL(pruss_cfg_xfr_enable);

static void pruss_of_free_clk_provider(void *data)
{
	struct device_node *clk_mux_np = data;

	of_clk_del_provider(clk_mux_np);
	of_node_put(clk_mux_np);
}

static void pruss_clk_unregister_mux(void *data)
{
	clk_unregister_mux(data);
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

	ret = devm_add_action_or_reset(dev, pruss_clk_unregister_mux, clk_mux);
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
	struct device *dev = pruss->dev;
	struct device_node *clks_np __free(device_node) =
			of_get_child_by_name(cfg_node, "clocks");
	const struct pruss_private_data *data = of_device_get_match_data(dev);
	int ret;

	if (!clks_np)
		return dev_err_probe(dev, -ENODEV,
				     "%pOF is missing its 'clocks' node\n",
				     cfg_node);

	if (data && data->has_core_mux_clock) {
		ret = pruss_clk_mux_setup(pruss, pruss->core_clk_mux,
					  "coreclk-mux", clks_np);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to setup coreclk-mux\n");
	}

	ret = pruss_clk_mux_setup(pruss, pruss->iep_clk_mux, "iepclk-mux",
				  clks_np);
	if (ret)
		return dev_err_probe(dev, ret, "failed to setup iepclk-mux\n");

	return 0;
}

static int pruss_of_setup_memories(struct device *dev, struct pruss *pruss)
{
	struct device_node *np = dev_of_node(dev);
	struct device_node *child __free(device_node) =
			of_get_child_by_name(np, "memories");
	const struct pruss_private_data *data = of_device_get_match_data(dev);
	const char *mem_names[PRUSS_MEM_MAX] = { "dram0", "dram1", "shrdram2" };
	int i;

	if (!child)
		return dev_err_probe(dev, -ENODEV,
				     "%pOF is missing its 'memories' node\n",
				     child);

	for (i = 0; i < PRUSS_MEM_MAX; i++) {
		struct resource res;
		int index;

		/*
		 * On AM437x one of two PRUSS units don't contain Shared RAM,
		 * skip it
		 */
		if (data && data->has_no_sharedram && i == PRUSS_MEM_SHRD_RAM2)
			continue;

		index = of_property_match_string(child, "reg-names",
						 mem_names[i]);
		if (index < 0)
			return index;

		if (of_address_to_resource(child, index, &res))
			return -EINVAL;

		pruss->mem_regions[i].va = devm_ioremap(dev, res.start,
							resource_size(&res));
		if (!pruss->mem_regions[i].va)
			return dev_err_probe(dev, -ENOMEM,
					     "failed to parse and map memory resource %d %s\n",
					     i, mem_names[i]);
		pruss->mem_regions[i].pa = res.start;
		pruss->mem_regions[i].size = resource_size(&res);

		dev_dbg(dev, "memory %8s: pa %pa size 0x%zx va %p\n",
			mem_names[i], &pruss->mem_regions[i].pa,
			pruss->mem_regions[i].size, pruss->mem_regions[i].va);
	}

	return 0;
}

static struct regmap_config regmap_conf = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int pruss_cfg_of_init(struct device *dev, struct pruss *pruss)
{
	struct device_node *np = dev_of_node(dev);
	struct device_node *child __free(device_node) =
			of_get_child_by_name(np, "cfg");
	struct resource res;
	int ret;

	if (!child)
		return dev_err_probe(dev, -ENODEV,
				     "%pOF is missing its 'cfg' node\n", child);

	if (of_address_to_resource(child, 0, &res))
		return -ENOMEM;

	pruss->cfg_base = devm_ioremap(dev, res.start, resource_size(&res));
	if (!pruss->cfg_base)
		return -ENOMEM;

	regmap_conf.name = kasprintf(GFP_KERNEL, "%pOFn@%llx", child,
				     (u64)res.start);
	regmap_conf.max_register = resource_size(&res) - 4;

	pruss->cfg_regmap = devm_regmap_init_mmio(dev, pruss->cfg_base,
						  &regmap_conf);
	kfree(regmap_conf.name);
	if (IS_ERR(pruss->cfg_regmap))
		return dev_err_probe(dev, PTR_ERR(pruss->cfg_regmap),
				     "regmap_init_mmio failed for cfg\n");

	ret = pruss_clk_init(pruss, child);
	if (ret)
		return dev_err_probe(dev, ret, "pruss_clk_init failed\n");

	return 0;
}

static int pruss_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pruss *pruss;
	int ret;

	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "failed to set the DMA coherent mask");
		return ret;
	}

	pruss = devm_kzalloc(dev, sizeof(*pruss), GFP_KERNEL);
	if (!pruss)
		return -ENOMEM;

	pruss->dev = dev;
	mutex_init(&pruss->lock);

	ret = pruss_of_setup_memories(dev, pruss);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, pruss);

	pm_runtime_enable(dev);
	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		dev_err(dev, "couldn't enable module\n");
		goto rpm_disable;
	}

	ret = pruss_cfg_of_init(dev, pruss);
	if (ret < 0)
		goto rpm_put;

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

static void pruss_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	devm_of_platform_depopulate(dev);

	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
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
	{ .compatible = "ti,am642-icssg", .data = &am65x_j721e_pruss_data, },
	{ .compatible = "ti,am625-pruss", .data = &am65x_j721e_pruss_data, },
	{},
};
MODULE_DEVICE_TABLE(of, pruss_of_match);

static struct platform_driver pruss_driver = {
	.driver = {
		.name = "pruss",
		.of_match_table = pruss_of_match,
	},
	.probe = pruss_probe,
	.remove = pruss_remove,
};
module_platform_driver(pruss_driver);

MODULE_AUTHOR("Suman Anna <s-anna@ti.com>");
MODULE_DESCRIPTION("PRU-ICSS Subsystem Driver");
MODULE_LICENSE("GPL v2");
