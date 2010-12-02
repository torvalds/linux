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
#include <linux/spi/ads7846.h>
#include <linux/i2c/twl.h>
#include <linux/regulator/machine.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/mcspi.h>
#include <plat/board.h>
#include <plat/usb.h>
#include <plat/common.h>
#include <plat/dma.h>
#include <plat/gpmc.h>
#include <plat/display.h>

#include <plat/gpmc-smc91x.h>

#include "board-flash.h"
#include "mux.h"
#include "sdram-qimonda-hyb18m512160af-6.h"
#include "hsmmc.h"
#include "pm.h"
#include "control.h"

#define CONFIG_DISABLE_HFCLK 1

#define SDP3430_TS_GPIO_IRQ_SDPV1	3
#define SDP3430_TS_GPIO_IRQ_SDPV2	2

#define ENABLE_VAUX3_DEDICATED	0x03
#define ENABLE_VAUX3_DEV_GRP	0x20

#define TWL4030_MSECURE_GPIO 22

/* FIXME: These values need to be updated based on more profiling on 3430sdp*/
static struct cpuidle_params omap3_cpuidle_params_table[] = {
	/* C1 */
	{1, 2, 2, 5},
	/* C2 */
	{1, 10, 10, 30},
	/* C3 */
	{1, 50, 50, 300},
	/* C4 */
	{1, 1500, 1800, 4000},
	/* C5 */
	{1, 2500, 7500, 12000},
	/* C6 */
	{1, 3000, 8500, 15000},
	/* C7 */
	{1, 10000, 30000, 300000},
};

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

static int ts_gpio;	/* Needed for ads7846_get_pendown_state */

/**
 * @brief ads7846_dev_init : Requests & sets GPIO line for pen-irq
 *
 * @return - void. If request gpio fails then Flag KERN_ERR.
 */
static void ads7846_dev_init(void)
{
	if (gpio_request(ts_gpio, "ADS7846 pendown") < 0) {
		printk(KERN_ERR "can't get ads746 pen down GPIO\n");
		return;
	}

	gpio_direction_input(ts_gpio);
	gpio_set_debounce(ts_gpio, 310);
}

static int ads7846_get_pendown_state(void)
{
	return !gpio_get_value(ts_gpio);
}

static struct ads7846_platform_data tsc2046_config __initdata = {
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
	.wakeup				= true,
};


static struct omap2_mcspi_device_config tsc2046_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,	/* 0: slave, 1: master */
};

static struct spi_board_info sdp3430_spi_board_info[] __initdata = {
	[0] = {
		/*
		 * TSC2046 operates at a max freqency of 2MHz, so
		 * operate slightly below at 1.5MHz
		 */
		.modalias		= "ads7846",
		.bus_num		= 1,
		.chip_select		= 0,
		.max_speed_hz		= 1500000,
		.controller_data	= &tsc2046_mcspi_config,
		.irq			= 0,
		.platform_data		= &tsc2046_config,
	},
};


#define SDP3430_LCD_PANEL_BACKLIGHT_GPIO	8
#define SDP3430_LCD_PANEL_ENABLE_GPIO		5

static unsigned backlight_gpio;
static unsigned enable_gpio;
static int lcd_enabled;
static int dvi_enabled;

static void __init sdp3430_display_init(void)
{
	int r;

	enable_gpio    = SDP3430_LCD_PANEL_ENABLE_GPIO;
	backlight_gpio = SDP3430_LCD_PANEL_BACKLIGHT_GPIO;

	r = gpio_request(enable_gpio, "LCD reset");
	if (r) {
		printk(KERN_ERR "failed to get LCD reset GPIO\n");
		goto err0;
	}

	r = gpio_request(backlight_gpio, "LCD Backlight");
	if (r) {
		printk(KERN_ERR "failed to get LCD backlight GPIO\n");
		goto err1;
	}

	gpio_direction_output(enable_gpio, 0);
	gpio_direction_output(backlight_gpio, 0);

	return;
err1:
	gpio_free(enable_gpio);
err0:
	return;
}

static int sdp3430_panel_enable_lcd(struct omap_dss_device *dssdev)
{
	if (dvi_enabled) {
		printk(KERN_ERR "cannot enable LCD, DVI is enabled\n");
		return -EINVAL;
	}

	gpio_direction_output(enable_gpio, 1);
	gpio_direction_output(backlight_gpio, 1);

	lcd_enabled = 1;

	return 0;
}

static void sdp3430_panel_disable_lcd(struct omap_dss_device *dssdev)
{
	lcd_enabled = 0;

	gpio_direction_output(enable_gpio, 0);
	gpio_direction_output(backlight_gpio, 0);
}

static int sdp3430_panel_enable_dvi(struct omap_dss_device *dssdev)
{
	if (lcd_enabled) {
		printk(KERN_ERR "cannot enable DVI, LCD is enabled\n");
		return -EINVAL;
	}

	dvi_enabled = 1;

	return 0;
}

static void sdp3430_panel_disable_dvi(struct omap_dss_device *dssdev)
{
	dvi_enabled = 0;
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

static struct omap_dss_device sdp3430_dvi_device = {
	.name			= "dvi",
	.driver_name		= "generic_panel",
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.phy.dpi.data_lines	= 24,
	.platform_enable	= sdp3430_panel_enable_dvi,
	.platform_disable	= sdp3430_panel_disable_dvi,
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

static struct platform_device sdp3430_dss_device = {
	.name		= "omapdss",
	.id		= -1,
	.dev		= {
		.platform_data = &sdp3430_dss_data,
	},
};

static struct regulator_consumer_supply sdp3430_vdda_dac_supply = {
	.supply		= "vdda_dac",
	.dev		= &sdp3430_dss_device.dev,
};

static struct platform_device *sdp3430_devices[] __initdata = {
	&sdp3430_dss_device,
};

static struct omap_board_config_kernel sdp3430_config[] __initdata = {
};

static void __init omap_3430sdp_init_irq(void)
{
	omap_board_config = sdp3430_config;
	omap_board_config_size = ARRAY_SIZE(sdp3430_config);
	omap3_pm_init_cpuidle(omap3_cpuidle_params_table);
	omap2_init_common_hw(hyb18m512160af6_sdrc_params, NULL);
	omap_init_irq();
	omap_gpio_init();
}

static int sdp3430_batt_table[] = {
/* 0 C*/
30800, 29500, 28300, 27100,
26000, 24900, 23900, 22900, 22000, 21100, 20300, 19400, 18700, 17900,
17200, 16500, 15900, 15300, 14700, 14100, 13600, 13100, 12600, 12100,
11600, 11200, 10800, 10400, 10000, 9630,   9280,   8950,   8620,   8310,
8020,   7730,   7460,   7200,   6950,   6710,   6470,   6250,   6040,   5830,
5640,   5450,   5260,   5090,   4920,   4760,   4600,   4450,   4310,   4170,
4040,   3910,   3790,   3670,   3550
};

static struct twl4030_bci_platform_data sdp3430_bci_data = {
	.battery_tmp_tbl	= sdp3430_batt_table,
	.tblsize		= ARRAY_SIZE(sdp3430_batt_table),
};

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		/* 8 bits (default) requires S6.3 == ON,
		 * so the SIM card isn't used; else 4 bits.
		 */
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
		.gpio_wp	= 4,
	},
	{
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
		.gpio_wp	= 7,
	},
	{}	/* Terminator */
};

static struct regulator_consumer_supply sdp3430_vmmc1_supply = {
	.supply			= "vmmc",
};

static struct regulator_consumer_supply sdp3430_vsim_supply = {
	.supply			= "vmmc_aux",
};

static struct regulator_consumer_supply sdp3430_vmmc2_supply = {
	.supply			= "vmmc",
};

static int sdp3430_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	/* gpio + 0 is "mmc0_cd" (input/IRQ),
	 * gpio + 1 is "mmc1_cd" (input/IRQ)
	 */
	mmc[0].gpio_cd = gpio + 0;
	mmc[1].gpio_cd = gpio + 1;
	omap2_hsmmc_init(mmc);

	/* link regulators to MMC adapters ... we "know" the
	 * regulators will be set up only *after* we return.
	 */
	sdp3430_vmmc1_supply.dev = mmc[0].dev;
	sdp3430_vsim_supply.dev = mmc[0].dev;
	sdp3430_vmmc2_supply.dev = mmc[1].dev;

	/* gpio + 7 is "sub_lcd_en_bkl" (output/PWM1) */
	gpio_request(gpio + 7, "sub_lcd_en_bkl");
	gpio_direction_output(gpio + 7, 0);

	/* gpio + 15 is "sub_lcd_nRST" (output) */
	gpio_request(gpio + 15, "sub_lcd_nRST");
	gpio_direction_output(gpio + 15, 0);

	return 0;
}

static struct twl4030_gpio_platform_data sdp3430_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.pulldowns	= BIT(2) | BIT(6) | BIT(8) | BIT(13)
				| BIT(16) | BIT(17),
	.setup		= sdp3430_twl_gpio_setup,
};

static struct twl4030_usb_data sdp3430_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static struct twl4030_madc_platform_data sdp3430_madc_data = {
	.irq_line	= 1,
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
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &sdp3430_vmmc1_supply,
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
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &sdp3430_vmmc2_supply,
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
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &sdp3430_vsim_supply,
};

/* VDAC for DSS driving S-Video */
static struct regulator_init_data sdp3430_vdac = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &sdp3430_vdda_dac_supply,
};

/* VPLL2 for digital video outputs */
static struct regulator_consumer_supply sdp3430_vpll2_supplies[] = {
	{
		.supply		= "vdds_dsi",
		.dev		= &sdp3430_dss_device.dev,
	}
};

static struct regulator_init_data sdp3430_vpll2 = {
	.constraints = {
		.name			= "VDVI",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(sdp3430_vpll2_supplies),
	.consumer_supplies	= sdp3430_vpll2_supplies,
};

static struct twl4030_codec_audio_data sdp3430_audio = {
	.audio_mclk = 26000000,
};

static struct twl4030_codec_data sdp3430_codec = {
	.audio_mclk = 26000000,
	.audio = &sdp3430_audio,
};

static struct twl4030_platform_data sdp3430_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.bci		= &sdp3430_bci_data,
	.gpio		= &sdp3430_gpio_data,
	.madc		= &sdp3430_madc_data,
	.keypad		= &sdp3430_kp_data,
	.usb		= &sdp3430_usb_data,
	.codec		= &sdp3430_codec,

	.vaux1		= &sdp3430_vaux1,
	.vaux2		= &sdp3430_vaux2,
	.vaux3		= &sdp3430_vaux3,
	.vaux4		= &sdp3430_vaux4,
	.vmmc1		= &sdp3430_vmmc1,
	.vmmc2		= &sdp3430_vmmc2,
	.vsim		= &sdp3430_vsim,
	.vdac		= &sdp3430_vdac,
	.vpll2		= &sdp3430_vpll2,
};

static struct i2c_board_info __initdata sdp3430_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &sdp3430_twldata,
	},
};

static int __init omap3430_i2c_init(void)
{
	/* i2c1 for PMIC only */
	omap_register_i2c_bus(1, 2600, sdp3430_i2c_boardinfo,
			ARRAY_SIZE(sdp3430_i2c_boardinfo));
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

static const struct ehci_hcd_omap_platform_data ehci_pdata __initconst = {

	.port_mode[0] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[1] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[2] = EHCI_HCD_OMAP_MODE_UNKNOWN,

	.phy_reset  = true,
	.reset_gpio_port[0]  = 57,
	.reset_gpio_port[1]  = 61,
	.reset_gpio_port[2]  = -EINVAL
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
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

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_OTG,
	.power			= 100,
};

static void __init omap_3430sdp_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	omap3430_i2c_init();
	platform_add_devices(sdp3430_devices, ARRAY_SIZE(sdp3430_devices));
	if (omap_rev() > OMAP3430_REV_ES1_0)
		ts_gpio = SDP3430_TS_GPIO_IRQ_SDPV2;
	else
		ts_gpio = SDP3430_TS_GPIO_IRQ_SDPV1;
	sdp3430_spi_board_info[0].irq = gpio_to_irq(ts_gpio);
	spi_register_board_info(sdp3430_spi_board_info,
				ARRAY_SIZE(sdp3430_spi_board_info));
	ads7846_dev_init();
	omap_serial_init();
	usb_musb_init(&musb_board_data);
	board_smc91x_init();
	board_flash_init(sdp_flash_partitions, chip_sel_3430);
	sdp3430_display_init();
	enable_board_wakeup_source();
	usb_ehci_init(&ehci_pdata);
}

MACHINE_START(OMAP_3430SDP, "OMAP3430 3430SDP board")
	/* Maintainer: Syed Khasim - Texas Instruments Inc */
	.boot_params	= 0x80000100,
	.map_io		= omap3_map_io,
	.reserve	= omap_reserve,
	.init_irq	= omap_3430sdp_init_irq,
	.init_machine	= omap_3430sdp_init,
	.timer		= &omap_timer,
MACHINE_END
