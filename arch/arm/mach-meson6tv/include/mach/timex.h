/*
 *  arch/arm/mach-meson6tv/include/mach/timex.h
 *
 *  Amlogic architecture timex specifications
 *
 *  Copyright (C) 2010-2013 AMLOGIC, INC.
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


#ifndef __MACH_MESON6TV_TIMEX_H
#define __MACH_MESON6TV_TIMEX_H

#ifndef CONFIG_MESON_CLOCK_TICK_RATE
#define CLOCK_TICK_RATE     25000000
#else
#define CLOCK_TICK_RATE     (CONFIG_MESON_CLOCK_TICK_RATE)
#endif

#endif // __MACH_MESON6TV_TIMEX_H