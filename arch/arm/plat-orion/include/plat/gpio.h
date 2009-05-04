/*
 * arch/arm/plat-orion/include/plat/gpio.h
 *
 * Marvell Orion SoC GPIO handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_GPIO_H
#define __PLAT_GPIO_H

/*
 * GENERIC_GPIO primitives.
 */
int gpio_request(unsigned pin, const char *label);
void gpio_free(unsigned pin);
int gpio_direction_input(unsigned pin);
int gpio_direction_output(unsigned pin, int value);
int gpio_get_value(unsigned pin);
void gpio_set_value(unsigned pin, int value);

/*
 * Orion-specific GPIO API extensions.
 */
void orion_gpio_set_unused(unsigned pin);
void orion_gpio_set_blink(unsigned pin, int blink);

#define GPIO_BIDI_OK		(1 << 0)
#define GPIO_INPUT_OK		(1 << 1)
#define GPIO_OUTPUT_OK		(1 << 2)
void orion_gpio_set_valid(unsigned pin, int mode);

/*
 * GPIO interrupt handling.
 */
extern struct irq_chip orion_gpio_irq_chip;
void orion_gpio_irq_handler(int irqoff);


#endif
