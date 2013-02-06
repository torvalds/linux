/*
 *  arch/arm/mach-exynos/p4-input.c
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <plat/gpio-cfg.h>
#include <plat/iic.h>

#if defined(CONFIG_TOUCHSCREEN_SYNAPTICS_S7301)
#include <linux/synaptics_s7301.h>
static bool have_tsp_ldo;
static struct charger_callbacks *charger_callbacks;

void synaptics_ts_charger_infom(bool en)
{
	if (charger_callbacks && charger_callbacks->inform_charger)
		charger_callbacks->inform_charger(charger_callbacks, en);
}

static void synaptics_ts_register_callback(struct charger_callbacks *cb)
{
	charger_callbacks = cb;
	printk(KERN_DEBUG "[TSP] %s\n", __func__);
}

static int synaptics_ts_set_power(bool en)
{
	if (!have_tsp_ldo)
		return -1;
	printk(KERN_DEBUG "[TSP] %s(%d)\n", __func__, en);

	if (en) {
		s3c_gpio_cfgpin(GPIO_TSP_SDA_18V, S3C_GPIO_SFN(0x3));
		s3c_gpio_setpull(GPIO_TSP_SDA_18V, S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(GPIO_TSP_SCL_18V, S3C_GPIO_SFN(0x3));
		s3c_gpio_setpull(GPIO_TSP_SCL_18V, S3C_GPIO_PULL_UP);

		s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
		gpio_set_value(GPIO_TSP_LDO_ON, 1);
		s3c_gpio_cfgpin(GPIO_TSP_RST, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_RST, S3C_GPIO_PULL_NONE);
		gpio_set_value(GPIO_TSP_RST, 1);
		s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
		s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_SFN(0xf));
	} else {
		s3c_gpio_cfgpin(GPIO_TSP_SDA_18V, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_SDA_18V, S3C_GPIO_PULL_NONE);
		gpio_set_value(GPIO_TSP_SDA_18V, 0);
		s3c_gpio_cfgpin(GPIO_TSP_SCL_18V, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_SCL_18V, S3C_GPIO_PULL_NONE);
		gpio_set_value(GPIO_TSP_SCL_18V, 0);

		s3c_gpio_cfgpin(GPIO_TSP_INT, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_INT, S3C_GPIO_PULL_NONE);
		gpio_set_value(GPIO_TSP_INT, 0);
		s3c_gpio_cfgpin(GPIO_TSP_RST, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_RST, S3C_GPIO_PULL_NONE);
		gpio_set_value(GPIO_TSP_RST, 0);
		s3c_gpio_cfgpin(GPIO_TSP_LDO_ON, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_TSP_LDO_ON, S3C_GPIO_PULL_NONE);
		gpio_set_value(GPIO_TSP_LDO_ON, 0);
	}

	return 0;
}

static void synaptics_ts_reset(void)
{
	printk(KERN_DEBUG "[TSP] %s\n", __func__);
	s3c_gpio_cfgpin(GPIO_TSP_RST, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(GPIO_TSP_RST, S3C_GPIO_PULL_NONE);
	gpio_set_value(GPIO_TSP_RST, 0);
	msleep(100);
	gpio_set_value(GPIO_TSP_RST, 1);
}

static struct synaptics_platform_data synaptics_ts_pdata = {
	.gpio_attn = GPIO_TSP_INT,
	.max_x = 1279,
	.max_y = 799,
	.max_pressure = 255,
	.max_width = 100,
	.x_line = 27,
	.y_line = 42,
	.set_power = synaptics_ts_set_power,
	.hw_reset = synaptics_ts_reset,
	.register_cb = synaptics_ts_register_callback,
};

static struct i2c_board_info i2c_synaptics[] __initdata = {
	{
		I2C_BOARD_INFO(SYNAPTICS_TS_NAME,
			SYNAPTICS_TS_ADDR),
		.platform_data = &synaptics_ts_pdata,
	},
};
#endif	/* CONFIG_TOUCHSCREEN_SYNAPTICS_S7301 */

#if defined(CONFIG_TOUCHSCREEN_ATMEL_MXT1664S)
#include <linux/i2c/mxt1664s.h>
static struct mxt_callbacks *mxt_callbacks;
static u32 hw_rev;

/* Caution: Note10(p4note) has various H/W revision and each revision
 * has different TSP tunning data.
 * So If you add or change the tunning data, please refer the below
 * simple description.
 *
 * H/W revision		Project
 * ~ 0.6			3G note10(N8010) final revision is 0.6
 * 0.7 ~ 0.8		Reserved
 * 0.9				LTE model such as N8020
 */

static u8 inform_data_rev6[] = {0,
	7, 0, 48, 255,
	7, 1, 11, 255,
	47, 1, 35, 40,
	55, 0, 1, 0,
	55, 1, 25, 11,
	55, 2, 7, 3,
	56, 36, 0, 3,
	62, 1, 0, 1,
	62, 10, 5, 21,
	62, 12, 5, 21,
	62, 19, 130, 62,
	62, 20, 12, 20,
};

static u8 inform_data_rev5[] = {0,
	7, 0, 48, 255,
	7, 1, 11, 255,
	46, 2, 10, 24,
	46, 3, 16, 24,
	47, 1, 35, 40,
	56, 36, 0, 3,
	62, 1, 0, 1,
	62, 9, 16, 20,
	62, 11, 16, 20,
	62, 13, 16, 20,
	62, 13, 0, 21,
	62, 19, 128, 112,
	62, 20, 20, 30,
};

/* Added for the LTE model */
static u8 inform_data_rev9[] = {0,
	7, 0, 48, 255,
	7, 1, 11, 255,
	46, 3, 16, 24,
	47, 1, 35, 45,
	47, 9, 16, 24,
	55, 0, 1, 0,
	55, 1, 25, 11,
	55, 2, 7, 3,
	56, 3, 45, 40,
	56, 36, 0, 3,
	62, 3, 0, 23,
	62, 7, 90, 18,
	62, 8, 1, 8,
	62, 10, 0, 8,
	62, 12, 0, 8,
	62, 13, 1, 0,
	62, 19, 136, 100,
	62, 21, 35, 45,
	62, 25, 16, 24,
	62, 26, 16, 24,
};

static u8 inform_data[] = {0,
	7, 0, 48, 255,
	7, 1, 11, 255,
	8, 0, 160, 90,
	46, 3, 16, 24,
	47, 9, 16, 24,
	55, 1, 25, 11,
	55, 2, 7, 3,
	56, 36, 0, 3,
	62, 1, 0, 1,
	62, 8, 25, 40,
	62, 9, 15, 40,
	62, 10, 22, 35,
	62, 11, 15, 40,
	62, 12, 22, 35,
	62, 13, 15, 40,
	62, 19, 136, 80,
	62, 20, 15, 5,
	62, 21, 40, 45,
	62, 22, 24, 32,
	62, 25, 16, 24,
	62, 26, 16, 24,
};

void ts_charger_infom(bool en)
{
	if (mxt_callbacks && mxt_callbacks->inform_charger)
		mxt_callbacks->inform_charger(mxt_callbacks, en);
}

static u8 *ts_register_callback(struct mxt_callbacks *cb)
{
	mxt_callbacks = cb;

	inform_data[0] = sizeof(inform_data);
	inform_data_rev5[0] = sizeof(inform_data_rev5);
	inform_data_rev6[0] = sizeof(inform_data_rev6);
	inform_data_rev9[0] = sizeof(inform_data_rev9);

	if (0x5 == hw_rev)
		return inform_data_rev5;
	else if (0x6 == hw_rev)
		return inform_data_rev6;
	else if (0x9 <= hw_rev)
		return inform_data_rev9;
	else
		return inform_data;
}

static int ts_power_on(void)
{
	int gpio = 0;

	gpio = GPIO_TSP_SDA_18V;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 1);

	gpio = GPIO_TSP_SCL_18V;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 1);

	gpio = GPIO_TSP_LDO_ON;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 1);

	gpio = GPIO_TSP_LDO_ON1;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 1);

	msleep(20);

	gpio = GPIO_TSP_LDO_ON2;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 1);

	msleep(20);
	gpio = GPIO_TSP_RST;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 1);

	/* touch interrupt pin */
	gpio = GPIO_TSP_INT;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

#if 0
	msleep(MXT_1664S_HW_RESET_TIME);
#endif

	gpio = GPIO_TSP_SDA_18V;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);

	gpio = GPIO_TSP_SCL_18V;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0x3));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);

	printk(KERN_ERR "mxt_power_on is finished\n");

	return 0;
}

static int ts_power_off(void)
{
	int gpio = 0;

	gpio = GPIO_TSP_SDA_18V;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 0);

	gpio = GPIO_TSP_SCL_18V;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 0);

	/* touch xvdd en pin */
	gpio = GPIO_TSP_LDO_ON2;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 0);

	gpio = GPIO_TSP_LDO_ON1;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 0);

	gpio = GPIO_TSP_LDO_ON;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 0);

	/* touch interrupt pin */
	gpio = GPIO_TSP_INT;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_DOWN);

	/* touch reset pin */
	gpio = GPIO_TSP_RST;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_OUTPUT);
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	gpio_set_value(gpio, 0);

	printk(KERN_ERR "mxt_power_off is finished\n");

	return 0;
}

static int ts_power_reset(void)
{
	ts_power_off();
	msleep(100);
	ts_power_on();
	msleep(300);
	return 0;
}

/*
	Configuration for MXT1664-S
*/
#define MXT1664S_CONFIG_DATE		"N80XX_ATM_0703"
#define MXT1664S_CONFIG_DATE_FOR_OVER_HW9	"N80XX_LTE_ATM_0905"

#define MXT1664S_MAX_MT_FINGERS	10
#define MXT1664S_BLEN_BATT		112
#define MXT1664S_CHRGTIME_BATT	180
#define MXT1664S_THRESHOLD_BATT	65
#define P4_NOTE_X_NUM				27
#define P4_NOTE_Y_NUM				42

static u8 t7_config_s[] = { GEN_POWERCONFIG_T7,
	255, 255, 150, 3
};

static u8 t8_config_s[] = { GEN_ACQUISITIONCONFIG_T8,
	MXT1664S_CHRGTIME_BATT, 0, 5, 10, 0, 0, 255, 255, 0, 0
};

static u8 t9_config_s[] = { TOUCH_MULTITOUCHSCREEN_T9,
	0x8B, 0, 0, P4_NOTE_X_NUM, P4_NOTE_Y_NUM,
	0, MXT1664S_BLEN_BATT, MXT1664S_THRESHOLD_BATT, 1, 1,
	10, 15, 1, 65, MXT1664S_MAX_MT_FINGERS, 20, 30, 20, 255, 15,
	255, 15, 5, 246, 5, 5, 0, 0, 0, 0,
	32, 20, 51, 53, 0, 1
};

static u8 t15_config_s[] = { TOUCH_KEYARRAY_T15,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0
};

static u8 t18_config_s[] = { SPT_COMCONFIG_T18,
	0, 0
};

static u8 t24_config_s[] = {
	PROCI_ONETOUCHGESTUREPROCESSOR_T24,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0
};

static u8 t25_config_s[] = {
	SPT_SELFTEST_T25,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 200
};

static u8 t27_config_s[] = {
	PROCI_TWOTOUCHGESTUREPROCESSOR_T27,
	0, 0, 0, 0, 0, 0, 0
};

static u8 t40_config_s[] = { PROCI_GRIPSUPPRESSION_T40,
	0x11, 3, 55, 0, 0
};

static u8 t42_config_s[] = { PROCI_TOUCHSUPPRESSION_T42,
	0, 42, 50, 50, 127, 0, 0, 0, 5, 5
};

static u8 t43_config_s[] = { SPT_DIGITIZER_T43,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0
};

static u8 t46_config_s[] = { SPT_CTECONFIG_T46,
	4, 0, 10, 16, 0, 0, 1, 0, 0, 0,
	15
};

static u8 t47_config_s[] = { PROCI_STYLUS_T47,
	73, 40, 60, 15, 10, 40, 0, 120, 1, 16,
	0, 0, 15
};

static u8 t55_config_s[] = {ADAPTIVE_T55,
	1, 25, 7, 10, 20, 1, 0
};

static u8 t56_config_s[] = {PROCI_SHIELDLESS_T56,
	3, 0, 1, 55, 25, 25, 25, 25, 25, 25,
	24, 24, 24, 23, 23, 23, 22, 22, 22, 21,
	21, 20, 20, 20, 19, 19, 18, 18, 18, 17,
	17, 0, 0, 0, 0, 0, 0, 128, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0
};

static u8 t57_config_s[] = {PROCI_EXTRATOUCHSCREENDATA_T57,
	0xe3, 25, 0
};

static u8 t61_config_s[] = {SPT_TIMER_T61,
	0, 0, 0, 0, 0
};

static u8 t62_config_s[] = {PROCG_NOISESUPPRESSION_T62,
	3, 0, 0, 23, 10, 0, 0, 0, 25, 0,
	5, 0, 5, 0, 2, 0, 5, 5, 10, 130,
	12, 40, 32, 20, 63, 16, 16, 4, 100, 0,
	0, 0, 0, 0, 60, 40, 2, 15, 1, 66,
	10, 20, 30, 20, 15, 5, 5, 0, 0, 0,
	0, 60, 15, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0
};

static u8 end_config_s[] = { RESERVED_T255 };

static const u8 *MXT1644S_config[] = {
	t7_config_s,
	t8_config_s,
	t9_config_s,
	t15_config_s,
	t18_config_s,
	t24_config_s,
	t25_config_s,
	t27_config_s,
	t40_config_s,
	t42_config_s,
	t43_config_s,
	t46_config_s,
	t47_config_s,
	t55_config_s,
	t56_config_s,
	t57_config_s,
	t61_config_s,
	t62_config_s,
	end_config_s,
};

static struct mxt_platform_data mxt1664s_pdata = {
	.max_finger_touches = MXT1664S_MAX_MT_FINGERS,
	.gpio_read_done = GPIO_TSP_INT,
	.min_x = 0,
	.max_x = 4095,
	.min_y = 0,
	.max_y = 4095,
	.min_z = 0,
	.max_z = 255,
	.min_w = 0,
	.max_w = 255,
	.config = MXT1644S_config,
	.power_on = ts_power_on,
	.power_off = ts_power_off,
	.power_reset = ts_power_reset,
	.boot_address = 0x26,
	.register_cb = ts_register_callback,
	.config_version = MXT1664S_CONFIG_DATE,
};

static struct i2c_board_info i2c_mxt1664s[] __initdata = {
	{
		I2C_BOARD_INFO(MXT_DEV_NAME, 0x4A),
		.platform_data = &mxt1664s_pdata,
	},
};
#endif

static void switch_config(u32 rev)
{
	int i = 0;

	/* the number of the array should be added by 1 */
	if (0x5 == rev) {
		t8_config_s[1] = 150;

		t9_config_s[9] = 1;
		t9_config_s[11] = 0;
		t9_config_s[12] = 5;
		t9_config_s[16] = 10;
		t9_config_s[17] = 20;
		t9_config_s[32] = 15;

		t47_config_s[2] = 35;
		t47_config_s[4] = 10;
		t47_config_s[5] = 2;
		t47_config_s[6] = 30;
		t47_config_s[10] = 24;

		t55_config_s[1] = 0;

		for (i = 5; i < 32; i++)
			t56_config_s[i] += 2;

		t62_config_s[9] = 20;
		t62_config_s[10] = 16;
		t62_config_s[11] = 0;
		t62_config_s[12] = 16;
		t62_config_s[13] = 0;
		t62_config_s[14] = 16;
		t62_config_s[20] = 128;
		t62_config_s[21] = 20;
		t62_config_s[22] = 10;
		t62_config_s[23] = 40;
		t62_config_s[24] = 10;
		t62_config_s[25] = 64;
		t62_config_s[26] = 24;
		t62_config_s[27] = 24;
		t62_config_s[35] = 64;
		t62_config_s[36] = 45;
	} else if (0x9 <= rev) {
		u8 tmp = 0;
		t7_config_s[1] = 48;
		t7_config_s[2] = 11;

		t8_config_s[1] = 1;

		t9_config_s[7] = 116;
		t9_config_s[8] = 55;
		t9_config_s[14] = 50;
		t9_config_s[27] = 64;

		t40_config_s[4] = 2;
		t40_config_s[5] = 2;

		t46_config_s[11] = 11;

		t47_config_s[2] = 35;

		t56_config_s[4] = 45;
		tmp = 22;
		for (i = 5; i < 21; i++) {
			if (1 == i % 4)
				tmp--;
			t56_config_s[i] = tmp;
		}

		for (i = 21; i < 28; i++)
			t56_config_s[i] = 17;

		for (i = 28; i < 31; i++)
			t56_config_s[i] = 16;

		t56_config_s[39] = 1;

		t62_config_s[1] = 125;
		t62_config_s[2] = 1;
		t62_config_s[4] = 0;
		t62_config_s[8] = 90;
		t62_config_s[9] = 1;
		t62_config_s[11] = 0;
		t62_config_s[13] = 0;
		t62_config_s[14] = 1;
		t62_config_s[20] = 136;
		t62_config_s[22] = 35;
		t62_config_s[35] = 80;
		t62_config_s[36] = 40;
		t62_config_s[38] = 5;
		t62_config_s[40] = 50;
		t62_config_s[42] = 30;
		t62_config_s[43] = 40;
		t62_config_s[44] = 10;
		t62_config_s[45] = 0;
		t62_config_s[48] = 30;
		t62_config_s[49] = 30;
		t62_config_s[53] = 20;

		/* Change Config Name for LTE */
		mxt1664s_pdata.config_version =
			MXT1664S_CONFIG_DATE_FOR_OVER_HW9;
	}
}

void __init p4_tsp_init(u32 system_rev)
{
	int gpio = 0, irq = 0;
	hw_rev = system_rev;

	printk(KERN_DEBUG "[TSP] %s rev : %u\n",
		__func__, hw_rev);

	printk(KERN_DEBUG "[TSP] TSP IC : %s\n",
		(5 <= hw_rev) ? "Atmel" : "Synaptics");

	gpio = GPIO_TSP_RST;
	gpio_request(gpio, "TSP_RST");
	gpio_direction_output(gpio, 1);
	gpio_export(gpio, 0);

	gpio = GPIO_TSP_LDO_ON;
	gpio_request(gpio, "TSP_LDO_ON");
	gpio_direction_output(gpio, 1);
	gpio_export(gpio, 0);

	if (5 <= hw_rev) {
		gpio = GPIO_TSP_LDO_ON1;
		gpio_request(gpio, "TSP_LDO_ON1");
		gpio_direction_output(gpio, 1);
		gpio_export(gpio, 0);

		gpio = GPIO_TSP_LDO_ON2;
		gpio_request(gpio, "TSP_LDO_ON2");
		gpio_direction_output(gpio, 1);
		gpio_export(gpio, 0);

		switch_config(hw_rev);
	} else if (1 <= hw_rev)
		have_tsp_ldo = true;

	gpio = GPIO_TSP_INT;
	gpio_request(gpio, "TSP_INT");
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
	s5p_register_gpio_interrupt(gpio);
	irq = gpio_to_irq(gpio);

#ifdef CONFIG_S3C_DEV_I2C3
	s3c_i2c3_set_platdata(NULL);

#if defined(CONFIG_TOUCHSCREEN_ATMEL_MXT1664S) && \
	defined(CONFIG_TOUCHSCREEN_SYNAPTICS_S7301)
	if (5 <= system_rev) {
		i2c_mxt1664s[0].irq = irq;
		i2c_register_board_info(3, i2c_mxt1664s,
			ARRAY_SIZE(i2c_mxt1664s));
	} else {
		i2c_synaptics[0].irq = irq;
		i2c_register_board_info(3, i2c_synaptics,
			ARRAY_SIZE(i2c_synaptics));
	}
#endif
#endif	/* CONFIG_S3C_DEV_I2C3 */

}

#if defined(CONFIG_EPEN_WACOM_G5SP)
#include <linux/wacom_i2c.h>
static struct wacom_g5_callbacks *wacom_callbacks;
static int wacom_init_hw(void);
static int wacom_suspend_hw(void);
static int wacom_resume_hw(void);
static int wacom_early_suspend_hw(void);
static int wacom_late_resume_hw(void);
static int wacom_reset_hw(void);
static void wacom_compulsory_flash_mode(bool en);
static void wacom_register_callbacks(struct wacom_g5_callbacks *cb);

static struct wacom_g5_platform_data wacom_platform_data = {
	.x_invert = 0,
	.y_invert = 0,
	.xy_switch = 0,
	.gpio_pendct = GPIO_PEN_PDCT_18V,
#ifdef WACOM_PEN_DETECT
	.gpio_pen_insert = GPIO_S_PEN_IRQ,
#endif
#ifdef WACOM_HAVE_FWE_PIN
	.compulsory_flash_mode = wacom_compulsory_flash_mode,
#endif
	.init_platform_hw = wacom_init_hw,
	.suspend_platform_hw = wacom_suspend_hw,
	.resume_platform_hw = wacom_resume_hw,
	.early_suspend_platform_hw = wacom_early_suspend_hw,
	.late_resume_platform_hw = wacom_late_resume_hw,
	.reset_platform_hw = wacom_reset_hw,
	.register_cb = wacom_register_callbacks,
};

static struct i2c_board_info i2c_devs6[] __initdata = {
	{
		I2C_BOARD_INFO("wacom_g5sp_i2c", 0x56),
		.platform_data = &wacom_platform_data,
	},
};

static void wacom_register_callbacks(struct wacom_g5_callbacks *cb)
{
	wacom_callbacks = cb;
};

static int wacom_init_hw(void)
{
	int ret;
	ret = gpio_request(GPIO_PEN_LDO_EN, "PEN_LDO_EN");
	if (ret) {
		printk(KERN_ERR "[E-PEN] faile to request gpio(GPIO_PEN_LDO_EN)\n");
		return ret;
	}
	s3c_gpio_cfgpin(GPIO_PEN_LDO_EN, S3C_GPIO_SFN(0x1));
	s3c_gpio_setpull(GPIO_PEN_LDO_EN, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_PEN_LDO_EN, 0);

	ret = gpio_request(GPIO_PEN_PDCT_18V, "PEN_PDCT");
	if (ret) {
		printk(KERN_ERR "[E-PEN] faile to request gpio(GPIO_PEN_PDCT_18V)\n");
		return ret;
	}
	s3c_gpio_cfgpin(GPIO_PEN_PDCT_18V, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(GPIO_PEN_PDCT_18V, S3C_GPIO_PULL_UP);

	ret = gpio_request(GPIO_PEN_IRQ_18V, "PEN_IRQ");
	if (ret) {
		printk(KERN_ERR "[E-PEN] faile to request gpio(GPIO_PEN_IRQ_18V)\n");
		return ret;
	}
	s3c_gpio_cfgpin(GPIO_PEN_IRQ_18V, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(GPIO_PEN_IRQ_18V, S3C_GPIO_PULL_DOWN);
	s5p_register_gpio_interrupt(GPIO_PEN_IRQ_18V);
	i2c_devs6[0].irq = gpio_to_irq(GPIO_PEN_IRQ_18V);

#ifdef WACOM_PEN_DETECT
	s3c_gpio_cfgpin(GPIO_S_PEN_IRQ, S3C_GPIO_SFN(0xf));
	s3c_gpio_setpull(GPIO_S_PEN_IRQ, S3C_GPIO_PULL_UP);
#endif

#ifdef WACOM_HAVE_FWE_PIN
	ret = gpio_request(GPIO_PEN_FWE0, "GPIO_PEN_FWE0");
	if (ret) {
		printk(KERN_ERR "[E-PEN] faile to request gpio(GPIO_PEN_FWE0)\n");
		return ret;
	}
	s3c_gpio_cfgpin(GPIO_PEN_FWE0, S3C_GPIO_SFN(0x1));
	s3c_gpio_setpull(GPIO_PEN_FWE0, S3C_GPIO_PULL_NONE);
	gpio_direction_output(GPIO_PEN_FWE0, 0);
#endif

	return 0;
}

#ifdef WACOM_HAVE_FWE_PIN
static void wacom_compulsory_flash_mode(bool en)
{
	gpio_set_value(GPIO_PEN_FWE0, en);
}

#endif

static int wacom_suspend_hw(void)
{
	return wacom_early_suspend_hw();
}

static int wacom_resume_hw(void)
{
	return wacom_late_resume_hw();
}

static int wacom_early_suspend_hw(void)
{
	gpio_set_value(GPIO_PEN_LDO_EN, 0);
	return 0;
}

static int wacom_late_resume_hw(void)
{
	gpio_set_value(GPIO_PEN_LDO_EN, 1);
	return 0;
}

static int wacom_reset_hw(void)
{
	return 0;
}

void __init p4_wacom_init(void)
{
	wacom_init_hw();
#ifdef CONFIG_S3C_DEV_I2C6
	s3c_i2c6_set_platdata(NULL);
	i2c_register_board_info(6, i2c_devs6, ARRAY_SIZE(i2c_devs6));
#endif
}
#endif	/* CONFIG_EPEN_WACOM_G5SP */

#if defined(CONFIG_KEYBOARD_GPIO)
#include <mach/sec_debug.h>
#include <linux/gpio_keys.h>
#define GPIO_KEYS(_code, _gpio, _active_low, _iswake, _hook)	\
{							\
	.code = _code,					\
	.gpio = _gpio,					\
	.active_low = _active_low,			\
	.type = EV_KEY,					\
	.wakeup = _iswake,				\
	.debounce_interval = 10,			\
	.isr_hook = _hook,				\
	.value = 1					\
}

struct gpio_keys_button p4_buttons[] = {
	GPIO_KEYS(KEY_VOLUMEUP, GPIO_VOL_UP,
		  1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_VOLUMEDOWN, GPIO_VOL_DOWN,
		  1, 1, sec_debug_check_crash_key),
	GPIO_KEYS(KEY_POWER, GPIO_nPOWER,
		  1, 1, sec_debug_check_crash_key),
};

struct gpio_keys_platform_data p4_gpiokeys_platform_data = {
	p4_buttons,
	ARRAY_SIZE(p4_buttons),
};

static struct platform_device p4_keypad = {
	.name	= "gpio-keys",
	.dev	= {
		.platform_data = &p4_gpiokeys_platform_data,
	},
};
#endif
void __init p4_key_init(void)
{
#if defined(CONFIG_KEYBOARD_GPIO)
	platform_device_register(&p4_keypad);
#endif
}

