// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/power_supply.h>
#include <linux/of.h>

#include "ab8500-bm.h"

/* Default: under this temperature, charging is stopped */
#define AB8500_TEMP_UNDER	3
/* Default: between this temp and AB8500_TEMP_UNDER charging is reduced */
#define AB8500_TEMP_LOW		8
/* Default: between this temp and AB8500_TEMP_OVER charging is reduced */
#define AB8500_TEMP_HIGH	43
/* Default: over this temp, charging is stopped */
#define AB8500_TEMP_OVER	48
/* Default: temperature hysteresis */
#define AB8500_TEMP_HYSTERESIS	3

static struct power_supply_battery_ocv_table ocv_cap_tbl[] = {
	{ .ocv = 4186000, .capacity = 100},
	{ .ocv = 4163000, .capacity = 99},
	{ .ocv = 4114000, .capacity = 95},
	{ .ocv = 4068000, .capacity = 90},
	{ .ocv = 3990000, .capacity = 80},
	{ .ocv = 3926000, .capacity = 70},
	{ .ocv = 3898000, .capacity = 65},
	{ .ocv = 3866000, .capacity = 60},
	{ .ocv = 3833000, .capacity = 55},
	{ .ocv = 3812000, .capacity = 50},
	{ .ocv = 3787000, .capacity = 40},
	{ .ocv = 3768000, .capacity = 30},
	{ .ocv = 3747000, .capacity = 25},
	{ .ocv = 3730000, .capacity = 20},
	{ .ocv = 3705000, .capacity = 15},
	{ .ocv = 3699000, .capacity = 14},
	{ .ocv = 3684000, .capacity = 12},
	{ .ocv = 3672000, .capacity = 9},
	{ .ocv = 3657000, .capacity = 7},
	{ .ocv = 3638000, .capacity = 6},
	{ .ocv = 3556000, .capacity = 4},
	{ .ocv = 3424000, .capacity = 2},
	{ .ocv = 3317000, .capacity = 1},
	{ .ocv = 3094000, .capacity = 0},
};

/*
 * Note that the res_to_temp table must be strictly sorted by falling
 * resistance values to work.
 */
static const struct ab8500_res_to_temp temp_tbl[] = {
	{-5, 214834},
	{ 0, 162943},
	{ 5, 124820},
	{10,  96520},
	{15,  75306},
	{20,  59254},
	{25,  47000},
	{30,  37566},
	{35,  30245},
	{40,  24520},
	{45,  20010},
	{50,  16432},
	{55,  13576},
	{60,  11280},
	{65,   9425},
};

/*
 * Note that the batres_vs_temp table must be strictly sorted by falling
 * temperature values to work. Factory resistance is 300 mOhm and the
 * resistance values to the right are percentages of 300 mOhm.
 */
static struct power_supply_resistance_temp_table temp_to_batres_tbl_thermistor[] = {
	{ .temp = 40, .resistance = 40 /* 120 mOhm */ },
	{ .temp = 30, .resistance = 45 /* 135 mOhm */ },
	{ .temp = 20, .resistance = 55 /* 165 mOhm */ },
	{ .temp = 10, .resistance = 77 /* 230 mOhm */ },
	{ .temp = 00, .resistance = 108 /* 325 mOhm */ },
	{ .temp = -10, .resistance = 158 /* 445 mOhm */ },
	{ .temp = -20, .resistance = 198 /* 595 mOhm */ },
};

/* Default battery type for reference designs is the unknown type */
static struct ab8500_battery_type bat_type_thermistor_unknown = {
	.resis_high = 0,
	.resis_low = 0,
	.maint_a_cur_lvl = 400,
	.maint_a_vol_lvl = 4050,
	.maint_a_chg_timer_h = 60,
	.maint_b_cur_lvl = 400,
	.maint_b_vol_lvl = 4000,
	.maint_b_chg_timer_h = 200,
	.low_high_cur_lvl = 300,
	.low_high_vol_lvl = 4000,
	.n_temp_tbl_elements = ARRAY_SIZE(temp_tbl),
	.r_to_t_tbl = temp_tbl,
};

static const struct ab8500_bm_capacity_levels cap_levels = {
	.critical	= 2,
	.low		= 10,
	.normal		= 70,
	.high		= 95,
	.full		= 100,
};

static const struct ab8500_fg_parameters fg = {
	.recovery_sleep_timer = 10,
	.recovery_total_time = 100,
	.init_timer = 1,
	.init_discard_time = 5,
	.init_total_time = 40,
	.high_curr_time = 60,
	.accu_charging = 30,
	.accu_high_curr = 30,
	.high_curr_threshold_ua = 50000,
	.lowbat_threshold_uv = 3100000,
	.battok_falling_th_sel0 = 2860,
	.battok_raising_th_sel1 = 2860,
	.maint_thres = 95,
	.user_cap_limit = 15,
	.pcut_enable = 1,
	.pcut_max_time = 127,
	.pcut_flag_time = 112,
	.pcut_max_restart = 15,
	.pcut_debounce_time = 2,
};

static const struct ab8500_maxim_parameters ab8500_maxi_params = {
	.ena_maxi = true,
	.chg_curr_ua = 910000,
	.wait_cycles = 10,
	.charger_curr_step_ua = 100000,
};

static const struct ab8500_bm_charger_parameters chg = {
	.usb_volt_max_uv	= 5500000,
	.usb_curr_max_ua	= 1500000,
	.ac_volt_max_uv		= 7500000,
	.ac_curr_max_ua		= 1500000,
};

/* This is referenced directly in the charger code */
struct ab8500_bm_data ab8500_bm_data = {
	.main_safety_tmr_h      = 4,
	.temp_interval_chg      = 20,
	.temp_interval_nochg    = 120,
	.usb_safety_tmr_h       = 4,
	.bkup_bat_v             = BUP_VCH_SEL_2P6V,
	.bkup_bat_i             = BUP_ICH_SEL_150UA,
	.no_maintenance         = false,
	.capacity_scaling       = false,
	.adc_therm              = AB8500_ADC_THERM_BATCTRL,
	.chg_unknown_bat        = false,
	.enable_overshoot       = false,
	.fg_res                 = 100,
	.cap_levels             = &cap_levels,
	.bat_type               = &bat_type_thermistor_unknown,
	.interval_charging      = 5,
	.interval_not_charging  = 120,
	.gnd_lift_resistance    = 34,
	.maxi                   = &ab8500_maxi_params,
	.chg_params             = &chg,
	.fg_params              = &fg,
};

int ab8500_bm_of_probe(struct power_supply *psy,
		       struct ab8500_bm_data *bm)
{
	struct power_supply_battery_info *bi;
	struct device *dev = &psy->dev;
	int ret;

	ret = power_supply_get_battery_info(psy, &bm->bi);
	if (ret) {
		dev_err(dev, "cannot retrieve battery info\n");
		return ret;
	}
	bi = bm->bi;

	/* Fill in defaults for any data missing from the device tree */
	if (bi->charge_full_design_uah < 0)
		/* The default capacity is 612 mAh for unknown batteries */
		bi->charge_full_design_uah = 612000;

	/*
	 * All of these voltages need to be specified or we will simply
	 * fall back to safe defaults.
	 */
	if ((bi->voltage_min_design_uv < 0) ||
	    (bi->voltage_max_design_uv < 0) ||
	    (bi->overvoltage_limit_uv < 0)) {
		/* Nominal voltage is 3.7V for unknown batteries */
		bi->voltage_min_design_uv = 3700000;
		bi->voltage_max_design_uv = 3700000;
		/* Termination voltage (overcharge limit) 4.05V */
		bi->overvoltage_limit_uv = 4050000;
	}

	if (bi->constant_charge_current_max_ua < 0)
		bi->constant_charge_current_max_ua = 400000;

	if (bi->constant_charge_voltage_max_uv < 0)
		bi->constant_charge_voltage_max_uv = 4100000;

	if (bi->charge_term_current_ua)
		/* Charging stops when we drop below this current */
		bi->charge_term_current_ua = 200000;

	/*
	 * Internal resistance and factory resistance are tightly coupled
	 * so both MUST be defined or we fall back to defaults.
	 */
	if ((bi->factory_internal_resistance_uohm < 0) ||
	    !bi->resist_table) {
		bi->factory_internal_resistance_uohm = 300000;
		bi->resist_table = temp_to_batres_tbl_thermistor;
		bi->resist_table_size = ARRAY_SIZE(temp_to_batres_tbl_thermistor);
	}

	if (!bi->ocv_table[0]) {
		/* Default capacity table at say 25 degrees Celsius */
		bi->ocv_temp[0] = 25;
		bi->ocv_table[0] = ocv_cap_tbl;
		bi->ocv_table_size[0] = ARRAY_SIZE(ocv_cap_tbl);
	}

	if (bi->temp_min == INT_MIN)
		bi->temp_min = AB8500_TEMP_UNDER;
	if (bi->temp_max == INT_MAX)
		bi->temp_max = AB8500_TEMP_OVER;
	if (bi->temp_alert_min == INT_MIN)
		bi->temp_alert_min = AB8500_TEMP_LOW;
	if (bi->temp_alert_max == INT_MAX)
		bi->temp_alert_max = AB8500_TEMP_HIGH;
	bm->temp_hysteresis = AB8500_TEMP_HYSTERESIS;

	return 0;
}

void ab8500_bm_of_remove(struct power_supply *psy,
			 struct ab8500_bm_data *bm)
{
	power_supply_put_battery_info(psy, bm->bi);
}
