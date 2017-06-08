/*
 * Driver for BCM6328 memory-mapped LEDs, based on leds-syscon.c
 *
 * Copyright 2015 Álvaro Fernández Rojas <noltari@gmail.com>
 * Copyright 2015 Jonas Gorski <jogo@openwrt.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define BCM6328_REG_INIT		0x00
#define BCM6328_REG_MODE_HI		0x04
#define BCM6328_REG_MODE_LO		0x08
#define BCM6328_REG_HWDIS		0x0c
#define BCM6328_REG_STROBE		0x10
#define BCM6328_REG_LNKACTSEL_HI	0x14
#define BCM6328_REG_LNKACTSEL_LO	0x18
#define BCM6328_REG_RBACK		0x1c
#define BCM6328_REG_SERMUX		0x20

#define BCM6328_LED_MAX_COUNT		24
#define BCM6328_LED_DEF_DELAY		500
#define BCM6328_LED_INTERVAL_MS		20

#define BCM6328_LED_INTV_MASK		0x3f
#define BCM6328_LED_FAST_INTV_SHIFT	6
#define BCM6328_LED_FAST_INTV_MASK	(BCM6328_LED_INTV_MASK << \
					 BCM6328_LED_FAST_INTV_SHIFT)
#define BCM6328_SERIAL_LED_EN		BIT(12)
#define BCM6328_SERIAL_LED_MUX		BIT(13)
#define BCM6328_SERIAL_LED_CLK_NPOL	BIT(14)
#define BCM6328_SERIAL_LED_DATA_PPOL	BIT(15)
#define BCM6328_SERIAL_LED_SHIFT_DIR	BIT(16)
#define BCM6328_LED_SHIFT_TEST		BIT(30)
#define BCM6328_LED_TEST		BIT(31)
#define BCM6328_INIT_MASK		(BCM6328_SERIAL_LED_EN | \
					 BCM6328_SERIAL_LED_MUX | \
					 BCM6328_SERIAL_LED_CLK_NPOL | \
					 BCM6328_SERIAL_LED_DATA_PPOL | \
					 BCM6328_SERIAL_LED_SHIFT_DIR)

#define BCM6328_LED_MODE_MASK		3
#define BCM6328_LED_MODE_ON		0
#define BCM6328_LED_MODE_FAST		1
#define BCM6328_LED_MODE_BLINK		2
#define BCM6328_LED_MODE_OFF		3
#define BCM6328_LED_SHIFT(X)		((X) << 1)

/**
 * struct bcm6328_led - state container for bcm6328 based LEDs
 * @cdev: LED class device for this LED
 * @mem: memory resource
 * @lock: memory lock
 * @pin: LED pin number
 * @blink_leds: blinking LEDs
 * @blink_delay: blinking delay
 * @active_low: LED is active low
 */
struct bcm6328_led {
	struct led_classdev cdev;
	void __iomem *mem;
	spinlock_t *lock;
	unsigned long pin;
	unsigned long *blink_leds;
	unsigned long *blink_delay;
	bool active_low;
};

static void bcm6328_led_write(void __iomem *reg, unsigned long data)
{
#ifdef CONFIG_CPU_BIG_ENDIAN
	iowrite32be(data, reg);
#else
	writel(data, reg);
#endif
}

static unsigned long bcm6328_led_read(void __iomem *reg)
{
#ifdef CONFIG_CPU_BIG_ENDIAN
	return ioread32be(reg);
#else
	return readl(reg);
#endif
}

/**
 * LEDMode 64 bits / 24 LEDs
 * bits [31:0] -> LEDs 8-23
 * bits [47:32] -> LEDs 0-7
 * bits [63:48] -> unused
 */
static unsigned long bcm6328_pin2shift(unsigned long pin)
{
	if (pin < 8)
		return pin + 16; /* LEDs 0-7 (bits 47:32) */
	else
		return pin - 8; /* LEDs 8-23 (bits 31:0) */
}

static void bcm6328_led_mode(struct bcm6328_led *led, unsigned long value)
{
	void __iomem *mode;
	unsigned long val, shift;

	shift = bcm6328_pin2shift(led->pin);
	if (shift / 16)
		mode = led->mem + BCM6328_REG_MODE_HI;
	else
		mode = led->mem + BCM6328_REG_MODE_LO;

	val = bcm6328_led_read(mode);
	val &= ~(BCM6328_LED_MODE_MASK << BCM6328_LED_SHIFT(shift % 16));
	val |= (value << BCM6328_LED_SHIFT(shift % 16));
	bcm6328_led_write(mode, val);
}

static void bcm6328_led_set(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	struct bcm6328_led *led =
		container_of(led_cdev, struct bcm6328_led, cdev);
	unsigned long flags;

	spin_lock_irqsave(led->lock, flags);
	*(led->blink_leds) &= ~BIT(led->pin);
	if ((led->active_low && value == LED_OFF) ||
	    (!led->active_low && value != LED_OFF))
		bcm6328_led_mode(led, BCM6328_LED_MODE_ON);
	else
		bcm6328_led_mode(led, BCM6328_LED_MODE_OFF);
	spin_unlock_irqrestore(led->lock, flags);
}

static unsigned long bcm6328_blink_delay(unsigned long delay)
{
	unsigned long bcm6328_delay;

	bcm6328_delay = delay + BCM6328_LED_INTERVAL_MS / 2;
	bcm6328_delay = bcm6328_delay / BCM6328_LED_INTERVAL_MS;
	if (bcm6328_delay == 0)
		bcm6328_delay = 1;

	return bcm6328_delay;
}

static int bcm6328_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on, unsigned long *delay_off)
{
	struct bcm6328_led *led =
		container_of(led_cdev, struct bcm6328_led, cdev);
	unsigned long delay, flags;
	int rc;

	if (!*delay_on)
		*delay_on = BCM6328_LED_DEF_DELAY;
	if (!*delay_off)
		*delay_off = BCM6328_LED_DEF_DELAY;

	delay = bcm6328_blink_delay(*delay_on);
	if (delay != bcm6328_blink_delay(*delay_off)) {
		dev_dbg(led_cdev->dev,
			"fallback to soft blinking (delay_on != delay_off)\n");
		return -EINVAL;
	}

	if (delay > BCM6328_LED_INTV_MASK) {
		dev_dbg(led_cdev->dev,
			"fallback to soft blinking (delay > %ums)\n",
			BCM6328_LED_INTV_MASK * BCM6328_LED_INTERVAL_MS);
		return -EINVAL;
	}

	spin_lock_irqsave(led->lock, flags);
	if (*(led->blink_leds) == 0 ||
	    *(led->blink_leds) == BIT(led->pin) ||
	    *(led->blink_delay) == delay) {
		unsigned long val;

		*(led->blink_leds) |= BIT(led->pin);
		*(led->blink_delay) = delay;

		val = bcm6328_led_read(led->mem + BCM6328_REG_INIT);
		val &= ~BCM6328_LED_FAST_INTV_MASK;
		val |= (delay << BCM6328_LED_FAST_INTV_SHIFT);
		bcm6328_led_write(led->mem + BCM6328_REG_INIT, val);

		bcm6328_led_mode(led, BCM6328_LED_MODE_BLINK);
		rc = 0;
	} else {
		dev_dbg(led_cdev->dev,
			"fallback to soft blinking (delay already set)\n");
		rc = -EINVAL;
	}
	spin_unlock_irqrestore(led->lock, flags);

	return rc;
}

static int bcm6328_hwled(struct device *dev, struct device_node *nc, u32 reg,
			 void __iomem *mem, spinlock_t *lock)
{
	int i, cnt;
	unsigned long flags, val;

	spin_lock_irqsave(lock, flags);
	val = bcm6328_led_read(mem + BCM6328_REG_HWDIS);
	val &= ~BIT(reg);
	bcm6328_led_write(mem + BCM6328_REG_HWDIS, val);
	spin_unlock_irqrestore(lock, flags);

	/* Only LEDs 0-7 can be activity/link controlled */
	if (reg >= 8)
		return 0;

	cnt = of_property_count_elems_of_size(nc, "brcm,link-signal-sources",
					      sizeof(u32));
	for (i = 0; i < cnt; i++) {
		u32 sel;
		void __iomem *addr;

		if (reg < 4)
			addr = mem + BCM6328_REG_LNKACTSEL_LO;
		else
			addr = mem + BCM6328_REG_LNKACTSEL_HI;

		of_property_read_u32_index(nc, "brcm,link-signal-sources", i,
					   &sel);

		if (reg / 4 != sel / 4) {
			dev_warn(dev, "invalid link signal source\n");
			continue;
		}

		spin_lock_irqsave(lock, flags);
		val = bcm6328_led_read(addr);
		val |= (BIT(reg) << (((sel % 4) * 4) + 16));
		bcm6328_led_write(addr, val);
		spin_unlock_irqrestore(lock, flags);
	}

	cnt = of_property_count_elems_of_size(nc,
					      "brcm,activity-signal-sources",
					      sizeof(u32));
	for (i = 0; i < cnt; i++) {
		u32 sel;
		void __iomem *addr;

		if (reg < 4)
			addr = mem + BCM6328_REG_LNKACTSEL_LO;
		else
			addr = mem + BCM6328_REG_LNKACTSEL_HI;

		of_property_read_u32_index(nc, "brcm,activity-signal-sources",
					   i, &sel);

		if (reg / 4 != sel / 4) {
			dev_warn(dev, "invalid activity signal source\n");
			continue;
		}

		spin_lock_irqsave(lock, flags);
		val = bcm6328_led_read(addr);
		val |= (BIT(reg) << ((sel % 4) * 4));
		bcm6328_led_write(addr, val);
		spin_unlock_irqrestore(lock, flags);
	}

	return 0;
}

static int bcm6328_led(struct device *dev, struct device_node *nc, u32 reg,
		       void __iomem *mem, spinlock_t *lock,
		       unsigned long *blink_leds, unsigned long *blink_delay)
{
	struct bcm6328_led *led;
	const char *state;
	int rc;

	led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->pin = reg;
	led->mem = mem;
	led->lock = lock;
	led->blink_leds = blink_leds;
	led->blink_delay = blink_delay;

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
			void __iomem *mode;
			unsigned long val, shift;

			shift = bcm6328_pin2shift(led->pin);
			if (shift / 16)
				mode = mem + BCM6328_REG_MODE_HI;
			else
				mode = mem + BCM6328_REG_MODE_LO;

			val = bcm6328_led_read(mode) >>
			      BCM6328_LED_SHIFT(shift % 16);
			val &= BCM6328_LED_MODE_MASK;
			if ((led->active_low && val == BCM6328_LED_MODE_OFF) ||
			    (!led->active_low && val == BCM6328_LED_MODE_ON))
				led->cdev.brightness = LED_FULL;
			else
				led->cdev.brightness = LED_OFF;
		} else {
			led->cdev.brightness = LED_OFF;
		}
	} else {
		led->cdev.brightness = LED_OFF;
	}

	bcm6328_led_set(&led->cdev, led->cdev.brightness);

	led->cdev.brightness_set = bcm6328_led_set;
	led->cdev.blink_set = bcm6328_blink_set;

	rc = led_classdev_register(dev, &led->cdev);
	if (rc < 0)
		return rc;

	dev_dbg(dev, "registered LED %s\n", led->cdev.name);

	return 0;
}

static int bcm6328_leds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	struct resource *mem_r;
	void __iomem *mem;
	spinlock_t *lock; /* memory lock */
	unsigned long val, *blink_leds, *blink_delay;

	mem_r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_r)
		return -EINVAL;

	mem = devm_ioremap_resource(dev, mem_r);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	lock = devm_kzalloc(dev, sizeof(*lock), GFP_KERNEL);
	if (!lock)
		return -ENOMEM;

	blink_leds = devm_kzalloc(dev, sizeof(*blink_leds), GFP_KERNEL);
	if (!blink_leds)
		return -ENOMEM;

	blink_delay = devm_kzalloc(dev, sizeof(*blink_delay), GFP_KERNEL);
	if (!blink_delay)
		return -ENOMEM;

	spin_lock_init(lock);

	bcm6328_led_write(mem + BCM6328_REG_HWDIS, ~0);
	bcm6328_led_write(mem + BCM6328_REG_LNKACTSEL_HI, 0);
	bcm6328_led_write(mem + BCM6328_REG_LNKACTSEL_LO, 0);

	val = bcm6328_led_read(mem + BCM6328_REG_INIT);
	val &= ~(BCM6328_INIT_MASK);
	if (of_property_read_bool(np, "brcm,serial-leds"))
		val |= BCM6328_SERIAL_LED_EN;
	if (of_property_read_bool(np, "brcm,serial-mux"))
		val |= BCM6328_SERIAL_LED_MUX;
	if (of_property_read_bool(np, "brcm,serial-clk-low"))
		val |= BCM6328_SERIAL_LED_CLK_NPOL;
	if (!of_property_read_bool(np, "brcm,serial-dat-low"))
		val |= BCM6328_SERIAL_LED_DATA_PPOL;
	if (!of_property_read_bool(np, "brcm,serial-shift-inv"))
		val |= BCM6328_SERIAL_LED_SHIFT_DIR;
	bcm6328_led_write(mem + BCM6328_REG_INIT, val);

	for_each_available_child_of_node(np, child) {
		int rc;
		u32 reg;

		if (of_property_read_u32(child, "reg", &reg))
			continue;

		if (reg >= BCM6328_LED_MAX_COUNT) {
			dev_err(dev, "invalid LED (%u >= %d)\n", reg,
				BCM6328_LED_MAX_COUNT);
			continue;
		}

		if (of_property_read_bool(child, "brcm,hardware-controlled"))
			rc = bcm6328_hwled(dev, child, reg, mem, lock);
		else
			rc = bcm6328_led(dev, child, reg, mem, lock,
					 blink_leds, blink_delay);

		if (rc < 0) {
			of_node_put(child);
			return rc;
		}
	}

	return 0;
}

static const struct of_device_id bcm6328_leds_of_match[] = {
	{ .compatible = "brcm,bcm6328-leds", },
	{ },
};
MODULE_DEVICE_TABLE(of, bcm6328_leds_of_match);

static struct platform_driver bcm6328_leds_driver = {
	.probe = bcm6328_leds_probe,
	.driver = {
		.name = "leds-bcm6328",
		.of_match_table = bcm6328_leds_of_match,
	},
};

module_platform_driver(bcm6328_leds_driver);

MODULE_AUTHOR("Álvaro Fernández Rojas <noltari@gmail.com>");
MODULE_AUTHOR("Jonas Gorski <jogo@openwrt.org>");
MODULE_DESCRIPTION("LED driver for BCM6328 controllers");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:leds-bcm6328");
