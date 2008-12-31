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

#include <linux/kernel.h>

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
extern int ks8695_gpio_interrupt(unsigned int pin, unsigned int type);

/*
 * Map IRQ number to GPIO line.
 */
extern int irq_to_gpio(unsigned int irq);

#include <asm-generic/gpio.h>

/* If it turns out that we need to optimise GPIO access for the
 * Micrel's GPIOs, then these can be changed to check their argument
 * directly as static inlines. However for now it's probably not
 * worthwhile.
 */
#define gpio_get_value __gpio_get_value
#define gpio_set_value __gpio_set_value
#define gpio_to_irq __gpio_to_irq

/* Register the GPIOs */
extern void ks8695_register_gpios(void);

#endif
