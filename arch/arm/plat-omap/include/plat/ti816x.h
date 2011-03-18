/*
 * This file contains the address data for various TI816X modules.
 *
 * Copyright (C) 2010 Texas Instruments, Inc. - http://www.ti.com/
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

#ifndef __ASM_ARCH_TI816X_H
#define __ASM_ARCH_TI816X_H

#define L4_SLOW_TI816X_BASE	0x48000000

#define TI816X_SCM_BASE		0x48140000
#define TI816X_CTRL_BASE	TI816X_SCM_BASE
#define TI816X_PRCM_BASE	0x48180000

#define TI816X_ARM_INTC_BASE	0x48200000

#endif /* __ASM_ARCH_TI816X_H */
