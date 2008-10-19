/*
 * arch/arm/plat-orion/gpio.c
 *
 * Marvell Orion SoC GPIO handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <asm/gpio.h>

static DEFINE_SPINLOCK(gpio_lock);
static const char *gpio_label[GPIO_MAX];  /* non null for allocated GPIOs */
static unsigned long gpio_valid[BITS_TO_LONGS(GPIO_MAX)];

static inline void __set_direction(unsigned pin, int input)
{
	u32 u;

	u = readl(GPIO_IO_CONF(pin));
	if (input)
		u |= 1 << (pin & 31);
	else
		u &= ~(1 << (pin & 31));
	writel(u, GPIO_IO_CONF(pin));
}

static void __set_level(unsigned pin, int high)
{
	u32 u;

	u = readl(GPIO_OUT(pin));
	if (high)
		u |= 1 << (pin & 31);
	else
		u &= ~(1 << (pin & 31));
	writel(u, GPIO_OUT(pin));
}


/*
 * GENERIC_GPIO primitives.
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
	 * Some callers might not have used gpio_request(),
	 * so flag this pin as requested now.
	 */
	if (gpio_label[pin] == NULL)
		gpio_label[pin] = "?";

	/*
	 * Configure GPIO direction.
	 */
	__set_direction(pin, 1);

	spin_unlock_irqrestore(&gpio_lock, flags);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_input);

int gpio_direction_output(unsigned pin, int value)
{
	unsigned long flags;
	u32 u;

	if (pin >= GPIO_MAX || !test_bit(pin, gpio_valid)) {
		pr_debug("%s: invalid GPIO %d\n", __func__, pin);
		return -EINVAL;
	}

	spin_lock_irqsave(&gpio_lock, flags);

	/*
	 * Some callers might not have used gpio_request(),
	 * so flag this pin as requested now.
	 */
	if (gpio_label[pin] == NULL)
		gpio_label[pin] = "?";

	/*
	 * Disable blinking.
	 */
	u = readl(GPIO_BLINK_EN(pin));
	u &= ~(1 << (pin & 31));
	writel(u, GPIO_BLINK_EN(pin));

	/*
	 * Configure GPIO output value.
	 */
	__set_level(pin, value);

	/*
	 * Configure GPIO direction.
	 */
	__set_direction(pin, 0);

	spin_unlock_irqrestore(&gpio_lock, flags);

	return 0;
}
EXPORT_SYMBOL(gpio_direction_output);

int gpio_get_value(unsigned pin)
{
	int val;

	if (readl(GPIO_IO_CONF(pin)) & (1 << (pin & 31)))
		val = readl(GPIO_DATA_IN(pin)) ^ readl(GPIO_IN_POL(pin));
	else
		val = readl(GPIO_OUT(pin));

	return (val >> (pin & 31)) & 1;
}
EXPORT_SYMBOL(gpio_get_value);

void gpio_set_value(unsigned pin, int value)
{
	unsigned long flags;
	u32 u;

	spin_lock_irqsave(&gpio_lock, flags);

	/*
	 * Disable blinking.
	 */
	u = readl(GPIO_BLINK_EN(pin));
	u &= ~(1 << (pin & 31));
	writel(u, GPIO_BLINK_EN(pin));

	/*
	 * Configure GPIO output value.
	 */
	__set_level(pin, value);

	spin_unlock_irqrestore(&gpio_lock, flags);
}
EXPORT_SYMBOL(gpio_set_value);

int gpio_request(unsigned pin, const char *label)
{
	unsigned long flags;
	int ret;

	if (pin >= GPIO_MAX || !test_bit(pin, gpio_valid)) {
		pr_debug("%s: invalid GPIO %d\n", __func__, pin);
		return -EINVAL;
	}

	spin_lock_irqsave(&gpio_lock, flags);
	if (gpio_label[pin] == NULL) {
		gpio_label[pin] = label ? label : "?";
		ret = 0;
	} else {
		pr_debug("%s: GPIO %d already used as %s\n",
			 __func__, pin, gpio_label[pin]);
		ret = -EBUSY;
	}
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

	if (gpio_label[pin] == NULL)
		pr_warning("%s: GPIO %d already freed\n", __func__, pin);
	else
		gpio_label[pin] = NULL;
}
EXPORT_SYMBOL(gpio_free);


/*
 * Orion-specific GPIO API extensions.
 */
void __init orion_gpio_set_unused(unsigned pin)
{
	/*
	 * Configure as output, drive low.
	 */
	__set_level(pin, 0);
	__set_direction(pin, 0);
}

void __init orion_gpio_set_valid(unsigned pin, int valid)
{
	if (valid)
		__set_bit(pin, gpio_valid);
	else
		__clear_bit(pin, gpio_valid);
}

void orion_gpio_set_blink(unsigned pin, int blink)
{
	unsigned long flags;
	u32 u;

	spin_lock_irqsave(&gpio_lock, flags);

	/*
	 * Set output value to zero.
	 */
	__set_level(pin, 0);

	u = readl(GPIO_BLINK_EN(pin));
	if (blink)
		u |= 1 << (pin & 31);
	else
		u &= ~(1 << (pin & 31));
	writel(u, GPIO_BLINK_EN(pin));

	spin_unlock_irqrestore(&gpio_lock, flags);
}
EXPORT_SYMBOL(orion_gpio_set_blink);
