/*
 * arch/arm/plat-spear/include/plat/gpio.h
 *
 * GPIO macros for SPEAr platform
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_GPIO_H
#define __PLAT_GPIO_H

#include <asm-generic/gpio.h>

#define gpio_get_value	__gpio_get_value
#define gpio_set_value	__gpio_set_value
#define gpio_cansleep	__gpio_cansleep
#define gpio_to_irq	__gpio_to_irq

#endif /* __PLAT_GPIO_H */
