/* arch/arm/mach-rk29/io.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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
#include <mach/rk29_iomap.h>
#include <asm/mach/map.h>
#include <mach/board.h>

#define RK29_DEVICE(name) { \
		.virtual = (unsigned long) RK29_##name##_BASE, \
		.pfn = __phys_to_pfn(RK29_##name##_PHYS), \
		.length = RK29_##name##_SIZE, \
		.type = MT_DEVICE_NONSHARED, \
	 }

static struct map_desc rk29_io_desc[] __initdata = {
	RK29_DEVICE(GICCPU),
	RK29_DEVICE(GICPERI),
	RK29_DEVICE(TIMER0),
	RK29_DEVICE(TIMER1),
	RK29_DEVICE(TIMER2),
	RK29_DEVICE(TIMER3),
	RK29_DEVICE(DDRC),
	RK29_DEVICE(UART1),
	RK29_DEVICE(PWM),
	RK29_DEVICE(GRF),
	RK29_DEVICE(CRU),
	RK29_DEVICE(PMU),
	RK29_DEVICE(GPIO0),
	RK29_DEVICE(GPIO1),
	RK29_DEVICE(GPIO2),
	RK29_DEVICE(GPIO3),
	RK29_DEVICE(GPIO4),
	RK29_DEVICE(GPIO5),
	RK29_DEVICE(GPIO6),
	RK29_DEVICE(NANDC),
	RK29_DEVICE(SPI0),
	RK29_DEVICE(SPI1),
	RK29_DEVICE(I2C0),
	RK29_DEVICE(I2C1),
	RK29_DEVICE(I2C2),
	RK29_DEVICE(I2C3),
#ifdef CONFIG_DDR_RECONFIG
	RK29_DEVICE(LCDC),
	RK29_DEVICE(GPU),
	RK29_DEVICE(VCODEC),
	RK29_DEVICE(VIP),
	RK29_DEVICE(IPP),
	RK29_DEVICE(DMAC0),
	RK29_DEVICE(DMAC1),
	RK29_DEVICE(SDMAC0),
#endif
};

extern void rk29_boot_mode_init_by_register(void);
void __init rk29_map_common_io(void)
{
	iotable_init(rk29_io_desc, ARRAY_SIZE(rk29_io_desc));
	rk29_boot_mode_init_by_register();
}
