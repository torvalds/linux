/*
 * linux/arch/arm/mach-omap1/board-palmte.c
 *
 * Modified from board-generic.c
 *
 * Support for the Palm Tungsten E PDA.
 *
 * Original version : Laurent Gonzalez
 *
 * Maintainers : http://palmtelinux.sf.net
 *                palmtelinux-developpers@lists.sf.net
 *
 * Copyright (c) 2006 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/tsc2102.h>
#include <linux/interrupt.h>
#include <linux/apm-emulation.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <asm/arch/gpio.h>
#include <asm/arch/mux.h>
#include <asm/arch/usb.h>
#include <asm/arch/tc.h>
#include <asm/arch/dma.h>
#include <asm/arch/board.h>
#include <asm/arch/irda.h>
#include <asm/arch/keypad.h>
#include <asm/arch/common.h>
#include <asm/arch/mcbsp.h>
#include <asm/arch/omap-alsa.h>

static void __init omap_palmte_init_irq(void)
{
	omap1_init_common_hw();
	omap_init_irq();
	omap_gpio_init();
}

static const int palmte_keymap[] = {
	KEY(0, 0, KEY_F1),		/* Calendar */
	KEY(0, 1, KEY_F2),		/* Contacts */
	KEY(0, 2, KEY_F3),		/* Tasks List */
	KEY(0, 3, KEY_F4),		/* Note Pad */
	KEY(0, 4, KEY_POWER),
	KEY(1, 0, KEY_LEFT),
	KEY(1, 1, KEY_DOWN),
	KEY(1, 2, KEY_UP),
	KEY(1, 3, KEY_RIGHT),
	KEY(1, 4, KEY_ENTER),
	0,
};

static struct omap_kp_platform_data palmte_kp_data = {
	.rows	= 8,
	.cols	= 8,
	.keymap = (int *) palmte_keymap,
	.rep	= 1,
	.delay	= 12,
};

static struct resource palmte_kp_resources[] = {
	[0]	= {
		.start	= INT_KEYBOARD,
		.end	= INT_KEYBOARD,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device palmte_kp_device = {
	.name		= "omap-keypad",
	.id		= -1,
	.dev		= {
		.platform_data	= &palmte_kp_data,
	},
	.num_resources	= ARRAY_SIZE(palmte_kp_resources),
	.resource	= palmte_kp_resources,
};

static struct mtd_partition palmte_rom_partitions[] = {
	/* PalmOS "Small ROM", contains the bootloader and the debugger */
	{
		.name		= "smallrom",
		.offset		= 0,
		.size		= 0xa000,
		.mask_flags	= MTD_WRITEABLE,
	},
	/* PalmOS "Big ROM", a filesystem with all the OS code and data */
	{
		.name		= "bigrom",
		.offset		= SZ_128K,
		/*
		 * 0x5f0000 bytes big in the multi-language ("EFIGS") version,
		 * 0x7b0000 bytes in the English-only ("enUS") version.
		 */
		.size		= 0x7b0000,
		.mask_flags	= MTD_WRITEABLE,
	},
};

static struct flash_platform_data palmte_rom_data = {
	.map_name	= "map_rom",
	.width		= 2,
	.parts		= palmte_rom_partitions,
	.nr_parts	= ARRAY_SIZE(palmte_rom_partitions),
};

static struct resource palmte_rom_resource = {
	.start		= OMAP_CS0_PHYS,
	.end		= OMAP_CS0_PHYS + SZ_8M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device palmte_rom_device = {
	.name		= "omapflash",
	.id		= -1,
	.dev		= {
		.platform_data	= &palmte_rom_data,
	},
	.num_resources	= 1,
	.resource	= &palmte_rom_resource,
};

static struct platform_device palmte_lcd_device = {
	.name		= "lcd_palmte",
	.id		= -1,
};

static struct omap_backlight_config palmte_backlight_config = {
	.default_intensity	= 0xa0,
};

static struct platform_device palmte_backlight_device = {
	.name		= "omap-bl",
	.id		= -1,
	.dev		= {
		.platform_data	= &palmte_backlight_config,
	},
};

static struct omap_irda_config palmte_irda_config = {
	.transceiver_cap	= IR_SIRMODE,
	.rx_channel		= OMAP_DMA_UART3_RX,
	.tx_channel		= OMAP_DMA_UART3_TX,
	.dest_start		= UART3_THR,
	.src_start		= UART3_RHR,
	.tx_trigger		= 0,
	.rx_trigger		= 0,
};

static struct resource palmte_irda_resources[] = {
	[0]	= {
		.start	= INT_UART3,
		.end	= INT_UART3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device palmte_irda_device = {
	.name		= "omapirda",
	.id		= -1,
	.dev		= {
		.platform_data	= &palmte_irda_config,
	},
	.num_resources	= ARRAY_SIZE(palmte_irda_resources),
	.resource	= palmte_irda_resources,
};

static struct platform_device *palmte_devices[] __initdata = {
	&palmte_rom_device,
	&palmte_kp_device,
	&palmte_lcd_device,
	&palmte_backlight_device,
	&palmte_irda_device,
};

static struct omap_usb_config palmte_usb_config __initdata = {
	.register_dev	= 1,	/* Mini-B only receptacle */
	.hmc_mode	= 0,
	.pins[0]	= 2,
};

static struct omap_mmc_config palmte_mmc_config __initdata = {
	.mmc[0]		= {
		.enabled 	= 1,
		.wp_pin		= PALMTE_MMC_WP_GPIO,
		.power_pin	= PALMTE_MMC_POWER_GPIO,
		.switch_pin	= PALMTE_MMC_SWITCH_GPIO,
	},
};

static struct omap_lcd_config palmte_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_uart_config palmte_uart_config __initdata = {
	.enabled_uarts = (1 << 0) | (1 << 1) | (0 << 2),
};

static struct omap_mcbsp_reg_cfg palmte_mcbsp1_regs = {
	.spcr2	= FRST | GRST | XRST | XINTM(3),
	.xcr2	= XDATDLY(1) | XFIG,
	.xcr1	= XWDLEN1(OMAP_MCBSP_WORD_32),
	.pcr0	= SCLKME | FSXP | CLKXP,
};

static struct omap_alsa_codec_config palmte_alsa_config = {
	.name			= "TSC2102 audio",
	.mcbsp_regs_alsa	= &palmte_mcbsp1_regs,
	.codec_configure_dev	= NULL,	/* tsc2102_configure, */
	.codec_set_samplerate	= NULL,	/* tsc2102_set_samplerate, */
	.codec_clock_setup	= NULL,	/* tsc2102_clock_setup, */
	.codec_clock_on		= NULL,	/* tsc2102_clock_on, */
	.codec_clock_off	= NULL,	/* tsc2102_clock_off, */
	.get_default_samplerate	= NULL,	/* tsc2102_get_default_samplerate, */
};

#ifdef CONFIG_APM
/*
 * Values measured in 10 minute intervals averaged over 10 samples.
 * May differ slightly from device to device but should be accurate
 * enough to give basic idea of battery life left and trigger
 * potential alerts.
 */
static const int palmte_battery_sample[] = {
	2194, 2157, 2138, 2120,
	2104, 2089, 2075, 2061,
	2048, 2038, 2026, 2016,
	2008, 1998, 1989, 1980,
	1970, 1958, 1945, 1928,
	1910, 1888, 1860, 1827,
	1791, 1751, 1709, 1656,
};

#define INTERVAL		10
#define BATTERY_HIGH_TRESHOLD	66
#define BATTERY_LOW_TRESHOLD	33

static void palmte_get_power_status(struct apm_power_info *info, int *battery)
{
	int charging, batt, hi, lo, mid;

	charging = !omap_get_gpio_datain(PALMTE_DC_GPIO);
	batt = battery[0];
	if (charging)
		batt -= 60;

	hi = ARRAY_SIZE(palmte_battery_sample);
	lo = 0;

	info->battery_flag = 0;
	info->units = APM_UNITS_MINS;

	if (batt > palmte_battery_sample[lo]) {
		info->battery_life = 100;
		info->time = INTERVAL * ARRAY_SIZE(palmte_battery_sample);
	} else if (batt <= palmte_battery_sample[hi - 1]) {
		info->battery_life = 0;
		info->time = 0;
	} else {
		while (hi > lo + 1) {
			mid = (hi + lo) >> 1;
			if (batt <= palmte_battery_sample[mid])
				lo = mid;
			else
				hi = mid;
		}

		mid = palmte_battery_sample[lo] - palmte_battery_sample[hi];
		hi = palmte_battery_sample[lo] - batt;
		info->battery_life = 100 - (100 * lo + 100 * hi / mid) /
			ARRAY_SIZE(palmte_battery_sample);
		info->time = INTERVAL * (ARRAY_SIZE(palmte_battery_sample) -
				lo) - INTERVAL * hi / mid;
	}

	if (charging) {
		info->ac_line_status = APM_AC_ONLINE;
		info->battery_status = APM_BATTERY_STATUS_CHARGING;
		info->battery_flag |= APM_BATTERY_FLAG_CHARGING;
	} else {
		info->ac_line_status = APM_AC_OFFLINE;
		if (info->battery_life > BATTERY_HIGH_TRESHOLD)
			info->battery_status = APM_BATTERY_STATUS_HIGH;
		else if (info->battery_life > BATTERY_LOW_TRESHOLD)
			info->battery_status = APM_BATTERY_STATUS_LOW;
		else
			info->battery_status = APM_BATTERY_STATUS_CRITICAL;
	}

	if (info->battery_life > BATTERY_HIGH_TRESHOLD)
		info->battery_flag |= APM_BATTERY_FLAG_HIGH;
	else if (info->battery_life > BATTERY_LOW_TRESHOLD)
		info->battery_flag |= APM_BATTERY_FLAG_LOW;
	else
		info->battery_flag |= APM_BATTERY_FLAG_CRITICAL;
}
#else
#define palmte_get_power_status	NULL
#endif

static struct tsc2102_config palmte_tsc2102_config = {
	.use_internal	= 0,
	.monitor	= TSC_BAT1 | TSC_AUX | TSC_TEMP,
	.temp_at25c	= { 2200, 2615 },
	.apm_report	= palmte_get_power_status,
	.alsa_config	= &palmte_alsa_config,
};

static struct omap_board_config_kernel palmte_config[] __initdata = {
	{ OMAP_TAG_USB,		&palmte_usb_config },
	{ OMAP_TAG_MMC,		&palmte_mmc_config },
	{ OMAP_TAG_LCD,		&palmte_lcd_config },
	{ OMAP_TAG_UART,	&palmte_uart_config },
};

static struct spi_board_info palmte_spi_info[] __initdata = {
	{
		.modalias	= "tsc2102",
		.bus_num	= 2,	/* uWire (officially) */
		.chip_select	= 0,	/* As opposed to 3 */
		.irq		= OMAP_GPIO_IRQ(PALMTE_PINTDAV_GPIO),
		.platform_data	= &palmte_tsc2102_config,
		.max_speed_hz	= 8000000,
	},
};

static void palmte_headphones_detect(void *data, int state)
{
	if (state) {
		/* Headphones connected, disable speaker */
		omap_set_gpio_dataout(PALMTE_SPEAKER_GPIO, 0);
		printk(KERN_INFO "PM: speaker off\n");
	} else {
		/* Headphones unplugged, re-enable speaker */
		omap_set_gpio_dataout(PALMTE_SPEAKER_GPIO, 1);
		printk(KERN_INFO "PM: speaker on\n");
	}
}

static void __init palmte_misc_gpio_setup(void)
{
	/* Set TSC2102 PINTDAV pin as input (used by TSC2102 driver) */
	if (omap_request_gpio(PALMTE_PINTDAV_GPIO)) {
		printk(KERN_ERR "Could not reserve PINTDAV GPIO!\n");
		return;
	}
	omap_set_gpio_direction(PALMTE_PINTDAV_GPIO, 1);

	/* Set USB-or-DC-IN pin as input (unused) */
	if (omap_request_gpio(PALMTE_USB_OR_DC_GPIO)) {
		printk(KERN_ERR "Could not reserve cable signal GPIO!\n");
		return;
	}
	omap_set_gpio_direction(PALMTE_USB_OR_DC_GPIO, 1);
}

static void __init omap_palmte_init(void)
{
	omap_board_config = palmte_config;
	omap_board_config_size = ARRAY_SIZE(palmte_config);

	platform_add_devices(palmte_devices, ARRAY_SIZE(palmte_devices));

	spi_register_board_info(palmte_spi_info, ARRAY_SIZE(palmte_spi_info));
	palmte_misc_gpio_setup();
	omap_serial_init();
	omap_register_i2c_bus(1, 100, NULL, 0);
}

static void __init omap_palmte_map_io(void)
{
	omap1_map_common_io();
}

MACHINE_START(OMAP_PALMTE, "OMAP310 based Palm Tungsten E")
	.phys_io	= 0xfff00000,
	.io_pg_offst	= ((0xfef00000) >> 18) & 0xfffc,
	.boot_params	= 0x10000100,
	.map_io		= omap_palmte_map_io,
	.init_irq	= omap_palmte_init_irq,
	.init_machine	= omap_palmte_init,
	.timer		= &omap_timer,
MACHINE_END
