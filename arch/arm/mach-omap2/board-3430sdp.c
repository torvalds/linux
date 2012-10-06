/*
 * linux/arch/arm/mach-omap2/board-3430sdp.c
 *
 * Copyright (C) 2007 Texas Instruments
 *
 * Modified from mach-omap2/board-generic.c
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
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/spi/spi.h>
#include <linux/i2c/twl.h>
#include <linux/regulator/machine.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>
#include <linux/platform_data/spi-omap2-mcspi.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/usb.h>
#include "common.h"
#include <plat/dma.h>
#include <plat/gpmc.h>
#include <video/omapdss.h>
#include <video/omap-panel-tfp410.h>

#include "gpmc-smc91x.h"

#include "board-flash.h"
#include "mux.h"
#include "sdram-qimonda-hyb18m512160af-6.h"
#include "hsmmc.h"
#include "pm.h"
#include "control.h"
#include "common-board-devices.h"

#define CONFIG_DISABLE_HFCLK 1

#define SDP3430_TS_GPIO_IRQ_SDPV1	3
#define SDP3430_TS_GPIO_IRQ_SDPV2	2

#define ENABLE_VAUX3_DEDICATED	0x03
#define ENABLE_VAUX3_DEV_GRP	0x20

#define TWL4030_MSECURE_GPIO 22

static uint32_t board_keymap[] = {
	KEY(0, 0, KEY_LEFT),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_A),
	KEY(0, 3, KEY_B),
	KEY(0, 4, KEY_C),
	KEY(1, 0, KEY_DOWN),
	KEY(1, 1, KEY_UP),
	KEY(1, 2, KEY_E),
	KEY(1, 3, KEY_F),
	KEY(1, 4, KEY_G),
	KEY(2, 0, KEY_ENTER),
	KEY(2, 1, KEY_I),
	KEY(2, 2, KEY_J),
	KEY(2, 3, KEY_K),
	KEY(2, 4, KEY_3),
	KEY(3, 0, KEY_M),
	KEY(3, 1, KEY_N),
	KEY(3, 2, KEY_O),
	KEY(3, 3, KEY_P),
	KEY(3, 4, KEY_Q),
	KEY(4, 0, KEY_R),
	KEY(4, 1, KEY_4),
	KEY(4, 2, KEY_T),
	KEY(4, 3, KEY_U),
	KEY(4, 4, KEY_D),
	KEY(5, 0, KEY_V),
	KEY(5, 1, KEY_W),
	KEY(5, 2, KEY_L),
	KEY(5, 3, KEY_S),
	KEY(5, 4, KEY_H),
	0
};

static struct matrix_keymap_data board_map_data = {
	.keymap			= board_keymap,
	.keymap_size		= ARRAY_SIZE(board_keymap),
};

static struct twl4030_keypad_data sdp3430_kp_data = {
	.keymap_data	= &board_map_data,
	.rows		= 5,
	.cols		= 6,
	.rep		= 1,
};

#define SDP3430_LCD_PANEL_BACKLIGHT_GPIO	8
#define SDP3430_LCD_PANEL_ENABLE_GPIO		5

static struct gpio sdp3430_dss_gpios[] __initdata = {
	{SDP3430_LCD_PANEL_ENABLE_GPIO,    GPIOF_OUT_INIT_LOW, "LCD reset"    },
	{SDP3430_LCD_PANEL_BACKLIGHT_GPIO, GPIOF_OUT_INIT_LOW, "LCD Backlight"},
};

static void __init sdp3430_display_init(void)
{
	int r;

	r = gpio_request_array(sdp3430_dss_gpios,
			       ARRAY_SIZE(sdp3430_dss_gpios));
	if (r)
		printk(KERN_ERR "failed to get LCD control GPIOs\n");

}

static int sdp3430_panel_enable_lcd(struct omap_dss_device *dssdev)
{
	gpio_direction_output(SDP3430_LCD_PANEL_ENABLE_GPIO, 1);
	gpio_direction_output(SDP3430_LCD_PANEL_BACKLIGHT_GPIO, 1);

	return 0;
}

static void sdp3430_panel_disable_lcd(struct omap_dss_device *dssdev)
{
	gpio_direction_output(SDP3430_LCD_PANEL_ENABLE_GPIO, 0);
	gpio_direction_output(SDP3430_LCD_PANEL_BACKLIGHT_GPIO, 0);
}

static int sdp3430_panel_enable_tv(struct omap_dss_device *dssdev)
{
	return 0;
}

static void sdp3430_panel_disable_tv(struct omap_dss_device *dssdev)
{
}


static struct omap_dss_device sdp3430_lcd_device = {
	.name			= "lcd",
	.driver_name		= "sharp_ls_panel",
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.phy.dpi.data_lines	= 16,
	.platform_enable	= sdp3430_panel_enable_lcd,
	.platform_disable	= sdp3430_panel_disable_lcd,
};

static struct tfp410_platform_data dvi_panel = {
	.power_down_gpio	= -1,
};

static struct omap_dss_device sdp3430_dvi_device = {
	.name			= "dvi",
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.driver_name		= "tfp410",
	.data			= &dvi_panel,
	.phy.dpi.data_lines	= 24,
};

static struct omap_dss_device sdp3430_tv_device = {
	.name			= "tv",
	.driver_name		= "venc",
	.type			= OMAP_DISPLAY_TYPE_VENC,
	.phy.venc.type		= OMAP_DSS_VENC_TYPE_SVIDEO,
	.platform_enable	= sdp3430_panel_enable_tv,
	.platform_disable	= sdp3430_panel_disable_tv,
};


static struct omap_dss_device *sdp3430_dss_devices[] = {
	&sdp3430_lcd_device,
	&sdp3430_dvi_device,
	&sdp3430_tv_device,
};

static struct omap_dss_board_info sdp3430_dss_data = {
	.num_devices	= ARRAY_SIZE(sdp3430_dss_devices),
	.devices	= sdp3430_dss_devices,
	.default_device	= &sdp3430_lcd_device,
};

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		/* 8 bits (default) requires S6.3 == ON,
		 * so the SIM card isn't used; else 4 bits.
		 */
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
		.gpio_wp	= 4,
		.deferred	= true,
	},
	{
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
		.gpio_wp	= 7,
		.deferred	= true,
	},
	{}	/* Terminator */
};

static int sdp3430_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	/* gpio + 0 is "mmc0_cd" (input/IRQ),
	 * gpio + 1 is "mmc1_cd" (input/IRQ)
	 */
	mmc[0].gpio_cd = gpio + 0;
	mmc[1].gpio_cd = gpio + 1;
	omap_hsmmc_late_init(mmc);

	/* gpio + 7 is "sub_lcd_en_bkl" (output/PWM1) */
	gpio_request_one(gpio + 7, GPIOF_OUT_INIT_LOW, "sub_lcd_en_bkl");

	/* gpio + 15 is "sub_lcd_nRST" (output) */
	gpio_request_one(gpio + 15, GPIOF_OUT_INIT_LOW, "sub_lcd_nRST");

	return 0;
}

static struct twl4030_gpio_platform_data sdp3430_gpio_data = {
	.pulldowns	= BIT(2) | BIT(6) | BIT(8) | BIT(13)
				| BIT(16) | BIT(17),
	.setup		= sdp3430_twl_gpio_setup,
};

/* regulator consumer mappings */

/* ads7846 on SPI */
static struct regulator_consumer_supply sdp3430_vaux3_supplies[] = {
	REGULATOR_SUPPLY("vcc", "spi1.0"),
};

static struct regulator_consumer_supply sdp3430_vmmc1_supplies[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.0"),
};

static struct regulator_consumer_supply sdp3430_vsim_supplies[] = {
	REGULATOR_SUPPLY("vmmc_aux", "omap_hsmmc.0"),
};

static struct regulator_consumer_supply sdp3430_vmmc2_supplies[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.1"),
};

/*
 * Apply all the fixed voltages since most versions of U-Boot
 * don't bother with that initialization.
 */

/* VAUX1 for mainboard (irda and sub-lcd) */
static struct regulator_init_data sdp3430_vaux1 = {
	.constraints = {
		.min_uV			= 2800000,
		.max_uV			= 2800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

/* VAUX2 for camera module */
static struct regulator_init_data sdp3430_vaux2 = {
	.constraints = {
		.min_uV			= 2800000,
		.max_uV			= 2800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

/* VAUX3 for LCD board */
static struct regulator_init_data sdp3430_vaux3 = {
	.constraints = {
		.min_uV			= 2800000,
		.max_uV			= 2800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies		= ARRAY_SIZE(sdp3430_vaux3_supplies),
	.consumer_supplies		= sdp3430_vaux3_supplies,
};

/* VAUX4 for OMAP VDD_CSI2 (camera) */
static struct regulator_init_data sdp3430_vaux4 = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

/* VMMC1 for OMAP VDD_MMC1 (i/o) and MMC1 card */
static struct regulator_init_data sdp3430_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(sdp3430_vmmc1_supplies),
	.consumer_supplies	= sdp3430_vmmc1_supplies,
};

/* VMMC2 for MMC2 card */
static struct regulator_init_data sdp3430_vmmc2 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 1850000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(sdp3430_vmmc2_supplies),
	.consumer_supplies	= sdp3430_vmmc2_supplies,
};

/* VSIM for OMAP VDD_MMC1A (i/o for DAT4..DAT7) */
static struct regulator_init_data sdp3430_vsim = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 3000000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(sdp3430_vsim_supplies),
	.consumer_supplies	= sdp3430_vsim_supplies,
};

static struct twl4030_platform_data sdp3430_twldata = {
	/* platform_data for children goes here */
	.gpio		= &sdp3430_gpio_data,
	.keypad		= &sdp3430_kp_data,

	.vaux1		= &sdp3430_vaux1,
	.vaux2		= &sdp3430_vaux2,
	.vaux3		= &sdp3430_vaux3,
	.vaux4		= &sdp3430_vaux4,
	.vmmc1		= &sdp3430_vmmc1,
	.vmmc2		= &sdp3430_vmmc2,
	.vsim		= &sdp3430_vsim,
};

static int __init omap3430_i2c_init(void)
{
	/* i2c1 for PMIC only */
	omap3_pmic_get_config(&sdp3430_twldata,
			TWL_COMMON_PDATA_USB | TWL_COMMON_PDATA_BCI |
			TWL_COMMON_PDATA_MADC | TWL_COMMON_PDATA_AUDIO,
			TWL_COMMON_REGULATOR_VDAC | TWL_COMMON_REGULATOR_VPLL2);
	sdp3430_twldata.vdac->constraints.apply_uV = true;
	sdp3430_twldata.vpll2->constraints.apply_uV = true;
	sdp3430_twldata.vpll2->constraints.name = "VDVI";

	omap3_pmic_init("twl4030", &sdp3430_twldata);

	/* i2c2 on camera connector (for sensor control) and optional isp1301 */
	omap_register_i2c_bus(2, 400, NULL, 0);
	/* i2c3 on display connector (for DVI, tfp410) */
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)

static struct omap_smc91x_platform_data board_smc91x_data = {
	.cs		= 3,
	.flags		= GPMC_MUX_ADD_DATA | GPMC_TIMINGS_SMC91C96 |
				IORESOURCE_IRQ_LOWLEVEL,
};

static void __init board_smc91x_init(void)
{
	if (omap_rev() > OMAP3430_REV_ES1_0)
		board_smc91x_data.gpio_irq = 6;
	else
		board_smc91x_data.gpio_irq = 29;

	gpmc_smc91x_init(&board_smc91x_data);
}

#else

static inline void board_smc91x_init(void)
{
}

#endif

static void enable_board_wakeup_source(void)
{
	/* T2 interrupt line (keypad) */
	omap_mux_init_signal("sys_nirq",
		OMAP_WAKEUP_EN | OMAP_PIN_INPUT_PULLUP);
}

static const struct usbhs_omap_board_data usbhs_bdata __initconst = {

	.port_mode[0] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[1] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[2] = OMAP_USBHS_PORT_MODE_UNUSED,

	.phy_reset  = true,
	.reset_gpio_port[0]  = 57,
	.reset_gpio_port[1]  = 61,
	.reset_gpio_port[2]  = -EINVAL
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#else
#define board_mux	NULL
#endif

/*
 * SDP3430 V2 Board CS organization
 * Different from SDP3430 V1. Now 4 switches used to specify CS
 *
 * See also the Switch S8 settings in the comments.
 */
static char chip_sel_3430[][GPMC_CS_NUM] = {
	{PDC_NOR, PDC_NAND, PDC_ONENAND, DBG_MPDB, 0, 0, 0, 0}, /* S8:1111 */
	{PDC_ONENAND, PDC_NAND, PDC_NOR, DBG_MPDB, 0, 0, 0, 0}, /* S8:1110 */
	{PDC_NAND, PDC_ONENAND, PDC_NOR, DBG_MPDB, 0, 0, 0, 0}, /* S8:1101 */
};

static struct mtd_partition sdp_nor_partitions[] = {
	/* bootloader (U-Boot, etc) in first sector */
	{
		.name		= "Bootloader-NOR",
		.offset		= 0,
		.size		= SZ_256K,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	},
	/* bootloader params in the next sector */
	{
		.name		= "Params-NOR",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_256K,
		.mask_flags	= 0,
	},
	/* kernel */
	{
		.name		= "Kernel-NOR",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_2M,
		.mask_flags	= 0
	},
	/* file system */
	{
		.name		= "Filesystem-NOR",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0
	}
};

static struct mtd_partition sdp_onenand_partitions[] = {
	{
		.name		= "X-Loader-OneNAND",
		.offset		= 0,
		.size		= 4 * (64 * 2048),
		.mask_flags	= MTD_WRITEABLE  /* force read-only */
	},
	{
		.name		= "U-Boot-OneNAND",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 2 * (64 * 2048),
		.mask_flags	= MTD_WRITEABLE  /* force read-only */
	},
	{
		.name		= "U-Boot Environment-OneNAND",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 1 * (64 * 2048),
	},
	{
		.name		= "Kernel-OneNAND",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 16 * (64 * 2048),
	},
	{
		.name		= "File System-OneNAND",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct mtd_partition sdp_nand_partitions[] = {
	/* All the partition sizes are listed in terms of NAND block size */
	{
		.name		= "X-Loader-NAND",
		.offset		= 0,
		.size		= 4 * (64 * 2048),
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot-NAND",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size		= 10 * (64 * 2048),
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "Boot Env-NAND",

		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x1c0000 */
		.size		= 6 * (64 * 2048),
	},
	{
		.name		= "Kernel-NAND",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x280000 */
		.size		= 40 * (64 * 2048),
	},
	{
		.name		= "File System - NAND",
		.size		= MTDPART_SIZ_FULL,
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x780000 */
	},
};

static struct flash_partitions sdp_flash_partitions[] = {
	{
		.parts = sdp_nor_partitions,
		.nr_parts = ARRAY_SIZE(sdp_nor_partitions),
	},
	{
		.parts = sdp_onenand_partitions,
		.nr_parts = ARRAY_SIZE(sdp_onenand_partitions),
	},
	{
		.parts = sdp_nand_partitions,
		.nr_parts = ARRAY_SIZE(sdp_nand_partitions),
	},
};

static void __init omap_3430sdp_init(void)
{
	int gpio_pendown;

	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	omap_hsmmc_init(mmc);
	omap3430_i2c_init();
	omap_display_init(&sdp3430_dss_data);
	if (omap_rev() > OMAP3430_REV_ES1_0)
		gpio_pendown = SDP3430_TS_GPIO_IRQ_SDPV2;
	else
		gpio_pendown = SDP3430_TS_GPIO_IRQ_SDPV1;
	omap_ads7846_init(1, gpio_pendown, 310, NULL);
	omap_serial_init();
	omap_sdrc_init(hyb18m512160af6_sdrc_params, NULL);
	usb_musb_init(NULL);
	board_smc91x_init();
	board_flash_init(sdp_flash_partitions, chip_sel_3430, 0);
	sdp3430_display_init();
	enable_board_wakeup_source();
	usbhs_init(&usbhs_bdata);
}

MACHINE_START(OMAP_3430SDP, "OMAP3430 3430SDP board")
	/* Maintainer: Syed Khasim - Texas Instruments Inc */
	.atag_offset	= 0x100,
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3430_init_early,
	.init_irq	= omap3_init_irq,
	.handle_irq	= omap3_intc_handle_irq,
	.init_machine	= omap_3430sdp_init,
	.init_late	= omap3430_init_late,
	.timer		= &omap3_timer,
	.restart	= omap_prcm_restart,
MACHINE_END
