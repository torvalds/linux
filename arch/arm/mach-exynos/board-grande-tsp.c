/*
 * linux/arch/arm/mach-exynos/midas-tsp.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/i2c/mxt224s_grande.h>
#include <linux/regulator/consumer.h>
#include <plat/gpio-cfg.h>

extern bool is_cable_attached;

static struct charging_status_callbacks {
	void (*tsp_set_charging_cable) (int type);
} charging_cbs;

void tsp_register_callback(void *function)
{
	charging_cbs.tsp_set_charging_cable = function;
}

void tsp_read_ta_status(void *ta_status)
{
	*(bool *) ta_status = is_cable_attached;
}

void tsp_charger_infom(bool en)
{
	if (charging_cbs.tsp_set_charging_cable)
		charging_cbs.tsp_set_charging_cable(en);
}

static void mxt224s_power_on(void)
{
	gpio_set_value(GPIO_TSP_EN, 1);
	mdelay(30);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));
	printk(KERN_INFO "mxt224s_power_on is finished\n");
}

EXPORT_SYMBOL(mxt224s_power_on);

static void mxt224s_power_off(void)
{
	gpio_set_value(GPIO_TSP_EN, 0);
	s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_INPUT);
	s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_DOWN);
	printk(KERN_INFO "mxt224s_power_off is finished\n");
}

EXPORT_SYMBOL(mxt224s_power_off);

/*
	Configuration for MXT224-S
*/
#define MXT224S_MAX_MT_FINGERS		5
#define MXT224S_CHRGTIME_BATT		25
#define MXT224S_CHRGTIME_CHRG		60
#define MXT224S_THRESHOLD_BATT		60
#define MXT224S_THRESHOLD_CHRG		70
#define MXT224S_CALCFG_BATT		210
#define MXT224S_CALCFG_CHRG		210


static u8 t7_config_s[] = { GEN_POWERCONFIG_T7,
0x20, 0xFF, 0x32, 0x03
};

static u8 t8_config_s[] = { GEN_ACQUISITIONCONFIG_T8,
0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x0A, 0x1E, 0x00, 0x00
};

static u8 t9_config_s[] = { TOUCH_MULTITOUCHSCREEN_T9,
	0x83, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x60, 0x37, 0x02, 0x07,
	0x0A, 0x0A, 0x01, 0x3F, 0x0A, 0x0F, 0x1E, 0x0A, 0x1F, 0x03,
0xDF, 0x01, 0x0F, 0x0F, 0x19, 0x19, 0x80, 0x00, 0xC0, 0x00,
0x14, 0x0F, 0x00, 0x00, 0x00, 0x00
};

   static u8 t9_config_s_ta[] = { TOUCH_MULTITOUCHSCREEN_T9,
			 0x83, 0x00, 0x00, 0x13, 0x0B, 0x00, 80, 40, 0x02, 0x07,
			 0x0A, 0x0A, 0x01, 65, 0x0A, 0x0F, 0x1E, 0x0A, 0x1F, 0x03,
   0xDF, 0x01, 15, 15, 25, 25, 0x80, 0x00, 0xC0, 0x00,
   0x14, 0x0F, 0x00, 0x00, 0x00, 0x00
 };

static u8 t15_config_s[] = { TOUCH_KEYARRAY_T15,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0
};

static u8 t18_config_s[] = { SPT_COMCONFIG_T18,
	0, 0
};

static u8 t19_config_s[] = { SPT_GPIOPWM_T19,
	0, 0, 0, 0, 0, 0
};

static u8 t23_config_s[] = { TOUCH_PROXIMITY_T23,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0
};

static u8 t25_config_s[] = { SPT_SELFTEST_T25,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0
};

static u8 t40_config_s[] = { PROCI_GRIPSUPPRESSION_T40,
	0, 0, 0, 0, 0
};

static u8 t42_config_s[] = { PROCI_TOUCHSUPPRESSION_T42,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t46_config_s[] = { SPT_CTECONFIG_T46,
0x04, 0x00, 0x10, 0x20, 0x00, 0x01, 0x03, 0x00, 0x00, 0x01
};

static u8 t47_config_s[] = { PROCI_STYLUS_T47,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00
};

static u8 t55_config_s[] = { PROCI_ADAPTIVETHRESHOLD_T55,
	0, 0, 0, 0, 0, 0
};

static u8 t56_config_s[] = { PROCI_SHIELDLESS_T56,
	0x03, 0x00, 0x01, 0x26, 0x0A, 0x0A, 0x0A, 0x0C, 0x0C, 0x0C,
	0x0C, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x10, 0x10, 0x12, 0x12,
	0x12, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00
};

static u8 t56_config_s_ta[] = { PROCI_SHIELDLESS_T56,
	0x03, 0x00, 0x01, 0x1E, 0x0A, 0x0A, 0x0A, 0x0C, 0x0C, 0x0C,
	0x0C, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x10, 0x10, 0x12, 0x12,
	0x12, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00
   };

static u8 t57_config_s[] = { PROCI_EXTRATOUCHSCREENDATA_T57,
	0xE3, 0x0F, 0x00
};

static u8 t61_config_s[] = {SPT_TIMER_T61,
	0x03, 0x00, 0x00, 0x00, 0x00
};

static u8 t62_config_s[] = { PROCG_NOISESUPPRESSION_T62,
0x4F, 0x02, 0x00, 0x16, 0x08, 0x00, 0x00, 0x00, 0x01, 0x00,
0x00, 0x00, 0x00, 0x01, 0x05, 0x00, 0x0A, 0x05, 0x05, 0x80,
0x0F, 0x0F, 0x20, 0x0F, 0x40, 0x10, 0x10, 0x04, 0x64, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x60, 0x28, 0x02, 0x05, 0x01, 0x30,
	0x0A, 0x0F, 0x0F, 0x1E, 0x1E, 0xF6, 0xF6, 0xF2, 0x3E, 0x00,
	0x00, 0x12, 0x0A, 0x00
};

static u8 t62_config_s_ta[] = { PROCG_NOISESUPPRESSION_T62,
0x4F, 0x00, 0x00, 0x16, 0x03, 0x00, 0x00, 0x00, 0x14, 0x00,
0x00, 0x00, 0x00, 0x01, 0x05, 0x00, 0x0A, 0x05, 0x05, 0x80,
0x0F, 0x0F, 0x20, 0x0F, 0x3F, 0x10, 0x10, 0x04, 0x64, 0x00,
0x00, 0x00, 0x00, 0x00, 0x60, 0x28, 0x02, 0x05, 0x01, 0x30,
0x0A, 0x0F, 0x0F, 0x1E, 0x1E, 0xF6, 0xF6, 0xF2, 0x3E, 0x00,
0x00, 0x12, 0x0A, 0x00
};

static u8 end_config_s[] = { RESERVED_T255 };

static const u8 *mxt224s_config[] = {
	t7_config_s,
	t8_config_s,
	t9_config_s,
	t15_config_s,
	t18_config_s,
	t19_config_s,
	t23_config_s,
	t25_config_s,
	t40_config_s,
	t42_config_s,
	t46_config_s,
	t47_config_s,
	t55_config_s,
	t56_config_s,
	t57_config_s,
	t61_config_s,
	t62_config_s,
	end_config_s
};

static struct mxt224s_platform_data mxt224s_data = {
	.max_finger_touches = MXT224S_MAX_MT_FINGERS,
	.gpio_read_done = GPIO_TSP_INT,
	.min_x = 0,
	.max_x = 479,
	.min_y = 0,
	.max_y = 799,
	.min_z = 0,
	.max_z = 255,
	.min_w = 0,
	.max_w = 30,
	.config = mxt224s_config,
	.config_e = mxt224s_config,
	.chrgtime_batt = 0,
	.chrgtime_charging = 0,
	.atchcalst = 0,
	.atchcalsthr = 0,
	.tchthr_batt = 0,
	.tchthr_charging = 0,
	.tchthr_batt_e = 0,
	.tchthr_charging_e = 0,
	.calcfg_batt_e = 0,
	.calcfg_charging_e = 0,
	.atchcalsthr_e = 0,
	.atchfrccalthr_e = 0,
	.atchfrccalratio_e = 0,
	.idlesyncsperx_batt = 0,
	.idlesyncsperx_charging = 0,
	.actvsyncsperx_batt = 0,
	.actvsyncsperx_charging = 0,
	.idleacqint_batt = 0,
	.idleacqint_charging = 0,
	.actacqint_batt = 0,
	.actacqint_charging = 0,
	.xloclip_batt = 0,
	.xloclip_charging = 0,
	.xhiclip_batt = 0,
	.xhiclip_charging = 0,
	.yloclip_batt = 0,
	.yloclip_charging = 0,
	.yhiclip_batt = 0,
	.yhiclip_charging = 0,
	.xedgectrl_batt = 0,
	.xedgectrl_charging = 0,
	.xedgedist_batt = 0,
	.xedgedist_charging = 0,
	.yedgectrl_batt = 0,
	.yedgectrl_charging = 0,
	.yedgedist_batt = 0,
	.yedgedist_charging = 0,
	.tchhyst_batt = 0,
	.tchhyst_charging = 0,
	.t9_config_batt = t9_config_s,
	.t9_config_chrg = t9_config_s_ta,
	.t56_config_batt = t56_config_s,
	.t56_config_chrg = t56_config_s_ta,
	.t62_config_batt = t62_config_s,
	.t62_config_chrg = t62_config_s_ta,
	.power_on = mxt224s_power_on,
	.power_off = mxt224s_power_off,
	.register_cb = tsp_register_callback,
	.read_ta_status = tsp_read_ta_status,
	.config_fw_version = "W899_At_0720",
};

/* I2C3 */
static struct i2c_board_info i2c_devs3[] __initdata = {
	{
	I2C_BOARD_INFO(MXT_DEV_NAME, 0x4b),
	.platform_data = &mxt224s_data},
};


void __init midas_tsp_init(void)
{
#ifndef CONFIG_MACH_NEWTON_BD
	int gpio;
	int ret;
	printk(KERN_INFO "[TSP] midas_tsp_init() is called\n");

	/* TSP_INT: XEINT_4 */
	gpio = GPIO_TSP_INT;
	ret = gpio_request(gpio, "TSP_INT");
	if (ret)
		pr_err("failed to request gpio(TSP_INT)\n");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	/* s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP); */
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

	s5p_register_gpio_interrupt(gpio);
	i2c_devs3[0].irq = gpio_to_irq(gpio);

	printk(KERN_INFO "%s touch : %d\n", __func__, i2c_devs3[0].irq);
#endif

	s3c_gpio_cfgpin(GPIO_TSP_EN, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_EN, S3C_GPIO_PULL_NONE);

	i2c_register_board_info(3, i2c_devs3, ARRAY_SIZE(i2c_devs3));
}


