/*
 * Board-specific setup code for Remote Media Terminal 1 (RMT1)
 * add-on board for the ATNGW100 Network Gateway
 *
 * Copyright (C) 2008 Mediama Technologies
 * Based on ATNGW100 Network Gateway (Copyright (C) Atmel)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/linkage.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/leds.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/atmel_serial.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>

#include <video/atmel_lcdc.h>
#include <sound/atmel-ac97c.h>

#include <asm/delay.h>
#include <asm/io.h>
#include <asm/setup.h>

#include <mach/at32ap700x.h>
#include <mach/board.h>
#include <mach/init.h>
#include <mach/portmux.h>

/* Define board-specifoic GPIO assignments */
#define PIN_LCD_BL	GPIO_PIN_PA(28)
#define PWM_CH_BL	0	/* Must match with GPIO pin definition */
#define PIN_LCD_DISP	GPIO_PIN_PA(31)
#define	PIN_AC97_RST_N	GPIO_PIN_PA(30)
#define PB_EXTINT_BASE	25
#define TS_IRQ		0
#define PIN_TS_EXTINT	GPIO_PIN_PB(PB_EXTINT_BASE+TS_IRQ)
#define PIN_PB_LEFT	GPIO_PIN_PB(11)
#define PIN_PB_RIGHT	GPIO_PIN_PB(12)
#define PIN_PWR_SW_N	GPIO_PIN_PB(14)
#define PIN_PWR_ON	GPIO_PIN_PB(13)
#define PIN_ZB_RST_N	GPIO_PIN_PA(21)
#define PIN_BT_RST	GPIO_PIN_PA(22)
#define PIN_LED_SYS	GPIO_PIN_PA(16)
#define PIN_LED_A	GPIO_PIN_PA(19)
#define PIN_LED_B	GPIO_PIN_PE(19)

#ifdef CONFIG_BOARD_MRMT_LCD_LQ043T3DX0X
/* Sharp LQ043T3DX0x (or compatible) panel */
static struct fb_videomode __initdata lcd_fb_modes[] = {
	{
		.name		= "480x272 @ 59.94Hz",
		.refresh	= 59.94,
		.xres		= 480,		.yres		= 272,
		.pixclock	= KHZ2PICOS(9000),

		.left_margin	= 2,		.right_margin	= 2,
		.upper_margin	= 3,		.lower_margin	= 9,
		.hsync_len	= 41,		.vsync_len	= 1,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs __initdata lcd_fb_default_monspecs = {
	.manufacturer		= "SHA",
	.monitor		= "LQ043T3DX02",
	.modedb			= lcd_fb_modes,
	.modedb_len		= ARRAY_SIZE(lcd_fb_modes),
	.hfmin			= 14915,
	.hfmax			= 17638,
	.vfmin			= 53,
	.vfmax			= 61,
	.dclkmax		= 9260000,
};

static struct atmel_lcdfb_info __initdata rmt_lcdc_data = {
	.default_bpp		= 24,
	.default_dmacon		= ATMEL_LCDC_DMAEN | ATMEL_LCDC_DMA2DEN,
	.default_lcdcon2	= (ATMEL_LCDC_DISTYPE_TFT
				   | ATMEL_LCDC_CLKMOD_ALWAYSACTIVE
				   | ATMEL_LCDC_INVCLK_NORMAL
				   | ATMEL_LCDC_MEMOR_BIG),
	.lcd_wiring_mode	= ATMEL_LCDC_WIRING_RGB,
	.default_monspecs	= &lcd_fb_default_monspecs,
	.guard_time		= 2,
};
#endif

#ifdef CONFIG_BOARD_MRMT_LCD_KWH043GM08
/* Sharp KWH043GM08-Fxx (or compatible) panel */
static struct fb_videomode __initdata lcd_fb_modes[] = {
	{
		.name		= "480x272 @ 59.94Hz",
		.refresh	= 59.94,
		.xres		= 480,		.yres		= 272,
		.pixclock	= KHZ2PICOS(9000),

		.left_margin	= 2,		.right_margin	= 2,
		.upper_margin	= 3,		.lower_margin	= 9,
		.hsync_len	= 41,		.vsync_len	= 1,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs __initdata lcd_fb_default_monspecs = {
	.manufacturer		= "FOR",
	.monitor		= "KWH043GM08",
	.modedb			= lcd_fb_modes,
	.modedb_len		= ARRAY_SIZE(lcd_fb_modes),
	.hfmin			= 14915,
	.hfmax			= 17638,
	.vfmin			= 53,
	.vfmax			= 61,
	.dclkmax		= 9260000,
};

static struct atmel_lcdfb_info __initdata rmt_lcdc_data = {
	.default_bpp		= 24,
	.default_dmacon		= ATMEL_LCDC_DMAEN | ATMEL_LCDC_DMA2DEN,
	.default_lcdcon2	= (ATMEL_LCDC_DISTYPE_TFT
				   | ATMEL_LCDC_CLKMOD_ALWAYSACTIVE
				   | ATMEL_LCDC_INVCLK_INVERTED
				   | ATMEL_LCDC_MEMOR_BIG),
	.lcd_wiring_mode	= ATMEL_LCDC_WIRING_RGB,
	.default_monspecs	= &lcd_fb_default_monspecs,
	.guard_time		= 2,
};
#endif

#ifdef CONFIG_BOARD_MRMT_AC97
static struct ac97c_platform_data __initdata ac97c0_data = {
	.reset_pin		= PIN_AC97_RST_N,
};
#endif

#ifdef CONFIG_BOARD_MRMT_UCB1400_TS
/* NOTE: IRQ assignment relies on kernel module parameter */
static struct platform_device rmt_ts_device = {
	.name	= "ucb1400_ts",
	.id	= -1,
	}
};
#endif

#ifdef CONFIG_BOARD_MRMT_BL_PWM
/* PWM LEDs: LCD Backlight, etc */
static struct gpio_led rmt_pwm_led[] = {
	/* here the "gpio" is actually a PWM channel */
	{ .name = "backlight",	.gpio = PWM_CH_BL, },
};

static struct gpio_led_platform_data rmt_pwm_led_data = {
	.num_leds	= ARRAY_SIZE(rmt_pwm_led),
	.leds		= rmt_pwm_led,
};

static struct platform_device rmt_pwm_led_dev = {
	.name		= "leds-atmel-pwm",
	.id		= -1,
	.dev		= {
		.platform_data	= &rmt_pwm_led_data,
	},
};
#endif

#ifdef CONFIG_BOARD_MRMT_ADS7846_TS
static int ads7846_pendown_state(void)
{
	return !gpio_get_value( PIN_TS_EXTINT );	/* PENIRQ.*/
}

static struct ads7846_platform_data ads_info = {
	.model				= 7846,
	.keep_vref_on			= 0,	/* Use external VREF pin */
	.vref_delay_usecs		= 0,
	.vref_mv			= 3300,	/* VREF = 3.3V */
	.settle_delay_usecs		= 800,
	.penirq_recheck_delay_usecs	= 800,
	.x_plate_ohms			= 750,
	.y_plate_ohms			= 300,
	.pressure_max			= 4096,
	.debounce_max			= 1,
	.debounce_rep			= 0,
	.debounce_tol			= (~0),
	.get_pendown_state		= ads7846_pendown_state,
	.filter				= NULL,
	.filter_init			= NULL,
};

static struct spi_board_info spi01_board_info[] __initdata = {
	{
		.modalias	= "ads7846",
		.max_speed_hz	= 31250*26,
		.bus_num	= 0,
		.chip_select	= 1,
		.platform_data	= &ads_info,
		.irq		= AT32_EXTINT(TS_IRQ),
	},
};
#endif

/* GPIO Keys: left, right, power, etc */
static const struct gpio_keys_button rmt_gpio_keys_buttons[] = {
	[0] = {
		.type		= EV_KEY,
		.code		= KEY_POWER,
		.gpio		= PIN_PWR_SW_N,
		.active_low	= 1,
		.desc		= "power button",
	},
	[1] = {
		.type		= EV_KEY,
		.code		= KEY_LEFT,
		.gpio		= PIN_PB_LEFT,
		.active_low	= 1,
		.desc		= "left button",
	},
	[2] = {
		.type		= EV_KEY,
		.code		= KEY_RIGHT,
		.gpio		= PIN_PB_RIGHT,
		.active_low	= 1,
		.desc		= "right button",
	},
};

static const struct gpio_keys_platform_data rmt_gpio_keys_data = {
	.nbuttons =	ARRAY_SIZE(rmt_gpio_keys_buttons),
	.buttons =	(void *) rmt_gpio_keys_buttons,
};

static struct platform_device rmt_gpio_keys = {
	.name =		"gpio-keys",
	.id =		-1,
	.dev = {
		.platform_data = (void *) &rmt_gpio_keys_data,
	}
};

#ifdef CONFIG_BOARD_MRMT_RTC_I2C
static struct i2c_board_info __initdata mrmt1_i2c_rtc = {
	I2C_BOARD_INFO("s35390a", 0x30),
	.irq		= 0,
};
#endif

static void mrmt_power_off(void)
{
	/* PWR_ON=0 will force power off */
	gpio_set_value( PIN_PWR_ON, 0 );
}

static int __init mrmt1_init(void)
{
	gpio_set_value( PIN_PWR_ON, 1 );	/* Ensure PWR_ON is enabled */

	pm_power_off = mrmt_power_off;

	/* Setup USARTS (other than console) */
	at32_map_usart(2, 1, 0);	/* USART 2: /dev/ttyS1, RMT1:DB9M */
	at32_map_usart(3, 2, ATMEL_USART_RTS | ATMEL_USART_CTS);
			/* USART 3: /dev/ttyS2, RMT1:Wireless, w/ RTS/CTS */
	at32_add_device_usart(1);
	at32_add_device_usart(2);

	/* Select GPIO Key pins */
	at32_select_gpio( PIN_PWR_SW_N, AT32_GPIOF_DEGLITCH);
	at32_select_gpio( PIN_PB_LEFT, AT32_GPIOF_DEGLITCH);
	at32_select_gpio( PIN_PB_RIGHT, AT32_GPIOF_DEGLITCH);
	platform_device_register(&rmt_gpio_keys);

#ifdef CONFIG_BOARD_MRMT_RTC_I2C
	i2c_register_board_info(0, &mrmt1_i2c_rtc, 1);
#endif

#ifndef CONFIG_BOARD_MRMT_LCD_DISABLE
	/* User "alternate" LCDC inferface on Port E & D */
	/* NB: exclude LCDC_CC pin, as NGW100 reserves it for other use */
	at32_add_device_lcdc(0, &rmt_lcdc_data,
		fbmem_start, fbmem_size,
		(ATMEL_LCDC_ALT_24BIT | ATMEL_LCDC_PE_DVAL ) );
#endif

#ifdef CONFIG_BOARD_MRMT_AC97
	at32_add_device_ac97c(0, &ac97c0_data, AC97C_BOTH);
#endif

#ifdef CONFIG_BOARD_MRMT_ADS7846_TS
	/* Select the Touchscreen interrupt pin mode */
	at32_select_periph( GPIO_PIOB_BASE, 1 << (PB_EXTINT_BASE+TS_IRQ),
			GPIO_PERIPH_A, AT32_GPIOF_DEGLITCH);
	irq_set_irq_type(AT32_EXTINT(TS_IRQ), IRQ_TYPE_EDGE_FALLING);
	at32_spi_setup_slaves(0,spi01_board_info,ARRAY_SIZE(spi01_board_info));
	spi_register_board_info(spi01_board_info,ARRAY_SIZE(spi01_board_info));
#endif

#ifdef CONFIG_BOARD_MRMT_UCB1400_TS
	/* Select the Touchscreen interrupt pin mode */
	at32_select_periph( GPIO_PIOB_BASE, 1 << (PB_EXTINT_BASE+TS_IRQ),
			GPIO_PERIPH_A, AT32_GPIOF_DEGLITCH);
	platform_device_register(&rmt_ts_device);
#endif

	at32_select_gpio( PIN_LCD_DISP, AT32_GPIOF_OUTPUT );
	gpio_request( PIN_LCD_DISP, "LCD_DISP" );
	gpio_direction_output( PIN_LCD_DISP, 0 );	/* LCD DISP */
#ifdef CONFIG_BOARD_MRMT_LCD_DISABLE
	/* Keep Backlight and DISP off */
	at32_select_gpio( PIN_LCD_BL, AT32_GPIOF_OUTPUT );
	gpio_request( PIN_LCD_BL, "LCD_BL" );
	gpio_direction_output( PIN_LCD_BL, 0 );		/* Backlight */
#else
	gpio_set_value( PIN_LCD_DISP, 1 );	/* DISP asserted first */
#ifdef CONFIG_BOARD_MRMT_BL_PWM
	/* Use PWM for Backlight controls */
	at32_add_device_pwm(1 << PWM_CH_BL);
	platform_device_register(&rmt_pwm_led_dev);
#else
	/* Backlight always on */
	udelay( 1 );
	at32_select_gpio( PIN_LCD_BL, AT32_GPIOF_OUTPUT );
	gpio_request( PIN_LCD_BL, "LCD_BL" );
	gpio_direction_output( PIN_LCD_BL, 1 );
#endif
#endif

	/* Make sure BT and Zigbee modules in reset */
	at32_select_gpio( PIN_BT_RST, AT32_GPIOF_OUTPUT );
	gpio_request( PIN_BT_RST, "BT_RST" );
	gpio_direction_output( PIN_BT_RST, 1 );
	/* BT Module in Reset */

	at32_select_gpio( PIN_ZB_RST_N, AT32_GPIOF_OUTPUT );
	gpio_request( PIN_ZB_RST_N, "ZB_RST_N" );
	gpio_direction_output( PIN_ZB_RST_N, 0 );
	/* XBee Module in Reset */

#ifdef CONFIG_BOARD_MRMT_WIRELESS_ZB
	udelay( 1000 );
	/* Unreset the XBee Module */
	gpio_set_value( PIN_ZB_RST_N, 1 );
#endif
#ifdef CONFIG_BOARD_MRMT_WIRELESS_BT
	udelay( 1000 );
	/* Unreset the BT Module */
	gpio_set_value( PIN_BT_RST, 0 );
#endif

	return 0;
}
arch_initcall(mrmt1_init);

static int __init mrmt1_early_init(void)
{
	/* To maintain power-on signal in case boot loader did not already */
	at32_select_gpio( PIN_PWR_ON, AT32_GPIOF_OUTPUT );
	gpio_request( PIN_PWR_ON, "PIN_PWR_ON" );
	gpio_direction_output( PIN_PWR_ON, 1 );

	return 0;
}
core_initcall(mrmt1_early_init);
