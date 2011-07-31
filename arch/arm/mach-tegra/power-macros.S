/*
 * arch/arm/mach-tegra/power-macros.S
 *
 * Assembly macros useful for power state save / restore routines
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/* returns the ID of the current processor */
.macro	cpu_id, rd
	mrc	p15, 0, \rd, c0, c0, 5
	and	\rd, \rd, #0xF
.endm


.macro mov32, reg, val
	movw	\reg, #:lower16:\val
	movt	\reg, #:upper16:\val
.endm

/* waits until the microsecond counter (base) ticks, for exact timing loops */
.macro	wait_for_us, rd, base, tmp
	ldr	\rd, [\base]
1001:	ldr	\tmp, [\base]
	cmp	\rd, \tmp
	beq	1001b
	mov	\tmp, \rd
.endm

/* waits until the microsecond counter (base) is >= rn */
.macro	wait_until, rn, base, tmp
1002:	ldr	\tmp, [\base]
	sub	\tmp, \tmp, \rn
	ands	\tmp, \tmp, #0x80000000
	dmb
	bne	1002b
.endm

/* Enable Coresight access on cpu */
.macro	enable_coresite, tmp
	mov32	\tmp, 0xC5ACCE55
	mcr	p14, 0, \tmp, c7, c12, 6
.endm
