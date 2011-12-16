/* linux/arch/arm/mach-s5pv210/mach-aquila.c
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
#include <linux/mfd/max8998.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/regulator/fixed.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/gpio.h>

#include <asm/hardware/vic.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <mach/map.h>
#include <mach/regs-clock.h>

#include <plat/gpio-cfg.h>
#include <plat/regs-serial.h>
#include <plat/s5pv210.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/fb.h>
#include <plat/fimc-core.h>
#include <plat/sdhci.h>
#include <plat/s5p-time.h>
#include <plat/regs-fb-v4.h>

/* Following are default values for UCON, ULCON and UFCON UART registers */
#define AQUILA_UCON_DEFAULT	(S3C2410_UCON_TXILEVEL |	\
				 S3C2410_UCON_RXILEVEL |	\
				 S3C2410_UCON_TXIRQMODE |	\
				 S3C2410_UCON_RXIRQMODE |	\
				 S3C2410_UCON_RXFIFO_TOI |	\
				 S3C2443_UCON_RXERR_IRQEN)

#define AQUILA_ULCON_DEFAULT	S3C2410_LCON_CS8

#define AQUILA_UFCON_DEFAULT	S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg aquila_uartcfgs[] __initdata = {
	[0] = {
		.hwport		= 0,
		.flags		= 0,
		.ucon		= AQUILA_UCON_DEFAULT,
		.ulcon		= AQUILA_ULCON_DEFAULT,
		/*
		 * Actually UART0 can support 256 bytes fifo, but aquila board
		 * supports 128 bytes fifo because of initial chip bug
		 */
		.ufcon		= AQUILA_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG128 | S5PV210_UFCON_RXTRIG128,
	},
	[1] = {
		.hwport		= 1,
		.flags		= 0,
		.ucon		= AQUILA_UCON_DEFAULT,
		.ulcon		= AQUILA_ULCON_DEFAULT,
		.ufcon		= AQUILA_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG64 | S5PV210_UFCON_RXTRIG64,
	},
	[2] = {
		.hwport		= 2,
		.flags		= 0,
		.ucon		= AQUILA_UCON_DEFAULT,
		.ulcon		= AQUILA_ULCON_DEFAULT,
		.ufcon		= AQUILA_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG16 | S5PV210_UFCON_RXTRIG16,
	},
	[3] = {
		.hwport		= 3,
		.flags		= 0,
		.ucon		= AQUILA_UCON_DEFAULT,
		.ulcon		= AQUILA_ULCON_DEFAULT,
		.ufcon		= AQUILA_UFCON_DEFAULT |
			S5PV210_UFCON_TXTRIG16 | S5PV210_UFCON_RXTRIG16,
	},
};

/* Frame Buffer */
static struct s3c_fb_pd_win aquila_fb_win0 = {
	.win_mode = {
		.left_margin = 16,
		.right_margin = 16,
		.upper_margin = 3,
		.lower_margin = 28,
		.hsync_len = 2,
		.vsync_len = 2,
		.xres = 480,
		.yres = 800,
	},
	.max_bpp = 32,
	.default_bpp = 16,
};

static struct s3c_fb_pd_win aquila_fb_win1 = {
	.win_mode = {
		.left_margin = 16,
		.right_margin = 16,
		.upper_margin = 3,
		.lower_margin = 28,
		.hsync_len = 2,
		.vsync_len = 2,
		.xres = 480,
		.yres = 800,
	},
	.max_bpp = 32,
	.default_bpp = 16,
};

static struct s3c_fb_platdata aquila_lcd_pdata __initdata = {
	.win[0]		= &aquila_fb_win0,
	.win[1]		= &aquila_fb_win1,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_HSYNC | VIDCON1_INV_VSYNC |
			  VIDCON1_INV_VCLK | VIDCON1_INV_VDEN,
	.setup_gpio	= s5pv210_fb_gpio_setup_24bpp,
};

/* MAX8998 regulators */
#if defined(CONFIG_REGULATOR_MAX8998) || defined(CONFIG_REGULATOR_MAX8998_MODULE)

static struct regulator_init_data aquila_ldo2_data = {
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

static struct regulator_init_data aquila_ldo3_data = {
	.constraints	= {
		.name		= "VUSB+MIPI_1.1V",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data aquila_ldo4_data = {
	.constraints	= {
		.name		= "VDAC_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
	},
};

static struct regulator_init_data aquila_ldo5_data = {
	.constraints	= {
		.name		= "VTF_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
	},
};

static struct regulator_init_data aquila_ldo6_data = {
	.constraints	= {
		.name		= "VCC_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
	},
};

static struct regulator_init_data aquila_ldo7_data = {
	.constraints	= {
		.name		= "VCC_3.0V",
		.min_uV		= 3000000,
		.max_uV		= 3000000,
		.apply_uV	= 1,
		.boot_on	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data aquila_ldo8_data = {
	.constraints	= {
		.name		= "VUSB+VADC_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data aquila_ldo9_data = {
	.constraints	= {
		.name		= "VCC+VCAM_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data aquila_ldo10_data = {
	.constraints	= {
		.name		= "VPLL_1.1V",
		.min_uV		= 1100000,
		.max_uV		= 1100000,
		.apply_uV	= 1,
		.boot_on	= 1,
	},
};

static struct regulator_init_data aquila_ldo11_data = {
	.constraints	= {
		.name		= "CAM_IO_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data aquila_ldo12_data = {
	.constraints	= {
		.name		= "CAM_ISP_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data aquila_ldo13_data = {
	.constraints	= {
		.name		= "CAM_A_2.8V",
		.min_uV		= 2800000,
		.max_uV		= 2800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data aquila_ldo14_data = {
	.constraints	= {
		.name		= "CAM_CIF_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data aquila_ldo15_data = {
	.constraints	= {
		.name		= "CAM_AF_3.3V",
		.min_uV		= 3300000,
		.max_uV		= 3300000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data aquila_ldo16_data = {
	.constraints	= {
		.name		= "VMIPI_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct regulator_init_data aquila_ldo17_data = {
	.constraints	= {
		.name		= "CAM_8M_1.8V",
		.min_uV		= 1800000,
		.max_uV		= 1800000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

/* BUCK */
static struct regulator_consumer_supply buck1_consumer =
	REGULATOR_SUPPLY("vddarm", NULL);

static struct regulator_consumer_supply buck2_consumer =
	REGULATOR_SUPPLY("vddint", NULL);

static struct regulator_init_data aquila_buck1_data = {
	.constraints	= {
		.name		= "VARM_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &buck1_consumer,
};

static struct regulator_init_data aquila_buck2_data = {
	.constraints	= {
		.name		= "VINT_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.valid_ops_mask	= REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &buck2_consumer,
};

static struct regulator_init_data aquila_buck3_data = {
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

static struct regulator_init_data aquila_buck4_data = {
	.constraints	= {
		.name		= "CAM_CORE_1.2V",
		.min_uV		= 1200000,
		.max_uV		= 1200000,
		.apply_uV	= 1,
		.always_on	= 1,
	},
};

static struct max8998_regulator_data aquila_regulators[] = {
	{ MAX8998_LDO2,  &aquila_ldo2_data },
	{ MAX8998_LDO3,  &aquila_ldo3_data },
	{ MAX8998_LDO4,  &aquila_ldo4_data },
	{ MAX8998_LDO5,  &aquila_ldo5_data },
	{ MAX8998_LDO6,  &aquila_ldo6_data },
	{ MAX8998_LDO7,  &aquila_ldo7_data },
	{ MAX8998_LDO8,  &aquila_ldo8_data },
	{ MAX8998_LDO9,  &aquila_ldo9_data },
	{ MAX8998_LDO10, &aquila_ldo10_data },
	{ MAX8998_LDO11, &aquila_ldo11_data },
	{ MAX8998_LDO12, &aquila_ldo12_data },
	{ MAX8998_LDO13, &aquila_ldo13_data },
	{ MAX8998_LDO14, &aquila_ldo14_data },
	{ MAX8998_LDO15, &aquila_ldo15_data },
	{ MAX8998_LDO16, &aquila_ldo16_data },
	{ MAX8998_LDO17, &aquila_ldo17_data },
	{ MAX8998_BUCK1, &aquila_buck1_data },
	{ MAX8998_BUCK2, &aquila_buck2_data },
	{ MAX8998_BUCK3, &aquila_buck3_data },
	{ MAX8998_BUCK4, &aquila_buck4_data },
};

static struct max8998_platform_data aquila_max8998_pdata = {
	.num_regulators	= ARRAY_SIZE(aquila_regulators),
	.regulators	= aquila_regulators,
	.buck1_set1	= S5PV210_GPH0(3),
	.buck1_set2	= S5PV210_GPH0(4),
	.buck2_set3	= S5PV210_GPH0(5),
	.buck1_voltage1	= 1200000,
	.buck1_voltage2	= 1200000,
	.buck1_voltage3	= 1200000,
	.buck1_voltage4	= 1200000,
	.buck2_voltage1	= 1200000,
	.buck2_voltage2	= 1200000,
};
#endif

static struct regulator_consumer_supply wm8994_fixed_voltage0_supplies[] = {
	REGULATOR_SUPPLY("DBVDD", "5-001a"),
	REGULATOR_SUPPLY("AVDD2", "5-001a"),
	REGULATOR_SUPPLY("CPVDD", "5-001a"),
};

static struct regulator_consumer_supply wm8994_fixed_voltage1_supplies[] = {
	REGULATOR_SUPPLY("SPKVDD1", "5-001a"),
	REGULATOR_SUPPLY("SPKVDD2", "5-001a"),
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

static struct regulator_consumer_supply wm8994_avdd1_supply =
	REGULATOR_SUPPLY("AVDD1", "5-001a");

static struct regulator_consumer_supply wm8994_dcvdd_supply =
	REGULATOR_SUPPLY("DCVDD", "5-001a");

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
static struct i2c_gpio_platform_data aquila_i2c_gpio_pmic_data = {
	.sda_pin	= S5PV210_GPJ4(0),	/* XMSMCSN */
	.scl_pin	= S5PV210_GPJ4(3),	/* XMSMIRQN */
};

static struct platform_device aquila_i2c_gpio_pmic = {
	.name		= "i2c-gpio",
	.id		= AP_I2C_GPIO_PMIC_BUS_4,
	.dev		= {
		.platform_data = &aquila_i2c_gpio_pmic_data,
	},
};

static struct i2c_board_info i2c_gpio_pmic_devs[] __initdata = {
#if defined(CONFIG_REGULATOR_MAX8998) || defined(CONFIG_REGULATOR_MAX8998_MODULE)
	{
		/* 0xCC when SRAD = 0 */
		I2C_BOARD_INFO("max8998", 0xCC >> 1),
		.platform_data = &aquila_max8998_pdata,
	},
#endif
};

/* GPIO I2C AP 1.8V */
#define AP_I2C_GPIO_BUS_5	5
static struct i2c_gpio_platform_data aquila_i2c_gpio5_data = {
	.sda_pin	= S5PV210_MP05(3),	/* XM0ADDR_11 */
	.scl_pin	= S5PV210_MP05(2),	/* XM0ADDR_10 */
};

static struct platform_device aquila_i2c_gpio5 = {
	.name		= "i2c-gpio",
	.id		= AP_I2C_GPIO_BUS_5,
	.dev		= {
		.platform_data	= &aquila_i2c_gpio5_data,
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
static struct gpio_keys_button aquila_gpio_keys_table[] = {
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

static struct gpio_keys_platform_data aquila_gpio_keys_data = {
	.buttons	= aquila_gpio_keys_table,
	.nbuttons	= ARRAY_SIZE(aquila_gpio_keys_table),
};

static struct platform_device aquila_device_gpiokeys = {
	.name = "gpio-keys",
	.dev = {
		.platform_data = &aquila_gpio_keys_data,
	},
};

static void __init aquila_pmic_init(void)
{
	/* AP_PMIC_IRQ: EINT7 */
	s3c_gpio_cfgpin(S5PV210_GPH0(7), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(S5PV210_GPH0(7), S3C_GPIO_PULL_UP);

	/* nPower: EINT22 */
	s3c_gpio_cfgpin(S5PV210_GPH2(6), S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(S5PV210_GPH2(6), S3C_GPIO_PULL_UP);
}

/* MoviNAND */
static struct s3c_sdhci_platdata aquila_hsmmc0_data __initdata = {
	.max_width		= 4,
	.cd_type		= S3C_SDHCI_CD_PERMANENT,
};

/* Wireless LAN */
static struct s3c_sdhci_platdata aquila_hsmmc1_data __initdata = {
	.max_width		= 4,
	.cd_type		= S3C_SDHCI_CD_EXTERNAL,
	/* ext_cd_{init,cleanup} callbacks will be added later */
};

/* External Flash */
#define AQUILA_EXT_FLASH_EN	S5PV210_MP05(4)
#define AQUILA_EXT_FLASH_CD	S5PV210_GPH3(4)
static struct s3c_sdhci_platdata aquila_hsmmc2_data __initdata = {
	.max_width		= 4,
	.cd_type		= S3C_SDHCI_CD_GPIO,
	.ext_cd_gpio		= AQUILA_EXT_FLASH_CD,
	.ext_cd_gpio_invert	= 1,
};

static void aquila_setup_sdhci(void)
{
	gpio_request(AQUILA_EXT_FLASH_EN, "FLASH_EN");
	gpio_direction_output(AQUILA_EXT_FLASH_EN, 1);

	s3c_sdhci0_set_platdata(&aquila_hsmmc0_data);
	s3c_sdhci1_set_platdata(&aquila_hsmmc1_data);
	s3c_sdhci2_set_platdata(&aquila_hsmmc2_data);
};

static struct platform_device *aquila_devices[] __initdata = {
	&aquila_i2c_gpio_pmic,
	&aquila_i2c_gpio5,
	&aquila_device_gpiokeys,
	&s3c_device_fb,
	&s5p_device_onenand,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
	&s3c_device_hsmmc2,
	&s5p_device_fimc0,
	&s5p_device_fimc1,
	&s5p_device_fimc2,
	&s5pv210_device_iis0,
	&wm8994_fixed_voltage0,
	&wm8994_fixed_voltage1,
};

static void __init aquila_sound_init(void)
{
	unsigned int gpio;

	/* CODEC_XTAL_EN
	 *
	 * The Aquila board have a oscillator which provide main clock
	 * to WM8994 codec. The oscillator provide 24MHz clock to WM8994
	 * clock. Set gpio setting of "CODEC_XTAL_EN" to enable a oscillator.
	 * */
	gpio = S5PV210_GPH3(2);		/* XEINT_26 */
	gpio_request(gpio, "CODEC_XTAL_EN");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

	/* Ths main clock of WM8994 codec uses the output of CLKOUT pin.
	 * The CLKOUT[9:8] set to 0x3(XUSBXTI) of 0xE010E000(OTHERS)
	 * because it needs 24MHz clock to operate WM8994 codec.
	 */
	__raw_writel(__raw_readl(S5P_OTHERS) | (0x3 << 8), S5P_OTHERS);
}

static void __init aquila_map_io(void)
{
	s5p_init_io(NULL, 0, S5P_VA_CHIPID);
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(aquila_uartcfgs, ARRAY_SIZE(aquila_uartcfgs));
	s5p_set_timer_source(S5P_PWM3, S5P_PWM4);
}

static void __init aquila_machine_init(void)
{
	/* PMIC */
	aquila_pmic_init();
	i2c_register_board_info(AP_I2C_GPIO_PMIC_BUS_4, i2c_gpio_pmic_devs,
			ARRAY_SIZE(i2c_gpio_pmic_devs));
	/* SDHCI */
	aquila_setup_sdhci();

	s3c_fimc_setname(0, "s5p-fimc");
	s3c_fimc_setname(1, "s5p-fimc");
	s3c_fimc_setname(2, "s5p-fimc");

	/* SOUND */
	aquila_sound_init();
	i2c_register_board_info(AP_I2C_GPIO_BUS_5, i2c_gpio5_devs,
			ARRAY_SIZE(i2c_gpio5_devs));

	/* FB */
	s3c_fb_set_platdata(&aquila_lcd_pdata);

	platform_add_devices(aquila_devices, ARRAY_SIZE(aquila_devices));
}

MACHINE_START(AQUILA, "Aquila")
	/* Maintainers:
	   Marek Szyprowski <m.szyprowski@samsung.com>
	   Kyungmin Park <kyungmin.park@samsung.com> */
	.atag_offset	= 0x100,
	.init_irq	= s5pv210_init_irq,
	.handle_irq	= vic_handle_irq,
	.map_io		= aquila_map_io,
	.init_machine	= aquila_machine_init,
	.timer		= &s5p_timer,
MACHINE_END
