/*
 * Pistachio IRQ setup
 *
 * Copyright (C) 2014 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#ifndef __ASM_MACH_PISTACHIO_GPIO_H
#define __ASM_MACH_PISTACHIO_GPIO_H

#include <asm-generic/gpio.h>

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep
#define gpio_to_irq	__gpio_to_irq

#endif /* __ASM_MACH_PISTACHIO_GPIO_H */
