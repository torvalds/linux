/*
 * Board support file for OMAP4430 based PandaBoard.
 *
 * Copyright (C) 2010 Texas Instruments
 *
 * Author: David Anders <x0132446@ti.com>
 *
 * Based on mach-omap2/board-4430sdp.c
 *
 * Author: Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Based on mach-omap2/board-3430sdp.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/usb/otg.h>
#include <linux/i2c/twl.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/wl12xx.h>

#include <mach/hardware.h>
#include <mach/omap4-common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <video/omapdss.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/usb.h>
#include <plat/mmc.h>
#include <video/omap-panel-generic-dpi.h>
#include "timer-gp.h"

#include "hsmmc.h"
#include "control.h"
#include "mux.h"
#include "common-board-devices.h"

#define GPIO_HUB_POWER		1
#define GPIO_HUB_NRESET		62
#define GPIO_WIFI_PMENA		43
#define GPIO_WIFI_IRQ		53
#define HDMI_GPIO_HPD 60 /* Hot plug pin for HDMI */
#define HDMI_GPIO_LS_OE 41 /* Level shifter for HDMI */

/* wl127x BT, FM, GPS connectivity chip */
static int wl1271_gpios[] = {46, -1, -1};
static struct platform_device wl1271_device = {
	.name	= "kim",
	.id	= -1,
	.dev	= {
		.platform_data	= &wl1271_gpios,
	},
};

static struct gpio_led gpio_leds[] = {
	{
		.name			= "pandaboard::status1",
		.default_trigger	= "heartbeat",
		.gpio			= 7,
	},
	{
		.name			= "pandaboard::status2",
		.default_trigger	= "mmc0",
		.gpio			= 8,
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

static struct platform_device *panda_devices[] __initdata = {
	&leds_gpio,
	&wl1271_device,
};

static void __init omap4_panda_init_early(void)
{
	omap2_init_common_infrastructure();
	omap2_init_common_devices(NULL, NULL);
}

static const struct usbhs_omap_board_data usbhs_bdata __initconst = {
	.port_mode[0] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[1] = OMAP_USBHS_PORT_MODE_UNUSED,
	.port_mode[2] = OMAP_USBHS_PORT_MODE_UNUSED,
	.phy_reset  = false,
	.reset_gpio_port[0]  = -EINVAL,
	.reset_gpio_port[1]  = -EINVAL,
	.reset_gpio_port[2]  = -EINVAL
};

static struct gpio panda_ehci_gpios[] __initdata = {
	{ GPIO_HUB_POWER,	GPIOF_OUT_INIT_LOW,  "hub_power"  },
	{ GPIO_HUB_NRESET,	GPIOF_OUT_INIT_LOW,  "hub_nreset" },
};

static void __init omap4_ehci_init(void)
{
	int ret;
	struct clk *phy_ref_clk;

	/* FREF_CLK3 provides the 19.2 MHz reference clock to the PHY */
	phy_ref_clk = clk_get(NULL, "auxclk3_ck");
	if (IS_ERR(phy_ref_clk)) {
		pr_err("Cannot request auxclk3\n");
		return;
	}
	clk_set_rate(phy_ref_clk, 19200000);
	clk_enable(phy_ref_clk);

	/* disable the power to the usb hub prior to init and reset phy+hub */
	ret = gpio_request_array(panda_ehci_gpios,
				 ARRAY_SIZE(panda_ehci_gpios));
	if (ret) {
		pr_err("Unable to initialize EHCI power/reset\n");
		return;
	}

	gpio_export(GPIO_HUB_POWER, 0);
	gpio_export(GPIO_HUB_NRESET, 0);
	gpio_set_value(GPIO_HUB_NRESET, 1);

	usbhs_init(&usbhs_bdata);

	/* enable power to hub */
	gpio_set_value(GPIO_HUB_POWER, 1);
}

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_UTMI,
	.mode			= MUSB_OTG,
	.power			= 100,
};

static struct twl4030_usb_data omap4_usbphy_data = {
	.phy_init	= omap4430_phy_init,
	.phy_exit	= omap4430_phy_exit,
	.phy_power	= omap4430_phy_power,
	.phy_set_clock	= omap4430_phy_set_clk,
	.phy_suspend	= omap4430_phy_suspend,
};

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
		.gpio_wp	= -EINVAL,
		.gpio_cd	= -EINVAL,
	},
	{
		.name		= "wl1271",
		.mmc		= 5,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_POWER_OFF_CARD,
		.gpio_wp	= -EINVAL,
		.gpio_cd	= -EINVAL,
		.ocr_mask	= MMC_VDD_165_195,
		.nonremovable	= true,
	},
	{}	/* Terminator */
};

static struct regulator_consumer_supply omap4_panda_vmmc_supply[] = {
	{
		.supply = "vmmc",
		.dev_name = "omap_hsmmc.0",
	},
};

static struct regulator_consumer_supply omap4_panda_vmmc5_supply = {
	.supply = "vmmc",
	.dev_name = "omap_hsmmc.4",
};

static struct regulator_init_data panda_vmmc5 = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &omap4_panda_vmmc5_supply,
};

static struct fixed_voltage_config panda_vwlan = {
	.supply_name = "vwl1271",
	.microvolts = 1800000, /* 1.8V */
	.gpio = GPIO_WIFI_PMENA,
	.startup_delay = 70000, /* 70msec */
	.enable_high = 1,
	.enabled_at_boot = 0,
	.init_data = &panda_vmmc5,
};

static struct platform_device omap_vwlan_device = {
	.name		= "reg-fixed-voltage",
	.id		= 1,
	.dev = {
		.platform_data = &panda_vwlan,
	},
};

struct wl12xx_platform_data omap_panda_wlan_data  __initdata = {
	.irq = OMAP_GPIO_IRQ(GPIO_WIFI_IRQ),
	/* PANDA ref clock is 38.4 MHz */
	.board_ref_clock = 2,
};

static int omap4_twl6030_hsmmc_late_init(struct device *dev)
{
	int ret = 0;
	struct platform_device *pdev = container_of(dev,
				struct platform_device, dev);
	struct omap_mmc_platform_data *pdata = dev->platform_data;

	if (!pdata) {
		dev_err(dev, "%s: NULL platform data\n", __func__);
		return -EINVAL;
	}
	/* Setting MMC1 Card detect Irq */
	if (pdev->id == 0) {
		ret = twl6030_mmc_card_detect_config();
		 if (ret)
			dev_err(dev, "%s: Error card detect config(%d)\n",
				__func__, ret);
		 else
			pdata->slots[0].card_detect = twl6030_mmc_card_detect;
	}
	return ret;
}

static __init void omap4_twl6030_hsmmc_set_late_init(struct device *dev)
{
	struct omap_mmc_platform_data *pdata;

	/* dev can be null if CONFIG_MMC_OMAP_HS is not set */
	if (!dev) {
		pr_err("Failed omap4_twl6030_hsmmc_set_late_init\n");
		return;
	}
	pdata = dev->platform_data;

	pdata->init =	omap4_twl6030_hsmmc_late_init;
}

static int __init omap4_twl6030_hsmmc_init(struct omap2_hsmmc_info *controllers)
{
	struct omap2_hsmmc_info *c;

	omap2_hsmmc_init(controllers);
	for (c = controllers; c->mmc; c++)
		omap4_twl6030_hsmmc_set_late_init(c->dev);

	return 0;
}

static struct regulator_init_data omap4_panda_vaux2 = {
	.constraints = {
		.min_uV			= 1200000,
		.max_uV			= 2800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask	 = REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data omap4_panda_vaux3 = {
	.constraints = {
		.min_uV			= 1000000,
		.max_uV			= 3000000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask	 = REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

/* VMMC1 for MMC1 card */
static struct regulator_init_data omap4_panda_vmmc = {
	.constraints = {
		.min_uV			= 1200000,
		.max_uV			= 3000000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask	 = REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = 1,
	.consumer_supplies      = omap4_panda_vmmc_supply,
};

static struct regulator_init_data omap4_panda_vpp = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 2500000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask	 = REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data omap4_panda_vana = {
	.constraints = {
		.min_uV			= 2100000,
		.max_uV			= 2100000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask	 = REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data omap4_panda_vcxio = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask	 = REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data omap4_panda_vdac = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask	 = REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data omap4_panda_vusb = {
	.constraints = {
		.min_uV			= 3300000,
		.max_uV			= 3300000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask	 =	REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data omap4_panda_clk32kg = {
	.constraints = {
		.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
	},
};

static struct twl4030_platform_data omap4_panda_twldata = {
	.irq_base	= TWL6030_IRQ_BASE,
	.irq_end	= TWL6030_IRQ_END,

	/* Regulators */
	.vmmc		= &omap4_panda_vmmc,
	.vpp		= &omap4_panda_vpp,
	.vana		= &omap4_panda_vana,
	.vcxio		= &omap4_panda_vcxio,
	.vdac		= &omap4_panda_vdac,
	.vusb		= &omap4_panda_vusb,
	.vaux2		= &omap4_panda_vaux2,
	.vaux3		= &omap4_panda_vaux3,
	.clk32kg	= &omap4_panda_clk32kg,
	.usb		= &omap4_usbphy_data,
};

/*
 * Display monitor features are burnt in their EEPROM as EDID data. The EEPROM
 * is connected as I2C slave device, and can be accessed at address 0x50
 */
static struct i2c_board_info __initdata panda_i2c_eeprom[] = {
	{
		I2C_BOARD_INFO("eeprom", 0x50),
	},
};

static int __init omap4_panda_i2c_init(void)
{
	omap4_pmic_init("twl6030", &omap4_panda_twldata);
	omap_register_i2c_bus(2, 400, NULL, 0);
	/*
	 * Bus 3 is attached to the DVI port where devices like the pico DLP
	 * projector don't work reliably with 400kHz
	 */
	omap_register_i2c_bus(3, 100, panda_i2c_eeprom,
					ARRAY_SIZE(panda_i2c_eeprom));
	omap_register_i2c_bus(4, 400, NULL, 0);
	return 0;
}

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	/* WLAN IRQ - GPIO 53 */
	OMAP4_MUX(GPMC_NCS3, OMAP_MUX_MODE3 | OMAP_PIN_INPUT),
	/* WLAN POWER ENABLE - GPIO 43 */
	OMAP4_MUX(GPMC_A19, OMAP_MUX_MODE3 | OMAP_PIN_OUTPUT),
	/* WLAN SDIO: MMC5 CMD */
	OMAP4_MUX(SDMMC5_CMD, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	/* WLAN SDIO: MMC5 CLK */
	OMAP4_MUX(SDMMC5_CLK, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	/* WLAN SDIO: MMC5 DAT[0-3] */
	OMAP4_MUX(SDMMC5_DAT0, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP4_MUX(SDMMC5_DAT1, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP4_MUX(SDMMC5_DAT2, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP4_MUX(SDMMC5_DAT3, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	/* gpio 0 - TFP410 PD */
	OMAP4_MUX(KPD_COL1, OMAP_PIN_OUTPUT | OMAP_MUX_MODE3),
	/* dispc2_data23 */
	OMAP4_MUX(USBB2_ULPITLL_STP, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data22 */
	OMAP4_MUX(USBB2_ULPITLL_DIR, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data21 */
	OMAP4_MUX(USBB2_ULPITLL_NXT, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data20 */
	OMAP4_MUX(USBB2_ULPITLL_DAT0, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data19 */
	OMAP4_MUX(USBB2_ULPITLL_DAT1, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data18 */
	OMAP4_MUX(USBB2_ULPITLL_DAT2, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data15 */
	OMAP4_MUX(USBB2_ULPITLL_DAT3, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data14 */
	OMAP4_MUX(USBB2_ULPITLL_DAT4, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data13 */
	OMAP4_MUX(USBB2_ULPITLL_DAT5, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data12 */
	OMAP4_MUX(USBB2_ULPITLL_DAT6, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data11 */
	OMAP4_MUX(USBB2_ULPITLL_DAT7, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data10 */
	OMAP4_MUX(DPM_EMU3, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data9 */
	OMAP4_MUX(DPM_EMU4, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data16 */
	OMAP4_MUX(DPM_EMU5, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data17 */
	OMAP4_MUX(DPM_EMU6, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_hsync */
	OMAP4_MUX(DPM_EMU7, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_pclk */
	OMAP4_MUX(DPM_EMU8, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_vsync */
	OMAP4_MUX(DPM_EMU9, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_de */
	OMAP4_MUX(DPM_EMU10, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data8 */
	OMAP4_MUX(DPM_EMU11, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data7 */
	OMAP4_MUX(DPM_EMU12, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data6 */
	OMAP4_MUX(DPM_EMU13, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data5 */
	OMAP4_MUX(DPM_EMU14, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data4 */
	OMAP4_MUX(DPM_EMU15, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data3 */
	OMAP4_MUX(DPM_EMU16, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data2 */
	OMAP4_MUX(DPM_EMU17, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data1 */
	OMAP4_MUX(DPM_EMU18, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	/* dispc2_data0 */
	OMAP4_MUX(DPM_EMU19, OMAP_PIN_OUTPUT | OMAP_MUX_MODE5),
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};

static struct omap_device_pad serial2_pads[] __initdata = {
	OMAP_MUX_STATIC("uart2_cts.uart2_cts",
			 OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP_MUX_STATIC("uart2_rts.uart2_rts",
			 OMAP_PIN_OUTPUT | OMAP_MUX_MODE0),
	OMAP_MUX_STATIC("uart2_rx.uart2_rx",
			 OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP_MUX_STATIC("uart2_tx.uart2_tx",
			 OMAP_PIN_OUTPUT | OMAP_MUX_MODE0),
};

static struct omap_device_pad serial3_pads[] __initdata = {
	OMAP_MUX_STATIC("uart3_cts_rctx.uart3_cts_rctx",
			 OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP_MUX_STATIC("uart3_rts_sd.uart3_rts_sd",
			 OMAP_PIN_OUTPUT | OMAP_MUX_MODE0),
	OMAP_MUX_STATIC("uart3_rx_irrx.uart3_rx_irrx",
			 OMAP_PIN_INPUT | OMAP_MUX_MODE0),
	OMAP_MUX_STATIC("uart3_tx_irtx.uart3_tx_irtx",
			 OMAP_PIN_OUTPUT | OMAP_MUX_MODE0),
};

static struct omap_device_pad serial4_pads[] __initdata = {
	OMAP_MUX_STATIC("uart4_rx.uart4_rx",
			 OMAP_PIN_INPUT | OMAP_MUX_MODE0),
	OMAP_MUX_STATIC("uart4_tx.uart4_tx",
			 OMAP_PIN_OUTPUT | OMAP_MUX_MODE0),
};

static struct omap_board_data serial2_data __initdata = {
	.id             = 1,
	.pads           = serial2_pads,
	.pads_cnt       = ARRAY_SIZE(serial2_pads),
};

static struct omap_board_data serial3_data __initdata = {
	.id             = 2,
	.pads           = serial3_pads,
	.pads_cnt       = ARRAY_SIZE(serial3_pads),
};

static struct omap_board_data serial4_data __initdata = {
	.id             = 3,
	.pads           = serial4_pads,
	.pads_cnt       = ARRAY_SIZE(serial4_pads),
};

static inline void board_serial_init(void)
{
	struct omap_board_data bdata;
	bdata.flags     = 0;
	bdata.pads      = NULL;
	bdata.pads_cnt  = 0;
	bdata.id        = 0;
	/* pass dummy data for UART1 */
	omap_serial_init_port(&bdata);

	omap_serial_init_port(&serial2_data);
	omap_serial_init_port(&serial3_data);
	omap_serial_init_port(&serial4_data);
}
#else
#define board_mux	NULL

static inline void board_serial_init(void)
{
	omap_serial_init();
}
#endif

/* Display DVI */
#define PANDA_DVI_TFP410_POWER_DOWN_GPIO	0

static int omap4_panda_enable_dvi(struct omap_dss_device *dssdev)
{
	gpio_set_value(dssdev->reset_gpio, 1);
	return 0;
}

static void omap4_panda_disable_dvi(struct omap_dss_device *dssdev)
{
	gpio_set_value(dssdev->reset_gpio, 0);
}

/* Using generic display panel */
static struct panel_generic_dpi_data omap4_dvi_panel = {
	.name			= "generic",
	.platform_enable	= omap4_panda_enable_dvi,
	.platform_disable	= omap4_panda_disable_dvi,
};

struct omap_dss_device omap4_panda_dvi_device = {
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.name			= "dvi",
	.driver_name		= "generic_dpi_panel",
	.data			= &omap4_dvi_panel,
	.phy.dpi.data_lines	= 24,
	.reset_gpio		= PANDA_DVI_TFP410_POWER_DOWN_GPIO,
	.channel		= OMAP_DSS_CHANNEL_LCD2,
};

int __init omap4_panda_dvi_init(void)
{
	int r;

	/* Requesting TFP410 DVI GPIO and disabling it, at bootup */
	r = gpio_request_one(omap4_panda_dvi_device.reset_gpio,
				GPIOF_OUT_INIT_LOW, "DVI PD");
	if (r)
		pr_err("Failed to get DVI powerdown GPIO\n");

	return r;
}


static void omap4_panda_hdmi_mux_init(void)
{
	/* PAD0_HDMI_HPD_PAD1_HDMI_CEC */
	omap_mux_init_signal("hdmi_hpd",
			OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_signal("hdmi_cec",
			OMAP_PIN_INPUT_PULLUP);
	/* PAD0_HDMI_DDC_SCL_PAD1_HDMI_DDC_SDA */
	omap_mux_init_signal("hdmi_ddc_scl",
			OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_signal("hdmi_ddc_sda",
			OMAP_PIN_INPUT_PULLUP);
}

static struct gpio panda_hdmi_gpios[] = {
	{ HDMI_GPIO_HPD,	GPIOF_OUT_INIT_HIGH, "hdmi_gpio_hpd"   },
	{ HDMI_GPIO_LS_OE,	GPIOF_OUT_INIT_HIGH, "hdmi_gpio_ls_oe" },
};

static int omap4_panda_panel_enable_hdmi(struct omap_dss_device *dssdev)
{
	int status;

	status = gpio_request_array(panda_hdmi_gpios,
				    ARRAY_SIZE(panda_hdmi_gpios));
	if (status)
		pr_err("Cannot request HDMI GPIOs\n");

	return status;
}

static void omap4_panda_panel_disable_hdmi(struct omap_dss_device *dssdev)
{
	gpio_free(HDMI_GPIO_LS_OE);
	gpio_free(HDMI_GPIO_HPD);
}

static struct omap_dss_device  omap4_panda_hdmi_device = {
	.name = "hdmi",
	.driver_name = "hdmi_panel",
	.type = OMAP_DISPLAY_TYPE_HDMI,
	.platform_enable = omap4_panda_panel_enable_hdmi,
	.platform_disable = omap4_panda_panel_disable_hdmi,
	.channel = OMAP_DSS_CHANNEL_DIGIT,
};

static struct omap_dss_device *omap4_panda_dss_devices[] = {
	&omap4_panda_dvi_device,
	&omap4_panda_hdmi_device,
};

static struct omap_dss_board_info omap4_panda_dss_data = {
	.num_devices	= ARRAY_SIZE(omap4_panda_dss_devices),
	.devices	= omap4_panda_dss_devices,
	.default_device	= &omap4_panda_dvi_device,
};

void omap4_panda_display_init(void)
{
	int r;

	r = omap4_panda_dvi_init();
	if (r)
		pr_err("error initializing panda DVI\n");

	omap4_panda_hdmi_mux_init();
	omap_display_init(&omap4_panda_dss_data);
}

static void __init omap4_panda_init(void)
{
	int package = OMAP_PACKAGE_CBS;

	if (omap_rev() == OMAP4430_REV_ES1_0)
		package = OMAP_PACKAGE_CBL;
	omap4_mux_init(board_mux, NULL, package);

	if (wl12xx_set_platform_data(&omap_panda_wlan_data))
		pr_err("error setting wl12xx data\n");

	omap4_panda_i2c_init();
	platform_add_devices(panda_devices, ARRAY_SIZE(panda_devices));
	platform_device_register(&omap_vwlan_device);
	board_serial_init();
	omap4_twl6030_hsmmc_init(mmc);
	omap4_ehci_init();
	usb_musb_init(&musb_board_data);
	omap4_panda_display_init();
}

static void __init omap4_panda_map_io(void)
{
	omap2_set_globals_443x();
	omap44xx_map_common_io();
}

MACHINE_START(OMAP4_PANDA, "OMAP4 Panda board")
	/* Maintainer: David Anders - Texas Instruments Inc */
	.boot_params	= 0x80000100,
	.reserve	= omap_reserve,
	.map_io		= omap4_panda_map_io,
	.init_early	= omap4_panda_init_early,
	.init_irq	= gic_init_irq,
	.init_machine	= omap4_panda_init,
	.timer		= &omap_timer,
MACHINE_END
