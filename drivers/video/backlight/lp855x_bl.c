// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI LP855x Backlight Driver
 *
 *			Copyright (C) 2011 Texas Instruments
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_data/lp855x.h>
#include <linux/pwm.h>
#include <linux/regulator/consumer.h>

/* LP8550/1/2/3/6 Registers */
#define LP855X_BRIGHTNESS_CTRL		0x00
#define LP855X_DEVICE_CTRL		0x01
#define LP855X_EEPROM_START		0xA0
#define LP855X_EEPROM_END		0xA7
#define LP8556_EPROM_START		0xA0
#define LP8556_EPROM_END		0xAF

/* LP8555/7 Registers */
#define LP8557_BL_CMD			0x00
#define LP8557_BL_MASK			0x01
#define LP8557_BL_ON			0x01
#define LP8557_BL_OFF			0x00
#define LP8557_BRIGHTNESS_CTRL		0x04
#define LP8557_CONFIG			0x10
#define LP8555_EPROM_START		0x10
#define LP8555_EPROM_END		0x7A
#define LP8557_EPROM_START		0x10
#define LP8557_EPROM_END		0x1E

#define DEFAULT_BL_NAME		"lcd-backlight"
#define MAX_BRIGHTNESS		255

enum lp855x_brightness_ctrl_mode {
	PWM_BASED = 1,
	REGISTER_BASED,
};

struct lp855x;

/*
 * struct lp855x_device_config
 * @pre_init_device: init device function call before updating the brightness
 * @reg_brightness: register address for brigthenss control
 * @reg_devicectrl: register address for device control
 * @post_init_device: late init device function call
 */
struct lp855x_device_config {
	int (*pre_init_device)(struct lp855x *);
	u8 reg_brightness;
	u8 reg_devicectrl;
	int (*post_init_device)(struct lp855x *);
};

struct lp855x {
	const char *chipname;
	enum lp855x_chip_id chip_id;
	enum lp855x_brightness_ctrl_mode mode;
	struct lp855x_device_config *cfg;
	struct i2c_client *client;
	struct backlight_device *bl;
	struct device *dev;
	struct lp855x_platform_data *pdata;
	struct pwm_device *pwm;
	struct regulator *supply;	/* regulator for VDD input */
	struct regulator *enable;	/* regulator for EN/VDDIO input */
};

static int lp855x_write_byte(struct lp855x *lp, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(lp->client, reg, data);
}

static int lp855x_update_bit(struct lp855x *lp, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	ret = i2c_smbus_read_byte_data(lp->client, reg);
	if (ret < 0) {
		dev_err(lp->dev, "failed to read 0x%.2x\n", reg);
		return ret;
	}

	tmp = (u8)ret;
	tmp &= ~mask;
	tmp |= data & mask;

	return lp855x_write_byte(lp, reg, tmp);
}

static bool lp855x_is_valid_rom_area(struct lp855x *lp, u8 addr)
{
	u8 start, end;

	switch (lp->chip_id) {
	case LP8550:
	case LP8551:
	case LP8552:
	case LP8553:
		start = LP855X_EEPROM_START;
		end = LP855X_EEPROM_END;
		break;
	case LP8556:
		start = LP8556_EPROM_START;
		end = LP8556_EPROM_END;
		break;
	case LP8555:
		start = LP8555_EPROM_START;
		end = LP8555_EPROM_END;
		break;
	case LP8557:
		start = LP8557_EPROM_START;
		end = LP8557_EPROM_END;
		break;
	default:
		return false;
	}

	return addr >= start && addr <= end;
}

static int lp8557_bl_off(struct lp855x *lp)
{
	/* BL_ON = 0 before updating EPROM settings */
	return lp855x_update_bit(lp, LP8557_BL_CMD, LP8557_BL_MASK,
				LP8557_BL_OFF);
}

static int lp8557_bl_on(struct lp855x *lp)
{
	/* BL_ON = 1 after updating EPROM settings */
	return lp855x_update_bit(lp, LP8557_BL_CMD, LP8557_BL_MASK,
				LP8557_BL_ON);
}

static struct lp855x_device_config lp855x_dev_cfg = {
	.reg_brightness = LP855X_BRIGHTNESS_CTRL,
	.reg_devicectrl = LP855X_DEVICE_CTRL,
};

static struct lp855x_device_config lp8557_dev_cfg = {
	.reg_brightness = LP8557_BRIGHTNESS_CTRL,
	.reg_devicectrl = LP8557_CONFIG,
	.pre_init_device = lp8557_bl_off,
	.post_init_device = lp8557_bl_on,
};

/*
 * Device specific configuration flow
 *
 *    a) pre_init_device(optional)
 *    b) update the brightness register
 *    c) update device control register
 *    d) update ROM area(optional)
 *    e) post_init_device(optional)
 *
 */
static int lp855x_configure(struct lp855x *lp)
{
	u8 val, addr;
	int i, ret;
	struct lp855x_platform_data *pd = lp->pdata;

	if (lp->cfg->pre_init_device) {
		ret = lp->cfg->pre_init_device(lp);
		if (ret) {
			dev_err(lp->dev, "pre init device err: %d\n", ret);
			goto err;
		}
	}

	val = pd->initial_brightness;
	ret = lp855x_write_byte(lp, lp->cfg->reg_brightness, val);
	if (ret)
		goto err;

	val = pd->device_control;
	ret = lp855x_write_byte(lp, lp->cfg->reg_devicectrl, val);
	if (ret)
		goto err;

	if (pd->size_program > 0) {
		for (i = 0; i < pd->size_program; i++) {
			addr = pd->rom_data[i].addr;
			val = pd->rom_data[i].val;
			if (!lp855x_is_valid_rom_area(lp, addr))
				continue;

			ret = lp855x_write_byte(lp, addr, val);
			if (ret)
				goto err;
		}
	}

	if (lp->cfg->post_init_device) {
		ret = lp->cfg->post_init_device(lp);
		if (ret) {
			dev_err(lp->dev, "post init device err: %d\n", ret);
			goto err;
		}
	}

	return 0;

err:
	return ret;
}

static void lp855x_pwm_ctrl(struct lp855x *lp, int br, int max_br)
{
	struct pwm_device *pwm;
	struct pwm_state state;

	/* request pwm device with the consumer name */
	if (!lp->pwm) {
		pwm = devm_pwm_get(lp->dev, lp->chipname);
		if (IS_ERR(pwm))
			return;

		lp->pwm = pwm;

		pwm_init_state(lp->pwm, &state);
	} else {
		pwm_get_state(lp->pwm, &state);
	}

	state.period = lp->pdata->period_ns;
	state.duty_cycle = div_u64(br * state.period, max_br);
	state.enabled = state.duty_cycle;

	pwm_apply_state(lp->pwm, &state);
}

static int lp855x_bl_update_status(struct backlight_device *bl)
{
	struct lp855x *lp = bl_get_data(bl);
	int brightness = bl->props.brightness;

	if (bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;

	if (lp->mode == PWM_BASED)
		lp855x_pwm_ctrl(lp, brightness, bl->props.max_brightness);
	else if (lp->mode == REGISTER_BASED)
		lp855x_write_byte(lp, lp->cfg->reg_brightness, (u8)brightness);

	return 0;
}

static const struct backlight_ops lp855x_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = lp855x_bl_update_status,
};

static int lp855x_backlight_register(struct lp855x *lp)
{
	struct backlight_device *bl;
	struct backlight_properties props;
	struct lp855x_platform_data *pdata = lp->pdata;
	const char *name = pdata->name ? : DEFAULT_BL_NAME;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = MAX_BRIGHTNESS;

	if (pdata->initial_brightness > props.max_brightness)
		pdata->initial_brightness = props.max_brightness;

	props.brightness = pdata->initial_brightness;

	bl = devm_backlight_device_register(lp->dev, name, lp->dev, lp,
				       &lp855x_bl_ops, &props);
	if (IS_ERR(bl))
		return PTR_ERR(bl);

	lp->bl = bl;

	return 0;
}

static ssize_t lp855x_get_chip_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lp855x *lp = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", lp->chipname);
}

static ssize_t lp855x_get_bl_ctl_mode(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct lp855x *lp = dev_get_drvdata(dev);
	char *strmode = NULL;

	if (lp->mode == PWM_BASED)
		strmode = "pwm based";
	else if (lp->mode == REGISTER_BASED)
		strmode = "register based";

	return scnprintf(buf, PAGE_SIZE, "%s\n", strmode);
}

static DEVICE_ATTR(chip_id, S_IRUGO, lp855x_get_chip_id, NULL);
static DEVICE_ATTR(bl_ctl_mode, S_IRUGO, lp855x_get_bl_ctl_mode, NULL);

static struct attribute *lp855x_attributes[] = {
	&dev_attr_chip_id.attr,
	&dev_attr_bl_ctl_mode.attr,
	NULL,
};

static const struct attribute_group lp855x_attr_group = {
	.attrs = lp855x_attributes,
};

#ifdef CONFIG_OF
static int lp855x_parse_dt(struct lp855x *lp)
{
	struct device *dev = lp->dev;
	struct device_node *node = dev->of_node;
	struct lp855x_platform_data *pdata = lp->pdata;
	int rom_length;

	if (!node) {
		dev_err(dev, "no platform data\n");
		return -EINVAL;
	}

	of_property_read_string(node, "bl-name", &pdata->name);
	of_property_read_u8(node, "dev-ctrl", &pdata->device_control);
	of_property_read_u8(node, "init-brt", &pdata->initial_brightness);
	of_property_read_u32(node, "pwm-period", &pdata->period_ns);

	/* Fill ROM platform data if defined */
	rom_length = of_get_child_count(node);
	if (rom_length > 0) {
		struct lp855x_rom_data *rom;
		struct device_node *child;
		int i = 0;

		rom = devm_kcalloc(dev, rom_length, sizeof(*rom), GFP_KERNEL);
		if (!rom)
			return -ENOMEM;

		for_each_child_of_node(node, child) {
			of_property_read_u8(child, "rom-addr", &rom[i].addr);
			of_property_read_u8(child, "rom-val", &rom[i].val);
			i++;
		}

		pdata->size_program = rom_length;
		pdata->rom_data = &rom[0];
	}

	return 0;
}
#else
static int lp855x_parse_dt(struct lp855x *lp)
{
	return -EINVAL;
}
#endif

static int lp855x_parse_acpi(struct lp855x *lp)
{
	int ret;

	/*
	 * On ACPI the device has already been initialized by the firmware
	 * and is in register mode, so we can read back the settings from
	 * the registers.
	 */
	ret = i2c_smbus_read_byte_data(lp->client, lp->cfg->reg_brightness);
	if (ret < 0)
		return ret;

	lp->pdata->initial_brightness = ret;

	ret = i2c_smbus_read_byte_data(lp->client, lp->cfg->reg_devicectrl);
	if (ret < 0)
		return ret;

	lp->pdata->device_control = ret;
	return 0;
}

static int lp855x_probe(struct i2c_client *cl)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(cl);
	const struct acpi_device_id *acpi_id = NULL;
	struct device *dev = &cl->dev;
	struct lp855x *lp;
	int ret;

	if (!i2c_check_functionality(cl->adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EIO;

	lp = devm_kzalloc(dev, sizeof(struct lp855x), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	lp->client = cl;
	lp->dev = dev;
	lp->pdata = dev_get_platdata(dev);

	if (id) {
		lp->chipname = id->name;
		lp->chip_id = id->driver_data;
	} else {
		acpi_id = acpi_match_device(dev->driver->acpi_match_table, dev);
		if (!acpi_id)
			return -ENODEV;

		lp->chipname = acpi_id->id;
		lp->chip_id = acpi_id->driver_data;
	}

	switch (lp->chip_id) {
	case LP8550:
	case LP8551:
	case LP8552:
	case LP8553:
	case LP8556:
		lp->cfg = &lp855x_dev_cfg;
		break;
	case LP8555:
	case LP8557:
		lp->cfg = &lp8557_dev_cfg;
		break;
	default:
		return -EINVAL;
	}

	if (!lp->pdata) {
		lp->pdata = devm_kzalloc(dev, sizeof(*lp->pdata), GFP_KERNEL);
		if (!lp->pdata)
			return -ENOMEM;

		if (id) {
			ret = lp855x_parse_dt(lp);
			if (ret < 0)
				return ret;
		} else {
			ret = lp855x_parse_acpi(lp);
			if (ret < 0)
				return ret;
		}
	}

	if (lp->pdata->period_ns > 0)
		lp->mode = PWM_BASED;
	else
		lp->mode = REGISTER_BASED;

	lp->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(lp->supply)) {
		if (PTR_ERR(lp->supply) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		lp->supply = NULL;
	}

	lp->enable = devm_regulator_get_optional(dev, "enable");
	if (IS_ERR(lp->enable)) {
		ret = PTR_ERR(lp->enable);
		if (ret == -ENODEV) {
			lp->enable = NULL;
		} else {
			return dev_err_probe(dev, ret, "getting enable regulator\n");
		}
	}

	if (lp->supply) {
		ret = regulator_enable(lp->supply);
		if (ret < 0) {
			dev_err(dev, "failed to enable supply: %d\n", ret);
			return ret;
		}
	}

	if (lp->enable) {
		ret = regulator_enable(lp->enable);
		if (ret < 0) {
			dev_err(dev, "failed to enable vddio: %d\n", ret);
			goto disable_supply;
		}

		/*
		 * LP8555 datasheet says t_RESPONSE (time between VDDIO and
		 * I2C) is 1ms.
		 */
		usleep_range(1000, 2000);
	}

	i2c_set_clientdata(cl, lp);

	ret = lp855x_configure(lp);
	if (ret) {
		dev_err(dev, "device config err: %d", ret);
		goto disable_vddio;
	}

	ret = lp855x_backlight_register(lp);
	if (ret) {
		dev_err(dev, "failed to register backlight. err: %d\n", ret);
		goto disable_vddio;
	}

	ret = sysfs_create_group(&dev->kobj, &lp855x_attr_group);
	if (ret) {
		dev_err(dev, "failed to register sysfs. err: %d\n", ret);
		goto disable_vddio;
	}

	backlight_update_status(lp->bl);

	return 0;

disable_vddio:
	if (lp->enable)
		regulator_disable(lp->enable);
disable_supply:
	if (lp->supply)
		regulator_disable(lp->supply);

	return ret;
}

static void lp855x_remove(struct i2c_client *cl)
{
	struct lp855x *lp = i2c_get_clientdata(cl);

	lp->bl->props.brightness = 0;
	backlight_update_status(lp->bl);
	if (lp->enable)
		regulator_disable(lp->enable);
	if (lp->supply)
		regulator_disable(lp->supply);
	sysfs_remove_group(&lp->dev->kobj, &lp855x_attr_group);
}

static const struct of_device_id lp855x_dt_ids[] __maybe_unused = {
	{ .compatible = "ti,lp8550", },
	{ .compatible = "ti,lp8551", },
	{ .compatible = "ti,lp8552", },
	{ .compatible = "ti,lp8553", },
	{ .compatible = "ti,lp8555", },
	{ .compatible = "ti,lp8556", },
	{ .compatible = "ti,lp8557", },
	{ }
};
MODULE_DEVICE_TABLE(of, lp855x_dt_ids);

static const struct i2c_device_id lp855x_ids[] = {
	{"lp8550", LP8550},
	{"lp8551", LP8551},
	{"lp8552", LP8552},
	{"lp8553", LP8553},
	{"lp8555", LP8555},
	{"lp8556", LP8556},
	{"lp8557", LP8557},
	{ }
};
MODULE_DEVICE_TABLE(i2c, lp855x_ids);

#ifdef CONFIG_ACPI
static const struct acpi_device_id lp855x_acpi_match[] = {
	/* Xiaomi specific HID used for the LP8556 on the Mi Pad 2 */
	{ "XMCC0001", LP8556 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, lp855x_acpi_match);
#endif

static struct i2c_driver lp855x_driver = {
	.driver = {
		   .name = "lp855x",
		   .of_match_table = of_match_ptr(lp855x_dt_ids),
		   .acpi_match_table = ACPI_PTR(lp855x_acpi_match),
		   },
	.probe_new = lp855x_probe,
	.remove = lp855x_remove,
	.id_table = lp855x_ids,
};

module_i2c_driver(lp855x_driver);

MODULE_DESCRIPTION("Texas Instruments LP855x Backlight driver");
MODULE_AUTHOR("Milo Kim <milo.kim@ti.com>");
MODULE_LICENSE("GPL");
