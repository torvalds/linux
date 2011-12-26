/*
 * include/mach/io.h
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

#ifndef __SW_IO_H
#define __SW_IO_H

#define IO_SPACE_LIMIT 0x1ffffff

#define SW_IO_PHYS      0x01c00000
#define SW_IO_SIZE      (SZ_1M * 3)
#define SW_IO_VIRT      0xf1c00000

#define __mem_pci(a) (a)
#define __io(a)         __typesafe_io(a)

#endif
