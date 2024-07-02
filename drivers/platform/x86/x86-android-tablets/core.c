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
#include <linux/gpio/consumer.h>
#include <linux/gpio/machine.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serdev.h>
#include <linux/string.h>

#include "x86-android-tablets.h"
#include "../serdev_helpers.h"

static struct platform_device *x86_android_tablet_device;

/*
 * This helper allows getting a gpio_desc *before* the actual device consuming
 * the GPIO has been instantiated. This function _must_ only be used to handle
 * this special case such as e.g. :
 *
 * 1. Getting an IRQ from a GPIO for i2c_board_info.irq which is passed to
 * i2c_client_new() to instantiate i2c_client-s; or
 * 2. Calling desc_to_gpio() to get an old style GPIO number for gpio_keys
 * platform_data which still uses old style GPIO numbers.
 *
 * Since the consuming device has not been instatiated yet a dynamic lookup
 * is generated using the special x86_android_tablet dev for dev_id.
 *
 * For normal GPIO lookups a standard static gpiod_lookup_table _must_ be used.
 */
int x86_android_tablet_get_gpiod(const char *chip, int pin, const char *con_id,
				 bool active_low, enum gpiod_flags dflags,
				 struct gpio_desc **desc)
{
	struct gpiod_lookup_table *lookup;
	struct gpio_desc *gpiod;

	lookup = kzalloc(struct_size(lookup, table, 2), GFP_KERNEL);
	if (!lookup)
		return -ENOMEM;

	lookup->dev_id = KBUILD_MODNAME;
	lookup->table[0].key = chip;
	lookup->table[0].chip_hwnum = pin;
	lookup->table[0].con_id = con_id;
	lookup->table[0].flags = active_low ? GPIO_ACTIVE_LOW : GPIO_ACTIVE_HIGH;

	gpiod_add_lookup_table(lookup);
	gpiod = devm_gpiod_get(&x86_android_tablet_device->dev, con_id, dflags);
	gpiod_remove_lookup_table(lookup);
	kfree(lookup);

	if (IS_ERR(gpiod)) {
		pr_err("error %ld getting GPIO %s %d\n", PTR_ERR(gpiod), chip, pin);
		return PTR_ERR(gpiod);
	}

	if (desc)
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
		ret = x86_android_tablet_get_gpiod(data->chip, data->index, data->con_id,
						   false, GPIOD_ASIS, &gpiod);
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

		if (data->free_gpio)
			devm_gpiod_put(&x86_android_tablet_device->dev, gpiod);

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
static int spi_dev_count;
static int pdev_count;
static int serdev_count;
static struct i2c_client **i2c_clients;
static struct spi_device **spi_devs;
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

static __init int x86_instantiate_spi_dev(const struct x86_dev_info *dev_info, int idx)
{
	const struct x86_spi_dev_info *spi_dev_info = &dev_info->spi_dev_info[idx];
	struct spi_board_info board_info = spi_dev_info->board_info;
	struct spi_controller *controller;
	struct acpi_device *adev;
	acpi_handle handle;
	acpi_status status;

	board_info.irq = x86_acpi_irq_helper_get(&spi_dev_info->irq_data);
	if (board_info.irq < 0)
		return board_info.irq;

	status = acpi_get_handle(NULL, spi_dev_info->ctrl_path, &handle);
	if (ACPI_FAILURE(status)) {
		pr_err("Error could not get %s handle\n", spi_dev_info->ctrl_path);
		return -ENODEV;
	}

	adev = acpi_fetch_acpi_dev(handle);
	if (!adev) {
		pr_err("Error could not get adev for %s\n", spi_dev_info->ctrl_path);
		return -ENODEV;
	}

	controller = acpi_spi_find_controller_by_adev(adev);
	if (!controller) {
		pr_err("Error could not get SPI controller for %s\n", spi_dev_info->ctrl_path);
		return -ENODEV;
	}

	spi_devs[idx] = spi_new_device(controller, &board_info);
	put_device(&controller->dev);
	if (!spi_devs[idx])
		return dev_err_probe(&controller->dev, -ENOMEM,
				     "creating SPI-device %d\n", idx);

	return 0;
}

static __init int x86_instantiate_serdev(const struct x86_serdev_info *info, int idx)
{
	struct acpi_device *serdev_adev;
	struct serdev_device *serdev;
	struct device *ctrl_dev;
	int ret = -ENODEV;

	ctrl_dev = get_serdev_controller(info->ctrl_hid, info->ctrl_uid, 0,
					 info->ctrl_devname);
	if (IS_ERR(ctrl_dev))
		return PTR_ERR(ctrl_dev);

	serdev_adev = acpi_dev_get_first_match_dev(info->serdev_hid, NULL, -1);
	if (!serdev_adev) {
		pr_err("error could not get %s serdev adev\n", info->serdev_hid);
		goto put_ctrl_dev;
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
put_ctrl_dev:
	put_device(ctrl_dev);
	return ret;
}

static void x86_android_tablet_remove(struct platform_device *pdev)
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

	for (i = 0; i < spi_dev_count; i++)
		spi_unregister_device(spi_devs[i]);

	kfree(spi_devs);

	for (i = 0; i < i2c_client_count; i++)
		i2c_unregister_device(i2c_clients[i]);

	kfree(i2c_clients);

	if (exit_handler)
		exit_handler();

	for (i = 0; gpiod_lookup_tables && gpiod_lookup_tables[i]; i++)
		gpiod_remove_lookup_table(gpiod_lookup_tables[i]);

	software_node_unregister(bat_swnode);
}

static __init int x86_android_tablet_probe(struct platform_device *pdev)
{
	const struct x86_dev_info *dev_info;
	const struct dmi_system_id *id;
	int i, ret = 0;

	id = dmi_first_match(x86_android_tablet_ids);
	if (!id)
		return -ENODEV;

	dev_info = id->driver_data;
	/* Allow x86_android_tablet_device use before probe() exits */
	x86_android_tablet_device = pdev;

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
			x86_android_tablet_remove(pdev);
			return ret;
		}
		exit_handler = dev_info->exit;
	}

	i2c_clients = kcalloc(dev_info->i2c_client_count, sizeof(*i2c_clients), GFP_KERNEL);
	if (!i2c_clients) {
		x86_android_tablet_remove(pdev);
		return -ENOMEM;
	}

	i2c_client_count = dev_info->i2c_client_count;
	for (i = 0; i < i2c_client_count; i++) {
		ret = x86_instantiate_i2c_client(dev_info, i);
		if (ret < 0) {
			x86_android_tablet_remove(pdev);
			return ret;
		}
	}

	spi_devs = kcalloc(dev_info->spi_dev_count, sizeof(*spi_devs), GFP_KERNEL);
	if (!spi_devs) {
		x86_android_tablet_remove(pdev);
		return -ENOMEM;
	}

	spi_dev_count = dev_info->spi_dev_count;
	for (i = 0; i < spi_dev_count; i++) {
		ret = x86_instantiate_spi_dev(dev_info, i);
		if (ret < 0) {
			x86_android_tablet_remove(pdev);
			return ret;
		}
	}

	/* + 1 to make space for (optional) gpio_keys_button pdev */
	pdevs = kcalloc(dev_info->pdev_count + 1, sizeof(*pdevs), GFP_KERNEL);
	if (!pdevs) {
		x86_android_tablet_remove(pdev);
		return -ENOMEM;
	}

	pdev_count = dev_info->pdev_count;
	for (i = 0; i < pdev_count; i++) {
		pdevs[i] = platform_device_register_full(&dev_info->pdev_info[i]);
		if (IS_ERR(pdevs[i])) {
			x86_android_tablet_remove(pdev);
			return PTR_ERR(pdevs[i]);
		}
	}

	serdevs = kcalloc(dev_info->serdev_count, sizeof(*serdevs), GFP_KERNEL);
	if (!serdevs) {
		x86_android_tablet_remove(pdev);
		return -ENOMEM;
	}

	serdev_count = dev_info->serdev_count;
	for (i = 0; i < serdev_count; i++) {
		ret = x86_instantiate_serdev(&dev_info->serdev_info[i], i);
		if (ret < 0) {
			x86_android_tablet_remove(pdev);
			return ret;
		}
	}

	if (dev_info->gpio_button_count) {
		struct gpio_keys_platform_data pdata = { };
		struct gpio_desc *gpiod;

		buttons = kcalloc(dev_info->gpio_button_count, sizeof(*buttons), GFP_KERNEL);
		if (!buttons) {
			x86_android_tablet_remove(pdev);
			return -ENOMEM;
		}

		for (i = 0; i < dev_info->gpio_button_count; i++) {
			ret = x86_android_tablet_get_gpiod(dev_info->gpio_button[i].chip,
							   dev_info->gpio_button[i].pin,
							   dev_info->gpio_button[i].button.desc,
							   false, GPIOD_IN, &gpiod);
			if (ret < 0) {
				x86_android_tablet_remove(pdev);
				return ret;
			}

			buttons[i] = dev_info->gpio_button[i].button;
			buttons[i].gpio = desc_to_gpio(gpiod);
			/* Release gpiod so that gpio-keys can request it */
			devm_gpiod_put(&x86_android_tablet_device->dev, gpiod);
		}

		pdata.buttons = buttons;
		pdata.nbuttons = dev_info->gpio_button_count;

		pdevs[pdev_count] = platform_device_register_data(&pdev->dev, "gpio-keys",
								  PLATFORM_DEVID_AUTO,
								  &pdata, sizeof(pdata));
		if (IS_ERR(pdevs[pdev_count])) {
			x86_android_tablet_remove(pdev);
			return PTR_ERR(pdevs[pdev_count]);
		}
		pdev_count++;
	}

	return 0;
}

static struct platform_driver x86_android_tablet_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
	},
	.remove_new = x86_android_tablet_remove,
};

static int __init x86_android_tablet_init(void)
{
	x86_android_tablet_device = platform_create_bundle(&x86_android_tablet_driver,
						   x86_android_tablet_probe,
						   NULL, 0, NULL, 0);

	return PTR_ERR_OR_ZERO(x86_android_tablet_device);
}
module_init(x86_android_tablet_init);

static void __exit x86_android_tablet_exit(void)
{
	platform_device_unregister(x86_android_tablet_device);
	platform_driver_unregister(&x86_android_tablet_driver);
}
module_exit(x86_android_tablet_exit);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("X86 Android tablets DSDT fixups driver");
MODULE_LICENSE("GPL");
