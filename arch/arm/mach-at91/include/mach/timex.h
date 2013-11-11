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

#ifdef CONFIG_ARCH_AT91X40

#define AT91X40_MASTER_CLOCK	40000000
#define CLOCK_TICK_RATE		(AT91X40_MASTER_CLOCK)

#else

#define CLOCK_TICK_RATE		12345678

#endif

#endif /* __ASM_ARCH_TIMEX_H */
