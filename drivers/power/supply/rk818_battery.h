/*
 * rk818_battery.h: fuel gauge driver structures
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

#ifndef RK818_BATTERY
#define RK818_BATTERY

/* RK818_INT_STS_MSK_REG2 */
#define PLUG_IN_MSK		BIT(0)
#define PLUG_OUT_MSK		BIT(1)
#define CHRG_CVTLMT_INT_MSK	BIT(6)

/* RK818_TS_CTRL_REG */
#define GG_EN			BIT(7)
#define ADC_CUR_EN		BIT(6)
#define ADC_TS1_EN		BIT(5)
#define ADC_TS2_EN		BIT(4)
#define TS1_CUR_MSK		0x03

/* RK818_GGCON */
#define OCV_SAMP_MIN_MSK	0x0c
#define OCV_SAMP_8MIN		(0x00 << 2)

#define ADC_CAL_MIN_MSK		0x30
#define ADC_CAL_8MIN		(0x00 << 4)
#define ADC_CUR_MODE		BIT(1)

/* RK818_GGSTS */
#define BAT_CON			BIT(4)
#define RELAX_VOL1_UPD		BIT(3)
#define RELAX_VOL2_UPD		BIT(2)
#define RELAX_VOL12_UPD_MSK	(RELAX_VOL1_UPD | RELAX_VOL2_UPD)

/* RK818_SUP_STS_REG */
#define CHRG_STATUS_MSK		0x70
#define BAT_EXS			BIT(7)
#define CHARGE_OFF		(0x0 << 4)
#define DEAD_CHARGE		(0x1 << 4)
#define TRICKLE_CHARGE		(0x2 << 4)
#define CC_OR_CV		(0x3 << 4)
#define CHARGE_FINISH		(0x4 << 4)
#define USB_OVER_VOL		(0x5 << 4)
#define BAT_TMP_ERR		(0x6 << 4)
#define TIMER_ERR		(0x7 << 4)
#define USB_VLIMIT_EN		BIT(3)
#define USB_CLIMIT_EN		BIT(2)
#define USB_EXIST		BIT(1)
#define USB_EFF			BIT(0)

/* RK818_USB_CTRL_REG */
#define CHRG_CT_EN		BIT(7)
#define FINISH_CUR_MSK		0xc0
#define TEMP_105C		(0x02 << 2)
#define FINISH_100MA		(0x00 << 6)
#define FINISH_150MA		(0x01 << 6)
#define FINISH_200MA		(0x02 << 6)
#define FINISH_250MA		(0x03 << 6)

/* RK818_CHRG_CTRL_REG3 */
#define CHRG_TERM_MODE_MSK	BIT(5)
#define CHRG_TERM_ANA_SIGNAL	(0 << 5)
#define CHRG_TERM_DIG_SIGNAL	BIT(5)
#define CHRG_TIMER_CCCV_EN	BIT(2)
#define CHRG_EN			BIT(7)

/* RK818_VB_MON_REG */
#define	RK818_VBAT_LOW_3V0      0x02
#define	RK818_VBAT_LOW_3V4      0x06
#define PLUG_IN_STS		BIT(6)

/* RK818_THERMAL_REG */
#define FB_TEMP_MSK		0x0c
#define HOTDIE_STS		BIT(1)

/* RK818_INT_STS_MSK_REG1 */
#define VB_LOW_INT_EN		BIT(1)

/* RK818_MISC_MARK_REG */
#define FG_INIT			BIT(5)
#define FG_RESET_LATE		BIT(4)
#define FG_RESET_NOW		BIT(3)
#define ALGO_REST_MODE_MSK	(0xc0)
#define ALGO_REST_MODE_SHIFT	6

/* bit shift */
#define FB_TEMP_SHIFT		2

/* parse ocv table param */
#define TIMER_MS_COUNTS		1000
#define MAX_PERCENTAGE		100
#define MAX_INTERPOLATE		1000
#define MAX_INT			0x7FFF

#define DRIVER_VERSION		"7.1"

struct battery_platform_data {
	u32 *ocv_table;
	u32 *zero_table;
	u32 *ntc_table;
	u32 ocv_size;
	u32 max_chrg_voltage;
	u32 ntc_size;
	int ntc_degree_from;
	u32 pwroff_vol;
	u32 monitor_sec;
	u32 zero_algorithm_vol;
	u32 zero_reserve_dsoc;
	u32 bat_res;
	u32 design_capacity;
	u32 design_qmax;
	u32 sleep_enter_current;
	u32 sleep_exit_current;
	u32 max_soc_offset;
	u32 sample_res;
	u32 bat_mode;
	u32 fb_temp;
	u32 energy_mode;
	u32 cccv_hour;
	u32 ntc_uA;
	u32 ntc_factor;
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

static const u16 feedback_temp_array[] = {
	85, 95, 105, 115
};

static const u16 chrg_vol_sel_array[] = {
	4050, 4100, 4150, 4200, 4250, 4300, 4350
};

static const u16 chrg_cur_sel_array[] = {
	1000, 1200, 1400, 1600, 1800, 2000, 2250, 2400, 2600, 2800, 3000
};

static const u16 chrg_cur_input_array[] = {
	450, 80, 850, 1000, 1250, 1500, 1750, 2000, 2250, 2500, 2750, 3000
};

void kernel_power_off(void);
int rk818_bat_temp_notifier_register(struct notifier_block *nb);
int rk818_bat_temp_notifier_unregister(struct notifier_block *nb);

#endif
