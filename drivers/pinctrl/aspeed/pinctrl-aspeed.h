/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 IBM Corp.
 */

#ifndef PINCTRL_ASPEED
#define PINCTRL_ASPEED

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/regmap.h>

#include "pinmux-aspeed.h"

/**
 * @param The pinconf parameter type
 * @pins The pin range this config struct covers, [low, high]
 * @reg The register housing the configuration bits
 * @mask The mask to select the bits of interest in @reg
 */
struct aspeed_pin_config {
	enum pin_config_param param;
	unsigned int pins[2];
	unsigned int reg;
	u8 bit;
	u8 value;
};

#define ASPEED_PINCTRL_PIN(name_) \
	[name_] = { \
		.number = name_, \
		.name = #name_, \
		.drv_data = (void *) &(PIN_SYM(name_)) \
	}

struct aspeed_pinctrl_data {
	struct regmap *scu;

	const struct pinctrl_pin_desc *pins;
	const unsigned int npins;

	const struct aspeed_pin_config *configs;
	const unsigned int nconfigs;

	struct aspeed_pinmux_data pinmux;
};

/* Aspeed pinctrl helpers */
int aspeed_pinctrl_get_groups_count(struct pinctrl_dev *pctldev);
const char *aspeed_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
		unsigned int group);
int aspeed_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
		unsigned int group, const unsigned int **pins,
		unsigned int *npins);
void aspeed_pinctrl_pin_dbg_show(struct pinctrl_dev *pctldev,
		struct seq_file *s, unsigned int offset);
int aspeed_pinmux_get_fn_count(struct pinctrl_dev *pctldev);
const char *aspeed_pinmux_get_fn_name(struct pinctrl_dev *pctldev,
		unsigned int function);
int aspeed_pinmux_get_fn_groups(struct pinctrl_dev *pctldev,
		unsigned int function, const char * const **groups,
		unsigned int * const num_groups);
int aspeed_pinmux_set_mux(struct pinctrl_dev *pctldev, unsigned int function,
		unsigned int group);
int aspeed_gpio_request_enable(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range,
		unsigned int offset);
int aspeed_pinctrl_probe(struct platform_device *pdev,
		struct pinctrl_desc *pdesc,
		struct aspeed_pinctrl_data *pdata);
int aspeed_pin_config_get(struct pinctrl_dev *pctldev, unsigned int offset,
		unsigned long *config);
int aspeed_pin_config_set(struct pinctrl_dev *pctldev, unsigned int offset,
		unsigned long *configs, unsigned int num_configs);
int aspeed_pin_config_group_get(struct pinctrl_dev *pctldev,
		unsigned int selector,
		unsigned long *config);
int aspeed_pin_config_group_set(struct pinctrl_dev *pctldev,
		unsigned int selector,
		unsigned long *configs,
		unsigned int num_configs);

#endif /* PINCTRL_ASPEED */
