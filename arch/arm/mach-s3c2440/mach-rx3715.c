/* linux/arch/arm/mach-s3c2440/mach-rx3715.c
 *
 * Copyright (c) 2003,2004 Simtec Electronics
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
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/sysdev.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/plat-s3c/regs-serial.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-lcd.h>

#include <asm/arch/h1940.h>
#include <asm/plat-s3c/nand.h>
#include <asm/arch/fb.h>

#include <asm/plat-s3c24xx/clock.h>
#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c24xx/cpu.h>
#include <asm/plat-s3c24xx/pm.h>

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


static struct s3c24xx_uart_clksrc rx3715_serial_clocks[] = {
	[0] = {
		.name		= "fclk",
		.divisor	= 0,
		.min_baud	= 0,
		.max_baud	= 0,
	}
};

static struct s3c2410_uartcfg rx3715_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
		.clocks	     = rx3715_serial_clocks,
		.clocks_size = ARRAY_SIZE(rx3715_serial_clocks),
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x00,
		.clocks	     = rx3715_serial_clocks,
		.clocks_size = ARRAY_SIZE(rx3715_serial_clocks),
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.uart_flags  = UPF_CONS_FLOW,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
		.clocks	     = rx3715_serial_clocks,
		.clocks_size = ARRAY_SIZE(rx3715_serial_clocks),
	}
};

/* framebuffer lcd controller information */

static struct s3c2410fb_display rx3715_lcdcfg __initdata = {
	.regs	= {
		.lcdcon1 =	S3C2410_LCDCON1_TFT16BPP | \
				S3C2410_LCDCON1_TFT | \
				S3C2410_LCDCON1_CLKVAL(0x0C),

		.lcdcon2 =	S3C2410_LCDCON2_VBPD(5) | \
				S3C2410_LCDCON2_LINEVAL(319) | \
				S3C2410_LCDCON2_VFPD(6) | \
				S3C2410_LCDCON2_VSPW(2),

		.lcdcon3 =	S3C2410_LCDCON3_HBPD(35) | \
				S3C2410_LCDCON3_HOZVAL(239) | \
				S3C2410_LCDCON3_HFPD(35),

		.lcdcon4 =	S3C2410_LCDCON4_MVAL(0) | \
				S3C2410_LCDCON4_HSPW(7),

		.lcdcon5 =	S3C2410_LCDCON5_INVVLINE |
				S3C2410_LCDCON5_FRM565 |
				S3C2410_LCDCON5_HWSWP,
	},

	.type		= S3C2410_LCDCON1_TFT,
	.width		= 240,
	.height		= 320,

	.xres		= 240,
	.yres		= 320,
	.bpp		= 16,
	.left_margin	= 36,
	.right_margin	= 36,
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

	.fixed_syncs =	1,
};

static struct mtd_partition rx3715_nand_part[] = {
	[0] = {
		.name		= "Whole Flash",
		.offset		= 0,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= MTD_WRITEABLE,
	}
};

static struct s3c2410_nand_set rx3715_nand_sets[] = {
	[0] = {
		.name		= "Internal",
		.nr_chips	= 1,
		.nr_partitions	= ARRAY_SIZE(rx3715_nand_part),
		.partitions	= rx3715_nand_part,
	},
};

static struct s3c2410_platform_nand rx3715_nand_info = {
	.tacls		= 25,
	.twrph0		= 50,
	.twrph1		= 15,
	.nr_sets	= ARRAY_SIZE(rx3715_nand_sets),
	.sets		= rx3715_nand_sets,
};

static struct platform_device *rx3715_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
	&s3c_device_nand,
};

static void __init rx3715_map_io(void)
{
	s3c_device_nand.dev.platform_data = &rx3715_nand_info;

	s3c24xx_init_io(rx3715_iodesc, ARRAY_SIZE(rx3715_iodesc));
	s3c24xx_init_clocks(16934000);
	s3c24xx_init_uarts(rx3715_uartcfgs, ARRAY_SIZE(rx3715_uartcfgs));
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
	s3c2410_pm_init();

	s3c24xx_fb_set_platdata(&rx3715_fb_info);
	platform_add_devices(rx3715_devices, ARRAY_SIZE(rx3715_devices));
}

MACHINE_START(RX3715, "IPAQ-RX3715")
	/* Maintainer: Ben Dooks <ben@fluff.org> */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= rx3715_map_io,
	.init_irq	= rx3715_init_irq,
	.init_machine	= rx3715_init_machine,
	.timer		= &s3c24xx_timer,
MACHINE_END
