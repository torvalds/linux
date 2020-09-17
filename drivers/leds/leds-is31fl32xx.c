// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for ISSI IS31FL32xx family of I2C LED controllers
 *
 * Copyright 2015 Allworx Corp.
 *
 * Datasheets:
 *   http://www.issi.com/US/product-analog-fxled-driver.shtml
 *   http://www.si-en.com/product.asp?parentid=890
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>

/* Used to indicate a device has no such register */
#define IS31FL32XX_REG_NONE 0xFF

/* Software Shutdown bit in Shutdown Register */
#define IS31FL32XX_SHUTDOWN_SSD_ENABLE  0
#define IS31FL32XX_SHUTDOWN_SSD_DISABLE BIT(0)

/* IS31FL3216 has a number of unique registers */
#define IS31FL3216_CONFIG_REG 0x00
#define IS31FL3216_LIGHTING_EFFECT_REG 0x03
#define IS31FL3216_CHANNEL_CONFIG_REG 0x04

/* Software Shutdown bit in 3216 Config Register */
#define IS31FL3216_CONFIG_SSD_ENABLE  BIT(7)
#define IS31FL3216_CONFIG_SSD_DISABLE 0

struct is31fl32xx_priv;
struct is31fl32xx_led_data {
	struct led_classdev cdev;
	u8 channel; /* 1-based, max priv->cdef->channels */
	struct is31fl32xx_priv *priv;
};

struct is31fl32xx_priv {
	const struct is31fl32xx_chipdef *cdef;
	struct i2c_client *client;
	unsigned int num_leds;
	struct is31fl32xx_led_data leds[];
};

/**
 * struct is31fl32xx_chipdef - chip-specific attributes
 * @channels            : Number of LED channels
 * @shutdown_reg        : address of Shutdown register (optional)
 * @pwm_update_reg      : address of PWM Update register
 * @global_control_reg  : address of Global Control register (optional)
 * @reset_reg           : address of Reset register (optional)
 * @pwm_register_base   : address of first PWM register
 * @pwm_registers_reversed: : true if PWM registers count down instead of up
 * @led_control_register_base : address of first LED control register (optional)
 * @enable_bits_per_led_control_register: number of LEDs enable bits in each
 * @reset_func:         : pointer to reset function
 *
 * For all optional register addresses, the sentinel value %IS31FL32XX_REG_NONE
 * indicates that this chip has no such register.
 *
 * If non-NULL, @reset_func will be called during probing to set all
 * necessary registers to a known initialization state. This is needed
 * for chips that do not have a @reset_reg.
 *
 * @enable_bits_per_led_control_register must be >=1 if
 * @led_control_register_base != %IS31FL32XX_REG_NONE.
 */
struct is31fl32xx_chipdef {
	u8	channels;
	u8	shutdown_reg;
	u8	pwm_update_reg;
	u8	global_control_reg;
	u8	reset_reg;
	u8	pwm_register_base;
	bool	pwm_registers_reversed;
	u8	led_control_register_base;
	u8	enable_bits_per_led_control_register;
	int (*reset_func)(struct is31fl32xx_priv *priv);
	int (*sw_shutdown_func)(struct is31fl32xx_priv *priv, bool enable);
};

static const struct is31fl32xx_chipdef is31fl3236_cdef = {
	.channels				= 36,
	.shutdown_reg				= 0x00,
	.pwm_update_reg				= 0x25,
	.global_control_reg			= 0x4a,
	.reset_reg				= 0x4f,
	.pwm_register_base			= 0x01,
	.led_control_register_base		= 0x26,
	.enable_bits_per_led_control_register	= 1,
};

static const struct is31fl32xx_chipdef is31fl3235_cdef = {
	.channels				= 28,
	.shutdown_reg				= 0x00,
	.pwm_update_reg				= 0x25,
	.global_control_reg			= 0x4a,
	.reset_reg				= 0x4f,
	.pwm_register_base			= 0x05,
	.led_control_register_base		= 0x2a,
	.enable_bits_per_led_control_register	= 1,
};

static const struct is31fl32xx_chipdef is31fl3218_cdef = {
	.channels				= 18,
	.shutdown_reg				= 0x00,
	.pwm_update_reg				= 0x16,
	.global_control_reg			= IS31FL32XX_REG_NONE,
	.reset_reg				= 0x17,
	.pwm_register_base			= 0x01,
	.led_control_register_base		= 0x13,
	.enable_bits_per_led_control_register	= 6,
};

static int is31fl3216_reset(struct is31fl32xx_priv *priv);
static int is31fl3216_software_shutdown(struct is31fl32xx_priv *priv,
					bool enable);
static const struct is31fl32xx_chipdef is31fl3216_cdef = {
	.channels				= 16,
	.shutdown_reg				= IS31FL32XX_REG_NONE,
	.pwm_update_reg				= 0xB0,
	.global_control_reg			= IS31FL32XX_REG_NONE,
	.reset_reg				= IS31FL32XX_REG_NONE,
	.pwm_register_base			= 0x10,
	.pwm_registers_reversed			= true,
	.led_control_register_base		= 0x01,
	.enable_bits_per_led_control_register	= 8,
	.reset_func				= is31fl3216_reset,
	.sw_shutdown_func			= is31fl3216_software_shutdown,
};

static int is31fl32xx_write(struct is31fl32xx_priv *priv, u8 reg, u8 val)
{
	int ret;

	dev_dbg(&priv->client->dev, "writing register 0x%02X=0x%02X", reg, val);

	ret =  i2c_smbus_write_byte_data(priv->client, reg, val);
	if (ret) {
		dev_err(&priv->client->dev,
			"register write to 0x%02X failed (error %d)",
			reg, ret);
	}
	return ret;
}

/*
 * Custom reset function for IS31FL3216 because it does not have a RESET
 * register the way that the other IS31FL32xx chips do. We don't bother
 * writing the GPIO and animation registers, because the registers we
 * do write ensure those will have no effect.
 */
static int is31fl3216_reset(struct is31fl32xx_priv *priv)
{
	unsigned int i;
	int ret;

	ret = is31fl32xx_write(priv, IS31FL3216_CONFIG_REG,
			       IS31FL3216_CONFIG_SSD_ENABLE);
	if (ret)
		return ret;
	for (i = 0; i < priv->cdef->channels; i++) {
		ret = is31fl32xx_write(priv, priv->cdef->pwm_register_base+i,
				       0x00);
		if (ret)
			return ret;
	}
	ret = is31fl32xx_write(priv, priv->cdef->pwm_update_reg, 0);
	if (ret)
		return ret;
	ret = is31fl32xx_write(priv, IS31FL3216_LIGHTING_EFFECT_REG, 0x00);
	if (ret)
		return ret;
	ret = is31fl32xx_write(priv, IS31FL3216_CHANNEL_CONFIG_REG, 0x00);
	if (ret)
		return ret;

	return 0;
}

/*
 * Custom Software-Shutdown function for IS31FL3216 because it does not have
 * a SHUTDOWN register the way that the other IS31FL32xx chips do.
 * We don't bother doing a read/modify/write on the CONFIG register because
 * we only ever use a value of '0' for the other fields in that register.
 */
static int is31fl3216_software_shutdown(struct is31fl32xx_priv *priv,
					bool enable)
{
	u8 value = enable ? IS31FL3216_CONFIG_SSD_ENABLE :
			    IS31FL3216_CONFIG_SSD_DISABLE;

	return is31fl32xx_write(priv, IS31FL3216_CONFIG_REG, value);
}

/*
 * NOTE: A mutex is not needed in this function because:
 * - All referenced data is read-only after probe()
 * - The I2C core has a mutex on to protect the bus
 * - There are no read/modify/write operations
 * - Intervening operations between the write of the PWM register
 *   and the Update register are harmless.
 *
 * Example:
 *	PWM_REG_1 write 16
 *	UPDATE_REG write 0
 *	PWM_REG_2 write 128
 *	UPDATE_REG write 0
 *   vs:
 *	PWM_REG_1 write 16
 *	PWM_REG_2 write 128
 *	UPDATE_REG write 0
 *	UPDATE_REG write 0
 * are equivalent. Poking the Update register merely applies all PWM
 * register writes up to that point.
 */
static int is31fl32xx_brightness_set(struct led_classdev *led_cdev,
				     enum led_brightness brightness)
{
	const struct is31fl32xx_led_data *led_data =
		container_of(led_cdev, struct is31fl32xx_led_data, cdev);
	const struct is31fl32xx_chipdef *cdef = led_data->priv->cdef;
	u8 pwm_register_offset;
	int ret;

	dev_dbg(led_cdev->dev, "%s: %d\n", __func__, brightness);

	/* NOTE: led_data->channel is 1-based */
	if (cdef->pwm_registers_reversed)
		pwm_register_offset = cdef->channels - led_data->channel;
	else
		pwm_register_offset = led_data->channel - 1;

	ret = is31fl32xx_write(led_data->priv,
			       cdef->pwm_register_base + pwm_register_offset,
			       brightness);
	if (ret)
		return ret;

	return is31fl32xx_write(led_data->priv, cdef->pwm_update_reg, 0);
}

static int is31fl32xx_reset_regs(struct is31fl32xx_priv *priv)
{
	const struct is31fl32xx_chipdef *cdef = priv->cdef;
	int ret;

	if (cdef->reset_reg != IS31FL32XX_REG_NONE) {
		ret = is31fl32xx_write(priv, cdef->reset_reg, 0);
		if (ret)
			return ret;
	}

	if (cdef->reset_func)
		return cdef->reset_func(priv);

	return 0;
}

static int is31fl32xx_software_shutdown(struct is31fl32xx_priv *priv,
					bool enable)
{
	const struct is31fl32xx_chipdef *cdef = priv->cdef;
	int ret;

	if (cdef->shutdown_reg != IS31FL32XX_REG_NONE) {
		u8 value = enable ? IS31FL32XX_SHUTDOWN_SSD_ENABLE :
				    IS31FL32XX_SHUTDOWN_SSD_DISABLE;
		ret = is31fl32xx_write(priv, cdef->shutdown_reg, value);
		if (ret)
			return ret;
	}

	if (cdef->sw_shutdown_func)
		return cdef->sw_shutdown_func(priv, enable);

	return 0;
}

static int is31fl32xx_init_regs(struct is31fl32xx_priv *priv)
{
	const struct is31fl32xx_chipdef *cdef = priv->cdef;
	int ret;

	ret = is31fl32xx_reset_regs(priv);
	if (ret)
		return ret;

	/*
	 * Set enable bit for all channels.
	 * We will control state with PWM registers alone.
	 */
	if (cdef->led_control_register_base != IS31FL32XX_REG_NONE) {
		u8 value =
		    GENMASK(cdef->enable_bits_per_led_control_register-1, 0);
		u8 num_regs = cdef->channels /
				cdef->enable_bits_per_led_control_register;
		int i;

		for (i = 0; i < num_regs; i++) {
			ret = is31fl32xx_write(priv,
					       cdef->led_control_register_base+i,
					       value);
			if (ret)
				return ret;
		}
	}

	ret = is31fl32xx_software_shutdown(priv, false);
	if (ret)
		return ret;

	if (cdef->global_control_reg != IS31FL32XX_REG_NONE) {
		ret = is31fl32xx_write(priv, cdef->global_control_reg, 0x00);
		if (ret)
			return ret;
	}

	return 0;
}

static int is31fl32xx_parse_child_dt(const struct device *dev,
				     const struct device_node *child,
				     struct is31fl32xx_led_data *led_data)
{
	struct led_classdev *cdev = &led_data->cdev;
	int ret = 0;
	u32 reg;

	if (of_property_read_string(child, "label", &cdev->name))
		cdev->name = child->name;

	ret = of_property_read_u32(child, "reg", &reg);
	if (ret || reg < 1 || reg > led_data->priv->cdef->channels) {
		dev_err(dev,
			"Child node %pOF does not have a valid reg property\n",
			child);
		return -EINVAL;
	}
	led_data->channel = reg;

	of_property_read_string(child, "linux,default-trigger",
				&cdev->default_trigger);

	cdev->brightness_set_blocking = is31fl32xx_brightness_set;

	return 0;
}

static struct is31fl32xx_led_data *is31fl32xx_find_led_data(
					struct is31fl32xx_priv *priv,
					u8 channel)
{
	size_t i;

	for (i = 0; i < priv->num_leds; i++) {
		if (priv->leds[i].channel == channel)
			return &priv->leds[i];
	}

	return NULL;
}

static int is31fl32xx_parse_dt(struct device *dev,
			       struct is31fl32xx_priv *priv)
{
	struct device_node *child;
	int ret = 0;

	for_each_child_of_node(dev->of_node, child) {
		struct is31fl32xx_led_data *led_data =
			&priv->leds[priv->num_leds];
		const struct is31fl32xx_led_data *other_led_data;

		led_data->priv = priv;

		ret = is31fl32xx_parse_child_dt(dev, child, led_data);
		if (ret)
			goto err;

		/* Detect if channel is already in use by another child */
		other_led_data = is31fl32xx_find_led_data(priv,
							  led_data->channel);
		if (other_led_data) {
			dev_err(dev,
				"%s and %s both attempting to use channel %d\n",
				led_data->cdev.name,
				other_led_data->cdev.name,
				led_data->channel);
			goto err;
		}

		ret = devm_led_classdev_register(dev, &led_data->cdev);
		if (ret) {
			dev_err(dev, "failed to register PWM led for %s: %d\n",
				led_data->cdev.name, ret);
			goto err;
		}

		priv->num_leds++;
	}

	return 0;

err:
	of_node_put(child);
	return ret;
}

static const struct of_device_id of_is31fl32xx_match[] = {
	{ .compatible = "issi,is31fl3236", .data = &is31fl3236_cdef, },
	{ .compatible = "issi,is31fl3235", .data = &is31fl3235_cdef, },
	{ .compatible = "issi,is31fl3218", .data = &is31fl3218_cdef, },
	{ .compatible = "si-en,sn3218",    .data = &is31fl3218_cdef, },
	{ .compatible = "issi,is31fl3216", .data = &is31fl3216_cdef, },
	{ .compatible = "si-en,sn3216",    .data = &is31fl3216_cdef, },
	{},
};

MODULE_DEVICE_TABLE(of, of_is31fl32xx_match);

static int is31fl32xx_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	const struct is31fl32xx_chipdef *cdef;
	struct device *dev = &client->dev;
	struct is31fl32xx_priv *priv;
	int count;
	int ret = 0;

	cdef = device_get_match_data(dev);

	count = of_get_child_count(dev->of_node);
	if (!count)
		return -EINVAL;

	priv = devm_kzalloc(dev, struct_size(priv, leds, count),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	priv->cdef = cdef;
	i2c_set_clientdata(client, priv);

	ret = is31fl32xx_init_regs(priv);
	if (ret)
		return ret;

	ret = is31fl32xx_parse_dt(dev, priv);
	if (ret)
		return ret;

	return 0;
}

static int is31fl32xx_remove(struct i2c_client *client)
{
	struct is31fl32xx_priv *priv = i2c_get_clientdata(client);

	return is31fl32xx_reset_regs(priv);
}

/*
 * i2c-core (and modalias) requires that id_table be properly filled,
 * even though it is not used for DeviceTree based instantiation.
 */
static const struct i2c_device_id is31fl32xx_id[] = {
	{ "is31fl3236" },
	{ "is31fl3235" },
	{ "is31fl3218" },
	{ "sn3218" },
	{ "is31fl3216" },
	{ "sn3216" },
	{},
};

MODULE_DEVICE_TABLE(i2c, is31fl32xx_id);

static struct i2c_driver is31fl32xx_driver = {
	.driver = {
		.name	= "is31fl32xx",
		.of_match_table = of_is31fl32xx_match,
	},
	.probe		= is31fl32xx_probe,
	.remove		= is31fl32xx_remove,
	.id_table	= is31fl32xx_id,
};

module_i2c_driver(is31fl32xx_driver);

MODULE_AUTHOR("David Rivshin <drivshin@allworx.com>");
MODULE_DESCRIPTION("ISSI IS31FL32xx LED driver");
MODULE_LICENSE("GPL v2");
