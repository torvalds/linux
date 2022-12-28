// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Marek Vasut <marex@denx.de>
 *
 * Based on rpi_touchscreen.c by Eric Anholt <eric@anholt.net>
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

/* I2C registers of the Atmel microcontroller. */
#define REG_ID		0x80
#define REG_PORTA	0x81
#define REG_PORTB	0x82
#define REG_PORTC	0x83
#define REG_POWERON	0x85
#define REG_PWM		0x86
#define REG_ADDR_L	0x8c
#define REG_ADDR_H	0x8d
#define REG_WRITE_DATA_H	0x90
#define REG_WRITE_DATA_L	0x91

#define PA_LCD_DITHB		BIT(0)
#define PA_LCD_MODE		BIT(1)
#define PA_LCD_LR		BIT(2)
#define PA_LCD_UD		BIT(3)

#define PB_BRIDGE_PWRDNX_N	BIT(0)
#define PB_LCD_VCC_N		BIT(1)
#define PB_LCD_MAIN		BIT(7)

#define PC_LED_EN		BIT(0)
#define PC_RST_TP_N		BIT(1)
#define PC_RST_LCD_N		BIT(2)
#define PC_RST_BRIDGE_N		BIT(3)

enum gpio_signals {
	RST_BRIDGE_N,	/* TC358762 bridge reset */
	RST_TP_N,	/* Touch controller reset */
	NUM_GPIO
};

struct gpio_signal_mappings {
	unsigned int reg;
	unsigned int mask;
};

static const struct gpio_signal_mappings mappings[NUM_GPIO] = {
	[RST_BRIDGE_N] = { REG_PORTC, PC_RST_BRIDGE_N | PC_RST_LCD_N  },
	[RST_TP_N] = { REG_PORTC, PC_RST_TP_N },
};

struct attiny_lcd {
	/* lock to serialise overall accesses to the Atmel */
	struct mutex	lock;
	struct regmap	*regmap;
	bool gpio_states[NUM_GPIO];
	u8 port_states[3];

	struct gpio_chip gc;
};

static const struct regmap_config attiny_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.disable_locking = 1,
	.max_register = REG_WRITE_DATA_L,
	.cache_type = REGCACHE_RBTREE,
};

static int attiny_set_port_state(struct attiny_lcd *state, int reg, u8 val)
{
	state->port_states[reg - REG_PORTA] = val;
	return regmap_write(state->regmap, reg, val);
};

static u8 attiny_get_port_state(struct attiny_lcd *state, int reg)
{
	return state->port_states[reg - REG_PORTA];
};

static int attiny_lcd_power_enable(struct regulator_dev *rdev)
{
	struct attiny_lcd *state = rdev_get_drvdata(rdev);

	mutex_lock(&state->lock);

	/* Ensure bridge, and tp stay in reset */
	attiny_set_port_state(state, REG_PORTC, 0);
	usleep_range(5000, 10000);

	/* Default to the same orientation as the closed source
	 * firmware used for the panel.  Runtime rotation
	 * configuration will be supported using VC4's plane
	 * orientation bits.
	 */
	attiny_set_port_state(state, REG_PORTA, PA_LCD_LR);
	usleep_range(5000, 10000);
	/* Main regulator on, and power to the panel (LCD_VCC_N) */
	attiny_set_port_state(state, REG_PORTB, PB_LCD_MAIN);
	usleep_range(5000, 10000);
	/* Bring controllers out of reset */
	attiny_set_port_state(state, REG_PORTC, PC_LED_EN);

	msleep(80);

	mutex_unlock(&state->lock);

	return 0;
}

static int attiny_lcd_power_disable(struct regulator_dev *rdev)
{
	struct attiny_lcd *state = rdev_get_drvdata(rdev);

	mutex_lock(&state->lock);

	regmap_write(rdev->regmap, REG_PWM, 0);
	usleep_range(5000, 10000);

	attiny_set_port_state(state, REG_PORTA, 0);
	usleep_range(5000, 10000);
	attiny_set_port_state(state, REG_PORTB, PB_LCD_VCC_N);
	usleep_range(5000, 10000);
	attiny_set_port_state(state, REG_PORTC, 0);
	msleep(30);

	mutex_unlock(&state->lock);

	return 0;
}

static int attiny_lcd_power_is_enabled(struct regulator_dev *rdev)
{
	struct attiny_lcd *state = rdev_get_drvdata(rdev);
	unsigned int data;
	int ret, i;

	mutex_lock(&state->lock);

	for (i = 0; i < 10; i++) {
		ret = regmap_read(rdev->regmap, REG_PORTC, &data);
		if (!ret)
			break;
		usleep_range(10000, 12000);
	}

	mutex_unlock(&state->lock);

	if (ret < 0)
		return ret;

	return data & PC_RST_BRIDGE_N;
}

static const struct regulator_init_data attiny_regulator_default = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

static const struct regulator_ops attiny_regulator_ops = {
	.enable = attiny_lcd_power_enable,
	.disable = attiny_lcd_power_disable,
	.is_enabled = attiny_lcd_power_is_enabled,
};

static const struct regulator_desc attiny_regulator = {
	.name	= "tc358762-power",
	.ops	= &attiny_regulator_ops,
	.type	= REGULATOR_VOLTAGE,
	.owner	= THIS_MODULE,
};

static int attiny_update_status(struct backlight_device *bl)
{
	struct attiny_lcd *state = bl_get_data(bl);
	struct regmap *regmap = state->regmap;
	int brightness = backlight_get_brightness(bl);
	int ret, i;

	mutex_lock(&state->lock);

	for (i = 0; i < 10; i++) {
		ret = regmap_write(regmap, REG_PWM, brightness);
		if (!ret)
			break;
	}

	mutex_unlock(&state->lock);

	return ret;
}

static const struct backlight_ops attiny_bl = {
	.update_status	= attiny_update_status,
};

static int attiny_gpio_get_direction(struct gpio_chip *gc, unsigned int off)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static void attiny_gpio_set(struct gpio_chip *gc, unsigned int off, int val)
{
	struct attiny_lcd *state = gpiochip_get_data(gc);
	u8 last_val;

	if (off >= NUM_GPIO)
		return;

	mutex_lock(&state->lock);

	last_val = attiny_get_port_state(state, mappings[off].reg);
	if (val)
		last_val |= mappings[off].mask;
	else
		last_val &= ~mappings[off].mask;

	attiny_set_port_state(state, mappings[off].reg, last_val);

	if (off == RST_BRIDGE_N && val) {
		usleep_range(5000, 8000);
		regmap_write(state->regmap, REG_ADDR_H, 0x04);
		usleep_range(5000, 8000);
		regmap_write(state->regmap, REG_ADDR_L, 0x7c);
		usleep_range(5000, 8000);
		regmap_write(state->regmap, REG_WRITE_DATA_H, 0x00);
		usleep_range(5000, 8000);
		regmap_write(state->regmap, REG_WRITE_DATA_L, 0x00);

		msleep(100);
	}

	mutex_unlock(&state->lock);
}

static int attiny_i2c_read(struct i2c_client *client, u8 reg, unsigned int *buf)
{
	struct i2c_msg msgs[1];
	u8 addr_buf[1] = { reg };
	u8 data_buf[1] = { 0, };
	int ret;

	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = ARRAY_SIZE(addr_buf);
	msgs[0].buf = addr_buf;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	usleep_range(5000, 10000);

	/* Read data from register */
	msgs[0].addr = client->addr;
	msgs[0].flags = I2C_M_RD;
	msgs[0].len = 1;
	msgs[0].buf = data_buf;

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*buf = data_buf[0];
	return 0;
}

/*
 * I2C driver interface functions
 */
static int attiny_i2c_probe(struct i2c_client *i2c)
{
	struct backlight_properties props = { };
	struct regulator_config config = { };
	struct backlight_device *bl;
	struct regulator_dev *rdev;
	struct attiny_lcd *state;
	struct regmap *regmap;
	unsigned int data;
	int ret;

	state = devm_kzalloc(&i2c->dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	mutex_init(&state->lock);
	i2c_set_clientdata(i2c, state);

	regmap = devm_regmap_init_i2c(i2c, &attiny_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		goto error;
	}

	ret = attiny_i2c_read(i2c, REG_ID, &data);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read REG_ID reg: %d\n", ret);
		goto error;
	}

	switch (data) {
	case 0xde: /* ver 1 */
	case 0xc3: /* ver 2 */
		break;
	default:
		dev_err(&i2c->dev, "Unknown Atmel firmware revision: 0x%02x\n", data);
		ret = -ENODEV;
		goto error;
	}

	regmap_write(regmap, REG_POWERON, 0);
	msleep(30);
	regmap_write(regmap, REG_PWM, 0);

	config.dev = &i2c->dev;
	config.regmap = regmap;
	config.of_node = i2c->dev.of_node;
	config.init_data = &attiny_regulator_default;
	config.driver_data = state;

	rdev = devm_regulator_register(&i2c->dev, &attiny_regulator, &config);
	if (IS_ERR(rdev)) {
		dev_err(&i2c->dev, "Failed to register ATTINY regulator\n");
		ret = PTR_ERR(rdev);
		goto error;
	}

	props.type = BACKLIGHT_RAW;
	props.max_brightness = 0xff;

	state->regmap = regmap;

	bl = devm_backlight_device_register(&i2c->dev, dev_name(&i2c->dev),
					    &i2c->dev, state, &attiny_bl,
					    &props);
	if (IS_ERR(bl)) {
		ret = PTR_ERR(bl);
		goto error;
	}

	bl->props.brightness = 0xff;

	state->gc.parent = &i2c->dev;
	state->gc.label = i2c->name;
	state->gc.owner = THIS_MODULE;
	state->gc.base = -1;
	state->gc.ngpio = NUM_GPIO;

	state->gc.set = attiny_gpio_set;
	state->gc.get_direction = attiny_gpio_get_direction;
	state->gc.can_sleep = true;

	ret = devm_gpiochip_add_data(&i2c->dev, &state->gc, state);
	if (ret) {
		dev_err(&i2c->dev, "Failed to create gpiochip: %d\n", ret);
		goto error;
	}

	return 0;

error:
	mutex_destroy(&state->lock);

	return ret;
}

static void attiny_i2c_remove(struct i2c_client *client)
{
	struct attiny_lcd *state = i2c_get_clientdata(client);

	mutex_destroy(&state->lock);
}

static const struct of_device_id attiny_dt_ids[] = {
	{ .compatible = "raspberrypi,7inch-touchscreen-panel-regulator" },
	{},
};
MODULE_DEVICE_TABLE(of, attiny_dt_ids);

static struct i2c_driver attiny_regulator_driver = {
	.driver = {
		.name = "rpi_touchscreen_attiny",
		.of_match_table = of_match_ptr(attiny_dt_ids),
	},
	.probe_new = attiny_i2c_probe,
	.remove	= attiny_i2c_remove,
};

module_i2c_driver(attiny_regulator_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Regulator device driver for Raspberry Pi 7-inch touchscreen");
MODULE_LICENSE("GPL v2");
