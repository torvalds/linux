/*
 * rk816_battery.h: fuel gauge driver structures
 *
 * Copyright (C) 2016 Rockchip Electronics Co., Ltd
 * Author: chenjh <chenjh@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef RK816_BATTERY
#define RK816_BATTERY

/*TS_CTRL_REG*/
#define GG_EN			BIT(7)
#define ADC_CUR_EN		BIT(6)
#define ADC_TS1_EN		BIT(5)
#define ADC_TS2_EN		BIT(4)

/*GGCON*/
#define ADC_CUR_MODE		(0x01 << 1)
#define AVG_CUR_MODE		(0x00 << 0)
#define ADC_CAL_MIN_MSK		0x30
#define ADC_CAL_8MIN		(0x00 << 4)
#define OCV_SAMP_MIN_MSK	0x0c
#define OCV_SAMP_8MIN		(0x00 << 2)

/*GGSTS*/
#define FCC_LOCK		(1 << 5)
#define BAT_CON			(1 << 4)
#define RELAX_STS		(1 << 1)
#define RELAX_VOL1_UPD		BIT(3)
#define RELAX_VOL2_UPD		BIT(2)
#define RELAX_VOL12_UPD_MSK	(RELAX_VOL1_UPD | RELAX_VOL2_UPD)

/*SUP_STS_REG*/
#define BAT_EXS			(1 << 7)
#define CHARGE_OFF		(0x00 << 4)
#define DEAD_CHARGE		(0x01 << 4)
#define TRICKLE_CHARGE		(0x02 << 4)
#define CC_OR_CV		(0x03 << 4)
#define CHARGE_FINISH		(0x04 << 4)
#define USB_OVER_VOL		(0x05 << 4)
#define BAT_TMP_ERR		(0x06 << 4)
#define TIMER_ERR		(0x07 << 4)
#define USB_EXIST		(1 << 1)
#define USB_EFF			(1 << 0)
#define USB_VLIMIT_EN		BIT(3)
#define USB_CLIMIT_EN		BIT(2)
#define CHRG_STATUS_MSK		0x70

/*USB_CTRL_REG*/
#define CHRG_CT_EN		(1 << 7)
#define INPUT_CUR_MSK		(0x0f)
#define FINISH_CUR_MSK		0xc0

/* BAT_CTRL_REG */
#define USB_SYS_EN		BIT(6)

/* THERMAL_REG */
#define FB_TEMP_MSK		0x0c
#define HOTDIE_STS		BIT(1)

/*CHGR_CUR_INPUT*/
#define INPUT_CUR450MA		(0x00)
#define INPUT_CUR800MA		(0x01)
#define INPUT_CUR850MA		(0x02)
#define INPUT_CUR1000MA		(0x03)
#define INPUT_CUR1250MA		(0x04)
#define INPUT_CUR1500MA		(0x05)
#define INPUT_CUR1750MA		(0x06)
#define INPUT_CUR2000MA		(0x07)
#define INPUT_CUR2250MA		(0x08)
#define INPUT_CUR2500MA		(0x09)
#define INPUT_CUR2750MA		(0x0A)
#define INPUT_CUR3000MA		(0x0B)

/*CHRG_VOL_SEL*/
#define CHRG_VOL4050MV		(0x00 << 4)
#define CHRG_VOL4100MV		(0x01 << 4)
#define CHRG_VOL4150MV		(0x02 << 4)
#define CHRG_VOL4200MV		(0x03 << 4)
#define CHRG_VOL4300MV		(0x04 << 4)
#define CHRG_VOL4350MV		(0x05 << 4)

/*CHRG_CUR_SEL*/
#define CHRG_CUR1000MA		(0x00)
#define CHRG_CUR1200MA		(0x01)
#define CHRG_CUR1400MA		(0x02)
#define CHRG_CUR1600MA		(0x03)
#define CHRG_CUR1800MA		(0x04)
#define CHRG_CUR2000MA		(0x05)
#define CHRG_CUR2200MA		(0x06)
#define CHRG_CUR2400MA		(0x07)
#define CHRG_CUR2600MA		(0x08)
#define CHRG_CUR2800MA		(0x09)
#define CHRG_CUR3000MA		(0x0A)

/*THREAML_REG*/
#define TEMP_85C		(0x00 << 2)
#define TEMP_95C		(0x01 << 2)
#define TEMP_105C		(0x02 << 2)
#define TEMP_115C		(0x03 << 2)

/*CHRG_CTRL_REG2*/
#define CHG_CCCV_4HOUR		(0x00)
#define CHG_CCCV_5HOUR		(0x01)
#define CHG_CCCV_6HOUR		(0x02)
#define CHG_CCCV_8HOUR		(0x03)
#define CHG_CCCV_10HOUR		(0x04)
#define CHG_CCCV_12HOUR		(0x05)
#define CHG_CCCV_14HOUR		(0x06)
#define CHG_CCCV_16HOUR		(0x07)
#define FINISH_100MA		(0x00 << 6)
#define FINISH_150MA		(0x01 << 6)
#define FINISH_200MA		(0x02 << 6)
#define FINISH_250MA		(0x03 << 6)

/*CHRG_CTRL_REG3*/
#define CHRG_TERM_ANA_SIGNAL	(0 << 5)
#define CHRG_TERM_DIG_SIGNAL	(1 << 5)
#define CHRG_TIMER_CCCV_EN	(1 << 2)
#define CHRG_EN			(1 << 7)

#define BOOST_OTG_MASK		((0x3 << 5) | (0x3 << 1))
#define BOOST_OTG_ON		((0x3 << 5) | (0x3 << 1))
#define BOOST_OTG_OFF		((0x3 << 5) | (0x0 << 1))
#define BOOST_OFF_OTG_ON	((0x3 << 5) | (0x2 << 1))

#define OTG_BOOST_SLP_ON	(0x3 << 5)

/* MISC_MARK_REG */
#define FG_INIT			BIT(3)
#define FG_RESET_LATE		BIT(1)
#define FG_RESET_NOW		BIT(0)
#define ALGO_REST_MODE_MSK	(0xc0)
#define ALGO_REST_MODE_SHIFT	6

#define FB_TEMP_SHIFT		2
#define CHRG_VOL_SEL_SHIFT	4
#define CHRG_CRU_INPUT_SHIFT	0
#define CHRG_CRU_SEL_SHIFT	0
#define CHRG_CCCV_HOUR_SHIFT	0
#define	OCV_CALIB_SHIFT		(1 << 1)
#define PLUG_IN_STS		(1 << 6)

#define DRIVER_VERSION		"1.2"
#define TIMER_MS_COUNTS		1000
#define MAX_PERCENTAGE		100
#define MAX_INT			0x7FFF
#define MAX_INTERPOLATE		1000

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
	int otg5v_suspend_enable;
	u32 sample_res;
	bool extcon;
};

enum work_mode {
	MODE_ZERO = 0,
	MODE_FINISH,
	MODE_SMOOTH_CHRG,
	MODE_SMOOTH_DISCHRG,
	MODE_SMOOTH,
};

enum bat_mode {
	MODE_BATTARY = 0,
	MODE_VIRTUAL,
};

enum charger_t {
	USB_TYPE_UNKNOWN_CHARGER,
	USB_TYPE_NONE_CHARGER,
	USB_TYPE_USB_CHARGER,
	USB_TYPE_AC_CHARGER,
	USB_TYPE_CDP_CHARGER,
	DC_TYPE_DC_CHARGER,
	DC_TYPE_NONE_CHARGER,
};

enum charger_state {
	OFFLINE = 0,
	ONLINE
};

static const u16 FEED_BACK_TEMP[] = {
	85, 95, 105, 115
};

static const u16 CHRG_VOL_SEL[] = {
	4050, 4100, 4150, 4200, 4250, 4300, 4350
};

/*
 * If sample resistor changes, we need caculate a new CHRG_CUR_SEL[] table.
 *
 * Calculation method:
 * 1. find 20mR(default) current charge table, that is:
 *	20mR: [1000, 1200, 1400, 1600, 1800, 2000, 2250, 2400]
 *
 * 2. caculate Rfac(not care much, just using it) by sample resistor(ie. Rsam);
 *	Rsam = 20mR: Rfac = 10;
 *	Rsam > 20mR: Rfac = Rsam * 10 / 20;
 *	Rsam < 20mR: Rfac = 20 * 10 / Rsam;
 *
 * 3. from step2, we get Rfac, then we can get new charge current table by 20mR
 *    charge table:
 *	Iorg: member from 20mR charge table; Inew: new member for charge table.
 *
 *	Rsam > 20mR: Inew = Iorg * 10 / Rfac;
 *	Rsam < 20mR: Inew = Iorg * Rfac / 10;
 *
 * Notice: Inew should round up if it is not a integer!!!
 *
 * Example:
 *	10mR: [2000, 2400, 2800, 3200, 3600, 4000, 4500, 4800]
 *	20mR: [1000, 1200, 1400, 1600, 1800, 2000, 2250, 2400]
 *	40mR: [500,  600,  700,  800,  900,  1000, 1125, 1200]
 *	50mR: [400,  480,  560,  640,  720,  800,  900,  960]
 *	60mR: [334,  400,  467,  534,  600,  667,  750,  800]
 *
 * You should add property 'sample_res = <Rsam>' at battery node.
 */

static const u16 CHRG_CUR_SEL[] = {
	1000, 1200, 1400, 1600, 1800, 2000, 2250, 2400
};

static const u16 CHRG_CUR_INPUT[] = {
	450, 80, 850, 1000, 1250, 1500, 1750, 2000
};

void kernel_power_off(void);

#endif
