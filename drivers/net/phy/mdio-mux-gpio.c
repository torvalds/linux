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
#include <linux/init.h>
#include <linux/phy.h>
#include <linux/mdio-mux.h>
#include <linux/of_gpio.h>

#define DRV_VERSION "1.0"
#define DRV_DESCRIPTION "GPIO controlled MDIO bus multiplexer driver"

#define MDIO_MUX_GPIO_MAX_BITS 8

struct mdio_mux_gpio_state {
	int gpio[MDIO_MUX_GPIO_MAX_BITS];
	unsigned int num_gpios;
	void *mux_handle;
};

static int mdio_mux_gpio_switch_fn(int current_child, int desired_child,
				   void *data)
{
	int change;
	unsigned int n;
	struct mdio_mux_gpio_state *s = data;

	if (current_child == desired_child)
		return 0;

	change = current_child == -1 ? -1 : current_child ^ desired_child;

	for (n = 0; n < s->num_gpios; n++) {
		if (change & 1)
			gpio_set_value_cansleep(s->gpio[n],
						(desired_child & 1) != 0);
		change >>= 1;
		desired_child >>= 1;
	}

	return 0;
}

static int mdio_mux_gpio_probe(struct platform_device *pdev)
{
	enum of_gpio_flags f;
	struct mdio_mux_gpio_state *s;
	unsigned int num_gpios;
	unsigned int n;
	int r;

	if (!pdev->dev.of_node)
		return -ENODEV;

	num_gpios = of_gpio_count(pdev->dev.of_node);
	if (num_gpios == 0 || num_gpios > MDIO_MUX_GPIO_MAX_BITS)
		return -ENODEV;

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->num_gpios = num_gpios;

	for (n = 0; n < num_gpios; ) {
		int gpio = of_get_gpio_flags(pdev->dev.of_node, n, &f);
		if (gpio < 0) {
			r = (gpio == -ENODEV) ? -EPROBE_DEFER : gpio;
			goto err;
		}
		s->gpio[n] = gpio;

		n++;

		r = gpio_request(gpio, "mdio_mux_gpio");
		if (r)
			goto err;

		r = gpio_direction_output(gpio, 0);
		if (r)
			goto err;
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
		gpio_free(s->gpio[n]);
	}
	return r;
}

static int mdio_mux_gpio_remove(struct platform_device *pdev)
{
	struct mdio_mux_gpio_state *s = pdev->dev.platform_data;
	mdio_mux_uninit(s->mux_handle);
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
