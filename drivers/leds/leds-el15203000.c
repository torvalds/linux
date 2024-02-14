// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Crane Merchandising Systems. All rights reserved.
// Copyright (C) 2019 Oleh Kravchenko <oleg@kaa.org.ua>

#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>

/*
 * EL15203000 SPI protocol description:
 * +-----+---------+
 * | LED | COMMAND |
 * +-----+---------+
 * |  1  |    1    |
 * +-----+---------+
 * (*) LEDs MCU board expects 20 msec delay per byte.
 *
 * LEDs:
 * +----------+--------------+-------------------------------------------+
 * |    ID    |     NAME     |         DESCRIPTION                       |
 * +----------+--------------+-------------------------------------------+
 * | 'P' 0x50 |     Pipe     | Consists from 5 LEDs, controlled by board |
 * +----------+--------------+-------------------------------------------+
 * | 'S' 0x53 | Screen frame | Light tube around the screen              |
 * +----------+--------------+-------------------------------------------+
 * | 'V' 0x56 | Vending area | Highlights a cup of coffee                |
 * +----------+--------------+-------------------------------------------+
 *
 * COMMAND:
 * +----------+-----------------+--------------+--------------+
 * |  VALUES  |       PIPE      | SCREEN FRAME | VENDING AREA |
 * +----------+-----------------+--------------+--------------+
 * | '0' 0x30 |                      Off                      |
 * +----------+-----------------------------------------------+
 * | '1' 0x31 |                      On                       |
 * +----------+-----------------+--------------+--------------+
 * | '2' 0x32 |     Cascade     |   Breathing  |
 * +----------+-----------------+--------------+
 * | '3' 0x33 | Inverse cascade |
 * +----------+-----------------+
 * | '4' 0x34 |     Bounce      |
 * +----------+-----------------+
 * | '5' 0x35 | Inverse bounce  |
 * +----------+-----------------+
 */

/* EL15203000 default settings */
#define EL_FW_DELAY_USEC	20000ul
#define EL_PATTERN_DELAY_MSEC	800u
#define EL_PATTERN_LEN		10u
#define EL_PATTERN_HALF_LEN	(EL_PATTERN_LEN / 2)

enum el15203000_command {
	/* for all LEDs */
	EL_OFF			= '0',
	EL_ON			= '1',

	/* for Screen LED */
	EL_SCREEN_BREATHING	= '2',

	/* for Pipe LED */
	EL_PIPE_CASCADE		= '2',
	EL_PIPE_INV_CASCADE	= '3',
	EL_PIPE_BOUNCE		= '4',
	EL_PIPE_INV_BOUNCE	= '5',
};

struct el15203000_led {
	struct led_classdev	ldev;
	struct el15203000	*priv;
	u32			reg;
};

struct el15203000 {
	struct device		*dev;
	struct mutex		lock;
	struct spi_device	*spi;
	unsigned long		delay;
	size_t			count;
	struct el15203000_led	leds[];
};

#define to_el15203000_led(d)	container_of(d, struct el15203000_led, ldev)

static int el15203000_cmd(struct el15203000_led *led, u8 brightness)
{
	int		ret;
	u8		cmd[2];
	size_t		i;

	mutex_lock(&led->priv->lock);

	dev_dbg(led->priv->dev, "Set brightness of 0x%02x(%c) to 0x%02x(%c)",
		led->reg, led->reg, brightness, brightness);

	/* to avoid SPI mistiming with firmware we should wait some time */
	if (time_after(led->priv->delay, jiffies)) {
		dev_dbg(led->priv->dev, "Wait %luus to sync",
			EL_FW_DELAY_USEC);

		usleep_range(EL_FW_DELAY_USEC,
			     EL_FW_DELAY_USEC + 1);
	}

	cmd[0] = led->reg;
	cmd[1] = brightness;

	for (i = 0; i < ARRAY_SIZE(cmd); i++) {
		if (i)
			usleep_range(EL_FW_DELAY_USEC,
				     EL_FW_DELAY_USEC + 1);

		ret = spi_write(led->priv->spi, &cmd[i], sizeof(cmd[i]));
		if (ret) {
			dev_err(led->priv->dev,
				"spi_write() error %d", ret);
			break;
		}
	}

	led->priv->delay = jiffies + usecs_to_jiffies(EL_FW_DELAY_USEC);

	mutex_unlock(&led->priv->lock);

	return ret;
}

static int el15203000_set_blocking(struct led_classdev *ldev,
				   enum led_brightness brightness)
{
	struct el15203000_led *led = to_el15203000_led(ldev);

	return el15203000_cmd(led, brightness == LED_OFF ? EL_OFF : EL_ON);
}

static int el15203000_pattern_set_S(struct led_classdev *ldev,
				    struct led_pattern *pattern,
				    u32 len, int repeat)
{
	struct el15203000_led *led = to_el15203000_led(ldev);

	if (repeat > 0 || len != 2 ||
	    pattern[0].delta_t != 4000 || pattern[0].brightness != 0 ||
	    pattern[1].delta_t != 4000 || pattern[1].brightness != 1)
		return -EINVAL;

	dev_dbg(led->priv->dev, "Breathing mode for 0x%02x(%c)",
		led->reg, led->reg);

	return el15203000_cmd(led, EL_SCREEN_BREATHING);
}

static bool is_cascade(const struct led_pattern *pattern, u32 len,
		       bool inv, bool right)
{
	int val, t;
	u32 i;

	if (len != EL_PATTERN_HALF_LEN)
		return false;

	val = right ? BIT(4) : BIT(0);

	for (i = 0; i < len; i++) {
		t = inv ? ~val & GENMASK(4, 0) : val;

		if (pattern[i].delta_t != EL_PATTERN_DELAY_MSEC ||
		    pattern[i].brightness != t)
			return false;

		val = right ? val >> 1 : val << 1;
	}

	return true;
}

static bool is_bounce(const struct led_pattern *pattern, u32 len, bool inv)
{
	if (len != EL_PATTERN_LEN)
		return false;

	return is_cascade(pattern, EL_PATTERN_HALF_LEN, inv, false) &&
	       is_cascade(pattern + EL_PATTERN_HALF_LEN,
			  EL_PATTERN_HALF_LEN, inv, true);
}

static int el15203000_pattern_set_P(struct led_classdev *ldev,
				    struct led_pattern *pattern,
				    u32 len, int repeat)
{
	struct el15203000_led	*led = to_el15203000_led(ldev);
	u8			cmd;

	if (repeat > 0)
		return -EINVAL;

	if (is_cascade(pattern, len, false, false)) {
		dev_dbg(led->priv->dev, "Cascade mode for 0x%02x(%c)",
			led->reg, led->reg);

		cmd = EL_PIPE_CASCADE;
	} else if (is_cascade(pattern, len, true, false)) {
		dev_dbg(led->priv->dev, "Inverse cascade mode for 0x%02x(%c)",
			led->reg, led->reg);

		cmd = EL_PIPE_INV_CASCADE;
	} else if (is_bounce(pattern, len, false)) {
		dev_dbg(led->priv->dev, "Bounce mode for 0x%02x(%c)",
			led->reg, led->reg);

		cmd = EL_PIPE_BOUNCE;
	} else if (is_bounce(pattern, len, true)) {
		dev_dbg(led->priv->dev, "Inverse bounce mode for 0x%02x(%c)",
			led->reg, led->reg);

		cmd = EL_PIPE_INV_BOUNCE;
	} else {
		dev_err(led->priv->dev, "Invalid hw_pattern for 0x%02x(%c)!",
			led->reg, led->reg);

		return -EINVAL;
	}

	return el15203000_cmd(led, cmd);
}

static int el15203000_pattern_clear(struct led_classdev *ldev)
{
	struct el15203000_led *led = to_el15203000_led(ldev);

	return el15203000_cmd(led, EL_OFF);
}

static int el15203000_probe_dt(struct el15203000 *priv)
{
	struct el15203000_led	*led = priv->leds;
	struct fwnode_handle	*child;
	int			ret;

	device_for_each_child_node(priv->dev, child) {
		struct led_init_data init_data = {};

		ret = fwnode_property_read_u32(child, "reg", &led->reg);
		if (ret) {
			dev_err(priv->dev, "LED without ID number");
			goto err_child_out;
		}

		if (led->reg > U8_MAX) {
			dev_err(priv->dev, "LED value %d is invalid", led->reg);
			ret = -EINVAL;
			goto err_child_out;
		}

		led->priv			  = priv;
		led->ldev.max_brightness	  = LED_ON;
		led->ldev.brightness_set_blocking = el15203000_set_blocking;

		if (led->reg == 'S') {
			led->ldev.pattern_set	= el15203000_pattern_set_S;
			led->ldev.pattern_clear	= el15203000_pattern_clear;
		} else if (led->reg == 'P') {
			led->ldev.pattern_set	= el15203000_pattern_set_P;
			led->ldev.pattern_clear	= el15203000_pattern_clear;
		}

		init_data.fwnode = child;
		ret = devm_led_classdev_register_ext(priv->dev, &led->ldev,
						     &init_data);
		if (ret) {
			dev_err(priv->dev,
				"failed to register LED device %s, err %d",
				led->ldev.name, ret);
			goto err_child_out;
		}

		led++;
	}

	return 0;

err_child_out:
	fwnode_handle_put(child);
	return ret;
}

static int el15203000_probe(struct spi_device *spi)
{
	struct el15203000	*priv;
	size_t			count;

	count = device_get_child_node_count(&spi->dev);
	if (!count) {
		dev_err(&spi->dev, "LEDs are not defined in device tree!");
		return -ENODEV;
	}

	priv = devm_kzalloc(&spi->dev, struct_size(priv, leds, count),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->lock);
	priv->count	= count;
	priv->dev	= &spi->dev;
	priv->spi	= spi;
	priv->delay	= jiffies -
			  usecs_to_jiffies(EL_FW_DELAY_USEC);

	spi_set_drvdata(spi, priv);

	return el15203000_probe_dt(priv);
}

static void el15203000_remove(struct spi_device *spi)
{
	struct el15203000 *priv = spi_get_drvdata(spi);

	mutex_destroy(&priv->lock);
}

static const struct of_device_id el15203000_dt_ids[] = {
	{ .compatible = "crane,el15203000", },
	{},
};

MODULE_DEVICE_TABLE(of, el15203000_dt_ids);

static struct spi_driver el15203000_driver = {
	.probe		= el15203000_probe,
	.remove		= el15203000_remove,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= el15203000_dt_ids,
	},
};

module_spi_driver(el15203000_driver);

MODULE_AUTHOR("Oleh Kravchenko <oleg@kaa.org.ua>");
MODULE_DESCRIPTION("el15203000 LED driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:el15203000");
