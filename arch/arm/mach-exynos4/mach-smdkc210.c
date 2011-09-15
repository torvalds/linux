/* linux/arch/arm/mach-exynos4/mach-smdkc210.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/serial_core.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/lcd.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/smsc911x.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/pwm_backlight.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <video/platform_lcd.h>

#include <plat/regs-serial.h>
#include <plat/regs-srom.h>
#include <plat/regs-fb-v4.h>
#include <plat/exynos4.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/fb.h>
#include <plat/sdhci.h>
#include <plat/iic.h>
#include <plat/pd.h>
#include <plat/gpio-cfg.h>
#include <plat/backlight.h>

#include <mach/map.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define SMDKC210_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define SMDKC210_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDKC210_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg smdkc210_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDKC210_UCON_DEFAULT,
		.ulcon		= SMDKC210_ULCON_DEFAULT,
		.ufcon		= SMDKC210_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= SMDKC210_UCON_DEFAULT,
		.ulcon		= SMDKC210_ULCON_DEFAULT,
		.ufcon		= SMDKC210_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDKC210_UCON_DEFAULT,
		.ulcon		= SMDKC210_ULCON_DEFAULT,
		.ufcon		= SMDKC210_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDKC210_UCON_DEFAULT,
		.ulcon		= SMDKC210_ULCON_DEFAULT,
		.ufcon		= SMDKC210_UFCON_DEFAULT,
	},
};

static struct s3c_sdhci_platdata smdkc210_hsmmc0_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= EXYNOS4_GPK0(2),
	.ext_cd_gpio_invert	= 1,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
#ifdef CONFIG_EXYNOS4_SDHCI_CH0_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};

static struct s3c_sdhci_platdata smdkc210_hsmmc1_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= EXYNOS4_GPK0(2),
	.ext_cd_gpio_invert	= 1,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};

static struct s3c_sdhci_platdata smdkc210_hsmmc2_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= EXYNOS4_GPK2(2),
	.ext_cd_gpio_invert	= 1,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
#ifdef CONFIG_EXYNOS4_SDHCI_CH2_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};

static struct s3c_sdhci_platdata smdkc210_hsmmc3_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= EXYNOS4_GPK2(2),
	.ext_cd_gpio_invert	= 1,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};

static void lcd_lte480wv_set_power(struct plat_lcd_data *pd,
				   unsigned int power)
{
	if (power) {
#if !defined(CONFIG_BACKLIGHT_PWM)
		gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_HIGH, "GPD0");
		gpio_free(EXYNOS4_GPD0(1));
#endif
		/* fire nRESET on power up */
		gpio_request(EXYNOS4_GPX0(6), "GPX0");

		gpio_direction_output(EXYNOS4_GPX0(6), 1);
		mdelay(100);

		gpio_set_value(EXYNOS4_GPX0(6), 0);
		mdelay(10);

		gpio_set_value(EXYNOS4_GPX0(6), 1);
		mdelay(10);

		gpio_free(EXYNOS4_GPX0(6));
	} else {
#if !defined(CONFIG_BACKLIGHT_PWM)
		gpio_request_one(EXYNOS4_GPD0(1), GPIOF_OUT_INIT_LOW, "GPD0");
		gpio_free(EXYNOS4_GPD0(1));
#endif
	}
}

static struct plat_lcd_data smdkc210_lcd_lte480wv_data = {
	.set_power		= lcd_lte480wv_set_power,
};

static struct platform_device smdkc210_lcd_lte480wv = {
	.name			= "platform-lcd",
	.dev.parent		= &s5p_device_fimd0.dev,
	.dev.platform_data	= &smdkc210_lcd_lte480wv_data,
};

static struct s3c_fb_pd_win smdkc210_fb_win0 = {
	.win_mode = {
		.left_margin	= 13,
		.right_margin	= 8,
		.upper_margin	= 7,
		.lower_margin	= 5,
		.hsync_len	= 3,
		.vsync_len	= 1,
		.xres		= 800,
		.yres		= 480,
	},
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_platdata smdkc210_lcd0_pdata __initdata = {
	.win[0]		= &smdkc210_fb_win0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= exynos4_fimd0_gpio_setup_24bpp,
};

static struct resource smdkc210_smsc911x_resources[] = {
	[0] = {
		.start	= EXYNOS4_PA_SROM_BANK(1),
		.end	= EXYNOS4_PA_SROM_BANK(1) + SZ_64K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_EINT(5),
		.end	= IRQ_EINT(5),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct smsc911x_platform_config smsc9215_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.flags		= SMSC911X_USE_16BIT | SMSC911X_FORCE_INTERNAL_PHY,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.mac		= {0x00, 0x80, 0x00, 0x23, 0x45, 0x67},
};

static struct platform_device smdkc210_smsc911x = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smdkc210_smsc911x_resources),
	.resource	= smdkc210_smsc911x_resources,
	.dev		= {
		.platform_data	= &smsc9215_config,
	},
};

static struct i2c_board_info i2c_devs1[] __initdata = {
	{I2C_BOARD_INFO("wm8994", 0x1a),},
};

static struct platform_device *smdkc210_devices[] __initdata = {
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc3,
	&s3c_device_i2c1,
	&s3c_device_rtc,
	&s3c_device_wdt,
	&exynos4_device_ac97,
	&exynos4_device_i2s0,
	&exynos4_device_pd[PD_MFC],
	&exynos4_device_pd[PD_G3D],
	&exynos4_device_pd[PD_LCD0],
	&exynos4_device_pd[PD_LCD1],
	&exynos4_device_pd[PD_CAM],
	&exynos4_device_pd[PD_TV],
	&exynos4_device_pd[PD_GPS],
	&exynos4_device_sysmmu,
	&samsung_asoc_dma,
	&s5p_device_fimd0,
	&smdkc210_lcd_lte480wv,
	&smdkc210_smsc911x,
};

static void __init smdkc210_smsc911x_init(void)
{
	u32 cs1;

	/* configure nCS1 width to 16 bits */
	cs1 = __raw_readl(S5P_SROM_BW) &
		~(S5P_SROM_BW__CS_MASK << S5P_SROM_BW__NCS1__SHIFT);
	cs1 |= ((1 << S5P_SROM_BW__DATAWIDTH__SHIFT) |
		(1 << S5P_SROM_BW__WAITENABLE__SHIFT) |
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

/* LCD Backlight data */
static struct samsung_bl_gpio_info smdkc210_bl_gpio_info = {
	.no = EXYNOS4_GPD0(1),
	.func = S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data smdkc210_bl_data = {
	.pwm_id = 1,
	.pwm_period_ns  = 1000,
};

static void __init smdkc210_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(smdkc210_uartcfgs, ARRAY_SIZE(smdkc210_uartcfgs));
}

static void __init smdkc210_machine_init(void)
{
	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));

	smdkc210_smsc911x_init();

	s3c_sdhci0_set_platdata(&smdkc210_hsmmc0_pdata);
	s3c_sdhci1_set_platdata(&smdkc210_hsmmc1_pdata);
	s3c_sdhci2_set_platdata(&smdkc210_hsmmc2_pdata);
	s3c_sdhci3_set_platdata(&smdkc210_hsmmc3_pdata);

	samsung_bl_set(&smdkc210_bl_gpio_info, &smdkc210_bl_data);
	s5p_fimd0_set_platdata(&smdkc210_lcd0_pdata);

	platform_add_devices(smdkc210_devices, ARRAY_SIZE(smdkc210_devices));
}

MACHINE_START(SMDKC210, "SMDKC210")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= smdkc210_map_io,
	.init_machine	= smdkc210_machine_init,
	.timer		= &exynos4_timer,
MACHINE_END
