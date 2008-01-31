/*
 * Support for HTC Magician PDA phones:
 * i-mate JAM, O2 Xda mini, Orange SPV M500, Qtek s100, Qtek s110
 * and T-Mobile MDA Compact.
 *
 * Copyright (c) 2006-2007 Philipp Zabel
 *
 * Based on hx4700.c, spitz.c and others.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/physmap.h>

#include <asm/gpio.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/arch/magician.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/irda.h>
#include <asm/arch/ohci.h>

#include "generic.h"

/*
 * IRDA
 */

static void magician_irda_transceiver_mode(struct device *dev, int mode)
{
	gpio_set_value(GPIO83_MAGICIAN_nIR_EN, mode & IR_OFF);
}

static struct pxaficp_platform_data magician_ficp_info = {
	.transceiver_cap  = IR_SIRMODE | IR_OFF,
	.transceiver_mode = magician_irda_transceiver_mode,
};

/*
 * GPIO Keys
 */

static struct gpio_keys_button magician_button_table[] = {
	{KEY_POWER,      GPIO0_MAGICIAN_KEY_POWER,      0, "Power button"},
	{KEY_ESC,        GPIO37_MAGICIAN_KEY_HANGUP,    0, "Hangup button"},
	{KEY_F10,        GPIO38_MAGICIAN_KEY_CONTACTS,  0, "Contacts button"},
	{KEY_CALENDAR,   GPIO90_MAGICIAN_KEY_CALENDAR,  0, "Calendar button"},
	{KEY_CAMERA,     GPIO91_MAGICIAN_KEY_CAMERA,    0, "Camera button"},
	{KEY_UP,         GPIO93_MAGICIAN_KEY_UP,        0, "Up button"},
	{KEY_DOWN,       GPIO94_MAGICIAN_KEY_DOWN,      0, "Down button"},
	{KEY_LEFT,       GPIO95_MAGICIAN_KEY_LEFT,      0, "Left button"},
	{KEY_RIGHT,      GPIO96_MAGICIAN_KEY_RIGHT,     0, "Right button"},
	{KEY_KPENTER,    GPIO97_MAGICIAN_KEY_ENTER,     0, "Action button"},
	{KEY_RECORD,     GPIO98_MAGICIAN_KEY_RECORD,    0, "Record button"},
	{KEY_VOLUMEUP,   GPIO100_MAGICIAN_KEY_VOL_UP,   0, "Volume up"},
	{KEY_VOLUMEDOWN, GPIO101_MAGICIAN_KEY_VOL_DOWN, 0, "Volume down"},
	{KEY_PHONE,      GPIO102_MAGICIAN_KEY_PHONE,    0, "Phone button"},
	{KEY_PLAY,       GPIO99_MAGICIAN_HEADPHONE_IN,  0, "Headset button"},
};

static struct gpio_keys_platform_data gpio_keys_data = {
	.buttons  = magician_button_table,
	.nbuttons = ARRAY_SIZE(magician_button_table),
};

static struct platform_device gpio_keys = {
	.name = "gpio-keys",
	.dev  = {
		.platform_data = &gpio_keys_data,
	},
	.id   = -1,
};

/*
 * LCD - Toppoly TD028STEB1
 */

static struct pxafb_mode_info toppoly_modes[] = {
	{
		.pixclock     = 96153,
		.bpp          = 16,
		.xres         = 240,
		.yres         = 320,
		.hsync_len    = 11,
		.vsync_len    = 3,
		.left_margin  = 19,
		.upper_margin = 2,
		.right_margin = 10,
		.lower_margin = 2,
		.sync         = 0,
	},
};

static struct pxafb_mach_info toppoly_info = {
	.modes       = toppoly_modes,
	.num_modes   = 1,
	.fixed_modes = 1,
	.lccr0       = LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3       = LCCR3_PixRsEdg,
};

/*
 * Backlight
 */

static void magician_set_bl_intensity(int intensity)
{
	if (intensity) {
		PWM_CTRL0 = 1;
		PWM_PERVAL0 = 0xc8;
		PWM_PWDUTY0 = intensity;
		pxa_set_cken(CKEN_PWM0, 1);
	} else {
		pxa_set_cken(CKEN_PWM0, 0);
	}
}

static struct generic_bl_info backlight_info = {
	.default_intensity = 0x64,
	.limit_mask        = 0x0b,
	.max_intensity     = 0xc7,
	.set_bl_intensity  = magician_set_bl_intensity,
};

static struct platform_device backlight = {
	.name = "corgi-bl",
	.dev  = {
		.platform_data = &backlight_info,
	},
	.id   = -1,
};


/*
 * USB OHCI
 */

static int magician_ohci_init(struct device *dev)
{
	UHCHR = (UHCHR | UHCHR_SSEP2 | UHCHR_PCPL | UHCHR_CGR) &
	    ~(UHCHR_SSEP1 | UHCHR_SSEP3 | UHCHR_SSE);

	return 0;
}

static struct pxaohci_platform_data magician_ohci_info = {
	.port_mode    = PMM_PERPORT_MODE,
	.init         = magician_ohci_init,
	.power_budget = 0,
};


/*
 * StrataFlash
 */

#define PXA_CS_SIZE		0x04000000

static struct resource strataflash_resource = {
	.start = PXA_CS0_PHYS,
	.end   = PXA_CS0_PHYS + PXA_CS_SIZE - 1,
	.flags = IORESOURCE_MEM,
};

static struct physmap_flash_data strataflash_data = {
	.width = 4,
};

static struct platform_device strataflash = {
	.name          = "physmap-flash",
	.id            = -1,
	.num_resources = 1,
	.resource      = &strataflash_resource,
	.dev = {
		.platform_data = &strataflash_data,
	},
};

/*
 * Platform devices
 */

static struct platform_device *devices[] __initdata = {
	&gpio_keys,
	&backlight,
	&strataflash,
};

static void __init magician_init(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
	pxa_set_ohci_info(&magician_ohci_info);
	pxa_set_ficp_info(&magician_ficp_info);
	set_pxa_fb_info(&toppoly_info);
}


MACHINE_START(MAGICIAN, "HTC Magician")
	.phys_io = 0x40000000,
	.io_pg_offst = (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params = 0xa0000100,
	.map_io = pxa_map_io,
	.init_irq = pxa27x_init_irq,
	.init_machine = magician_init,
	.timer = &pxa_timer,
MACHINE_END
