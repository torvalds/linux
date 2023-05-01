/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Coldfire generic GPIO support
 *
 * (C) Copyright 2009, Steven King <sfking@fdwdc.com>
*/

#ifndef coldfire_gpio_h
#define coldfire_gpio_h

#include <linux/io.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfgpio.h>
/*
 * The Generic GPIO functions
 *
 * If the gpio is a compile time constant and is one of the Coldfire gpios,
 * use the inline version, otherwise dispatch thru gpiolib.
 */

static inline int gpio_get_value(unsigned gpio)
{
	if (__builtin_constant_p(gpio) && gpio < MCFGPIO_PIN_MAX)
		return mcfgpio_read(__mcfgpio_ppdr(gpio)) & mcfgpio_bit(gpio);
	else
		return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	if (__builtin_constant_p(gpio) && gpio < MCFGPIO_PIN_MAX) {
		if (gpio < MCFGPIO_SCR_START) {
			unsigned long flags;
			MCFGPIO_PORTTYPE data;

			local_irq_save(flags);
			data = mcfgpio_read(__mcfgpio_podr(gpio));
			if (value)
				data |= mcfgpio_bit(gpio);
			else
				data &= ~mcfgpio_bit(gpio);
			mcfgpio_write(data, __mcfgpio_podr(gpio));
			local_irq_restore(flags);
		} else {
			if (value)
				mcfgpio_write(mcfgpio_bit(gpio),
						MCFGPIO_SETR_PORT(gpio));
			else
				mcfgpio_write(~mcfgpio_bit(gpio),
						MCFGPIO_CLRR_PORT(gpio));
		}
	} else
		__gpio_set_value(gpio, value);
}

static inline int gpio_to_irq(unsigned gpio)
{
#if defined(MCFGPIO_IRQ_MIN)
	if ((gpio >= MCFGPIO_IRQ_MIN) && (gpio < MCFGPIO_IRQ_MAX))
#else
	if (gpio < MCFGPIO_IRQ_MAX)
#endif
		return gpio + MCFGPIO_IRQ_VECBASE;
	else
		return __gpio_to_irq(gpio);
}

static inline int gpio_cansleep(unsigned gpio)
{
	return gpio < MCFGPIO_PIN_MAX ? 0 : __gpio_cansleep(gpio);
}

#ifndef CONFIG_GPIOLIB
static inline int gpio_request_one(unsigned gpio, unsigned long flags, const char *label)
{
	int err;

	err = gpio_request(gpio, label);
	if (err)
		return err;

	if (flags & GPIOF_DIR_IN)
		err = gpio_direction_input(gpio);
	else
		err = gpio_direction_output(gpio,
			(flags & GPIOF_INIT_HIGH) ? 1 : 0);

	if (err)
		gpio_free(gpio);

	return err;
}
#endif /* !CONFIG_GPIOLIB */
#endif
