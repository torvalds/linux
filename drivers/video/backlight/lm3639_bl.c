// SPDX-License-Identifier: GPL-2.0-only
/*
* Simple driver for Texas Instruments LM3639 Backlight + Flash LED driver chip
* Copyright (C) 2012 Texas Instruments
*/
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/platform_data/lm3639_bl.h>

#define REG_DEV_ID	0x00
#define REG_CHECKSUM	0x01
#define REG_BL_CONF_1	0x02
#define REG_BL_CONF_2	0x03
#define REG_BL_CONF_3	0x04
#define REG_BL_CONF_4	0x05
#define REG_FL_CONF_1	0x06
#define REG_FL_CONF_2	0x07
#define REG_FL_CONF_3	0x08
#define REG_IO_CTRL	0x09
#define REG_ENABLE	0x0A
#define REG_FLAG	0x0B
#define REG_MAX		REG_FLAG

struct lm3639_chip_data {
	struct device *dev;
	struct lm3639_platform_data *pdata;

	struct backlight_device *bled;
	struct led_classdev cdev_flash;
	struct led_classdev cdev_torch;
	struct regmap *regmap;

	unsigned int bled_mode;
	unsigned int bled_map;
	unsigned int last_flag;
};

/* initialize chip */
static int lm3639_chip_init(struct lm3639_chip_data *pchip)
{
	int ret;
	unsigned int reg_val;
	struct lm3639_platform_data *pdata = pchip->pdata;

	/* input pins config. */
	ret =
	    regmap_update_bits(pchip->regmap, REG_BL_CONF_1, 0x08,
			       pdata->pin_pwm);
	if (ret < 0)
		goto out;

	reg_val = (pdata->pin_pwm & 0x40) | pdata->pin_strobe | pdata->pin_tx;
	ret = regmap_update_bits(pchip->regmap, REG_IO_CTRL, 0x7C, reg_val);
	if (ret < 0)
		goto out;

	/* init brightness */
	ret = regmap_write(pchip->regmap, REG_BL_CONF_4, pdata->init_brt_led);
	if (ret < 0)
		goto out;

	ret = regmap_write(pchip->regmap, REG_BL_CONF_3, pdata->init_brt_led);
	if (ret < 0)
		goto out;

	/* output pins config. */
	if (!pdata->init_brt_led) {
		reg_val = pdata->fled_pins;
		reg_val |= pdata->bled_pins;
	} else {
		reg_val = pdata->fled_pins;
		reg_val |= pdata->bled_pins | 0x01;
	}

	ret = regmap_update_bits(pchip->regmap, REG_ENABLE, 0x79, reg_val);
	if (ret < 0)
		goto out;

	return ret;
out:
	dev_err(pchip->dev, "i2c failed to access register\n");
	return ret;
}

/* update and get brightness */
static int lm3639_bled_update_status(struct backlight_device *bl)
{
	int ret;
	unsigned int reg_val;
	struct lm3639_chip_data *pchip = bl_get_data(bl);
	struct lm3639_platform_data *pdata = pchip->pdata;

	ret = regmap_read(pchip->regmap, REG_FLAG, &reg_val);
	if (ret < 0)
		goto out;

	if (reg_val != 0)
		dev_info(pchip->dev, "last flag is 0x%x\n", reg_val);

	/* pwm control */
	if (pdata->pin_pwm) {
		if (pdata->pwm_set_intensity)
			pdata->pwm_set_intensity(bl->props.brightness,
						 pdata->max_brt_led);
		else
			dev_err(pchip->dev,
				"No pwm control func. in plat-data\n");
		return bl->props.brightness;
	}

	/* i2c control and set brigtness */
	ret = regmap_write(pchip->regmap, REG_BL_CONF_4, bl->props.brightness);
	if (ret < 0)
		goto out;
	ret = regmap_write(pchip->regmap, REG_BL_CONF_3, bl->props.brightness);
	if (ret < 0)
		goto out;

	if (!bl->props.brightness)
		ret = regmap_update_bits(pchip->regmap, REG_ENABLE, 0x01, 0x00);
	else
		ret = regmap_update_bits(pchip->regmap, REG_ENABLE, 0x01, 0x01);
	if (ret < 0)
		goto out;

	return bl->props.brightness;
out:
	dev_err(pchip->dev, "i2c failed to access registers\n");
	return bl->props.brightness;
}

static int lm3639_bled_get_brightness(struct backlight_device *bl)
{
	int ret;
	unsigned int reg_val;
	struct lm3639_chip_data *pchip = bl_get_data(bl);
	struct lm3639_platform_data *pdata = pchip->pdata;

	if (pdata->pin_pwm) {
		if (pdata->pwm_get_intensity)
			bl->props.brightness = pdata->pwm_get_intensity();
		else
			dev_err(pchip->dev,
				"No pwm control func. in plat-data\n");
		return bl->props.brightness;
	}

	ret = regmap_read(pchip->regmap, REG_BL_CONF_1, &reg_val);
	if (ret < 0)
		goto out;
	if (reg_val & 0x10)
		ret = regmap_read(pchip->regmap, REG_BL_CONF_4, &reg_val);
	else
		ret = regmap_read(pchip->regmap, REG_BL_CONF_3, &reg_val);
	if (ret < 0)
		goto out;
	bl->props.brightness = reg_val;

	return bl->props.brightness;
out:
	dev_err(pchip->dev, "i2c failed to access register\n");
	return bl->props.brightness;
}

static const struct backlight_ops lm3639_bled_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = lm3639_bled_update_status,
	.get_brightness = lm3639_bled_get_brightness,
};

/* backlight mapping mode */
static ssize_t lm3639_bled_mode_store(struct device *dev,
				      struct device_attribute *devAttr,
				      const char *buf, size_t size)
{
	ssize_t ret;
	struct lm3639_chip_data *pchip = dev_get_drvdata(dev);
	unsigned int state;

	ret = kstrtouint(buf, 10, &state);
	if (ret)
		goto out_input;

	if (!state)
		ret =
		    regmap_update_bits(pchip->regmap, REG_BL_CONF_1, 0x10,
				       0x00);
	else
		ret =
		    regmap_update_bits(pchip->regmap, REG_BL_CONF_1, 0x10,
				       0x10);

	if (ret < 0)
		goto out;

	return size;

out:
	dev_err(pchip->dev, "%s:i2c access fail to register\n", __func__);
	return ret;

out_input:
	dev_err(pchip->dev, "%s:input conversion fail\n", __func__);
	return ret;

}

static DEVICE_ATTR(bled_mode, S_IWUSR, NULL, lm3639_bled_mode_store);

/* torch */
static void lm3639_torch_brightness_set(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	int ret;
	unsigned int reg_val;
	struct lm3639_chip_data *pchip;

	pchip = container_of(cdev, struct lm3639_chip_data, cdev_torch);

	ret = regmap_read(pchip->regmap, REG_FLAG, &reg_val);
	if (ret < 0)
		goto out;
	if (reg_val != 0)
		dev_info(pchip->dev, "last flag is 0x%x\n", reg_val);

	/* brightness 0 means off state */
	if (!brightness) {
		ret = regmap_update_bits(pchip->regmap, REG_ENABLE, 0x06, 0x00);
		if (ret < 0)
			goto out;
		return;
	}

	ret = regmap_update_bits(pchip->regmap,
				 REG_FL_CONF_1, 0x70, (brightness - 1) << 4);
	if (ret < 0)
		goto out;
	ret = regmap_update_bits(pchip->regmap, REG_ENABLE, 0x06, 0x02);
	if (ret < 0)
		goto out;

	return;
out:
	dev_err(pchip->dev, "i2c failed to access register\n");
}

/* flash */
static void lm3639_flash_brightness_set(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	int ret;
	unsigned int reg_val;
	struct lm3639_chip_data *pchip;

	pchip = container_of(cdev, struct lm3639_chip_data, cdev_flash);

	ret = regmap_read(pchip->regmap, REG_FLAG, &reg_val);
	if (ret < 0)
		goto out;
	if (reg_val != 0)
		dev_info(pchip->dev, "last flag is 0x%x\n", reg_val);

	/* torch off before flash control */
	ret = regmap_update_bits(pchip->regmap, REG_ENABLE, 0x06, 0x00);
	if (ret < 0)
		goto out;

	/* brightness 0 means off state */
	if (!brightness)
		return;

	ret = regmap_update_bits(pchip->regmap,
				 REG_FL_CONF_1, 0x0F, brightness - 1);
	if (ret < 0)
		goto out;
	ret = regmap_update_bits(pchip->regmap, REG_ENABLE, 0x06, 0x06);
	if (ret < 0)
		goto out;

	return;
out:
	dev_err(pchip->dev, "i2c failed to access register\n");
}

static const struct regmap_config lm3639_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_MAX,
};

static int lm3639_probe(struct i2c_client *client)
{
	int ret;
	struct lm3639_chip_data *pchip;
	struct lm3639_platform_data *pdata = dev_get_platdata(&client->dev);
	struct backlight_properties props;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c functionality check fail.\n");
		return -EOPNOTSUPP;
	}

	if (pdata == NULL) {
		dev_err(&client->dev, "Needs Platform Data.\n");
		return -ENODATA;
	}

	pchip = devm_kzalloc(&client->dev,
			     sizeof(struct lm3639_chip_data), GFP_KERNEL);
	if (!pchip)
		return -ENOMEM;

	pchip->pdata = pdata;
	pchip->dev = &client->dev;

	pchip->regmap = devm_regmap_init_i2c(client, &lm3639_regmap);
	if (IS_ERR(pchip->regmap)) {
		ret = PTR_ERR(pchip->regmap);
		dev_err(&client->dev, "fail : allocate register map: %d\n",
			ret);
		return ret;
	}
	i2c_set_clientdata(client, pchip);

	/* chip initialize */
	ret = lm3639_chip_init(pchip);
	if (ret < 0) {
		dev_err(&client->dev, "fail : chip init\n");
		goto err_out;
	}

	/* backlight */
	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.brightness = pdata->init_brt_led;
	props.max_brightness = pdata->max_brt_led;
	pchip->bled =
	    devm_backlight_device_register(pchip->dev, "lm3639_bled",
					   pchip->dev, pchip, &lm3639_bled_ops,
					   &props);
	if (IS_ERR(pchip->bled)) {
		dev_err(&client->dev, "fail : backlight register\n");
		ret = PTR_ERR(pchip->bled);
		goto err_out;
	}

	ret = device_create_file(&(pchip->bled->dev), &dev_attr_bled_mode);
	if (ret < 0) {
		dev_err(&client->dev, "failed : add sysfs entries\n");
		goto err_out;
	}

	/* flash */
	pchip->cdev_flash.name = "lm3639_flash";
	pchip->cdev_flash.max_brightness = 16;
	pchip->cdev_flash.brightness_set = lm3639_flash_brightness_set;
	ret = led_classdev_register((struct device *)
				    &client->dev, &pchip->cdev_flash);
	if (ret < 0) {
		dev_err(&client->dev, "fail : flash register\n");
		goto err_flash;
	}

	/* torch */
	pchip->cdev_torch.name = "lm3639_torch";
	pchip->cdev_torch.max_brightness = 8;
	pchip->cdev_torch.brightness_set = lm3639_torch_brightness_set;
	ret = led_classdev_register((struct device *)
				    &client->dev, &pchip->cdev_torch);
	if (ret < 0) {
		dev_err(&client->dev, "fail : torch register\n");
		goto err_torch;
	}

	return 0;

err_torch:
	led_classdev_unregister(&pchip->cdev_flash);
err_flash:
	device_remove_file(&(pchip->bled->dev), &dev_attr_bled_mode);
err_out:
	return ret;
}

static void lm3639_remove(struct i2c_client *client)
{
	struct lm3639_chip_data *pchip = i2c_get_clientdata(client);

	regmap_write(pchip->regmap, REG_ENABLE, 0x00);

	led_classdev_unregister(&pchip->cdev_torch);
	led_classdev_unregister(&pchip->cdev_flash);
	if (pchip->bled)
		device_remove_file(&(pchip->bled->dev), &dev_attr_bled_mode);
}

static const struct i2c_device_id lm3639_id[] = {
	{LM3639_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, lm3639_id);
static struct i2c_driver lm3639_i2c_driver = {
	.driver = {
		   .name = LM3639_NAME,
		   },
	.probe = lm3639_probe,
	.remove = lm3639_remove,
	.id_table = lm3639_id,
};

module_i2c_driver(lm3639_i2c_driver);

MODULE_DESCRIPTION("Texas Instruments Backlight+Flash LED driver for LM3639");
MODULE_AUTHOR("Daniel Jeong <gshark.jeong@gmail.com>");
MODULE_AUTHOR("Ldd Mlp <ldd-mlp@list.ti.com>");
MODULE_LICENSE("GPL v2");
