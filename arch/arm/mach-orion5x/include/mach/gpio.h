/*
 * arch/arm/mach-orion5x/include/mach/gpio.h
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

extern int gpio_request(unsigned pin, const char *label);
extern void gpio_free(unsigned pin);
extern int gpio_direction_input(unsigned pin);
extern int gpio_direction_output(unsigned pin, int value);
extern int gpio_get_value(unsigned pin);
extern void gpio_set_value(unsigned pin, int value);
extern void orion5x_gpio_set_blink(unsigned pin, int blink);
extern void gpio_display(void);		/* debug */

static inline int gpio_to_irq(int pin)
{
	return pin + IRQ_ORION5X_GPIO_START;
}

static inline int irq_to_gpio(int irq)
{
	return irq - IRQ_ORION5X_GPIO_START;
}

#include <asm-generic/gpio.h>		/* cansleep wrappers */
