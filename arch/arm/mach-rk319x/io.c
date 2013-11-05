/*
 *
 * Copyright (C) 2013 ROCKCHIP, Inc.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <mach/debug_uart.h>

#define RK319X_DEVICE(name) { \
		.virtual = (unsigned long) RK319X_##name##_BASE, \
		.pfn = __phys_to_pfn(RK319X_##name##_PHYS), \
		.length = RK319X_##name##_SIZE, \
		.type = MT_DEVICE, \
	}

static struct map_desc rk319x_io_desc[] __initdata = {
	RK319X_DEVICE(ROM),
	RK319X_DEVICE(CORE),
	RK319X_DEVICE(CPU_AXI_BUS),
#if CONFIG_RK_DEBUG_UART == 0
	RK319X_DEVICE(UART0),
#elif CONFIG_RK_DEBUG_UART == 1
	RK319X_DEVICE(UART1),
#elif CONFIG_RK_DEBUG_UART == 2
	RK319X_DEVICE(UART2),
#elif CONFIG_RK_DEBUG_UART == 3
	RK319X_DEVICE(UART3),
#endif
	RK319X_DEVICE(GRF),
	RK319X_DEVICE(BB_GRF),
	RK319X_DEVICE(CRU),
	RK319X_DEVICE(PMU),
	RK319X_DEVICE(GPIO0),
	RK319X_DEVICE(GPIO1),
	RK319X_DEVICE(GPIO2),
	RK319X_DEVICE(GPIO3),
	RK319X_DEVICE(GPIO4),
	RK319X_DEVICE(TIMER),
	RK319X_DEVICE(EFUSE),
	RK319X_DEVICE(PWM),
	RK319X_DEVICE(DDR_PCTL),
	RK319X_DEVICE(DDR_PUBL),
};

void __init rk30_map_common_io(void)
{
	iotable_init(rk319x_io_desc, ARRAY_SIZE(rk319x_io_desc));
}
