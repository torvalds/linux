// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Christian Mauderer <oss@c-mauderer.de>

/*
 * The driver supports controllers with a very simple SPI protocol:
 * - one LED is controlled by a single byte on MOSI
 * - the value of the byte gives the brightness between two values (lowest to
 *   highest)
 * - no return value is necessary (no MISO signal)
 *
 * The value for minimum and maximum brightness depends on the device
 * (compatible string).
 *
 * Supported devices:
 * - "ubnt,acb-spi-led": Microcontroller (SONiX 8F26E611LA) based device used
 *   for example in Ubiquiti airCube ISP. Reverse engineered protocol for this
 *   controller:
 *   * Higher two bits set a mode. Lower six bits are a parameter.
 *   * Mode: 00 -> set brightness between 0x00 (min) and 0x3F (max)
 *   * Mode: 01 -> pulsing pattern (min -> max -> min) with an interval. From
 *     some tests, the period is about (50ms + 102ms * parameter). There is a
 *     slightly different pattern starting from 0x10 (longer gap between the
 *     pulses) but the time still follows that calculation.
 *   * Mode: 10 -> same as 01 but with only a ramp from min to max. Again a
 *     slight jump in the pattern at 0x10.
 *   * Mode: 11 -> blinking (off -> 25% -> off -> 25% -> ...) with a period of
 *     (105ms * parameter)
 *   NOTE: This driver currently only supports mode 00.
 */

#include <linux/leds.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <uapi/linux/uleds.h>

struct spi_byte_chipdef {
	/* SPI byte that will be send to switch the LED off */
	u8	off_value;
	/* SPI byte that will be send to switch the LED to maximum brightness */
	u8	max_value;
};

struct spi_byte_led {
	struct led_classdev		ldev;
	struct spi_device		*spi;
	char				name[LED_MAX_NAME_SIZE];
	struct mutex			mutex;
	const struct spi_byte_chipdef	*cdef;
};

static const struct spi_byte_chipdef ubnt_acb_spi_led_cdef = {
	.off_value = 0x0,
	.max_value = 0x3F,
};

static int spi_byte_brightness_set_blocking(struct led_classdev *dev,
					    enum led_brightness brightness)
{
	struct spi_byte_led *led = container_of(dev, struct spi_byte_led, ldev);
	u8 value;
	int ret;

	value = (u8) brightness + led->cdef->off_value;

	mutex_lock(&led->mutex);
	ret = spi_write(led->spi, &value, sizeof(value));
	mutex_unlock(&led->mutex);

	return ret;
}

static int spi_byte_probe(struct spi_device *spi)
{
	struct fwnode_handle *child __free(fwnode_handle) = NULL;
	struct device *dev = &spi->dev;
	struct spi_byte_led *led;
	struct led_init_data init_data = {};
	enum led_default_state state;
	int ret;

	if (device_get_child_node_count(dev) != 1) {
		dev_err(dev, "Device must have exactly one LED sub-node.");
		return -EINVAL;
	}

	led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	ret = devm_mutex_init(dev, &led->mutex);
	if (ret)
		return ret;

	led->spi = spi;
	led->cdef = device_get_match_data(dev);
	led->ldev.brightness = LED_OFF;
	led->ldev.max_brightness = led->cdef->max_value - led->cdef->off_value;
	led->ldev.brightness_set_blocking = spi_byte_brightness_set_blocking;

	child = device_get_next_child_node(dev, NULL);

	state = led_init_default_state_get(child);
	if (state == LEDS_DEFSTATE_ON)
		led->ldev.brightness = led->ldev.max_brightness;
	spi_byte_brightness_set_blocking(&led->ldev,
					 led->ldev.brightness);

	init_data.fwnode = child;
	init_data.devicename = "leds-spi-byte";
	init_data.default_label = ":";

	return devm_led_classdev_register_ext(dev, &led->ldev, &init_data);
}

static const struct of_device_id spi_byte_dt_ids[] = {
	{ .compatible = "ubnt,acb-spi-led", .data = &ubnt_acb_spi_led_cdef },
	{}
};
MODULE_DEVICE_TABLE(of, spi_byte_dt_ids);

static struct spi_driver spi_byte_driver = {
	.probe		= spi_byte_probe,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= spi_byte_dt_ids,
	},
};
module_spi_driver(spi_byte_driver);

MODULE_AUTHOR("Christian Mauderer <oss@c-mauderer.de>");
MODULE_DESCRIPTION("single byte SPI LED driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:leds-spi-byte");
