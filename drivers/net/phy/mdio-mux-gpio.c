/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011, 2012 Cavium, Inc.
 */

#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/of_mdio.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/mdio-mux.h>
#include <linux/of_gpio.h>

#define DRV_VERSION "1.1"
#define DRV_DESCRIPTION "GPIO controlled MDIO bus multiplexer driver"

#define MDIO_MUX_GPIO_MAX_BITS 8

struct mdio_mux_gpio_state {
	struct gpio_desc *gpio[MDIO_MUX_GPIO_MAX_BITS];
	unsigned int num_gpios;
	void *mux_handle;
};

static int mdio_mux_gpio_switch_fn(int current_child, int desired_child,
				   void *data)
{
	int values[MDIO_MUX_GPIO_MAX_BITS];
	unsigned int n;
	struct mdio_mux_gpio_state *s = data;

	if (current_child == desired_child)
		return 0;

	for (n = 0; n < s->num_gpios; n++) {
		values[n] = (desired_child >> n) & 1;
	}
	gpiod_set_array_cansleep(s->num_gpios, s->gpio, values);

	return 0;
}

static int mdio_mux_gpio_probe(struct platform_device *pdev)
{
	struct mdio_mux_gpio_state *s;
	int num_gpios;
	unsigned int n;
	int r;

	if (!pdev->dev.of_node)
		return -ENODEV;

	num_gpios = of_gpio_count(pdev->dev.of_node);
	if (num_gpios <= 0 || num_gpios > MDIO_MUX_GPIO_MAX_BITS)
		return -ENODEV;

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->num_gpios = num_gpios;

	for (n = 0; n < num_gpios; ) {
		struct gpio_desc *gpio = gpiod_get_index(&pdev->dev, NULL, n,
							 GPIOD_OUT_LOW);
		if (IS_ERR(gpio)) {
			r = PTR_ERR(gpio);
			goto err;
		}
		s->gpio[n] = gpio;
		n++;
	}

	r = mdio_mux_init(&pdev->dev,
			  mdio_mux_gpio_switch_fn, &s->mux_handle, s);

	if (r == 0) {
		pdev->dev.platform_data = s;
		return 0;
	}
err:
	while (n) {
		n--;
		gpiod_put(s->gpio[n]);
	}
	return r;
}

static int mdio_mux_gpio_remove(struct platform_device *pdev)
{
	unsigned int n;
	struct mdio_mux_gpio_state *s = dev_get_platdata(&pdev->dev);
	mdio_mux_uninit(s->mux_handle);
	for (n = 0; n < s->num_gpios; n++)
		gpiod_put(s->gpio[n]);
	return 0;
}

static struct of_device_id mdio_mux_gpio_match[] = {
	{
		.compatible = "mdio-mux-gpio",
	},
	{
		/* Legacy compatible property. */
		.compatible = "cavium,mdio-mux-sn74cbtlv3253",
	},
	{},
};
MODULE_DEVICE_TABLE(of, mdio_mux_gpio_match);

static struct platform_driver mdio_mux_gpio_driver = {
	.driver = {
		.name		= "mdio-mux-gpio",
		.owner		= THIS_MODULE,
		.of_match_table = mdio_mux_gpio_match,
	},
	.probe		= mdio_mux_gpio_probe,
	.remove		= mdio_mux_gpio_remove,
};

module_platform_driver(mdio_mux_gpio_driver);

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("David Daney");
MODULE_LICENSE("GPL");
