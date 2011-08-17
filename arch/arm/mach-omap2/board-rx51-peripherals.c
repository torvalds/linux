/*
 * linux/arch/arm/mach-omap2/board-rx51-peripherals.c
 *
 * Copyright (C) 2008-2009 Nokia
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/spi/spi.h>
#include <linux/wl12xx.h>
#include <linux/i2c.h>
#include <linux/i2c/twl.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/regulator/machine.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/mmc/host.h>
#include <linux/power/isp1704_charger.h>

#include <plat/mcspi.h>
#include <plat/board.h>
#include <plat/common.h>
#include <plat/dma.h>
#include <plat/gpmc.h>
#include <plat/onenand.h>
#include <plat/gpmc-smc91x.h>

#include <mach/board-rx51.h>

#include <sound/tlv320aic3x.h>
#include <sound/tpa6130a2-plat.h>
#include <media/radio-si4713.h>
#include <media/si4713.h>
#include <linux/leds-lp5523.h>

#include <../drivers/staging/iio/light/tsl2563.h>

#include "mux.h"
#include "hsmmc.h"
#include "common-board-devices.h"

#define SYSTEM_REV_B_USES_VAUX3	0x1699
#define SYSTEM_REV_S_USES_VAUX3 0x8

#define RX51_WL1251_POWER_GPIO		87
#define RX51_WL1251_IRQ_GPIO		42
#define RX51_FMTX_RESET_GPIO		163
#define RX51_FMTX_IRQ			53
#define RX51_LP5523_CHIP_EN_GPIO	41

#define RX51_USB_TRANSCEIVER_RST_GPIO	67

/* list all spi devices here */
enum {
	RX51_SPI_WL1251,
	RX51_SPI_MIPID,		/* LCD panel */
	RX51_SPI_TSC2005,	/* Touch Controller */
};

static struct wl12xx_platform_data wl1251_pdata;

#if defined(CONFIG_SENSORS_TSL2563) || defined(CONFIG_SENSORS_TSL2563_MODULE)
static struct tsl2563_platform_data rx51_tsl2563_platform_data = {
	.cover_comp_gain = 16,
};
#endif

#if defined(CONFIG_LEDS_LP5523) || defined(CONFIG_LEDS_LP5523_MODULE)
static struct lp5523_led_config rx51_lp5523_led_config[] = {
	{
		.chan_nr	= 0,
		.led_current	= 50,
	}, {
		.chan_nr	= 1,
		.led_current	= 50,
	}, {
		.chan_nr	= 2,
		.led_current	= 50,
	}, {
		.chan_nr	= 3,
		.led_current	= 50,
	}, {
		.chan_nr	= 4,
		.led_current	= 50,
	}, {
		.chan_nr	= 5,
		.led_current	= 50,
	}, {
		.chan_nr	= 6,
		.led_current	= 50,
	}, {
		.chan_nr	= 7,
		.led_current	= 50,
	}, {
		.chan_nr	= 8,
		.led_current	= 50,
	}
};

static int rx51_lp5523_setup(void)
{
	return gpio_request_one(RX51_LP5523_CHIP_EN_GPIO, GPIOF_DIR_OUT,
			"lp5523_enable");
}

static void rx51_lp5523_release(void)
{
	gpio_free(RX51_LP5523_CHIP_EN_GPIO);
}

static void rx51_lp5523_enable(bool state)
{
	gpio_set_value(RX51_LP5523_CHIP_EN_GPIO, !!state);
}

static struct lp5523_platform_data rx51_lp5523_platform_data = {
	.led_config		= rx51_lp5523_led_config,
	.num_channels		= ARRAY_SIZE(rx51_lp5523_led_config),
	.clock_mode		= LP5523_CLOCK_AUTO,
	.setup_resources	= rx51_lp5523_setup,
	.release_resources	= rx51_lp5523_release,
	.enable			= rx51_lp5523_enable,
};
#endif

static struct omap2_mcspi_device_config wl1251_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,
};

static struct omap2_mcspi_device_config mipid_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,
};

static struct omap2_mcspi_device_config tsc2005_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,
};

static struct spi_board_info rx51_peripherals_spi_board_info[] __initdata = {
	[RX51_SPI_WL1251] = {
		.modalias		= "wl1251",
		.bus_num		= 4,
		.chip_select		= 0,
		.max_speed_hz   	= 48000000,
		.mode                   = SPI_MODE_3,
		.controller_data	= &wl1251_mcspi_config,
		.platform_data		= &wl1251_pdata,
	},
	[RX51_SPI_MIPID] = {
		.modalias		= "acx565akm",
		.bus_num		= 1,
		.chip_select		= 2,
		.max_speed_hz		= 6000000,
		.controller_data	= &mipid_mcspi_config,
	},
	[RX51_SPI_TSC2005] = {
		.modalias		= "tsc2005",
		.bus_num		= 1,
		.chip_select		= 0,
		/* .irq = OMAP_GPIO_IRQ(RX51_TSC2005_IRQ_GPIO),*/
		.max_speed_hz		= 6000000,
		.controller_data	= &tsc2005_mcspi_config,
		/* .platform_data = &tsc2005_config,*/
	},
};

static void rx51_charger_set_power(bool on)
{
	gpio_set_value(RX51_USB_TRANSCEIVER_RST_GPIO, on);
}

static struct isp1704_charger_data rx51_charger_data = {
	.set_power	= rx51_charger_set_power,
};

static struct platform_device rx51_charger_device = {
	.name	= "isp1704_charger",
	.dev	= {
		.platform_data = &rx51_charger_data,
	},
};

static void __init rx51_charger_init(void)
{
	WARN_ON(gpio_request_one(RX51_USB_TRANSCEIVER_RST_GPIO,
		GPIOF_OUT_INIT_LOW, "isp1704_reset"));

	platform_device_register(&rx51_charger_device);
}

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)

#define RX51_GPIO_CAMERA_LENS_COVER	110
#define RX51_GPIO_CAMERA_FOCUS		68
#define RX51_GPIO_CAMERA_CAPTURE	69
#define RX51_GPIO_KEYPAD_SLIDE		71
#define RX51_GPIO_LOCK_BUTTON		113
#define RX51_GPIO_PROXIMITY		89

#define RX51_GPIO_DEBOUNCE_TIMEOUT	10

static struct gpio_keys_button rx51_gpio_keys[] = {
	{
		.desc			= "Camera Lens Cover",
		.type			= EV_SW,
		.code			= SW_CAMERA_LENS_COVER,
		.gpio			= RX51_GPIO_CAMERA_LENS_COVER,
		.active_low		= 1,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}, {
		.desc			= "Camera Focus",
		.type			= EV_KEY,
		.code			= KEY_CAMERA_FOCUS,
		.gpio			= RX51_GPIO_CAMERA_FOCUS,
		.active_low		= 1,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}, {
		.desc			= "Camera Capture",
		.type			= EV_KEY,
		.code			= KEY_CAMERA,
		.gpio			= RX51_GPIO_CAMERA_CAPTURE,
		.active_low		= 1,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}, {
		.desc			= "Lock Button",
		.type			= EV_KEY,
		.code			= KEY_SCREENLOCK,
		.gpio			= RX51_GPIO_LOCK_BUTTON,
		.active_low		= 1,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}, {
		.desc			= "Keypad Slide",
		.type			= EV_SW,
		.code			= SW_KEYPAD_SLIDE,
		.gpio			= RX51_GPIO_KEYPAD_SLIDE,
		.active_low		= 1,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}, {
		.desc			= "Proximity Sensor",
		.type			= EV_SW,
		.code			= SW_FRONT_PROXIMITY,
		.gpio			= RX51_GPIO_PROXIMITY,
		.active_low		= 0,
		.debounce_interval	= RX51_GPIO_DEBOUNCE_TIMEOUT,
	}
};

static struct gpio_keys_platform_data rx51_gpio_keys_data = {
	.buttons	= rx51_gpio_keys,
	.nbuttons	= ARRAY_SIZE(rx51_gpio_keys),
};

static struct platform_device rx51_gpio_keys_device = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data	= &rx51_gpio_keys_data,
	},
};

static void __init rx51_add_gpio_keys(void)
{
	platform_device_register(&rx51_gpio_keys_device);
}
#else
static void __init rx51_add_gpio_keys(void)
{
}
#endif /* CONFIG_KEYBOARD_GPIO || CONFIG_KEYBOARD_GPIO_MODULE */

static uint32_t board_keymap[] = {
	/*
	 * Note that KEY(x, 8, KEY_XXX) entries represent "entrire row
	 * connected to the ground" matrix state.
	 */
	KEY(0, 0, KEY_Q),
	KEY(0, 1, KEY_O),
	KEY(0, 2, KEY_P),
	KEY(0, 3, KEY_COMMA),
	KEY(0, 4, KEY_BACKSPACE),
	KEY(0, 6, KEY_A),
	KEY(0, 7, KEY_S),

	KEY(1, 0, KEY_W),
	KEY(1, 1, KEY_D),
	KEY(1, 2, KEY_F),
	KEY(1, 3, KEY_G),
	KEY(1, 4, KEY_H),
	KEY(1, 5, KEY_J),
	KEY(1, 6, KEY_K),
	KEY(1, 7, KEY_L),

	KEY(2, 0, KEY_E),
	KEY(2, 1, KEY_DOT),
	KEY(2, 2, KEY_UP),
	KEY(2, 3, KEY_ENTER),
	KEY(2, 5, KEY_Z),
	KEY(2, 6, KEY_X),
	KEY(2, 7, KEY_C),
	KEY(2, 8, KEY_F9),

	KEY(3, 0, KEY_R),
	KEY(3, 1, KEY_V),
	KEY(3, 2, KEY_B),
	KEY(3, 3, KEY_N),
	KEY(3, 4, KEY_M),
	KEY(3, 5, KEY_SPACE),
	KEY(3, 6, KEY_SPACE),
	KEY(3, 7, KEY_LEFT),

	KEY(4, 0, KEY_T),
	KEY(4, 1, KEY_DOWN),
	KEY(4, 2, KEY_RIGHT),
	KEY(4, 4, KEY_LEFTCTRL),
	KEY(4, 5, KEY_RIGHTALT),
	KEY(4, 6, KEY_LEFTSHIFT),
	KEY(4, 8, KEY_F10),

	KEY(5, 0, KEY_Y),
	KEY(5, 8, KEY_F11),

	KEY(6, 0, KEY_U),

	KEY(7, 0, KEY_I),
	KEY(7, 1, KEY_F7),
	KEY(7, 2, KEY_F8),
};

static struct matrix_keymap_data board_map_data = {
	.keymap			= board_keymap,
	.keymap_size		= ARRAY_SIZE(board_keymap),
};

static struct twl4030_keypad_data rx51_kp_data = {
	.keymap_data	= &board_map_data,
	.rows		= 8,
	.cols		= 8,
	.rep		= 1,
};

/* Enable input logic and pull all lines up when eMMC is on. */
static struct omap_board_mux rx51_mmc2_on_mux[] = {
	OMAP3_MUX(SDMMC2_CMD, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT0, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT1, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT2, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT3, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT4, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT5, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT6, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT7, OMAP_PIN_INPUT_PULLUP | OMAP_MUX_MODE0),
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};

/* Disable input logic and pull all lines down when eMMC is off. */
static struct omap_board_mux rx51_mmc2_off_mux[] = {
	OMAP3_MUX(SDMMC2_CMD, OMAP_PULL_ENA | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT0, OMAP_PULL_ENA | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT1, OMAP_PULL_ENA | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT2, OMAP_PULL_ENA | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT3, OMAP_PULL_ENA | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT4, OMAP_PULL_ENA | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT5, OMAP_PULL_ENA | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT6, OMAP_PULL_ENA | OMAP_MUX_MODE0),
	OMAP3_MUX(SDMMC2_DAT7, OMAP_PULL_ENA | OMAP_MUX_MODE0),
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};

static struct omap_mux_partition *partition;

/*
 * Current flows to eMMC when eMMC is off and the data lines are pulled up,
 * so pull them down. N.B. we pull 8 lines because we are using 8 lines.
 */
static void rx51_mmc2_remux(struct device *dev, int slot, int power_on)
{
	if (power_on)
		omap_mux_write_array(partition, rx51_mmc2_on_mux);
	else
		omap_mux_write_array(partition, rx51_mmc2_off_mux);
}

static struct omap2_hsmmc_info mmc[] __initdata = {
	{
		.name		= "external",
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.cover_only	= true,
		.gpio_cd	= 160,
		.gpio_wp	= -EINVAL,
		.power_saving	= true,
	},
	{
		.name		= "internal",
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
						/* See also rx51_mmc2_remux */
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
		.nonremovable	= true,
		.power_saving	= true,
		.remux		= rx51_mmc2_remux,
	},
	{}	/* Terminator */
};

static struct regulator_consumer_supply rx51_vmmc1_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.0"),
};

static struct regulator_consumer_supply rx51_vaux2_supply[] = {
	REGULATOR_SUPPLY("vdds_csib", "omap3isp"),
};

static struct regulator_consumer_supply rx51_vaux3_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.1"),
};

static struct regulator_consumer_supply rx51_vsim_supply[] = {
	REGULATOR_SUPPLY("vmmc_aux", "omap_hsmmc.1"),
};

static struct regulator_consumer_supply rx51_vmmc2_supplies[] = {
	/* tlv320aic3x analog supplies */
	REGULATOR_SUPPLY("AVDD", "2-0018"),
	REGULATOR_SUPPLY("DRVDD", "2-0018"),
	REGULATOR_SUPPLY("AVDD", "2-0019"),
	REGULATOR_SUPPLY("DRVDD", "2-0019"),
	/* tpa6130a2 */
	REGULATOR_SUPPLY("Vdd", "2-0060"),
	/* Keep vmmc as last item. It is not iterated for newer boards */
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.1"),
};

static struct regulator_consumer_supply rx51_vio_supplies[] = {
	/* tlv320aic3x digital supplies */
	REGULATOR_SUPPLY("IOVDD", "2-0018"),
	REGULATOR_SUPPLY("DVDD", "2-0018"),
	REGULATOR_SUPPLY("IOVDD", "2-0019"),
	REGULATOR_SUPPLY("DVDD", "2-0019"),
	/* Si4713 IO supply */
	REGULATOR_SUPPLY("vio", "2-0063"),
};

static struct regulator_consumer_supply rx51_vaux1_consumers[] = {
	REGULATOR_SUPPLY("vdds_sdi", "omapdss"),
	/* Si4713 supply */
	REGULATOR_SUPPLY("vdd", "2-0063"),
};

static struct regulator_init_data rx51_vaux1 = {
	.constraints = {
		.name			= "V28",
		.min_uV			= 2800000,
		.max_uV			= 2800000,
		.always_on		= true, /* due battery cover sensor */
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(rx51_vaux1_consumers),
	.consumer_supplies	= rx51_vaux1_consumers,
};

static struct regulator_init_data rx51_vaux2 = {
	.constraints = {
		.name			= "VCSI",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(rx51_vaux2_supply),
	.consumer_supplies	= rx51_vaux2_supply,
};

/* VAUX3 - adds more power to VIO_18 rail */
static struct regulator_init_data rx51_vaux3_cam = {
	.constraints = {
		.name			= "VCAM_DIG_18",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data rx51_vaux3_mmc = {
	.constraints = {
		.name			= "VMMC2_30",
		.min_uV			= 2800000,
		.max_uV			= 3000000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(rx51_vaux3_supply),
	.consumer_supplies	= rx51_vaux3_supply,
};

static struct regulator_init_data rx51_vaux4 = {
	.constraints = {
		.name			= "VCAM_ANA_28",
		.min_uV			= 2800000,
		.max_uV			= 2800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
};

static struct regulator_init_data rx51_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(rx51_vmmc1_supply),
	.consumer_supplies	= rx51_vmmc1_supply,
};

static struct regulator_init_data rx51_vmmc2 = {
	.constraints = {
		.name			= "V28_A",
		.min_uV			= 2800000,
		.max_uV			= 3000000,
		.always_on		= true, /* due VIO leak to AIC34 VDDs */
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(rx51_vmmc2_supplies),
	.consumer_supplies	= rx51_vmmc2_supplies,
};

static struct regulator_init_data rx51_vpll1 = {
	.constraints = {
		.name			= "VPLL",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.always_on		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE,
	},
};

static struct regulator_init_data rx51_vpll2 = {
	.constraints = {
		.name			= "VSDI_CSI",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.always_on		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE,
	},
};

static struct regulator_init_data rx51_vsim = {
	.constraints = {
		.name			= "VMMC2_IO_18",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(rx51_vsim_supply),
	.consumer_supplies	= rx51_vsim_supply,
};

static struct regulator_init_data rx51_vio = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(rx51_vio_supplies),
	.consumer_supplies	= rx51_vio_supplies,
};

static struct regulator_init_data rx51_vintana1 = {
	.constraints = {
		.name			= "VINTANA1",
		.min_uV			= 1500000,
		.max_uV			= 1500000,
		.always_on		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE,
	},
};

static struct regulator_init_data rx51_vintana2 = {
	.constraints = {
		.name			= "VINTANA2",
		.min_uV			= 2750000,
		.max_uV			= 2750000,
		.apply_uV		= true,
		.always_on		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE,
	},
};

static struct regulator_init_data rx51_vintdig = {
	.constraints = {
		.name			= "VINTDIG",
		.min_uV			= 1500000,
		.max_uV			= 1500000,
		.always_on		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE,
	},
};

static struct si4713_platform_data rx51_si4713_i2c_data __initdata_or_module = {
	.gpio_reset	= RX51_FMTX_RESET_GPIO,
};

static struct i2c_board_info rx51_si4713_board_info __initdata_or_module = {
	I2C_BOARD_INFO("si4713", SI4713_I2C_ADDR_BUSEN_HIGH),
	.platform_data	= &rx51_si4713_i2c_data,
};

static struct radio_si4713_platform_data rx51_si4713_data __initdata_or_module = {
	.i2c_bus	= 2,
	.subdev_board_info = &rx51_si4713_board_info,
};

static struct platform_device rx51_si4713_dev = {
	.name	= "radio-si4713",
	.id	= -1,
	.dev	= {
		.platform_data	= &rx51_si4713_data,
	},
};

static __init void rx51_init_si4713(void)
{
	int err;

	err = gpio_request_one(RX51_FMTX_IRQ, GPIOF_DIR_IN, "si4713 irq");
	if (err) {
		printk(KERN_ERR "Cannot request si4713 irq gpio. %d\n", err);
		return;
	}
	rx51_si4713_board_info.irq = gpio_to_irq(RX51_FMTX_IRQ);
	platform_device_register(&rx51_si4713_dev);
}

static int rx51_twlgpio_setup(struct device *dev, unsigned gpio, unsigned n)
{
	/* FIXME this gpio setup is just a placeholder for now */
	gpio_request_one(gpio + 6, GPIOF_OUT_INIT_LOW, "backlight_pwm");
	gpio_request_one(gpio + 7, GPIOF_OUT_INIT_LOW, "speaker_en");

	return 0;
}

static struct twl4030_gpio_platform_data rx51_gpio_data = {
	.gpio_base		= OMAP_MAX_GPIO_LINES,
	.irq_base		= TWL4030_GPIO_IRQ_BASE,
	.irq_end		= TWL4030_GPIO_IRQ_END,
	.pulldowns		= BIT(0) | BIT(1) | BIT(2) | BIT(3)
				| BIT(4) | BIT(5)
				| BIT(8) | BIT(9) | BIT(10) | BIT(11)
				| BIT(12) | BIT(13) | BIT(14) | BIT(15)
				| BIT(16) | BIT(17) ,
	.setup			= rx51_twlgpio_setup,
};

static struct twl4030_ins sleep_on_seq[] __initdata = {
/*
 * Turn off everything
 */
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 1, 0, RES_STATE_SLEEP), 2},
};

static struct twl4030_script sleep_on_script __initdata = {
	.script = sleep_on_seq,
	.size   = ARRAY_SIZE(sleep_on_seq),
	.flags  = TWL4030_SLEEP_SCRIPT,
};

static struct twl4030_ins wakeup_seq[] __initdata = {
/*
 * Reenable everything
 */
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 1, 0, RES_STATE_ACTIVE), 2},
};

static struct twl4030_script wakeup_script __initdata = {
	.script	= wakeup_seq,
	.size	= ARRAY_SIZE(wakeup_seq),
	.flags	= TWL4030_WAKEUP12_SCRIPT,
};

static struct twl4030_ins wakeup_p3_seq[] __initdata = {
/*
 * Reenable everything
 */
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 1, 0, RES_STATE_ACTIVE), 2},
};

static struct twl4030_script wakeup_p3_script __initdata = {
	.script	= wakeup_p3_seq,
	.size	= ARRAY_SIZE(wakeup_p3_seq),
	.flags	= TWL4030_WAKEUP3_SCRIPT,
};

static struct twl4030_ins wrst_seq[] __initdata = {
/*
 * Reset twl4030.
 * Reset VDD1 regulator.
 * Reset VDD2 regulator.
 * Reset VPLL1 regulator.
 * Enable sysclk output.
 * Reenable twl4030.
 */
	{MSG_SINGULAR(DEV_GRP_NULL, RES_RESET, RES_STATE_OFF), 2},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_ALL, 0, 1, RES_STATE_ACTIVE),
		0x13},
	{MSG_BROADCAST(DEV_GRP_NULL, RES_GRP_PP, 0, 3, RES_STATE_OFF), 0x13},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_VDD1, RES_STATE_WRST), 0x13},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_VDD2, RES_STATE_WRST), 0x13},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_VPLL1, RES_STATE_WRST), 0x35},
	{MSG_SINGULAR(DEV_GRP_P3, RES_HFCLKOUT, RES_STATE_ACTIVE), 2},
	{MSG_SINGULAR(DEV_GRP_NULL, RES_RESET, RES_STATE_ACTIVE), 2},
};

static struct twl4030_script wrst_script __initdata = {
	.script = wrst_seq,
	.size   = ARRAY_SIZE(wrst_seq),
	.flags  = TWL4030_WRST_SCRIPT,
};

static struct twl4030_script *twl4030_scripts[] __initdata = {
	/* wakeup12 script should be loaded before sleep script, otherwise a
	   board might hit retention before loading of wakeup script is
	   completed. This can cause boot failures depending on timing issues.
	*/
	&wakeup_script,
	&sleep_on_script,
	&wakeup_p3_script,
	&wrst_script,
};

static struct twl4030_resconfig twl4030_rconfig[] __initdata = {
	{ .resource = RES_VDD1, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = RES_STATE_OFF,
	  .remap_sleep = RES_STATE_OFF
	},
	{ .resource = RES_VDD2, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = RES_STATE_OFF,
	  .remap_sleep = RES_STATE_OFF
	},
	{ .resource = RES_VPLL1, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = RES_STATE_OFF,
	  .remap_sleep = RES_STATE_OFF
	},
	{ .resource = RES_VPLL2, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VAUX1, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VAUX2, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VAUX3, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VAUX4, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VMMC1, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VMMC2, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VDAC, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VSIM, .devgroup = -1,
	  .type = -1, .type2 = 3, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VINTANA1, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = -1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VINTANA2, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VINTDIG, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = -1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_VIO, .devgroup = DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_CLKEN, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = 1, .type2 = -1 , .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_REGEN, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_NRES_PWRON, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_SYSEN, .devgroup = DEV_GRP_P1 | DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_HFCLKOUT, .devgroup = DEV_GRP_P3,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_32KCLKOUT, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_RESET, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ .resource = RES_MAIN_REF, .devgroup = -1,
	  .type = 1, .type2 = -1, .remap_off = -1, .remap_sleep = -1
	},
	{ 0, 0},
};

static struct twl4030_power_data rx51_t2scripts_data __initdata = {
	.scripts        = twl4030_scripts,
	.num = ARRAY_SIZE(twl4030_scripts),
	.resource_config = twl4030_rconfig,
};

struct twl4030_vibra_data rx51_vibra_data __initdata = {
	.coexist	= 0,
};

struct twl4030_audio_data rx51_audio_data __initdata = {
	.audio_mclk	= 26000000,
	.vibra		= &rx51_vibra_data,
};

static struct twl4030_platform_data rx51_twldata __initdata = {
	/* platform_data for children goes here */
	.gpio			= &rx51_gpio_data,
	.keypad			= &rx51_kp_data,
	.power			= &rx51_t2scripts_data,
	.audio			= &rx51_audio_data,

	.vaux1			= &rx51_vaux1,
	.vaux2			= &rx51_vaux2,
	.vaux4			= &rx51_vaux4,
	.vmmc1			= &rx51_vmmc1,
	.vpll1			= &rx51_vpll1,
	.vpll2			= &rx51_vpll2,
	.vsim			= &rx51_vsim,
	.vintana1		= &rx51_vintana1,
	.vintana2		= &rx51_vintana2,
	.vintdig		= &rx51_vintdig,
	.vio			= &rx51_vio,
};

static struct tpa6130a2_platform_data rx51_tpa6130a2_data __initdata_or_module = {
	.id			= TPA6130A2,
	.power_gpio		= 98,
};

/* Audio setup data */
static struct aic3x_setup_data rx51_aic34_setup = {
	.gpio_func[0] = AIC3X_GPIO1_FUNC_DISABLED,
	.gpio_func[1] = AIC3X_GPIO2_FUNC_DIGITAL_MIC_INPUT,
};

static struct aic3x_pdata rx51_aic3x_data = {
	.setup = &rx51_aic34_setup,
	.gpio_reset = 60,
};

static struct aic3x_pdata rx51_aic3x_data2 = {
	.gpio_reset = 60,
};

static struct i2c_board_info __initdata rx51_peripherals_i2c_board_info_2[] = {
	{
		I2C_BOARD_INFO("tlv320aic3x", 0x18),
		.platform_data = &rx51_aic3x_data,
	},
	{
		I2C_BOARD_INFO("tlv320aic3x", 0x19),
		.platform_data = &rx51_aic3x_data2,
	},
#if defined(CONFIG_SENSORS_TSL2563) || defined(CONFIG_SENSORS_TSL2563_MODULE)
	{
		I2C_BOARD_INFO("tsl2563", 0x29),
		.platform_data = &rx51_tsl2563_platform_data,
	},
#endif
#if defined(CONFIG_LEDS_LP5523) || defined(CONFIG_LEDS_LP5523_MODULE)
	{
		I2C_BOARD_INFO("lp5523", 0x32),
		.platform_data  = &rx51_lp5523_platform_data,
	},
#endif
	{
		I2C_BOARD_INFO("tpa6130a2", 0x60),
		.platform_data = &rx51_tpa6130a2_data,
	}
};

static int __init rx51_i2c_init(void)
{
	if ((system_rev >= SYSTEM_REV_S_USES_VAUX3 && system_rev < 0x100) ||
	    system_rev >= SYSTEM_REV_B_USES_VAUX3) {
		rx51_twldata.vaux3 = &rx51_vaux3_mmc;
		/* Only older boards use VMMC2 for internal MMC */
		rx51_vmmc2.num_consumer_supplies--;
	} else {
		rx51_twldata.vaux3 = &rx51_vaux3_cam;
	}
	rx51_twldata.vmmc2 = &rx51_vmmc2;
	omap3_pmic_get_config(&rx51_twldata,
			TWL_COMMON_PDATA_USB | TWL_COMMON_PDATA_MADC,
			TWL_COMMON_REGULATOR_VDAC);

	rx51_twldata.vdac->constraints.apply_uV = true;
	rx51_twldata.vdac->constraints.name = "VDAC";

	omap_pmic_init(1, 2200, "twl5030", INT_34XX_SYS_NIRQ, &rx51_twldata);
	omap_register_i2c_bus(2, 100, rx51_peripherals_i2c_board_info_2,
			      ARRAY_SIZE(rx51_peripherals_i2c_board_info_2));
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

#if defined(CONFIG_MTD_ONENAND_OMAP2) || \
	defined(CONFIG_MTD_ONENAND_OMAP2_MODULE)

static struct mtd_partition onenand_partitions[] = {
	{
		.name           = "bootloader",
		.offset         = 0,
		.size           = 0x20000,
		.mask_flags     = MTD_WRITEABLE,	/* Force read-only */
	},
	{
		.name           = "config",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 0x60000,
	},
	{
		.name           = "log",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 0x40000,
	},
	{
		.name           = "kernel",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 0x200000,
	},
	{
		.name           = "initfs",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 0x200000,
	},
	{
		.name           = "rootfs",
		.offset         = MTDPART_OFS_APPEND,
		.size           = MTDPART_SIZ_FULL,
	},
};

static struct omap_onenand_platform_data board_onenand_data[] = {
	{
		.cs		= 0,
		.gpio_irq	= 65,
		.parts		= onenand_partitions,
		.nr_parts	= ARRAY_SIZE(onenand_partitions),
		.flags		= ONENAND_SYNC_READWRITE,
	}
};
#endif

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)

static struct omap_smc91x_platform_data board_smc91x_data = {
	.cs		= 1,
	.gpio_irq	= 54,
	.gpio_pwrdwn	= 86,
	.gpio_reset	= 164,
	.flags		= GPMC_TIMINGS_SMC91C96 | IORESOURCE_IRQ_HIGHLEVEL,
};

static void __init board_smc91x_init(void)
{
	omap_mux_init_gpio(54, OMAP_PIN_INPUT_PULLDOWN);
	omap_mux_init_gpio(86, OMAP_PIN_OUTPUT);
	omap_mux_init_gpio(164, OMAP_PIN_OUTPUT);

	gpmc_smc91x_init(&board_smc91x_data);
}

#else

static inline void board_smc91x_init(void)
{
}

#endif

static void rx51_wl1251_set_power(bool enable)
{
	gpio_set_value(RX51_WL1251_POWER_GPIO, enable);
}

static struct gpio rx51_wl1251_gpios[] __initdata = {
	{ RX51_WL1251_POWER_GPIO, GPIOF_OUT_INIT_LOW,	"wl1251 power"	},
	{ RX51_WL1251_IRQ_GPIO,	  GPIOF_IN,		"wl1251 irq"	},
};

static void __init rx51_init_wl1251(void)
{
	int irq, ret;

	ret = gpio_request_array(rx51_wl1251_gpios,
				 ARRAY_SIZE(rx51_wl1251_gpios));
	if (ret < 0)
		goto error;

	irq = gpio_to_irq(RX51_WL1251_IRQ_GPIO);
	if (irq < 0)
		goto err_irq;

	wl1251_pdata.set_power = rx51_wl1251_set_power;
	rx51_peripherals_spi_board_info[RX51_SPI_WL1251].irq = irq;

	return;

err_irq:
	gpio_free(RX51_WL1251_IRQ_GPIO);
	gpio_free(RX51_WL1251_POWER_GPIO);
error:
	printk(KERN_ERR "wl1251 board initialisation failed\n");
	wl1251_pdata.set_power = NULL;

	/*
	 * Now rx51_peripherals_spi_board_info[1].irq is zero and
	 * set_power is null, and wl1251_probe() will fail.
	 */
}

void __init rx51_peripherals_init(void)
{
	rx51_i2c_init();
	regulator_has_full_constraints();
	gpmc_onenand_init(board_onenand_data);
	board_smc91x_init();
	rx51_add_gpio_keys();
	rx51_init_wl1251();
	rx51_init_si4713();
	spi_register_board_info(rx51_peripherals_spi_board_info,
				ARRAY_SIZE(rx51_peripherals_spi_board_info));

	partition = omap_mux_get("core");
	if (partition)
		omap2_hsmmc_init(mmc);

	rx51_charger_init();
}

