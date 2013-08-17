/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/i2c.h>
#if defined(CONFIG_TOUCHSCREEN_MXT540E)
#include <linux/i2c/mxt540e.h>
#include <linux/interrupt.h>
#elif defined(CONFIG_TOUCHSCREEN_COASIA)
#include <linux/input/pixcir_ts.h>
#endif
#include <linux/export.h>

#include <plat/devs.h>
#include <plat/gpio-cfg.h>

#include <mach/irqs.h>
#include <mach/hs-iic.h>
#include <plat/iic.h>
#include <mach/regs-gpio.h>

#include "board-smdk5410.h"

#define GPIO_TSP_INT		EXYNOS5410_GPX3(1)
#define GPIO_LEVEL_LOW		0

struct class *sec_class;
EXPORT_SYMBOL(sec_class);

#if defined(CONFIG_TOUCHSCREEN_MXT540E)
static struct charging_status_callbacks {
	void (*tsp_set_charging_cable) (int type);
} charging_cbs;
bool is_cable_attached;

static void tsp_register_callback(void *function)
{
	charging_cbs.tsp_set_charging_cable = function;
}

static void tsp_read_ta_status(void *ta_status)
{
	*(bool *)ta_status = is_cable_attached;
}

static void mxt540e_power_on(void)
{
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));
}

static void mxt540e_power_off(void)
{
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_DOWN);
	gpio_direction_output(GPIO_TSP_INT, GPIO_LEVEL_LOW);
}

#define MXT540E_MAX_MT_FINGERS		10
#define MXT540E_CHRGTIME_BATT		48
#define MXT540E_CHRGTIME_CHRG		48
#define MXT540E_THRESHOLD_BATT		50
#define MXT540E_THRESHOLD_CHRG		40
#define MXT540E_ACTVSYNCSPERX_BATT		24
#define MXT540E_ACTVSYNCSPERX_CHRG		28
#define MXT540E_CALCFG_BATT		98
#define MXT540E_CALCFG_CHRG		114
#define MXT540E_ATCHFRCCALTHR_WAKEUP		8
#define MXT540E_ATCHFRCCALRATIO_WAKEUP		180
#define MXT540E_ATCHFRCCALTHR_NORMAL		40
#define MXT540E_ATCHFRCCALRATIO_NORMAL		55

static u8 t7_config_e[] = { GEN_POWERCONFIG_T7,
	48, 255, 50
};

static u8 t8_config_e[] = { GEN_ACQUISITIONCONFIG_T8,
	MXT540E_CHRGTIME_BATT, 0, 5, 1, 0, 0, 4, 20,
	MXT540E_ATCHFRCCALTHR_WAKEUP, MXT540E_ATCHFRCCALRATIO_WAKEUP
};

static u8 t9_config_e[] = { TOUCH_MULTITOUCHSCREEN_T9,
	131, 0, 0, 16, 26, 0, 192, MXT540E_THRESHOLD_BATT, 2, 6,
	10, 10, 10, 80, MXT540E_MAX_MT_FINGERS, 20, 40, 20, 31, 3,
	255, 4, 3, 3, 2, 2, 136, 60, 136, 40,
	18, 15, 0, 0, 0
};

static u8 t15_config_e[] = { TOUCH_KEYARRAY_T15,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t18_config_e[] = { SPT_COMCONFIG_T18,
	0, 0
};

static u8 t19_config_e[] = { SPT_GPIOPWM_T19,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t24_config_e[] = { PROCI_ONETOUCHGESTUREPROCESSOR_T24,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t25_config_e[] = { SPT_SELFTEST_T25,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t27_config_e[] = { PROCI_TWOTOUCHGESTUREPROCESSOR_T27,
	0, 0, 0, 0, 0, 0, 0
};

static u8 t40_config_e[] = { PROCI_GRIPSUPPRESSION_T40,
	0, 0, 0, 0, 0
};

static u8 t42_config_e[] = { PROCI_TOUCHSUPPRESSION_T42,
	0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t43_config_e[] = { SPT_DIGITIZER_T43,
	0, 0, 0, 0, 0, 0, 0
};

static u8 t46_config_e[] = { SPT_CTECONFIG_T46,
	0, 0, 16, MXT540E_ACTVSYNCSPERX_BATT, 0, 0, 1, 0
};

static u8 t47_config_e[] = { PROCI_STYLUS_T47,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t48_config_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, MXT540E_CALCFG_BATT, 0, 0, 0, 0, 0, 1, 2,
	0, 0, 0, 6, 6, 0, 0, 28, 4, 64,
	10, 0, 20, 6, 0, 30, 0, 0, 0, 0,
	0, 0, 0, 0, 192, MXT540E_THRESHOLD_BATT, 2, 10, 10, 47,
	MXT540E_MAX_MT_FINGERS, 5, 20, 253, 0, 7, 7, 160, 55, 136,
	0, 18, 5, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
};

static u8 t48_config_chrg_e[] = { PROCG_NOISESUPPRESSION_T48,
	3, 132, MXT540E_CALCFG_CHRG, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 6, 6, 0, 0, 36, 4, 64,
	10, 0, 10, 6, 0, 20, 0, 0, 0, 0,
	0, 0, 0, 0, 112, MXT540E_THRESHOLD_CHRG, 2, 10, 5, 47,
	MXT540E_MAX_MT_FINGERS, 5, 20, 253, 0, 7, 7, 160, 55, 136,
	0, 18, 10, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
};

static u8 t52_config_e[] = { TOUCH_PROXKEY_T52,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t55_config_e[] = {ADAPTIVE_T55,
	0, 0, 0, 0, 0, 0
};

static u8 t57_config_e[] = {SPT_GENERICDATA_T57,
	243, 25, 1
};

static u8 t61_config_e[] = {SPT_TIMER_T61,
	0, 0, 0, 0, 0
};

static u8 end_config_e[] = { RESERVED_T255 };

static const u8 *mxt540e_config[] = {
	t7_config_e,
	t8_config_e,
	t9_config_e,
	t15_config_e,
	t18_config_e,
	t19_config_e,
	t24_config_e,
	t25_config_e,
	t27_config_e,
	t40_config_e,
	t42_config_e,
	t43_config_e,
	t46_config_e,
	t47_config_e,
	t48_config_e,
	t52_config_e,
	t55_config_e,
	t57_config_e,
	t61_config_e,
	end_config_e,
};

struct mxt540e_platform_data mxt540e_data = {
	.max_finger_touches = MXT540E_MAX_MT_FINGERS,
	.gpio_read_done = GPIO_TSP_INT,
	.config_e = mxt540e_config,
	.min_x = 0,
	.max_x = 799,
	.min_y = 0,
	.max_y = 1279,
	.min_z = 0,
	.max_z = 255,
	.min_w = 0,
	.max_w = 30,
	.irqf_trigger_type = IRQF_TRIGGER_HIGH,
	.chrgtime_batt = MXT540E_CHRGTIME_BATT,
	.chrgtime_charging = MXT540E_CHRGTIME_CHRG,
	.tchthr_batt = MXT540E_THRESHOLD_BATT,
	.tchthr_charging = MXT540E_THRESHOLD_CHRG,
	.actvsyncsperx_batt = MXT540E_ACTVSYNCSPERX_BATT,
	.actvsyncsperx_charging = MXT540E_ACTVSYNCSPERX_CHRG,
	.calcfg_batt_e = MXT540E_CALCFG_BATT,
	.calcfg_charging_e = MXT540E_CALCFG_CHRG,
	.atchfrccalthr_e = MXT540E_ATCHFRCCALTHR_NORMAL,
	.atchfrccalratio_e = MXT540E_ATCHFRCCALRATIO_NORMAL,
	.t48_config_batt_e = t48_config_e,
	.t48_config_chrg_e = t48_config_chrg_e,
	.power_on = mxt540e_power_on,
	.power_off = mxt540e_power_off,
	.register_cb = tsp_register_callback,
	.read_ta_status = tsp_read_ta_status,
};
#endif

static void smdk5410_gpio_keys_config_setup(void)
{
	gpio_request_one(EXYNOS5410_GPX2(0), GPIOF_OUT_INIT_LOW, "GPX2");

	s3c_gpio_setpull(EXYNOS5410_GPX1(3), S3C_GPIO_PULL_UP);
	s3c_gpio_setpull(EXYNOS5410_GPX1(4), S3C_GPIO_PULL_UP);
}

static struct gpio_keys_button smdk5410_button[] = {
	{
		.code = KEY_POWER,
		.gpio = EXYNOS5410_GPX0(0),
		.active_low = 1,
		.wakeup = 1,
	}, {
		.code = KEY_VOLUMEDOWN,
		.gpio = EXYNOS5410_GPX1(3),
		.active_low = 1,
	}, {
		.code = KEY_VOLUMEUP,
		.gpio = EXYNOS5410_GPX1(4),
		.active_low = 1,
	},
};

static struct gpio_keys_platform_data smdk5410_gpiokeys_platform_data = {
	smdk5410_button,
	ARRAY_SIZE(smdk5410_button),
};

static struct platform_device smdk5410_gpio_keys = {
	.name	= "gpio-keys",
	.dev	= {
		.platform_data = &smdk5410_gpiokeys_platform_data,
	},
};

#if defined(CONFIG_TOUCHSCREEN_MXT540E)
static struct i2c_board_info i2c_devs_touch[] __initdata = {
	{
		I2C_BOARD_INFO(MXT540E_DEV_NAME, 0x4C),
		.irq		= IRQ_EINT(25),
		.platform_data	= &mxt540e_data,
	},
};
#elif defined(CONFIG_TOUCHSCREEN_COASIA)
struct s3c2410_platform_i2c i2c_data_coasia  __initdata = {
		.bus_num        = 3,
		.flags          = 0,
		.slave_addr     = 0x10,
		.frequency      = 200*1000,
		.sda_delay      = 100,
};

static struct pixcir_ts_platform_data smdk5410_ts_data = {
		.x_max = 2560,
		.y_max = 1600,
};
static struct i2c_board_info i2c_devs_touch[] __initdata = {
	{
		I2C_BOARD_INFO("pixcir_ts", 0x5C),
		.irq		= IRQ_EINT(21),
		.platform_data = &smdk5410_ts_data,
	},
};

static void exynos_coasia_touch_init(void)
{
	int gpio;
	gpio = EXYNOS5_GPX2(4);
	if (gpio_request(gpio, "GPX2")) {
		pr_err("%s : TS_RST request port error\n", __func__);
	} else {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
		gpio_direction_output(gpio, 0);
		usleep_range(20000, 21000);
		gpio_direction_output(gpio, 1);
		gpio_free(gpio);
	}
}
#endif
static struct platform_device *smdk5410_input_devices[] __initdata = {
#if defined(CONFIG_TOUCHSCREEN_MXT540E)
	&exynos5_device_hs_i2c3,
#elif defined(CONFIG_TOUCHSCREEN_COASIA)
	&s3c_device_i2c3,
#endif
	&smdk5410_gpio_keys,
};

void __init exynos5_smdk5410_input_init(void)
{
	sec_class = class_create(THIS_MODULE, "sec");

#if defined(CONFIG_TOUCHSCREEN_MXT540E)
	exynos5_hs_i2c3_set_platdata(NULL);
	i2c_register_board_info(7, i2c_devs_touch, ARRAY_SIZE(i2c_devs_touch));
#elif defined(CONFIG_TOUCHSCREEN_COASIA)
	s3c_i2c3_set_platdata(&i2c_data_coasia);
	i2c_register_board_info(3, i2c_devs_touch, ARRAY_SIZE(i2c_devs_touch));
	exynos_coasia_touch_init();
#endif
	smdk5410_gpio_keys_config_setup();
	platform_add_devices(smdk5410_input_devices,
			ARRAY_SIZE(smdk5410_input_devices));
}
