/*
 * Early serial output macro for Rockchip SoCs
 *
 * Copyright (C) 2012 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#if defined(CONFIG_DEBUG_RK29_UART0)
#define ROCKCHIP_UART_DEBUG_PHYS_BASE 0x20060000
#define ROCKCHIP_UART_DEBUG_VIRT_BASE 0xfed60000
#elif defined(CONFIG_DEBUG_RK29_UART1)
#define ROCKCHIP_UART_DEBUG_PHYS_BASE 0x20064000
#define ROCKCHIP_UART_DEBUG_VIRT_BASE 0xfed64000
#elif defined(CONFIG_DEBUG_RK29_UART2)
#define ROCKCHIP_UART_DEBUG_PHYS_BASE 0x20068000
#define ROCKCHIP_UART_DEBUG_VIRT_BASE 0xfed68000
#elif defined(CONFIG_DEBUG_RK3X_UART0)
#define ROCKCHIP_UART_DEBUG_PHYS_BASE 0x10124000
#define ROCKCHIP_UART_DEBUG_VIRT_BASE 0xfeb24000
#elif defined(CONFIG_DEBUG_RK3X_UART1)
#define ROCKCHIP_UART_DEBUG_PHYS_BASE 0x10126000
#define ROCKCHIP_UART_DEBUG_VIRT_BASE 0xfeb26000
#elif defined(CONFIG_DEBUG_RK3X_UART2)
#define ROCKCHIP_UART_DEBUG_PHYS_BASE 0x20064000
#define ROCKCHIP_UART_DEBUG_VIRT_BASE 0xfed64000
#elif defined(CONFIG_DEBUG_RK3X_UART3)
#define ROCKCHIP_UART_DEBUG_PHYS_BASE 0x20068000
#define ROCKCHIP_UART_DEBUG_VIRT_BASE 0xfed68000
#endif

	.macro	addruart, rp, rv, tmp
	ldr	\rp, =ROCKCHIP_UART_DEBUG_PHYS_BASE
	ldr	\rv, =ROCKCHIP_UART_DEBUG_VIRT_BASE
	.endm

#include <asm/hardware/debug-8250.S>
