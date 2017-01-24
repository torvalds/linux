/*
 * HTC Herald board configuration
 * Copyright (C) 2009 Cory Maccarrone <darkstar6262@gmail.com>
 * Copyright (C) 2009 Wing Linux
 *
 * Based on the board-htcwizard.c file from the linwizard project:
 * Copyright (C) 2006 Unai Uribarri
 * Copyright (C) 2008 linwizard.sourceforge.net
 *
 * This  program is  free  software; you  can  redistribute it  and/or
 * modify  it under the  terms of  the GNU  General Public  License as
 * published by the Free Software  Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 *
 * You should have  received a copy of the  GNU General Public License
 * along  with  this program;  if  not,  write  to the  Free  Software
 * Foundation,  Inc.,  51 Franklin  Street,  Fifth  Floor, Boston,  MA
 * 02110-1301, USA.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/htcpld.h>
#include <linux/leds.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/omapfb.h>
#include <linux/platform_data/keypad-omap.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/omap7xx.h>
#include "mmc.h"

#include <mach/irqs.h>
#include <mach/usb.h>

#include "common.h"

/* LCD register definition */
#define       OMAP_LCDC_CONTROL               (0xfffec000 + 0x00)
#define       OMAP_LCDC_STATUS                (0xfffec000 + 0x10)
#define       OMAP_DMA_LCD_CCR                (0xfffee300 + 0xc2)
#define       OMAP_DMA_LCD_CTRL               (0xfffee300 + 0xc4)
#define       OMAP_LCDC_CTRL_LCD_EN           (1 << 0)
#define       OMAP_LCDC_STAT_DONE             (1 << 0)

/* GPIO definitions for the power button and keyboard slide switch */
#define HTCHERALD_GPIO_POWER 139
#define HTCHERALD_GPIO_SLIDE 174
#define HTCHERALD_GIRQ_BTNS 141

/* GPIO definitions for the touchscreen */
#define HTCHERALD_GPIO_TS 76

/* HTCPLD definitions */

/*
 * CPLD Logic
 *
 * Chip 3 - 0x03
 *
 * Function            7 6 5 4  3 2 1 0
 * ------------------------------------
 * DPAD light          x x x x  x x x 1
 * SoundDev            x x x x  1 x x x
 * Screen white        1 x x x  x x x x
 * MMC power on        x x x x  x 1 x x
 * Happy times (n)     0 x x x  x 1 x x
 *
 * Chip 4 - 0x04
 *
 * Function            7 6 5 4  3 2 1 0
 * ------------------------------------
 * Keyboard light      x x x x  x x x 1
 * LCD Bright (4)      x x x x  x 1 1 x
 * LCD Bright (3)      x x x x  x 0 1 x
 * LCD Bright (2)      x x x x  x 1 0 x
 * LCD Bright (1)      x x x x  x 0 0 x
 * LCD Off             x x x x  0 x x x
 * LCD image (fb)      1 x x x  x x x x
 * LCD image (white)   0 x x x  x x x x
 * Caps lock LED       x x 1 x  x x x x
 *
 * Chip 5 - 0x05
 *
 * Function            7 6 5 4  3 2 1 0
 * ------------------------------------
 * Red (solid)         x x x x  x 1 x x
 * Red (flash)         x x x x  x x 1 x
 * Green (GSM flash)   x x x x  1 x x x
 * Green (GSM solid)   x x x 1  x x x x
 * Green (wifi flash)  x x 1 x  x x x x
 * Blue (bt flash)     x 1 x x  x x x x
 * DPAD Int Enable     1 x x x  x x x 0
 *
 * (Combinations of the above can be made for different colors.)
 * The direction pad interrupt enable must be set each time the
 * interrupt is handled.
 *
 * Chip 6 - 0x06
 *
 * Function            7 6 5 4  3 2 1 0
 * ------------------------------------
 * Vibrator            x x x x  1 x x x
 * Alt LED             x x x 1  x x x x
 * Screen white        1 x x x  x x x x
 * Screen white        x x 1 x  x x x x
 * Screen white        x 0 x x  x x x x
 * Enable kbd dpad     x x x x  x x 0 x
 * Happy Times         0 1 0 x  x x 0 x
 */

/*
 * HTCPLD GPIO lines start 16 after OMAP_MAX_GPIO_LINES to account
 * for the 16 MPUIO lines.
 */
#define HTCPLD_GPIO_START_OFFSET	(OMAP_MAX_GPIO_LINES + 16)
#define HTCPLD_IRQ(chip, offset)	(OMAP_IRQ_END + 8 * (chip) + (offset))
#define HTCPLD_BASE(chip, offset)	\
	(HTCPLD_GPIO_START_OFFSET + 8 * (chip) + (offset))

#define HTCPLD_GPIO_LED_DPAD		HTCPLD_BASE(0, 0)
#define HTCPLD_GPIO_LED_KBD		HTCPLD_BASE(1, 0)
#define HTCPLD_GPIO_LED_CAPS		HTCPLD_BASE(1, 5)
#define HTCPLD_GPIO_LED_RED_FLASH	HTCPLD_BASE(2, 1)
#define HTCPLD_GPIO_LED_RED_SOLID	HTCPLD_BASE(2, 2)
#define HTCPLD_GPIO_LED_GREEN_FLASH	HTCPLD_BASE(2, 3)
#define HTCPLD_GPIO_LED_GREEN_SOLID	HTCPLD_BASE(2, 4)
#define HTCPLD_GPIO_LED_WIFI		HTCPLD_BASE(2, 5)
#define HTCPLD_GPIO_LED_BT		HTCPLD_BASE(2, 6)
#define HTCPLD_GPIO_LED_VIBRATE		HTCPLD_BASE(3, 3)
#define HTCPLD_GPIO_LED_ALT		HTCPLD_BASE(3, 4)

#define HTCPLD_GPIO_RIGHT_KBD		HTCPLD_BASE(6, 7)
#define HTCPLD_GPIO_UP_KBD		HTCPLD_BASE(6, 6)
#define HTCPLD_GPIO_LEFT_KBD		HTCPLD_BASE(6, 5)
#define HTCPLD_GPIO_DOWN_KBD		HTCPLD_BASE(6, 4)

#define HTCPLD_GPIO_RIGHT_DPAD		HTCPLD_BASE(7, 7)
#define HTCPLD_GPIO_UP_DPAD		HTCPLD_BASE(7, 6)
#define HTCPLD_GPIO_LEFT_DPAD		HTCPLD_BASE(7, 5)
#define HTCPLD_GPIO_DOWN_DPAD		HTCPLD_BASE(7, 4)
#define HTCPLD_GPIO_ENTER_DPAD		HTCPLD_BASE(7, 3)

/*
 * The htcpld chip requires a gpio write to a specific line
 * to re-enable interrupts after one has occurred.
 */
#define HTCPLD_GPIO_INT_RESET_HI	HTCPLD_BASE(2, 7)
#define HTCPLD_GPIO_INT_RESET_LO	HTCPLD_BASE(2, 0)

/* Chip 5 */
#define HTCPLD_IRQ_RIGHT_KBD		HTCPLD_IRQ(0, 7)
#define HTCPLD_IRQ_UP_KBD		HTCPLD_IRQ(0, 6)
#define HTCPLD_IRQ_LEFT_KBD		HTCPLD_IRQ(0, 5)
#define HTCPLD_IRQ_DOWN_KBD		HTCPLD_IRQ(0, 4)

/* Chip 6 */
#define HTCPLD_IRQ_RIGHT_DPAD		HTCPLD_IRQ(1, 7)
#define HTCPLD_IRQ_UP_DPAD		HTCPLD_IRQ(1, 6)
#define HTCPLD_IRQ_LEFT_DPAD		HTCPLD_IRQ(1, 5)
#define HTCPLD_IRQ_DOWN_DPAD		HTCPLD_IRQ(1, 4)
#define HTCPLD_IRQ_ENTER_DPAD		HTCPLD_IRQ(1, 3)

/* Keyboard definition */

static const unsigned int htc_herald_keymap[] = {
	KEY(0, 0, KEY_RECORD), /* Mail button */
	KEY(1, 0, KEY_CAMERA), /* Camera */
	KEY(2, 0, KEY_PHONE), /* Send key */
	KEY(3, 0, KEY_VOLUMEUP), /* Volume up */
	KEY(4, 0, KEY_F2),  /* Right bar (landscape) */
	KEY(5, 0, KEY_MAIL), /* Win key (portrait) */
	KEY(6, 0, KEY_DIRECTORY), /* Right bar (protrait) */
	KEY(0, 1, KEY_LEFTCTRL), /* Windows key */
	KEY(1, 1, KEY_COMMA),
	KEY(2, 1, KEY_M),
	KEY(3, 1, KEY_K),
	KEY(4, 1, KEY_SLASH), /* OK key */
	KEY(5, 1, KEY_I),
	KEY(6, 1, KEY_U),
	KEY(0, 2, KEY_LEFTALT),
	KEY(1, 2, KEY_TAB),
	KEY(2, 2, KEY_N),
	KEY(3, 2, KEY_J),
	KEY(4, 2, KEY_ENTER),
	KEY(5, 2, KEY_H),
	KEY(6, 2, KEY_Y),
	KEY(0, 3, KEY_SPACE),
	KEY(1, 3, KEY_L),
	KEY(2, 3, KEY_B),
	KEY(3, 3, KEY_V),
	KEY(4, 3, KEY_BACKSPACE),
	KEY(5, 3, KEY_G),
	KEY(6, 3, KEY_T),
	KEY(0, 4, KEY_CAPSLOCK), /* Shift */
	KEY(1, 4, KEY_C),
	KEY(2, 4, KEY_F),
	KEY(3, 4, KEY_R),
	KEY(4, 4, KEY_O),
	KEY(5, 4, KEY_E),
	KEY(6, 4, KEY_D),
	KEY(0, 5, KEY_X),
	KEY(1, 5, KEY_Z),
	KEY(2, 5, KEY_S),
	KEY(3, 5, KEY_W),
	KEY(4, 5, KEY_P),
	KEY(5, 5, KEY_Q),
	KEY(6, 5, KEY_A),
	KEY(0, 6, KEY_CONNECT), /* Voice button */
	KEY(2, 6, KEY_CANCEL), /* End key */
	KEY(3, 6, KEY_VOLUMEDOWN), /* Volume down */
	KEY(4, 6, KEY_F1), /* Left bar (landscape) */
	KEY(5, 6, KEY_WWW), /* OK button (portrait) */
	KEY(6, 6, KEY_CALENDAR), /* Left bar (portrait) */
};

static const struct matrix_keymap_data htc_herald_keymap_data = {
	.keymap		= htc_herald_keymap,
	.keymap_size	= ARRAY_SIZE(htc_herald_keymap),
};

static struct omap_kp_platform_data htcherald_kp_data = {
	.rows	= 7,
	.cols	= 7,
	.delay = 20,
	.rep = true,
	.keymap_data = &htc_herald_keymap_data,
};

static struct resource kp_resources[] = {
	[0] = {
		.start	= INT_7XX_MPUIO_KEYPAD,
		.end	= INT_7XX_MPUIO_KEYPAD,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &htcherald_kp_data,
	},
	.num_resources	= ARRAY_SIZE(kp_resources),
	.resource	= kp_resources,
};

/* GPIO buttons for keyboard slide and power button */
static struct gpio_keys_button herald_gpio_keys_table[] = {
	{BTN_0,  HTCHERALD_GPIO_POWER, 1, "POWER", EV_KEY, 1, 20},
	{SW_LID, HTCHERALD_GPIO_SLIDE, 0, "SLIDE", EV_SW,  1, 20},

	{KEY_LEFT,  HTCPLD_GPIO_LEFT_KBD,  1, "LEFT",  EV_KEY, 1, 20},
	{KEY_RIGHT, HTCPLD_GPIO_RIGHT_KBD, 1, "RIGHT", EV_KEY, 1, 20},
	{KEY_UP,    HTCPLD_GPIO_UP_KBD,    1, "UP",    EV_KEY, 1, 20},
	{KEY_DOWN,  HTCPLD_GPIO_DOWN_KBD,  1, "DOWN",  EV_KEY, 1, 20},

	{KEY_LEFT,  HTCPLD_GPIO_LEFT_DPAD,   1, "DLEFT",  EV_KEY, 1, 20},
	{KEY_RIGHT, HTCPLD_GPIO_RIGHT_DPAD,  1, "DRIGHT", EV_KEY, 1, 20},
	{KEY_UP,    HTCPLD_GPIO_UP_DPAD,     1, "DUP",    EV_KEY, 1, 20},
	{KEY_DOWN,  HTCPLD_GPIO_DOWN_DPAD,   1, "DDOWN",  EV_KEY, 1, 20},
	{KEY_ENTER, HTCPLD_GPIO_ENTER_DPAD,  1, "DENTER", EV_KEY, 1, 20},
};

static struct gpio_keys_platform_data herald_gpio_keys_data = {
	.buttons	= herald_gpio_keys_table,
	.nbuttons	= ARRAY_SIZE(herald_gpio_keys_table),
	.rep		= true,
};

static struct platform_device herald_gpiokeys_device = {
	.name      = "gpio-keys",
	.id		= -1,
	.dev = {
		.platform_data = &herald_gpio_keys_data,
	},
};

/* LEDs for the Herald.  These connect to the HTCPLD GPIO device. */
static struct gpio_led gpio_leds[] = {
	{"dpad",        NULL, HTCPLD_GPIO_LED_DPAD,        0, 0, LEDS_GPIO_DEFSTATE_OFF},
	{"kbd",         NULL, HTCPLD_GPIO_LED_KBD,         0, 0, LEDS_GPIO_DEFSTATE_OFF},
	{"vibrate",     NULL, HTCPLD_GPIO_LED_VIBRATE,     0, 0, LEDS_GPIO_DEFSTATE_OFF},
	{"green_solid", NULL, HTCPLD_GPIO_LED_GREEN_SOLID, 0, 0, LEDS_GPIO_DEFSTATE_OFF},
	{"green_flash", NULL, HTCPLD_GPIO_LED_GREEN_FLASH, 0, 0, LEDS_GPIO_DEFSTATE_OFF},
	{"red_solid",   "mmc0", HTCPLD_GPIO_LED_RED_SOLID, 0, 0, LEDS_GPIO_DEFSTATE_OFF},
	{"red_flash",   NULL, HTCPLD_GPIO_LED_RED_FLASH,   0, 0, LEDS_GPIO_DEFSTATE_OFF},
	{"wifi",        NULL, HTCPLD_GPIO_LED_WIFI,        0, 0, LEDS_GPIO_DEFSTATE_OFF},
	{"bt",          NULL, HTCPLD_GPIO_LED_BT,          0, 0, LEDS_GPIO_DEFSTATE_OFF},
	{"caps",        NULL, HTCPLD_GPIO_LED_CAPS,        0, 0, LEDS_GPIO_DEFSTATE_OFF},
	{"alt",         NULL, HTCPLD_GPIO_LED_ALT,         0, 0, LEDS_GPIO_DEFSTATE_OFF},
};

static struct gpio_led_platform_data gpio_leds_data = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device gpio_leds_device = {
	.name		= "leds-gpio",
	.id		= 0,
	.dev	= {
		.platform_data	= &gpio_leds_data,
	},
};

/* HTC PLD chips */

static struct resource htcpld_resources[] = {
	[0] = {
		.flags  = IORESOURCE_IRQ,
	},
};

static struct htcpld_chip_platform_data htcpld_chips[] = {
	[0] = {
		.addr		= 0x03,
		.reset		= 0x04,
		.num_gpios	= 8,
		.gpio_out_base	= HTCPLD_BASE(0, 0),
		.gpio_in_base	= HTCPLD_BASE(4, 0),
	},
	[1] = {
		.addr		= 0x04,
		.reset		= 0x8e,
		.num_gpios	= 8,
		.gpio_out_base	= HTCPLD_BASE(1, 0),
		.gpio_in_base	= HTCPLD_BASE(5, 0),
	},
	[2] = {
		.addr		= 0x05,
		.reset		= 0x80,
		.num_gpios	= 8,
		.gpio_out_base	= HTCPLD_BASE(2, 0),
		.gpio_in_base	= HTCPLD_BASE(6, 0),
		.irq_base	= HTCPLD_IRQ(0, 0),
		.num_irqs	= 8,
	},
	[3] = {
		.addr		= 0x06,
		.reset		= 0x40,
		.num_gpios	= 8,
		.gpio_out_base	= HTCPLD_BASE(3, 0),
		.gpio_in_base	= HTCPLD_BASE(7, 0),
		.irq_base	= HTCPLD_IRQ(1, 0),
		.num_irqs	= 8,
	},
};

static struct htcpld_core_platform_data htcpld_pfdata = {
	.int_reset_gpio_hi = HTCPLD_GPIO_INT_RESET_HI,
	.int_reset_gpio_lo = HTCPLD_GPIO_INT_RESET_LO,
	.i2c_adapter_id	   = 1,

	.chip		   = htcpld_chips,
	.num_chip	   = ARRAY_SIZE(htcpld_chips),
};

static struct platform_device htcpld_device = {
	.name		= "i2c-htcpld",
	.id		= -1,
	.resource	= htcpld_resources,
	.num_resources	= ARRAY_SIZE(htcpld_resources),
	.dev	= {
		.platform_data	= &htcpld_pfdata,
	},
};

/* USB Device */
static struct omap_usb_config htcherald_usb_config __initdata = {
	.otg = 0,
	.register_host = 0,
	.register_dev  = 1,
	.hmc_mode = 4,
	.pins[0] = 2,
};

/* LCD Device resources */
static struct omap_lcd_config htcherald_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct platform_device lcd_device = {
	.name           = "lcd_htcherald",
	.id             = -1,
};

/* MMC Card */
#if IS_ENABLED(CONFIG_MMC_OMAP)
static struct omap_mmc_platform_data htc_mmc1_data = {
	.nr_slots                       = 1,
	.switch_slot                    = NULL,
	.slots[0]       = {
		.ocr_mask               = MMC_VDD_32_33 | MMC_VDD_33_34,
		.name                   = "mmcblk",
		.nomux                  = 1,
		.wires                  = 4,
		.switch_pin             = -1,
	},
};

static struct omap_mmc_platform_data *htc_mmc_data[1];
#endif


/* Platform devices for the Herald */
static struct platform_device *devices[] __initdata = {
	&kp_device,
	&lcd_device,
	&htcpld_device,
	&gpio_leds_device,
	&herald_gpiokeys_device,
};

/*
 * Touchscreen
 */
static const struct ads7846_platform_data htcherald_ts_platform_data = {
	.model			= 7846,
	.keep_vref_on		= 1,
	.x_plate_ohms		= 496,
	.gpio_pendown		= HTCHERALD_GPIO_TS,
	.pressure_max		= 10000,
	.pressure_min		= 5000,
	.x_min			= 528,
	.x_max			= 3760,
	.y_min			= 624,
	.y_max			= 3760,
};

static struct spi_board_info __initdata htcherald_spi_board_info[] = {
	{
		.modalias		= "ads7846",
		.platform_data		= &htcherald_ts_platform_data,
		.max_speed_hz		= 2500000,
		.bus_num		= 2,
		.chip_select		= 1,
	}
};

/*
 * Init functions from here on
 */

static void __init htcherald_lcd_init(void)
{
	u32 reg;
	unsigned int tries = 200;

	/* disable controller if active */
	reg = omap_readl(OMAP_LCDC_CONTROL);
	if (reg & OMAP_LCDC_CTRL_LCD_EN) {
		reg &= ~OMAP_LCDC_CTRL_LCD_EN;
		omap_writel(reg, OMAP_LCDC_CONTROL);

		/* wait for end of frame */
		while (!(omap_readl(OMAP_LCDC_STATUS) & OMAP_LCDC_STAT_DONE)) {
			tries--;
			if (!tries)
				break;
		}
		if (!tries)
			pr_err("Timeout waiting for end of frame -- LCD may not be available\n");

		/* turn off DMA */
		reg = omap_readw(OMAP_DMA_LCD_CCR);
		reg &= ~(1 << 7);
		omap_writew(reg, OMAP_DMA_LCD_CCR);

		reg = omap_readw(OMAP_DMA_LCD_CTRL);
		reg &= ~(1 << 8);
		omap_writew(reg, OMAP_DMA_LCD_CTRL);
	}
}

static void __init htcherald_map_io(void)
{
	omap7xx_map_io();

	/*
	 * The LCD panel must be disabled and DMA turned off here, as doing
	 * it later causes the LCD never to reinitialize.
	 */
	htcherald_lcd_init();

	printk(KERN_INFO "htcherald_map_io done.\n");
}

static void __init htcherald_disable_watchdog(void)
{
	/* Disable watchdog if running */
	if (omap_readl(OMAP_WDT_TIMER_MODE) & 0x8000) {
		/*
		 * disable a potentially running watchdog timer before
		 * it kills us.
		 */
		printk(KERN_WARNING "OMAP850 Watchdog seems to be activated, disabling it for now.\n");
		omap_writel(0xF5, OMAP_WDT_TIMER_MODE);
		omap_writel(0xA0, OMAP_WDT_TIMER_MODE);
	}
}

#define HTCHERALD_GPIO_USB_EN1 33
#define HTCHERALD_GPIO_USB_EN2 73
#define HTCHERALD_GPIO_USB_DM  35
#define HTCHERALD_GPIO_USB_DP  36

static void __init htcherald_usb_enable(void)
{
	unsigned int tries = 20;
	unsigned int value = 0;

	/* Request the GPIOs we need to control here */
	if (gpio_request(HTCHERALD_GPIO_USB_EN1, "herald_usb") < 0)
		goto err1;

	if (gpio_request(HTCHERALD_GPIO_USB_EN2, "herald_usb") < 0)
		goto err2;

	if (gpio_request(HTCHERALD_GPIO_USB_DM, "herald_usb") < 0)
		goto err3;

	if (gpio_request(HTCHERALD_GPIO_USB_DP, "herald_usb") < 0)
		goto err4;

	/* force USB_EN GPIO to 0 */
	do {
		/* output low */
		gpio_direction_output(HTCHERALD_GPIO_USB_EN1, 0);
	} while ((value = gpio_get_value(HTCHERALD_GPIO_USB_EN1)) == 1 &&
			--tries);

	if (value == 1)
		printk(KERN_WARNING "Unable to reset USB, trying to continue\n");

	gpio_direction_output(HTCHERALD_GPIO_USB_EN2, 0); /* output low */
	gpio_direction_input(HTCHERALD_GPIO_USB_DM); /* input */
	gpio_direction_input(HTCHERALD_GPIO_USB_DP); /* input */

	goto done;

err4:
	gpio_free(HTCHERALD_GPIO_USB_DM);
err3:
	gpio_free(HTCHERALD_GPIO_USB_EN2);
err2:
	gpio_free(HTCHERALD_GPIO_USB_EN1);
err1:
	printk(KERN_ERR "Unabled to request GPIO for USB\n");
done:
	printk(KERN_INFO "USB setup complete.\n");
}

static void __init htcherald_init(void)
{
	printk(KERN_INFO "HTC Herald init.\n");

	/* Do board initialization before we register all the devices */
	htcpld_resources[0].start = gpio_to_irq(HTCHERALD_GIRQ_BTNS);
	htcpld_resources[0].end = gpio_to_irq(HTCHERALD_GIRQ_BTNS);
	platform_add_devices(devices, ARRAY_SIZE(devices));

	htcherald_disable_watchdog();

	htcherald_usb_enable();
	omap1_usb_init(&htcherald_usb_config);

	htcherald_spi_board_info[0].irq = gpio_to_irq(HTCHERALD_GPIO_TS);
	spi_register_board_info(htcherald_spi_board_info,
		ARRAY_SIZE(htcherald_spi_board_info));

	omap_register_i2c_bus(1, 100, NULL, 0);

#if IS_ENABLED(CONFIG_MMC_OMAP)
	htc_mmc_data[0] = &htc_mmc1_data;
	omap1_init_mmc(htc_mmc_data, 1);
#endif

	omapfb_set_lcd_config(&htcherald_lcd_config);
}

MACHINE_START(HERALD, "HTC Herald")
	/* Maintainer: Cory Maccarrone <darkstar6262@gmail.com> */
	/* Maintainer: wing-linux.sourceforge.net */
	.atag_offset    = 0x100,
	.map_io         = htcherald_map_io,
	.init_early     = omap1_init_early,
	.init_irq       = omap1_init_irq,
	.handle_irq	= omap1_handle_irq,
	.init_machine   = htcherald_init,
	.init_late	= omap1_init_late,
	.init_time	= omap1_timer_init,
	.restart	= omap1_restart,
MACHINE_END
