// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Rockchip Electronics Co., Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#include <linux/arm-smccc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rockchip/rockchip_sip.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <soc/rockchip/rockchip_csu.h>

struct csu_bus {
	unsigned int id;
	unsigned int cfg_val;
	unsigned int en_mask;
	unsigned int disable_count;
};

struct csu_clk {
	unsigned int clk_id;
	unsigned int bus_id;
};

struct rockchip_csu {
	struct device *dev;
	struct csu_bus *bus;
	struct csu_clk *clk;
	unsigned int bus_cnt;
	unsigned int clk_cnt;
};

static struct rockchip_csu *rk_csu;
static DEFINE_MUTEX(csu_lock);

static int rockchip_csu_sip_config(struct device *dev, u32 bus_id, u32 cfg,
				   u32 enable_msk)
{
	struct arm_smccc_res res;

	dev_dbg(dev, "id=%u, cfg=0x%x, en_mask=0x%x\n", bus_id, cfg, enable_msk);
	res = sip_smc_bus_config(bus_id, cfg, enable_msk);

	return res.a0;
}

struct csu_clk *rockchip_csu_get(struct device *dev, const char *name)
{
	struct of_phandle_args args;
	struct csu_clk *clk = ERR_PTR(-ENOENT);
	unsigned int clk_id = 0;
	int index = 0, i = 0;

	if (!dev || !dev->of_node)
		return ERR_PTR(-ENODEV);
	if (!rk_csu || !rk_csu->bus || !rk_csu->clk)
		return ERR_PTR(-ENODEV);

	if (name)
		index = of_property_match_string(dev->of_node,
						 "rockchip,csu-names",
						 name);
	if (of_parse_phandle_with_fixed_args(dev->of_node, "rockchip,csu", 1,
					     index, &args)) {
		dev_err(dev, "Missing the phandle args name %s\n", name);
		return ERR_PTR(-ENODEV);
	}
	clk_id = args.args[0];

	for (i = 0; i < rk_csu->clk_cnt; i++) {
		if (clk_id == rk_csu->clk[i].clk_id) {
			clk = &rk_csu->clk[i];
			break;
		}
	}

	return clk;
}
EXPORT_SYMBOL(rockchip_csu_get);

static int csu_disable(struct csu_clk *clk, bool disable)
{
	struct csu_bus *bus = NULL;
	unsigned int en_mask = 0;
	int ret = 0;

	if (IS_ERR_OR_NULL(clk))
		return 0;
	if (clk->bus_id >= rk_csu->bus_cnt)
		return 0;
	bus = &rk_csu->bus[clk->bus_id];
	if (!bus)
		return 0;

	mutex_lock(&csu_lock);

	if (disable)
		bus->disable_count++;
	else if (bus->disable_count > 0)
		bus->disable_count--;

	if (bus->disable_count)
		en_mask = bus->en_mask & CSU_EN_MASK;
	else
		en_mask = bus->en_mask;

	ret = rockchip_csu_sip_config(rk_csu->dev, bus->id, bus->cfg_val, en_mask);
	if (ret)
		dev_err(rk_csu->dev, "csu sip config disable error\n");

	mutex_unlock(&csu_lock);

	return ret;
}

int rockchip_csu_enable(struct csu_clk *clk)
{
	return csu_disable(clk, false);
}
EXPORT_SYMBOL(rockchip_csu_enable);

int rockchip_csu_disable(struct csu_clk *clk)
{
	return csu_disable(clk, true);
}
EXPORT_SYMBOL(rockchip_csu_disable);

int rockchip_csu_set_div(struct csu_clk *clk, unsigned int div)
{
	struct csu_bus *bus = NULL;
	unsigned int cfg_val = 0;
	int ret = 0;

	if (IS_ERR_OR_NULL(clk))
		return 0;
	if (clk->bus_id >= rk_csu->bus_cnt)
		return 0;
	bus = &rk_csu->bus[clk->bus_id];
	if (!bus)
		return 0;

	mutex_lock(&csu_lock);

	if (div > CSU_MAX_DIV)
		div = CSU_MAX_DIV;
	cfg_val = (bus->cfg_val & ~CSU_DIV_MASK) | ((div - 1) & CSU_DIV_MASK);

	ret = rockchip_csu_sip_config(rk_csu->dev, bus->id, cfg_val, bus->en_mask);
	if (ret)
		dev_err(rk_csu->dev, "csu sip config freq error\n");

	mutex_unlock(&csu_lock);

	return ret;
}
EXPORT_SYMBOL(rockchip_csu_set_div);

static int rockchip_csu_parse_clk(struct rockchip_csu *csu)
{
	struct device *dev = csu->dev;
	struct device_node *np = dev->of_node;
	char *prop_name = "rockchip,clock";
	struct csu_clk *tmp;
	const struct property *prop;
	int count, i;

	prop = of_find_property(np, prop_name, NULL);
	if (!prop)
		return -EINVAL;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, prop_name);
	if (count < 0)
		return -EINVAL;

	if (count % 2)
		return -EINVAL;

	tmp = devm_kcalloc(dev, count / 2, sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	for (i = 0; i < count / 2; i++) {
		of_property_read_u32_index(np, prop_name, 2 * i,
					   &tmp[i].clk_id);
		of_property_read_u32_index(np, prop_name, 2 * i + 1,
					   &tmp[i].bus_id);
	}

	csu->clk = tmp;
	csu->clk_cnt = count / 2;

	return 0;
}

static int rockchip_csu_parse_bus_table(struct rockchip_csu *csu)
{
	struct device *dev = csu->dev;
	struct device_node *np = dev->of_node;
	char *prop_name = "rockchip,bus";
	struct csu_bus *tmp;
	const struct property *prop;
	int count, i;

	prop = of_find_property(np, prop_name, NULL);
	if (!prop)
		return -EINVAL;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, prop_name);
	if (count < 0)
		return -EINVAL;

	if (count % 3)
		return -EINVAL;

	tmp = devm_kcalloc(dev, count / 3, sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	for (i = 0; i < count / 3; i++) {
		of_property_read_u32_index(np, prop_name, 3 * i,
					   &tmp[i].id);
		of_property_read_u32_index(np, prop_name, 3 * i + 1,
					   &tmp[i].cfg_val);
		of_property_read_u32_index(np, prop_name, 3 * i + 2,
					   &tmp[i].en_mask);
	}

	csu->bus = tmp;
	csu->bus_cnt = count / 3;

	return 0;
}

static int rockchip_csu_bus_table(struct rockchip_csu *csu)
{
	struct device *dev = csu->dev;
	struct csu_bus *bus;
	int i;

	if (rockchip_csu_parse_bus_table(csu))
		return -EINVAL;

	for (i = 0; i < csu->bus_cnt; i++) {
		bus = &csu->bus[i];
		if (!bus || !bus->cfg_val) {
			dev_info(dev, "bus %d cfg-val invalid\n", i);
			continue;
		}
		if (rockchip_csu_sip_config(dev, bus->id, bus->cfg_val, bus->en_mask))
			dev_err(dev, "csu sip config error\n");
	}

	return 0;
}

static int rockchip_csu_bus_node(struct rockchip_csu *csu)
{
	struct device *dev = csu->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	struct csu_bus *bus;
	int bus_cnt = 0, i = 0;

	for_each_available_child_of_node(np, child)
		bus_cnt++;
	if (bus_cnt <= 0)
		return 0;

	csu->bus = devm_kcalloc(dev, bus_cnt, sizeof(*csu->bus), GFP_KERNEL);
	if (!csu->bus)
		return -ENOMEM;
	csu->bus_cnt = bus_cnt;

	for_each_available_child_of_node(np, child) {
		bus = &csu->bus[i++];
		if (of_property_read_u32_index(child, "bus-id", 0, &bus->id)) {
			dev_info(dev, "get bus-id error\n");
			continue;
		}

		if (of_property_read_u32_index(child, "cfg-val", 0,
					       &bus->cfg_val)) {
			dev_info(dev, "get cfg-val error\n");
			continue;
		}
		if (!bus->cfg_val) {
			dev_info(dev, "cfg-val invalid\n");
			continue;
		}

		if (of_property_read_u32_index(child, "enable-msk", 0,
					       &bus->en_mask)) {
			dev_info(dev, "get enable_msk error\n");
			continue;
		}

		if (rockchip_csu_sip_config(dev, bus->id, bus->cfg_val, bus->en_mask))
			dev_info(dev, "csu smc config error\n");
	}

	return 0;
}

static const struct of_device_id rockchip_csu_of_match[] = {
	{ .compatible = "rockchip,rk3562-csu", },
	{ },
};

MODULE_DEVICE_TABLE(of, rockchip_csu_of_match);

static int rockchip_csu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rockchip_csu *csu;
	int ret = 0;

	csu = devm_kzalloc(dev, sizeof(*csu), GFP_KERNEL);
	if (!csu)
		return -ENOMEM;

	csu->dev = dev;
	platform_set_drvdata(pdev, csu);

	rockchip_csu_parse_clk(csu);

	if (of_find_property(np, "rockchip,bus", NULL))
		ret = rockchip_csu_bus_table(csu);
	else
		ret = rockchip_csu_bus_node(csu);
	if (!ret)
		rk_csu = csu;

	return ret;
}

static struct platform_driver rockchip_csu_driver = {
	.probe	= rockchip_csu_probe,
	.driver = {
		.name	= "rockchip,csu",
		.of_match_table = rockchip_csu_of_match,
	},
};

static int __init rockchip_csu_init(void)
{
	int ret;

	ret = platform_driver_register(&rockchip_csu_driver);
	if (ret) {
		pr_err("failed to register csu driver\n");
		return ret;
	}

	return 0;
}

static void __exit rockchip_csu_exit(void)
{
	return platform_driver_unregister(&rockchip_csu_driver);
}

subsys_initcall(rockchip_csu_init);
module_exit(rockchip_csu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Finley Xiao <finley.xiao@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip clock subunit driver");
