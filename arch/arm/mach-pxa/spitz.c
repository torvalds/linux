/*
 * Support for Sharp SL-Cxx00 Series of PDAs
 * Models: SL-C3000 (Spitz), SL-C1000 (Akita) and SL-C3100 (Borzoi)
 *
 * Copyright (c) 2005 Richard Purdie
 *
 * Based on Sharp's 2.4 kernel patches/lubbock.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio_keys.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/i2c.h>
#include <linux/i2c/pxa-i2c.h>
#include <linux/platform_data/pca953x.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/spi/corgi_lcd.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/mtd/sharpsl.h>
#include <linux/mtd/physmap.h>
#include <linux/input/matrix_keypad.h>
#include <linux/regulator/machine.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/memblock.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/sharpsl_param.h>
#include <asm/hardware/scoop.h>

#include "pxa27x.h"
#include "pxa27x-udc.h"
#include <mach/reset.h>
#include <linux/platform_data/irda-pxaficp.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/usb-ohci-pxa27x.h>
#include <linux/platform_data/video-pxafb.h>
#include <mach/spitz.h>
#include "sharpsl_pm.h"
#include <mach/smemc.h>

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static unsigned long spitz_pin_config[] __initdata = {
	/* Chip Selects */
	GPIO78_nCS_2,	/* SCOOP #2 */
	GPIO79_nCS_3,	/* NAND */
	GPIO80_nCS_4,	/* SCOOP #1 */

	/* LCD - 16bpp Active TFT */
	GPIOxx_LCD_TFT_16BPP,

	/* PC Card */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO85_nPCE_1,
	GPIO54_nPCE_2,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,
	GPIO104_PSKTSEL,

	/* I2S */
	GPIO28_I2S_BITCLK_OUT,
	GPIO29_I2S_SDATA_IN,
	GPIO30_I2S_SDATA_OUT,
	GPIO31_I2S_SYNC,

	/* MMC */
	GPIO32_MMC_CLK,
	GPIO112_MMC_CMD,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,

	/* GPIOs */
	GPIO9_GPIO,	/* SPITZ_GPIO_nSD_DETECT */
	GPIO16_GPIO,	/* SPITZ_GPIO_SYNC */
	GPIO81_GPIO,	/* SPITZ_GPIO_nSD_WP */
	GPIO41_GPIO,	/* SPITZ_GPIO_USB_CONNECT */
	GPIO37_GPIO,	/* SPITZ_GPIO_USB_HOST */
	GPIO35_GPIO,	/* SPITZ_GPIO_USB_DEVICE */
	GPIO22_GPIO,	/* SPITZ_GPIO_HSYNC */
	GPIO94_GPIO,	/* SPITZ_GPIO_CF_CD */
	GPIO105_GPIO,	/* SPITZ_GPIO_CF_IRQ */
	GPIO106_GPIO,	/* SPITZ_GPIO_CF2_IRQ */

	/* GPIO matrix keypad */
	GPIO88_GPIO,	/* column 0 */
	GPIO23_GPIO,	/* column 1 */
	GPIO24_GPIO,	/* column 2 */
	GPIO25_GPIO,	/* column 3 */
	GPIO26_GPIO,	/* column 4 */
	GPIO27_GPIO,	/* column 5 */
	GPIO52_GPIO,	/* column 6 */
	GPIO103_GPIO,	/* column 7 */
	GPIO107_GPIO,	/* column 8 */
	GPIO108_GPIO,	/* column 9 */
	GPIO114_GPIO,	/* column 10 */
	GPIO12_GPIO,	/* row 0 */
	GPIO17_GPIO,	/* row 1 */
	GPIO91_GPIO,	/* row 2 */
	GPIO34_GPIO,	/* row 3 */
	GPIO36_GPIO,	/* row 4 */
	GPIO38_GPIO,	/* row 5 */
	GPIO39_GPIO,	/* row 6 */

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	GPIO0_GPIO | WAKEUP_ON_EDGE_RISE,	/* SPITZ_GPIO_KEY_INT */
	GPIO1_GPIO | WAKEUP_ON_EDGE_FALL,	/* SPITZ_GPIO_RESET */
};


/******************************************************************************
 * Scoop GPIO expander
 ******************************************************************************/
#if defined(CONFIG_SHARP_SCOOP) || defined(CONFIG_SHARP_SCOOP_MODULE)
/* SCOOP Device #1 */
static struct resource spitz_scoop_1_resources[] = {
	[0] = {
		.start		= 0x10800000,
		.end		= 0x10800fff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct scoop_config spitz_scoop_1_setup = {
	.io_dir		= SPITZ_SCP_IO_DIR,
	.io_out		= SPITZ_SCP_IO_OUT,
	.suspend_clr	= SPITZ_SCP_SUS_CLR,
	.suspend_set	= SPITZ_SCP_SUS_SET,
	.gpio_base	= SPITZ_SCP_GPIO_BASE,
};

struct platform_device spitz_scoop_1_device = {
	.name		= "sharp-scoop",
	.id		= 0,
	.dev		= {
		.platform_data	= &spitz_scoop_1_setup,
	},
	.num_resources	= ARRAY_SIZE(spitz_scoop_1_resources),
	.resource	= spitz_scoop_1_resources,
};

/* SCOOP Device #2 */
static struct resource spitz_scoop_2_resources[] = {
	[0] = {
		.start		= 0x08800040,
		.end		= 0x08800fff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct scoop_config spitz_scoop_2_setup = {
	.io_dir		= SPITZ_SCP2_IO_DIR,
	.io_out		= SPITZ_SCP2_IO_OUT,
	.suspend_clr	= SPITZ_SCP2_SUS_CLR,
	.suspend_set	= SPITZ_SCP2_SUS_SET,
	.gpio_base	= SPITZ_SCP2_GPIO_BASE,
};

struct platform_device spitz_scoop_2_device = {
	.name		= "sharp-scoop",
	.id		= 1,
	.dev		= {
		.platform_data	= &spitz_scoop_2_setup,
	},
	.num_resources	= ARRAY_SIZE(spitz_scoop_2_resources),
	.resource	= spitz_scoop_2_resources,
};

static void __init spitz_scoop_init(void)
{
	platform_device_register(&spitz_scoop_1_device);

	/* Akita doesn't have the second SCOOP chip */
	if (!machine_is_akita())
		platform_device_register(&spitz_scoop_2_device);
}

/* Power control is shared with between one of the CF slots and SD */
static void __maybe_unused spitz_card_pwr_ctrl(uint8_t enable, uint8_t new_cpr)
{
	unsigned short cpr;
	unsigned long flags;

	if (new_cpr & 0x7) {
		gpio_set_value(SPITZ_GPIO_CF_POWER, 1);
		mdelay(5);
	}

	local_irq_save(flags);

	cpr = read_scoop_reg(&spitz_scoop_1_device.dev, SCOOP_CPR);

	if (enable & new_cpr)
		cpr |= new_cpr;
	else
		cpr &= ~enable;

	write_scoop_reg(&spitz_scoop_1_device.dev, SCOOP_CPR, cpr);

	local_irq_restore(flags);

	if (!(cpr & 0x7)) {
		mdelay(1);
		gpio_set_value(SPITZ_GPIO_CF_POWER, 0);
	}
}

#else
static inline void spitz_scoop_init(void) {}
static inline void spitz_card_pwr_ctrl(uint8_t enable, uint8_t new_cpr) {}
#endif

/******************************************************************************
 * PCMCIA
 ******************************************************************************/
#if defined(CONFIG_PCMCIA_PXA2XX) || defined(CONFIG_PCMCIA_PXA2XX_MODULE)
static void spitz_pcmcia_pwr(struct device *scoop, uint16_t cpr, int nr)
{
	/* Only need to override behaviour for slot 0 */
	if (nr == 0)
		spitz_card_pwr_ctrl(
			cpr & (SCOOP_CPR_CF_3V | SCOOP_CPR_CF_XV), cpr);
	else
		write_scoop_reg(scoop, SCOOP_CPR, cpr);
}

static struct scoop_pcmcia_dev spitz_pcmcia_scoop[] = {
	{
		.dev		= &spitz_scoop_1_device.dev,
		.irq		= SPITZ_IRQ_GPIO_CF_IRQ,
		.cd_irq		= SPITZ_IRQ_GPIO_CF_CD,
		.cd_irq_str	= "PCMCIA0 CD",
	}, {
		.dev		= &spitz_scoop_2_device.dev,
		.irq		= SPITZ_IRQ_GPIO_CF2_IRQ,
		.cd_irq		= -1,
	},
};

static struct scoop_pcmcia_config spitz_pcmcia_config = {
	.devs		= &spitz_pcmcia_scoop[0],
	.num_devs	= 2,
	.power_ctrl	= spitz_pcmcia_pwr,
};

static void __init spitz_pcmcia_init(void)
{
	/* Akita has only one PCMCIA slot used */
	if (machine_is_akita())
		spitz_pcmcia_config.num_devs = 1;

	platform_scoop_config = &spitz_pcmcia_config;
}
#else
static inline void spitz_pcmcia_init(void) {}
#endif

/******************************************************************************
 * GPIO keyboard
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_MATRIX) || defined(CONFIG_KEYBOARD_MATRIX_MODULE)

#define SPITZ_KEY_CALENDAR	KEY_F1
#define SPITZ_KEY_ADDRESS	KEY_F2
#define SPITZ_KEY_FN		KEY_F3
#define SPITZ_KEY_CANCEL	KEY_F4
#define SPITZ_KEY_EXOK		KEY_F5
#define SPITZ_KEY_EXCANCEL	KEY_F6
#define SPITZ_KEY_EXJOGDOWN	KEY_F7
#define SPITZ_KEY_EXJOGUP	KEY_F8
#define SPITZ_KEY_JAP1		KEY_LEFTALT
#define SPITZ_KEY_JAP2		KEY_RIGHTCTRL
#define SPITZ_KEY_SYNC		KEY_F9
#define SPITZ_KEY_MAIL		KEY_F10
#define SPITZ_KEY_OK		KEY_F11
#define SPITZ_KEY_MENU		KEY_F12

static const uint32_t spitz_keymap[] = {
	KEY(0, 0, KEY_LEFTCTRL),
	KEY(0, 1, KEY_1),
	KEY(0, 2, KEY_3),
	KEY(0, 3, KEY_5),
	KEY(0, 4, KEY_6),
	KEY(0, 5, KEY_7),
	KEY(0, 6, KEY_9),
	KEY(0, 7, KEY_0),
	KEY(0, 8, KEY_BACKSPACE),
	KEY(0, 9, SPITZ_KEY_EXOK),	/* EXOK */
	KEY(0, 10, SPITZ_KEY_EXCANCEL),	/* EXCANCEL */
	KEY(1, 1, KEY_2),
	KEY(1, 2, KEY_4),
	KEY(1, 3, KEY_R),
	KEY(1, 4, KEY_Y),
	KEY(1, 5, KEY_8),
	KEY(1, 6, KEY_I),
	KEY(1, 7, KEY_O),
	KEY(1, 8, KEY_P),
	KEY(1, 9, SPITZ_KEY_EXJOGDOWN),	/* EXJOGDOWN */
	KEY(1, 10, SPITZ_KEY_EXJOGUP),	/* EXJOGUP */
	KEY(2, 0, KEY_TAB),
	KEY(2, 1, KEY_Q),
	KEY(2, 2, KEY_E),
	KEY(2, 3, KEY_T),
	KEY(2, 4, KEY_G),
	KEY(2, 5, KEY_U),
	KEY(2, 6, KEY_J),
	KEY(2, 7, KEY_K),
	KEY(3, 0, SPITZ_KEY_ADDRESS),	/* ADDRESS */
	KEY(3, 1, KEY_W),
	KEY(3, 2, KEY_S),
	KEY(3, 3, KEY_F),
	KEY(3, 4, KEY_V),
	KEY(3, 5, KEY_H),
	KEY(3, 6, KEY_M),
	KEY(3, 7, KEY_L),
	KEY(3, 9, KEY_RIGHTSHIFT),
	KEY(4, 0, SPITZ_KEY_CALENDAR),	/* CALENDAR */
	KEY(4, 1, KEY_A),
	KEY(4, 2, KEY_D),
	KEY(4, 3, KEY_C),
	KEY(4, 4, KEY_B),
	KEY(4, 5, KEY_N),
	KEY(4, 6, KEY_DOT),
	KEY(4, 8, KEY_ENTER),
	KEY(4, 9, KEY_LEFTSHIFT),
	KEY(5, 0, SPITZ_KEY_MAIL),	/* MAIL */
	KEY(5, 1, KEY_Z),
	KEY(5, 2, KEY_X),
	KEY(5, 3, KEY_MINUS),
	KEY(5, 4, KEY_SPACE),
	KEY(5, 5, KEY_COMMA),
	KEY(5, 7, KEY_UP),
	KEY(5, 10, SPITZ_KEY_FN),	/* FN */
	KEY(6, 0, KEY_SYSRQ),
	KEY(6, 1, SPITZ_KEY_JAP1),	/* JAP1 */
	KEY(6, 2, SPITZ_KEY_JAP2),	/* JAP2 */
	KEY(6, 3, SPITZ_KEY_CANCEL),	/* CANCEL */
	KEY(6, 4, SPITZ_KEY_OK),	/* OK */
	KEY(6, 5, SPITZ_KEY_MENU),	/* MENU */
	KEY(6, 6, KEY_LEFT),
	KEY(6, 7, KEY_DOWN),
	KEY(6, 8, KEY_RIGHT),
};

static const struct matrix_keymap_data spitz_keymap_data = {
	.keymap		= spitz_keymap,
	.keymap_size	= ARRAY_SIZE(spitz_keymap),
};

static const uint32_t spitz_row_gpios[] =
		{ 12, 17, 91, 34, 36, 38, 39 };
static const uint32_t spitz_col_gpios[] =
		{ 88, 23, 24, 25, 26, 27, 52, 103, 107, 108, 114 };

static struct matrix_keypad_platform_data spitz_mkp_pdata = {
	.keymap_data		= &spitz_keymap_data,
	.row_gpios		= spitz_row_gpios,
	.col_gpios		= spitz_col_gpios,
	.num_row_gpios		= ARRAY_SIZE(spitz_row_gpios),
	.num_col_gpios		= ARRAY_SIZE(spitz_col_gpios),
	.col_scan_delay_us	= 10,
	.debounce_ms		= 10,
	.wakeup			= 1,
};

static struct platform_device spitz_mkp_device = {
	.name		= "matrix-keypad",
	.id		= -1,
	.dev		= {
		.platform_data	= &spitz_mkp_pdata,
	},
};

static void __init spitz_mkp_init(void)
{
	platform_device_register(&spitz_mkp_device);
}
#else
static inline void spitz_mkp_init(void) {}
#endif

/******************************************************************************
 * GPIO keys
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button spitz_gpio_keys[] = {
	{
		.type	= EV_PWR,
		.code	= KEY_SUSPEND,
		.gpio	= SPITZ_GPIO_ON_KEY,
		.desc	= "On Off",
		.wakeup	= 1,
	},
	/* Two buttons detecting the lid state */
	{
		.type	= EV_SW,
		.code	= 0,
		.gpio	= SPITZ_GPIO_SWA,
		.desc	= "Display Down",
	},
	{
		.type	= EV_SW,
		.code	= 1,
		.gpio	= SPITZ_GPIO_SWB,
		.desc	= "Lid Closed",
	},
};

static struct gpio_keys_platform_data spitz_gpio_keys_platform_data = {
	.buttons	= spitz_gpio_keys,
	.nbuttons	= ARRAY_SIZE(spitz_gpio_keys),
};

static struct platform_device spitz_gpio_keys_device = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data	= &spitz_gpio_keys_platform_data,
	},
};

static void __init spitz_keys_init(void)
{
	platform_device_register(&spitz_gpio_keys_device);
}
#else
static inline void spitz_keys_init(void) {}
#endif

/******************************************************************************
 * LEDs
 ******************************************************************************/
#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
static struct gpio_led spitz_gpio_leds[] = {
	{
		.name			= "spitz:amber:charge",
		.default_trigger	= "sharpsl-charge",
		.gpio			= SPITZ_GPIO_LED_ORANGE,
	},
	{
		.name			= "spitz:green:hddactivity",
		.default_trigger	= "ide-disk",
		.gpio			= SPITZ_GPIO_LED_GREEN,
	},
};

static struct gpio_led_platform_data spitz_gpio_leds_info = {
	.leds		= spitz_gpio_leds,
	.num_leds	= ARRAY_SIZE(spitz_gpio_leds),
};

static struct platform_device spitz_led_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &spitz_gpio_leds_info,
	},
};

static void __init spitz_leds_init(void)
{
	platform_device_register(&spitz_led_device);
}
#else
static inline void spitz_leds_init(void) {}
#endif

/******************************************************************************
 * SSP Devices
 ******************************************************************************/
#if defined(CONFIG_SPI_PXA2XX) || defined(CONFIG_SPI_PXA2XX_MODULE)
static void spitz_ads7846_wait_for_hsync(void)
{
	while (gpio_get_value(SPITZ_GPIO_HSYNC))
		cpu_relax();

	while (!gpio_get_value(SPITZ_GPIO_HSYNC))
		cpu_relax();
}

static struct ads7846_platform_data spitz_ads7846_info = {
	.model			= 7846,
	.vref_delay_usecs	= 100,
	.x_plate_ohms		= 419,
	.y_plate_ohms		= 486,
	.pressure_max		= 1024,
	.gpio_pendown		= SPITZ_GPIO_TP_INT,
	.wait_for_sync		= spitz_ads7846_wait_for_hsync,
};

static struct pxa2xx_spi_chip spitz_ads7846_chip = {
	.gpio_cs		= SPITZ_GPIO_ADS7846_CS,
};

static void spitz_bl_kick_battery(void)
{
	void (*kick_batt)(void);

	kick_batt = symbol_get(sharpsl_battery_kick);
	if (kick_batt) {
		kick_batt();
		symbol_put(sharpsl_battery_kick);
	}
}

static struct corgi_lcd_platform_data spitz_lcdcon_info = {
	.init_mode		= CORGI_LCD_MODE_VGA,
	.max_intensity		= 0x2f,
	.default_intensity	= 0x1f,
	.limit_mask		= 0x0b,
	.gpio_backlight_cont	= SPITZ_GPIO_BACKLIGHT_CONT,
	.gpio_backlight_on	= SPITZ_GPIO_BACKLIGHT_ON,
	.kick_battery		= spitz_bl_kick_battery,
};

static struct pxa2xx_spi_chip spitz_lcdcon_chip = {
	.gpio_cs	= SPITZ_GPIO_LCDCON_CS,
};

static struct pxa2xx_spi_chip spitz_max1111_chip = {
	.gpio_cs	= SPITZ_GPIO_MAX1111_CS,
};

static struct spi_board_info spitz_spi_devices[] = {
	{
		.modalias		= "ads7846",
		.max_speed_hz		= 1200000,
		.bus_num		= 2,
		.chip_select		= 0,
		.platform_data		= &spitz_ads7846_info,
		.controller_data	= &spitz_ads7846_chip,
		.irq			= PXA_GPIO_TO_IRQ(SPITZ_GPIO_TP_INT),
	}, {
		.modalias		= "corgi-lcd",
		.max_speed_hz		= 50000,
		.bus_num		= 2,
		.chip_select		= 1,
		.platform_data		= &spitz_lcdcon_info,
		.controller_data	= &spitz_lcdcon_chip,
	}, {
		.modalias		= "max1111",
		.max_speed_hz		= 450000,
		.bus_num		= 2,
		.chip_select		= 2,
		.controller_data	= &spitz_max1111_chip,
	},
};

static struct pxa2xx_spi_master spitz_spi_info = {
	.num_chipselect	= 3,
};

static void __init spitz_spi_init(void)
{
	struct corgi_lcd_platform_data *lcd_data = &spitz_lcdcon_info;

	if (machine_is_akita()) {
		lcd_data->gpio_backlight_cont = AKITA_GPIO_BACKLIGHT_CONT;
		lcd_data->gpio_backlight_on = AKITA_GPIO_BACKLIGHT_ON;
	}

	pxa2xx_set_spi_info(2, &spitz_spi_info);
	spi_register_board_info(ARRAY_AND_SIZE(spitz_spi_devices));
}
#else
static inline void spitz_spi_init(void) {}
#endif

/******************************************************************************
 * SD/MMC card controller
 ******************************************************************************/
#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
/*
 * NOTE: The card detect interrupt isn't debounced so we delay it by 250ms to
 * give the card a chance to fully insert/eject.
 */
static int spitz_mci_setpower(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	if ((1 << vdd) & p_d->ocr_mask)
		spitz_card_pwr_ctrl(SCOOP_CPR_SD_3V, SCOOP_CPR_SD_3V);
	else
		spitz_card_pwr_ctrl(SCOOP_CPR_SD_3V, 0x0);

	return 0;
}

static struct pxamci_platform_data spitz_mci_platform_data = {
	.detect_delay_ms	= 250,
	.ocr_mask		= MMC_VDD_32_33|MMC_VDD_33_34,
	.setpower		= spitz_mci_setpower,
	.gpio_card_detect	= SPITZ_GPIO_nSD_DETECT,
	.gpio_card_ro		= SPITZ_GPIO_nSD_WP,
	.gpio_power		= -1,
};

static void __init spitz_mmc_init(void)
{
	pxa_set_mci_info(&spitz_mci_platform_data);
}
#else
static inline void spitz_mmc_init(void) {}
#endif

/******************************************************************************
 * USB Host
 ******************************************************************************/
#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static int spitz_ohci_init(struct device *dev)
{
	int err;

	err = gpio_request(SPITZ_GPIO_USB_HOST, "USB_HOST");
	if (err)
		return err;

	/* Only Port 2 is connected, setup USB Port 2 Output Control Register */
	UP2OCR = UP2OCR_HXS | UP2OCR_HXOE | UP2OCR_DPPDE | UP2OCR_DMPDE;

	return gpio_direction_output(SPITZ_GPIO_USB_HOST, 1);
}

static void spitz_ohci_exit(struct device *dev)
{
	gpio_free(SPITZ_GPIO_USB_HOST);
}

static struct pxaohci_platform_data spitz_ohci_platform_data = {
	.port_mode	= PMM_NPS_MODE,
	.init		= spitz_ohci_init,
	.exit		= spitz_ohci_exit,
	.flags		= ENABLE_PORT_ALL | NO_OC_PROTECTION,
	.power_budget	= 150,
};

static void __init spitz_uhc_init(void)
{
	pxa_set_ohci_info(&spitz_ohci_platform_data);
}
#else
static inline void spitz_uhc_init(void) {}
#endif

/******************************************************************************
 * IrDA
 ******************************************************************************/
#if defined(CONFIG_PXA_FICP) || defined(CONFIG_PXA_FICP_MODULE)
static struct pxaficp_platform_data spitz_ficp_platform_data = {
	.transceiver_cap	= IR_SIRMODE | IR_OFF,
};

static void __init spitz_irda_init(void)
{
	if (machine_is_akita())
		spitz_ficp_platform_data.gpio_pwdown = AKITA_GPIO_IR_ON;
	else
		spitz_ficp_platform_data.gpio_pwdown = SPITZ_GPIO_IR_ON;

	pxa_set_ficp_info(&spitz_ficp_platform_data);
}
#else
static inline void spitz_irda_init(void) {}
#endif

/******************************************************************************
 * Framebuffer
 ******************************************************************************/
#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
static struct pxafb_mode_info spitz_pxafb_modes[] = {
	{
		.pixclock       = 19231,
		.xres           = 480,
		.yres           = 640,
		.bpp            = 16,
		.hsync_len      = 40,
		.left_margin    = 46,
		.right_margin   = 125,
		.vsync_len      = 3,
		.upper_margin   = 1,
		.lower_margin   = 0,
		.sync           = 0,
	}, {
		.pixclock       = 134617,
		.xres           = 240,
		.yres           = 320,
		.bpp            = 16,
		.hsync_len      = 20,
		.left_margin    = 20,
		.right_margin   = 46,
		.vsync_len      = 2,
		.upper_margin   = 1,
		.lower_margin   = 0,
		.sync           = 0,
	},
};

static struct pxafb_mach_info spitz_pxafb_info = {
	.modes          = spitz_pxafb_modes,
	.num_modes      = ARRAY_SIZE(spitz_pxafb_modes),
	.fixed_modes    = 1,
	.lcd_conn	= LCD_COLOR_TFT_16BPP | LCD_ALTERNATE_MAPPING,
};

static void __init spitz_lcd_init(void)
{
	pxa_set_fb_info(NULL, &spitz_pxafb_info);
}
#else
static inline void spitz_lcd_init(void) {}
#endif

/******************************************************************************
 * NAND Flash
 ******************************************************************************/
#if defined(CONFIG_MTD_NAND_SHARPSL) || defined(CONFIG_MTD_NAND_SHARPSL_MODULE)
static struct mtd_partition spitz_nand_partitions[] = {
	{
		.name = "System Area",
		.offset = 0,
		.size = 7 * 1024 * 1024,
	}, {
		.name = "Root Filesystem",
		.offset = 7 * 1024 * 1024,
	}, {
		.name = "Home Filesystem",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
	},
};

static uint8_t scan_ff_pattern[] = { 0xff, 0xff };

static struct nand_bbt_descr spitz_nand_bbt = {
	.options	= 0,
	.offs		= 4,
	.len		= 2,
	.pattern	= scan_ff_pattern
};

static int akita_ooblayout_ecc(struct mtd_info *mtd, int section,
			       struct mtd_oob_region *oobregion)
{
	if (section > 12)
		return -ERANGE;

	switch (section % 3) {
	case 0:
		oobregion->offset = 5;
		oobregion->length = 1;
		break;

	case 1:
		oobregion->offset = 1;
		oobregion->length = 3;
		break;

	case 2:
		oobregion->offset = 6;
		oobregion->length = 2;
		break;
	}

	oobregion->offset += (section / 3) * 0x10;

	return 0;
}

static int akita_ooblayout_free(struct mtd_info *mtd, int section,
				struct mtd_oob_region *oobregion)
{
	if (section)
		return -ERANGE;

	oobregion->offset = 8;
	oobregion->length = 9;

	return 0;
}

static const struct mtd_ooblayout_ops akita_ooblayout_ops = {
	.ecc = akita_ooblayout_ecc,
	.free = akita_ooblayout_free,
};

static struct sharpsl_nand_platform_data spitz_nand_pdata = {
	.badblock_pattern	= &spitz_nand_bbt,
	.partitions		= spitz_nand_partitions,
	.nr_partitions		= ARRAY_SIZE(spitz_nand_partitions),
};

static struct resource spitz_nand_resources[] = {
	{
		.start	= PXA_CS3_PHYS,
		.end	= PXA_CS3_PHYS + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device spitz_nand_device = {
	.name		= "sharpsl-nand",
	.id		= -1,
	.resource	= spitz_nand_resources,
	.num_resources	= ARRAY_SIZE(spitz_nand_resources),
	.dev		= {
		.platform_data	= &spitz_nand_pdata,
	}
};

static void __init spitz_nand_init(void)
{
	if (machine_is_spitz()) {
		spitz_nand_partitions[1].size = 5 * 1024 * 1024;
	} else if (machine_is_akita()) {
		spitz_nand_partitions[1].size = 58 * 1024 * 1024;
		spitz_nand_bbt.len = 1;
		spitz_nand_pdata.ecc_layout = &akita_ooblayout_ops;
	} else if (machine_is_borzoi()) {
		spitz_nand_partitions[1].size = 32 * 1024 * 1024;
		spitz_nand_bbt.len = 1;
		spitz_nand_pdata.ecc_layout = &akita_ooblayout_ops;
	}

	platform_device_register(&spitz_nand_device);
}
#else
static inline void spitz_nand_init(void) {}
#endif

/******************************************************************************
 * NOR Flash
 ******************************************************************************/
#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition spitz_rom_parts[] = {
	{
		.name	="Boot PROM Filesystem",
		.offset	= 0x00140000,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data spitz_rom_data = {
	.width		= 2,
	.nr_parts	= ARRAY_SIZE(spitz_rom_parts),
	.parts		= spitz_rom_parts,
};

static struct resource spitz_rom_resources[] = {
	{
		.start	= PXA_CS0_PHYS,
		.end	= PXA_CS0_PHYS + SZ_8M - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device spitz_rom_device = {
	.name		= "physmap-flash",
	.id		= -1,
	.resource	= spitz_rom_resources,
	.num_resources	= ARRAY_SIZE(spitz_rom_resources),
	.dev		= {
		.platform_data	= &spitz_rom_data,
	},
};

static void __init spitz_nor_init(void)
{
	platform_device_register(&spitz_rom_device);
}
#else
static inline void spitz_nor_init(void) {}
#endif

/******************************************************************************
 * I2C devices
 ******************************************************************************/
#if defined(CONFIG_I2C_PXA) || defined(CONFIG_I2C_PXA_MODULE)
static struct pca953x_platform_data akita_pca953x_pdata = {
	.gpio_base		= AKITA_IOEXP_GPIO_BASE,
};

static struct i2c_board_info spitz_i2c_devs[] = {
	{
		.type		= "wm8750",
		.addr		= 0x1b,
	}, {
		.type		= "max7310",
		.addr		= 0x18,
		.platform_data	= &akita_pca953x_pdata,
	},
};

static struct regulator_consumer_supply isl6271a_consumers[] = {
	REGULATOR_SUPPLY("vcc_core", NULL),
};

static struct regulator_init_data isl6271a_info[] = {
	{
		.constraints = {
			.name		= "vcc_core range",
			.min_uV		= 850000,
			.max_uV		= 1600000,
			.always_on	= 1,
			.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE,
		},
	.consumer_supplies	= isl6271a_consumers,
	.num_consumer_supplies	= ARRAY_SIZE(isl6271a_consumers),
	}
};

static struct i2c_board_info spitz_pi2c_devs[] = {
	{
		.type		= "isl6271a",
		.addr		= 0x0c,
		.platform_data	= &isl6271a_info,
	},
};

static void __init spitz_i2c_init(void)
{
	int size = ARRAY_SIZE(spitz_i2c_devs);

	/* Only Akita has the max7310 chip */
	if (!machine_is_akita())
		size--;

	pxa_set_i2c_info(NULL);
	pxa27x_set_i2c_power_info(NULL);
	i2c_register_board_info(0, spitz_i2c_devs, size);
	i2c_register_board_info(1, ARRAY_AND_SIZE(spitz_pi2c_devs));
}
#else
static inline void spitz_i2c_init(void) {}
#endif

/******************************************************************************
 * Audio devices
 ******************************************************************************/
static inline void spitz_audio_init(void)
{
	platform_device_register_simple("spitz-audio", -1, NULL, 0);
}

/******************************************************************************
 * Machine init
 ******************************************************************************/
static void spitz_poweroff(void)
{
	pxa_restart(REBOOT_GPIO, NULL);
}

static void spitz_restart(enum reboot_mode mode, const char *cmd)
{
	uint32_t msc0 = __raw_readl(MSC0);
	/* Bootloader magic for a reboot */
	if ((msc0 & 0xffff0000) == 0x7ff00000)
		__raw_writel((msc0 & 0xffff) | 0x7ee00000, MSC0);

	spitz_poweroff();
}

static void __init spitz_init(void)
{
	init_gpio_reset(SPITZ_GPIO_ON_RESET, 1, 0);
	pm_power_off = spitz_poweroff;

	PMCR = 0x00;

	/* Stop 3.6MHz and drive HIGH to PCMCIA and CS */
	PCFR |= PCFR_OPDE;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(spitz_pin_config));

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	spitz_spi_init();
	spitz_scoop_init();
	spitz_mkp_init();
	spitz_keys_init();
	spitz_leds_init();
	spitz_mmc_init();
	spitz_pcmcia_init();
	spitz_irda_init();
	spitz_uhc_init();
	spitz_lcd_init();
	spitz_nor_init();
	spitz_nand_init();
	spitz_i2c_init();
	spitz_audio_init();

	regulator_has_full_constraints();
}

static void __init spitz_fixup(struct tag *tags, char **cmdline)
{
	sharpsl_save_param();
	memblock_add(0xa0000000, SZ_64M);
}

#ifdef CONFIG_MACH_SPITZ
MACHINE_START(SPITZ, "SHARP Spitz")
	.fixup		= spitz_fixup,
	.map_io		= pxa27x_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_machine	= spitz_init,
	.init_time	= pxa_timer_init,
	.restart	= spitz_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_BORZOI
MACHINE_START(BORZOI, "SHARP Borzoi")
	.fixup		= spitz_fixup,
	.map_io		= pxa27x_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_machine	= spitz_init,
	.init_time	= pxa_timer_init,
	.restart	= spitz_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_AKITA
MACHINE_START(AKITA, "SHARP Akita")
	.fixup		= spitz_fixup,
	.map_io		= pxa27x_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_machine	= spitz_init,
	.init_time	= pxa_timer_init,
	.restart	= spitz_restart,
MACHINE_END
#endif
