/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Battery temperature driver for AB8500
 *
 * License Terms: GNU General Public License v2
 * Author:
 *	Johan Palsson <johan.palsson@stericsson.com>
 *	Karl Komierowski <karl.komierowski@stericsson.com>
 *	Arun R Murthy <arun.murthy@stericsson.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/mfd/core.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/mfd/abx500/ab8500-gpadc.h>

#define VTVOUT_V			1800

#define BTEMP_THERMAL_LOW_LIMIT		-10
#define BTEMP_THERMAL_MED_LIMIT		0
#define BTEMP_THERMAL_HIGH_LIMIT_52	52
#define BTEMP_THERMAL_HIGH_LIMIT_57	57
#define BTEMP_THERMAL_HIGH_LIMIT_62	62

#define BTEMP_BATCTRL_CURR_SRC_7UA	7
#define BTEMP_BATCTRL_CURR_SRC_20UA	20

#define BTEMP_BATCTRL_CURR_SRC_16UA	16
#define BTEMP_BATCTRL_CURR_SRC_18UA	18

#define BTEMP_BATCTRL_CURR_SRC_60UA	60
#define BTEMP_BATCTRL_CURR_SRC_120UA	120

#define to_ab8500_btemp_device_info(x) container_of((x), \
	struct ab8500_btemp, btemp_psy);

/**
 * struct ab8500_btemp_interrupts - ab8500 interrupts
 * @name:	name of the interrupt
 * @isr		function pointer to the isr
 */
struct ab8500_btemp_interrupts {
	char *name;
	irqreturn_t (*isr)(int irq, void *data);
};

struct ab8500_btemp_events {
	bool batt_rem;
	bool btemp_high;
	bool btemp_medhigh;
	bool btemp_lowmed;
	bool btemp_low;
	bool ac_conn;
	bool usb_conn;
};

struct ab8500_btemp_ranges {
	int btemp_high_limit;
	int btemp_med_limit;
	int btemp_low_limit;
};

/**
 * struct ab8500_btemp - ab8500 BTEMP device information
 * @dev:		Pointer to the structure device
 * @node:		List of AB8500 BTEMPs, hence prepared for reentrance
 * @curr_source:	What current source we use, in uA
 * @bat_temp:		Dispatched battery temperature in degree Celcius
 * @prev_bat_temp	Last measured battery temperature in degree Celcius
 * @parent:		Pointer to the struct ab8500
 * @gpadc:		Pointer to the struct gpadc
 * @fg:			Pointer to the struct fg
 * @bm:           	Platform specific battery management information
 * @btemp_psy:		Structure for BTEMP specific battery properties
 * @events:		Structure for information about events triggered
 * @btemp_ranges:	Battery temperature range structure
 * @btemp_wq:		Work queue for measuring the temperature periodically
 * @btemp_periodic_work:	Work for measuring the temperature periodically
 * @initialized:	True if battery id read.
 */
struct ab8500_btemp {
	struct device *dev;
	struct list_head node;
	int curr_source;
	int bat_temp;
	int prev_bat_temp;
	struct ab8500 *parent;
	struct ab8500_gpadc *gpadc;
	struct ab8500_fg *fg;
	struct abx500_bm_data *bm;
	struct power_supply btemp_psy;
	struct ab8500_btemp_events events;
	struct ab8500_btemp_ranges btemp_ranges;
	struct workqueue_struct *btemp_wq;
	struct delayed_work btemp_periodic_work;
	bool initialized;
};

/* BTEMP power supply properties */
static enum power_supply_property ab8500_btemp_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_TEMP,
};

static LIST_HEAD(ab8500_btemp_list);

/**
 * ab8500_btemp_get() - returns a reference to the primary AB8500 BTEMP
 * (i.e. the first BTEMP in the instance list)
 */
struct ab8500_btemp *ab8500_btemp_get(void)
{
	struct ab8500_btemp *btemp;
	btemp = list_first_entry(&ab8500_btemp_list, struct ab8500_btemp, node);

	return btemp;
}
EXPORT_SYMBOL(ab8500_btemp_get);

/**
 * ab8500_btemp_batctrl_volt_to_res() - convert batctrl voltage to resistance
 * @di:		pointer to the ab8500_btemp structure
 * @v_batctrl:	measured batctrl voltage
 * @inst_curr:	measured instant current
 *
 * This function returns the battery resistance that is
 * derived from the BATCTRL voltage.
 * Returns value in Ohms.
 */
static int ab8500_btemp_batctrl_volt_to_res(struct ab8500_btemp *di,
	int v_batctrl, int inst_curr)
{
	int rbs;

	if (is_ab8500_1p1_or_earlier(di->parent)) {
		/*
		 * For ABB cut1.0 and 1.1 BAT_CTRL is internally
		 * connected to 1.8V through a 450k resistor
		 */
		return (450000 * (v_batctrl)) / (1800 - v_batctrl);
	}

	if (di->bm->adc_therm == ABx500_ADC_THERM_BATCTRL) {
		/*
		 * If the battery has internal NTC, we use the current
		 * source to calculate the resistance.
		 */
		rbs = (v_batctrl * 1000
		       - di->bm->gnd_lift_resistance * inst_curr)
		      / di->curr_source;
	} else {
		/*
		 * BAT_CTRL is internally
		 * connected to 1.8V through a 80k resistor
		 */
		rbs = (80000 * (v_batctrl)) / (1800 - v_batctrl);
	}

	return rbs;
}

/**
 * ab8500_btemp_read_batctrl_voltage() - measure batctrl voltage
 * @di:		pointer to the ab8500_btemp structure
 *
 * This function returns the voltage on BATCTRL. Returns value in mV.
 */
static int ab8500_btemp_read_batctrl_voltage(struct ab8500_btemp *di)
{
	int vbtemp;
	static int prev;

	vbtemp = ab8500_gpadc_convert(di->gpadc, BAT_CTRL);
	if (vbtemp < 0) {
		dev_err(di->dev,
			"%s gpadc conversion failed, using previous value",
			__func__);
		return prev;
	}
	prev = vbtemp;
	return vbtemp;
}

/**
 * ab8500_btemp_curr_source_enable() - enable/disable batctrl current source
 * @di:		pointer to the ab8500_btemp structure
 * @enable:	enable or disable the current source
 *
 * Enable or disable the current sources for the BatCtrl AD channel
 */
static int ab8500_btemp_curr_source_enable(struct ab8500_btemp *di,
	bool enable)
{
	int curr;
	int ret = 0;

	/*
	 * BATCTRL current sources are included on AB8500 cut2.0
	 * and future versions
	 */
	if (is_ab8500_1p1_or_earlier(di->parent))
		return 0;

	/* Only do this for batteries with internal NTC */
	if (di->bm->adc_therm == ABx500_ADC_THERM_BATCTRL && enable) {

		if (is_ab8540(di->parent)) {
			if (di->curr_source == BTEMP_BATCTRL_CURR_SRC_60UA)
				curr = BAT_CTRL_60U_ENA;
			else
				curr = BAT_CTRL_120U_ENA;
		} else if (is_ab9540(di->parent) || is_ab8505(di->parent)) {
			if (di->curr_source == BTEMP_BATCTRL_CURR_SRC_16UA)
				curr = BAT_CTRL_16U_ENA;
			else
				curr = BAT_CTRL_18U_ENA;
		} else {
			if (di->curr_source == BTEMP_BATCTRL_CURR_SRC_7UA)
				curr = BAT_CTRL_7U_ENA;
			else
				curr = BAT_CTRL_20U_ENA;
		}

		dev_dbg(di->dev, "Set BATCTRL %duA\n", di->curr_source);

		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			FORCE_BAT_CTRL_CMP_HIGH, FORCE_BAT_CTRL_CMP_HIGH);
		if (ret) {
			dev_err(di->dev, "%s failed setting cmp_force\n",
				__func__);
			return ret;
		}

		/*
		 * We have to wait one 32kHz cycle before enabling
		 * the current source, since ForceBatCtrlCmpHigh needs
		 * to be written in a separate cycle
		 */
		udelay(32);

		ret = abx500_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			FORCE_BAT_CTRL_CMP_HIGH | curr);
		if (ret) {
			dev_err(di->dev, "%s failed enabling current source\n",
				__func__);
			goto disable_curr_source;
		}
	} else if (di->bm->adc_therm == ABx500_ADC_THERM_BATCTRL && !enable) {
		dev_dbg(di->dev, "Disable BATCTRL curr source\n");

		if (is_ab8540(di->parent)) {
			/* Write 0 to the curr bits */
			ret = abx500_mask_and_set_register_interruptible(
				di->dev,
				AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
				BAT_CTRL_60U_ENA | BAT_CTRL_120U_ENA,
				~(BAT_CTRL_60U_ENA | BAT_CTRL_120U_ENA));
		} else if (is_ab9540(di->parent) || is_ab8505(di->parent)) {
			/* Write 0 to the curr bits */
			ret = abx500_mask_and_set_register_interruptible(
				di->dev,
				AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
				BAT_CTRL_16U_ENA | BAT_CTRL_18U_ENA,
				~(BAT_CTRL_16U_ENA | BAT_CTRL_18U_ENA));
		} else {
			/* Write 0 to the curr bits */
			ret = abx500_mask_and_set_register_interruptible(
				di->dev,
				AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
				BAT_CTRL_7U_ENA | BAT_CTRL_20U_ENA,
				~(BAT_CTRL_7U_ENA | BAT_CTRL_20U_ENA));
		}

		if (ret) {
			dev_err(di->dev, "%s failed disabling current source\n",
				__func__);
			goto disable_curr_source;
		}

		/* Enable Pull-Up and comparator */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER,	AB8500_BAT_CTRL_CURRENT_SOURCE,
			BAT_CTRL_PULL_UP_ENA | BAT_CTRL_CMP_ENA,
			BAT_CTRL_PULL_UP_ENA | BAT_CTRL_CMP_ENA);
		if (ret) {
			dev_err(di->dev, "%s failed enabling PU and comp\n",
				__func__);
			goto enable_pu_comp;
		}

		/*
		 * We have to wait one 32kHz cycle before disabling
		 * ForceBatCtrlCmpHigh since this needs to be written
		 * in a separate cycle
		 */
		udelay(32);

		/* Disable 'force comparator' */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			FORCE_BAT_CTRL_CMP_HIGH, ~FORCE_BAT_CTRL_CMP_HIGH);
		if (ret) {
			dev_err(di->dev, "%s failed disabling force comp\n",
				__func__);
			goto disable_force_comp;
		}
	}
	return ret;

	/*
	 * We have to try unsetting FORCE_BAT_CTRL_CMP_HIGH one more time
	 * if we got an error above
	 */
disable_curr_source:
	if (is_ab8540(di->parent)) {
		/* Write 0 to the curr bits */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			BAT_CTRL_60U_ENA | BAT_CTRL_120U_ENA,
			~(BAT_CTRL_60U_ENA | BAT_CTRL_120U_ENA));
	} else if (is_ab9540(di->parent) || is_ab8505(di->parent)) {
		/* Write 0 to the curr bits */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			BAT_CTRL_16U_ENA | BAT_CTRL_18U_ENA,
			~(BAT_CTRL_16U_ENA | BAT_CTRL_18U_ENA));
	} else {
		/* Write 0 to the curr bits */
		ret = abx500_mask_and_set_register_interruptible(di->dev,
			AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
			BAT_CTRL_7U_ENA | BAT_CTRL_20U_ENA,
			~(BAT_CTRL_7U_ENA | BAT_CTRL_20U_ENA));
	}

	if (ret) {
		dev_err(di->dev, "%s failed disabling current source\n",
			__func__);
		return ret;
	}
enable_pu_comp:
	/* Enable Pull-Up and comparator */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_CHARGER,	AB8500_BAT_CTRL_CURRENT_SOURCE,
		BAT_CTRL_PULL_UP_ENA | BAT_CTRL_CMP_ENA,
		BAT_CTRL_PULL_UP_ENA | BAT_CTRL_CMP_ENA);
	if (ret) {
		dev_err(di->dev, "%s failed enabling PU and comp\n",
			__func__);
		return ret;
	}

disable_force_comp:
	/*
	 * We have to wait one 32kHz cycle before disabling
	 * ForceBatCtrlCmpHigh since this needs to be written
	 * in a separate cycle
	 */
	udelay(32);

	/* Disable 'force comparator' */
	ret = abx500_mask_and_set_register_interruptible(di->dev,
		AB8500_CHARGER, AB8500_BAT_CTRL_CURRENT_SOURCE,
		FORCE_BAT_CTRL_CMP_HIGH, ~FORCE_BAT_CTRL_CMP_HIGH);
	if (ret) {
		dev_err(di->dev, "%s failed disabling force comp\n",
			__func__);
		return ret;
	}

	return ret;
}

/**
 * ab8500_btemp_get_batctrl_res() - get battery resistance
 * @di:		pointer to the ab8500_btemp structure
 *
 * This function returns the battery pack identification resistance.
 * Returns value in Ohms.
 */
static int ab8500_btemp_get_batctrl_res(struct ab8500_btemp *di)
{
	int ret;
	int batctrl = 0;
	int res;
	int inst_curr;
	int i;

	/*
	 * BATCTRL current sources are included on AB8500 cut2.0
	 * and future versions
	 */
	ret = ab8500_btemp_curr_source_enable(di, true);
	if (ret) {
		dev_err(di->dev, "%s curr source enabled failed\n", __func__);
		return ret;
	}

	if (!di->fg)
		di->fg = ab8500_fg_get();
	if (!di->fg) {
		dev_err(di->dev, "No fg found\n");
		return -EINVAL;
	}

	ret = ab8500_fg_inst_curr_start(di->fg);

	if (ret) {
		dev_err(di->dev, "Failed to start current measurement\n");
		return ret;
	}

	do {
		msleep(20);
	} while (!ab8500_fg_inst_curr_started(di->fg));

	i = 0;

	do {
		batctrl += ab8500_btemp_read_batctrl_voltage(di);
		i++;
		msleep(20);
	} while (!ab8500_fg_inst_curr_done(di->fg));
	batctrl /= i;

	ret = ab8500_fg_inst_curr_finalize(di->fg, &inst_curr);
	if (ret) {
		dev_err(di->dev, "Failed to finalize current measurement\n");
		return ret;
	}

	res = ab8500_btemp_batctrl_volt_to_res(di, batctrl, inst_curr);

	ret = ab8500_btemp_curr_source_enable(di, false);
	if (ret) {
		dev_err(di->dev, "%s curr source disable failed\n", __func__);
		return ret;
	}

	dev_dbg(di->dev, "%s batctrl: %d res: %d inst_curr: %d samples: %d\n",
		__func__, batctrl, res, inst_curr, i);

	return res;
}

/**
 * ab8500_btemp_res_to_temp() - resistance to temperature
 * @di:		pointer to the ab8500_btemp structure
 * @tbl:	pointer to the resiatance to temperature table
 * @tbl_size:	size of the resistance to temperature table
 * @res:	resistance to calculate the temperature from
 *
 * This function returns the battery temperature in degrees Celcius
 * based on the NTC resistance.
 */
static int ab8500_btemp_res_to_temp(struct ab8500_btemp *di,
	const struct abx500_res_to_temp *tbl, int tbl_size, int res)
{
	int i, temp;
	/*
	 * Calculate the formula for the straight line
	 * Simple interpolation if we are within
	 * the resistance table limits, extrapolate
	 * if resistance is outside the limits.
	 */
	if (res > tbl[0].resist)
		i = 0;
	else if (res <= tbl[tbl_size - 1].resist)
		i = tbl_size - 2;
	else {
		i = 0;
		while (!(res <= tbl[i].resist &&
			res > tbl[i + 1].resist))
			i++;
	}

	temp = tbl[i].temp + ((tbl[i + 1].temp - tbl[i].temp) *
		(res - tbl[i].resist)) / (tbl[i + 1].resist - tbl[i].resist);
	return temp;
}

/**
 * ab8500_btemp_measure_temp() - measure battery temperature
 * @di:		pointer to the ab8500_btemp structure
 *
 * Returns battery temperature (on success) else the previous temperature
 */
static int ab8500_btemp_measure_temp(struct ab8500_btemp *di)
{
	int temp;
	static int prev;
	int rbat, rntc, vntc;
	u8 id;

	id = di->bm->batt_id;

	if (di->bm->adc_therm == ABx500_ADC_THERM_BATCTRL &&
			id != BATTERY_UNKNOWN) {

		rbat = ab8500_btemp_get_batctrl_res(di);
		if (rbat < 0) {
			dev_err(di->dev, "%s get batctrl res failed\n",
				__func__);
			/*
			 * Return out-of-range temperature so that
			 * charging is stopped
			 */
			return BTEMP_THERMAL_LOW_LIMIT;
		}

		temp = ab8500_btemp_res_to_temp(di,
			di->bm->bat_type[id].r_to_t_tbl,
			di->bm->bat_type[id].n_temp_tbl_elements, rbat);
	} else {
		vntc = ab8500_gpadc_convert(di->gpadc, BTEMP_BALL);
		if (vntc < 0) {
			dev_err(di->dev,
				"%s gpadc conversion failed,"
				" using previous value\n", __func__);
			return prev;
		}
		/*
		 * The PCB NTC is sourced from VTVOUT via a 230kOhm
		 * resistor.
		 */
		rntc = 230000 * vntc / (VTVOUT_V - vntc);

		temp = ab8500_btemp_res_to_temp(di,
			di->bm->bat_type[id].r_to_t_tbl,
			di->bm->bat_type[id].n_temp_tbl_elements, rntc);
		prev = temp;
	}
	dev_dbg(di->dev, "Battery temperature is %d\n", temp);
	return temp;
}

/**
 * ab8500_btemp_id() - Identify the connected battery
 * @di:		pointer to the ab8500_btemp structure
 *
 * This function will try to identify the battery by reading the ID
 * resistor. Some brands use a combined ID resistor with a NTC resistor to
 * both be able to identify and to read the temperature of it.
 */
static int ab8500_btemp_id(struct ab8500_btemp *di)
{
	int res;
	u8 i;
	if (is_ab8540(di->parent))
		di->curr_source = BTEMP_BATCTRL_CURR_SRC_60UA;
	else if (is_ab9540(di->parent) || is_ab8505(di->parent))
		di->curr_source = BTEMP_BATCTRL_CURR_SRC_16UA;
	else
		di->curr_source = BTEMP_BATCTRL_CURR_SRC_7UA;

	di->bm->batt_id = BATTERY_UNKNOWN;

	res =  ab8500_btemp_get_batctrl_res(di);
	if (res < 0) {
		dev_err(di->dev, "%s get batctrl res failed\n", __func__);
		return -ENXIO;
	}

	/* BATTERY_UNKNOWN is defined on position 0, skip it! */
	for (i = BATTERY_UNKNOWN + 1; i < di->bm->n_btypes; i++) {
		if ((res <= di->bm->bat_type[i].resis_high) &&
			(res >= di->bm->bat_type[i].resis_low)) {
			dev_dbg(di->dev, "Battery detected on %s"
				" low %d < res %d < high: %d"
				" index: %d\n",
				di->bm->adc_therm == ABx500_ADC_THERM_BATCTRL ?
				"BATCTRL" : "BATTEMP",
				di->bm->bat_type[i].resis_low, res,
				di->bm->bat_type[i].resis_high, i);

			di->bm->batt_id = i;
			break;
		}
	}

	if (di->bm->batt_id == BATTERY_UNKNOWN) {
		dev_warn(di->dev, "Battery identified as unknown"
			", resistance %d Ohm\n", res);
		return -ENXIO;
	}

	/*
	 * We only have to change current source if the
	 * detected type is Type 1.
	 */
	if (di->bm->adc_therm == ABx500_ADC_THERM_BATCTRL &&
	    di->bm->batt_id == 1) {
		if (is_ab8540(di->parent)) {
			dev_dbg(di->dev,
				"Set BATCTRL current source to 60uA\n");
			di->curr_source = BTEMP_BATCTRL_CURR_SRC_60UA;
		} else if (is_ab9540(di->parent) || is_ab8505(di->parent)) {
			dev_dbg(di->dev,
				"Set BATCTRL current source to 16uA\n");
			di->curr_source = BTEMP_BATCTRL_CURR_SRC_16UA;
		} else {
			dev_dbg(di->dev, "Set BATCTRL current source to 20uA\n");
			di->curr_source = BTEMP_BATCTRL_CURR_SRC_20UA;
		}
	}

	return di->bm->batt_id;
}

/**
 * ab8500_btemp_periodic_work() - Measuring the temperature periodically
 * @work:	pointer to the work_struct structure
 *
 * Work function for measuring the temperature periodically
 */
static void ab8500_btemp_periodic_work(struct work_struct *work)
{
	int interval;
	int bat_temp;
	struct ab8500_btemp *di = container_of(work,
		struct ab8500_btemp, btemp_periodic_work.work);

	if (!di->initialized) {
		/* Identify the battery */
		if (ab8500_btemp_id(di) < 0)
			dev_warn(di->dev, "failed to identify the battery\n");
	}

	bat_temp = ab8500_btemp_measure_temp(di);
	/*
	 * Filter battery temperature.
	 * Allow direct updates on temperature only if two samples result in
	 * same temperature. Else only allow 1 degree change from previous
	 * reported value in the direction of the new measurement.
	 */
	if ((bat_temp == di->prev_bat_temp) || !di->initialized) {
		if ((di->bat_temp != di->prev_bat_temp) || !di->initialized) {
			di->initialized = true;
			di->bat_temp = bat_temp;
			power_supply_changed(&di->btemp_psy);
		}
	} else if (bat_temp < di->prev_bat_temp) {
		di->bat_temp--;
		power_supply_changed(&di->btemp_psy);
	} else if (bat_temp > di->prev_bat_temp) {
		di->bat_temp++;
		power_supply_changed(&di->btemp_psy);
	}
	di->prev_bat_temp = bat_temp;

	if (di->events.ac_conn || di->events.usb_conn)
		interval = di->bm->temp_interval_chg;
	else
		interval = di->bm->temp_interval_nochg;

	/* Schedule a new measurement */
	queue_delayed_work(di->btemp_wq,
		&di->btemp_periodic_work,
		round_jiffies(interval * HZ));
}

/**
 * ab8500_btemp_batctrlindb_handler() - battery removal detected
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab8500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_btemp_batctrlindb_handler(int irq, void *_di)
{
	struct ab8500_btemp *di = _di;
	dev_err(di->dev, "Battery removal detected!\n");

	di->events.batt_rem = true;
	power_supply_changed(&di->btemp_psy);

	return IRQ_HANDLED;
}

/**
 * ab8500_btemp_templow_handler() - battery temp lower than 10 degrees
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab8500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_btemp_templow_handler(int irq, void *_di)
{
	struct ab8500_btemp *di = _di;

	if (is_ab8500_3p3_or_earlier(di->parent)) {
		dev_dbg(di->dev, "Ignore false btemp low irq"
			" for ABB cut 1.0, 1.1, 2.0 and 3.3\n");
	} else {
		dev_crit(di->dev, "Battery temperature lower than -10deg c\n");

		di->events.btemp_low = true;
		di->events.btemp_high = false;
		di->events.btemp_medhigh = false;
		di->events.btemp_lowmed = false;
		power_supply_changed(&di->btemp_psy);
	}

	return IRQ_HANDLED;
}

/**
 * ab8500_btemp_temphigh_handler() - battery temp higher than max temp
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab8500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_btemp_temphigh_handler(int irq, void *_di)
{
	struct ab8500_btemp *di = _di;

	dev_crit(di->dev, "Battery temperature is higher than MAX temp\n");

	di->events.btemp_high = true;
	di->events.btemp_medhigh = false;
	di->events.btemp_lowmed = false;
	di->events.btemp_low = false;
	power_supply_changed(&di->btemp_psy);

	return IRQ_HANDLED;
}

/**
 * ab8500_btemp_lowmed_handler() - battery temp between low and medium
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab8500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_btemp_lowmed_handler(int irq, void *_di)
{
	struct ab8500_btemp *di = _di;

	dev_dbg(di->dev, "Battery temperature is between low and medium\n");

	di->events.btemp_lowmed = true;
	di->events.btemp_medhigh = false;
	di->events.btemp_high = false;
	di->events.btemp_low = false;
	power_supply_changed(&di->btemp_psy);

	return IRQ_HANDLED;
}

/**
 * ab8500_btemp_medhigh_handler() - battery temp between medium and high
 * @irq:       interrupt number
 * @_di:       void pointer that has to address of ab8500_btemp
 *
 * Returns IRQ status(IRQ_HANDLED)
 */
static irqreturn_t ab8500_btemp_medhigh_handler(int irq, void *_di)
{
	struct ab8500_btemp *di = _di;

	dev_dbg(di->dev, "Battery temperature is between medium and high\n");

	di->events.btemp_medhigh = true;
	di->events.btemp_lowmed = false;
	di->events.btemp_high = false;
	di->events.btemp_low = false;
	power_supply_changed(&di->btemp_psy);

	return IRQ_HANDLED;
}

/**
 * ab8500_btemp_periodic() - Periodic temperature measurements
 * @di:		pointer to the ab8500_btemp structure
 * @enable:	enable or disable periodic temperature measurements
 *
 * Starts of stops periodic temperature measurements. Periodic measurements
 * should only be done when a charger is connected.
 */
static void ab8500_btemp_periodic(struct ab8500_btemp *di,
	bool enable)
{
	dev_dbg(di->dev, "Enable periodic temperature measurements: %d\n",
		enable);
	/*
	 * Make sure a new measurement is done directly by cancelling
	 * any pending work
	 */
	cancel_delayed_work_sync(&di->btemp_periodic_work);

	if (enable)
		queue_delayed_work(di->btemp_wq, &di->btemp_periodic_work, 0);
}

/**
 * ab8500_btemp_get_temp() - get battery temperature
 * @di:		pointer to the ab8500_btemp structure
 *
 * Returns battery temperature
 */
int ab8500_btemp_get_temp(struct ab8500_btemp *di)
{
	int temp = 0;

	/*
	 * The BTEMP events are not reliabe on AB8500 cut3.3
	 * and prior versions
	 */
	if (is_ab8500_3p3_or_earlier(di->parent)) {
		temp = di->bat_temp * 10;
	} else {
		if (di->events.btemp_low) {
			if (temp > di->btemp_ranges.btemp_low_limit)
				temp = di->btemp_ranges.btemp_low_limit * 10;
			else
				temp = di->bat_temp * 10;
		} else if (di->events.btemp_high) {
			if (temp < di->btemp_ranges.btemp_high_limit)
				temp = di->btemp_ranges.btemp_high_limit * 10;
			else
				temp = di->bat_temp * 10;
		} else if (di->events.btemp_lowmed) {
			if (temp > di->btemp_ranges.btemp_med_limit)
				temp = di->btemp_ranges.btemp_med_limit * 10;
			else
				temp = di->bat_temp * 10;
		} else if (di->events.btemp_medhigh) {
			if (temp < di->btemp_ranges.btemp_med_limit)
				temp = di->btemp_ranges.btemp_med_limit * 10;
			else
				temp = di->bat_temp * 10;
		} else
			temp = di->bat_temp * 10;
	}
	return temp;
}
EXPORT_SYMBOL(ab8500_btemp_get_temp);

/**
 * ab8500_btemp_get_batctrl_temp() - get the temperature
 * @btemp:      pointer to the btemp structure
 *
 * Returns the batctrl temperature in millidegrees
 */
int ab8500_btemp_get_batctrl_temp(struct ab8500_btemp *btemp)
{
	return btemp->bat_temp * 1000;
}
EXPORT_SYMBOL(ab8500_btemp_get_batctrl_temp);

/**
 * ab8500_btemp_get_property() - get the btemp properties
 * @psy:        pointer to the power_supply structure
 * @psp:        pointer to the power_supply_property structure
 * @val:        pointer to the power_supply_propval union
 *
 * This function gets called when an application tries to get the btemp
 * properties by reading the sysfs files.
 * online:	presence of the battery
 * present:	presence of the battery
 * technology:	battery technology
 * temp:	battery temperature
 * Returns error code in case of failure else 0(on success)
 */
static int ab8500_btemp_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	struct ab8500_btemp *di;

	di = to_ab8500_btemp_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		if (di->events.batt_rem)
			val->intval = 0;
		else
			val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = di->bm->bat_type[di->bm->batt_id].name;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = ab8500_btemp_get_temp(di);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int ab8500_btemp_get_ext_psy_data(struct device *dev, void *data)
{
	struct power_supply *psy;
	struct power_supply *ext;
	struct ab8500_btemp *di;
	union power_supply_propval ret;
	int i, j;
	bool psy_found = false;

	psy = (struct power_supply *)data;
	ext = dev_get_drvdata(dev);
	di = to_ab8500_btemp_device_info(psy);

	/*
	 * For all psy where the name of your driver
	 * appears in any supplied_to
	 */
	for (i = 0; i < ext->num_supplicants; i++) {
		if (!strcmp(ext->supplied_to[i], psy->name))
			psy_found = true;
	}

	if (!psy_found)
		return 0;

	/* Go through all properties for the psy */
	for (j = 0; j < ext->num_properties; j++) {
		enum power_supply_property prop;
		prop = ext->properties[j];

		if (ext->get_property(ext, prop, &ret))
			continue;

		switch (prop) {
		case POWER_SUPPLY_PROP_PRESENT:
			switch (ext->type) {
			case POWER_SUPPLY_TYPE_MAINS:
				/* AC disconnected */
				if (!ret.intval && di->events.ac_conn) {
					di->events.ac_conn = false;
				}
				/* AC connected */
				else if (ret.intval && !di->events.ac_conn) {
					di->events.ac_conn = true;
					if (!di->events.usb_conn)
						ab8500_btemp_periodic(di, true);
				}
				break;
			case POWER_SUPPLY_TYPE_USB:
				/* USB disconnected */
				if (!ret.intval && di->events.usb_conn) {
					di->events.usb_conn = false;
				}
				/* USB connected */
				else if (ret.intval && !di->events.usb_conn) {
					di->events.usb_conn = true;
					if (!di->events.ac_conn)
						ab8500_btemp_periodic(di, true);
				}
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}
	return 0;
}

/**
 * ab8500_btemp_external_power_changed() - callback for power supply changes
 * @psy:       pointer to the structure power_supply
 *
 * This function is pointing to the function pointer external_power_changed
 * of the structure power_supply.
 * This function gets executed when there is a change in the external power
 * supply to the btemp.
 */
static void ab8500_btemp_external_power_changed(struct power_supply *psy)
{
	struct ab8500_btemp *di = to_ab8500_btemp_device_info(psy);

	class_for_each_device(power_supply_class, NULL,
		&di->btemp_psy, ab8500_btemp_get_ext_psy_data);
}

/* ab8500 btemp driver interrupts and their respective isr */
static struct ab8500_btemp_interrupts ab8500_btemp_irq[] = {
	{"BAT_CTRL_INDB", ab8500_btemp_batctrlindb_handler},
	{"BTEMP_LOW", ab8500_btemp_templow_handler},
	{"BTEMP_HIGH", ab8500_btemp_temphigh_handler},
	{"BTEMP_LOW_MEDIUM", ab8500_btemp_lowmed_handler},
	{"BTEMP_MEDIUM_HIGH", ab8500_btemp_medhigh_handler},
};

#if defined(CONFIG_PM)
static int ab8500_btemp_resume(struct platform_device *pdev)
{
	struct ab8500_btemp *di = platform_get_drvdata(pdev);

	ab8500_btemp_periodic(di, true);

	return 0;
}

static int ab8500_btemp_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ab8500_btemp *di = platform_get_drvdata(pdev);

	ab8500_btemp_periodic(di, false);

	return 0;
}
#else
#define ab8500_btemp_suspend      NULL
#define ab8500_btemp_resume       NULL
#endif

static int ab8500_btemp_remove(struct platform_device *pdev)
{
	struct ab8500_btemp *di = platform_get_drvdata(pdev);
	int i, irq;

	/* Disable interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_btemp_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab8500_btemp_irq[i].name);
		free_irq(irq, di);
	}

	/* Delete the work queue */
	destroy_workqueue(di->btemp_wq);

	flush_scheduled_work();
	power_supply_unregister(&di->btemp_psy);

	return 0;
}

static char *supply_interface[] = {
	"ab8500_chargalg",
	"ab8500_fg",
};

static int ab8500_btemp_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct abx500_bm_data *plat = pdev->dev.platform_data;
	struct power_supply_config psy_cfg = {};
	struct ab8500_btemp *di;
	int irq, i, ret = 0;
	u8 val;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di) {
		dev_err(&pdev->dev, "%s no mem for ab8500_btemp\n", __func__);
		return -ENOMEM;
	}

	if (!plat) {
		dev_err(&pdev->dev, "no battery management data supplied\n");
		return -EINVAL;
	}
	di->bm = plat;

	if (np) {
		ret = ab8500_bm_of_probe(&pdev->dev, np, di->bm);
		if (ret) {
			dev_err(&pdev->dev, "failed to get battery information\n");
			return ret;
		}
	}

	/* get parent data */
	di->dev = &pdev->dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);
	di->gpadc = ab8500_gpadc_get("ab8500-gpadc.0");

	di->initialized = false;

	/* BTEMP supply */
	di->btemp_psy.name = "ab8500_btemp";
	di->btemp_psy.type = POWER_SUPPLY_TYPE_BATTERY;
	di->btemp_psy.properties = ab8500_btemp_props;
	di->btemp_psy.num_properties = ARRAY_SIZE(ab8500_btemp_props);
	di->btemp_psy.get_property = ab8500_btemp_get_property;
	di->btemp_psy.external_power_changed =
		ab8500_btemp_external_power_changed;

	psy_cfg.supplied_to = supply_interface;
	psy_cfg.num_supplicants = ARRAY_SIZE(supply_interface);

	/* Create a work queue for the btemp */
	di->btemp_wq =
		create_singlethread_workqueue("ab8500_btemp_wq");
	if (di->btemp_wq == NULL) {
		dev_err(di->dev, "failed to create work queue\n");
		return -ENOMEM;
	}

	/* Init work for measuring temperature periodically */
	INIT_DEFERRABLE_WORK(&di->btemp_periodic_work,
		ab8500_btemp_periodic_work);

	/* Set BTEMP thermal limits. Low and Med are fixed */
	di->btemp_ranges.btemp_low_limit = BTEMP_THERMAL_LOW_LIMIT;
	di->btemp_ranges.btemp_med_limit = BTEMP_THERMAL_MED_LIMIT;

	ret = abx500_get_register_interruptible(di->dev, AB8500_CHARGER,
		AB8500_BTEMP_HIGH_TH, &val);
	if (ret < 0) {
		dev_err(di->dev, "%s ab8500 read failed\n", __func__);
		goto free_btemp_wq;
	}
	switch (val) {
	case BTEMP_HIGH_TH_57_0:
	case BTEMP_HIGH_TH_57_1:
		di->btemp_ranges.btemp_high_limit =
			BTEMP_THERMAL_HIGH_LIMIT_57;
		break;
	case BTEMP_HIGH_TH_52:
		di->btemp_ranges.btemp_high_limit =
			BTEMP_THERMAL_HIGH_LIMIT_52;
		break;
	case BTEMP_HIGH_TH_62:
		di->btemp_ranges.btemp_high_limit =
			BTEMP_THERMAL_HIGH_LIMIT_62;
		break;
	}

	/* Register BTEMP power supply class */
	ret = power_supply_register(di->dev, &di->btemp_psy, &psy_cfg);
	if (ret) {
		dev_err(di->dev, "failed to register BTEMP psy\n");
		goto free_btemp_wq;
	}

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_btemp_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab8500_btemp_irq[i].name);
		ret = request_threaded_irq(irq, NULL, ab8500_btemp_irq[i].isr,
			IRQF_SHARED | IRQF_NO_SUSPEND,
			ab8500_btemp_irq[i].name, di);

		if (ret) {
			dev_err(di->dev, "failed to request %s IRQ %d: %d\n"
				, ab8500_btemp_irq[i].name, irq, ret);
			goto free_irq;
		}
		dev_dbg(di->dev, "Requested %s IRQ %d: %d\n",
			ab8500_btemp_irq[i].name, irq, ret);
	}

	platform_set_drvdata(pdev, di);

	/* Kick off periodic temperature measurements */
	ab8500_btemp_periodic(di, true);
	list_add_tail(&di->node, &ab8500_btemp_list);

	return ret;

free_irq:
	power_supply_unregister(&di->btemp_psy);

	/* We also have to free all successfully registered irqs */
	for (i = i - 1; i >= 0; i--) {
		irq = platform_get_irq_byname(pdev, ab8500_btemp_irq[i].name);
		free_irq(irq, di);
	}
free_btemp_wq:
	destroy_workqueue(di->btemp_wq);
	return ret;
}

static const struct of_device_id ab8500_btemp_match[] = {
	{ .compatible = "stericsson,ab8500-btemp", },
	{ },
};

static struct platform_driver ab8500_btemp_driver = {
	.probe = ab8500_btemp_probe,
	.remove = ab8500_btemp_remove,
	.suspend = ab8500_btemp_suspend,
	.resume = ab8500_btemp_resume,
	.driver = {
		.name = "ab8500-btemp",
		.of_match_table = ab8500_btemp_match,
	},
};

static int __init ab8500_btemp_init(void)
{
	return platform_driver_register(&ab8500_btemp_driver);
}

static void __exit ab8500_btemp_exit(void)
{
	platform_driver_unregister(&ab8500_btemp_driver);
}

device_initcall(ab8500_btemp_init);
module_exit(ab8500_btemp_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski, Arun R Murthy");
MODULE_ALIAS("platform:ab8500-btemp");
MODULE_DESCRIPTION("AB8500 battery temperature driver");
