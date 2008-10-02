/*
 * arch/arm/mach-pnx4008/include/mach/hardware.h
 *
 * Copyright (c) 2005 MontaVista Software, Inc. <source@mvista.com>
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
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>
#include <mach/platform.h>

/* Start of virtual addresses for IO devices */
#define IO_BASE         0xF0000000

/* This macro relies on fact that for all HW i/o addresses bits 20-23 are 0 */
#define IO_ADDRESS(x)  (((((x) & 0xff000000) >> 4) | ((x) & 0xfffff)) | IO_BASE)

#endif
