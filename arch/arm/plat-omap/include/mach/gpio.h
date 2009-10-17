/*
 * arch/arm/plat-omap/include/mach/gpio.h
 *
 * OMAP GPIO handling defines and functions
 *
 * Copyright (C) 2003-2005 Nokia Corporation
 *
 * Written by Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef __ASM_ARCH_OMAP_GPIO_H
#define __ASM_ARCH_OMAP_GPIO_H

#include <linux/io.h>
#include <mach/irqs.h>

#define OMAP1_MPUIO_BASE			0xfffb5000

#if (defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850))

#define OMAP_MPUIO_INPUT_LATCH		0x00
#define OMAP_MPUIO_OUTPUT		0x02
#define OMAP_MPUIO_IO_CNTL		0x04
#define OMAP_MPUIO_KBR_LATCH		0x08
#define OMAP_MPUIO_KBC			0x0a
#define OMAP_MPUIO_GPIO_EVENT_MODE	0x0c
#define OMAP_MPUIO_GPIO_INT_EDGE	0x0e
#define OMAP_MPUIO_KBD_INT		0x10
#define OMAP_MPUIO_GPIO_INT		0x12
#define OMAP_MPUIO_KBD_MASKIT		0x14
#define OMAP_MPUIO_GPIO_MASKIT		0x16
#define OMAP_MPUIO_GPIO_DEBOUNCING	0x18
#define OMAP_MPUIO_LATCH		0x1a
#else
#define OMAP_MPUIO_INPUT_LATCH		0x00
#define OMAP_MPUIO_OUTPUT		0x04
#define OMAP_MPUIO_IO_CNTL		0x08
#define OMAP_MPUIO_KBR_LATCH		0x10
#define OMAP_MPUIO_KBC			0x14
#define OMAP_MPUIO_GPIO_EVENT_MODE	0x18
#define OMAP_MPUIO_GPIO_INT_EDGE	0x1c
#define OMAP_MPUIO_KBD_INT		0x20
#define OMAP_MPUIO_GPIO_INT		0x24
#define OMAP_MPUIO_KBD_MASKIT		0x28
#define OMAP_MPUIO_GPIO_MASKIT		0x2c
#define OMAP_MPUIO_GPIO_DEBOUNCING	0x30
#define OMAP_MPUIO_LATCH		0x34
#endif

#define OMAP34XX_NR_GPIOS		6

#define OMAP_MPUIO(nr)		(OMAP_MAX_GPIO_LINES + (nr))
#define OMAP_GPIO_IS_MPUIO(nr)	((nr) >= OMAP_MAX_GPIO_LINES)

#define OMAP_GPIO_IRQ(nr)	(OMAP_GPIO_IS_MPUIO(nr) ? \
				 IH_MPUIO_BASE + ((nr) & 0x0f) : \
				 IH_GPIO_BASE + (nr))

extern int omap_gpio_init(void);	/* Call from board init only */
extern void omap2_gpio_prepare_for_retention(void);
extern void omap2_gpio_resume_after_retention(void);
extern void omap_set_gpio_debounce(int gpio, int enable);
extern void omap_set_gpio_debounce_time(int gpio, int enable);

/*-------------------------------------------------------------------------*/

/* Wrappers for "new style" GPIO calls, using the new infrastructure
 * which lets us plug in FPGA, I2C, and other implementations.
 * *
 * The original OMAP-specfic calls should eventually be removed.
 */

#include <linux/errno.h>
#include <asm-generic/gpio.h>

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

static inline int irq_to_gpio(unsigned irq)
{
	int tmp;

	/* omap1 SOC mpuio */
	if (cpu_class_is_omap1() && (irq < (IH_MPUIO_BASE + 16)))
		return (irq - IH_MPUIO_BASE) + OMAP_MAX_GPIO_LINES;

	/* SOC gpio */
	tmp = irq - IH_GPIO_BASE;
	if (tmp < OMAP_MAX_GPIO_LINES)
		return tmp;

	/* we don't supply reverse mappings for non-SOC gpios */
	return -EIO;
}

#endif
