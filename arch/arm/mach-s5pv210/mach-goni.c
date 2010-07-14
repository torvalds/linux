/* linux/arch/arm/mach-s5pv210/mach-goni.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/fb.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/regs-fb.h>

#include <plat/regs-serial.h>
#include <plat/s5pv210.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/fb.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define S5PV210_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define S5PV210_ULCON_DEFAULT	S3C2410_LCON_CS8

#define S5PV210_UFCON_DEFAULT	S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg goni_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= S5PV210_UCON_DEFAULT,
		.ulcon		= S5PV210_ULCON_DEFAULT,
		.ufcon		= S5PV210_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG256 | S5PV210_UFCON_RXTRIG256,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= S5PV210_UCON_DEFAULT,
		.ulcon		= S5PV210_ULCON_DEFAULT,
		.ufcon		= S5PV210_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG64 | S5PV210_UFCON_RXTRIG64,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= S5PV210_UCON_DEFAULT,
		.ulcon		= S5PV210_ULCON_DEFAULT,
		.ufcon		= S5PV210_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG16 | S5PV210_UFCON_RXTRIG16,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= S5PV210_UCON_DEFAULT,
		.ulcon		= S5PV210_ULCON_DEFAULT,
		.ufcon		= S5PV210_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG16 | S5PV210_UFCON_RXTRIG16,
	},
};

/* Frame Buffer */
static struct s3c_fb_pd_win goni_fb_win0 = {
	.win_mode = {
		.pixclock = 1000000000000ULL / ((16+16+2+480)*(28+3+2+800)*55),
		.left_margin	= 16,
		.right_margin	= 16,
		.upper_margin	= 3,
		.lower_margin	= 28,
		.hsync_len	= 2,
		.vsync_len	= 2,
		.xres		= 480,
		.yres		= 800,
		.refresh	= 55,
	},
	.max_bpp	= 32,
	.default_bpp	= 16,
};

static struct s3c_fb_platdata goni_lcd_pdata __initdata = {
	.win[0]		= &goni_fb_win0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB |
			  VIDCON0_CLKSEL_LCD,
	.vidcon1	= VIDCON1_INV_VCLK | VIDCON1_INV_VDEN
			  | VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= s5pv210_fb_gpio_setup_24bpp,
};

static struct platform_device *goni_devices[] __initdata = {
	&s3c_device_fb,
	&s5pc110_device_onenand,
};

static void __init goni_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(goni_uartcfgs, ARRAY_SIZE(goni_uartcfgs));
}

static void __init goni_machine_init(void)
{
	/* FB */
	s3c_fb_set_platdata(&goni_lcd_pdata);

	platform_add_devices(goni_devices, ARRAY_SIZE(goni_devices));
}

MACHINE_START(GONI, "GONI")
	/* Maintainers: Kyungmin Park <kyungmin.park@samsung.com> */
	.phys_io	= S3C_PA_UART & 0xfff00000,
	.io_pg_offst	= (((u32)S3C_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= s5pv210_init_irq,
	.map_io		= goni_map_io,
	.init_machine	= goni_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
