// SPDX-License-Identifier: GPL-2.0-only
/*
 * Shared helpers to register GPIO-connected buttons and LEDs
 * on AMD Geode boards.
 */

#include <linux/err.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/property.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "geode-common.h"

static const struct software_node geode_gpiochip_node = {
	.name = "cs5535-gpio",
};

static const struct property_entry geode_gpio_keys_props[] = {
	PROPERTY_ENTRY_U32("poll-interval", 20),
	{ }
};

static const struct software_node geode_gpio_keys_node = {
	.name = "geode-gpio-keys",
	.properties = geode_gpio_keys_props,
};

static struct property_entry geode_restart_key_props[] = {
	{ /* Placeholder for GPIO property */ },
	PROPERTY_ENTRY_U32("linux,code", KEY_RESTART),
	PROPERTY_ENTRY_STRING("label", "Reset button"),
	PROPERTY_ENTRY_U32("debounce-interval", 100),
	{ }
};

static const struct software_node geode_restart_key_node = {
	.parent = &geode_gpio_keys_node,
	.properties = geode_restart_key_props,
};

static const struct software_node *geode_gpio_keys_swnodes[] __initconst = {
	&geode_gpiochip_node,
	&geode_gpio_keys_node,
	&geode_restart_key_node,
	NULL
};

/*
 * Creates gpio-keys-polled device for the restart key.
 *
 * Note that it needs to be called first, before geode_create_leds(),
 * because it registers gpiochip software node used by both gpio-keys and
 * leds-gpio devices.
 */
int __init geode_create_restart_key(unsigned int pin)
{
	struct platform_device_info keys_info = {
		.name	= "gpio-keys-polled",
		.id	= 1,
	};
	struct platform_device *pd;
	int err;

	geode_restart_key_props[0] = PROPERTY_ENTRY_GPIO("gpios",
							 &geode_gpiochip_node,
							 pin, GPIO_ACTIVE_LOW);

	err = software_node_register_node_group(geode_gpio_keys_swnodes);
	if (err) {
		pr_err("failed to register gpio-keys software nodes: %d\n", err);
		return err;
	}

	keys_info.fwnode = software_node_fwnode(&geode_gpio_keys_node);

	pd = platform_device_register_full(&keys_info);
	err = PTR_ERR_OR_ZERO(pd);
	if (err) {
		pr_err("failed to create gpio-keys device: %d\n", err);
		software_node_unregister_node_group(geode_gpio_keys_swnodes);
		return err;
	}

	return 0;
}

static const struct software_node geode_gpio_leds_node = {
	.name = "geode-leds",
};

#define MAX_LEDS	3

int __init geode_create_leds(const char *label, const struct geode_led *leds,
			      unsigned int n_leds)
{
	const struct software_node *group[MAX_LEDS + 2] = { 0 };
	struct software_node *swnodes;
	struct property_entry *props;
	struct platform_device_info led_info = {
		.name	= "leds-gpio",
		.id	= PLATFORM_DEVID_NONE,
	};
	struct platform_device *led_dev;
	const char *node_name;
	int err;
	int i;

	if (n_leds > MAX_LEDS) {
		pr_err("%s: too many LEDs\n", __func__);
		return -EINVAL;
	}

	swnodes = kcalloc(n_leds, sizeof(*swnodes), GFP_KERNEL);
	if (!swnodes)
		return -ENOMEM;

	/*
	 * Each LED is represented by 3 properties: "gpios",
	 * "linux,default-trigger", and am empty terminator.
	 */
	props = kcalloc(n_leds * 3, sizeof(*props), GFP_KERNEL);
	if (!props) {
		err = -ENOMEM;
		goto err_free_swnodes;
	}

	group[0] = &geode_gpio_leds_node;
	for (i = 0; i < n_leds; i++) {
		node_name = kasprintf(GFP_KERNEL, "%s:%d", label, i);
		if (!node_name) {
			err = -ENOMEM;
			goto err_free_names;
		}

		props[i * 3 + 0] =
			PROPERTY_ENTRY_GPIO("gpios", &geode_gpiochip_node,
					    leds[i].pin, GPIO_ACTIVE_LOW);
		props[i * 3 + 1] =
			PROPERTY_ENTRY_STRING("linux,default-trigger",
					      leds[i].default_on ?
					      "default-on" : "default-off");
		/* props[i * 3 + 2] is an empty terminator */

		swnodes[i] = SOFTWARE_NODE(node_name, &props[i * 3],
					   &geode_gpio_leds_node);
		group[i + 1] = &swnodes[i];
	}

	err = software_node_register_node_group(group);
	if (err) {
		pr_err("failed to register LED software nodes: %d\n", err);
		goto err_free_names;
	}

	led_info.fwnode = software_node_fwnode(&geode_gpio_leds_node);

	led_dev = platform_device_register_full(&led_info);
	err = PTR_ERR_OR_ZERO(led_dev);
	if (err) {
		pr_err("failed to create LED device: %d\n", err);
		goto err_unregister_group;
	}

	return 0;

err_unregister_group:
	software_node_unregister_node_group(group);
err_free_names:
	while (--i >= 0)
		kfree(swnodes[i].name);
	kfree(props);
err_free_swnodes:
	kfree(swnodes);
	return err;
}
