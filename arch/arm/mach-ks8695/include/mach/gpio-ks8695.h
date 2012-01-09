/*
 * Copyright (C) 2006 Andrew Victor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_KS8659_GPIO_H
#define __MACH_KS8659_GPIO_H

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

/* Register the GPIOs */
extern void ks8695_register_gpios(void);

#endif /* __MACH_KS8659_GPIO_H */
