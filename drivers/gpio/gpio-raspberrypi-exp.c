// SPDX-License-Identifier: GPL-2.0+
/*
 *  Raspberry Pi 3 expander GPIO driver
 *
 *  Uses the firmware mailbox service to communicate with the
 *  GPIO expander on the VPU.
 *
 *  Copyright (C) 2017 Raspberry Pi Trading Ltd.
 */

#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <soc/bcm2835/raspberrypi-firmware.h>

#define MODULE_NAME "raspberrypi-exp-gpio"
#define NUM_GPIO 8

#define RPI_EXP_GPIO_BASE	128

#define RPI_EXP_GPIO_DIR_IN	0
#define RPI_EXP_GPIO_DIR_OUT	1

struct rpi_exp_gpio {
	struct gpio_chip gc;
	struct rpi_firmware *fw;
};

/* VC4 firmware mailbox interface data structures */

struct gpio_set_config {
	u32 gpio;
	u32 direction;
	u32 polarity;
	u32 term_en;
	u32 term_pull_up;
	u32 state;
};

struct gpio_get_config {
	u32 gpio;
	u32 direction;
	u32 polarity;
	u32 term_en;
	u32 term_pull_up;
};

struct gpio_get_set_state {
	u32 gpio;
	u32 state;
};

static int rpi_exp_gpio_get_polarity(struct gpio_chip *gc, unsigned int off)
{
	struct rpi_exp_gpio *gpio;
	struct gpio_get_config get;
	int ret;

	gpio = gpiochip_get_data(gc);

	get.gpio = off + RPI_EXP_GPIO_BASE;	/* GPIO to update */

	ret = rpi_firmware_property(gpio->fw, RPI_FIRMWARE_GET_GPIO_CONFIG,
				    &get, sizeof(get));
	if (ret || get.gpio != 0) {
		dev_err(gc->parent, "Failed to get GPIO %u config (%d %x)\n",
			off, ret, get.gpio);
		return ret ? ret : -EIO;
	}
	return get.polarity;
}

static int rpi_exp_gpio_dir_in(struct gpio_chip *gc, unsigned int off)
{
	struct rpi_exp_gpio *gpio;
	struct gpio_set_config set_in;
	int ret;

	gpio = gpiochip_get_data(gc);

	set_in.gpio = off + RPI_EXP_GPIO_BASE;	/* GPIO to update */
	set_in.direction = RPI_EXP_GPIO_DIR_IN;
	set_in.term_en = 0;		/* termination disabled */
	set_in.term_pull_up = 0;	/* n/a as termination disabled */
	set_in.state = 0;		/* n/a as configured as an input */

	ret = rpi_exp_gpio_get_polarity(gc, off);
	if (ret < 0)
		return ret;
	set_in.polarity = ret;		/* Retain existing setting */

	ret = rpi_firmware_property(gpio->fw, RPI_FIRMWARE_SET_GPIO_CONFIG,
				    &set_in, sizeof(set_in));
	if (ret || set_in.gpio != 0) {
		dev_err(gc->parent, "Failed to set GPIO %u to input (%d %x)\n",
			off, ret, set_in.gpio);
		return ret ? ret : -EIO;
	}
	return 0;
}

static int rpi_exp_gpio_dir_out(struct gpio_chip *gc, unsigned int off, int val)
{
	struct rpi_exp_gpio *gpio;
	struct gpio_set_config set_out;
	int ret;

	gpio = gpiochip_get_data(gc);

	set_out.gpio = off + RPI_EXP_GPIO_BASE;	/* GPIO to update */
	set_out.direction = RPI_EXP_GPIO_DIR_OUT;
	set_out.term_en = 0;		/* n/a as an output */
	set_out.term_pull_up = 0;	/* n/a as termination disabled */
	set_out.state = val;		/* Output state */

	ret = rpi_exp_gpio_get_polarity(gc, off);
	if (ret < 0)
		return ret;
	set_out.polarity = ret;		/* Retain existing setting */

	ret = rpi_firmware_property(gpio->fw, RPI_FIRMWARE_SET_GPIO_CONFIG,
				    &set_out, sizeof(set_out));
	if (ret || set_out.gpio != 0) {
		dev_err(gc->parent, "Failed to set GPIO %u to output (%d %x)\n",
			off, ret, set_out.gpio);
		return ret ? ret : -EIO;
	}
	return 0;
}

static int rpi_exp_gpio_get_direction(struct gpio_chip *gc, unsigned int off)
{
	struct rpi_exp_gpio *gpio;
	struct gpio_get_config get;
	int ret;

	gpio = gpiochip_get_data(gc);

	get.gpio = off + RPI_EXP_GPIO_BASE;	/* GPIO to update */

	ret = rpi_firmware_property(gpio->fw, RPI_FIRMWARE_GET_GPIO_CONFIG,
				    &get, sizeof(get));
	if (ret || get.gpio != 0) {
		dev_err(gc->parent,
			"Failed to get GPIO %u config (%d %x)\n", off, ret,
			get.gpio);
		return ret ? ret : -EIO;
	}
	return !get.direction;
}

static int rpi_exp_gpio_get(struct gpio_chip *gc, unsigned int off)
{
	struct rpi_exp_gpio *gpio;
	struct gpio_get_set_state get;
	int ret;

	gpio = gpiochip_get_data(gc);

	get.gpio = off + RPI_EXP_GPIO_BASE;	/* GPIO to update */
	get.state = 0;		/* storage for returned value */

	ret = rpi_firmware_property(gpio->fw, RPI_FIRMWARE_GET_GPIO_STATE,
					 &get, sizeof(get));
	if (ret || get.gpio != 0) {
		dev_err(gc->parent,
			"Failed to get GPIO %u state (%d %x)\n", off, ret,
			get.gpio);
		return ret ? ret : -EIO;
	}
	return !!get.state;
}

static void rpi_exp_gpio_set(struct gpio_chip *gc, unsigned int off, int val)
{
	struct rpi_exp_gpio *gpio;
	struct gpio_get_set_state set;
	int ret;

	gpio = gpiochip_get_data(gc);

	set.gpio = off + RPI_EXP_GPIO_BASE;	/* GPIO to update */
	set.state = val;	/* Output state */

	ret = rpi_firmware_property(gpio->fw, RPI_FIRMWARE_SET_GPIO_STATE,
					 &set, sizeof(set));
	if (ret || set.gpio != 0)
		dev_err(gc->parent,
			"Failed to set GPIO %u state (%d %x)\n", off, ret,
			set.gpio);
}

static int rpi_exp_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *fw_node;
	struct rpi_firmware *fw;
	struct rpi_exp_gpio *rpi_gpio;

	fw_node = of_get_parent(np);
	if (!fw_node) {
		dev_err(dev, "Missing firmware node\n");
		return -ENOENT;
	}

	fw = rpi_firmware_get(fw_node);
	if (!fw)
		return -EPROBE_DEFER;

	rpi_gpio = devm_kzalloc(dev, sizeof(*rpi_gpio), GFP_KERNEL);
	if (!rpi_gpio)
		return -ENOMEM;

	rpi_gpio->fw = fw;
	rpi_gpio->gc.parent = dev;
	rpi_gpio->gc.label = MODULE_NAME;
	rpi_gpio->gc.owner = THIS_MODULE;
	rpi_gpio->gc.of_node = np;
	rpi_gpio->gc.base = -1;
	rpi_gpio->gc.ngpio = NUM_GPIO;

	rpi_gpio->gc.direction_input = rpi_exp_gpio_dir_in;
	rpi_gpio->gc.direction_output = rpi_exp_gpio_dir_out;
	rpi_gpio->gc.get_direction = rpi_exp_gpio_get_direction;
	rpi_gpio->gc.get = rpi_exp_gpio_get;
	rpi_gpio->gc.set = rpi_exp_gpio_set;
	rpi_gpio->gc.can_sleep = true;

	return devm_gpiochip_add_data(dev, &rpi_gpio->gc, rpi_gpio);
}

static const struct of_device_id rpi_exp_gpio_ids[] = {
	{ .compatible = "raspberrypi,firmware-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, rpi_exp_gpio_ids);

static struct platform_driver rpi_exp_gpio_driver = {
	.driver	= {
		.name		= MODULE_NAME,
		.of_match_table	= of_match_ptr(rpi_exp_gpio_ids),
	},
	.probe	= rpi_exp_gpio_probe,
};
module_platform_driver(rpi_exp_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dave Stevenson <dave.stevenson@raspberrypi.org>");
MODULE_DESCRIPTION("Raspberry Pi 3 expander GPIO driver");
MODULE_ALIAS("platform:rpi-exp-gpio");
