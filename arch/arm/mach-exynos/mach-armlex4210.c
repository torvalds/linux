/* linux/arch/arm/mach-exynos4/mach-armlex4210.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/smsc911x.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/gpio-cfg.h>
#include <plat/regs-serial.h>
#include <plat/regs-srom.h>
#include <plat/sdhci.h>

#include <mach/map.h>

#include "common.h"

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define ARMLEX4210_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define ARMLEX4210_ULCON_DEFAULT	S3C2410_LCON_CS8

#define ARMLEX4210_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg armlex4210_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= ARMLEX4210_UCON_DEFAULT,
		.ulcon		= ARMLEX4210_ULCON_DEFAULT,
		.ufcon		= ARMLEX4210_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= ARMLEX4210_UCON_DEFAULT,
		.ulcon		= ARMLEX4210_ULCON_DEFAULT,
		.ufcon		= ARMLEX4210_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= ARMLEX4210_UCON_DEFAULT,
		.ulcon		= ARMLEX4210_ULCON_DEFAULT,
		.ufcon		= ARMLEX4210_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= ARMLEX4210_UCON_DEFAULT,
		.ulcon		= ARMLEX4210_ULCON_DEFAULT,
		.ufcon		= ARMLEX4210_UFCON_DEFAULT,
	},
};

static struct s3c_sdhci_platdata armlex4210_hsmmc0_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_PERMANENT,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
#ifdef CONFIG_EXYNOS4_SDHCI_CH0_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};

static struct s3c_sdhci_platdata armlex4210_hsmmc2_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= EXYNOS4_GPX2(5),
	.ext_cd_gpio_invert	= 1,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
	.max_width		= 4,
};

static struct s3c_sdhci_platdata armlex4210_hsmmc3_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_PERMANENT,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
	.max_width		= 4,
};

static void __init armlex4210_sdhci_init(void)
{
	s3c_sdhci0_set_platdata(&armlex4210_hsmmc0_pdata);
	s3c_sdhci2_set_platdata(&armlex4210_hsmmc2_pdata);
	s3c_sdhci3_set_platdata(&armlex4210_hsmmc3_pdata);
}

static void __init armlex4210_wlan_init(void)
{
	/* enable */
	s3c_gpio_cfgpin(EXYNOS4_GPX2(0), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(EXYNOS4_GPX2(0), S3C_GPIO_PULL_UP);

	/* reset */
	s3c_gpio_cfgpin(EXYNOS4_GPX1(6), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(EXYNOS4_GPX1(6), S3C_GPIO_PULL_UP);

	/* wakeup */
	s3c_gpio_cfgpin(EXYNOS4_GPX1(5), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(EXYNOS4_GPX1(5), S3C_GPIO_PULL_UP);
}

static struct resource armlex4210_smsc911x_resources[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_SROM_BANK(3), SZ_64K),
	[1] = DEFINE_RES_NAMED(IRQ_EINT(27), 1, NULL, IORESOURCE_IRQ \
					| IRQF_TRIGGER_HIGH),
};

static struct smsc911x_platform_config smsc9215_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_HIGH,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.flags		= SMSC911X_USE_16BIT | SMSC911X_FORCE_INTERNAL_PHY,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.mac		= {0x00, 0x80, 0x00, 0x23, 0x45, 0x67},
};

static struct platform_device armlex4210_smsc911x = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(armlex4210_smsc911x_resources),
	.resource	= armlex4210_smsc911x_resources,
	.dev		= {
		.platform_data	= &smsc9215_config,
	},
};

static struct platform_device *armlex4210_devices[] __initdata = {
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc3,
	&s3c_device_rtc,
	&s3c_device_wdt,
	&exynos4_device_sysmmu,
	&samsung_asoc_dma,
	&armlex4210_smsc911x,
	&exynos4_device_ahci,
};

static void __init armlex4210_smsc911x_init(void)
{
	u32 cs1;

	/* configure nCS1 width to 16 bits */
	cs1 = __raw_readl(S5P_SROM_BW) &
		~(S5P_SROM_BW__CS_MASK << S5P_SROM_BW__NCS1__SHIFT);
	cs1 |= ((1 << S5P_SROM_BW__DATAWIDTH__SHIFT) |
		(0 << S5P_SROM_BW__WAITENABLE__SHIFT) |
		(1 << S5P_SROM_BW__ADDRMODE__SHIFT) |
		(1 << S5P_SROM_BW__BYTEENABLE__SHIFT)) <<
		S5P_SROM_BW__NCS1__SHIFT;
	__raw_writel(cs1, S5P_SROM_BW);

	/* set timing for nCS1 suitable for ethernet chip */
	__raw_writel((0x1 << S5P_SROM_BCX__PMC__SHIFT) |
		     (0x9 << S5P_SROM_BCX__TACP__SHIFT) |
		     (0xc << S5P_SROM_BCX__TCAH__SHIFT) |
		     (0x1 << S5P_SROM_BCX__TCOH__SHIFT) |
		     (0x6 << S5P_SROM_BCX__TACC__SHIFT) |
		     (0x1 << S5P_SROM_BCX__TCOS__SHIFT) |
		     (0x1 << S5P_SROM_BCX__TACS__SHIFT), S5P_SROM_BC1);
}

static void __init armlex4210_map_io(void)
{
	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(armlex4210_uartcfgs,
			   ARRAY_SIZE(armlex4210_uartcfgs));
}

static void __init armlex4210_machine_init(void)
{
	armlex4210_smsc911x_init();

	armlex4210_sdhci_init();

	armlex4210_wlan_init();

	platform_add_devices(armlex4210_devices,
			     ARRAY_SIZE(armlex4210_devices));
}

MACHINE_START(ARMLEX4210, "ARMLEX4210")
	/* Maintainer: Alim Akhtar <alim.akhtar@samsung.com> */
	.atag_offset	= 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= armlex4210_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= armlex4210_machine_init,
	.timer		= &exynos4_timer,
	.restart	= exynos4_restart,
MACHINE_END
