// SPDX-License-Identifier: GPL-2.0-only
/*
 * Backlight driver for ArcticSand ARC_X_C_0N_0N Devices
 *
 * Copyright 2016 ArcticSand, Inc.
 * Author : Brian Dodge <bdodge@arcticsand.com>
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>

enum arcxcnn_chip_id {
	ARC2C0608
};

/**
 * struct arcxcnn_platform_data
 * @name		: Backlight driver name (NULL will use default)
 * @initial_brightness	: initial value of backlight brightness
 * @leden		: initial LED string enables, upper bit is global on/off
 * @led_config_0	: fading speed (period between intensity steps)
 * @led_config_1	: misc settings, see datasheet
 * @dim_freq		: pwm dimming frequency if in pwm mode
 * @comp_config		: misc config, see datasheet
 * @filter_config	: RC/PWM filter config, see datasheet
 * @trim_config		: full scale current trim, see datasheet
 */
struct arcxcnn_platform_data {
	const char *name;
	u16 initial_brightness;
	u8	leden;
	u8	led_config_0;
	u8	led_config_1;
	u8	dim_freq;
	u8	comp_config;
	u8	filter_config;
	u8	trim_config;
};

#define ARCXCNN_CMD		0x00	/* Command Register */
#define ARCXCNN_CMD_STDBY	0x80	/*   I2C Standby */
#define ARCXCNN_CMD_RESET	0x40	/*   Reset */
#define ARCXCNN_CMD_BOOST	0x10	/*   Boost */
#define ARCXCNN_CMD_OVP_MASK	0x0C	/*   --- Over Voltage Threshold */
#define ARCXCNN_CMD_OVP_XXV	0x0C	/*   <rsvrd> Over Voltage Threshold */
#define ARCXCNN_CMD_OVP_20V	0x08	/*   20v Over Voltage Threshold */
#define ARCXCNN_CMD_OVP_24V	0x04	/*   24v Over Voltage Threshold */
#define ARCXCNN_CMD_OVP_31V	0x00	/*   31.4v Over Voltage Threshold */
#define ARCXCNN_CMD_EXT_COMP	0x01	/*   part (0) or full (1) ext. comp */

#define ARCXCNN_CONFIG		0x01	/* Configuration */
#define ARCXCNN_STATUS1		0x02	/* Status 1 */
#define ARCXCNN_STATUS2		0x03	/* Status 2 */
#define ARCXCNN_FADECTRL	0x04	/* Fading Control */
#define ARCXCNN_ILED_CONFIG	0x05	/* ILED Configuration */
#define ARCXCNN_ILED_DIM_PWM	0x00	/*   config dim mode pwm */
#define ARCXCNN_ILED_DIM_INT	0x04	/*   config dim mode internal */
#define ARCXCNN_LEDEN		0x06	/* LED Enable Register */
#define ARCXCNN_LEDEN_ISETEXT	0x80	/*   Full-scale current set extern */
#define ARCXCNN_LEDEN_MASK	0x3F	/*   LED string enables mask */
#define ARCXCNN_LEDEN_BITS	0x06	/*   Bits of LED string enables */
#define ARCXCNN_LEDEN_LED1	0x01
#define ARCXCNN_LEDEN_LED2	0x02
#define ARCXCNN_LEDEN_LED3	0x04
#define ARCXCNN_LEDEN_LED4	0x08
#define ARCXCNN_LEDEN_LED5	0x10
#define ARCXCNN_LEDEN_LED6	0x20

#define ARCXCNN_WLED_ISET_LSB	0x07	/* LED ISET LSB (in upper nibble) */
#define ARCXCNN_WLED_ISET_LSB_SHIFT 0x04  /* ISET LSB Left Shift */
#define ARCXCNN_WLED_ISET_MSB	0x08	/* LED ISET MSB (8 bits) */

#define ARCXCNN_DIMFREQ		0x09
#define ARCXCNN_COMP_CONFIG	0x0A
#define ARCXCNN_FILT_CONFIG	0x0B
#define ARCXCNN_IMAXTUNE	0x0C
#define ARCXCNN_ID_MSB		0x1E
#define ARCXCNN_ID_LSB		0x1F

#define MAX_BRIGHTNESS		4095
#define INIT_BRIGHT		60

struct arcxcnn {
	struct i2c_client *client;
	struct backlight_device *bl;
	struct device *dev;
	struct arcxcnn_platform_data *pdata;
};

static int arcxcnn_update_field(struct arcxcnn *lp, u8 reg, u8 mask, u8 data)
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

	return i2c_smbus_write_byte_data(lp->client, reg, tmp);
}

static int arcxcnn_set_brightness(struct arcxcnn *lp, u32 brightness)
{
	int ret;
	u8 val;

	/* lower nibble of brightness goes in upper nibble of LSB register */
	val = (brightness & 0xF) << ARCXCNN_WLED_ISET_LSB_SHIFT;
	ret = i2c_smbus_write_byte_data(lp->client,
		ARCXCNN_WLED_ISET_LSB, val);
	if (ret < 0)
		return ret;

	/* remaining 8 bits of brightness go in MSB register */
	val = (brightness >> 4);
	return i2c_smbus_write_byte_data(lp->client,
		ARCXCNN_WLED_ISET_MSB, val);
}

static int arcxcnn_bl_update_status(struct backlight_device *bl)
{
	struct arcxcnn *lp = bl_get_data(bl);
	u32 brightness = bl->props.brightness;
	int ret;

	if (bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;

	ret = arcxcnn_set_brightness(lp, brightness);
	if (ret)
		return ret;

	/* set power-on/off/save modes */
	return arcxcnn_update_field(lp, ARCXCNN_CMD, ARCXCNN_CMD_STDBY,
		(bl->props.power == 0) ? 0 : ARCXCNN_CMD_STDBY);
}

static const struct backlight_ops arcxcnn_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = arcxcnn_bl_update_status,
};

static int arcxcnn_backlight_register(struct arcxcnn *lp)
{
	struct backlight_properties *props;
	const char *name = lp->pdata->name ? : "arctic_bl";

	props = devm_kzalloc(lp->dev, sizeof(*props), GFP_KERNEL);
	if (!props)
		return -ENOMEM;

	props->type = BACKLIGHT_PLATFORM;
	props->max_brightness = MAX_BRIGHTNESS;

	if (lp->pdata->initial_brightness > props->max_brightness)
		lp->pdata->initial_brightness = props->max_brightness;

	props->brightness = lp->pdata->initial_brightness;

	lp->bl = devm_backlight_device_register(lp->dev, name, lp->dev, lp,
				       &arcxcnn_bl_ops, props);
	return PTR_ERR_OR_ZERO(lp->bl);
}

static void arcxcnn_parse_dt(struct arcxcnn *lp)
{
	struct device *dev = lp->dev;
	struct device_node *node = dev->of_node;
	u32 prog_val, num_entry, entry, sources[ARCXCNN_LEDEN_BITS];
	int ret;

	/* device tree entry isn't required, defaults are OK */
	if (!node)
		return;

	ret = of_property_read_string(node, "label", &lp->pdata->name);
	if (ret < 0)
		lp->pdata->name = NULL;

	ret = of_property_read_u32(node, "default-brightness", &prog_val);
	if (ret == 0)
		lp->pdata->initial_brightness = prog_val;

	ret = of_property_read_u32(node, "arc,led-config-0", &prog_val);
	if (ret == 0)
		lp->pdata->led_config_0 = (u8)prog_val;

	ret = of_property_read_u32(node, "arc,led-config-1", &prog_val);
	if (ret == 0)
		lp->pdata->led_config_1 = (u8)prog_val;

	ret = of_property_read_u32(node, "arc,dim-freq", &prog_val);
	if (ret == 0)
		lp->pdata->dim_freq = (u8)prog_val;

	ret = of_property_read_u32(node, "arc,comp-config", &prog_val);
	if (ret == 0)
		lp->pdata->comp_config = (u8)prog_val;

	ret = of_property_read_u32(node, "arc,filter-config", &prog_val);
	if (ret == 0)
		lp->pdata->filter_config = (u8)prog_val;

	ret = of_property_read_u32(node, "arc,trim-config", &prog_val);
	if (ret == 0)
		lp->pdata->trim_config = (u8)prog_val;

	ret = of_property_count_u32_elems(node, "led-sources");
	if (ret < 0) {
		lp->pdata->leden = ARCXCNN_LEDEN_MASK; /* all on is default */
	} else {
		num_entry = ret;
		if (num_entry > ARCXCNN_LEDEN_BITS)
			num_entry = ARCXCNN_LEDEN_BITS;

		ret = of_property_read_u32_array(node, "led-sources", sources,
					num_entry);
		if (ret < 0) {
			dev_err(dev, "led-sources node is invalid.\n");
			return;
		}

		lp->pdata->leden = 0;

		/* for each enable in source, set bit in led enable */
		for (entry = 0; entry < num_entry; entry++) {
			u8 onbit = 1 << sources[entry];

			lp->pdata->leden |= onbit;
		}
	}
}

static int arcxcnn_probe(struct i2c_client *cl, const struct i2c_device_id *id)
{
	struct arcxcnn *lp;
	int ret;

	if (!i2c_check_functionality(cl->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	lp = devm_kzalloc(&cl->dev, sizeof(*lp), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	lp->client = cl;
	lp->dev = &cl->dev;
	lp->pdata = dev_get_platdata(&cl->dev);

	/* reset the device */
	ret = i2c_smbus_write_byte_data(lp->client,
		ARCXCNN_CMD, ARCXCNN_CMD_RESET);
	if (ret)
		goto probe_err;

	if (!lp->pdata) {
		lp->pdata = devm_kzalloc(lp->dev,
				sizeof(*lp->pdata), GFP_KERNEL);
		if (!lp->pdata)
			return -ENOMEM;

		/* Setup defaults based on power-on defaults */
		lp->pdata->name = NULL;
		lp->pdata->initial_brightness = INIT_BRIGHT;
		lp->pdata->leden = ARCXCNN_LEDEN_MASK;

		lp->pdata->led_config_0 = i2c_smbus_read_byte_data(
			lp->client, ARCXCNN_FADECTRL);

		lp->pdata->led_config_1 = i2c_smbus_read_byte_data(
			lp->client, ARCXCNN_ILED_CONFIG);
		/* insure dim mode is not default pwm */
		lp->pdata->led_config_1 |= ARCXCNN_ILED_DIM_INT;

		lp->pdata->dim_freq = i2c_smbus_read_byte_data(
			lp->client, ARCXCNN_DIMFREQ);

		lp->pdata->comp_config = i2c_smbus_read_byte_data(
			lp->client, ARCXCNN_COMP_CONFIG);

		lp->pdata->filter_config = i2c_smbus_read_byte_data(
			lp->client, ARCXCNN_FILT_CONFIG);

		lp->pdata->trim_config = i2c_smbus_read_byte_data(
			lp->client, ARCXCNN_IMAXTUNE);

		if (IS_ENABLED(CONFIG_OF))
			arcxcnn_parse_dt(lp);
	}

	i2c_set_clientdata(cl, lp);

	/* constrain settings to what is possible */
	if (lp->pdata->initial_brightness > MAX_BRIGHTNESS)
		lp->pdata->initial_brightness = MAX_BRIGHTNESS;

	/* set initial brightness */
	ret = arcxcnn_set_brightness(lp, lp->pdata->initial_brightness);
	if (ret)
		goto probe_err;

	/* set other register values directly */
	ret = i2c_smbus_write_byte_data(lp->client, ARCXCNN_FADECTRL,
		lp->pdata->led_config_0);
	if (ret)
		goto probe_err;

	ret = i2c_smbus_write_byte_data(lp->client, ARCXCNN_ILED_CONFIG,
		lp->pdata->led_config_1);
	if (ret)
		goto probe_err;

	ret = i2c_smbus_write_byte_data(lp->client, ARCXCNN_DIMFREQ,
		lp->pdata->dim_freq);
	if (ret)
		goto probe_err;

	ret = i2c_smbus_write_byte_data(lp->client, ARCXCNN_COMP_CONFIG,
		lp->pdata->comp_config);
	if (ret)
		goto probe_err;

	ret = i2c_smbus_write_byte_data(lp->client, ARCXCNN_FILT_CONFIG,
		lp->pdata->filter_config);
	if (ret)
		goto probe_err;

	ret = i2c_smbus_write_byte_data(lp->client, ARCXCNN_IMAXTUNE,
		lp->pdata->trim_config);
	if (ret)
		goto probe_err;

	/* set initial LED Enables */
	arcxcnn_update_field(lp, ARCXCNN_LEDEN,
		ARCXCNN_LEDEN_MASK, lp->pdata->leden);

	ret = arcxcnn_backlight_register(lp);
	if (ret)
		goto probe_register_err;

	backlight_update_status(lp->bl);

	return 0;

probe_register_err:
	dev_err(lp->dev,
		"failed to register backlight.\n");

probe_err:
	dev_err(lp->dev,
		"failure ret: %d\n", ret);
	return ret;
}

static int arcxcnn_remove(struct i2c_client *cl)
{
	struct arcxcnn *lp = i2c_get_clientdata(cl);

	/* disable all strings (ignore errors) */
	i2c_smbus_write_byte_data(lp->client,
		ARCXCNN_LEDEN, 0x00);
	/* reset the device (ignore errors) */
	i2c_smbus_write_byte_data(lp->client,
		ARCXCNN_CMD, ARCXCNN_CMD_RESET);

	lp->bl->props.brightness = 0;

	backlight_update_status(lp->bl);

	return 0;
}

static const struct of_device_id arcxcnn_dt_ids[] = {
	{ .compatible = "arc,arc2c0608" },
	{ }
};
MODULE_DEVICE_TABLE(of, arcxcnn_dt_ids);

static const struct i2c_device_id arcxcnn_ids[] = {
	{"arc2c0608", ARC2C0608},
	{ }
};
MODULE_DEVICE_TABLE(i2c, arcxcnn_ids);

static struct i2c_driver arcxcnn_driver = {
	.driver = {
		.name = "arcxcnn_bl",
		.of_match_table = of_match_ptr(arcxcnn_dt_ids),
	},
	.probe = arcxcnn_probe,
	.remove = arcxcnn_remove,
	.id_table = arcxcnn_ids,
};
module_i2c_driver(arcxcnn_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Brian Dodge <bdodge@arcticsand.com>");
MODULE_DESCRIPTION("ARCXCNN Backlight driver");
