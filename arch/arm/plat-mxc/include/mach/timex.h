/*
 *  Copyright (C) 1999 ARM Limited
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_ARCH_MXC_TIMEX_H__
#define __ASM_ARCH_MXC_TIMEX_H__

#if defined CONFIG_ARCH_MX1
#define CLOCK_TICK_RATE		16000000
#elif defined CONFIG_ARCH_MX2
#define CLOCK_TICK_RATE		13300000
#elif defined CONFIG_ARCH_MX3
#define CLOCK_TICK_RATE		16625000
#elif defined CONFIG_ARCH_MX25
#define CLOCK_TICK_RATE		16000000
#elif defined CONFIG_ARCH_MX5
#define CLOCK_TICK_RATE		8000000
#elif defined CONFIG_ARCH_MXC91231
#define CLOCK_TICK_RATE		13000000
#endif

#endif				/* __ASM_ARCH_MXC_TIMEX_H__ */
