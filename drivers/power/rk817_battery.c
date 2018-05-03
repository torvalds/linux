/*
 * rk817 battery  driver
 *
 * Copyright (C) 2018 Rockchip Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "rk817-bat: " fmt

#include <linux/delay.h>
#include <linux/extcon.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/mfd/rk808.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/power/rk_usbbc.h>
#include <linux/regmap.h>
#include <linux/rk_keys.h>
#include <linux/rtc.h>
#include <linux/timer.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>

static int dbg_enable;

module_param_named(dbg_level, dbg_enable, int, 0644);

#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

#define BAT_INFO(fmt, args...) pr_info(fmt, ##args)

#define DRIVER_VERSION	"1.00"
#define SFT_SET_KB	1

#define DIV(x)	((x) ? (x) : 1)
#define ENABLE	0x01
#define DISABLE	0x00
#define MAX_INTERPOLATE		1000
#define MAX_PERCENTAGE		100
#define MAX_INT			0x7FFF

/* RK818_GGCON */
#define OCV_SAMP_MIN_MSK	0x0c
#define OCV_SAMP_8MIN		(0x00 << 2)

#define ADC_CAL_8MIN		0x00
#define RELAX_VOL12_UPD_MSK	(RELAX_VOL1_UPD | RELAX_VOL2_UPD)
#define MINUTE(x)	\
	((x) * 60)

#define ADC_TO_CURRENT(adc_value, samp_res)	\
	(adc_value * 172 / 1000 / samp_res)
#define CURRENT_TO_ADC(current, samp_res)	\
	(current * 1000 * samp_res / 172)

#define ADC_TO_CAPACITY(adc_value, samp_res)	\
	(adc_value / 1000 * 172 / 3600 / samp_res)
#define CAPACITY_TO_ADC(capacity, samp_res)	\
	(capacity * samp_res * 3600 / 172 * 1000)

#define ADC_TO_CAPACITY_UAH(adc_value, samp_res)	\
	(adc_value / 3600 * 172 / samp_res)
#define ADC_TO_CAPACITY_MAH(adc_value, samp_res)	\
	(adc_value / 1000 * 172 / 3600 / samp_res)

/* THREAML_REG */
#define TEMP_85C		(0x00 << 2)
#define TEMP_95C		(0x01 << 2)
#define TEMP_105C		(0x02 << 2)
#define TEMP_115C		(0x03 << 2)

#define ZERO_LOAD_LVL1			1400
#define ZERO_LOAD_LVL2			600

/* zero algorithm */
#define PWROFF_THRESD			3400
#define MIN_ZERO_DSOC_ACCURACY		10	/*0.01%*/
#define MIN_ZERO_OVERCNT		100
#define MIN_ACCURACY			1
#define DEF_PWRPATH_RES			50
#define WAIT_DSOC_DROP_SEC		15
#define WAIT_SHTD_DROP_SEC		30
#define MIN_ZERO_GAP_XSOC1		10
#define MIN_ZERO_GAP_XSOC2		5
#define MIN_ZERO_GAP_XSOC3		3
#define MIN_ZERO_GAP_CALIB		5

#define ADC_CALIB_THRESHOLD		4
#define ADC_CALIB_LMT_MIN		3
#define ADC_CALIB_CNT			5

/* default param */
#define DEFAULT_BAT_RES			135
#define DEFAULT_SLP_ENTER_CUR		300
#define DEFAULT_SLP_EXIT_CUR		300
#define DEFAULT_SLP_FILTER_CUR		100
#define DEFAULT_PWROFF_VOL_THRESD	3400
#define DEFAULT_MONITOR_SEC		5
#define DEFAULT_ALGR_VOL_THRESD1	3850
#define DEFAULT_ALGR_VOL_THRESD2	3950
#define DEFAULT_CHRG_VOL_SEL		CHRG_VOL4200MV
#define DEFAULT_CHRG_CUR_SEL		CHRG_CUR1400MA
#define DEFAULT_CHRG_CUR_INPUT		INPUT_CUR2000MA
#define DEFAULT_POFFSET			42
#define DEFAULT_MAX_SOC_OFFSET		60
#define DEFAULT_FB_TEMP			TEMP_115C
#define DEFAULT_ENERGY_MODE		0
#define DEFAULT_ZERO_RESERVE_DSOC	10
#define DEFAULT_SAMPLE_RES		20

/* sample resistor and division */
#define SAMPLE_RES_10MR			10
#define SAMPLE_RES_20MR			20
#define SAMPLE_RES_DIV1			1
#define SAMPLE_RES_DIV2			2

/* sleep */
#define SLP_CURR_MAX			40
#define SLP_CURR_MIN			6
#define DISCHRG_TIME_STEP1		MINUTE(10)
#define DISCHRG_TIME_STEP2		MINUTE(60)
#define SLP_DSOC_VOL_THRESD		3600
#define REBOOT_PERIOD_SEC		180
#define REBOOT_MAX_CNT			80

#define TIMER_MS_COUNTS		1000
/* fcc */
#define MIN_FCC				500
#define CAP_INVALID			0x80

/* virtual params */
#define VIRTUAL_CURRENT			1000
#define VIRTUAL_VOLTAGE			3888
#define VIRTUAL_SOC			66
#define VIRTUAL_PRESET			1
#define VIRTUAL_TEMPERATURE		188
#define VIRTUAL_STATUS			POWER_SUPPLY_STATUS_CHARGING

#define FINISH_CHRG_CUR1		1000
#define FINISH_CHRG_CUR2		1500
#define FINISH_MAX_SOC_DELAY		20
#define TERM_CHRG_DSOC			88
#define TERM_CHRG_CURR			600
#define TERM_CHRG_K			650
#define SIMULATE_CHRG_INTV		8
#define SIMULATE_CHRG_CURR		400
#define SIMULATE_CHRG_K			1500
#define FULL_CHRG_K			400

enum work_mode {
	MODE_ZERO = 0,
	MODE_FINISH,
	MODE_SMOOTH_CHRG,
	MODE_SMOOTH_DISCHRG,
	MODE_SMOOTH,
};

enum charge_status {
	CHRG_OFF,
	DEAD_CHRG,
	TRICKLE_CHRG,
	CC_OR_CV_CHRG,
	CHARGE_FINISH,
	USB_OVER_VOL,
	BAT_TMP_ERR,
	BAT_TIM_ERR,
};

enum bat_mode {
	MODE_BATTARY = 0,
	MODE_VIRTUAL,
};

enum rk817_sample_time {
	S_8_MIN,
	S_16_MIN,
	S_32_MIN,
	S_48_MIN,
};

enum rk817_output_mode {
	AVERAGE_MODE,
	INSTANT_MODE,
};

enum rk817_battery_fields {
	ADC_SLP_RATE, BAT_CUR_ADC_EN, BAT_VOL_ADC_EN,
	USB_VOL_ADC_EN, TS_ADC_EN, SYS_VOL_ADC_EN, GG_EN, /*ADC_CONFIG0*/
	CUR_ADC_DITH_SEL, CUR_ADC_DIH_EN, CUR_ADC_CHOP_EN,
	CUR_ADC_CHOP_SEL, CUR_ADC_CHOP_VREF_EN, /*CUR_ADC_CFG0*/
	CUR_ADC_VCOM_SEL, CUR_ADC_VCOM_BUF_INC, CUR_ADC_VREF_BUF_INC,
	CUR_ADC_BIAS_DEC, CUR_ADC_IBIAS_SEL,/*CUR_ADC_CFG1*/
	VOL_ADC_EXT_VREF_EN, VOL_ADC_DITH_SEL, VOL_ADC_DITH_EN,
	VOL_ADC_CHOP_EN, VOL_ADC_CHOP_SEL, VOL_ADC_CHOP_VREF_EN,
	VOL_ADC_VCOM_SEL, VOL_ADC_VCOM_BUF_INC, VOL_ADC_VREF_BUF_INC,
	VOL_ADC_IBIAS_SEL, /*VOL_ADC_CFG1*/
	RLX_CUR_FILTER, TS_FUN, VOL_ADC_TSCUR_SEL,
	VOL_CALIB_UPD, CUR_CALIB_UPD, /*ADC_CONFIG1*/
	CUR_OUT_MOD, VOL_OUT_MOD, FRAME_SMP_INTERV,
	ADC_OFF_CAL_INTERV, RLX_SPT, /*GG_CON*/
	OCV_UPD, RELAX_STS, RELAX_VOL2_UPD, RELAX_VOL1_UPD, BAT_CON,
	QMAX_UPD_SOFT, TERM_UPD, OCV_STS, /*GG_STS*/
	RELAX_THRE_H, RELAX_THRE_L, /*RELAX_THRE*/
	RELAX_VOL1_H, RELAX_VOL1_L,
	RELAX_VOL2_H, RELAX_VOL2_L,
	RELAX_CUR1_H, RELAX_CUR1_L,
	RELAX_CUR2_H, RELAX_CUR2_L,
	OCV_THRE_VOL,
	OCV_VOL_H, OCV_VOL_L,
	OCV_VOL0_H, OCV_VOL0_L,
	OCV_CUR_H, OCV_CUR_L,
	OCV_CUR0_H, OCV_CUR0_L,
	PWRON_VOL_H, PWRON_VOL_L,
	PWRON_CUR_H, PWRON_CUR_L,
	OFF_CNT,
	Q_INIT_H3, Q_INIT_H2, Q_INIT_L1, Q_INIT_L0,
	Q_PRESS_H3, Q_PRESS_H2, Q_PRESS_L1, Q_PRESS_L0,
	BAT_VOL_H, BAT_VOL_L,
	BAT_CUR_H, BAT_CUR_L,
	BAT_TS_H, BAT_TS_L,
	USB_VOL_H, USB_VOL_L,
	SYS_VOL_H, SYS_VOL_L,
	Q_MAX_H3, Q_MAX_H2, Q_MAX_L1, Q_MAX_L0,
	Q_TERM_H3, Q_TERM_H2, Q_TERM_L1, Q_TERM_L0,
	Q_OCV_H3, Q_OCV_H2, Q_OCV_L1, Q_OCV_L0,
	OCV_CNT,
	SLEEP_CON_SAMP_CUR_H, SLEEP_CON_SAMP_CUR_L,
	CAL_OFFSET_H, CAL_OFFSET_L,
	VCALIB0_H, VCALIB0_L,
	VCALIB1_H, VCALIB1_L,
	IOFFSET_H, IOFFSET_L,
	BAT_R0, SOC_REG0, SOC_REG1, SOC_REG2,
	REMAIN_CAP_REG2, REMAIN_CAP_REG1, REMAIN_CAP_REG0,
	NEW_FCC_REG2, NEW_FCC_REG1, NEW_FCC_REG0,
	RESET_MODE,
	FG_INIT, HALT_CNT_REG, CALC_REST_REGL, CALC_REST_REGH,
	VOL_ADC_B3,  VOL_ADC_B2, VOL_ADC_B1, VOL_ADC_B0,
	VOL_ADC_K3, VOL_ADC_K2, VOL_ADC_K1, VOL_ADC_K0,
	BAT_EXS, CHG_STS, BAT_OVP_STS, CHRG_IN_CLAMP,
	CHIP_NAME_H, CHIP_NAME_L,
	F_MAX_FIELDS
};

static const struct reg_field rk817_battery_reg_fields[] = {
	[ADC_SLP_RATE] = REG_FIELD(0x50, 0, 0),
	[BAT_CUR_ADC_EN] = REG_FIELD(0x50, 2, 2),
	[BAT_VOL_ADC_EN] = REG_FIELD(0x50, 3, 3),
	[USB_VOL_ADC_EN] = REG_FIELD(0x50, 4, 4),
	[TS_ADC_EN] = REG_FIELD(0x50, 5, 5),
	[SYS_VOL_ADC_EN] = REG_FIELD(0x50, 6, 6),
	[GG_EN] = REG_FIELD(0x50, 7, 7),/*ADC_CONFIG0*/

	[CUR_ADC_DITH_SEL] = REG_FIELD(0x51, 1, 3),
	[CUR_ADC_DIH_EN] = REG_FIELD(0x51, 4, 4),
	[CUR_ADC_CHOP_EN] = REG_FIELD(0x51, 5, 5),
	[CUR_ADC_CHOP_SEL] = REG_FIELD(0x51, 6, 6),
	[CUR_ADC_CHOP_VREF_EN] = REG_FIELD(0x51, 7, 7), /*CUR_ADC_COFG0*/

	[CUR_ADC_VCOM_SEL] = REG_FIELD(0x52, 0, 1),
	[CUR_ADC_VCOM_BUF_INC] = REG_FIELD(0x52, 2, 2),
	[CUR_ADC_VREF_BUF_INC] = REG_FIELD(0x52, 3, 3),
	[CUR_ADC_BIAS_DEC] = REG_FIELD(0x52, 4, 4),
	[CUR_ADC_IBIAS_SEL] = REG_FIELD(0x52, 5, 6), /*CUR_ADC_COFG1*/

	[VOL_ADC_EXT_VREF_EN] = REG_FIELD(0x53, 0, 0),
	[VOL_ADC_DITH_SEL]  = REG_FIELD(0x53, 1, 3),
	[VOL_ADC_DITH_EN] = REG_FIELD(0x53, 4, 4),
	[VOL_ADC_CHOP_EN] = REG_FIELD(0x53, 5, 5),
	[VOL_ADC_CHOP_SEL] = REG_FIELD(0x53, 6, 6),
	[VOL_ADC_CHOP_VREF_EN] = REG_FIELD(0x53, 7, 7),/*VOL_ADC_COFG0*/

	[VOL_ADC_VCOM_SEL] = REG_FIELD(0x54, 0, 1),
	[VOL_ADC_VCOM_BUF_INC] = REG_FIELD(0x54, 2, 2),
	[VOL_ADC_VREF_BUF_INC] = REG_FIELD(0x54, 3, 3),
	[VOL_ADC_IBIAS_SEL] = REG_FIELD(0x54, 5, 6), /*VOL_ADC_COFG1*/

	[RLX_CUR_FILTER] = REG_FIELD(0x55, 0, 1),
	[TS_FUN] = REG_FIELD(0x55, 3, 3),
	[VOL_ADC_TSCUR_SEL] = REG_FIELD(0x55, 4, 5),
	[VOL_CALIB_UPD] = REG_FIELD(0x55, 6, 6),
	[CUR_CALIB_UPD] = REG_FIELD(0x55, 7, 7), /*ADC_CONFIG1*/

	[CUR_OUT_MOD] = REG_FIELD(0x56, 0, 0),
	[VOL_OUT_MOD] = REG_FIELD(0x56, 1, 1),
	[FRAME_SMP_INTERV] = REG_FIELD(0x56, 2, 3),
	[ADC_OFF_CAL_INTERV] = REG_FIELD(0x56, 4, 5),
	[RLX_SPT] = REG_FIELD(0x56, 6, 7), /*GG_CON*/

	[OCV_UPD] = REG_FIELD(0x57, 0, 0),
	[RELAX_STS] = REG_FIELD(0x57, 1, 1),
	[RELAX_VOL2_UPD] = REG_FIELD(0x57, 2, 2),
	[RELAX_VOL1_UPD] = REG_FIELD(0x57, 3, 3),
	[BAT_CON] = REG_FIELD(0x57, 4, 4),
	[QMAX_UPD_SOFT] = REG_FIELD(0x57, 5, 5),
	[TERM_UPD] = REG_FIELD(0x57, 6, 6),
	[OCV_STS] = REG_FIELD(0x57, 7, 7), /*GG_STS*/

	[RELAX_THRE_H] = REG_FIELD(0x58, 0, 7),
	[RELAX_THRE_L] = REG_FIELD(0x59, 0, 7),

	[RELAX_VOL1_H] = REG_FIELD(0x5A, 0, 7),
	[RELAX_VOL1_L] = REG_FIELD(0x5B, 0, 7),
	[RELAX_VOL2_H] = REG_FIELD(0x5C, 0, 7),
	[RELAX_VOL2_L] = REG_FIELD(0x5D, 0, 7),

	[RELAX_CUR1_H] = REG_FIELD(0x5E, 0, 7),
	[RELAX_CUR1_L] = REG_FIELD(0x5F, 0, 7),
	[RELAX_CUR2_H] = REG_FIELD(0x60, 0, 7),
	[RELAX_CUR2_L] = REG_FIELD(0x61, 0, 7),

	[OCV_THRE_VOL] = REG_FIELD(0x62, 0, 7),

	[OCV_VOL_H] = REG_FIELD(0x63, 0, 7),
	[OCV_VOL_L] = REG_FIELD(0x64, 0, 7),
	[OCV_VOL0_H] = REG_FIELD(0x65, 0, 7),
	[OCV_VOL0_L] = REG_FIELD(0x66, 0, 7),
	[OCV_CUR_H] = REG_FIELD(0x67, 0, 7),
	[OCV_CUR_L] = REG_FIELD(0x68, 0, 7),
	[OCV_CUR0_H] = REG_FIELD(0x69, 0, 7),
	[OCV_CUR0_L] = REG_FIELD(0x6A, 0, 7),
	[PWRON_VOL_H] = REG_FIELD(0x6B, 0, 7),
	[PWRON_VOL_L] = REG_FIELD(0x6C, 0, 7),
	[PWRON_CUR_H] = REG_FIELD(0x6D, 0, 7),
	[PWRON_CUR_L] = REG_FIELD(0x6E, 0, 7),
	[OFF_CNT] = REG_FIELD(0x6F, 0, 7),
	[Q_INIT_H3] = REG_FIELD(0x70, 0, 7),
	[Q_INIT_H2] = REG_FIELD(0x71, 0, 7),
	[Q_INIT_L1] = REG_FIELD(0x72, 0, 7),
	[Q_INIT_L0] = REG_FIELD(0x73, 0, 7),

	[Q_PRESS_H3] = REG_FIELD(0x74, 0, 7),
	[Q_PRESS_H2] = REG_FIELD(0x75, 0, 7),
	[Q_PRESS_L1] = REG_FIELD(0x76, 0, 7),
	[Q_PRESS_L0] = REG_FIELD(0x77, 0, 7),

	[BAT_VOL_H] = REG_FIELD(0x78, 0, 7),
	[BAT_VOL_L] = REG_FIELD(0x79, 0, 7),

	[BAT_CUR_H] = REG_FIELD(0x7A, 0, 7),
	[BAT_CUR_L] = REG_FIELD(0x7B, 0, 7),

	[BAT_TS_H] = REG_FIELD(0x7C, 0, 7),
	[BAT_TS_L] = REG_FIELD(0x7D, 0, 7),
	[USB_VOL_H] = REG_FIELD(0x7E, 0, 7),
	[USB_VOL_L] = REG_FIELD(0x7F, 0, 7),

	[SYS_VOL_H] = REG_FIELD(0x80, 0, 7),
	[SYS_VOL_L] = REG_FIELD(0x81, 0, 7),
	[Q_MAX_H3] = REG_FIELD(0x82, 0, 7),
	[Q_MAX_H2] = REG_FIELD(0x83, 0, 7),
	[Q_MAX_L1] = REG_FIELD(0x84, 0, 7),
	[Q_MAX_L0] = REG_FIELD(0x85, 0, 7),

	[Q_TERM_H3] = REG_FIELD(0x86, 0, 7),
	[Q_TERM_H2] = REG_FIELD(0x87, 0, 7),
	[Q_TERM_L1] = REG_FIELD(0x88, 0, 7),
	[Q_TERM_L0] = REG_FIELD(0x89, 0, 7),
	[Q_OCV_H3] = REG_FIELD(0x8A, 0, 7),
	[Q_OCV_H2] = REG_FIELD(0x8B, 0, 7),

	[Q_OCV_L1] = REG_FIELD(0x8C, 0, 7),
	[Q_OCV_L0] = REG_FIELD(0x8D, 0, 7),
	[OCV_CNT] = REG_FIELD(0x8E, 0, 7),
	[SLEEP_CON_SAMP_CUR_H] = REG_FIELD(0x8F, 0, 7),
	[SLEEP_CON_SAMP_CUR_L] = REG_FIELD(0x90, 0, 7),
	[CAL_OFFSET_H] = REG_FIELD(0x91, 0, 7),
	[CAL_OFFSET_L] = REG_FIELD(0x92, 0, 7),
	[VCALIB0_H] = REG_FIELD(0x93, 0, 7),
	[VCALIB0_L] = REG_FIELD(0x94, 0, 7),
	[VCALIB1_H] = REG_FIELD(0x95, 0, 7),
	[VCALIB1_L] = REG_FIELD(0x96, 0, 7),
	[IOFFSET_H] = REG_FIELD(0x97, 0, 7),
	[IOFFSET_L] = REG_FIELD(0x98, 0, 7),

	[BAT_R0] = REG_FIELD(0x99, 0, 7),
	[SOC_REG0] = REG_FIELD(0x9A, 0, 7),
	[SOC_REG1] = REG_FIELD(0x9B, 0, 7),
	[SOC_REG2] = REG_FIELD(0x9C, 0, 7),

	[REMAIN_CAP_REG0] = REG_FIELD(0x9D, 0, 7),
	[REMAIN_CAP_REG1] = REG_FIELD(0x9E, 0, 7),
	[REMAIN_CAP_REG2] = REG_FIELD(0x9F, 0, 7),
	[NEW_FCC_REG0] = REG_FIELD(0xA0, 0, 7),
	[NEW_FCC_REG1] = REG_FIELD(0xA1, 0, 7),
	[NEW_FCC_REG2] = REG_FIELD(0xA2, 0, 7),
	[RESET_MODE] = REG_FIELD(0xA3, 0, 3),
	[FG_INIT] = REG_FIELD(0xA5, 7, 7),

	[HALT_CNT_REG] = REG_FIELD(0xA6, 0, 7),
	[CALC_REST_REGL] = REG_FIELD(0xA7, 0, 7),
	[CALC_REST_REGH] = REG_FIELD(0xA8, 0, 7),

	[VOL_ADC_B3] = REG_FIELD(0xA9, 0, 7),
	[VOL_ADC_B2] = REG_FIELD(0xAA, 0, 7),
	[VOL_ADC_B1] = REG_FIELD(0xAB, 0, 7),
	[VOL_ADC_B0] = REG_FIELD(0xAC, 0, 7),

	[VOL_ADC_K3] = REG_FIELD(0xAD, 0, 7),
	[VOL_ADC_K2] = REG_FIELD(0xAE, 0, 7),
	[VOL_ADC_K1] = REG_FIELD(0xAF, 0, 7),
	[VOL_ADC_K0] = REG_FIELD(0xB0, 0, 7),
	[BAT_EXS] = REG_FIELD(0xEB, 7, 7),
	[CHG_STS] = REG_FIELD(0xEB, 4, 6),
	[BAT_OVP_STS] = REG_FIELD(0xEB, 3, 3),
	[CHRG_IN_CLAMP] = REG_FIELD(0xEB, 2, 2),
	[CHIP_NAME_H] = REG_FIELD(0xED, 0, 7),
	[CHIP_NAME_L] = REG_FIELD(0xEE, 0, 7),
};

struct battery_platform_data {
	u32 *ocv_table;
	u32 *zero_table;

	u32 table_t[4][21];
	int temp_t[4];
	u32 temp_t_num;

	u32 *ntc_table;
	u32 ocv_size;
	u32 ntc_size;
	int ntc_degree_from;
	u32 ntc_factor;
	u32 max_input_current;
	u32 max_chrg_current;
	u32 max_chrg_voltage;
	u32 lp_input_current;
	u32 lp_soc_min;
	u32 lp_soc_max;
	u32 pwroff_vol;
	u32 monitor_sec;
	u32 zero_algorithm_vol;
	u32 zero_reserve_dsoc;
	u32 bat_res;
	u32 design_capacity;
	u32 design_qmax;
	u32 sleep_enter_current;
	u32 sleep_exit_current;
	u32 sleep_filter_current;

	u32 power_dc2otg;
	u32 max_soc_offset;
	u32 bat_mode;
	u32 fb_temp;
	u32 energy_mode;
	u32 cccv_hour;
	u32 dc_det_adc;
	int dc_det_pin;
	u8  dc_det_level;
	u32 sample_res;
	bool extcon;
};

struct rk817_battery_device {
	struct platform_device		*pdev;
	struct device				*dev;
	struct i2c_client			*client;
	struct rk808			*rk817;
	struct power_supply			*bat;
	struct power_supply		*usb_psy;
	struct power_supply		*ac_psy;
	struct regmap			*regmap;
	struct regmap_field		*rmap_fields[F_MAX_FIELDS];
	struct battery_platform_data	*pdata;
	struct workqueue_struct		*bat_monitor_wq;
	struct delayed_work		bat_delay_work;
	struct delayed_work		calib_delay_work;
	struct wake_lock		wake_lock;
	struct timer_list		caltimer;

	int				res_div;
	int				bat_res;
	bool				is_first_power_on;
	int				chrg_status;
	int				res_fac;
	int				over_20mR;
	bool				is_initialized;
	bool				bat_first_power_on;
	u8				ac_in;
	u8				usb_in;
	u8				otg_in;
	u8				dc_in;
	u8				prop_status;
	int				cvtlmt_irq;
	int				current_avg;
	int				current_relax;
	int				voltage_usb;
	int				voltage_sys;
	int				voltage_avg;
	int				voltage_ocv;
	int				voltage_relax;
	int				voltage_k;/* VCALIB0 VCALIB1 */
	int				voltage_b;
	u32				remain_cap;
	int				design_cap;
	int				nac;
	int				fcc;
	int				lock_fcc;
	int				qmax;
	int				dsoc;
	int				rsoc;
	int				poffset;
	int				fake_offline;
	int				age_ocv_soc;
	bool				age_allow_update;
	int				age_level;
	int				age_ocv_cap;
	int				pwron_voltage;
	int				age_voltage;
	int				age_adjust_cap;
	unsigned long			age_keep_sec;
	int				zero_timeout_cnt;
	int				zero_remain_cap;
	int				zero_dsoc;
	int				zero_linek;
	u64				zero_drop_sec;
	u64				shtd_drop_sec;

	int				powerpatch_res;
	int				zero_voltage_avg;
	int				zero_current_avg;
	int				zero_vsys;
	int				zero_dead_voltage;
	int				zero_dead_soc;
	int				zero_dead_cap;
	int				zero_batvol_to_ocv;
	int				zero_batocv_to_soc;
	int				zero_batocv_to_cap;
	int				zero_xsoc;
	unsigned long			finish_base;
	struct timeval			rtc_base;
	int				sm_remain_cap;
	int				sm_linek;
	int				sm_chrg_dsoc;
	int				sm_dischrg_dsoc;
	int				smooth_soc;
	int				algo_rest_val;
	int				algo_rest_mode;
	int				sleep_sum_cap;
	int				sleep_remain_cap;
	unsigned long			sleep_dischrg_sec;
	unsigned long			sleep_sum_sec;
	bool				sleep_chrg_online;
	u8				sleep_chrg_status;
	bool				adc_allow_update;
	int                             fb_blank;
	bool				s2r; /*suspend to resume*/
	u32				work_mode;
	int				temperature;
	int				chrg_cur_lp_input;
	int				chrg_vol_sel;
	int				chrg_cur_input;
	int				chrg_cur_sel;
	u32				monitor_ms;
	u32				pwroff_min;
	u32				adc_calib_cnt;
	unsigned long			chrg_finish_base;
	unsigned long			boot_base;
	unsigned long			flat_match_sec;
	unsigned long			plug_in_base;
	unsigned long			plug_out_base;
	u8				halt_cnt;
	bool				is_halt;
	bool				is_max_soc_offset;
	bool				is_sw_reset;
	bool				is_ocv_calib;
	bool				is_first_on;
	bool				is_force_calib;
	int				last_dsoc;
	u8				cvtlmt_int_event;
	u8				slp_dcdc_en_reg;
	int				ocv_pre_dsoc;
	int				ocv_new_dsoc;
	int				max_pre_dsoc;
	int				max_new_dsoc;
	int				force_pre_dsoc;
	int				force_new_dsoc;

	int				dbg_cap_low0;
	int				dbg_pwr_dsoc;
	int				dbg_pwr_rsoc;
	int				dbg_pwr_vol;
	int				dbg_chrg_min[10];
	int				dbg_meet_soc;
	int				dbg_calc_dsoc;
	int				dbg_calc_rsoc;
	int				is_charging;
	unsigned long			charge_count;
};

static u64 get_boot_sec(void)
{
	struct timespec ts;

	get_monotonic_boottime(&ts);

	return ts.tv_sec;
}

static unsigned long base2sec(unsigned long x)
{
	if (x)
		return (get_boot_sec() > x) ? (get_boot_sec() - x) : 0;
	else
		return 0;
}

static u32 interpolate(int value, u32 *table, int size)
{
	u8 i;
	u16 d;

	for (i = 0; i < size; i++) {
		if (value < table[i])
			break;
	}

	if ((i > 0) && (i < size)) {
		d = (value - table[i - 1]) * (MAX_INTERPOLATE / (size - 1));
		d /= table[i] - table[i - 1];
		d = d + (i - 1) * (MAX_INTERPOLATE / (size - 1));
	} else {
		d = i * ((MAX_INTERPOLATE + size / 2) / size);
	}

	if (d > 1000)
		d = 1000;

	return d;
}

/* (a * b) / c */
static int32_t ab_div_c(u32 a, u32 b, u32 c)
{
	bool sign;
	u32 ans = MAX_INT;
	int tmp;

	sign = ((((a ^ b) ^ c) & 0x80000000) != 0);
	if (c != 0) {
		if (sign)
			c = -c;
		tmp = (a * b + (c >> 1)) / c;
		if (tmp < MAX_INT)
			ans = tmp;
	}

	if (sign)
		ans = -ans;

	return ans;
}

static int rk817_bat_field_read(struct rk817_battery_device *battery,
				enum rk817_battery_fields field_id)
{
	int val;
	int ret;

	ret = regmap_field_read(battery->rmap_fields[field_id], &val);
	if (ret < 0)
		return ret;

	return val;
}

static int rk817_bat_field_write(struct rk817_battery_device *battery,
				 enum rk817_battery_fields field_id,
				 unsigned int val)
{
	return regmap_field_write(battery->rmap_fields[field_id], val);
}

/*cal_offset: current offset value*/
static int rk817_bat_get_coffset(struct rk817_battery_device *battery)
{
	int  coffset_value = 0;

	coffset_value |= rk817_bat_field_read(battery, CAL_OFFSET_H) << 8;
	coffset_value |= rk817_bat_field_read(battery, CAL_OFFSET_L);

	return coffset_value;
}

static void rk817_bat_set_coffset(struct rk817_battery_device *battery, int val)
{
	u8  buf = 0;

	buf = (val >> 8) & 0xff;
	rk817_bat_field_write(battery, CAL_OFFSET_H, buf);
	buf = (val >> 0) & 0xff;
	rk817_bat_field_write(battery, CAL_OFFSET_L, buf);
}

/* current offset value calculated */
static int rk817_bat_get_ioffset(struct rk817_battery_device *battery)
{
	int  ioffset_value = 0;

	ioffset_value |= rk817_bat_field_read(battery, IOFFSET_H) << 8;
	ioffset_value |= rk817_bat_field_read(battery, IOFFSET_L);

	return ioffset_value;
}

static void rk817_bat_current_calibration(struct rk817_battery_device *battery)
{
	int pwron_value, ioffset, cal_offset;

	pwron_value = rk817_bat_field_read(battery, PWRON_CUR_H) << 8;
	pwron_value |= rk817_bat_field_read(battery, PWRON_CUR_L);

	ioffset = rk817_bat_get_ioffset(battery);

	DBG("Caloffset: 0x%x\n", rk817_bat_get_coffset(battery));
	DBG("IOFFSET: 0x%x\n", ioffset);
	if (0)
		cal_offset = pwron_value + ioffset;
	else
		cal_offset = ioffset;

	rk817_bat_set_coffset(battery, cal_offset);
	DBG("Caloffset: 0x%x\n", rk817_bat_get_coffset(battery));

}

static int rk817_bat_get_vaclib0(struct rk817_battery_device *battery)
{
	int vcalib_value = 0;

	vcalib_value |= rk817_bat_field_read(battery, VCALIB0_H) << 8;
	vcalib_value |= rk817_bat_field_read(battery, VCALIB0_L);

	return vcalib_value;
}

static int rk817_bat_get_vaclib1(struct rk817_battery_device *battery)
{
	int vcalib_value = 0;

	vcalib_value |= rk817_bat_field_read(battery, VCALIB1_H) << 8;
	vcalib_value |= rk817_bat_field_read(battery, VCALIB1_L);

	return vcalib_value;
}

static void rk817_bat_init_voltage_kb(struct rk817_battery_device *battery)
{
	int vcalib0, vcalib1;

	vcalib0 = rk817_bat_get_vaclib0(battery);
	vcalib1 =  rk817_bat_get_vaclib1(battery);
	battery->voltage_k = (4025 - 2300) * 1000 / DIV(vcalib1 - vcalib0);
	battery->voltage_b = 4025 - (battery->voltage_k * vcalib1) / 1000;
}

static void rk817_bat_restart_relax(struct rk817_battery_device *battery)
{
	rk817_bat_field_write(battery, RELAX_VOL1_UPD, 0x00);
	rk817_bat_field_write(battery, RELAX_VOL2_UPD, 0x00);
}

static bool is_rk817_bat_relax_mode(struct rk817_battery_device *battery)
{
	u8 relax_sts, relax_vol1_upd, relax_vol2_upd;

	relax_sts = rk817_bat_field_read(battery, RELAX_STS);
	relax_vol1_upd = rk817_bat_field_read(battery, RELAX_VOL1_UPD);
	relax_vol2_upd = rk817_bat_field_read(battery, RELAX_VOL2_UPD);

	DBG("RELAX_STS: %d\n", relax_sts);
	DBG("RELAX_VOL1_UPD: %d\n", relax_vol1_upd);
	DBG("RELAX_VOL2_UPD: %d\n", relax_vol2_upd);
	if (relax_sts && relax_vol1_upd && relax_vol2_upd)
		return true;
	else
		return false;
}

static u16 rk817_bat_get_relax_vol1(struct rk817_battery_device *battery)
{
	u16 vol, val = 0;

	val = rk817_bat_field_read(battery, RELAX_VOL1_H) << 8;
	val |= rk817_bat_field_read(battery, RELAX_VOL1_L);
	vol = battery->voltage_k * val / 1000 + battery->voltage_b;

	return vol;
}

static u16 rk817_bat_get_relax_vol2(struct rk817_battery_device *battery)
{
	u16 vol, val = 0;

	val = rk817_bat_field_read(battery, RELAX_VOL2_H) << 8;
	val |= rk817_bat_field_read(battery, RELAX_VOL2_L);
	vol = battery->voltage_k * val / 1000 + battery->voltage_b;

	return vol;
}

static u16 rk817_bat_get_relax_voltage(struct rk817_battery_device *battery)
{
	u16 relax_vol1, relax_vol2;

	if (!is_rk817_bat_relax_mode(battery))
		return 0;

	relax_vol1 = rk817_bat_get_relax_vol1(battery);
	relax_vol2 = rk817_bat_get_relax_vol2(battery);

	return relax_vol1 > relax_vol2 ? relax_vol1 : relax_vol2;
}

static void rk817_bat_set_relax_sample(struct rk817_battery_device *battery)
{
	u8 buf;
	int enter_thres, filter_thres;
	struct battery_platform_data *pdata = battery->pdata;

	filter_thres = pdata->sleep_filter_current * 1000 / 1506;

	enter_thres = CURRENT_TO_ADC(pdata->sleep_enter_current,
				     battery->res_div);
	filter_thres = CURRENT_TO_ADC(pdata->sleep_filter_current,
				      battery->res_div);

	/* set relax enter and exit threshold */
	buf = (enter_thres >> 8) & 0xff;
	rk817_bat_field_write(battery, RELAX_THRE_H, buf);
	buf = enter_thres & 0xff;
	rk817_bat_field_write(battery, RELAX_THRE_L, buf);
	/* set sample current threshold */
	buf = (filter_thres >> 8) & 0xff;
	rk817_bat_field_write(battery, SLEEP_CON_SAMP_CUR_H, buf);
	buf = filter_thres & 0xff;
	rk817_bat_field_write(battery, SLEEP_CON_SAMP_CUR_L, buf);

	/* reset relax update state */
	rk817_bat_restart_relax(battery);
	DBG("<%s>. sleep_enter_current = %d, sleep_exit_current = %d\n",
	    __func__, pdata->sleep_enter_current, pdata->sleep_exit_current);
}

/* runtime OCV voltage,  |RLX_VOL2 - RLX_VOL1| < OCV_THRE,
 * the OCV reg update every 120s
 */
static void rk817_bat_ocv_thre(struct rk817_battery_device *battery, int value)
{
	rk817_bat_field_write(battery, OCV_THRE_VOL, value);
}

static int rk817_bat_get_ocv_voltage(struct rk817_battery_device *battery)
{
	int vol, val = 0;

	val = rk817_bat_field_read(battery, OCV_VOL_H) << 8;
	val |= rk817_bat_field_read(battery, OCV_VOL_L);
	vol = battery->voltage_k * val / 1000 + battery->voltage_b;

	return vol;
}

static int rk817_bat_get_ocv0_voltage0(struct rk817_battery_device *battery)
{
	int vol, val = 0;

	val = rk817_bat_field_read(battery, OCV_VOL0_H) << 8;
	val |= rk817_bat_field_read(battery, OCV_VOL0_L);
	vol = battery->voltage_k * val / 1000 + battery->voltage_b;

	return vol;
}

/* power on battery voltage */
static int rk817_bat_get_pwron_voltage(struct rk817_battery_device *battery)
{
	int vol, val = 0;

	val = rk817_bat_field_read(battery, PWRON_VOL_H) << 8;
	val |= rk817_bat_field_read(battery, PWRON_VOL_L);
	vol = battery->voltage_k * val / 1000 + battery->voltage_b;

	return vol;
}

static int rk817_bat_get_battery_voltage(struct rk817_battery_device *battery)
{
	int vol, val = 0;

	val = rk817_bat_field_read(battery, BAT_VOL_H) << 8;
	val |= rk817_bat_field_read(battery, BAT_VOL_L) << 0;

	vol = battery->voltage_k * val / 1000 + battery->voltage_b;

	return vol;
}

static int rk817_bat_get_USB_voltage(struct rk817_battery_device *battery)
{
	int vol, val = 0;

	rk817_bat_field_write(battery, USB_VOL_ADC_EN, 0x01);

	val = rk817_bat_field_read(battery, USB_VOL_H) << 8;
	val |= rk817_bat_field_read(battery, USB_VOL_L) << 0;

	vol = (battery->voltage_k * val / 1000 + battery->voltage_b) * 60 / 46;

	return vol;
}

static int rk817_bat_get_sys_voltage(struct rk817_battery_device *battery)
{
	int vol, val = 0;

	val = rk817_bat_field_read(battery, SYS_VOL_H) << 8;
	val |= rk817_bat_field_read(battery, SYS_VOL_L) << 0;

	vol = (battery->voltage_k * val / 1000 + battery->voltage_b) * 60 / 46;

	return vol;
}

static int rk817_bat_get_avg_current(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val = rk817_bat_field_read(battery, BAT_CUR_H) << 8;
	val |= rk817_bat_field_read(battery, BAT_CUR_L);

	if (val & 0x8000)
		val -= 0x10000;

	cur = ADC_TO_CURRENT(val, battery->res_div);

	return cur;
}

static int rk817_bat_get_relax_cur1(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val = rk817_bat_field_read(battery, RELAX_CUR1_H) << 8;
	val |= rk817_bat_field_read(battery, RELAX_CUR1_L);

	if (val & 0x8000)
		val -= 0x10000;

	cur = ADC_TO_CURRENT(val, battery->res_div);

	return cur;
}

static int rk817_bat_get_relax_cur2(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val |= rk817_bat_field_read(battery, RELAX_CUR2_H) << 8;
	val = rk817_bat_field_read(battery, RELAX_CUR2_L);

	if (val & 0x8000)
		val -= 0x10000;

	cur = ADC_TO_CURRENT(val, battery->res_div);

	return cur;
}

static int rk817_bat_get_relax_current(struct rk817_battery_device *battery)
{
	int relax_cur1, relax_cur2;

	if (!is_rk817_bat_relax_mode(battery))
		return 0;

	relax_cur1 = rk817_bat_get_relax_cur1(battery);
	relax_cur2 = rk817_bat_get_relax_cur2(battery);

	return (relax_cur1 < relax_cur2) ? relax_cur1 : relax_cur2;
}

static int rk817_bat_get_ocv_current(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val = rk817_bat_field_read(battery, OCV_CUR_H) << 8;
	val |= rk817_bat_field_read(battery, OCV_CUR_L);

	if (val & 0x8000)
		val -= 0x10000;

	cur = ADC_TO_CURRENT(val, battery->res_div);

	return cur;
}

static int rk817_bat_get_ocv_current0(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val = rk817_bat_field_read(battery, OCV_CUR0_H) << 8;
	val |= rk817_bat_field_read(battery, OCV_CUR0_L);

	if (val & 0x8000)
		val -= 0x10000;

	cur = ADC_TO_CURRENT(val, battery->res_div);

	return cur;
}

static int rk817_bat_get_pwron_current(struct rk817_battery_device *battery)
{
	int cur, val = 0;

	val = rk817_bat_field_read(battery, PWRON_CUR_H) << 8;
	val |= rk817_bat_field_read(battery, PWRON_CUR_L);

	if (val & 0x8000)
		val -= 0x10000;
	cur = ADC_TO_CURRENT(val, battery->res_div);

	return cur;
}

static bool rk817_bat_remain_cap_is_valid(struct rk817_battery_device *battery)
{
	return !(rk817_bat_field_read(battery, Q_PRESS_H3) & CAP_INVALID);
}

static u32 rk817_bat_get_capacity_uah(struct rk817_battery_device *battery)
{
	u32 val = 0, capacity = 0;

	if (rk817_bat_remain_cap_is_valid(battery)) {
		val = rk817_bat_field_read(battery, Q_PRESS_H3) << 24;
		val |= rk817_bat_field_read(battery, Q_PRESS_H2) << 16;
		val |= rk817_bat_field_read(battery, Q_PRESS_L1) << 8;
		val |= rk817_bat_field_read(battery, Q_PRESS_L0) << 0;

		capacity = ADC_TO_CAPACITY_UAH(val, battery->res_div);
	}

	DBG("xxxxxxxxxxxxx capacity = %d\n", capacity);
	return  capacity;
}

static u32 rk817_bat_get_capacity_mah(struct rk817_battery_device *battery)
{
	u32 val, capacity = 0;

	if (rk817_bat_remain_cap_is_valid(battery)) {
		val = rk817_bat_field_read(battery, Q_PRESS_H3) << 24;
		val |= rk817_bat_field_read(battery, Q_PRESS_H2) << 16;
		val |= rk817_bat_field_read(battery, Q_PRESS_L1) << 8;
		val |= rk817_bat_field_read(battery, Q_PRESS_L0) << 0;

		capacity = ADC_TO_CAPACITY(val, battery->res_div);
	}
	DBG("Q_PRESS_H3 = 0x%x\n", rk817_bat_field_read(battery, Q_PRESS_H3));
	DBG("Q_PRESS_H2 = 0x%x\n", rk817_bat_field_read(battery, Q_PRESS_H2));
	DBG("Q_PRESS_H1 = 0x%x\n", rk817_bat_field_read(battery, Q_PRESS_L1));
	DBG("Q_PRESS_H0 = 0x%x\n", rk817_bat_field_read(battery, Q_PRESS_L0));

	DBG("xxxxxxxxxxxxx capacity = %d\n", capacity);
	return  capacity;
}

static void  fuel_gauge_q_init_info(struct rk817_battery_device *battery)
{
	DBG("Q_INIT_H3 = 0x%x\n", rk817_bat_field_read(battery, Q_INIT_H3));
	DBG("Q_INIT_H2 = 0x%x\n", rk817_bat_field_read(battery, Q_INIT_H2));
	DBG("Q_INIT_L1 = 0x%x\n", rk817_bat_field_read(battery, Q_INIT_L1));
	DBG("Q_INIT_L0 = 0x%x\n", rk817_bat_field_read(battery, Q_INIT_L0));
}

static void rk817_bat_init_coulomb_cap(struct rk817_battery_device *battery,
				       u32 capacity)
{
	u8 buf;
	u32 cap;

	fuel_gauge_q_init_info(battery);
	cap = CAPACITY_TO_ADC(capacity, battery->res_div);
	DBG("new cap: 0x%x\n", cap);
	buf = (cap >> 24) & 0xff;
	rk817_bat_field_write(battery, Q_INIT_H3, buf);
	buf = (cap >> 16) & 0xff;
	rk817_bat_field_write(battery, Q_INIT_H2, buf);
	buf = (cap >> 8) & 0xff;
	rk817_bat_field_write(battery, Q_INIT_L1, buf);
	buf = (cap >> 0) & 0xff;
	rk817_bat_field_write(battery, Q_INIT_L0, buf);

	battery->rsoc = capacity * 1000 * 100 / battery->fcc;
	battery->remain_cap = capacity * 1000;
	DBG("new remaincap: %d\n", battery->remain_cap);
	fuel_gauge_q_init_info(battery);
}

static void rk817_bat_save_cap(struct rk817_battery_device *battery,
			       int capacity)
{
	u8 buf;
	static u32 old_cap;

	if (capacity >= battery->qmax)
		capacity = battery->qmax;
	if (capacity <= 0)
		capacity = 0;
	if (old_cap == capacity)
		return;

	old_cap = capacity;
	buf = (capacity >> 16) & 0xff;
	rk817_bat_field_write(battery, REMAIN_CAP_REG2, buf);
	buf = (capacity >> 8) & 0xff;
	rk817_bat_field_write(battery, REMAIN_CAP_REG1, buf);
	buf = (capacity >> 0) & 0xff;
	rk817_bat_field_write(battery, REMAIN_CAP_REG0, buf);
}

static void rk817_bat_update_qmax(struct rk817_battery_device *battery,
				  u32 capacity)
{
	u8 buf;
	u32 cap_adc;

	cap_adc = CAPACITY_TO_ADC(capacity, battery->res_div);
	buf = (cap_adc >> 24) & 0xff;
	rk817_bat_field_write(battery, Q_MAX_H3, buf);
	buf = (cap_adc >> 16) & 0xff;
	rk817_bat_field_write(battery, Q_MAX_H2, buf);
	buf = (cap_adc >> 8) & 0xff;
	rk817_bat_field_write(battery, Q_MAX_L1, buf);
	buf = (cap_adc >> 0) & 0xff;
	rk817_bat_field_write(battery, Q_MAX_L0, buf);
	 battery->qmax = capacity;
}

static int rk817_bat_get_qmax(struct rk817_battery_device *battery)
{
	u32 capacity;
	int val = 0;

	val = rk817_bat_field_read(battery, Q_MAX_H3) << 24;
	val |= rk817_bat_field_read(battery, Q_MAX_H2) << 16;
	val |= rk817_bat_field_read(battery, Q_MAX_L1) << 8;
	val |= rk817_bat_field_read(battery, Q_MAX_L0) << 0;
	capacity = ADC_TO_CAPACITY(val, battery->res_div);
	battery->qmax = capacity;
	return capacity;
}

static void rk817_bat_save_fcc(struct rk817_battery_device *battery, int  fcc)
{
	u8 buf;

	buf = (fcc >> 16) & 0xff;
	rk817_bat_field_write(battery, NEW_FCC_REG2, buf);
	buf = (fcc >> 8) & 0xff;
	rk817_bat_field_write(battery, NEW_FCC_REG1, buf);
	buf = (fcc >> 0) & 0xff;
	rk817_bat_field_write(battery, NEW_FCC_REG0, buf);
}

static int rk817_bat_get_fcc(struct rk817_battery_device *battery)
{
	u32 fcc = 0;

	fcc |= rk817_bat_field_read(battery, NEW_FCC_REG2) << 16;
	fcc |= rk817_bat_field_read(battery, NEW_FCC_REG1) << 8;
	fcc |= rk817_bat_field_read(battery, NEW_FCC_REG0) << 0;

	if (fcc < MIN_FCC) {
		DBG("invalid fcc(%d), use design cap", fcc);
		fcc = battery->pdata->design_capacity;
		rk817_bat_save_fcc(battery, fcc);
	} else if (fcc > battery->pdata->design_qmax) {
		DBG("invalid fcc(%d), use qmax", fcc);
		fcc = battery->pdata->design_qmax;
		rk817_bat_save_fcc(battery, fcc);
	}

	return fcc;
}

static int rk817_bat_get_rsoc(struct rk817_battery_device *battery)
{
	int remain_cap;

	remain_cap = rk817_bat_get_capacity_uah(battery);

	return remain_cap * 100 / DIV(battery->fcc);
}

static int rk817_bat_get_off_count(struct rk817_battery_device *battery)
{
	return rk817_bat_field_read(battery, OFF_CNT);
}

static int rk817_bat_get_ocv_count(struct rk817_battery_device *battery)
{
	return rk817_bat_field_read(battery, OCV_CNT);
}

static int rk817_bat_vol_to_soc(struct rk817_battery_device *battery,
				int voltage)
{
	u32 *ocv_table, temp;
	int ocv_size, ocv_soc;

	ocv_table = battery->pdata->ocv_table;
	ocv_size = battery->pdata->ocv_size;
	temp = interpolate(voltage, ocv_table, ocv_size);
	ocv_soc = ab_div_c(temp, MAX_PERCENTAGE, MAX_INTERPOLATE);

	return ocv_soc;
}

static int rk817_bat_vol_to_cap(struct rk817_battery_device *battery,
				int voltage)
{
	u32 *ocv_table, temp;
	int ocv_size, capacity;

	ocv_table = battery->pdata->ocv_table;
	ocv_size = battery->pdata->ocv_size;
	temp = interpolate(voltage, ocv_table, ocv_size);
	capacity = ab_div_c(temp, battery->fcc, MAX_INTERPOLATE);

	return capacity;
}

static void rk817_bat_save_dsoc(struct rk817_battery_device *battery,
				int save_soc)
{
	static int last_soc = -1;

	if (last_soc != save_soc) {
		rk817_bat_field_write(battery, SOC_REG0,
				      save_soc & 0xff);
		rk817_bat_field_write(battery, SOC_REG1,
				      (save_soc >> 8) & 0xff);
		rk817_bat_field_write(battery, SOC_REG2,
				      (save_soc >> 16) & 0xff);

		last_soc = save_soc;
	}
}

static int rk817_bat_get_prev_dsoc(struct rk817_battery_device *battery)
{
	int soc_save;

	soc_save = rk817_bat_field_read(battery, SOC_REG0);
	soc_save |= (rk817_bat_field_read(battery, SOC_REG1) << 8);
	soc_save |= (rk817_bat_field_read(battery, SOC_REG2) << 16);

	return soc_save;
}

static bool is_rk817_bat_first_pwron(struct rk817_battery_device *battery)
{
	if (rk817_bat_field_read(battery, BAT_CON)) {
		rk817_bat_field_write(battery, BAT_CON, 0x00);
		return true;
	}

	return false;
}

static int rk817_bat_get_charge_status(struct rk817_battery_device *battery)
{
	int status;

	status = rk817_bat_field_read(battery, CHG_STS);

	switch (status) {
	case CHRG_OFF:
		DBG("charge off...\n");
		break;
	case DEAD_CHRG:
		DBG("dead charge...\n");
		break;
	case TRICKLE_CHRG:
		DBG("trickle charge...\n");
		break;
	case CC_OR_CV_CHRG:
		DBG("CC or CV charge...\n");
		break;
	case CHARGE_FINISH:
		DBG("charge finish...\n");
		break;
	case USB_OVER_VOL:
		DBG("USB over voltage...\n");
		break;
	case BAT_TMP_ERR:
		DBG("battery temperature error...\n");
		break;
	case BAT_TIM_ERR:
		DBG("battery timer error..\n");
		break;
	default:
		break;
	}

	return status;
}

/*
 * cccv and finish switch all the time will cause dsoc freeze,
 * if so, do finish chrg, 100ma is less than min finish_ma.
 */
static bool rk817_bat_fake_finish_mode(struct rk817_battery_device *battery)
{
	if ((battery->rsoc == 100) &&
	    (rk817_bat_get_charge_status(battery) == CC_OR_CV_CHRG) &&
	    (abs(battery->current_avg) <= 100))
		return true;
	else
		return false;
}

static int get_charge_status(struct rk817_battery_device *battery)
{
	return rk817_bat_get_charge_status(battery);
}

static bool is_rk817_bat_ocv_valid(struct rk817_battery_device *battery)
{
	return (!battery->is_initialized && battery->pwroff_min >= 30);
}

static void rk817_bat_gas_gaugle_enable(struct rk817_battery_device *battery)
{
		rk817_bat_field_write(battery, GG_EN, ENABLE);
}

static void rk817_bat_gg_con_init(struct rk817_battery_device *battery)
{
	rk817_bat_field_write(battery, RLX_SPT, S_8_MIN);
	rk817_bat_field_write(battery, ADC_OFF_CAL_INTERV, S_8_MIN);
	rk817_bat_field_write(battery, VOL_OUT_MOD, AVERAGE_MODE);
	rk817_bat_field_write(battery, CUR_OUT_MOD, AVERAGE_MODE);
}

static void  rk817_bat_adc_init(struct rk817_battery_device *battery)
{
	rk817_bat_field_write(battery, SYS_VOL_ADC_EN, ENABLE);
	rk817_bat_field_write(battery, TS_ADC_EN, ENABLE);
	rk817_bat_field_write(battery, USB_VOL_ADC_EN, ENABLE);
	rk817_bat_field_write(battery, BAT_VOL_ADC_EN, ENABLE);
	rk817_bat_field_write(battery, BAT_CUR_ADC_EN, ENABLE);
}

static void rk817_bat_init_info(struct rk817_battery_device *battery)
{
	battery->design_cap = battery->pdata->design_capacity;
	battery->qmax = battery->pdata->design_qmax;
	battery->bat_res = battery->pdata->bat_res;
	battery->monitor_ms = battery->pdata->monitor_sec * TIMER_MS_COUNTS;
	battery->res_div = (battery->pdata->sample_res == SAMPLE_RES_20MR) ?
		       SAMPLE_RES_DIV2 : SAMPLE_RES_DIV1;
	DBG("battery->qmax :%d\n", battery->qmax);
}

static int rk817_bat_get_prev_cap(struct rk817_battery_device *battery)
{
	int val = 0;

	val = rk817_bat_field_read(battery, REMAIN_CAP_REG2) << 16;
	val |= rk817_bat_field_read(battery, REMAIN_CAP_REG1) << 8;
	val |= rk817_bat_field_read(battery, REMAIN_CAP_REG0) << 0;

	return val;
}

static u8 rk817_bat_get_halt_cnt(struct rk817_battery_device *battery)
{
	return rk817_bat_field_read(battery, HALT_CNT_REG);
}

static void rk817_bat_inc_halt_cnt(struct rk817_battery_device *battery)
{
	u8 cnt;

	cnt =  rk817_bat_field_read(battery, HALT_CNT_REG);
	rk817_bat_field_write(battery, HALT_CNT_REG, ++cnt);
}

static bool is_rk817_bat_last_halt(struct rk817_battery_device *battery)
{
	int pre_cap = rk817_bat_get_prev_cap(battery);
	int now_cap = rk817_bat_get_capacity_mah(battery);

	/* over 10%: system halt last time */
	if (abs(now_cap - pre_cap) > (battery->fcc / 10)) {
		rk817_bat_inc_halt_cnt(battery);
		return true;
	} else {
		return false;
	}
}

static u8 is_rk817_bat_initialized(struct rk817_battery_device *battery)
{
	u8 val = rk817_bat_field_read(battery, FG_INIT);

	if (val) {
		rk817_bat_field_write(battery, FG_INIT, 0x00);
		return true;
	} else {
		return false;
	}
}

static void rk817_bat_calc_sm_linek(struct rk817_battery_device *battery)
{
	int linek;
	int diff, delta;
	int current_avg = rk817_bat_get_avg_current(battery);

	delta = abs(battery->dsoc - battery->rsoc);
	diff = delta * 3;/* speed:3/4 */

	if (current_avg > 0) {
		if (battery->dsoc < battery->rsoc)
			linek = 1000 * (delta + diff) / DIV(diff);
		else if (battery->dsoc > battery->rsoc)
			linek = 1000 * diff / DIV(delta + diff);
		else
			linek = 1000;
	} else {
		if (battery->dsoc < battery->rsoc)
			linek = -1000 * diff / DIV(delta + diff);
		else if (battery->dsoc > battery->rsoc)
			linek = -1000 * (delta + diff) / DIV(diff);
		else
			linek = -1000;
	}

	battery->dbg_meet_soc = (battery->dsoc >= battery->rsoc) ?
		(battery->dsoc - diff) : (battery->rsoc - diff);

	battery->sm_linek = linek;
	battery->sm_remain_cap = battery->remain_cap;
	battery->dbg_calc_dsoc = battery->dsoc;
	battery->dbg_calc_rsoc = battery->rsoc;
}

static void rk817_bat_smooth_algo_prepare(struct rk817_battery_device *battery)
{
	battery->smooth_soc = battery->dsoc;

	DBG("<%s>. dsoc=%d, dsoc:smooth_soc=%d\n",
	    __func__, battery->dsoc, battery->smooth_soc);
	rk817_bat_calc_sm_linek(battery);
}

static void rk817_bat_finish_algo_prepare(struct rk817_battery_device *battery)
{
	battery->finish_base = get_boot_sec();

	if (!battery->finish_base)
		battery->finish_base = 1;
}

static void rk817_bat_init_dsoc_algorithm(struct rk817_battery_device *battery)
{
	if (battery->dsoc >= 100 * 1000)
		battery->dsoc = 100 * 1000;
	else if (battery->dsoc <= 0)
		battery->dsoc = 0;
	/* init current mode */
	battery->voltage_avg = rk817_bat_get_battery_voltage(battery);
	battery->current_avg = rk817_bat_get_avg_current(battery);

	if (get_charge_status(battery) == CHARGE_FINISH) {
		rk817_bat_finish_algo_prepare(battery);
		battery->work_mode = MODE_FINISH;
	} else {
		rk817_bat_smooth_algo_prepare(battery);
		battery->work_mode = MODE_SMOOTH;
	}
	DBG("%s, sm_remain_cap = %d, smooth_soc = %d\n",
	    __func__, battery->sm_remain_cap, battery->smooth_soc);
}

static void rk817_bat_first_pwron(struct rk817_battery_device *battery)
{
	battery->rsoc =
		rk817_bat_vol_to_soc(battery,
				     battery->pwron_voltage) * 1000;/* uAH */
	battery->dsoc = battery->rsoc;
	battery->fcc	= battery->pdata->design_capacity;
	battery->nac = rk817_bat_vol_to_cap(battery, battery->pwron_voltage);

	rk817_bat_update_qmax(battery, battery->qmax);
	rk817_bat_save_fcc(battery, battery->fcc);
	DBG("%s, rsoc = %d, dsoc = %d, fcc = %d, nac = %d\n",
	    __func__, battery->rsoc, battery->dsoc, battery->fcc, battery->nac);
}

static void rk817_bat_not_first_pwron(struct rk817_battery_device *battery)
{
	int now_cap, pre_soc, pre_cap, ocv_cap, ocv_soc, ocv_vol;

	battery->fcc = rk817_bat_get_fcc(battery);
	pre_soc = rk817_bat_get_prev_dsoc(battery);
	pre_cap = rk817_bat_get_prev_cap(battery);
	now_cap = rk817_bat_get_capacity_mah(battery);
	battery->remain_cap = pre_cap * 1000;
	battery->is_halt = is_rk817_bat_last_halt(battery);
	battery->halt_cnt = rk817_bat_get_halt_cnt(battery);
	battery->is_initialized = is_rk817_bat_initialized(battery);
	battery->is_ocv_calib = is_rk817_bat_ocv_valid(battery);

	if (battery->is_halt) {
		BAT_INFO("system halt last time... cap: pre=%d, now=%d\n",
			 pre_cap, now_cap);
		if (now_cap < 0)
			now_cap = 0;
		rk817_bat_init_coulomb_cap(battery, now_cap);
		pre_cap = now_cap;
		pre_soc = battery->rsoc;
		goto finish;
	} else if (battery->is_initialized) {
		/* uboot initialized */
		BAT_INFO("initialized yet..\n");
		goto finish;
	} else if (battery->is_ocv_calib) {
		/* not initialized and poweroff_cnt above 30 min */
		ocv_vol = rk817_bat_get_ocv_voltage(battery);
		ocv_soc = rk817_bat_vol_to_soc(battery, ocv_vol);
		ocv_cap = rk817_bat_vol_to_cap(battery, ocv_vol);
		pre_cap = ocv_cap;
		battery->ocv_pre_dsoc = pre_soc;
		battery->ocv_new_dsoc = ocv_soc;
		if (abs(ocv_soc - pre_soc) >= battery->pdata->max_soc_offset) {
			battery->ocv_pre_dsoc = pre_soc;
			battery->ocv_new_dsoc = ocv_soc;
			battery->is_max_soc_offset = true;
			BAT_INFO("trigger max soc offset, dsoc: %d -> %d\n",
				 pre_soc, ocv_soc);
			pre_soc = ocv_soc;
		}
		BAT_INFO("OCV calib: cap=%d, rsoc=%d\n", ocv_cap, ocv_soc);
	} else if (battery->pwroff_min > 0) {
		ocv_vol = rk817_bat_get_ocv_voltage(battery);
		ocv_soc = rk817_bat_vol_to_soc(battery, ocv_vol);
		ocv_cap = rk817_bat_vol_to_cap(battery, ocv_vol);
		battery->force_pre_dsoc = pre_soc;
		battery->force_new_dsoc = ocv_soc;
		if (abs(ocv_soc - pre_soc) >= 80) {
			battery->is_force_calib = true;
			BAT_INFO("dsoc force calib: %d -> %d\n",
				 pre_soc, ocv_soc);
			pre_soc = ocv_soc;
			pre_cap = ocv_cap;
		}
	}
finish:
	battery->dsoc = pre_soc;
	battery->nac = pre_cap;
	if (battery->nac < 0)
		battery->nac = 0;

	DBG("dsoc=%d cap=%d v=%d ov=%d rv=%d min=%d psoc=%d pcap=%d\n",
	    battery->dsoc, battery->nac, rk817_bat_get_battery_voltage(battery),
	    rk817_bat_get_ocv_voltage(battery),
	    rk817_bat_get_relax_voltage(battery),
	    battery->pwroff_min, rk817_bat_get_prev_dsoc(battery),
	    rk817_bat_get_prev_cap(battery));
}

static void rk817_bat_rsoc_init(struct rk817_battery_device *battery)
{
	battery->is_first_power_on = is_rk817_bat_first_pwron(battery);
	battery->pwroff_min = rk817_bat_get_off_count(battery);
	battery->pwron_voltage = rk817_bat_get_pwron_voltage(battery);

	DBG("%s, is_first_power_on = %d, pwroff_min = %d, pwron_voltage = %d\n",
	    __func__, battery->is_first_power_on,
	    battery->pwroff_min, battery->pwron_voltage);

	if (battery->is_first_power_on)
		rk817_bat_first_pwron(battery);
	else
		rk817_bat_not_first_pwron(battery);

	 rk817_bat_save_dsoc(battery, battery->dsoc);
}

static void rk817_bat_caltimer_isr(unsigned long data)
{
	struct rk817_battery_device *battery =
		(struct rk817_battery_device *)data;

	mod_timer(&battery->caltimer, jiffies + MINUTE(8) * HZ);
	queue_delayed_work(battery->bat_monitor_wq,
			   &battery->calib_delay_work,
			   msecs_to_jiffies(10));
}

static void rk817_bat_internal_calib(struct work_struct *work)
{
	struct rk817_battery_device *battery = container_of(work,
			struct rk817_battery_device, calib_delay_work.work);

	return;

	rk817_bat_current_calibration(battery);
	/* calib voltage kb */
	rk817_bat_init_voltage_kb(battery);

	DBG("caltimer:coffset=0x%x\n", rk817_bat_get_coffset(battery));
}

static void rk817_bat_init_caltimer(struct rk817_battery_device *battery)
{
	setup_timer(&battery->caltimer,
		    rk817_bat_caltimer_isr,
		    (unsigned long)battery);
	battery->caltimer.expires = jiffies + MINUTE(8) * HZ;
	add_timer(&battery->caltimer);
	INIT_DELAYED_WORK(&battery->calib_delay_work, rk817_bat_internal_calib);
}

static void rk817_bat_init_fg(struct rk817_battery_device *battery)
{
	rk817_bat_adc_init(battery);
	rk817_bat_gas_gaugle_enable(battery);
	rk817_bat_gg_con_init(battery);
	rk817_bat_init_voltage_kb(battery);
	rk817_bat_set_relax_sample(battery);
	rk817_bat_ocv_thre(battery, 0xff);
	rk817_bat_init_caltimer(battery);
	rk817_bat_rsoc_init(battery);
	rk817_bat_init_coulomb_cap(battery, battery->nac);
	DBG("rsoc%d, fcc = %d\n", battery->rsoc, battery->fcc);
	rk817_bat_init_dsoc_algorithm(battery);
	battery->qmax = rk817_bat_get_qmax(battery);
	battery->voltage_avg = rk817_bat_get_battery_voltage(battery);
	battery->voltage_sys = rk817_bat_get_sys_voltage(battery);

	battery->voltage_ocv = rk817_bat_get_ocv_voltage(battery);
	battery->voltage_relax = rk817_bat_get_relax_voltage(battery);
	battery->current_avg = rk817_bat_get_avg_current(battery);
	battery->dbg_pwr_dsoc = battery->dsoc;
	battery->dbg_pwr_rsoc = battery->rsoc;
	battery->dbg_pwr_vol = battery->voltage_avg;
	battery->temperature = VIRTUAL_TEMPERATURE;

	DBG("probe init: battery->dsoc = %d, rsoc = %d\n"
	    "remain_cap = %d\n, battery_vol = %d\n, system_vol = %d, qmax = %d\n",
	    battery->dsoc, battery->rsoc, battery->remain_cap,
	    battery->voltage_avg, battery->voltage_sys, battery->qmax);
	DBG("OCV_THRE_VOL: 0x%x", rk817_bat_field_read(battery, OCV_THRE_VOL));
}

static int rk817_bat_parse_dt(struct rk817_battery_device *battery)
{
	u32 out_value;
	int length, ret;
	size_t size;
	struct battery_platform_data *pdata;
	struct device *dev = battery->dev;
	struct device_node *np = battery->dev->of_node;

	pdata = devm_kzalloc(battery->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	battery->pdata = pdata;
	/* init default param */
	pdata->bat_res = DEFAULT_BAT_RES;
	pdata->monitor_sec = DEFAULT_MONITOR_SEC;
	pdata->pwroff_vol = DEFAULT_PWROFF_VOL_THRESD;
	pdata->sleep_exit_current = DEFAULT_SLP_EXIT_CUR;
	pdata->sleep_enter_current = DEFAULT_SLP_ENTER_CUR;

	pdata->sleep_filter_current = DEFAULT_SLP_FILTER_CUR;
	pdata->bat_mode = MODE_BATTARY;
	pdata->max_soc_offset = DEFAULT_MAX_SOC_OFFSET;
	pdata->fb_temp = DEFAULT_FB_TEMP;
	pdata->energy_mode = DEFAULT_ENERGY_MODE;
	pdata->zero_reserve_dsoc = DEFAULT_ZERO_RESERVE_DSOC * 1000;

	pdata->sample_res = DEFAULT_SAMPLE_RES;

	/* parse necessary param */
	if (!of_find_property(np, "ocv_table", &length)) {
		dev_err(dev, "ocv_table not found!\n");
		return -EINVAL;
	}

	pdata->ocv_size = length / sizeof(u32);
	if (pdata->ocv_size <= 0) {
		dev_err(dev, "invalid ocv table\n");
		return -EINVAL;
	}

	size = sizeof(*pdata->ocv_table) * pdata->ocv_size;
	pdata->ocv_table = devm_kzalloc(battery->dev, size, GFP_KERNEL);
	if (!pdata->ocv_table)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "ocv_table", pdata->ocv_table,
					 pdata->ocv_size);
	if (ret < 0)
		return ret;

	ret = of_property_read_u32(np, "design_capacity", &out_value);
	if (ret < 0) {
		dev_err(dev, "design_capacity not found!\n");
		return ret;
	}
	pdata->design_capacity = out_value;

	ret = of_property_read_u32(np, "design_qmax", &out_value);
	if (ret < 0) {
		dev_err(dev, "design_qmax not found!\n");
		return ret;
	}
	pdata->design_qmax = out_value;

	/* parse unnecessary param */
	ret = of_property_read_u32(np, "sample_res", &pdata->sample_res);
	if (ret < 0)
		dev_err(dev, "sample_res missing!\n");

	ret = of_property_read_u32(np, "fb_temperature", &pdata->fb_temp);
	if (ret < 0)
		dev_err(dev, "fb_temperature missing!\n");

	ret = of_property_read_u32(np, "energy_mode", &pdata->energy_mode);
	if (ret < 0)
		dev_err(dev, "energy_mode missing!\n");

	ret = of_property_read_u32(np, "max_soc_offset",
				   &pdata->max_soc_offset);
	if (ret < 0)
		dev_err(dev, "max_soc_offset missing!\n");

	ret = of_property_read_u32(np, "monitor_sec", &pdata->monitor_sec);
	if (ret < 0)
		dev_err(dev, "monitor_sec missing!\n");

	ret = of_property_read_u32(np, "zero_algorithm_vol",
				   &pdata->zero_algorithm_vol);
	if (ret < 0)
		dev_err(dev, "zero_algorithm_vol missing!\n");

	ret = of_property_read_u32(np, "zero_reserve_dsoc",
				   &pdata->zero_reserve_dsoc);
	if (ret < 0)
		dev_err(dev, "zero_reserve_dsoc missing!\n");
	pdata->zero_reserve_dsoc *= 1000;

	ret = of_property_read_u32(np, "virtual_power", &pdata->bat_mode);
	if (ret < 0)
		dev_err(dev, "virtual_power missing!\n");

	ret = of_property_read_u32(np, "bat_res", &pdata->bat_res);
	if (ret < 0)
		dev_err(dev, "bat_res missing!\n");

	ret = of_property_read_u32(np, "sleep_enter_current",
				   &pdata->sleep_enter_current);
	if (ret < 0)
		dev_err(dev, "sleep_enter_current missing!\n");

	ret = of_property_read_u32(np, "sleep_exit_current",
				   &pdata->sleep_exit_current);
	if (ret < 0)
		dev_err(dev, "sleep_exit_current missing!\n");

	ret = of_property_read_u32(np, "sleep_filter_current",
				   &pdata->sleep_filter_current);
	if (ret < 0)
		dev_err(dev, "sleep_filter_current missing!\n");

	ret = of_property_read_u32(np, "power_off_thresd", &pdata->pwroff_vol);
	if (ret < 0)
		dev_err(dev, "power_off_thresd missing!\n");

	DBG("the battery dts info dump:\n"
	    "bat_res:%d\n"
	    "res_sample:%d\n"
	    "design_capacity:%d\n"
	    "design_qmax :%d\n"
	    "sleep_enter_current:%d\n"
	    "sleep_exit_current:%d\n"
	    "sleep_filter_current:%d\n"
	    "zero_algorithm_vol:%d\n"
	    "zero_reserve_dsoc:%d\n"
	    "monitor_sec:%d\n"
	    "max_soc_offset:%d\n"
	    "virtual_power:%d\n"
	    "pwroff_vol:%d\n",
	    pdata->bat_res,
	    pdata->sample_res,
	    pdata->design_capacity,
	    pdata->design_qmax,
	    pdata->sleep_enter_current,
	    pdata->sleep_exit_current,
	    pdata->sleep_filter_current,
	    pdata->zero_algorithm_vol,
	    pdata->zero_reserve_dsoc,
	    pdata->monitor_sec,
	    pdata->max_soc_offset,
	    pdata->bat_mode,
	    pdata->pwroff_vol);

	return 0;
}

static enum power_supply_property rk817_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
};

static int rk817_bat_get_usb_psy(struct device *dev, void *data)
{
	struct rk817_battery_device *battery = data;
	struct power_supply *psy = dev_get_drvdata(dev);

	if (psy->desc->type == POWER_SUPPLY_TYPE_USB) {
		battery->usb_psy = psy;
		return 1;
	}

	return 0;
}

static int rk817_bat_get_ac_psy(struct device *dev, void *data)
{
	struct rk817_battery_device *battery = data;
	struct power_supply *psy = dev_get_drvdata(dev);

	if (psy->desc->type == POWER_SUPPLY_TYPE_MAINS) {
		battery->ac_psy = psy;
		return 1;
	}

	return 0;
}

static void rk817_bat_get_chrg_psy(struct rk817_battery_device *battery)
{
	if (!battery->usb_psy)
		class_for_each_device(power_supply_class, NULL, (void *)battery,
				      rk817_bat_get_usb_psy);
	if (!battery->ac_psy)
		class_for_each_device(power_supply_class, NULL, (void *)battery,
				      rk817_bat_get_ac_psy);
}

static int rk817_bat_get_charge_state(struct rk817_battery_device *battery)
{
	union power_supply_propval val;
	int ret;
	struct power_supply *psy;

	if (!battery->usb_psy || !battery->ac_psy)
		rk817_bat_get_chrg_psy(battery);

	psy = battery->usb_psy;
	if (psy) {
		ret = psy->desc->get_property(psy, POWER_SUPPLY_PROP_ONLINE,
					      &val);
		if (!ret)
			battery->usb_in = val.intval;
	}

	psy = battery->ac_psy;
	if (psy) {
		ret = psy->desc->get_property(psy, POWER_SUPPLY_PROP_ONLINE,
					      &val);
		if (!ret)
			battery->ac_in = val.intval;
	}

	DBG("%s: ac_online=%d, usb_online=%d\n",
	    __func__, battery->ac_in, battery->usb_in);

	return (battery->usb_in || battery->ac_in);
}

static int rk817_battery_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct rk817_battery_device *battery = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = battery->current_avg * 1000;/*uA*/
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_CURRENT * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = battery->voltage_avg * 1000;/*uV*/
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_VOLTAGE * 1000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = (battery->dsoc  + 500) / 1000;
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_SOC;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = battery->temperature;
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_TEMPERATURE;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (battery->pdata->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_STATUS;
		else if (battery->dsoc == 100 * 1000)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if (rk817_bat_get_charge_state(battery))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = battery->charge_count;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = 4500 * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = 5000 * 1000;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct power_supply_desc rk817_bat_desc = {
	.name		= "battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= rk817_bat_props,
	.num_properties	= ARRAY_SIZE(rk817_bat_props),
	.get_property	= rk817_battery_get_property,
};

static int rk817_bat_init_power_supply(struct rk817_battery_device *battery)
{
	struct power_supply_config psy_cfg = { .drv_data = battery, };

	battery->bat = devm_power_supply_register(battery->dev,
						  &rk817_bat_desc,
						  &psy_cfg);
	if (IS_ERR(battery->bat)) {
		dev_err(battery->dev, "register bat power supply fail\n");
		return PTR_ERR(battery->bat);
	}

	return 0;
}

static void rk817_bat_power_supply_changed(struct rk817_battery_device *battery)
{
	static int old_soc = -1;

	if (battery->dsoc > 100 * 1000)
		battery->dsoc = 100 * 1000;
	else if (battery->dsoc < 0)
		battery->dsoc = 0;

	if (battery->dsoc == old_soc)
		return;

	old_soc = battery->dsoc;
	battery->last_dsoc = battery->dsoc;
	power_supply_changed(battery->bat);
	DBG("changed: dsoc=%d, rsoc=%d, v=%d, ov=%d c=%d, cap=%d, f=%d\n",
	    battery->dsoc, battery->rsoc, battery->voltage_avg,
	    battery->voltage_ocv, battery->current_avg,
	    battery->remain_cap, battery->fcc);

	DBG("dl=%d, rl=%d, v=%d, halt=%d, halt_n=%d, max=%d\n"
	    "init=%d, sw=%d, calib=%d, below0=%d, force=%d\n",
	    battery->dbg_pwr_dsoc, battery->dbg_pwr_rsoc,
	    battery->dbg_pwr_vol,
	    battery->is_halt, battery->halt_cnt,
	    battery->is_max_soc_offset,
	    battery->is_initialized, battery->is_sw_reset,
	    battery->is_ocv_calib,
	    battery->dbg_cap_low0, battery->is_force_calib);
}

static void rk817_battery_debug_info(struct rk817_battery_device *battery)
{
	rk817_bat_get_battery_voltage(battery);
	rk817_bat_get_sys_voltage(battery);
	rk817_bat_get_USB_voltage(battery);
	rk817_bat_get_pwron_voltage(battery);
	rk817_bat_get_ocv_voltage(battery);
	rk817_bat_get_ocv0_voltage0(battery);

	rk817_bat_current_calibration(battery);
	rk817_bat_get_avg_current(battery);
	rk817_bat_get_relax_cur1(battery);
	rk817_bat_get_relax_cur2(battery);
	rk817_bat_get_relax_current(battery);
	rk817_bat_get_ocv_current(battery);
	rk817_bat_get_ocv_current0(battery);
	rk817_bat_get_pwron_current(battery);
	rk817_bat_get_ocv_count(battery);
	rk817_bat_save_dsoc(battery, battery->dsoc);
	DBG("capactiy = %d\n", rk817_bat_get_capacity_mah(battery));
}

static void
rk817_bat_update_charging_status(struct rk817_battery_device *battery)
{
	int is_charging;

	is_charging = rk817_bat_get_charge_state(battery);
	if (is_charging == battery->is_charging)
		return;

	battery->is_charging = is_charging;
	if (is_charging)
		battery->charge_count++;
}

static void rk817_bat_update_info(struct rk817_battery_device *battery)
{
	battery->voltage_avg = rk817_bat_get_battery_voltage(battery);
	battery->voltage_sys = rk817_bat_get_sys_voltage(battery);
	battery->current_avg = rk817_bat_get_avg_current(battery);
	battery->voltage_relax = rk817_bat_get_relax_voltage(battery);
	battery->rsoc = rk817_bat_get_rsoc(battery);
	battery->remain_cap = rk817_bat_get_capacity_uah(battery);
	battery->voltage_usb = rk817_bat_get_USB_voltage(battery);
	battery->chrg_status = get_charge_status(battery);
	rk817_bat_update_charging_status(battery);
	DBG("valtage usb: %d\n", battery->voltage_usb);
	DBG("UPDATE: voltage_avg = %d\n"
	    "voltage_sys = %d\n"
	    "curren_avg = %d\n"
	    "rsoc = %d\n"
	    "chrg_status = %d\n"
	    "PWRON_CUR = %d\n"
	    "remain_cap = %d\n",
	    battery->voltage_avg,
	    battery->voltage_sys,
	    battery->current_avg,
	    battery->rsoc,
	    battery->chrg_status,
	    rk817_bat_get_pwron_current(battery),
	    battery->remain_cap);

	/* smooth charge */
	if (battery->remain_cap / 1000 > battery->fcc) {
		/*battery->sm_remain_cap -=*/
		/*(battery->remain_cap - battery->fcc * 1000);*/
		battery->sm_remain_cap = battery->fcc * 1000;
		DBG("<%s>. cap: remain=%d, sm_remain=%d\n",
		    __func__, battery->remain_cap, battery->sm_remain_cap);
		DBG("fcc: %d\n", battery->fcc);
		rk817_bat_init_coulomb_cap(battery, battery->fcc + 100);
		rk817_bat_init_coulomb_cap(battery, battery->fcc);
		rk817_bat_get_capacity_mah(battery);
	}

	if (battery->chrg_status != CHARGE_FINISH)
		battery->finish_base = get_boot_sec();
}

static void rk817_bat_save_data(struct rk817_battery_device *battery)
{
	rk817_bat_save_dsoc(battery, battery->dsoc);
	rk817_bat_save_cap(battery, battery->remain_cap / 1000);
}

/* high load: current < 0 with charger in.
 * System will not shutdown while dsoc=0% with charging state(ac_in),
 * which will cause over discharge, so oppose status before report states.
 */
static void rk817_bat_lowpwr_check(struct rk817_battery_device *battery)
{
	static u64 time;
	int pwr_off_thresd = battery->pdata->pwroff_vol;

	if (battery->current_avg < 0 && battery->voltage_avg < pwr_off_thresd) {
		if (!time)
			time = get_boot_sec();

		if ((base2sec(time) > MINUTE(1)) ||
		    (battery->voltage_avg <= pwr_off_thresd - 50)) {
			battery->fake_offline = 1;
			if (battery->voltage_avg <= pwr_off_thresd - 50)
				battery->dsoc -= 1000;
			DBG("low power, soc=%d, current=%d\n",
			    battery->dsoc, battery->current_avg);
		}
	} else {
		time = 0;
		battery->fake_offline = 0;
	}

	DBG("<%s>. t=%lu, dsoc=%d, current=%d, fake_offline=%d\n",
	    __func__, base2sec(time), battery->dsoc,
	    battery->current_avg, battery->fake_offline);
}

static void rk817_bat_calc_smooth_dischrg(struct rk817_battery_device *battery)
{
	int tmp_soc = 0;

	/* check new dsoc */
	if (battery->smooth_soc < 0)
		battery->smooth_soc = 0;

	tmp_soc = battery->smooth_soc / 1000;

	if (tmp_soc != battery->dsoc / 1000) {
		if (battery->smooth_soc > battery->dsoc)
			return;

		if (battery->smooth_soc + 1000 > battery->dsoc)
			battery->dsoc = battery->smooth_soc;
		else
			battery->dsoc -= 1000;

		if (battery->dsoc <= 0)
			battery->dsoc = 0;
	}
}

static void rk817_bat_smooth_algorithm(struct rk817_battery_device *battery)
{
	int ydsoc = 0, delta_cap = 0, old_cap = 0, tmp_soc;
	/*int linek;*/
	int diff, delta;
	/*int current_avg = rk817_bat_get_avg_current(battery);*/

	delta = abs(battery->dsoc - battery->rsoc);
	diff = delta * 3;/* speed:3/4 */

	/* charge and discharge switch */
	if ((battery->sm_linek * battery->current_avg <= 0)) {
		DBG("<%s>. linek mode, retinit sm linek..\n", __func__);
		rk817_bat_calc_sm_linek(battery);
	}

	/*battery->sm_linek = linek;*/

	battery->remain_cap = rk817_bat_get_capacity_uah(battery);

	old_cap = battery->sm_remain_cap;
	DBG("smooth: smooth_soc = %d, dsoc = %d, battery->sm_linek = %d\n",
	    battery->smooth_soc, battery->dsoc, battery->sm_linek);

	/* discharge status: sm_remain_cap > remain_cap, delta_cap > 0 */
	/* from charge to discharge:
	 * remain_cap may be above sm_remain_cap, delta_cap <= 0
	 */
	delta_cap = battery->remain_cap - battery->sm_remain_cap;
	DBG("smooth: sm_remain_cap = %d, remain_cap = %d\n",
	    battery->sm_remain_cap, battery->remain_cap);
	DBG("smooth: delta_cap = %d, dsoc = %d\n",
	    delta_cap, battery->dsoc);

	if (delta_cap == 0) {
		DBG("<%s>. delta_cap = 0\n", __func__);
		return;
	}

	/* discharge: sm_linek < 0, if delate_cap <0, ydsoc > 0 */
	ydsoc = battery->sm_linek * abs(delta_cap / DIV(battery->fcc)) / 10;

	DBG("smooth: ydsoc = %d, fcc = %d\n", ydsoc, battery->fcc);
	if (ydsoc == 0) {
		DBG("<%s>. ydsoc = 0\n", __func__);
		return;
	}
	battery->sm_remain_cap = battery->remain_cap;

	DBG("<%s>. k=%d, ydsoc=%d; cap:old=%d, new:%d; delta_cap=%d\n",
	    __func__, battery->sm_linek, ydsoc, old_cap,
	    battery->sm_remain_cap, delta_cap);

	/* discharge mode */
	/* discharge mode, but ydsoc > 0,
	 * from charge status to dischrage
	 */
	battery->smooth_soc += ydsoc;
	if (ydsoc < 0) {
		rk817_bat_calc_smooth_dischrg(battery);
	} else {
		if (battery->smooth_soc < 0)
			battery->smooth_soc = 0;

		tmp_soc = battery->smooth_soc / 1000;

		if (tmp_soc != battery->dsoc / 1000) {
			if (battery->smooth_soc < battery->dsoc)
				return;

			battery->dsoc = battery->smooth_soc;
			if (battery->dsoc <= 0)
				battery->dsoc = 0;
		}
	}

	if (battery->s2r) {
		battery->s2r = false;
		rk817_bat_calc_sm_linek(battery);
	}

	DBG("smooth: smooth_soc = %d, dsoc = %d\n",
	    battery->smooth_soc, battery->dsoc);
	DBG("smooth: delta_cap = %d, dsoc = %d\n",
	    delta_cap, battery->dsoc);
}

static void rk817_bat_calc_zero_linek(struct rk817_battery_device *battery)
{
	int dead_voltage, ocv_voltage;
	int voltage_avg, current_avg, vsys;
	int ocv_cap, dead_cap, xsoc;
	int ocv_soc, dead_soc;
	int pwroff_vol;
	int min_gap_xsoc;
	int powerpatch_res;

	if ((abs(battery->current_avg) < 400) && (battery->dsoc / 1000 > 5))
		pwroff_vol = battery->pdata->pwroff_vol + 50;
	else
		pwroff_vol = battery->pdata->pwroff_vol;

	/* calc estimate ocv voltage */
	voltage_avg = rk817_bat_get_battery_voltage(battery);
	current_avg = rk817_bat_get_avg_current(battery);
	vsys = voltage_avg + (current_avg * DEF_PWRPATH_RES) / 1000;

	powerpatch_res = (voltage_avg - vsys) * 1000 / current_avg;

	battery->zero_voltage_avg = voltage_avg;
	battery->zero_current_avg = current_avg;
	battery->zero_vsys = vsys;

	DBG("Zero: voltage_avg = %d, Vsys = %d\n", voltage_avg, vsys);
	DBG("Zero: powerpatch_res = %d\n", powerpatch_res);
	DBG("ZERO0: shtd_vol: poweroff_vol(usr) = %d\n"
	    "pwroff_vol = %d\n"
	    "zero_reserve_dsoc = %d\n",
	    battery->pdata->pwroff_vol,
	    pwroff_vol,
	    battery->pdata->zero_reserve_dsoc);

	/* get the dead ocv voltage, pwroff_vol is vsys */
	dead_voltage = pwroff_vol - current_avg *
				(battery->bat_res + DEF_PWRPATH_RES) / 1000;

	ocv_voltage = voltage_avg - (current_avg * battery->bat_res) / 1000;
	DBG("ZERO0: dead_voltage(shtd) = %d, ocv_voltage(now) = %d\n",
	    dead_voltage, ocv_voltage);

	/* calc estimate soc and cap */
	dead_soc = rk817_bat_vol_to_soc(battery, dead_voltage);
	dead_cap = rk817_bat_vol_to_cap(battery, dead_voltage);
	DBG("ZERO0: dead_soc = %d, dead_cap = %d\n",
	    dead_soc, dead_cap);

	ocv_soc = rk817_bat_vol_to_soc(battery, ocv_voltage);
	ocv_cap = rk817_bat_vol_to_cap(battery, ocv_voltage);
	DBG("ZERO0: ocv_soc = %d, ocv_cap = %d\n",
	    ocv_soc, ocv_cap);

	/* xsoc: available rsoc */
	xsoc = ocv_soc - dead_soc;

	battery->zero_dead_voltage = dead_voltage;
	battery->zero_dead_soc = dead_soc;
	battery->zero_dead_cap = dead_cap;

	battery->zero_batvol_to_ocv = ocv_voltage;
	battery->zero_batocv_to_soc = ocv_soc;
	battery->zero_batocv_to_cap = ocv_cap;

	battery->zero_xsoc = xsoc;

	DBG("Zero: xsoc = %d\n", xsoc);
	/* min_gap_xsoc: reserve xsoc */
	if (abs(current_avg) > ZERO_LOAD_LVL1)
		min_gap_xsoc = MIN_ZERO_GAP_XSOC3;
	else if (abs(current_avg) > ZERO_LOAD_LVL2)
		min_gap_xsoc = MIN_ZERO_GAP_XSOC2;
	else
		min_gap_xsoc = MIN_ZERO_GAP_XSOC1;

	if ((xsoc <= 30) &&
	    (battery->dsoc >= battery->pdata->zero_reserve_dsoc))
		min_gap_xsoc = min_gap_xsoc + MIN_ZERO_GAP_CALIB;

	battery->zero_remain_cap = battery->remain_cap;
	battery->zero_timeout_cnt = 0;
	if ((battery->dsoc / 1000 <= 1) && (xsoc > 0)) {
		battery->zero_linek = 400;
		battery->zero_drop_sec = 0;
	} else if (xsoc >= 0) {
		battery->zero_drop_sec = 0;
		battery->zero_linek =
			(battery->zero_dsoc + xsoc / 2) / DIV(xsoc);
		/* battery energy mode to use up voltage */
		if ((battery->pdata->energy_mode) &&
		    (xsoc - battery->dsoc / 1000 >= MIN_ZERO_GAP_XSOC3) &&
		    (battery->dsoc  / 1000 <= 10) && (battery->zero_linek < 300)) {
			battery->zero_linek = 300;
			DBG("ZERO-new: zero_linek adjust step0...\n");
		/* reserve enough power yet, slow down any way */
		} else if ((xsoc - battery->dsoc / 1000 >= min_gap_xsoc) ||
			   ((xsoc - battery->dsoc / 1000 >= MIN_ZERO_GAP_XSOC2) &&
			    (battery->dsoc / 1000 <= 10) && (xsoc > 15))) {
			if (xsoc <= 20 &&
			    battery->dsoc / 1000 >= battery->pdata->zero_reserve_dsoc)
				battery->zero_linek = 1200;
			else if (xsoc - battery->dsoc / 1000 >= 2 * min_gap_xsoc)
				battery->zero_linek = 400;
			else if (xsoc - battery->dsoc / 1000 >= 3 + min_gap_xsoc)
				battery->zero_linek = 600;
			else
				battery->zero_linek = 800;
			DBG("ZERO-new: zero_linek adjust step1...\n");
		/* control zero mode beginning enter */
		} else if ((battery->zero_linek > 1800) &&
			   (battery->dsoc / 1000 > 70)) {
			battery->zero_linek = 1800;
			DBG("ZERO-new: zero_linek adjust step2...\n");
		/* dsoc close to xsoc: it must reserve power */
		} else if ((battery->zero_linek > 1000) &&
			   (battery->zero_linek < 1200)) {
			battery->zero_linek = 1200;
			DBG("ZERO-new: zero_linek adjust step3...\n");
		/* dsoc[5~15], dsoc < xsoc */
		} else if ((battery->dsoc / 1000 <= 15 && battery->dsoc > 5) &&
			   (battery->zero_linek <= 1200)) {
			/* slow down */
			if ((xsoc - battery->dsoc / 1000) >= min_gap_xsoc)
				battery->zero_linek = 800;
			/* reserve power */
			else
				battery->zero_linek = 1200;
			DBG("ZERO-new: zero_linek adjust step4...\n");
		/* dsoc[5, 100], dsoc < xsoc */
		} else if ((battery->zero_linek < 1000) &&
			   (battery->dsoc / 1000 >= 5)) {
			if ((xsoc - battery->dsoc / 1000) < min_gap_xsoc) {
				/* reserve power */
				battery->zero_linek = 1200;
			} else {
				if (abs(battery->current_avg) > 500)/* heavy */
					battery->zero_linek = 900;
				else
					battery->zero_linek = 1000;
			}
			DBG("ZERO-new: zero_linek adjust step5...\n");
		/* dsoc[0~5], dsoc < xsoc */
		} else if ((battery->zero_linek < 1000) &&
			   (battery->dsoc  / 1000 <= 5)) {
			if ((xsoc - battery->dsoc / 1000) <= 3)
				battery->zero_linek = 1200;
			else
				battery->zero_linek = 800;
			DBG("ZERO-new: zero_linek adjust step6...\n");
		}
	} else {
		/* xsoc < 0 */
		battery->zero_linek = 1000;
		if (!battery->zero_drop_sec)
			battery->zero_drop_sec = get_boot_sec();
		if (base2sec(battery->zero_drop_sec) >= WAIT_DSOC_DROP_SEC) {
			DBG("ZERO0: t=%lu\n", base2sec(battery->zero_drop_sec));
			battery->zero_drop_sec = 0;
			battery->dsoc -= 1000;
			if (battery->dsoc < 0)
				battery->dsoc = 0;
			battery->zero_dsoc = battery->dsoc;
		}
	}

	if (voltage_avg < pwroff_vol - 70) {
		if (!battery->shtd_drop_sec)
			battery->shtd_drop_sec = get_boot_sec();
		if (base2sec(battery->shtd_drop_sec) > WAIT_SHTD_DROP_SEC) {
			DBG("voltage extreme low...soc:%d->0\n", battery->dsoc);
			battery->shtd_drop_sec = 0;
			battery->dsoc = 0;
		}
	} else {
		battery->shtd_drop_sec = 0;
	}

	DBG("Zero: zero_linek = %d\n", battery->zero_linek);
}

static void rk817_bat_zero_algo_prepare(struct rk817_battery_device *battery)
{
	int tmp_dsoc;

	tmp_dsoc = battery->zero_dsoc / 1000;

	if (tmp_dsoc != battery->smooth_soc / 1000)
		battery->zero_dsoc = battery->smooth_soc;

	DBG("zero_smooth: zero_dsoc = %d\n", battery->zero_dsoc);

	rk817_bat_calc_zero_linek(battery);
}

static void rk817_bat_calc_zero_algorithm(struct rk817_battery_device *battery)
{
	int tmp_soc;

	tmp_soc = battery->zero_dsoc / 1000;

	if (tmp_soc == battery->dsoc / 1000)
		return;

	if (battery->zero_dsoc > battery->dsoc)
		return;

	if (battery->zero_dsoc < battery->dsoc - 1000)
		battery->dsoc -= 1000;
	else
		battery->dsoc = battery->zero_dsoc;
}

static void rk817_bat_zero_algorithm(struct rk817_battery_device *battery)
{
	int delta_cap = 0, delta_soc = 0;

	battery->zero_timeout_cnt++;
	delta_cap = battery->zero_remain_cap - battery->remain_cap;
	delta_soc = battery->zero_linek * delta_cap / DIV(battery->fcc) / 10;

	DBG("zero algorithm start\n");
	DBG("DEAD: dead_voltage: %d\n"
	    "dead_soc: %d\n"
	    "dead_cap: %d\n"
	    "powoff_vol: %d\n",
	    battery->zero_dead_voltage,
	    battery->zero_dead_soc,
	    battery->zero_dead_cap,
	    battery->pdata->pwroff_vol);
	DBG("DEAD: bat_voltage: %d\n"
	    "bat_current: %d\n"
	    "batvol_to_ocv: %d\n"
	    "batocv_to_soc: %d\n"
	    "batocv_to_cap: %d\n",
	    battery->zero_voltage_avg,
	    battery->zero_current_avg,
	    battery->zero_batvol_to_ocv,
	    battery->zero_batocv_to_soc,
	    battery->zero_batocv_to_cap);
	DBG("DEAD: Xsoc: %d, zero_reserve_dsoc: %d\n",
	    battery->zero_xsoc, battery->pdata->zero_reserve_dsoc);
	DBG("CAP: zero_remain_cap = %d, remain_cap = %d\n",
	    battery->zero_remain_cap, battery->remain_cap);
	DBG("Zero: zero_delta_cap = %d, zero_link = %d, delta_soc = %d\n",
	    delta_cap, battery->zero_linek, delta_soc);
	DBG("zero algorithm end\n");

	if ((delta_soc >= MIN_ZERO_DSOC_ACCURACY) ||
	    (battery->zero_timeout_cnt > MIN_ZERO_OVERCNT) ||
	    (battery->zero_linek == 0)) {
		DBG("ZERO1:--------- enter calc -----------\n");
		battery->zero_timeout_cnt = 0;
		battery->zero_dsoc -= delta_soc;
		rk817_bat_calc_zero_algorithm(battery);
		DBG("Zero: dsoc: %d\n", battery->dsoc);
		rk817_bat_calc_zero_linek(battery);
	}
}

static void rk817_bat_finish_algorithm(struct rk817_battery_device *battery)
{
	unsigned long finish_sec, soc_sec;
	int plus_soc, finish_current, rest = 0;

	/* rsoc */
	if ((battery->remain_cap != battery->fcc) &&
	    (get_charge_status(battery) == CHARGE_FINISH)) {
		battery->age_adjust_cap +=
			(battery->fcc * 1000 - battery->remain_cap);
		rk817_bat_init_coulomb_cap(battery, battery->fcc);
		rk817_bat_get_capacity_mah(battery);
	}

	/* dsoc */
	if (battery->dsoc < 100 * 1000) {
		if (!battery->finish_base)
			battery->finish_base = get_boot_sec();

		finish_current = (battery->rsoc - battery->dsoc) > FINISH_MAX_SOC_DELAY ?
					FINISH_CHRG_CUR2 : FINISH_CHRG_CUR1;
		finish_sec = base2sec(battery->finish_base);

		soc_sec = battery->fcc * 3600 / 100 / DIV(finish_current);
		plus_soc = finish_sec / DIV(soc_sec);
		if (finish_sec > soc_sec) {
			rest = finish_sec % soc_sec;
			battery->dsoc += plus_soc * 1000;
			battery->finish_base = get_boot_sec();
			if (battery->finish_base > rest)
				battery->finish_base = get_boot_sec() - rest;
		}
		DBG("CHARGE_FINISH:dsoc<100,dsoc=%d\n"
		    "soc_time=%lu, sec_finish=%lu, plus_soc=%d, rest=%d\n",
		    battery->dsoc, soc_sec, finish_sec, plus_soc, rest);
		DBG("battery->age_adjust_cap = %d\n", battery->age_adjust_cap);
	}
}

static void rk817_bat_display_smooth(struct rk817_battery_device *battery)
{
	/* discharge: reinit "zero & smooth" algorithm to avoid handling dsoc */
	if (battery->s2r && !battery->sleep_chrg_online) {
		DBG("s2r: discharge, reset algorithm...\n");
		battery->s2r = false;
		rk817_bat_zero_algo_prepare(battery);
		rk817_bat_smooth_algo_prepare(battery);
		return;
	}

	if (battery->work_mode == MODE_FINISH) {
		DBG("step1: charge finish...\n");
		rk817_bat_finish_algorithm(battery);

		if ((get_charge_status(battery) != CHARGE_FINISH) &&
		    !rk817_bat_fake_finish_mode(battery)) {
			if ((battery->current_avg < 0) &&
			    (battery->voltage_avg < battery->pdata->zero_algorithm_vol)) {
				DBG("step1: change to zero mode...\n");
				rk817_bat_zero_algo_prepare(battery);
				battery->work_mode = MODE_ZERO;
			} else {
				DBG("step1: change to smooth mode...\n");
				rk817_bat_smooth_algo_prepare(battery);
				battery->work_mode = MODE_SMOOTH;
			}
		}
	} else if (battery->work_mode == MODE_ZERO) {
		DBG("step2: zero algorithm...\n");
		rk817_bat_zero_algorithm(battery);
		if ((battery->voltage_avg >=
		    battery->pdata->zero_algorithm_vol + 50) ||
		    (battery->current_avg >= 0)) {
			DBG("step2: change to smooth mode...\n");
			rk817_bat_smooth_algo_prepare(battery);
			battery->work_mode = MODE_SMOOTH;
		} else if ((get_charge_status(battery) == CHARGE_FINISH) ||
			   rk817_bat_fake_finish_mode(battery)) {
			DBG("step2: change to finish mode...\n");
			rk817_bat_finish_algo_prepare(battery);
			battery->work_mode = MODE_FINISH;
		}
	} else {
		DBG("step3: smooth algorithm...\n");
		rk817_bat_smooth_algorithm(battery);
		if ((battery->current_avg < 0) &&
		    (battery->voltage_avg <
		     battery->pdata->zero_algorithm_vol)) {
			DBG("step3: change to zero mode...\n");
			rk817_bat_zero_algo_prepare(battery);
			battery->work_mode = MODE_ZERO;
		} else if ((get_charge_status(battery) == CHARGE_FINISH) ||
			   rk817_bat_fake_finish_mode(battery)) {
			DBG("step3: change to finish mode...\n");
			rk817_bat_finish_algo_prepare(battery);
			battery->work_mode = MODE_FINISH;
		}
	}
}

static void rk817_bat_output_info(struct rk817_battery_device *battery)
{
	DBG("info start:\n");
	DBG("info: voltage_k = %d\n", battery->voltage_k);
	DBG("info: voltage_b = %d\n", battery->voltage_b);
	DBG("info: voltage = %d\n", battery->voltage_avg);
	DBG("info: voltage_sys = %d\n", battery->voltage_sys);
	DBG("info: current = %d\n", battery->current_avg);

	DBG("info: FCC = %d\n", battery->fcc);
	DBG("info: remain_cap = %d\n", battery->remain_cap);
	DBG("info: sm_remain_cap = %d\n", battery->sm_remain_cap);
	DBG("info: sm_link = %d\n", battery->sm_linek);
	DBG("info: smooth_soc = %d\n", battery->smooth_soc);

	DBG("info: zero_remain_cap = %d\n", battery->zero_remain_cap);
	DBG("info: zero_link = %d\n", battery->zero_linek);
	DBG("info: zero_dsoc = %d\n", battery->zero_dsoc);

	DBG("info: remain_cap = %d\n", battery->remain_cap);
	DBG("info: dsoc = %d, dsoc/1000 = %d\n",
	    battery->dsoc, battery->dsoc / 1000);
	DBG("info: rsoc = %d\n", battery->rsoc);
	DBG("info END.\n");
}

static void rk817_battery_work(struct work_struct *work)
{
	struct rk817_battery_device *battery =
		container_of(work,
			     struct rk817_battery_device,
			     bat_delay_work.work);

	rk817_bat_update_info(battery);
	rk817_bat_lowpwr_check(battery);
	rk817_bat_display_smooth(battery);
	rk817_bat_power_supply_changed(battery);
	rk817_bat_save_data(battery);
	rk817_bat_output_info(battery);

	if (rk817_bat_field_read(battery, CUR_CALIB_UPD)) {
		rk817_bat_current_calibration(battery);
		rk817_bat_init_voltage_kb(battery);
		rk817_bat_field_write(battery, CUR_CALIB_UPD, 0x01);
	}

	queue_delayed_work(battery->bat_monitor_wq, &battery->bat_delay_work,
			   msecs_to_jiffies(battery->monitor_ms));
}

#ifdef CONFIG_OF
static const struct of_device_id rk817_bat_of_match[] = {
	{ .compatible = "rk817,battery", },
	{ },
};
MODULE_DEVICE_TABLE(of, rk817_bat_of_match);
#else
static const struct of_device_id rk817_bat_of_match[] = {
	{ },
};
#endif

static int rk817_battery_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(rk817_bat_of_match, &pdev->dev);
	struct rk817_battery_device *battery;
	struct rk808 *rk817 = dev_get_drvdata(pdev->dev.parent);
	struct i2c_client *client = rk817->i2c;
	int i,  ret;

	if (!of_id) {
		dev_err(&pdev->dev, "Failed to find matching dt id\n");
		return -ENODEV;
	}

	battery = devm_kzalloc(&client->dev, sizeof(*battery), GFP_KERNEL);
	if (!battery)
		return -EINVAL;

	battery->rk817 = rk817;
	battery->client = client;
	battery->dev = &pdev->dev;
	platform_set_drvdata(pdev, battery);

	battery->regmap = rk817->regmap;
	if (IS_ERR(battery->regmap)) {
		dev_err(battery->dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(rk817_battery_reg_fields); i++) {
		const struct reg_field *reg_fields = rk817_battery_reg_fields;

		battery->rmap_fields[i] =
			devm_regmap_field_alloc(battery->dev,
						battery->regmap,
						reg_fields[i]);
		if (IS_ERR(battery->rmap_fields[i])) {
			dev_err(battery->dev, "cannot allocate regmap field\n");
			return PTR_ERR(battery->rmap_fields[i]);
		}
	}

	ret = rk817_bat_parse_dt(battery);
	if (ret < 0) {
		dev_err(battery->dev, "battery parse dt failed!\n");
		return ret;
	}

	rk817_bat_init_info(battery);
	rk817_bat_init_fg(battery);

	rk817_battery_debug_info(battery);

	rk817_bat_update_info(battery);

	rk817_bat_output_info(battery);
	battery->bat_monitor_wq = alloc_ordered_workqueue("%s",
			WQ_MEM_RECLAIM | WQ_FREEZABLE, "rk817-bat-monitor-wq");
	INIT_DELAYED_WORK(&battery->bat_delay_work, rk817_battery_work);
	queue_delayed_work(battery->bat_monitor_wq, &battery->bat_delay_work,
			   msecs_to_jiffies(TIMER_MS_COUNTS * 5));

	ret = rk817_bat_init_power_supply(battery);
	if (ret) {
		dev_err(battery->dev, "rk817 power supply register failed!\n");
		return ret;
	}
	wake_lock_init(&battery->wake_lock, WAKE_LOCK_SUSPEND,
		       "rk817_bat_lock");

	DBG("name: 0x%x", rk817_bat_field_read(battery, CHIP_NAME_H));
	DBG("%x\n", rk817_bat_field_read(battery, CHIP_NAME_L));
	DBG("driver version %s\n", DRIVER_VERSION);

	return 0;
}

static void rk817_battery_shutdown(struct platform_device *dev)
{
}

#ifdef CONFIG_PM_SLEEP
static int  rk817_bat_pm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk817_battery_device *battery = dev_get_drvdata(&pdev->dev);

	cancel_delayed_work_sync(&battery->bat_delay_work);

	battery->s2r = false;
	battery->sleep_chrg_status = get_charge_status(battery);
	battery->current_avg = rk817_bat_get_avg_current(battery);
	if (battery->current_avg > 0 ||
	    (battery->sleep_chrg_status == CC_OR_CV_CHRG) ||
	    (battery->sleep_chrg_status == CHARGE_FINISH))
		battery->sleep_chrg_online = 1;
	else
		battery->sleep_chrg_online = 0;

	battery->remain_cap = rk817_bat_get_capacity_uah(battery);
	battery->rsoc = rk817_bat_get_rsoc(battery);

	do_gettimeofday(&battery->rtc_base);
	rk817_bat_save_data(battery);

	if (battery->sleep_chrg_status != CHARGE_FINISH)
		battery->finish_base = get_boot_sec();

	if ((battery->work_mode == MODE_ZERO) &&
	    (battery->current_avg >= 0)) {
		DBG("suspend: MODE_ZERO exit...\n");
		/* it need't do prepare for mode finish and smooth, it will
		 * be done in display_smooth
		 */
		if (battery->sleep_chrg_status == CHARGE_FINISH) {
			battery->work_mode = MODE_FINISH;
			battery->finish_base = get_boot_sec();
		} else {
			battery->work_mode = MODE_SMOOTH;
			rk817_bat_smooth_algo_prepare(battery);
		}
	}

	DBG("suspend get_boot_sec: %lld\n", get_boot_sec());

	DBG("suspend: dl=%d rl=%d c=%d v=%d cap=%d at=%ld ch=%d\n",
	    battery->dsoc, battery->rsoc, battery->current_avg,
	    rk817_bat_get_battery_voltage(battery),
	    rk817_bat_get_capacity_uah(battery),
	    battery->sleep_dischrg_sec, battery->sleep_chrg_online);
	DBG("battery->sleep_chrg_status=%d\n", battery->sleep_chrg_status);

	return 0;
}

static int rk817_bat_rtc_sleep_sec(struct rk817_battery_device *battery)
{
	int err;
	int interval_sec = 0;
	struct rtc_time tm;
	struct timespec tv = { .tv_nsec = NSEC_PER_SEC >> 1, };
	struct rtc_device *rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);

	err = rtc_read_time(rtc, &tm);
	if (err) {
		dev_err(rtc->dev.parent, "hctosys: read hardware clk failed\n");
		return 0;
	}

	err = rtc_valid_tm(&tm);
	if (err) {
		dev_err(rtc->dev.parent, "hctosys: invalid date time\n");
		return 0;
	}

	rtc_tm_to_time(&tm, &tv.tv_sec);
	interval_sec = tv.tv_sec - battery->rtc_base.tv_sec;

	return (interval_sec > 0) ? interval_sec : 0;
}

static void rk817_bat_relife_age_flag(struct rk817_battery_device *battery)
{
	u8 ocv_soc, ocv_cap, soc_level;

	if (battery->voltage_relax <= 0)
		return;

	ocv_soc = rk817_bat_vol_to_soc(battery, battery->voltage_relax);
	ocv_cap = rk817_bat_vol_to_cap(battery, battery->voltage_relax);
	DBG("<%s>. ocv_soc=%d, min=%lu, vol=%d\n", __func__,
	    ocv_soc, battery->sleep_dischrg_sec / 60, battery->voltage_relax);

	/* sleep enough time and ocv_soc enough low */
	if (!battery->age_allow_update && ocv_soc <= 10) {
		battery->age_voltage = battery->voltage_relax;
		battery->age_ocv_cap = ocv_cap;
		battery->age_ocv_soc = ocv_soc;
		battery->age_adjust_cap = 0;

		if (ocv_soc <= 1)
			battery->age_level = 100;
		else if (ocv_soc < 5)
			battery->age_level = 90;
		else
			battery->age_level = 80;

		/*soc_level = rk818_bat_get_age_level(battery);*/
		soc_level = 0;
		if (soc_level > battery->age_level) {
			battery->age_allow_update = false;
		} else {
			battery->age_allow_update = true;
			battery->age_keep_sec = get_boot_sec();
		}

		BAT_INFO("resume: age_vol:%d, age_ocv_cap:%d, age_ocv_soc:%d, "
			 "soc_level:%d, age_allow_update:%d, "
			 "age_level:%d\n",
			 battery->age_voltage, battery->age_ocv_cap,
			 ocv_soc, soc_level,
			 battery->age_allow_update, battery->age_level);
	}
}

static void rk817_bat_init_capacity(struct rk817_battery_device *battery,
				    u32 cap)
{
	int delta_cap;

	delta_cap = cap - battery->remain_cap;
	if (!delta_cap)
		return;

	battery->age_adjust_cap += delta_cap;
	rk817_bat_init_coulomb_cap(battery, cap);
	rk817_bat_smooth_algo_prepare(battery);
	rk817_bat_zero_algo_prepare(battery);
}

static void rk817_bat_relax_vol_calib(struct rk817_battery_device *battery)
{
	int soc, cap, vol;

	vol = battery->voltage_relax;
	soc = rk817_bat_vol_to_soc(battery, vol);
	cap = rk817_bat_vol_to_cap(battery, vol);
	rk817_bat_init_capacity(battery, cap);
	BAT_INFO("sleep ocv calib: rsoc=%d, cap=%d\n", soc, cap);
}

static int rk817_bat_sleep_dischrg(struct rk817_battery_device *battery)
{
	bool ocv_soc_updated = false;
	int tgt_dsoc, gap_soc, sleep_soc = 0;
	int pwroff_vol = battery->pdata->pwroff_vol;
	unsigned long sleep_sec = battery->sleep_dischrg_sec;

	DBG("<%s>. enter: dsoc=%d, rsoc=%d, rv=%d, v=%d, sleep_min=%lu\n",
	    __func__, battery->dsoc, battery->rsoc, battery->voltage_relax,
	    battery->voltage_avg, sleep_sec / 60);

	if (battery->voltage_relax >= battery->voltage_avg) {
		rk817_bat_relax_vol_calib(battery);
		rk817_bat_restart_relax(battery);
		rk817_bat_relife_age_flag(battery);
		ocv_soc_updated = true;
	}

	/* handle dsoc */
	if (battery->dsoc <= battery->rsoc) {
		battery->sleep_sum_cap = (SLP_CURR_MIN * sleep_sec / 3600);
		sleep_soc = battery->sleep_sum_cap * 100 / DIV(battery->fcc);
		tgt_dsoc = battery->dsoc - sleep_soc * 1000;
		if (sleep_soc > 0) {
			BAT_INFO("calib0: rl=%d, dl=%d, intval=%d\n",
				 battery->rsoc, battery->dsoc, sleep_soc);
			if (battery->dsoc / 1000 < 5) {
				battery->dsoc -= 1000;
			} else if ((tgt_dsoc / 1000 < 5) &&
				   (battery->dsoc  / 1000 >= 5)) {
				if (battery->dsoc / 1000 == 5)
					battery->dsoc -= 1000;
				else
					battery->dsoc = 5 * 1000;
			} else if (tgt_dsoc / 1000 > 5) {
				battery->dsoc = tgt_dsoc;
			}
		}

		DBG("%s: dsoc<=rsoc, sum_cap=%d==>sleep_soc=%d, tgt_dsoc=%d\n",
		    __func__, battery->sleep_sum_cap, sleep_soc, tgt_dsoc);
	} else {
		/* di->dsoc > di->rsoc */
		battery->sleep_sum_cap = (SLP_CURR_MAX * sleep_sec / 3600);
		sleep_soc = battery->sleep_sum_cap / DIV(battery->fcc / 100);
		gap_soc = battery->dsoc - battery->rsoc;

		DBG("calib1: rsoc=%d, dsoc=%d, intval=%d\n",
		    battery->rsoc, battery->dsoc, sleep_soc);
		if (gap_soc > sleep_soc) {
			if ((gap_soc - 5000) > (sleep_soc * 2 * 1000))
				battery->dsoc -= (sleep_soc * 2 * 1000);
			else
				battery->dsoc -= sleep_soc * 1000;
		} else {
			battery->dsoc = battery->rsoc;
		}

		DBG("%s: dsoc>rsoc, sum_cap=%d=>sleep_soc=%d, gap_soc=%d\n",
		    __func__, battery->sleep_sum_cap, sleep_soc, gap_soc);
	}

	if (battery->voltage_avg <= pwroff_vol - 70) {
		battery->dsoc = 0;
		DBG("low power sleeping, shutdown... %d\n", battery->dsoc);
	}

	if (ocv_soc_updated && sleep_soc &&
	    (battery->rsoc - battery->dsoc) < 5000 &&
	    battery->dsoc < 40 * 1000) {
		battery->dsoc -= 1000;
		DBG("low power sleeping, reserved... %d\n", battery->dsoc);
	}

	if (battery->dsoc <= 0) {
		battery->dsoc = 0;
		DBG("sleep dsoc is %d...\n", battery->dsoc);
	}

	DBG("<%s>. out: dsoc=%d, rsoc=%d, sum_cap=%d\n",
	    __func__, battery->dsoc, battery->rsoc, battery->sleep_sum_cap);

	return sleep_soc;
}

static int rk817_bat_pm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk817_battery_device *battery = dev_get_drvdata(&pdev->dev);
	int interval_sec = 0, time_step, pwroff_vol;

	battery->s2r = true;
	battery->current_avg = rk817_bat_get_avg_current(battery);
	battery->voltage_relax = rk817_bat_get_relax_voltage(battery);
	battery->voltage_avg = rk817_bat_get_battery_voltage(battery);
	battery->remain_cap = rk817_bat_get_capacity_uah(battery);
	battery->rsoc = rk817_bat_get_rsoc(battery);
	interval_sec = rk817_bat_rtc_sleep_sec(battery);
	battery->sleep_sum_sec += interval_sec;
	pwroff_vol = battery->pdata->pwroff_vol;

	if (!battery->sleep_chrg_online) {
		/* only add up discharge sleep seconds */
		battery->sleep_dischrg_sec += interval_sec;
		if (battery->voltage_avg <= pwroff_vol + 50)
			time_step = DISCHRG_TIME_STEP1;
		else
			time_step = DISCHRG_TIME_STEP2;
	}

	DBG("resume: dl=%d rl=%d c=%d v=%d rv=%d "
	    "cap=%d dt=%d at=%ld ch=%d, sec = %d\n",
	    battery->dsoc, battery->rsoc, battery->current_avg,
	    battery->voltage_avg, battery->voltage_relax,
	    rk817_bat_get_capacity_uah(battery), interval_sec,
	    battery->sleep_dischrg_sec, battery->sleep_chrg_online,
	    interval_sec);

	/* sleep: enough time and discharge */
	if ((!battery->sleep_chrg_online) &&
	    (battery->sleep_dischrg_sec > time_step)) {
		if (rk817_bat_sleep_dischrg(battery))
			battery->sleep_dischrg_sec = 0;
	}

	rk817_bat_save_data(battery);

	/* charge/lowpower lock: for battery work to update dsoc and rsoc */
	if ((battery->sleep_chrg_online) ||
	    (!battery->sleep_chrg_online &&
	    battery->voltage_avg < battery->pdata->pwroff_vol))
		wake_lock_timeout(&battery->wake_lock, msecs_to_jiffies(2000));

	queue_delayed_work(battery->bat_monitor_wq, &battery->bat_delay_work,
			   msecs_to_jiffies(1000));

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(rk817_bat_pm_ops,
			 rk817_bat_pm_suspend,
			 rk817_bat_pm_resume);

static struct platform_driver rk817_battery_driver = {
	.probe = rk817_battery_probe,
	.shutdown = rk817_battery_shutdown,
	.driver = {
		.name = "rk817-battery",
		.pm = &rk817_bat_pm_ops,
		.of_match_table = of_match_ptr(rk817_bat_of_match),
	},
};

static int __init rk817_battery_init(void)
{
	return platform_driver_register(&rk817_battery_driver);
}
fs_initcall_sync(rk817_battery_init);

static void __exit rk817_battery_exit(void)
{
	platform_driver_unregister(&rk817_battery_driver);
}
module_exit(rk817_battery_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("shengfeixu <xsf@rock-chips.com>");
MODULE_DESCRIPTION("rk817 battery Charger Driver");

