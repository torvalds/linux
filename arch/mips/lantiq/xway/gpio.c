/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/ioport.h>
#include <linux/io.h>

#include <lantiq_soc.h>

#define LTQ_GPIO_OUT		0x00
#define LTQ_GPIO_IN		0x04
#define LTQ_GPIO_DIR		0x08
#define LTQ_GPIO_ALTSEL0	0x0C
#define LTQ_GPIO_ALTSEL1	0x10
#define LTQ_GPIO_OD		0x14

#define PINS_PER_PORT		16
#define MAX_PORTS		3

#define ltq_gpio_getbit(m, r, p)	(!!(ltq_r32(m + r) & (1 << p)))
#define ltq_gpio_setbit(m, r, p)	ltq_w32_mask(0, (1 << p), m + r)
#define ltq_gpio_clearbit(m, r, p)	ltq_w32_mask((1 << p), 0, m + r)

struct ltq_gpio {
	void __iomem *membase;
	struct gpio_chip chip;
};

static struct ltq_gpio ltq_gpio_port[MAX_PORTS];

int ltq_gpio_request(unsigned int pin, unsigned int alt0,
	unsigned int alt1, unsigned int dir, const char *name)
{
	int id = 0;

	if (pin >= (MAX_PORTS * PINS_PER_PORT))
		return -EINVAL;
	if (gpio_request(pin, name)) {
		pr_err("failed to setup lantiq gpio: %s\n", name);
		return -EBUSY;
	}
	if (dir)
		gpio_direction_output(pin, 1);
	else
		gpio_direction_input(pin);
	while (pin >= PINS_PER_PORT) {
		pin -= PINS_PER_PORT;
		id++;
	}
	if (alt0)
		ltq_gpio_setbit(ltq_gpio_port[id].membase,
			LTQ_GPIO_ALTSEL0, pin);
	else
		ltq_gpio_clearbit(ltq_gpio_port[id].membase,
			LTQ_GPIO_ALTSEL0, pin);
	if (alt1)
		ltq_gpio_setbit(ltq_gpio_port[id].membase,
			LTQ_GPIO_ALTSEL1, pin);
	else
		ltq_gpio_clearbit(ltq_gpio_port[id].membase,
			LTQ_GPIO_ALTSEL1, pin);
	return 0;
}
EXPORT_SYMBOL(ltq_gpio_request);

static void ltq_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct ltq_gpio *ltq_gpio = container_of(chip, struct ltq_gpio, chip);

	if (value)
		ltq_gpio_setbit(ltq_gpio->membase, LTQ_GPIO_OUT, offset);
	else
		ltq_gpio_clearbit(ltq_gpio->membase, LTQ_GPIO_OUT, offset);
}

static int ltq_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct ltq_gpio *ltq_gpio = container_of(chip, struct ltq_gpio, chip);

	return ltq_gpio_getbit(ltq_gpio->membase, LTQ_GPIO_IN, offset);
}

static int ltq_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct ltq_gpio *ltq_gpio = container_of(chip, struct ltq_gpio, chip);

	ltq_gpio_clearbit(ltq_gpio->membase, LTQ_GPIO_OD, offset);
	ltq_gpio_clearbit(ltq_gpio->membase, LTQ_GPIO_DIR, offset);

	return 0;
}

static int ltq_gpio_direction_output(struct gpio_chip *chip,
	unsigned int offset, int value)
{
	struct ltq_gpio *ltq_gpio = container_of(chip, struct ltq_gpio, chip);

	ltq_gpio_setbit(ltq_gpio->membase, LTQ_GPIO_OD, offset);
	ltq_gpio_setbit(ltq_gpio->membase, LTQ_GPIO_DIR, offset);
	ltq_gpio_set(chip, offset, value);

	return 0;
}

static int ltq_gpio_req(struct gpio_chip *chip, unsigned offset)
{
	struct ltq_gpio *ltq_gpio = container_of(chip, struct ltq_gpio, chip);

	ltq_gpio_clearbit(ltq_gpio->membase, LTQ_GPIO_ALTSEL0, offset);
	ltq_gpio_clearbit(ltq_gpio->membase, LTQ_GPIO_ALTSEL1, offset);
	return 0;
}

static int ltq_gpio_probe(struct platform_device *pdev)
{
	struct resource *res;

	if (pdev->id >= MAX_PORTS) {
		dev_err(&pdev->dev, "invalid gpio port %d\n",
			pdev->id);
		return -EINVAL;
	}
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get memory for gpio port %d\n",
			pdev->id);
		return -ENOENT;
	}
	res = devm_request_mem_region(&pdev->dev, res->start,
		resource_size(res), dev_name(&pdev->dev));
	if (!res) {
		dev_err(&pdev->dev,
			"failed to request memory for gpio port %d\n",
			pdev->id);
		return -EBUSY;
	}
	ltq_gpio_port[pdev->id].membase = devm_ioremap_nocache(&pdev->dev,
		res->start, resource_size(res));
	if (!ltq_gpio_port[pdev->id].membase) {
		dev_err(&pdev->dev, "failed to remap memory for gpio port %d\n",
			pdev->id);
		return -ENOMEM;
	}
	ltq_gpio_port[pdev->id].chip.label = "ltq_gpio";
	ltq_gpio_port[pdev->id].chip.direction_input = ltq_gpio_direction_input;
	ltq_gpio_port[pdev->id].chip.direction_output =
		ltq_gpio_direction_output;
	ltq_gpio_port[pdev->id].chip.get = ltq_gpio_get;
	ltq_gpio_port[pdev->id].chip.set = ltq_gpio_set;
	ltq_gpio_port[pdev->id].chip.request = ltq_gpio_req;
	ltq_gpio_port[pdev->id].chip.base = PINS_PER_PORT * pdev->id;
	ltq_gpio_port[pdev->id].chip.ngpio = PINS_PER_PORT;
	platform_set_drvdata(pdev, &ltq_gpio_port[pdev->id]);
	return gpiochip_add(&ltq_gpio_port[pdev->id].chip);
}

static struct platform_driver
ltq_gpio_driver = {
	.probe = ltq_gpio_probe,
	.driver = {
		.name = "ltq_gpio",
		.owner = THIS_MODULE,
	},
};

int __init ltq_gpio_init(void)
{
	int ret = platform_driver_register(&ltq_gpio_driver);

	if (ret)
		pr_info("ltq_gpio : Error registering platfom driver!");
	return ret;
}

postcore_initcall(ltq_gpio_init);
