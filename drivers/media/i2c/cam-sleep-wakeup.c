// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2023 Rockchip Electronics Co., Ltd

#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/rk-preisp.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include "cam-sleep-wakeup.h"

struct cam_sw_info *cam_sw_init(void)
{
	struct cam_sw_info *cam_info =
		kzalloc(sizeof(*cam_info), GFP_KERNEL);
	if (!cam_info)
		pr_err("kmalloc for cam fail\n");
	return cam_info;
}
EXPORT_SYMBOL_GPL(cam_sw_init);

int cam_sw_deinit(struct cam_sw_info *info)
{
	if (IS_ERR_OR_NULL(info)) {
		pr_err("%s param is null\n", __func__);
		return -EINVAL;
	}

	kfree(info);
	return 0;
}
EXPORT_SYMBOL_GPL(cam_sw_deinit);

int cam_sw_clk_init(struct cam_sw_info *info, struct clk *xvclk, u32 clk_freq)
{
	if (IS_ERR_OR_NULL(info)) {
		pr_err("%s param is null\n", __func__);
		return -EINVAL;
	}

	info->clk.xvclk = xvclk;
	info->clk.clk_freq = clk_freq;
	return 0;
}
EXPORT_SYMBOL_GPL(cam_sw_clk_init);

int cam_sw_reset_pin_init(struct cam_sw_info *info,
			  struct gpio_desc *reset_gpio, bool reset_active_state)
{
	if (IS_ERR_OR_NULL(info)) {
		pr_err("%s param is null\n", __func__);
		return -EINVAL;
	}

	info->pin.reset_gpio = reset_gpio;
	info->pin.reset_active_state = reset_active_state;
	return 0;
}
EXPORT_SYMBOL_GPL(cam_sw_reset_pin_init);

int cam_sw_pwdn_pin_init(struct cam_sw_info *info, struct gpio_desc *pwdn_gpio,
			 bool pwdn_active_state)
{
	if (IS_ERR_OR_NULL(info)) {
		pr_err("%s param is null\n", __func__);
		return -EINVAL;
	}

	info->pin.pwdn_gpio = pwdn_gpio;
	info->pin.pwdn_active_state = pwdn_active_state;
	return 0;
}
EXPORT_SYMBOL_GPL(cam_sw_pwdn_pin_init);

int cam_sw_pinctrl_init(struct cam_sw_info *info, struct pinctrl *pinctrl,
			struct pinctrl_state *pins_default,
			struct pinctrl_state *pins_sleep)
{
	if (IS_ERR_OR_NULL(info)) {
		pr_err("%s param is null\n", __func__);
		return -EINVAL;
	}

	info->pin.pinctrl = pinctrl;
	info->pin.pins_default = pins_default;
	info->pin.pins_sleep = pins_sleep;
	return 0;
}
EXPORT_SYMBOL_GPL(cam_sw_pinctrl_init);

int cam_sw_regulator_bulk_init(struct cam_sw_info *info, int supplies_num,
			       struct regulator_bulk_data *supplies)
{
	if (IS_ERR_OR_NULL(info)) {
		pr_err("%s param is null\n", __func__);
		return -EINVAL;
	}
	info->pin.supplies_num = supplies_num;
	info->pin.supplies = supplies;
	return 0;
}
EXPORT_SYMBOL_GPL(cam_sw_regulator_bulk_init);

int cam_sw_write_array_cb_init(struct cam_sw_info *info,
			       struct i2c_client *client, void *array_regs,
			       sensor_write_array write_array)
{
	if (IS_ERR_OR_NULL(info) || IS_ERR_OR_NULL(client)) {
		pr_err("%s param is null\n", __func__);
		return -EINVAL;
	}

	info->client = client;
	info->array_regs = array_regs;
	info->write_array = write_array;

	return 0;
}
EXPORT_SYMBOL_GPL(cam_sw_write_array_cb_init);

int cam_sw_write_array(struct cam_sw_info *info)
{
	if (IS_ERR_OR_NULL(info) || !info->write_array) {
		pr_err("%s param is null\n", __func__);
		return -EINVAL;
	}

	info->write_array(info->client, info->array_regs);

	return 0;
}
EXPORT_SYMBOL_GPL(cam_sw_write_array);

int cam_sw_prepare_wakeup(struct cam_sw_info *info, struct device *dev)
{
	int ret = 0;

	if (!IS_ERR_OR_NULL(info->pin.pins_default)) {
		ret = pinctrl_select_state(info->pin.pinctrl,
					   info->pin.pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	if (clk_set_rate(info->clk.xvclk, info->clk.clk_freq) < 0)
		dev_warn(dev, "clk_set_rate fail!");

	if (clk_get_rate(info->clk.xvclk) != info->clk.clk_freq)
		dev_warn(dev, "clk_get_rate fail!");

	if (clk_prepare_enable(info->clk.xvclk) < 0) {
		dev_err(dev, "clk_prepare_enable fail!");
		return -EINVAL;
	}

	if (!IS_ERR(info->pin.reset_gpio))
		gpiod_set_value_cansleep(info->pin.reset_gpio, info->pin.reset_active_state);

	if (!IS_ERR(info->pin.supplies) && info->pin.supplies_num) {
		ret = regulator_bulk_enable(info->pin.supplies_num, info->pin.supplies);
		if (ret != 0)
			dev_err(dev, "regulator_bulk_enable fail");
	}

	if (!IS_ERR(info->pin.reset_gpio))
		gpiod_set_value_cansleep(info->pin.reset_gpio, !info->pin.reset_active_state);

	if (!IS_ERR(info->pin.pwdn_gpio))
		gpiod_set_value_cansleep(info->pin.pwdn_gpio, info->pin.pwdn_active_state);

	return 0;
}
EXPORT_SYMBOL_GPL(cam_sw_prepare_wakeup);

int cam_sw_prepare_sleep(struct cam_sw_info *info)
{
	if (!IS_ERR(info->clk.xvclk))
		clk_disable_unprepare(info->clk.xvclk);

	if (!IS_ERR(info->pin.pwdn_gpio))
		gpiod_set_value_cansleep(info->pin.pwdn_gpio, !info->pin.pwdn_active_state);

	if (!IS_ERR(info->pin.reset_gpio))
		gpiod_set_value_cansleep(info->pin.reset_gpio, !info->pin.reset_active_state);

	if (!IS_ERR_OR_NULL(info->pin.pins_sleep))
		pinctrl_select_state(info->pin.pinctrl, info->pin.pins_sleep);

	if (!IS_ERR(info->pin.supplies) && info->pin.supplies_num)
		regulator_bulk_disable(info->pin.supplies_num, info->pin.supplies);

	return 0;
}
EXPORT_SYMBOL_GPL(cam_sw_prepare_sleep);

MODULE_LICENSE("GPL");
