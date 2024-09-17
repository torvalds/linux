// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO controller in LSI ZEVIO SoCs.
 *
 * Author: Fabian Vogt <fabian@ritter-vogt.de>
 */

#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/gpio/driver.h>

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

/* Bit number of GPIO in its section */
#define ZEVIO_GPIO_BIT(gpio) (gpio&7)

struct zevio_gpio {
	struct gpio_chip        chip;
	spinlock_t		lock;
	void __iomem		*regs;
};

static inline u32 zevio_gpio_port_get(struct zevio_gpio *c, unsigned pin,
					unsigned port_offset)
{
	unsigned section_offset = ((pin >> 3) & 3)*ZEVIO_GPIO_SECTION_SIZE;
	return readl(IOMEM(c->regs + section_offset + port_offset));
}

static inline void zevio_gpio_port_set(struct zevio_gpio *c, unsigned pin,
					unsigned port_offset, u32 val)
{
	unsigned section_offset = ((pin >> 3) & 3)*ZEVIO_GPIO_SECTION_SIZE;
	writel(val, IOMEM(c->regs + section_offset + port_offset));
}

/* Functions for struct gpio_chip */
static int zevio_gpio_get(struct gpio_chip *chip, unsigned pin)
{
	struct zevio_gpio *controller = gpiochip_get_data(chip);
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
	struct zevio_gpio *controller = gpiochip_get_data(chip);
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
	struct zevio_gpio *controller = gpiochip_get_data(chip);
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
	struct zevio_gpio *controller = gpiochip_get_data(chip);
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

static const struct gpio_chip zevio_gpio_chip = {
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

	platform_set_drvdata(pdev, controller);

	/* Copy our reference */
	controller->chip = zevio_gpio_chip;
	controller->chip.parent = &pdev->dev;

	controller->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(controller->regs))
		return dev_err_probe(&pdev->dev, PTR_ERR(controller->regs),
				     "failed to ioremap memory resource\n");

	status = devm_gpiochip_add_data(&pdev->dev, &controller->chip, controller);
	if (status) {
		dev_err(&pdev->dev, "failed to add gpiochip: %d\n", status);
		return status;
	}

	spin_lock_init(&controller->lock);

	/* Disable interrupts, they only cause errors */
	for (i = 0; i < controller->chip.ngpio; i += 8)
		zevio_gpio_port_set(controller, i, ZEVIO_GPIO_INT_MASK, 0xFF);

	dev_dbg(controller->chip.parent, "ZEVIO GPIO controller set up!\n");

	return 0;
}

static const struct of_device_id zevio_gpio_of_match[] = {
	{ .compatible = "lsi,zevio-gpio", },
	{ },
};

static struct platform_driver zevio_gpio_driver = {
	.driver		= {
		.name	= "gpio-zevio",
		.of_match_table = zevio_gpio_of_match,
		.suppress_bind_attrs = true,
	},
	.probe		= zevio_gpio_probe,
};
builtin_platform_driver(zevio_gpio_driver);
