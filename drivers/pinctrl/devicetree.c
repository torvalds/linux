/*
 * Device tree integration for the pin control subsystem
 *
 * Copyright (C) 2012 NVIDIA CORPORATION. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/of.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/slab.h>

#include "core.h"
#include "devicetree.h"

/**
 * struct pinctrl_dt_map - mapping table chunk parsed from device tree
 * @node: list node for struct pinctrl's @dt_maps field
 * @pctldev: the pin controller that allocated this struct, and will free it
 * @maps: the mapping table entries
 */
struct pinctrl_dt_map {
	struct list_head node;
	struct pinctrl_dev *pctldev;
	struct pinctrl_map *map;
	unsigned num_maps;
};

static void dt_free_map(struct pinctrl_dev *pctldev,
		     struct pinctrl_map *map, unsigned num_maps)
{
	if (pctldev) {
		struct pinctrl_ops *ops = pctldev->desc->pctlops;
		ops->dt_free_map(pctldev, map, num_maps);
	} else {
		/* There is no pctldev for PIN_MAP_TYPE_DUMMY_STATE */
		kfree(map);
	}
}

void pinctrl_dt_free_maps(struct pinctrl *p)
{
	struct pinctrl_dt_map *dt_map, *n1;

	list_for_each_entry_safe(dt_map, n1, &p->dt_maps, node) {
		pinctrl_unregister_map(dt_map->map);
		list_del(&dt_map->node);
		dt_free_map(dt_map->pctldev, dt_map->map,
			    dt_map->num_maps);
		kfree(dt_map);
	}

	of_node_put(p->dev->of_node);
}

static int dt_remember_or_free_map(struct pinctrl *p, const char *statename,
				   struct pinctrl_dev *pctldev,
				   struct pinctrl_map *map, unsigned num_maps)
{
	int i;
	struct pinctrl_dt_map *dt_map;

	/* Initialize common mapping table entry fields */
	for (i = 0; i < num_maps; i++) {
		map[i].dev_name = dev_name(p->dev);
		map[i].name = statename;
		if (pctldev)
			map[i].ctrl_dev_name = dev_name(pctldev->dev);
	}

	/* Remember the converted mapping table entries */
	dt_map = kzalloc(sizeof(*dt_map), GFP_KERNEL);
	if (!dt_map) {
		dev_err(p->dev, "failed to alloc struct pinctrl_dt_map\n");
		dt_free_map(pctldev, map, num_maps);
		return -ENOMEM;
	}

	dt_map->pctldev = pctldev;
	dt_map->map = map;
	dt_map->num_maps = num_maps;
	list_add_tail(&dt_map->node, &p->dt_maps);

	return pinctrl_register_map(map, num_maps, false, true);
}

static struct pinctrl_dev *find_pinctrl_by_of_node(struct device_node *np)
{
	struct pinctrl_dev *pctldev;

	list_for_each_entry(pctldev, &pinctrldev_list, node)
		if (pctldev->dev->of_node == np)
			return pctldev;

	return NULL;
}

struct pinctrl_dev *of_pinctrl_get(struct device_node *np)
{
	struct pinctrl_dev *pctldev;

	pctldev = find_pinctrl_by_of_node(np);
	if (!pctldev)
		return NULL;

	return pctldev;
}

static int dt_to_map_one_config(struct pinctrl *p, const char *statename,
				struct device_node *np_config)
{
	struct device_node *np_pctldev;
	struct pinctrl_dev *pctldev;
	struct pinctrl_ops *ops;
	int ret;
	struct pinctrl_map *map;
	unsigned num_maps;

	/* Find the pin controller containing np_config */
	np_pctldev = of_node_get(np_config);
	for (;;) {
		np_pctldev = of_get_next_parent(np_pctldev);
		if (!np_pctldev || of_node_is_root(np_pctldev)) {
			dev_info(p->dev, "could not find pctldev for node %s, deferring probe\n",
				np_config->full_name);
			of_node_put(np_pctldev);
			/* OK let's just assume this will appear later then */
			return -EPROBE_DEFER;
		}
		pctldev = find_pinctrl_by_of_node(np_pctldev);
		if (pctldev)
			break;
		/* Do not defer probing of hogs (circular loop) */
		if (np_pctldev == p->dev->of_node) {
			of_node_put(np_pctldev);
			return -ENODEV;
		}
	}
	of_node_put(np_pctldev);

	/*
	 * Call pinctrl driver to parse device tree node, and
	 * generate mapping table entries
	 */
	ops = pctldev->desc->pctlops;
	if (!ops->dt_node_to_map) {
		dev_err(p->dev, "pctldev %s doesn't support DT\n",
			dev_name(pctldev->dev));
		return -ENODEV;
	}
	ret = ops->dt_node_to_map(pctldev, np_config, &map, &num_maps);
	if (ret < 0)
		return ret;

	/* Stash the mapping table chunk away for later use */
	return dt_remember_or_free_map(p, statename, pctldev, map, num_maps);
}

static int dt_remember_dummy_state(struct pinctrl *p, const char *statename)
{
	struct pinctrl_map *map;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map) {
		dev_err(p->dev, "failed to alloc struct pinctrl_map\n");
		return -ENOMEM;
	}

	/* There is no pctldev for PIN_MAP_TYPE_DUMMY_STATE */
	map->type = PIN_MAP_TYPE_DUMMY_STATE;

	return dt_remember_or_free_map(p, statename, NULL, map, 1);
}

int pinctrl_dt_to_map(struct pinctrl *p)
{
	struct device_node *np = p->dev->of_node;
	int state, ret;
	char *propname;
	struct property *prop;
	const char *statename;
	const __be32 *list;
	int size, config;
	phandle phandle;
	struct device_node *np_config;

	/* CONFIG_OF enabled, p->dev not instantiated from DT */
	if (!np) {
		dev_dbg(p->dev, "no of_node; not parsing pinctrl DT\n");
		return 0;
	}

	/* We may store pointers to property names within the node */
	of_node_get(np);

	/* For each defined state ID */
	for (state = 0; ; state++) {
		/* Retrieve the pinctrl-* property */
		propname = kasprintf(GFP_KERNEL, "pinctrl-%d", state);
		prop = of_find_property(np, propname, &size);
		kfree(propname);
		if (!prop)
			break;
		list = prop->value;
		size /= sizeof(*list);

		/* Determine whether pinctrl-names property names the state */
		ret = of_property_read_string_index(np, "pinctrl-names",
						    state, &statename);
		/*
		 * If not, statename is just the integer state ID. But rather
		 * than dynamically allocate it and have to free it later,
		 * just point part way into the property name for the string.
		 */
		if (ret < 0) {
			/* strlen("pinctrl-") == 8 */
			statename = prop->name + 8;
		}

		/* For every referenced pin configuration node in it */
		for (config = 0; config < size; config++) {
			phandle = be32_to_cpup(list++);

			/* Look up the pin configuration node */
			np_config = of_find_node_by_phandle(phandle);
			if (!np_config) {
				dev_err(p->dev,
					"prop %s index %i invalid phandle\n",
					prop->name, config);
				ret = -EINVAL;
				goto err;
			}

			/* Parse the node */
			ret = dt_to_map_one_config(p, statename, np_config);
			of_node_put(np_config);
			if (ret < 0)
				goto err;
		}

		/* No entries in DT? Generate a dummy state table entry */
		if (!size) {
			ret = dt_remember_dummy_state(p, statename);
			if (ret < 0)
				goto err;
		}
	}

	return 0;

err:
	pinctrl_dt_free_maps(p);
	return ret;
}
