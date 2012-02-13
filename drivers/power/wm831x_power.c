/*
 * PMU driver for Wolfson Microelectronics wm831x PMICs
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/auxadc.h>
#include <linux/mfd/wm831x/pmu.h>
#include <linux/mfd/wm831x/pdata.h>
#include <linux/mfd/wm831x/irq.h>

#define WM831X_DEBUG
#undef  WM831X_DEBUG

#ifdef WM831X_DEBUG
#define	WM_BATT_DBG(x...) printk(KERN_INFO x)
#else
#define	WM_BATT_DBG(x...)  do {} while (0)
#endif

#define WM831X_CHG_SYSLO_SHIFT   4
#define WM831X_CHG_SYSOK_SHIFT   0
#define WM831X_CHG_SYSLO_MASK  ~(0x7 << 4)
#define WM831X_CHG_SYSOK_MASK  ~(0x7 << 0)

#define batt_num   52

static int batt_step_table[batt_num] = {
	3400,3420,3440,3475,3505,3525,
	3540,3557,3570,3580,3610,
	3630,3640,3652,3662,3672,
	3680,3687,3693,3699,3705,
	3710,3714,3718,3722,3726,
	3730,3734,3738,3742,3746,
	3750,3756,3764,3774,3786,
	3800,3808,3817,3827,3845,
	3950,3964,3982,4002,4026,
	4030,4034,4055,4070,4085,4120
};

static int batt_disp_table[batt_num] = {
	0,1,2,3,5,7,
	9,11,13,15,17,
	19,21,23,25,27,
	29,31,33,35,37,
	39,41,43,45,47,
	49,51,53,55,57,
	59,61,63,65,67,
	69,71,73,75,77,
	79,81,83,85,87,
	89,91,93,95,97,100
};

static int batt_chg_step_table[batt_num] = {
	3520,3525,3575,3600,3620,3644,//+160
	3662,3670,3684,3700,3715,//+150
	3720,3748,3756,3775,3790,//+140
	3810,3814,3818,3822,3825,//+130
	3830,3832,3834,3836,3837,//+120
	3839,3841,3842,3844,3844,//+110
	3855,3860,3864,3871,3890,//+100
	3910,3930,3952,3977,3997,//+90
	4030,4047,4064,4080,4096,//+80
	4132,4144,4150,4170,4195,4200//+70
};



#define TIMER_MS_COUNTS 1000
struct wm_batt_priv_data {
	int online;
	int status;
	int health;
	int level;
	int temp;
	int voltage;
};

struct wm831x_power {
	struct wm831x *wm831x;
	struct power_supply wall;
	struct power_supply usb;
	struct power_supply battery;
	struct work_struct batt_work;
	struct timer_list timer;
	struct wm_batt_priv_data batt_info;
	struct wake_lock 	syslo_wake;
	int interval;
};

struct wm831x_power *g_wm831x_power;

static int power_test_sysfs_init(void);
extern void wm831x_batt_vol_level(struct wm831x_power *power, int batt_vol, int *level);
static DEFINE_MUTEX(charging_mutex);
static struct wake_lock batt_wake_lock;

int wm831x_read_on_pin_status(void)
{
	int ret;
	
	if(!g_wm831x_power)
	{
		printk("err:%s:g_wm831x_power address is 0\n",__FUNCTION__);
		return -1;
	}
	
	ret = wm831x_reg_read(g_wm831x_power->wm831x, WM831X_ON_PIN_CONTROL);
	if (ret < 0)
		return ret;

	return !(ret & WM831X_ON_PIN_STS) ? 1 : 0;
}

static int wm831x_power_check_online(struct wm831x *wm831x, int supply,
				     union power_supply_propval *val)
{
	int ret;

	ret = wm831x_reg_read(wm831x, WM831X_SYSTEM_STATUS);
	if (ret < 0)
		return ret;

	if (ret & supply)
		val->intval = 1;
	else
		val->intval = 0;

	return 0;
}

int wm831x_read_chg_status(void)
{
	int ret, usb_chg = 0, wall_chg = 0;
	
	if(!g_wm831x_power)
	{
		printk("err:%s:g_wm831x_power address is 0\n",__FUNCTION__);
		return -1;
	}
	
	ret = wm831x_reg_read(g_wm831x_power->wm831x, WM831X_SYSTEM_STATUS);
	if (ret < 0)
		return ret;

	if (ret & WM831X_PWR_USB)
		usb_chg = 1;
	if (ret & WM831X_PWR_WALL)
		wall_chg = 1;

	return ((usb_chg | wall_chg) ? 1 : 0);
}

static int wm831x_power_read_voltage(struct wm831x *wm831x,
				     enum wm831x_auxadc src,
				     union power_supply_propval *val)
{
	int ret;
	ret = wm831x_auxadc_read_uv(wm831x, src);
	if (ret >= 0)
		val->intval = ret;
	
	return ret ;
}

int wm831x_read_batt_voltage(void)
{
	int ret = 0;
	
	if(!g_wm831x_power)
	{
		printk("err:%s:g_wm831x_power address is 0\n",__FUNCTION__);
		return -1;
	}
	
	ret = wm831x_auxadc_read_uv(g_wm831x_power->wm831x, WM831X_AUX_BATT);
	return ret;
}
//EXPORT_SYMBOL_GPL(wm831x_get_batt_voltage);

/*********************************************************************
 *		WALL Power
 *********************************************************************/
static int wm831x_wall_get_prop(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct wm831x_power *wm831x_power = dev_get_drvdata(psy->dev->parent);
	struct wm831x *wm831x = wm831x_power->wm831x;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = wm831x_power_check_online(wm831x, WM831X_PWR_WALL, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = wm831x_power_read_voltage(wm831x, WM831X_AUX_WALL, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property wm831x_wall_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

/*********************************************************************
 *		USB Power
 *********************************************************************/
static int wm831x_usb_get_prop(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct wm831x_power *wm831x_power = dev_get_drvdata(psy->dev->parent);
	struct wm831x *wm831x = wm831x_power->wm831x;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = wm831x_power_check_online(wm831x, WM831X_PWR_USB, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = wm831x_power_read_voltage(wm831x, WM831X_AUX_USB, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property wm831x_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

/*********************************************************************
 *		Battery properties
 *********************************************************************/

struct chg_map {
	int val;
	int reg_val;
};

static struct chg_map trickle_ilims[] = {
	{  50, 0 << WM831X_CHG_TRKL_ILIM_SHIFT },
	{ 100, 1 << WM831X_CHG_TRKL_ILIM_SHIFT },
	{ 150, 2 << WM831X_CHG_TRKL_ILIM_SHIFT },
	{ 200, 3 << WM831X_CHG_TRKL_ILIM_SHIFT },
};

static struct chg_map vsels[] = {
	{ 4050, 0 << WM831X_CHG_VSEL_SHIFT },
	{ 4100, 1 << WM831X_CHG_VSEL_SHIFT },
	{ 4150, 2 << WM831X_CHG_VSEL_SHIFT },
	{ 4200, 3 << WM831X_CHG_VSEL_SHIFT },
};

static struct chg_map fast_ilims[] = {
	{    0,  0 << WM831X_CHG_FAST_ILIM_SHIFT },
	{   50,  1 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  100,  2 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  150,  3 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  200,  4 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  250,  5 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  300,  6 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  350,  7 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  400,  8 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  450,  9 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  500, 10 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  600, 11 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  700, 12 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  800, 13 << WM831X_CHG_FAST_ILIM_SHIFT },
	{  900, 14 << WM831X_CHG_FAST_ILIM_SHIFT },
	{ 1000, 15 << WM831X_CHG_FAST_ILIM_SHIFT },
};

static struct chg_map eoc_iterms[] = {
	{ 20, 0 << WM831X_CHG_ITERM_SHIFT },
	{ 30, 1 << WM831X_CHG_ITERM_SHIFT },
	{ 40, 2 << WM831X_CHG_ITERM_SHIFT },
	{ 50, 3 << WM831X_CHG_ITERM_SHIFT },
	{ 60, 4 << WM831X_CHG_ITERM_SHIFT },
	{ 70, 5 << WM831X_CHG_ITERM_SHIFT },
	{ 80, 6 << WM831X_CHG_ITERM_SHIFT },
	{ 90, 7 << WM831X_CHG_ITERM_SHIFT },
};

static struct chg_map chg_times[] = {
	{  60,  0 << WM831X_CHG_TIME_SHIFT },
	{  90,  1 << WM831X_CHG_TIME_SHIFT },
	{ 120,  2 << WM831X_CHG_TIME_SHIFT },
	{ 150,  3 << WM831X_CHG_TIME_SHIFT },
	{ 180,  4 << WM831X_CHG_TIME_SHIFT },
	{ 210,  5 << WM831X_CHG_TIME_SHIFT },
	{ 240,  6 << WM831X_CHG_TIME_SHIFT },
	{ 270,  7 << WM831X_CHG_TIME_SHIFT },
	{ 300,  8 << WM831X_CHG_TIME_SHIFT },
	{ 330,  9 << WM831X_CHG_TIME_SHIFT },
	{ 360, 10 << WM831X_CHG_TIME_SHIFT },
	{ 390, 11 << WM831X_CHG_TIME_SHIFT },
	{ 420, 12 << WM831X_CHG_TIME_SHIFT },
	{ 450, 13 << WM831X_CHG_TIME_SHIFT },
	{ 480, 14 << WM831X_CHG_TIME_SHIFT },
	{ 510, 15 << WM831X_CHG_TIME_SHIFT },
};

static struct chg_map chg_syslos[] = {
	{ 2800, 0 << WM831X_CHG_SYSLO_SHIFT},
	{ 2900, 1 << WM831X_CHG_SYSLO_SHIFT},
	{ 3000, 2 << WM831X_CHG_SYSLO_SHIFT},
	{ 3100, 3 << WM831X_CHG_SYSLO_SHIFT},
	{ 3200, 4 << WM831X_CHG_SYSLO_SHIFT},
	{ 3300, 5 << WM831X_CHG_SYSLO_SHIFT},
	{ 3400, 6 << WM831X_CHG_SYSLO_SHIFT},
	{ 3500, 7 << WM831X_CHG_SYSLO_SHIFT},
};

static struct chg_map chg_sysoks[] = {
	{ 2800, 0 << WM831X_CHG_SYSOK_SHIFT},
	{ 2900, 1 << WM831X_CHG_SYSOK_SHIFT},
	{ 3000, 2 << WM831X_CHG_SYSOK_SHIFT},
	{ 3100, 3 << WM831X_CHG_SYSOK_SHIFT},
	{ 3200, 4 << WM831X_CHG_SYSOK_SHIFT},
	{ 3300, 5 << WM831X_CHG_SYSOK_SHIFT},
	{ 3400, 6 << WM831X_CHG_SYSOK_SHIFT},
	{ 3500, 7 << WM831X_CHG_SYSOK_SHIFT},
};

static void wm831x_battey_apply_config(struct wm831x *wm831x,
				       struct chg_map *map, int count, int val,
				       int *reg, const char *name,
				       const char *units)
{
	int i;
	for (i = 0; i < count; i++)
		if (val == map[i].val)
			break;
	if (i == count) {
		dev_err(wm831x->dev, "Invalid %s %d%s\n",
			name, val, units);
	} else {
		*reg |= map[i].reg_val;
		dev_dbg(wm831x->dev, "Set %s of %d%s\n", name, val, units);
	}
}

static void wm831x_config_battery(struct wm831x *wm831x)
{
	struct wm831x_pdata *wm831x_pdata = wm831x->dev->platform_data;
	struct wm831x_battery_pdata *pdata;
	int ret, reg1, reg2, reg3;

	if (!wm831x_pdata || !wm831x_pdata->battery) {
		dev_warn(wm831x->dev,
			 "No battery charger configuration\n");
		return;
	}

	pdata = wm831x_pdata->battery;

	reg1 = 0;
	reg2 = 0;
	reg3 = 0;

	if (!pdata->enable) {
		dev_info(wm831x->dev, "Battery charger disabled\n");
		return;
	}

	reg1 |= WM831X_CHG_ENA;
	if (pdata->off_mask)
		reg2 |= WM831X_CHG_OFF_MSK;
	if (pdata->fast_enable)
		reg1 |= WM831X_CHG_FAST;

	wm831x_battey_apply_config(wm831x, trickle_ilims,
				   ARRAY_SIZE(trickle_ilims),
				   pdata->trickle_ilim, &reg2,
				   "trickle charge current limit", "mA");

	wm831x_battey_apply_config(wm831x, vsels, ARRAY_SIZE(vsels),
				   pdata->vsel, &reg2,
				   "target voltage", "mV");

	wm831x_battey_apply_config(wm831x, fast_ilims, ARRAY_SIZE(fast_ilims),
				   pdata->fast_ilim, &reg2,
				   "fast charge current limit", "mA");

	wm831x_battey_apply_config(wm831x, eoc_iterms, ARRAY_SIZE(eoc_iterms),
				   pdata->eoc_iterm, &reg1,
				   "end of charge current threshold", "mA");

	wm831x_battey_apply_config(wm831x, chg_times, ARRAY_SIZE(chg_times),
				   pdata->timeout, &reg2,
				   "charger timeout", "min");

	wm831x_battey_apply_config(wm831x, chg_syslos, ARRAY_SIZE(chg_syslos),
				   pdata->syslo, &reg3,
				   "syslo voltage", "mV");

	wm831x_battey_apply_config(wm831x, chg_sysoks, ARRAY_SIZE(chg_sysoks),
				   pdata->sysok, &reg3,
				   "sysok voltage", "mV");

	ret = wm831x_reg_unlock(wm831x);
	if (ret != 0) {
		dev_err(wm831x->dev, "Failed to unlock registers: %d\n", ret);
		return;
	}

	ret = wm831x_set_bits(wm831x, WM831X_CHARGER_CONTROL_1,
			      WM831X_CHG_ENA_MASK |
			      WM831X_CHG_FAST_MASK |
			      WM831X_CHG_ITERM_MASK,
			      reg1);
	if (ret != 0) {
		dev_err(wm831x->dev, "Failed to set charger control 1: %d\n",
			ret);
	}
	ret = wm831x_set_bits(wm831x, WM831X_CHARGER_CONTROL_2,
			      WM831X_CHG_OFF_MSK |
			      WM831X_CHG_TIME_MASK |
			      WM831X_CHG_FAST_ILIM_MASK |
			      WM831X_CHG_TRKL_ILIM_MASK |
			      WM831X_CHG_VSEL_MASK,
			      reg2);
	if (ret != 0) {
		dev_err(wm831x->dev, "Failed to set charger control 2: %d\n",
			ret);
	}

	ret = wm831x_set_bits(wm831x, WM831X_SYSVDD_CONTROL,
				          WM831X_CHG_SYSLO_MASK |
						  WM831X_CHG_SYSOK_MASK,
						  reg3);
	if (ret < 0) {
		dev_err(wm831x->dev, "Failed to set sysvdd control reg: %d\n",ret);
	}

	wm831x_reg_lock(wm831x);
}

static int wm831x_bat_check_status(struct wm831x *wm831x, int *status)
{
	int ret;

	ret = wm831x_reg_read(wm831x, WM831X_SYSTEM_STATUS);
	if (ret < 0)
		return ret;

	if (ret & WM831X_PWR_SRC_BATT) {
		*status = POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	}

	ret = wm831x_reg_read(wm831x, WM831X_CHARGER_STATUS);
	if (ret < 0)
		return ret;


	switch (ret & WM831X_CHG_STATE_MASK) {
	case WM831X_CHG_STATE_OFF:
		*status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case WM831X_CHG_STATE_TRICKLE:
	case WM831X_CHG_STATE_FAST:
		*status = POWER_SUPPLY_STATUS_CHARGING;
		break;

	default:
		*status = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	}

	return 0;
}
int wm831x_read_bat_charging_status(void)
{
	int ret, status;
	
	if(!g_wm831x_power)
	{
		printk("err:%s:g_wm831x_power address is 0\n",__FUNCTION__);
		return -1;
	}
	
	ret = wm831x_bat_check_status(g_wm831x_power->wm831x, &status);
	if (ret < 0)
		return ret;
	if (status == POWER_SUPPLY_STATUS_CHARGING) 
		return 1;
	return 0;
}

static int wm831x_bat_check_type(struct wm831x *wm831x, int *type)
{
	int ret;
#ifdef WM831X_DEBUG_0 
	ret = wm831x_reg_read(wm831x, WM831X_POWER_STATE);
	if (ret < 0)
		return ret;
	WM_BATT_DBG("%s: wm831x power status %#x\n", __FUNCTION__, ret);

	ret = wm831x_reg_read(wm831x, WM831X_SYSTEM_STATUS);
	if (ret < 0)
		return ret;
	WM_BATT_DBG("%s: wm831x system status %#x\n", __FUNCTION__, ret);

	ret = wm831x_reg_read(wm831x, WM831X_CHARGER_CONTROL_1);
	if (ret < 0)
		return ret;
	WM_BATT_DBG("%s: wm831x charger control1 %#x\n", __FUNCTION__, ret);

	ret = wm831x_reg_read(wm831x, WM831X_CHARGER_CONTROL_2);
	if (ret < 0)
		return ret;
	WM_BATT_DBG("%s: wm831x charger control2 %#x\n", __FUNCTION__, ret);

	ret = wm831x_reg_read(wm831x, WM831X_CHARGER_STATUS);
	if (ret < 0)
		return ret;
	WM_BATT_DBG("%s: wm831x charger status %#x\n\n", __FUNCTION__, ret);
#endif

	ret = wm831x_reg_read(wm831x, WM831X_CHARGER_STATUS);
	if (ret < 0)
		return ret;

	switch (ret & WM831X_CHG_STATE_MASK) {
	case WM831X_CHG_STATE_TRICKLE:
	case WM831X_CHG_STATE_TRICKLE_OT:
		*type = POWER_SUPPLY_CHARGE_TYPE_TRICKLE;
		break;
	case WM831X_CHG_STATE_FAST:
	case WM831X_CHG_STATE_FAST_OT:
		*type = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;
	default:
		*type = POWER_SUPPLY_CHARGE_TYPE_NONE;
		break;
	}

	return 0;
}

static int wm831x_bat_check_health(struct wm831x *wm831x, int *health)
{
	int ret;

	ret = wm831x_reg_read(wm831x, WM831X_CHARGER_STATUS);
	if (ret < 0)
		return ret;

	if (ret & WM831X_BATT_HOT_STS) {
		*health = POWER_SUPPLY_HEALTH_OVERHEAT;
		return 0;
	}

	if (ret & WM831X_BATT_COLD_STS) {
		*health = POWER_SUPPLY_HEALTH_COLD;
		return 0;
	}

	if (ret & WM831X_BATT_OV_STS) {
		*health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		return 0;
	}

	switch (ret & WM831X_CHG_STATE_MASK) {
	case WM831X_CHG_STATE_TRICKLE_OT:
	case WM831X_CHG_STATE_FAST_OT:
		*health = POWER_SUPPLY_HEALTH_OVERHEAT;
		break;
	case WM831X_CHG_STATE_DEFECTIVE:
		*health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		break;
	default:
		*health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	}

	return 0;
}

static int wm831x_bat_get_prop(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct wm831x_power *wm831x_power = dev_get_drvdata(psy->dev->parent);
	struct wm831x *wm831x = wm831x_power->wm831x;
	int  ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		ret = wm831x_bat_check_status(wm831x, &val->intval);
		//val->intval = wm831x_power->batt_info.status;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
	//case POWER_SUPPLY_PROP_ONLINE:
		//ret = wm831x_power_check_online(wm831x, WM831X_PWR_SRC_BATT, val);
		val->intval = wm831x_power->batt_info.online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		//ret = wm831x_power_read_voltage(wm831x, WM831X_AUX_BATT, val);
		val->intval = wm831x_power->batt_info.voltage;//uV
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		//ret = wm831x_bat_check_health(wm831x, &val->intval);
		val->intval = wm831x_power->batt_info.health;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		ret = wm831x_bat_check_type(wm831x, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		//ret = wm831x_power_read_voltage(wm831x, WM831X_AUX_BATT, val);
		//wm831x_batt_vol_level(wm831x_power, val->intval, &level);
		//val->intval = level;
		val->intval = wm831x_power->batt_info.level;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property wm831x_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY, /* in percents! */
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
};

#ifdef CONFIG_WM831X_WITH_BATTERY
static const char *wm831x_bat_irqs[] = {
	"BATT HOT",
	"BATT COLD",
	"BATT FAIL",
	"OV",
	"END",
	"TO",
	"MODE",
	"START",
};

static irqreturn_t wm831x_bat_irq(int irq, void *data)
{
	struct wm831x_power *wm831x_power = data;
	struct wm831x *wm831x = wm831x_power->wm831x;
	int irq0;
	
	irq0 = wm831x->irq_base + WM831X_IRQ_CHG_BATT_HOT + 1;
	dev_crit(wm831x->dev, "battery changed: i=%d\n", irq-irq0);
			
	/* The battery charger is autonomous so we don't need to do
	 * anything except kick user space */
	power_supply_changed(&wm831x_power->battery);

	return IRQ_HANDLED;
}
#endif

/*********************************************************************
 *		Initialisation
 *********************************************************************/

static irqreturn_t wm831x_syslo_irq(int irq, void *data)
{
	struct wm831x_power *wm831x_power = data;
	struct wm831x *wm831x = wm831x_power->wm831x;

	/* Not much we can actually *do* but tell people for
	 * posterity, we're probably about to run out of power. */
	dev_crit(wm831x->dev, "SYSVDD under voltage and wake lock 60s\n");
	wake_lock_timeout(&wm831x_power->syslo_wake,60*HZ);//wait for android closing system
	return IRQ_HANDLED;
}

static irqreturn_t wm831x_pwr_src_irq(int irq, void *data)
{
	struct wm831x_power *wm831x_power = data;
	struct wm831x *wm831x = wm831x_power->wm831x;

	wake_lock_timeout(&batt_wake_lock, 30 * HZ);
	dev_dbg(wm831x->dev, "Power source changed\n");
	WM_BATT_DBG("%s:Power source changed\n", __FUNCTION__); 
	/* Just notify for everything - little harm in overnotifying. */
	power_supply_changed(&wm831x_power->battery);
	power_supply_changed(&wm831x_power->usb);
	power_supply_changed(&wm831x_power->wall);


	return IRQ_HANDLED;
}

static void wm831x_batt_timer_handler(unsigned long data)
{
	struct wm831x_power *wm831x_power = (struct wm831x_power*)data;
	schedule_work(&wm831x_power->batt_work);
	mod_timer(&wm831x_power->timer, jiffies + msecs_to_jiffies(wm831x_power->interval));
}

void wm831x_batt_vol_level(struct wm831x_power *wm831x_power, int batt_vol, int *level)
{
	int i, ret, status;
	static int chg_plus = 1000;
	static int chg_minus = 1000;
	static int chg_curr = 0;
	static int chg_num = 60;
	static int disp_plus = 1000;
	static int disp_minus = 1000;
	static int disp_minus2 = 1000;
	static int disp_curr = 0;
	static int disp_num = 50;
	static int batt_level_all = 0;
	static int batt_level[20];
	static int avr_num = 0;
	static int avr_int = 0;


	*level = wm831x_power->batt_info.level;
	ret = wm831x_bat_check_status(wm831x_power->wm831x, &status);
	if (ret < 0) {
		printk("%s: check bat status failer...err = %d\n", __FUNCTION__, ret);
		return;
	}

	if (status == POWER_SUPPLY_STATUS_NOT_CHARGING 
			&& batt_vol >= batt_step_table[batt_num-1]) {
		*level = 100;
		return;
	}

	if (status == POWER_SUPPLY_STATUS_CHARGING) 
	{
		disp_plus = 0;
		disp_minus = 0;
		disp_minus2 = 0;
		disp_curr = 0;
		for(i = 0; i < batt_num; i++){        
			if(batt_vol >= batt_chg_step_table[i] && 
					 batt_vol < batt_chg_step_table[i+1])
				break;     
		}
		if(batt_vol <= batt_chg_step_table[0])
			i = 0;
		if(batt_vol >= batt_chg_step_table[batt_num-1])
			i = batt_num-1;
		if(avr_int==0){
		       	batt_level[avr_num] = batt_disp_table[i];
		  	batt_level_all += batt_level[avr_num];
			avr_num++;
			if(avr_num >= 20)
			{
				avr_num = 0;
				avr_int = 1;
			}
			else
			{
				*level = batt_disp_table[i];
				return 0;
			}
		}
		else {
			batt_level_all -= batt_level[avr_num];
			batt_level[avr_num]=batt_disp_table[i];
			batt_level_all += batt_level[avr_num];
			avr_num++;
		}
		if(avr_num >= 20) 
	       		avr_num = 0;
		*level = batt_level_all/20;
		if ((chg_plus == 1000) && (chg_minus == 1000))
		{
			*level = *level;
			chg_plus = 0;
			chg_minus = 0;
			chg_curr = 0;

		}
		else
		{			

			if (*level >= (wm831x_power->batt_info.level+1))
			{
				chg_minus = 0;
				chg_curr = 0;
				if(*level < 85)
					chg_num =10;
				else
					chg_num = 5;
				if (++chg_plus > chg_num)
				{
					*level = wm831x_power->batt_info.level + 1;
					chg_plus = 0;
				
				}
				else
				{
					*level = wm831x_power->batt_info.level;
				}
			}
			else
			{
				chg_plus = 0;
				chg_minus = 0;
				chg_curr = 0;
				*level = wm831x_power->batt_info.level;
			}
		}
		

		if (*level >= 100)
			*level = 100;
		if (*level < 0)
			*level = 0;
	}
	else 
	{
		chg_plus = 0;
		chg_minus = 0;
		chg_curr = 0;
		for(i = 0; i < batt_num; i++){        
			if(batt_vol >= batt_step_table[i] && 
					 batt_vol < batt_step_table[i+1])
				break;     
		}
		if(batt_vol <= batt_step_table[0])
			i = 0;
		if(batt_vol >= batt_step_table[batt_num-1])
			i = batt_num-1;
		if(avr_int==0){
		       	batt_level[avr_num] = batt_disp_table[i];
		  	batt_level_all += batt_level[avr_num];
			avr_num++;
			if(avr_num >= 20)
			{
				avr_num = 0;
				avr_int = 1;
			}
			else
			{
				*level = batt_disp_table[i];
				return 0;
			}
		}
		else {
			batt_level_all -= batt_level[avr_num];
			batt_level[avr_num]=batt_disp_table[i];
			batt_level_all += batt_level[avr_num];
			avr_num++;
		}
		if(avr_num >= 20) 
	       		avr_num = 0;
		*level = batt_level_all/20;
		if ((disp_plus == 1000) && (disp_minus == 1000))
		{
			*level = *level;
			disp_plus = 0;
			disp_minus = 0;
			disp_minus2 =0;
			disp_curr = 0;
		}
		else
		{	
			if(*level <= (wm831x_power ->batt_info.level -20))
			{
				disp_plus = 0;
				disp_curr = 0;
				disp_minus2 = 0;
				disp_num = 1;
				 if (++disp_minus > disp_num)
				{
					*level = wm831x_power->batt_info.level - 20;
					disp_minus = 0;
				}
				else
				{
					*level = wm831x_power->batt_info.level;
				}
			}
			else if (*level <= (wm831x_power->batt_info.level-1)) 	
			{
				disp_plus = 0;
				disp_curr = 0;
				disp_minus = 0;
				if((*level < 17) || (*level > 85))
					disp_num = 30;
				else
					disp_num = 80;
		
				 if (++disp_minus2 > disp_num)
				{
					*level = wm831x_power->batt_info.level - 1;
					disp_minus2 = 0;
				}
				else
				{

					*level = wm831x_power->batt_info.level;
				}
			}
			else
			{
				disp_plus = 0;
				disp_minus = 0;
				disp_minus2 = 0;
				disp_curr = 0;
				*level = wm831x_power->batt_info.level;
			}
		}

		if (*level >= 100)
			*level = 100;
		if (*level < 0)
			*level = 0;
	}
}

static void wm831x_batt_work(struct work_struct *work)
{
	int online, status,health,level, ret; 
    union power_supply_propval val;
	struct wm831x_power *power = container_of(work, struct wm831x_power, batt_work);

	ret = wm831x_power_check_online(power->wm831x, WM831X_PWR_SRC_BATT, &val);
	if (ret < 0) {
		printk("%s: check bat online failer...  err = %d\n", __FUNCTION__, ret);
		return;
	}
	online = val.intval;

	ret = wm831x_bat_check_status(power->wm831x, &status);
	if (ret < 0) {
		printk("%s: check bat status failer...  err = %d\n", __FUNCTION__, ret);
		return;
	}

	ret = wm831x_bat_check_health(power->wm831x, &health);
	if (ret < 0) {
		printk("%s: check bat health failer...  err = %d\n", __FUNCTION__, ret);
		return;
	}

	ret = wm831x_power_read_voltage(power->wm831x, WM831X_AUX_BATT, &val);
	if (ret < 0) {
		printk("%s: read bat voltage failer...err = %d\n", __FUNCTION__, ret);
		return;
	}
	power->batt_info.voltage = val.intval;

	wm831x_batt_vol_level(power, val.intval / 1000, &level);
	//mod_timer(&power->timer, jiffies + msecs_to_jiffies(power->interval));

	if (online != power->batt_info.online || status != power->batt_info.status
			|| health != power->batt_info.health || level != power->batt_info.level)
	{
		power->batt_info.online = online;
		power->batt_info.status = status;
		power->batt_info.health = health;
		power->batt_info.level  = level;

		power_supply_changed(&power->battery);
	}

}

#ifdef CONFIG_POWER_ON_CHARGER_DISPLAY
static void wm831x_batt_check(struct wm831x_power *power)
{
	int online, status,health,level, ret; 
	union power_supply_propval val;
//	struct wm831x_power *power = container_of(work, struct wm831x_power, batt_work);

	ret = wm831x_power_check_online(power->wm831x, WM831X_PWR_SRC_BATT, &val);
	if (ret < 0) {
		printk("%s: check bat online failer...  err = %d\n", __FUNCTION__, ret);
		return;
	}
	online = val.intval;

	ret = wm831x_bat_check_status(power->wm831x, &status);
	if (ret < 0) {
		printk("%s: check bat status failer...  err = %d\n", __FUNCTION__, ret);
		return;
	}

	ret = wm831x_bat_check_health(power->wm831x, &health);
	if (ret < 0) {
		printk("%s: check bat health failer...  err = %d\n", __FUNCTION__, ret);
		return;
	}

	ret = wm831x_power_read_voltage(power->wm831x, WM831X_AUX_BATT, &val);
	if (ret < 0) {
		printk("%s: read bat voltage failer...err = %d\n", __FUNCTION__, ret);
		return;
	}
	power->batt_info.voltage = val.intval;

	wm831x_batt_vol_level(power, val.intval / 1000, &level);
	//mod_timer(&power->timer, jiffies + msecs_to_jiffies(power->interval));

	if (online != power->batt_info.online || status != power->batt_info.status
			|| health != power->batt_info.health || level != power->batt_info.level)
	{
		power->batt_info.online = online;
		power->batt_info.status = status;
		power->batt_info.health = health;
		power->batt_info.level  = level;

		power_supply_changed(&power->battery);
	}
}
#endif


static __devinit int wm831x_power_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_power *power;
	struct power_supply *usb;
	struct power_supply *battery;
	struct power_supply *wall;
	int ret, irq;

	power = kzalloc(sizeof(struct wm831x_power), GFP_KERNEL);
	if (power == NULL)
		return -ENOMEM;

	power->wm831x = wm831x;
	platform_set_drvdata(pdev, power);

	usb = &power->usb;
	battery = &power->battery;
	wall = &power->wall;

	/* We ignore configuration failures since we can still read back
	 * the status without enabling the charger.
	 */
	wm831x_config_battery(wm831x);
	wake_lock_init(&batt_wake_lock, WAKE_LOCK_SUSPEND, "batt_lock");

	wall->name = "wm831x-wall";
	wall->type = POWER_SUPPLY_TYPE_MAINS;
	wall->properties = wm831x_wall_props;
	wall->num_properties = ARRAY_SIZE(wm831x_wall_props);
	wall->get_property = wm831x_wall_get_prop;
	ret = power_supply_register(&pdev->dev, wall);
	if (ret)
		goto err_kmalloc;

	battery->name = "wm831x-battery";
	battery->properties = wm831x_bat_props;
	battery->num_properties = ARRAY_SIZE(wm831x_bat_props);
	battery->get_property = wm831x_bat_get_prop;
	battery->use_for_apm = 1;
	ret = power_supply_register(&pdev->dev, battery);
	if (ret)
		goto err_wall;

	usb->name = "wm831x-usb",
	usb->type = POWER_SUPPLY_TYPE_USB;
	usb->properties = wm831x_usb_props;
	usb->num_properties = ARRAY_SIZE(wm831x_usb_props);
	usb->get_property = wm831x_usb_get_prop;
	ret = power_supply_register(&pdev->dev, usb);
	if (ret)
		goto err_battery;

	irq = platform_get_irq_byname(pdev, "SYSLO");
	ret = request_threaded_irq(irq, NULL, wm831x_syslo_irq,
				   IRQF_TRIGGER_RISING, "System power low",
				   power);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request SYSLO IRQ %d: %d\n",
			irq, ret);
		goto err_usb;
	}

	irq = platform_get_irq_byname(pdev, "PWR SRC");
	ret = request_threaded_irq(irq, NULL, wm831x_pwr_src_irq,
				   IRQF_TRIGGER_RISING, "Power source",
				   power);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request PWR SRC IRQ %d: %d\n",
			irq, ret);
		goto err_syslo;
	}

#ifdef CONFIG_WM831X_WITH_BATTERY
	int i;
	for (i = 0; i < ARRAY_SIZE(wm831x_bat_irqs); i++) {
		irq = platform_get_irq_byname(pdev, wm831x_bat_irqs[i]);
		ret = request_threaded_irq(irq, NULL, wm831x_bat_irq,
					   IRQF_TRIGGER_RISING,
					   wm831x_bat_irqs[i],
					   power);
		WM_BATT_DBG("%s: %s irq no %d\n", __FUNCTION__, wm831x_bat_irqs[i], irq);
		if (ret != 0) {
			dev_err(&pdev->dev,
				"Failed to request %s IRQ %d: %d\n",
				wm831x_bat_irqs[i], irq, ret);
			goto err_bat_irq;
		}
	}
#endif

	power->interval = TIMER_MS_COUNTS;
	power->batt_info.level = 100;
	power->batt_info.voltage   = 4200000;
	power->batt_info.online    = 1;
	power->batt_info.status    = POWER_SUPPLY_STATUS_DISCHARGING;
	power->batt_info.health    = POWER_SUPPLY_HEALTH_GOOD;

	wake_lock_init(&power->syslo_wake, WAKE_LOCK_SUSPEND, "wm831x_syslo_wake");
	INIT_WORK(&power->batt_work, wm831x_batt_work);
	setup_timer(&power->timer, wm831x_batt_timer_handler, (unsigned long)power);
	power->timer.expires = jiffies + msecs_to_jiffies(1000);
	add_timer(&power->timer);

	g_wm831x_power = power;
#ifdef CONFIG_POWER_ON_CHARGER_DISPLAY
	wm831x_batt_check(power);//xsf
#endif
	
	printk("%s:wm831x_power initialized\n",__FUNCTION__);
	power_test_sysfs_init();
	return ret;
	
#ifdef CONFIG_WM831X_WITH_BATTERY
err_bat_irq:
	for (; i >= 0; i--) {
		irq = platform_get_irq_byname(pdev, wm831x_bat_irqs[i]);
		free_irq(irq, power);
	}
	irq = platform_get_irq_byname(pdev, "PWR SRC");
	free_irq(irq, power);
#endif

err_syslo:
	irq = platform_get_irq_byname(pdev, "SYSLO");
	free_irq(irq, power);
err_usb:
	power_supply_unregister(usb);
err_battery:
	power_supply_unregister(battery);
err_wall:
	power_supply_unregister(wall);
err_kmalloc:
	kfree(power);
	return ret;
}

static __devexit int wm831x_power_remove(struct platform_device *pdev)
{
	struct wm831x_power *wm831x_power = platform_get_drvdata(pdev);
	int irq;
#ifdef CONFIG_WM831X_WITH_BATTERY
	int i;
	for (i = 0; i < ARRAY_SIZE(wm831x_bat_irqs); i++) {
		irq = platform_get_irq_byname(pdev, wm831x_bat_irqs[i]);
		free_irq(irq, wm831x_power);
	}
#endif
	irq = platform_get_irq_byname(pdev, "PWR SRC");
	free_irq(irq, wm831x_power);

	irq = platform_get_irq_byname(pdev, "SYSLO");
	free_irq(irq, wm831x_power);

	power_supply_unregister(&wm831x_power->battery);
	power_supply_unregister(&wm831x_power->wall);
	power_supply_unregister(&wm831x_power->usb);
	kfree(wm831x_power);
	return 0;
}

#ifdef CONFIG_PM
static int wm831x_battery_suspend(struct platform_device *dev, pm_message_t state)
{
	struct wm831x_power *power = (struct wm831x_power *)platform_get_drvdata(dev);
	flush_scheduled_work();
	del_timer(&power->timer);
	return 0;
}

static int wm831x_battery_resume(struct platform_device *dev)
{
	struct wm831x_power *power = (struct wm831x_power *)platform_get_drvdata(dev);
	power->timer.expires = jiffies + msecs_to_jiffies(power->interval);
	add_timer(&power->timer);
	return 0;
}
#else
#define wm831x_battery_suspend NULL
#define wm831x_battery_resume  NULL
#endif

static struct platform_driver wm831x_power_driver = {
	.probe = wm831x_power_probe,
	.remove = __devexit_p(wm831x_power_remove),
	.suspend = wm831x_battery_suspend,
	.resume = wm831x_battery_resume,
	.driver = {
		.name = "wm831x-power",
	},
};
static int __init wm831x_power_init(void)
{
	return platform_driver_register(&wm831x_power_driver);
}
#ifndef CONFIG_POWER_ON_CHARGER_DISPLAY
module_init(wm831x_power_init);
#else
subsys_initcall(wm831x_power_init);
#endif

static void __exit wm831x_power_exit(void)
{
	platform_driver_unregister(&wm831x_power_driver);
}
module_exit(wm831x_power_exit);


static ssize_t power_prop_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	int level, power_status, system_status, chg_ctl1, chg_ctl2, chg_status;
    union power_supply_propval val;

	if (!g_wm831x_power)
		return -1;
	power_status = wm831x_reg_read(g_wm831x_power->wm831x, WM831X_POWER_STATE);
	if (power_status < 0)
		return power_status;
	//printk("wm831x power status %#x\n", ret);

	system_status = wm831x_reg_read(g_wm831x_power->wm831x, WM831X_SYSTEM_STATUS);
	if (system_status < 0)
		return system_status;
	//printk("wm831x system status %#x\n", ret);

	chg_ctl1 = wm831x_reg_read(g_wm831x_power->wm831x, WM831X_CHARGER_CONTROL_1);
	if (chg_ctl1 < 0)
		return chg_ctl1;
	//printk("wm831x charger control1 %#x\n", ret);

	chg_ctl2 = wm831x_reg_read(g_wm831x_power->wm831x, WM831X_CHARGER_CONTROL_2);
	if (chg_ctl2 < 0)
		return chg_ctl2;
	//printk("wm831x charger control2 %#x\n", ret);

	chg_status = wm831x_reg_read(g_wm831x_power->wm831x, WM831X_CHARGER_STATUS);
	if (chg_status < 0)
		return chg_status;
	//printk("wm831x charger status %#x\n", ret);

	ret = wm831x_power_read_voltage(g_wm831x_power->wm831x, WM831X_AUX_BATT, &val);
	if (ret < 0)
		return ret;
	wm831x_batt_vol_level(g_wm831x_power, val.intval / 1000, &level);
	//printk("batt_vol = %d batt_level = %d\n", val.intval, level);
	//
	sprintf(buf, "power_status=%#x\n"
			     "system_status=%#x\n"
				 "chg_ctl1=%#x\n"
				 "chg_ctl2=%#x\n"
				 "chg_status=%#x\n"
				 "batt_vol=%d\n"
				 "batt_level=%d%%\n",
				 power_status,
				 system_status,
				 chg_ctl1,
				 chg_ctl2,
				 chg_status,
				 val.intval,
				 level);
	ret = strlen(buf) + 1;
	return ret;
}

static DEVICE_ATTR(prop, 0444, power_prop_show, NULL);

static struct kobject *power_test_kobj;

static int power_test_sysfs_init(void)
{
	int ret ;
	power_test_kobj = kobject_create_and_add("power_test_prop", NULL);
	if (power_test_kobj == NULL) {
		printk(KERN_ERR
		       "power_test_sysfs_init:"\
		       "subsystem_register failed\n");
		ret = -ENOMEM;
		goto err;
	}
	ret = sysfs_create_file(power_test_kobj, &dev_attr_prop.attr);
	if (ret) {
		printk(KERN_ERR
		       "power_test_sysfs_init:"\
		       "sysfs_create_group failed\n");
		goto err1;
	}

	return 0 ;
err1:
	kobject_del(power_test_kobj);
err:
	return ret ;
}


MODULE_DESCRIPTION("Power supply driver for WM831x PMICs");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm831x-power");
