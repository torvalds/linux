/* linux/arch/arm/mach-exynos4/mach-origen.c
 *
 * Copyright (c) 2011 Insignal Co., Ltd.
 *		http://www.insignal.co.kr/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/serial_core.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/input.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <plat/exynos4.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/sdhci.h>
#include <plat/iic.h>

#include <mach/map.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define ORIGEN_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define ORIGEN_ULCON_DEFAULT	S3C2410_LCON_CS8

#define ORIGEN_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg origen_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= ORIGEN_UCON_DEFAULT,
		.ulcon		= ORIGEN_ULCON_DEFAULT,
		.ufcon		= ORIGEN_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= ORIGEN_UCON_DEFAULT,
		.ulcon		= ORIGEN_ULCON_DEFAULT,
		.ufcon		= ORIGEN_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= ORIGEN_UCON_DEFAULT,
		.ulcon		= ORIGEN_ULCON_DEFAULT,
		.ufcon		= ORIGEN_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= ORIGEN_UCON_DEFAULT,
		.ulcon		= ORIGEN_ULCON_DEFAULT,
		.ufcon		= ORIGEN_UFCON_DEFAULT,
	},
};

static struct s3c_sdhci_platdata origen_hsmmc2_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= EXYNOS4_GPK2(2),
	.ext_cd_gpio_invert	= 1,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};

static struct platform_device *origen_devices[] __initdata = {
	&s3c_device_hsmmc2,
	&s3c_device_rtc,
	&s3c_device_wdt,
};

static void __init origen_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(origen_uartcfgs, ARRAY_SIZE(origen_uartcfgs));
}

static void __init origen_machine_init(void)
{
	s3c_sdhci2_set_platdata(&origen_hsmmc2_pdata);
	platform_add_devices(origen_devices, ARRAY_SIZE(origen_devices));
}

MACHINE_START(ORIGEN, "ORIGEN")
	/* Maintainer: JeongHyeon Kim <jhkim@insignal.co.kr> */
	.atag_offset	= 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= origen_map_io,
	.init_machine	= origen_machine_init,
	.timer		= &exynos4_timer,
MACHINE_END
