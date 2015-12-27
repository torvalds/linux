/*
 * GPIO driver for the ACCES 104-IDIO-16 family
 * Copyright (C) 2015 William Breathitt Gray
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

static unsigned idio_16_base;
module_param(idio_16_base, uint, 0);
MODULE_PARM_DESC(idio_16_base, "ACCES 104-IDIO-16 base address");

/**
 * struct idio_16_gpio - GPIO device private data structure
 * @chip:	instance of the gpio_chip
 * @lock:	synchronization lock to prevent gpio_set race conditions
 * @base:	base port address of the GPIO device
 * @extent:	extent of port address region of the GPIO device
 * @out_state:	output bits state
 */
struct idio_16_gpio {
	struct gpio_chip chip;
	spinlock_t lock;
	unsigned base;
	unsigned extent;
	unsigned out_state;
};

static int idio_16_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	if (offset > 15)
		return 1;

	return 0;
}

static int idio_16_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}

static int idio_16_gpio_direction_output(struct gpio_chip *chip,
	unsigned offset, int value)
{
	chip->set(chip, offset, value);
	return 0;
}

static struct idio_16_gpio *to_idio16gpio(struct gpio_chip *gc)
{
	return container_of(gc, struct idio_16_gpio, chip);
}

static int idio_16_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct idio_16_gpio *const idio16gpio = to_idio16gpio(chip);
	const unsigned BIT_MASK = 1U << (offset-16);

	if (offset < 16)
		return -EINVAL;

	if (offset < 24)
		return !!(inb(idio16gpio->base + 1) & BIT_MASK);

	return !!(inb(idio16gpio->base + 5) & (BIT_MASK>>8));
}

static void idio_16_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct idio_16_gpio *const idio16gpio = to_idio16gpio(chip);
	const unsigned BIT_MASK = 1U << offset;
	unsigned long flags;

	if (offset > 15)
		return;

	spin_lock_irqsave(&idio16gpio->lock, flags);

	if (value)
		idio16gpio->out_state |= BIT_MASK;
	else
		idio16gpio->out_state &= ~BIT_MASK;

	if (offset > 7)
		outb(idio16gpio->out_state >> 8, idio16gpio->base + 4);
	else
		outb(idio16gpio->out_state, idio16gpio->base);

	spin_unlock_irqrestore(&idio16gpio->lock, flags);
}

static int __init idio_16_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct idio_16_gpio *idio16gpio;
	int err;

	const unsigned BASE = idio_16_base;
	const unsigned EXTENT = 8;
	const char *const NAME = dev_name(dev);

	idio16gpio = devm_kzalloc(dev, sizeof(*idio16gpio), GFP_KERNEL);
	if (!idio16gpio)
		return -ENOMEM;

	if (!request_region(BASE, EXTENT, NAME)) {
		dev_err(dev, "Unable to lock %s port addresses (0x%X-0x%X)\n",
			NAME, BASE, BASE + EXTENT);
		err = -EBUSY;
		goto err_lock_io_port;
	}

	idio16gpio->chip.label = NAME;
	idio16gpio->chip.dev = dev;
	idio16gpio->chip.owner = THIS_MODULE;
	idio16gpio->chip.base = -1;
	idio16gpio->chip.ngpio = 32;
	idio16gpio->chip.get_direction = idio_16_gpio_get_direction;
	idio16gpio->chip.direction_input = idio_16_gpio_direction_input;
	idio16gpio->chip.direction_output = idio_16_gpio_direction_output;
	idio16gpio->chip.get = idio_16_gpio_get;
	idio16gpio->chip.set = idio_16_gpio_set;
	idio16gpio->base = BASE;
	idio16gpio->extent = EXTENT;
	idio16gpio->out_state = 0xFFFF;

	spin_lock_init(&idio16gpio->lock);

	dev_set_drvdata(dev, idio16gpio);

	err = gpiochip_add(&idio16gpio->chip);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
		goto err_gpio_register;
	}

	return 0;

err_gpio_register:
	release_region(BASE, EXTENT);
err_lock_io_port:
	return err;
}

static int idio_16_remove(struct platform_device *pdev)
{
	struct idio_16_gpio *const idio16gpio = platform_get_drvdata(pdev);

	gpiochip_remove(&idio16gpio->chip);
	release_region(idio16gpio->base, idio16gpio->extent);

	return 0;
}

static struct platform_device *idio_16_device;

static struct platform_driver idio_16_driver = {
	.driver = {
		.name = "104-idio-16"
	},
	.remove = idio_16_remove
};

static void __exit idio_16_exit(void)
{
	platform_device_unregister(idio_16_device);
	platform_driver_unregister(&idio_16_driver);
}

static int __init idio_16_init(void)
{
	int err;

	idio_16_device = platform_device_alloc(idio_16_driver.driver.name, -1);
	if (!idio_16_device)
		return -ENOMEM;

	err = platform_device_add(idio_16_device);
	if (err)
		goto err_platform_device;

	err = platform_driver_probe(&idio_16_driver, idio_16_probe);
	if (err)
		goto err_platform_driver;

	return 0;

err_platform_driver:
	platform_device_del(idio_16_device);
err_platform_device:
	platform_device_put(idio_16_device);
	return err;
}

module_init(idio_16_init);
module_exit(idio_16_exit);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("ACCES 104-IDIO-16 GPIO driver");
MODULE_LICENSE("GPL");
