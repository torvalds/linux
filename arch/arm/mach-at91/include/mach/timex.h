/*
 * arch/arm/mach-at91/include/mach/timex.h
 *
 *  Copyright (C) 2003 SAN People
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_TIMEX_H
#define __ASM_ARCH_TIMEX_H

#include <mach/hardware.h>

#if defined(CONFIG_ARCH_AT91RM9200)

#define CLOCK_TICK_RATE		(AT91_SLOW_CLOCK)

#elif defined(CONFIG_ARCH_AT91SAM9260)

#if defined(CONFIG_MACH_USB_A9260) || defined(CONFIG_MACH_QIL_A9260)
#define AT91SAM9_MASTER_CLOCK	90000000
#else
#define AT91SAM9_MASTER_CLOCK	99300000
#endif

#define CLOCK_TICK_RATE		(AT91SAM9_MASTER_CLOCK/16)

#elif defined(CONFIG_ARCH_AT91SAM9261)

#define AT91SAM9_MASTER_CLOCK	99300000
#define CLOCK_TICK_RATE		(AT91SAM9_MASTER_CLOCK/16)

#elif defined(CONFIG_ARCH_AT91SAM9G10)

#define AT91SAM9_MASTER_CLOCK	133000000
#define CLOCK_TICK_RATE		(AT91SAM9_MASTER_CLOCK/16)

#elif defined(CONFIG_ARCH_AT91SAM9263)

#if defined(CONFIG_MACH_USB_A9263)
#define AT91SAM9_MASTER_CLOCK	90000000
#else
#define AT91SAM9_MASTER_CLOCK	99959500
#endif

#define CLOCK_TICK_RATE		(AT91SAM9_MASTER_CLOCK/16)

#elif defined(CONFIG_ARCH_AT91SAM9RL)

#define AT91SAM9_MASTER_CLOCK	100000000
#define CLOCK_TICK_RATE		(AT91SAM9_MASTER_CLOCK/16)

#elif defined(CONFIG_ARCH_AT91SAM9G20)

#if defined(CONFIG_MACH_USB_A9G20)
#define AT91SAM9_MASTER_CLOCK	133000000
#else
#define AT91SAM9_MASTER_CLOCK	132096000
#endif

#define CLOCK_TICK_RATE		(AT91SAM9_MASTER_CLOCK/16)

#elif defined(CONFIG_ARCH_AT91SAM9G45)

#define AT91SAM9_MASTER_CLOCK	133333333
#define CLOCK_TICK_RATE		(AT91SAM9_MASTER_CLOCK/16)

#elif defined(CONFIG_ARCH_AT91CAP9)

#define AT91CAP9_MASTER_CLOCK	100000000
#define CLOCK_TICK_RATE		(AT91CAP9_MASTER_CLOCK/16)

#elif defined(CONFIG_ARCH_AT91X40)

#define AT91X40_MASTER_CLOCK	40000000
#define CLOCK_TICK_RATE		(AT91X40_MASTER_CLOCK)

#endif

#endif
