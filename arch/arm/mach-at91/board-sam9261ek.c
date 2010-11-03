/*
 * linux/arch/arm/mach-at91/board-sam9261ek.c
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2006 Atmel
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
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/spi/at73c213.h>
#include <linux/clk.h>
#include <linux/dm9000.h>
#include <linux/fb.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>

#include <video/atmel_lcdc.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/at91sam9_smc.h>
#include <mach/at91_shdwc.h>

#include "sam9_smc.h"
#include "generic.h"


static void __init ek_map_io(void)
{
	/* Initialize processor: 18.432 MHz crystal */
	at91sam9261_initialize(18432000);

	/* Setup the LEDs */
	at91_init_leds(AT91_PIN_PA13, AT91_PIN_PA14);

	/* DBGU on ttyS0. (Rx & Tx only) */
	at91_register_uart(0, 0, 0);

	/* set serial console to ttyS0 (ie, DBGU) */
	at91_set_serial_console(0);
}

static void __init ek_init_irq(void)
{
	at91sam9261_init_interrupts(NULL);
}


/*
 * DM9000 ethernet device
 */
#if defined(CONFIG_DM9000)
static struct resource dm9000_resource[] = {
	[0] = {
		.start	= AT91_CHIPSELECT_2,
		.end	= AT91_CHIPSELECT_2 + 3,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start	= AT91_CHIPSELECT_2 + 0x44,
		.end	= AT91_CHIPSELECT_2 + 0xFF,
		.flags	= IORESOURCE_MEM
	},
	[2] = {
		.start	= AT91_PIN_PC11,
		.end	= AT91_PIN_PC11,
		.flags	= IORESOURCE_IRQ
			| IORESOURCE_IRQ_LOWEDGE | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct dm9000_plat_data dm9000_platdata = {
	.flags		= DM9000_PLATF_16BITONLY | DM9000_PLATF_NO_EEPROM,
};

static struct platform_device dm9000_device = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dm9000_resource),
	.resource	= dm9000_resource,
	.dev		= {
		.platform_data	= &dm9000_platdata,
	}
};

/*
 * SMC timings for the DM9000.
 * Note: These timings were calculated for MASTER_CLOCK = 100000000 according to the DM9000 timings.
 */
static struct sam9_smc_config __initdata dm9000_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 2,
	.ncs_write_setup	= 0,
	.nwe_setup		= 2,

	.ncs_read_pulse		= 8,
	.nrd_pulse		= 4,
	.ncs_write_pulse	= 8,
	.nwe_pulse		= 4,

	.read_cycle		= 16,
	.write_cycle		= 16,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE | AT91_SMC_BAT_WRITE | AT91_SMC_DBW_16,
	.tdf_cycles		= 1,
};

static void __init ek_add_device_dm9000(void)
{
	/* Configure chip-select 2 (DM9000) */
	sam9_smc_configure(2, &dm9000_smc_config);

	/* Configure Reset signal as output */
	at91_set_gpio_output(AT91_PIN_PC10, 0);

	/* Configure Interrupt pin as input, no pull-up */
	at91_set_gpio_input(AT91_PIN_PC11, 0);

	platform_device_register(&dm9000_device);
}
#else
static void __init ek_add_device_dm9000(void) {}
#endif /* CONFIG_DM9000 */


/*
 * USB Host Port
 */
static struct at91_usbh_data __initdata ek_usbh_data = {
	.ports		= 2,
};


/*
 * USB Device Port
 */
static struct at91_udc_data __initdata ek_udc_data = {
	.vbus_pin	= AT91_PIN_PB29,
	.pullup_pin	= 0,		/* pull-up driven by UDC */
};


/*
 * NAND flash
 */
static struct mtd_partition __initdata ek_nand_partition[] = {
	{
		.name	= "Partition 1",
		.offset	= 0,
		.size	= SZ_256K,
	},
	{
		.name	= "Partition 2",
		.offset	= MTDPART_OFS_NXTBLK,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct mtd_partition * __init nand_partitions(int size, int *num_partitions)
{
	*num_partitions = ARRAY_SIZE(ek_nand_partition);
	return ek_nand_partition;
}

static struct atmel_nand_data __initdata ek_nand_data = {
	.ale		= 22,
	.cle		= 21,
//	.det_pin	= ... not connected
	.rdy_pin	= AT91_PIN_PC15,
	.enable_pin	= AT91_PIN_PC14,
	.partition_info	= nand_partitions,
#if defined(CONFIG_MTD_NAND_ATMEL_BUSWIDTH_16)
	.bus_width_16	= 1,
#else
	.bus_width_16	= 0,
#endif
};

static struct sam9_smc_config __initdata ek_nand_smc_config = {
	.ncs_read_setup		= 0,
	.nrd_setup		= 1,
	.ncs_write_setup	= 0,
	.nwe_setup		= 1,

	.ncs_read_pulse		= 3,
	.nrd_pulse		= 3,
	.ncs_write_pulse	= 3,
	.nwe_pulse		= 3,

	.read_cycle		= 5,
	.write_cycle		= 5,

	.mode			= AT91_SMC_READMODE | AT91_SMC_WRITEMODE | AT91_SMC_EXNWMODE_DISABLE,
	.tdf_cycles		= 2,
};

static void __init ek_add_device_nand(void)
{
	/* setup bus-width (8 or 16) */
	if (ek_nand_data.bus_width_16)
		ek_nand_smc_config.mode |= AT91_SMC_DBW_16;
	else
		ek_nand_smc_config.mode |= AT91_SMC_DBW_8;

	/* configure chip-select 3 (NAND) */
	sam9_smc_configure(3, &ek_nand_smc_config);

	at91_add_device_nand(&ek_nand_data);
}

/*
 * SPI related devices
 */
#if defined(CONFIG_SPI_ATMEL) || defined(CONFIG_SPI_ATMEL_MODULE)

/*
 * ADS7846 Touchscreen
 */
#if defined(CONFIG_TOUCHSCREEN_ADS7846) || defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)

static int ads7843_pendown_state(void)
{
	return !at91_get_gpio_value(AT91_PIN_PC2);	/* Touchscreen PENIRQ */
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

static void __init ek_add_device_ts(void)
{
	at91_set_B_periph(AT91_PIN_PC2, 1);	/* External IRQ0, with pullup */
	at91_set_gpio_input(AT91_PIN_PA11, 1);	/* Touchscreen BUSY signal */
}
#else
static void __init ek_add_device_ts(void) {}
#endif

/*
 * Audio
 */
static struct at73c213_board_info at73c213_data = {
	.ssc_id		= 1,
#if defined(CONFIG_MACH_AT91SAM9261EK)
	.shortname	= "AT91SAM9261-EK external DAC",
#else
	.shortname	= "AT91SAM9G10-EK external DAC",
#endif
};

#if defined(CONFIG_SND_AT73C213) || defined(CONFIG_SND_AT73C213_MODULE)
static void __init at73c213_set_clk(struct at73c213_board_info *info)
{
	struct clk *pck2;
	struct clk *plla;

	pck2 = clk_get(NULL, "pck2");
	plla = clk_get(NULL, "plla");

	/* AT73C213 MCK Clock */
	at91_set_B_periph(AT91_PIN_PB31, 0);	/* PCK2 */

	clk_set_parent(pck2, plla);
	clk_put(plla);

	info->dac_clk = pck2;
}
#else
static void __init at73c213_set_clk(struct at73c213_board_info *info) {}
#endif

/*
 * SPI devices
 */
static struct spi_board_info ek_spi_devices[] = {
	{	/* DataFlash chip */
		.modalias	= "mtd_dataflash",
		.chip_select	= 0,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	},
#if defined(CONFIG_TOUCHSCREEN_ADS7846) || defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
	{
		.modalias	= "ads7846",
		.chip_select	= 2,
		.max_speed_hz	= 125000 * 26,	/* (max sample rate @ 3V) * (cmd + data + overhead) */
		.bus_num	= 0,
		.platform_data	= &ads_info,
		.irq		= AT91SAM9261_ID_IRQ0,
		.controller_data = (void *) AT91_PIN_PA28,	/* CS pin */
	},
#endif
#if defined(CONFIG_MTD_AT91_DATAFLASH_CARD)
	{	/* DataFlash card - jumper (J12) configurable to CS3 or CS0 */
		.modalias	= "mtd_dataflash",
		.chip_select	= 3,
		.max_speed_hz	= 15 * 1000 * 1000,
		.bus_num	= 0,
	},
#elif defined(CONFIG_SND_AT73C213) || defined(CONFIG_SND_AT73C213_MODULE)
	{	/* AT73C213 DAC */
		.modalias	= "at73c213",
		.chip_select	= 3,
		.max_speed_hz	= 10 * 1000 * 1000,
		.bus_num	= 0,
		.mode		= SPI_MODE_1,
		.platform_data	= &at73c213_data,
		.controller_data = (void*) AT91_PIN_PA29,	/* default for CS3 is PA6, but it must be PA29 */
	},
#endif
};

#else /* CONFIG_SPI_ATMEL_* */
/* spi0 and mmc/sd share the same PIO pins: cannot be used at the same time */

/*
 * MCI (SD/MMC)
 * det_pin, wp_pin and vcc_pin are not connected
 */
static struct at91_mmc_data __initdata ek_mmc_data = {
	.wire4		= 1,
};

#endif /* CONFIG_SPI_ATMEL_* */


/*
 * LCD Controller
 */
#if defined(CONFIG_FB_ATMEL) || defined(CONFIG_FB_ATMEL_MODULE)

#if defined(CONFIG_FB_ATMEL_STN)

/* STN */
static struct fb_videomode at91_stn_modes[] = {
        {
		.name           = "SP06Q002 @ 75",
		.refresh        = 75,
		.xres           = 320,          .yres           = 240,
		.pixclock       = KHZ2PICOS(1440),

		.left_margin    = 1,            .right_margin   = 1,
		.upper_margin   = 0,            .lower_margin   = 0,
		.hsync_len      = 1,            .vsync_len      = 1,

		.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.vmode          = FB_VMODE_NONINTERLACED,
        },
};

static struct fb_monspecs at91fb_default_stn_monspecs = {
        .manufacturer   = "HIT",
        .monitor        = "SP06Q002",

        .modedb         = at91_stn_modes,
        .modedb_len     = ARRAY_SIZE(at91_stn_modes),
        .hfmin          = 15000,
        .hfmax          = 64000,
        .vfmin          = 50,
        .vfmax          = 150,
};

#define AT91SAM9261_DEFAULT_STN_LCDCON2	(ATMEL_LCDC_MEMOR_LITTLE \
					| ATMEL_LCDC_DISTYPE_STNMONO \
					| ATMEL_LCDC_CLKMOD_ALWAYSACTIVE \
					| ATMEL_LCDC_IFWIDTH_4 \
					| ATMEL_LCDC_SCANMOD_SINGLE)

static void at91_lcdc_stn_power_control(int on)
{
	/* backlight */
	if (on) {	/* power up */
		at91_set_gpio_value(AT91_PIN_PC14, 0);
		at91_set_gpio_value(AT91_PIN_PC15, 0);
	} else {	/* power down */
		at91_set_gpio_value(AT91_PIN_PC14, 1);
		at91_set_gpio_value(AT91_PIN_PC15, 1);
	}
}

static struct atmel_lcdfb_info __initdata ek_lcdc_data = {
	.default_bpp			= 1,
	.default_dmacon			= ATMEL_LCDC_DMAEN,
	.default_lcdcon2		= AT91SAM9261_DEFAULT_STN_LCDCON2,
	.default_monspecs		= &at91fb_default_stn_monspecs,
	.atmel_lcdfb_power_control	= at91_lcdc_stn_power_control,
	.guard_time			= 1,
#if defined(CONFIG_MACH_AT91SAM9G10EK)
	.lcd_wiring_mode		= ATMEL_LCDC_WIRING_RGB,
#endif
};

#else

/* TFT */
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

static struct fb_monspecs at91fb_default_tft_monspecs = {
	.manufacturer	= "HIT",
	.monitor        = "TX09D50VM1CCA",

	.modedb		= at91_tft_vga_modes,
	.modedb_len	= ARRAY_SIZE(at91_tft_vga_modes),
	.hfmin		= 15000,
	.hfmax		= 64000,
	.vfmin		= 50,
	.vfmax		= 150,
};

#define AT91SAM9261_DEFAULT_TFT_LCDCON2	(ATMEL_LCDC_MEMOR_LITTLE \
					| ATMEL_LCDC_DISTYPE_TFT    \
					| ATMEL_LCDC_CLKMOD_ALWAYSACTIVE)

static void at91_lcdc_tft_power_control(int on)
{
	if (on)
		at91_set_gpio_value(AT91_PIN_PA12, 0);	/* power up */
	else
		at91_set_gpio_value(AT91_PIN_PA12, 1);	/* power down */
}

static struct atmel_lcdfb_info __initdata ek_lcdc_data = {
	.lcdcon_is_backlight		= true,
	.default_bpp			= 16,
	.default_dmacon			= ATMEL_LCDC_DMAEN,
	.default_lcdcon2		= AT91SAM9261_DEFAULT_TFT_LCDCON2,
	.default_monspecs		= &at91fb_default_tft_monspecs,
	.atmel_lcdfb_power_control	= at91_lcdc_tft_power_control,
	.guard_time			= 1,
#if defined(CONFIG_MACH_AT91SAM9G10EK)
	.lcd_wiring_mode		= ATMEL_LCDC_WIRING_RGB,
#endif
};
#endif

#else
static struct atmel_lcdfb_info __initdata ek_lcdc_data;
#endif


/*
 * GPIO Buttons
 */
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button ek_buttons[] = {
	{
		.gpio		= AT91_PIN_PA27,
		.code		= BTN_0,
		.desc		= "Button 0",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= AT91_PIN_PA26,
		.code		= BTN_1,
		.desc		= "Button 1",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= AT91_PIN_PA25,
		.code		= BTN_2,
		.desc		= "Button 2",
		.active_low	= 1,
		.wakeup		= 1,
	},
	{
		.gpio		= AT91_PIN_PA24,
		.code		= BTN_3,
		.desc		= "Button 3",
		.active_low	= 1,
		.wakeup		= 1,
	}
};

static struct gpio_keys_platform_data ek_button_data = {
	.buttons	= ek_buttons,
	.nbuttons	= ARRAY_SIZE(ek_buttons),
};

static struct platform_device ek_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &ek_button_data,
	}
};

static void __init ek_add_device_buttons(void)
{
	at91_set_gpio_input(AT91_PIN_PA27, 1);	/* btn0 */
	at91_set_deglitch(AT91_PIN_PA27, 1);
	at91_set_gpio_input(AT91_PIN_PA26, 1);	/* btn1 */
	at91_set_deglitch(AT91_PIN_PA26, 1);
	at91_set_gpio_input(AT91_PIN_PA25, 1);	/* btn2 */
	at91_set_deglitch(AT91_PIN_PA25, 1);
	at91_set_gpio_input(AT91_PIN_PA24, 1);	/* btn3 */
	at91_set_deglitch(AT91_PIN_PA24, 1);

	platform_device_register(&ek_button_device);
}
#else
static void __init ek_add_device_buttons(void) {}
#endif

/*
 * LEDs
 */
static struct gpio_led ek_leds[] = {
	{	/* "bottom" led, green, userled1 to be defined */
		.name			= "ds7",
		.gpio			= AT91_PIN_PA14,
		.active_low		= 1,
		.default_trigger	= "none",
	},
	{	/* "top" led, green, userled2 to be defined */
		.name			= "ds8",
		.gpio			= AT91_PIN_PA13,
		.active_low		= 1,
		.default_trigger	= "none",
	},
	{	/* "power" led, yellow */
		.name			= "ds1",
		.gpio			= AT91_PIN_PA23,
		.default_trigger	= "heartbeat",
	}
};

static void __init ek_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* USB Host */
	at91_add_device_usbh(&ek_usbh_data);
	/* USB Device */
	at91_add_device_udc(&ek_udc_data);
	/* I2C */
	at91_add_device_i2c(NULL, 0);
	/* NAND */
	ek_add_device_nand();
	/* DM9000 ethernet */
	ek_add_device_dm9000();

	/* spi0 and mmc/sd share the same PIO pins */
#if defined(CONFIG_SPI_ATMEL) || defined(CONFIG_SPI_ATMEL_MODULE)
	/* SPI */
	at91_add_device_spi(ek_spi_devices, ARRAY_SIZE(ek_spi_devices));
	/* Touchscreen */
	ek_add_device_ts();
	/* SSC (to AT73C213) */
	at73c213_set_clk(&at73c213_data);
	at91_add_device_ssc(AT91SAM9261_ID_SSC1, ATMEL_SSC_TX);
#else
	/* MMC */
	at91_add_device_mmc(0, &ek_mmc_data);
#endif
	/* LCD Controller */
	at91_add_device_lcdc(&ek_lcdc_data);
	/* Push Buttons */
	ek_add_device_buttons();
	/* LEDs */
	at91_gpio_leds(ek_leds, ARRAY_SIZE(ek_leds));
}

#if defined(CONFIG_MACH_AT91SAM9261EK)
MACHINE_START(AT91SAM9261EK, "Atmel AT91SAM9261-EK")
#else
MACHINE_START(AT91SAM9G10EK, "Atmel AT91SAM9G10-EK")
#endif
	/* Maintainer: Atmel */
	.boot_params	= AT91_SDRAM_BASE + 0x100,
	.timer		= &at91sam926x_timer,
	.map_io		= ek_map_io,
	.init_irq	= ek_init_irq,
	.init_machine	= ek_board_init,
MACHINE_END
