// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DMI based code to deal with broken DSDTs on X86 tablets which ship with
 * Android as (part of) the factory image. The factory kernels shipped on these
 * devices typically have a bunch of things hardcoded, rather than specified
 * in their DSDT.
 *
 * Copyright (C) 2021-2023 Hans de Goede <hdegoede@redhat.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serdev.h>
#include <linux/string.h>

#include "x86-android-tablets.h"
/* For gpiochip_get_desc() which is EXPORT_SYMBOL_GPL() */
#include "../../../gpio/gpiolib.h"
#include "../../../gpio/gpiolib-acpi.h"

static int gpiochip_find_match_label(struct gpio_chip *gc, void *data)
{
	return gc->label && !strcmp(gc->label, data);
}

int x86_android_tablet_get_gpiod(const char *label, int pin, struct gpio_desc **desc)
{
	struct gpio_desc *gpiod;
	struct gpio_chip *chip;

	chip = gpiochip_find((void *)label, gpiochip_find_match_label);
	if (!chip) {
		pr_err("error cannot find GPIO chip %s\n", label);
		return -ENODEV;
	}

	gpiod = gpiochip_get_desc(chip, pin);
	if (IS_ERR(gpiod)) {
		pr_err("error %ld getting GPIO %s %d\n", PTR_ERR(gpiod), label, pin);
		return PTR_ERR(gpiod);
	}

	*desc = gpiod;
	return 0;
}

int x86_acpi_irq_helper_get(const struct x86_acpi_irq_data *data)
{
	struct irq_fwspec fwspec = { };
	struct irq_domain *domain;
	struct acpi_device *adev;
	struct gpio_desc *gpiod;
	unsigned int irq_type;
	acpi_handle handle;
	acpi_status status;
	int irq, ret;

	switch (data->type) {
	case X86_ACPI_IRQ_TYPE_APIC:
		/*
		 * The DSDT may already reference the GSI in a device skipped by
		 * acpi_quirk_skip_i2c_client_enumeration(). Unregister the GSI
		 * to avoid EBUSY errors in this case.
		 */
		acpi_unregister_gsi(data->index);
		irq = acpi_register_gsi(NULL, data->index, data->trigger, data->polarity);
		if (irq < 0)
			pr_err("error %d getting APIC IRQ %d\n", irq, data->index);

		return irq;
	case X86_ACPI_IRQ_TYPE_GPIOINT:
		/* Like acpi_dev_gpio_irq_get(), but without parsing ACPI resources */
		ret = x86_android_tablet_get_gpiod(data->chip, data->index, &gpiod);
		if (ret)
			return ret;

		irq = gpiod_to_irq(gpiod);
		if (irq < 0) {
			pr_err("error %d getting IRQ %s %d\n", irq, data->chip, data->index);
			return irq;
		}

		irq_type = acpi_dev_get_irq_type(data->trigger, data->polarity);
		if (irq_type != IRQ_TYPE_NONE && irq_type != irq_get_trigger_type(irq))
			irq_set_irq_type(irq, irq_type);

		return irq;
	case X86_ACPI_IRQ_TYPE_PMIC:
		status = acpi_get_handle(NULL, data->chip, &handle);
		if (ACPI_FAILURE(status)) {
			pr_err("error could not get %s handle\n", data->chip);
			return -ENODEV;
		}

		adev = acpi_fetch_acpi_dev(handle);
		if (!adev) {
			pr_err("error could not get %s adev\n", data->chip);
			return -ENODEV;
		}

		fwspec.fwnode = acpi_fwnode_handle(adev);
		domain = irq_find_matching_fwspec(&fwspec, data->domain);
		if (!domain) {
			pr_err("error could not find IRQ domain for %s\n", data->chip);
			return -ENODEV;
		}

		return irq_create_mapping(domain, data->index);
	default:
		return 0;
	}
}

static int i2c_client_count;
static int pdev_count;
static int serdev_count;
static struct i2c_client **i2c_clients;
static struct platform_device **pdevs;
static struct serdev_device **serdevs;
static struct gpio_keys_button *buttons;
static struct gpiod_lookup_table * const *gpiod_lookup_tables;
static const struct software_node *bat_swnode;
static void (*exit_handler)(void);

static __init int x86_instantiate_i2c_client(const struct x86_dev_info *dev_info,
					     int idx)
{
	const struct x86_i2c_client_info *client_info = &dev_info->i2c_client_info[idx];
	struct i2c_board_info board_info = client_info->board_info;
	struct i2c_adapter *adap;
	acpi_handle handle;
	acpi_status status;

	board_info.irq = x86_acpi_irq_helper_get(&client_info->irq_data);
	if (board_info.irq < 0)
		return board_info.irq;

	status = acpi_get_handle(NULL, client_info->adapter_path, &handle);
	if (ACPI_FAILURE(status)) {
		pr_err("Error could not get %s handle\n", client_info->adapter_path);
		return -ENODEV;
	}

	adap = i2c_acpi_find_adapter_by_handle(handle);
	if (!adap) {
		pr_err("error could not get %s adapter\n", client_info->adapter_path);
		return -ENODEV;
	}

	i2c_clients[idx] = i2c_new_client_device(adap, &board_info);
	put_device(&adap->dev);
	if (IS_ERR(i2c_clients[idx]))
		return dev_err_probe(&adap->dev, PTR_ERR(i2c_clients[idx]),
				      "creating I2C-client %d\n", idx);

	return 0;
}

static __init int x86_instantiate_serdev(const struct x86_serdev_info *info, int idx)
{
	struct acpi_device *ctrl_adev, *serdev_adev;
	struct serdev_device *serdev;
	struct device *ctrl_dev;
	int ret = -ENODEV;

	ctrl_adev = acpi_dev_get_first_match_dev(info->ctrl_hid, info->ctrl_uid, -1);
	if (!ctrl_adev) {
		pr_err("error could not get %s/%s ctrl adev\n",
		       info->ctrl_hid, info->ctrl_uid);
		return -ENODEV;
	}

	serdev_adev = acpi_dev_get_first_match_dev(info->serdev_hid, NULL, -1);
	if (!serdev_adev) {
		pr_err("error could not get %s serdev adev\n", info->serdev_hid);
		goto put_ctrl_adev;
	}

	/* get_first_physical_node() returns a weak ref, no need to put() it */
	ctrl_dev = acpi_get_first_physical_node(ctrl_adev);
	if (!ctrl_dev)	{
		pr_err("error could not get %s/%s ctrl physical dev\n",
		       info->ctrl_hid, info->ctrl_uid);
		goto put_serdev_adev;
	}

	/* ctrl_dev now points to the controller's parent, get the controller */
	ctrl_dev = device_find_child_by_name(ctrl_dev, info->ctrl_devname);
	if (!ctrl_dev) {
		pr_err("error could not get %s/%s %s ctrl dev\n",
		       info->ctrl_hid, info->ctrl_uid, info->ctrl_devname);
		goto put_serdev_adev;
	}

	serdev = serdev_device_alloc(to_serdev_controller(ctrl_dev));
	if (!serdev) {
		ret = -ENOMEM;
		goto put_serdev_adev;
	}

	ACPI_COMPANION_SET(&serdev->dev, serdev_adev);
	acpi_device_set_enumerated(serdev_adev);

	ret = serdev_device_add(serdev);
	if (ret) {
		dev_err(&serdev->dev, "error %d adding serdev\n", ret);
		serdev_device_put(serdev);
		goto put_serdev_adev;
	}

	serdevs[idx] = serdev;

put_serdev_adev:
	acpi_dev_put(serdev_adev);
put_ctrl_adev:
	acpi_dev_put(ctrl_adev);
	return ret;
}

static void x86_android_tablet_cleanup(void)
{
	int i;

	for (i = 0; i < serdev_count; i++) {
		if (serdevs[i])
			serdev_device_remove(serdevs[i]);
	}

	kfree(serdevs);

	for (i = 0; i < pdev_count; i++)
		platform_device_unregister(pdevs[i]);

	kfree(pdevs);
	kfree(buttons);

	for (i = 0; i < i2c_client_count; i++)
		i2c_unregister_device(i2c_clients[i]);

	kfree(i2c_clients);

	if (exit_handler)
		exit_handler();

	for (i = 0; gpiod_lookup_tables && gpiod_lookup_tables[i]; i++)
		gpiod_remove_lookup_table(gpiod_lookup_tables[i]);

	software_node_unregister(bat_swnode);
}

static __init int x86_android_tablet_init(void)
{
	const struct x86_dev_info *dev_info;
	const struct dmi_system_id *id;
	struct gpio_chip *chip;
	int i, ret = 0;

	id = dmi_first_match(x86_android_tablet_ids);
	if (!id)
		return -ENODEV;

	dev_info = id->driver_data;

	/*
	 * The broken DSDTs on these devices often also include broken
	 * _AEI (ACPI Event Interrupt) handlers, disable these.
	 */
	if (dev_info->invalid_aei_gpiochip) {
		chip = gpiochip_find(dev_info->invalid_aei_gpiochip,
				     gpiochip_find_match_label);
		if (!chip) {
			pr_err("error cannot find GPIO chip %s\n", dev_info->invalid_aei_gpiochip);
			return -ENODEV;
		}
		acpi_gpiochip_free_interrupts(chip);
	}

	/*
	 * Since this runs from module_init() it cannot use -EPROBE_DEFER,
	 * instead pre-load any modules which are listed as requirements.
	 */
	for (i = 0; dev_info->modules && dev_info->modules[i]; i++)
		request_module(dev_info->modules[i]);

	bat_swnode = dev_info->bat_swnode;
	if (bat_swnode) {
		ret = software_node_register(bat_swnode);
		if (ret)
			return ret;
	}

	gpiod_lookup_tables = dev_info->gpiod_lookup_tables;
	for (i = 0; gpiod_lookup_tables && gpiod_lookup_tables[i]; i++)
		gpiod_add_lookup_table(gpiod_lookup_tables[i]);

	if (dev_info->init) {
		ret = dev_info->init();
		if (ret < 0) {
			x86_android_tablet_cleanup();
			return ret;
		}
		exit_handler = dev_info->exit;
	}

	i2c_clients = kcalloc(dev_info->i2c_client_count, sizeof(*i2c_clients), GFP_KERNEL);
	if (!i2c_clients) {
		x86_android_tablet_cleanup();
		return -ENOMEM;
	}

	i2c_client_count = dev_info->i2c_client_count;
	for (i = 0; i < i2c_client_count; i++) {
		ret = x86_instantiate_i2c_client(dev_info, i);
		if (ret < 0) {
			x86_android_tablet_cleanup();
			return ret;
		}
	}

	/* + 1 to make space for (optional) gpio_keys_button pdev */
	pdevs = kcalloc(dev_info->pdev_count + 1, sizeof(*pdevs), GFP_KERNEL);
	if (!pdevs) {
		x86_android_tablet_cleanup();
		return -ENOMEM;
	}

	pdev_count = dev_info->pdev_count;
	for (i = 0; i < pdev_count; i++) {
		pdevs[i] = platform_device_register_full(&dev_info->pdev_info[i]);
		if (IS_ERR(pdevs[i])) {
			x86_android_tablet_cleanup();
			return PTR_ERR(pdevs[i]);
		}
	}

	serdevs = kcalloc(dev_info->serdev_count, sizeof(*serdevs), GFP_KERNEL);
	if (!serdevs) {
		x86_android_tablet_cleanup();
		return -ENOMEM;
	}

	serdev_count = dev_info->serdev_count;
	for (i = 0; i < serdev_count; i++) {
		ret = x86_instantiate_serdev(&dev_info->serdev_info[i], i);
		if (ret < 0) {
			x86_android_tablet_cleanup();
			return ret;
		}
	}

	if (dev_info->gpio_button_count) {
		struct gpio_keys_platform_data pdata = { };
		struct gpio_desc *gpiod;

		buttons = kcalloc(dev_info->gpio_button_count, sizeof(*buttons), GFP_KERNEL);
		if (!buttons) {
			x86_android_tablet_cleanup();
			return -ENOMEM;
		}

		for (i = 0; i < dev_info->gpio_button_count; i++) {
			ret = x86_android_tablet_get_gpiod(dev_info->gpio_button[i].chip,
							   dev_info->gpio_button[i].pin, &gpiod);
			if (ret < 0) {
				x86_android_tablet_cleanup();
				return ret;
			}

			buttons[i] = dev_info->gpio_button[i].button;
			buttons[i].gpio = desc_to_gpio(gpiod);
		}

		pdata.buttons = buttons;
		pdata.nbuttons = dev_info->gpio_button_count;

		pdevs[pdev_count] = platform_device_register_data(NULL, "gpio-keys",
								  PLATFORM_DEVID_AUTO,
								  &pdata, sizeof(pdata));
		if (IS_ERR(pdevs[pdev_count])) {
			x86_android_tablet_cleanup();
			return PTR_ERR(pdevs[pdev_count]);
		}
		pdev_count++;
	}

	return 0;
}

module_init(x86_android_tablet_init);
module_exit(x86_android_tablet_cleanup);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("X86 Android tablets DSDT fixups driver");
MODULE_LICENSE("GPL");
