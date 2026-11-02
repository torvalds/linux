// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pinctrl / GPIO driver for StarFive JHB100 SoC
 *
 * Copyright (C) 2024 StarFive Technology Co., Ltd.
 * Author: Alex Soo <yuklin.soo@starfivetech.com>
 *
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/minmax.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinctrl-utils.h"
#include "../pinmux.h"
#include "pinctrl-starfive-jhb100.h"

#define GPOEN_ENABLE				0
#define GPOEN_DISABLE				1

#define JHB100_DEBOUNCE_WIDTH_STAGES_MAX	GENMASK(16, 0)
#define JHB100_DEBOUNCE_WIDTH_STAGE_NS		80

/* mode select */
#define JHB100_PUSH_PULL			0
#define JHB100_OPEN_DRAIN			1
#define JHB100_LEGACY_FAST_MODE_PLUS		2
#define JHB100_LEGACY_FAST_MODE			3

/* i2c open-drain pull-up select */
#define JHB100_I2C_OPEN_DRAIN_PU_600_OHMS	0
#define JHB100_I2C_OPEN_DRAIN_PU_900_OHMS	1
#define JHB100_I2C_OPEN_DRAIN_PU_1200_OHMS	2
#define JHB100_I2C_OPEN_DRAIN_PU_2000_OHMS	3

/*
 * On the StarFive JHB100 SoC, every 32 GPIOs correspond to one register address. The
 * driver abstracts every 32 GPIOs into one GPIO bank to simplify code implementation.
 */
#define JHB100_NR_GPIOS_PER_BANK		32
#define JHB100_BANK_OFFSET(n)			((n) * 4)
#define JHB100_GPIO_BANK(gpio)			((gpio) / JHB100_NR_GPIOS_PER_BANK)
#define JHB100_GPIO_MASK(gpio)			BIT((gpio) % JHB100_NR_GPIOS_PER_BANK)
#define JHB100_GPIO_REG_OFFSET(gpio)		JHB100_BANK_OFFSET(JHB100_GPIO_BANK(gpio))

static int jhb100_map_get_func_val(struct jhb100_pinctrl *sfp, const char *function,
				   unsigned int pin)
{
	const struct jhb100_pinctrl_func_maps *fmaps = sfp->info->fmaps;
	size_t num = sfp->info->num_maps;

	for (int i = 0; i < num; i++) {
		if (!strcmp(function, fmaps[i].func)) {
			if (!fmaps[i].max_pin)
				return fmaps[i].val;

			if (pin < fmaps[i].max_pin)
				return fmaps[i].val;

			continue;
		}
	}

	return -EINVAL;
}

static const struct config_reg_layout_desc *get_crl_desc_by_pin(struct jhb100_pinctrl *sfp,
								unsigned int pin)
{
	const struct config_reg_layout_desc *crl_desc = sfp->info->crl_desc;
	unsigned int i = 0;

	do {
		if (pin >= crl_desc[i].pin_start &&
		    pin < crl_desc[i].pin_start + crl_desc[i].pin_cnt)
			return &crl_desc[i];
	} while (crl_desc[i++].pin_start != 0xff);

	return NULL;
}

static const struct pinctrl_ops jhb100_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name	  = pinctrl_generic_get_group_name,
	.get_group_pins   = pinctrl_generic_get_group_pins,
	.dt_node_to_map	  = pinctrl_generic_pins_function_dt_node_to_map,
	.dt_free_map	  = pinctrl_utils_free_map,
};

static void jhb100_set_function(struct jhb100_pinctrl *sfp,
				unsigned int pin, u8 func)
{
	const struct starfive_pinctrl_regs *pinctrl_regs = sfp->info->regs;
	unsigned int pins_per_reg, offset, shift;
	void __iomem *func_sel_reg;
	u32 func_sel_mask;
	u32 func_sel_val;

	if (!pinctrl_regs->func_sel.reg || !pinctrl_regs->func_sel.width_per_pin)
		return;

	pins_per_reg = 32 / pinctrl_regs->func_sel.width_per_pin;
	offset = 4 * (pin / pins_per_reg);
	shift = pinctrl_regs->func_sel.width_per_pin * (pin % pins_per_reg);

	func_sel_reg = sfp->base + pinctrl_regs->func_sel.reg + offset;
	func_sel_mask = GENMASK(pinctrl_regs->func_sel.width_per_pin - 1, 0) << shift;
	func_sel_val = func << shift;

	guard(raw_spinlock_irqsave)(&sfp->lock);

	func_sel_val |= readl_relaxed(func_sel_reg) & ~func_sel_mask;
	writel_relaxed(func_sel_val, func_sel_reg);
}

static int jhb100_set_one_pin_mux(struct jhb100_pinctrl *sfp,
				  unsigned int pin,
				  u8 func)
{
	jhb100_set_function(sfp, pin, func);

	return 0;
}

static int jhb100_set_mux(struct pinctrl_dev *pctldev,
			  unsigned int fsel, unsigned int gsel)
{
	struct jhb100_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *group;
	unsigned int i;
	const char **functions;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	functions = group->data;

	for (i = 0; i < group->grp.npins; i++) {
		int function;

		function = jhb100_map_get_func_val(sfp, functions[i], group->grp.pins[i]);
		if (function < 0) {
			dev_err(pctldev->dev, "invalid function %s\n", functions[i]);
			return function;
		}

		jhb100_set_one_pin_mux(sfp, group->grp.pins[i], function);
	}

	return 0;
}

static int jhb100_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int pin)
{
	struct jhb100_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	const struct starfive_pinctrl_regs *pinctrl_regs = sfp->info->regs;
	const struct jhb100_pinctrl_domain_info *info = sfp->info;
	unsigned int pins_per_reg, offset, shift;
	u32 func_sel_mask, fs;
	void __iomem *reg_gpio_func_sel;
	s8 gpio_func_sel = sfp->gpio_func_sel_arr[pin];

	if (!pinctrl_regs->func_sel.reg || !pinctrl_regs->func_sel.width_per_pin)
		return -EINVAL;

	pins_per_reg = 32 / pinctrl_regs->func_sel.width_per_pin;
	offset = 4 * (pin / pins_per_reg);
	shift = pinctrl_regs->func_sel.width_per_pin * (pin % pins_per_reg);

	reg_gpio_func_sel = sfp->base + info->regs->func_sel.reg + offset;
	func_sel_mask = GENMASK(info->regs->func_sel.width_per_pin - 1, 0) << shift;

	if (gpio_func_sel < 0)
		return -EINVAL;

	guard(raw_spinlock_irqsave)(&sfp->lock);

	fs = readl_relaxed(reg_gpio_func_sel);
	fs &= ~func_sel_mask;
	fs |= (gpio_func_sel << shift);
	writel_relaxed(fs, reg_gpio_func_sel);

	return 0;
}

static void jhb100_padcfg_rmw(struct jhb100_pinctrl *sfp,
			      unsigned int pin, u32 mask, u32 value)
{
	void __iomem *reg;
	unsigned int offset;
	int padcfg_base;

	padcfg_base = sfp->info->regs->config;

	offset = 4 * pin;

	reg = sfp->base + padcfg_base + offset;

	value &= mask;

	guard(raw_spinlock_irqsave)(&sfp->lock);

	value |= readl_relaxed(reg) & ~mask;
	writel_relaxed(value, reg);
}

static int jhb100_gpio_direction_input(struct gpio_chip *gc,
				       unsigned int gpio)
{
	struct jhb100_pinctrl *sfp = gpiochip_get_data(gc);
	const struct jhb100_pinctrl_domain_info *info = sfp->info;
	const struct config_reg_layout_desc *crl_desc;
	unsigned int offset = JHB100_GPIO_REG_OFFSET(gpio);
	void __iomem *reg_gpio_oen;
	u32 doen = 0;

	crl_desc = get_crl_desc_by_pin(sfp, gpio);
	if (!crl_desc) {
		dev_err(sfp->dev, "pin %d can't not found reg layout descriptor\n",
			gpio);
		return -EINVAL;
	}

	jhb100_padcfg_rmw(sfp, gpio,
			  RL_DESC_GENMASK(crl_desc, PAD_CFG_INPUT_ENABLE) |
			  RL_DESC_GENMASK(crl_desc, PAD_CFG_SCHMITT_TRIGGER_SELECT),
			  RL_DESC_GENMASK(crl_desc, PAD_CFG_INPUT_ENABLE) |
			  RL_DESC_GENMASK(crl_desc, PAD_CFG_SCHMITT_TRIGGER_SELECT));

	reg_gpio_oen = sfp->base + info->regs->output_en + offset;

	guard(raw_spinlock_irqsave)(&sfp->lock);
	doen = readl_relaxed(reg_gpio_oen) | JHB100_GPIO_MASK(gpio);
	writel_relaxed(doen, reg_gpio_oen);

	return 0;
}

static int jhb100_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio)
{
	struct jhb100_pinctrl *sfp = gpiochip_get_data(gc);
	const struct jhb100_pinctrl_domain_info *info = sfp->info;
	const struct config_reg_layout_desc *crl_desc;
	unsigned int offset = JHB100_GPIO_REG_OFFSET(gpio);
	void __iomem *reg_gpio_oen;
	u32 doen = 0;

	crl_desc = get_crl_desc_by_pin(sfp, gpio);
	if (!crl_desc) {
		dev_err(sfp->dev, "pin %d can't not found reg layout descriptor\n",
			gpio);
		return -EINVAL;
	}

	jhb100_padcfg_rmw(sfp, gpio,
			  RL_DESC_GENMASK(crl_desc, PAD_CFG_INPUT_ENABLE) |
			  RL_DESC_GENMASK(crl_desc, PAD_CFG_SCHMITT_TRIGGER_SELECT) |
			  RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_DOWN) |
			  RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_UP),
			  0);

	reg_gpio_oen = sfp->base + info->regs->output_en + offset;

	guard(raw_spinlock_irqsave)(&sfp->lock);
	doen = readl_relaxed(reg_gpio_oen) & ~JHB100_GPIO_MASK(gpio);
	writel_relaxed(doen, reg_gpio_oen);

	return 0;
}

static int jhb100_gpio_set_direction(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned int pin,
				     bool input)
{
	struct jhb100_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);

	if (input)
		return jhb100_gpio_direction_input(&sfp->gc, pin);

	return jhb100_gpio_direction_output(&sfp->gc, pin);
}

static const struct pinmux_ops jhb100_pinmux_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= jhb100_set_mux,
	.gpio_request_enable	= jhb100_gpio_request_enable,
	.gpio_set_direction	= jhb100_gpio_set_direction,
};

static const u8 jhb100_drive_strength_ma[4] = { 2, 4, 8, 12 };

static const u8 jhb100_drive_strength_ma_3bit[8] = { 2, 5, 8, 10, 14, 16, 18, 20 };

static u32 jhb100_padcfg_ds_to_mA(u32 padcfg)
{
	return jhb100_drive_strength_ma[padcfg];
}

static u32 jhb100_padcfg_ds_to_mA_3bit(u32 padcfg)
{
	return jhb100_drive_strength_ma_3bit[padcfg];
}

static u32 jhb100_padcfg_ds_to_uA(u32 padcfg)
{
	return (jhb100_drive_strength_ma[padcfg] * 1000);
}

static u32 jhb100_padcfg_ds_to_uA_3bit(u32 padcfg)
{
	return (jhb100_drive_strength_ma_3bit[padcfg] * 1000);
}

static u32 jhb100_padcfg_ds_from_mA(u32 v)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(jhb100_drive_strength_ma); i++) {
		if (v <= jhb100_drive_strength_ma[i])
			break;
	}
	return i;
}

static u32 jhb100_padcfg_ds_from_mA_3bit(u32 v)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(jhb100_drive_strength_ma_3bit); i++) {
		if (v <= jhb100_drive_strength_ma_3bit[i])
			break;
	}
	return i;
}

static u32 jhb100_padcfg_ds_from_uA(u32 v)
{
	/* Convert from uA to mA */
	v /= 1000;

	return jhb100_padcfg_ds_from_mA(v);
}

static u32 jhb100_padcfg_ds_from_uA_3bit(u32 v)
{
	/* Convert from uA to mA */
	v /= 1000;

	return jhb100_padcfg_ds_from_mA_3bit(v);
}

static u32 jhb100_padcfg_vsel_to_reg(u32 val)
{
	switch (val) {
	case 3300:
	case 1800:
		return 0;
	case 2500:
		return 1;
	default:
		return 0;
	}
}

static int jhb100_pincfg_reg_to_vref(u32 val)
{
	const int vref_table[JHB100_PINVREF_NUM] = { 3300, 2500, 1800 };

	return (val < JHB100_PINVREF_NUM) ? vref_table[val] : -ENOTSUPP;
}

static u32 jhb100_pincfg_vref_to_reg(u32 val)
{
	switch (val) {
	case 3300:
		return JHB100_PINVREF_3_3V;
	case 2500:
		return JHB100_PINVREF_2_5V;
	case 1800:
		return JHB100_PINVREF_1_8V;
	default:
		return JHB100_PINVREF_3_3V;
	}
}

static int jhb100_pincfg_pin_vref_get(struct jhb100_pinctrl *sfp, unsigned int pin)
{
	const struct pinvref_reg *vref = &sfp->info->regs->vref;
	u32 grp = 0, i;
	int val;

	while (grp < vref->num_pv) {
		for (i = 0; i < vref->pv_desc[grp].num_pins; i++) {
			if (pin != vref->pv_desc[grp].pin_grp[i])
				continue;

			val = readl(sfp->base + vref->reg + grp * 4);

			return jhb100_pincfg_reg_to_vref(val);
		}

		grp++;
	}

	return -ENOTSUPP;
}

static void jhb100_pincfg_pin_vref_set(struct jhb100_pinctrl *sfp, unsigned int pin,
				       u32 arg)
{
	const struct pinvref_reg *vref = &sfp->info->regs->vref;
	u32 grp = 0, i;

	while (grp < vref->num_pv) {
		for (i = 0; i < vref->pv_desc[grp].num_pins; i++) {
			if (pin != vref->pv_desc[grp].pin_grp[i])
				continue;

			arg = jhb100_pincfg_vref_to_reg(arg);

			if (!(vref->pv_desc[grp].range & BIT(arg)))
				return;

			guard(raw_spinlock_irqsave)(&sfp->lock);
			writel(arg, sfp->base + vref->reg + grp * 4);
			return;
		}

		grp++;
	}
}

static int jhb100_pinconf_get(struct pinctrl_dev *pctldev,
			      unsigned int pin, unsigned long *config)
{
	struct jhb100_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	int param = pinconf_to_config_param(*config);
	const struct config_reg_layout_desc *crl_desc;
	unsigned int padcfg_base, offset;
	bool enabled = false;
	u32 padcfg, arg;
	int ret;

	padcfg_base = sfp->info->regs->config;
	offset = 4 * pin;

	if (pin >= sfp->npins)
		return -EINVAL;

	padcfg = readl_relaxed(sfp->base + padcfg_base + offset);

	crl_desc = get_crl_desc_by_pin(sfp, pin);
	if (!crl_desc) {
		dev_err(sfp->dev, "pin %d can't not found reg layout descriptor\n", pin);
		return -EINVAL;
	}

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		arg = 0;

		if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_PULL_DOWN) ||
		    !RL_DESC_SUPPORTED(crl_desc, PAD_CFG_PULL_UP))
			return -ENOTSUPP;

		enabled = !(padcfg & (RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_DOWN) |
				      RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_UP)));
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		arg = 1;

		if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_PULL_DOWN))
			return -ENOTSUPP;

		enabled = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_DOWN))
			  >> RL_DESC_SHIFT(crl_desc, PAD_CFG_PULL_DOWN);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_PULL_UP) &&
		    !RL_DESC_SUPPORTED(crl_desc, PAD_CFG_OPEN_DRAIN_PULL_UP_SEL))
			return -ENOTSUPP;

		if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_PULL_UP)) {
			arg = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_UP)) >>
			      RL_DESC_SHIFT(crl_desc, PAD_CFG_PULL_UP);

			enabled = arg ? true : false;
		}

		if (!enabled && RL_DESC_SUPPORTED(crl_desc, PAD_CFG_OPEN_DRAIN_PULL_UP_SEL)) {
			enabled = true;

			arg = (padcfg &
			       RL_DESC_GENMASK(crl_desc, PAD_CFG_OPEN_DRAIN_PULL_UP_SEL)) >>
			       RL_DESC_SHIFT(crl_desc, PAD_CFG_OPEN_DRAIN_PULL_UP_SEL);

			if (arg == JHB100_I2C_OPEN_DRAIN_PU_600_OHMS)
				arg = 600;
			else if (arg == JHB100_I2C_OPEN_DRAIN_PU_900_OHMS)
				arg = 900;
			else if (arg == JHB100_I2C_OPEN_DRAIN_PU_1200_OHMS)
				arg = 1200;
			else if (arg == JHB100_I2C_OPEN_DRAIN_PU_2000_OHMS)
				arg = 2000;
			else
				return -ENOTSUPP;
		}

		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT)) {
			enabled = true;

			arg = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT)) >>
			      RL_DESC_SHIFT(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT);
			arg = jhb100_padcfg_ds_to_mA(arg);
		} else if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT)) {
			enabled = true;

			arg = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT)) >>
			      RL_DESC_SHIFT(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT);
			arg = jhb100_padcfg_ds_to_mA_3bit(arg);
		} else {
			return -ENOTSUPP;
		}
		break;
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
		if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT)) {
			enabled = true;

			arg = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT)) >>
			      RL_DESC_SHIFT(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT);
			arg = jhb100_padcfg_ds_to_uA(arg);
		} else if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT)) {
			enabled = true;

			arg = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT)) >>
			      RL_DESC_SHIFT(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT);
			arg = jhb100_padcfg_ds_to_uA_3bit(arg);
		} else {
			return -ENOTSUPP;
		}
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_INPUT_ENABLE))
			return -ENOTSUPP;

		enabled = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_INPUT_ENABLE))
			   >> RL_DESC_SHIFT(crl_desc, PAD_CFG_INPUT_ENABLE);
		arg = enabled;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_SCHMITT_TRIGGER_SELECT))
			return -ENOTSUPP;

		enabled = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_SCHMITT_TRIGGER_SELECT))
			   >> RL_DESC_SHIFT(crl_desc, PAD_CFG_SCHMITT_TRIGGER_SELECT);
		arg = enabled;
		break;
	case PIN_CONFIG_SLEW_RATE:
		if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_SLEW_RATE)) {
			enabled = true;

			arg = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_SLEW_RATE)) >>
			      RL_DESC_SHIFT(crl_desc, PAD_CFG_SLEW_RATE);
		} else if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_MODE_SELECT)) {
			enabled = true;

			arg = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_MODE_SELECT)) >>
			      RL_DESC_SHIFT(crl_desc, PAD_CFG_MODE_SELECT);

			if (arg == JHB100_LEGACY_FAST_MODE_PLUS)
				arg = 1;
			else if (arg == JHB100_LEGACY_FAST_MODE)
				arg = 0;
			else
				return -ENOTSUPP;
		} else {
			return -ENOTSUPP;
		}
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_MODE_SELECT))
			return -ENOTSUPP;

		arg = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_MODE_SELECT)) >>
		      RL_DESC_SHIFT(crl_desc, PAD_CFG_MODE_SELECT);

		if (arg == JHB100_PUSH_PULL)
			enabled = true;

		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_MODE_SELECT))
			return -ENOTSUPP;

		arg = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_MODE_SELECT)) >>
		      RL_DESC_SHIFT(crl_desc, PAD_CFG_MODE_SELECT);

		if (arg == JHB100_OPEN_DRAIN)
			enabled = true;

		break;
	case PIN_CONFIG_INPUT_DEBOUNCE_NS:
		if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_DEBOUNCE_WIDTH))
			return -ENOTSUPP;

		enabled = true;

		arg = (padcfg & RL_DESC_GENMASK(crl_desc, PAD_CFG_DEBOUNCE_WIDTH)) >>
		      RL_DESC_SHIFT(crl_desc, PAD_CFG_DEBOUNCE_WIDTH);

		arg *= JHB100_DEBOUNCE_WIDTH_STAGE_NS;

		break;
	case PIN_CONFIG_POWER_SOURCE:
		if (!sfp->info->regs->vref.num_pv)
			return -ENOTSUPP;

		ret = jhb100_pincfg_pin_vref_get(sfp, pin);
		if (ret < 0)
			return ret;

		arg = ret;
		enabled = true;

		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return enabled ? 0 : -EINVAL;
}

static int jhb100_pinconf_set(struct pinctrl_dev *pctldev,
			      unsigned int pin, unsigned long *configs,
			      unsigned int num_configs)
{
	struct jhb100_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	const struct config_reg_layout_desc *crl_desc;
	bool vref_set = false;
	u32 value = 0;
	u32 mask = 0;
	u32 vref = 0;
	int i;

	crl_desc = get_crl_desc_by_pin(sfp, pin);
	if (!crl_desc) {
		dev_err(sfp->dev, "pin %d can't not found reg layout descriptor\n", pin);
		return -EINVAL;
	}

	for (i = 0; i < num_configs; i++) {
		int param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_PULL_DOWN) ||
			    !RL_DESC_SUPPORTED(crl_desc, PAD_CFG_PULL_UP))
				return -ENOTSUPP;

			mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_DOWN) |
				RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_UP);
			value &= ~(RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_DOWN) |
				   RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_UP));
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (!arg || !RL_DESC_SUPPORTED(crl_desc, PAD_CFG_PULL_DOWN) ||
			    !RL_DESC_SUPPORTED(crl_desc, PAD_CFG_PULL_UP))
				return -ENOTSUPP;

			mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_DOWN) |
				RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_UP);
			value &= ~(RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_DOWN) |
				   RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_UP));
			value |= RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_DOWN);
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			if ((!arg || arg == 1) && RL_DESC_SUPPORTED(crl_desc, PAD_CFG_PULL_UP)) {
				mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_DOWN) |
					RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_UP);
				value &= ~(RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_DOWN) |
					RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_UP));
				value |= RL_DESC_GENMASK(crl_desc, PAD_CFG_PULL_UP);
			} else if (arg &&
				   RL_DESC_SUPPORTED(crl_desc, PAD_CFG_OPEN_DRAIN_PULL_UP_SEL)) {
				mask |= RL_DESC_GENMASK(crl_desc,
							PAD_CFG_OPEN_DRAIN_PULL_UP_SEL);
				value &= ~RL_DESC_GENMASK(crl_desc,
							  PAD_CFG_OPEN_DRAIN_PULL_UP_SEL);
				switch (arg) {
				case 600:
					value |= JHB100_I2C_OPEN_DRAIN_PU_600_OHMS <<
						 RL_DESC_SHIFT(crl_desc,
							       PAD_CFG_OPEN_DRAIN_PULL_UP_SEL);
					break;
				case 900:
					value |= JHB100_I2C_OPEN_DRAIN_PU_900_OHMS <<
						 RL_DESC_SHIFT(crl_desc,
							       PAD_CFG_OPEN_DRAIN_PULL_UP_SEL);
					break;
				case 1200:
					value |= JHB100_I2C_OPEN_DRAIN_PU_1200_OHMS <<
						 RL_DESC_SHIFT(crl_desc,
							       PAD_CFG_OPEN_DRAIN_PULL_UP_SEL);
					break;
				case 2000:
					value |= JHB100_I2C_OPEN_DRAIN_PU_2000_OHMS <<
						 RL_DESC_SHIFT(crl_desc,
							       PAD_CFG_OPEN_DRAIN_PULL_UP_SEL);
					break;
				default:
					return -ENOTSUPP;
				}
			} else {
				return -ENOTSUPP;
			}
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT)) {
				mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT);
				value &= ~RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT);
				value |= jhb100_padcfg_ds_from_mA(arg) <<
					 RL_DESC_SHIFT(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT);
			} else if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT)) {
				mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT);
				value &= ~RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT);
				value |= jhb100_padcfg_ds_from_mA_3bit(arg) <<
					 RL_DESC_SHIFT(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT);
			} else {
				return -ENOTSUPP;
			}
			break;
		case PIN_CONFIG_DRIVE_STRENGTH_UA:
			if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT)) {
				mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT);
				value &= ~RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT);
				value |= jhb100_padcfg_ds_from_uA(arg) <<
					 RL_DESC_SHIFT(crl_desc, PAD_CFG_DRIVE_STRENGTH_2BIT);
			} else if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT)) {
				mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT);
				value &= ~RL_DESC_GENMASK(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT);
				value |= jhb100_padcfg_ds_from_uA_3bit(arg) <<
					 RL_DESC_SHIFT(crl_desc, PAD_CFG_DRIVE_STRENGTH_3BIT);
			} else {
				return -ENOTSUPP;
			}
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_INPUT_ENABLE))
				return -ENOTSUPP;

			mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_INPUT_ENABLE);
			value = arg ? (value | RL_DESC_GENMASK(crl_desc, PAD_CFG_INPUT_ENABLE))
				: value;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_SCHMITT_TRIGGER_SELECT))
				return -ENOTSUPP;

			mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_SCHMITT_TRIGGER_SELECT);
			value = arg ?
				(value | RL_DESC_GENMASK(crl_desc, PAD_CFG_SCHMITT_TRIGGER_SELECT))
				: value;
			break;
		case PIN_CONFIG_SLEW_RATE:
			if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_SLEW_RATE)) {
				mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_SLEW_RATE);
				value = arg ?
					(value | RL_DESC_GENMASK(crl_desc, PAD_CFG_SLEW_RATE)) :
					value;
			} else if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_MODE_SELECT)) {
				mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_MODE_SELECT);
				value &= ~RL_DESC_GENMASK(crl_desc, PAD_CFG_MODE_SELECT);
				value |= arg ?
					 JHB100_LEGACY_FAST_MODE_PLUS <<
					 RL_DESC_SHIFT(crl_desc, PAD_CFG_MODE_SELECT) :
					 JHB100_LEGACY_FAST_MODE <<
					 RL_DESC_SHIFT(crl_desc, PAD_CFG_MODE_SELECT);
			} else {
				return -ENOTSUPP;
			}
			break;
		case PIN_CONFIG_DRIVE_PUSH_PULL:
			if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_MODE_SELECT))
				return -ENOTSUPP;

			mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_MODE_SELECT);
			value &= ~RL_DESC_GENMASK(crl_desc, PAD_CFG_MODE_SELECT);
			value |= JHB100_PUSH_PULL <<
				 RL_DESC_SHIFT(crl_desc, PAD_CFG_MODE_SELECT);
			break;
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_MODE_SELECT))
				return -ENOTSUPP;

			mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_MODE_SELECT);
			value &= ~RL_DESC_GENMASK(crl_desc, PAD_CFG_MODE_SELECT);
			value |= JHB100_OPEN_DRAIN <<
				 RL_DESC_SHIFT(crl_desc, PAD_CFG_MODE_SELECT);
			break;
		case PIN_CONFIG_INPUT_DEBOUNCE_NS: {
			u32 debounce_stage;

			if (!RL_DESC_SUPPORTED(crl_desc, PAD_CFG_DEBOUNCE_WIDTH))
				return -ENOTSUPP;

			mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_DEBOUNCE_WIDTH);
			value &= ~RL_DESC_GENMASK(crl_desc, PAD_CFG_DEBOUNCE_WIDTH);

			debounce_stage = arg ? arg / JHB100_DEBOUNCE_WIDTH_STAGE_NS : 0;
			debounce_stage = umin(debounce_stage, JHB100_DEBOUNCE_WIDTH_STAGES_MAX);

			value |= (debounce_stage <<
				  RL_DESC_SHIFT(crl_desc, PAD_CFG_DEBOUNCE_WIDTH));

			break;
		};
		case PIN_CONFIG_POWER_SOURCE:
			if (!sfp->info->regs->vref.num_pv &&
			    !RL_DESC_SUPPORTED(crl_desc, PAD_CFG_VSEL))
				return -ENOTSUPP;

			if (sfp->info->regs->vref.num_pv) {
				vref_set = true;
				vref = arg;
			}

			if (RL_DESC_SUPPORTED(crl_desc, PAD_CFG_VSEL)) {
				mask |= RL_DESC_GENMASK(crl_desc, PAD_CFG_VSEL);
				value &= ~RL_DESC_GENMASK(crl_desc, PAD_CFG_VSEL);

				value |= jhb100_padcfg_vsel_to_reg(arg) <<
					 RL_DESC_SHIFT(crl_desc, PAD_CFG_VSEL);
			}
			break;
		default:
			return -ENOTSUPP;
		}
	}

	jhb100_padcfg_rmw(sfp, pin, mask, value);

	if (vref_set)
		jhb100_pincfg_pin_vref_set(sfp, pin, vref);

	return 0;
}

static int jhb100_pinconf_group_get(struct pinctrl_dev *pctldev,
				    unsigned int gsel,
				    unsigned long *config)
{
	const struct group_desc *group;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	return jhb100_pinconf_get(pctldev, group->grp.pins[0], config);
}

static int jhb100_pinconf_group_set(struct pinctrl_dev *pctldev,
				    unsigned int gsel,
				    unsigned long *configs,
				    unsigned int num_configs)
{
	const struct group_desc *group;
	int ret;
	u32 i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	for (i = 0; i < group->grp.npins; i++) {
		ret = jhb100_pinconf_set(pctldev, group->grp.pins[i], configs, num_configs);
		if (ret) {
			dev_err(pctldev->dev, "failed to set config for pin %d\n",
				group->grp.pins[i]);
			return ret;
		}
	}

	return 0;
}

static const struct pinconf_ops jhb100_pinconf_ops = {
	.pin_config_get		= jhb100_pinconf_get,
	.pin_config_set		= jhb100_pinconf_set,
	.pin_config_group_get	= jhb100_pinconf_group_get,
	.pin_config_group_set	= jhb100_pinconf_group_set,
	.is_generic		= true,
};

static int jhb100_gpio_get_direction(struct gpio_chip *gc,
				     unsigned int gpio)
{
	struct jhb100_pinctrl *sfp = gpiochip_get_data(gc);
	const struct jhb100_pinctrl_domain_info *info = sfp->info;
	unsigned int offset = JHB100_GPIO_REG_OFFSET(gpio);
	u32 doen;
	void __iomem *reg_gpio_oen;

	reg_gpio_oen = sfp->base + info->regs->output_en + offset;

	doen = !!(readl_relaxed(reg_gpio_oen) & JHB100_GPIO_MASK(gpio));

	return doen == GPOEN_ENABLE ? GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int jhb100_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct jhb100_pinctrl *sfp = gpiochip_get_data(gc);
	const struct starfive_pinctrl_regs *pinctrl_regs = sfp->info->regs;
	void __iomem *reg_gpio_status;

	reg_gpio_status = sfp->base + pinctrl_regs->gpio_status +
			  JHB100_GPIO_REG_OFFSET(gpio);

	return !!(readl_relaxed(reg_gpio_status) & JHB100_GPIO_MASK(gpio));
}

static int jhb100_gpio_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
	struct jhb100_pinctrl *sfp = gpiochip_get_data(gc);
	const struct starfive_pinctrl_regs *pinctrl_regs = sfp->info->regs;
	void __iomem *reg_gpio_out;
	u32 dout;

	reg_gpio_out = sfp->base + pinctrl_regs->output + JHB100_GPIO_REG_OFFSET(gpio);

	guard(raw_spinlock_irqsave)(&sfp->lock);

	dout = readl_relaxed(reg_gpio_out);

	if (value)
		dout |= JHB100_GPIO_MASK(gpio);
	else
		dout &= ~JHB100_GPIO_MASK(gpio);

	writel_relaxed(dout, reg_gpio_out);

	return 0;
}

static int jhb100_pinctrl_gpio_direction_output(struct gpio_chip *gc,
						unsigned int gpio, int value)
{
	int ret;

	ret = jhb100_gpio_set(gc, gpio, value);
	if (ret)
		return ret;

	return pinctrl_gpio_direction_output(gc, gpio);
}

static void jhb100_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct jhb100_pinctrl *sfp = gpiochip_get_data(gc);
	const struct starfive_pinctrl_regs *pinctrl_regs = sfp->info->regs;
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	unsigned int offset = JHB100_GPIO_REG_OFFSET(gpio);
	void __iomem *ic;
	u32 value;
	u32 mask;

	ic = sfp->base + pinctrl_regs->irq_clr + offset;
	mask = JHB100_GPIO_MASK(gpio);

	guard(raw_spinlock_irqsave)(&sfp->lock);

	value = readl_relaxed(ic) & ~mask;
	writel_relaxed(value | mask, ic);
	value = readl_relaxed(ic) & ~mask;
	writel_relaxed(value, ic);
}

static void jhb100_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct jhb100_pinctrl *sfp = gpiochip_get_data(gc);
	const struct starfive_pinctrl_regs *pinctrl_regs = sfp->info->regs;
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	unsigned int offset = JHB100_GPIO_REG_OFFSET(gpio);
	void __iomem *ien;
	u32 value;
	u32 mask;

	ien = sfp->base + pinctrl_regs->irq_en + offset;
	mask = JHB100_GPIO_MASK(gpio);

	guard(raw_spinlock_irqsave)(&sfp->lock);

	value = readl_relaxed(ien) & ~mask;
	writel_relaxed(value, ien);

	gpiochip_disable_irq(gc, d->hwirq);
}

static void jhb100_irq_mask_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct jhb100_pinctrl *sfp = gpiochip_get_data(gc);
	const struct starfive_pinctrl_regs *pinctrl_regs = sfp->info->regs;
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	unsigned int offset = JHB100_GPIO_REG_OFFSET(gpio);
	void __iomem *ien;
	void __iomem *ic;
	u32 value;
	u32 mask;

	ien = sfp->base + pinctrl_regs->irq_en + offset;
	ic = sfp->base + pinctrl_regs->irq_clr + offset;
	mask = JHB100_GPIO_MASK(gpio);

	guard(raw_spinlock_irqsave)(&sfp->lock);

	value = readl_relaxed(ien) & ~mask;
	writel_relaxed(value, ien);

	value = readl_relaxed(ic) & ~mask;
	writel_relaxed(value | mask, ic);
	value = readl_relaxed(ic) & ~mask;
	writel_relaxed(value, ic);
}

static void jhb100_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct jhb100_pinctrl *sfp = gpiochip_get_data(gc);
	const struct starfive_pinctrl_regs *pinctrl_regs = sfp->info->regs;
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	unsigned int offset = JHB100_GPIO_REG_OFFSET(gpio);
	void __iomem *ien;
	u32 value;
	u32 mask;

	ien = sfp->base + pinctrl_regs->irq_en + offset;
	mask = JHB100_GPIO_MASK(gpio);

	gpiochip_enable_irq(gc, d->hwirq);

	guard(raw_spinlock_irqsave)(&sfp->lock);

	value = readl_relaxed(ien) | mask;
	writel_relaxed(value, ien);
}

static int jhb100_irq_set_type(struct irq_data *d, unsigned int trigger)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct jhb100_pinctrl *sfp = gpiochip_get_data(gc);
	const struct starfive_pinctrl_regs *pinctrl_regs = sfp->info->regs;
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	unsigned int offset = JHB100_GPIO_REG_OFFSET(gpio);
	void __iomem *base;
	u32 irq_type, edge_both, polarity, mask;

	base = sfp->base + offset;
	mask = JHB100_GPIO_MASK(gpio);

	switch (trigger) {
	case IRQ_TYPE_EDGE_RISING:
		irq_type  = mask; /* 1: edge triggered */
		edge_both = 0;    /* 0: single edge */
		polarity  = mask; /* 1: rising edge */
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_type  = mask; /* 1: edge triggered */
		edge_both = 0;    /* 0: single edge */
		polarity  = 0;    /* 0: falling edge */
		break;
	case IRQ_TYPE_EDGE_BOTH:
		irq_type  = mask; /* 1: edge triggered */
		edge_both = mask; /* 1: both edges */
		polarity  = 0;    /* 0: ignored */
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		irq_type  = 0;    /* 0: level triggered */
		edge_both = 0;    /* 0: ignored */
		polarity  = mask; /* 1: high level */
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irq_type  = 0;    /* 0: level triggered */
		edge_both = 0;    /* 0: ignored */
		polarity  = 0;    /* 0: low level */
		break;
	default:
		return -EINVAL;
	}

	if (trigger & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else
		irq_set_handler_locked(d, handle_level_irq);

	guard(raw_spinlock_irqsave)(&sfp->lock);

	irq_type |= readl_relaxed(base + pinctrl_regs->irq_trigger) & ~mask;
	writel_relaxed(irq_type, base + pinctrl_regs->irq_trigger);

	edge_both |= readl_relaxed(base + pinctrl_regs->irq_both_edge) & ~mask;
	writel_relaxed(edge_both, base + pinctrl_regs->irq_both_edge);

	if (irq_type & mask) { /* edge polarity */
		polarity |= readl_relaxed(base + pinctrl_regs->irq_edge) & ~mask;
		writel_relaxed(polarity, base + pinctrl_regs->irq_edge);
	} else { /* level polarity */
		polarity |= readl_relaxed(base + pinctrl_regs->irq_level) & ~mask;
		writel_relaxed(polarity, base + pinctrl_regs->irq_level);
	}

	return 0;
}

static int jhb100_irq_set_wake(struct irq_data *d, unsigned int enable)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct jhb100_pinctrl *sfp = gpiochip_get_data(gc);
	int ret;

	if (enable)
		ret = enable_irq_wake(sfp->irq);
	else
		ret = disable_irq_wake(sfp->irq);
	if (ret)
		dev_err(sfp->dev, "failed to %s wake-up interrupt\n",
			enable ? "enable" : "disable");

	return ret;
}

static void jhb100_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);

	seq_puts(p, gc->label);
}

static const struct irq_chip jhb100_irq_chip = {
	.irq_ack        = jhb100_irq_ack,
	.irq_mask       = jhb100_irq_mask,
	.irq_mask_ack   = jhb100_irq_mask_ack,
	.irq_unmask     = jhb100_irq_unmask,
	.irq_set_type   = jhb100_irq_set_type,
	.irq_set_wake   = jhb100_irq_set_wake,
	.irq_print_chip = jhb100_irq_print_chip,
	.flags          = IRQCHIP_SET_TYPE_MASKED |
			  IRQCHIP_IMMUTABLE |
			  IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND |
			  IRQCHIP_MASK_ON_SUSPEND,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static irqreturn_t jhb100_gpio_irq_handler(int irq, void *dev_id)
{
	struct jhb100_pinctrl *sfp = dev_id;
	struct gpio_chip *gc = &sfp->gc;
	struct gpio_irq_chip *girq = &gc->irq;
	const struct starfive_pinctrl_regs *pinctrl_regs = sfp->info->regs;
	irqreturn_t ret = IRQ_NONE;
	unsigned int bit;
	unsigned long is;

	for (unsigned int i = 0; i < sfp->num_banks; i++) {
		is = readl_relaxed(sfp->base + pinctrl_regs->irq_status +
				   JHB100_BANK_OFFSET(i));
		if (!is)
			continue;

		for_each_set_bit(bit, &is, JHB100_NR_GPIOS_PER_BANK) {
			unsigned int gpio = i * JHB100_NR_GPIOS_PER_BANK + bit;

			if (gpio >= gc->ngpio)
				break;

			generic_handle_domain_irq(girq->domain, gpio);
		}

		ret = IRQ_HANDLED;
	}

	return ret;
}

static
struct pinctrl_pin_desc *devm_create_pins_from_pld(struct device *dev,
						   const struct jhb100_pin_layout_desc *desc,
						   const char *prefix,
						   unsigned int *total_pins,
						   unsigned int *total_gpios,
						   s8 **gpio_func_sel_arr)
{
	struct pinctrl_pin_desc *pins = NULL;
	unsigned int i, j, ngpios = 0, npins = 0, pin_index = 0;
	unsigned int same_name_found = 0;
	s8 *arr;

	if (!dev || !desc || !prefix) {
		dev_err(dev, "Invalid parameters: desc=%p, prefix=%s\n",
			desc, prefix);
		return ERR_PTR(-EINVAL);
	}

	for (i = 0; desc[i].pin_start != 0xff; i++) {
		if (!desc[i].pin_cnt) {
			dev_err(dev, "Invalid pin cnt\n");
			return ERR_PTR(-EINVAL);
		}

		npins += desc[i].pin_cnt;
	}

	if (npins == 0) {
		dev_err(dev, "No pins defined\n");
		return ERR_PTR(-EINVAL);
	}

	dev_dbg(dev, "Total pins to create: %d\n", npins);

	pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return ERR_PTR(-ENOMEM);

	arr = devm_kzalloc(dev, npins, GFP_KERNEL);
	if (!arr)
		return ERR_PTR(-ENOMEM);

	for (i = 0; desc[i].pin_start != 0xff; i++) {
		same_name_found = 0;

		for (j = 0; j < i; j++) {
			if (!strcmp(desc[j].name, desc[i].name)) {
				same_name_found = 1;
				break;
			}
		}

		for (j = 0; j < desc[i].pin_cnt; j++) {
			char *name = NULL;
			int pin_num = desc[i].pin_start + j;

			pins[pin_index].number = pin_num;
			if (same_name_found) {
				name = devm_kasprintf(dev, GFP_KERNEL, "%s_%s_%d",
						      prefix, desc[i].name,
						      desc[i].pin_start + j);
			} else {
				if (desc[i].pin_cnt > 1)
					name = devm_kasprintf(dev, GFP_KERNEL, "%s_%s_%d",
							      prefix, desc[i].name, j);
				else
					name = devm_kasprintf(dev, GFP_KERNEL, "%s_%s",
							      prefix, desc[i].name);
			}

			if (!name) {
				dev_err(dev, "Failed to allocate pin name for pin %d\n",
					pin_num);
				return ERR_PTR(-ENOMEM);
			}

			if (!strcmp(desc[i].name, "gpio") || desc[i].gpio_func_sel != -1)
				ngpios++;

			pins[pin_index].name = name;
			arr[pin_index] = desc[i].gpio_func_sel;
			pin_index++;
		}
	}

	*total_pins = npins;
	*total_gpios = ngpios;
	*gpio_func_sel_arr = arr;

	return pins;
}

static void jhb100_reset_control_assert(void *data)
{
	reset_control_assert(data);
}

int jhb100_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_irq_chip *girq;
	struct gpio_chip *gc;
	const struct jhb100_pinctrl_domain_info *info;
	struct jhb100_pinctrl *sfp;
	struct pinctrl_desc *jhb100_pinctrl_desc;
	const struct starfive_pinctrl_regs *pinctrl_regs;
	struct reset_control *rst;
	struct clk *clk;
	int ret;

	info = device_get_match_data(&pdev->dev);
	if (!info)
		return -ENODEV;

	pinctrl_regs = info->regs;

	sfp = devm_kzalloc(dev, sizeof(*sfp), GFP_KERNEL);
	if (!sfp)
		return -ENOMEM;

	sfp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sfp->base))
		return PTR_ERR(sfp->base);

	clk = devm_clk_get_optional_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "could not get & enable clock\n");

	rst = devm_reset_control_array_get_optional_shared(dev);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "could not get reset control\n");

	ret = reset_control_deassert(rst);
	if (ret)
		return dev_err_probe(dev, ret, "could not deassert reset\n");

	ret = devm_add_action_or_reset(dev, jhb100_reset_control_assert, rst);
	if (ret)
		return ret;

	sfp->irq = platform_get_irq(pdev, 0);
	if (sfp->irq < 0)
		return sfp->irq;

	sfp->pins = devm_create_pins_from_pld(dev, info->pl_desc, info->name,
					      &sfp->npins, &sfp->ngpios,
					      &sfp->gpio_func_sel_arr);
	if (IS_ERR(sfp->pins))
		return PTR_ERR(sfp->pins);

	jhb100_pinctrl_desc = devm_kzalloc(&pdev->dev,
					   sizeof(*jhb100_pinctrl_desc),
					   GFP_KERNEL);
	if (!jhb100_pinctrl_desc)
		return -ENOMEM;

	jhb100_pinctrl_desc->name = dev_name(dev);
	jhb100_pinctrl_desc->pctlops = &jhb100_pinctrl_ops;
	jhb100_pinctrl_desc->pmxops = &jhb100_pinmux_ops;
	jhb100_pinctrl_desc->confops = &jhb100_pinconf_ops;
	jhb100_pinctrl_desc->owner = THIS_MODULE;
	jhb100_pinctrl_desc->pins = sfp->pins;
	jhb100_pinctrl_desc->npins = sfp->npins;

	sfp->info = info;
	sfp->dev = dev;
	platform_set_drvdata(pdev, sfp);

	raw_spin_lock_init(&sfp->lock);

	ret = devm_pinctrl_register_and_init(dev, jhb100_pinctrl_desc,
					     sfp, &sfp->pctl);
	if (ret)
		return dev_err_probe(dev, ret,
				     "could not register pinctrl driver\n");

	ret = pinctrl_enable(sfp->pctl);
	if (ret)
		return ret;

	sfp->num_banks = DIV_ROUND_UP(sfp->ngpios, JHB100_NR_GPIOS_PER_BANK);
	if (sfp->num_banks > JHB100_MAX_BANKS)
		return dev_err_probe(dev, -EINVAL,
				     "%u GPIOs need %u banks, max %d\n",
				     sfp->ngpios, sfp->num_banks, JHB100_MAX_BANKS);

	for (unsigned int i = 0; i < sfp->num_banks; i++) {
		/* mask all GPIO interrupts */
		writel_relaxed(0U, sfp->base + pinctrl_regs->irq_en + JHB100_BANK_OFFSET(i));
		/* clear all interrupts */
		writel_relaxed(~0U, sfp->base + pinctrl_regs->irq_clr + JHB100_BANK_OFFSET(i));
		writel_relaxed(0U, sfp->base + pinctrl_regs->irq_clr + JHB100_BANK_OFFSET(i));
	}

	gc = &sfp->gc;
	gc->label = dev_name(dev);
	gc->parent = dev;
	gc->owner = THIS_MODULE;
	gc->request = gpiochip_generic_request;
	gc->free = gpiochip_generic_free;
	gc->get_direction = jhb100_gpio_get_direction;
	gc->direction_input = pinctrl_gpio_direction_input;
	gc->direction_output = jhb100_pinctrl_gpio_direction_output;
	gc->get = jhb100_gpio_get;
	gc->set = jhb100_gpio_set;
	gc->set_config = gpiochip_generic_config;
	gc->base = -1;
	gc->ngpio = sfp->ngpios;

	girq = &gc->irq;
	girq->handler = handle_simple_irq;

	gpio_irq_chip_set_chip(girq, &jhb100_irq_chip);

	ret = devm_gpiochip_add_data(dev, gc, sfp);
	if (ret)
		return dev_err_probe(dev, ret, "could not register gpiochip\n");

	ret = devm_request_irq(dev, sfp->irq, jhb100_gpio_irq_handler, 0,
			       gc->label, sfp);
	if (ret < 0)
		return ret;

	dev_info(dev, "StarFive JHB100 GPIO chip registered %d GPIOs\n",
		 sfp->ngpios);

	return 0;
}
EXPORT_SYMBOL_GPL(jhb100_pinctrl_probe);

MODULE_DESCRIPTION("Pinctrl driver for the StarFive JHB100 SoC");
MODULE_AUTHOR("Alex Soo <yuklin.soo@starfivetech.com>");
MODULE_LICENSE("GPL");
