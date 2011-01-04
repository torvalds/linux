/*
 *  include/asm-sh/gpio.h
 *
 * Generic GPIO API and pinmux table support for SuperH.
 *
 * Copyright (c) 2008 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_GPIO_H
#define __ASM_SH_GPIO_H

#include <linux/kernel.h>
#include <linux/errno.h>

#if defined(CONFIG_CPU_SH3)
#include <cpu/gpio.h>
#endif

#define ARCH_NR_GPIOS 512
#include <linux/sh_pfc.h>

#ifdef CONFIG_GPIOLIB

static inline int gpio_get_value(unsigned gpio)
{
	return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	__gpio_set_value(gpio, value);
}

static inline int gpio_cansleep(unsigned gpio)
{
	return __gpio_cansleep(gpio);
}

static inline int gpio_to_irq(unsigned gpio)
{
	return __gpio_to_irq(gpio);
}

static inline int irq_to_gpio(unsigned int irq)
{
	return -ENOSYS;
}

#endif /* CONFIG_GPIOLIB */

#endif /* __ASM_SH_GPIO_H */
