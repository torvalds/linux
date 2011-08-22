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

/*
 * Map IRQ number to GPIO line.
 */
extern int irq_to_gpio(unsigned int irq);

#endif
