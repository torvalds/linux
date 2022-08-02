/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip Vehicle driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */
#ifndef __VEHICLE_GPIO_H
#define __VEHICLE_GPIO_H

#include "vehicle_cfg.h"

struct gpio_detect {
	int gpio;
	int atv_val;
	int val;
	int irq;
	int mirror;
	int num;
	unsigned int debounce_ms;
	struct delayed_work work;
	struct device *dev;
};
/*
 * true : reverse on
 * false : reverse over
 */
bool vehicle_gpio_reverse_check(struct gpio_detect *gpiod);

int vehicle_gpio_init(struct gpio_detect *gpiod, const char *ad_name);

int vehicle_gpio_deinit(struct gpio_detect *gpiod);

#endif
