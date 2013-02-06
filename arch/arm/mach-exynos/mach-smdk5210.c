/* linux/arch/arm/mach-exynos/mach-smdk5210.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/fb.h>
#include <linux/lcd.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/delay.h>
#include <linux/pwm_backlight.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/cma.h>
#include <linux/memblock.h>
#include <linux/mmc/host.h>
#include <linux/smsc911x.h>
#include <linux/clk.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <plat/gpio-cfg.h>
#include <plat/regs-serial.h>
#include <plat/regs-fb-v4.h>
#include <plat/exynos5.h>
#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/fb.h>
#include <plat/fb-s5p.h>
#include <plat/fb-core.h>
#include <plat/regs-fb-v4.h>
#include <plat/backlight.h>
#include <plat/dp.h>
#include <plat/iic.h>
#include <plat/tv-core.h>
#if defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
#include <plat/s5p-mfc.h>
#endif
#include <plat/sdhci.h>
#include <plat/regs-srom.h>
#include <plat/ehci.h>
#include <plat/udc-ss.h>

#include <mach/map.h>
#include <mach/exynos-ion.h>
#ifdef CONFIG_EXYNOS4_DEV_DWMCI
#include <mach/dwmci.h>
#endif

#include <video/platform_lcd.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define SMDK5210_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define SMDK5210_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDK5210_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg smdk5210_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDK5210_UCON_DEFAULT,
		.ulcon		= SMDK5210_ULCON_DEFAULT,
		.ufcon		= SMDK5210_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= SMDK5210_UCON_DEFAULT,
		.ulcon		= SMDK5210_ULCON_DEFAULT,
		.ufcon		= SMDK5210_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDK5210_UCON_DEFAULT,
		.ulcon		= SMDK5210_ULCON_DEFAULT,
		.ufcon		= SMDK5210_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDK5210_UCON_DEFAULT,
		.ulcon		= SMDK5210_ULCON_DEFAULT,
		.ufcon		= SMDK5210_UFCON_DEFAULT,
	},
};

static struct resource smdk5210_smsc911x_resources[] = {
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

static struct platform_device smdk5210_smsc911x = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smdk5210_smsc911x_resources),
	.resource	= smdk5210_smsc911x_resources,
	.dev		= {
		.platform_data	= &smsc9215_config,
	},
};

#ifdef CONFIG_EXYNOS_MEDIA_DEVICE
struct platform_device exynos_device_md0 = {
	.name = "exynos-mdev",
	.id = 0,
};

struct platform_device exynos_device_md1 = {
	.name = "exynos-mdev",
	.id = 1,
};
#endif

#ifdef CONFIG_FB_S3C
#if defined(CONFIG_LCD_LMS501KF03)
static int lcd_power_on(struct lcd_device *ld, int enable)
{
	return 1;
}

static int reset_lcd(struct lcd_device *ld)
{
	int err = 0;

	err = gpio_request_one(EXYNOS5_GPX0(6), GPIOF_OUT_INIT_HIGH, "GPX0");
	if (err) {
		printk(KERN_ERR "failed to request GPX0 for "
				"lcd reset control\n");
		return err;
	}
	gpio_set_value(EXYNOS5_GPX0(6), 0);
	mdelay(1);

	gpio_set_value(EXYNOS5_GPX0(6), 1);

	gpio_free(EXYNOS5_GPX0(6));

#ifndef CONFIG_BACKLIGHT_PWM
	gpio_request_one(EXYNOS5_GPB2(0), GPIOF_OUT_INIT_HIGH, "GPB2");
	gpio_free(EXYNOS5_GPB2(0));
#endif

	return 1;
}

static struct lcd_platform_data lms501kf03_platform_data = {
	.reset			= reset_lcd,
	.power_on		= lcd_power_on,
	.lcd_enabled		= 0,
	.reset_delay		= 100,	/* 100ms */
};

#define	LCD_BUS_NUM	3
#define	DISPLAY_CS	EXYNOS5_GPA2(5)		/*Chip select */
#define	DISPLAY_CLK	EXYNOS5_GPA2(4)		/* SPI clock */
#define	DISPLAY_SI	EXYNOS5_GPA2(7)		/* SPI MOSI */

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias		= "lms501kf03",
		.platform_data		= (void *)&lms501kf03_platform_data,
		.max_speed_hz		= 1200000,
		.bus_num		= LCD_BUS_NUM,
		.chip_select		= 0,
		.mode			= SPI_MODE_3,
		.controller_data	= (void *)DISPLAY_CS,
	}
};

static struct spi_gpio_platform_data lms501kf03_spi_gpio_data = {
	.sck	= DISPLAY_CLK,
	.mosi	= DISPLAY_SI,
	.miso	= -1,
	.num_chipselect = 1,
};

static struct platform_device s3c_device_spi_gpio = {
	.name	= "spi_gpio",
	.id	= LCD_BUS_NUM,
	.dev	= {
		.parent		= &s5p_device_fimd1.dev,
		.platform_data	= &lms501kf03_spi_gpio_data,
	},
};

static struct s3c_fb_pd_win smdk5210_fb_win0 = {
	.win_mode = {
		.left_margin	= 8,	/* HBPD */
		.right_margin	= 8,	/* HFPD */
		.upper_margin	= 6,	/* VBPD */
		.lower_margin	= 6,	/* VFPD */
		.hsync_len	= 6,	/* HSPW */
		.vsync_len	= 4,	/* VSPW */
		.xres		= 480,
		.yres		= 800,
	},
	.virtual_x		= 480,
	.virtual_y		= 1600,
	.width			= 66,
	.height			= 109,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk5210_fb_win1 = {
	.win_mode = {
		.left_margin	= 8,	/* HBPD */
		.right_margin	= 8,	/* HFPD */
		.upper_margin	= 6,	/* VBPD */
		.lower_margin	= 6,	/* VFPD */
		.hsync_len	= 6,	/* HSPW */
		.vsync_len	= 4,	/* VSPW */
		.xres		= 480,
		.yres		= 800,
	},
	.virtual_x		= 480,
	.virtual_y		= 1600,
	.width			= 66,
	.height			= 109,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk5210_fb_win2 = {
	.win_mode = {
		.left_margin	= 8,	/* HBPD */
		.right_margin	= 8,	/* HFPD */
		.upper_margin	= 6,	/* VBPD */
		.lower_margin	= 6,	/* VFPD */
		.hsync_len	= 6,	/* HSPW */
		.vsync_len	= 4,	/* VSPW */
		.xres		= 480,
		.yres		= 800,
	},
	.virtual_x		= 480,
	.virtual_y		= 1600,
	.width			= 66,
	.height			= 109,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

#elif defined(CONFIG_LCD_WA101S)
static void lcd_wa101s_set_power(struct plat_lcd_data *pd,
				   unsigned int power)
{
	if (power) {
#ifndef CONFIG_BACKLIGHT_PWM
		gpio_request_one(EXYNOS5_GPB2(0), GPIOF_OUT_INIT_HIGH, "GPB2");
		gpio_free(EXYNOS5_GPB2(0));
#endif
		/* fire nRESET on power up */
		gpio_request_one(EXYNOS5_GPX0(6), GPIOF_OUT_INIT_HIGH, "GPX0");
		mdelay(100);

		gpio_set_value(EXYNOS5_GPX0(6), 0);
		mdelay(10);

		gpio_set_value(EXYNOS5_GPX0(6), 1);
		mdelay(10);

		gpio_free(EXYNOS5_GPX0(6));
	} else {
#ifndef CONFIG_BACKLIGHT_PWM
		gpio_request_one(EXYNOS5_GPB2(0), GPIOF_OUT_INIT_LOW, "GPB2");
		gpio_free(EXYNOS5_GPB2(0));
#endif
	}
}

static struct plat_lcd_data smdk5210_lcd_wa101s_data = {
	.set_power		= lcd_wa101s_set_power,
};

static struct platform_device smdk5210_lcd_wa101s = {
	.name			= "platform-lcd",
	.dev.parent		= &s5p_device_fimd1.dev,
	.dev.platform_data      = &smdk5210_lcd_wa101s_data,
};

static struct s3c_fb_pd_win smdk5210_fb_win0 = {
	.win_mode = {
		.left_margin	= 80,
		.right_margin	= 48,
		.upper_margin	= 14,
		.lower_margin	= 3,
		.hsync_len	= 32,
		.vsync_len	= 5,
		.xres		= 1360, /* real size : 1366 */
		.yres		= 768,
	},
	.virtual_x		= 1360, /* real size : 1366 */
	.virtual_y		= 768 * 2,
	.width			= 223,
	.height			= 125,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk5210_fb_win1 = {
	.win_mode = {
		.left_margin	= 80,
		.right_margin	= 48,
		.upper_margin	= 14,
		.lower_margin	= 3,
		.hsync_len	= 32,
		.vsync_len	= 5,
		.xres		= 1360, /* real size : 1366 */
		.yres		= 768,
	},
	.virtual_x		= 1360, /* real size : 1366 */
	.virtual_y		= 768 * 2,
	.width			= 223,
	.height			= 125,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk5210_fb_win2 = {
	.win_mode = {
		.left_margin	= 80,
		.right_margin	= 48,
		.upper_margin	= 14,
		.lower_margin	= 3,
		.hsync_len	= 32,
		.vsync_len	= 5,
		.xres		= 1360, /* real size : 1366 */
		.yres		= 768,
	},
	.virtual_x		= 1360, /* real size : 1366 */
	.virtual_y		= 768 * 2,
	.width			= 223,
	.height			= 125,
	.max_bpp		= 32,
	.default_bpp		= 24,
};
#elif defined(CONFIG_S5P_DP)
static struct s3c_fb_pd_win smdk5210_fb_win0 = {
	.win_mode = {
		.refresh	= 39,
		.left_margin	= 172,
		.right_margin	= 60,
		.upper_margin	= 25,
		.lower_margin	= 10,
		.hsync_len	= 80,
		.vsync_len	= 10,
		.xres		= 1920,
		.yres		= 1080,
	},
	.virtual_x		= 1920,
	.virtual_y		= 1080 * 2,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk5210_fb_win1 = {
	.win_mode = {
		.refresh	= 39,
		.left_margin	= 172,
		.right_margin	= 60,
		.upper_margin	= 25,
		.lower_margin	= 10,
		.hsync_len	= 80,
		.vsync_len	= 10,
		.xres		= 1920,
		.yres		= 1080,
	},
	.virtual_x		= 1920,
	.virtual_y		= 1080 * 2,
	.max_bpp		= 32,
	.default_bpp		= 24,
};

static struct s3c_fb_pd_win smdk5210_fb_win2 = {
	.win_mode = {
		.refresh	= 39,
		.left_margin	= 172,
		.right_margin	= 60,
		.upper_margin	= 25,
		.lower_margin	= 10,
		.hsync_len	= 80,
		.vsync_len	= 10,
		.xres		= 1920,
		.yres		= 1080,
	},
	.virtual_x		= 1920,
	.virtual_y		= 1080 * 2,
	.max_bpp		= 32,
	.default_bpp		= 24,
};
#endif

static void exynos_fimd_gpio_setup_24bpp(void)
{
	unsigned int reg = 0;

#if defined(CONFIG_LCD_WA101S)
	exynos4_fimd_cfg_gpios(EXYNOS5210_GPJ0(0), 5, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	exynos4_fimd_cfg_gpios(EXYNOS5210_GPJ1(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	exynos4_fimd_cfg_gpios(EXYNOS5210_GPJ2(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	exynos4_fimd_cfg_gpios(EXYNOS5210_GPJ3(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	exynos4_fimd_cfg_gpios(EXYNOS5210_GPJ4(0), 2, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
#elif defined(CONFIG_LCD_LMS501KF03)
	exynos4_fimd_cfg_gpios(EXYNOS5210_GPJ0(0), 5, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV4);
	exynos4_fimd_cfg_gpios(EXYNOS5210_GPJ1(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	exynos4_fimd_cfg_gpios(EXYNOS5210_GPJ2(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	exynos4_fimd_cfg_gpios(EXYNOS5210_GPJ3(0), 8, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
	exynos4_fimd_cfg_gpios(EXYNOS5210_GPJ4(0), 2, S3C_GPIO_SFN(2), S5P_GPIO_DRVSTR_LV1);
#endif

#if defined(CONFIG_S5P_DP)
	/* Set Hotplug detect for DP */
	s3c_gpio_cfgpin(EXYNOS5_GPX0(7), S3C_GPIO_SFN(3));
#endif

	/*
	 * Set DISP1BLK_CFG register for Display path selection
	 * DISP1_BLK output source selection : DISP1BLK_CFG[28:27]
	 *---------------------
	 *  10 | From DISP0_BLK
	 *  01 | From DISP1_BLK : selected
	 *
	 * FIMD of DISP1_BLK Bypass selection : DISP1BLK_CFG[15]
	 * ---------------------
	 *  0 | MIE/MDNIE
	 *  1 | FIMD : selected
	 */
	  reg = __raw_readl(S3C_VA_SYS + 0x0214);
	reg &= ~((1 << 28) | (1 << 27) | (1 << 15));	/* To save other reset values */
	reg |=  (0 << 28) | (1 << 27) | (1 << 15);
	__raw_writel(reg, S3C_VA_SYS + 0x0214);
}

static struct s3c_fb_platdata smdk5210_lcd1_pdata __initdata = {
#if defined(CONFIG_LCD_WA101S) || defined(CONFIG_LCD_LMS501KF03) || \
	defined(CONFIG_S5P_DP)
	.win[0]		= &smdk5210_fb_win0,
	.win[1]		= &smdk5210_fb_win1,
	.win[2]		= &smdk5210_fb_win2,
#endif
	.default_win	= 2,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
#if defined(CONFIG_LCD_LMS501KF03)
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
#elif defined(CONFIG_LCD_WA101S)
	.vidcon1	= VIDCON1_INV_VCLK | VIDCON1_INV_HSYNC |
			  VIDCON1_INV_VSYNC,
#elif defined(CONFIG_S5P_DP)
	.vidcon1	= 0,
#endif
	.setup_gpio	= exynos_fimd_gpio_setup_24bpp,
};
#endif

#ifdef CONFIG_S5P_DP
static struct video_info smdk5210_dp_config = {
	.name			= "DELL U2410, for SMDK TEST",

	.h_total		= 2222,
	.h_active		= 1920,
	.h_sync_width		= 80,
	.h_back_porch		= 172,
	.h_front_porch		= 60,

	.v_total		= 1125,
	.v_active		= 1080,
	.v_sync_width		= 10,
	.v_back_porch		= 25,
	.v_front_porch		= 10,

	.v_sync_rate		= 60,

	.mvid			= 0,
	.nvid			= 0,

	.h_sync_polarity	= 0,
	.v_sync_polarity	= 0,
	.interlaced		= 0,

	.color_space		= COLOR_RGB,
	.dynamic_range		= VESA,
	.ycbcr_coeff		= COLOR_YCBCR601,
	.color_depth		= COLOR_8,

	.sync_clock		= 0,
	.even_field		= 0,

	.refresh_denominator	= REFRESH_DENOMINATOR_1,

	.test_pattern		= COLORBAR_32,
	.link_rate		= LINK_RATE_1_62GBPS,
	.lane_count		= LANE_COUNT2,

	.video_mute_on		= 0,

	.master_mode		= 0,
	.bist_mode		= 0,
};

static struct s5p_dp_platdata smdk5210_dp_data __initdata = {
	.video_info	= &smdk5210_dp_config,
	.phy_init	= s5p_dp_phy_init,
	.phy_exit	= s5p_dp_phy_exit,
};
#endif

#ifdef CONFIG_EXYNOS4_DEV_DWMCI
static void exynos_dwmci_cfg_gpio(int width)
{
	unsigned int gpio;

	for (gpio = EXYNOS5_GPC0(0); gpio < EXYNOS5_GPC0(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV2);
	}

	switch (width) {
	case 8:
		for (gpio = EXYNOS5_GPC1(3); gpio <= EXYNOS5_GPC1(6); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(4));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV2);
		}
	case 4:
		for (gpio = EXYNOS5_GPC0(3); gpio <= EXYNOS5_GPC0(6); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV2);
		}
		break;
	case 1:
		gpio = EXYNOS5_GPC0(3);
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV2);
	default:
		break;
	}
}

static struct dw_mci_board exynos_dwmci_pdata __initdata = {
	.num_slots		= 1,
	.quirks			= DW_MCI_QUIRK_BROKEN_CARD_DETECTION | DW_MCI_QUIRK_HIGHSPEED,
	.bus_hz			= 66 * 1000 * 1000,
	.caps			= MMC_CAP_UHS_DDR50 | MMC_CAP_1_8V_DDR | MMC_CAP_8_BIT_DATA,
	.detect_delay_ms	= 200,
	.hclk_name		= "dwmci",
	.cclk_name		= "sclk_dwmci",
	.cfg_gpio		= exynos_dwmci_cfg_gpio,
};
#endif

#ifdef CONFIG_S3C_DEV_HSMMC
static struct s3c_sdhci_platdata smdk5210_hsmmc0_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
#ifdef CONFIG_EXYNOS4_SDHCI_CH0_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};
#endif

#ifdef CONFIG_S3C_DEV_HSMMC1
static struct s3c_sdhci_platdata smdk5210_hsmmc1_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};
#endif

#ifdef CONFIG_S3C_DEV_HSMMC2
static struct s3c_sdhci_platdata smdk5210_hsmmc2_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
#ifdef CONFIG_EXYNOS4_SDHCI_CH2_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};
#endif

#ifdef CONFIG_S3C_DEV_HSMMC3
static struct s3c_sdhci_platdata smdk5210_hsmmc3_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
	.clk_type		= S3C_SDHCI_CLK_DIV_EXTERNAL,
};
#endif

static struct regulator_consumer_supply wm8994_fixed_voltage0_supplies[] = {
	REGULATOR_SUPPLY("AVDD2", "1-001a"),
	REGULATOR_SUPPLY("CPVDD", "1-001a"),
};

static struct regulator_consumer_supply wm8994_fixed_voltage1_supplies[] = {
	REGULATOR_SUPPLY("SPKVDD1", "1-001a"),
	REGULATOR_SUPPLY("SPKVDD2", "1-001a"),
};

static struct regulator_consumer_supply wm8994_fixed_voltage2_supplies =
	REGULATOR_SUPPLY("DBVDD", "1-001a");

static struct regulator_init_data wm8994_fixed_voltage0_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(wm8994_fixed_voltage0_supplies),
	.consumer_supplies	= wm8994_fixed_voltage0_supplies,
};

static struct regulator_init_data wm8994_fixed_voltage1_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(wm8994_fixed_voltage1_supplies),
	.consumer_supplies	= wm8994_fixed_voltage1_supplies,
};

static struct regulator_init_data wm8994_fixed_voltage2_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_fixed_voltage2_supplies,
};

static struct fixed_voltage_config wm8994_fixed_voltage0_config = {
	.supply_name	= "VDD_1.8V",
	.microvolts	= 1800000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage0_init_data,
};

static struct fixed_voltage_config wm8994_fixed_voltage1_config = {
	.supply_name	= "DC_5V",
	.microvolts	= 5000000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage1_init_data,
};

static struct fixed_voltage_config wm8994_fixed_voltage2_config = {
	.supply_name	= "VDD_3.3V",
	.microvolts	= 3300000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage2_init_data,
};

static struct platform_device wm8994_fixed_voltage0 = {
	.name		= "reg-fixed-voltage",
	.id		= 0,
	.dev		= {
		.platform_data	= &wm8994_fixed_voltage0_config,
	},
};

static struct platform_device wm8994_fixed_voltage1 = {
	.name		= "reg-fixed-voltage",
	.id		= 1,
	.dev		= {
		.platform_data	= &wm8994_fixed_voltage1_config,
	},
};

static struct platform_device wm8994_fixed_voltage2 = {
	.name		= "reg-fixed-voltage",
	.id		= 2,
	.dev		= {
		.platform_data	= &wm8994_fixed_voltage2_config,
	},
};

static struct regulator_consumer_supply wm8994_avdd1_supply =
	REGULATOR_SUPPLY("AVDD1", "1-001a");

static struct regulator_consumer_supply wm8994_dcvdd_supply =
	REGULATOR_SUPPLY("DCVDD", "1-001a");

static struct regulator_init_data wm8994_ldo1_data = {
	.constraints	= {
		.name		= "AVDD1",
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_avdd1_supply,
};

static struct regulator_init_data wm8994_ldo2_data = {
	.constraints	= {
		.name		= "DCVDD",
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_dcvdd_supply,
};

static struct wm8994_pdata wm8994_platform_data = {
	/* configure gpio1 function: 0x0001(Logic level input/output) */
	.gpio_defaults[0] = 0x0001,
	/* If the i2s0 and i2s2 is enabled simultaneously */
	.gpio_defaults[7] = 0x8100, /* GPIO8  DACDAT3 in */
	.gpio_defaults[8] = 0x0100, /* GPIO9  ADCDAT3 out */
	.gpio_defaults[9] = 0x0100, /* GPIO10 LRCLK3  out */
	.gpio_defaults[10] = 0x0100,/* GPIO11 BCLK3   out */
	.ldo[0] = { 0, NULL, &wm8994_ldo1_data },
	.ldo[1] = { 0, NULL, &wm8994_ldo2_data },
};

static struct i2c_board_info i2c_devs1[] __initdata = {
	{
		I2C_BOARD_INFO("wm8994", 0x1a),
		.platform_data	= &wm8994_platform_data,
	},
};

static struct i2c_board_info i2c_devs2[] __initdata = {
	{
		I2C_BOARD_INFO("exynos_hdcp", (0x74 >> 1)),
	},
};

#ifdef CONFIG_USB_EHCI_S5P
static struct s5p_ehci_platdata smdk5210_ehci_pdata;

static void __init smdk5210_ehci_init(void)
{
	struct s5p_ehci_platdata *pdata = &smdk5210_ehci_pdata;

	s5p_ehci_set_platdata(pdata);
}
#endif

#ifdef CONFIG_USB_OHCI_S5P
static struct s5p_ohci_platdata smdk5210_ohci_pdata;

static void __init smdk5210_ohci_init(void)
{
	struct s5p_ohci_platdata *pdata = &smdk5210_ohci_pdata;

	s5p_ohci_set_platdata(pdata);
}
#endif

#ifdef CONFIG_EXYNOS_DEV_SS_UDC
static struct exynos_ss_udc_plat smdk5210_ss_udc_pdata;

static void __init smdk5210_ss_udc_init(void)
{
	struct exynos_ss_udc_plat *pdata = &smdk5210_ss_udc_pdata;

	exynos_ss_udc_set_platdata(pdata);
}
#endif

static struct platform_device *smdk5210_devices[] __initdata = {
#ifdef CONFIG_EXYNOS_MEDIA_DEVICE
	&exynos_device_md0,
	&exynos_device_md1,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_GSCALER
	&exynos5_device_gsc0,
	&exynos5_device_gsc1,
	&exynos5_device_gsc2,
	&exynos5_device_gsc3,
#endif
#ifdef CONFIG_S5P_DP
	&s5p_device_dp,
#endif
#ifdef CONFIG_FB_S3C
	&s5p_device_fimd1,
#if defined(CONFIG_LCD_LMS501KF03)
	&s3c_device_spi_gpio,
#elif defined(CONFIG_LCD_WA101S)
	&smdk5210_lcd_wa101s,
#endif	
#endif	
#if defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
	&s5p_device_mfc,
#endif
#ifdef CONFIG_ION_EXYNOS
	&exynos_device_ion,
#endif
	&s3c_device_i2c1,
	&s3c_device_i2c2,
#ifdef CONFIG_S3C_DEV_HSMMC
	&s3c_device_hsmmc0,
#endif
#ifdef CONFIG_S3C_DEV_HSMMC1
	&s3c_device_hsmmc1,
#endif
#ifdef CONFIG_S3C_DEV_HSMMC2
	&s3c_device_hsmmc2,
#endif
#ifdef CONFIG_S3C_DEV_HSMMC3
	&s3c_device_hsmmc3,
#endif
#ifdef CONFIG_SND_SAMSUNG_AC97
	&exynos_device_ac97,
#endif
#ifdef CONFIG_SND_SAMSUNG_I2S
	&exynos_device_i2s0,
#endif
#ifdef CONFIG_SND_SAMSUNG_PCM
	&exynos_device_pcm0,
#endif
#ifdef CONFIG_SND_SAMSUNG_SPDIF
	&exynos_device_spdif,
#endif
#ifdef CONFIG_SND_SAMSUNG_RP
	&exynos_device_srp,
#endif
	&wm8994_fixed_voltage0,
	&wm8994_fixed_voltage1,
	&wm8994_fixed_voltage2,
	&samsung_asoc_dma,
	&samsung_asoc_idma,
#ifdef CONFIG_EXYNOS4_DEV_DWMCI
	&exynos_device_dwmci,
#endif
	&smdk5210_smsc911x,

#ifdef CONFIG_VIDEO_EXYNOS_TV
#ifdef CONFIG_VIDEO_EXYNOS_HDMI
	&s5p_device_hdmi,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_HDMIPHY
	&s5p_device_i2c_hdmiphy,
#endif
#ifdef CONFIG_VIDEO_EXYNOS_MIXER
	&s5p_device_mixer,
#endif
#endif
#ifdef CONFIG_USB_EHCI_S5P
	&s5p_device_ehci,
#endif
#ifdef CONFIG_USB_OHCI_S5P
	&s5p_device_ohci,
#endif
#ifdef CONFIG_EXYNOS_DEV_SS_UDC
	&exynos_device_ss_udc,
#endif
};

#ifdef CONFIG_SAMSUNG_DEV_BACKLIGHT
/* LCD Backlight data */
static struct samsung_bl_gpio_info smdk5210_bl_gpio_info = {
	.no = EXYNOS5_GPB2(0),
	.func = S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data smdk5210_bl_data = {
	.pwm_id = 0,
#if defined(CONFIG_LCD_LMS501KF03)
	.pwm_period_ns  = 1000,
#endif
};
#endif

#if defined(CONFIG_S5P_MEM_CMA)
static void __init exynos5_cma_region_reserve(
			struct cma_region *regions_normal,
			struct cma_region *regions_secure)
{
	struct cma_region *reg;
	size_t size_secure = 0, align_secure = 0;
	phys_addr_t paddr = 0;

	for (reg = regions_normal; reg->size != 0; reg++) {
		if ((reg->alignment & (reg->alignment - 1)) || reg->reserved)
			continue;

		if (reg->start) {
			if (!memblock_is_region_reserved(reg->start, reg->size)
			    && memblock_reserve(reg->start, reg->size) >= 0)
				reg->reserved = 1;
		} else {
			paddr = __memblock_alloc_base(reg->size, reg->alignment,
					MEMBLOCK_ALLOC_ACCESSIBLE);
			if (paddr) {
				reg->start = paddr;
				reg->reserved = 1;
				if (reg->size & (reg->alignment - 1))
					memblock_free(paddr + reg->size,
						ALIGN(reg->size, reg->alignment)
						- reg->size);
			}
		}
	}

	if (regions_secure && regions_secure->size) {
		for (reg = regions_secure; reg->size != 0; reg++)
			size_secure += reg->size;

		reg--;

		align_secure = reg->alignment;
		BUG_ON(align_secure & (align_secure - 1));

		paddr -= size_secure;
		paddr &= ~(align_secure - 1);

		if (!memblock_reserve(paddr, size_secure)) {
			do {
				reg->start = paddr;
				reg->reserved = 1;
				paddr += reg->size;
			} while (reg-- != regions_secure);
		}
	}
}

static void __init exynos5_reserve_mem(void)
{
	static struct cma_region regions[] = {
#ifdef CONFIG_ANDROID_PMEM_MEMSIZE_PMEM
		{
			.name = "pmem",
			.size = CONFIG_ANDROID_PMEM_MEMSIZE_PMEM * SZ_1K,
			.start = 0,
		},
#endif
#ifdef CONFIG_ANDROID_PMEM_MEMSIZE_PMEM_GPU1
		{
			.name = "pmem_gpu1",
			.size = CONFIG_ANDROID_PMEM_MEMSIZE_PMEM_GPU1 * SZ_1K,
			.start = 0,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMD
		{
			.name = "fimd",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_FIMD * SZ_1K,
			.start = 0
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_GSC0
		{
			.name = "gsc0",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_GSC0 * SZ_1K,
			.start = 0
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_GSC1
		{
			.name = "gsc1",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_GSC1 * SZ_1K,
			.start = 0
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_GSC2
		{
			.name = "gsc2",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_GSC2 * SZ_1K,
			.start = 0
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_GSC3
		{
			.name = "gsc3",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_GSC3 * SZ_1K,
			.start = 0
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_S5P_MFC
		{
			.name		= "fw",
			.size		= 1 << 20,
			{ .alignment	= 128 << 10 },
			.start		= 0x44000000,
		},
		{
			.name		= "b1",
			.size		= 32 << 20,
			.start		= 0x45000000,
		},
#endif
#ifdef CONFIG_VIDEO_SAMSUNG_MEMSIZE_TV
		{
			.name = "tv",
			.size = CONFIG_VIDEO_SAMSUNG_MEMSIZE_TV * SZ_1K,
			.start = 0
		},
#endif
		{
			.size = 0
		},
	};
	static const char map[] __initconst =
		"android_pmem.0=pmem;android_pmem.1=pmem_gpu1;"
		"s3cfb.0=fimd;"
		"exynos-gsc.0=gsc0;exynos-gsc.1=gsc1;exynos-gsc.2=gsc2;exynos-gsc.3=gsc3;"
		"ion-exynos=fimd,gsc0,gsc1,gsc2,gsc3;"
		"s5p-mfc-v6/f=fw;"
		"s5p-mfc-v6/a=b1;"
		"s5p-mixer=tv;";

	cma_set_defaults(regions, map);

	exynos5_cma_region_reserve(regions, NULL);
}
#else /* !CONFIG_S5P_MEM_CMA */
static inline void exynos5_reserve_mem(void)
{
}
#endif

static void __init smdk5210_map_io(void)
{
	clk_xusbxti.rate = 24000000;
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(smdk5210_uartcfgs, ARRAY_SIZE(smdk5210_uartcfgs));
	exynos5_reserve_mem();
}

static void __init smdk5210_smsc911x_init(void)
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

static void s5p_tv_setup(void)
{
	/* direct HPD to HDMI chip */
	gpio_request(EXYNOS5_GPX3(7), "hpd-plug");

	gpio_direction_input(EXYNOS5_GPX3(7));
	s3c_gpio_cfgpin(EXYNOS5_GPX3(7), S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(EXYNOS5_GPX3(7), S3C_GPIO_PULL_NONE);

	/* setup dependencies between TV devices */
	/* This will be added after power domain for exynos5 is developed */
	/* s5p_device_hdmi.dev.parent = &exynos4_device_pd[PD_TV].dev; */
	/* s5p_device_mixer.dev.parent = &exynos4_device_pd[PD_TV].dev; */
}

static void __init smdk5210_machine_init(void)
{
#if defined(CONFIG_VIDEO_EXYNOS_TV) && defined(CONFIG_VIDEO_EXYNOS_HDMI)
	dev_set_name(&s5p_device_hdmi.dev, "exynos5-hdmi");
	clk_add_alias("hdmi", "s5p-hdmi", "hdmi", &s5p_device_hdmi.dev);
	clk_add_alias("hdmiphy", "s5p-hdmi", "hdmiphy", &s5p_device_hdmi.dev);
#endif
	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));
	s3c_i2c2_set_platdata(NULL);
	i2c_register_board_info(2, i2c_devs2, ARRAY_SIZE(i2c_devs2));

#ifdef CONFIG_FB_S3C
	dev_set_name(&s5p_device_fimd1.dev, "s3cfb.1");
	clk_add_alias("lcd", "exynos5-fb.1", "lcd", &s5p_device_fimd1.dev);
	clk_add_alias("sclk_fimd", "exynos5-fb.1", "sclk_fimd",
			&s5p_device_fimd1.dev);
	s5p_fb_setname(1, "exynos5-fb");

#if defined(CONFIG_LCD_LMS501KF03)
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));
#endif
	s5p_fimd1_set_platdata(&smdk5210_lcd1_pdata);
#endif

#ifdef CONFIG_S5P_DP
	s5p_dp_set_platdata(&smdk5210_dp_data);
#endif

#ifdef CONFIG_SAMSUNG_DEV_BACKLIGHT
	samsung_bl_set(&smdk5210_bl_gpio_info, &smdk5210_bl_data);
#endif

#if defined(CONFIG_VIDEO_SAMSUNG_S5P_MFC)
	dev_set_name(&s5p_device_mfc.dev, "s3c-mfc");
	clk_add_alias("mfc", "s5p-mfc-v6", "mfc", &s5p_device_mfc.dev);
	s5p_mfc_setname(&s5p_device_mfc, "s5p-mfc-v6");
#endif
#ifdef CONFIG_ION_EXYNOS
	exynos_ion_set_platdata();
#endif

#ifdef CONFIG_EXYNOS4_DEV_DWMCI
	exynos_dwmci_set_platdata(&exynos_dwmci_pdata);
#endif
#ifdef CONFIG_S3C_DEV_HSMMC
	s3c_sdhci0_set_platdata(&smdk5210_hsmmc0_pdata);
#endif
#ifdef CONFIG_S3C_DEV_HSMMC1
	s3c_sdhci1_set_platdata(&smdk5210_hsmmc1_pdata);
#endif
#ifdef CONFIG_S3C_DEV_HSMMC2
	s3c_sdhci2_set_platdata(&smdk5210_hsmmc2_pdata);
#endif
#ifdef CONFIG_S3C_DEV_HSMMC3
	s3c_sdhci3_set_platdata(&smdk5210_hsmmc3_pdata);
#endif
#ifdef CONFIG_USB_EHCI_S5P
	smdk5210_ehci_init();
#endif
#ifdef CONFIG_USB_OHCI_S5P
	smdk5210_ohci_init();
#endif
#ifdef CONFIG_EXYNOS_DEV_SS_UDC
	smdk5210_ss_udc_init();
#endif
	smdk5210_smsc911x_init();
	platform_add_devices(smdk5210_devices, ARRAY_SIZE(smdk5210_devices));

#ifdef CONFIG_FB_S3C
	exynos4_fimd_setup_clock(&s5p_device_fimd1.dev, "sclk_fimd", "mout_mpll_user",
				800 * MHZ);
#endif
#ifdef CONFIG_VIDEO_EXYNOS_TV
	s5p_tv_setup();
	s5p_i2c_hdmiphy_set_platdata(NULL);
#endif
}

MACHINE_START(SMDK5210, "SMDK5210")
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= exynos5_init_irq,
	.map_io		= smdk5210_map_io,
	.init_machine	= smdk5210_machine_init,
	.timer		= &exynos4_timer,
MACHINE_END
