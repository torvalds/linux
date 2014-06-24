/*
 * Copyright (C) 2013-2014 ROCKCHIP, Inc.
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

#include <linux/rockchip/iomap.h>
#define UART_SHIFT 2

	.macro addruart, rp, rv, tmp
		mrc	p15, 0, \tmp, c0, c0, 0
		ldr	\rv, = 0x413fc090
		cmp	\tmp, \rv
		ldreq	\rp, = RK3188_UART2_PHYS
		beq	10f
		ldr	\rv, = 0x410fc075
		cmp	\tmp, \rv
		ldreq	\rp, = RK3036_UART2_PHYS
		beq	10f
		ldr	\rp, = RK3288_UART_DBG_PHYS
10:		ldr	\rv, = 0xfec00000
		mov	\tmp, \rp
		bic	\tmp, \tmp, #0xff000000
		bic	\tmp, \tmp, #0x00f00000
		add	\rv, \rv, \tmp
	.endm

#include "8250_32.S"
