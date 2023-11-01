// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Battery temperature driver for AB8500
 *
 * Author:
 *	Johan Palsson <johan.palsson@stericsson.com>
 *	Karl Komierowski <karl.komierowski@stericsson.com>
 *	Arun R Murthy <arun.murthy@stericsson.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/component.h>
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
#include <linux/thermal.h>
#include <linux/iio/consumer.h>
#include <linux/fixp-arith.h>

#include "ab8500-bm.h"

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
 * @bat_temp:		Dispatched battery temperature in degree Celsius
 * @prev_bat_temp	Last measured battery temperature in degree Celsius
 * @parent:		Pointer to the struct ab8500
 * @tz:			Thermal zone for the battery
 * @adc_bat_ctrl:	ADC channel for the battery control
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
	struct thermal_zone_device *tz;
	struct iio_channel *bat_ctrl;
	struct ab8500_fg *fg;
	struct ab8500_bm_data *bm;
	struct power_supply *btemp_psy;
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
	POWER_SUPPLY_PROP_TEMP,
};

static LIST_HEAD(ab8500_btemp_list);

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
	if (is_ab8500_1p1_or_earlier(di->parent)) {
		/*
		 * For ABB cut1.0 and 1.1 BAT_CTRL is internally
		 * connected to 1.8V through a 450k resistor
		 */
		return (450000 * (v_batctrl)) / (1800 - v_batctrl);
	}

	/*
	 * BAT_CTRL is internally
	 * connected to 1.8V through a 80k resistor
	 */
	return (80000 * (v_batctrl)) / (1800 - v_batctrl);
}

/**
 * ab8500_btemp_read_batctrl_voltage() - measure batctrl voltage
 * @di:		pointer to the ab8500_btemp structure
 *
 * This function returns the voltage on BATCTRL. Returns value in mV.
 */
static int ab8500_btemp_read_batctrl_voltage(struct ab8500_btemp *di)
{
	int vbtemp, ret;
	static int prev;

	ret = iio_read_channel_processed(di->bat_ctrl, &vbtemp);
	if (ret < 0) {
		dev_err(di->dev,
			"%s ADC conversion failed, using previous value",
			__func__);
		return prev;
	}
	prev = vbtemp;
	return vbtemp;
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

	dev_dbg(di->dev, "%s batctrl: %d res: %d inst_curr: %d samples: %d\n",
		__func__, batctrl, res, inst_curr, i);

	return res;
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
	struct power_supply_battery_info *bi = di->bm->bi;
	int res;

	di->curr_source = BTEMP_BATCTRL_CURR_SRC_7UA;

	res =  ab8500_btemp_get_batctrl_res(di);
	if (res < 0) {
		dev_err(di->dev, "%s get batctrl res failed\n", __func__);
		return -ENXIO;
	}

	if (power_supply_battery_bti_in_range(bi, res)) {
		dev_info(di->dev, "Battery detected on BATCTRL (pin C3)"
			 " resistance %d Ohm = %d Ohm +/- %d%%\n",
			 res, bi->bti_resistance_ohm,
			 bi->bti_resistance_tolerance);
	} else {
		dev_warn(di->dev, "Battery identified as unknown"
			 ", resistance %d Ohm\n", res);
		return -ENXIO;
	}

	return 0;
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
	/* Assume 25 degrees celsius as start temperature */
	static int prev = 25;
	int ret;

	if (!di->initialized) {
		/* Identify the battery */
		if (ab8500_btemp_id(di) < 0)
			dev_warn(di->dev, "failed to identify the battery\n");
	}

	/* Failover if a reading is erroneous, use last meausurement */
	ret = thermal_zone_get_temp(di->tz, &bat_temp);
	if (ret) {
		dev_err(di->dev, "error reading temperature\n");
		bat_temp = prev;
	} else {
		/* Convert from millicentigrades to centigrades */
		bat_temp /= 1000;
		prev = bat_temp;
	}

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
			power_supply_changed(di->btemp_psy);
		}
	} else if (bat_temp < di->prev_bat_temp) {
		di->bat_temp--;
		power_supply_changed(di->btemp_psy);
	} else if (bat_temp > di->prev_bat_temp) {
		di->bat_temp++;
		power_supply_changed(di->btemp_psy);
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
	power_supply_changed(di->btemp_psy);

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
		power_supply_changed(di->btemp_psy);
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
	power_supply_changed(di->btemp_psy);

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
	power_supply_changed(di->btemp_psy);

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
	power_supply_changed(di->btemp_psy);

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
static int ab8500_btemp_get_temp(struct ab8500_btemp *di)
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
	struct ab8500_btemp *di = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		if (di->events.batt_rem)
			val->intval = 0;
		else
			val->intval = 1;
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
	struct power_supply *ext = dev_get_drvdata(dev);
	const char **supplicants = (const char **)ext->supplied_to;
	struct ab8500_btemp *di;
	union power_supply_propval ret;
	int j;

	psy = (struct power_supply *)data;
	di = power_supply_get_drvdata(psy);

	/*
	 * For all psy where the name of your driver
	 * appears in any supplied_to
	 */
	j = match_string(supplicants, ext->num_supplicants, psy->desc->name);
	if (j < 0)
		return 0;

	/* Go through all properties for the psy */
	for (j = 0; j < ext->desc->num_properties; j++) {
		enum power_supply_property prop;
		prop = ext->desc->properties[j];

		if (power_supply_get_property(ext, prop, &ret))
			continue;

		switch (prop) {
		case POWER_SUPPLY_PROP_PRESENT:
			switch (ext->desc->type) {
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
	class_for_each_device(power_supply_class, NULL, psy,
			      ab8500_btemp_get_ext_psy_data);
}

/* ab8500 btemp driver interrupts and their respective isr */
static struct ab8500_btemp_interrupts ab8500_btemp_irq[] = {
	{"BAT_CTRL_INDB", ab8500_btemp_batctrlindb_handler},
	{"BTEMP_LOW", ab8500_btemp_templow_handler},
	{"BTEMP_HIGH", ab8500_btemp_temphigh_handler},
	{"BTEMP_LOW_MEDIUM", ab8500_btemp_lowmed_handler},
	{"BTEMP_MEDIUM_HIGH", ab8500_btemp_medhigh_handler},
};

static int __maybe_unused ab8500_btemp_resume(struct device *dev)
{
	struct ab8500_btemp *di = dev_get_drvdata(dev);

	ab8500_btemp_periodic(di, true);

	return 0;
}

static int __maybe_unused ab8500_btemp_suspend(struct device *dev)
{
	struct ab8500_btemp *di = dev_get_drvdata(dev);

	ab8500_btemp_periodic(di, false);

	return 0;
}

static char *supply_interface[] = {
	"ab8500_chargalg",
	"ab8500_fg",
};

static const struct power_supply_desc ab8500_btemp_desc = {
	.name			= "ab8500_btemp",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= ab8500_btemp_props,
	.num_properties		= ARRAY_SIZE(ab8500_btemp_props),
	.get_property		= ab8500_btemp_get_property,
	.external_power_changed	= ab8500_btemp_external_power_changed,
};

static int ab8500_btemp_bind(struct device *dev, struct device *master,
			     void *data)
{
	struct ab8500_btemp *di = dev_get_drvdata(dev);

	/* Create a work queue for the btemp */
	di->btemp_wq =
		alloc_workqueue("ab8500_btemp_wq", WQ_MEM_RECLAIM, 0);
	if (di->btemp_wq == NULL) {
		dev_err(dev, "failed to create work queue\n");
		return -ENOMEM;
	}

	/* Kick off periodic temperature measurements */
	ab8500_btemp_periodic(di, true);

	return 0;
}

static void ab8500_btemp_unbind(struct device *dev, struct device *master,
				void *data)
{
	struct ab8500_btemp *di = dev_get_drvdata(dev);

	/* Delete the work queue */
	destroy_workqueue(di->btemp_wq);
}

static const struct component_ops ab8500_btemp_component_ops = {
	.bind = ab8500_btemp_bind,
	.unbind = ab8500_btemp_unbind,
};

static int ab8500_btemp_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_cfg = {};
	struct device *dev = &pdev->dev;
	struct ab8500_btemp *di;
	int irq, i, ret = 0;
	u8 val;

	di = devm_kzalloc(dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->bm = &ab8500_bm_data;

	/* get parent data */
	di->dev = dev;
	di->parent = dev_get_drvdata(pdev->dev.parent);

	/* Get thermal zone and ADC */
	di->tz = thermal_zone_get_zone_by_name("battery-thermal");
	if (IS_ERR(di->tz)) {
		ret = PTR_ERR(di->tz);
		/*
		 * This usually just means we are probing before the thermal
		 * zone, so just defer.
		 */
		if (ret == -ENODEV)
			ret = -EPROBE_DEFER;
		return dev_err_probe(dev, ret,
				     "failed to get battery thermal zone\n");
	}
	di->bat_ctrl = devm_iio_channel_get(dev, "bat_ctrl");
	if (IS_ERR(di->bat_ctrl)) {
		ret = dev_err_probe(dev, PTR_ERR(di->bat_ctrl),
				    "failed to get BAT CTRL ADC channel\n");
		return ret;
	}

	di->initialized = false;

	psy_cfg.supplied_to = supply_interface;
	psy_cfg.num_supplicants = ARRAY_SIZE(supply_interface);
	psy_cfg.drv_data = di;

	/* Init work for measuring temperature periodically */
	INIT_DEFERRABLE_WORK(&di->btemp_periodic_work,
		ab8500_btemp_periodic_work);

	/* Set BTEMP thermal limits. Low and Med are fixed */
	di->btemp_ranges.btemp_low_limit = BTEMP_THERMAL_LOW_LIMIT;
	di->btemp_ranges.btemp_med_limit = BTEMP_THERMAL_MED_LIMIT;

	ret = abx500_get_register_interruptible(dev, AB8500_CHARGER,
		AB8500_BTEMP_HIGH_TH, &val);
	if (ret < 0) {
		dev_err(dev, "%s ab8500 read failed\n", __func__);
		return ret;
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
	di->btemp_psy = devm_power_supply_register(dev, &ab8500_btemp_desc,
						   &psy_cfg);
	if (IS_ERR(di->btemp_psy)) {
		dev_err(dev, "failed to register BTEMP psy\n");
		return PTR_ERR(di->btemp_psy);
	}

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(ab8500_btemp_irq); i++) {
		irq = platform_get_irq_byname(pdev, ab8500_btemp_irq[i].name);
		if (irq < 0)
			return irq;

		ret = devm_request_threaded_irq(dev, irq, NULL,
			ab8500_btemp_irq[i].isr,
			IRQF_SHARED | IRQF_NO_SUSPEND | IRQF_ONESHOT,
			ab8500_btemp_irq[i].name, di);

		if (ret) {
			dev_err(dev, "failed to request %s IRQ %d: %d\n"
				, ab8500_btemp_irq[i].name, irq, ret);
			return ret;
		}
		dev_dbg(dev, "Requested %s IRQ %d: %d\n",
			ab8500_btemp_irq[i].name, irq, ret);
	}

	platform_set_drvdata(pdev, di);

	list_add_tail(&di->node, &ab8500_btemp_list);

	return component_add(dev, &ab8500_btemp_component_ops);
}

static void ab8500_btemp_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &ab8500_btemp_component_ops);
}

static SIMPLE_DEV_PM_OPS(ab8500_btemp_pm_ops, ab8500_btemp_suspend, ab8500_btemp_resume);

static const struct of_device_id ab8500_btemp_match[] = {
	{ .compatible = "stericsson,ab8500-btemp", },
	{ },
};
MODULE_DEVICE_TABLE(of, ab8500_btemp_match);

struct platform_driver ab8500_btemp_driver = {
	.probe = ab8500_btemp_probe,
	.remove_new = ab8500_btemp_remove,
	.driver = {
		.name = "ab8500-btemp",
		.of_match_table = ab8500_btemp_match,
		.pm = &ab8500_btemp_pm_ops,
	},
};
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Johan Palsson, Karl Komierowski, Arun R Murthy");
MODULE_ALIAS("platform:ab8500-btemp");
MODULE_DESCRIPTION("AB8500 battery temperature driver");
