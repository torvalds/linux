// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Rafał Miłecki <rafal@milecki.pl>
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define BCM63138_MAX_LEDS				32
#define BCM63138_MAX_BRIGHTNESS				9

#define BCM63138_LED_BITS				4				/* how many bits control a single LED */
#define BCM63138_LED_MASK				((1 << BCM63138_LED_BITS) - 1)	/* 0xf */
#define BCM63138_LEDS_PER_REG				(32 / BCM63138_LED_BITS)	/* 8 */

#define BCM63138_GLB_CTRL				0x00
#define  BCM63138_GLB_CTRL_SERIAL_LED_DATA_PPOL		0x00000002
#define  BCM63138_GLB_CTRL_SERIAL_LED_EN_POL		0x00000008
#define BCM63138_MASK					0x04
#define BCM63138_HW_LED_EN				0x08
#define BCM63138_SERIAL_LED_SHIFT_SEL			0x0c
#define BCM63138_FLASH_RATE_CTRL1			0x10
#define BCM63138_FLASH_RATE_CTRL2			0x14
#define BCM63138_FLASH_RATE_CTRL3			0x18
#define BCM63138_FLASH_RATE_CTRL4			0x1c
#define BCM63138_BRIGHT_CTRL1				0x20
#define BCM63138_BRIGHT_CTRL2				0x24
#define BCM63138_BRIGHT_CTRL3				0x28
#define BCM63138_BRIGHT_CTRL4				0x2c
#define BCM63138_POWER_LED_CFG				0x30
#define BCM63138_HW_POLARITY				0xb4
#define BCM63138_SW_DATA				0xb8
#define BCM63138_SW_POLARITY				0xbc
#define BCM63138_PARALLEL_LED_POLARITY			0xc0
#define BCM63138_SERIAL_LED_POLARITY			0xc4
#define BCM63138_HW_LED_STATUS				0xc8
#define BCM63138_FLASH_CTRL_STATUS			0xcc
#define BCM63138_FLASH_BRT_CTRL				0xd0
#define BCM63138_FLASH_P_LED_OUT_STATUS			0xd4
#define BCM63138_FLASH_S_LED_OUT_STATUS			0xd8

struct bcm63138_leds {
	struct device *dev;
	void __iomem *base;
	spinlock_t lock;
};

struct bcm63138_led {
	struct bcm63138_leds *leds;
	struct led_classdev cdev;
	u32 pin;
	bool active_low;
};

/*
 * I/O access
 */

static void bcm63138_leds_write(struct bcm63138_leds *leds, unsigned int reg,
				u32 data)
{
	writel(data, leds->base + reg);
}

static unsigned long bcm63138_leds_read(struct bcm63138_leds *leds,
					unsigned int reg)
{
	return readl(leds->base + reg);
}

static void bcm63138_leds_update_bits(struct bcm63138_leds *leds,
				      unsigned int reg, u32 mask, u32 val)
{
	WARN_ON(val & ~mask);

	bcm63138_leds_write(leds, reg, (bcm63138_leds_read(leds, reg) & ~mask) | (val & mask));
}

/*
 * Helpers
 */

static void bcm63138_leds_set_flash_rate(struct bcm63138_leds *leds,
					 struct bcm63138_led *led,
					 u8 value)
{
	int reg_offset = (led->pin >> fls((BCM63138_LEDS_PER_REG - 1))) * 4;
	int shift = (led->pin & (BCM63138_LEDS_PER_REG - 1)) * BCM63138_LED_BITS;

	bcm63138_leds_update_bits(leds, BCM63138_FLASH_RATE_CTRL1 + reg_offset,
				  BCM63138_LED_MASK << shift, value << shift);
}

static void bcm63138_leds_set_bright(struct bcm63138_leds *leds,
				     struct bcm63138_led *led,
				     u8 value)
{
	int reg_offset = (led->pin >> fls((BCM63138_LEDS_PER_REG - 1))) * 4;
	int shift = (led->pin & (BCM63138_LEDS_PER_REG - 1)) * BCM63138_LED_BITS;

	bcm63138_leds_update_bits(leds, BCM63138_BRIGHT_CTRL1 + reg_offset,
				  BCM63138_LED_MASK << shift, value << shift);
}

static void bcm63138_leds_enable_led(struct bcm63138_leds *leds,
				     struct bcm63138_led *led,
				     enum led_brightness value)
{
	u32 bit = BIT(led->pin);

	bcm63138_leds_update_bits(leds, BCM63138_SW_DATA, bit, value ? bit : 0);
}

/*
 * API callbacks
 */

static void bcm63138_leds_brightness_set(struct led_classdev *led_cdev,
					 enum led_brightness value)
{
	struct bcm63138_led *led = container_of(led_cdev, struct bcm63138_led, cdev);
	struct bcm63138_leds *leds = led->leds;
	unsigned long flags;

	spin_lock_irqsave(&leds->lock, flags);

	bcm63138_leds_enable_led(leds, led, value);
	if (!value)
		bcm63138_leds_set_flash_rate(leds, led, 0);
	else
		bcm63138_leds_set_bright(leds, led, value);

	spin_unlock_irqrestore(&leds->lock, flags);
}

static int bcm63138_leds_blink_set(struct led_classdev *led_cdev,
				   unsigned long *delay_on,
				   unsigned long *delay_off)
{
	struct bcm63138_led *led = container_of(led_cdev, struct bcm63138_led, cdev);
	struct bcm63138_leds *leds = led->leds;
	unsigned long flags;
	u8 value;

	if (!*delay_on && !*delay_off) {
		*delay_on = 640;
		*delay_off = 640;
	}

	if (*delay_on != *delay_off) {
		dev_dbg(led_cdev->dev, "Blinking at unequal delays is not supported\n");
		return -EINVAL;
	}

	switch (*delay_on) {
	case 1152 ... 1408: /* 1280 ms ± 10% */
		value = 0x7;
		break;
	case 576 ... 704: /* 640 ms ± 10% */
		value = 0x6;
		break;
	case 288 ... 352: /* 320 ms ± 10% */
		value = 0x5;
		break;
	case 126 ... 154: /* 140 ms ± 10% */
		value = 0x4;
		break;
	case 59 ... 72: /* 65 ms ± 10% */
		value = 0x3;
		break;
	default:
		dev_dbg(led_cdev->dev, "Blinking delay value %lu is unsupported\n",
			*delay_on);
		return -EINVAL;
	}

	spin_lock_irqsave(&leds->lock, flags);

	bcm63138_leds_enable_led(leds, led, BCM63138_MAX_BRIGHTNESS);
	bcm63138_leds_set_flash_rate(leds, led, value);

	spin_unlock_irqrestore(&leds->lock, flags);

	return 0;
}

/*
 * LED driver
 */

static void bcm63138_leds_create_led(struct bcm63138_leds *leds,
				     struct device_node *np)
{
	struct led_init_data init_data = {
		.fwnode = of_fwnode_handle(np),
	};
	struct device *dev = leds->dev;
	struct bcm63138_led *led;
	struct pinctrl *pinctrl;
	u32 bit;
	int err;

	led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
	if (!led) {
		dev_err(dev, "Failed to alloc LED\n");
		return;
	}

	led->leds = leds;

	if (of_property_read_u32(np, "reg", &led->pin)) {
		dev_err(dev, "Missing \"reg\" property in %pOF\n", np);
		goto err_free;
	}

	if (led->pin >= BCM63138_MAX_LEDS) {
		dev_err(dev, "Invalid \"reg\" value %d\n", led->pin);
		goto err_free;
	}

	led->active_low = of_property_read_bool(np, "active-low");

	led->cdev.max_brightness = BCM63138_MAX_BRIGHTNESS;
	led->cdev.brightness_set = bcm63138_leds_brightness_set;
	led->cdev.blink_set = bcm63138_leds_blink_set;

	err = devm_led_classdev_register_ext(dev, &led->cdev, &init_data);
	if (err) {
		dev_err(dev, "Failed to register LED %pOF: %d\n", np, err);
		goto err_free;
	}

	pinctrl = devm_pinctrl_get_select_default(led->cdev.dev);
	if (IS_ERR(pinctrl) && PTR_ERR(pinctrl) != -ENODEV) {
		dev_warn(led->cdev.dev, "Failed to select %pOF pinctrl: %ld\n",
			 np, PTR_ERR(pinctrl));
	}

	bit = BIT(led->pin);
	bcm63138_leds_update_bits(leds, BCM63138_PARALLEL_LED_POLARITY, bit,
				  led->active_low ? 0 : bit);
	bcm63138_leds_update_bits(leds, BCM63138_HW_LED_EN, bit, 0);
	bcm63138_leds_set_flash_rate(leds, led, 0);
	bcm63138_leds_enable_led(leds, led, led->cdev.brightness);

	return;

err_free:
	devm_kfree(dev, led);
}

static int bcm63138_leds_probe(struct platform_device *pdev)
{
	struct device_node *np = dev_of_node(&pdev->dev);
	struct device *dev = &pdev->dev;
	struct bcm63138_leds *leds;
	struct device_node *child;

	leds = devm_kzalloc(dev, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	leds->dev = dev;

	leds->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(leds->base))
		return PTR_ERR(leds->base);

	spin_lock_init(&leds->lock);

	bcm63138_leds_write(leds, BCM63138_GLB_CTRL,
			    BCM63138_GLB_CTRL_SERIAL_LED_DATA_PPOL |
			    BCM63138_GLB_CTRL_SERIAL_LED_EN_POL);
	bcm63138_leds_write(leds, BCM63138_HW_LED_EN, 0);
	bcm63138_leds_write(leds, BCM63138_SERIAL_LED_POLARITY, 0);
	bcm63138_leds_write(leds, BCM63138_PARALLEL_LED_POLARITY, 0);

	for_each_available_child_of_node(np, child) {
		bcm63138_leds_create_led(leds, child);
	}

	return 0;
}

static const struct of_device_id bcm63138_leds_of_match_table[] = {
	{ .compatible = "brcm,bcm63138-leds", },
	{ },
};

static struct platform_driver bcm63138_leds_driver = {
	.probe = bcm63138_leds_probe,
	.driver = {
		.name = "leds-bcm63xxx",
		.of_match_table = bcm63138_leds_of_match_table,
	},
};

module_platform_driver(bcm63138_leds_driver);

MODULE_AUTHOR("Rafał Miłecki");
MODULE_DESCRIPTION("Broadcom BCM63138 SoC LED driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, bcm63138_leds_of_match_table);
