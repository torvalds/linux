/*
 * arch/arm/plat-omap/include/mach/gpioexpander.h
 *
 *
 * Copyright (C) 2004 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef __ASM_ARCH_OMAP_GPIOEXPANDER_H
#define __ASM_ARCH_OMAP_GPIOEXPANDER_H

/* Function Prototypes for GPIO Expander functions */

#ifdef CONFIG_GPIOEXPANDER_OMAP
int read_gpio_expa(u8 *, int);
int write_gpio_expa(u8 , int);
#else
static inline int read_gpio_expa(u8 *val, int addr)
{
	return 0;
}
static inline int write_gpio_expa(u8 val, int addr)
{
	return 0;
}
#endif

#endif /* __ASM_ARCH_OMAP_GPIOEXPANDER_H */
