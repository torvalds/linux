/*
 * Copyright 2012 ST Ericsson.
 *
 * Power supply driver for ST Ericsson pm2xxx_charger charger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/mfd/abx500/ux500_chargalg.h>
#include <linux/pm2301_charger.h>
#include <linux/gpio.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>

#include "pm2301_charger.h"

#define to_pm2xxx_charger_ac_device_info(x) container_of((x), \
		struct pm2xxx_charger, ac_chg)
#define SLEEP_MIN		50
#define SLEEP_MAX		100
#define PM2XXX_AUTOSUSPEND_DELAY 500

static int pm2xxx_interrupt_registers[] = {
	PM2XXX_REG_INT1,
	PM2XXX_REG_INT2,
	PM2XXX_REG_INT3,
	PM2XXX_REG_INT4,
	PM2XXX_REG_INT5,
	PM2XXX_REG_INT6,
};

static enum power_supply_property pm2xxx_charger_ac_props[] = {
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
};

static int pm2xxx_charger_voltage_map[] = {
	3500,
	3525,
	3550,
	3575,
	3600,
	3625,
	3650,
	3675,
	3700,
	3725,
	3750,
	3775,
	3800,
	3825,
	3850,
	3875,
	3900,
	3925,
	3950,
	3975,
	4000,
	4025,
	4050,
	4075,
	4100,
	4125,
	4150,
	4175,
	4200,
	4225,
	4250,
	4275,
	4300,
};

static int pm2xxx_charger_current_map[] = {
	200,
	200,
	400,
	600,
	800,
	1000,
	1200,
	1400,
	1600,
	1800,
	2000,
	2200,
	2400,
	2600,
	2800,
	3000,
};

static const struct i2c_device_id pm2xxx_ident[] = {
	{ "pm2301", 0 },
	{ }
};

static void set_lpn_pin(struct pm2xxx_charger *pm2)
{
	if (!pm2->ac.charger_connected && gpio_is_valid(pm2->lpn_pin)) {
		gpio_set_value(pm2->lpn_pin, 1);
		usleep_range(SLEEP_MIN, SLEEP_MAX);
	}
}

static void clear_lpn_pin(struct pm2xxx_charger *pm2)
{
	if (!pm2->ac.charger_connected && gpio_is_valid(pm2->lpn_pin))
		gpio_set_value(pm2->lpn_pin, 0);
}

static int pm2xxx_reg_read(struct pm2xxx_charger *pm2, int reg, u8 *val)
{
	int ret;

	/* wake up the device */
	pm_runtime_get_sync(pm2->dev);

	ret = i2c_smbus_read_i2c_block_data(pm2->config.pm2xxx_i2c, reg,
				1, val);
	if (ret < 0)
		dev_err(pm2->dev, "Error reading register at 0x%x\n", reg);
	else
		ret = 0;

	pm_runtime_put_sync(pm2->dev);

	return ret;
}

static int pm2xxx_reg_write(struct pm2xxx_charger *pm2, int reg, u8 val)
{
	int ret;

	/* wake up the device */
	pm_runtime_get_sync(pm2->dev);

	ret = i2c_smbus_write_i2c_block_data(pm2->config.pm2xxx_i2c, reg,
				1, &val);
	if (ret < 0)
		dev_err(pm2->dev, "Error writing register at 0x%x\n", reg);
	else
		ret = 0;

	pm_runtime_put_sync(pm2->dev);

	return ret;
}

static int pm2xxx_charging_enable_mngt(struct pm2xxx_charger *pm2)
{
	int ret;

	/* Enable charging */
	ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG2,
			(PM2XXX_CH_AUTO_RESUME_EN | PM2XXX_CHARGER_ENA));

	return ret;
}

static int pm2xxx_charging_disable_mngt(struct pm2xxx_charger *pm2)
{
	int ret;

	/* Disable SW EOC ctrl */
	ret = pm2xxx_reg_write(pm2, PM2XXX_SW_CTRL_REG, PM2XXX_SWCTRL_HW);
	if (ret < 0) {
		dev_err(pm2->dev, "%s pm2xxx write failed\n", __func__);
		return ret;
	}

	/* Disable charging */
	ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG2,
			(PM2XXX_CH_AUTO_RESUME_DIS | PM2XXX_CHARGER_DIS));
	if (ret < 0) {
		dev_err(pm2->dev, "%s pm2xxx write failed\n", __func__);
		return ret;
	}

	return 0;
}

static int pm2xxx_charger_batt_therm_mngt(struct pm2xxx_charger *pm2, int val)
{
	queue_work(pm2->charger_wq, &pm2->check_main_thermal_prot_work);

	return 0;
}


int pm2xxx_charger_die_therm_mngt(struct pm2xxx_charger *pm2, int val)
{
	queue_work(pm2->charger_wq, &pm2->check_main_thermal_prot_work);

	return 0;
}

static int pm2xxx_charger_ovv_mngt(struct pm2xxx_charger *pm2, int val)
{
	dev_err(pm2->dev, "Overvoltage detected\n");
	pm2->flags.ovv = true;
	power_supply_changed(&pm2->ac_chg.psy);

	/* Schedule a new HW failure check */
	queue_delayed_work(pm2->charger_wq, &pm2->check_hw_failure_work, 0);

	return 0;
}

static int pm2xxx_charger_wd_exp_mngt(struct pm2xxx_charger *pm2, int val)
{
	dev_dbg(pm2->dev , "20 minutes watchdog expired\n");

	pm2->ac.wd_expired = true;
	power_supply_changed(&pm2->ac_chg.psy);

	return 0;
}

static int pm2xxx_charger_vbat_lsig_mngt(struct pm2xxx_charger *pm2, int val)
{
	int ret;

	switch (val) {
	case PM2XXX_INT1_ITVBATLOWR:
		dev_dbg(pm2->dev, "VBAT grows above VBAT_LOW level\n");
		/* Enable SW EOC ctrl */
		ret = pm2xxx_reg_write(pm2, PM2XXX_SW_CTRL_REG,
							PM2XXX_SWCTRL_SW);
		if (ret < 0) {
			dev_err(pm2->dev, "%s pm2xxx write failed\n", __func__);
			return ret;
		}
		break;

	case PM2XXX_INT1_ITVBATLOWF:
		dev_dbg(pm2->dev, "VBAT drops below VBAT_LOW level\n");
		/* Disable SW EOC ctrl */
		ret = pm2xxx_reg_write(pm2, PM2XXX_SW_CTRL_REG,
							PM2XXX_SWCTRL_HW);
		if (ret < 0) {
			dev_err(pm2->dev, "%s pm2xxx write failed\n", __func__);
			return ret;
		}
		break;

	default:
		dev_err(pm2->dev, "Unknown VBAT level\n");
	}

	return 0;
}

static int pm2xxx_charger_bat_disc_mngt(struct pm2xxx_charger *pm2, int val)
{
	dev_dbg(pm2->dev, "battery disconnected\n");

	return 0;
}

static int pm2xxx_charger_detection(struct pm2xxx_charger *pm2, u8 *val)
{
	int ret;

	ret = pm2xxx_reg_read(pm2, PM2XXX_SRCE_REG_INT2, val);

	if (ret < 0) {
		dev_err(pm2->dev, "Charger detection failed\n");
		goto out;
	}

	*val &= (PM2XXX_INT2_S_ITVPWR1PLUG | PM2XXX_INT2_S_ITVPWR2PLUG);

out:
	return ret;
}

static int pm2xxx_charger_itv_pwr_plug_mngt(struct pm2xxx_charger *pm2, int val)
{

	int ret;
	u8 read_val;

	/*
	 * Since we can't be sure that the events are received
	 * synchronously, we have the check if the main charger is
	 * connected by reading the interrupt source register.
	 */
	ret = pm2xxx_charger_detection(pm2, &read_val);

	if ((ret == 0) && read_val) {
		pm2->ac.charger_connected = 1;
		pm2->ac_conn = true;
		queue_work(pm2->charger_wq, &pm2->ac_work);
	}


	return ret;
}

static int pm2xxx_charger_itv_pwr_unplug_mngt(struct pm2xxx_charger *pm2,
								int val)
{
	pm2->ac.charger_connected = 0;
	queue_work(pm2->charger_wq, &pm2->ac_work);

	return 0;
}

static int pm2_int_reg0(void *pm2_data, int val)
{
	struct pm2xxx_charger *pm2 = pm2_data;
	int ret = 0;

	if (val & PM2XXX_INT1_ITVBATLOWR) {
		ret = pm2xxx_charger_vbat_lsig_mngt(pm2,
						PM2XXX_INT1_ITVBATLOWR);
		if (ret < 0)
			goto out;
	}

	if (val & PM2XXX_INT1_ITVBATLOWF) {
		ret = pm2xxx_charger_vbat_lsig_mngt(pm2,
						PM2XXX_INT1_ITVBATLOWF);
		if (ret < 0)
			goto out;
	}

	if (val & PM2XXX_INT1_ITVBATDISCONNECT) {
		ret = pm2xxx_charger_bat_disc_mngt(pm2,
				PM2XXX_INT1_ITVBATDISCONNECT);
		if (ret < 0)
			goto out;
	}
out:
	return ret;
}

static int pm2_int_reg1(void *pm2_data, int val)
{
	struct pm2xxx_charger *pm2 = pm2_data;
	int ret = 0;

	if (val & (PM2XXX_INT2_ITVPWR1PLUG | PM2XXX_INT2_ITVPWR2PLUG)) {
		dev_dbg(pm2->dev , "Main charger plugged\n");
		ret = pm2xxx_charger_itv_pwr_plug_mngt(pm2, val &
			(PM2XXX_INT2_ITVPWR1PLUG | PM2XXX_INT2_ITVPWR2PLUG));
	}

	if (val &
		(PM2XXX_INT2_ITVPWR1UNPLUG | PM2XXX_INT2_ITVPWR2UNPLUG)) {
		dev_dbg(pm2->dev , "Main charger unplugged\n");
		ret = pm2xxx_charger_itv_pwr_unplug_mngt(pm2, val &
						(PM2XXX_INT2_ITVPWR1UNPLUG |
						PM2XXX_INT2_ITVPWR2UNPLUG));
	}

	return ret;
}

static int pm2_int_reg2(void *pm2_data, int val)
{
	struct pm2xxx_charger *pm2 = pm2_data;
	int ret = 0;

	if (val & PM2XXX_INT3_ITAUTOTIMEOUTWD)
		ret = pm2xxx_charger_wd_exp_mngt(pm2, val);

	if (val & (PM2XXX_INT3_ITCHPRECHARGEWD |
				PM2XXX_INT3_ITCHCCWD | PM2XXX_INT3_ITCHCVWD)) {
		dev_dbg(pm2->dev,
			"Watchdog occured for precharge, CC and CV charge\n");
	}

	return ret;
}

static int pm2_int_reg3(void *pm2_data, int val)
{
	struct pm2xxx_charger *pm2 = pm2_data;
	int ret = 0;

	if (val & (PM2XXX_INT4_ITCHARGINGON)) {
		dev_dbg(pm2->dev ,
			"chargind operation has started\n");
	}

	if (val & (PM2XXX_INT4_ITVRESUME)) {
		dev_dbg(pm2->dev,
			"battery discharged down to VResume threshold\n");
	}

	if (val & (PM2XXX_INT4_ITBATTFULL)) {
		dev_dbg(pm2->dev , "battery fully detected\n");
	}

	if (val & (PM2XXX_INT4_ITCVPHASE)) {
		dev_dbg(pm2->dev, "CV phase enter with 0.5C charging\n");
	}

	if (val & (PM2XXX_INT4_ITVPWR2OVV | PM2XXX_INT4_ITVPWR1OVV)) {
		pm2->failure_case = VPWR_OVV;
		ret = pm2xxx_charger_ovv_mngt(pm2, val &
			(PM2XXX_INT4_ITVPWR2OVV | PM2XXX_INT4_ITVPWR1OVV));
		dev_dbg(pm2->dev, "VPWR/VSYSTEM overvoltage detected\n");
	}

	if (val & (PM2XXX_INT4_S_ITBATTEMPCOLD |
				PM2XXX_INT4_S_ITBATTEMPHOT)) {
		ret = pm2xxx_charger_batt_therm_mngt(pm2, val &
			(PM2XXX_INT4_S_ITBATTEMPCOLD |
			PM2XXX_INT4_S_ITBATTEMPHOT));
		dev_dbg(pm2->dev, "BTEMP is too Low/High\n");
	}

	return ret;
}

static int pm2_int_reg4(void *pm2_data, int val)
{
	struct pm2xxx_charger *pm2 = pm2_data;
	int ret = 0;

	if (val & PM2XXX_INT5_ITVSYSTEMOVV) {
		pm2->failure_case = VSYSTEM_OVV;
		ret = pm2xxx_charger_ovv_mngt(pm2, val &
						PM2XXX_INT5_ITVSYSTEMOVV);
		dev_dbg(pm2->dev, "VSYSTEM overvoltage detected\n");
	}

	if (val & (PM2XXX_INT5_ITTHERMALWARNINGFALL |
				PM2XXX_INT5_ITTHERMALWARNINGRISE |
				PM2XXX_INT5_ITTHERMALSHUTDOWNFALL |
				PM2XXX_INT5_ITTHERMALSHUTDOWNRISE)) {
		dev_dbg(pm2->dev, "BTEMP die temperature is too Low/High\n");
		ret = pm2xxx_charger_die_therm_mngt(pm2, val &
			(PM2XXX_INT5_ITTHERMALWARNINGFALL |
			PM2XXX_INT5_ITTHERMALWARNINGRISE |
			PM2XXX_INT5_ITTHERMALSHUTDOWNFALL |
			PM2XXX_INT5_ITTHERMALSHUTDOWNRISE));
	}

	return ret;
}

static int pm2_int_reg5(void *pm2_data, int val)
{
	struct pm2xxx_charger *pm2 = pm2_data;
	int ret = 0;

	if (val & (PM2XXX_INT6_ITVPWR2DROP | PM2XXX_INT6_ITVPWR1DROP)) {
		dev_dbg(pm2->dev, "VMPWR drop to VBAT level\n");
	}

	if (val & (PM2XXX_INT6_ITVPWR2VALIDRISE |
			PM2XXX_INT6_ITVPWR1VALIDRISE |
			PM2XXX_INT6_ITVPWR2VALIDFALL |
			PM2XXX_INT6_ITVPWR1VALIDFALL)) {
		dev_dbg(pm2->dev, "Falling/Rising edge on WPWR1/2\n");
	}

	return ret;
}

static irqreturn_t  pm2xxx_irq_int(int irq, void *data)
{
	struct pm2xxx_charger *pm2 = data;
	struct pm2xxx_interrupts *interrupt = pm2->pm2_int;
	int i;

	/* wake up the device */
	pm_runtime_get_sync(pm2->dev);

	do {
		for (i = 0; i < PM2XXX_NUM_INT_REG; i++) {
			pm2xxx_reg_read(pm2,
				pm2xxx_interrupt_registers[i],
				&(interrupt->reg[i]));

			if (interrupt->reg[i] > 0)
				interrupt->handler[i](pm2, interrupt->reg[i]);
		}
	} while (gpio_get_value(pm2->pdata->gpio_irq_number) == 0);

	pm_runtime_mark_last_busy(pm2->dev);
	pm_runtime_put_autosuspend(pm2->dev);

	return IRQ_HANDLED;
}

static int pm2xxx_charger_get_ac_cv(struct pm2xxx_charger *pm2)
{
	int ret = 0;
	u8 val;

	if (pm2->ac.charger_connected && pm2->ac.charger_online) {

		ret = pm2xxx_reg_read(pm2, PM2XXX_SRCE_REG_INT4, &val);
		if (ret < 0) {
			dev_err(pm2->dev, "%s pm2xxx read failed\n", __func__);
			goto out;
		}

		if (val & PM2XXX_INT4_S_ITCVPHASE)
			ret = PM2XXX_CONST_VOLT;
		else
			ret = PM2XXX_CONST_CURR;
	}
out:
	return ret;
}

static int pm2xxx_current_to_regval(int curr)
{
	int i;

	if (curr < pm2xxx_charger_current_map[0])
		return 0;

	for (i = 1; i < ARRAY_SIZE(pm2xxx_charger_current_map); i++) {
		if (curr < pm2xxx_charger_current_map[i])
			return (i - 1);
	}

	i = ARRAY_SIZE(pm2xxx_charger_current_map) - 1;
	if (curr == pm2xxx_charger_current_map[i])
		return i;
	else
		return -EINVAL;
}

static int pm2xxx_voltage_to_regval(int curr)
{
	int i;

	if (curr < pm2xxx_charger_voltage_map[0])
		return 0;

	for (i = 1; i < ARRAY_SIZE(pm2xxx_charger_voltage_map); i++) {
		if (curr < pm2xxx_charger_voltage_map[i])
			return i - 1;
	}

	i = ARRAY_SIZE(pm2xxx_charger_voltage_map) - 1;
	if (curr == pm2xxx_charger_voltage_map[i])
		return i;
	else
		return -EINVAL;
}

static int pm2xxx_charger_update_charger_current(struct ux500_charger *charger,
		int ich_out)
{
	int ret;
	int curr_index;
	struct pm2xxx_charger *pm2;
	u8 val;

	if (charger->psy.type == POWER_SUPPLY_TYPE_MAINS)
		pm2 = to_pm2xxx_charger_ac_device_info(charger);
	else
		return -ENXIO;

	curr_index = pm2xxx_current_to_regval(ich_out);
	if (curr_index < 0) {
		dev_err(pm2->dev,
			"Charger current too high, charging not started\n");
		return -ENXIO;
	}

	ret = pm2xxx_reg_read(pm2, PM2XXX_BATT_CTRL_REG6, &val);
	if (ret >= 0) {
		val &= ~PM2XXX_DIR_CH_CC_CURRENT_MASK;
		val |= curr_index;
		ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG6, val);
		if (ret < 0) {
			dev_err(pm2->dev,
				"%s write failed\n", __func__);
		}
	}
	else
		dev_err(pm2->dev, "%s read failed\n", __func__);

	return ret;
}

static int pm2xxx_charger_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct pm2xxx_charger *pm2;

	pm2 = to_pm2xxx_charger_ac_device_info(psy_to_ux500_charger(psy));

	switch (psp) {
	case POWER_SUPPLY_PROP_HEALTH:
		if (pm2->flags.mainextchnotok)
			val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
		else if (pm2->ac.wd_expired)
			val->intval = POWER_SUPPLY_HEALTH_DEAD;
		else if (pm2->flags.main_thermal_prot)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (pm2->flags.ovv)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = pm2->ac.charger_online;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = pm2->ac.charger_connected;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		pm2->ac.cv_active = pm2xxx_charger_get_ac_cv(pm2);
		val->intval = pm2->ac.cv_active;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int pm2xxx_charging_init(struct pm2xxx_charger *pm2)
{
	int ret = 0;

	/* enable CC and CV watchdog */
	ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG3,
		(PM2XXX_CH_WD_CV_PHASE_60MIN | PM2XXX_CH_WD_CC_PHASE_60MIN));
	if( ret < 0)
		return ret;

	/* enable precharge watchdog */
	ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG4,
					PM2XXX_CH_WD_PRECH_PHASE_60MIN);

	/* Disable auto timeout */
	ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG5,
					PM2XXX_CH_WD_AUTO_TIMEOUT_20MIN);

	/*
     * EOC current level = 100mA
	 * Precharge current level = 100mA
	 * CC current level = 1000mA
	 */
	ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG6,
		(PM2XXX_DIR_CH_CC_CURRENT_1000MA |
		PM2XXX_CH_PRECH_CURRENT_100MA |
		PM2XXX_CH_EOC_CURRENT_100MA));

	/*
     * recharge threshold = 3.8V
	 * Precharge to CC threshold = 2.9V
	 */
	ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG7,
		(PM2XXX_CH_PRECH_VOL_2_9 | PM2XXX_CH_VRESUME_VOL_3_8));

	/* float voltage charger level = 4.2V */
	ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG8,
		PM2XXX_CH_VOLT_4_2);

	/* Voltage drop between VBAT and VSYS in HW charging = 300mV */
	ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG9,
		(PM2XXX_CH_150MV_DROP_300MV | PM2XXX_CHARCHING_INFO_DIS |
		PM2XXX_CH_CC_REDUCED_CURRENT_IDENT |
		PM2XXX_CH_CC_MODEDROP_DIS));

	/* Input charger level of over voltage = 10V */
	ret = pm2xxx_reg_write(pm2, PM2XXX_INP_VOLT_VPWR2,
					PM2XXX_VPWR2_OVV_10);
	ret = pm2xxx_reg_write(pm2, PM2XXX_INP_VOLT_VPWR1,
					PM2XXX_VPWR1_OVV_10);

	/* Input charger drop */
	ret = pm2xxx_reg_write(pm2, PM2XXX_INP_DROP_VPWR2,
		(PM2XXX_VPWR2_HW_OPT_DIS | PM2XXX_VPWR2_VALID_DIS |
		PM2XXX_VPWR2_DROP_DIS));
	ret = pm2xxx_reg_write(pm2, PM2XXX_INP_DROP_VPWR1,
		(PM2XXX_VPWR1_HW_OPT_DIS | PM2XXX_VPWR1_VALID_DIS |
		PM2XXX_VPWR1_DROP_DIS));

	/* Disable battery low monitoring */
	ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_LOW_LEV_COMP_REG,
		PM2XXX_VBAT_LOW_MONITORING_ENA);

	return ret;
}

static int pm2xxx_charger_ac_en(struct ux500_charger *charger,
	int enable, int vset, int iset)
{
	int ret;
	int volt_index;
	int curr_index;
	u8 val;

	struct pm2xxx_charger *pm2 = to_pm2xxx_charger_ac_device_info(charger);

	if (enable) {
		if (!pm2->ac.charger_connected) {
			dev_dbg(pm2->dev, "AC charger not connected\n");
			return -ENXIO;
		}

		dev_dbg(pm2->dev, "Enable AC: %dmV %dmA\n", vset, iset);
		if (!pm2->vddadc_en_ac) {
			regulator_enable(pm2->regu);
			pm2->vddadc_en_ac = true;
		}

		ret = pm2xxx_charging_init(pm2);
		if (ret < 0) {
			dev_err(pm2->dev, "%s charging init failed\n",
					__func__);
			goto error_occured;
		}

		volt_index = pm2xxx_voltage_to_regval(vset);
		curr_index = pm2xxx_current_to_regval(iset);

		if (volt_index < 0 || curr_index < 0) {
			dev_err(pm2->dev,
				"Charger voltage or current too high, "
				"charging not started\n");
			return -ENXIO;
		}

		ret = pm2xxx_reg_read(pm2, PM2XXX_BATT_CTRL_REG8, &val);
		if (ret < 0) {
			dev_err(pm2->dev, "%s pm2xxx read failed\n", __func__);
			goto error_occured;
		}
		val &= ~PM2XXX_CH_VOLT_MASK;
		val |= volt_index;
		ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG8, val);
		if (ret < 0) {
			dev_err(pm2->dev, "%s pm2xxx write failed\n", __func__);
			goto error_occured;
		}

		ret = pm2xxx_reg_read(pm2, PM2XXX_BATT_CTRL_REG6, &val);
		if (ret < 0) {
			dev_err(pm2->dev, "%s pm2xxx read failed\n", __func__);
			goto error_occured;
		}
		val &= ~PM2XXX_DIR_CH_CC_CURRENT_MASK;
		val |= curr_index;
		ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_CTRL_REG6, val);
		if (ret < 0) {
			dev_err(pm2->dev, "%s pm2xxx write failed\n", __func__);
			goto error_occured;
		}

		if (!pm2->bat->enable_overshoot) {
			ret = pm2xxx_reg_read(pm2, PM2XXX_LED_CTRL_REG, &val);
			if (ret < 0) {
				dev_err(pm2->dev, "%s pm2xxx read failed\n",
								__func__);
				goto error_occured;
			}
			val |= PM2XXX_ANTI_OVERSHOOT_EN;
			ret = pm2xxx_reg_write(pm2, PM2XXX_LED_CTRL_REG, val);
			if (ret < 0) {
				dev_err(pm2->dev, "%s pm2xxx write failed\n",
								__func__);
				goto error_occured;
			}
		}

		ret = pm2xxx_charging_enable_mngt(pm2);
		if (ret < 0) {
			dev_err(pm2->dev, "Failed to enable"
						"pm2xxx ac charger\n");
			goto error_occured;
		}

		pm2->ac.charger_online = 1;
	} else {
		pm2->ac.charger_online = 0;
		pm2->ac.wd_expired = false;

		/* Disable regulator if enabled */
		if (pm2->vddadc_en_ac) {
			regulator_disable(pm2->regu);
			pm2->vddadc_en_ac = false;
		}

		ret = pm2xxx_charging_disable_mngt(pm2);
		if (ret < 0) {
			dev_err(pm2->dev, "failed to disable"
						"pm2xxx ac charger\n");
			goto error_occured;
		}

		dev_dbg(pm2->dev, "PM2301: " "Disabled AC charging\n");
	}
	power_supply_changed(&pm2->ac_chg.psy);

error_occured:
	return ret;
}

static int pm2xxx_charger_watchdog_kick(struct ux500_charger *charger)
{
	int ret;
	struct pm2xxx_charger *pm2;

	if (charger->psy.type == POWER_SUPPLY_TYPE_MAINS)
		pm2 = to_pm2xxx_charger_ac_device_info(charger);
	else
		return -ENXIO;

	ret = pm2xxx_reg_write(pm2, PM2XXX_BATT_WD_KICK, WD_TIMER);
	if (ret)
		dev_err(pm2->dev, "Failed to kick WD!\n");

	return ret;
}

static void pm2xxx_charger_ac_work(struct work_struct *work)
{
	struct pm2xxx_charger *pm2 = container_of(work,
		struct pm2xxx_charger, ac_work);


	power_supply_changed(&pm2->ac_chg.psy);
	sysfs_notify(&pm2->ac_chg.psy.dev->kobj, NULL, "present");
};

static void pm2xxx_charger_check_hw_failure_work(struct work_struct *work)
{
	u8 reg_value;

	struct pm2xxx_charger *pm2 = container_of(work,
		struct pm2xxx_charger, check_hw_failure_work.work);

	if (pm2->flags.ovv) {
		pm2xxx_reg_read(pm2, PM2XXX_SRCE_REG_INT4, &reg_value);

		if (!(reg_value & (PM2XXX_INT4_S_ITVPWR1OVV |
					PM2XXX_INT4_S_ITVPWR2OVV))) {
			pm2->flags.ovv = false;
			power_supply_changed(&pm2->ac_chg.psy);
		}
	}

	/* If we still have a failure, schedule a new check */
	if (pm2->flags.ovv) {
		queue_delayed_work(pm2->charger_wq,
			&pm2->check_hw_failure_work, round_jiffies(HZ));
	}
}

static void pm2xxx_charger_check_main_thermal_prot_work(
	struct work_struct *work)
{
	int ret;
	u8 val;

	struct pm2xxx_charger *pm2 = container_of(work, struct pm2xxx_charger,
					check_main_thermal_prot_work);

	/* Check if die temp warning is still active */
	ret = pm2xxx_reg_read(pm2, PM2XXX_SRCE_REG_INT5, &val);
	if (ret < 0) {
		dev_err(pm2->dev, "%s pm2xxx read failed\n", __func__);
		return;
	}
	if (val & (PM2XXX_INT5_S_ITTHERMALWARNINGRISE
			| PM2XXX_INT5_S_ITTHERMALSHUTDOWNRISE))
		pm2->flags.main_thermal_prot = true;
	else if (val & (PM2XXX_INT5_S_ITTHERMALWARNINGFALL
				| PM2XXX_INT5_S_ITTHERMALSHUTDOWNFALL))
		pm2->flags.main_thermal_prot = false;

	power_supply_changed(&pm2->ac_chg.psy);
}

static struct pm2xxx_interrupts pm2xxx_int = {
	.handler[0] = pm2_int_reg0,
	.handler[1] = pm2_int_reg1,
	.handler[2] = pm2_int_reg2,
	.handler[3] = pm2_int_reg3,
	.handler[4] = pm2_int_reg4,
	.handler[5] = pm2_int_reg5,
};

static struct pm2xxx_irq pm2xxx_charger_irq[] = {
	{"PM2XXX_IRQ_INT", pm2xxx_irq_int},
};

#ifdef CONFIG_PM

#ifdef CONFIG_PM_SLEEP

static int pm2xxx_wall_charger_resume(struct device *dev)
{
	struct i2c_client *i2c_client = to_i2c_client(dev);
	struct pm2xxx_charger *pm2;

	pm2 =  (struct pm2xxx_charger *)i2c_get_clientdata(i2c_client);
	set_lpn_pin(pm2);

	/* If we still have a HW failure, schedule a new check */
	if (pm2->flags.ovv)
		queue_delayed_work(pm2->charger_wq,
				&pm2->check_hw_failure_work, 0);

	return 0;
}

static int pm2xxx_wall_charger_suspend(struct device *dev)
{
	struct i2c_client *i2c_client = to_i2c_client(dev);
	struct pm2xxx_charger *pm2;

	pm2 =  (struct pm2xxx_charger *)i2c_get_clientdata(i2c_client);
	clear_lpn_pin(pm2);

	/* Cancel any pending HW failure check */
	if (delayed_work_pending(&pm2->check_hw_failure_work))
		cancel_delayed_work(&pm2->check_hw_failure_work);

	flush_work(&pm2->ac_work);
	flush_work(&pm2->check_main_thermal_prot_work);

	return 0;
}

#endif

#ifdef CONFIG_PM_RUNTIME

static int  pm2xxx_runtime_suspend(struct device *dev)
{
	struct i2c_client *pm2xxx_i2c_client = to_i2c_client(dev);
	struct pm2xxx_charger *pm2;
	int ret = 0;

	pm2 = (struct pm2xxx_charger *)i2c_get_clientdata(pm2xxx_i2c_client);
	if (!pm2) {
		dev_err(pm2->dev, "no pm2xxx_charger data supplied\n");
		ret = -EINVAL;
		return ret;
	}

	clear_lpn_pin(pm2);

	return ret;
}

static int  pm2xxx_runtime_resume(struct device *dev)
{
	struct i2c_client *pm2xxx_i2c_client = to_i2c_client(dev);
	struct pm2xxx_charger *pm2;
	int ret = 0;

	pm2 = (struct pm2xxx_charger *)i2c_get_clientdata(pm2xxx_i2c_client);
	if (!pm2) {
		dev_err(pm2->dev, "no pm2xxx_charger data supplied\n");
		ret = -EINVAL;
		return ret;
	}

	if (gpio_is_valid(pm2->lpn_pin) && gpio_get_value(pm2->lpn_pin) == 0)
		set_lpn_pin(pm2);

	return ret;
}

#endif

static const struct dev_pm_ops pm2xxx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm2xxx_wall_charger_suspend,
		pm2xxx_wall_charger_resume)
	SET_RUNTIME_PM_OPS(pm2xxx_runtime_suspend, pm2xxx_runtime_resume, NULL)
};
#define  PM2XXX_PM_OPS (&pm2xxx_pm_ops)
#else
#define  PM2XXX_PM_OPS  NULL
#endif

static int pm2xxx_wall_charger_probe(struct i2c_client *i2c_client,
		const struct i2c_device_id *id)
{
	struct pm2xxx_platform_data *pl_data = i2c_client->dev.platform_data;
	struct pm2xxx_charger *pm2;
	int ret = 0;
	u8 val;
	int i;

	if (!pl_data) {
		dev_err(&i2c_client->dev, "No platform data supplied\n");
		return -EINVAL;
	}

	pm2 = kzalloc(sizeof(struct pm2xxx_charger), GFP_KERNEL);
	if (!pm2) {
		dev_err(&i2c_client->dev, "pm2xxx_charger allocation failed\n");
		return -ENOMEM;
	}

	/* get parent data */
	pm2->dev = &i2c_client->dev;

	pm2->pm2_int = &pm2xxx_int;

	/* get charger spcific platform data */
	if (!pl_data->wall_charger) {
		dev_err(pm2->dev, "no charger platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}

	pm2->pdata = pl_data->wall_charger;

	/* get battery specific platform data */
	if (!pl_data->battery) {
		dev_err(pm2->dev, "no battery platform data supplied\n");
		ret = -EINVAL;
		goto free_device_info;
	}

	pm2->bat = pl_data->battery;

	if (!i2c_check_functionality(i2c_client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA |
			I2C_FUNC_SMBUS_READ_WORD_DATA)) {
		ret = -ENODEV;
		dev_info(pm2->dev, "pm2301 i2c_check_functionality failed\n");
		goto free_device_info;
	}

	pm2->config.pm2xxx_i2c = i2c_client;
	pm2->config.pm2xxx_id = (struct i2c_device_id *) id;
	i2c_set_clientdata(i2c_client, pm2);

	/* AC supply */
	/* power_supply base class */
	pm2->ac_chg.psy.name = pm2->pdata->label;
	pm2->ac_chg.psy.type = POWER_SUPPLY_TYPE_MAINS;
	pm2->ac_chg.psy.properties = pm2xxx_charger_ac_props;
	pm2->ac_chg.psy.num_properties = ARRAY_SIZE(pm2xxx_charger_ac_props);
	pm2->ac_chg.psy.get_property = pm2xxx_charger_ac_get_property;
	pm2->ac_chg.psy.supplied_to = pm2->pdata->supplied_to;
	pm2->ac_chg.psy.num_supplicants = pm2->pdata->num_supplicants;
	/* pm2xxx_charger sub-class */
	pm2->ac_chg.ops.enable = &pm2xxx_charger_ac_en;
	pm2->ac_chg.ops.kick_wd = &pm2xxx_charger_watchdog_kick;
	pm2->ac_chg.ops.update_curr = &pm2xxx_charger_update_charger_current;
	pm2->ac_chg.max_out_volt = pm2xxx_charger_voltage_map[
		ARRAY_SIZE(pm2xxx_charger_voltage_map) - 1];
	pm2->ac_chg.max_out_curr = pm2xxx_charger_current_map[
		ARRAY_SIZE(pm2xxx_charger_current_map) - 1];
	pm2->ac_chg.wdt_refresh = WD_KICK_INTERVAL;
	pm2->ac_chg.enabled = true;
	pm2->ac_chg.external = true;

	/* Create a work queue for the charger */
	pm2->charger_wq = create_singlethread_workqueue("pm2xxx_charger_wq");
	if (pm2->charger_wq == NULL) {
		ret = -ENOMEM;
		dev_err(pm2->dev, "failed to create work queue\n");
		goto free_device_info;
	}

	/* Init work for charger detection */
	INIT_WORK(&pm2->ac_work, pm2xxx_charger_ac_work);

	/* Init work for checking HW status */
	INIT_WORK(&pm2->check_main_thermal_prot_work,
		pm2xxx_charger_check_main_thermal_prot_work);

	/* Init work for HW failure check */
	INIT_DEFERRABLE_WORK(&pm2->check_hw_failure_work,
		pm2xxx_charger_check_hw_failure_work);

	/*
	 * VDD ADC supply needs to be enabled from this driver when there
	 * is a charger connected to avoid erroneous BTEMP_HIGH/LOW
	 * interrupts during charging
	 */
	pm2->regu = regulator_get(pm2->dev, "vddadc");
	if (IS_ERR(pm2->regu)) {
		ret = PTR_ERR(pm2->regu);
		dev_err(pm2->dev, "failed to get vddadc regulator\n");
		goto free_charger_wq;
	}

	/* Register AC charger class */
	ret = power_supply_register(pm2->dev, &pm2->ac_chg.psy);
	if (ret) {
		dev_err(pm2->dev, "failed to register AC charger\n");
		goto free_regulator;
	}

	/* Register interrupts */
	ret = request_threaded_irq(gpio_to_irq(pm2->pdata->gpio_irq_number),
				NULL,
				pm2xxx_charger_irq[0].isr,
				pm2->pdata->irq_type,
				pm2xxx_charger_irq[0].name, pm2);

	if (ret != 0) {
		dev_err(pm2->dev, "failed to request %s IRQ %d: %d\n",
		pm2xxx_charger_irq[0].name,
			gpio_to_irq(pm2->pdata->gpio_irq_number), ret);
		goto unregister_pm2xxx_charger;
	}

	ret = pm_runtime_set_active(pm2->dev);
	if (ret)
		dev_err(pm2->dev, "set active Error\n");

	pm_runtime_enable(pm2->dev);
	pm_runtime_set_autosuspend_delay(pm2->dev, PM2XXX_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(pm2->dev);
	pm_runtime_resume(pm2->dev);

	/* pm interrupt can wake up system */
	ret = enable_irq_wake(gpio_to_irq(pm2->pdata->gpio_irq_number));
	if (ret) {
		dev_err(pm2->dev, "failed to set irq wake\n");
		goto unregister_pm2xxx_interrupt;
	}

	mutex_init(&pm2->lock);

	if (gpio_is_valid(pm2->pdata->lpn_gpio)) {
		/* get lpn GPIO from platform data */
		pm2->lpn_pin = pm2->pdata->lpn_gpio;

		/*
		 * Charger detection mechanism requires pulling up the LPN pin
		 * while i2c communication if Charger is not connected
		 * LPN pin of PM2301 is GPIO60 of AB9540
		 */
		ret = gpio_request(pm2->lpn_pin, "pm2301_lpm_gpio");

		if (ret < 0) {
			dev_err(pm2->dev, "pm2301_lpm_gpio request failed\n");
			goto disable_pm2_irq_wake;
		}
		ret = gpio_direction_output(pm2->lpn_pin, 0);
		if (ret < 0) {
			dev_err(pm2->dev, "pm2301_lpm_gpio direction failed\n");
			goto free_gpio;
		}
		set_lpn_pin(pm2);
	}

	/* read  interrupt registers */
	for (i = 0; i < PM2XXX_NUM_INT_REG; i++)
		pm2xxx_reg_read(pm2,
			pm2xxx_interrupt_registers[i],
			&val);

	ret = pm2xxx_charger_detection(pm2, &val);

	if ((ret == 0) && val) {
		pm2->ac.charger_connected = 1;
		ab8500_override_turn_on_stat(~AB8500_POW_KEY_1_ON,
					     AB8500_MAIN_CH_DET);
		pm2->ac_conn = true;
		power_supply_changed(&pm2->ac_chg.psy);
		sysfs_notify(&pm2->ac_chg.psy.dev->kobj, NULL, "present");
	}

	return 0;

free_gpio:
	if (gpio_is_valid(pm2->lpn_pin))
		gpio_free(pm2->lpn_pin);
disable_pm2_irq_wake:
	disable_irq_wake(gpio_to_irq(pm2->pdata->gpio_irq_number));
unregister_pm2xxx_interrupt:
	/* disable interrupt */
	free_irq(gpio_to_irq(pm2->pdata->gpio_irq_number), pm2);
unregister_pm2xxx_charger:
	/* unregister power supply */
	power_supply_unregister(&pm2->ac_chg.psy);
free_regulator:
	/* disable the regulator */
	regulator_put(pm2->regu);
free_charger_wq:
	destroy_workqueue(pm2->charger_wq);
free_device_info:
	kfree(pm2);

	return ret;
}

static int pm2xxx_wall_charger_remove(struct i2c_client *i2c_client)
{
	struct pm2xxx_charger *pm2 = i2c_get_clientdata(i2c_client);

	/* Disable pm_runtime */
	pm_runtime_disable(pm2->dev);
	/* Disable AC charging */
	pm2xxx_charger_ac_en(&pm2->ac_chg, false, 0, 0);

	/* Disable wake by pm interrupt */
	disable_irq_wake(gpio_to_irq(pm2->pdata->gpio_irq_number));

	/* Disable interrupts */
	free_irq(gpio_to_irq(pm2->pdata->gpio_irq_number), pm2);

	/* Delete the work queue */
	destroy_workqueue(pm2->charger_wq);

	flush_scheduled_work();

	/* disable the regulator */
	regulator_put(pm2->regu);

	power_supply_unregister(&pm2->ac_chg.psy);

	if (gpio_is_valid(pm2->lpn_pin))
		gpio_free(pm2->lpn_pin);

	kfree(pm2);

	return 0;
}

static const struct i2c_device_id pm2xxx_id[] = {
	{ "pm2301", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, pm2xxx_id);

static struct i2c_driver pm2xxx_charger_driver = {
	.probe = pm2xxx_wall_charger_probe,
	.remove = pm2xxx_wall_charger_remove,
	.driver = {
		.name = "pm2xxx-wall_charger",
		.owner = THIS_MODULE,
		.pm = PM2XXX_PM_OPS,
	},
	.id_table = pm2xxx_id,
};

static int __init pm2xxx_charger_init(void)
{
	return i2c_add_driver(&pm2xxx_charger_driver);
}

static void __exit pm2xxx_charger_exit(void)
{
	i2c_del_driver(&pm2xxx_charger_driver);
}

device_initcall_sync(pm2xxx_charger_init);
module_exit(pm2xxx_charger_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Rajkumar kasirajan, Olivier Launay");
MODULE_ALIAS("i2c:pm2xxx-charger");
MODULE_DESCRIPTION("PM2xxx charger management driver");
