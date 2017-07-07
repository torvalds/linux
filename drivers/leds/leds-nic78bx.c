/*
 * Copyright (C) 2016 National Instruments Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/acpi.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define NIC78BX_USER1_LED_MASK		0x3
#define NIC78BX_USER1_GREEN_LED		BIT(0)
#define NIC78BX_USER1_YELLOW_LED	BIT(1)

#define NIC78BX_USER2_LED_MASK		0xC
#define NIC78BX_USER2_GREEN_LED		BIT(2)
#define NIC78BX_USER2_YELLOW_LED	BIT(3)

#define NIC78BX_LOCK_REG_OFFSET		1
#define NIC78BX_LOCK_VALUE		0xA5
#define NIC78BX_UNLOCK_VALUE		0x5A

#define NIC78BX_USER_LED_IO_SIZE	2

struct nic78bx_led_data {
	u16 io_base;
	spinlock_t lock;
	struct platform_device *pdev;
};

struct nic78bx_led {
	u8 bit;
	u8 mask;
	struct nic78bx_led_data *data;
	struct led_classdev cdev;
};

static inline struct nic78bx_led *to_nic78bx_led(struct led_classdev *cdev)
{
	return container_of(cdev, struct nic78bx_led, cdev);
}

static void nic78bx_brightness_set(struct led_classdev *cdev,
				  enum led_brightness brightness)
{
	struct nic78bx_led *nled = to_nic78bx_led(cdev);
	unsigned long flags;
	u8 value;

	spin_lock_irqsave(&nled->data->lock, flags);
	value = inb(nled->data->io_base);

	if (brightness) {
		value &= ~nled->mask;
		value |= nled->bit;
	} else {
		value &= ~nled->bit;
	}

	outb(value, nled->data->io_base);
	spin_unlock_irqrestore(&nled->data->lock, flags);
}

static enum led_brightness nic78bx_brightness_get(struct led_classdev *cdev)
{
	struct nic78bx_led *nled = to_nic78bx_led(cdev);
	unsigned long flags;
	u8 value;

	spin_lock_irqsave(&nled->data->lock, flags);
	value = inb(nled->data->io_base);
	spin_unlock_irqrestore(&nled->data->lock, flags);

	return (value & nled->bit) ? 1 : LED_OFF;
}

static struct nic78bx_led nic78bx_leds[] = {
	{
		.bit = NIC78BX_USER1_GREEN_LED,
		.mask = NIC78BX_USER1_LED_MASK,
		.cdev = {
			.name = "nilrt:green:user1",
			.max_brightness = 1,
			.brightness_set = nic78bx_brightness_set,
			.brightness_get = nic78bx_brightness_get,
		}
	},
	{
		.bit = NIC78BX_USER1_YELLOW_LED,
		.mask = NIC78BX_USER1_LED_MASK,
		.cdev = {
			.name = "nilrt:yellow:user1",
			.max_brightness = 1,
			.brightness_set = nic78bx_brightness_set,
			.brightness_get = nic78bx_brightness_get,
		}
	},
	{
		.bit = NIC78BX_USER2_GREEN_LED,
		.mask = NIC78BX_USER2_LED_MASK,
		.cdev = {
			.name = "nilrt:green:user2",
			.max_brightness = 1,
			.brightness_set = nic78bx_brightness_set,
			.brightness_get = nic78bx_brightness_get,
		}
	},
	{
		.bit = NIC78BX_USER2_YELLOW_LED,
		.mask = NIC78BX_USER2_LED_MASK,
		.cdev = {
			.name = "nilrt:yellow:user2",
			.max_brightness = 1,
			.brightness_set = nic78bx_brightness_set,
			.brightness_get = nic78bx_brightness_get,
		}
	}
};

static int nic78bx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nic78bx_led_data *led_data;
	struct resource *io_rc;
	int ret, i;

	led_data = devm_kzalloc(dev, sizeof(*led_data), GFP_KERNEL);
	if (!led_data)
		return -ENOMEM;

	led_data->pdev = pdev;
	platform_set_drvdata(pdev, led_data);

	io_rc = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!io_rc) {
		dev_err(dev, "missing IO resources\n");
		return -EINVAL;
	}

	if (resource_size(io_rc) < NIC78BX_USER_LED_IO_SIZE) {
		dev_err(dev, "IO region too small\n");
		return -EINVAL;
	}

	if (!devm_request_region(dev, io_rc->start, resource_size(io_rc),
				 KBUILD_MODNAME)) {
		dev_err(dev, "failed to get IO region\n");
		return -EBUSY;
	}

	led_data->io_base = io_rc->start;
	spin_lock_init(&led_data->lock);

	for (i = 0; i < ARRAY_SIZE(nic78bx_leds); i++) {
		nic78bx_leds[i].data = led_data;

		ret = devm_led_classdev_register(dev, &nic78bx_leds[i].cdev);
		if (ret)
			return ret;
	}

	/* Unlock LED register */
	outb(NIC78BX_UNLOCK_VALUE,
	     led_data->io_base + NIC78BX_LOCK_REG_OFFSET);

	return ret;
}

static int nic78bx_remove(struct platform_device *pdev)
{
	struct nic78bx_led_data *led_data = platform_get_drvdata(pdev);

	/* Lock LED register */
	outb(NIC78BX_LOCK_VALUE,
	     led_data->io_base + NIC78BX_LOCK_REG_OFFSET);

	return 0;
}

static const struct acpi_device_id led_device_ids[] = {
	{"NIC78B3", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, led_device_ids);

static struct platform_driver led_driver = {
	.probe = nic78bx_probe,
	.remove = nic78bx_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.acpi_match_table = ACPI_PTR(led_device_ids),
	},
};

module_platform_driver(led_driver);

MODULE_DESCRIPTION("National Instruments PXI User LEDs driver");
MODULE_AUTHOR("Hui Chun Ong <hui.chun.ong@ni.com>");
MODULE_LICENSE("GPL");
