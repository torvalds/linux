/* linux/arch/arm/mach-s3c2410/mach-h1940.c
 *
 * Copyright (c) 2003-2005 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.handhelds.org/projects/h1940.html
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
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/pwm_backlight.h>
#include <linux/i2c.h>
#include <video/platform_lcd.h>

#include <linux/mmc/host.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <plat/regs-serial.h>
#include <mach/regs-lcd.h>
#include <mach/regs-clock.h>

#include <mach/regs-gpio.h>
#include <mach/gpio-fns.h>
#include <mach/gpio-nrs.h>

#include <mach/h1940.h>
#include <mach/h1940-latch.h>
#include <mach/fb.h>
#include <plat/udc.h>
#include <plat/iic.h>

#include <plat/gpio-cfg.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/pm.h>
#include <plat/mci.h>
#include <plat/ts.h>

#include <sound/uda1380.h>

#define H1940_LATCH		((void __force __iomem *)0xF8000000)

#define H1940_PA_LATCH		S3C2410_CS2

#define H1940_LATCH_BIT(x)	(1 << ((x) + 16 - S3C_GPIO_END))

static struct map_desc h1940_iodesc[] __initdata = {
	[0] = {
		.virtual	= (unsigned long)H1940_LATCH,
		.pfn		= __phys_to_pfn(H1940_PA_LATCH),
		.length		= SZ_16K,
		.type		= MT_DEVICE
	},
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg h1940_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = 0x245,
		.ulcon	     = 0x03,
		.ufcon	     = 0x00,
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.uart_flags  = UPF_CONS_FLOW,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
	}
};

/* Board control latch control */

static unsigned int latch_state;

static void h1940_latch_control(unsigned int clear, unsigned int set)
{
	unsigned long flags;

	local_irq_save(flags);

	latch_state &= ~clear;
	latch_state |= set;

	__raw_writel(latch_state, H1940_LATCH);

	local_irq_restore(flags);
}

static inline int h1940_gpiolib_to_latch(int offset)
{
	return 1 << (offset + 16);
}

static void h1940_gpiolib_latch_set(struct gpio_chip *chip,
					unsigned offset, int value)
{
	int latch_bit = h1940_gpiolib_to_latch(offset);

	h1940_latch_control(value ? 0 : latch_bit,
		value ? latch_bit : 0);
}

static int h1940_gpiolib_latch_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	h1940_gpiolib_latch_set(chip, offset, value);
	return 0;
}

static int h1940_gpiolib_latch_get(struct gpio_chip *chip,
					unsigned offset)
{
	return (latch_state >> (offset + 16)) & 1;
}

struct gpio_chip h1940_latch_gpiochip = {
	.base			= H1940_LATCH_GPIO(0),
	.owner			= THIS_MODULE,
	.label			= "H1940_LATCH",
	.ngpio			= 16,
	.direction_output	= h1940_gpiolib_latch_output,
	.set			= h1940_gpiolib_latch_set,
	.get			= h1940_gpiolib_latch_get,
};

static void h1940_udc_pullup(enum s3c2410_udc_cmd_e cmd)
{
	printk(KERN_DEBUG "udc: pullup(%d)\n",cmd);

	switch (cmd)
	{
		case S3C2410_UDC_P_ENABLE :
			gpio_set_value(H1940_LATCH_USB_DP, 1);
			break;
		case S3C2410_UDC_P_DISABLE :
			gpio_set_value(H1940_LATCH_USB_DP, 0);
			break;
		case S3C2410_UDC_P_RESET :
			break;
		default:
			break;
	}
}

static struct s3c2410_udc_mach_info h1940_udc_cfg __initdata = {
	.udc_command		= h1940_udc_pullup,
	.vbus_pin		= S3C2410_GPG(5),
	.vbus_pin_inverted	= 1,
};

static struct s3c2410_ts_mach_info h1940_ts_cfg __initdata = {
		.delay = 10000,
		.presc = 49,
		.oversampling_shift = 2,
		.cfg_gpio = s3c24xx_ts_cfg_gpio,
};

/**
 * Set lcd on or off
 **/
static struct s3c2410fb_display h1940_lcd __initdata = {
	.lcdcon5=	S3C2410_LCDCON5_FRM565 | \
			S3C2410_LCDCON5_INVVLINE | \
			S3C2410_LCDCON5_HWSWP,

	.type =		S3C2410_LCDCON1_TFT,
	.width =	240,
	.height =	320,
	.pixclock =	260000,
	.xres =		240,
	.yres =		320,
	.bpp =		16,
	.left_margin =	8,
	.right_margin =	20,
	.hsync_len =	4,
	.upper_margin =	8,
	.lower_margin = 7,
	.vsync_len =	1,
};

static struct s3c2410fb_mach_info h1940_fb_info __initdata = {
	.displays = &h1940_lcd,
	.num_displays = 1,
	.default_display = 0,

	.lpcsel=	0x02,
	.gpccon=	0xaa940659,
	.gpccon_mask=	0xffffffff,
	.gpcup=		0x0000ffff,
	.gpcup_mask=	0xffffffff,
	.gpdcon=	0xaa84aaa0,
	.gpdcon_mask=	0xffffffff,
	.gpdup=		0x0000faff,
	.gpdup_mask=	0xffffffff,
};

static struct platform_device h1940_device_leds = {
	.name             = "h1940-leds",
	.id               = -1,
};

static struct platform_device h1940_device_bluetooth = {
	.name             = "h1940-bt",
	.id               = -1,
};

static void h1940_set_mmc_power(unsigned char power_mode, unsigned short vdd)
{
	switch (power_mode) {
	case MMC_POWER_OFF:
		gpio_set_value(H1940_LATCH_SD_POWER, 0);
		break;
	case MMC_POWER_UP:
	case MMC_POWER_ON:
		gpio_set_value(H1940_LATCH_SD_POWER, 1);
		break;
	default:
		break;
	};
}

static struct s3c24xx_mci_pdata h1940_mmc_cfg __initdata = {
	.gpio_detect   = S3C2410_GPF(5),
	.gpio_wprotect = S3C2410_GPH(8),
	.set_power     = h1940_set_mmc_power,
	.ocr_avail     = MMC_VDD_32_33,
};

static int h1940_backlight_init(struct device *dev)
{
	gpio_request(S3C2410_GPB(0), "Backlight");

	gpio_direction_output(S3C2410_GPB(0), 0);
	s3c_gpio_setpull(S3C2410_GPB(0), S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(S3C2410_GPB(0), S3C2410_GPB0_TOUT0);
	gpio_set_value(H1940_LATCH_MAX1698_nSHUTDOWN, 1);

	return 0;
}

static int h1940_backlight_notify(struct device *dev, int brightness)
{
	if (!brightness) {
		gpio_direction_output(S3C2410_GPB(0), 1);
		gpio_set_value(H1940_LATCH_MAX1698_nSHUTDOWN, 0);
	} else {
		gpio_direction_output(S3C2410_GPB(0), 0);
		s3c_gpio_setpull(S3C2410_GPB(0), S3C_GPIO_PULL_NONE);
		s3c_gpio_cfgpin(S3C2410_GPB(0), S3C2410_GPB0_TOUT0);
		gpio_set_value(H1940_LATCH_MAX1698_nSHUTDOWN, 1);
	}
	return brightness;
}

static void h1940_backlight_exit(struct device *dev)
{
	gpio_direction_output(S3C2410_GPB(0), 1);
	gpio_set_value(H1940_LATCH_MAX1698_nSHUTDOWN, 0);
}


static struct platform_pwm_backlight_data backlight_data = {
	.pwm_id         = 0,
	.max_brightness = 100,
	.dft_brightness = 50,
	/* tcnt = 0x31 */
	.pwm_period_ns  = 36296,
	.init           = h1940_backlight_init,
	.notify		= h1940_backlight_notify,
	.exit           = h1940_backlight_exit,
};

static struct platform_device h1940_backlight = {
	.name = "pwm-backlight",
	.dev  = {
		.parent = &s3c_device_timer[0].dev,
		.platform_data = &backlight_data,
	},
	.id   = -1,
};

static void h1940_lcd_power_set(struct plat_lcd_data *pd,
					unsigned int power)
{
	int value;

	if (!power) {
		gpio_set_value(S3C2410_GPC(0), 0);
		/* wait for 3ac */
		do {
			value = gpio_get_value(S3C2410_GPC(6));
		} while (value);

		gpio_set_value(H1940_LATCH_LCD_P2, 0);
		gpio_set_value(H1940_LATCH_LCD_P3, 0);
		gpio_set_value(H1940_LATCH_LCD_P4, 0);

		gpio_direction_output(S3C2410_GPC(1), 0);
		gpio_direction_output(S3C2410_GPC(4), 0);

		gpio_set_value(H1940_LATCH_LCD_P1, 0);
		gpio_set_value(H1940_LATCH_LCD_P0, 0);

		gpio_set_value(S3C2410_GPC(5), 0);

	} else {
		gpio_set_value(H1940_LATCH_LCD_P0, 1);
		gpio_set_value(H1940_LATCH_LCD_P1, 1);

		s3c_gpio_cfgpin(S3C2410_GPC(1), S3C_GPIO_SFN(2));
		s3c_gpio_cfgpin(S3C2410_GPC(4), S3C_GPIO_SFN(2));

		gpio_set_value(S3C2410_GPC(5), 1);
		gpio_set_value(S3C2410_GPC(0), 1);

		gpio_set_value(H1940_LATCH_LCD_P3, 1);
		gpio_set_value(H1940_LATCH_LCD_P2, 1);
		gpio_set_value(H1940_LATCH_LCD_P4, 1);
	}
}

static struct plat_lcd_data h1940_lcd_power_data = {
	.set_power      = h1940_lcd_power_set,
};

static struct platform_device h1940_lcd_powerdev = {
	.name                   = "platform-lcd",
	.dev.parent             = &s3c_device_lcd.dev,
	.dev.platform_data      = &h1940_lcd_power_data,
};

static struct uda1380_platform_data uda1380_info = {
	.gpio_power	= H1940_LATCH_UDA_POWER,
	.gpio_reset	= S3C2410_GPA(12),
	.dac_clk	= UDA1380_DAC_CLK_SYSCLK,
};

static struct i2c_board_info h1940_i2c_devices[] = {
	{
		I2C_BOARD_INFO("uda1380", 0x1a),
		.platform_data = &uda1380_info,
	},
};

static struct platform_device *h1940_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_iis,
	&s3c_device_pcm,
	&s3c_device_usbgadget,
	&h1940_device_leds,
	&h1940_device_bluetooth,
	&s3c_device_sdi,
	&s3c_device_rtc,
	&s3c_device_timer[0],
	&h1940_backlight,
	&h1940_lcd_powerdev,
	&s3c_device_adc,
	&s3c_device_ts,
};

static void __init h1940_map_io(void)
{
	s3c24xx_init_io(h1940_iodesc, ARRAY_SIZE(h1940_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(h1940_uartcfgs, ARRAY_SIZE(h1940_uartcfgs));

	/* setup PM */

#ifdef CONFIG_PM_H1940
	memcpy(phys_to_virt(H1940_SUSPEND_RESUMEAT), h1940_pm_return, 1024);
#endif
	s3c_pm_init();

	/* Add latch gpio chip, set latch initial value */
	h1940_latch_control(0, 0);
	WARN_ON(gpiochip_add(&h1940_latch_gpiochip));
}

/* H1940 and RX3715 need to reserve this for suspend */
static void __init h1940_reserve(void)
{
	memblock_reserve(0x30003000, 0x1000);
	memblock_reserve(0x30081000, 0x1000);
}

static void __init h1940_init_irq(void)
{
	s3c24xx_init_irq();
}

static void __init h1940_init(void)
{
	u32 tmp;

	s3c24xx_fb_set_platdata(&h1940_fb_info);
	s3c24xx_mci_set_platdata(&h1940_mmc_cfg);
 	s3c24xx_udc_set_platdata(&h1940_udc_cfg);
	s3c24xx_ts_set_platdata(&h1940_ts_cfg);
	s3c_i2c0_set_platdata(NULL);

	/* Turn off suspend on both USB ports, and switch the
	 * selectable USB port to USB device mode. */

	s3c2410_modify_misccr(S3C2410_MISCCR_USBHOST |
			      S3C2410_MISCCR_USBSUSPND0 |
			      S3C2410_MISCCR_USBSUSPND1, 0x0);

	tmp =   (0x78 << S3C24XX_PLLCON_MDIVSHIFT)
	      | (0x02 << S3C24XX_PLLCON_PDIVSHIFT)
	      | (0x03 << S3C24XX_PLLCON_SDIVSHIFT);
	writel(tmp, S3C2410_UPLLCON);

	gpio_request(S3C2410_GPC(0), "LCD power");
	gpio_request(S3C2410_GPC(1), "LCD power");
	gpio_request(S3C2410_GPC(4), "LCD power");
	gpio_request(S3C2410_GPC(5), "LCD power");
	gpio_request(S3C2410_GPC(6), "LCD power");
	gpio_request(H1940_LATCH_LCD_P0, "LCD power");
	gpio_request(H1940_LATCH_LCD_P1, "LCD power");
	gpio_request(H1940_LATCH_LCD_P2, "LCD power");
	gpio_request(H1940_LATCH_LCD_P3, "LCD power");
	gpio_request(H1940_LATCH_LCD_P4, "LCD power");
	gpio_request(H1940_LATCH_MAX1698_nSHUTDOWN, "LCD power");
	gpio_direction_output(S3C2410_GPC(0), 0);
	gpio_direction_output(S3C2410_GPC(1), 0);
	gpio_direction_output(S3C2410_GPC(4), 0);
	gpio_direction_output(S3C2410_GPC(5), 0);
	gpio_direction_input(S3C2410_GPC(6));
	gpio_direction_output(H1940_LATCH_LCD_P0, 0);
	gpio_direction_output(H1940_LATCH_LCD_P1, 0);
	gpio_direction_output(H1940_LATCH_LCD_P2, 0);
	gpio_direction_output(H1940_LATCH_LCD_P3, 0);
	gpio_direction_output(H1940_LATCH_LCD_P4, 0);
	gpio_direction_output(H1940_LATCH_MAX1698_nSHUTDOWN, 0);

	gpio_request(H1940_LATCH_USB_DP, "USB pullup");
	gpio_direction_output(H1940_LATCH_USB_DP, 0);

	gpio_request(H1940_LATCH_SD_POWER, "SD power");
	gpio_direction_output(H1940_LATCH_SD_POWER, 0);

	platform_add_devices(h1940_devices, ARRAY_SIZE(h1940_devices));

	i2c_register_board_info(0, h1940_i2c_devices,
		ARRAY_SIZE(h1940_i2c_devices));
}

MACHINE_START(H1940, "IPAQ-H1940")
	/* Maintainer: Ben Dooks <ben-linux@fluff.org> */
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= h1940_map_io,
	.reserve	= h1940_reserve,
	.init_irq	= h1940_init_irq,
	.init_machine	= h1940_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
