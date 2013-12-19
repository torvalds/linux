/*
 * This file contains the address info for various AM33XX modules.
 *
 * Copyright (C) 2011 Texas Instruments, Inc. - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_ARCH_AM33XX_H
#define __ASM_ARCH_AM33XX_H

#define L4_SLOW_AM33XX_BASE	0x48000000

#define AM33XX_SCM_BASE		0x44E10000
#define AM33XX_CTRL_BASE	AM33XX_SCM_BASE
#define AM33XX_PRCM_BASE	0x44E00000
#define AM43XX_PRCM_BASE	0x44DF0000
#define AM33XX_TAP_BASE		(AM33XX_CTRL_BASE + 0x3FC)

#endif /* __ASM_ARCH_AM33XX_H */
