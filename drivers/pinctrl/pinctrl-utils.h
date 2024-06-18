/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Utils functions to implement the pincontrol driver.
 *
 * Copyright (c) 2013, NVIDIA Corporation.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */
#ifndef __PINCTRL_UTILS_H__
#define __PINCTRL_UTILS_H__

#include <linux/pinctrl/machine.h>

struct pinctrl_dev;
struct pinctrl_map;

int pinctrl_utils_reserve_map(struct pinctrl_dev *pctldev,
		struct pinctrl_map **map, unsigned int *reserved_maps,
		unsigned int *num_maps, unsigned int reserve);
int pinctrl_utils_add_map_mux(struct pinctrl_dev *pctldev,
		struct pinctrl_map **map, unsigned int *reserved_maps,
		unsigned int *num_maps, const char *group,
		const char *function);
int pinctrl_utils_add_map_configs(struct pinctrl_dev *pctldev,
		struct pinctrl_map **map, unsigned int *reserved_maps,
		unsigned int *num_maps, const char *group,
		unsigned long *configs, unsigned int num_configs,
		enum pinctrl_map_type type);
int pinctrl_utils_add_config(struct pinctrl_dev *pctldev,
		unsigned long **configs, unsigned int *num_configs,
		unsigned long config);
void pinctrl_utils_free_map(struct pinctrl_dev *pctldev,
		struct pinctrl_map *map, unsigned int num_maps);

#endif /* __PINCTRL_UTILS_H__ */
