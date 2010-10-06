/* linux/arch/arm/mach-s5pv310/mach-smdkv310.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/serial_core.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>
#include <asm/hardware/cache-l2x0.h>

#include <plat/regs-serial.h>
#include <plat/s5pv310.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/sdhci.h>

#include <mach/map.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define SMDKV310_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define SMDKV310_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDKV310_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg smdkv310_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDKV310_UCON_DEFAULT,
		.ulcon		= SMDKV310_ULCON_DEFAULT,
		.ufcon		= SMDKV310_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= SMDKV310_UCON_DEFAULT,
		.ulcon		= SMDKV310_ULCON_DEFAULT,
		.ufcon		= SMDKV310_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDKV310_UCON_DEFAULT,
		.ulcon		= SMDKV310_ULCON_DEFAULT,
		.ufcon		= SMDKV310_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDKV310_UCON_DEFAULT,
		.ulcon		= SMDKV310_ULCON_DEFAULT,
		.ufcon		= SMDKV310_UFCON_DEFAULT,
	},
};

static struct s3c_sdhci_platdata smdkv310_hsmmc0_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= S5PV310_GPK0(2),
	.ext_cd_gpio_invert	= 1,
#ifdef CONFIG_S5PV310_SDHCI_CH0_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};

static struct s3c_sdhci_platdata smdkv310_hsmmc1_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= S5PV310_GPK0(2),
	.ext_cd_gpio_invert	= 1,
};

static struct s3c_sdhci_platdata smdkv310_hsmmc2_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= S5PV310_GPK2(2),
	.ext_cd_gpio_invert	= 1,
#ifdef CONFIG_S5PV310_SDHCI_CH2_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};

static struct s3c_sdhci_platdata smdkv310_hsmmc3_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= S5PV310_GPK2(2),
	.ext_cd_gpio_invert	= 1,
};

static struct platform_device *smdkv310_devices[] __initdata = {
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc3,
	&s3c_device_rtc,
	&s3c_device_wdt,
};

static void __init smdkv310_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(smdkv310_uartcfgs, ARRAY_SIZE(smdkv310_uartcfgs));
}

static void __init smdkv310_machine_init(void)
{
	s3c_sdhci0_set_platdata(&smdkv310_hsmmc0_pdata);
	s3c_sdhci1_set_platdata(&smdkv310_hsmmc1_pdata);
	s3c_sdhci2_set_platdata(&smdkv310_hsmmc2_pdata);
	s3c_sdhci3_set_platdata(&smdkv310_hsmmc3_pdata);

#ifdef CONFIG_CACHE_L2X0
	l2x0_init(S5P_VA_L2CC, 1 << 28, 0xffffffff);
#endif

	platform_add_devices(smdkv310_devices, ARRAY_SIZE(smdkv310_devices));
}

MACHINE_START(SMDKV310, "SMDKV310")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	/* Maintainer: Changhwan Youn <chaos.youn@samsung.com> */
	.phys_io	= S3C_PA_UART & 0xfff00000,
	.io_pg_offst	= (((u32)S3C_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= s5pv310_init_irq,
	.map_io		= smdkv310_map_io,
	.init_machine	= smdkv310_machine_init,
	.timer		= &s5pv310_timer,
MACHINE_END
