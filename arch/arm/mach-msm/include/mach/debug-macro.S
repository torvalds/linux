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

#ifdef CONFIG_HAS_MSM_DEBUG_UART_PHYS
	.macro	addruart, rp, rv
	ldr	\rp, =MSM_DEBUG_UART_PHYS
	ldr	\rv, =MSM_DEBUG_UART_BASE
	.endm

	.macro	senduart,rd,rx
	teq	\rx, #0
	strne	\rd, [\rx, #0x0C]
	.endm

	.macro	waituart,rd,rx
	@ wait for TX_READY
1001:	ldr	\rd, [\rx, #0x08]
	tst	\rd, #0x04
	beq	1001b
	.endm

	.macro	busyuart,rd,rx
	.endm
#endif
