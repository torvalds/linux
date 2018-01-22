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
#include <linux/gpio/consumer.h>

#define DRV_VERSION "1.1"
#define DRV_DESCRIPTION "GPIO controlled MDIO bus multiplexer driver"

struct mdio_mux_gpio_state {
	struct gpio_descs *gpios;
	void *mux_handle;
};

static int mdio_mux_gpio_switch_fn(int current_child, int desired_child,
				   void *data)
{
	struct mdio_mux_gpio_state *s = data;
	int values[s->gpios->ndescs];
	unsigned int n;

	if (current_child == desired_child)
		return 0;

	for (n = 0; n < s->gpios->ndescs; n++)
		values[n] = (desired_child >> n) & 1;

	gpiod_set_array_value_cansleep(s->gpios->ndescs, s->gpios->desc,
				       values);

	return 0;
}

static int mdio_mux_gpio_probe(struct platform_device *pdev)
{
	struct mdio_mux_gpio_state *s;
	int r;

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->gpios = gpiod_get_array(&pdev->dev, NULL, GPIOD_OUT_LOW);
	if (IS_ERR(s->gpios))
		return PTR_ERR(s->gpios);

	r = mdio_mux_init(&pdev->dev, pdev->dev.of_node,
			  mdio_mux_gpio_switch_fn, &s->mux_handle, s, NULL);

	if (r != 0) {
		gpiod_put_array(s->gpios);
		return r;
	}

	pdev->dev.platform_data = s;
	return 0;
}

static int mdio_mux_gpio_remove(struct platform_device *pdev)
{
	struct mdio_mux_gpio_state *s = dev_get_platdata(&pdev->dev);
	mdio_mux_uninit(s->mux_handle);
	gpiod_put_array(s->gpios);
	return 0;
}

static const struct of_device_id mdio_mux_gpio_match[] = {
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
