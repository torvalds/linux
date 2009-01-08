/*
 * arch/arm/mach-w90x900/include/mach/uncompress.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation
 * All rights reserved.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * Based on arch/arm/mach-s3c2410/include/mach/uncompress.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

/* Defines for UART registers */

#include <mach/regs-serial.h>
#include <mach/map.h>

#define arch_decomp_wdog()

static void putc(int ch)
{
}

static inline void flush(void)
{
}

static void arch_decomp_setup(void)
{
}

#endif/* __ASM_W90X900_UNCOMPRESS_H */
