/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: BIBEK BASU <bibek.basu@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/mfd/ab8500.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/ab8500/gpio.h>

/*
 * GPIO registers offset
 * Bank: 0x10
 */
#define AB8500_GPIO_SEL1_REG	0x00
#define AB8500_GPIO_SEL2_REG	0x01
#define AB8500_GPIO_SEL3_REG	0x02
#define AB8500_GPIO_SEL4_REG	0x03
#define AB8500_GPIO_SEL5_REG	0x04
#define AB8500_GPIO_SEL6_REG	0x05

#define AB8500_GPIO_DIR1_REG	0x10
#define AB8500_GPIO_DIR2_REG	0x11
#define AB8500_GPIO_DIR3_REG	0x12
#define AB8500_GPIO_DIR4_REG	0x13
#define AB8500_GPIO_DIR5_REG	0x14
#define AB8500_GPIO_DIR6_REG	0x15

#define AB8500_GPIO_OUT1_REG	0x20
#define AB8500_GPIO_OUT2_REG	0x21
#define AB8500_GPIO_OUT3_REG	0x22
#define AB8500_GPIO_OUT4_REG	0x23
#define AB8500_GPIO_OUT5_REG	0x24
#define AB8500_GPIO_OUT6_REG	0x25

#define AB8500_GPIO_PUD1_REG	0x30
#define AB8500_GPIO_PUD2_REG	0x31
#define AB8500_GPIO_PUD3_REG	0x32
#define AB8500_GPIO_PUD4_REG	0x33
#define AB8500_GPIO_PUD5_REG	0x34
#define AB8500_GPIO_PUD6_REG	0x35

#define AB8500_GPIO_IN1_REG	0x40
#define AB8500_GPIO_IN2_REG	0x41
#define AB8500_GPIO_IN3_REG	0x42
#define AB8500_GPIO_IN4_REG	0x43
#define AB8500_GPIO_IN5_REG	0x44
#define AB8500_GPIO_IN6_REG	0x45
#define AB8500_GPIO_ALTFUN_REG	0x45
#define ALTFUN_REG_INDEX	6
#define AB8500_NUM_GPIO		42
#define AB8500_NUM_VIR_GPIO_IRQ	16

enum ab8500_gpio_action {
	NONE,
	STARTUP,
	SHUTDOWN,
	MASK,
	UNMASK
};

struct ab8500_gpio {
	struct gpio_chip chip;
	struct ab8500 *parent;
	struct device *dev;
	struct mutex lock;
	u32 irq_base;
	enum ab8500_gpio_action irq_action;
	u16 rising;
	u16 falling;
};
/**
 * to_ab8500_gpio() - get the pointer to ab8500_gpio
 * @chip:	Member of the structure ab8500_gpio
 */
static inline struct ab8500_gpio *to_ab8500_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct ab8500_gpio, chip);
}

static int ab8500_gpio_set_bits(struct gpio_chip *chip, u8 reg,
					unsigned offset, int val)
{
	struct ab8500_gpio *ab8500_gpio = to_ab8500_gpio(chip);
	u8 pos = offset % 8;
	int ret;

	reg = reg + (offset / 8);
	ret = abx500_mask_and_set_register_interruptible(ab8500_gpio->dev,
				AB8500_MISC, reg, 1 << pos, val << pos);
	if (ret < 0)
		dev_err(ab8500_gpio->dev, "%s write failed\n", __func__);
	return ret;
}
/**
 * ab8500_gpio_get() - Get the particular GPIO value
 * @chip: Gpio device
 * @offset: GPIO number to read
 */
static int ab8500_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct ab8500_gpio *ab8500_gpio = to_ab8500_gpio(chip);
	u8 mask = 1 << (offset % 8);
	u8 reg = AB8500_GPIO_OUT1_REG + (offset / 8);
	int ret;
	u8 data;
	ret = abx500_get_register_interruptible(ab8500_gpio->dev, AB8500_MISC,
						reg, &data);
	if (ret < 0) {
		dev_err(ab8500_gpio->dev, "%s read failed\n", __func__);
		return ret;
	}
	return (data & mask) >> (offset % 8);
}

static void ab8500_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct ab8500_gpio *ab8500_gpio = to_ab8500_gpio(chip);
	int ret;
	/* Write the data */
	ret = ab8500_gpio_set_bits(chip, AB8500_GPIO_OUT1_REG, offset, 1);
	if (ret < 0)
		dev_err(ab8500_gpio->dev, "%s write failed\n", __func__);
}

static int ab8500_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
					int val)
{
	int ret;
	/* set direction as output */
	ret = ab8500_gpio_set_bits(chip, AB8500_GPIO_DIR1_REG, offset, 1);
	if (ret < 0)
		return ret;
	/* disable pull down */
	ret = ab8500_gpio_set_bits(chip, AB8500_GPIO_PUD1_REG, offset, 1);
	if (ret < 0)
		return ret;
	/* set the output as 1 or 0 */
	return ab8500_gpio_set_bits(chip, AB8500_GPIO_OUT1_REG, offset, val);

}

static int ab8500_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	/* set the register as input */
	return ab8500_gpio_set_bits(chip, AB8500_GPIO_DIR1_REG, offset, 0);
}

static int ab8500_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	/*
	 * Only some GPIOs are interrupt capable, and they are
	 * organized in discontiguous clusters:
	 *
	 *	GPIO6 to GPIO13
	 *	GPIO24 and GPIO25
	 *	GPIO36 to GPIO41
	 */
	static struct ab8500_gpio_irq_cluster {
		int start;
		int end;
	} clusters[] = {
		{.start = 6,  .end = 13},
		{.start = 24, .end = 25},
		{.start = 36, .end = 41},
	};
	struct ab8500_gpio *ab8500_gpio = to_ab8500_gpio(chip);
	int base = ab8500_gpio->irq_base;
	int i;

	for (i = 0; i < ARRAY_SIZE(clusters); i++) {
		struct ab8500_gpio_irq_cluster *cluster = &clusters[i];

		if (offset >= cluster->start && offset <= cluster->end)
			return base + offset - cluster->start;

		/* Advance by the number of gpios in this cluster */
		base += cluster->end - cluster->start + 1;
	}

	return -EINVAL;
}

static struct gpio_chip ab8500gpio_chip = {
	.label			= "ab8500_gpio",
	.owner			= THIS_MODULE,
	.direction_input	= ab8500_gpio_direction_input,
	.get			= ab8500_gpio_get,
	.direction_output	= ab8500_gpio_direction_output,
	.set			= ab8500_gpio_set,
	.to_irq			= ab8500_gpio_to_irq,
};

static unsigned int irq_to_rising(unsigned int irq)
{
	struct ab8500_gpio *ab8500_gpio = get_irq_chip_data(irq);
	int offset = irq - ab8500_gpio->irq_base;
	int new_irq = offset +  AB8500_INT_GPIO6R
			+ ab8500_gpio->parent->irq_base;
	return new_irq;
}

static unsigned int irq_to_falling(unsigned int irq)
{
	struct ab8500_gpio *ab8500_gpio = get_irq_chip_data(irq);
	int offset = irq - ab8500_gpio->irq_base;
	int new_irq = offset +  AB8500_INT_GPIO6F
			+  ab8500_gpio->parent->irq_base;
	return new_irq;

}

static unsigned int rising_to_irq(unsigned int irq, void *dev)
{
	struct ab8500_gpio *ab8500_gpio = dev;
	int offset = irq - AB8500_INT_GPIO6R
			- ab8500_gpio->parent->irq_base ;
	int new_irq = offset + ab8500_gpio->irq_base;
	return new_irq;
}

static unsigned int falling_to_irq(unsigned int irq, void *dev)
{
	struct ab8500_gpio *ab8500_gpio = dev;
	int offset = irq - AB8500_INT_GPIO6F
			- ab8500_gpio->parent->irq_base ;
	int new_irq = offset + ab8500_gpio->irq_base;
	return new_irq;

}

/*
 * IRQ handler
 */

static irqreturn_t handle_rising(int irq, void *dev)
{

	handle_nested_irq(rising_to_irq(irq , dev));
	return IRQ_HANDLED;
}

static irqreturn_t handle_falling(int irq, void *dev)
{

	handle_nested_irq(falling_to_irq(irq, dev));
	return IRQ_HANDLED;
}

static void ab8500_gpio_irq_lock(unsigned int irq)
{
	struct ab8500_gpio *ab8500_gpio = get_irq_chip_data(irq);
	mutex_lock(&ab8500_gpio->lock);
}

static void ab8500_gpio_irq_sync_unlock(unsigned int irq)
{
	struct ab8500_gpio *ab8500_gpio = get_irq_chip_data(irq);
	int offset = irq - ab8500_gpio->irq_base;
	bool rising = ab8500_gpio->rising & BIT(offset);
	bool falling = ab8500_gpio->falling & BIT(offset);
	int ret;

	switch (ab8500_gpio->irq_action)	{
	case STARTUP:
		if (rising)
			ret = request_threaded_irq(irq_to_rising(irq),
					NULL, handle_rising,
					IRQF_TRIGGER_RISING,
					"ab8500-gpio-r", ab8500_gpio);
		if (falling)
			ret = request_threaded_irq(irq_to_falling(irq),
				       NULL, handle_falling,
				       IRQF_TRIGGER_FALLING,
				       "ab8500-gpio-f", ab8500_gpio);
		break;
	case SHUTDOWN:
		if (rising)
			free_irq(irq_to_rising(irq), ab8500_gpio);
		if (falling)
			free_irq(irq_to_falling(irq), ab8500_gpio);
		break;
	case MASK:
		if (rising)
			disable_irq(irq_to_rising(irq));
		if (falling)
			disable_irq(irq_to_falling(irq));
		break;
	case UNMASK:
		if (rising)
			enable_irq(irq_to_rising(irq));
		if (falling)
			enable_irq(irq_to_falling(irq));
		break;
	case NONE:
		break;
	}
	ab8500_gpio->irq_action = NONE;
	ab8500_gpio->rising &= ~(BIT(offset));
	ab8500_gpio->falling &= ~(BIT(offset));
	mutex_unlock(&ab8500_gpio->lock);
}


static void ab8500_gpio_irq_mask(unsigned int irq)
{
	struct ab8500_gpio *ab8500_gpio = get_irq_chip_data(irq);
	ab8500_gpio->irq_action = MASK;
}

static void ab8500_gpio_irq_unmask(unsigned int irq)
{
	struct ab8500_gpio *ab8500_gpio = get_irq_chip_data(irq);
	ab8500_gpio->irq_action = UNMASK;
}

static int ab8500_gpio_irq_set_type(unsigned int irq, unsigned int type)
{
	struct ab8500_gpio *ab8500_gpio = get_irq_chip_data(irq);
	int offset = irq - ab8500_gpio->irq_base;

	if (type == IRQ_TYPE_EDGE_BOTH) {
		ab8500_gpio->rising =  BIT(offset);
		ab8500_gpio->falling = BIT(offset);
	} else if (type == IRQ_TYPE_EDGE_RISING) {
		ab8500_gpio->rising =  BIT(offset);
	} else  {
		ab8500_gpio->falling = BIT(offset);
	}
	return 0;
}

unsigned int ab8500_gpio_irq_startup(unsigned int irq)
{
	struct ab8500_gpio *ab8500_gpio = get_irq_chip_data(irq);
	ab8500_gpio->irq_action = STARTUP;
	return 0;
}

void ab8500_gpio_irq_shutdown(unsigned int irq)
{
	struct ab8500_gpio *ab8500_gpio = get_irq_chip_data(irq);
	ab8500_gpio->irq_action = SHUTDOWN;
}

static struct irq_chip ab8500_gpio_irq_chip = {
	.name			= "ab8500-gpio",
	.startup		= ab8500_gpio_irq_startup,
	.shutdown		= ab8500_gpio_irq_shutdown,
	.bus_lock		= ab8500_gpio_irq_lock,
	.bus_sync_unlock	= ab8500_gpio_irq_sync_unlock,
	.mask			= ab8500_gpio_irq_mask,
	.unmask			= ab8500_gpio_irq_unmask,
	.set_type		= ab8500_gpio_irq_set_type,
};

static int ab8500_gpio_irq_init(struct ab8500_gpio *ab8500_gpio)
{
	u32 base = ab8500_gpio->irq_base;
	int irq;

	for (irq = base; irq < base + AB8500_NUM_VIR_GPIO_IRQ ; irq++) {
		set_irq_chip_data(irq, ab8500_gpio);
		set_irq_chip_and_handler(irq, &ab8500_gpio_irq_chip,
				handle_simple_irq);
		set_irq_nested_thread(irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		set_irq_noprobe(irq);
#endif
	}

	return 0;
}

static void ab8500_gpio_irq_remove(struct ab8500_gpio *ab8500_gpio)
{
	int base = ab8500_gpio->irq_base;
	int irq;

	for (irq = base; irq < base + AB8500_NUM_VIR_GPIO_IRQ; irq++) {
#ifdef CONFIG_ARM
		set_irq_flags(irq, 0);
#endif
		set_irq_chip_and_handler(irq, NULL, NULL);
		set_irq_chip_data(irq, NULL);
	}
}

static int __devinit ab8500_gpio_probe(struct platform_device *pdev)
{
	struct ab8500_platform_data *ab8500_pdata =
				dev_get_platdata(pdev->dev.parent);
	struct ab8500_gpio_platform_data *pdata;
	struct ab8500_gpio *ab8500_gpio;
	int ret;
	int i;

	pdata = ab8500_pdata->gpio;
	if (!pdata)	{
		dev_err(&pdev->dev, "gpio platform data missing\n");
		return -ENODEV;
	}

	ab8500_gpio = kzalloc(sizeof(struct ab8500_gpio), GFP_KERNEL);
	if (ab8500_gpio == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}
	ab8500_gpio->dev = &pdev->dev;
	ab8500_gpio->parent = dev_get_drvdata(pdev->dev.parent);
	ab8500_gpio->chip = ab8500gpio_chip;
	ab8500_gpio->chip.ngpio = AB8500_NUM_GPIO;
	ab8500_gpio->chip.dev = &pdev->dev;
	ab8500_gpio->chip.base = pdata->gpio_base;
	ab8500_gpio->irq_base = pdata->irq_base;
	/* initialize the lock */
	mutex_init(&ab8500_gpio->lock);
	/*
	 * AB8500 core will handle and clear the IRQ
	 * configre GPIO based on config-reg value.
	 * These values are for selecting the PINs as
	 * GPIO or alternate function
	 */
	for (i = AB8500_GPIO_SEL1_REG; i <= AB8500_GPIO_SEL6_REG; i++)	{
		ret = abx500_set_register_interruptible(ab8500_gpio->dev,
				AB8500_MISC, i,
				pdata->config_reg[i]);
		if (ret < 0)
			goto out_free;
	}
	ret = abx500_set_register_interruptible(ab8500_gpio->dev, AB8500_MISC,
				AB8500_GPIO_ALTFUN_REG,
				pdata->config_reg[ALTFUN_REG_INDEX]);
	if (ret < 0)
		goto out_free;

	ret = ab8500_gpio_irq_init(ab8500_gpio);
	if (ret)
		goto out_free;
	ret = gpiochip_add(&ab8500_gpio->chip);
	if (ret) {
		dev_err(&pdev->dev, "unable to add gpiochip: %d\n",
				ret);
		goto out_rem_irq;
	}
	platform_set_drvdata(pdev, ab8500_gpio);
	return 0;

out_rem_irq:
	ab8500_gpio_irq_remove(ab8500_gpio);
out_free:
	mutex_destroy(&ab8500_gpio->lock);
	kfree(ab8500_gpio);
	return ret;
}

/*
 * ab8500_gpio_remove() - remove Ab8500-gpio driver
 * @pdev :	Platform device registered
 */
static int __devexit ab8500_gpio_remove(struct platform_device *pdev)
{
	struct ab8500_gpio *ab8500_gpio = platform_get_drvdata(pdev);
	int ret;

	ret = gpiochip_remove(&ab8500_gpio->chip);
	if (ret < 0) {
		dev_err(ab8500_gpio->dev, "unable to remove gpiochip:\
				%d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&ab8500_gpio->lock);
	kfree(ab8500_gpio);

	return 0;
}

static struct platform_driver ab8500_gpio_driver = {
	.driver = {
		.name = "ab8500-gpio",
		.owner = THIS_MODULE,
	},
	.probe = ab8500_gpio_probe,
	.remove = __devexit_p(ab8500_gpio_remove),
};

static int __init ab8500_gpio_init(void)
{
	return platform_driver_register(&ab8500_gpio_driver);
}
arch_initcall(ab8500_gpio_init);

static void __exit ab8500_gpio_exit(void)
{
	platform_driver_unregister(&ab8500_gpio_driver);
}
module_exit(ab8500_gpio_exit);

MODULE_AUTHOR("BIBEK BASU <bibek.basu@stericsson.com>");
MODULE_DESCRIPTION("Driver allows to use AB8500 unused pins\
			to be used as GPIO");
MODULE_ALIAS("AB8500 GPIO driver");
MODULE_LICENSE("GPL v2");
