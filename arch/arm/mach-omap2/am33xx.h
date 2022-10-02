/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file contains the address info for various AM33XX modules.
 *
 * Copyright (C) 2011 Texas Instruments, Inc. - https://www.ti.com/
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
