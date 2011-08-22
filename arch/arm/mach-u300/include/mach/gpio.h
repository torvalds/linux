/*
 *
 * arch/arm/mach-u300/include/mach/gpio.h
 *
 *
 * Copyright (C) 2007-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * GPIO block resgister definitions and inline macros for
 * U300 GPIO COH 901 335 or COH 901 571/3
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */

#ifndef __MACH_U300_GPIO_H
#define __MACH_U300_GPIO_H

#define __ARM_GPIOLIB_COMPLEX

/* These can be found in arch/arm/mach-u300/gpio.c */
extern int gpio_is_valid(int number);
extern int gpio_request(unsigned gpio, const char *label);
extern void gpio_free(unsigned gpio);
extern int gpio_direction_input(unsigned gpio);
extern int gpio_direction_output(unsigned gpio, int value);
extern int gpio_register_callback(unsigned gpio,
				  int (*func)(void *arg),
				  void *);
extern int gpio_unregister_callback(unsigned gpio);
extern void enable_irq_on_gpio_pin(unsigned gpio, int edge);
extern void disable_irq_on_gpio_pin(unsigned gpio);
extern void gpio_pullup(unsigned gpio, int value);
extern int gpio_get_value(unsigned gpio);
extern void gpio_set_value(unsigned gpio, int value);

#define gpio_get_value_cansleep gpio_get_value
#define gpio_set_value_cansleep gpio_set_value

/* translates a pin number to a port number */
#define PIN_TO_PORT(val) (val >> 3)

/* wrappers to sleep-enable the previous two functions */
static inline unsigned gpio_to_irq(unsigned gpio)
{
	return PIN_TO_PORT(gpio) + IRQ_U300_GPIO_PORT0;
}
#define gpio_to_irq gpio_to_irq

#endif /* __MACH_U300_GPIO_H */
