/*
 * Support for HP iPAQ hx4700 PDAs.
 *
 * Copyright (c) 2008-2009 Philipp Zabel
 *
 * Based on code:
 *    Copyright (c) 2004 Hewlett-Packard Company.
 *    Copyright (c) 2005 SDG Systems, LLC
 *    Copyright (c) 2006 Anton Vorontsov <cbou@mail.ru>
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
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/lcd.h>
#include <linux/mfd/htc-egpio.h>
#include <linux/mfd/asic3.h>
#include <linux/mtd/physmap.h>
#include <linux/pda_power.h>
#include <linux/pwm_backlight.h>
#include <linux/regulator/bq24022.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/max1586.h>
#include <linux/spi/ads7846.h>
#include <linux/spi/spi.h>
#include <linux/spi/pxa2xx_spi.h>
#include <linux/usb/gpio_vbus.h>
#include <linux/i2c/pxa-i2c.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/pxa27x.h>
#include <mach/hx4700.h>
#include <mach/irda.h>

#include <video/platform_lcd.h>
#include <video/w100fb.h>

#include "devices.h"
#include "generic.h"

/* Physical address space information */

#define ATI_W3220_PHYS  PXA_CS2_PHYS /* ATI Imageon 3220 Graphics */
#define ASIC3_PHYS      PXA_CS3_PHYS
#define ASIC3_SD_PHYS   (PXA_CS3_PHYS + 0x02000000)

static unsigned long hx4700_pin_config[] __initdata = {

	/* SDRAM and Static Memory I/O Signals */
	GPIO20_nSDCS_2,
	GPIO21_nSDCS_3,
	GPIO15_nCS_1,
	GPIO78_nCS_2,   /* W3220 */
	GPIO79_nCS_3,   /* ASIC3 */
	GPIO80_nCS_4,
	GPIO33_nCS_5,	/* EGPIO, WLAN */

	/* PC CARD */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO54_nPCE_2,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,
	GPIO85_nPCE_1,
	GPIO104_PSKTSEL,

	/* I2C */
	GPIO117_I2C_SCL,
	GPIO118_I2C_SDA,

	/* FFUART (RS-232) */
	GPIO34_FFUART_RXD,
	GPIO35_FFUART_CTS,
	GPIO36_FFUART_DCD,
	GPIO37_FFUART_DSR,
	GPIO38_FFUART_RI,
	GPIO39_FFUART_TXD,
	GPIO40_FFUART_DTR,
	GPIO41_FFUART_RTS,

	/* BTUART */
	GPIO42_BTUART_RXD,
	GPIO43_BTUART_TXD,
	GPIO44_BTUART_CTS,
	GPIO45_BTUART_RTS,

	/* PWM 1 (Backlight) */
	GPIO17_PWM1_OUT,

	/* I2S */
	GPIO28_I2S_BITCLK_OUT,
	GPIO29_I2S_SDATA_IN,
	GPIO30_I2S_SDATA_OUT,
	GPIO31_I2S_SYNC,
	GPIO113_I2S_SYSCLK,

	/* SSP 1 (NavPoint) */
	GPIO23_SSP1_SCLK,
	GPIO24_SSP1_SFRM,
	GPIO25_SSP1_TXD,
	GPIO26_SSP1_RXD,

	/* SSP 2 (TSC2046) */
	GPIO19_SSP2_SCLK,
	GPIO86_SSP2_RXD,
	GPIO87_SSP2_TXD,
	GPIO88_GPIO,

	/* HX4700 specific input GPIOs */
	GPIO12_GPIO,	/* ASIC3_IRQ */
	GPIO13_GPIO,	/* W3220_IRQ */
	GPIO14_GPIO,	/* nWLAN_IRQ */

	GPIO10_GPIO,	/* GSM_IRQ */
	GPIO13_GPIO,	/* CPLD_IRQ */
	GPIO107_GPIO,	/* DS1WM_IRQ */
	GPIO108_GPIO,	/* GSM_READY */
	GPIO58_GPIO,	/* TSC2046_nPENIRQ */
	GPIO66_GPIO,	/* nSDIO_IRQ */
};

/*
 * IRDA
 */

static struct pxaficp_platform_data ficp_info = {
	.gpio_pwdown		= GPIO105_HX4700_nIR_ON,
	.transceiver_cap	= IR_SIRMODE | IR_OFF,
};

/*
 * GPIO Keys
 */

#define INIT_KEY(_code, _gpio, _active_low, _desc)	\
	{						\
		.code       = KEY_##_code,		\
		.gpio       = _gpio,			\
		.active_low = _active_low,		\
		.desc       = _desc,			\
		.type       = EV_KEY,			\
		.wakeup     = 1,			\
	}

static struct gpio_keys_button gpio_keys_buttons[] = {
	INIT_KEY(POWER,       GPIO0_HX4700_nKEY_POWER,   1, "Power button"),
	INIT_KEY(MAIL,        GPIO94_HX4700_KEY_MAIL,    0, "Mail button"),
	INIT_KEY(ADDRESSBOOK, GPIO99_HX4700_KEY_CONTACTS,0, "Contacts button"),
	INIT_KEY(RECORD,      GPIOD6_nKEY_RECORD,        1, "Record button"),
	INIT_KEY(CALENDAR,    GPIOD1_nKEY_CALENDAR,      1, "Calendar button"),
	INIT_KEY(HOMEPAGE,    GPIOD3_nKEY_HOME,          1, "Home button"),
};

static struct gpio_keys_platform_data gpio_keys_data = {
	.buttons = gpio_keys_buttons,
	.nbuttons = ARRAY_SIZE(gpio_keys_buttons),
};

static struct platform_device gpio_keys = {
	.name = "gpio-keys",
	.dev  = {
		.platform_data = &gpio_keys_data,
	},
	.id   = -1,
};

/*
 * ASIC3
 */

static u16 asic3_gpio_config[] = {
	/* ASIC3 GPIO banks A and B along with some of C and D
	   implement the buffering for the CF slot. */
	ASIC3_CONFIG_GPIO(0, 1, 1, 0),
	ASIC3_CONFIG_GPIO(1, 1, 1, 0),
	ASIC3_CONFIG_GPIO(2, 1, 1, 0),
	ASIC3_CONFIG_GPIO(3, 1, 1, 0),
	ASIC3_CONFIG_GPIO(4, 1, 1, 0),
	ASIC3_CONFIG_GPIO(5, 1, 1, 0),
	ASIC3_CONFIG_GPIO(6, 1, 1, 0),
	ASIC3_CONFIG_GPIO(7, 1, 1, 0),
	ASIC3_CONFIG_GPIO(8, 1, 1, 0),
	ASIC3_CONFIG_GPIO(9, 1, 1, 0),
	ASIC3_CONFIG_GPIO(10, 1, 1, 0),
	ASIC3_CONFIG_GPIO(11, 1, 1, 0),
	ASIC3_CONFIG_GPIO(12, 1, 1, 0),
	ASIC3_CONFIG_GPIO(13, 1, 1, 0),
	ASIC3_CONFIG_GPIO(14, 1, 1, 0),
	ASIC3_CONFIG_GPIO(15, 1, 1, 0),

	ASIC3_CONFIG_GPIO(16, 1, 1, 0),
	ASIC3_CONFIG_GPIO(17, 1, 1, 0),
	ASIC3_CONFIG_GPIO(18, 1, 1, 0),
	ASIC3_CONFIG_GPIO(19, 1, 1, 0),
	ASIC3_CONFIG_GPIO(20, 1, 1, 0),
	ASIC3_CONFIG_GPIO(21, 1, 1, 0),
	ASIC3_CONFIG_GPIO(22, 1, 1, 0),
	ASIC3_CONFIG_GPIO(23, 1, 1, 0),
	ASIC3_CONFIG_GPIO(24, 1, 1, 0),
	ASIC3_CONFIG_GPIO(25, 1, 1, 0),
	ASIC3_CONFIG_GPIO(26, 1, 1, 0),
	ASIC3_CONFIG_GPIO(27, 1, 1, 0),
	ASIC3_CONFIG_GPIO(28, 1, 1, 0),
	ASIC3_CONFIG_GPIO(29, 1, 1, 0),
	ASIC3_CONFIG_GPIO(30, 1, 1, 0),
	ASIC3_CONFIG_GPIO(31, 1, 1, 0),

	/* GPIOC - CF, LEDs, SD */
	ASIC3_GPIOC0_LED0,		/* red */
	ASIC3_GPIOC1_LED1,		/* green */
	ASIC3_GPIOC2_LED2,		/* blue */
	ASIC3_GPIOC4_CF_nCD,
	ASIC3_GPIOC5_nCIOW,
	ASIC3_GPIOC6_nCIOR,
	ASIC3_GPIOC7_nPCE_1,
	ASIC3_GPIOC8_nPCE_2,
	ASIC3_GPIOC9_nPOE,
	ASIC3_GPIOC10_nPWE,
	ASIC3_GPIOC11_PSKTSEL,
	ASIC3_GPIOC12_nPREG,
	ASIC3_GPIOC13_nPWAIT,
	ASIC3_GPIOC14_nPIOIS16,
	ASIC3_GPIOC15_nPIOR,

	/* GPIOD: input GPIOs, CF */
	ASIC3_GPIOD11_nCIOIS16,
	ASIC3_GPIOD12_nCWAIT,
	ASIC3_GPIOD15_nPIOW,
};

static struct resource asic3_resources[] = {
	/* GPIO part */
	[0] = {
		.start	= ASIC3_PHYS,
		.end	= ASIC3_PHYS + ASIC3_MAP_SIZE_16BIT - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gpio_to_irq(GPIO12_HX4700_ASIC3_IRQ),
		.end	= gpio_to_irq(GPIO12_HX4700_ASIC3_IRQ),
		.flags	= IORESOURCE_IRQ,
	},
	/* SD part */
	[2] = {
		.start	= ASIC3_SD_PHYS,
		.end	= ASIC3_SD_PHYS + ASIC3_MAP_SIZE_16BIT - 1,
		.flags	= IORESOURCE_MEM,
	},
	[3] = {
		.start	= gpio_to_irq(GPIO66_HX4700_ASIC3_nSDIO_IRQ),
		.end	= gpio_to_irq(GPIO66_HX4700_ASIC3_nSDIO_IRQ),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct asic3_platform_data asic3_platform_data = {
	.gpio_config     = asic3_gpio_config,
	.gpio_config_num = ARRAY_SIZE(asic3_gpio_config),
	.irq_base        = IRQ_BOARD_START,
	.gpio_base       = HX4700_ASIC3_GPIO_BASE,
};

static struct platform_device asic3 = {
	.name          = "asic3",
	.id            = -1,
	.resource      = asic3_resources,
	.num_resources = ARRAY_SIZE(asic3_resources),
	.dev = {
		.platform_data = &asic3_platform_data,
	},
};

/*
 * EGPIO
 */

static struct resource egpio_resources[] = {
	[0] = {
		.start = PXA_CS5_PHYS,
		.end   = PXA_CS5_PHYS + 0x4 - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct htc_egpio_chip egpio_chips[] = {
	[0] = {
		.reg_start = 0,
		.gpio_base = HX4700_EGPIO_BASE,
		.num_gpios = 8,
		.direction = HTC_EGPIO_OUTPUT,
	},
};

static struct htc_egpio_platform_data egpio_info = {
	.reg_width = 16,
	.bus_width = 16,
	.chip      = egpio_chips,
	.num_chips = ARRAY_SIZE(egpio_chips),
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
 * LCD - Sony display connected to ATI Imageon w3220
 */

static void sony_lcd_init(void)
{
	gpio_set_value(GPIO84_HX4700_LCD_SQN, 1);
	gpio_set_value(GPIO110_HX4700_LCD_LVDD_3V3_ON, 0);
	gpio_set_value(GPIO111_HX4700_LCD_AVDD_3V3_ON, 0);
	gpio_set_value(GPIO70_HX4700_LCD_SLIN1, 0);
	gpio_set_value(GPIO62_HX4700_LCD_nRESET, 0);
	mdelay(10);
	gpio_set_value(GPIO59_HX4700_LCD_PC1, 0);
	gpio_set_value(GPIO110_HX4700_LCD_LVDD_3V3_ON, 0);
	mdelay(20);

	gpio_set_value(GPIO110_HX4700_LCD_LVDD_3V3_ON, 1);
	mdelay(5);
	gpio_set_value(GPIO111_HX4700_LCD_AVDD_3V3_ON, 1);

	/* FIXME: init w3220 registers here */

	mdelay(5);
	gpio_set_value(GPIO70_HX4700_LCD_SLIN1, 1);
	mdelay(10);
	gpio_set_value(GPIO62_HX4700_LCD_nRESET, 1);
	mdelay(10);
	gpio_set_value(GPIO59_HX4700_LCD_PC1, 1);
	mdelay(10);
	gpio_set_value(GPIO112_HX4700_LCD_N2V7_7V3_ON, 1);
}

static void sony_lcd_off(void)
{
	gpio_set_value(GPIO59_HX4700_LCD_PC1, 0);
	gpio_set_value(GPIO62_HX4700_LCD_nRESET, 0);
	mdelay(10);
	gpio_set_value(GPIO112_HX4700_LCD_N2V7_7V3_ON, 0);
	mdelay(10);
	gpio_set_value(GPIO111_HX4700_LCD_AVDD_3V3_ON, 0);
	mdelay(10);
	gpio_set_value(GPIO110_HX4700_LCD_LVDD_3V3_ON, 0);
}

#ifdef CONFIG_PM
static void w3220_lcd_suspend(struct w100fb_par *wfb)
{
	sony_lcd_off();
}

static void w3220_lcd_resume(struct w100fb_par *wfb)
{
	sony_lcd_init();
}
#else
#define w3220_lcd_resume	NULL
#define w3220_lcd_suspend	NULL
#endif

static struct w100_tg_info w3220_tg_info = {
	.suspend	= w3220_lcd_suspend,
	.resume		= w3220_lcd_resume,
};

/*  				 W3220_VGA		QVGA */
static struct w100_gen_regs w3220_regs = {
	.lcd_format =        0x00000003,
	.lcdd_cntl1 =        0x00000000,
	.lcdd_cntl2 =        0x0003ffff,
	.genlcd_cntl1 =      0x00abf003,	/* 0x00fff003 */
	.genlcd_cntl2 =      0x00000003,
	.genlcd_cntl3 =      0x000102aa,
};

static struct w100_mode w3220_modes[] = {
{
	.xres 		= 480,
	.yres 		= 640,
	.left_margin 	= 15,
	.right_margin 	= 16,
	.upper_margin 	= 8,
	.lower_margin 	= 7,
	.crtc_ss	= 0x00000000,
	.crtc_ls	= 0xa1ff01f9,	/* 0x21ff01f9 */
	.crtc_gs	= 0xc0000000,	/* 0x40000000 */
	.crtc_vpos_gs	= 0x0000028f,
	.crtc_ps1_active = 0x00000000,	/* 0x41060010 */
	.crtc_rev	= 0,
	.crtc_dclk	= 0x80000000,
	.crtc_gclk	= 0x040a0104,
	.crtc_goe	= 0,
	.pll_freq 	= 95,
	.pixclk_divider = 4,
	.pixclk_divider_rotated = 4,
	.pixclk_src     = CLK_SRC_PLL,
	.sysclk_divider = 0,
	.sysclk_src     = CLK_SRC_PLL,
},
{
	.xres 		= 240,
	.yres 		= 320,
	.left_margin 	= 9,
	.right_margin 	= 8,
	.upper_margin 	= 5,
	.lower_margin 	= 4,
	.crtc_ss	= 0x80150014,
	.crtc_ls        = 0xa0fb00f7,
	.crtc_gs	= 0xc0080007,
	.crtc_vpos_gs	= 0x00080007,
	.crtc_rev	= 0x0000000a,
	.crtc_dclk	= 0x81700030,
	.crtc_gclk	= 0x8015010f,
	.crtc_goe	= 0x00000000,
	.pll_freq 	= 95,
	.pixclk_divider = 4,
	.pixclk_divider_rotated = 4,
	.pixclk_src     = CLK_SRC_PLL,
	.sysclk_divider = 0,
	.sysclk_src     = CLK_SRC_PLL,
},
};

struct w100_mem_info w3220_mem_info = {
	.ext_cntl        = 0x09640011,
	.sdram_mode_reg  = 0x00600021,
	.ext_timing_cntl = 0x1a001545,	/* 0x15001545 */
	.io_cntl         = 0x7ddd7333,
	.size            = 0x1fffff,
};

struct w100_bm_mem_info w3220_bm_mem_info = {
	.ext_mem_bw = 0x50413e01,
	.offset = 0,
	.ext_timing_ctl = 0x00043f7f,
	.ext_cntl = 0x00000010,
	.mode_reg = 0x00250000,
	.io_cntl = 0x0fff0000,
	.config = 0x08301480,
};

static struct w100_gpio_regs w3220_gpio_info = {
	.init_data1 = 0xdfe00100,	/* GPIO_DATA */
	.gpio_dir1  = 0xffff0000,	/* GPIO_CNTL1 */
	.gpio_oe1   = 0x00000000,	/* GPIO_CNTL2 */
	.init_data2 = 0x00000000,	/* GPIO_DATA2 */
	.gpio_dir2  = 0x00000000,	/* GPIO_CNTL3 */
	.gpio_oe2   = 0x00000000,	/* GPIO_CNTL4 */
};

static struct w100fb_mach_info w3220_info = {
	.tg        = &w3220_tg_info,
	.mem       = &w3220_mem_info,
	.bm_mem    = &w3220_bm_mem_info,
	.gpio      = &w3220_gpio_info,
	.regs      = &w3220_regs,
	.modelist  = w3220_modes,
	.num_modes = 2,
	.xtal_freq = 16000000,
};

static struct resource w3220_resources[] = {
	[0] = {
		.start	= ATI_W3220_PHYS,
		.end	= ATI_W3220_PHYS + 0x00ffffff,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device w3220 = {
	.name	= "w100fb",
	.id	= -1,
	.dev	= {
		.platform_data = &w3220_info,
	},
	.num_resources = ARRAY_SIZE(w3220_resources),
	.resource      = w3220_resources,
};

static void hx4700_lcd_set_power(struct plat_lcd_data *pd, unsigned int power)
{
	if (power)
		sony_lcd_init();
	else
		sony_lcd_off();
}

static struct plat_lcd_data hx4700_lcd_data = {
	.set_power = hx4700_lcd_set_power,
};

static struct platform_device hx4700_lcd = {
	.name = "platform-lcd",
	.id   = -1,
	.dev  = {
		.platform_data = &hx4700_lcd_data,
		.parent        = &w3220.dev,
	},
};

/*
 * Backlight
 */

static struct platform_pwm_backlight_data backlight_data = {
	.pwm_id         = 1,
	.max_brightness = 200,
	.dft_brightness = 100,
	.pwm_period_ns  = 30923,
};

static struct platform_device backlight = {
	.name = "pwm-backlight",
	.id   = -1,
	.dev  = {
		.parent        = &pxa27x_device_pwm1.dev,
		.platform_data = &backlight_data,
	},
};

/*
 * USB "Transceiver"
 */

static struct gpio_vbus_mach_info gpio_vbus_info = {
	.gpio_pullup        = GPIO76_HX4700_USBC_PUEN,
	.gpio_vbus          = GPIOD14_nUSBC_DETECT,
	.gpio_vbus_inverted = 1,
};

static struct platform_device gpio_vbus = {
	.name          = "gpio-vbus",
	.id            = -1,
	.dev = {
		.platform_data = &gpio_vbus_info,
	},
};

/*
 * Touchscreen - TSC2046 connected to SSP2
 */

static const struct ads7846_platform_data tsc2046_info = {
	.model            = 7846,
	.vref_delay_usecs = 100,
	.pressure_max     = 1024,
	.debounce_max     = 10,
	.debounce_tol     = 3,
	.debounce_rep     = 1,
	.gpio_pendown     = GPIO58_HX4700_TSC2046_nPENIRQ,
};

static struct pxa2xx_spi_chip tsc2046_chip = {
	.tx_threshold = 1,
	.rx_threshold = 2,
	.timeout      = 64,
	.gpio_cs      = GPIO88_HX4700_TSC2046_CS,
};

static struct spi_board_info tsc2046_board_info[] __initdata = {
	{
		.modalias        = "ads7846",
		.bus_num         = 2,
		.max_speed_hz    = 2600000, /* 100 kHz sample rate */
		.irq             = gpio_to_irq(GPIO58_HX4700_TSC2046_nPENIRQ),
		.platform_data   = &tsc2046_info,
		.controller_data = &tsc2046_chip,
	},
};

static struct pxa2xx_spi_master pxa_ssp2_master_info = {
	.num_chipselect = 1,
	.clock_enable   = CKEN_SSP2,
	.enable_dma     = 1,
};

/*
 * External power
 */

static int power_supply_init(struct device *dev)
{
	return gpio_request(GPIOD9_nAC_IN, "AC charger detect");
}

static int hx4700_is_ac_online(void)
{
	return !gpio_get_value(GPIOD9_nAC_IN);
}

static void power_supply_exit(struct device *dev)
{
	gpio_free(GPIOD9_nAC_IN);
}

static char *hx4700_supplicants[] = {
	"ds2760-battery.0", "backup-battery"
};

static struct pda_power_pdata power_supply_info = {
	.init            = power_supply_init,
	.is_ac_online    = hx4700_is_ac_online,
	.exit            = power_supply_exit,
	.supplied_to     = hx4700_supplicants,
	.num_supplicants = ARRAY_SIZE(hx4700_supplicants),
};

static struct resource power_supply_resources[] = {
	[0] = {
		.name  = "ac",
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
		         IORESOURCE_IRQ_LOWEDGE,
		.start = gpio_to_irq(GPIOD9_nAC_IN),
		.end   = gpio_to_irq(GPIOD9_nAC_IN),
	},
	[1] = {
		.name  = "usb",
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE |
		         IORESOURCE_IRQ_LOWEDGE,
		.start = gpio_to_irq(GPIOD14_nUSBC_DETECT),
		.end   = gpio_to_irq(GPIOD14_nUSBC_DETECT),
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
 * Battery charger
 */

static struct regulator_consumer_supply bq24022_consumers[] = {
	{
		.dev = &gpio_vbus.dev,
		.supply = "vbus_draw",
	},
	{
		.dev = &power_supply.dev,
		.supply = "ac_draw",
	},
};

static struct regulator_init_data bq24022_init_data = {
	.constraints = {
		.max_uA         = 500000,
		.valid_ops_mask = REGULATOR_CHANGE_CURRENT|REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(bq24022_consumers),
	.consumer_supplies      = bq24022_consumers,
};

static struct bq24022_mach_info bq24022_info = {
	.gpio_nce   = GPIO72_HX4700_BQ24022_nCHARGE_EN,
	.gpio_iset2 = GPIO96_HX4700_BQ24022_ISET2,
	.init_data  = &bq24022_init_data,
};

static struct platform_device bq24022 = {
	.name = "bq24022",
	.id   = -1,
	.dev  = {
		.platform_data = &bq24022_info,
	},
};

/*
 * StrataFlash
 */

static void hx4700_set_vpp(struct platform_device *pdev, int vpp)
{
	gpio_set_value(GPIO91_HX4700_FLASH_VPEN, vpp);
}

static struct resource strataflash_resource = {
	.start = PXA_CS0_PHYS,
	.end   = PXA_CS0_PHYS + SZ_128M - 1,
	.flags = IORESOURCE_MEM,
};

static struct physmap_flash_data strataflash_data = {
	.width = 4,
	.set_vpp = hx4700_set_vpp,
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
 * Maxim MAX1587A on PI2C
 */

static struct regulator_consumer_supply max1587a_consumer = {
	.supply = "vcc_core",
};

static struct regulator_init_data max1587a_v3_info = {
	.constraints = {
		.name = "vcc_core range",
		.min_uV =  900000,
		.max_uV = 1705000,
		.always_on = 1,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE,
	},
	.num_consumer_supplies = 1,
	.consumer_supplies     = &max1587a_consumer,
};

static struct max1586_subdev_data max1587a_subdev = {
	.name = "vcc_core",
	.id   = MAX1586_V3,
	.platform_data = &max1587a_v3_info,
};

static struct max1586_platform_data max1587a_info = {
	.num_subdevs = 1,
	.subdevs     = &max1587a_subdev,
	.v3_gain     = MAX1586_GAIN_R24_3k32, /* 730..1550 mV */
};

static struct i2c_board_info __initdata pi2c_board_info[] = {
	{
		I2C_BOARD_INFO("max1586", 0x14),
		.platform_data = &max1587a_info,
	},
};

/*
 * PCMCIA
 */

static struct platform_device pcmcia = {
	.name = "hx4700-pcmcia",
	.dev  = {
		.parent = &asic3.dev,
	},
};

/*
 * Platform devices
 */

static struct platform_device *devices[] __initdata = {
	&asic3,
	&gpio_keys,
	&backlight,
	&w3220,
	&hx4700_lcd,
	&egpio,
	&bq24022,
	&gpio_vbus,
	&power_supply,
	&strataflash,
	&pcmcia,
};

static struct gpio global_gpios[] = {
	{ GPIO12_HX4700_ASIC3_IRQ, GPIOF_IN, "ASIC3_IRQ" },
	{ GPIO13_HX4700_W3220_IRQ, GPIOF_IN, "W3220_IRQ" },
	{ GPIO14_HX4700_nWLAN_IRQ, GPIOF_IN, "WLAN_IRQ" },
	{ GPIO59_HX4700_LCD_PC1,          GPIOF_OUT_INIT_HIGH, "LCD_PC1" },
	{ GPIO62_HX4700_LCD_nRESET,       GPIOF_OUT_INIT_HIGH, "LCD_RESET" },
	{ GPIO70_HX4700_LCD_SLIN1,        GPIOF_OUT_INIT_HIGH, "LCD_SLIN1" },
	{ GPIO84_HX4700_LCD_SQN,          GPIOF_OUT_INIT_HIGH, "LCD_SQN" },
	{ GPIO110_HX4700_LCD_LVDD_3V3_ON, GPIOF_OUT_INIT_HIGH, "LCD_LVDD" },
	{ GPIO111_HX4700_LCD_AVDD_3V3_ON, GPIOF_OUT_INIT_HIGH, "LCD_AVDD" },
	{ GPIO32_HX4700_RS232_ON,         GPIOF_OUT_INIT_HIGH, "RS232_ON" },
	{ GPIO71_HX4700_ASIC3_nRESET,     GPIOF_OUT_INIT_HIGH, "ASIC3_nRESET" },
	{ GPIO82_HX4700_EUART_RESET,      GPIOF_OUT_INIT_HIGH, "EUART_RESET" },
	{ GPIO105_HX4700_nIR_ON,          GPIOF_OUT_INIT_HIGH, "nIR_EN" },
};

static void __init hx4700_init(void)
{
	int ret;

	pxa2xx_mfp_config(ARRAY_AND_SIZE(hx4700_pin_config));
	ret = gpio_request_array(ARRAY_AND_SIZE(global_gpios));
	if (ret)
		pr_err ("hx4700: Failed to request GPIOs.\n");

	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	platform_add_devices(devices, ARRAY_SIZE(devices));

	pxa_set_ficp_info(&ficp_info);
	pxa27x_set_i2c_power_info(NULL);
	pxa_set_i2c_info(NULL);
	i2c_register_board_info(1, ARRAY_AND_SIZE(pi2c_board_info));
	pxa2xx_set_spi_info(2, &pxa_ssp2_master_info);
	spi_register_board_info(ARRAY_AND_SIZE(tsc2046_board_info));

	gpio_set_value(GPIO71_HX4700_ASIC3_nRESET, 0);
	mdelay(10);
	gpio_set_value(GPIO71_HX4700_ASIC3_nRESET, 1);
	mdelay(10);
}

MACHINE_START(H4700, "HP iPAQ HX4700")
	.boot_params  = 0xa0000100,
	.map_io       = pxa27x_map_io,
	.nr_irqs      = HX4700_NR_IRQS,
	.init_irq     = pxa27x_init_irq,
	.handle_irq     = pxa27x_handle_irq,
	.init_machine = hx4700_init,
	.timer        = &pxa_timer,
MACHINE_END
