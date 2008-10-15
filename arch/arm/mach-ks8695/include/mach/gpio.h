/*
 * arch/arm/mach-ks8695/include/mach/gpio.h
 *
 * Copyright (C) 2006 Andrew Victor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_GPIO_H_
#define __ASM_ARCH_GPIO_H_

#define KS8695_GPIO_0		0
#define KS8695_GPIO_1		1
#define KS8695_GPIO_2		2
#define KS8695_GPIO_3		3
#define KS8695_GPIO_4		4
#define KS8695_GPIO_5		5
#define KS8695_GPIO_6		6
#define KS8695_GPIO_7		7
#define KS8695_GPIO_8		8
#define KS8695_GPIO_9		9
#define KS8695_GPIO_10		10
#define KS8695_GPIO_11		11
#define KS8695_GPIO_12		12
#define KS8695_GPIO_13		13
#define KS8695_GPIO_14		14
#define KS8695_GPIO_15		15


/*
 * Configure GPIO pin as external interrupt source.
 */
int __init_or_module ks8695_gpio_interrupt(unsigned int pin, unsigned int type);

/*
 * Configure the GPIO line as an input.
 */
int __init_or_module gpio_direction_input(unsigned int pin);

/*
 * Configure the GPIO line as an output, with default state.
 */
int __init_or_module gpio_direction_output(unsigned int pin, unsigned int state);

/*
 * Set the state of an output GPIO line.
 */
void gpio_set_value(unsigned int pin, unsigned int state);

/*
 * Read the state of a GPIO line.
 */
int gpio_get_value(unsigned int pin);

/*
 * Map GPIO line to IRQ number.
 */
int gpio_to_irq(unsigned int pin);

/*
 * Map IRQ number to GPIO line.
 */
int irq_to_gpio(unsigned int irq);


#include <asm-generic/gpio.h>

static inline int gpio_request(unsigned int pin, const char *label)
{
	return 0;
}

static inline void gpio_free(unsigned int pin)
{
}

#endif
