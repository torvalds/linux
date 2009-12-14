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
#include <linux/i2c/twl4030.h>
#include <linux/regulator/machine.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/mcspi.h>
#include <mach/mux.h>
#include <mach/board.h>
#include <mach/usb.h>
#include <mach/common.h>
#include <mach/dma.h>
#include <mach/gpmc.h>

#include <mach/control.h>
#include <mach/gpmc-smc91x.h>

#include "sdram-qimonda-hyb18m512160af-6.h"
#include "mmc-twl4030.h"

#define CONFIG_DISABLE_HFCLK 1

#define SDP3430_TS_GPIO_IRQ_SDPV1	3
#define SDP3430_TS_GPIO_IRQ_SDPV2	2

#define ENABLE_VAUX3_DEDICATED	0x03
#define ENABLE_VAUX3_DEV_GRP	0x20

#define TWL4030_MSECURE_GPIO 22

static int board_keymap[] = {
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

	omap_set_gpio_debounce(ts_gpio, 1);
	omap_set_gpio_debounce_time(ts_gpio, 0xa);
}

static int ads7846_get_pendown_state(void)
{
	return !gpio_get_value(ts_gpio);
}

static struct ads7846_platform_data tsc2046_config __initdata = {
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
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

static struct platform_device sdp3430_lcd_device = {
	.name		= "sdp2430_lcd",
	.id		= -1,
};

static struct regulator_consumer_supply sdp3430_vdac_supply = {
	.supply		= "vdac",
	.dev		= &sdp3430_lcd_device.dev,
};

static struct regulator_consumer_supply sdp3430_vdvi_supply = {
	.supply		= "vdvi",
	.dev		= &sdp3430_lcd_device.dev,
};

static struct platform_device *sdp3430_devices[] __initdata = {
	&sdp3430_lcd_device,
};

static struct omap_lcd_config sdp3430_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel sdp3430_config[] __initdata = {
	{ OMAP_TAG_LCD,		&sdp3430_lcd_config },
};

static void __init omap_3430sdp_init_irq(void)
{
	omap_board_config = sdp3430_config;
	omap_board_config_size = ARRAY_SIZE(sdp3430_config);
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

static struct twl4030_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		/* 8 bits (default) requires S6.3 == ON,
		 * so the SIM card isn't used; else 4 bits.
		 */
		.wires		= 8,
		.gpio_wp	= 4,
	},
	{
		.mmc		= 2,
		.wires		= 8,
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
	twl4030_mmc_init(mmc);

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
	.consumer_supplies	= &sdp3430_vdac_supply,
};

/* VPLL2 for digital video outputs */
static struct regulator_init_data sdp3430_vpll2 = {
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
	.consumer_supplies	= &sdp3430_vdvi_supply,
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
	omap_cfg_reg(AF26_34XX_SYS_NIRQ); /* T2 interrupt line (keypad) */
}

static void __init omap_3430sdp_init(void)
{
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
	usb_musb_init();
	board_smc91x_init();
	enable_board_wakeup_source();
}

static void __init omap_3430sdp_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(OMAP_3430SDP, "OMAP3430 3430SDP board")
	/* Maintainer: Syed Khasim - Texas Instruments Inc */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_3430sdp_map_io,
	.init_irq	= omap_3430sdp_init_irq,
	.init_machine	= omap_3430sdp_init,
	.timer		= &omap_timer,
MACHINE_END
