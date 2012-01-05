/*
 * linux/arch/arm/mach-at91/board-cap9adk.c
 *
 *  Copyright (C) 2007 Stelian Pop <stelian.pop@leadtechdesign.com>
 *  Copyright (C) 2007 Lead Tech Design <www.leadtechdesign.com>
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2007 Atmel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/fb.h>
#include <linux/mtd/physmap.h>

#include <video/atmel_lcdc.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/board.h>
#include <mach/at91cap9_matrix.h>
#include <mach/at91sam9_smc.h>
#include <mach/system_rev.h>

#include "sam9_smc.h"
#include "generic.h"


static void __init cap9adk_init_early(void)
{
	/* Initialize processor: 12 MHz crystal */
	at91_initialize(12000000);

	/* Setup the LEDs: USER1 and USER2 LED for cpu/timer... */
	at91_init_leds(AT91_PIN_PA10, AT91_PIN_PA11);
	/* ... POWER LED always on */
	at91_set_gpio_output(AT91_PIN_PC29, 1);

	/* Setup the serial ports and console */
	at91_register_uart(0, 0, 0);		/* DBGU = ttyS0 */
	at91_set_serial_console(0);
}

/*
 * USB Host port
 */
static struct at91_usbh_data __initdata cap9adk_usbh_data = {
	.ports		= 2,
};

/*
 * USB HS Device port
 */
static struct usba_platform_data __initdata cap9adk_usba_udc_data = {
	.vbus_pin	= AT91_PIN_PB31,
};

/*
 * ADS7846 Touchscreen
 */
#if defined(CONFIG_TOUCHSCREEN_ADS7846) || defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
static int ads7843_pendown_state(void)
{
	return !at91_get_gpio_value(AT91_PIN_PC4);	/* Touchscreen PENIRQ */
}

static struct ads7846_platform_data ads_info = {
	.model			= 7843,
	.x_min			= 150,
	.x_max			= 3830,
	.y_min			= 190,
	.y_max			= 3830,
	.vref_delay_usecs	= 100,
	.x_plate_ohms		= 450,
	.y_plate_ohms		= 250,
	.pressure_max		= 15000,
	.debounce_max		= 1,
	.debounce_rep		= 0,
	.debounce_tol		= (~0),
	.get_pendown_state	= ads7843_pendown_state,
};

static void __init cap9adk_add_device_ts(void)
{
	at91_set_gpio_input(AT91_PIN_PC4, 1);	/* Touchscreen PENIRQ */
	at91_set_gpio_input(AT91_PIN_PC5, 1);	/* Touchscreen BUSY */
}
#else
static void __init cap9adk_add_device_ts(void) {}
#endif


/*
 * SPI devices.
 */
static struct spi_board_info cap9adk_spi_devices[] = {
#if defined(CONFIG_MTD_AT91_DATAFLASH_CARD)
	{	/* DataFlash card */
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	},
#endif
#if defined(CONFIG_TOUCHSCREEN_ADS7846) || defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
	{
		.modalias	= "ads7846",
		.chip_select	= 3,		/* can be 2 or 3, depending on J2 jumper */
		.max_speed_hz	= 125000 * 26,	/* (max sample rate @ 3V) * (cmd + data + overhead) */
		.bus_num	= 0,
		.platform_data	= &ads_info,
		.irq		= AT91_PIN_PC4,
	},
#endif
};


/*
 * MCI (SD/MMC)
 */
static struct at91_mmc_data __initdata cap9adk_mmc_data = {
	.wire4		= 1,
//	.det_pin	= ... not connected
//	.wp_pin		= ... not connected
//	.vcc_pin	= ... not connected
};


/*
 * MACB Ethernet device
 */
static struct at91_eth_data __initdata cap9adk_macb_data = {
	.is_rmii	= 1,
};


/*
 * NAND flash
 */
static struct mtd_partition __initdata cap9adk_nand_partitions[] = {
	{
		.name	= "NAND partition",
		.offset	= 0,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct atmel_nand_data __initdata cap9adk_nand_data = {
	.ale		= 21,
	.cle		= 22,
//	.det_pin	= ... not connected
//	.rdy_pin	= ... not connected
	.enable_pin	= AT91_PIN_PD15,
	.parts		= cap9adk_nand_partitions,
	.num_parts	= ARRAY_SIZE(cap9adk_nand_partitions),
};

static struct sam9_smc_config __initdata cap9adk_nand_smc_config = {
	.ncs_read_setup		= 1,
	.nrd_setup		= 2,
	.ncs_write_setup	= 1,
	.nwe_setup		= 2,

	.ncs_read_pulse		= 6,
	.nrd_pulse		= 4,
	.ncs_write_pulse	= 6,
	.nwe_pulse		= 4,

	.read_cycle		= 8,
	.write_cycle		= 8,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE,
	.tdf_cycles		= 1,
};

static void __init cap9adk_add_device_nand(void)
{
	unsigned long csa;

	csa = at91_sys_read(AT91_MATRIX_EBICSA);
	at91_sys_write(AT91_MATRIX_EBICSA, csa | AT91_MATRIX_EBI_VDDIOMSEL_3_3V);

	cap9adk_nand_data.bus_width_16 = board_have_nand_16bit();
	/* setup bus-width (8 or 16) */
	if (cap9adk_nand_data.bus_width_16)
		cap9adk_nand_smc_config.mode |= AT91_SMC_DBW_16;
	else
		cap9adk_nand_smc_config.mode |= AT91_SMC_DBW_8;

	/* configure chip-select 3 (NAND) */
	sam9_smc_configure(3, &cap9adk_nand_smc_config);

	at91_add_device_nand(&cap9adk_nand_data);
}


/*
 * NOR flash
 */
static struct mtd_partition cap9adk_nor_partitions[] = {
	{
		.name		= "NOR partition",
		.offset		= 0,
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data cap9adk_nor_data = {
	.width		= 2,
	.parts		= cap9adk_nor_partitions,
	.nr_parts	= ARRAY_SIZE(cap9adk_nor_partitions),
};

#define NOR_BASE	AT91_CHIPSELECT_0
#define NOR_SIZE	SZ_8M

static struct resource nor_flash_resources[] = {
	{
		.start	= NOR_BASE,
		.end	= NOR_BASE + NOR_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device cap9adk_nor_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
				.platform_data	= &cap9adk_nor_data,
	},
	.resource	= nor_flash_resources,
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
};

static struct sam9_smc_config __initdata cap9adk_nor_smc_config = {
	.ncs_read_setup		= 2,
	.nrd_setup		= 4,
	.ncs_write_setup	= 2,
	.nwe_setup		= 4,

	.ncs_read_pulse		= 10,
	.nrd_pulse		= 8,
	.ncs_write_pulse	= 10,
	.nwe_pulse		= 8,

	.read_cycle		= 16,
	.write_cycle		= 16,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_BAT_WRITE | AT91_SMC_DBW_16,
	.tdf_cycles		= 1,
};

static __init void cap9adk_add_device_nor(void)
{
	unsigned long csa;

	csa = at91_sys_read(AT91_MATRIX_EBICSA);
	at91_sys_write(AT91_MATRIX_EBICSA, csa | AT91_MATRIX_EBI_VDDIOMSEL_3_3V);

	/* configure chip-select 0 (NOR) */
	sam9_smc_configure(0, &cap9adk_nor_smc_config);

	platform_device_register(&cap9adk_nor_flash);
}


/*
 * LCD Controller
 */
#if defined(CONFIG_FB_ATMEL) || defined(CONFIG_FB_ATMEL_MODULE)
static struct fb_videomode at91_tft_vga_modes[] = {
	{
	        .name           = "TX09D50VM1CCA @ 60",
		.refresh	= 60,
		.xres		= 240,		.yres		= 320,
		.pixclock	= KHZ2PICOS(4965),

		.left_margin	= 1,		.right_margin	= 33,
		.upper_margin	= 1,		.lower_margin	= 0,
		.hsync_len	= 5,		.vsync_len	= 1,

		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs at91fb_default_monspecs = {
	.manufacturer	= "HIT",
	.monitor        = "TX09D70VM1CCA",

	.modedb		= at91_tft_vga_modes,
	.modedb_len	= ARRAY_SIZE(at91_tft_vga_modes),
	.hfmin		= 15000,
	.hfmax		= 64000,
	.vfmin		= 50,
	.vfmax		= 150,
};

#define AT91CAP9_DEFAULT_LCDCON2 	(ATMEL_LCDC_MEMOR_LITTLE \
					| ATMEL_LCDC_DISTYPE_TFT    \
					| ATMEL_LCDC_CLKMOD_ALWAYSACTIVE)

static void at91_lcdc_power_control(int on)
{
	if (on)
		at91_set_gpio_value(AT91_PIN_PC0, 0);	/* power up */
	else
		at91_set_gpio_value(AT91_PIN_PC0, 1);	/* power down */
}

/* Driver datas */
static struct atmel_lcdfb_info __initdata cap9adk_lcdc_data = {
	.default_bpp			= 16,
	.default_dmacon			= ATMEL_LCDC_DMAEN,
	.default_lcdcon2		= AT91CAP9_DEFAULT_LCDCON2,
	.default_monspecs		= &at91fb_default_monspecs,
	.atmel_lcdfb_power_control	= at91_lcdc_power_control,
	.guard_time			= 1,
};

#else
static struct atmel_lcdfb_info __initdata cap9adk_lcdc_data;
#endif


/*
 * AC97
 */
static struct ac97c_platform_data cap9adk_ac97_data = {
//	.reset_pin	= ... not connected
};


static void __init cap9adk_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* USB Host */
	at91_add_device_usbh(&cap9adk_usbh_data);
	/* USB HS */
	at91_add_device_usba(&cap9adk_usba_udc_data);
	/* SPI */
	at91_add_device_spi(cap9adk_spi_devices, ARRAY_SIZE(cap9adk_spi_devices));
	/* Touchscreen */
	cap9adk_add_device_ts();
	/* MMC */
	at91_add_device_mmc(1, &cap9adk_mmc_data);
	/* Ethernet */
	at91_add_device_eth(&cap9adk_macb_data);
	/* NAND */
	cap9adk_add_device_nand();
	/* NOR Flash */
	cap9adk_add_device_nor();
	/* I2C */
	at91_add_device_i2c(NULL, 0);
	/* LCD Controller */
	at91_add_device_lcdc(&cap9adk_lcdc_data);
	/* AC97 */
	at91_add_device_ac97(&cap9adk_ac97_data);
}

MACHINE_START(AT91CAP9ADK, "Atmel AT91CAP9A-DK")
	/* Maintainer: Stelian Pop <stelian.pop@leadtechdesign.com> */
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.init_early	= cap9adk_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= cap9adk_board_init,
MACHINE_END
