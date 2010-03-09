/*
 * Board support file for OMAP4430 SDP.
 *
 * Copyright (C) 2009 Texas Instruments
 *
 * Author: Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Based on mach-omap2/board-3430sdp.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/usb/otg.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/control.h>
#include <plat/timer-gp.h>
#include <plat/usb.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>

static struct platform_device sdp4430_lcd_device = {
	.name		= "sdp4430_lcd",
	.id		= -1,
};

static struct platform_device *sdp4430_devices[] __initdata = {
	&sdp4430_lcd_device,
};

static struct omap_lcd_config sdp4430_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel sdp4430_config[] __initdata = {
	{ OMAP_TAG_LCD,		&sdp4430_lcd_config },
};

#ifdef CONFIG_CACHE_L2X0
noinline void omap_smc1(u32 fn, u32 arg)
{
	register u32 r12 asm("r12") = fn;
	register u32 r0 asm("r0") = arg;

	/* This is common routine cache secure monitor API used to
	 * modify the PL310 secure registers.
	 * r0 contains the value to be modified and "r12" contains
	 * the monitor API number. It uses few CPU registers
	 * internally and hence they need be backed up including
	 * link register "lr".
	 * Explicitly save r11 and r12 the compiler generated code
	 * won't save it.
	 */
	asm volatile(
		"stmfd r13!, {r11,r12}\n"
		"dsb\n"
		"smc\n"
		"ldmfd r13!, {r11,r12}\n"
		: "+r" (r0), "+r" (r12)
		:
		: "r4", "r5", "r10", "lr", "cc");
}
EXPORT_SYMBOL(omap_smc1);

static int __init omap_l2_cache_init(void)
{
	void __iomem *l2cache_base;

	/* To avoid code running on other OMAPs in
	 * multi-omap builds
	 */
	if (!cpu_is_omap44xx())
		return -ENODEV;

	/* Static mapping, never released */
	l2cache_base = ioremap(OMAP44XX_L2CACHE_BASE, SZ_4K);
	BUG_ON(!l2cache_base);

	/* Enable PL310 L2 Cache controller */
	omap_smc1(0x102, 0x1);

	/* 32KB way size, 16-way associativity,
	* parity disabled
	*/
	l2x0_init(l2cache_base, 0x0e050000, 0xc0000fff);

	return 0;
}
early_initcall(omap_l2_cache_init);
#endif

static void __init gic_init_irq(void)
{
	void __iomem *base;

	/* Static mapping, never released */
	base = ioremap(OMAP44XX_GIC_DIST_BASE, SZ_4K);
	BUG_ON(!base);
	gic_dist_init(0, base, 29);

	/* Static mapping, never released */
	gic_cpu_base_addr = ioremap(OMAP44XX_GIC_CPU_BASE, SZ_512);
	BUG_ON(!gic_cpu_base_addr);
	gic_cpu_init(0, gic_cpu_base_addr);
}

static void __init omap_4430sdp_init_irq(void)
{
	omap_board_config = sdp4430_config;
	omap_board_config_size = ARRAY_SIZE(sdp4430_config);
	omap2_init_common_hw(NULL, NULL);
#ifdef CONFIG_OMAP_32K_TIMER
	omap2_gp_clockevent_set_gptimer(1);
#endif
	gic_init_irq();
	omap_gpio_init();
}

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_UTMI,
	.mode			= MUSB_PERIPHERAL,
	.power			= 100,
};

static void __init omap_4430sdp_init(void)
{
	platform_add_devices(sdp4430_devices, ARRAY_SIZE(sdp4430_devices));
	omap_serial_init();
	/* OMAP4 SDP uses internal transceiver so register nop transceiver */
	usb_nop_xceiv_register();
	/* FIXME: allow multi-omap to boot until musb is updated for omap4 */
	if (!cpu_is_omap44xx())
		usb_musb_init(&musb_board_data);
}

static void __init omap_4430sdp_map_io(void)
{
	omap2_set_globals_443x();
	omap44xx_map_common_io();
}

MACHINE_START(OMAP_4430SDP, "OMAP4430 4430SDP board")
	/* Maintainer: Santosh Shilimkar - Texas Instruments Inc */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xfa000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_4430sdp_map_io,
	.init_irq	= omap_4430sdp_init_irq,
	.init_machine	= omap_4430sdp_init,
	.timer		= &omap_timer,
MACHINE_END
