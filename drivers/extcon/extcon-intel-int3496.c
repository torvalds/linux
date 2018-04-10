/*
 * Intel INT3496 ACPI device extcon driver
 *
 * Copyright (c) 2016 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on android x86 kernel code which is:
 *
 * Copyright (c) 2014, Intel Corporation.
 * Author: David Cohen <david.a.cohen@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/acpi.h>
#include <linux/extcon-provider.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define INT3496_GPIO_USB_ID	0
#define INT3496_GPIO_VBUS_EN	1
#define INT3496_GPIO_USB_MUX	2
#define DEBOUNCE_TIME		msecs_to_jiffies(50)

struct int3496_data {
	struct device *dev;
	struct extcon_dev *edev;
	struct delayed_work work;
	struct gpio_desc *gpio_usb_id;
	struct gpio_desc *gpio_vbus_en;
	struct gpio_desc *gpio_usb_mux;
	int usb_id_irq;
};

static const unsigned int int3496_cable[] = {
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static const struct acpi_gpio_params id_gpios = { INT3496_GPIO_USB_ID, 0, false };
static const struct acpi_gpio_params vbus_gpios = { INT3496_GPIO_VBUS_EN, 0, false };
static const struct acpi_gpio_params mux_gpios = { INT3496_GPIO_USB_MUX, 0, false };

static const struct acpi_gpio_mapping acpi_int3496_default_gpios[] = {
	/*
	 * Some platforms have a bug in ACPI GPIO description making IRQ
	 * GPIO to be output only. Ask the GPIO core to ignore this limit.
	 */
	{ "id-gpios", &id_gpios, 1, ACPI_GPIO_QUIRK_NO_IO_RESTRICTION },
	{ "vbus-gpios", &vbus_gpios, 1 },
	{ "mux-gpios", &mux_gpios, 1 },
	{ },
};

static void int3496_do_usb_id(struct work_struct *work)
{
	struct int3496_data *data =
		container_of(work, struct int3496_data, work.work);
	int id = gpiod_get_value_cansleep(data->gpio_usb_id);

	/* id == 1: PERIPHERAL, id == 0: HOST */
	dev_dbg(data->dev, "Connected %s cable\n", id ? "PERIPHERAL" : "HOST");

	/*
	 * Peripheral: set USB mux to peripheral and disable VBUS
	 * Host: set USB mux to host and enable VBUS
	 */
	if (!IS_ERR(data->gpio_usb_mux))
		gpiod_direction_output(data->gpio_usb_mux, id);

	if (!IS_ERR(data->gpio_vbus_en))
		gpiod_direction_output(data->gpio_vbus_en, !id);

	extcon_set_state_sync(data->edev, EXTCON_USB_HOST, !id);
}

static irqreturn_t int3496_thread_isr(int irq, void *priv)
{
	struct int3496_data *data = priv;

	/* Let the pin settle before processing it */
	mod_delayed_work(system_wq, &data->work, DEBOUNCE_TIME);

	return IRQ_HANDLED;
}

static int int3496_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct int3496_data *data;
	int ret;

	ret = devm_acpi_dev_add_driver_gpios(dev, acpi_int3496_default_gpios);
	if (ret) {
		dev_err(dev, "can't add GPIO ACPI mapping\n");
		return ret;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	INIT_DELAYED_WORK(&data->work, int3496_do_usb_id);

	data->gpio_usb_id = devm_gpiod_get(dev, "id", GPIOD_IN);
	if (IS_ERR(data->gpio_usb_id)) {
		ret = PTR_ERR(data->gpio_usb_id);
		dev_err(dev, "can't request USB ID GPIO: %d\n", ret);
		return ret;
	}

	data->usb_id_irq = gpiod_to_irq(data->gpio_usb_id);
	if (data->usb_id_irq < 0) {
		dev_err(dev, "can't get USB ID IRQ: %d\n", data->usb_id_irq);
		return data->usb_id_irq;
	}

	data->gpio_vbus_en = devm_gpiod_get(dev, "vbus", GPIOD_ASIS);
	if (IS_ERR(data->gpio_vbus_en))
		dev_info(dev, "can't request VBUS EN GPIO\n");

	data->gpio_usb_mux = devm_gpiod_get(dev, "mux", GPIOD_ASIS);
	if (IS_ERR(data->gpio_usb_mux))
		dev_info(dev, "can't request USB MUX GPIO\n");

	/* register extcon device */
	data->edev = devm_extcon_dev_allocate(dev, int3496_cable);
	if (IS_ERR(data->edev))
		return -ENOMEM;

	ret = devm_extcon_dev_register(dev, data->edev);
	if (ret < 0) {
		dev_err(dev, "can't register extcon device: %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(dev, data->usb_id_irq,
					NULL, int3496_thread_isr,
					IRQF_SHARED | IRQF_ONESHOT |
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING,
					dev_name(dev), data);
	if (ret < 0) {
		dev_err(dev, "can't request IRQ for USB ID GPIO: %d\n", ret);
		return ret;
	}

	/* process id-pin so that we start with the right status */
	queue_delayed_work(system_wq, &data->work, 0);
	flush_delayed_work(&data->work);

	platform_set_drvdata(pdev, data);

	return 0;
}

static int int3496_remove(struct platform_device *pdev)
{
	struct int3496_data *data = platform_get_drvdata(pdev);

	devm_free_irq(&pdev->dev, data->usb_id_irq, data);
	cancel_delayed_work_sync(&data->work);

	return 0;
}

static const struct acpi_device_id int3496_acpi_match[] = {
	{ "INT3496" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, int3496_acpi_match);

static struct platform_driver int3496_driver = {
	.driver = {
		.name = "intel-int3496",
		.acpi_match_table = int3496_acpi_match,
	},
	.probe = int3496_probe,
	.remove = int3496_remove,
};

module_platform_driver(int3496_driver);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Intel INT3496 ACPI device extcon driver");
MODULE_LICENSE("GPL");
