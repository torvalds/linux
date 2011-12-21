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

#ifdef CONFIG_GPIOLIB

static inline int irq_to_gpio(unsigned int irq)
{
	return -ENOSYS;
}

#else

#define __ARM_GPIOLIB_COMPLEX

#endif /* CONFIG_GPIOLIB */

#endif /* __ASM_ARCH_GPIO_H */
