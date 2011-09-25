/*
 * sh7372 lowlevel sleep code for "Core Standby Mode"
 *
 * Copyright (C) 2011 Magnus Damm
 *
 * In "Core Standby Mode" the ARM core is off, but L2 cache is still on
 *
 * Based on mach-omap2/sleep34xx.S
 *
 * (C) Copyright 2007 Texas Instruments
 * Karthik Dasu <karthik-dp@ti.com>
 *
 * (C) Copyright 2004 Texas Instruments, <www.ti.com>
 * Richard Woodruff <r-woodruff2@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR /PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/memory.h>
#include <asm/assembler.h>

	.align	12
	.text
	.global sh7372_resume_core_standby
sh7372_resume_core_standby:
	ldr     pc, 1f
1:	.long   cpu_resume - PAGE_OFFSET + PLAT_PHYS_OFFSET
