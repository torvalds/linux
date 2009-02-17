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
#include <linux/irq.h>
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


/*****************************************************************************
 * Orion GPIO IRQ
 *
 * GPIO_IN_POL register controls whether GPIO_DATA_IN will hold the same
 * value of the line or the opposite value.
 *
 * Level IRQ handlers: DATA_IN is used directly as cause register.
 *                     Interrupt are masked by LEVEL_MASK registers.
 * Edge IRQ handlers:  Change in DATA_IN are latched in EDGE_CAUSE.
 *                     Interrupt are masked by EDGE_MASK registers.
 * Both-edge handlers: Similar to regular Edge handlers, but also swaps
 *                     the polarity to catch the next line transaction.
 *                     This is a race condition that might not perfectly
 *                     work on some use cases.
 *
 * Every eight GPIO lines are grouped (OR'ed) before going up to main
 * cause register.
 *
 *                    EDGE  cause    mask
 *        data-in   /--------| |-----| |----\
 *     -----| |-----                         ---- to main cause reg
 *           X      \----------------| |----/
 *        polarity    LEVEL          mask
 *
 ****************************************************************************/

static void gpio_irq_ack(u32 irq)
{
	int type = irq_desc[irq].status & IRQ_TYPE_SENSE_MASK;
	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)) {
		int pin = irq_to_gpio(irq);
		writel(~(1 << (pin & 31)), GPIO_EDGE_CAUSE(pin));
	}
}

static void gpio_irq_mask(u32 irq)
{
	int pin = irq_to_gpio(irq);
	int type = irq_desc[irq].status & IRQ_TYPE_SENSE_MASK;
	u32 reg = (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)) ?
		GPIO_EDGE_MASK(pin) : GPIO_LEVEL_MASK(pin);
	u32 u = readl(reg);
	u &= ~(1 << (pin & 31));
	writel(u, reg);
}

static void gpio_irq_unmask(u32 irq)
{
	int pin = irq_to_gpio(irq);
	int type = irq_desc[irq].status & IRQ_TYPE_SENSE_MASK;
	u32 reg = (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)) ?
		GPIO_EDGE_MASK(pin) : GPIO_LEVEL_MASK(pin);
	u32 u = readl(reg);
	u |= 1 << (pin & 31);
	writel(u, reg);
}

static int gpio_irq_set_type(u32 irq, u32 type)
{
	int pin = irq_to_gpio(irq);
	struct irq_desc *desc;
	u32 u;

	u = readl(GPIO_IO_CONF(pin)) & (1 << (pin & 31));
	if (!u) {
		printk(KERN_ERR "orion gpio_irq_set_type failed "
				"(irq %d, pin %d).\n", irq, pin);
		return -EINVAL;
	}

	desc = irq_desc + irq;

	/*
	 * Set edge/level type.
	 */
	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)) {
		desc->handle_irq = handle_edge_irq;
	} else if (type & (IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW)) {
		desc->handle_irq = handle_level_irq;
	} else {
		printk(KERN_ERR "failed to set irq=%d (type=%d)\n", irq, type);
		return -EINVAL;
	}

	/*
	 * Configure interrupt polarity.
	 */
	if (type == IRQ_TYPE_EDGE_RISING || type == IRQ_TYPE_LEVEL_HIGH) {
		u = readl(GPIO_IN_POL(pin));
		u &= ~(1 << (pin & 31));
		writel(u, GPIO_IN_POL(pin));
	} else if (type == IRQ_TYPE_EDGE_FALLING || type == IRQ_TYPE_LEVEL_LOW) {
		u = readl(GPIO_IN_POL(pin));
		u |= 1 << (pin & 31);
		writel(u, GPIO_IN_POL(pin));
	} else if (type == IRQ_TYPE_EDGE_BOTH) {
		u32 v;

		v = readl(GPIO_IN_POL(pin)) ^ readl(GPIO_DATA_IN(pin));

		/*
		 * set initial polarity based on current input level
		 */
		u = readl(GPIO_IN_POL(pin));
		if (v & (1 << (pin & 31)))
			u |= 1 << (pin & 31);		/* falling */
		else
			u &= ~(1 << (pin & 31));	/* rising */
		writel(u, GPIO_IN_POL(pin));
	}

	desc->status = (desc->status & ~IRQ_TYPE_SENSE_MASK) | type;

	return 0;
}

struct irq_chip orion_gpio_irq_chip = {
	.name		= "orion_gpio",
	.ack		= gpio_irq_ack,
	.mask		= gpio_irq_mask,
	.unmask		= gpio_irq_unmask,
	.set_type	= gpio_irq_set_type,
};

void orion_gpio_irq_handler(int pinoff)
{
	u32 cause;
	int pin;

	cause = readl(GPIO_DATA_IN(pinoff)) & readl(GPIO_LEVEL_MASK(pinoff));
	cause |= readl(GPIO_EDGE_CAUSE(pinoff)) & readl(GPIO_EDGE_MASK(pinoff));

	for (pin = pinoff; pin < pinoff + 8; pin++) {
		int irq = gpio_to_irq(pin);
		struct irq_desc *desc = irq_desc + irq;

		if (!(cause & (1 << (pin & 31))))
			continue;

		if ((desc->status & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_BOTH) {
			/* Swap polarity (race with GPIO line) */
			u32 polarity;

			polarity = readl(GPIO_IN_POL(pin));
			polarity ^= 1 << (pin & 31);
			writel(polarity, GPIO_IN_POL(pin));
		}
		desc_handle_irq(irq, desc);
	}
}
