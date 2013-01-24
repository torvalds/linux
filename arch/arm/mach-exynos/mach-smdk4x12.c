/*
 * linux/arch/arm/mach-exynos4/mach-smdk4x12.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/lcd.h>
#include <linux/mfd/max8997.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/regulator/machine.h>
#include <linux/serial_core.h>
#include <linux/platform_data/i2c-s3c2410.h>
#include <linux/platform_data/s3c-hsotg.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <video/samsung_fimd.h>
#include <plat/backlight.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/fb.h>
#include <plat/gpio-cfg.h>
#include <plat/keypad.h>
#include <plat/mfc.h>
#include <plat/regs-serial.h>
#include <plat/sdhci.h>

#include <mach/map.h>

#include <drm/exynos_drm.h>
#include "common.h"

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define SMDK4X12_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define SMDK4X12_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDK4X12_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S5PV210_UFCON_TXTRIG4 |	\
				 S5PV210_UFCON_RXTRIG4)

static struct s3c2410_uartcfg smdk4x12_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDK4X12_UCON_DEFAULT,
		.ulcon		= SMDK4X12_ULCON_DEFAULT,
		.ufcon		= SMDK4X12_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= SMDK4X12_UCON_DEFAULT,
		.ulcon		= SMDK4X12_ULCON_DEFAULT,
		.ufcon		= SMDK4X12_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDK4X12_UCON_DEFAULT,
		.ulcon		= SMDK4X12_ULCON_DEFAULT,
		.ufcon		= SMDK4X12_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDK4X12_UCON_DEFAULT,
		.ulcon		= SMDK4X12_ULCON_DEFAULT,
		.ufcon		= SMDK4X12_UFCON_DEFAULT,
	},
};

static struct s3c_sdhci_platdata smdk4x12_hsmmc2_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
#ifdef CONFIG_EXYNOS4_SDHCI_CH2_8BIT
	.max_width		= 8,
	.host_caps		= MMC_CAP_8_BIT_DATA,
#endif
};

static struct s3c_sdhci_platdata smdk4x12_hsmmc3_pdata __initdata = {
	.cd_type		= S3C_SDHCI_CD_INTERNAL,
};

static struct regulator_consumer_supply max8997_buck1 =
	REGULATOR_SUPPLY("vdd_arm", NULL);

static struct regulator_consumer_supply max8997_buck2 =
	REGULATOR_SUPPLY("vdd_int", NULL);

static struct regulator_consumer_supply max8997_buck3 =
	REGULATOR_SUPPLY("vdd_g3d", NULL);

static struct regulator_init_data max8997_buck1_data = {
	.constraints	= {
		.name		= "VDD_ARM_SMDK4X12",
		.min_uV		= 925000,
		.max_uV		= 1350000,
		.always_on	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &max8997_buck1,
};

static struct regulator_init_data max8997_buck2_data = {
	.constraints	= {
		.name		= "VDD_INT_SMDK4X12",
		.min_uV		= 950000,
		.max_uV		= 1150000,
		.always_on	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &max8997_buck2,
};

static struct regulator_init_data max8997_buck3_data = {
	.constraints	= {
		.name		= "VDD_G3D_SMDK4X12",
		.min_uV		= 950000,
		.max_uV		= 1150000,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
		.state_mem	= {
			.disabled	= 1,
		},
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &max8997_buck3,
};

static struct max8997_regulator_data smdk4x12_max8997_regulators[] = {
	{ MAX8997_BUCK1, &max8997_buck1_data },
	{ MAX8997_BUCK2, &max8997_buck2_data },
	{ MAX8997_BUCK3, &max8997_buck3_data },
};

static struct max8997_platform_data smdk4x12_max8997_pdata = {
	.num_regulators	= ARRAY_SIZE(smdk4x12_max8997_regulators),
	.regulators	= smdk4x12_max8997_regulators,

	.buck1_voltage[0] = 1100000,	/* 1.1V */
	.buck1_voltage[1] = 1100000,	/* 1.1V */
	.buck1_voltage[2] = 1100000,	/* 1.1V */
	.buck1_voltage[3] = 1100000,	/* 1.1V */
	.buck1_voltage[4] = 1100000,	/* 1.1V */
	.buck1_voltage[5] = 1100000,	/* 1.1V */
	.buck1_voltage[6] = 1000000,	/* 1.0V */
	.buck1_voltage[7] = 950000,	/* 0.95V */

	.buck2_voltage[0] = 1100000,	/* 1.1V */
	.buck2_voltage[1] = 1000000,	/* 1.0V */
	.buck2_voltage[2] = 950000,	/* 0.95V */
	.buck2_voltage[3] = 900000,	/* 0.9V */
	.buck2_voltage[4] = 1100000,	/* 1.1V */
	.buck2_voltage[5] = 1000000,	/* 1.0V */
	.buck2_voltage[6] = 950000,	/* 0.95V */
	.buck2_voltage[7] = 900000,	/* 0.9V */

	.buck5_voltage[0] = 1100000,	/* 1.1V */
	.buck5_voltage[1] = 1100000,	/* 1.1V */
	.buck5_voltage[2] = 1100000,	/* 1.1V */
	.buck5_voltage[3] = 1100000,	/* 1.1V */
	.buck5_voltage[4] = 1100000,	/* 1.1V */
	.buck5_voltage[5] = 1100000,	/* 1.1V */
	.buck5_voltage[6] = 1100000,	/* 1.1V */
	.buck5_voltage[7] = 1100000,	/* 1.1V */
};

static struct i2c_board_info smdk4x12_i2c_devs0[] __initdata = {
	{
		I2C_BOARD_INFO("max8997", 0x66),
		.platform_data	= &smdk4x12_max8997_pdata,
	}
};

static struct i2c_board_info smdk4x12_i2c_devs1[] __initdata = {
	{ I2C_BOARD_INFO("wm8994", 0x1a), }
};

static struct i2c_board_info smdk4x12_i2c_devs3[] __initdata = {
	/* nothing here yet */
};

static struct i2c_board_info smdk4x12_i2c_devs7[] __initdata = {
	/* nothing here yet */
};

static struct samsung_bl_gpio_info smdk4x12_bl_gpio_info = {
	.no = EXYNOS4_GPD0(1),
	.func = S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data smdk4x12_bl_data = {
	.pwm_id = 1,
	.pwm_period_ns  = 1000,
};

static struct pwm_lookup smdk4x12_pwm_lookup[] = {
	PWM_LOOKUP("s3c24xx-pwm.1", 0, "pwm-backlight.0", NULL),
};

static uint32_t smdk4x12_keymap[] __initdata = {
	/* KEY(row, col, keycode) */
	KEY(1, 3, KEY_1), KEY(1, 4, KEY_2), KEY(1, 5, KEY_3),
	KEY(1, 6, KEY_4), KEY(1, 7, KEY_5),
	KEY(2, 5, KEY_D), KEY(2, 6, KEY_A), KEY(2, 7, KEY_B),
	KEY(0, 7, KEY_E), KEY(0, 5, KEY_C)
};

static struct matrix_keymap_data smdk4x12_keymap_data __initdata = {
	.keymap		= smdk4x12_keymap,
	.keymap_size	= ARRAY_SIZE(smdk4x12_keymap),
};

static struct samsung_keypad_platdata smdk4x12_keypad_data __initdata = {
	.keymap_data	= &smdk4x12_keymap_data,
	.rows		= 3,
	.cols		= 8,
};

#ifdef CONFIG_DRM_EXYNOS_FIMD
static struct exynos_drm_fimd_pdata drm_fimd_pdata = {
	.panel	= {
		.timing	= {
			.left_margin	= 8,
			.right_margin	= 8,
			.upper_margin	= 6,
			.lower_margin	= 6,
			.hsync_len	= 6,
			.vsync_len	= 4,
			.xres		= 480,
			.yres		= 800,
		},
	},
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.default_win	= 0,
	.bpp		= 32,
};
#else
static struct s3c_fb_pd_win smdk4x12_fb_win0 = {
	.xres		= 480,
	.yres		= 800,
	.virtual_x	= 480,
	.virtual_y	= 800 * 2,
	.max_bpp	= 32,
	.default_bpp	= 24,
};

static struct fb_videomode smdk4x12_lcd_timing = {
	.left_margin	= 8,
	.right_margin	= 8,
	.upper_margin	= 6,
	.lower_margin	= 6,
	.hsync_len	= 6,
	.vsync_len	= 4,
	.xres		= 480,
	.yres		= 800,
};

static struct s3c_fb_platdata smdk4x12_lcd_pdata __initdata = {
	.win[0]		= &smdk4x12_fb_win0,
	.vtiming	= &smdk4x12_lcd_timing,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= exynos4_fimd0_gpio_setup_24bpp,
};
#endif

/* USB OTG */
static struct s3c_hsotg_plat smdk4x12_hsotg_pdata;

static struct platform_device *smdk4x12_devices[] __initdata = {
	&s3c_device_hsmmc2,
	&s3c_device_hsmmc3,
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_i2c3,
	&s3c_device_i2c7,
	&s3c_device_rtc,
	&s3c_device_usb_hsotg,
	&s3c_device_wdt,
	&s5p_device_fimc0,
	&s5p_device_fimc1,
	&s5p_device_fimc2,
	&s5p_device_fimc3,
	&s5p_device_fimc_md,
	&s5p_device_fimd0,
	&s5p_device_mfc,
	&s5p_device_mfc_l,
	&s5p_device_mfc_r,
	&samsung_device_keypad,
};

static void __init smdk4x12_map_io(void)
{
	exynos_init_io(NULL, 0);
	s3c24xx_init_clocks(clk_xusbxti.rate);
	s3c24xx_init_uarts(smdk4x12_uartcfgs, ARRAY_SIZE(smdk4x12_uartcfgs));
}

static void __init smdk4x12_reserve(void)
{
	s5p_mfc_reserve_mem(0x43000000, 8 << 20, 0x51000000, 8 << 20);
}

static void __init smdk4x12_machine_init(void)
{
	s3c_i2c0_set_platdata(NULL);
	i2c_register_board_info(0, smdk4x12_i2c_devs0,
				ARRAY_SIZE(smdk4x12_i2c_devs0));

	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, smdk4x12_i2c_devs1,
				ARRAY_SIZE(smdk4x12_i2c_devs1));

	s3c_i2c3_set_platdata(NULL);
	i2c_register_board_info(3, smdk4x12_i2c_devs3,
				ARRAY_SIZE(smdk4x12_i2c_devs3));

	s3c_i2c7_set_platdata(NULL);
	i2c_register_board_info(7, smdk4x12_i2c_devs7,
				ARRAY_SIZE(smdk4x12_i2c_devs7));

	samsung_bl_set(&smdk4x12_bl_gpio_info, &smdk4x12_bl_data);
	pwm_add_table(smdk4x12_pwm_lookup, ARRAY_SIZE(smdk4x12_pwm_lookup));

	samsung_keypad_set_platdata(&smdk4x12_keypad_data);

	s3c_sdhci2_set_platdata(&smdk4x12_hsmmc2_pdata);
	s3c_sdhci3_set_platdata(&smdk4x12_hsmmc3_pdata);

	s3c_hsotg_set_platdata(&smdk4x12_hsotg_pdata);

#ifdef CONFIG_DRM_EXYNOS_FIMD
	s5p_device_fimd0.dev.platform_data = &drm_fimd_pdata;
	exynos4_fimd0_gpio_setup_24bpp();
#else
	s5p_fimd0_set_platdata(&smdk4x12_lcd_pdata);
#endif

	platform_add_devices(smdk4x12_devices, ARRAY_SIZE(smdk4x12_devices));
}

MACHINE_START(SMDK4212, "SMDK4212")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.atag_offset	= 0x100,
	.smp		= smp_ops(exynos_smp_ops),
	.init_irq	= exynos4_init_irq,
	.map_io		= smdk4x12_map_io,
	.init_machine	= smdk4x12_machine_init,
	.timer		= &exynos4_timer,
	.restart	= exynos4_restart,
	.reserve	= &smdk4x12_reserve,
MACHINE_END

MACHINE_START(SMDK4412, "SMDK4412")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	/* Maintainer: Changhwan Youn <chaos.youn@samsung.com> */
	.atag_offset	= 0x100,
	.smp		= smp_ops(exynos_smp_ops),
	.init_irq	= exynos4_init_irq,
	.map_io		= smdk4x12_map_io,
	.init_machine	= smdk4x12_machine_init,
	.init_late	= exynos_init_late,
	.timer		= &exynos4_timer,
	.restart	= exynos4_restart,
	.reserve	= &smdk4x12_reserve,
MACHINE_END
