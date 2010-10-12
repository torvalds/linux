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
#include <linux/io.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/omap7xx.h>
#include <plat/common.h>
#include <plat/board.h>
#include <plat/keypad.h>
#include <plat/usb.h>

#include <mach/irqs.h>

#include <linux/delay.h>

/* LCD register definition */
#define       OMAP_LCDC_CONTROL               (0xfffec000 + 0x00)
#define       OMAP_LCDC_STATUS                (0xfffec000 + 0x10)
#define       OMAP_DMA_LCD_CCR                (0xfffee300 + 0xc2)
#define       OMAP_DMA_LCD_CTRL               (0xfffee300 + 0xc4)
#define       OMAP_LCDC_CTRL_LCD_EN           (1 << 0)
#define       OMAP_LCDC_STAT_DONE             (1 << 0)

static struct omap_lcd_config htcherald_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel htcherald_config[] __initdata = {
	{ OMAP_TAG_LCD, &htcherald_lcd_config },
};

/* Keyboard definition */

static int htc_herald_keymap[] = {
	KEY(0, 0, KEY_RECORD), /* Mail button */
	KEY(0, 1, KEY_CAMERA), /* Camera */
	KEY(0, 2, KEY_PHONE), /* Send key */
	KEY(0, 3, KEY_VOLUMEUP), /* Volume up */
	KEY(0, 4, KEY_F2),  /* Right bar (landscape) */
	KEY(0, 5, KEY_MAIL), /* Win key (portrait) */
	KEY(0, 6, KEY_DIRECTORY), /* Right bar (protrait) */
	KEY(1, 0, KEY_LEFTCTRL), /* Windows key */
	KEY(1, 1, KEY_COMMA),
	KEY(1, 2, KEY_M),
	KEY(1, 3, KEY_K),
	KEY(1, 4, KEY_SLASH), /* OK key */
	KEY(1, 5, KEY_I),
	KEY(1, 6, KEY_U),
	KEY(2, 0, KEY_LEFTALT),
	KEY(2, 1, KEY_TAB),
	KEY(2, 2, KEY_N),
	KEY(2, 3, KEY_J),
	KEY(2, 4, KEY_ENTER),
	KEY(2, 5, KEY_H),
	KEY(2, 6, KEY_Y),
	KEY(3, 0, KEY_SPACE),
	KEY(3, 1, KEY_L),
	KEY(3, 2, KEY_B),
	KEY(3, 3, KEY_V),
	KEY(3, 4, KEY_BACKSPACE),
	KEY(3, 5, KEY_G),
	KEY(3, 6, KEY_T),
	KEY(4, 0, KEY_CAPSLOCK), /* Shift */
	KEY(4, 1, KEY_C),
	KEY(4, 2, KEY_F),
	KEY(4, 3, KEY_R),
	KEY(4, 4, KEY_O),
	KEY(4, 5, KEY_E),
	KEY(4, 6, KEY_D),
	KEY(5, 0, KEY_X),
	KEY(5, 1, KEY_Z),
	KEY(5, 2, KEY_S),
	KEY(5, 3, KEY_W),
	KEY(5, 4, KEY_P),
	KEY(5, 5, KEY_Q),
	KEY(5, 6, KEY_A),
	KEY(6, 0, KEY_CONNECT), /* Voice button */
	KEY(6, 2, KEY_CANCEL), /* End key */
	KEY(6, 3, KEY_VOLUMEDOWN), /* Volume down */
	KEY(6, 4, KEY_F1), /* Left bar (landscape) */
	KEY(6, 5, KEY_WWW), /* OK button (portrait) */
	KEY(6, 6, KEY_CALENDAR), /* Left bar (portrait) */
	0
};

struct omap_kp_platform_data htcherald_kp_data = {
	.rows	= 7,
	.cols	= 7,
	.delay = 20,
	.rep = 1,
	.keymap = htc_herald_keymap,
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

/* USB Device */
static struct omap_usb_config htcherald_usb_config __initdata = {
	.otg = 0,
	.register_host = 0,
	.register_dev  = 1,
	.hmc_mode = 4,
	.pins[0] = 2,
};

/* LCD Device resources */
static struct platform_device lcd_device = {
	.name           = "lcd_htcherald",
	.id             = -1,
};

static struct platform_device *devices[] __initdata = {
	&kp_device,
	&lcd_device,
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
			printk(KERN_WARNING "Timeout waiting for end of frame "
			       "-- LCD may not be available\n");

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
	omap1_map_common_io();

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

	omap_gpio_init();

	omap_board_config = htcherald_config;
	omap_board_config_size = ARRAY_SIZE(htcherald_config);
	platform_add_devices(devices, ARRAY_SIZE(devices));

	htcherald_disable_watchdog();

	htcherald_usb_enable();
	omap1_usb_init(&htcherald_usb_config);
}

static void __init htcherald_init_irq(void)
{
	printk(KERN_INFO "htcherald_init_irq.\n");
	omap1_init_common_hw();
	omap_init_irq();
}

MACHINE_START(HERALD, "HTC Herald")
	/* Maintainer: Cory Maccarrone <darkstar6262@gmail.com> */
	/* Maintainer: wing-linux.sourceforge.net */
	.phys_io        = 0xfff00000,
	.io_pg_offst    = ((0xfef00000) >> 18) & 0xfffc,
	.boot_params    = 0x10000100,
	.map_io         = htcherald_map_io,
	.reserve	= omap_reserve,
	.init_irq       = htcherald_init_irq,
	.init_machine   = htcherald_init,
	.timer          = &omap_timer,
MACHINE_END
