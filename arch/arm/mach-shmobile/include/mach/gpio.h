/*
 * Generic GPIO API and pinmux table support
 *
 * Copyright (c) 2008  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_ARCH_GPIO_H
#define __ASM_ARCH_GPIO_H

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/sh_pfc.h>
#include <linux/io.h>

#ifdef CONFIG_GPIOLIB

static inline int irq_to_gpio(unsigned int irq)
{
	return -ENOSYS;
}

#else

#define __ARM_GPIOLIB_COMPLEX

#endif /* CONFIG_GPIOLIB */

/*
 * FIXME !!
 *
 * current gpio frame work doesn't have
 * the method to control only pull up/down/free.
 * this function should be replaced by correct gpio function
 */
static inline void __init gpio_direction_none(void __iomem * addr)
{
	__raw_writeb(0x00, addr);
}

static inline void __init gpio_request_pullup(void __iomem * addr)
{
	u8 data = __raw_readb(addr);

	data &= 0x0F;
	data |= 0xC0;
	__raw_writeb(data, addr);
}

static inline void __init gpio_request_pulldown(void __iomem * addr)
{
	u8 data = __raw_readb(addr);

	data &= 0x0F;
	data |= 0xA0;

	__raw_writeb(data, addr);
}

#endif /* __ASM_ARCH_GPIO_H */
