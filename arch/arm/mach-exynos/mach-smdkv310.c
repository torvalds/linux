/* linux/arch/arm/mach-exynos4/mach-smdkv310.c
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
#include <linux/input.h>
#include <linux/pwm_backlight.h>

#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>

#include <video/platform_lcd.h>
#include <plat/regs-serial.h>
#include <plat/regs-srom.h>
#include <plat/regs-fb-v4.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/fb.h>
#include <plat/keypad.h>
#include <plat/sdhci.h>
#include <plat/iic.h>
#include <plat/pd.h>
#include <plat/gpio-cfg.h>
#include <plat/backlight.h>
#include <plat/mfc.h>
#include <plat/ehci.h>
#include <plat/clock.h>

#include <mach/map.h>
#include <mach/ohci.h>

#include <drm/exynos_drm.h>
#include "common.h"

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
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
#ifdef CONFIG_EXYNOS4_SDHCI_CH0_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};

static struct s3c_sdhci_platdata smdkv310_hsmmc1_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= EXYNOS4_GPK0(2),
	.ext_cd_gpio_invert	= 1,
};

static struct s3c_sdhci_platdata smdkv310_hsmmc2_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
#ifdef CONFIG_EXYNOS4_SDHCI_CH2_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};

static struct s3c_sdhci_platdata smdkv310_hsmmc3_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= EXYNOS4_GPK2(2),
	.ext_cd_gpio_invert	= 1,
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
		gpio_request_one(EXYNOS4_GPX0(6), GPIOF_OUT_INIT_HIGH, "GPX0");
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

static struct plat_lcd_data smdkv310_lcd_lte480wv_data = {
	.set_power		= lcd_lte480wv_set_power,
};

static struct platform_device smdkv310_lcd_lte480wv = {
	.name			= "platform-lcd",
	.dev.parent		= &s5p_device_fimd0.dev,
	.dev.platform_data	= &smdkv310_lcd_lte480wv_data,
};

#ifdef CONFIG_DRM_EXYNOS
static struct exynos_drm_fimd_pdata drm_fimd_pdata = {
	.panel	= {
		.timing	= {
			.left_margin	= 13,
			.right_margin	= 8,
			.upper_margin	= 7,
			.lower_margin	= 5,
			.hsync_len	= 3,
			.vsync_len	= 1,
			.xres		= 800,
			.yres		= 480,
		},
	},
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.default_win	= 0,
	.bpp		= 32,
};
#else
static struct s3c_fb_pd_win smdkv310_fb_win0 = {
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

static struct s3c_fb_platdata smdkv310_lcd0_pdata __initdata = {
	.win[0]		= &smdkv310_fb_win0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= exynos4_fimd0_gpio_setup_24bpp,
};
#endif

static struct resource smdkv310_smsc911x_resources[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_SROM_BANK(1), SZ_64K),
	[1] = DEFINE_RES_NAMED(IRQ_EINT(5), 1, NULL, IORESOURCE_IRQ \
						| IRQF_TRIGGER_LOW),
};

static struct smsc911x_platform_config smsc9215_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.flags		= SMSC911X_USE_16BIT | SMSC911X_FORCE_INTERNAL_PHY,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.mac		= {0x00, 0x80, 0x00, 0x23, 0x45, 0x67},
};

static struct platform_device smdkv310_smsc911x = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smdkv310_smsc911x_resources),
	.resource	= smdkv310_smsc911x_resources,
	.dev		= {
		.platform_data	= &smsc9215_config,
	},
};

static uint32_t smdkv310_keymap[] __initdata = {
	/* KEY(row, col, keycode) */
	KEY(0, 3, KEY_1), KEY(0, 4, KEY_2), KEY(0, 5, KEY_3),
	KEY(0, 6, KEY_4), KEY(0, 7, KEY_5),
	KEY(1, 3, KEY_A), KEY(1, 4, KEY_B), KEY(1, 5, KEY_C),
	KEY(1, 6, KEY_D), KEY(1, 7, KEY_E)
};

static struct matrix_keymap_data smdkv310_keymap_data __initdata = {
	.keymap		= smdkv310_keymap,
	.keymap_size	= ARRAY_SIZE(smdkv310_keymap),
};

static struct samsung_keypad_platdata smdkv310_keypad_data __initdata = {
	.keymap_data	= &smdkv310_keymap_data,
	.rows		= 2,
	.cols		= 8,
};

static struct i2c_board_info i2c_devs1[] __initdata = {
	{I2C_BOARD_INFO("wm8994", 0x1a),},
};

/* USB EHCI */
static struct s5p_ehci_platdata smdkv310_ehci_pdata;

static void __init smdkv310_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &smdkv310_ehci_pdata;

	s5p_ehci_set_platdata(pdata);
}

/* USB OHCI */
static struct exynos4_ohci_platdata smdkv310_ohci_pdata;

static void __init smdkv310_ohci_init(void)
{
	struct exynos4_ohci_platdata *pdata = &smdkv310_ohci_pdata;

	exynos4_ohci_set_platdata(pdata);
}

static struct platform_device *smdkv310_devices[] __initdata = {
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc3,
	&s3c_device_i2c1,
	&s5p_device_i2c_hdmiphy,
	&s3c_device_rtc,
	&s3c_device_wdt,
	&s5p_device_ehci,
	&s5p_device_fimc0,
	&s5p_device_fimc1,
	&s5p_device_fimc2,
	&s5p_device_fimc3,
	&s5p_device_fimc_md,
	&s5p_device_g2d,
	&s5p_device_jpeg,
#ifdef CONFIG_DRM_EXYNOS
	&exynos_device_drm,
#endif
	&exynos4_device_ac97,
	&exynos4_device_i2s0,
	&exynos4_device_ohci,
	&samsung_device_keypad,
	&s5p_device_mfc,
	&s5p_device_mfc_l,
	&s5p_device_mfc_r,
	&exynos4_device_spdif,
	&samsung_asoc_dma,
	&samsung_asoc_idma,
	&s5p_device_fimd0,
	&smdkv310_lcd_lte480wv,
	&smdkv310_smsc911x,
	&exynos4_device_ahci,
	&s5p_device_hdmi,
	&s5p_device_mixer,
};

static void __init smdkv310_smsc911x_init(void)
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
static struct samsung_bl_gpio_info smdkv310_bl_gpio_info = {
	.no = EXYNOS4_GPD0(1),
	.func = S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data smdkv310_bl_data = {
	.pwm_id = 1,
	.pwm_period_ns  = 1000,
};

static void s5p_tv_setup(void)
{
	/* direct HPD to HDMI chip */
	WARN_ON(gpio_request_one(EXYNOS4_GPX3(7), GPIOF_IN, "hpd-plug"));
	s3c_gpio_cfgpin(EXYNOS4_GPX3(7), S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(EXYNOS4_GPX3(7), S3C_GPIO_PULL_NONE);
}

static void __init smdkv310_map_io(void)
{
	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(smdkv310_uartcfgs, ARRAY_SIZE(smdkv310_uartcfgs));
}

static void __init smdkv310_reserve(void)
{
	s5p_mfc_reserve_mem(0x43000000, 8 << 20, 0x51000000, 8 << 20);
}

static void __init smdkv310_machine_init(void)
{
	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));

	smdkv310_smsc911x_init();

	s3c_sdhci0_set_platdata(&smdkv310_hsmmc0_pdata);
	s3c_sdhci1_set_platdata(&smdkv310_hsmmc1_pdata);
	s3c_sdhci2_set_platdata(&smdkv310_hsmmc2_pdata);
	s3c_sdhci3_set_platdata(&smdkv310_hsmmc3_pdata);

	s5p_tv_setup();
	s5p_i2c_hdmiphy_set_platdata(NULL);

	samsung_keypad_set_platdata(&smdkv310_keypad_data);

	samsung_bl_set(&smdkv310_bl_gpio_info, &smdkv310_bl_data);
#ifdef CONFIG_DRM_EXYNOS
	s5p_device_fimd0.dev.platform_data = &drm_fimd_pdata;
	exynos4_fimd0_gpio_setup_24bpp();
#else
	s5p_fimd0_set_platdata(&smdkv310_lcd0_pdata);
#endif

	smdkv310_ehci_init();
	smdkv310_ohci_init();
	clk_xusbxti.rate = 24000000;

	platform_add_devices(smdkv310_devices, ARRAY_SIZE(smdkv310_devices));
}

MACHINE_START(SMDKV310, "SMDKV310")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	/* Maintainer: Changhwan Youn <chaos.youn@samsung.com> */
	.atag_offset	= 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= smdkv310_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= smdkv310_machine_init,
	.timer		= &exynos4_timer,
	.reserve	= &smdkv310_reserve,
	.restart	= exynos4_restart,
MACHINE_END

MACHINE_START(SMDKC210, "SMDKC210")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.atag_offset	= 0x100,
	.init_irq	= exynos4_init_irq,
	.map_io		= smdkv310_map_io,
	.handle_irq	= gic_handle_irq,
	.init_machine	= smdkv310_machine_init,
	.init_late	= exynos_init_late,
	.timer		= &exynos4_timer,
	.restart	= exynos4_restart,
MACHINE_END
