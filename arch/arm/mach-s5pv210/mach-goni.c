/* linux/arch/arm/mach-s5pv210/mach-goni.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/i2c/atmel_mxt_ts.h>
#include <linux/mfd/max8998.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/regulator/fixed.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/lcd.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/regs-fb.h>

#include <plat/gpio-cfg.h>
#include <plat/regs-serial.h>
#include <plat/s5pv210.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/fb.h>
#include <plat/iic.h>
#include <plat/keypad.h>
#include <plat/sdhci.h>
#include <plat/clock.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define GONI_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define GONI_ULCON_DEFAULT	S3C2410_LCON_CS8

#define GONI_UFCON_DEFAULT	S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg goni_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= GONI_UCON_DEFAULT,
		.ulcon		= GONI_ULCON_DEFAULT,
		.ufcon		= GONI_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG256 | S5PV210_UFCON_RXTRIG256,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= GONI_UCON_DEFAULT,
		.ulcon		= GONI_ULCON_DEFAULT,
		.ufcon		= GONI_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG64 | S5PV210_UFCON_RXTRIG64,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= GONI_UCON_DEFAULT,
		.ulcon		= GONI_ULCON_DEFAULT,
		.ufcon		= GONI_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG16 | S5PV210_UFCON_RXTRIG16,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= GONI_UCON_DEFAULT,
		.ulcon		= GONI_ULCON_DEFAULT,
		.ufcon		= GONI_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG16 | S5PV210_UFCON_RXTRIG16,
	},
};

/* Frame Buffer */
static struct s3c_fb_pd_win goni_fb_win0 = {
	.win_mode = {
		.left_margin	= 16,
		.right_margin	= 16,
		.upper_margin	= 2,
		.lower_margin	= 28,
		.hsync_len	= 2,
		.vsync_len	= 1,
		.xres		= 480,
		.yres		= 800,
		.refresh	= 55,
	},
	.max_bpp	= 32,
	.default_bpp	= 16,
};

static struct s3c_fb_platdata goni_lcd_pdata __initdata = {
	.win[0]		= &goni_fb_win0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB |
			  VIDCON0_CLKSEL_LCD,
	.vidcon1	= VIDCON1_INV_VCLK | VIDCON1_INV_VDEN
			  | VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC,
	.setup_gpio	= s5pv210_fb_gpio_setup_24bpp,
};

static int lcd_power_on(struct lcd_device *ld, int enable)
{
	return 1;
}

static int reset_lcd(struct lcd_device *ld)
{
	static unsigned int first = 1;
	int reset_gpio = -1;

	reset_gpio = S5PV210_MP05(5);

	if (first) {
		gpio_request(reset_gpio, "MLCD_RST");
		first = 0;
	}

	gpio_direction_output(reset_gpio, 1);
	return 1;
}

static struct lcd_platform_data goni_lcd_platform_data = {
	.reset			= reset_lcd,
	.power_on		= lcd_power_on,
	.lcd_enabled		= 0,
	.reset_delay		= 120,	/* 120ms */
	.power_on_delay		= 25,	/* 25ms */
	.power_off_delay	= 200,	/* 200ms */
};

#define LCD_BUS_NUM	3
static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias	= "s6e63m0",
		.platform_data	= &goni_lcd_platform_data,
		.max_speed_hz	= 1200000,
		.bus_num	= LCD_BUS_NUM,
		.chip_select	= 0,
		.mode		= SPI_MODE_3,
		.controller_data = (void *)S5PV210_MP01(1), /* DISPLAY_CS */
	},
};

static struct spi_gpio_platform_data lcd_spi_gpio_data = {
	.sck	= S5PV210_MP04(1), /* DISPLAY_CLK */
	.mosi	= S5PV210_MP04(3), /* DISPLAY_SI */
	.miso	= SPI_GPIO_NO_MISO,
	.num_chipselect	= 1,
};

static struct platform_device goni_spi_gpio = {
	.name	= "spi_gpio",
	.id	= LCD_BUS_NUM,
	.dev	= {
		.parent		= &s3c_device_fb.dev,
		.platform_data	= &lcd_spi_gpio_data,
	},
};

/* KEYPAD */
static uint32_t keymap[] __initdata = {
	/* KEY(row, col, keycode) */
	KEY(0, 1, KEY_MENU),		/* Send */
	KEY(0, 2, KEY_BACK),		/* End */
	KEY(1, 1, KEY_CONFIG),		/* Half shot */
	KEY(1, 2, KEY_VOLUMEUP),
	KEY(2, 1, KEY_CAMERA),		/* Full shot */
	KEY(2, 2, KEY_VOLUMEDOWN),
};

static struct matrix_keymap_data keymap_data __initdata = {
	.keymap		= keymap,
	.keymap_size	= ARRAY_SIZE(keymap),
};

static struct samsung_keypad_platdata keypad_data __initdata = {
	.keymap_data	= &keymap_data,
	.rows		= 3,
	.cols		= 3,
};

/* Radio */
static struct i2c_board_info i2c1_devs[] __initdata = {
	{
		I2C_BOARD_INFO("si470x", 0x10),
	},
};

static void __init goni_radio_init(void)
{
	int gpio;

	gpio = S5PV210_GPJ2(4);			/* XMSMDATA_4 */
	gpio_request(gpio, "FM_INT");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	i2c1_devs[0].irq = gpio_to_irq(gpio);

	gpio = S5PV210_GPJ2(5);			/* XMSMDATA_5 */
	gpio_request(gpio, "FM_RST");
	gpio_direction_output(gpio, 1);
}

/* TSP */
static struct mxt_platform_data qt602240_platform_data = {
	.x_line		= 17,
	.y_line		= 11,
	.x_size		= 800,
	.y_size		= 480,
	.blen		= 0x21,
	.threshold	= 0x28,
	.voltage	= 2800000,              /* 2.8V */
	.orient		= MXT_DIAGONAL,
	.irqflags	= IRQF_TRIGGER_FALLING,
};

static struct s3c2410_platform_i2c i2c2_data __initdata = {
	.flags		= 0,
	.bus_num	= 2,
	.slave_addr	= 0x10,
	.frequency	= 400 * 1000,
	.sda_delay	= 100,
};

static struct i2c_board_info i2c2_devs[] __initdata = {
	{
		I2C_BOARD_INFO("qt602240_ts", 0x4a),
		.platform_data = &qt602240_platform_data,
	},
};

static void __init goni_tsp_init(void)
{
	int gpio;

	gpio = S5PV210_GPJ1(3);		/* XMSMADDR_11 */
	gpio_request(gpio, "TSP_LDO_ON");
	gpio_direction_output(gpio, 1);
	gpio_export(gpio, 0);

	gpio = S5PV210_GPJ0(5);		/* XMSMADDR_5 */
	gpio_request(gpio, "TSP_INT");

	s5p_register_gpio_interrupt(gpio);
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
	i2c2_devs[0].irq = gpio_to_irq(gpio);
}

/* MAX8998 regulators */
#if defined(CONFIG_REGULATOR_MAX8998) || defined(CONFIG_REGULATOR_MAX8998_MODULE)

static struct regulator_consumer_supply goni_ldo5_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "s3c-sdhci.0"),
};

static struct regulator_init_data goni_ldo2_data = {
	.constraints	= {
		.name		= "VALIVE_1.1V",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.always_on	= 1,
		.state_mem	= {
			.enabled = 1,
		},
	},
};

static struct regulator_init_data goni_ldo3_data = {
	.constraints	= {
		.name		= "VUSB/MIPI_1.1V",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data goni_ldo4_data = {
	.constraints	= {
		.name		= "VDAC_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
	},
};

static struct regulator_init_data goni_ldo5_data = {
	.constraints	= {
		.name		= "VTF_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(goni_ldo5_consumers),
	.consumer_supplies = goni_ldo5_consumers,
};

static struct regulator_init_data goni_ldo6_data = {
	.constraints	= {
		.name		= "VCC_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
	},
};

static struct regulator_init_data goni_ldo7_data = {
	.constraints	= {
		.name		= "VLCD_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data goni_ldo8_data = {
	.constraints	= {
		.name		= "VUSB/VADC_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data goni_ldo9_data = {
	.constraints	= {
		.name		= "VCC/VCAM_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data goni_ldo10_data = {
	.constraints	= {
		.name		= "VPLL_1.1V",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.boot_on	= 1,
	},
};

static struct regulator_init_data goni_ldo11_data = {
	.constraints	= {
		.name		= "CAM_IO_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data goni_ldo12_data = {
	.constraints	= {
		.name		= "CAM_ISP_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data goni_ldo13_data = {
	.constraints	= {
		.name		= "CAM_A_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data goni_ldo14_data = {
	.constraints	= {
		.name		= "CAM_CIF_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data goni_ldo15_data = {
	.constraints	= {
		.name		= "CAM_AF_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data goni_ldo16_data = {
	.constraints	= {
		.name		= "VMIPI_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data goni_ldo17_data = {
	.constraints	= {
		.name		= "VCC_3.0V_LCD",
		.min_uV		= 3000000,
		.max_uV		= 3000000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

/* BUCK */
static struct regulator_consumer_supply buck1_consumer[] = {
	{	.supply	= "vddarm", },
};

static struct regulator_consumer_supply buck2_consumer[] = {
	{	.supply	= "vddint", },
};

static struct regulator_init_data goni_buck1_data = {
	.constraints	= {
		.name		= "VARM_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(buck1_consumer),
	.consumer_supplies	= buck1_consumer,
};

static struct regulator_init_data goni_buck2_data = {
	.constraints	= {
		.name		= "VINT_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(buck2_consumer),
	.consumer_supplies	= buck2_consumer,
};

static struct regulator_init_data goni_buck3_data = {
	.constraints	= {
		.name		= "VCC_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.state_mem	= {
			.enabled = 1,
		},
	},
};

static struct regulator_init_data goni_buck4_data = {
	.constraints	= {
		.name		= "CAM_CORE_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct max8998_regulator_data goni_regulators[] = {
	{ MAX8998_LDO2,  &goni_ldo2_data },
	{ MAX8998_LDO3,  &goni_ldo3_data },
	{ MAX8998_LDO4,  &goni_ldo4_data },
	{ MAX8998_LDO5,  &goni_ldo5_data },
	{ MAX8998_LDO6,  &goni_ldo6_data },
	{ MAX8998_LDO7,  &goni_ldo7_data },
	{ MAX8998_LDO8,  &goni_ldo8_data },
	{ MAX8998_LDO9,  &goni_ldo9_data },
	{ MAX8998_LDO10, &goni_ldo10_data },
	{ MAX8998_LDO11, &goni_ldo11_data },
	{ MAX8998_LDO12, &goni_ldo12_data },
	{ MAX8998_LDO13, &goni_ldo13_data },
	{ MAX8998_LDO14, &goni_ldo14_data },
	{ MAX8998_LDO15, &goni_ldo15_data },
	{ MAX8998_LDO16, &goni_ldo16_data },
	{ MAX8998_LDO17, &goni_ldo17_data },
	{ MAX8998_BUCK1, &goni_buck1_data },
	{ MAX8998_BUCK2, &goni_buck2_data },
	{ MAX8998_BUCK3, &goni_buck3_data },
	{ MAX8998_BUCK4, &goni_buck4_data },
};

static struct max8998_platform_data goni_max8998_pdata = {
	.num_regulators	= ARRAY_SIZE(goni_regulators),
	.regulators	= goni_regulators,
	.buck1_set1	= S5PV210_GPH0(3),
	.buck1_set2	= S5PV210_GPH0(4),
	.buck2_set3	= S5PV210_GPH0(5),
	.buck1_max_voltage1 = 1200000,
	.buck1_max_voltage2 = 1200000,
	.buck2_max_voltage = 1200000,
};
#endif

static struct regulator_consumer_supply wm8994_fixed_voltage0_supplies[] = {
	{
		.dev_name	= "5-001a",
		.supply		= "DBVDD",
	}, {
		.dev_name	= "5-001a",
		.supply		= "AVDD2",
	}, {
		.dev_name	= "5-001a",
		.supply		= "CPVDD",
	},
};

static struct regulator_consumer_supply wm8994_fixed_voltage1_supplies[] = {
	{
		.dev_name	= "5-001a",
		.supply		= "SPKVDD1",
	}, {
		.dev_name	= "5-001a",
		.supply		= "SPKVDD2",
	},
};

static struct regulator_init_data wm8994_fixed_voltage0_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(wm8994_fixed_voltage0_supplies),
	.consumer_supplies	= wm8994_fixed_voltage0_supplies,
};

static struct regulator_init_data wm8994_fixed_voltage1_init_data = {
	.constraints = {
		.always_on = 1,
	},
	.num_consumer_supplies	= ARRAY_SIZE(wm8994_fixed_voltage1_supplies),
	.consumer_supplies	= wm8994_fixed_voltage1_supplies,
};

static struct fixed_voltage_config wm8994_fixed_voltage0_config = {
	.supply_name	= "VCC_1.8V_PDA",
	.microvolts	= 1800000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage0_init_data,
};

static struct fixed_voltage_config wm8994_fixed_voltage1_config = {
	.supply_name	= "V_BAT",
	.microvolts	= 3700000,
	.gpio		= -EINVAL,
	.init_data	= &wm8994_fixed_voltage1_init_data,
};

static struct platform_device wm8994_fixed_voltage0 = {
	.name		= "reg-fixed-voltage",
	.id		= 0,
	.dev		= {
		.platform_data	= &wm8994_fixed_voltage0_config,
	},
};

static struct platform_device wm8994_fixed_voltage1 = {
	.name		= "reg-fixed-voltage",
	.id		= 1,
	.dev		= {
		.platform_data	= &wm8994_fixed_voltage1_config,
	},
};

static struct regulator_consumer_supply wm8994_avdd1_supply = {
	.dev_name	= "5-001a",
	.supply		= "AVDD1",
};

static struct regulator_consumer_supply wm8994_dcvdd_supply = {
	.dev_name	= "5-001a",
	.supply		= "DCVDD",
};

static struct regulator_init_data wm8994_ldo1_data = {
	.constraints	= {
		.name		= "AVDD1_3.0V",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_avdd1_supply,
};

static struct regulator_init_data wm8994_ldo2_data = {
	.constraints	= {
		.name		= "DCVDD_1.0V",
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &wm8994_dcvdd_supply,
};

static struct wm8994_pdata wm8994_platform_data = {
	/* configure gpio1 function: 0x0001(Logic level input/output) */
	.gpio_defaults[0] = 0x0001,
	/* configure gpio3/4/5/7 function for AIF2 voice */
	.gpio_defaults[2] = 0x8100,
	.gpio_defaults[3] = 0x8100,
	.gpio_defaults[4] = 0x8100,
	.gpio_defaults[6] = 0x0100,
	/* configure gpio8/9/10/11 function for AIF3 BT */
	.gpio_defaults[7] = 0x8100,
	.gpio_defaults[8] = 0x0100,
	.gpio_defaults[9] = 0x0100,
	.gpio_defaults[10] = 0x0100,
	.ldo[0]	= { S5PV210_MP03(6), NULL, &wm8994_ldo1_data },	/* XM0FRNB_2 */
	.ldo[1]	= { 0, NULL, &wm8994_ldo2_data },
};

/* GPIO I2C PMIC */
#define AP_I2C_GPIO_PMIC_BUS_4	4
static struct i2c_gpio_platform_data goni_i2c_gpio_pmic_data = {
	.sda_pin	= S5PV210_GPJ4(0),	/* XMSMCSN */
	.scl_pin	= S5PV210_GPJ4(3),	/* XMSMIRQN */
};

static struct platform_device goni_i2c_gpio_pmic = {
	.name		= "i2c-gpio",
	.id		= AP_I2C_GPIO_PMIC_BUS_4,
	.dev		= {
		.platform_data	= &goni_i2c_gpio_pmic_data,
	},
};

static struct i2c_board_info i2c_gpio_pmic_devs[] __initdata = {
#if defined(CONFIG_REGULATOR_MAX8998) || defined(CONFIG_REGULATOR_MAX8998_MODULE)
	{
		/* 0xCC when SRAD = 0 */
		I2C_BOARD_INFO("max8998", 0xCC >> 1),
		.platform_data = &goni_max8998_pdata,
	},
#endif
};

/* GPIO I2C AP 1.8V */
#define AP_I2C_GPIO_BUS_5	5
static struct i2c_gpio_platform_data goni_i2c_gpio5_data = {
	.sda_pin	= S5PV210_MP05(3),	/* XM0ADDR_11 */
	.scl_pin	= S5PV210_MP05(2),	/* XM0ADDR_10 */
};

static struct platform_device goni_i2c_gpio5 = {
	.name		= "i2c-gpio",
	.id		= AP_I2C_GPIO_BUS_5,
	.dev		= {
		.platform_data	= &goni_i2c_gpio5_data,
	},
};

static struct i2c_board_info i2c_gpio5_devs[] __initdata = {
	{
		/* CS/ADDR = low 0x34 (FYI: high = 0x36) */
		I2C_BOARD_INFO("wm8994", 0x1a),
		.platform_data	= &wm8994_platform_data,
	},
};

/* PMIC Power button */
static struct gpio_keys_button goni_gpio_keys_table[] = {
	{
		.code 		= KEY_POWER,
		.gpio		= S5PV210_GPH2(6),
		.desc		= "gpio-keys: KEY_POWER",
		.type		= EV_KEY,
		.active_low	= 1,
		.wakeup		= 1,
		.debounce_interval = 1,
	},
};

static struct gpio_keys_platform_data goni_gpio_keys_data = {
	.buttons	= goni_gpio_keys_table,
	.nbuttons	= ARRAY_SIZE(goni_gpio_keys_table),
};

static struct platform_device goni_device_gpiokeys = {
	.name = "gpio-keys",
	.dev = {
		.platform_data = &goni_gpio_keys_data,
	},
};

static void __init goni_pmic_init(void)
{
	/* AP_PMIC_IRQ: EINT7 */
	s3c_gpio_cfgpin(S5PV210_GPH0(7), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(S5PV210_GPH0(7), S3C_GPIO_PULL_UP);

	/* nPower: EINT22 */
	s3c_gpio_cfgpin(S5PV210_GPH2(6), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(S5PV210_GPH2(6), S3C_GPIO_PULL_UP);
}

/* MoviNAND */
static struct s3c_sdhci_platdata goni_hsmmc0_data __initdata = {
	.max_width		= 4,
	.cd_type		= S3C_SDHCI_CD_PERMANENT,
};

/* Wireless LAN */
static struct s3c_sdhci_platdata goni_hsmmc1_data __initdata = {
	.max_width		= 4,
	.cd_type		= S3C_SDHCI_CD_EXTERNAL,
	/* ext_cd_{init,cleanup} callbacks will be added later */
};

/* External Flash */
#define GONI_EXT_FLASH_EN	S5PV210_MP05(4)
#define GONI_EXT_FLASH_CD	S5PV210_GPH3(4)
static struct s3c_sdhci_platdata goni_hsmmc2_data __initdata = {
	.max_width		= 4,
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= GONI_EXT_FLASH_CD,
	.ext_cd_gpio_invert	= 1,
};

static struct regulator_consumer_supply mmc2_supplies[] = {
	REGULATOR_SUPPLY("vmmc", "s3c-sdhci.2"),
};

static struct regulator_init_data mmc2_fixed_voltage_init_data = {
	.constraints		= {
		.name		= "V_TF_2.8V",
		.valid_ops_mask	= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(mmc2_supplies),
	.consumer_supplies	= mmc2_supplies,
};

static struct fixed_voltage_config mmc2_fixed_voltage_config = {
	.supply_name		= "EXT_FLASH_EN",
	.microvolts		= 2800000,
	.gpio			= GONI_EXT_FLASH_EN,
	.enable_high		= true,
	.init_data		= &mmc2_fixed_voltage_init_data,
};

static struct platform_device mmc2_fixed_voltage = {
	.name		= "reg-fixed-voltage",
	.id		= 2,
	.dev		= {
		.platform_data	= &mmc2_fixed_voltage_config,
	},
};

static void goni_setup_sdhci(void)
{
	s3c_sdhci0_set_platdata(&goni_hsmmc0_data);
	s3c_sdhci1_set_platdata(&goni_hsmmc1_data);
	s3c_sdhci2_set_platdata(&goni_hsmmc2_data);
};

static struct platform_device *goni_devices[] __initdata = {
	&s3c_device_fb,
	&s5p_device_onenand,
	&goni_spi_gpio,
	&goni_i2c_gpio_pmic,
	&goni_i2c_gpio5,
	&mmc2_fixed_voltage,
	&goni_device_gpiokeys,
	&s5p_device_fimc0,
	&s5p_device_fimc1,
	&s5p_device_fimc2,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
	&s3c_device_hsmmc2,
	&s5pv210_device_iis0,
	&s3c_device_usb_hsotg,
	&samsung_device_keypad,
	&s3c_device_i2c1,
	&s3c_device_i2c2,
	&wm8994_fixed_voltage0,
	&wm8994_fixed_voltage1,
};

static void __init goni_sound_init(void)
{
	/* Ths main clock of WM8994 codec uses the output of CLKOUT pin.
	 * The CLKOUT[9:8] set to 0x3(XUSBXTI) of 0xE010E000(OTHERS)
	 * because it needs 24MHz clock to operate WM8994 codec.
	 */
	__raw_writel(__raw_readl(S5P_OTHERS) | (0x3 << 8), S5P_OTHERS);
}

static void __init goni_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(goni_uartcfgs, ARRAY_SIZE(goni_uartcfgs));
}

static void __init goni_machine_init(void)
{
	/* Radio: call before I2C 1 registeration */
	goni_radio_init();

	/* I2C1 */
	s3c_i2c1_set_platdata(NULL);
	i2c_register_board_info(1, i2c1_devs, ARRAY_SIZE(i2c1_devs));

	/* TSP: call before I2C 2 registeration */
	goni_tsp_init();

	/* I2C2 */
	s3c_i2c2_set_platdata(&i2c2_data);
	i2c_register_board_info(2, i2c2_devs, ARRAY_SIZE(i2c2_devs));

	/* PMIC */
	goni_pmic_init();
	i2c_register_board_info(AP_I2C_GPIO_PMIC_BUS_4, i2c_gpio_pmic_devs,
			ARRAY_SIZE(i2c_gpio_pmic_devs));
	/* SDHCI */
	goni_setup_sdhci();

	/* SOUND */
	goni_sound_init();
	i2c_register_board_info(AP_I2C_GPIO_BUS_5, i2c_gpio5_devs,
			ARRAY_SIZE(i2c_gpio5_devs));

	/* FB */
	s3c_fb_set_platdata(&goni_lcd_pdata);

	/* SPI */
	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));

	/* KEYPAD */
	samsung_keypad_set_platdata(&keypad_data);

	clk_xusbxti.rate = 24000000;

	platform_add_devices(goni_devices, ARRAY_SIZE(goni_devices));
}

MACHINE_START(GONI, "GONI")
	/* Maintainers: Kyungmin Park <kyungmin.park@samsung.com> */
	.boot_params	= S5P_PA_SDRAM + 0x100,
	.init_irq	= s5pv210_init_irq,
	.map_io		= goni_map_io,
	.init_machine	= goni_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
