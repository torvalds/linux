// SPDX-License-Identifier: GPL-2.0-only
/*
 * Utils functions to implement the pincontrol driver.
 *
 * Copyright (c) 2013, NVIDIA Corporation.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */
#include <linux/device.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/of.h>
#include <linux/slab.h>
#include "core.h"
#include "pinctrl-utils.h"

int pinctrl_utils_reserve_map(struct pinctrl_dev *pctldev,
		struct pinctrl_map **map, unsigned *reserved_maps,
		unsigned *num_maps, unsigned reserve)
{
	unsigned old_num = *reserved_maps;
	unsigned new_num = *num_maps + reserve;
	struct pinctrl_map *new_map;

	if (old_num >= new_num)
		return 0;

	new_map = krealloc_array(*map, new_num, sizeof(*new_map), GFP_KERNEL);
	if (!new_map) {
		dev_err(pctldev->dev, "krealloc(map) failed\n");
		return -ENOMEM;
	}

	memset(new_map + old_num, 0, (new_num - old_num) * sizeof(*new_map));

	*map = new_map;
	*reserved_maps = new_num;
	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_utils_reserve_map);

int pinctrl_utils_add_map_mux(struct pinctrl_dev *pctldev,
		struct pinctrl_map **map, unsigned *reserved_maps,
		unsigned *num_maps, const char *group,
		const char *function)
{
	if (WARN_ON(*num_maps == *reserved_maps))
		return -ENOSPC;

	(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)[*num_maps].data.mux.group = group;
	(*map)[*num_maps].data.mux.function = function;
	(*num_maps)++;

	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_utils_add_map_mux);

int pinctrl_utils_add_map_configs(struct pinctrl_dev *pctldev,
		struct pinctrl_map **map, unsigned *reserved_maps,
		unsigned *num_maps, const char *group,
		unsigned long *configs, unsigned num_configs,
		enum pinctrl_map_type type)
{
	unsigned long *dup_configs;

	if (WARN_ON(*num_maps == *reserved_maps))
		return -ENOSPC;

	dup_configs = kmemdup(configs, num_configs * sizeof(*dup_configs),
			      GFP_KERNEL);
	if (!dup_configs)
		return -ENOMEM;

	(*map)[*num_maps].type = type;
	(*map)[*num_maps].data.configs.group_or_pin = group;
	(*map)[*num_maps].data.configs.configs = dup_configs;
	(*map)[*num_maps].data.configs.num_configs = num_configs;
	(*num_maps)++;

	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_utils_add_map_configs);

int pinctrl_utils_add_config(struct pinctrl_dev *pctldev,
		unsigned long **configs, unsigned *num_configs,
		unsigned long config)
{
	unsigned old_num = *num_configs;
	unsigned new_num = old_num + 1;
	unsigned long *new_configs;

	new_configs = krealloc(*configs, sizeof(*new_configs) * new_num,
			       GFP_KERNEL);
	if (!new_configs) {
		dev_err(pctldev->dev, "krealloc(configs) failed\n");
		return -ENOMEM;
	}

	new_configs[old_num] = config;

	*configs = new_configs;
	*num_configs = new_num;

	return 0;
}
EXPORT_SYMBOL_GPL(pinctrl_utils_add_config);

void pinctrl_utils_free_map(struct pinctrl_dev *pctldev,
	      struct pinctrl_map *map, unsigned num_maps)
{
	int i;

	for (i = 0; i < num_maps; i++) {
		switch (map[i].type) {
		case PIN_MAP_TYPE_CONFIGS_GROUP:
		case PIN_MAP_TYPE_CONFIGS_PIN:
			kfree(map[i].data.configs.configs);
			break;
		default:
			break;
		}
	}
	kfree(map);
}
EXPORT_SYMBOL_GPL(pinctrl_utils_free_map);
