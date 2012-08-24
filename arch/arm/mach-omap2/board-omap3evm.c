/*
 * linux/arch/arm/mach-omap2/board-omap3evm.c
 *
 * Copyright (C) 2008 Texas Instruments
 *
 * Modified from mach-omap2/board-3430sdp.c
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
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/leds.h>
#include <linux/interrupt.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>

#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/i2c/twl.h>
#include <linux/usb/otg.h>
#include <linux/smsc911x.h>

#include <linux/wl12xx.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/mmc/host.h>
#include <linux/export.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/usb.h>
#include <linux/platform_data/mtd-nand-omap2.h>
#include "common.h"
#include <linux/platform_data/spi-omap2-mcspi.h>
#include <video/omapdss.h>
#include <video/omap-panel-tfp410.h>

#include "mux.h"
#include "sdram-micron-mt46h32m32lf-6.h"
#include "hsmmc.h"
#include "common-board-devices.h"

#define OMAP3_EVM_TS_GPIO	175
#define OMAP3_EVM_EHCI_VBUS	22
#define OMAP3_EVM_EHCI_SELECT	61

#define OMAP3EVM_ETHR_START	0x2c000000
#define OMAP3EVM_ETHR_SIZE	1024
#define OMAP3EVM_ETHR_ID_REV	0x50
#define OMAP3EVM_ETHR_GPIO_IRQ	176
#define OMAP3EVM_SMSC911X_CS	5
/*
 * Eth Reset signal
 *	64 = Generation 1 (<=RevD)
 *	7 = Generation 2 (>=RevE)
 */
#define OMAP3EVM_GEN1_ETHR_GPIO_RST	64
#define OMAP3EVM_GEN2_ETHR_GPIO_RST	7

/*
 * OMAP35x EVM revision
 * Run time detection of EVM revision is done by reading Ethernet
 * PHY ID -
 *	GEN_1	= 0x01150000
 *	GEN_2	= 0x92200000
 */
enum {
	OMAP3EVM_BOARD_GEN_1 = 0,	/* EVM Rev between  A - D */
	OMAP3EVM_BOARD_GEN_2,		/* EVM Rev >= Rev E */
};

static u8 omap3_evm_version;

u8 get_omap3_evm_rev(void)
{
	return omap3_evm_version;
}
EXPORT_SYMBOL(get_omap3_evm_rev);

static void __init omap3_evm_get_revision(void)
{
	void __iomem *ioaddr;
	unsigned int smsc_id;

	/* Ethernet PHY ID is stored at ID_REV register */
	ioaddr = ioremap_nocache(OMAP3EVM_ETHR_START, SZ_1K);
	if (!ioaddr)
		return;
	smsc_id = readl(ioaddr + OMAP3EVM_ETHR_ID_REV) & 0xFFFF0000;
	iounmap(ioaddr);

	switch (smsc_id) {
	/*SMSC9115 chipset*/
	case 0x01150000:
		omap3_evm_version = OMAP3EVM_BOARD_GEN_1;
		break;
	/*SMSC 9220 chipset*/
	case 0x92200000:
	default:
		omap3_evm_version = OMAP3EVM_BOARD_GEN_2;
	}
}

#if defined(CONFIG_SMSC911X) || defined(CONFIG_SMSC911X_MODULE)
#include <plat/gpmc-smsc911x.h>

static struct omap_smsc911x_platform_data smsc911x_cfg = {
	.cs             = OMAP3EVM_SMSC911X_CS,
	.gpio_irq       = OMAP3EVM_ETHR_GPIO_IRQ,
	.gpio_reset     = -EINVAL,
	.flags		= SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS,
};

static inline void __init omap3evm_init_smsc911x(void)
{
	/* Configure ethernet controller reset gpio */
	if (cpu_is_omap3430()) {
		if (get_omap3_evm_rev() == OMAP3EVM_BOARD_GEN_1)
			smsc911x_cfg.gpio_reset = OMAP3EVM_GEN1_ETHR_GPIO_RST;
		else
			smsc911x_cfg.gpio_reset = OMAP3EVM_GEN2_ETHR_GPIO_RST;
	}

	gpmc_smsc911x_init(&smsc911x_cfg);
}

#else
static inline void __init omap3evm_init_smsc911x(void) { return; }
#endif

/*
 * OMAP3EVM LCD Panel control signals
 */
#define OMAP3EVM_LCD_PANEL_LR		2
#define OMAP3EVM_LCD_PANEL_UD		3
#define OMAP3EVM_LCD_PANEL_INI		152
#define OMAP3EVM_LCD_PANEL_ENVDD	153
#define OMAP3EVM_LCD_PANEL_QVGA		154
#define OMAP3EVM_LCD_PANEL_RESB		155
#define OMAP3EVM_LCD_PANEL_BKLIGHT_GPIO	210
#define OMAP3EVM_DVI_PANEL_EN_GPIO	199

static struct gpio omap3_evm_dss_gpios[] __initdata = {
	{ OMAP3EVM_LCD_PANEL_RESB,  GPIOF_OUT_INIT_HIGH, "lcd_panel_resb"  },
	{ OMAP3EVM_LCD_PANEL_INI,   GPIOF_OUT_INIT_HIGH, "lcd_panel_ini"   },
	{ OMAP3EVM_LCD_PANEL_QVGA,  GPIOF_OUT_INIT_LOW,  "lcd_panel_qvga"  },
	{ OMAP3EVM_LCD_PANEL_LR,    GPIOF_OUT_INIT_HIGH, "lcd_panel_lr"    },
	{ OMAP3EVM_LCD_PANEL_UD,    GPIOF_OUT_INIT_HIGH, "lcd_panel_ud"    },
	{ OMAP3EVM_LCD_PANEL_ENVDD, GPIOF_OUT_INIT_LOW,  "lcd_panel_envdd" },
};

static int lcd_enabled;
static int dvi_enabled;

static void __init omap3_evm_display_init(void)
{
	int r;

	r = gpio_request_array(omap3_evm_dss_gpios,
			       ARRAY_SIZE(omap3_evm_dss_gpios));
	if (r)
		printk(KERN_ERR "failed to get lcd_panel_* gpios\n");
}

static int omap3_evm_enable_lcd(struct omap_dss_device *dssdev)
{
	if (dvi_enabled) {
		printk(KERN_ERR "cannot enable LCD, DVI is enabled\n");
		return -EINVAL;
	}
	gpio_set_value(OMAP3EVM_LCD_PANEL_ENVDD, 0);

	if (get_omap3_evm_rev() >= OMAP3EVM_BOARD_GEN_2)
		gpio_set_value_cansleep(OMAP3EVM_LCD_PANEL_BKLIGHT_GPIO, 0);
	else
		gpio_set_value_cansleep(OMAP3EVM_LCD_PANEL_BKLIGHT_GPIO, 1);

	lcd_enabled = 1;
	return 0;
}

static void omap3_evm_disable_lcd(struct omap_dss_device *dssdev)
{
	gpio_set_value(OMAP3EVM_LCD_PANEL_ENVDD, 1);

	if (get_omap3_evm_rev() >= OMAP3EVM_BOARD_GEN_2)
		gpio_set_value_cansleep(OMAP3EVM_LCD_PANEL_BKLIGHT_GPIO, 1);
	else
		gpio_set_value_cansleep(OMAP3EVM_LCD_PANEL_BKLIGHT_GPIO, 0);

	lcd_enabled = 0;
}

static struct omap_dss_device omap3_evm_lcd_device = {
	.name			= "lcd",
	.driver_name		= "sharp_ls_panel",
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.phy.dpi.data_lines	= 18,
	.platform_enable	= omap3_evm_enable_lcd,
	.platform_disable	= omap3_evm_disable_lcd,
};

static int omap3_evm_enable_tv(struct omap_dss_device *dssdev)
{
	return 0;
}

static void omap3_evm_disable_tv(struct omap_dss_device *dssdev)
{
}

static struct omap_dss_device omap3_evm_tv_device = {
	.name			= "tv",
	.driver_name		= "venc",
	.type			= OMAP_DISPLAY_TYPE_VENC,
	.phy.venc.type		= OMAP_DSS_VENC_TYPE_SVIDEO,
	.platform_enable	= omap3_evm_enable_tv,
	.platform_disable	= omap3_evm_disable_tv,
};

static struct tfp410_platform_data dvi_panel = {
	.power_down_gpio	= OMAP3EVM_DVI_PANEL_EN_GPIO,
};

static struct omap_dss_device omap3_evm_dvi_device = {
	.name			= "dvi",
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.driver_name		= "tfp410",
	.data			= &dvi_panel,
	.phy.dpi.data_lines	= 24,
};

static struct omap_dss_device *omap3_evm_dss_devices[] = {
	&omap3_evm_lcd_device,
	&omap3_evm_tv_device,
	&omap3_evm_dvi_device,
};

static struct omap_dss_board_info omap3_evm_dss_data = {
	.num_devices	= ARRAY_SIZE(omap3_evm_dss_devices),
	.devices	= omap3_evm_dss_devices,
	.default_device	= &omap3_evm_lcd_device,
};

static struct regulator_consumer_supply omap3evm_vmmc1_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.0"),
};

static struct regulator_consumer_supply omap3evm_vsim_supply[] = {
	REGULATOR_SUPPLY("vmmc_aux", "omap_hsmmc.0"),
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data omap3evm_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(omap3evm_vmmc1_supply),
	.consumer_supplies	= omap3evm_vmmc1_supply,
};

/* VSIM for MMC1 pins DAT4..DAT7 (2 mA, plus card == max 50 mA) */
static struct regulator_init_data omap3evm_vsim = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 3000000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(omap3evm_vsim_supply),
	.consumer_supplies	= omap3evm_vsim_supply,
};

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= 63,
		.deferred	= true,
	},
#ifdef CONFIG_WL12XX_PLATFORM_DATA
	{
		.name		= "wl1271",
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_POWER_OFF_CARD,
		.gpio_wp	= -EINVAL,
		.gpio_cd	= -EINVAL,
		.nonremovable	= true,
	},
#endif
	{}	/* Terminator */
};

static struct gpio_led gpio_leds[] = {
	{
		.name			= "omap3evm::ledb",
		/* normally not visible (board underside) */
		.default_trigger	= "default-on",
		.gpio			= -EINVAL,	/* gets replaced */
		.active_low		= true,
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


static int omap3evm_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	int r, lcd_bl_en;

	/* gpio + 0 is "mmc0_cd" (input/IRQ) */
	mmc[0].gpio_cd = gpio + 0;
	omap_hsmmc_late_init(mmc);

	/*
	 * Most GPIOs are for USB OTG.  Some are mostly sent to
	 * the P2 connector; notably LEDA for the LCD backlight.
	 */

	/* TWL4030_GPIO_MAX + 0 == ledA, LCD Backlight control */
	lcd_bl_en = get_omap3_evm_rev() >= OMAP3EVM_BOARD_GEN_2 ?
		GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW;
	r = gpio_request_one(gpio + TWL4030_GPIO_MAX, lcd_bl_en, "EN_LCD_BKL");
	if (r)
		printk(KERN_ERR "failed to get/set lcd_bkl gpio\n");

	/* gpio + 7 == DVI Enable */
	gpio_request_one(gpio + 7, GPIOF_OUT_INIT_LOW, "EN_DVI");

	/* TWL4030_GPIO_MAX + 1 == ledB (out, active low LED) */
	gpio_leds[0].gpio = gpio + TWL4030_GPIO_MAX + 1;

	platform_device_register(&leds_gpio);

	/* Enable VBUS switch by setting TWL4030.GPIO2DIR as output
	 * for starting USB tranceiver
	 */
#ifdef CONFIG_TWL4030_CORE
	if (get_omap3_evm_rev() >= OMAP3EVM_BOARD_GEN_2) {
		u8 val;

		twl_i2c_read_u8(TWL4030_MODULE_GPIO, &val, REG_GPIODATADIR1);
		val |= 0x04; /* TWL4030.GPIO2DIR BIT at GPIODATADIR1(0x9B) */
		twl_i2c_write_u8(TWL4030_MODULE_GPIO, val, REG_GPIODATADIR1);
	}
#endif

	return 0;
}

static struct twl4030_gpio_platform_data omap3evm_gpio_data = {
	.use_leds	= true,
	.setup		= omap3evm_twl_gpio_setup,
};

static uint32_t board_keymap[] = {
	KEY(0, 0, KEY_LEFT),
	KEY(0, 1, KEY_DOWN),
	KEY(0, 2, KEY_ENTER),
	KEY(0, 3, KEY_M),

	KEY(1, 0, KEY_RIGHT),
	KEY(1, 1, KEY_UP),
	KEY(1, 2, KEY_I),
	KEY(1, 3, KEY_N),

	KEY(2, 0, KEY_A),
	KEY(2, 1, KEY_E),
	KEY(2, 2, KEY_J),
	KEY(2, 3, KEY_O),

	KEY(3, 0, KEY_B),
	KEY(3, 1, KEY_F),
	KEY(3, 2, KEY_K),
	KEY(3, 3, KEY_P)
};

static struct matrix_keymap_data board_map_data = {
	.keymap			= board_keymap,
	.keymap_size		= ARRAY_SIZE(board_keymap),
};

static struct twl4030_keypad_data omap3evm_kp_data = {
	.keymap_data	= &board_map_data,
	.rows		= 4,
	.cols		= 4,
	.rep		= 1,
};

/* ads7846 on SPI */
static struct regulator_consumer_supply omap3evm_vio_supply[] = {
	REGULATOR_SUPPLY("vcc", "spi1.0"),
};

/* VIO for ads7846 */
static struct regulator_init_data omap3evm_vio = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(omap3evm_vio_supply),
	.consumer_supplies	= omap3evm_vio_supply,
};

#ifdef CONFIG_WL12XX_PLATFORM_DATA

#define OMAP3EVM_WLAN_PMENA_GPIO	(150)
#define OMAP3EVM_WLAN_IRQ_GPIO		(149)

static struct regulator_consumer_supply omap3evm_vmmc2_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.1"),
};

/* VMMC2 for driving the WL12xx module */
static struct regulator_init_data omap3evm_vmmc2 = {
	.constraints = {
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(omap3evm_vmmc2_supply),
	.consumer_supplies	= omap3evm_vmmc2_supply,
};

static struct fixed_voltage_config omap3evm_vwlan = {
	.supply_name		= "vwl1271",
	.microvolts		= 1800000, /* 1.80V */
	.gpio			= OMAP3EVM_WLAN_PMENA_GPIO,
	.startup_delay		= 70000, /* 70ms */
	.enable_high		= 1,
	.enabled_at_boot	= 0,
	.init_data		= &omap3evm_vmmc2,
};

static struct platform_device omap3evm_wlan_regulator = {
	.name		= "reg-fixed-voltage",
	.id		= 1,
	.dev = {
		.platform_data	= &omap3evm_vwlan,
	},
};

struct wl12xx_platform_data omap3evm_wlan_data __initdata = {
	.board_ref_clock = WL12XX_REFCLOCK_38, /* 38.4 MHz */
};
#endif

/* VAUX2 for USB */
static struct regulator_consumer_supply omap3evm_vaux2_supplies[] = {
	REGULATOR_SUPPLY("VDD_CSIPHY1", "omap3isp"),	/* OMAP ISP */
	REGULATOR_SUPPLY("VDD_CSIPHY2", "omap3isp"),	/* OMAP ISP */
	REGULATOR_SUPPLY("hsusb1", "ehci-omap.0"),
	REGULATOR_SUPPLY("vaux2", NULL),
};

static struct regulator_init_data omap3evm_vaux2 = {
	.constraints = {
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies		= ARRAY_SIZE(omap3evm_vaux2_supplies),
	.consumer_supplies		= omap3evm_vaux2_supplies,
};

static struct twl4030_platform_data omap3evm_twldata = {
	/* platform_data for children goes here */
	.keypad		= &omap3evm_kp_data,
	.gpio		= &omap3evm_gpio_data,
	.vio		= &omap3evm_vio,
	.vmmc1		= &omap3evm_vmmc1,
	.vsim		= &omap3evm_vsim,
};

static int __init omap3_evm_i2c_init(void)
{
	omap3_pmic_get_config(&omap3evm_twldata,
			TWL_COMMON_PDATA_USB | TWL_COMMON_PDATA_MADC |
			TWL_COMMON_PDATA_AUDIO,
			TWL_COMMON_REGULATOR_VDAC | TWL_COMMON_REGULATOR_VPLL2);

	omap3evm_twldata.vdac->constraints.apply_uV = true;
	omap3evm_twldata.vpll2->constraints.apply_uV = true;

	omap3_pmic_init("twl4030", &omap3evm_twldata);
	omap_register_i2c_bus(2, 400, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static struct usbhs_omap_board_data usbhs_bdata __initdata = {

	.port_mode[0] = OMAP_USBHS_PORT_MODE_UNUSED,
	.port_mode[1] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[2] = OMAP_USBHS_PORT_MODE_UNUSED,

	.phy_reset  = true,
	/* PHY reset GPIO will be runtime programmed based on EVM version */
	.reset_gpio_port[0]  = -EINVAL,
	.reset_gpio_port[1]  = -EINVAL,
	.reset_gpio_port[2]  = -EINVAL
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux omap35x_board_mux[] __initdata = {
	OMAP3_MUX(SYS_NIRQ, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP |
				OMAP_PIN_OFF_INPUT_PULLUP | OMAP_PIN_OFF_OUTPUT_LOW |
				OMAP_PIN_OFF_WAKEUPENABLE),
	OMAP3_MUX(MCSPI1_CS1, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP |
				OMAP_PIN_OFF_INPUT_PULLUP | OMAP_PIN_OFF_OUTPUT_LOW |
				OMAP_PIN_OFF_WAKEUPENABLE),
	OMAP3_MUX(SYS_BOOT5, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP |
				OMAP_PIN_OFF_NONE),
	OMAP3_MUX(GPMC_WAIT2, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP |
				OMAP_PIN_OFF_NONE),
#ifdef CONFIG_WL12XX_PLATFORM_DATA
	/* WLAN IRQ - GPIO 149 */
	OMAP3_MUX(UART1_RTS, OMAP_MUX_MODE4 | OMAP_PIN_INPUT),

	/* WLAN POWER ENABLE - GPIO 150 */
	OMAP3_MUX(UART1_CTS, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),

	/* MMC2 SDIO pin muxes for WL12xx */
	OMAP3_MUX(SDMMC2_CLK, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP3_MUX(SDMMC2_CMD, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP3_MUX(SDMMC2_DAT0, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP3_MUX(SDMMC2_DAT1, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP3_MUX(SDMMC2_DAT2, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP3_MUX(SDMMC2_DAT3, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
#endif
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};

static struct omap_board_mux omap36x_board_mux[] __initdata = {
	OMAP3_MUX(SYS_NIRQ, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP |
				OMAP_PIN_OFF_INPUT_PULLUP | OMAP_PIN_OFF_OUTPUT_LOW |
				OMAP_PIN_OFF_WAKEUPENABLE),
	OMAP3_MUX(MCSPI1_CS1, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP |
				OMAP_PIN_OFF_INPUT_PULLUP | OMAP_PIN_OFF_OUTPUT_LOW |
				OMAP_PIN_OFF_WAKEUPENABLE),
	/* AM/DM37x EVM: DSS data bus muxed with sys_boot */
	OMAP3_MUX(DSS_DATA18, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
	OMAP3_MUX(DSS_DATA19, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
	OMAP3_MUX(DSS_DATA22, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
	OMAP3_MUX(DSS_DATA21, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
	OMAP3_MUX(DSS_DATA22, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
	OMAP3_MUX(DSS_DATA23, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
	OMAP3_MUX(SYS_BOOT0, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
	OMAP3_MUX(SYS_BOOT1, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
	OMAP3_MUX(SYS_BOOT3, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
	OMAP3_MUX(SYS_BOOT4, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
	OMAP3_MUX(SYS_BOOT5, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
	OMAP3_MUX(SYS_BOOT6, OMAP_MUX_MODE3 | OMAP_PIN_OFF_NONE),
#ifdef CONFIG_WL12XX_PLATFORM_DATA
	/* WLAN IRQ - GPIO 149 */
	OMAP3_MUX(UART1_RTS, OMAP_MUX_MODE4 | OMAP_PIN_INPUT),

	/* WLAN POWER ENABLE - GPIO 150 */
	OMAP3_MUX(UART1_CTS, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),

	/* MMC2 SDIO pin muxes for WL12xx */
	OMAP3_MUX(SDMMC2_CLK, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP3_MUX(SDMMC2_CMD, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP3_MUX(SDMMC2_DAT0, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP3_MUX(SDMMC2_DAT1, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP3_MUX(SDMMC2_DAT2, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
	OMAP3_MUX(SDMMC2_DAT3, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLUP),
#endif

	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#else
#define omap35x_board_mux	NULL
#define omap36x_board_mux	NULL
#endif

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_OTG,
	.power			= 100,
};

static struct gpio omap3_evm_ehci_gpios[] __initdata = {
	{ OMAP3_EVM_EHCI_VBUS,	 GPIOF_OUT_INIT_HIGH,  "enable EHCI VBUS" },
	{ OMAP3_EVM_EHCI_SELECT, GPIOF_OUT_INIT_LOW,   "select EHCI port" },
};

static void __init omap3_evm_wl12xx_init(void)
{
#ifdef CONFIG_WL12XX_PLATFORM_DATA
	int ret;

	/* WL12xx WLAN Init */
	omap3evm_wlan_data.irq = gpio_to_irq(OMAP3EVM_WLAN_IRQ_GPIO);
	ret = wl12xx_set_platform_data(&omap3evm_wlan_data);
	if (ret)
		pr_err("error setting wl12xx data: %d\n", ret);
	ret = platform_device_register(&omap3evm_wlan_regulator);
	if (ret)
		pr_err("error registering wl12xx device: %d\n", ret);
#endif
}

static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x.0"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x.0"),
};

static struct mtd_partition omap3evm_nand_partitions[] = {
	/* All the partition sizes are listed in terms of NAND block size */
	{
		.name           = "X-Loader",
		.offset         = 0,
		.size           = 4*(SZ_128K),
		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name           = "U-Boot",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 14*(SZ_128K),
		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name           = "U-Boot Env",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 2*(SZ_128K)
	},
	{
		.name           = "Kernel",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 40*(SZ_128K)
	},
	{
		.name           = "File system",
		.size           = MTDPART_SIZ_FULL,
		.offset         = MTDPART_OFS_APPEND,
	},
};

static void __init omap3_evm_init(void)
{
	struct omap_board_mux *obm;

	omap3_evm_get_revision();
	regulator_register_fixed(0, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	obm = (cpu_is_omap3630()) ? omap36x_board_mux : omap35x_board_mux;
	omap3_mux_init(obm, OMAP_PACKAGE_CBB);

	omap_mux_init_gpio(63, OMAP_PIN_INPUT);
	omap_hsmmc_init(mmc);

	if (get_omap3_evm_rev() >= OMAP3EVM_BOARD_GEN_2)
		omap3evm_twldata.vaux2 = &omap3evm_vaux2;

	omap3_evm_i2c_init();

	omap_display_init(&omap3_evm_dss_data);

	omap_serial_init();
	omap_sdrc_init(mt46h32m32lf6_sdrc_params, NULL);

	/* OMAP3EVM uses ISP1504 phy and so register nop transceiver */
	usb_nop_xceiv_register();

	if (get_omap3_evm_rev() >= OMAP3EVM_BOARD_GEN_2) {
		/* enable EHCI VBUS using GPIO22 */
		omap_mux_init_gpio(OMAP3_EVM_EHCI_VBUS, OMAP_PIN_INPUT_PULLUP);
		/* Select EHCI port on main board */
		omap_mux_init_gpio(OMAP3_EVM_EHCI_SELECT,
				   OMAP_PIN_INPUT_PULLUP);
		gpio_request_array(omap3_evm_ehci_gpios,
				   ARRAY_SIZE(omap3_evm_ehci_gpios));

		/* setup EHCI phy reset config */
		omap_mux_init_gpio(21, OMAP_PIN_INPUT_PULLUP);
		usbhs_bdata.reset_gpio_port[1] = 21;

		/* EVM REV >= E can supply 500mA with EXTVBUS programming */
		musb_board_data.power = 500;
		musb_board_data.extvbus = 1;
	} else {
		/* setup EHCI phy reset on MDC */
		omap_mux_init_gpio(135, OMAP_PIN_OUTPUT);
		usbhs_bdata.reset_gpio_port[1] = 135;
	}
	usb_musb_init(&musb_board_data);
	usbhs_init(&usbhs_bdata);
	omap_nand_flash_init(NAND_BUSWIDTH_16, omap3evm_nand_partitions,
			     ARRAY_SIZE(omap3evm_nand_partitions));

	omap_ads7846_init(1, OMAP3_EVM_TS_GPIO, 310, NULL);
	omap3evm_init_smsc911x();
	omap3_evm_display_init();
	omap3_evm_wl12xx_init();
}

MACHINE_START(OMAP3EVM, "OMAP3 EVM")
	/* Maintainer: Syed Mohammed Khasim - Texas Instruments */
	.atag_offset	= 0x100,
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap35xx_init_early,
	.init_irq	= omap3_init_irq,
	.handle_irq	= omap3_intc_handle_irq,
	.init_machine	= omap3_evm_init,
	.init_late	= omap35xx_init_late,
	.timer		= &omap3_timer,
	.restart	= omap_prcm_restart,
MACHINE_END
