/* linux/arch/arm/mach-s5pc100/mach-smdkc100.c
 *
 * Copyright 2009 Samsung Electronics Co.
 * Author: Byungho Min <bhmin@samsung.com>
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
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/pwm_backlight.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/map.h>
#include <mach/regs-fb.h>
#include <mach/regs-gpio.h>

#include <video/platform_lcd.h>

#include <asm/irq.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <plat/gpio-cfg.h>

#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/s5pc100.h>
#include <plat/fb.h>
#include <plat/iic.h>
#include <plat/ata.h>
#include <plat/adc.h>
#include <plat/keypad.h>
#include <plat/ts.h>
#include <plat/audio.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define SMDKC100_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define SMDKC100_ULCON_DEFAULT	S3C2410_LCON_CS8

#define SMDKC100_UFCON_DEFAULT	(S3C2410_UFCON_FIFOMODE |	\
				 S3C2440_UFCON_RXTRIG8 |	\
				 S3C2440_UFCON_TXTRIG16)

static struct s3c2410_uartcfg smdkc100_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = SMDKC100_UCON_DEFAULT,
		.ulcon	     = SMDKC100_ULCON_DEFAULT,
		.ufcon	     = SMDKC100_UFCON_DEFAULT,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = SMDKC100_UCON_DEFAULT,
		.ulcon	     = SMDKC100_ULCON_DEFAULT,
		.ufcon	     = SMDKC100_UFCON_DEFAULT,
	},
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = SMDKC100_UCON_DEFAULT,
		.ulcon	     = SMDKC100_ULCON_DEFAULT,
		.ufcon	     = SMDKC100_UFCON_DEFAULT,
	},
	[3] = {
		.hwport	     = 3,
		.flags	     = 0,
		.ucon	     = SMDKC100_UCON_DEFAULT,
		.ulcon	     = SMDKC100_ULCON_DEFAULT,
		.ufcon	     = SMDKC100_UFCON_DEFAULT,
	},
};

/* I2C0 */
static struct i2c_board_info i2c_devs0[] __initdata = {
	{I2C_BOARD_INFO("wm8580", 0x1b),},
};

/* I2C1 */
static struct i2c_board_info i2c_devs1[] __initdata = {
};

/* LCD power controller */
static void smdkc100_lcd_power_set(struct plat_lcd_data *pd,
				   unsigned int power)
{
	if (power) {
		/* module reset */
		gpio_direction_output(S5PC100_GPH0(6), 1);
		mdelay(100);
		gpio_direction_output(S5PC100_GPH0(6), 0);
		mdelay(10);
		gpio_direction_output(S5PC100_GPH0(6), 1);
		mdelay(10);
	}
}

static struct plat_lcd_data smdkc100_lcd_power_data = {
	.set_power	= smdkc100_lcd_power_set,
};

static struct platform_device smdkc100_lcd_powerdev = {
	.name			= "platform-lcd",
	.dev.parent		= &s3c_device_fb.dev,
	.dev.platform_data	= &smdkc100_lcd_power_data,
};

/* Frame Buffer */
static struct s3c_fb_pd_win smdkc100_fb_win0 = {
	/* this is to ensure we use win0 */
	.win_mode	= {
		.left_margin	= 8,
		.right_margin	= 13,
		.upper_margin	= 7,
		.lower_margin	= 5,
		.hsync_len	= 3,
		.vsync_len	= 1,
		.xres		= 800,
		.yres		= 480,
		.refresh	= 80,
	},
	.max_bpp	= 32,
	.default_bpp	= 16,
};

static struct s3c_fb_platdata smdkc100_lcd_pdata __initdata = {
	.win[0]		= &smdkc100_fb_win0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= s5pc100_fb_gpio_setup_24bpp,
};

static struct s3c_ide_platdata smdkc100_ide_pdata __initdata = {
	.setup_gpio	= s5pc100_ide_setup_gpio,
};

static uint32_t smdkc100_keymap[] __initdata = {
	/* KEY(row, col, keycode) */
	KEY(0, 3, KEY_1), KEY(0, 4, KEY_2), KEY(0, 5, KEY_3),
	KEY(0, 6, KEY_4), KEY(0, 7, KEY_5),
	KEY(1, 3, KEY_A), KEY(1, 4, KEY_B), KEY(1, 5, KEY_C),
	KEY(1, 6, KEY_D), KEY(1, 7, KEY_E)
};

static struct matrix_keymap_data smdkc100_keymap_data __initdata = {
	.keymap		= smdkc100_keymap,
	.keymap_size	= ARRAY_SIZE(smdkc100_keymap),
};

static struct samsung_keypad_platdata smdkc100_keypad_data __initdata = {
	.keymap_data	= &smdkc100_keymap_data,
	.rows		= 2,
	.cols		= 8,
};

static int smdkc100_backlight_init(struct device *dev)
{
	int ret;

	ret = gpio_request(S5PC100_GPD(0), "Backlight");
	if (ret) {
		printk(KERN_ERR "failed to request GPF for PWM-OUT0\n");
		return ret;
	}

	/* Configure GPIO pin with S5PC100_GPD_TOUT_0 */
	s3c_gpio_cfgpin(S5PC100_GPD(0), S3C_GPIO_SFN(2));

	return 0;
}

static void smdkc100_backlight_exit(struct device *dev)
{
	s3c_gpio_cfgpin(S5PC100_GPD(0), S3C_GPIO_OUTPUT);
	gpio_free(S5PC100_GPD(0));
}

static struct platform_pwm_backlight_data smdkc100_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= 255,
	.dft_brightness	= 255,
	.pwm_period_ns	= 78770,
	.init		= smdkc100_backlight_init,
	.exit		= smdkc100_backlight_exit,
};

static struct platform_device smdkc100_backlight_device = {
	.name		= "pwm-backlight",
	.dev		= {
		.parent		= &s3c_device_timer[0].dev,
		.platform_data	= &smdkc100_backlight_data,
	},
};

static struct platform_device *smdkc100_devices[] __initdata = {
	&s3c_device_adc,
	&s3c_device_cfcon,
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_fb,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
	&s3c_device_hsmmc2,
	&s3c_device_ts,
	&s3c_device_wdt,
	&smdkc100_lcd_powerdev,
	&samsung_asoc_dma,
	&s5pc100_device_iis0,
	&samsung_device_keypad,
	&s5pc100_device_ac97,
	&s3c_device_rtc,
	&s5p_device_fimc0,
	&s5p_device_fimc1,
	&s5p_device_fimc2,
	&s5pc100_device_spdif,
	&s3c_device_timer[0],
	&smdkc100_backlight_device,
};

static struct s3c2410_ts_mach_info s3c_ts_platform __initdata = {
	.delay			= 10000,
	.presc			= 49,
	.oversampling_shift	= 2,
};

static void __init smdkc100_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(12000000);
	s3c24xx_init_uarts(smdkc100_uartcfgs, ARRAY_SIZE(smdkc100_uartcfgs));
}

static void __init smdkc100_machine_init(void)
{
	s3c24xx_ts_set_platdata(&s3c_ts_platform);

	/* I2C */
	s3c_i2c0_set_platdata(NULL);
	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(0, i2c_devs0, ARRAY_SIZE(i2c_devs0));
	i2c_register_board_info(1, i2c_devs1, ARRAY_SIZE(i2c_devs1));

	s3c_fb_set_platdata(&smdkc100_lcd_pdata);
	s3c_ide_set_platdata(&smdkc100_ide_pdata);

	samsung_keypad_set_platdata(&smdkc100_keypad_data);

	s5pc100_spdif_setup_gpio(S5PC100_SPDIF_GPD);

	/* LCD init */
	gpio_request(S5PC100_GPH0(6), "GPH0");
	smdkc100_lcd_power_set(&smdkc100_lcd_power_data, 0);
	platform_add_devices(smdkc100_devices, ARRAY_SIZE(smdkc100_devices));
}

MACHINE_START(SMDKC100, "SMDKC100")
	/* Maintainer: Byungho Min <bhmin@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= s5pc100_init_irq,
	.map_io		= smdkc100_map_io,
	.init_machine	= smdkc100_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
