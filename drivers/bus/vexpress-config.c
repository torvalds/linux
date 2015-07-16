/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2014 ARM Limited
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/vexpress.h>


struct vexpress_config_bridge {
	struct vexpress_config_bridge_ops *ops;
	void *context;
};


static DEFINE_MUTEX(vexpress_config_mutex);
static struct class *vexpress_config_class;
static u32 vexpress_config_site_master = VEXPRESS_SITE_MASTER;


void vexpress_config_set_master(u32 site)
{
	vexpress_config_site_master = site;
}

u32 vexpress_config_get_master(void)
{
	return vexpress_config_site_master;
}

void vexpress_config_lock(void *arg)
{
	mutex_lock(&vexpress_config_mutex);
}

void vexpress_config_unlock(void *arg)
{
	mutex_unlock(&vexpress_config_mutex);
}


static void vexpress_config_find_prop(struct device_node *node,
		const char *name, u32 *val)
{
	/* Default value */
	*val = 0;

	of_node_get(node);
	while (node) {
		if (of_property_read_u32(node, name, val) == 0) {
			of_node_put(node);
			return;
		}
		node = of_get_next_parent(node);
	}
}

int vexpress_config_get_topo(struct device_node *node, u32 *site,
		u32 *position, u32 *dcc)
{
	vexpress_config_find_prop(node, "arm,vexpress,site", site);
	if (*site == VEXPRESS_SITE_MASTER)
		*site = vexpress_config_site_master;
	if (WARN_ON(vexpress_config_site_master == VEXPRESS_SITE_MASTER))
		return -EINVAL;
	vexpress_config_find_prop(node, "arm,vexpress,position", position);
	vexpress_config_find_prop(node, "arm,vexpress,dcc", dcc);

	return 0;
}


static void vexpress_config_devres_release(struct device *dev, void *res)
{
	struct vexpress_config_bridge *bridge = dev_get_drvdata(dev->parent);
	struct regmap *regmap = res;

	bridge->ops->regmap_exit(regmap, bridge->context);
}

struct regmap *devm_regmap_init_vexpress_config(struct device *dev)
{
	struct vexpress_config_bridge *bridge;
	struct regmap *regmap;
	struct regmap **res;

	if (WARN_ON(dev->parent->class != vexpress_config_class))
		return ERR_PTR(-ENODEV);

	bridge = dev_get_drvdata(dev->parent);
	if (WARN_ON(!bridge))
		return ERR_PTR(-EINVAL);

	res = devres_alloc(vexpress_config_devres_release, sizeof(*res),
			GFP_KERNEL);
	if (!res)
		return ERR_PTR(-ENOMEM);

	regmap = bridge->ops->regmap_init(dev, bridge->context);
	if (IS_ERR(regmap)) {
		devres_free(res);
		return regmap;
	}

	*res = regmap;
	devres_add(dev, res);

	return regmap;
}
EXPORT_SYMBOL_GPL(devm_regmap_init_vexpress_config);

struct device *vexpress_config_bridge_register(struct device *parent,
		struct vexpress_config_bridge_ops *ops, void *context)
{
	struct device *dev;
	struct vexpress_config_bridge *bridge;

	if (!vexpress_config_class) {
		vexpress_config_class = class_create(THIS_MODULE,
				"vexpress-config");
		if (IS_ERR(vexpress_config_class))
			return (void *)vexpress_config_class;
	}

	dev = device_create(vexpress_config_class, parent, 0,
			NULL, "%s.bridge", dev_name(parent));

	if (IS_ERR(dev))
		return dev;

	bridge = devm_kmalloc(dev, sizeof(*bridge), GFP_KERNEL);
	if (!bridge) {
		put_device(dev);
		device_unregister(dev);
		return ERR_PTR(-ENOMEM);
	}
	bridge->ops = ops;
	bridge->context = context;

	dev_set_drvdata(dev, bridge);

	dev_dbg(parent, "Registered bridge '%s', parent node %p\n",
			dev_name(dev), parent->of_node);

	return dev;
}


static int vexpress_config_node_match(struct device *dev, const void *data)
{
	const struct device_node *node = data;

	dev_dbg(dev, "Parent node %p, looking for %p\n",
			dev->parent->of_node, node);

	return dev->parent->of_node == node;
}

static int vexpress_config_populate(struct device_node *node)
{
	struct device_node *bridge;
	struct device *parent;

	bridge = of_parse_phandle(node, "arm,vexpress,config-bridge", 0);
	if (!bridge)
		return -EINVAL;

	parent = class_find_device(vexpress_config_class, NULL, bridge,
			vexpress_config_node_match);
	if (WARN_ON(!parent))
		return -ENODEV;

	return of_platform_populate(node, NULL, NULL, parent);
}

static int __init vexpress_config_init(void)
{
	int err = 0;
	struct device_node *node;

	/* Need the config devices early, before the "normal" devices... */
	for_each_compatible_node(node, NULL, "arm,vexpress,config-bus") {
		err = vexpress_config_populate(node);
		if (err)
			break;
	}

	return err;
}
postcore_initcall(vexpress_config_init);

