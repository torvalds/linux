/*
 * board-cm-t35.c (CompuLab CM-T35 module)
 *
 * Copyright (C) 2009 CompuLab, Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <linux/i2c/at24.h>
#include <linux/i2c/twl.h>
#include <linux/regulator/machine.h>

#include <linux/spi/spi.h>
#include <linux/spi/tdo24m.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/nand.h>
#include <plat/gpmc.h>
#include <plat/usb.h>
#include <plat/display.h>

#include <mach/hardware.h>

#include "mux.h"
#include "sdram-micron-mt46h32m32lf-6.h"
#include "hsmmc.h"

#define CM_T35_GPIO_PENDOWN	57

#define CM_T35_SMSC911X_CS	5
#define CM_T35_SMSC911X_GPIO	163
#define SB_T35_SMSC911X_CS	4
#define SB_T35_SMSC911X_GPIO	65

#define NAND_BLOCK_SIZE		SZ_128K
#define GPMC_CS0_BASE		0x60
#define GPMC_CS0_BASE_ADDR	(OMAP34XX_GPMC_VIRT + GPMC_CS0_BASE)

#if defined(CONFIG_SMSC911X) || defined(CONFIG_SMSC911X_MODULE)
#include <linux/smsc911x.h>

static struct smsc911x_platform_config cm_t35_smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct resource cm_t35_smsc911x_resources[] = {
	{
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= OMAP_GPIO_IRQ(CM_T35_SMSC911X_GPIO),
		.end	= OMAP_GPIO_IRQ(CM_T35_SMSC911X_GPIO),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct platform_device cm_t35_smsc911x_device = {
	.name		= "smsc911x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(cm_t35_smsc911x_resources),
	.resource	= cm_t35_smsc911x_resources,
	.dev		= {
		.platform_data = &cm_t35_smsc911x_config,
	},
};

static struct resource sb_t35_smsc911x_resources[] = {
	{
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= OMAP_GPIO_IRQ(SB_T35_SMSC911X_GPIO),
		.end	= OMAP_GPIO_IRQ(SB_T35_SMSC911X_GPIO),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct platform_device sb_t35_smsc911x_device = {
	.name		= "smsc911x",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(sb_t35_smsc911x_resources),
	.resource	= sb_t35_smsc911x_resources,
	.dev		= {
		.platform_data = &cm_t35_smsc911x_config,
	},
};

static void __init cm_t35_init_smsc911x(struct platform_device *dev,
					int cs, int irq_gpio)
{
	unsigned long cs_mem_base;

	if (gpmc_cs_request(cs, SZ_16M, &cs_mem_base) < 0) {
		pr_err("CM-T35: Failed request for GPMC mem for smsc911x\n");
		return;
	}

	dev->resource[0].start = cs_mem_base + 0x0;
	dev->resource[0].end   = cs_mem_base + 0xff;

	if ((gpio_request(irq_gpio, "ETH IRQ") == 0) &&
	    (gpio_direction_input(irq_gpio) == 0)) {
		gpio_export(irq_gpio, 0);
	} else {
		pr_err("CM-T35: could not obtain gpio for SMSC911X IRQ\n");
		return;
	}

	platform_device_register(dev);
}

static void __init cm_t35_init_ethernet(void)
{
	cm_t35_init_smsc911x(&cm_t35_smsc911x_device,
			     CM_T35_SMSC911X_CS, CM_T35_SMSC911X_GPIO);
	cm_t35_init_smsc911x(&sb_t35_smsc911x_device,
			     SB_T35_SMSC911X_CS, SB_T35_SMSC911X_GPIO);
}
#else
static inline void __init cm_t35_init_ethernet(void) { return; }
#endif

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
#include <linux/leds.h>

static struct gpio_led cm_t35_leds[] = {
	[0] = {
		.gpio			= 186,
		.name			= "cm-t35:green",
		.default_trigger	= "heartbeat",
		.active_low		= 0,
	},
};

static struct gpio_led_platform_data cm_t35_led_pdata = {
	.num_leds	= ARRAY_SIZE(cm_t35_leds),
	.leds		= cm_t35_leds,
};

static struct platform_device cm_t35_led_device = {
	.name		= "leds-gpio",
	.id		= -1,
	.dev		= {
		.platform_data	= &cm_t35_led_pdata,
	},
};

static void __init cm_t35_init_led(void)
{
	platform_device_register(&cm_t35_led_device);
}
#else
static inline void cm_t35_init_led(void) {}
#endif

#if defined(CONFIG_MTD_NAND_OMAP2) || defined(CONFIG_MTD_NAND_OMAP2_MODULE)
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

static struct mtd_partition cm_t35_nand_partitions[] = {
	{
		.name           = "xloader",
		.offset         = 0,			/* Offset = 0x00000 */
		.size           = 4 * NAND_BLOCK_SIZE,
		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name           = "uboot",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size           = 15 * NAND_BLOCK_SIZE,
	},
	{
		.name           = "uboot environment",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x260000 */
		.size           = 2 * NAND_BLOCK_SIZE,
	},
	{
		.name           = "linux",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x280000 */
		.size           = 32 * NAND_BLOCK_SIZE,
	},
	{
		.name           = "rootfs",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x680000 */
		.size           = MTDPART_SIZ_FULL,
	},
};

static struct omap_nand_platform_data cm_t35_nand_data = {
	.parts			= cm_t35_nand_partitions,
	.nr_parts		= ARRAY_SIZE(cm_t35_nand_partitions),
	.dma_channel		= -1,	/* disable DMA in OMAP NAND driver */
	.cs			= 0,
	.gpmc_cs_baseaddr	= (void __iomem *)GPMC_CS0_BASE_ADDR,
	.gpmc_baseaddr		= (void __iomem *)OMAP34XX_GPMC_VIRT,

};

static struct resource cm_t35_nand_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device cm_t35_nand_device = {
	.name		= "omap2-nand",
	.id		= -1,
	.num_resources	= 1,
	.resource	= &cm_t35_nand_resource,
	.dev		= {
		.platform_data	= &cm_t35_nand_data,
	},
};

static void __init cm_t35_init_nand(void)
{
	if (platform_device_register(&cm_t35_nand_device) < 0)
		pr_err("CM-T35: Unable to register NAND device\n");
}
#else
static inline void cm_t35_init_nand(void) {}
#endif

#if defined(CONFIG_TOUCHSCREEN_ADS7846) || \
	defined(CONFIG_TOUCHSCREEN_ADS7846_MODULE)
#include <linux/spi/ads7846.h>

#include <plat/mcspi.h>

static struct omap2_mcspi_device_config ads7846_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,	/* 0: slave, 1: master */
};

static int ads7846_get_pendown_state(void)
{
	return !gpio_get_value(CM_T35_GPIO_PENDOWN);
}

static struct ads7846_platform_data ads7846_config = {
	.x_max			= 0x0fff,
	.y_max			= 0x0fff,
	.x_plate_ohms		= 180,
	.pressure_max		= 255,
	.debounce_max		= 10,
	.debounce_tol		= 3,
	.debounce_rep		= 1,
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
};

static struct spi_board_info cm_t35_spi_board_info[] __initdata = {
	{
		.modalias		= "ads7846",
		.bus_num		= 1,
		.chip_select		= 0,
		.max_speed_hz		= 1500000,
		.controller_data	= &ads7846_mcspi_config,
		.irq			= OMAP_GPIO_IRQ(CM_T35_GPIO_PENDOWN),
		.platform_data		= &ads7846_config,
	},
};

static void __init cm_t35_init_ads7846(void)
{
	if ((gpio_request(CM_T35_GPIO_PENDOWN, "ADS7846_PENDOWN") == 0) &&
	    (gpio_direction_input(CM_T35_GPIO_PENDOWN) == 0)) {
		gpio_export(CM_T35_GPIO_PENDOWN, 0);
	} else {
		pr_err("CM-T35: could not obtain gpio for ADS7846_PENDOWN\n");
		return;
	}

	spi_register_board_info(cm_t35_spi_board_info,
				ARRAY_SIZE(cm_t35_spi_board_info));
}
#else
static inline void cm_t35_init_ads7846(void) {}
#endif

#define CM_T35_LCD_EN_GPIO 157
#define CM_T35_LCD_BL_GPIO 58
#define CM_T35_DVI_EN_GPIO 54

static int lcd_bl_gpio;
static int lcd_en_gpio;
static int dvi_en_gpio;

static int lcd_enabled;
static int dvi_enabled;

static int cm_t35_panel_enable_lcd(struct omap_dss_device *dssdev)
{
	if (dvi_enabled) {
		printk(KERN_ERR "cannot enable LCD, DVI is enabled\n");
		return -EINVAL;
	}

	gpio_set_value(lcd_en_gpio, 1);
	gpio_set_value(lcd_bl_gpio, 1);

	lcd_enabled = 1;

	return 0;
}

static void cm_t35_panel_disable_lcd(struct omap_dss_device *dssdev)
{
	lcd_enabled = 0;

	gpio_set_value(lcd_bl_gpio, 0);
	gpio_set_value(lcd_en_gpio, 0);
}

static int cm_t35_panel_enable_dvi(struct omap_dss_device *dssdev)
{
	if (lcd_enabled) {
		printk(KERN_ERR "cannot enable DVI, LCD is enabled\n");
		return -EINVAL;
	}

	gpio_set_value(dvi_en_gpio, 0);
	dvi_enabled = 1;

	return 0;
}

static void cm_t35_panel_disable_dvi(struct omap_dss_device *dssdev)
{
	gpio_set_value(dvi_en_gpio, 1);
	dvi_enabled = 0;
}

static int cm_t35_panel_enable_tv(struct omap_dss_device *dssdev)
{
	return 0;
}

static void cm_t35_panel_disable_tv(struct omap_dss_device *dssdev)
{
}

static struct omap_dss_device cm_t35_lcd_device = {
	.name			= "lcd",
	.driver_name		= "toppoly_tdo35s_panel",
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.phy.dpi.data_lines	= 18,
	.platform_enable	= cm_t35_panel_enable_lcd,
	.platform_disable	= cm_t35_panel_disable_lcd,
};

static struct omap_dss_device cm_t35_dvi_device = {
	.name			= "dvi",
	.driver_name		= "generic_panel",
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.phy.dpi.data_lines	= 24,
	.platform_enable	= cm_t35_panel_enable_dvi,
	.platform_disable	= cm_t35_panel_disable_dvi,
};

static struct omap_dss_device cm_t35_tv_device = {
	.name			= "tv",
	.driver_name		= "venc",
	.type			= OMAP_DISPLAY_TYPE_VENC,
	.phy.venc.type		= OMAP_DSS_VENC_TYPE_SVIDEO,
	.platform_enable	= cm_t35_panel_enable_tv,
	.platform_disable	= cm_t35_panel_disable_tv,
};

static struct omap_dss_device *cm_t35_dss_devices[] = {
	&cm_t35_lcd_device,
	&cm_t35_dvi_device,
	&cm_t35_tv_device,
};

static struct omap_dss_board_info cm_t35_dss_data = {
	.num_devices	= ARRAY_SIZE(cm_t35_dss_devices),
	.devices	= cm_t35_dss_devices,
	.default_device	= &cm_t35_dvi_device,
};

static struct platform_device cm_t35_dss_device = {
	.name		= "omapdss",
	.id		= -1,
	.dev		= {
		.platform_data = &cm_t35_dss_data,
	},
};

static struct omap2_mcspi_device_config tdo24m_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,	/* 0: slave, 1: master */
};

static struct tdo24m_platform_data tdo24m_config = {
	.model = TDO35S,
};

static struct spi_board_info cm_t35_lcd_spi_board_info[] __initdata = {
	{
		.modalias		= "tdo24m",
		.bus_num		= 4,
		.chip_select		= 0,
		.max_speed_hz		= 1000000,
		.controller_data	= &tdo24m_mcspi_config,
		.platform_data		= &tdo24m_config,
	},
};

static void __init cm_t35_init_display(void)
{
	int err;

	lcd_en_gpio = CM_T35_LCD_EN_GPIO;
	lcd_bl_gpio = CM_T35_LCD_BL_GPIO;
	dvi_en_gpio = CM_T35_DVI_EN_GPIO;

	spi_register_board_info(cm_t35_lcd_spi_board_info,
				ARRAY_SIZE(cm_t35_lcd_spi_board_info));

	err = gpio_request(lcd_en_gpio, "LCD RST");
	if (err) {
		pr_err("CM-T35: failed to get LCD reset GPIO\n");
		goto out;
	}

	err = gpio_request(lcd_bl_gpio, "LCD BL");
	if (err) {
		pr_err("CM-T35: failed to get LCD backlight control GPIO\n");
		goto err_lcd_bl;
	}

	err = gpio_request(dvi_en_gpio, "DVI EN");
	if (err) {
		pr_err("CM-T35: failed to get DVI reset GPIO\n");
		goto err_dvi_en;
	}

	gpio_export(lcd_en_gpio, 0);
	gpio_export(lcd_bl_gpio, 0);
	gpio_export(dvi_en_gpio, 0);
	gpio_direction_output(lcd_en_gpio, 0);
	gpio_direction_output(lcd_bl_gpio, 0);
	gpio_direction_output(dvi_en_gpio, 1);

	msleep(50);
	gpio_set_value(lcd_en_gpio, 1);

	err = platform_device_register(&cm_t35_dss_device);
	if (err) {
		pr_err("CM-T35: failed to register DSS device\n");
		goto err_dev_reg;
	}

	return;

err_dev_reg:
	gpio_free(dvi_en_gpio);
err_dvi_en:
	gpio_free(lcd_bl_gpio);
err_lcd_bl:
	gpio_free(lcd_en_gpio);
out:

	return;
}

static struct regulator_consumer_supply cm_t35_vmmc1_supply = {
	.supply			= "vmmc",
};

static struct regulator_consumer_supply cm_t35_vsim_supply = {
	.supply			= "vmmc_aux",
};

static struct regulator_consumer_supply cm_t35_vdac_supply = {
	.supply		= "vdda_dac",
	.dev		= &cm_t35_dss_device.dev,
};

static struct regulator_consumer_supply cm_t35_vdvi_supply = {
	.supply		= "vdvi",
	.dev		= &cm_t35_dss_device.dev,
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data cm_t35_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &cm_t35_vmmc1_supply,
};

/* VSIM for MMC1 pins DAT4..DAT7 (2 mA, plus card == max 50 mA) */
static struct regulator_init_data cm_t35_vsim = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 3000000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &cm_t35_vsim_supply,
};

/* VDAC for DSS driving S-Video (8 mA unloaded, max 65 mA) */
static struct regulator_init_data cm_t35_vdac = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &cm_t35_vdac_supply,
};

/* VPLL2 for digital video outputs */
static struct regulator_init_data cm_t35_vpll2 = {
	.constraints = {
		.name			= "VDVI",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &cm_t35_vdvi_supply,
};

static struct twl4030_usb_data cm_t35_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static int cm_t35_keymap[] = {
	KEY(0, 0, KEY_A),	KEY(0, 1, KEY_B),	KEY(0, 2, KEY_LEFT),
	KEY(1, 0, KEY_UP),	KEY(1, 1, KEY_ENTER),	KEY(1, 2, KEY_DOWN),
	KEY(2, 0, KEY_RIGHT),	KEY(2, 1, KEY_C),	KEY(2, 2, KEY_D),
};

static struct matrix_keymap_data cm_t35_keymap_data = {
	.keymap			= cm_t35_keymap,
	.keymap_size		= ARRAY_SIZE(cm_t35_keymap),
};

static struct twl4030_keypad_data cm_t35_kp_data = {
	.keymap_data	= &cm_t35_keymap_data,
	.rows		= 3,
	.cols		= 3,
	.rep		= 1,
};

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,

	},
	{
		.mmc		= 2,
		.wires		= 4,
		.transceiver	= 1,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
		.ocr_mask	= 0x00100000,	/* 3.3V */
	},
	{}	/* Terminator */
};

static struct ehci_hcd_omap_platform_data ehci_pdata = {
	.port_mode[0] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[1] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[2] = EHCI_HCD_OMAP_MODE_UNKNOWN,

	.phy_reset  = true,
	.reset_gpio_port[0]  = -EINVAL,
	.reset_gpio_port[1]  = -EINVAL,
	.reset_gpio_port[2]  = -EINVAL
};

static int cm_t35_twl_gpio_setup(struct device *dev, unsigned gpio,
				 unsigned ngpio)
{
	int wlan_rst = gpio + 2;

	if ((gpio_request(wlan_rst, "WLAN RST") == 0) &&
	    (gpio_direction_output(wlan_rst, 1) == 0)) {
		gpio_export(wlan_rst, 0);

		udelay(10);
		gpio_set_value(wlan_rst, 0);
		udelay(10);
		gpio_set_value(wlan_rst, 1);
	} else {
		pr_err("CM-T35: could not obtain gpio for WiFi reset\n");
	}

	/* gpio + 0 is "mmc0_cd" (input/IRQ) */
	mmc[0].gpio_cd = gpio + 0;
	omap2_hsmmc_init(mmc);

	/* link regulators to MMC adapters */
	cm_t35_vmmc1_supply.dev = mmc[0].dev;
	cm_t35_vsim_supply.dev = mmc[0].dev;

	/* setup USB with proper PHY reset GPIOs */
	ehci_pdata.reset_gpio_port[0] = gpio + 6;
	ehci_pdata.reset_gpio_port[1] = gpio + 7;

	usb_ehci_init(&ehci_pdata);

	return 0;
}

static struct twl4030_gpio_platform_data cm_t35_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.setup          = cm_t35_twl_gpio_setup,
};

static struct twl4030_platform_data cm_t35_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.keypad		= &cm_t35_kp_data,
	.usb		= &cm_t35_usb_data,
	.gpio		= &cm_t35_gpio_data,
	.vmmc1		= &cm_t35_vmmc1,
	.vsim		= &cm_t35_vsim,
	.vdac		= &cm_t35_vdac,
	.vpll2		= &cm_t35_vpll2,
};

static struct i2c_board_info __initdata cm_t35_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("tps65930", 0x48),
		.flags		= I2C_CLIENT_WAKE,
		.irq		= INT_34XX_SYS_NIRQ,
		.platform_data	= &cm_t35_twldata,
	},
};

static void __init cm_t35_init_i2c(void)
{
	omap_register_i2c_bus(1, 2600, cm_t35_i2c_boardinfo,
			      ARRAY_SIZE(cm_t35_i2c_boardinfo));
}

static struct omap_board_config_kernel cm_t35_config[] __initdata = {
};

static void __init cm_t35_init_irq(void)
{
	omap_board_config = cm_t35_config;
	omap_board_config_size = ARRAY_SIZE(cm_t35_config);

	omap2_init_common_hw(mt46h32m32lf6_sdrc_params,
			     mt46h32m32lf6_sdrc_params);
	omap_init_irq();
	omap_gpio_init();
}

static void __init cm_t35_map_io(void)
{
	omap2_set_globals_343x();
	omap34xx_map_common_io();
}

static struct omap_board_mux board_mux[] __initdata = {
	/* nCS and IRQ for CM-T35 ethernet */
	OMAP3_MUX(GPMC_NCS5, OMAP_MUX_MODE0),
	OMAP3_MUX(UART3_CTS_RCTX, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP),

	/* nCS and IRQ for SB-T35 ethernet */
	OMAP3_MUX(GPMC_NCS4, OMAP_MUX_MODE0),
	OMAP3_MUX(GPMC_WAIT3, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP),

	/* PENDOWN GPIO */
	OMAP3_MUX(GPMC_NCS6, OMAP_MUX_MODE4 | OMAP_PIN_INPUT),

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

	/* MMC 2 */
	OMAP3_MUX(SDMMC2_DAT4, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(SDMMC2_DAT5, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(SDMMC2_DAT6, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(SDMMC2_DAT7, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),

	/* McSPI 1 */
	OMAP3_MUX(MCSPI1_CLK, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCSPI1_SIMO, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCSPI1_SOMI, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCSPI1_CS0, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLDOWN),

	/* McSPI 4 */
	OMAP3_MUX(MCBSP1_CLKR, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP1_DX, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP1_DR, OMAP_MUX_MODE1 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP1_FSX, OMAP_MUX_MODE1 | OMAP_PIN_INPUT_PULLUP),

	/* McBSP 2 */
	OMAP3_MUX(MCBSP2_FSX, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP2_CLKX, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP2_DR, OMAP_MUX_MODE0 | OMAP_PIN_INPUT),
	OMAP3_MUX(MCBSP2_DX, OMAP_MUX_MODE0 | OMAP_PIN_OUTPUT),

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

	/* display controls */
	OMAP3_MUX(MCBSP1_FSR, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(GPMC_NCS7, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	OMAP3_MUX(GPMC_NCS3, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),

	/* TPS IRQ */
	OMAP3_MUX(SYS_NIRQ, OMAP_MUX_MODE0 | OMAP_WAKEUP_EN | \
		  OMAP_PIN_INPUT_PULLUP),

	{ .reg_offset = OMAP_MUX_TERMINATOR },
};

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_OTG,
	.power			= 100,
};

static void __init cm_t35_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CUS);
	omap_serial_init();
	cm_t35_init_i2c();
	cm_t35_init_nand();
	cm_t35_init_ads7846();
	cm_t35_init_ethernet();
	cm_t35_init_led();
	cm_t35_init_display();

	usb_musb_init(&musb_board_data);
}

MACHINE_START(CM_T35, "Compulab CM-T35")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= cm_t35_map_io,
	.init_irq	= cm_t35_init_irq,
	.init_machine	= cm_t35_init,
	.timer		= &omap_timer,
MACHINE_END
