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
#include <linux/delay.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/mfd/htc-egpio.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/physmap.h>
#include <linux/pda_power.h>

#include <asm/gpio.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/arch/magician.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/i2c.h>
#include <asm/arch/mmc.h>
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
 * EGPIO (Xilinx CPLD)
 *
 * 7 32-bit aligned 8-bit registers: 3x output, 1x irq, 3x input
 */

static struct resource egpio_resources[] = {
	[0] = {
		.start = PXA_CS3_PHYS,
		.end   = PXA_CS3_PHYS + 0x20,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = gpio_to_irq(GPIO13_MAGICIAN_CPLD_IRQ),
		.end   = gpio_to_irq(GPIO13_MAGICIAN_CPLD_IRQ),
		.flags = IORESOURCE_IRQ,
	},
};

static struct htc_egpio_chip egpio_chips[] = {
	[0] = {
		.reg_start = 0,
		.gpio_base = MAGICIAN_EGPIO(0, 0),
		.num_gpios = 24,
		.direction = HTC_EGPIO_OUTPUT,
		.initial_values = 0x40, /* EGPIO_MAGICIAN_GSM_RESET */
	},
	[1] = {
		.reg_start = 4,
		.gpio_base = MAGICIAN_EGPIO(4, 0),
		.num_gpios = 24,
		.direction = HTC_EGPIO_INPUT,
	},
};

static struct htc_egpio_platform_data egpio_info = {
	.reg_width    = 8,
	.bus_width    = 32,
	.irq_base     = IRQ_BOARD_START,
	.num_irqs     = 4,
	.ack_register = 3,
	.chip         = egpio_chips,
	.num_chips    = ARRAY_SIZE(egpio_chips),
};

static struct platform_device egpio = {
	.name          = "htc-egpio",
	.id            = -1,
	.resource      = egpio_resources,
	.num_resources = ARRAY_SIZE(egpio_resources),
	.dev = {
		.platform_data = &egpio_info,
	},
};

/*
 * LCD - Toppoly TD028STEB1 or Samsung LTP280QV
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

static struct pxafb_mode_info samsung_modes[] = {
	{
		.pixclock     = 96153,
		.bpp          = 16,
		.xres         = 240,
		.yres         = 320,
		.hsync_len    = 8,
		.vsync_len    = 4,
		.left_margin  = 9,
		.upper_margin = 4,
		.right_margin = 9,
		.lower_margin = 4,
		.sync         = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	},
};

static void toppoly_lcd_power(int on, struct fb_var_screeninfo *si)
{
	pr_debug("Toppoly LCD power\n");

	if (on) {
		pr_debug("on\n");
		gpio_set_value(EGPIO_MAGICIAN_TOPPOLY_POWER, 1);
		gpio_set_value(GPIO106_MAGICIAN_LCD_POWER_3, 1);
		udelay(2000);
		gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 1);
		udelay(2000);
		/* FIXME: enable LCDC here */
		udelay(2000);
		gpio_set_value(GPIO104_MAGICIAN_LCD_POWER_1, 1);
		udelay(2000);
		gpio_set_value(GPIO105_MAGICIAN_LCD_POWER_2, 1);
	} else {
		pr_debug("off\n");
		msleep(15);
		gpio_set_value(GPIO105_MAGICIAN_LCD_POWER_2, 0);
		udelay(500);
		gpio_set_value(GPIO104_MAGICIAN_LCD_POWER_1, 0);
		udelay(1000);
		gpio_set_value(GPIO106_MAGICIAN_LCD_POWER_3, 0);
		gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 0);
	}
}

static void samsung_lcd_power(int on, struct fb_var_screeninfo *si)
{
	pr_debug("Samsung LCD power\n");

	if (on) {
		pr_debug("on\n");
		if (system_rev < 3)
			gpio_set_value(GPIO75_MAGICIAN_SAMSUNG_POWER, 1);
		else
			gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 1);
		mdelay(10);
		gpio_set_value(GPIO106_MAGICIAN_LCD_POWER_3, 1);
		mdelay(10);
		gpio_set_value(GPIO104_MAGICIAN_LCD_POWER_1, 1);
		mdelay(30);
		gpio_set_value(GPIO105_MAGICIAN_LCD_POWER_2, 1);
		mdelay(10);
	} else {
		pr_debug("off\n");
		mdelay(10);
		gpio_set_value(GPIO105_MAGICIAN_LCD_POWER_2, 0);
		mdelay(30);
		gpio_set_value(GPIO104_MAGICIAN_LCD_POWER_1, 0);
		mdelay(10);
		gpio_set_value(GPIO106_MAGICIAN_LCD_POWER_3, 0);
		mdelay(10);
		if (system_rev < 3)
			gpio_set_value(GPIO75_MAGICIAN_SAMSUNG_POWER, 0);
		else
			gpio_set_value(EGPIO_MAGICIAN_LCD_POWER, 0);
	}
}

static struct pxafb_mach_info toppoly_info = {
	.modes           = toppoly_modes,
	.num_modes       = 1,
	.fixed_modes     = 1,
	.lccr0           = LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3           = LCCR3_PixRsEdg,
	.pxafb_lcd_power = toppoly_lcd_power,
};

static struct pxafb_mach_info samsung_info = {
	.modes           = samsung_modes,
	.num_modes       = 1,
	.fixed_modes     = 1,
	.lccr0           = LCCR0_LDDALT | LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3           = LCCR3_PixFlEdg,
	.pxafb_lcd_power = samsung_lcd_power,
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
	.name = "generic-bl",
	.dev  = {
		.platform_data = &backlight_info,
	},
	.id   = -1,
};


/*
 * External power
 */

static int magician_is_ac_online(void)
{
	return gpio_get_value(EGPIO_MAGICIAN_CABLE_STATE_AC);
}

static int magician_is_usb_online(void)
{
	return gpio_get_value(EGPIO_MAGICIAN_CABLE_STATE_USB);
}

static void magician_set_charge(int flags)
{
	gpio_set_value(GPIO30_MAGICIAN_nCHARGE_EN, !flags);
	gpio_set_value(EGPIO_MAGICIAN_CHARGE_EN, flags);
}

static struct pda_power_pdata power_supply_info = {
	.is_ac_online = magician_is_ac_online,
	.is_usb_online = magician_is_usb_online,
	.set_charge = magician_set_charge,
};

static struct resource power_supply_resources[] = {
	[0] = {
		.name  = "ac",
		.flags = IORESOURCE_IRQ,
		.start = IRQ_MAGICIAN_AC,
		.end   = IRQ_MAGICIAN_AC,
	},
	[1] = {
		.name  = "usb",
		.flags = IORESOURCE_IRQ,
		.start = IRQ_MAGICIAN_AC,
		.end   = IRQ_MAGICIAN_AC,
	},
};

static struct platform_device power_supply = {
	.name = "pda-power",
	.id   = -1,
	.dev  = {
		.platform_data = &power_supply_info,
	},
	.resource      = power_supply_resources,
	.num_resources = ARRAY_SIZE(power_supply_resources),
};


/*
 * MMC/SD
 */

static int magician_mci_init(struct device *dev,
				irq_handler_t detect_irq, void *data)
{
	return request_irq(IRQ_MAGICIAN_SD, detect_irq,
				IRQF_DISABLED | IRQF_SAMPLE_RANDOM,
				"MMC card detect", data);
}

static void magician_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data *pdata = dev->platform_data;

	gpio_set_value(EGPIO_MAGICIAN_SD_POWER, (1 << vdd) & pdata->ocr_mask);
}

static int magician_mci_get_ro(struct device *dev)
{
	return (!gpio_get_value(EGPIO_MAGICIAN_nSD_READONLY));
}

static void magician_mci_exit(struct device *dev, void *data)
{
	free_irq(IRQ_MAGICIAN_SD, data);
}

static struct pxamci_platform_data magician_mci_info = {
	.ocr_mask = MMC_VDD_32_33|MMC_VDD_33_34,
	.init     = magician_mci_init,
	.get_ro   = magician_mci_get_ro,
	.setpower = magician_mci_setpower,
	.exit     = magician_mci_exit,
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

static void magician_set_vpp(struct map_info *map, int vpp)
{
	gpio_set_value(EGPIO_MAGICIAN_FLASH_VPP, vpp);
}

#define PXA_CS_SIZE		0x04000000

static struct resource strataflash_resource = {
	.start = PXA_CS0_PHYS,
	.end   = PXA_CS0_PHYS + PXA_CS_SIZE - 1,
	.flags = IORESOURCE_MEM,
};

static struct physmap_flash_data strataflash_data = {
	.width = 4,
	.set_vpp = magician_set_vpp,
};

static struct platform_device strataflash = {
	.name          = "physmap-flash",
	.id            = -1,
	.resource      = &strataflash_resource,
	.num_resources = 1,
	.dev = {
		.platform_data = &strataflash_data,
	},
};

/*
 * Platform devices
 */

static struct platform_device *devices[] __initdata = {
	&gpio_keys,
	&egpio,
	&backlight,
	&power_supply,
	&strataflash,
};

static void __init magician_init(void)
{
	void __iomem *cpld;
	int lcd_select;

	platform_add_devices(devices, ARRAY_SIZE(devices));
	pxa_set_i2c_info(NULL);
	pxa_set_mci_info(&magician_mci_info);
	pxa_set_ohci_info(&magician_ohci_info);
	pxa_set_ficp_info(&magician_ficp_info);

	/* Check LCD type we have */
	cpld = ioremap_nocache(PXA_CS3_PHYS, 0x1000);
	if (cpld) {
		u8 board_id = __raw_readb(cpld+0x14);
		system_rev = board_id & 0x7;
		lcd_select = board_id & 0x8;
		iounmap(cpld);
		pr_info("LCD type: %s\n", lcd_select ? "Samsung" : "Toppoly");
		if (lcd_select && (system_rev < 3))
			pxa_gpio_mode(GPIO75_MAGICIAN_SAMSUNG_POWER_MD);
		pxa_gpio_mode(GPIO104_MAGICIAN_LCD_POWER_1_MD);
		pxa_gpio_mode(GPIO105_MAGICIAN_LCD_POWER_2_MD);
		pxa_gpio_mode(GPIO106_MAGICIAN_LCD_POWER_3_MD);
		set_pxa_fb_info(lcd_select ? &samsung_info : &toppoly_info);
	} else
		pr_err("LCD detection: CPLD mapping failed\n");
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
