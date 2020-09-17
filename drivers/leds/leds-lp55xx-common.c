// SPDX-License-Identifier: GPL-2.0-only
/*
 * LP5521/LP5523/LP55231/LP5562 Common Driver
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * Derived from leds-lp5521.c, leds-lp5523.c
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_data/leds-lp55xx.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>

#include "leds-lp55xx-common.h"

/* External clock rate */
#define LP55XX_CLK_32K			32768

static struct lp55xx_led *cdev_to_lp55xx_led(struct led_classdev *cdev)
{
	return container_of(cdev, struct lp55xx_led, cdev);
}

static struct lp55xx_led *dev_to_lp55xx_led(struct device *dev)
{
	return cdev_to_lp55xx_led(dev_get_drvdata(dev));
}

static struct lp55xx_led *mcled_cdev_to_led(struct led_classdev_mc *mc_cdev)
{
	return container_of(mc_cdev, struct lp55xx_led, mc_cdev);
}

static void lp55xx_reset_device(struct lp55xx_chip *chip)
{
	struct lp55xx_device_config *cfg = chip->cfg;
	u8 addr = cfg->reset.addr;
	u8 val  = cfg->reset.val;

	/* no error checking here because no ACK from the device after reset */
	lp55xx_write(chip, addr, val);
}

static int lp55xx_detect_device(struct lp55xx_chip *chip)
{
	struct lp55xx_device_config *cfg = chip->cfg;
	u8 addr = cfg->enable.addr;
	u8 val  = cfg->enable.val;
	int ret;

	ret = lp55xx_write(chip, addr, val);
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	ret = lp55xx_read(chip, addr, &val);
	if (ret)
		return ret;

	if (val != cfg->enable.val)
		return -ENODEV;

	return 0;
}

static int lp55xx_post_init_device(struct lp55xx_chip *chip)
{
	struct lp55xx_device_config *cfg = chip->cfg;

	if (!cfg->post_init_device)
		return 0;

	return cfg->post_init_device(chip);
}

static ssize_t led_current_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lp55xx_led *led = dev_to_lp55xx_led(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", led->led_current);
}

static ssize_t led_current_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct lp55xx_led *led = dev_to_lp55xx_led(dev);
	struct lp55xx_chip *chip = led->chip;
	unsigned long curr;

	if (kstrtoul(buf, 0, &curr))
		return -EINVAL;

	if (curr > led->max_current)
		return -EINVAL;

	if (!chip->cfg->set_led_current)
		return len;

	mutex_lock(&chip->lock);
	chip->cfg->set_led_current(led, (u8)curr);
	mutex_unlock(&chip->lock);

	return len;
}

static ssize_t max_current_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lp55xx_led *led = dev_to_lp55xx_led(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", led->max_current);
}

static DEVICE_ATTR_RW(led_current);
static DEVICE_ATTR_RO(max_current);

static struct attribute *lp55xx_led_attrs[] = {
	&dev_attr_led_current.attr,
	&dev_attr_max_current.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lp55xx_led);

static int lp55xx_set_mc_brightness(struct led_classdev *cdev,
				    enum led_brightness brightness)
{
	struct led_classdev_mc *mc_dev = lcdev_to_mccdev(cdev);
	struct lp55xx_led *led = mcled_cdev_to_led(mc_dev);
	struct lp55xx_device_config *cfg = led->chip->cfg;

	led_mc_calc_color_components(&led->mc_cdev, brightness);
	return cfg->multicolor_brightness_fn(led);

}

static int lp55xx_set_brightness(struct led_classdev *cdev,
			     enum led_brightness brightness)
{
	struct lp55xx_led *led = cdev_to_lp55xx_led(cdev);
	struct lp55xx_device_config *cfg = led->chip->cfg;

	led->brightness = (u8)brightness;
	return cfg->brightness_fn(led);
}

static int lp55xx_init_led(struct lp55xx_led *led,
			struct lp55xx_chip *chip, int chan)
{
	struct lp55xx_platform_data *pdata = chip->pdata;
	struct lp55xx_device_config *cfg = chip->cfg;
	struct device *dev = &chip->cl->dev;
	int max_channel = cfg->max_channel;
	struct mc_subled *mc_led_info;
	struct led_classdev *led_cdev;
	char name[32];
	int i, j = 0;
	int ret;

	if (chan >= max_channel) {
		dev_err(dev, "invalid channel: %d / %d\n", chan, max_channel);
		return -EINVAL;
	}

	if (pdata->led_config[chan].led_current == 0)
		return 0;

	if (pdata->led_config[chan].name) {
		led->cdev.name = pdata->led_config[chan].name;
	} else {
		snprintf(name, sizeof(name), "%s:channel%d",
			pdata->label ? : chip->cl->name, chan);
		led->cdev.name = name;
	}

	if (pdata->led_config[chan].num_colors > 1) {
		mc_led_info = devm_kcalloc(dev,
					   pdata->led_config[chan].num_colors,
					   sizeof(*mc_led_info), GFP_KERNEL);
		if (!mc_led_info)
			return -ENOMEM;

		led_cdev = &led->mc_cdev.led_cdev;
		led_cdev->name = led->cdev.name;
		led_cdev->brightness_set_blocking = lp55xx_set_mc_brightness;
		led->mc_cdev.num_colors = pdata->led_config[chan].num_colors;
		for (i = 0; i < led->mc_cdev.num_colors; i++) {
			mc_led_info[i].color_index =
				pdata->led_config[chan].color_id[i];
			mc_led_info[i].channel =
					pdata->led_config[chan].output_num[i];
			j++;
		}

		led->mc_cdev.subled_info = mc_led_info;
	} else {
		led->cdev.brightness_set_blocking = lp55xx_set_brightness;
	}

	led->cdev.groups = lp55xx_led_groups;
	led->cdev.default_trigger = pdata->led_config[chan].default_trigger;
	led->led_current = pdata->led_config[chan].led_current;
	led->max_current = pdata->led_config[chan].max_current;
	led->chan_nr = pdata->led_config[chan].chan_nr;

	if (led->chan_nr >= max_channel) {
		dev_err(dev, "Use channel numbers between 0 and %d\n",
			max_channel - 1);
		return -EINVAL;
	}

	if (pdata->led_config[chan].num_colors > 1)
		ret = devm_led_classdev_multicolor_register(dev, &led->mc_cdev);
	else
		ret = devm_led_classdev_register(dev, &led->cdev);

	if (ret) {
		dev_err(dev, "led register err: %d\n", ret);
		return ret;
	}

	return 0;
}

static void lp55xx_firmware_loaded(const struct firmware *fw, void *context)
{
	struct lp55xx_chip *chip = context;
	struct device *dev = &chip->cl->dev;
	enum lp55xx_engine_index idx = chip->engine_idx;

	if (!fw) {
		dev_err(dev, "firmware request failed\n");
		return;
	}

	/* handling firmware data is chip dependent */
	mutex_lock(&chip->lock);

	chip->engines[idx - 1].mode = LP55XX_ENGINE_LOAD;
	chip->fw = fw;
	if (chip->cfg->firmware_cb)
		chip->cfg->firmware_cb(chip);

	mutex_unlock(&chip->lock);

	/* firmware should be released for other channel use */
	release_firmware(chip->fw);
	chip->fw = NULL;
}

static int lp55xx_request_firmware(struct lp55xx_chip *chip)
{
	const char *name = chip->cl->name;
	struct device *dev = &chip->cl->dev;

	return request_firmware_nowait(THIS_MODULE, false, name, dev,
				GFP_KERNEL, chip, lp55xx_firmware_loaded);
}

static ssize_t select_engine_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;

	return sprintf(buf, "%d\n", chip->engine_idx);
}

static ssize_t select_engine_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	/* select the engine to be run */

	switch (val) {
	case LP55XX_ENGINE_1:
	case LP55XX_ENGINE_2:
	case LP55XX_ENGINE_3:
		mutex_lock(&chip->lock);
		chip->engine_idx = val;
		ret = lp55xx_request_firmware(chip);
		mutex_unlock(&chip->lock);
		break;
	default:
		dev_err(dev, "%lu: invalid engine index. (1, 2, 3)\n", val);
		return -EINVAL;
	}

	if (ret) {
		dev_err(dev, "request firmware err: %d\n", ret);
		return ret;
	}

	return len;
}

static inline void lp55xx_run_engine(struct lp55xx_chip *chip, bool start)
{
	if (chip->cfg->run_engine)
		chip->cfg->run_engine(chip, start);
}

static ssize_t run_engine_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct lp55xx_led *led = i2c_get_clientdata(to_i2c_client(dev));
	struct lp55xx_chip *chip = led->chip;
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	/* run or stop the selected engine */

	if (val <= 0) {
		lp55xx_run_engine(chip, false);
		return len;
	}

	mutex_lock(&chip->lock);
	lp55xx_run_engine(chip, true);
	mutex_unlock(&chip->lock);

	return len;
}

static DEVICE_ATTR_RW(select_engine);
static DEVICE_ATTR_WO(run_engine);

static struct attribute *lp55xx_engine_attributes[] = {
	&dev_attr_select_engine.attr,
	&dev_attr_run_engine.attr,
	NULL,
};

static const struct attribute_group lp55xx_engine_attr_group = {
	.attrs = lp55xx_engine_attributes,
};

int lp55xx_write(struct lp55xx_chip *chip, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(chip->cl, reg, val);
}
EXPORT_SYMBOL_GPL(lp55xx_write);

int lp55xx_read(struct lp55xx_chip *chip, u8 reg, u8 *val)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(chip->cl, reg);
	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}
EXPORT_SYMBOL_GPL(lp55xx_read);

int lp55xx_update_bits(struct lp55xx_chip *chip, u8 reg, u8 mask, u8 val)
{
	int ret;
	u8 tmp;

	ret = lp55xx_read(chip, reg, &tmp);
	if (ret)
		return ret;

	tmp &= ~mask;
	tmp |= val & mask;

	return lp55xx_write(chip, reg, tmp);
}
EXPORT_SYMBOL_GPL(lp55xx_update_bits);

bool lp55xx_is_extclk_used(struct lp55xx_chip *chip)
{
	struct clk *clk;
	int err;

	clk = devm_clk_get(&chip->cl->dev, "32k_clk");
	if (IS_ERR(clk))
		goto use_internal_clk;

	err = clk_prepare_enable(clk);
	if (err)
		goto use_internal_clk;

	if (clk_get_rate(clk) != LP55XX_CLK_32K) {
		clk_disable_unprepare(clk);
		goto use_internal_clk;
	}

	dev_info(&chip->cl->dev, "%dHz external clock used\n",	LP55XX_CLK_32K);

	chip->clk = clk;
	return true;

use_internal_clk:
	dev_info(&chip->cl->dev, "internal clock used\n");
	return false;
}
EXPORT_SYMBOL_GPL(lp55xx_is_extclk_used);

int lp55xx_init_device(struct lp55xx_chip *chip)
{
	struct lp55xx_platform_data *pdata;
	struct lp55xx_device_config *cfg;
	struct device *dev = &chip->cl->dev;
	int ret = 0;

	WARN_ON(!chip);

	pdata = chip->pdata;
	cfg = chip->cfg;

	if (!pdata || !cfg)
		return -EINVAL;

	if (pdata->enable_gpiod) {
		gpiod_set_consumer_name(pdata->enable_gpiod, "LP55xx enable");
		gpiod_set_value(pdata->enable_gpiod, 0);
		usleep_range(1000, 2000); /* Keep enable down at least 1ms */
		gpiod_set_value(pdata->enable_gpiod, 1);
		usleep_range(1000, 2000); /* 500us abs min. */
	}

	lp55xx_reset_device(chip);

	/*
	 * Exact value is not available. 10 - 20ms
	 * appears to be enough for reset.
	 */
	usleep_range(10000, 20000);

	ret = lp55xx_detect_device(chip);
	if (ret) {
		dev_err(dev, "device detection err: %d\n", ret);
		goto err;
	}

	/* chip specific initialization */
	ret = lp55xx_post_init_device(chip);
	if (ret) {
		dev_err(dev, "post init device err: %d\n", ret);
		goto err_post_init;
	}

	return 0;

err_post_init:
	lp55xx_deinit_device(chip);
err:
	return ret;
}
EXPORT_SYMBOL_GPL(lp55xx_init_device);

void lp55xx_deinit_device(struct lp55xx_chip *chip)
{
	struct lp55xx_platform_data *pdata = chip->pdata;

	if (chip->clk)
		clk_disable_unprepare(chip->clk);

	if (pdata->enable_gpiod)
		gpiod_set_value(pdata->enable_gpiod, 0);
}
EXPORT_SYMBOL_GPL(lp55xx_deinit_device);

int lp55xx_register_leds(struct lp55xx_led *led, struct lp55xx_chip *chip)
{
	struct lp55xx_platform_data *pdata = chip->pdata;
	struct lp55xx_device_config *cfg = chip->cfg;
	int num_channels = pdata->num_channels;
	struct lp55xx_led *each;
	u8 led_current;
	int ret;
	int i;

	if (!cfg->brightness_fn) {
		dev_err(&chip->cl->dev, "empty brightness configuration\n");
		return -EINVAL;
	}

	for (i = 0; i < num_channels; i++) {

		/* do not initialize channels that are not connected */
		if (pdata->led_config[i].led_current == 0)
			continue;

		led_current = pdata->led_config[i].led_current;
		each = led + i;
		ret = lp55xx_init_led(each, chip, i);
		if (ret)
			goto err_init_led;

		chip->num_leds++;
		each->chip = chip;

		/* setting led current at each channel */
		if (cfg->set_led_current)
			cfg->set_led_current(each, led_current);
	}

	return 0;

err_init_led:
	return ret;
}
EXPORT_SYMBOL_GPL(lp55xx_register_leds);

int lp55xx_register_sysfs(struct lp55xx_chip *chip)
{
	struct device *dev = &chip->cl->dev;
	struct lp55xx_device_config *cfg = chip->cfg;
	int ret;

	if (!cfg->run_engine || !cfg->firmware_cb)
		goto dev_specific_attrs;

	ret = sysfs_create_group(&dev->kobj, &lp55xx_engine_attr_group);
	if (ret)
		return ret;

dev_specific_attrs:
	return cfg->dev_attr_group ?
		sysfs_create_group(&dev->kobj, cfg->dev_attr_group) : 0;
}
EXPORT_SYMBOL_GPL(lp55xx_register_sysfs);

void lp55xx_unregister_sysfs(struct lp55xx_chip *chip)
{
	struct device *dev = &chip->cl->dev;
	struct lp55xx_device_config *cfg = chip->cfg;

	if (cfg->dev_attr_group)
		sysfs_remove_group(&dev->kobj, cfg->dev_attr_group);

	sysfs_remove_group(&dev->kobj, &lp55xx_engine_attr_group);
}
EXPORT_SYMBOL_GPL(lp55xx_unregister_sysfs);

static int lp55xx_parse_common_child(struct device_node *np,
				     struct lp55xx_led_config *cfg,
				     int led_number, int *chan_nr)
{
	int ret;

	of_property_read_string(np, "chan-name",
				&cfg[led_number].name);
	of_property_read_u8(np, "led-cur",
			    &cfg[led_number].led_current);
	of_property_read_u8(np, "max-cur",
			    &cfg[led_number].max_current);

	ret = of_property_read_u32(np, "reg", chan_nr);
	if (ret)
		return ret;

	if (*chan_nr < 0 || *chan_nr > cfg->max_channel)
		return -EINVAL;

	return 0;
}

static int lp55xx_parse_multi_led_child(struct device_node *child,
					 struct lp55xx_led_config *cfg,
					 int child_number, int color_number)
{
	int chan_nr, color_id, ret;

	ret = lp55xx_parse_common_child(child, cfg, child_number, &chan_nr);
	if (ret)
		return ret;

	ret = of_property_read_u32(child, "color", &color_id);
	if (ret)
		return ret;

	cfg[child_number].color_id[color_number] = color_id;
	cfg[child_number].output_num[color_number] = chan_nr;

	return 0;
}

static int lp55xx_parse_multi_led(struct device_node *np,
				  struct lp55xx_led_config *cfg,
				  int child_number)
{
	struct device_node *child;
	int num_colors = 0, ret;

	for_each_available_child_of_node(np, child) {
		ret = lp55xx_parse_multi_led_child(child, cfg, child_number,
						   num_colors);
		if (ret)
			return ret;
		num_colors++;
	}

	cfg[child_number].num_colors = num_colors;

	return 0;
}

static int lp55xx_parse_logical_led(struct device_node *np,
				   struct lp55xx_led_config *cfg,
				   int child_number)
{
	int led_color, ret;
	int chan_nr = 0;

	cfg[child_number].default_trigger =
		of_get_property(np, "linux,default-trigger", NULL);

	ret = of_property_read_u32(np, "color", &led_color);
	if (ret)
		return ret;

	if (led_color == LED_COLOR_ID_RGB)
		return lp55xx_parse_multi_led(np, cfg, child_number);

	ret =  lp55xx_parse_common_child(np, cfg, child_number, &chan_nr);
	if (ret < 0)
		return ret;

	cfg[child_number].chan_nr = chan_nr;

	return ret;
}

struct lp55xx_platform_data *lp55xx_of_populate_pdata(struct device *dev,
						      struct device_node *np,
						      struct lp55xx_chip *chip)
{
	struct device_node *child;
	struct lp55xx_platform_data *pdata;
	struct lp55xx_led_config *cfg;
	int num_channels;
	int i = 0;
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	num_channels = of_get_available_child_count(np);
	if (num_channels == 0) {
		dev_err(dev, "no LED channels\n");
		return ERR_PTR(-EINVAL);
	}

	cfg = devm_kcalloc(dev, num_channels, sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return ERR_PTR(-ENOMEM);

	pdata->led_config = &cfg[0];
	pdata->num_channels = num_channels;
	cfg->max_channel = chip->cfg->max_channel;

	for_each_available_child_of_node(np, child) {
		ret = lp55xx_parse_logical_led(child, cfg, i);
		if (ret)
			return ERR_PTR(-EINVAL);
		i++;
	}

	of_property_read_string(np, "label", &pdata->label);
	of_property_read_u8(np, "clock-mode", &pdata->clock_mode);

	pdata->enable_gpiod = devm_gpiod_get_optional(dev, "enable",
						      GPIOD_ASIS);
	if (IS_ERR(pdata->enable_gpiod))
		return ERR_CAST(pdata->enable_gpiod);

	/* LP8501 specific */
	of_property_read_u8(np, "pwr-sel", (u8 *)&pdata->pwr_sel);

	return pdata;
}
EXPORT_SYMBOL_GPL(lp55xx_of_populate_pdata);

MODULE_AUTHOR("Milo Kim <milo.kim@ti.com>");
MODULE_DESCRIPTION("LP55xx Common Driver");
MODULE_LICENSE("GPL");
