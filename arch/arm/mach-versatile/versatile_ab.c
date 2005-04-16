/*
 *  linux/arch/arm/mach-versatile/versatile_ab.c
 *
 *  Copyright (C) 2004 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
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

#include <linux/config.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/hardware/amba.h>

#include <asm/mach/arch.h>

#include "core.h"

MACHINE_START(VERSATILE_AB, "ARM-Versatile AB")
	MAINTAINER("ARM Ltd/Deep Blue Solutions Ltd")
	BOOT_MEM(0x00000000, 0x101f1000, 0xf11f1000)
	BOOT_PARAMS(0x00000100)
	MAPIO(versatile_map_io)
	INITIRQ(versatile_init_irq)
	.timer		= &versatile_timer,
	INIT_MACHINE(versatile_init)
MACHINE_END
