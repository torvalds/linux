/*
 * arch/arm/mach-tegra/include/mach/debug-macro.S
 *
 * Copyright (C) 2010,2011 Google, Inc.
 * Copyright (C) 2011-2012 NVIDIA CORPORATION. All Rights Reserved.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Erik Gilling <konkers@google.com>
 *	Doug Anderson <dianders@chromium.org>
 *	Stephen Warren <swarren@nvidia.com>
 *
 * Portions based on mach-omap2's debug-macro.S
 * Copyright (C) 1994-1999 Russell King
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

#include <linux/serial_reg.h>

#include "../../iomap.h"
#include "../../irammap.h"

		.macro  addruart, rp, rv, tmp
		adr	\rp, 99f		@ actual addr of 99f
		ldr	\rv, [\rp]		@ linked addr is stored there
		sub	\rv, \rv, \rp		@ offset between the two
		ldr	\rp, [\rp, #4]		@ linked tegra_uart_config
		sub	\tmp, \rp, \rv		@ actual tegra_uart_config
		ldr	\rp, [\tmp]		@ Load tegra_uart_config
		cmp	\rp, #1			@ needs intitialization?
		bne	100f			@ no; go load the addresses
		mov	\rv, #0			@ yes; record init is done
		str	\rv, [\tmp]
		mov	\rp, #TEGRA_IRAM_BASE	@ See if cookie is in IRAM
		ldr	\rv, [\rp, #TEGRA_IRAM_DEBUG_UART_OFFSET]
		movw	\rp, #TEGRA_IRAM_DEBUG_UART_COOKIE & 0xffff
		movt	\rp, #TEGRA_IRAM_DEBUG_UART_COOKIE >> 16
		cmp	\rv, \rp		@ Cookie present?
		bne	100f			@ No, use default UART
		mov	\rp, #TEGRA_IRAM_BASE	@ Load UART address from IRAM
		ldr	\rv, [\rp, #TEGRA_IRAM_DEBUG_UART_OFFSET + 4]
		str	\rv, [\tmp, #4]		@ Store in tegra_uart_phys
		sub	\rv, \rv, #IO_APB_PHYS	@ Calculate virt address
		add	\rv, \rv, #IO_APB_VIRT
		str	\rv, [\tmp, #8]		@ Store in tegra_uart_virt
		b	100f

		.align
99:		.word	.
		.word	tegra_uart_config
		.ltorg

100:		ldr	\rp, [\tmp, #4]		@ Load tegra_uart_phys
		ldr	\rv, [\tmp, #8]		@ Load tegra_uart_virt
		.endm

#define UART_SHIFT 2

/*
 * Code below is swiped from <asm/hardware/debug-8250.S>, but add an extra
 * check to make sure that we aren't in the CONFIG_TEGRA_DEBUG_UART_NONE case.
 * We use the fact that all 5 valid UART addresses all have something in the
 * 2nd-to-lowest byte.
 */

		.macro	senduart, rd, rx
		tst	\rx, #0x0000ff00
		strneb	\rd, [\rx, #UART_TX << UART_SHIFT]
1001:
		.endm

		.macro	busyuart, rd, rx
		tst	\rx, #0x0000ff00
		beq	1002f
1001:		ldrb	\rd, [\rx, #UART_LSR << UART_SHIFT]
		and	\rd, \rd, #UART_LSR_TEMT | UART_LSR_THRE
		teq	\rd, #UART_LSR_TEMT | UART_LSR_THRE
		bne	1001b
1002:
		.endm

		.macro	waituart, rd, rx
#ifdef FLOW_CONTROL
		tst	\rx, #0x0000ff00
		beq	1002f
1001:		ldrb	\rd, [\rx, #UART_MSR << UART_SHIFT]
		tst	\rd, #UART_MSR_CTS
		beq	1001b
1002:
#endif
		.endm
