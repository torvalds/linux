// SPDX-License-Identifier: GPL-2.0-only
/*
 * LP5812 LED driver
 *
 * Copyright (C) 2025 Texas Instruments
 *
 * Author: Jared Zhou <jared-zhou@ti.com>
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include "leds-lp5812.h"

static const struct lp5812_mode_mapping chip_mode_map[] = {
	{"direct_mode", 0, 0, 0, 0, 0, 0},
	{"tcm:1:0", 1, 0, 0, 0, 0, 0},
	{"tcm:1:1", 1, 1, 0, 0, 0, 0},
	{"tcm:1:2", 1, 2, 0, 0, 0, 0},
	{"tcm:1:3", 1, 3, 0, 0, 0, 0},
	{"tcm:2:0:1", 2, 0, 1, 0, 0, 0},
	{"tcm:2:0:2", 2, 0, 2, 0, 0, 0},
	{"tcm:2:0:3", 2, 0, 3, 0, 0, 0},
	{"tcm:2:1:2", 2, 1, 2, 0, 0, 0},
	{"tcm:2:1:3", 2, 1, 3, 0, 0, 0},
	{"tcm:2:2:3", 2, 2, 3, 0, 0, 0},
	{"tcm:3:0:1:2", 3, 0, 1, 2, 0, 0},
	{"tcm:3:0:1:3", 3, 0, 1, 3, 0, 0},
	{"tcm:3:0:2:3", 3, 0, 2, 3, 0, 0},
	{"tcm:4:0:1:2:3", 4, 0, 1, 2, 3, 0},
	{"mix:1:0:1", 5, 1, 0, 0, 0, 0},
	{"mix:1:0:2", 5, 2, 0, 0, 0, 0},
	{"mix:1:0:3", 5, 3, 0, 0, 0, 0},
	{"mix:1:1:0", 5, 0, 0, 0, 0, 1},
	{"mix:1:1:2", 5, 2, 0, 0, 0, 1},
	{"mix:1:1:3", 5, 3, 0, 0, 0, 1},
	{"mix:1:2:0", 5, 0, 0, 0, 0, 2},
	{"mix:1:2:1", 5, 1, 0, 0, 0, 2},
	{"mix:1:2:3", 5, 3, 0, 0, 0, 2},
	{"mix:1:3:0", 5, 0, 0, 0, 0, 3},
	{"mix:1:3:1", 5, 1, 0, 0, 0, 3},
	{"mix:1:3:2", 5, 2, 0, 0, 0, 3},
	{"mix:2:0:1:2", 6, 1, 2, 0, 0, 0},
	{"mix:2:0:1:3", 6, 1, 3, 0, 0, 0},
	{"mix:2:0:2:3", 6, 2, 3, 0, 0, 0},
	{"mix:2:1:0:2", 6, 0, 2, 0, 0, 1},
	{"mix:2:1:0:3", 6, 0, 3, 0, 0, 1},
	{"mix:2:1:2:3", 6, 2, 3, 0, 0, 1},
	{"mix:2:2:0:1", 6, 0, 1, 0, 0, 2},
	{"mix:2:2:0:3", 6, 0, 3, 0, 0, 2},
	{"mix:2:2:1:3", 6, 1, 3, 0, 0, 2},
	{"mix:2:3:0:1", 6, 0, 1, 0, 0, 3},
	{"mix:2:3:0:2", 6, 0, 2, 0, 0, 3},
	{"mix:2:3:1:2", 6, 1, 2, 0, 0, 3},
	{"mix:3:0:1:2:3", 7, 1, 2, 3, 0, 0},
	{"mix:3:1:0:2:3", 7, 0, 2, 3, 0, 1},
	{"mix:3:2:0:1:3", 7, 0, 1, 3, 0, 2},
	{"mix:3:3:0:1:2", 7, 0, 1, 2, 0, 3}
};

static int lp5812_write(struct lp5812_chip *chip, u16 reg, u8 val)
{
	struct device *dev = &chip->client->dev;
	struct i2c_msg msg;
	u8 buf[LP5812_DATA_LENGTH];
	u8 reg_addr_bit8_9;
	int ret;

	/* Extract register address bits 9 and 8 for Address Byte 1 */
	reg_addr_bit8_9 = (reg >> LP5812_REG_ADDR_HIGH_SHIFT) & LP5812_REG_ADDR_BIT_8_9_MASK;

	/* Prepare payload: Address Byte 2 (bits [7:0]) and value to write */
	buf[LP5812_DATA_BYTE_0_IDX] = (u8)(reg & LP5812_REG_ADDR_LOW_MASK);
	buf[LP5812_DATA_BYTE_1_IDX] = val;

	/* Construct I2C message for a write operation */
	msg.addr = (chip->client->addr << LP5812_CHIP_ADDR_SHIFT) | reg_addr_bit8_9;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	ret = i2c_transfer(chip->client->adapter, &msg, 1);
	if (ret == 1)
		return 0;

	dev_err(dev, "I2C write error, ret=%d\n", ret);
	return ret < 0 ? ret : -EIO;
}

static int lp5812_read(struct lp5812_chip *chip, u16 reg, u8 *val)
{
	struct device *dev = &chip->client->dev;
	struct i2c_msg msgs[LP5812_READ_MSG_LENGTH];
	u8 ret_val;
	u8 reg_addr_bit8_9;
	u8 converted_reg;
	int ret;

	/* Extract register address bits 9 and 8 for Address Byte 1 */
	reg_addr_bit8_9 = (reg >> LP5812_REG_ADDR_HIGH_SHIFT) & LP5812_REG_ADDR_BIT_8_9_MASK;

	/* Lower 8 bits go in Address Byte 2 */
	converted_reg = (u8)(reg & LP5812_REG_ADDR_LOW_MASK);

	/* Prepare I2C write message to set register address */
	msgs[LP5812_MSG_0_IDX].addr =
		(chip->client->addr << LP5812_CHIP_ADDR_SHIFT) | reg_addr_bit8_9;
	msgs[LP5812_MSG_0_IDX].flags = 0;
	msgs[LP5812_MSG_0_IDX].len = 1;
	msgs[LP5812_MSG_0_IDX].buf = &converted_reg;

	/* Prepare I2C read message to retrieve register value */
	msgs[LP5812_MSG_1_IDX].addr =
		(chip->client->addr << LP5812_CHIP_ADDR_SHIFT) | reg_addr_bit8_9;
	msgs[LP5812_MSG_1_IDX].flags = I2C_M_RD;
	msgs[LP5812_MSG_1_IDX].len = 1;
	msgs[LP5812_MSG_1_IDX].buf = &ret_val;

	ret = i2c_transfer(chip->client->adapter, msgs, LP5812_READ_MSG_LENGTH);
	if (ret == LP5812_READ_MSG_LENGTH) {
		*val = ret_val;
		return 0;
	}

	dev_err(dev, "I2C read error, ret=%d\n", ret);
	*val = 0;
	return ret < 0 ? ret : -EIO;
}

static int lp5812_read_tsd_config_status(struct lp5812_chip *chip, u8 *reg_val)
{
	return lp5812_read(chip, LP5812_TSD_CONFIG_STATUS, reg_val);
}

static int lp5812_update_regs_config(struct lp5812_chip *chip)
{
	u8 reg_val;
	int ret;

	ret = lp5812_write(chip, LP5812_CMD_UPDATE, LP5812_UPDATE_CMD_VAL);
	if (ret)
		return ret;

	ret = lp5812_read_tsd_config_status(chip, &reg_val);
	if (ret)
		return ret;

	return reg_val & LP5812_CFG_ERR_STATUS_MASK;
}

static ssize_t parse_drive_mode(struct lp5812_chip *chip, const char *str)
{
	int i;

	chip->drive_mode.bits.mix_sel_led_0 = false;
	chip->drive_mode.bits.mix_sel_led_1 = false;
	chip->drive_mode.bits.mix_sel_led_2 = false;
	chip->drive_mode.bits.mix_sel_led_3 = false;

	if (sysfs_streq(str, LP5812_MODE_DIRECT_NAME)) {
		chip->drive_mode.bits.led_mode = LP5812_MODE_DIRECT_VALUE;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(chip_mode_map); i++) {
		if (!sysfs_streq(str, chip_mode_map[i].mode_name))
			continue;

		chip->drive_mode.bits.led_mode = chip_mode_map[i].mode;
		chip->scan_order.bits.order0 = chip_mode_map[i].scan_order_0;
		chip->scan_order.bits.order1 = chip_mode_map[i].scan_order_1;
		chip->scan_order.bits.order2 = chip_mode_map[i].scan_order_2;
		chip->scan_order.bits.order3 = chip_mode_map[i].scan_order_3;

		switch (chip_mode_map[i].selection_led) {
		case LP5812_MODE_MIX_SELECT_LED_0:
			chip->drive_mode.bits.mix_sel_led_0 = true;
			break;
		case LP5812_MODE_MIX_SELECT_LED_1:
			chip->drive_mode.bits.mix_sel_led_1 = true;
			break;
		case LP5812_MODE_MIX_SELECT_LED_2:
			chip->drive_mode.bits.mix_sel_led_2 = true;
			break;
		case LP5812_MODE_MIX_SELECT_LED_3:
			chip->drive_mode.bits.mix_sel_led_3 = true;
			break;
		default:
			return -EINVAL;
		}

		return 0;
	}

	return -EINVAL;
}

static int lp5812_set_drive_mode_scan_order(struct lp5812_chip *chip)
{
	u8 val;
	int ret;

	val = chip->drive_mode.val;
	ret = lp5812_write(chip, LP5812_DEV_CONFIG1, val);
	if (ret)
		return ret;

	val = chip->scan_order.val;
	ret = lp5812_write(chip, LP5812_DEV_CONFIG2, val);

	return ret;
}

static int lp5812_set_led_mode(struct lp5812_chip *chip, int led_number,
			       enum control_mode mode)
{
	u8 reg_val;
	u16 reg;
	int ret;

	/*
	 * Select device configuration register.
	 * Reg3 for LED_0–LED_3, LED_A0–A2, LED_B0
	 * Reg4 for LED_B1–B2, LED_C0–C2, LED_D0–D2
	 */
	if (led_number < LP5812_NUMBER_LED_IN_REG)
		reg = LP5812_DEV_CONFIG3;
	else
		reg = LP5812_DEV_CONFIG4;

	ret = lp5812_read(chip, reg, &reg_val);
	if (ret)
		return ret;

	if (mode == LP5812_MODE_MANUAL)
		reg_val &= ~(LP5812_ENABLE << (led_number % LP5812_NUMBER_LED_IN_REG));
	else
		reg_val |= (LP5812_ENABLE << (led_number % LP5812_NUMBER_LED_IN_REG));

	ret = lp5812_write(chip, reg, reg_val);
	if (ret)
		return ret;

	ret = lp5812_update_regs_config(chip);

	return ret;
}

static int lp5812_manual_dc_pwm_control(struct lp5812_chip *chip, int led_number,
					u8 val, enum dimming_type dimming_type)
{
	u16 led_base_reg;
	int ret;

	if (dimming_type == LP5812_DIMMING_ANALOG)
		led_base_reg = LP5812_MANUAL_DC_BASE;
	else
		led_base_reg = LP5812_MANUAL_PWM_BASE;

	ret = lp5812_write(chip, led_base_reg + led_number, val);

	return ret;
}

static int lp5812_multicolor_brightness(struct lp5812_led *led)
{
	struct lp5812_chip *chip = led->chip;
	int ret, i;

	guard(mutex)(&chip->lock);
	for (i = 0; i < led->mc_cdev.num_colors; i++) {
		ret = lp5812_manual_dc_pwm_control(chip, led->mc_cdev.subled_info[i].channel,
						   led->mc_cdev.subled_info[i].brightness,
						   LP5812_DIMMING_PWM);
		if (ret)
			return ret;
	}

	return 0;
}

static int lp5812_led_brightness(struct lp5812_led *led)
{
	struct lp5812_chip *chip = led->chip;
	struct lp5812_led_config *led_cfg;
	int ret;

	led_cfg = &chip->led_config[led->chan_nr];

	guard(mutex)(&chip->lock);
	ret = lp5812_manual_dc_pwm_control(chip, led_cfg->led_id[0],
					   led->brightness, LP5812_DIMMING_PWM);

	return ret;
}

static int lp5812_set_brightness(struct led_classdev *cdev,
				 enum led_brightness brightness)
{
	struct lp5812_led *led = container_of(cdev, struct lp5812_led, cdev);

	led->brightness = (u8)brightness;

	return lp5812_led_brightness(led);
}

static int lp5812_set_mc_brightness(struct led_classdev *cdev,
				    enum led_brightness brightness)
{
	struct led_classdev_mc *mc_dev = lcdev_to_mccdev(cdev);
	struct lp5812_led *led = container_of(mc_dev, struct lp5812_led, mc_cdev);

	led_mc_calc_color_components(&led->mc_cdev, brightness);

	return lp5812_multicolor_brightness(led);
}

static int lp5812_init_led(struct lp5812_led *led, struct lp5812_chip *chip, int chan)
{
	struct device *dev = &chip->client->dev;
	struct mc_subled *mc_led_info;
	struct led_classdev *led_cdev;
	int i, ret;

	if (chip->led_config[chan].name) {
		led->cdev.name = chip->led_config[chan].name;
	} else {
		led->cdev.name = devm_kasprintf(dev, GFP_KERNEL, "%s:channel%d",
						chip->label ? : chip->client->name, chan);
		if (!led->cdev.name)
			return -ENOMEM;
	}

	if (!chip->led_config[chan].is_sc_led) {
		mc_led_info = devm_kcalloc(dev, chip->led_config[chan].num_colors,
					   sizeof(*mc_led_info), GFP_KERNEL);
		if (!mc_led_info)
			return -ENOMEM;

		led_cdev = &led->mc_cdev.led_cdev;
		led_cdev->name = led->cdev.name;
		led_cdev->brightness_set_blocking = lp5812_set_mc_brightness;
		led->mc_cdev.num_colors = chip->led_config[chan].num_colors;

		for (i = 0; i < led->mc_cdev.num_colors; i++) {
			mc_led_info[i].color_index = chip->led_config[chan].color_id[i];
			mc_led_info[i].channel = chip->led_config[chan].led_id[i];
		}

		led->mc_cdev.subled_info = mc_led_info;
	} else {
		led->cdev.brightness_set_blocking = lp5812_set_brightness;
	}

	led->chan_nr = chan;

	if (chip->led_config[chan].is_sc_led) {
		ret = devm_led_classdev_register(dev, &led->cdev);
		if (ret == 0)
			led->cdev.dev->platform_data = led;
	} else {
		ret = devm_led_classdev_multicolor_register(dev, &led->mc_cdev);
		if (ret == 0)
			led->mc_cdev.led_cdev.dev->platform_data = led;
	}

	return ret;
}

static int lp5812_register_leds(struct lp5812_led *leds, struct lp5812_chip *chip)
{
	struct lp5812_led *led;
	int num_channels = chip->num_channels;
	u8 reg_val;
	u16 reg;
	int ret, i, j;

	for (i = 0; i < num_channels; i++) {
		led = &leds[i];
		ret = lp5812_init_led(led, chip, i);
		if (ret)
			goto err_init_led;

		led->chip = chip;

		for (j = 0; j < chip->led_config[i].num_colors; j++) {
			ret = lp5812_write(chip,
					   LP5812_AUTO_DC_BASE + chip->led_config[i].led_id[j],
					   chip->led_config[i].max_current[j]);
			if (ret)
				goto err_init_led;

			ret = lp5812_manual_dc_pwm_control(chip, chip->led_config[i].led_id[j],
							   chip->led_config[i].max_current[j],
							   LP5812_DIMMING_ANALOG);
			if (ret)
				goto err_init_led;

			ret = lp5812_set_led_mode(chip, chip->led_config[i].led_id[j],
						  LP5812_MODE_MANUAL);
			if (ret)
				goto err_init_led;

			reg = (chip->led_config[i].led_id[j] < LP5812_NUMBER_LED_IN_REG) ?
				LP5812_LED_EN_1 : LP5812_LED_EN_2;

			ret = lp5812_read(chip, reg, &reg_val);
			if (ret)
				goto err_init_led;

			reg_val |= (LP5812_ENABLE << (chip->led_config[i].led_id[j] %
				LP5812_NUMBER_LED_IN_REG));

			ret = lp5812_write(chip, reg, reg_val);
			if (ret)
				goto err_init_led;
		}
	}

	return 0;

err_init_led:
	return ret;
}

static int lp5812_init_device(struct lp5812_chip *chip)
{
	int ret;

	usleep_range(LP5812_WAIT_DEVICE_STABLE_MIN, LP5812_WAIT_DEVICE_STABLE_MAX);

	ret = lp5812_write(chip, LP5812_REG_ENABLE, LP5812_ENABLE);
	if (ret) {
		dev_err(&chip->client->dev, "failed to enable LP5812 device\n");
		return ret;
	}

	ret = lp5812_write(chip, LP5812_DEV_CONFIG12, LP5812_LSD_LOD_START_UP);
	if (ret) {
		dev_err(&chip->client->dev, "failed to configure device safety thresholds\n");
		return ret;
	}

	ret = parse_drive_mode(chip, chip->scan_mode);
	if (ret)
		return ret;

	ret = lp5812_set_drive_mode_scan_order(chip);
	if (ret)
		return ret;

	ret = lp5812_update_regs_config(chip);
	if (ret) {
		dev_err(&chip->client->dev, "failed to apply configuration updates\n");
		return ret;
	}

	return 0;
}

static void lp5812_deinit_device(struct lp5812_chip *chip)
{
	lp5812_write(chip, LP5812_LED_EN_1, LP5812_DISABLE);
	lp5812_write(chip, LP5812_LED_EN_2, LP5812_DISABLE);
	lp5812_write(chip, LP5812_REG_ENABLE, LP5812_DISABLE);
}

static int lp5812_parse_led_channel(struct device_node *np,
				    struct lp5812_led_config *cfg,
				    int color_number)
{
	int color_id, reg, ret;
	u32 max_cur;

	ret = of_property_read_u32(np, "reg", &reg);
	if (ret)
		return ret;

	cfg->led_id[color_number] = reg;

	ret = of_property_read_u32(np, "led-max-microamp", &max_cur);
	if (ret)
		max_cur = 0;
	/* Convert microamps to driver units */
	cfg->max_current[color_number] = max_cur / 100;

	ret = of_property_read_u32(np, "color", &color_id);
	if (ret)
		color_id = 0;
	cfg->color_id[color_number] = color_id;

	return 0;
}

static int lp5812_parse_led(struct device_node *np,
			    struct lp5812_led_config *cfg,
			    int led_index)
{
	int num_colors, ret;

	of_property_read_string(np, "label", &cfg[led_index].name);

	ret = of_property_read_u32(np, "reg", &cfg[led_index].chan_nr);
	if (ret)
		return ret;

	num_colors = 0;
	for_each_available_child_of_node_scoped(np, child) {
		ret = lp5812_parse_led_channel(child, &cfg[led_index], num_colors);
		if (ret)
			return ret;

		num_colors++;
	}

	if (num_colors == 0) {
		ret = lp5812_parse_led_channel(np, &cfg[led_index], 0);
		if (ret)
			return ret;

		num_colors = 1;
		cfg[led_index].is_sc_led = true;
	} else {
		cfg[led_index].is_sc_led = false;
	}

	cfg[led_index].num_colors = num_colors;

	return 0;
}

static int lp5812_of_probe(struct device *dev,
			   struct device_node *np,
			   struct lp5812_chip *chip)
{
	struct lp5812_led_config *cfg;
	int num_channels, i = 0, ret;

	num_channels = of_get_available_child_count(np);
	if (num_channels == 0) {
		dev_err(dev, "no LED channels\n");
		return -EINVAL;
	}

	cfg = devm_kcalloc(dev, num_channels, sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	chip->led_config = &cfg[0];
	chip->num_channels = num_channels;

	for_each_available_child_of_node_scoped(np, child) {
		ret = lp5812_parse_led(child, cfg, i);
		if (ret)
			return -EINVAL;
		i++;
	}

	ret = of_property_read_string(np, "ti,scan-mode", &chip->scan_mode);
	if (ret)
		chip->scan_mode = LP5812_MODE_DIRECT_NAME;

	of_property_read_string(np, "label", &chip->label);

	return 0;
}

static int lp5812_probe(struct i2c_client *client)
{
	struct lp5812_chip *chip;
	struct device_node *np = dev_of_node(&client->dev);
	struct lp5812_led *leds;
	int ret;

	if (!np)
		return -EINVAL;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	ret = lp5812_of_probe(&client->dev, np, chip);
	if (ret)
		return ret;

	leds = devm_kcalloc(&client->dev, chip->num_channels, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	chip->client = client;
	mutex_init(&chip->lock);
	i2c_set_clientdata(client, chip);

	ret = lp5812_init_device(chip);
	if (ret)
		return ret;

	ret = lp5812_register_leds(leds, chip);
	if (ret)
		goto err_out;

	return 0;

err_out:
	lp5812_deinit_device(chip);
	return ret;
}

static void lp5812_remove(struct i2c_client *client)
{
	struct lp5812_chip *chip = i2c_get_clientdata(client);

	lp5812_deinit_device(chip);
}

static const struct of_device_id of_lp5812_match[] = {
	{ .compatible = "ti,lp5812" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_lp5812_match);

static struct i2c_driver lp5812_driver = {
	.driver = {
		.name   = "lp5812",
		.of_match_table = of_lp5812_match,
	},
	.probe		= lp5812_probe,
	.remove		= lp5812_remove,
};
module_i2c_driver(lp5812_driver);

MODULE_DESCRIPTION("Texas Instruments LP5812 LED Driver");
MODULE_AUTHOR("Jared Zhou <jared-zhou@ti.com>");
MODULE_LICENSE("GPL");
