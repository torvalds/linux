/* linux/arch/arm/mach-s5p64x0/mach-smdk6440.c
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/pwm_backlight.h>
#include <linux/fb.h>
#include <linux/mmc/host.h>

#include <video/platform_lcd.h>
#include <video/samsung_fimd.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <mach/hardware.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>

#include <plat/regs-serial.h>
#include <plat/gpio-cfg.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <linux/platform_data/i2c-s3c2410.h>
#include <plat/pll.h>
#include <plat/adc.h>
#include <linux/platform_data/touchscreen-s3c2410.h>
#include <plat/samsung-time.h>
#include <plat/backlight.h>
#include <plat/fb.h>
#include <plat/sdhci.h>

#include "common.h"
#include "i2c.h"

#define SMDK6440_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				S3C2410_UCON_RXILEVEL |		\
				S3C2410_UCON_TXIRQMODE |	\
				S3C2410_UCON_RXIRQMODE |	\
				S3C2410_UCON_RXFIFO_TOI |	\
				S3C2443_UCON_RXERR_IRQEN)

#define SMDK6440_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDK6440_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				S3C2440_UFCON_TXTRIG16 |	\
				S3C2410_UFCON_RXTRIG8)

static struct s3c2410_uartcfg smdk6440_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= SMDK6440_UCON_DEFAULT,
		.ulcon		= SMDK6440_ULCON_DEFAULT,
		.ufcon		= SMDK6440_UFCON_DEFAULT,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= SMDK6440_UCON_DEFAULT,
		.ulcon		= SMDK6440_ULCON_DEFAULT,
		.ufcon		= SMDK6440_UFCON_DEFAULT,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= SMDK6440_UCON_DEFAULT,
		.ulcon		= SMDK6440_ULCON_DEFAULT,
		.ufcon		= SMDK6440_UFCON_DEFAULT,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= SMDK6440_UCON_DEFAULT,
		.ulcon		= SMDK6440_ULCON_DEFAULT,
		.ufcon		= SMDK6440_UFCON_DEFAULT,
	},
};

/* Frame Buffer */
static struct s3c_fb_pd_win smdk6440_fb_win0 = {
	.max_bpp	= 32,
	.default_bpp	= 24,
	.xres		= 800,
	.yres		= 480,
};

static struct fb_videomode smdk6440_lcd_timing = {
	.left_margin	= 8,
	.right_margin	= 13,
	.upper_margin	= 7,
	.lower_margin	= 5,
	.hsync_len	= 3,
	.vsync_len	= 1,
	.xres		= 800,
	.yres		= 480,
};

static struct s3c_fb_platdata smdk6440_lcd_pdata __initdata = {
	.win[0]		= &smdk6440_fb_win0,
	.vtiming	= &smdk6440_lcd_timing,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= s5p64x0_fb_gpio_setup_24bpp,
};

/* LCD power controller */
static void smdk6440_lte480_reset_power(struct plat_lcd_data *pd,
					 unsigned int power)
{
	int err;

	if (power) {
		err = gpio_request(S5P6440_GPN(5), "GPN");
		if (err) {
			printk(KERN_ERR "failed to request GPN for lcd reset\n");
			return;
		}

		gpio_direction_output(S5P6440_GPN(5), 1);
		gpio_set_value(S5P6440_GPN(5), 0);
		gpio_set_value(S5P6440_GPN(5), 1);
		gpio_free(S5P6440_GPN(5));
	}
}

static struct plat_lcd_data smdk6440_lcd_power_data = {
	.set_power	= smdk6440_lte480_reset_power,
};

static struct platform_device smdk6440_lcd_lte480wv = {
	.name			= "platform-lcd",
	.dev.parent		= &s3c_device_fb.dev,
	.dev.platform_data	= &smdk6440_lcd_power_data,
};

static struct platform_device *smdk6440_devices[] __initdata = {
	&s3c_device_adc,
	&s3c_device_rtc,
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&samsung_device_pwm,
	&s3c_device_ts,
	&s3c_device_wdt,
	&s5p6440_device_iis,
	&s3c_device_fb,
	&smdk6440_lcd_lte480wv,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
	&s3c_device_hsmmc2,
};

static struct s3c_sdhci_platdata smdk6440_hsmmc0_pdata __initdata = {
	.cd_type	= S3C_SDHCI_CD_NONE,
};

static struct s3c_sdhci_platdata smdk6440_hsmmc1_pdata __initdata = {
	.cd_type	= S3C_SDHCI_CD_INTERNAL,
#if defined(CONFIG_S5P64X0_SD_CH1_8BIT)
	.max_width	= 8,
	.host_caps	= MMC_CAP_8_BIT_DATA,
#endif
};

static struct s3c_sdhci_platdata smdk6440_hsmmc2_pdata __initdata = {
	.cd_type	= S3C_SDHCI_CD_NONE,
};

static struct s3c2410_platform_i2c s5p6440_i2c0_data __initdata = {
	.flags		= 0,
	.slave_addr	= 0x10,
	.frequency	= 100*1000,
	.sda_delay	= 100,
	.cfg_gpio	= s5p6440_i2c0_cfg_gpio,
};

static struct s3c2410_platform_i2c s5p6440_i2c1_data __initdata = {
	.flags		= 0,
	.bus_num	= 1,
	.slave_addr	= 0x10,
	.frequency	= 100*1000,
	.sda_delay	= 100,
	.cfg_gpio	= s5p6440_i2c1_cfg_gpio,
};

static struct i2c_board_info smdk6440_i2c_devs0[] __initdata = {
	{ I2C_BOARD_INFO("24c08", 0x50), },
	{ I2C_BOARD_INFO("wm8580", 0x1b), },
};

static struct i2c_board_info smdk6440_i2c_devs1[] __initdata = {
	/* To be populated */
};

/* LCD Backlight data */
static struct samsung_bl_gpio_info smdk6440_bl_gpio_info = {
	.no = S5P6440_GPF(15),
	.func = S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data smdk6440_bl_data = {
	.pwm_id = 1,
};

static void __init smdk6440_map_io(void)
{
	s5p64x0_init_io(NULL, 0);
	s3c24xx_init_clocks(12000000);
	s3c24xx_init_uarts(smdk6440_uartcfgs, ARRAY_SIZE(smdk6440_uartcfgs));
	samsung_set_timer_source(SAMSUNG_PWM3, SAMSUNG_PWM4);
}

static void s5p6440_set_lcd_interface(void)
{
	unsigned int cfg;

	/* select TFT LCD type (RGB I/F) */
	cfg = __raw_readl(S5P64X0_SPCON0);
	cfg &= ~S5P64X0_SPCON0_LCD_SEL_MASK;
	cfg |= S5P64X0_SPCON0_LCD_SEL_RGB;
	__raw_writel(cfg, S5P64X0_SPCON0);
}

static void __init smdk6440_machine_init(void)
{
	s3c24xx_ts_set_platdata(NULL);

	s3c_i2c0_set_platdata(&s5p6440_i2c0_data);
	s3c_i2c1_set_platdata(&s5p6440_i2c1_data);
	i2c_register_board_info(0, smdk6440_i2c_devs0,
			ARRAY_SIZE(smdk6440_i2c_devs0));
	i2c_register_board_info(1, smdk6440_i2c_devs1,
			ARRAY_SIZE(smdk6440_i2c_devs1));

	s5p6440_set_lcd_interface();
	s3c_fb_set_platdata(&smdk6440_lcd_pdata);

	s3c_sdhci0_set_platdata(&smdk6440_hsmmc0_pdata);
	s3c_sdhci1_set_platdata(&smdk6440_hsmmc1_pdata);
	s3c_sdhci2_set_platdata(&smdk6440_hsmmc2_pdata);

	platform_add_devices(smdk6440_devices, ARRAY_SIZE(smdk6440_devices));

	samsung_bl_set(&smdk6440_bl_gpio_info, &smdk6440_bl_data);
}

MACHINE_START(SMDK6440, "SMDK6440")
	/* Maintainer: Kukjin Kim <kgene.kim@samsung.com> */
	.atag_offset	= 0x100,

	.init_irq	= s5p6440_init_irq,
	.map_io		= smdk6440_map_io,
	.init_machine	= smdk6440_machine_init,
	.init_time	= samsung_timer_init,
	.restart	= s5p64x0_restart,
MACHINE_END
