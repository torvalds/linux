/* linux/arch/arm/mach-s3c2440/mach-rx3715.c
 *
 * Copyright (c) 2003-2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.handhelds.org/projects/rx3715.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include <linux/platform_data/mtd-nand-s3c2410.h>

#include <asm/irq.h>
#include <asm/mach-types.h>

#include <mach/fb.h>
#include <mach/hardware.h>
#include <mach/regs-gpio.h>
#include <mach/regs-lcd.h>

#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/pm.h>
#include <plat/regs-serial.h>

#include "common.h"
#include "h1940.h"

static struct map_desc rx3715_iodesc[] __initdata = {
	/* dump ISA space somewhere unused */

	{
		.virtual	= (u32)S3C24XX_VA_ISA_WORD,
		.pfn		= __phys_to_pfn(S3C2410_CS3),
		.length		= SZ_1M,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (u32)S3C24XX_VA_ISA_BYTE,
		.pfn		= __phys_to_pfn(S3C2410_CS3),
		.length		= SZ_1M,
		.type		= MT_DEVICE,
	},
};

static struct s3c2410_uartcfg rx3715_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
		.clk_sel	= S3C2410_UCON_CLKSEL3,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x00,
		.clk_sel	= S3C2410_UCON_CLKSEL3,
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.uart_flags  = UPF_CONS_FLOW,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
		.clk_sel	= S3C2410_UCON_CLKSEL3,
	}
};

/* framebuffer lcd controller information */

static struct s3c2410fb_display rx3715_lcdcfg __initdata = {
	.lcdcon5 =	S3C2410_LCDCON5_INVVLINE |
			S3C2410_LCDCON5_FRM565 |
			S3C2410_LCDCON5_HWSWP,

	.type		= S3C2410_LCDCON1_TFT,
	.width		= 240,
	.height		= 320,

	.pixclock	= 260000,
	.xres		= 240,
	.yres		= 320,
	.bpp		= 16,
	.left_margin	= 36,
	.right_margin	= 36,
	.hsync_len	= 8,
	.upper_margin	= 6,
	.lower_margin	= 7,
	.vsync_len	= 3,
};

static struct s3c2410fb_mach_info rx3715_fb_info __initdata = {

	.displays =	&rx3715_lcdcfg,
	.num_displays =	1,
	.default_display = 0,

	.lpcsel =	0xf82,

	.gpccon =	0xaa955699,
	.gpccon_mask =	0xffc003cc,
	.gpcup =	0x0000ffff,
	.gpcup_mask =	0xffffffff,

	.gpdcon =	0xaa95aaa1,
	.gpdcon_mask =	0xffc0fff0,
	.gpdup =	0x0000faff,
	.gpdup_mask =	0xffffffff,
};

static struct mtd_partition __initdata rx3715_nand_part[] = {
	[0] = {
		.name		= "Whole Flash",
		.offset		= 0,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= MTD_WRITEABLE,
	}
};

static struct s3c2410_nand_set __initdata rx3715_nand_sets[] = {
	[0] = {
		.name		= "Internal",
		.nr_chips	= 1,
		.nr_partitions	= ARRAY_SIZE(rx3715_nand_part),
		.partitions	= rx3715_nand_part,
	},
};

static struct s3c2410_platform_nand __initdata rx3715_nand_info = {
	.tacls		= 25,
	.twrph0		= 50,
	.twrph1		= 15,
	.nr_sets	= ARRAY_SIZE(rx3715_nand_sets),
	.sets		= rx3715_nand_sets,
};

static struct platform_device *rx3715_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_iis,
	&s3c_device_nand,
};

static void __init rx3715_map_io(void)
{
	s3c24xx_init_io(rx3715_iodesc, ARRAY_SIZE(rx3715_iodesc));
	s3c24xx_init_clocks(16934000);
	s3c24xx_init_uarts(rx3715_uartcfgs, ARRAY_SIZE(rx3715_uartcfgs));
}

/* H1940 and RX3715 need to reserve this for suspend */
static void __init rx3715_reserve(void)
{
	memblock_reserve(0x30003000, 0x1000);
	memblock_reserve(0x30081000, 0x1000);
}

static void __init rx3715_init_irq(void)
{
	s3c24xx_init_irq();
}

static void __init rx3715_init_machine(void)
{
#ifdef CONFIG_PM_H1940
	memcpy(phys_to_virt(H1940_SUSPEND_RESUMEAT), h1940_pm_return, 1024);
#endif
	s3c_pm_init();

	s3c_nand_set_platdata(&rx3715_nand_info);
	s3c24xx_fb_set_platdata(&rx3715_fb_info);
	platform_add_devices(rx3715_devices, ARRAY_SIZE(rx3715_devices));
}

MACHINE_START(RX3715, "IPAQ-RX3715")
	/* Maintainer: Ben Dooks <ben-linux@fluff.org> */
	.atag_offset	= 0x100,
	.map_io		= rx3715_map_io,
	.reserve	= rx3715_reserve,
	.init_irq	= rx3715_init_irq,
	.init_machine	= rx3715_init_machine,
	.init_time	= s3c24xx_timer_init,
	.restart	= s3c244x_restart,
MACHINE_END
