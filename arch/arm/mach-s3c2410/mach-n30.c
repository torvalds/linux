/* Machine specific code for the Acer n30, Acer N35, Navman PiN 570,
 * Yakumo AlphaX and Airis NC05 PDAs.
 *
 * Copyright (c) 2003-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Copyright (c) 2005-2008 Christer Weinigel <christer@weinigel.se>
 *
 * There is a wiki with more information about the n30 port at
 * http://handhelds.org/moin/moin.cgi/AcerN30Documentation .
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include <linux/delay.h>
#include <linux/gpio_keys.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/timer.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/arch/leds-gpio.h>
#include <asm/arch/regs-gpio.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include <asm/plat-s3c/iic.h>
#include <asm/plat-s3c/regs-serial.h>

#include <asm/plat-s3c24xx/clock.h>
#include <asm/plat-s3c24xx/cpu.h>
#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c24xx/s3c2410.h>
#include <asm/plat-s3c24xx/udc.h>

static struct map_desc n30_iodesc[] __initdata = {
	/* nothing here yet */
};

static struct s3c2410_uartcfg n30_uartcfgs[] = {
	/* Normal serial port */
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = 0x2c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	/* IR port */
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.uart_flags  = UPF_CONS_FLOW,
		.ucon	     = 0x2c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
	},
	/* On the N30 the bluetooth controller is connected here.
	 * On the N35 and variants the GPS receiver is connected here. */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = 0x2c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
};

static void n30_udc_pullup(enum s3c2410_udc_cmd_e cmd)
{
	switch (cmd)
	{
		case S3C2410_UDC_P_ENABLE :
			s3c2410_gpio_setpin(S3C2410_GPB3, 1);
			break;
		case S3C2410_UDC_P_DISABLE :
			s3c2410_gpio_setpin(S3C2410_GPB3, 0);
			break;
		case S3C2410_UDC_P_RESET :
			break;
		default:
			break;
	}
}

static struct s3c2410_udc_mach_info n30_udc_cfg __initdata = {
	.udc_command		= n30_udc_pullup,
	.vbus_pin		= S3C2410_GPG1,
	.vbus_pin_inverted	= 0,
};

static struct gpio_keys_button n30_buttons[] = {
	{
		.gpio		= S3C2410_GPF0,
		.code		= KEY_POWER,
		.desc		= "Power",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPG9,
		.code		= KEY_UP,
		.desc		= "Thumbwheel Up",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPG8,
		.code		= KEY_DOWN,
		.desc		= "Thumbwheel Down",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPG7,
		.code		= KEY_ENTER,
		.desc		= "Thumbwheel Press",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPF7,
		.code		= KEY_HOMEPAGE,
		.desc		= "Home",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPF6,
		.code		= KEY_CALENDAR,
		.desc		= "Calendar",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPF5,
		.code		= KEY_ADDRESSBOOK,
		.desc		= "Contacts",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPF4,
		.code		= KEY_MAIL,
		.desc		= "Mail",
		.active_low	= 0,
	},
};

static struct gpio_keys_platform_data n30_button_data = {
	.buttons	= n30_buttons,
	.nbuttons	= ARRAY_SIZE(n30_buttons),
};

static struct platform_device n30_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data	= &n30_button_data,
	}
};

static struct gpio_keys_button n35_buttons[] = {
	{
		.gpio		= S3C2410_GPF0,
		.code		= KEY_POWER,
		.desc		= "Power",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPG9,
		.code		= KEY_UP,
		.desc		= "Joystick Up",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPG8,
		.code		= KEY_DOWN,
		.desc		= "Joystick Down",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPG6,
		.code		= KEY_DOWN,
		.desc		= "Joystick Left",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPG5,
		.code		= KEY_DOWN,
		.desc		= "Joystick Right",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPG7,
		.code		= KEY_ENTER,
		.desc		= "Joystick Press",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPF7,
		.code		= KEY_HOMEPAGE,
		.desc		= "Home",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPF6,
		.code		= KEY_CALENDAR,
		.desc		= "Calendar",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPF5,
		.code		= KEY_ADDRESSBOOK,
		.desc		= "Contacts",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPF4,
		.code		= KEY_MAIL,
		.desc		= "Mail",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPF3,
		.code		= SW_RADIO,
		.desc		= "GPS Antenna",
		.active_low	= 0,
	},
	{
		.gpio		= S3C2410_GPG2,
		.code		= SW_HEADPHONE_INSERT,
		.desc		= "Headphone",
		.active_low	= 0,
	},
};

static struct gpio_keys_platform_data n35_button_data = {
	.buttons	= n35_buttons,
	.nbuttons	= ARRAY_SIZE(n35_buttons),
};

static struct platform_device n35_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &n35_button_data,
	}
};

/* This is the bluetooth LED on the device. */
static struct s3c24xx_led_platdata n30_blue_led_pdata = {
	.name		= "blue_led",
	.gpio		= S3C2410_GPG6,
	.def_trigger	= "",
};

/* This LED is driven by the battery microcontroller, and is blinking
 * red, blinking green or solid green when the battery is low,
 * charging or full respectively.  By driving GPD9 low, it's possible
 * to force the LED to blink red, so call that warning LED.  */
static struct s3c24xx_led_platdata n30_warning_led_pdata = {
	.name		= "warning_led",
	.flags          = S3C24XX_LEDF_ACTLOW,
	.gpio		= S3C2410_GPD9,
	.def_trigger	= "",
};

static struct platform_device n30_blue_led = {
	.name		= "s3c24xx_led",
	.id		= 1,
	.dev		= {
		.platform_data	= &n30_blue_led_pdata,
	},
};

static struct platform_device n30_warning_led = {
	.name		= "s3c24xx_led",
	.id		= 2,
	.dev		= {
		.platform_data	= &n30_warning_led_pdata,
	},
};

static struct platform_device *n30_devices[] __initdata = {
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
	&s3c_device_usb,
	&s3c_device_usbgadget,
	&n30_button_device,
	&n30_blue_led,
	&n30_warning_led,
};

static struct platform_device *n35_devices[] __initdata = {
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
	&s3c_device_usbgadget,
	&n35_button_device,
};

static struct s3c2410_platform_i2c n30_i2ccfg = {
	.flags		= 0,
	.slave_addr	= 0x10,
	.bus_freq	= 10*1000,
	.max_freq	= 10*1000,
};

static void __init n30_map_io(void)
{
	s3c24xx_init_io(n30_iodesc, ARRAY_SIZE(n30_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(n30_uartcfgs, ARRAY_SIZE(n30_uartcfgs));
}

static void __init n30_init_irq(void)
{
	s3c24xx_init_irq();
}

/* GPB3 is the line that controls the pull-up for the USB D+ line */

static void __init n30_init(void)
{
	s3c_device_i2c.dev.platform_data = &n30_i2ccfg;
	s3c24xx_udc_set_platdata(&n30_udc_cfg);

	/* Turn off suspend on both USB ports, and switch the
	 * selectable USB port to USB device mode. */

	s3c2410_modify_misccr(S3C2410_MISCCR_USBHOST |
			      S3C2410_MISCCR_USBSUSPND0 |
			      S3C2410_MISCCR_USBSUSPND1, 0x0);

	if (machine_is_n30()) {
		/* Turn off suspend on both USB ports, and switch the
		 * selectable USB port to USB device mode. */
		s3c2410_modify_misccr(S3C2410_MISCCR_USBHOST |
				      S3C2410_MISCCR_USBSUSPND0 |
				      S3C2410_MISCCR_USBSUSPND1, 0x0);

		platform_add_devices(n30_devices, ARRAY_SIZE(n30_devices));
	}

	if (machine_is_n35()) {
		/* Turn off suspend and switch the selectable USB port
		 * to USB device mode.  Turn on suspend for the host
		 * port since it is not connected on the N35.
		 *
		 * Actually, the host port is available at some pads
		 * on the back of the device, so it would actually be
		 * possible to add a USB device inside the N35 if you
		 * are willing to do some hardware modifications. */
		s3c2410_modify_misccr(S3C2410_MISCCR_USBHOST |
				      S3C2410_MISCCR_USBSUSPND0 |
				      S3C2410_MISCCR_USBSUSPND1,
				      S3C2410_MISCCR_USBSUSPND1);

		platform_add_devices(n35_devices, ARRAY_SIZE(n35_devices));
	}
}

MACHINE_START(N30, "Acer-N30")
	/* Maintainer: Christer Weinigel <christer@weinigel.se>,
				Ben Dooks <ben-linux@fluff.org>
	*/
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.timer		= &s3c24xx_timer,
	.init_machine	= n30_init,
	.init_irq	= n30_init_irq,
	.map_io		= n30_map_io,
MACHINE_END

MACHINE_START(N35, "Acer-N35")
	/* Maintainer: Christer Weinigel <christer@weinigel.se>
	*/
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.timer		= &s3c24xx_timer,
	.init_machine	= n30_init,
	.init_irq	= n30_init_irq,
	.map_io		= n30_map_io,
MACHINE_END
