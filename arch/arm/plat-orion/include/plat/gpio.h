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

#include <linux/init.h>
#include <linux/types.h>

/*
 * Orion-specific GPIO API extensions.
 */
void orion_gpio_set_unused(unsigned pin);
void orion_gpio_set_blink(unsigned pin, int blink);
int orion_gpio_led_blink_set(unsigned gpio, int state,
	unsigned long *delay_on, unsigned long *delay_off);

#define GPIO_INPUT_OK		(1 << 0)
#define GPIO_OUTPUT_OK		(1 << 1)
void orion_gpio_set_valid(unsigned pin, int mode);

/* Initialize gpiolib. */
void __init orion_gpio_init(int gpio_base, int ngpio,
			    u32 base, int mask_offset, int secondary_irq_base);

/*
 * GPIO interrupt handling.
 */
void orion_gpio_irq_handler(int irqoff);


#endif
