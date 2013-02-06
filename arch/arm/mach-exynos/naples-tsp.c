/*
 * linux/arch/arm/mach-exynos/naples-tsp.c
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
#include <linux/regulator/consumer.h>
#include <plat/gpio-cfg.h>
#include <linux/delay.h>
#include <mach/naples-tsp.h>
#ifdef CONFIG_TOUCHSCREEN_ATMEL_MXT224
#include <linux/i2c/mxt224.h>
#define TSP_IRQ_READY_DELAY 45
/*-------------MXT224  TOUCH DRIVER by Xtopher----------*/

#define MXT224_MAX_MT_FINGERS 10
/*
  Configuration for MXT224-E
*/
#define MXT224E_THRESHOLD_BATT		50
#define MXT224E_THRESHOLD_CHRG		40
#define MXT224E_CALCFG_BATT		0x42
#define MXT224E_CALCFG_CHRG		0x52
#define MXT224E_ATCHFRCCALTHR_NORMAL		40
#define MXT224E_ATCHFRCCALRATIO_NORMAL		55
#define MXT224E_GHRGTIME_BATT		27
#define MXT224E_GHRGTIME_CHRG		22
#define MXT224E_ATCHCALST		4
#define MXT224E_ATCHCALTHR		35
#define MXT224E_BLEN_BATT		32
#define MXT224E_BLEN_CHRG		16
#define MXT224E_MOVFILTER_BATT		46
#define MXT224E_MOVFILTER_CHRG		46
#define MXT224E_ACTVSYNCSPERX_NORMAL		32
#define MXT224E_NEXTTCHDI_NORMAL		0

static u8 t7_config_e[] = {GEN_POWERCONFIG_T7,
				48,		/* IDLEACQINT */
				255,	/* ACTVACQINT */
				25/* ACTV2IDLETO: 25 * 200ms = 5s */};
static u8 t8_config_e[] = {GEN_ACQUISITIONCONFIG_T8,
				27, 0, 5, 1, 0, 0, 5, 35, 40, 55};

/* NEXTTCHDI added */
static u8 t9_config_e[] = {TOUCH_MULTITOUCHSCREEN_T9,
				139, 0, 0, 19, 11, 0, 32, 50, 2, 0,
				10,
				15,	/* MOVHYSTI */
				1, 46, MXT224_MAX_MT_FINGERS, 5, 40, 10, 191, 3,
				27, 2, 10, 10, 10, 10, 143, 40, 143, 80,
				18, 15, 50, 50, 3};

static u8 t15_config_e[] = {TOUCH_KEYARRAY_T15, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0};
static u8 t18_config_e[] = {SPT_COMCONFIG_T18, 0, 0};
static u8 t23_config_e[] = {TOUCH_PROXIMITY_T23, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static u8 t25_config_e[] = {SPT_SELFTEST_T25, 0, 0, 0, 0, 0, 0,
	0, 0};
static u8 t40_config_e[] = {PROCI_GRIPSUPPRESSION_T40, 0, 0,
	0, 0, 0};
static u8 t42_config_e[] = {PROCI_TOUCHSUPPRESSION_T42, 0,
	0, 0, 0, 0, 0, 0, 0};
static u8 t46_config_e[] = {SPT_CTECONFIG_T46, 0, 3, 24, 32, 0,
	0, 1, 0, 0};
static u8 t47_config_e[] = {PROCI_STYLUS_T47, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0};

static u8 t38_config_e[] = {SPT_USERDATA_T38, 0, 1, 15, 19, 45, 40, 0, 0};

static u8 t48_config_chrg_e[] = {PROCG_NOISESUPPRESSION_T48,
	3, 132, 0x52, 0, 0, 0, 0, 0, 10, 15,
	0, 0, 0, 6, 6, 0, 0, 64, 4, 64,
	10, 0, 9, 5, 0, 15, 0, 20, 0, 0,
	0, 0, 0, 0, 0, 40, 2,/*blen=0,threshold=50*/
	15,/* MOVHYSTI */
	1, 47,/* MoveFilter 46->47, for chargeing*/
	10, 5, 40, 240, 245, 10, 10, 148, 50, 143,
	80, 18, 15, 0};

static u8 t48_config_e[] = {PROCG_NOISESUPPRESSION_T48,
	3, 132, 0x40, 0, 0, 0, 0, 0, 10, 15,
	0, 0, 0, 6, 6, 0, 0, 48, 4, 48,
	10, 0, 10, 5, 0, 20, 0, 5, 0, 0,  /*byte 27 original value 20*/
	0, 0, 0, 0, 32, 50, 2,
	15, 1, 46,
	MXT224_MAX_MT_FINGERS, 5, 40, 10, 10,
	10, 10, 143, 40, 143,
	80, 18, 15, 0};

static u8 end_config_e[] = {RESERVED_T255};

static const u8 *mxt224e_config[] = {
	t7_config_e,
	t8_config_e,
	t9_config_e,
	t15_config_e,
	t18_config_e,
	t23_config_e,
	t25_config_e,
	t40_config_e,
	t42_config_e,
	t46_config_e,
	t47_config_e,
	t48_config_e,
	t38_config_e,
	end_config_e,
};
/*
  Configuration for MXT224
*/
#define MXT224_THRESHOLD_BATT		40
#define MXT224_THRESHOLD_BATT_INIT		55
#define MXT224_THRESHOLD_CHRG		70
#define MXT224_NOISE_THRESHOLD_BATT		30
#define MXT224_NOISE_THRESHOLD_CHRG		40
#define MXT224_MOVFILTER_BATT		11
#define MXT224_MOVFILTER_CHRG		46
#define MXT224_ATCHCALST		9
#define MXT224_ATCHCALTHR		30

static u8 t7_config[] = { GEN_POWERCONFIG_T7,
	48,			/* IDLEACQINT */
	255,			/* ACTVACQINT */
	25			/* ACTV2IDLETO: 25 * 200ms = 5s */
};

static u8 t8_config[] = { GEN_ACQUISITIONCONFIG_T8,
	10, 0, 5, 1, 0, 0, MXT224_ATCHCALST, MXT224_ATCHCALTHR
};				/*byte 3: 0 */

static u8 t9_config[] = { TOUCH_MULTITOUCHSCREEN_T9,
	131, 0, 0, 19, 11, 0, 32, MXT224_THRESHOLD_BATT, 2, 0,
	0,
	15,			/* MOVHYSTI */
	1, MXT224_MOVFILTER_BATT, MXT224_MAX_MT_FINGERS, 5, 40, 10, 191, 3,
	27, 2, 0, 0, 0, 0, 143, 55, 143, 90, 18
};

static u8 t18_config[] = { SPT_COMCONFIG_T18,
	0, 1
};

static u8 t20_config[] = { PROCI_GRIPFACESUPPRESSION_T20,
	7, 0, 0, 0, 0, 0, 0, 30, 20, 4, 15, 10
};

static u8 t22_config[] = { PROCG_NOISESUPPRESSION_T22,
	143, 0, 0, 0, 0, 0, 0, 3, MXT224_NOISE_THRESHOLD_BATT, 0, 0, 29, 34, 39,
	49, 58, 3
};

static u8 t28_config[] = { SPT_CTECONFIG_T28,
			   0, 0, 3, 16, 19, 60
};
static u8 end_config[] = { RESERVED_T255 };

static const u8 *mxt224_config[] = {
	t7_config,
	t8_config,
	t9_config,
	t18_config,
	t20_config,
	t22_config,
	t28_config,
	end_config,
};

struct mxt224_callbacks *charger_callbacks;
void tsp_charger_infom(bool en)
{
	if (charger_callbacks && charger_callbacks->inform_charger)
		charger_callbacks->inform_charger(charger_callbacks, en);
	pr_debug("[TSP] %s - %s\n", __func__,
		en ? "on" : "off");
}

void tsp_register_callback(struct mxt224_callbacks *cb)
{
	charger_callbacks = cb;
}

void tsp_read_ta_status(void *ta_status)
{
	*(bool *)ta_status = is_cable_attached;
}

static int TSP_VDD_1_8V(int on)
{
	struct regulator *regulator;

	regulator = regulator_get(NULL, "touch_1.8v");
	if (IS_ERR(regulator))
		return PTR_ERR(regulator);

	if (on) {
		regulator_enable(regulator);
		pr_info("[TSP] Atmel power on\n");
	} else {
		/*
		 * TODO: If there is a case the regulator must be disabled
		 * (e,g firmware update?), consider regulator_force_disable.
		 */
		if (regulator_is_enabled(regulator))
			regulator_disable(regulator);
	}
	regulator_put(regulator);

	return 0;
}

static void mxt224_power_on(void)
{

	struct regulator *regulator;

	regulator = regulator_get(NULL, "touch");
	if (IS_ERR(regulator))
		return ;
	regulator_enable(regulator);
	TSP_VDD_1_8V(1);
	regulator_put(regulator);
	msleep(TSP_IRQ_READY_DELAY);
	pr_info("mxt224_power_on is finished\n");
}

static void mxt224_power_off(void)
{
	struct regulator *regulator;
	regulator = regulator_get(NULL, "touch");
	if (IS_ERR(regulator))
		return ;

	regulator_disable(regulator);
	TSP_VDD_1_8V(0);
	regulator_put(regulator);
	msleep(TSP_IRQ_READY_DELAY);
	pr_info("mxt224_power_off is finished\n");
}

static struct mxt224_platform_data mxt224_data = {
	.max_finger_touches = MXT224_MAX_MT_FINGERS,
	.gpio_read_done = GPIO_TSP_INT,
	.config = mxt224_config,
	.config_e = mxt224e_config,
	.t48_config_batt_e = t48_config_e,
	.t48_config_chrg_e = t48_config_chrg_e,
	.min_x = 0,
	.max_x = 540,
	.min_y = 0,
	.max_y = 960,
	.min_z = 0,
	.max_z = 255,
	.min_w = 0,
	.max_w = 30,
	.atchcalst = MXT224_ATCHCALST,
	.atchcalsthr = MXT224_ATCHCALTHR,
	.tchthr_batt = MXT224_THRESHOLD_BATT,
	.tchthr_batt_init = MXT224_THRESHOLD_BATT_INIT,
	.tchthr_charging = MXT224_THRESHOLD_CHRG,
	.noisethr_batt = MXT224_NOISE_THRESHOLD_BATT,
	.noisethr_charging = MXT224_NOISE_THRESHOLD_CHRG,
	.movfilter_batt = MXT224_MOVFILTER_BATT,
	.movfilter_charging = MXT224_MOVFILTER_CHRG,
	.atchcalst_e = MXT224E_ATCHCALST,
	.atchcalsthr_e = MXT224E_ATCHCALTHR,
	.tchthr_batt_e = MXT224E_THRESHOLD_BATT,
	.tchthr_charging_e = MXT224E_THRESHOLD_CHRG,
	.calcfg_batt_e = MXT224E_CALCFG_BATT,
	.calcfg_charging_e = MXT224E_CALCFG_CHRG,
	.atchfrccalthr_e = MXT224E_ATCHFRCCALTHR_NORMAL,
	.atchfrccalratio_e = MXT224E_ATCHFRCCALRATIO_NORMAL,
	.chrgtime_batt_e = MXT224E_GHRGTIME_BATT,
	.chrgtime_charging_e = MXT224E_GHRGTIME_CHRG,
	.blen_batt_e = MXT224E_BLEN_BATT,
	.blen_charging_e = MXT224E_BLEN_CHRG,
	.movfilter_batt_e = MXT224E_MOVFILTER_BATT,
	.movfilter_charging_e = MXT224E_MOVFILTER_CHRG,
	.actvsyncsperx_e = MXT224E_ACTVSYNCSPERX_NORMAL,
	.nexttchdi_e = MXT224E_NEXTTCHDI_NORMAL,
	.power_on = mxt224_power_on,
	.power_off = mxt224_power_off,
	.register_cb = tsp_register_callback,
	.read_ta_status = tsp_read_ta_status,
};

static struct i2c_board_info i2c_devs3[] __initdata = {
	{
		I2C_BOARD_INFO(MXT224_DEV_NAME, 0x4A),
		.platform_data = &mxt224_data
	}
};

void __init naples_tsp_init(void)
{
	int gpio;
	pr_info("[TSP] naples_tsp_init() is called\n");

	/* TSP_INT: XEINT_4 */
	gpio = GPIO_TSP_INT;
	s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(0xf));
	/* s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP); */
	s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);

	s5p_register_gpio_interrupt(gpio);
	i2c_devs3[0].irq = gpio_to_irq(gpio);
	i2c_register_board_info(3, i2c_devs3,
		ARRAY_SIZE(i2c_devs3));
}
#endif
