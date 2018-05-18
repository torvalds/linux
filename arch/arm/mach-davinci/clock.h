/*
 * TI DaVinci clock definitions
 *
 * Copyright (C) 2006-2007 Texas Instruments.
 * Copyright (C) 2008-2009 Deep Root Systems, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_DAVINCI_CLOCK_H
#define __ARCH_ARM_DAVINCI_CLOCK_H

/* PLL/Reset register offsets */
#define PLLCTL          0x100
#define PLLCTL_PLLEN    BIT(0)
#define PLLCTL_PLLPWRDN	BIT(1)
#define PLLCTL_PLLRST	BIT(3)
#define PLLCTL_PLLDIS	BIT(4)
#define PLLCTL_PLLENSRC	BIT(5)
#define PLLCTL_CLKMODE  BIT(8)

#define PLLM		0x110
#define PLLM_PLLM_MASK  0xff

#define PREDIV          0x114
#define PLLDIV1         0x118
#define PLLDIV2         0x11c
#define PLLDIV3         0x120
#define POSTDIV         0x128
#define BPDIV           0x12c
#define PLLCMD		0x138
#define PLLSTAT		0x13c
#define PLLALNCTL	0x140
#define PLLDCHANGE	0x144
#define PLLCKEN		0x148
#define PLLCKSTAT	0x14c
#define PLLSYSTAT	0x150
#define PLLDIV4         0x160
#define PLLDIV5         0x164
#define PLLDIV6         0x168
#define PLLDIV7         0x16c
#define PLLDIV8         0x170
#define PLLDIV9         0x174
#define PLLDIV_EN       BIT(15)
#define PLLDIV_RATIO_MASK 0x1f

/*
 * OMAP-L138 system reference guide recommends a wait for 4 OSCIN/CLKIN
 * cycles to ensure that the PLLC has switched to bypass mode. Delay of 1us
 * ensures we are good for all > 4MHz OSCIN/CLKIN inputs. Typically the input
 * is ~25MHz. Units are micro seconds.
 */
#define PLL_BYPASS_TIME		1
/* From OMAP-L138 datasheet table 6-4. Units are micro seconds */
#define PLL_RESET_TIME		1
/*
 * From OMAP-L138 datasheet table 6-4; assuming prediv = 1, sqrt(pllm) = 4
 * Units are micro seconds.
 */
#define PLL_LOCK_TIME		20

#endif
