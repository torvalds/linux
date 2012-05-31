#include <linux/export.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/abx500/ab8500-bm.h>

/*
 * These are the defined batteries that uses a NTC and ID resistor placed
 * inside of the battery pack.
 * Note that the res_to_temp table must be strictly sorted by falling resistance
 * values to work.
 */
static struct abx500_res_to_temp temp_tbl_A_thermistor[] = {
	{-5, 53407},
	{ 0, 48594},
	{ 5, 43804},
	{10, 39188},
	{15, 34870},
	{20, 30933},
	{25, 27422},
	{30, 24347},
	{35, 21694},
	{40, 19431},
	{45, 17517},
	{50, 15908},
	{55, 14561},
	{60, 13437},
	{65, 12500},
};

static struct abx500_res_to_temp temp_tbl_B_thermistor[] = {
	{-5, 200000},
	{ 0, 159024},
	{ 5, 151921},
	{10, 144300},
	{15, 136424},
	{20, 128565},
	{25, 120978},
	{30, 113875},
	{35, 107397},
	{40, 101629},
	{45,  96592},
	{50,  92253},
	{55,  88569},
	{60,  85461},
	{65,  82869},
};

static struct abx500_v_to_cap cap_tbl_A_thermistor[] = {
	{4171,	100},
	{4114,	 95},
	{4009,	 83},
	{3947,	 74},
	{3907,	 67},
	{3863,	 59},
	{3830,	 56},
	{3813,	 53},
	{3791,	 46},
	{3771,	 33},
	{3754,	 25},
	{3735,	 20},
	{3717,	 17},
	{3681,	 13},
	{3664,	  8},
	{3651,	  6},
	{3635,	  5},
	{3560,	  3},
	{3408,    1},
	{3247,	  0},
};

static struct abx500_v_to_cap cap_tbl_B_thermistor[] = {
	{4161,	100},
	{4124,	 98},
	{4044,	 90},
	{4003,	 85},
	{3966,	 80},
	{3933,	 75},
	{3888,	 67},
	{3849,	 60},
	{3813,	 55},
	{3787,	 47},
	{3772,	 30},
	{3751,	 25},
	{3718,	 20},
	{3681,	 16},
	{3660,	 14},
	{3589,	 10},
	{3546,	  7},
	{3495,	  4},
	{3404,	  2},
	{3250,	  0},
};

static struct abx500_v_to_cap cap_tbl[] = {
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
static struct abx500_res_to_temp temp_tbl[] = {
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
static struct batres_vs_temp temp_to_batres_tbl_thermistor[] = {
	{ 40, 120},
	{ 30, 135},
	{ 20, 165},
	{ 10, 230},
	{ 00, 325},
	{-10, 445},
	{-20, 595},
};

/*
 * Note that the batres_vs_temp table must be strictly sorted by falling
 * temperature values to work.
 */
static struct batres_vs_temp temp_to_batres_tbl_ext_thermistor[] = {
	{ 60, 300},
	{ 30, 300},
	{ 20, 300},
	{ 10, 300},
	{ 00, 300},
	{-10, 300},
	{-20, 300},
};

/* battery resistance table for LI ION 9100 battery */
static struct batres_vs_temp temp_to_batres_tbl_9100[] = {
	{ 60, 180},
	{ 30, 180},
	{ 20, 180},
	{ 10, 180},
	{ 00, 180},
	{-10, 180},
	{-20, 180},
};

static struct abx500_battery_type bat_type_thermistor[] = {
	[BATTERY_UNKNOWN] = {
		/* First element always represent the UNKNOWN battery */
		.name = POWER_SUPPLY_TECHNOLOGY_UNKNOWN,
		.resis_high = 0,
		.resis_low = 0,
		.battery_resistance = 300,
		.charge_full_design = 612,
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
	},
	{
		.name = POWER_SUPPLY_TECHNOLOGY_LIPO,
		.resis_high = 53407,
		.resis_low = 12500,
		.battery_resistance = 300,
		.charge_full_design = 900,
		.nominal_voltage = 3600,
		.termination_vol = 4150,
		.termination_curr = 80,
		.recharge_cap = 95,
		.normal_cur_lvl = 700,
		.normal_vol_lvl = 4200,
		.maint_a_cur_lvl = 600,
		.maint_a_vol_lvl = 4150,
		.maint_a_chg_timer_h = 60,
		.maint_b_cur_lvl = 600,
		.maint_b_vol_lvl = 4100,
		.maint_b_chg_timer_h = 200,
		.low_high_cur_lvl = 300,
		.low_high_vol_lvl = 4000,
		.n_temp_tbl_elements = ARRAY_SIZE(temp_tbl_A_thermistor),
		.r_to_t_tbl = temp_tbl_A_thermistor,
		.n_v_cap_tbl_elements = ARRAY_SIZE(cap_tbl_A_thermistor),
		.v_to_cap_tbl = cap_tbl_A_thermistor,
		.n_batres_tbl_elements = ARRAY_SIZE(temp_to_batres_tbl_thermistor),
		.batres_tbl = temp_to_batres_tbl_thermistor,

	},
	{
		.name = POWER_SUPPLY_TECHNOLOGY_LIPO,
		.resis_high = 200000,
		.resis_low = 82869,
		.battery_resistance = 300,
		.charge_full_design = 900,
		.nominal_voltage = 3600,
		.termination_vol = 4150,
		.termination_curr = 80,
		.recharge_cap = 95,
		.normal_cur_lvl = 700,
		.normal_vol_lvl = 4200,
		.maint_a_cur_lvl = 600,
		.maint_a_vol_lvl = 4150,
		.maint_a_chg_timer_h = 60,
		.maint_b_cur_lvl = 600,
		.maint_b_vol_lvl = 4100,
		.maint_b_chg_timer_h = 200,
		.low_high_cur_lvl = 300,
		.low_high_vol_lvl = 4000,
		.n_temp_tbl_elements = ARRAY_SIZE(temp_tbl_B_thermistor),
		.r_to_t_tbl = temp_tbl_B_thermistor,
		.n_v_cap_tbl_elements = ARRAY_SIZE(cap_tbl_B_thermistor),
		.v_to_cap_tbl = cap_tbl_B_thermistor,
		.n_batres_tbl_elements = ARRAY_SIZE(temp_to_batres_tbl_thermistor),
		.batres_tbl = temp_to_batres_tbl_thermistor,
	},
};

static struct abx500_battery_type bat_type_ext_thermistor[] = {
	[BATTERY_UNKNOWN] = {
		/* First element always represent the UNKNOWN battery */
		.name = POWER_SUPPLY_TECHNOLOGY_UNKNOWN,
		.resis_high = 0,
		.resis_low = 0,
		.battery_resistance = 300,
		.charge_full_design = 612,
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
	},
/*
 * These are the batteries that doesn't have an internal NTC resistor to measure
 * its temperature. The temperature in this case is measure with a NTC placed
 * near the battery but on the PCB.
 */
	{
		.name = POWER_SUPPLY_TECHNOLOGY_LIPO,
		.resis_high = 76000,
		.resis_low = 53000,
		.battery_resistance = 300,
		.charge_full_design = 900,
		.nominal_voltage = 3700,
		.termination_vol = 4150,
		.termination_curr = 100,
		.recharge_cap = 95,
		.normal_cur_lvl = 700,
		.normal_vol_lvl = 4200,
		.maint_a_cur_lvl = 600,
		.maint_a_vol_lvl = 4150,
		.maint_a_chg_timer_h = 60,
		.maint_b_cur_lvl = 600,
		.maint_b_vol_lvl = 4100,
		.maint_b_chg_timer_h = 200,
		.low_high_cur_lvl = 300,
		.low_high_vol_lvl = 4000,
		.n_temp_tbl_elements = ARRAY_SIZE(temp_tbl),
		.r_to_t_tbl = temp_tbl,
		.n_v_cap_tbl_elements = ARRAY_SIZE(cap_tbl),
		.v_to_cap_tbl = cap_tbl,
		.n_batres_tbl_elements = ARRAY_SIZE(temp_to_batres_tbl_thermistor),
		.batres_tbl = temp_to_batres_tbl_thermistor,
	},
	{
		.name = POWER_SUPPLY_TECHNOLOGY_LION,
		.resis_high = 30000,
		.resis_low = 10000,
		.battery_resistance = 300,
		.charge_full_design = 950,
		.nominal_voltage = 3700,
		.termination_vol = 4150,
		.termination_curr = 100,
		.recharge_cap = 95,
		.normal_cur_lvl = 700,
		.normal_vol_lvl = 4200,
		.maint_a_cur_lvl = 600,
		.maint_a_vol_lvl = 4150,
		.maint_a_chg_timer_h = 60,
		.maint_b_cur_lvl = 600,
		.maint_b_vol_lvl = 4100,
		.maint_b_chg_timer_h = 200,
		.low_high_cur_lvl = 300,
		.low_high_vol_lvl = 4000,
		.n_temp_tbl_elements = ARRAY_SIZE(temp_tbl),
		.r_to_t_tbl = temp_tbl,
		.n_v_cap_tbl_elements = ARRAY_SIZE(cap_tbl),
		.v_to_cap_tbl = cap_tbl,
		.n_batres_tbl_elements = ARRAY_SIZE(temp_to_batres_tbl_thermistor),
		.batres_tbl = temp_to_batres_tbl_thermistor,
	},
	{
		.name = POWER_SUPPLY_TECHNOLOGY_LION,
		.resis_high = 95000,
		.resis_low = 76001,
		.battery_resistance = 300,
		.charge_full_design = 950,
		.nominal_voltage = 3700,
		.termination_vol = 4150,
		.termination_curr = 100,
		.recharge_cap = 95,
		.normal_cur_lvl = 700,
		.normal_vol_lvl = 4200,
		.maint_a_cur_lvl = 600,
		.maint_a_vol_lvl = 4150,
		.maint_a_chg_timer_h = 60,
		.maint_b_cur_lvl = 600,
		.maint_b_vol_lvl = 4100,
		.maint_b_chg_timer_h = 200,
		.low_high_cur_lvl = 300,
		.low_high_vol_lvl = 4000,
		.n_temp_tbl_elements = ARRAY_SIZE(temp_tbl),
		.r_to_t_tbl = temp_tbl,
		.n_v_cap_tbl_elements = ARRAY_SIZE(cap_tbl),
		.v_to_cap_tbl = cap_tbl,
		.n_batres_tbl_elements = ARRAY_SIZE(temp_to_batres_tbl_thermistor),
		.batres_tbl = temp_to_batres_tbl_thermistor,
	},
};

static const struct abx500_bm_capacity_levels cap_levels = {
	.critical	= 2,
	.low		= 10,
	.normal		= 70,
	.high		= 95,
	.full		= 100,
};

static const struct abx500_fg_parameters fg = {
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

static const struct abx500_maxim_parameters maxi_params = {
	.ena_maxi = true,
	.chg_curr = 910,
	.wait_cycles = 10,
	.charger_curr_step = 100,
};

static const struct abx500_bm_charger_parameters chg = {
	.usb_volt_max		= 5500,
	.usb_curr_max		= 1500,
	.ac_volt_max		= 7500,
	.ac_curr_max		= 1500,
};

struct abx500_bm_data ab8500_bm_data = {
	.temp_under             = 3,
	.temp_low               = 8,
	.temp_high              = 43,
	.temp_over              = 48,
	.main_safety_tmr_h      = 4,
	.temp_interval_chg      = 20,
	.temp_interval_nochg    = 120,
	.usb_safety_tmr_h       = 4,
	.bkup_bat_v             = BUP_VCH_SEL_2P6V,
	.bkup_bat_i             = BUP_ICH_SEL_150UA,
	.no_maintenance         = false,
	.capacity_scaling       = false,
	.adc_therm              = ABx500_ADC_THERM_BATCTRL,
	.chg_unknown_bat        = false,
	.enable_overshoot       = false,
	.fg_res                 = 100,
	.cap_levels             = &cap_levels,
	.bat_type               = bat_type_thermistor,
	.n_btypes               = 3,
	.batt_id                = 0,
	.interval_charging      = 5,
	.interval_not_charging  = 120,
	.temp_hysteresis        = 3,
	.gnd_lift_resistance    = 34,
	.maxi                   = &maxi_params,
	.chg_params             = &chg,
	.fg_params              = &fg,
};

int ab8500_bm_of_probe(struct device *dev,
		       struct device_node *np,
		       struct abx500_bm_data *bm)
{
	struct batres_vs_temp *tmp_batres_tbl;
	struct device_node *battery_node;
	const char *btech;
	int i;

	/* get phandle to 'battery-info' node */
	battery_node = of_parse_phandle(np, "battery", 0);
	if (!battery_node) {
		dev_err(dev, "battery node or reference missing\n");
		return -EINVAL;
	}

	btech = of_get_property(battery_node, "stericsson,battery-type", NULL);
	if (!btech) {
		dev_warn(dev, "missing property battery-name/type\n");
		return -EINVAL;
	}

	if (strncmp(btech, "LION", 4) == 0) {
		bm->no_maintenance  = true;
		bm->chg_unknown_bat = true;
		bm->bat_type[BATTERY_UNKNOWN].charge_full_design = 2600;
		bm->bat_type[BATTERY_UNKNOWN].termination_vol    = 4150;
		bm->bat_type[BATTERY_UNKNOWN].recharge_cap       = 95;
		bm->bat_type[BATTERY_UNKNOWN].normal_cur_lvl     = 520;
		bm->bat_type[BATTERY_UNKNOWN].normal_vol_lvl     = 4200;
	}

	if (of_property_read_bool(battery_node, "thermistor-on-batctrl")) {
		if (strncmp(btech, "LION", 4) == 0)
			tmp_batres_tbl = temp_to_batres_tbl_9100;
		else
			tmp_batres_tbl = temp_to_batres_tbl_thermistor;
	} else {
		bm->n_btypes   = 4;
		bm->bat_type   = bat_type_ext_thermistor;
		bm->adc_therm  = ABx500_ADC_THERM_BATTEMP;
		tmp_batres_tbl = temp_to_batres_tbl_ext_thermistor;
	}

	/* select the battery resolution table */
	for (i = 0; i < bm->n_btypes; ++i)
		bm->bat_type[i].batres_tbl = tmp_batres_tbl;

	of_node_put(battery_node);

	return 0;
}
