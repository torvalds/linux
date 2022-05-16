// SPDX-License-Identifier: GPL-2.0-only
/*
 * Motorola CPCAP PMIC battery charger driver
 *
 * Copyright (C) 2017 Tony Lindgren <tony@atomide.com>
 *
 * Rewritten for Linux power framework with some parts based on
 * on earlier driver found in the Motorola Linux kernel:
 *
 * Copyright (C) 2009-2010 Motorola, Inc.
 */

#include <linux/atomic.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

#include <linux/gpio/consumer.h>
#include <linux/usb/phy_companion.h>
#include <linux/phy/omap_usb.h>
#include <linux/usb/otg.h>
#include <linux/iio/consumer.h>
#include <linux/mfd/motorola-cpcap.h>

/*
 * CPCAP_REG_CRM register bits. For documentation of somewhat similar hardware,
 * see NXP "MC13783 Power Management and Audio Circuit Users's Guide"
 * MC13783UG.pdf chapter "8.5 Battery Interface Register Summary". The registers
 * and values for CPCAP are different, but some of the internal components seem
 * similar. Also see the Motorola Linux kernel cpcap-regbits.h. CPCAP_REG_CHRGR_1
 * bits that seem to describe the CRM register.
 */
#define CPCAP_REG_CRM_UNUSED_641_15	BIT(15)	/* 641 = register number */
#define CPCAP_REG_CRM_UNUSED_641_14	BIT(14)	/* 641 = register number */
#define CPCAP_REG_CRM_CHRG_LED_EN	BIT(13)	/* Charger LED */
#define CPCAP_REG_CRM_RVRSMODE		BIT(12)	/* USB VBUS output enable */
#define CPCAP_REG_CRM_ICHRG_TR1		BIT(11)	/* Trickle charge current */
#define CPCAP_REG_CRM_ICHRG_TR0		BIT(10)
#define CPCAP_REG_CRM_FET_OVRD		BIT(9)	/* 0 = hardware, 1 = FET_CTRL */
#define CPCAP_REG_CRM_FET_CTRL		BIT(8)	/* BPFET 1 if FET_OVRD set */
#define CPCAP_REG_CRM_VCHRG3		BIT(7)	/* Charge voltage bits */
#define CPCAP_REG_CRM_VCHRG2		BIT(6)
#define CPCAP_REG_CRM_VCHRG1		BIT(5)
#define CPCAP_REG_CRM_VCHRG0		BIT(4)
#define CPCAP_REG_CRM_ICHRG3		BIT(3)	/* Charge current bits */
#define CPCAP_REG_CRM_ICHRG2		BIT(2)
#define CPCAP_REG_CRM_ICHRG1		BIT(1)
#define CPCAP_REG_CRM_ICHRG0		BIT(0)

/* CPCAP_REG_CRM trickle charge voltages */
#define CPCAP_REG_CRM_TR(val)		(((val) & 0x3) << 10)
#define CPCAP_REG_CRM_TR_0A00		CPCAP_REG_CRM_TR(0x0)
#define CPCAP_REG_CRM_TR_0A24		CPCAP_REG_CRM_TR(0x1)
#define CPCAP_REG_CRM_TR_0A48		CPCAP_REG_CRM_TR(0x2)
#define CPCAP_REG_CRM_TR_0A72		CPCAP_REG_CRM_TR(0x4)

/*
 * CPCAP_REG_CRM charge voltages based on the ADC channel 1 values.
 * Note that these register bits don't match MC13783UG.pdf VCHRG
 * register bits.
 */
#define CPCAP_REG_CRM_VCHRG(val)	(((val) & 0xf) << 4)
#define CPCAP_REG_CRM_VCHRG_3V80	CPCAP_REG_CRM_VCHRG(0x0)
#define CPCAP_REG_CRM_VCHRG_4V10	CPCAP_REG_CRM_VCHRG(0x1)
#define CPCAP_REG_CRM_VCHRG_4V12	CPCAP_REG_CRM_VCHRG(0x2)
#define CPCAP_REG_CRM_VCHRG_4V15	CPCAP_REG_CRM_VCHRG(0x3)
#define CPCAP_REG_CRM_VCHRG_4V17	CPCAP_REG_CRM_VCHRG(0x4)
#define CPCAP_REG_CRM_VCHRG_4V20	CPCAP_REG_CRM_VCHRG(0x5)
#define CPCAP_REG_CRM_VCHRG_4V23	CPCAP_REG_CRM_VCHRG(0x6)
#define CPCAP_REG_CRM_VCHRG_4V25	CPCAP_REG_CRM_VCHRG(0x7)
#define CPCAP_REG_CRM_VCHRG_4V27	CPCAP_REG_CRM_VCHRG(0x8)
#define CPCAP_REG_CRM_VCHRG_4V30	CPCAP_REG_CRM_VCHRG(0x9)
#define CPCAP_REG_CRM_VCHRG_4V33	CPCAP_REG_CRM_VCHRG(0xa)
#define CPCAP_REG_CRM_VCHRG_4V35	CPCAP_REG_CRM_VCHRG(0xb)
#define CPCAP_REG_CRM_VCHRG_4V38	CPCAP_REG_CRM_VCHRG(0xc)
#define CPCAP_REG_CRM_VCHRG_4V40	CPCAP_REG_CRM_VCHRG(0xd)
#define CPCAP_REG_CRM_VCHRG_4V42	CPCAP_REG_CRM_VCHRG(0xe)
#define CPCAP_REG_CRM_VCHRG_4V44	CPCAP_REG_CRM_VCHRG(0xf)

/*
 * CPCAP_REG_CRM charge currents. These seem to match MC13783UG.pdf
 * values in "Table 8-3. Charge Path Regulator Current Limit
 * Characteristics" for the nominal values.
 */
#define CPCAP_REG_CRM_ICHRG(val)	(((val) & 0xf) << 0)
#define CPCAP_REG_CRM_ICHRG_0A000	CPCAP_REG_CRM_ICHRG(0x0)
#define CPCAP_REG_CRM_ICHRG_0A070	CPCAP_REG_CRM_ICHRG(0x1)
#define CPCAP_REG_CRM_ICHRG_0A177	CPCAP_REG_CRM_ICHRG(0x2)
#define CPCAP_REG_CRM_ICHRG_0A266	CPCAP_REG_CRM_ICHRG(0x3)
#define CPCAP_REG_CRM_ICHRG_0A355	CPCAP_REG_CRM_ICHRG(0x4)
#define CPCAP_REG_CRM_ICHRG_0A443	CPCAP_REG_CRM_ICHRG(0x5)
#define CPCAP_REG_CRM_ICHRG_0A532	CPCAP_REG_CRM_ICHRG(0x6)
#define CPCAP_REG_CRM_ICHRG_0A621	CPCAP_REG_CRM_ICHRG(0x7)
#define CPCAP_REG_CRM_ICHRG_0A709	CPCAP_REG_CRM_ICHRG(0x8)
#define CPCAP_REG_CRM_ICHRG_0A798	CPCAP_REG_CRM_ICHRG(0x9)
#define CPCAP_REG_CRM_ICHRG_0A886	CPCAP_REG_CRM_ICHRG(0xa)
#define CPCAP_REG_CRM_ICHRG_0A975	CPCAP_REG_CRM_ICHRG(0xb)
#define CPCAP_REG_CRM_ICHRG_1A064	CPCAP_REG_CRM_ICHRG(0xc)
#define CPCAP_REG_CRM_ICHRG_1A152	CPCAP_REG_CRM_ICHRG(0xd)
#define CPCAP_REG_CRM_ICHRG_1A596	CPCAP_REG_CRM_ICHRG(0xe)
#define CPCAP_REG_CRM_ICHRG_NO_LIMIT	CPCAP_REG_CRM_ICHRG(0xf)

/* CPCAP_REG_VUSBC register bits needed for VBUS */
#define CPCAP_BIT_VBUS_SWITCH		BIT(0)	/* VBUS boost to 5V */

enum {
	CPCAP_CHARGER_IIO_BATTDET,
	CPCAP_CHARGER_IIO_VOLTAGE,
	CPCAP_CHARGER_IIO_VBUS,
	CPCAP_CHARGER_IIO_CHRG_CURRENT,
	CPCAP_CHARGER_IIO_BATT_CURRENT,
	CPCAP_CHARGER_IIO_NR,
};

enum {
	CPCAP_CHARGER_DISCONNECTED,
	CPCAP_CHARGER_DETECTING,
	CPCAP_CHARGER_CHARGING,
	CPCAP_CHARGER_DONE,
};

struct cpcap_charger_ddata {
	struct device *dev;
	struct regmap *reg;
	struct list_head irq_list;
	struct delayed_work detect_work;
	struct delayed_work vbus_work;
	struct gpio_desc *gpio[2];		/* gpio_reven0 & 1 */

	struct iio_channel *channels[CPCAP_CHARGER_IIO_NR];

	struct power_supply *usb;

	struct phy_companion comparator;	/* For USB VBUS */
	unsigned int vbus_enabled:1;
	unsigned int feeding_vbus:1;
	atomic_t active;

	int status;
	int state;
	int voltage;
};

struct cpcap_interrupt_desc {
	int irq;
	struct list_head node;
	const char *name;
};

struct cpcap_charger_ints_state {
	bool chrg_det;
	bool rvrs_chrg;
	bool vbusov;

	bool chrg_se1b;
	bool rvrs_mode;
	bool chrgcurr2;
	bool chrgcurr1;
	bool vbusvld;

	bool battdetb;
};

static enum power_supply_property cpcap_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

/* No battery always shows temperature of -40000 */
static bool cpcap_charger_battery_found(struct cpcap_charger_ddata *ddata)
{
	struct iio_channel *channel;
	int error, temperature;

	channel = ddata->channels[CPCAP_CHARGER_IIO_BATTDET];
	error = iio_read_channel_processed(channel, &temperature);
	if (error < 0) {
		dev_warn(ddata->dev, "%s failed: %i\n", __func__, error);

		return false;
	}

	return temperature > -20000 && temperature < 60000;
}

static int cpcap_charger_get_charge_voltage(struct cpcap_charger_ddata *ddata)
{
	struct iio_channel *channel;
	int error, value = 0;

	channel = ddata->channels[CPCAP_CHARGER_IIO_VOLTAGE];
	error = iio_read_channel_processed(channel, &value);
	if (error < 0) {
		dev_warn(ddata->dev, "%s failed: %i\n", __func__, error);

		return 0;
	}

	return value;
}

static int cpcap_charger_get_charge_current(struct cpcap_charger_ddata *ddata)
{
	struct iio_channel *channel;
	int error, value = 0;

	channel = ddata->channels[CPCAP_CHARGER_IIO_CHRG_CURRENT];
	error = iio_read_channel_processed(channel, &value);
	if (error < 0) {
		dev_warn(ddata->dev, "%s failed: %i\n", __func__, error);

		return 0;
	}

	return value;
}

static int cpcap_charger_get_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      union power_supply_propval *val)
{
	struct cpcap_charger_ddata *ddata = dev_get_drvdata(psy->dev.parent);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = ddata->status;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		val->intval = ddata->voltage;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (ddata->status == POWER_SUPPLY_STATUS_CHARGING)
			val->intval = cpcap_charger_get_charge_voltage(ddata) *
				1000;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (ddata->status == POWER_SUPPLY_STATUS_CHARGING)
			val->intval = cpcap_charger_get_charge_current(ddata) *
				1000;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = ddata->status == POWER_SUPPLY_STATUS_CHARGING;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cpcap_charger_match_voltage(int voltage)
{
	switch (voltage) {
	case 0 ... 4100000 - 1: return 3800000;
	case 4100000 ... 4120000 - 1: return 4100000;
	case 4120000 ... 4150000 - 1: return 4120000;
	case 4150000 ... 4170000 - 1: return 4150000;
	case 4170000 ... 4200000 - 1: return 4170000;
	case 4200000 ... 4230000 - 1: return 4200000;
	case 4230000 ... 4250000 - 1: return 4230000;
	case 4250000 ... 4270000 - 1: return 4250000;
	case 4270000 ... 4300000 - 1: return 4270000;
	case 4300000 ... 4330000 - 1: return 4300000;
	case 4330000 ... 4350000 - 1: return 4330000;
	case 4350000 ... 4380000 - 1: return 4350000;
	case 4380000 ... 4400000 - 1: return 4380000;
	case 4400000 ... 4420000 - 1: return 4400000;
	case 4420000 ... 4440000 - 1: return 4420000;
	case 4440000: return 4440000;
	default: return 0;
	}
}

static int
cpcap_charger_get_bat_const_charge_voltage(struct cpcap_charger_ddata *ddata)
{
	union power_supply_propval prop;
	struct power_supply *battery;
	int voltage = ddata->voltage;
	int error;

	battery = power_supply_get_by_name("battery");
	if (battery) {
		error = power_supply_get_property(battery,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
				&prop);
		if (!error)
			voltage = prop.intval;
	}

	return voltage;
}

static int cpcap_charger_set_property(struct power_supply *psy,
				      enum power_supply_property psp,
				      const union power_supply_propval *val)
{
	struct cpcap_charger_ddata *ddata = dev_get_drvdata(psy->dev.parent);
	int voltage, batvolt;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		voltage = cpcap_charger_match_voltage(val->intval);
		batvolt = cpcap_charger_get_bat_const_charge_voltage(ddata);
		if (voltage > batvolt)
			voltage = batvolt;
		ddata->voltage = voltage;
		schedule_delayed_work(&ddata->detect_work, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cpcap_charger_property_is_writeable(struct power_supply *psy,
					       enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		return 1;
	default:
		return 0;
	}
}

static void cpcap_charger_set_cable_path(struct cpcap_charger_ddata *ddata,
					 bool enabled)
{
	if (!ddata->gpio[0])
		return;

	gpiod_set_value(ddata->gpio[0], enabled);
}

static void cpcap_charger_set_inductive_path(struct cpcap_charger_ddata *ddata,
					     bool enabled)
{
	if (!ddata->gpio[1])
		return;

	gpiod_set_value(ddata->gpio[1], enabled);
}

static int cpcap_charger_set_state(struct cpcap_charger_ddata *ddata,
				   int max_voltage, int charge_current,
				   int trickle_current)
{
	bool enable;
	int error;

	enable = (charge_current || trickle_current);
	dev_dbg(ddata->dev, "%s enable: %i\n", __func__, enable);

	if (!enable) {
		error = regmap_update_bits(ddata->reg, CPCAP_REG_CRM,
					   0x3fff,
					   CPCAP_REG_CRM_FET_OVRD |
					   CPCAP_REG_CRM_FET_CTRL);
		if (error) {
			ddata->status = POWER_SUPPLY_STATUS_UNKNOWN;
			goto out_err;
		}

		ddata->status = POWER_SUPPLY_STATUS_DISCHARGING;

		return 0;
	}

	error = regmap_update_bits(ddata->reg, CPCAP_REG_CRM, 0x3fff,
				   CPCAP_REG_CRM_CHRG_LED_EN |
				   trickle_current |
				   CPCAP_REG_CRM_FET_OVRD |
				   CPCAP_REG_CRM_FET_CTRL |
				   max_voltage |
				   charge_current);
	if (error) {
		ddata->status = POWER_SUPPLY_STATUS_UNKNOWN;
		goto out_err;
	}

	ddata->status = POWER_SUPPLY_STATUS_CHARGING;

	return 0;

out_err:
	dev_err(ddata->dev, "%s failed with %i\n", __func__, error);

	return error;
}

static bool cpcap_charger_vbus_valid(struct cpcap_charger_ddata *ddata)
{
	int error, value = 0;
	struct iio_channel *channel =
		ddata->channels[CPCAP_CHARGER_IIO_VBUS];

	error = iio_read_channel_processed(channel, &value);
	if (error >= 0)
		return value > 3900 ? true : false;

	dev_err(ddata->dev, "error reading VBUS: %i\n", error);

	return false;
}

/* VBUS control functions for the USB PHY companion */
static void cpcap_charger_vbus_work(struct work_struct *work)
{
	struct cpcap_charger_ddata *ddata;
	bool vbus = false;
	int error;

	ddata = container_of(work, struct cpcap_charger_ddata,
			     vbus_work.work);

	if (ddata->vbus_enabled) {
		vbus = cpcap_charger_vbus_valid(ddata);
		if (vbus) {
			dev_info(ddata->dev, "VBUS already provided\n");

			return;
		}

		ddata->feeding_vbus = true;
		cpcap_charger_set_cable_path(ddata, false);
		cpcap_charger_set_inductive_path(ddata, false);

		error = cpcap_charger_set_state(ddata, 0, 0, 0);
		if (error)
			goto out_err;

		error = regmap_update_bits(ddata->reg, CPCAP_REG_VUSBC,
					   CPCAP_BIT_VBUS_SWITCH,
					   CPCAP_BIT_VBUS_SWITCH);
		if (error)
			goto out_err;

		error = regmap_update_bits(ddata->reg, CPCAP_REG_CRM,
					   CPCAP_REG_CRM_RVRSMODE,
					   CPCAP_REG_CRM_RVRSMODE);
		if (error)
			goto out_err;
	} else {
		error = regmap_update_bits(ddata->reg, CPCAP_REG_VUSBC,
					   CPCAP_BIT_VBUS_SWITCH, 0);
		if (error)
			goto out_err;

		error = regmap_update_bits(ddata->reg, CPCAP_REG_CRM,
					   CPCAP_REG_CRM_RVRSMODE, 0);
		if (error)
			goto out_err;

		cpcap_charger_set_cable_path(ddata, true);
		cpcap_charger_set_inductive_path(ddata, true);
		ddata->feeding_vbus = false;
	}

	return;

out_err:
	dev_err(ddata->dev, "%s could not %s vbus: %i\n", __func__,
		ddata->vbus_enabled ? "enable" : "disable", error);
}

static int cpcap_charger_set_vbus(struct phy_companion *comparator,
				  bool enabled)
{
	struct cpcap_charger_ddata *ddata =
		container_of(comparator, struct cpcap_charger_ddata,
			     comparator);

	ddata->vbus_enabled = enabled;
	schedule_delayed_work(&ddata->vbus_work, 0);

	return 0;
}

/* Charger interrupt handling functions */

static int cpcap_charger_get_ints_state(struct cpcap_charger_ddata *ddata,
					struct cpcap_charger_ints_state *s)
{
	int val, error;

	error = regmap_read(ddata->reg, CPCAP_REG_INTS1, &val);
	if (error)
		return error;

	s->chrg_det = val & BIT(13);
	s->rvrs_chrg = val & BIT(12);
	s->vbusov = val & BIT(11);

	error = regmap_read(ddata->reg, CPCAP_REG_INTS2, &val);
	if (error)
		return error;

	s->chrg_se1b = val & BIT(13);
	s->rvrs_mode = val & BIT(6);
	s->chrgcurr2 = val & BIT(5);
	s->chrgcurr1 = val & BIT(4);
	s->vbusvld = val & BIT(3);

	error = regmap_read(ddata->reg, CPCAP_REG_INTS4, &val);
	if (error)
		return error;

	s->battdetb = val & BIT(6);

	return 0;
}

static void cpcap_charger_update_state(struct cpcap_charger_ddata *ddata,
				       int state)
{
	const char *status;

	if (state > CPCAP_CHARGER_DONE) {
		dev_warn(ddata->dev, "unknown state: %i\n", state);

		return;
	}

	ddata->state = state;

	switch (state) {
	case CPCAP_CHARGER_DISCONNECTED:
		status = "DISCONNECTED";
		break;
	case CPCAP_CHARGER_DETECTING:
		status = "DETECTING";
		break;
	case CPCAP_CHARGER_CHARGING:
		status = "CHARGING";
		break;
	case CPCAP_CHARGER_DONE:
		status = "DONE";
		break;
	default:
		return;
	}

	dev_dbg(ddata->dev, "state: %s\n", status);
}

static int cpcap_charger_voltage_to_regval(int voltage)
{
	int offset;

	switch (voltage) {
	case 0 ... 4100000 - 1:
		return 0;
	case 4100000 ... 4200000 - 1:
		offset = 1;
		break;
	case 4200000 ... 4300000 - 1:
		offset = 0;
		break;
	case 4300000 ... 4380000 - 1:
		offset = -1;
		break;
	case 4380000 ... 4440000:
		offset = -2;
		break;
	default:
		return 0;
	}

	return ((voltage - 4100000) / 20000) + offset;
}

static void cpcap_charger_disconnect(struct cpcap_charger_ddata *ddata,
				     int state, unsigned long delay)
{
	int error;

	error = cpcap_charger_set_state(ddata, 0, 0, 0);
	if (error)
		return;

	cpcap_charger_update_state(ddata, state);
	power_supply_changed(ddata->usb);
	schedule_delayed_work(&ddata->detect_work, delay);
}

static void cpcap_usb_detect(struct work_struct *work)
{
	struct cpcap_charger_ddata *ddata;
	struct cpcap_charger_ints_state s;
	int error;

	ddata = container_of(work, struct cpcap_charger_ddata,
			     detect_work.work);

	error = cpcap_charger_get_ints_state(ddata, &s);
	if (error)
		return;

	/* Just init the state if a charger is connected with no chrg_det set */
	if (!s.chrg_det && s.chrgcurr1 && s.vbusvld) {
		cpcap_charger_update_state(ddata, CPCAP_CHARGER_DETECTING);

		return;
	}

	/*
	 * If battery voltage is higher than charge voltage, it may have been
	 * charged to 4.35V by Android. Try again in 10 minutes.
	 */
	if (cpcap_charger_get_charge_voltage(ddata) > ddata->voltage) {
		cpcap_charger_disconnect(ddata, CPCAP_CHARGER_DETECTING,
					 HZ * 60 * 10);

		return;
	}

	/* Throttle chrgcurr2 interrupt for charger done and retry */
	switch (ddata->state) {
	case CPCAP_CHARGER_CHARGING:
		if (s.chrgcurr2)
			break;
		if (s.chrgcurr1 && s.vbusvld) {
			cpcap_charger_disconnect(ddata, CPCAP_CHARGER_DONE,
						 HZ * 5);
			return;
		}
		break;
	case CPCAP_CHARGER_DONE:
		if (!s.chrgcurr2)
			break;
		cpcap_charger_disconnect(ddata, CPCAP_CHARGER_DETECTING,
					 HZ * 5);
		return;
	default:
		break;
	}

	if (!ddata->feeding_vbus && cpcap_charger_vbus_valid(ddata) &&
	    s.chrgcurr1) {
		int max_current;
		int vchrg;

		if (cpcap_charger_battery_found(ddata))
			max_current = CPCAP_REG_CRM_ICHRG_1A596;
		else
			max_current = CPCAP_REG_CRM_ICHRG_0A532;

		vchrg = cpcap_charger_voltage_to_regval(ddata->voltage);
		error = cpcap_charger_set_state(ddata,
						CPCAP_REG_CRM_VCHRG(vchrg),
						max_current, 0);
		if (error)
			goto out_err;
		cpcap_charger_update_state(ddata, CPCAP_CHARGER_CHARGING);
	} else {
		error = cpcap_charger_set_state(ddata, 0, 0, 0);
		if (error)
			goto out_err;
		cpcap_charger_update_state(ddata, CPCAP_CHARGER_DISCONNECTED);
	}

	power_supply_changed(ddata->usb);
	return;

out_err:
	dev_err(ddata->dev, "%s failed with %i\n", __func__, error);
}

static irqreturn_t cpcap_charger_irq_thread(int irq, void *data)
{
	struct cpcap_charger_ddata *ddata = data;

	if (!atomic_read(&ddata->active))
		return IRQ_NONE;

	schedule_delayed_work(&ddata->detect_work, 0);

	return IRQ_HANDLED;
}

static int cpcap_usb_init_irq(struct platform_device *pdev,
			      struct cpcap_charger_ddata *ddata,
			      const char *name)
{
	struct cpcap_interrupt_desc *d;
	int irq, error;

	irq = platform_get_irq_byname(pdev, name);
	if (irq < 0)
		return -ENODEV;

	error = devm_request_threaded_irq(ddata->dev, irq, NULL,
					  cpcap_charger_irq_thread,
					  IRQF_SHARED,
					  name, ddata);
	if (error) {
		dev_err(ddata->dev, "could not get irq %s: %i\n",
			name, error);

		return error;
	}

	d = devm_kzalloc(ddata->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->name = name;
	d->irq = irq;
	list_add(&d->node, &ddata->irq_list);

	return 0;
}

static const char * const cpcap_charger_irqs[] = {
	/* REG_INT_0 */
	"chrg_det", "rvrs_chrg",

	/* REG_INT1 */
	"chrg_se1b", "se0conn", "rvrs_mode", "chrgcurr2", "chrgcurr1", "vbusvld",

	/* REG_INT_3 */
	"battdetb",
};

static int cpcap_usb_init_interrupts(struct platform_device *pdev,
				     struct cpcap_charger_ddata *ddata)
{
	int i, error;

	for (i = 0; i < ARRAY_SIZE(cpcap_charger_irqs); i++) {
		error = cpcap_usb_init_irq(pdev, ddata, cpcap_charger_irqs[i]);
		if (error)
			return error;
	}

	return 0;
}

static void cpcap_charger_init_optional_gpios(struct cpcap_charger_ddata *ddata)
{
	int i;

	for (i = 0; i < 2; i++) {
		ddata->gpio[i] = devm_gpiod_get_index(ddata->dev, "mode",
						      i, GPIOD_OUT_HIGH);
		if (IS_ERR(ddata->gpio[i])) {
			dev_info(ddata->dev, "no mode change GPIO%i: %li\n",
				 i, PTR_ERR(ddata->gpio[i]));
			ddata->gpio[i] = NULL;
		}
	}
}

static int cpcap_charger_init_iio(struct cpcap_charger_ddata *ddata)
{
	const char * const names[CPCAP_CHARGER_IIO_NR] = {
		"battdetb", "battp", "vbus", "chg_isense", "batti",
	};
	int error, i;

	for (i = 0; i < CPCAP_CHARGER_IIO_NR; i++) {
		ddata->channels[i] = devm_iio_channel_get(ddata->dev,
							  names[i]);
		if (IS_ERR(ddata->channels[i])) {
			error = PTR_ERR(ddata->channels[i]);
			goto out_err;
		}

		if (!ddata->channels[i]->indio_dev) {
			error = -ENXIO;
			goto out_err;
		}
	}

	return 0;

out_err:
	if (error != -EPROBE_DEFER)
		dev_err(ddata->dev, "could not initialize VBUS or ID IIO: %i\n",
			error);

	return error;
}

static const struct power_supply_desc cpcap_charger_usb_desc = {
	.name		= "usb",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= cpcap_charger_props,
	.num_properties	= ARRAY_SIZE(cpcap_charger_props),
	.get_property	= cpcap_charger_get_property,
	.set_property	= cpcap_charger_set_property,
	.property_is_writeable = cpcap_charger_property_is_writeable,
};

#ifdef CONFIG_OF
static const struct of_device_id cpcap_charger_id_table[] = {
	{
		.compatible = "motorola,mapphone-cpcap-charger",
	},
	{},
};
MODULE_DEVICE_TABLE(of, cpcap_charger_id_table);
#endif

static int cpcap_charger_probe(struct platform_device *pdev)
{
	struct cpcap_charger_ddata *ddata;
	const struct of_device_id *of_id;
	struct power_supply_config psy_cfg = {};
	int error;

	of_id = of_match_device(of_match_ptr(cpcap_charger_id_table),
				&pdev->dev);
	if (!of_id)
		return -EINVAL;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	ddata->dev = &pdev->dev;
	ddata->voltage = 4200000;

	ddata->reg = dev_get_regmap(ddata->dev->parent, NULL);
	if (!ddata->reg)
		return -ENODEV;

	INIT_LIST_HEAD(&ddata->irq_list);
	INIT_DELAYED_WORK(&ddata->detect_work, cpcap_usb_detect);
	INIT_DELAYED_WORK(&ddata->vbus_work, cpcap_charger_vbus_work);
	platform_set_drvdata(pdev, ddata);

	error = cpcap_charger_init_iio(ddata);
	if (error)
		return error;

	atomic_set(&ddata->active, 1);

	psy_cfg.of_node = pdev->dev.of_node;
	psy_cfg.drv_data = ddata;

	ddata->usb = devm_power_supply_register(ddata->dev,
						&cpcap_charger_usb_desc,
						&psy_cfg);
	if (IS_ERR(ddata->usb)) {
		error = PTR_ERR(ddata->usb);
		dev_err(ddata->dev, "failed to register USB charger: %i\n",
			error);

		return error;
	}

	error = cpcap_usb_init_interrupts(pdev, ddata);
	if (error)
		return error;

	ddata->comparator.set_vbus = cpcap_charger_set_vbus;
	error = omap_usb2_set_comparator(&ddata->comparator);
	if (error == -ENODEV) {
		dev_info(ddata->dev, "charger needs phy, deferring probe\n");
		return -EPROBE_DEFER;
	}

	cpcap_charger_init_optional_gpios(ddata);

	schedule_delayed_work(&ddata->detect_work, 0);

	return 0;
}

static int cpcap_charger_remove(struct platform_device *pdev)
{
	struct cpcap_charger_ddata *ddata = platform_get_drvdata(pdev);
	int error;

	atomic_set(&ddata->active, 0);
	error = omap_usb2_set_comparator(NULL);
	if (error)
		dev_warn(ddata->dev, "could not clear USB comparator: %i\n",
			 error);

	error = cpcap_charger_set_state(ddata, 0, 0, 0);
	if (error)
		dev_warn(ddata->dev, "could not clear charger: %i\n",
			 error);
	cancel_delayed_work_sync(&ddata->vbus_work);
	cancel_delayed_work_sync(&ddata->detect_work);

	return 0;
}

static struct platform_driver cpcap_charger_driver = {
	.probe = cpcap_charger_probe,
	.driver	= {
		.name	= "cpcap-charger",
		.of_match_table = of_match_ptr(cpcap_charger_id_table),
	},
	.remove	= cpcap_charger_remove,
};
module_platform_driver(cpcap_charger_driver);

MODULE_AUTHOR("Tony Lindgren <tony@atomide.com>");
MODULE_DESCRIPTION("CPCAP Battery Charger Interface driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cpcap-charger");
