/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#ifndef __ASM_ARCH_MXC_HARDWARE_H__
#define __ASM_ARCH_MXC_HARDWARE_H__

#include <asm/sizes.h>

#ifdef CONFIG_ARCH_MX3
#include <mach/mx3x.h>
#include <mach/mx31.h>
#include <mach/mx35.h>
#endif

#ifdef CONFIG_ARCH_MX2
# include <mach/mx2x.h>
# ifdef CONFIG_MACH_MX21
#  include <mach/mx21.h>
# endif
# ifdef CONFIG_MACH_MX27
#  include <mach/mx27.h>
# endif
#endif

#ifdef CONFIG_ARCH_MX1
# include <mach/mx1.h>
#endif

#ifdef CONFIG_ARCH_MX25
# include <mach/mx25.h>
#endif

#ifdef CONFIG_ARCH_MXC91231
# include <mach/mxc91231.h>
#endif

#include <mach/mxc.h>

#endif /* __ASM_ARCH_MXC_HARDWARE_H__ */
