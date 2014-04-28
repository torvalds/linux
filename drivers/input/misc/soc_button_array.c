/*
 * Supports for the button array on SoC tablets originally running
 * Windows 8.
 *
 * (C) Copyright 2014 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/pnp.h>

/*
 * Definition of buttons on the tablet. The ACPI index of each button
 * is defined in section 2.8.7.2 of "Windows ACPI Design Guide for SoC
 * Platforms"
 */
#define MAX_NBUTTONS	5

struct soc_button_info {
	const char *name;
	int acpi_index;
	unsigned int event_type;
	unsigned int event_code;
	bool autorepeat;
	bool wakeup;
};

/*
 * Some of the buttons like volume up/down are auto repeat, while others
 * are not. To support both, we register two platform devices, and put
 * buttons into them based on whether the key should be auto repeat.
 */
#define BUTTON_TYPES	2

struct soc_button_data {
	struct platform_device *children[BUTTON_TYPES];
};

/*
 * Get the Nth GPIO number from the ACPI object.
 */
static int soc_button_lookup_gpio(struct device *dev, int acpi_index)
{
	struct gpio_desc *desc;
	int gpio;

	desc = gpiod_get_index(dev, KBUILD_MODNAME, acpi_index);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	gpio = desc_to_gpio(desc);

	gpiod_put(desc);

	return gpio;
}

static struct platform_device *
soc_button_device_create(struct pnp_dev *pdev,
			 const struct soc_button_info *button_info,
			 bool autorepeat)
{
	const struct soc_button_info *info;
	struct platform_device *pd;
	struct gpio_keys_button *gpio_keys;
	struct gpio_keys_platform_data *gpio_keys_pdata;
	int n_buttons = 0;
	int gpio;
	int error;

	gpio_keys_pdata = devm_kzalloc(&pdev->dev,
				       sizeof(*gpio_keys_pdata) +
					sizeof(*gpio_keys) * MAX_NBUTTONS,
				       GFP_KERNEL);
	gpio_keys = (void *)(gpio_keys_pdata + 1);

	for (info = button_info; info->name; info++) {
		if (info->autorepeat != autorepeat)
			continue;

		gpio = soc_button_lookup_gpio(&pdev->dev, info->acpi_index);
		if (gpio < 0)
			continue;

		gpio_keys[n_buttons].type = info->event_type;
		gpio_keys[n_buttons].code = info->event_code;
		gpio_keys[n_buttons].gpio = gpio;
		gpio_keys[n_buttons].active_low = 1;
		gpio_keys[n_buttons].desc = info->name;
		gpio_keys[n_buttons].wakeup = info->wakeup;
		n_buttons++;
	}

	if (n_buttons == 0) {
		error = -ENODEV;
		goto err_free_mem;
	}

	gpio_keys_pdata->buttons = gpio_keys;
	gpio_keys_pdata->nbuttons = n_buttons;
	gpio_keys_pdata->rep = autorepeat;

	pd = platform_device_alloc("gpio-keys", PLATFORM_DEVID_AUTO);
	if (!pd) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	error = platform_device_add_data(pd, gpio_keys_pdata,
					 sizeof(*gpio_keys_pdata));
	if (error)
		goto err_free_pdev;

	error = platform_device_add(pd);
	if (error)
		goto err_free_pdev;

	return pd;

err_free_pdev:
	platform_device_put(pd);
err_free_mem:
	devm_kfree(&pdev->dev, gpio_keys_pdata);
	return ERR_PTR(error);
}

static void soc_button_remove(struct pnp_dev *pdev)
{
	struct soc_button_data *priv = pnp_get_drvdata(pdev);
	int i;

	for (i = 0; i < BUTTON_TYPES; i++)
		if (priv->children[i])
			platform_device_unregister(priv->children[i]);
}

static int soc_button_pnp_probe(struct pnp_dev *pdev,
				const struct pnp_device_id *id)
{
	const struct soc_button_info *button_info = (void *)id->driver_data;
	struct soc_button_data *priv;
	struct platform_device *pd;
	int i;
	int error;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	pnp_set_drvdata(pdev, priv);

	for (i = 0; i < BUTTON_TYPES; i++) {
		pd = soc_button_device_create(pdev, button_info, i == 0);
		if (IS_ERR(pd)) {
			error = PTR_ERR(pd);
			if (error != -ENODEV) {
				soc_button_remove(pdev);
				return error;
			}
			continue;
		}

		priv->children[i] = pd;
	}

	if (!priv->children[0] && !priv->children[1])
		return -ENODEV;

	return 0;
}

static struct soc_button_info soc_button_PNP0C40[] = {
	{ "power", 0, EV_KEY, KEY_POWER, false, true },
	{ "home", 1, EV_KEY, KEY_HOME, false, true },
	{ "volume_up", 2, EV_KEY, KEY_VOLUMEUP, true, false },
	{ "volume_down", 3, EV_KEY, KEY_VOLUMEDOWN, true, false },
	{ "rotation_lock", 4, EV_SW, SW_ROTATE_LOCK, false, false },
	{ }
};

static const struct pnp_device_id soc_button_pnp_match[] = {
	{ .id = "PNP0C40", .driver_data = (long)soc_button_PNP0C40 },
	{ .id = "" }
};
MODULE_DEVICE_TABLE(pnp, soc_button_pnp_match);

static struct pnp_driver soc_button_pnp_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= soc_button_pnp_match,
	.probe          = soc_button_pnp_probe,
	.remove		= soc_button_remove,
};

static int __init soc_button_init(void)
{
	return pnp_register_driver(&soc_button_pnp_driver);
}

static void __exit soc_button_exit(void)
{
	pnp_unregister_driver(&soc_button_pnp_driver);
}

module_init(soc_button_init);
module_exit(soc_button_exit);

MODULE_LICENSE("GPL");
