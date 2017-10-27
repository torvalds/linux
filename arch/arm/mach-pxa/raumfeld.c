/*
 * arch/arm/mach-pxa/raumfeld.c
 *
 * Support for the following Raumfeld devices:
 *
 * 	* Controller
 *  	* Connector
 *  	* Speaker S/M
 *
 * See http://www.raumfeld.com for details.
 *
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <linux/smsc911x.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/leds.h>
#include <linux/w1-gpio.h>
#include <linux/sched.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/i2c.h>
#include <linux/i2c/pxa-i2c.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/lis3lv02d.h>
#include <linux/pda_power.h>
#include <linux/power_supply.h>
#include <linux/regulator/max8660.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#include <asm/system_info.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include "pxa300.h"
#include <linux/platform_data/usb-ohci-pxa27x.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/mtd-nand-pxa3xx.h>

#include "generic.h"
#include "devices.h"

/* common GPIO	definitions */

/* inputs */
#define GPIO_ON_OFF		(14)
#define GPIO_VOLENC_A		(19)
#define GPIO_VOLENC_B		(20)
#define GPIO_CHARGE_DONE	(23)
#define GPIO_CHARGE_IND		(27)
#define GPIO_TOUCH_IRQ		(32)
#define GPIO_ETH_IRQ		(40)
#define GPIO_SPI_MISO		(98)
#define GPIO_ACCEL_IRQ		(104)
#define GPIO_RESCUE_BOOT	(115)
#define GPIO_DOCK_DETECT	(116)
#define GPIO_KEY1		(117)
#define GPIO_KEY2		(118)
#define GPIO_KEY3		(119)
#define GPIO_CHARGE_USB_OK	(112)
#define GPIO_CHARGE_DC_OK	(101)
#define GPIO_CHARGE_USB_SUSP	(102)

/* outputs */
#define GPIO_SHUTDOWN_SUPPLY	(16)
#define GPIO_SHUTDOWN_BATT	(18)
#define GPIO_CHRG_PEN2		(31)
#define GPIO_TFT_VA_EN		(33)
#define GPIO_SPDIF_CS		(34)
#define GPIO_LED2		(35)
#define GPIO_LED1		(36)
#define GPIO_SPDIF_RESET	(38)
#define GPIO_SPI_CLK		(95)
#define GPIO_MCLK_DAC_CS	(96)
#define GPIO_SPI_MOSI		(97)
#define GPIO_W1_PULLUP_ENABLE	(105)
#define GPIO_DISPLAY_ENABLE	(106)
#define GPIO_MCLK_RESET		(111)
#define GPIO_W2W_RESET		(113)
#define GPIO_W2W_PDN		(114)
#define GPIO_CODEC_RESET	(120)
#define GPIO_AUDIO_VA_ENABLE	(124)
#define GPIO_ACCEL_CS		(125)
#define GPIO_ONE_WIRE		(126)

/*
 * GPIO configurations
 */
static mfp_cfg_t raumfeld_controller_pin_config[] __initdata = {
	/* UART1 */
	GPIO77_UART1_RXD,
	GPIO78_UART1_TXD,
	GPIO79_UART1_CTS,
	GPIO81_UART1_DSR,
	GPIO83_UART1_DTR,
	GPIO84_UART1_RTS,

	/* UART3 */
	GPIO110_UART3_RXD,

	/* USB Host */
	GPIO0_2_USBH_PEN,
	GPIO1_2_USBH_PWR,

	/* I2C */
	GPIO21_I2C_SCL | MFP_LPM_FLOAT | MFP_PULL_FLOAT,
	GPIO22_I2C_SDA | MFP_LPM_FLOAT | MFP_PULL_FLOAT,

	/* SPI */
	GPIO34_GPIO,	/* SPDIF_CS */
	GPIO96_GPIO,	/* MCLK_CS */
	GPIO125_GPIO,	/* ACCEL_CS */

	/* MMC */
	GPIO3_MMC1_DAT0,
	GPIO4_MMC1_DAT1,
	GPIO5_MMC1_DAT2,
	GPIO6_MMC1_DAT3,
	GPIO7_MMC1_CLK,
	GPIO8_MMC1_CMD,

	/* One-wire */
	GPIO126_GPIO | MFP_LPM_FLOAT,
	GPIO105_GPIO | MFP_PULL_LOW | MFP_LPM_PULL_LOW,

	/* CHRG_USB_OK */
	GPIO101_GPIO | MFP_PULL_HIGH,
	/* CHRG_USB_OK */
	GPIO112_GPIO | MFP_PULL_HIGH,
	/* CHRG_USB_SUSP */
	GPIO102_GPIO,
	/* DISPLAY_ENABLE */
	GPIO106_GPIO,
	/* DOCK_DETECT */
	GPIO116_GPIO | MFP_LPM_FLOAT | MFP_PULL_FLOAT,

	/* LCD */
	GPIO54_LCD_LDD_0,
	GPIO55_LCD_LDD_1,
	GPIO56_LCD_LDD_2,
	GPIO57_LCD_LDD_3,
	GPIO58_LCD_LDD_4,
	GPIO59_LCD_LDD_5,
	GPIO60_LCD_LDD_6,
	GPIO61_LCD_LDD_7,
	GPIO62_LCD_LDD_8,
	GPIO63_LCD_LDD_9,
	GPIO64_LCD_LDD_10,
	GPIO65_LCD_LDD_11,
	GPIO66_LCD_LDD_12,
	GPIO67_LCD_LDD_13,
	GPIO68_LCD_LDD_14,
	GPIO69_LCD_LDD_15,
	GPIO70_LCD_LDD_16,
	GPIO71_LCD_LDD_17,
	GPIO72_LCD_FCLK,
	GPIO73_LCD_LCLK,
	GPIO74_LCD_PCLK,
	GPIO75_LCD_BIAS,
};

static mfp_cfg_t raumfeld_connector_pin_config[] __initdata = {
	/* UART1 */
	GPIO77_UART1_RXD,
	GPIO78_UART1_TXD,
	GPIO79_UART1_CTS,
	GPIO81_UART1_DSR,
	GPIO83_UART1_DTR,
	GPIO84_UART1_RTS,

	/* UART3 */
	GPIO110_UART3_RXD,

	/* USB Host */
	GPIO0_2_USBH_PEN,
	GPIO1_2_USBH_PWR,

	/* I2C */
	GPIO21_I2C_SCL | MFP_LPM_FLOAT | MFP_PULL_FLOAT,
	GPIO22_I2C_SDA | MFP_LPM_FLOAT | MFP_PULL_FLOAT,

	/* SPI */
	GPIO34_GPIO,	/* SPDIF_CS */
	GPIO96_GPIO,	/* MCLK_CS */
	GPIO125_GPIO,	/* ACCEL_CS */

	/* MMC */
	GPIO3_MMC1_DAT0,
	GPIO4_MMC1_DAT1,
	GPIO5_MMC1_DAT2,
	GPIO6_MMC1_DAT3,
	GPIO7_MMC1_CLK,
	GPIO8_MMC1_CMD,

	/* Ethernet */
	GPIO1_nCS2,			/* CS */
	GPIO40_GPIO | MFP_PULL_HIGH,	/* IRQ */

	/* SSP for I2S */
	GPIO85_SSP1_SCLK,
	GPIO89_SSP1_EXTCLK,
	GPIO86_SSP1_FRM,
	GPIO87_SSP1_TXD,
	GPIO88_SSP1_RXD,
	GPIO90_SSP1_SYSCLK,

	/* SSP2 for S/PDIF */
	GPIO25_SSP2_SCLK,
	GPIO26_SSP2_FRM,
	GPIO27_SSP2_TXD,
	GPIO29_SSP2_EXTCLK,

	/* LEDs */
	GPIO35_GPIO | MFP_LPM_PULL_LOW,
	GPIO36_GPIO | MFP_LPM_DRIVE_HIGH,
};

static mfp_cfg_t raumfeld_speaker_pin_config[] __initdata = {
	/* UART1 */
	GPIO77_UART1_RXD,
	GPIO78_UART1_TXD,
	GPIO79_UART1_CTS,
	GPIO81_UART1_DSR,
	GPIO83_UART1_DTR,
	GPIO84_UART1_RTS,

	/* UART3 */
	GPIO110_UART3_RXD,

	/* USB Host */
	GPIO0_2_USBH_PEN,
	GPIO1_2_USBH_PWR,

	/* I2C */
	GPIO21_I2C_SCL | MFP_LPM_FLOAT | MFP_PULL_FLOAT,
	GPIO22_I2C_SDA | MFP_LPM_FLOAT | MFP_PULL_FLOAT,

	/* SPI */
	GPIO34_GPIO,	/* SPDIF_CS */
	GPIO96_GPIO,	/* MCLK_CS */
	GPIO125_GPIO,	/* ACCEL_CS */

	/* MMC */
	GPIO3_MMC1_DAT0,
	GPIO4_MMC1_DAT1,
	GPIO5_MMC1_DAT2,
	GPIO6_MMC1_DAT3,
	GPIO7_MMC1_CLK,
	GPIO8_MMC1_CMD,

	/* Ethernet */
	GPIO1_nCS2,			/* CS */
	GPIO40_GPIO | MFP_PULL_HIGH,	/* IRQ */

	/* SSP for I2S */
	GPIO85_SSP1_SCLK,
	GPIO89_SSP1_EXTCLK,
	GPIO86_SSP1_FRM,
	GPIO87_SSP1_TXD,
	GPIO88_SSP1_RXD,
	GPIO90_SSP1_SYSCLK,

	/* LEDs */
	GPIO35_GPIO | MFP_LPM_PULL_LOW,
	GPIO36_GPIO | MFP_LPM_DRIVE_HIGH,
};

/*
 * SMSC LAN9220 Ethernet
 */

static struct resource smc91x_resources[] = {
	{
		.start	= PXA3xx_CS2_PHYS,
		.end	= PXA3xx_CS2_PHYS + 0xfffff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= PXA_GPIO_TO_IRQ(GPIO_ETH_IRQ),
		.end	= PXA_GPIO_TO_IRQ(GPIO_ETH_IRQ),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_FALLING,
	}
};

static struct smsc911x_platform_config raumfeld_smsc911x_config = {
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS,
};

static struct platform_device smc91x_device = {
	.name		= "smsc911x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
	.dev		= {
		.platform_data = &raumfeld_smsc911x_config,
	}
};

/**
 * NAND
 */

static struct mtd_partition raumfeld_nand_partitions[] = {
	{
		.name		= "Bootloader",
		.offset		= 0,
		.size		= 0xa0000,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	{
		.name		= "BootloaderEnvironment",
		.offset		= 0xa0000,
		.size		= 0x20000,
	},
	{
		.name		= "BootloaderSplashScreen",
		.offset		= 0xc0000,
		.size		= 0x60000,
	},
	{
		.name		= "UBI",
		.offset		= 0x120000,
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct pxa3xx_nand_platform_data raumfeld_nand_info = {
	.enable_arbiter	= 1,
	.keep_config	= 1,
	.num_cs		= 1,
	.parts[0]	= raumfeld_nand_partitions,
	.nr_parts[0]	= ARRAY_SIZE(raumfeld_nand_partitions),
};

/**
 * USB (OHCI) support
 */

static struct pxaohci_platform_data raumfeld_ohci_info = {
	.port_mode      = PMM_GLOBAL_MODE,
	.flags		= ENABLE_PORT1,
};

/**
 * Rotary encoder input device
 */

static struct gpiod_lookup_table raumfeld_rotary_gpios_table = {
	.dev_id = "rotary-encoder.0",
	.table = {
		GPIO_LOOKUP_IDX("gpio-0",
				GPIO_VOLENC_A, NULL, 0, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-0",
				GPIO_VOLENC_B, NULL, 1, GPIO_ACTIVE_HIGH),
		{ },
	},
};

static const struct property_entry raumfeld_rotary_properties[] __initconst = {
	PROPERTY_ENTRY_INTEGER("rotary-encoder,steps-per-period", u32, 24),
	PROPERTY_ENTRY_INTEGER("linux,axis",			  u32, REL_X),
	PROPERTY_ENTRY_INTEGER("rotary-encoder,relative_axis",	  u32, 1),
	{ },
};

static struct platform_device rotary_encoder_device = {
	.name		= "rotary-encoder",
	.id		= 0,
};

/**
 * GPIO buttons
 */

static struct gpio_keys_button gpio_keys_button[] = {
	{
		.code			= KEY_F1,
		.type			= EV_KEY,
		.gpio			= GPIO_KEY1,
		.active_low		= 1,
		.wakeup			= 0,
		.debounce_interval	= 5, /* ms */
		.desc			= "Button 1",
	},
	{
		.code			= KEY_F2,
		.type			= EV_KEY,
		.gpio			= GPIO_KEY2,
		.active_low		= 1,
		.wakeup			= 0,
		.debounce_interval	= 5, /* ms */
		.desc			= "Button 2",
	},
	{
		.code			= KEY_F3,
		.type			= EV_KEY,
		.gpio			= GPIO_KEY3,
		.active_low		= 1,
		.wakeup			= 0,
		.debounce_interval	= 5, /* ms */
		.desc			= "Button 3",
	},
	{
		.code			= KEY_F4,
		.type			= EV_KEY,
		.gpio			= GPIO_RESCUE_BOOT,
		.active_low		= 0,
		.wakeup			= 0,
		.debounce_interval	= 5, /* ms */
		.desc			= "rescue boot button",
	},
	{
		.code			= KEY_F5,
		.type			= EV_KEY,
		.gpio			= GPIO_DOCK_DETECT,
		.active_low		= 1,
		.wakeup			= 0,
		.debounce_interval	= 5, /* ms */
		.desc			= "dock detect",
	},
	{
		.code			= KEY_F6,
		.type			= EV_KEY,
		.gpio			= GPIO_ON_OFF,
		.active_low		= 0,
		.wakeup			= 0,
		.debounce_interval	= 5, /* ms */
		.desc			= "on_off button",
	},
};

static struct gpio_keys_platform_data gpio_keys_platform_data = {
	.buttons	= gpio_keys_button,
	.nbuttons	= ARRAY_SIZE(gpio_keys_button),
	.rep		= 0,
};

static struct platform_device raumfeld_gpio_keys_device = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev 	= {
		.platform_data	= &gpio_keys_platform_data,
	}
};

/**
 * GPIO LEDs
 */

static struct gpio_led raumfeld_leds[] = {
	{
		.name		= "raumfeld:1",
		.gpio		= GPIO_LED1,
		.active_low	= 1,
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
	{
		.name		= "raumfeld:2",
		.gpio		= GPIO_LED2,
		.active_low	= 0,
		.default_state	= LEDS_GPIO_DEFSTATE_OFF,
	}
};

static struct gpio_led_platform_data raumfeld_led_platform_data = {
	.leds		= raumfeld_leds,
	.num_leds	= ARRAY_SIZE(raumfeld_leds),
};

static struct platform_device raumfeld_led_device = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &raumfeld_led_platform_data,
	},
};

/**
 * One-wire (W1 bus) support
 */

static void w1_enable_external_pullup(int enable)
{
	gpio_set_value(GPIO_W1_PULLUP_ENABLE, enable);
	msleep(100);
}

static struct w1_gpio_platform_data w1_gpio_platform_data = {
	.pin			= GPIO_ONE_WIRE,
	.is_open_drain		= 0,
	.enable_external_pullup	= w1_enable_external_pullup,
	.ext_pullup_enable_pin	= -EINVAL,
};

static struct platform_device raumfeld_w1_gpio_device = {
	.name	= "w1-gpio",
	.dev	= {
		.platform_data = &w1_gpio_platform_data
	}
};

static void __init raumfeld_w1_init(void)
{
	int ret = gpio_request(GPIO_W1_PULLUP_ENABLE,
				"W1 external pullup enable");

	if (ret < 0)
		pr_warn("Unable to request GPIO_W1_PULLUP_ENABLE\n");
	else
		gpio_direction_output(GPIO_W1_PULLUP_ENABLE, 0);

	platform_device_register(&raumfeld_w1_gpio_device);
}

/**
 * Framebuffer device
 */

static struct pwm_lookup raumfeld_pwm_lookup[] = {
	PWM_LOOKUP("pxa27x-pwm.0", 0, "pwm-backlight", NULL, 10000,
		   PWM_POLARITY_NORMAL),
};

/* PWM controlled backlight */
static struct platform_pwm_backlight_data raumfeld_pwm_backlight_data = {
	.max_brightness	= 100,
	.dft_brightness	= 100,
	.enable_gpio	= -1,
};

static struct platform_device raumfeld_pwm_backlight_device = {
	.name	= "pwm-backlight",
	.dev	= {
		.parent		= &pxa27x_device_pwm0.dev,
		.platform_data	= &raumfeld_pwm_backlight_data,
	}
};

/* LT3593 controlled backlight */
static struct gpio_led raumfeld_lt3593_led = {
	.name		= "backlight",
	.gpio		= mfp_to_gpio(MFP_PIN_GPIO17),
	.default_state	= LEDS_GPIO_DEFSTATE_ON,
};

static struct gpio_led_platform_data raumfeld_lt3593_platform_data = {
	.leds		= &raumfeld_lt3593_led,
	.num_leds	= 1,
};

static struct platform_device raumfeld_lt3593_device = {
	.name	= "leds-lt3593",
	.id	= -1,
	.dev	= {
		.platform_data = &raumfeld_lt3593_platform_data,
	},
};

static struct pxafb_mode_info sharp_lq043t3dx02_mode = {
	.pixclock	= 111000,
	.xres		= 480,
	.yres		= 272,
	.bpp		= 16,
	.hsync_len	= 41,
	.left_margin	= 2,
	.right_margin	= 1,
	.vsync_len	= 10,
	.upper_margin	= 3,
	.lower_margin	= 1,
	.sync		= 0,
};

static struct pxafb_mach_info raumfeld_sharp_lcd_info = {
	.modes		= &sharp_lq043t3dx02_mode,
	.num_modes	= 1,
	.video_mem_size = 0x400000,
	.lcd_conn	= LCD_COLOR_TFT_16BPP | LCD_PCLK_EDGE_FALL,
#ifdef CONFIG_PXA3XX_GCU
	.acceleration_enabled = 1,
#endif
};

static void __init raumfeld_lcd_init(void)
{
	int ret;

	ret = gpio_request(GPIO_TFT_VA_EN, "display VA enable");
	if (ret < 0)
		pr_warn("Unable to request GPIO_TFT_VA_EN\n");
	else
		gpio_direction_output(GPIO_TFT_VA_EN, 1);

	msleep(100);

	ret = gpio_request(GPIO_DISPLAY_ENABLE, "display enable");
	if (ret < 0)
		pr_warn("Unable to request GPIO_DISPLAY_ENABLE\n");
	else
		gpio_direction_output(GPIO_DISPLAY_ENABLE, 1);

	/* Hardware revision 2 has the backlight regulator controlled
	 * by an LT3593, earlier and later devices use PWM for that. */
	if ((system_rev & 0xff) == 2) {
		platform_device_register(&raumfeld_lt3593_device);
	} else {
		mfp_cfg_t raumfeld_pwm_pin_config = GPIO17_PWM0_OUT;
		pxa3xx_mfp_config(&raumfeld_pwm_pin_config, 1);
		pwm_add_table(raumfeld_pwm_lookup,
			      ARRAY_SIZE(raumfeld_pwm_lookup));
		platform_device_register(&raumfeld_pwm_backlight_device);
	}

	pxa_set_fb_info(NULL, &raumfeld_sharp_lcd_info);
	platform_device_register(&pxa3xx_device_gcu);
}

/**
 * SPI devices
 */

static struct spi_gpio_platform_data raumfeld_spi_platform_data = {
	.sck		= GPIO_SPI_CLK,
	.mosi		= GPIO_SPI_MOSI,
	.miso		= GPIO_SPI_MISO,
	.num_chipselect	= 3,
};

static struct platform_device raumfeld_spi_device = {
	.name	= "spi_gpio",
	.id	= 0,
	.dev 	= {
		.platform_data	= &raumfeld_spi_platform_data,
	}
};

static struct lis3lv02d_platform_data lis3_pdata = {
	.click_flags 	= LIS3_CLICK_SINGLE_X |
			  LIS3_CLICK_SINGLE_Y |
			  LIS3_CLICK_SINGLE_Z,
	.irq_cfg	= LIS3_IRQ1_CLICK | LIS3_IRQ2_CLICK,
	.wakeup_flags	= LIS3_WAKEUP_X_LO | LIS3_WAKEUP_X_HI |
			  LIS3_WAKEUP_Y_LO | LIS3_WAKEUP_Y_HI |
			  LIS3_WAKEUP_Z_LO | LIS3_WAKEUP_Z_HI,
	.wakeup_thresh	= 10,
	.click_thresh_x = 10,
	.click_thresh_y = 10,
	.click_thresh_z = 10,
};

#define SPI_AK4104	\
{			\
	.modalias	= "ak4104-codec",	\
	.max_speed_hz	= 10000,		\
	.bus_num	= 0,			\
	.chip_select	= 0,			\
	.controller_data = (void *) GPIO_SPDIF_CS,	\
}

#define SPI_LIS3	\
{			\
	.modalias	= "lis3lv02d_spi",	\
	.max_speed_hz	= 1000000,		\
	.bus_num	= 0,			\
	.chip_select	= 1,			\
	.controller_data = (void *) GPIO_ACCEL_CS,	\
	.platform_data	= &lis3_pdata,		\
	.irq		= PXA_GPIO_TO_IRQ(GPIO_ACCEL_IRQ),	\
}

#define SPI_DAC7512	\
{	\
	.modalias	= "dac7512",		\
	.max_speed_hz	= 1000000,		\
	.bus_num	= 0,			\
	.chip_select	= 2,			\
	.controller_data = (void *) GPIO_MCLK_DAC_CS,	\
}

static struct spi_board_info connector_spi_devices[] __initdata = {
	SPI_AK4104,
	SPI_DAC7512,
};

static struct spi_board_info speaker_spi_devices[] __initdata = {
	SPI_DAC7512,
};

static struct spi_board_info controller_spi_devices[] __initdata = {
	SPI_LIS3,
};

/**
 * MMC for Marvell Libertas 8688 via SDIO
 */

static int raumfeld_mci_init(struct device *dev, irq_handler_t isr, void *data)
{
	gpio_set_value(GPIO_W2W_RESET, 1);
	gpio_set_value(GPIO_W2W_PDN, 1);

	return 0;
}

static void raumfeld_mci_exit(struct device *dev, void *data)
{
	gpio_set_value(GPIO_W2W_RESET, 0);
	gpio_set_value(GPIO_W2W_PDN, 0);
}

static struct pxamci_platform_data raumfeld_mci_platform_data = {
	.init			= raumfeld_mci_init,
	.exit			= raumfeld_mci_exit,
	.detect_delay_ms	= 200,
	.gpio_card_detect	= -1,
	.gpio_card_ro		= -1,
	.gpio_power		= -1,
};

/*
 * External power / charge logic
 */

static int power_supply_init(struct device *dev)
{
	return 0;
}

static void power_supply_exit(struct device *dev)
{
}

static int raumfeld_is_ac_online(void)
{
	return !gpio_get_value(GPIO_CHARGE_DC_OK);
}

static int raumfeld_is_usb_online(void)
{
	return 0;
}

static char *raumfeld_power_supplicants[] = { "ds2760-battery.0" };

static void raumfeld_power_signal_charged(void)
{
	struct power_supply *psy =
		power_supply_get_by_name(raumfeld_power_supplicants[0]);

	if (psy) {
		power_supply_set_battery_charged(psy);
		power_supply_put(psy);
	}
}

static int raumfeld_power_resume(void)
{
	/* check if GPIO_CHARGE_DONE went low while we were sleeping */
	if (!gpio_get_value(GPIO_CHARGE_DONE))
		raumfeld_power_signal_charged();

	return 0;
}

static struct pda_power_pdata power_supply_info = {
	.init			= power_supply_init,
	.is_ac_online		= raumfeld_is_ac_online,
	.is_usb_online		= raumfeld_is_usb_online,
	.exit			= power_supply_exit,
	.supplied_to		= raumfeld_power_supplicants,
	.num_supplicants	= ARRAY_SIZE(raumfeld_power_supplicants),
	.resume			= raumfeld_power_resume,
};

static struct resource power_supply_resources[] = {
	{
		.name  = "ac",
		.flags = IORESOURCE_IRQ |
			 IORESOURCE_IRQ_HIGHEDGE | IORESOURCE_IRQ_LOWEDGE,
		.start = GPIO_CHARGE_DC_OK,
		.end   = GPIO_CHARGE_DC_OK,
	},
};

static irqreturn_t charge_done_irq(int irq, void *dev_id)
{
	raumfeld_power_signal_charged();
	return IRQ_HANDLED;
}

static struct platform_device raumfeld_power_supply = {
	.name = "pda-power",
	.id   = -1,
	.dev  = {
		.platform_data = &power_supply_info,
	},
	.resource      = power_supply_resources,
	.num_resources = ARRAY_SIZE(power_supply_resources),
};

static void __init raumfeld_power_init(void)
{
	int ret;

	/* Set PEN2 high to enable maximum charge current */
	ret = gpio_request(GPIO_CHRG_PEN2, "CHRG_PEN2");
	if (ret < 0)
		pr_warn("Unable to request GPIO_CHRG_PEN2\n");
	else
		gpio_direction_output(GPIO_CHRG_PEN2, 1);

	ret = gpio_request(GPIO_CHARGE_DC_OK, "CABLE_DC_OK");
	if (ret < 0)
		pr_warn("Unable to request GPIO_CHARGE_DC_OK\n");

	ret = gpio_request(GPIO_CHARGE_USB_SUSP, "CHARGE_USB_SUSP");
	if (ret < 0)
		pr_warn("Unable to request GPIO_CHARGE_USB_SUSP\n");
	else
		gpio_direction_output(GPIO_CHARGE_USB_SUSP, 0);

	power_supply_resources[0].start = gpio_to_irq(GPIO_CHARGE_DC_OK);
	power_supply_resources[0].end = gpio_to_irq(GPIO_CHARGE_DC_OK);

	ret = request_irq(gpio_to_irq(GPIO_CHARGE_DONE),
			&charge_done_irq, IORESOURCE_IRQ_LOWEDGE,
			"charge_done", NULL);

	if (ret < 0)
		printk(KERN_ERR "%s: unable to register irq %d\n", __func__,
			GPIO_CHARGE_DONE);
	else
		platform_device_register(&raumfeld_power_supply);
}

/* Fixed regulator for AUDIO_VA, 0-0048 maps to the cs4270 codec device */

static struct regulator_consumer_supply audio_va_consumer_supply =
	REGULATOR_SUPPLY("va", "0-0048");

static struct regulator_init_data audio_va_initdata = {
	.consumer_supplies = &audio_va_consumer_supply,
	.num_consumer_supplies = 1,
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

static struct fixed_voltage_config audio_va_config = {
	.supply_name		= "audio_va",
	.microvolts		= 5000000,
	.gpio			= GPIO_AUDIO_VA_ENABLE,
	.enable_high		= 1,
	.enabled_at_boot	= 0,
	.init_data		= &audio_va_initdata,
};

static struct platform_device audio_va_device = {
	.name	= "reg-fixed-voltage",
	.id	= 0,
	.dev	= {
		.platform_data = &audio_va_config,
	},
};

/* Dummy supplies for Codec's VD/VLC */

static struct regulator_consumer_supply audio_dummy_supplies[] = {
	REGULATOR_SUPPLY("vd", "0-0048"),
	REGULATOR_SUPPLY("vlc", "0-0048"),
};

static struct regulator_init_data audio_dummy_initdata = {
	.consumer_supplies = audio_dummy_supplies,
	.num_consumer_supplies = ARRAY_SIZE(audio_dummy_supplies),
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
};

static struct fixed_voltage_config audio_dummy_config = {
	.supply_name		= "audio_vd",
	.microvolts		= 3300000,
	.gpio			= -1,
	.init_data		= &audio_dummy_initdata,
};

static struct platform_device audio_supply_dummy_device = {
	.name	= "reg-fixed-voltage",
	.id	= 1,
	.dev	= {
		.platform_data = &audio_dummy_config,
	},
};

static struct platform_device *audio_regulator_devices[] = {
	&audio_va_device,
	&audio_supply_dummy_device,
};

/**
 * Regulator support via MAX8660
 */

static struct regulator_consumer_supply vcc_mmc_supply =
	REGULATOR_SUPPLY("vmmc", "pxa2xx-mci.0");

static struct regulator_init_data vcc_mmc_init_data = {
	.constraints = {
		.min_uV			= 3300000,
		.max_uV			= 3300000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL,
		.valid_ops_mask		= REGULATOR_CHANGE_STATUS |
					  REGULATOR_CHANGE_VOLTAGE |
					  REGULATOR_CHANGE_MODE,
	},
	.consumer_supplies = &vcc_mmc_supply,
	.num_consumer_supplies = 1,
};

static struct max8660_subdev_data max8660_v6_subdev_data = {
	.id		= MAX8660_V6,
	.name		= "vmmc",
	.platform_data	= &vcc_mmc_init_data,
};

static struct max8660_platform_data max8660_pdata = {
	.subdevs = &max8660_v6_subdev_data,
	.num_subdevs = 1,
};

/**
 * I2C devices
 */

static struct i2c_board_info raumfeld_pwri2c_board_info = {
	.type		= "max8660",
	.addr		= 0x34,
	.platform_data	= &max8660_pdata,
};

static struct i2c_board_info raumfeld_connector_i2c_board_info __initdata = {
	.type	= "cs4270",
	.addr	= 0x48,
};

static struct gpiod_lookup_table raumfeld_controller_gpios_table = {
	.dev_id = "0-000a",
	.table = {
		GPIO_LOOKUP("gpio-pxa",
			    GPIO_TOUCH_IRQ, "attn", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static const struct resource raumfeld_controller_resources[] __initconst = {
	{
		.start	= PXA_GPIO_TO_IRQ(GPIO_TOUCH_IRQ),
		.end	= PXA_GPIO_TO_IRQ(GPIO_TOUCH_IRQ),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_HIGH,
	},
};

static struct i2c_board_info raumfeld_controller_i2c_board_info __initdata = {
	.type	= "eeti_ts",
	.addr	= 0x0a,
	.resources = raumfeld_controller_resources,
	.num_resources = ARRAY_SIZE(raumfeld_controller_resources),
};

static struct platform_device *raumfeld_common_devices[] = {
	&raumfeld_gpio_keys_device,
	&raumfeld_led_device,
	&raumfeld_spi_device,
};

static void __init raumfeld_audio_init(void)
{
	int ret;

	ret = gpio_request(GPIO_CODEC_RESET, "cs4270 reset");
	if (ret < 0)
		pr_warn("unable to request GPIO_CODEC_RESET\n");
	else
		gpio_direction_output(GPIO_CODEC_RESET, 1);

	ret = gpio_request(GPIO_SPDIF_RESET, "ak4104 s/pdif reset");
	if (ret < 0)
		pr_warn("unable to request GPIO_SPDIF_RESET\n");
	else
		gpio_direction_output(GPIO_SPDIF_RESET, 1);

	ret = gpio_request(GPIO_MCLK_RESET, "MCLK reset");
	if (ret < 0)
		pr_warn("unable to request GPIO_MCLK_RESET\n");
	else
		gpio_direction_output(GPIO_MCLK_RESET, 1);

	platform_add_devices(ARRAY_AND_SIZE(audio_regulator_devices));
}

static void __init raumfeld_common_init(void)
{
	int ret;

	/* The on/off button polarity has changed after revision 1 */
	if ((system_rev & 0xff) > 1) {
		int i;

		for (i = 0; i < ARRAY_SIZE(gpio_keys_button); i++)
			if (!strcmp(gpio_keys_button[i].desc, "on_off button"))
				gpio_keys_button[i].active_low = 1;
	}

	enable_irq_wake(IRQ_WAKEUP0);

	pxa3xx_set_nand_info(&raumfeld_nand_info);
	pxa3xx_set_i2c_power_info(NULL);
	pxa_set_ohci_info(&raumfeld_ohci_info);
	pxa_set_mci_info(&raumfeld_mci_platform_data);
	pxa_set_i2c_info(NULL);
	pxa_set_ffuart_info(NULL);

	ret = gpio_request(GPIO_W2W_RESET, "Wi2Wi reset");
	if (ret < 0)
		pr_warn("Unable to request GPIO_W2W_RESET\n");
	else
		gpio_direction_output(GPIO_W2W_RESET, 0);

	ret = gpio_request(GPIO_W2W_PDN, "Wi2Wi powerup");
	if (ret < 0)
		pr_warn("Unable to request GPIO_W2W_PDN\n");
	else
		gpio_direction_output(GPIO_W2W_PDN, 0);

	/* this can be used to switch off the device */
	ret = gpio_request(GPIO_SHUTDOWN_SUPPLY, "supply shutdown");
	if (ret < 0)
		pr_warn("Unable to request GPIO_SHUTDOWN_SUPPLY\n");
	else
		gpio_direction_output(GPIO_SHUTDOWN_SUPPLY, 0);

	platform_add_devices(ARRAY_AND_SIZE(raumfeld_common_devices));
	i2c_register_board_info(1, &raumfeld_pwri2c_board_info, 1);
}

static void __init __maybe_unused raumfeld_controller_init(void)
{
	int ret;

	pxa3xx_mfp_config(ARRAY_AND_SIZE(raumfeld_controller_pin_config));

	gpiod_add_lookup_table(&raumfeld_rotary_gpios_table);
	device_add_properties(&rotary_encoder_device.dev,
			      raumfeld_rotary_properties);
	platform_device_register(&rotary_encoder_device);

	spi_register_board_info(ARRAY_AND_SIZE(controller_spi_devices));

	gpiod_add_lookup_table(&raumfeld_controller_gpios_table);
	i2c_register_board_info(0, &raumfeld_controller_i2c_board_info, 1);

	ret = gpio_request(GPIO_SHUTDOWN_BATT, "battery shutdown");
	if (ret < 0)
		pr_warn("Unable to request GPIO_SHUTDOWN_BATT\n");
	else
		gpio_direction_output(GPIO_SHUTDOWN_BATT, 0);

	raumfeld_common_init();
	raumfeld_power_init();
	raumfeld_lcd_init();
	raumfeld_w1_init();
}

static void __init __maybe_unused raumfeld_connector_init(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(raumfeld_connector_pin_config));
	spi_register_board_info(ARRAY_AND_SIZE(connector_spi_devices));
	i2c_register_board_info(0, &raumfeld_connector_i2c_board_info, 1);

	platform_device_register(&smc91x_device);

	raumfeld_audio_init();
	raumfeld_common_init();
}

static void __init __maybe_unused raumfeld_speaker_init(void)
{
	pxa3xx_mfp_config(ARRAY_AND_SIZE(raumfeld_speaker_pin_config));
	spi_register_board_info(ARRAY_AND_SIZE(speaker_spi_devices));
	i2c_register_board_info(0, &raumfeld_connector_i2c_board_info, 1);

	platform_device_register(&smc91x_device);

	gpiod_add_lookup_table(&raumfeld_rotary_gpios_table);
	device_add_properties(&rotary_encoder_device.dev,
			      raumfeld_rotary_properties);
	platform_device_register(&rotary_encoder_device);

	raumfeld_audio_init();
	raumfeld_common_init();
}

/* physical memory regions */
#define	RAUMFELD_SDRAM_BASE	0xa0000000	/* SDRAM region */

#ifdef CONFIG_MACH_RAUMFELD_RC
MACHINE_START(RAUMFELD_RC, "Raumfeld Controller")
	.atag_offset	= 0x100,
	.init_machine	= raumfeld_controller_init,
	.map_io		= pxa3xx_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa3xx_init_irq,
	.handle_irq	= pxa3xx_handle_irq,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_RAUMFELD_CONNECTOR
MACHINE_START(RAUMFELD_CONNECTOR, "Raumfeld Connector")
	.atag_offset	= 0x100,
	.init_machine	= raumfeld_connector_init,
	.map_io		= pxa3xx_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa3xx_init_irq,
	.handle_irq	= pxa3xx_handle_irq,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END
#endif

#ifdef CONFIG_MACH_RAUMFELD_SPEAKER
MACHINE_START(RAUMFELD_SPEAKER, "Raumfeld Speaker")
	.atag_offset	= 0x100,
	.init_machine	= raumfeld_speaker_init,
	.map_io		= pxa3xx_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa3xx_init_irq,
	.handle_irq	= pxa3xx_handle_irq,
	.init_time	= pxa_timer_init,
	.restart	= pxa_restart,
MACHINE_END
#endif
