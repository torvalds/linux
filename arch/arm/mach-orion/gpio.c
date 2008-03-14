/*
 * arch/arm/mach-orion/gpio.c
 *
 * GPIO functions for Marvell Orion System On Chip
 *
 * Maintainer: Tzachi Perelstein <tzachi@marvell.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <asm/gpio.h>
#include <asm/arch/orion.h>
#include "common.h"

static DEFINE_SPINLOCK(gpio_lock);
static unsigned long gpio_valid[BITS_TO_LONGS(GPIO_MAX)];
static const char *gpio_label[GPIO_MAX];  /* non null for allocated GPIOs */

void __init orion_gpio_set_valid_pins(u32 pins)
{
	gpio_valid[0] = pins;
}

/*
 * GENERIC_GPIO primitives
 */
int gpio_direction_input(unsigned pin)
{
	unsigned long flags;

	if (pin >= GPIO_MAX || !test_bit(pin, gpio_valid)) {
		pr_debug("%s: invalid GPIO %d\n", __func__, pin);
		return -EINVAL;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	/*
	 * Some callers might have not used the gpio_request(),
	 * so flag this pin as requested now.
	 */
	if (!gpio_label[pin])
		gpio_label[pin] = "?";

	orion_setbits(GPIO_IO_CONF, 1 << pin);

	spin_unlock_irqrestore(&gpio_lock, flags);
	return 0;
}
EXPORT_SYMBOL(gpio_direction_input);

int gpio_direction_output(unsigned pin, int value)
{
	unsigned long flags;
	int mask;

	if (pin >= GPIO_MAX || !test_bit(pin, gpio_valid)) {
		pr_debug("%s: invalid GPIO %d\n", __func__, pin);
		return -EINVAL;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	/*
	 * Some callers might have not used the gpio_request(),
	 * so flag this pin as requested now.
	 */
	if (!gpio_label[pin])
		gpio_label[pin] = "?";

	mask = 1 << pin;
	orion_clrbits(GPIO_BLINK_EN, mask);
	if (value)
		orion_setbits(GPIO_OUT, mask);
	else
		orion_clrbits(GPIO_OUT, mask);
	orion_clrbits(GPIO_IO_CONF, mask);

	spin_unlock_irqrestore(&gpio_lock, flags);
	return 0;
}
EXPORT_SYMBOL(gpio_direction_output);

int gpio_get_value(unsigned pin)
{
	int val, mask = 1 << pin;

	if (orion_read(GPIO_IO_CONF) & mask)
		val = orion_read(GPIO_DATA_IN) ^ orion_read(GPIO_IN_POL);
	else
		val = orion_read(GPIO_OUT);

	return val & mask;
}
EXPORT_SYMBOL(gpio_get_value);

void gpio_set_value(unsigned pin, int value)
{
	unsigned long flags;
	int mask = 1 << pin;

	spin_lock_irqsave(&gpio_lock, flags);

	orion_clrbits(GPIO_BLINK_EN, mask);
	if (value)
		orion_setbits(GPIO_OUT, mask);
	else
		orion_clrbits(GPIO_OUT, mask);

	spin_unlock_irqrestore(&gpio_lock, flags);
}
EXPORT_SYMBOL(gpio_set_value);

void orion_gpio_set_blink(unsigned pin, int blink)
{
	unsigned long flags;
	int mask = 1 << pin;

	spin_lock_irqsave(&gpio_lock, flags);

	orion_clrbits(GPIO_OUT, mask);
	if (blink)
		orion_setbits(GPIO_BLINK_EN, mask);
	else
		orion_clrbits(GPIO_BLINK_EN, mask);

	spin_unlock_irqrestore(&gpio_lock, flags);
}
EXPORT_SYMBOL(orion_gpio_set_blink);

int gpio_request(unsigned pin, const char *label)
{
	int ret = 0;
	unsigned long flags;

	if (pin >= GPIO_MAX || !test_bit(pin, gpio_valid)) {
		pr_debug("%s: invalid GPIO %d\n", __func__, pin);
		return -EINVAL;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	if (gpio_label[pin]) {
		pr_debug("%s: GPIO %d already used as %s\n",
			 __func__, pin, gpio_label[pin]);
		ret = -EBUSY;
	} else
		gpio_label[pin] = label ? label : "?";

	spin_unlock_irqrestore(&gpio_lock, flags);
	return ret;
}
EXPORT_SYMBOL(gpio_request);

void gpio_free(unsigned pin)
{
	if (pin >= GPIO_MAX || !test_bit(pin, gpio_valid)) {
		pr_debug("%s: invalid GPIO %d\n", __func__, pin);
		return;
	}

	if (!gpio_label[pin])
		pr_warning("%s: GPIO %d already freed\n", __func__, pin);
	else
		gpio_label[pin] = NULL;
}
EXPORT_SYMBOL(gpio_free);

/* Debug helper */
void gpio_display(void)
{
	int i;

	for (i = 0; i < GPIO_MAX; i++) {
		printk(KERN_DEBUG "Pin-%d: ", i);

		if (!test_bit(i, gpio_valid)) {
			printk("non-GPIO\n");
		} else if (!gpio_label[i]) {
			printk("GPIO, free\n");
		} else {
			printk("GPIO, used by %s, ", gpio_label[i]);
			if (orion_read(GPIO_IO_CONF) & (1 << i)) {
				printk("input, active %s, level %s, edge %s\n",
				((orion_read(GPIO_IN_POL) >> i) & 1) ? "low" : "high",
				((orion_read(GPIO_LEVEL_MASK) >> i) & 1) ? "enabled" : "masked",
				((orion_read(GPIO_EDGE_MASK) >> i) & 1) ? "enabled" : "masked");
			} else {
				printk("output, val=%d\n", (orion_read(GPIO_OUT) >> i) & 1);
			}
		}
	}

	printk(KERN_DEBUG "MPP_0_7_CTRL (0x%08x) = 0x%08x\n",
				MPP_0_7_CTRL, orion_read(MPP_0_7_CTRL));
	printk(KERN_DEBUG "MPP_8_15_CTRL (0x%08x) = 0x%08x\n",
				MPP_8_15_CTRL, orion_read(MPP_8_15_CTRL));
	printk(KERN_DEBUG "MPP_16_19_CTRL (0x%08x) = 0x%08x\n",
				MPP_16_19_CTRL, orion_read(MPP_16_19_CTRL));
	printk(KERN_DEBUG "MPP_DEV_CTRL (0x%08x) = 0x%08x\n",
				MPP_DEV_CTRL, orion_read(MPP_DEV_CTRL));
	printk(KERN_DEBUG "GPIO_OUT (0x%08x) = 0x%08x\n",
				GPIO_OUT, orion_read(GPIO_OUT));
	printk(KERN_DEBUG "GPIO_IO_CONF (0x%08x) = 0x%08x\n",
				GPIO_IO_CONF, orion_read(GPIO_IO_CONF));
	printk(KERN_DEBUG "GPIO_BLINK_EN (0x%08x) = 0x%08x\n",
				GPIO_BLINK_EN, orion_read(GPIO_BLINK_EN));
	printk(KERN_DEBUG "GPIO_IN_POL (0x%08x) = 0x%08x\n",
				GPIO_IN_POL, orion_read(GPIO_IN_POL));
	printk(KERN_DEBUG "GPIO_DATA_IN (0x%08x) = 0x%08x\n",
				GPIO_DATA_IN, orion_read(GPIO_DATA_IN));
	printk(KERN_DEBUG "GPIO_LEVEL_MASK (0x%08x) = 0x%08x\n",
				GPIO_LEVEL_MASK, orion_read(GPIO_LEVEL_MASK));
	printk(KERN_DEBUG "GPIO_EDGE_CAUSE (0x%08x) = 0x%08x\n",
				GPIO_EDGE_CAUSE, orion_read(GPIO_EDGE_CAUSE));
	printk(KERN_DEBUG "GPIO_EDGE_MASK (0x%08x) = 0x%08x\n",
				GPIO_EDGE_MASK, orion_read(GPIO_EDGE_MASK));
}
