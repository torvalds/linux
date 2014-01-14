/*
 * Copyright (c) 2006-2009 Victor Chukhantsev, Denis Grigoriev,
 * Copyright (c) 2007-2010 Vasily Khoruzhick
 *
 * based on smdk2440 written by Ben Dooks
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
#include <linux/memblock.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/device.h>
#include <linux/pda_power.h>
#include <linux/pwm_backlight.h>
#include <linux/pwm.h>
#include <linux/s3c_adc_battery.h>
#include <linux/leds.h>
#include <linux/i2c.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <linux/mmc/host.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/platform_data/i2c-s3c2410.h>
#include <linux/platform_data/mmc-s3cmci.h>
#include <linux/platform_data/mtd-nand-s3c2410.h>
#include <linux/platform_data/touchscreen-s3c2410.h>
#include <linux/platform_data/usb-s3c2410_udc.h>

#include <sound/uda1380.h>

#include <mach/fb.h>
#include <mach/regs-gpio.h>
#include <mach/regs-lcd.h>
#include <mach/gpio-samsung.h>

#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/pm.h>
#include <plat/regs-serial.h>
#include <plat/samsung-time.h>
#include <plat/gpio-cfg.h>

#include "common.h"
#include "h1940.h"

#define LCD_PWM_PERIOD 192960
#define LCD_PWM_DUTY 127353

static struct map_desc rx1950_iodesc[] __initdata = {
};

static struct s3c2410_uartcfg rx1950_uartcfgs[] __initdata = {
	[0] = {
	       .hwport = 0,
	       .flags = 0,
	       .ucon = 0x3c5,
	       .ulcon = 0x03,
	       .ufcon = 0x51,
		.clk_sel = S3C2410_UCON_CLKSEL3,
	},
	[1] = {
	       .hwport = 1,
	       .flags = 0,
	       .ucon = 0x3c5,
	       .ulcon = 0x03,
	       .ufcon = 0x51,
		.clk_sel = S3C2410_UCON_CLKSEL3,
	},
	/* IR port */
	[2] = {
	       .hwport = 2,
	       .flags = 0,
	       .ucon = 0x3c5,
	       .ulcon = 0x43,
	       .ufcon = 0xf1,
		.clk_sel = S3C2410_UCON_CLKSEL3,
	},
};

static struct s3c2410fb_display rx1950_display = {
	.type = S3C2410_LCDCON1_TFT,
	.width = 240,
	.height = 320,
	.xres = 240,
	.yres = 320,
	.bpp = 16,

	.pixclock = 260000,
	.left_margin = 10,
	.right_margin = 20,
	.hsync_len = 10,
	.upper_margin = 2,
	.lower_margin = 2,
	.vsync_len = 2,

	.lcdcon5 = S3C2410_LCDCON5_FRM565 |
			   S3C2410_LCDCON5_INVVCLK |
			   S3C2410_LCDCON5_INVVLINE |
			   S3C2410_LCDCON5_INVVFRAME |
			   S3C2410_LCDCON5_HWSWP |
			   (0x02 << 13) |
			   (0x02 << 15),

};

static int power_supply_init(struct device *dev)
{
	return gpio_request(S3C2410_GPF(2), "cable plugged");
}

static int rx1950_is_ac_online(void)
{
	return !gpio_get_value(S3C2410_GPF(2));
}

static void power_supply_exit(struct device *dev)
{
	gpio_free(S3C2410_GPF(2));
}

static char *rx1950_supplicants[] = {
	"main-battery"
};

static struct pda_power_pdata power_supply_info = {
	.init			= power_supply_init,
	.is_ac_online		= rx1950_is_ac_online,
	.exit			= power_supply_exit,
	.supplied_to		= rx1950_supplicants,
	.num_supplicants	= ARRAY_SIZE(rx1950_supplicants),
};

static struct resource power_supply_resources[] = {
	[0] = DEFINE_RES_NAMED(IRQ_EINT2, 1, "ac", IORESOURCE_IRQ \
			| IORESOURCE_IRQ_LOWEDGE | IORESOURCE_IRQ_HIGHEDGE),
};

static struct platform_device power_supply = {
	.name			= "pda-power",
	.id			= -1,
	.dev			= {
					.platform_data =
						&power_supply_info,
	},
	.resource		= power_supply_resources,
	.num_resources		= ARRAY_SIZE(power_supply_resources),
};

static const struct s3c_adc_bat_thresh bat_lut_noac[] = {
	{ .volt = 4100, .cur = 156, .level = 100},
	{ .volt = 4050, .cur = 156, .level = 95},
	{ .volt = 4025, .cur = 141, .level = 90},
	{ .volt = 3995, .cur = 144, .level = 85},
	{ .volt = 3957, .cur = 162, .level = 80},
	{ .volt = 3931, .cur = 147, .level = 75},
	{ .volt = 3902, .cur = 147, .level = 70},
	{ .volt = 3863, .cur = 153, .level = 65},
	{ .volt = 3838, .cur = 150, .level = 60},
	{ .volt = 3800, .cur = 153, .level = 55},
	{ .volt = 3765, .cur = 153, .level = 50},
	{ .volt = 3748, .cur = 172, .level = 45},
	{ .volt = 3740, .cur = 153, .level = 40},
	{ .volt = 3714, .cur = 175, .level = 35},
	{ .volt = 3710, .cur = 156, .level = 30},
	{ .volt = 3963, .cur = 156, .level = 25},
	{ .volt = 3672, .cur = 178, .level = 20},
	{ .volt = 3651, .cur = 178, .level = 15},
	{ .volt = 3629, .cur = 178, .level = 10},
	{ .volt = 3612, .cur = 162, .level = 5},
	{ .volt = 3605, .cur = 162, .level = 0},
};

static const struct s3c_adc_bat_thresh bat_lut_acin[] = {
	{ .volt = 4200, .cur = 0, .level = 100},
	{ .volt = 4190, .cur = 0, .level = 99},
	{ .volt = 4178, .cur = 0, .level = 95},
	{ .volt = 4110, .cur = 0, .level = 70},
	{ .volt = 4076, .cur = 0, .level = 65},
	{ .volt = 4046, .cur = 0, .level = 60},
	{ .volt = 4021, .cur = 0, .level = 55},
	{ .volt = 3999, .cur = 0, .level = 50},
	{ .volt = 3982, .cur = 0, .level = 45},
	{ .volt = 3965, .cur = 0, .level = 40},
	{ .volt = 3957, .cur = 0, .level = 35},
	{ .volt = 3948, .cur = 0, .level = 30},
	{ .volt = 3936, .cur = 0, .level = 25},
	{ .volt = 3927, .cur = 0, .level = 20},
	{ .volt = 3906, .cur = 0, .level = 15},
	{ .volt = 3880, .cur = 0, .level = 10},
	{ .volt = 3829, .cur = 0, .level = 5},
	{ .volt = 3820, .cur = 0, .level = 0},
};

static int rx1950_bat_init(void)
{
	int ret;

	ret = gpio_request(S3C2410_GPJ(2), "rx1950-charger-enable-1");
	if (ret)
		goto err_gpio1;
	ret = gpio_request(S3C2410_GPJ(3), "rx1950-charger-enable-2");
	if (ret)
		goto err_gpio2;

	return 0;

err_gpio2:
	gpio_free(S3C2410_GPJ(2));
err_gpio1:
	return ret;
}

static void rx1950_bat_exit(void)
{
	gpio_free(S3C2410_GPJ(2));
	gpio_free(S3C2410_GPJ(3));
}

static void rx1950_enable_charger(void)
{
	gpio_direction_output(S3C2410_GPJ(2), 1);
	gpio_direction_output(S3C2410_GPJ(3), 1);
}

static void rx1950_disable_charger(void)
{
	gpio_direction_output(S3C2410_GPJ(2), 0);
	gpio_direction_output(S3C2410_GPJ(3), 0);
}

static DEFINE_SPINLOCK(rx1950_blink_spin);

static int rx1950_led_blink_set(unsigned gpio, int state,
	unsigned long *delay_on, unsigned long *delay_off)
{
	int blink_gpio, check_gpio;

	switch (gpio) {
	case S3C2410_GPA(6):
		blink_gpio = S3C2410_GPA(4);
		check_gpio = S3C2410_GPA(3);
		break;
	case S3C2410_GPA(7):
		blink_gpio = S3C2410_GPA(3);
		check_gpio = S3C2410_GPA(4);
		break;
	default:
		return -EINVAL;
		break;
	}

	if (delay_on && delay_off && !*delay_on && !*delay_off)
		*delay_on = *delay_off = 500;

	spin_lock(&rx1950_blink_spin);

	switch (state) {
	case GPIO_LED_NO_BLINK_LOW:
	case GPIO_LED_NO_BLINK_HIGH:
		if (!gpio_get_value(check_gpio))
			gpio_set_value(S3C2410_GPJ(6), 0);
		gpio_set_value(blink_gpio, 0);
		gpio_set_value(gpio, state);
		break;
	case GPIO_LED_BLINK:
		gpio_set_value(gpio, 0);
		gpio_set_value(S3C2410_GPJ(6), 1);
		gpio_set_value(blink_gpio, 1);
		break;
	}

	spin_unlock(&rx1950_blink_spin);

	return 0;
}

static struct gpio_led rx1950_leds_desc[] = {
	{
		.name			= "Green",
		.default_trigger	= "main-battery-full",
		.gpio			= S3C2410_GPA(6),
		.retain_state_suspended	= 1,
	},
	{
		.name			= "Red",
		.default_trigger
			= "main-battery-charging-blink-full-solid",
		.gpio			= S3C2410_GPA(7),
		.retain_state_suspended	= 1,
	},
	{
		.name			= "Blue",
		.default_trigger	= "rx1950-acx-mem",
		.gpio			= S3C2410_GPA(11),
		.retain_state_suspended	= 1,
	},
};

static struct gpio_led_platform_data rx1950_leds_pdata = {
	.num_leds	= ARRAY_SIZE(rx1950_leds_desc),
	.leds		= rx1950_leds_desc,
	.gpio_blink_set	= rx1950_led_blink_set,
};

static struct platform_device rx1950_leds = {
	.name	= "leds-gpio",
	.id		= -1,
	.dev	= {
				.platform_data = &rx1950_leds_pdata,
	},
};

static struct s3c_adc_bat_pdata rx1950_bat_cfg = {
	.init = rx1950_bat_init,
	.exit = rx1950_bat_exit,
	.enable_charger = rx1950_enable_charger,
	.disable_charger = rx1950_disable_charger,
	.gpio_charge_finished = S3C2410_GPF(3),
	.lut_noac = bat_lut_noac,
	.lut_noac_cnt = ARRAY_SIZE(bat_lut_noac),
	.lut_acin = bat_lut_acin,
	.lut_acin_cnt = ARRAY_SIZE(bat_lut_acin),
	.volt_channel = 0,
	.current_channel = 1,
	.volt_mult = 4235,
	.current_mult = 2900,
	.internal_impedance = 200,
};

static struct platform_device rx1950_battery = {
	.name             = "s3c-adc-battery",
	.id               = -1,
	.dev = {
		.parent = &s3c_device_adc.dev,
		.platform_data = &rx1950_bat_cfg,
	},
};

static struct s3c2410fb_mach_info rx1950_lcd_cfg = {
	.displays = &rx1950_display,
	.num_displays = 1,
	.default_display = 0,

	.lpcsel = 0x02,
	.gpccon = 0xaa9556a9,
	.gpccon_mask = 0xffc003fc,
	.gpcup = 0x0000ffff,
	.gpcup_mask = 0xffffffff,

	.gpdcon = 0xaa90aaa1,
	.gpdcon_mask = 0xffc0fff0,
	.gpdup = 0x0000fcfd,
	.gpdup_mask = 0xffffffff,

};

static struct pwm_device *lcd_pwm;

static void rx1950_lcd_power(int enable)
{
	int i;
	static int enabled;
	if (enabled == enable)
		return;
	if (!enable) {

		/* GPC11-GPC15->OUTPUT */
		for (i = 11; i < 16; i++)
			gpio_direction_output(S3C2410_GPC(i), 1);

		/* Wait a bit here... */
		mdelay(100);

		/* GPD2-GPD7->OUTPUT */
		/* GPD11-GPD15->OUTPUT */
		/* GPD2-GPD7->1, GPD11-GPD15->1 */
		for (i = 2; i < 8; i++)
			gpio_direction_output(S3C2410_GPD(i), 1);
		for (i = 11; i < 16; i++)
			gpio_direction_output(S3C2410_GPD(i), 1);

		/* Wait a bit here...*/
		mdelay(100);

		/* GPB0->OUTPUT, GPB0->0 */
		gpio_direction_output(S3C2410_GPB(0), 0);

		/* GPC1-GPC4->OUTPUT, GPC1-4->0 */
		for (i = 1; i < 5; i++)
			gpio_direction_output(S3C2410_GPC(i), 0);

		/* GPC15-GPC11->0 */
		for (i = 11; i < 16; i++)
			gpio_direction_output(S3C2410_GPC(i), 0);

		/* GPD15-GPD11->0, GPD2->GPD7->0 */
		for (i = 11; i < 16; i++)
			gpio_direction_output(S3C2410_GPD(i), 0);

		for (i = 2; i < 8; i++)
			gpio_direction_output(S3C2410_GPD(i), 0);

		/* GPC6->0, GPC7->0, GPC5->0 */
		gpio_direction_output(S3C2410_GPC(6), 0);
		gpio_direction_output(S3C2410_GPC(7), 0);
		gpio_direction_output(S3C2410_GPC(5), 0);

		/* GPB1->OUTPUT, GPB1->0 */
		gpio_direction_output(S3C2410_GPB(1), 0);
		pwm_config(lcd_pwm, 0, LCD_PWM_PERIOD);
		pwm_disable(lcd_pwm);

		/* GPC0->0, GPC10->0 */
		gpio_direction_output(S3C2410_GPC(0), 0);
		gpio_direction_output(S3C2410_GPC(10), 0);
	} else {
		pwm_config(lcd_pwm, LCD_PWM_DUTY, LCD_PWM_PERIOD);
		pwm_enable(lcd_pwm);

		gpio_direction_output(S3C2410_GPC(0), 1);
		gpio_direction_output(S3C2410_GPC(5), 1);

		s3c_gpio_cfgpin(S3C2410_GPB(1), S3C2410_GPB1_TOUT1);
		gpio_direction_output(S3C2410_GPC(7), 1);

		for (i = 1; i < 5; i++)
			s3c_gpio_cfgpin(S3C2410_GPC(i), S3C_GPIO_SFN(2));

		for (i = 11; i < 16; i++)
			s3c_gpio_cfgpin(S3C2410_GPC(i), S3C_GPIO_SFN(2));

		for (i = 2; i < 8; i++)
			s3c_gpio_cfgpin(S3C2410_GPD(i), S3C_GPIO_SFN(2));

		for (i = 11; i < 16; i++)
			s3c_gpio_cfgpin(S3C2410_GPD(i), S3C_GPIO_SFN(2));

		gpio_direction_output(S3C2410_GPC(10), 1);
		gpio_direction_output(S3C2410_GPC(6), 1);
	}
	enabled = enable;
}

static void rx1950_bl_power(int enable)
{
	static int enabled;
	if (enabled == enable)
		return;
	if (!enable) {
			gpio_direction_output(S3C2410_GPB(0), 0);
	} else {
			/* LED driver need a "push" to power on */
			gpio_direction_output(S3C2410_GPB(0), 1);
			/* Warm up backlight for one period of PWM.
			 * Without this trick its almost impossible to
			 * enable backlight with low brightness value
			 */
			ndelay(48000);
			s3c_gpio_cfgpin(S3C2410_GPB(0), S3C2410_GPB0_TOUT0);
	}
	enabled = enable;
}

static int rx1950_backlight_init(struct device *dev)
{
	WARN_ON(gpio_request(S3C2410_GPB(0), "Backlight"));
	lcd_pwm = pwm_request(1, "RX1950 LCD");
	if (IS_ERR(lcd_pwm)) {
		dev_err(dev, "Unable to request PWM for LCD power!\n");
		return PTR_ERR(lcd_pwm);
	}

	rx1950_lcd_power(1);
	rx1950_bl_power(1);

	return 0;
}

static void rx1950_backlight_exit(struct device *dev)
{
	rx1950_bl_power(0);
	rx1950_lcd_power(0);

	pwm_free(lcd_pwm);
	gpio_free(S3C2410_GPB(0));
}


static int rx1950_backlight_notify(struct device *dev, int brightness)
{
	if (!brightness) {
		rx1950_bl_power(0);
		rx1950_lcd_power(0);
	} else {
		rx1950_lcd_power(1);
		rx1950_bl_power(1);
	}
	return brightness;
}

static struct platform_pwm_backlight_data rx1950_backlight_data = {
	.pwm_id = 0,
	.max_brightness = 24,
	.dft_brightness = 4,
	.pwm_period_ns = 48000,
	.enable_gpio = -1,
	.init = rx1950_backlight_init,
	.notify = rx1950_backlight_notify,
	.exit = rx1950_backlight_exit,
};

static struct platform_device rx1950_backlight = {
	.name = "pwm-backlight",
	.dev = {
		.parent = &samsung_device_pwm.dev,
		.platform_data = &rx1950_backlight_data,
	},
};

static void rx1950_set_mmc_power(unsigned char power_mode, unsigned short vdd)
{
	switch (power_mode) {
	case MMC_POWER_OFF:
		gpio_direction_output(S3C2410_GPJ(1), 0);
		break;
	case MMC_POWER_UP:
	case MMC_POWER_ON:
		gpio_direction_output(S3C2410_GPJ(1), 1);
		break;
	default:
		break;
	}
}

static struct s3c24xx_mci_pdata rx1950_mmc_cfg __initdata = {
	.gpio_detect = S3C2410_GPF(5),
	.gpio_wprotect = S3C2410_GPH(8),
	.set_power = rx1950_set_mmc_power,
	.ocr_avail = MMC_VDD_32_33,
};

static struct mtd_partition rx1950_nand_part[] = {
	[0] = {
			.name = "Boot0",
			.offset = 0,
			.size = 0x4000,
			.mask_flags = MTD_WRITEABLE,
	},
	[1] = {
			.name = "Boot1",
			.offset = MTDPART_OFS_APPEND,
			.size = 0x40000,
			.mask_flags = MTD_WRITEABLE,
	},
	[2] = {
			.name = "Kernel",
			.offset = MTDPART_OFS_APPEND,
			.size = 0x300000,
			.mask_flags = 0,
	},
	[3] = {
			.name = "Filesystem",
			.offset = MTDPART_OFS_APPEND,
			.size = MTDPART_SIZ_FULL,
			.mask_flags = 0,
	},
};

static struct s3c2410_nand_set rx1950_nand_sets[] = {
	[0] = {
			.name = "Internal",
			.nr_chips = 1,
			.nr_partitions = ARRAY_SIZE(rx1950_nand_part),
			.partitions = rx1950_nand_part,
	},
};

static struct s3c2410_platform_nand rx1950_nand_info = {
	.tacls = 25,
	.twrph0 = 50,
	.twrph1 = 15,
	.nr_sets = ARRAY_SIZE(rx1950_nand_sets),
	.sets = rx1950_nand_sets,
};

static struct s3c2410_udc_mach_info rx1950_udc_cfg __initdata = {
	.vbus_pin = S3C2410_GPG(5),
	.vbus_pin_inverted = 1,
	.pullup_pin = S3C2410_GPJ(5),
};

static struct s3c2410_ts_mach_info rx1950_ts_cfg __initdata = {
	.delay = 10000,
	.presc = 49,
	.oversampling_shift = 3,
};

static struct gpio_keys_button rx1950_gpio_keys_table[] = {
	{
		.code		= KEY_POWER,
		.gpio		= S3C2410_GPF(0),
		.active_low	= 1,
		.desc		= "Power button",
		.wakeup		= 1,
	},
	{
		.code		= KEY_F5,
		.gpio		= S3C2410_GPF(7),
		.active_low	= 1,
		.desc		= "Record button",
	},
	{
		.code		= KEY_F1,
		.gpio		= S3C2410_GPG(0),
		.active_low	= 1,
		.desc		= "Calendar button",
	},
	{
		.code		= KEY_F2,
		.gpio		= S3C2410_GPG(2),
		.active_low	= 1,
		.desc		= "Contacts button",
	},
	{
		.code		= KEY_F3,
		.gpio		= S3C2410_GPG(3),
		.active_low	= 1,
		.desc		= "Mail button",
	},
	{
		.code		= KEY_F4,
		.gpio		= S3C2410_GPG(7),
		.active_low	= 1,
		.desc		= "WLAN button",
	},
	{
		.code		= KEY_LEFT,
		.gpio		= S3C2410_GPG(10),
		.active_low	= 1,
		.desc		= "Left button",
	},
	{
		.code		= KEY_RIGHT,
		.gpio		= S3C2410_GPG(11),
		.active_low	= 1,
		.desc		= "Right button",
	},
	{
		.code		= KEY_UP,
		.gpio		= S3C2410_GPG(4),
		.active_low	= 1,
		.desc		= "Up button",
	},
	{
		.code		= KEY_DOWN,
		.gpio		= S3C2410_GPG(6),
		.active_low	= 1,
		.desc		= "Down button",
	},
	{
		.code		= KEY_ENTER,
		.gpio		= S3C2410_GPG(9),
		.active_low	= 1,
		.desc		= "Ok button"
	},
};

static struct gpio_keys_platform_data rx1950_gpio_keys_data = {
	.buttons = rx1950_gpio_keys_table,
	.nbuttons = ARRAY_SIZE(rx1950_gpio_keys_table),
};

static struct platform_device rx1950_device_gpiokeys = {
	.name = "gpio-keys",
	.dev.platform_data = &rx1950_gpio_keys_data,
};

static struct uda1380_platform_data uda1380_info = {
	.gpio_power	= S3C2410_GPJ(0),
	.gpio_reset	= S3C2410_GPD(0),
	.dac_clk	= UDA1380_DAC_CLK_SYSCLK,
};

static struct i2c_board_info rx1950_i2c_devices[] = {
	{
		I2C_BOARD_INFO("uda1380", 0x1a),
		.platform_data = &uda1380_info,
	},
};

static struct platform_device *rx1950_devices[] __initdata = {
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_iis,
	&s3c_device_usbgadget,
	&s3c_device_rtc,
	&s3c_device_nand,
	&s3c_device_sdi,
	&s3c_device_adc,
	&s3c_device_ts,
	&samsung_device_pwm,
	&rx1950_backlight,
	&rx1950_device_gpiokeys,
	&power_supply,
	&rx1950_battery,
	&rx1950_leds,
};

static struct clk *rx1950_clocks[] __initdata = {
	&s3c24xx_clkout0,
	&s3c24xx_clkout1,
};

static void __init rx1950_map_io(void)
{
	s3c24xx_clkout0.parent  = &clk_h;
	s3c24xx_clkout1.parent  = &clk_f;

	s3c24xx_register_clocks(rx1950_clocks, ARRAY_SIZE(rx1950_clocks));

	s3c24xx_init_io(rx1950_iodesc, ARRAY_SIZE(rx1950_iodesc));
	s3c24xx_init_clocks(16934000);
	s3c24xx_init_uarts(rx1950_uartcfgs, ARRAY_SIZE(rx1950_uartcfgs));
	samsung_set_timer_source(SAMSUNG_PWM3, SAMSUNG_PWM4);

	/* setup PM */

#ifdef CONFIG_PM_H1940
	memcpy(phys_to_virt(H1940_SUSPEND_RESUMEAT), h1940_pm_return, 8);
#endif

	s3c_pm_init();
}

static void __init rx1950_init_machine(void)
{
	int i;

	s3c24xx_fb_set_platdata(&rx1950_lcd_cfg);
	s3c24xx_udc_set_platdata(&rx1950_udc_cfg);
	s3c24xx_ts_set_platdata(&rx1950_ts_cfg);
	s3c24xx_mci_set_platdata(&rx1950_mmc_cfg);
	s3c_i2c0_set_platdata(NULL);
	s3c_nand_set_platdata(&rx1950_nand_info);

	/* Turn off suspend on both USB ports, and switch the
	 * selectable USB port to USB device mode. */
	s3c2410_modify_misccr(S3C2410_MISCCR_USBHOST |
						S3C2410_MISCCR_USBSUSPND0 |
						S3C2410_MISCCR_USBSUSPND1, 0x0);

	/* mmc power is disabled by default */
	WARN_ON(gpio_request(S3C2410_GPJ(1), "MMC power"));
	gpio_direction_output(S3C2410_GPJ(1), 0);

	for (i = 0; i < 8; i++)
		WARN_ON(gpio_request(S3C2410_GPC(i), "LCD power"));

	for (i = 10; i < 16; i++)
		WARN_ON(gpio_request(S3C2410_GPC(i), "LCD power"));

	for (i = 2; i < 8; i++)
		WARN_ON(gpio_request(S3C2410_GPD(i), "LCD power"));

	for (i = 11; i < 16; i++)
		WARN_ON(gpio_request(S3C2410_GPD(i), "LCD power"));

	WARN_ON(gpio_request(S3C2410_GPB(1), "LCD power"));

	WARN_ON(gpio_request(S3C2410_GPA(3), "Red blink"));
	WARN_ON(gpio_request(S3C2410_GPA(4), "Green blink"));
	WARN_ON(gpio_request(S3C2410_GPJ(6), "LED blink"));
	gpio_direction_output(S3C2410_GPA(3), 0);
	gpio_direction_output(S3C2410_GPA(4), 0);
	gpio_direction_output(S3C2410_GPJ(6), 0);

	platform_add_devices(rx1950_devices, ARRAY_SIZE(rx1950_devices));

	i2c_register_board_info(0, rx1950_i2c_devices,
		ARRAY_SIZE(rx1950_i2c_devices));
}

/* H1940 and RX3715 need to reserve this for suspend */
static void __init rx1950_reserve(void)
{
	memblock_reserve(0x30003000, 0x1000);
	memblock_reserve(0x30081000, 0x1000);
}

MACHINE_START(RX1950, "HP iPAQ RX1950")
    /* Maintainers: Vasily Khoruzhick */
	.atag_offset = 0x100,
	.map_io = rx1950_map_io,
	.reserve	= rx1950_reserve,
	.init_irq	= s3c2442_init_irq,
	.init_machine = rx1950_init_machine,
	.init_time	= samsung_timer_init,
	.restart	= s3c244x_restart,
MACHINE_END
