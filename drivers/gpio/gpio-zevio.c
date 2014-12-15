/*
 * GPIO controller in LSI ZEVIO SoCs.
 *
 * Author: Fabian Vogt <fabian@ritter-vogt.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/gpio.h>

/*
 * Memory layout:
 * This chip has four gpio sections, each controls 8 GPIOs.
 * Bit 0 in section 0 is GPIO 0, bit 2 in section 1 is GPIO 10.
 * Disclaimer: Reverse engineered!
 * For more information refer to:
 * http://hackspire.unsads.com/wiki/index.php/Memory-mapped_I/O_ports#90000000_-_General_Purpose_I.2FO_.28GPIO.29
 *
 * 0x00-0x3F: Section 0
 *     +0x00: Masked interrupt status (read-only)
 *     +0x04: R: Interrupt status W: Reset interrupt status
 *     +0x08: R: Interrupt mask W: Mask interrupt
 *     +0x0C: W: Unmask interrupt (write-only)
 *     +0x10: Direction: I/O=1/0
 *     +0x14: Output
 *     +0x18: Input (read-only)
 *     +0x20: R: Level interrupt W: Set as level interrupt
 * 0x40-0x7F: Section 1
 * 0x80-0xBF: Section 2
 * 0xC0-0xFF: Section 3
 */

#define ZEVIO_GPIO_SECTION_SIZE			0x40

/* Offsets to various registers */
#define ZEVIO_GPIO_INT_MASKED_STATUS	0x00
#define ZEVIO_GPIO_INT_STATUS		0x04
#define ZEVIO_GPIO_INT_UNMASK		0x08
#define ZEVIO_GPIO_INT_MASK		0x0C
#define ZEVIO_GPIO_DIRECTION		0x10
#define ZEVIO_GPIO_OUTPUT		0x14
#define ZEVIO_GPIO_INPUT			0x18
#define ZEVIO_GPIO_INT_STICKY		0x20

#define to_zevio_gpio(chip) container_of(to_of_mm_gpio_chip(chip), \
				struct zevio_gpio, chip)

/* Bit number of GPIO in its section */
#define ZEVIO_GPIO_BIT(gpio) (gpio&7)

struct zevio_gpio {
	spinlock_t		lock;
	struct of_mm_gpio_chip	chip;
};

static inline u32 zevio_gpio_port_get(struct zevio_gpio *c, unsigned pin,
					unsigned port_offset)
{
	unsigned section_offset = ((pin >> 3) & 3)*ZEVIO_GPIO_SECTION_SIZE;
	return readl(IOMEM(c->chip.regs + section_offset + port_offset));
}

static inline void zevio_gpio_port_set(struct zevio_gpio *c, unsigned pin,
					unsigned port_offset, u32 val)
{
	unsigned section_offset = ((pin >> 3) & 3)*ZEVIO_GPIO_SECTION_SIZE;
	writel(val, IOMEM(c->chip.regs + section_offset + port_offset));
}

/* Functions for struct gpio_chip */
static int zevio_gpio_get(struct gpio_chip *chip, unsigned pin)
{
	struct zevio_gpio *controller = to_zevio_gpio(chip);
	u32 val, dir;

	spin_lock(&controller->lock);
	dir = zevio_gpio_port_get(controller, pin, ZEVIO_GPIO_DIRECTION);
	if (dir & BIT(ZEVIO_GPIO_BIT(pin)))
		val = zevio_gpio_port_get(controller, pin, ZEVIO_GPIO_INPUT);
	else
		val = zevio_gpio_port_get(controller, pin, ZEVIO_GPIO_OUTPUT);
	spin_unlock(&controller->lock);

	return (val >> ZEVIO_GPIO_BIT(pin)) & 0x1;
}

static void zevio_gpio_set(struct gpio_chip *chip, unsigned pin, int value)
{
	struct zevio_gpio *controller = to_zevio_gpio(chip);
	u32 val;

	spin_lock(&controller->lock);
	val = zevio_gpio_port_get(controller, pin, ZEVIO_GPIO_OUTPUT);
	if (value)
		val |= BIT(ZEVIO_GPIO_BIT(pin));
	else
		val &= ~BIT(ZEVIO_GPIO_BIT(pin));

	zevio_gpio_port_set(controller, pin, ZEVIO_GPIO_OUTPUT, val);
	spin_unlock(&controller->lock);
}

static int zevio_gpio_direction_input(struct gpio_chip *chip, unsigned pin)
{
	struct zevio_gpio *controller = to_zevio_gpio(chip);
	u32 val;

	spin_lock(&controller->lock);

	val = zevio_gpio_port_get(controller, pin, ZEVIO_GPIO_DIRECTION);
	val |= BIT(ZEVIO_GPIO_BIT(pin));
	zevio_gpio_port_set(controller, pin, ZEVIO_GPIO_DIRECTION, val);

	spin_unlock(&controller->lock);

	return 0;
}

static int zevio_gpio_direction_output(struct gpio_chip *chip,
				       unsigned pin, int value)
{
	struct zevio_gpio *controller = to_zevio_gpio(chip);
	u32 val;

	spin_lock(&controller->lock);
	val = zevio_gpio_port_get(controller, pin, ZEVIO_GPIO_OUTPUT);
	if (value)
		val |= BIT(ZEVIO_GPIO_BIT(pin));
	else
		val &= ~BIT(ZEVIO_GPIO_BIT(pin));

	zevio_gpio_port_set(controller, pin, ZEVIO_GPIO_OUTPUT, val);
	val = zevio_gpio_port_get(controller, pin, ZEVIO_GPIO_DIRECTION);
	val &= ~BIT(ZEVIO_GPIO_BIT(pin));
	zevio_gpio_port_set(controller, pin, ZEVIO_GPIO_DIRECTION, val);

	spin_unlock(&controller->lock);

	return 0;
}

static int zevio_gpio_to_irq(struct gpio_chip *chip, unsigned pin)
{
	/*
	 * TODO: Implement IRQs.
	 * Not implemented yet due to weird lockups
	 */

	return -ENXIO;
}

static struct gpio_chip zevio_gpio_chip = {
	.direction_input	= zevio_gpio_direction_input,
	.direction_output	= zevio_gpio_direction_output,
	.set			= zevio_gpio_set,
	.get			= zevio_gpio_get,
	.to_irq			= zevio_gpio_to_irq,
	.base			= 0,
	.owner			= THIS_MODULE,
	.ngpio			= 32,
	.of_gpio_n_cells	= 2,
};

/* Initialization */
static int zevio_gpio_probe(struct platform_device *pdev)
{
	struct zevio_gpio *controller;
	int status, i;

	controller = devm_kzalloc(&pdev->dev, sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return -ENOMEM;

	/* Copy our reference */
	controller->chip.gc = zevio_gpio_chip;
	controller->chip.gc.dev = &pdev->dev;

	status = of_mm_gpiochip_add(pdev->dev.of_node, &(controller->chip));
	if (status) {
		dev_err(&pdev->dev, "failed to add gpiochip: %d\n", status);
		return status;
	}

	spin_lock_init(&controller->lock);

	/* Disable interrupts, they only cause errors */
	for (i = 0; i < controller->chip.gc.ngpio; i += 8)
		zevio_gpio_port_set(controller, i, ZEVIO_GPIO_INT_MASK, 0xFF);

	dev_dbg(controller->chip.gc.dev, "ZEVIO GPIO controller set up!\n");

	return 0;
}

static const struct of_device_id zevio_gpio_of_match[] = {
	{ .compatible = "lsi,zevio-gpio", },
	{ },
};

MODULE_DEVICE_TABLE(of, zevio_gpio_of_match);

static struct platform_driver zevio_gpio_driver = {
	.driver		= {
		.name	= "gpio-zevio",
		.of_match_table = zevio_gpio_of_match,
	},
	.probe		= zevio_gpio_probe,
};
module_platform_driver(zevio_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fabian Vogt <fabian@ritter-vogt.de>");
MODULE_DESCRIPTION("LSI ZEVIO SoC GPIO driver");
