/*
 * Copyright (C) 2009 Integration Software and Electronic Engineering.
 *
 * Modified from mach-omap2/board-generic.c
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
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/input.h>

#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/i2c/twl.h>
#include <linux/mmc/host.h>

#include <linux/mtd/nand.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <video/omapdss.h>
#include <video/omap-panel-tfp410.h>
#include <linux/platform_data/mtd-onenand-omap2.h>

#include "common.h"
#include "gpmc.h"
#include "mux.h"
#include "hsmmc.h"
#include "sdram-numonyx-m65kxxxxam.h"
#include "common-board-devices.h"
#include "board-flash.h"
#include "control.h"
#include "gpmc-onenand.h"

#define IGEP2_SMSC911X_CS       5
#define IGEP2_SMSC911X_GPIO     176
#define IGEP2_GPIO_USBH_NRESET  24
#define IGEP2_GPIO_LED0_GREEN   26
#define IGEP2_GPIO_LED0_RED     27
#define IGEP2_GPIO_LED1_RED     28
#define IGEP2_GPIO_DVI_PUP      170

#define IGEP2_RB_GPIO_WIFI_NPD     94
#define IGEP2_RB_GPIO_WIFI_NRESET  95
#define IGEP2_RB_GPIO_BT_NRESET    137
#define IGEP2_RC_GPIO_WIFI_NPD     138
#define IGEP2_RC_GPIO_WIFI_NRESET  139
#define IGEP2_RC_GPIO_BT_NRESET    137

#define IGEP3_GPIO_LED0_GREEN	54
#define IGEP3_GPIO_LED0_RED	53
#define IGEP3_GPIO_LED1_RED	16
#define IGEP3_GPIO_USBH_NRESET  183

#define IGEP_SYSBOOT_MASK           0x1f
#define IGEP_SYSBOOT_NAND           0x0f
#define IGEP_SYSBOOT_ONENAND        0x10

/*
 * IGEP2 Hardware Revision Table
 *
 *  --------------------------------------------------------------------------
 * | Id. | Hw Rev.            | HW0 (28) | WIFI_NPD | WIFI_NRESET | BT_NRESET |
 *  --------------------------------------------------------------------------
 * |  0  | B                  |   high   |  gpio94  |   gpio95    |     -     |
 * |  0  | B/C (B-compatible) |   high   |  gpio94  |   gpio95    |  gpio137  |
 * |  1  | C                  |   low    |  gpio138 |   gpio139   |  gpio137  |
 *  --------------------------------------------------------------------------
 */

#define IGEP2_BOARD_HWREV_B	0
#define IGEP2_BOARD_HWREV_C	1
#define IGEP3_BOARD_HWREV	2

static u8 hwrev;

static void __init igep2_get_revision(void)
{
	u8 ret;

	if (machine_is_igep0030()) {
		hwrev = IGEP3_BOARD_HWREV;
		return;
	}

	omap_mux_init_gpio(IGEP2_GPIO_LED1_RED, OMAP_PIN_INPUT);

	if (gpio_request_one(IGEP2_GPIO_LED1_RED, GPIOF_IN, "GPIO_HW0_REV")) {
		pr_warning("IGEP2: Could not obtain gpio GPIO_HW0_REV\n");
		pr_err("IGEP2: Unknown Hardware Revision\n");
		return;
	}

	ret = gpio_get_value(IGEP2_GPIO_LED1_RED);
	if (ret == 0) {
		pr_info("IGEP2: Hardware Revision C (B-NON compatible)\n");
		hwrev = IGEP2_BOARD_HWREV_C;
	} else if (ret ==  1) {
		pr_info("IGEP2: Hardware Revision B/C (B compatible)\n");
		hwrev = IGEP2_BOARD_HWREV_B;
	} else {
		pr_err("IGEP2: Unknown Hardware Revision\n");
		hwrev = -1;
	}

	gpio_free(IGEP2_GPIO_LED1_RED);
}

#if defined(CONFIG_MTD_ONENAND_OMAP2) ||		\
	defined(CONFIG_MTD_ONENAND_OMAP2_MODULE) ||	\
	defined(CONFIG_MTD_NAND_OMAP2) ||		\
	defined(CONFIG_MTD_NAND_OMAP2_MODULE)

#define ONENAND_MAP             0x20000000

/* NAND04GR4E1A ( x2 Flash built-in COMBO POP MEMORY )
 * Since the device is equipped with two DataRAMs, and two-plane NAND
 * Flash memory array, these two component enables simultaneous program
 * of 4KiB. Plane1 has only even blocks such as block0, block2, block4
 * while Plane2 has only odd blocks such as block1, block3, block5.
 * So MTD regards it as 4KiB page size and 256KiB block size 64*(2*2048)
 */

static struct mtd_partition igep_flash_partitions[] = {
	{
		.name           = "X-Loader",
		.offset         = 0,
		.size           = 2 * (64*(2*2048))
	},
	{
		.name           = "U-Boot",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 6 * (64*(2*2048)),
	},
	{
		.name           = "Environment",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 2 * (64*(2*2048)),
	},
	{
		.name           = "Kernel",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 12 * (64*(2*2048)),
	},
	{
		.name           = "File System",
		.offset         = MTDPART_OFS_APPEND,
		.size           = MTDPART_SIZ_FULL,
	},
};

static inline u32 igep_get_sysboot_value(void)
{
	return omap_ctrl_readl(OMAP343X_CONTROL_STATUS) & IGEP_SYSBOOT_MASK;
}

static void __init igep_flash_init(void)
{
	u32 mux;
	mux = igep_get_sysboot_value();

	if (mux == IGEP_SYSBOOT_NAND) {
		pr_info("IGEP: initializing NAND memory device\n");
		board_nand_init(igep_flash_partitions,
				ARRAY_SIZE(igep_flash_partitions),
				0, NAND_BUSWIDTH_16, nand_default_timings);
	} else if (mux == IGEP_SYSBOOT_ONENAND) {
		pr_info("IGEP: initializing OneNAND memory device\n");
		board_onenand_init(igep_flash_partitions,
				   ARRAY_SIZE(igep_flash_partitions), 0);
	} else {
		pr_err("IGEP: Flash: unsupported sysboot sequence found\n");
	}
}

#else
static void __init igep_flash_init(void) {}
#endif

#if defined(CONFIG_SMSC911X) || defined(CONFIG_SMSC911X_MODULE)

#include <linux/smsc911x.h>
#include "gpmc-smsc911x.h"

static struct omap_smsc911x_platform_data smsc911x_cfg = {
	.cs             = IGEP2_SMSC911X_CS,
	.gpio_irq       = IGEP2_SMSC911X_GPIO,
	.gpio_reset     = -EINVAL,
	.flags		= SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS,
};

static inline void __init igep2_init_smsc911x(void)
{
	gpmc_smsc911x_init(&smsc911x_cfg);
}

#else
static inline void __init igep2_init_smsc911x(void) { }
#endif

static struct regulator_consumer_supply igep_vmmc1_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.0"),
};

/* VMMC1 for OMAP VDD_MMC1 (i/o) and MMC1 card */
static struct regulator_init_data igep_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(igep_vmmc1_supply),
	.consumer_supplies      = igep_vmmc1_supply,
};

static struct regulator_consumer_supply igep_vio_supply[] = {
	REGULATOR_SUPPLY("vmmc_aux", "omap_hsmmc.1"),
};

static struct regulator_init_data igep_vio = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= 1,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(igep_vio_supply),
	.consumer_supplies      = igep_vio_supply,
};

static struct regulator_consumer_supply igep_vmmc2_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.1"),
};

static struct regulator_init_data igep_vmmc2 = {
	.constraints		= {
		.valid_modes_mask	= REGULATOR_MODE_NORMAL,
		.always_on		= 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(igep_vmmc2_supply),
	.consumer_supplies	= igep_vmmc2_supply,
};

static struct fixed_voltage_config igep_vwlan = {
	.supply_name		= "vwlan",
	.microvolts		= 3300000,
	.gpio			= -EINVAL,
	.enabled_at_boot	= 1,
	.init_data		= &igep_vmmc2,
};

static struct platform_device igep_vwlan_device = {
	.name		= "reg-fixed-voltage",
	.id		= 0,
	.dev = {
		.platform_data	= &igep_vwlan,
	},
};

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
		.deferred	= true,
	},
#if defined(CONFIG_LIBERTAS_SDIO) || defined(CONFIG_LIBERTAS_SDIO_MODULE)
	{
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
#endif
	{}      /* Terminator */
};

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
#include <linux/leds.h>

static struct gpio_led igep_gpio_leds[] = {
	[0] = {
		.name			= "gpio-led:red:d0",
		.default_trigger	= "default-off"
	},
	[1] = {
		.name			= "gpio-led:green:d0",
		.default_trigger	= "default-off",
	},
	[2] = {
		.name			= "gpio-led:red:d1",
		.default_trigger	= "default-off",
	},
	[3] = {
		.name			= "gpio-led:green:d1",
		.default_trigger	= "heartbeat",
		.gpio			= -EINVAL, /* gets replaced */
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data igep_led_pdata = {
	.leds           = igep_gpio_leds,
	.num_leds       = ARRAY_SIZE(igep_gpio_leds),
};

static struct platform_device igep_led_device = {
	 .name   = "leds-gpio",
	 .id     = -1,
	 .dev    = {
		 .platform_data  =  &igep_led_pdata,
	},
};

static void __init igep_leds_init(void)
{
	if (machine_is_igep0020()) {
		igep_gpio_leds[0].gpio = IGEP2_GPIO_LED0_RED;
		igep_gpio_leds[1].gpio = IGEP2_GPIO_LED0_GREEN;
		igep_gpio_leds[2].gpio = IGEP2_GPIO_LED1_RED;
	} else {
		igep_gpio_leds[0].gpio = IGEP3_GPIO_LED0_RED;
		igep_gpio_leds[1].gpio = IGEP3_GPIO_LED0_GREEN;
		igep_gpio_leds[2].gpio = IGEP3_GPIO_LED1_RED;
	}

	platform_device_register(&igep_led_device);
}

#else
static struct gpio igep_gpio_leds[] __initdata = {
	{ -EINVAL,	GPIOF_OUT_INIT_LOW, "gpio-led:red:d0"   },
	{ -EINVAL,	GPIOF_OUT_INIT_LOW, "gpio-led:green:d0" },
	{ -EINVAL,	GPIOF_OUT_INIT_LOW, "gpio-led:red:d1"   },
};

static inline void igep_leds_init(void)
{
	int i;

	if (machine_is_igep0020()) {
		igep_gpio_leds[0].gpio = IGEP2_GPIO_LED0_RED;
		igep_gpio_leds[1].gpio = IGEP2_GPIO_LED0_GREEN;
		igep_gpio_leds[2].gpio = IGEP2_GPIO_LED1_RED;
	} else {
		igep_gpio_leds[0].gpio = IGEP3_GPIO_LED0_RED;
		igep_gpio_leds[1].gpio = IGEP3_GPIO_LED0_GREEN;
		igep_gpio_leds[2].gpio = IGEP3_GPIO_LED1_RED;
	}

	if (gpio_request_array(igep_gpio_leds, ARRAY_SIZE(igep_gpio_leds))) {
		pr_warning("IGEP v2: Could not obtain leds gpios\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(igep_gpio_leds); i++)
		gpio_export(igep_gpio_leds[i].gpio, 0);
}
#endif

static struct gpio igep2_twl_gpios[] = {
	{ -EINVAL, GPIOF_IN,		"GPIO_EHCI_NOC"  },
	{ -EINVAL, GPIOF_OUT_INIT_LOW,	"GPIO_USBH_CPEN" },
};

static int igep_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	int ret;

	/* gpio + 0 is "mmc0_cd" (input/IRQ) */
	mmc[0].gpio_cd = gpio + 0;
	omap_hsmmc_late_init(mmc);

	/* TWL4030_GPIO_MAX + 1 == ledB (out, active low LED) */
#if !defined(CONFIG_LEDS_GPIO) && !defined(CONFIG_LEDS_GPIO_MODULE)
	ret = gpio_request_one(gpio + TWL4030_GPIO_MAX + 1, GPIOF_OUT_INIT_HIGH,
			       "gpio-led:green:d1");
	if (ret == 0)
		gpio_export(gpio + TWL4030_GPIO_MAX + 1, 0);
	else
		pr_warning("IGEP: Could not obtain gpio GPIO_LED1_GREEN\n");
#else
	igep_gpio_leds[3].gpio = gpio + TWL4030_GPIO_MAX + 1;
#endif

	if (machine_is_igep0030())
		return 0;

	/*
	 * REVISIT: need ehci-omap hooks for external VBUS
	 * power switch and overcurrent detect
	 */
	igep2_twl_gpios[0].gpio = gpio + 1;

	/* TWL4030_GPIO_MAX + 0 == ledA, GPIO_USBH_CPEN (out, active low) */
	igep2_twl_gpios[1].gpio = gpio + TWL4030_GPIO_MAX;

	ret = gpio_request_array(igep2_twl_gpios, ARRAY_SIZE(igep2_twl_gpios));
	if (ret < 0)
		pr_err("IGEP2: Could not obtain gpio for USBH_CPEN");

	return 0;
};

static struct twl4030_gpio_platform_data igep_twl4030_gpio_pdata = {
	.use_leds	= true,
	.setup		= igep_twl_gpio_setup,
};

static struct tfp410_platform_data dvi_panel = {
	.i2c_bus_num		= 3,
	.power_down_gpio	= IGEP2_GPIO_DVI_PUP,
};

static struct omap_dss_device igep2_dvi_device = {
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.name			= "dvi",
	.driver_name		= "tfp410",
	.data			= &dvi_panel,
	.phy.dpi.data_lines	= 24,
};

static struct omap_dss_device *igep2_dss_devices[] = {
	&igep2_dvi_device
};

static struct omap_dss_board_info igep2_dss_data = {
	.num_devices	= ARRAY_SIZE(igep2_dss_devices),
	.devices	= igep2_dss_devices,
	.default_device	= &igep2_dvi_device,
};

static struct platform_device *igep_devices[] __initdata = {
	&igep_vwlan_device,
};

static int igep2_keymap[] = {
	KEY(0, 0, KEY_LEFT),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_A),
	KEY(0, 3, KEY_B),
	KEY(1, 0, KEY_DOWN),
	KEY(1, 1, KEY_UP),
	KEY(1, 2, KEY_E),
	KEY(1, 3, KEY_F),
	KEY(2, 0, KEY_ENTER),
	KEY(2, 1, KEY_I),
	KEY(2, 2, KEY_J),
	KEY(2, 3, KEY_K),
	KEY(3, 0, KEY_M),
	KEY(3, 1, KEY_N),
	KEY(3, 2, KEY_O),
	KEY(3, 3, KEY_P)
};

static struct matrix_keymap_data igep2_keymap_data = {
	.keymap			= igep2_keymap,
	.keymap_size		= ARRAY_SIZE(igep2_keymap),
};

static struct twl4030_keypad_data igep2_keypad_pdata = {
	.keymap_data	= &igep2_keymap_data,
	.rows		= 4,
	.cols		= 4,
	.rep		= 1,
};

static struct twl4030_platform_data igep_twldata = {
	/* platform_data for children goes here */
	.gpio		= &igep_twl4030_gpio_pdata,
	.vmmc1          = &igep_vmmc1,
	.vio		= &igep_vio,
};

static struct i2c_board_info __initdata igep2_i2c3_boardinfo[] = {
	{
		I2C_BOARD_INFO("eeprom", 0x50),
	},
};

static void __init igep_i2c_init(void)
{
	int ret;

	omap3_pmic_get_config(&igep_twldata, TWL_COMMON_PDATA_USB,
			      TWL_COMMON_REGULATOR_VPLL2);
	igep_twldata.vpll2->constraints.apply_uV = true;
	igep_twldata.vpll2->constraints.name = "VDVI";

	if (machine_is_igep0020()) {
		/*
		 * Bus 3 is attached to the DVI port where devices like the
		 * pico DLP projector don't work reliably with 400kHz
		 */
		ret = omap_register_i2c_bus(3, 100, igep2_i2c3_boardinfo,
					    ARRAY_SIZE(igep2_i2c3_boardinfo));
		if (ret)
			pr_warning("IGEP2: Could not register I2C3 bus (%d)\n", ret);

		igep_twldata.keypad	= &igep2_keypad_pdata;
		/* Get common pmic data */
		omap3_pmic_get_config(&igep_twldata, TWL_COMMON_PDATA_AUDIO, 0);
	}

	omap3_pmic_init("twl4030", &igep_twldata);
}

static const struct usbhs_omap_board_data igep2_usbhs_bdata __initconst = {
	.port_mode[0] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[1] = OMAP_USBHS_PORT_MODE_UNUSED,
	.port_mode[2] = OMAP_USBHS_PORT_MODE_UNUSED,

	.phy_reset = true,
	.reset_gpio_port[0] = IGEP2_GPIO_USBH_NRESET,
	.reset_gpio_port[1] = -EINVAL,
	.reset_gpio_port[2] = -EINVAL,
};

static const struct usbhs_omap_board_data igep3_usbhs_bdata __initconst = {
	.port_mode[0] = OMAP_USBHS_PORT_MODE_UNUSED,
	.port_mode[1] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[2] = OMAP_USBHS_PORT_MODE_UNUSED,

	.phy_reset = true,
	.reset_gpio_port[0] = -EINVAL,
	.reset_gpio_port[1] = IGEP3_GPIO_USBH_NRESET,
	.reset_gpio_port[2] = -EINVAL,
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	/* SMSC9221 LAN Controller ETH IRQ (GPIO_176) */
	OMAP3_MUX(MCSPI1_CS2, OMAP_MUX_MODE4 | OMAP_PIN_INPUT),
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

#if defined(CONFIG_LIBERTAS_SDIO) || defined(CONFIG_LIBERTAS_SDIO_MODULE)
static struct gpio igep_wlan_bt_gpios[] __initdata = {
	{ -EINVAL, GPIOF_OUT_INIT_HIGH, "GPIO_WIFI_NPD"	   },
	{ -EINVAL, GPIOF_OUT_INIT_HIGH, "GPIO_WIFI_NRESET" },
	{ -EINVAL, GPIOF_OUT_INIT_HIGH, "GPIO_BT_NRESET"   },
};

static void __init igep_wlan_bt_init(void)
{
	int err;

	/* GPIO's for WLAN-BT combo depends on hardware revision */
	if (hwrev == IGEP2_BOARD_HWREV_B) {
		igep_wlan_bt_gpios[0].gpio = IGEP2_RB_GPIO_WIFI_NPD;
		igep_wlan_bt_gpios[1].gpio = IGEP2_RB_GPIO_WIFI_NRESET;
		igep_wlan_bt_gpios[2].gpio = IGEP2_RB_GPIO_BT_NRESET;
	} else if (hwrev == IGEP2_BOARD_HWREV_C || machine_is_igep0030()) {
		igep_wlan_bt_gpios[0].gpio = IGEP2_RC_GPIO_WIFI_NPD;
		igep_wlan_bt_gpios[1].gpio = IGEP2_RC_GPIO_WIFI_NRESET;
		igep_wlan_bt_gpios[2].gpio = IGEP2_RC_GPIO_BT_NRESET;
	} else
		return;

	/* Make sure that the GPIO pins are muxed correctly */
	omap_mux_init_gpio(igep_wlan_bt_gpios[0].gpio, OMAP_PIN_OUTPUT);
	omap_mux_init_gpio(igep_wlan_bt_gpios[1].gpio, OMAP_PIN_OUTPUT);
	omap_mux_init_gpio(igep_wlan_bt_gpios[2].gpio, OMAP_PIN_OUTPUT);

	err = gpio_request_array(igep_wlan_bt_gpios,
				 ARRAY_SIZE(igep_wlan_bt_gpios));
	if (err) {
		pr_warning("IGEP2: Could not obtain WIFI/BT gpios\n");
		return;
	}

	gpio_export(igep_wlan_bt_gpios[0].gpio, 0);
	gpio_export(igep_wlan_bt_gpios[1].gpio, 0);
	gpio_export(igep_wlan_bt_gpios[2].gpio, 0);

	gpio_set_value(igep_wlan_bt_gpios[1].gpio, 0);
	udelay(10);
	gpio_set_value(igep_wlan_bt_gpios[1].gpio, 1);

}
#else
static inline void __init igep_wlan_bt_init(void) { }
#endif

static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x.0"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x.0"),
};

static void __init igep_init(void)
{
	regulator_register_fixed(1, dummy_supplies, ARRAY_SIZE(dummy_supplies));
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);

	/* Get IGEP2 hardware revision */
	igep2_get_revision();

	omap_hsmmc_init(mmc);

	/* Register I2C busses and drivers */
	igep_i2c_init();
	platform_add_devices(igep_devices, ARRAY_SIZE(igep_devices));
	omap_serial_init();
	omap_sdrc_init(m65kxxxxam_sdrc_params,
				  m65kxxxxam_sdrc_params);
	usb_musb_init(NULL);

	igep_flash_init();
	igep_leds_init();
	omap_twl4030_audio_init("igep2");

	/*
	 * WLAN-BT combo module from MuRata which has a Marvell WLAN
	 * (88W8686) + CSR Bluetooth chipset. Uses SDIO interface.
	 */
	igep_wlan_bt_init();

	if (machine_is_igep0020()) {
		omap_display_init(&igep2_dss_data);
		igep2_init_smsc911x();
		usbhs_init(&igep2_usbhs_bdata);
	} else {
		usbhs_init(&igep3_usbhs_bdata);
	}
}

MACHINE_START(IGEP0020, "IGEP v2 board")
	.atag_offset	= 0x100,
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap35xx_init_early,
	.init_irq	= omap3_init_irq,
	.handle_irq	= omap3_intc_handle_irq,
	.init_machine	= igep_init,
	.init_late	= omap35xx_init_late,
	.init_time	= omap3_sync32k_timer_init,
	.restart	= omap3xxx_restart,
MACHINE_END

MACHINE_START(IGEP0030, "IGEP OMAP3 module")
	.atag_offset	= 0x100,
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap35xx_init_early,
	.init_irq	= omap3_init_irq,
	.handle_irq	= omap3_intc_handle_irq,
	.init_machine	= igep_init,
	.init_late	= omap35xx_init_late,
	.init_time	= omap3_sync32k_timer_init,
	.restart	= omap3xxx_restart,
MACHINE_END
