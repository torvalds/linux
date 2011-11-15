/*
 * include/mach/hardware.h
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * core header file for Lichee Linux BSP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __SW_HARDWARE_H
#define __SW_HARDWARE_H

#include <asm/sizes.h>

/* macro to get at IO space when running virtually */
#define IO_ADDRESS(x)           ((x) + 0xf0000000)

#define __io_address(n)         __io(IO_ADDRESS(n))

#endif


