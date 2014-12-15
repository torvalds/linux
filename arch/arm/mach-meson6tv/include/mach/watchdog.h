/*
 *  arch/arm/mach-meson/include/mach/system.h
 *
 *  Copyright (C) 2010 AMLOGIC, INC.
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
#ifndef __ASM_ARCH_WATCHDOG_H
#define __ASM_ARCH_WATCHDOG_H

#include <linux/io.h>
#include <plat/io.h>
#include <mach/hardware.h>
#include <mach/register.h>

#define WATCHDOG_ENABLE_BIT  22
#define WATCHDOG_COUNT_MASK ((1<<WATCHDOG_ENABLE_BIT)-1)
#define WDT_ONE_SECOND 100000

#define MAX_TIMEOUT (WATCHDOG_COUNT_MASK/100000)
#define MIN_TIMEOUT 1

#endif
