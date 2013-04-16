/*
 * board-devkit8000.c - TimLL Devkit8000
 *
 * Copyright (C) 2009 Kim Botherway
 * Copyright (C) 2010 Thomas Weber
 *
 * Modified from mach-omap2/board-omap3beagle.c
 *
 * Initial code: Syed Mohammed Khasim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/mmc/host.h>
#include <linux/usb/phy.h>

#include <linux/regulator/machine.h>
#include <linux/i2c/twl.h>
#include "id.h"
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include "common.h"
#include "gpmc.h"
#include <linux/platform_data/mtd-nand-omap2.h>
#include <video/omapdss.h>
#include <video/omap-panel-data.h>

#include <linux/platform_data/spi-omap2-mcspi.h>
#include <linux/input/matrix_keypad.h>
#include <linux/spi/spi.h>
#include <linux/dm9000.h>
#include <linux/interrupt.h>

#include "sdram-micron-mt46h32m32lf-6.h"
#include "mux.h"
#include "hsmmc.h"
#include "board-flash.h"
#include "common-board-devices.h"

#define	NAND_CS			0

#define OMAP_DM9000_GPIO_IRQ	25
#define OMAP3_DEVKIT_TS_GPIO	27

static struct mtd_partition devkit8000_nand_partitions[] = {
	/* All the partition sizes are listed in terms of NAND block size */
	{
		.name		= "X-Loader",
		.offset		= 0,
		.size		= 4 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size		= 15 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot Env",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x260000 */
		.size		= 1 * NAND_BLOCK_SIZE,
	},
	{
		.name		= "Kernel",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x280000 */
		.size		= 32 * NAND_BLOCK_SIZE,
	},
	{
		.name		= "File System",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x680000 */
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
		.gpio_wp	= 29,
		.deferred	= true,
	},
	{}	/* Terminator */
};

static int devkit8000_panel_enable_lcd(struct omap_dss_device *dssdev)
{
	if (gpio_is_valid(dssdev->reset_gpio))
		gpio_set_value_cansleep(dssdev->reset_gpio, 1);
	return 0;
}

static void devkit8000_panel_disable_lcd(struct omap_dss_device *dssdev)
{
	if (gpio_is_valid(dssdev->reset_gpio))
		gpio_set_value_cansleep(dssdev->reset_gpio, 0);
}

static struct regulator_consumer_supply devkit8000_vmmc1_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.0"),
};

/* ads7846 on SPI */
static struct regulator_consumer_supply devkit8000_vio_supply[] = {
	REGULATOR_SUPPLY("vcc", "spi2.0"),
};

static struct panel_generic_dpi_data lcd_panel = {
	.name			= "innolux_at070tn83",
	.platform_enable        = devkit8000_panel_enable_lcd,
	.platform_disable       = devkit8000_panel_disable_lcd,
};

static struct omap_dss_device devkit8000_lcd_device = {
	.name                   = "lcd",
	.type                   = OMAP_DISPLAY_TYPE_DPI,
	.driver_name            = "generic_dpi_panel",
	.data			= &lcd_panel,
	.phy.dpi.data_lines     = 24,
};

static struct tfp410_platform_data dvi_panel = {
	.power_down_gpio	= -1,
	.i2c_bus_num		= 1,
};

static struct omap_dss_device devkit8000_dvi_device = {
	.name                   = "dvi",
	.type                   = OMAP_DISPLAY_TYPE_DPI,
	.driver_name            = "tfp410",
	.data			= &dvi_panel,
	.phy.dpi.data_lines     = 24,
};

static struct omap_dss_device devkit8000_tv_device = {
	.name                   = "tv",
	.driver_name            = "venc",
	.type                   = OMAP_DISPLAY_TYPE_VENC,
	.phy.venc.type          = OMAP_DSS_VENC_TYPE_SVIDEO,
};


static struct omap_dss_device *devkit8000_dss_devices[] = {
	&devkit8000_lcd_device,
	&devkit8000_dvi_device,
	&devkit8000_tv_device,
};

static struct omap_dss_board_info devkit8000_dss_data = {
	.num_devices = ARRAY_SIZE(devkit8000_dss_devices),
	.devices = devkit8000_dss_devices,
	.default_device = &devkit8000_lcd_device,
};

static uint32_t board_keymap[] = {
	KEY(0, 0, KEY_1),
	KEY(1, 0, KEY_2),
	KEY(2, 0, KEY_3),
	KEY(0, 1, KEY_4),
	KEY(1, 1, KEY_5),
	KEY(2, 1, KEY_6),
	KEY(3, 1, KEY_F5),
	KEY(0, 2, KEY_7),
	KEY(1, 2, KEY_8),
	KEY(2, 2, KEY_9),
	KEY(3, 2, KEY_F6),
	KEY(0, 3, KEY_F7),
	KEY(1, 3, KEY_0),
	KEY(2, 3, KEY_F8),
	PERSISTENT_KEY(4, 5),
	KEY(4, 4, KEY_VOLUMEUP),
	KEY(5, 5, KEY_VOLUMEDOWN),
	0
};

static struct matrix_keymap_data board_map_data = {
	.keymap			= board_keymap,
	.keymap_size		= ARRAY_SIZE(board_keymap),
};

static struct twl4030_keypad_data devkit8000_kp_data = {
	.keymap_data	= &board_map_data,
	.rows		= 6,
	.cols		= 6,
	.rep		= 1,
};

static struct gpio_led gpio_leds[];

static int devkit8000_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	int ret;

	/* gpio + 0 is "mmc0_cd" (input/IRQ) */
	mmc[0].gpio_cd = gpio + 0;
	omap_hsmmc_late_init(mmc);

	/* TWL4030_GPIO_MAX + 1 == ledB, PMU_STAT (out, active low LED) */
	gpio_leds[2].gpio = gpio + TWL4030_GPIO_MAX + 1;

	/* TWL4030_GPIO_MAX + 0 is "LCD_PWREN" (out, active high) */
	devkit8000_lcd_device.reset_gpio = gpio + TWL4030_GPIO_MAX + 0;
	ret = gpio_request_one(devkit8000_lcd_device.reset_gpio,
			       GPIOF_OUT_INIT_LOW, "LCD_PWREN");
	if (ret < 0) {
		devkit8000_lcd_device.reset_gpio = -EINVAL;
		printk(KERN_ERR "Failed to request GPIO for LCD_PWRN\n");
	}

	/* gpio + 7 is "DVI_PD" (out, active low) */
	dvi_panel.power_down_gpio = gpio + 7;

	return 0;
}

static struct twl4030_gpio_platform_data devkit8000_gpio_data = {
	.use_leds	= true,
	.pulldowns	= BIT(1) | BIT(2) | BIT(6) | BIT(8) | BIT(13)
				| BIT(15) | BIT(16) | BIT(17),
	.setup		= devkit8000_twl_gpio_setup,
};

static struct regulator_consumer_supply devkit8000_vpll1_supplies[] = {
	REGULATOR_SUPPLY("vdds_dsi", "omapdss"),
	REGULATOR_SUPPLY("vdds_dsi", "omapdss_dsi.0"),
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data devkit8000_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(devkit8000_vmmc1_supply),
	.consumer_supplies	= devkit8000_vmmc1_supply,
};

/* VPLL1 for digital video outputs */
static struct regulator_init_data devkit8000_vpll1 = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(devkit8000_vpll1_supplies),
	.consumer_supplies	= devkit8000_vpll1_supplies,
};

/* VAUX4 for ads7846 and nubs */
static struct regulator_init_data devkit8000_vio = {
	.constraints = {
		.min_uV                 = 1800000,
		.max_uV                 = 1800000,
		.apply_uV               = true,
		.valid_modes_mask       = REGULATOR_MODE_NORMAL
			| REGULATOR_MODE_STANDBY,
		.valid_ops_mask         = REGULATOR_CHANGE_MODE
			| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(devkit8000_vio_supply),
	.consumer_supplies      = devkit8000_vio_supply,
};

static struct twl4030_platform_data devkit8000_twldata = {
	/* platform_data for children goes here */
	.gpio		= &devkit8000_gpio_data,
	.vmmc1		= &devkit8000_vmmc1,
	.vpll1		= &devkit8000_vpll1,
	.vio		= &devkit8000_vio,
	.keypad		= &devkit8000_kp_data,
};

static int __init devkit8000_i2c_init(void)
{
	omap3_pmic_get_config(&devkit8000_twldata,
			  TWL_COMMON_PDATA_USB | TWL_COMMON_PDATA_AUDIO,
			  TWL_COMMON_REGULATOR_VDAC);
	omap3_pmic_init("tps65930", &devkit8000_twldata);
	/* Bus 3 is attached to the DVI port where devices like the pico DLP
	 * projector don't work reliably with 400kHz */
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static struct gpio_led gpio_leds[] = {
	{
		.name			= "led1",
		.default_trigger	= "heartbeat",
		.gpio			= 186,
		.active_low		= true,
	},
	{
		.name			= "led2",
		.default_trigger	= "mmc0",
		.gpio			= 163,
		.active_low		= true,
	},
	{
		.name			= "ledB",
		.default_trigger	= "none",
		.gpio			= 153,
		.active_low             = true,
	},
	{
		.name			= "led3",
		.default_trigger	= "none",
		.gpio			= 164,
		.active_low             = true,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_led_info,
	},
};

static struct gpio_keys_button gpio_buttons[] = {
	{
		.code			= BTN_EXTRA,
		.gpio			= 26,
		.desc			= "user",
		.wakeup			= 1,
	},
};

static struct gpio_keys_platform_data gpio_key_info = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

static struct platform_device keys_gpio = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_key_info,
	},
};

#define OMAP_DM9000_BASE	0x2c000000

static struct resource omap_dm9000_resources[] = {
	[0] = {
		.start		= OMAP_DM9000_BASE,
		.end		= (OMAP_DM9000_BASE + 0x4 - 1),
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= (OMAP_DM9000_BASE + 0x400),
		.end		= (OMAP_DM9000_BASE + 0x400 + 0x4 - 1),
		.flags		= IORESOURCE_MEM,
	},
	[2] = {
		.flags		= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct dm9000_plat_data omap_dm9000_platdata = {
	.flags = DM9000_PLATF_16BITONLY,
};

static struct platform_device omap_dm9000_dev = {
	.name = "dm9000",
	.id = -1,
	.num_resources	= ARRAY_SIZE(omap_dm9000_resources),
	.resource	= omap_dm9000_resources,
	.dev = {
		.platform_data = &omap_dm9000_platdata,
	},
};

static void __init omap_dm9000_init(void)
{
	unsigned char *eth_addr = omap_dm9000_platdata.dev_addr;
	struct omap_die_id odi;
	int ret;

	ret = gpio_request_one(OMAP_DM9000_GPIO_IRQ, GPIOF_IN, "dm9000 irq");
	if (ret < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for dm9000 IRQ\n",
			OMAP_DM9000_GPIO_IRQ);
		return;
	}

	/* init the mac address using DIE id */
	omap_get_die_id(&odi);

	eth_addr[0] = 0x02; /* locally administered */
	eth_addr[1] = odi.id_1 & 0xff;
	eth_addr[2] = (odi.id_0 & 0xff000000) >> 24;
	eth_addr[3] = (odi.id_0 & 0x00ff0000) >> 16;
	eth_addr[4] = (odi.id_0 & 0x0000ff00) >> 8;
	eth_addr[5] = (odi.id_0 & 0x000000ff);
}

static struct platform_device *devkit8000_devices[] __initdata = {
	&leds_gpio,
	&keys_gpio,
	&omap_dm9000_dev,
};

static struct usbhs_omap_platform_data usbhs_bdata __initdata = {

	.port_mode[0] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[1] = OMAP_USBHS_PORT_MODE_UNUSED,
	.port_mode[2] = OMAP_USBHS_PORT_MODE_UNUSED,

	.phy_reset  = true,
	.reset_gpio_port[0]  = -EINVAL,
	.reset_gpio_port[1]  = -EINVAL,
	.reset_gpio_port[2]  = -EINVAL
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	/* nCS and IRQ for Devkit8000 ethernet */
	OMAP3_MUX(GPMC_NCS6, OMAP_MUX_MODE0),
	OMAP3_MUX(ETK_D11, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP),

	/* McSPI 2*/
	OMAP3_MUX(MCSPI2_CLK, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCSPI2_SIMO, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(MCSPI2_SOMI, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCSPI2_CS0, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(MCSPI2_CS1, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),

	/* PENDOWN GPIO */
	OMAP3_MUX(ETK_D13, OMAP_MUX_MODE4 | OMAP_PIN_INPUT),

	/* mUSB */
	OMAP3_MUX(HSUSB0_CLK, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(HSUSB0_STP, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(HSUSB0_DIR, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(HSUSB0_NXT, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(HSUSB0_DATA0, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(HSUSB0_DATA1, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(HSUSB0_DATA2, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(HSUSB0_DATA3, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(HSUSB0_DATA4, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(HSUSB0_DATA5, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(HSUSB0_DATA6, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(HSUSB0_DATA7, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),

	/* USB 1 */
	OMAP3_MUX(ETK_CTL, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),
	OMAP3_MUX(ETK_CLK, OMAP_MUX_MODE3 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(ETK_D8, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),
	OMAP3_MUX(ETK_D9, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),
	OMAP3_MUX(ETK_D0, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),
	OMAP3_MUX(ETK_D1, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),
	OMAP3_MUX(ETK_D2, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),
	OMAP3_MUX(ETK_D3, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),
	OMAP3_MUX(ETK_D4, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),
	OMAP3_MUX(ETK_D5, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),
	OMAP3_MUX(ETK_D6, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),
	OMAP3_MUX(ETK_D7, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),

	/* MMC 1 */
	OMAP3_MUX(SDMMC1_CLK, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(SDMMC1_CMD, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(SDMMC1_DAT0, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(SDMMC1_DAT1, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(SDMMC1_DAT2, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(SDMMC1_DAT3, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(SDMMC1_DAT4, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(SDMMC1_DAT5, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(SDMMC1_DAT6, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(SDMMC1_DAT7, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),

	/* McBSP 2 */
	OMAP3_MUX(MCBSP2_FSX, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP2_CLKX, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP2_DR, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP2_DX, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),

	/* I2C 1 */
	OMAP3_MUX(I2C1_SCL, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(I2C1_SDA, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),

	/* I2C 2 */
	OMAP3_MUX(I2C2_SCL, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(I2C2_SDA, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),

	/* I2C 3 */
	OMAP3_MUX(I2C3_SCL, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(I2C3_SDA, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),

	/* I2C 4 */
	OMAP3_MUX(I2C4_SCL, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(I2C4_SDA, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),

	/* serial ports */
	OMAP3_MUX(MCBSP3_CLKX, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(MCBSP3_FSX, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),
	OMAP3_MUX(UART1_TX, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(UART1_RX, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),

	/* DSS */
	OMAP3_MUX(DSS_PCLK, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_HSYNC, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_VSYNC, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_ACBIAS, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA0, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA1, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA2, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA3, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA4, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA5, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA6, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA7, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA8, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA9, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA10, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA11, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA12, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA13, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA14, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA15, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA16, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA17, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA18, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA19, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA20, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA21, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA22, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(DSS_DATA23, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),

	/* expansion port */
	/* McSPI 1 */
	OMAP3_MUX(MCSPI1_CLK, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCSPI1_SIMO, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCSPI1_SOMI, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCSPI1_CS0, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLDOWN),
	OMAP3_MUX(MCSPI1_CS3, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLDOWN),

	/* HDQ */
	OMAP3_MUX(HDQ_SIO, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),

	/* McSPI4 */
	OMAP3_MUX(MCBSP1_CLKR, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP1_DX, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP1_DR, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP1_FSX, OMAP_MUX_MODE1 | OMAP_PIN_INPUT_PULLUP),

	/* MMC 2 */
	OMAP3_MUX(SDMMC2_DAT4, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(SDMMC2_DAT5, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(SDMMC2_DAT6, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(SDMMC2_DAT7, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),

	/* I2C3 */
	OMAP3_MUX(I2C3_SCL, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(I2C3_SDA, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),

	OMAP3_MUX(MCBSP1_CLKX, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(MCBSP_CLKS, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(MCBSP1_FSR, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),

	OMAP3_MUX(GPMC_NCS7, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(GPMC_NCS3, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),

	/* TPS IRQ */
	OMAP3_MUX(SYS_NIRQ, OMAP_MUX_MODE0 | OMAP_WAKEUP_EN | \
			OMAP_PIN_INPUT_PULLUP),

	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static void __init devkit8000_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CUS);
	omap_serial_init();
	omap_sdrc_init(mt46h32m32lf6_sdrc_params,
				  mt46h32m32lf6_sdrc_params);

	omap_dm9000_init();

	omap_hsmmc_init(mmc);
	devkit8000_i2c_init();
	omap_dm9000_resources[2].start = gpio_to_irq(OMAP_DM9000_GPIO_IRQ);
	platform_add_devices(devkit8000_devices,
			ARRAY_SIZE(devkit8000_devices));

	omap_display_init(&devkit8000_dss_data);

	omap_ads7846_init(2, OMAP3_DEVKIT_TS_GPIO, 0, NULL);

	usb_bind_phy("musb-hdrc.0.auto", 0, "twl4030_usb");
	usb_musb_init(NULL);
	usbhs_init(&usbhs_bdata);
	board_nand_init(devkit8000_nand_partitions,
			ARRAY_SIZE(devkit8000_nand_partitions), NAND_CS,
			NAND_BUSWIDTH_16, NULL);
	omap_twl4030_audio_init("omap3beagle", NULL);

	/* Ensure SDRC pins are mux'd for self-refresh */
	omap_mux_init_signal("sdrc_cke0", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("sdrc_cke1", OMAP_PIN_OUTPUT);
}

MACHINE_START(DEVKIT8000, "OMAP3 Devkit8000")
	.atag_offset	= 0x100,
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap35xx_init_early,
	.init_irq	= omap3_init_irq,
	.handle_irq	= omap3_intc_handle_irq,
	.init_machine	= devkit8000_init,
	.init_late	= omap35xx_init_late,
	.init_time	= omap3_secure_sync32k_timer_init,
	.restart	= omap3xxx_restart,
MACHINE_END
