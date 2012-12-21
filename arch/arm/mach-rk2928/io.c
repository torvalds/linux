/* arch/arm/mach-rk2928/io.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
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

#define RK2928_DEVICE(name) { \
		.virtual = (unsigned long) RK2928_##name##_BASE, \
		.pfn = __phys_to_pfn(RK2928_##name##_PHYS), \
		.length = RK2928_##name##_SIZE, \
		.type = MT_DEVICE, \
	}

static struct map_desc rk2928_io_desc[] __initdata = {
	RK2928_DEVICE(CORE),
	RK2928_DEVICE(CPU_AXI_BUS),
#if CONFIG_RK_DEBUG_UART == 0
	RK2928_DEVICE(UART0),
#elif CONFIG_RK_DEBUG_UART == 1
	RK2928_DEVICE(UART1),
#elif CONFIG_RK_DEBUG_UART == 2
	RK2928_DEVICE(UART2),
#endif
	RK2928_DEVICE(GRF),
	RK2928_DEVICE(CRU),
	RK2928_DEVICE(GPIO0),
	RK2928_DEVICE(GPIO1),
	RK2928_DEVICE(GPIO2),
	RK2928_DEVICE(GPIO3),
	RK2928_DEVICE(TIMER0),
	RK2928_DEVICE(TIMER1),
	RK2928_DEVICE(PWM),
	RK2928_DEVICE(DDR_PCTL),
	RK2928_DEVICE(DDR_PHY),
	RK2928_DEVICE(RKI2C0),
	RK2928_DEVICE(RKI2C1),
};

void __init rk2928_map_common_io(void)
{
	iotable_init(rk2928_io_desc, ARRAY_SIZE(rk2928_io_desc));
}
