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

static const struct ab8500_v_to_cap cap_tbl[] = {
	{4186,	100},
	{4163,	 99},
	{4114,	 95},
	{4068,	 90},
	{3990,	 80},
	{3926,	 70},
	{3898,	 65},
	{3866,	 60},
	{3833,	 55},
	{3812,	 50},
	{3787,	 40},
	{3768,	 30},
	{3747,	 25},
	{3730,	 20},
	{3705,	 15},
	{3699,	 14},
	{3684,	 12},
	{3672,	  9},
	{3657,	  7},
	{3638,	  6},
	{3556,	  4},
	{3424,	  2},
	{3317,	  1},
	{3094,	  0},
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
 * temperature values to work.
 */
static const struct batres_vs_temp temp_to_batres_tbl_thermistor[] = {
	{ 40, 120},
	{ 30, 135},
	{ 20, 165},
	{ 10, 230},
	{ 00, 325},
	{-10, 445},
	{-20, 595},
};

/* Default battery type for reference designs is the unknown type */
static struct ab8500_battery_type bat_type_thermistor_unknown = {
	.resis_high = 0,
	.resis_low = 0,
	.battery_resistance = 300,
	.nominal_voltage = 3700,
	.termination_vol = 4050,
	.termination_curr = 200,
	.recharge_cap = 95,
	.normal_cur_lvl = 400,
	.normal_vol_lvl = 4100,
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
	.n_v_cap_tbl_elements = ARRAY_SIZE(cap_tbl),
	.v_to_cap_tbl = cap_tbl,
	.n_batres_tbl_elements = ARRAY_SIZE(temp_to_batres_tbl_thermistor),
	.batres_tbl = temp_to_batres_tbl_thermistor,
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
	.high_curr_threshold = 50,
	.lowbat_threshold = 3100,
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
	.chg_curr = 910,
	.wait_cycles = 10,
	.charger_curr_step = 100,
};

static const struct ab8500_bm_charger_parameters chg = {
	.usb_volt_max		= 5500,
	.usb_curr_max		= 1500,
	.ac_volt_max		= 7500,
	.ac_curr_max		= 1500,
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
	struct power_supply_battery_info *bi = &bm->bi;
	struct device *dev = &psy->dev;
	int ret;

	ret = power_supply_get_battery_info(psy, bi);
	if (ret) {
		dev_err(dev, "cannot retrieve battery info\n");
		return ret;
	}

	/* Fill in defaults for any data missing from the device tree */
	if (bi->charge_full_design_uah < 0)
		/* The default capacity is 612 mAh for unknown batteries */
		bi->charge_full_design_uah = 612000;
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
	power_supply_put_battery_info(psy, &bm->bi);
}
