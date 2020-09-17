// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for BCM6358 memory-mapped LEDs, based on leds-syscon.c
 *
 * Copyright 2015 Álvaro Fernández Rojas <noltari@gmail.com>
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define BCM6358_REG_MODE		0x0
#define BCM6358_REG_CTRL		0x4

#define BCM6358_SLED_CLKDIV_MASK	3
#define BCM6358_SLED_CLKDIV_1		0
#define BCM6358_SLED_CLKDIV_2		1
#define BCM6358_SLED_CLKDIV_4		2
#define BCM6358_SLED_CLKDIV_8		3

#define BCM6358_SLED_POLARITY		BIT(2)
#define BCM6358_SLED_BUSY		BIT(3)

#define BCM6358_SLED_MAX_COUNT		32
#define BCM6358_SLED_WAIT		100

/**
 * struct bcm6358_led - state container for bcm6358 based LEDs
 * @cdev: LED class device for this LED
 * @mem: memory resource
 * @lock: memory lock
 * @pin: LED pin number
 * @active_low: LED is active low
 */
struct bcm6358_led {
	struct led_classdev cdev;
	void __iomem *mem;
	spinlock_t *lock;
	unsigned long pin;
	bool active_low;
};

static void bcm6358_led_write(void __iomem *reg, unsigned long data)
{
#ifdef CONFIG_CPU_BIG_ENDIAN
	iowrite32be(data, reg);
#else
	writel(data, reg);
#endif
}

static unsigned long bcm6358_led_read(void __iomem *reg)
{
#ifdef CONFIG_CPU_BIG_ENDIAN
	return ioread32be(reg);
#else
	return readl(reg);
#endif
}

static unsigned long bcm6358_led_busy(void __iomem *mem)
{
	unsigned long val;

	while ((val = bcm6358_led_read(mem + BCM6358_REG_CTRL)) &
		BCM6358_SLED_BUSY)
		udelay(BCM6358_SLED_WAIT);

	return val;
}

static void bcm6358_led_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct bcm6358_led *led =
		container_of(led_cdev, struct bcm6358_led, cdev);
	unsigned long flags, val;

	spin_lock_irqsave(led->lock, flags);
	bcm6358_led_busy(led->mem);
	val = bcm6358_led_read(led->mem + BCM6358_REG_MODE);
	if ((led->active_low && value == LED_OFF) ||
	    (!led->active_low && value != LED_OFF))
		val |= BIT(led->pin);
	else
		val &= ~(BIT(led->pin));
	bcm6358_led_write(led->mem + BCM6358_REG_MODE, val);
	spin_unlock_irqrestore(led->lock, flags);
}

static int bcm6358_led(struct device *dev, struct device_node *nc, u32 reg,
		       void __iomem *mem, spinlock_t *lock)
{
	struct bcm6358_led *led;
	const char *state;
	int rc;

	led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->pin = reg;
	led->mem = mem;
	led->lock = lock;

	if (of_property_read_bool(nc, "active-low"))
		led->active_low = true;

	led->cdev.name = of_get_property(nc, "label", NULL) ? : nc->name;
	led->cdev.default_trigger = of_get_property(nc,
						    "linux,default-trigger",
						    NULL);

	if (!of_property_read_string(nc, "default-state", &state)) {
		if (!strcmp(state, "on")) {
			led->cdev.brightness = LED_FULL;
		} else if (!strcmp(state, "keep")) {
			unsigned long val;
			val = bcm6358_led_read(led->mem + BCM6358_REG_MODE);
			val &= BIT(led->pin);
			if ((led->active_low && !val) ||
			    (!led->active_low && val))
				led->cdev.brightness = LED_FULL;
			else
				led->cdev.brightness = LED_OFF;
		} else {
			led->cdev.brightness = LED_OFF;
		}
	} else {
		led->cdev.brightness = LED_OFF;
	}

	bcm6358_led_set(&led->cdev, led->cdev.brightness);

	led->cdev.brightness_set = bcm6358_led_set;

	rc = led_classdev_register(dev, &led->cdev);
	if (rc < 0)
		return rc;

	dev_dbg(dev, "registered LED %s\n", led->cdev.name);

	return 0;
}

static int bcm6358_leds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev_of_node(&pdev->dev);
	struct device_node *child;
	void __iomem *mem;
	spinlock_t *lock; /* memory lock */
	unsigned long val;
	u32 clk_div;

	mem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	lock = devm_kzalloc(dev, sizeof(*lock), GFP_KERNEL);
	if (!lock)
		return -ENOMEM;

	spin_lock_init(lock);

	val = bcm6358_led_busy(mem);
	val &= ~(BCM6358_SLED_POLARITY | BCM6358_SLED_CLKDIV_MASK);
	if (of_property_read_bool(np, "brcm,clk-dat-low"))
		val |= BCM6358_SLED_POLARITY;
	of_property_read_u32(np, "brcm,clk-div", &clk_div);
	switch (clk_div) {
	case 8:
		val |= BCM6358_SLED_CLKDIV_8;
		break;
	case 4:
		val |= BCM6358_SLED_CLKDIV_4;
		break;
	case 2:
		val |= BCM6358_SLED_CLKDIV_2;
		break;
	default:
		val |= BCM6358_SLED_CLKDIV_1;
		break;
	}
	bcm6358_led_write(mem + BCM6358_REG_CTRL, val);

	for_each_available_child_of_node(np, child) {
		int rc;
		u32 reg;

		if (of_property_read_u32(child, "reg", &reg))
			continue;

		if (reg >= BCM6358_SLED_MAX_COUNT) {
			dev_err(dev, "invalid LED (%u >= %d)\n", reg,
				BCM6358_SLED_MAX_COUNT);
			continue;
		}

		rc = bcm6358_led(dev, child, reg, mem, lock);
		if (rc < 0) {
			of_node_put(child);
			return rc;
		}
	}

	return 0;
}

static const struct of_device_id bcm6358_leds_of_match[] = {
	{ .compatible = "brcm,bcm6358-leds", },
	{ },
};
MODULE_DEVICE_TABLE(of, bcm6358_leds_of_match);

static struct platform_driver bcm6358_leds_driver = {
	.probe = bcm6358_leds_probe,
	.driver = {
		.name = "leds-bcm6358",
		.of_match_table = bcm6358_leds_of_match,
	},
};

module_platform_driver(bcm6358_leds_driver);

MODULE_AUTHOR("Álvaro Fernández Rojas <noltari@gmail.com>");
MODULE_DESCRIPTION("LED driver for BCM6358 controllers");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:leds-bcm6358");
