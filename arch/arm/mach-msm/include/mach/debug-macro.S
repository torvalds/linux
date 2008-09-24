/* arch/arm/mach-msm7200/include/mach/debug-macro.S
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <mach/hardware.h>
#include <mach/msm_iomap.h>

	.macro	addruart,rx
	@ see if the MMU is enabled and select appropriate base address
	mrc	p15, 0, \rx, c1, c0
	tst	\rx, #1
	ldreq	\rx, =MSM_UART1_PHYS
	ldrne	\rx, =MSM_UART1_BASE
	.endm

	.macro	senduart,rd,rx
	str	\rd, [\rx, #0x0C]
	.endm

	.macro	waituart,rd,rx
	@ wait for TX_READY
1:	ldr	\rd, [\rx, #0x08]
	tst	\rd, #0x04
	beq	1b
	.endm

	.macro	busyuart,rd,rx
	.endm
