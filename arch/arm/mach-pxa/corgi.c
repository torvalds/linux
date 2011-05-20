/*
 * Support for Sharp SL-C7xx PDAs
 * Models: SL-C700 (Corgi), SL-C750 (Shepherd), SL-C760 (Husky)
 *
 * Copyright (c) 2004-2005 Richard Purdie
 *
 * Based on Sharp's 2.4 kernel patches/lubbock.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/mmc/host.h>
#include <linux/mtd/physmap.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/backlight.h>
#include <linux/i2c.h>
#include <linux/i2c/pxa-i2c.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/spi/corgi_lcd.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/mtd/sharpsl.h>
#include <linux/input/matrix_keypad.h>
#include <video/w100fb.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/system.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/pxa25x.h>
#include <mach/irda.h>
#include <mach/mmc.h>
#include <mach/udc.h>
#include <mach/corgi.h>
#include <mach/sharpsl_pm.h>

#include <asm/mach/sharpsl_param.h>
#include <asm/hardware/scoop.h>

#include "generic.h"
#include "devices.h"

static unsigned long corgi_pin_config[] __initdata = {
	/* Static Memory I/O */
	GPIO78_nCS_2,	/* w100fb */
	GPIO80_nCS_4,	/* scoop */

	/* SSP1 */
	GPIO23_SSP1_SCLK,
	GPIO25_SSP1_TXD,
	GPIO26_SSP1_RXD,
	GPIO24_GPIO,	/* CORGI_GPIO_ADS7846_CS - SFRM as chip select */

	/* I2S */
	GPIO28_I2S_BITCLK_OUT,
	GPIO29_I2S_SDATA_IN,
	GPIO30_I2S_SDATA_OUT,
	GPIO31_I2S_SYNC,
	GPIO32_I2S_SYSCLK,

	/* Infra-Red */
	GPIO47_FICP_TXD,
	GPIO46_FICP_RXD,

	/* FFUART */
	GPIO40_FFUART_DTR,
	GPIO41_FFUART_RTS,
	GPIO39_FFUART_TXD,
	GPIO37_FFUART_DSR,
	GPIO34_FFUART_RXD,
	GPIO35_FFUART_CTS,

	/* PC Card */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO52_nPCE_1,
	GPIO53_nPCE_2,
	GPIO54_nPSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,

	/* MMC */
	GPIO6_MMC_CLK,
	GPIO8_MMC_CS0,

	/* GPIO Matrix Keypad */
	GPIO66_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 0 */
	GPIO67_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 1 */
	GPIO68_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 2 */
	GPIO69_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 3 */
	GPIO70_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 4 */
	GPIO71_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 5 */
	GPIO72_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 6 */
	GPIO73_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 7 */
	GPIO74_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 8 */
	GPIO75_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 9 */
	GPIO76_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 10 */
	GPIO77_GPIO | MFP_LPM_DRIVE_HIGH,	/* column 11 */
	GPIO58_GPIO,	/* row 0 */
	GPIO59_GPIO,	/* row 1 */
	GPIO60_GPIO,	/* row 2 */
	GPIO61_GPIO,	/* row 3 */
	GPIO62_GPIO,	/* row 4 */
	GPIO63_GPIO,	/* row 5 */
	GPIO64_GPIO,	/* row 6 */
	GPIO65_GPIO,	/* row 7 */

	/* GPIO */
	GPIO9_GPIO,				/* CORGI_GPIO_nSD_DETECT */
	GPIO7_GPIO,				/* CORGI_GPIO_nSD_WP */
	GPIO11_GPIO | WAKEUP_ON_EDGE_BOTH,	/* CORGI_GPIO_MAIN_BAT_{LOW,COVER} */
	GPIO13_GPIO | MFP_LPM_KEEP_OUTPUT,	/* CORGI_GPIO_LED_ORANGE */
	GPIO21_GPIO,				/* CORGI_GPIO_ADC_TEMP */
	GPIO22_GPIO,				/* CORGI_GPIO_IR_ON */
	GPIO33_GPIO,				/* CORGI_GPIO_SD_PWR */
	GPIO38_GPIO | MFP_LPM_KEEP_OUTPUT,	/* CORGI_GPIO_CHRG_ON */
	GPIO43_GPIO | MFP_LPM_KEEP_OUTPUT,	/* CORGI_GPIO_CHRG_UKN */
	GPIO44_GPIO,				/* CORGI_GPIO_HSYNC */

	GPIO0_GPIO | WAKEUP_ON_EDGE_BOTH,	/* CORGI_GPIO_KEY_INT */
	GPIO1_GPIO | WAKEUP_ON_EDGE_RISE,	/* CORGI_GPIO_AC_IN */
	GPIO3_GPIO | WAKEUP_ON_EDGE_BOTH,	/* CORGI_GPIO_WAKEUP */
};

/*
 * Corgi SCOOP Device
 */
static struct resource corgi_scoop_resources[] = {
	[0] = {
		.start		= 0x10800000,
		.end		= 0x10800fff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct scoop_config corgi_scoop_setup = {
	.io_dir 	= CORGI_SCOOP_IO_DIR,
	.io_out		= CORGI_SCOOP_IO_OUT,
	.gpio_base	= CORGI_SCOOP_GPIO_BASE,
};

struct platform_device corgiscoop_device = {
	.name		= "sharp-scoop",
	.id		= -1,
	.dev		= {
 		.platform_data	= &corgi_scoop_setup,
	},
	.num_resources	= ARRAY_SIZE(corgi_scoop_resources),
	.resource	= corgi_scoop_resources,
};

static struct scoop_pcmcia_dev corgi_pcmcia_scoop[] = {
{
	.dev        = &corgiscoop_device.dev,
	.irq        = CORGI_IRQ_GPIO_CF_IRQ,
	.cd_irq     = CORGI_IRQ_GPIO_CF_CD,
	.cd_irq_str = "PCMCIA0 CD",
},
};

static struct scoop_pcmcia_config corgi_pcmcia_config = {
	.devs         = &corgi_pcmcia_scoop[0],
	.num_devs     = 1,
};

static struct w100_mem_info corgi_fb_mem = {
	.ext_cntl          = 0x00040003,
	.sdram_mode_reg    = 0x00650021,
	.ext_timing_cntl   = 0x10002a4a,
	.io_cntl           = 0x7ff87012,
	.size              = 0x1fffff,
};

static struct w100_gen_regs corgi_fb_regs = {
	.lcd_format    = 0x00000003,
	.lcdd_cntl1    = 0x01CC0000,
	.lcdd_cntl2    = 0x0003FFFF,
	.genlcd_cntl1  = 0x00FFFF0D,
	.genlcd_cntl2  = 0x003F3003,
	.genlcd_cntl3  = 0x000102aa,
};

static struct w100_gpio_regs corgi_fb_gpio = {
	.init_data1   = 0x000000bf,
	.init_data2   = 0x00000000,
	.gpio_dir1    = 0x00000000,
	.gpio_oe1     = 0x03c0feff,
	.gpio_dir2    = 0x00000000,
	.gpio_oe2     = 0x00000000,
};

static struct w100_mode corgi_fb_modes[] = {
{
	.xres            = 480,
	.yres            = 640,
	.left_margin     = 0x56,
	.right_margin    = 0x55,
	.upper_margin    = 0x03,
	.lower_margin    = 0x00,
	.crtc_ss         = 0x82360056,
	.crtc_ls         = 0xA0280000,
	.crtc_gs         = 0x80280028,
	.crtc_vpos_gs    = 0x02830002,
	.crtc_rev        = 0x00400008,
	.crtc_dclk       = 0xA0000000,
	.crtc_gclk       = 0x8015010F,
	.crtc_goe        = 0x80100110,
	.crtc_ps1_active = 0x41060010,
	.pll_freq        = 75,
	.fast_pll_freq   = 100,
	.sysclk_src      = CLK_SRC_PLL,
	.sysclk_divider  = 0,
	.pixclk_src      = CLK_SRC_PLL,
	.pixclk_divider  = 2,
	.pixclk_divider_rotated = 6,
},{
	.xres            = 240,
	.yres            = 320,
	.left_margin     = 0x27,
	.right_margin    = 0x2e,
	.upper_margin    = 0x01,
	.lower_margin    = 0x00,
	.crtc_ss         = 0x81170027,
	.crtc_ls         = 0xA0140000,
	.crtc_gs         = 0xC0140014,
	.crtc_vpos_gs    = 0x00010141,
	.crtc_rev        = 0x00400008,
	.crtc_dclk       = 0xA0000000,
	.crtc_gclk       = 0x8015010F,
	.crtc_goe        = 0x80100110,
	.crtc_ps1_active = 0x41060010,
	.pll_freq        = 0,
	.fast_pll_freq   = 0,
	.sysclk_src      = CLK_SRC_XTAL,
	.sysclk_divider  = 0,
	.pixclk_src      = CLK_SRC_XTAL,
	.pixclk_divider  = 1,
	.pixclk_divider_rotated = 1,
},

};

static struct w100fb_mach_info corgi_fb_info = {
	.init_mode  = INIT_MODE_ROTATED,
	.mem        = &corgi_fb_mem,
	.regs       = &corgi_fb_regs,
	.modelist   = &corgi_fb_modes[0],
	.num_modes  = 2,
	.gpio       = &corgi_fb_gpio,
	.xtal_freq  = 12500000,
	.xtal_dbl   = 0,
};

static struct resource corgi_fb_resources[] = {
	[0] = {
		.start   = 0x08000000,
		.end     = 0x08ffffff,
		.flags   = IORESOURCE_MEM,
	},
};

static struct platform_device corgifb_device = {
	.name           = "w100fb",
	.id             = -1,
	.num_resources	= ARRAY_SIZE(corgi_fb_resources),
	.resource	= corgi_fb_resources,
	.dev            = {
		.platform_data = &corgi_fb_info,
	},

};

/*
 * Corgi Keyboard Device
 */
#define CORGI_KEY_CALENDER	KEY_F1
#define CORGI_KEY_ADDRESS	KEY_F2
#define CORGI_KEY_FN		KEY_F3
#define CORGI_KEY_CANCEL	KEY_F4
#define CORGI_KEY_OFF		KEY_SUSPEND
#define CORGI_KEY_EXOK		KEY_F5
#define CORGI_KEY_EXCANCEL	KEY_F6
#define CORGI_KEY_EXJOGDOWN	KEY_F7
#define CORGI_KEY_EXJOGUP	KEY_F8
#define CORGI_KEY_JAP1		KEY_LEFTCTRL
#define CORGI_KEY_JAP2		KEY_LEFTALT
#define CORGI_KEY_MAIL		KEY_F10
#define CORGI_KEY_OK		KEY_F11
#define CORGI_KEY_MENU		KEY_F12

static const uint32_t corgikbd_keymap[] = {
	KEY(0, 1, KEY_1),
	KEY(0, 2, KEY_3),
	KEY(0, 3, KEY_5),
	KEY(0, 4, KEY_6),
	KEY(0, 5, KEY_7),
	KEY(0, 6, KEY_9),
	KEY(0, 7, KEY_0),
	KEY(0, 8, KEY_BACKSPACE),
	KEY(1, 1, KEY_2),
	KEY(1, 2, KEY_4),
	KEY(1, 3, KEY_R),
	KEY(1, 4, KEY_Y),
	KEY(1, 5, KEY_8),
	KEY(1, 6, KEY_I),
	KEY(1, 7, KEY_O),
	KEY(1, 8, KEY_P),
	KEY(2, 0, KEY_TAB),
	KEY(2, 1, KEY_Q),
	KEY(2, 2, KEY_E),
	KEY(2, 3, KEY_T),
	KEY(2, 4, KEY_G),
	KEY(2, 5, KEY_U),
	KEY(2, 6, KEY_J),
	KEY(2, 7, KEY_K),
	KEY(3, 0, CORGI_KEY_CALENDER),
	KEY(3, 1, KEY_W),
	KEY(3, 2, KEY_S),
	KEY(3, 3, KEY_F),
	KEY(3, 4, KEY_V),
	KEY(3, 5, KEY_H),
	KEY(3, 6, KEY_M),
	KEY(3, 7, KEY_L),
	KEY(3, 9, KEY_RIGHTSHIFT),
	KEY(4, 0, CORGI_KEY_ADDRESS),
	KEY(4, 1, KEY_A),
	KEY(4, 2, KEY_D),
	KEY(4, 3, KEY_C),
	KEY(4, 4, KEY_B),
	KEY(4, 5, KEY_N),
	KEY(4, 6, KEY_DOT),
	KEY(4, 8, KEY_ENTER),
	KEY(4, 10, KEY_LEFTSHIFT),
	KEY(5, 0, CORGI_KEY_MAIL),
	KEY(5, 1, KEY_Z),
	KEY(5, 2, KEY_X),
	KEY(5, 3, KEY_MINUS),
	KEY(5, 4, KEY_SPACE),
	KEY(5, 5, KEY_COMMA),
	KEY(5, 7, KEY_UP),
	KEY(5, 11, CORGI_KEY_FN),
	KEY(6, 0, KEY_SYSRQ),
	KEY(6, 1, CORGI_KEY_JAP1),
	KEY(6, 2, CORGI_KEY_JAP2),
	KEY(6, 3, CORGI_KEY_CANCEL),
	KEY(6, 4, CORGI_KEY_OK),
	KEY(6, 5, CORGI_KEY_MENU),
	KEY(6, 6, KEY_LEFT),
	KEY(6, 7, KEY_DOWN),
	KEY(6, 8, KEY_RIGHT),
	KEY(7, 0, CORGI_KEY_OFF),
	KEY(7, 1, CORGI_KEY_EXOK),
	KEY(7, 2, CORGI_KEY_EXCANCEL),
	KEY(7, 3, CORGI_KEY_EXJOGDOWN),
	KEY(7, 4, CORGI_KEY_EXJOGUP),
};

static struct matrix_keymap_data corgikbd_keymap_data = {
	.keymap		= corgikbd_keymap,
	.keymap_size	= ARRAY_SIZE(corgikbd_keymap),
};

static const int corgikbd_row_gpios[] =
		{ 58, 59, 60, 61, 62, 63, 64, 65 };
static const int corgikbd_col_gpios[] =
		{ 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77 };

static struct matrix_keypad_platform_data corgikbd_pdata = {
	.keymap_data		= &corgikbd_keymap_data,
	.row_gpios		= corgikbd_row_gpios,
	.col_gpios		= corgikbd_col_gpios,
	.num_row_gpios		= ARRAY_SIZE(corgikbd_row_gpios),
	.num_col_gpios		= ARRAY_SIZE(corgikbd_col_gpios),
	.col_scan_delay_us	= 10,
	.debounce_ms		= 10,
	.wakeup			= 1,
};

static struct platform_device corgikbd_device = {
	.name		= "matrix-keypad",
	.id		= -1,
	.dev		= {
		.platform_data = &corgikbd_pdata,
	},
};

/*
 * Corgi LEDs
 */
static struct gpio_led corgi_gpio_leds[] = {
	{
		.name			= "corgi:amber:charge",
		.default_trigger	= "sharpsl-charge",
		.gpio			= CORGI_GPIO_LED_ORANGE,
	},
	{
		.name			= "corgi:green:mail",
		.default_trigger	= "nand-disk",
		.gpio			= CORGI_GPIO_LED_GREEN,
	},
};

static struct gpio_led_platform_data corgi_gpio_leds_info = {
	.leds		= corgi_gpio_leds,
	.num_leds	= ARRAY_SIZE(corgi_gpio_leds),
};

static struct platform_device corgiled_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data = &corgi_gpio_leds_info,
	},
};

/*
 * MMC/SD Device
 *
 * The card detect interrupt isn't debounced so we delay it by 250ms
 * to give the card a chance to fully insert/eject.
 */
static struct pxamci_platform_data corgi_mci_platform_data = {
	.detect_delay_ms	= 250,
	.ocr_mask		= MMC_VDD_32_33|MMC_VDD_33_34,
	.gpio_card_detect	= CORGI_GPIO_nSD_DETECT,
	.gpio_card_ro		= CORGI_GPIO_nSD_WP,
	.gpio_power		= CORGI_GPIO_SD_PWR,
};


/*
 * Irda
 */
static struct pxaficp_platform_data corgi_ficp_platform_data = {
	.gpio_pwdown		= CORGI_GPIO_IR_ON,
	.transceiver_cap	= IR_SIRMODE | IR_OFF,
};


/*
 * USB Device Controller
 */
static struct pxa2xx_udc_mach_info udc_info __initdata = {
	/* no connect GPIO; corgi can't tell connection status */
	.gpio_pullup		= CORGI_GPIO_USB_PULLUP,
};

#if defined(CONFIG_SPI_PXA2XX) || defined(CONFIG_SPI_PXA2XX_MASTER)
static struct pxa2xx_spi_master corgi_spi_info = {
	.num_chipselect	= 3,
};

static void corgi_wait_for_hsync(void)
{
	while (gpio_get_value(CORGI_GPIO_HSYNC))
		cpu_relax();

	while (!gpio_get_value(CORGI_GPIO_HSYNC))
		cpu_relax();
}

static struct ads7846_platform_data corgi_ads7846_info = {
	.model			= 7846,
	.vref_delay_usecs	= 100,
	.x_plate_ohms		= 419,
	.y_plate_ohms		= 486,
	.gpio_pendown		= CORGI_GPIO_TP_INT,
	.wait_for_sync		= corgi_wait_for_hsync,
};

static struct pxa2xx_spi_chip corgi_ads7846_chip = {
	.gpio_cs	= CORGI_GPIO_ADS7846_CS,
};

static void corgi_bl_kick_battery(void)
{
	void (*kick_batt)(void);

	kick_batt = symbol_get(sharpsl_battery_kick);
	if (kick_batt) {
		kick_batt();
		symbol_put(sharpsl_battery_kick);
	}
}

static struct corgi_lcd_platform_data corgi_lcdcon_info = {
	.init_mode		= CORGI_LCD_MODE_VGA,
	.max_intensity		= 0x2f,
	.default_intensity	= 0x1f,
	.limit_mask		= 0x0b,
	.gpio_backlight_cont	= CORGI_GPIO_BACKLIGHT_CONT,
	.gpio_backlight_on	= -1,
	.kick_battery		= corgi_bl_kick_battery,
};

static struct pxa2xx_spi_chip corgi_lcdcon_chip = {
	.gpio_cs	= CORGI_GPIO_LCDCON_CS,
};

static struct pxa2xx_spi_chip corgi_max1111_chip = {
	.gpio_cs	= CORGI_GPIO_MAX1111_CS,
};

static struct spi_board_info corgi_spi_devices[] = {
	{
		.modalias	= "ads7846",
		.max_speed_hz	= 1200000,
		.bus_num	= 1,
		.chip_select	= 0,
		.platform_data	= &corgi_ads7846_info,
		.controller_data= &corgi_ads7846_chip,
		.irq		= gpio_to_irq(CORGI_GPIO_TP_INT),
	}, {
		.modalias	= "corgi-lcd",
		.max_speed_hz	= 50000,
		.bus_num	= 1,
		.chip_select	= 1,
		.platform_data	= &corgi_lcdcon_info,
		.controller_data= &corgi_lcdcon_chip,
	}, {
		.modalias	= "max1111",
		.max_speed_hz	= 450000,
		.bus_num	= 1,
		.chip_select	= 2,
		.controller_data= &corgi_max1111_chip,
	},
};

static void __init corgi_init_spi(void)
{
	pxa2xx_set_spi_info(1, &corgi_spi_info);
	spi_register_board_info(ARRAY_AND_SIZE(corgi_spi_devices));
}
#else
static inline void corgi_init_spi(void) {}
#endif

static struct mtd_partition sharpsl_nand_partitions[] = {
	{
		.name = "System Area",
		.offset = 0,
		.size = 7 * 1024 * 1024,
	},
	{
		.name = "Root Filesystem",
		.offset = 7 * 1024 * 1024,
		.size = 25 * 1024 * 1024,
	},
	{
		.name = "Home Filesystem",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static uint8_t scan_ff_pattern[] = { 0xff, 0xff };

static struct nand_bbt_descr sharpsl_bbt = {
	.options = 0,
	.offs = 4,
	.len = 2,
	.pattern = scan_ff_pattern
};

static struct sharpsl_nand_platform_data sharpsl_nand_platform_data = {
	.badblock_pattern	= &sharpsl_bbt,
	.partitions		= sharpsl_nand_partitions,
	.nr_partitions		= ARRAY_SIZE(sharpsl_nand_partitions),
};

static struct resource sharpsl_nand_resources[] = {
	{
		.start	= 0x0C000000,
		.end	= 0x0C000FFF,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device sharpsl_nand_device = {
	.name		= "sharpsl-nand",
	.id		= -1,
	.resource	= sharpsl_nand_resources,
	.num_resources	= ARRAY_SIZE(sharpsl_nand_resources),
	.dev.platform_data	= &sharpsl_nand_platform_data,
};

static struct mtd_partition sharpsl_rom_parts[] = {
	{
		.name	="Boot PROM Filesystem",
		.offset	= 0x00120000,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data sharpsl_rom_data = {
	.width		= 2,
	.nr_parts	= ARRAY_SIZE(sharpsl_rom_parts),
	.parts		= sharpsl_rom_parts,
};

static struct resource sharpsl_rom_resources[] = {
	{
		.start	= 0x00000000,
		.end	= 0x007fffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device sharpsl_rom_device = {
	.name	= "physmap-flash",
	.id	= -1,
	.resource = sharpsl_rom_resources,
	.num_resources = ARRAY_SIZE(sharpsl_rom_resources),
	.dev.platform_data = &sharpsl_rom_data,
};

static struct platform_device *devices[] __initdata = {
	&corgiscoop_device,
	&corgifb_device,
	&corgikbd_device,
	&corgiled_device,
	&sharpsl_nand_device,
	&sharpsl_rom_device,
};

static struct i2c_board_info __initdata corgi_i2c_devices[] = {
	{ I2C_BOARD_INFO("wm8731", 0x1b) },
};

static void corgi_poweroff(void)
{
	if (!machine_is_corgi())
		/* Green LED off tells the bootloader to halt */
		gpio_set_value(CORGI_GPIO_LED_GREEN, 0);

	arm_machine_restart('h', NULL);
}

static void corgi_restart(char mode, const char *cmd)
{
	if (!machine_is_corgi())
		/* Green LED on tells the bootloader to reboot */
		gpio_set_value(CORGI_GPIO_LED_GREEN, 1);

	arm_machine_restart('h', cmd);
}

static void __init corgi_init(void)
{
	pm_power_off = corgi_poweroff;
	arm_pm_restart = corgi_restart;

	/* Stop 3.6MHz and drive HIGH to PCMCIA and CS */
	PCFR |= PCFR_OPDE;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(corgi_pin_config));

	/* allow wakeup from various GPIOs */
	gpio_set_wake(CORGI_GPIO_KEY_INT, 1);
	gpio_set_wake(CORGI_GPIO_WAKEUP, 1);
	gpio_set_wake(CORGI_GPIO_AC_IN, 1);
	gpio_set_wake(CORGI_GPIO_CHRG_FULL, 1);

	if (!machine_is_corgi())
		gpio_set_wake(CORGI_GPIO_MAIN_BAT_LOW, 1);

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	corgi_init_spi();

 	pxa_set_udc_info(&udc_info);
	pxa_set_mci_info(&corgi_mci_platform_data);
	pxa_set_ficp_info(&corgi_ficp_platform_data);
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(0, ARRAY_AND_SIZE(corgi_i2c_devices));

	platform_scoop_config = &corgi_pcmcia_config;

	if (machine_is_husky())
		sharpsl_nand_partitions[1].size = 53 * 1024 * 1024;

	platform_add_devices(devices, ARRAY_SIZE(devices));
}

static void __init fixup_corgi(struct machine_desc *desc,
		struct tag *tags, char **cmdline, struct meminfo *mi)
{
	sharpsl_save_param();
	mi->nr_banks=1;
	mi->bank[0].start = 0xa0000000;
	if (machine_is_corgi())
		mi->bank[0].size = (32*1024*1024);
	else
		mi->bank[0].size = (64*1024*1024);
}

#ifdef CONFIG_MACH_CORGI
MACHINE_START(CORGI, "SHARP Corgi")
	.fixup		= fixup_corgi,
	.map_io		= pxa25x_map_io,
	.init_irq	= pxa25x_init_irq,
	.init_machine	= corgi_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_SHEPHERD
MACHINE_START(SHEPHERD, "SHARP Shepherd")
	.fixup		= fixup_corgi,
	.map_io		= pxa25x_map_io,
	.init_irq	= pxa25x_init_irq,
	.init_machine	= corgi_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

#ifdef CONFIG_MACH_HUSKY
MACHINE_START(HUSKY, "SHARP Husky")
	.fixup		= fixup_corgi,
	.map_io		= pxa25x_map_io,
	.init_irq	= pxa25x_init_irq,
	.init_machine	= corgi_init,
	.timer		= &pxa_timer,
MACHINE_END
#endif

