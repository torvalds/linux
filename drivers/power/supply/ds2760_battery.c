/*
 * Driver for batteries with DS2760 chips inside.
 *
 * Copyright © 2007 Anton Vorontsov
 *	       2004-2007 Matt Reimer
 *	       2004 Szabolcs Gyurko
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * Author:  Anton Vorontsov <cbou@mail.ru>
 *	    February 2007
 *
 *	    Matt Reimer <mreimer@vpop.net>
 *	    April 2004, 2005, 2007
 *
 *	    Szabolcs Gyurko <szabolcs.gyurko@tlt.hu>
 *	    September 2004
 */

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/suspend.h>
#include <linux/w1.h>
#include <linux/of.h>

static unsigned int cache_time = 1000;
module_param(cache_time, uint, 0644);
MODULE_PARM_DESC(cache_time, "cache time in milliseconds");

static bool pmod_enabled;
module_param(pmod_enabled, bool, 0644);
MODULE_PARM_DESC(pmod_enabled, "PMOD enable bit");

static unsigned int rated_capacity;
module_param(rated_capacity, uint, 0644);
MODULE_PARM_DESC(rated_capacity, "rated battery capacity, 10*mAh or index");

static unsigned int current_accum;
module_param(current_accum, uint, 0644);
MODULE_PARM_DESC(current_accum, "current accumulator value");

#define W1_FAMILY_DS2760		0x30

/* Known commands to the DS2760 chip */
#define W1_DS2760_SWAP			0xAA
#define W1_DS2760_READ_DATA		0x69
#define W1_DS2760_WRITE_DATA		0x6C
#define W1_DS2760_COPY_DATA		0x48
#define W1_DS2760_RECALL_DATA		0xB8
#define W1_DS2760_LOCK			0x6A

/* Number of valid register addresses */
#define DS2760_DATA_SIZE		0x40

#define DS2760_PROTECTION_REG		0x00

#define DS2760_STATUS_REG		0x01
#define DS2760_STATUS_IE		(1 << 2)
#define DS2760_STATUS_SWEN		(1 << 3)
#define DS2760_STATUS_RNAOP		(1 << 4)
#define DS2760_STATUS_PMOD		(1 << 5)

#define DS2760_EEPROM_REG		0x07
#define DS2760_SPECIAL_FEATURE_REG	0x08
#define DS2760_VOLTAGE_MSB		0x0c
#define DS2760_VOLTAGE_LSB		0x0d
#define DS2760_CURRENT_MSB		0x0e
#define DS2760_CURRENT_LSB		0x0f
#define DS2760_CURRENT_ACCUM_MSB	0x10
#define DS2760_CURRENT_ACCUM_LSB	0x11
#define DS2760_TEMP_MSB			0x18
#define DS2760_TEMP_LSB			0x19
#define DS2760_EEPROM_BLOCK0		0x20
#define DS2760_ACTIVE_FULL		0x20
#define DS2760_EEPROM_BLOCK1		0x30
#define DS2760_STATUS_WRITE_REG		0x31
#define DS2760_RATED_CAPACITY		0x32
#define DS2760_CURRENT_OFFSET_BIAS	0x33
#define DS2760_ACTIVE_EMPTY		0x3b

struct ds2760_device_info {
	struct device *dev;

	/* DS2760 data, valid after calling ds2760_battery_read_status() */
	unsigned long update_time;	/* jiffies when data read */
	char raw[DS2760_DATA_SIZE];	/* raw DS2760 data */
	int voltage_raw;		/* units of 4.88 mV */
	int voltage_uV;			/* units of µV */
	int current_raw;		/* units of 0.625 mA */
	int current_uA;			/* units of µA */
	int accum_current_raw;		/* units of 0.25 mAh */
	int accum_current_uAh;		/* units of µAh */
	int temp_raw;			/* units of 0.125 °C */
	int temp_C;			/* units of 0.1 °C */
	int rated_capacity;		/* units of µAh */
	int rem_capacity;		/* percentage */
	int full_active_uAh;		/* units of µAh */
	int empty_uAh;			/* units of µAh */
	int life_sec;			/* units of seconds */
	int charge_status;		/* POWER_SUPPLY_STATUS_* */

	int full_counter;
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct workqueue_struct *monitor_wqueue;
	struct delayed_work monitor_work;
	struct delayed_work set_charged_work;
	struct notifier_block pm_notifier;
};

static int w1_ds2760_io(struct device *dev, char *buf, int addr, size_t count,
			int io)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	if (!dev)
		return 0;

	mutex_lock(&sl->master->bus_mutex);

	if (addr > DS2760_DATA_SIZE || addr < 0) {
		count = 0;
		goto out;
	}
	if (addr + count > DS2760_DATA_SIZE)
		count = DS2760_DATA_SIZE - addr;

	if (!w1_reset_select_slave(sl)) {
		if (!io) {
			w1_write_8(sl->master, W1_DS2760_READ_DATA);
			w1_write_8(sl->master, addr);
			count = w1_read_block(sl->master, buf, count);
		} else {
			w1_write_8(sl->master, W1_DS2760_WRITE_DATA);
			w1_write_8(sl->master, addr);
			w1_write_block(sl->master, buf, count);
			/* XXX w1_write_block returns void, not n_written */
		}
	}

out:
	mutex_unlock(&sl->master->bus_mutex);

	return count;
}

static int w1_ds2760_read(struct device *dev,
			  char *buf, int addr,
			  size_t count)
{
	return w1_ds2760_io(dev, buf, addr, count, 0);
}

static int w1_ds2760_write(struct device *dev,
			   char *buf,
			   int addr, size_t count)
{
	return w1_ds2760_io(dev, buf, addr, count, 1);
}

static int w1_ds2760_eeprom_cmd(struct device *dev, int addr, int cmd)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	if (!dev)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);

	if (w1_reset_select_slave(sl) == 0) {
		w1_write_8(sl->master, cmd);
		w1_write_8(sl->master, addr);
	}

	mutex_unlock(&sl->master->bus_mutex);
	return 0;
}

static int w1_ds2760_store_eeprom(struct device *dev, int addr)
{
	return w1_ds2760_eeprom_cmd(dev, addr, W1_DS2760_COPY_DATA);
}

static int w1_ds2760_recall_eeprom(struct device *dev, int addr)
{
	return w1_ds2760_eeprom_cmd(dev, addr, W1_DS2760_RECALL_DATA);
}

static ssize_t w1_slave_read(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr, char *buf,
			     loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	return w1_ds2760_read(dev, buf, off, count);
}

static BIN_ATTR_RO(w1_slave, DS2760_DATA_SIZE);

static struct bin_attribute *w1_ds2760_bin_attrs[] = {
	&bin_attr_w1_slave,
	NULL,
};

static const struct attribute_group w1_ds2760_group = {
	.bin_attrs = w1_ds2760_bin_attrs,
};

static const struct attribute_group *w1_ds2760_groups[] = {
	&w1_ds2760_group,
	NULL,
};
/* Some batteries have their rated capacity stored a N * 10 mAh, while
 * others use an index into this table. */
static int rated_capacities[] = {
	0,
	920,	/* Samsung */
	920,	/* BYD */
	920,	/* Lishen */
	920,	/* NEC */
	1440,	/* Samsung */
	1440,	/* BYD */
#ifdef CONFIG_MACH_H4700
	1800,	/* HP iPAQ hx4700 3.7V 1800mAh (359113-001) */
#else
	1440,	/* Lishen */
#endif
	1440,	/* NEC */
	2880,	/* Samsung */
	2880,	/* BYD */
	2880,	/* Lishen */
	2880,	/* NEC */
#ifdef CONFIG_MACH_H4700
	0,
	3600,	/* HP iPAQ hx4700 3.7V 3600mAh (359114-001) */
#endif
};

/* array is level at temps 0°C, 10°C, 20°C, 30°C, 40°C
 * temp is in Celsius */
static int battery_interpolate(int array[], int temp)
{
	int index, dt;

	if (temp <= 0)
		return array[0];
	if (temp >= 40)
		return array[4];

	index = temp / 10;
	dt    = temp % 10;

	return array[index] + (((array[index + 1] - array[index]) * dt) / 10);
}

static int ds2760_battery_read_status(struct ds2760_device_info *di)
{
	int ret, i, start, count, scale[5];

	if (di->update_time && time_before(jiffies, di->update_time +
					   msecs_to_jiffies(cache_time)))
		return 0;

	/* The first time we read the entire contents of SRAM/EEPROM,
	 * but after that we just read the interesting bits that change. */
	if (di->update_time == 0) {
		start = 0;
		count = DS2760_DATA_SIZE;
	} else {
		start = DS2760_VOLTAGE_MSB;
		count = DS2760_TEMP_LSB - start + 1;
	}

	ret = w1_ds2760_read(di->dev, di->raw + start, start, count);
	if (ret != count) {
		dev_warn(di->dev, "call to w1_ds2760_read failed (0x%p)\n",
			 di->dev);
		return 1;
	}

	di->update_time = jiffies;

	/* DS2760 reports voltage in units of 4.88mV, but the battery class
	 * reports in units of uV, so convert by multiplying by 4880. */
	di->voltage_raw = (di->raw[DS2760_VOLTAGE_MSB] << 3) |
			  (di->raw[DS2760_VOLTAGE_LSB] >> 5);
	di->voltage_uV = di->voltage_raw * 4880;

	/* DS2760 reports current in signed units of 0.625mA, but the battery
	 * class reports in units of µA, so convert by multiplying by 625. */
	di->current_raw =
	    (((signed char)di->raw[DS2760_CURRENT_MSB]) << 5) |
			  (di->raw[DS2760_CURRENT_LSB] >> 3);
	di->current_uA = di->current_raw * 625;

	/* DS2760 reports accumulated current in signed units of 0.25mAh. */
	di->accum_current_raw =
	    (((signed char)di->raw[DS2760_CURRENT_ACCUM_MSB]) << 8) |
			   di->raw[DS2760_CURRENT_ACCUM_LSB];
	di->accum_current_uAh = di->accum_current_raw * 250;

	/* DS2760 reports temperature in signed units of 0.125°C, but the
	 * battery class reports in units of 1/10 °C, so we convert by
	 * multiplying by .125 * 10 = 1.25. */
	di->temp_raw = (((signed char)di->raw[DS2760_TEMP_MSB]) << 3) |
				     (di->raw[DS2760_TEMP_LSB] >> 5);
	di->temp_C = di->temp_raw + (di->temp_raw / 4);

	/* At least some battery monitors (e.g. HP iPAQ) store the battery's
	 * maximum rated capacity. */
	if (di->raw[DS2760_RATED_CAPACITY] < ARRAY_SIZE(rated_capacities))
		di->rated_capacity = rated_capacities[
			(unsigned int)di->raw[DS2760_RATED_CAPACITY]];
	else
		di->rated_capacity = di->raw[DS2760_RATED_CAPACITY] * 10;

	di->rated_capacity *= 1000; /* convert to µAh */

	/* Calculate the full level at the present temperature. */
	di->full_active_uAh = di->raw[DS2760_ACTIVE_FULL] << 8 |
			      di->raw[DS2760_ACTIVE_FULL + 1];

	/* If the full_active_uAh value is not given, fall back to the rated
	 * capacity. This is likely to happen when chips are not part of the
	 * battery pack and is therefore not bootstrapped. */
	if (di->full_active_uAh == 0)
		di->full_active_uAh = di->rated_capacity / 1000L;

	scale[0] = di->full_active_uAh;
	for (i = 1; i < 5; i++)
		scale[i] = scale[i - 1] + di->raw[DS2760_ACTIVE_FULL + 1 + i];

	di->full_active_uAh = battery_interpolate(scale, di->temp_C / 10);
	di->full_active_uAh *= 1000; /* convert to µAh */

	/* Calculate the empty level at the present temperature. */
	scale[4] = di->raw[DS2760_ACTIVE_EMPTY + 4];
	for (i = 3; i >= 0; i--)
		scale[i] = scale[i + 1] + di->raw[DS2760_ACTIVE_EMPTY + i];

	di->empty_uAh = battery_interpolate(scale, di->temp_C / 10);
	di->empty_uAh *= 1000; /* convert to µAh */

	if (di->full_active_uAh == di->empty_uAh)
		di->rem_capacity = 0;
	else
		/* From Maxim Application Note 131: remaining capacity =
		 * ((ICA - Empty Value) / (Full Value - Empty Value)) x 100% */
		di->rem_capacity = ((di->accum_current_uAh - di->empty_uAh) * 100L) /
				    (di->full_active_uAh - di->empty_uAh);

	if (di->rem_capacity < 0)
		di->rem_capacity = 0;
	if (di->rem_capacity > 100)
		di->rem_capacity = 100;

	if (di->current_uA < -100L)
		di->life_sec = -((di->accum_current_uAh - di->empty_uAh) * 36L)
					/ (di->current_uA / 100L);
	else
		di->life_sec = 0;

	return 0;
}

static void ds2760_battery_set_current_accum(struct ds2760_device_info *di,
					     unsigned int acr_val)
{
	unsigned char acr[2];

	/* acr is in units of 0.25 mAh */
	acr_val *= 4L;
	acr_val /= 1000;

	acr[0] = acr_val >> 8;
	acr[1] = acr_val & 0xff;

	if (w1_ds2760_write(di->dev, acr, DS2760_CURRENT_ACCUM_MSB, 2) < 2)
		dev_warn(di->dev, "ACR write failed\n");
}

static void ds2760_battery_update_status(struct ds2760_device_info *di)
{
	int old_charge_status = di->charge_status;

	ds2760_battery_read_status(di);

	if (di->charge_status == POWER_SUPPLY_STATUS_UNKNOWN)
		di->full_counter = 0;

	if (power_supply_am_i_supplied(di->bat)) {
		if (di->current_uA > 10000) {
			di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
			di->full_counter = 0;
		} else if (di->current_uA < -5000) {
			if (di->charge_status != POWER_SUPPLY_STATUS_NOT_CHARGING)
				dev_notice(di->dev, "not enough power to "
					   "charge\n");
			di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
			di->full_counter = 0;
		} else if (di->current_uA < 10000 &&
			    di->charge_status != POWER_SUPPLY_STATUS_FULL) {

			/* Don't consider the battery to be full unless
			 * we've seen the current < 10 mA at least two
			 * consecutive times. */

			di->full_counter++;

			if (di->full_counter < 2) {
				di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
			} else {
				di->charge_status = POWER_SUPPLY_STATUS_FULL;
				ds2760_battery_set_current_accum(di,
						di->full_active_uAh);
			}
		}
	} else {
		di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;
		di->full_counter = 0;
	}

	if (di->charge_status != old_charge_status)
		power_supply_changed(di->bat);
}

static void ds2760_battery_write_status(struct ds2760_device_info *di,
					char status)
{
	if (status == di->raw[DS2760_STATUS_REG])
		return;

	w1_ds2760_write(di->dev, &status, DS2760_STATUS_WRITE_REG, 1);
	w1_ds2760_store_eeprom(di->dev, DS2760_EEPROM_BLOCK1);
	w1_ds2760_recall_eeprom(di->dev, DS2760_EEPROM_BLOCK1);
}

static void ds2760_battery_write_rated_capacity(struct ds2760_device_info *di,
						unsigned char rated_capacity)
{
	if (rated_capacity == di->raw[DS2760_RATED_CAPACITY])
		return;

	w1_ds2760_write(di->dev, &rated_capacity, DS2760_RATED_CAPACITY, 1);
	w1_ds2760_store_eeprom(di->dev, DS2760_EEPROM_BLOCK1);
	w1_ds2760_recall_eeprom(di->dev, DS2760_EEPROM_BLOCK1);
}

static void ds2760_battery_write_active_full(struct ds2760_device_info *di,
					     int active_full)
{
	unsigned char tmp[2] = {
		active_full >> 8,
		active_full & 0xff
	};

	if (tmp[0] == di->raw[DS2760_ACTIVE_FULL] &&
	    tmp[1] == di->raw[DS2760_ACTIVE_FULL + 1])
		return;

	w1_ds2760_write(di->dev, tmp, DS2760_ACTIVE_FULL, sizeof(tmp));
	w1_ds2760_store_eeprom(di->dev, DS2760_EEPROM_BLOCK0);
	w1_ds2760_recall_eeprom(di->dev, DS2760_EEPROM_BLOCK0);

	/* Write to the di->raw[] buffer directly - the DS2760_ACTIVE_FULL
	 * values won't be read back by ds2760_battery_read_status() */
	di->raw[DS2760_ACTIVE_FULL] = tmp[0];
	di->raw[DS2760_ACTIVE_FULL + 1] = tmp[1];
}

static void ds2760_battery_work(struct work_struct *work)
{
	struct ds2760_device_info *di = container_of(work,
		struct ds2760_device_info, monitor_work.work);
	const int interval = HZ * 60;

	dev_dbg(di->dev, "%s\n", __func__);

	ds2760_battery_update_status(di);
	queue_delayed_work(di->monitor_wqueue, &di->monitor_work, interval);
}

static void ds2760_battery_external_power_changed(struct power_supply *psy)
{
	struct ds2760_device_info *di = power_supply_get_drvdata(psy);

	dev_dbg(di->dev, "%s\n", __func__);

	mod_delayed_work(di->monitor_wqueue, &di->monitor_work, HZ/10);
}


static void ds2760_battery_set_charged_work(struct work_struct *work)
{
	char bias;
	struct ds2760_device_info *di = container_of(work,
		struct ds2760_device_info, set_charged_work.work);

	dev_dbg(di->dev, "%s\n", __func__);

	ds2760_battery_read_status(di);

	/* When we get notified by external circuitry that the battery is
	 * considered fully charged now, we know that there is no current
	 * flow any more. However, the ds2760's internal current meter is
	 * too inaccurate to rely on - spec say something ~15% failure.
	 * Hence, we use the current offset bias register to compensate
	 * that error.
	 */

	if (!power_supply_am_i_supplied(di->bat))
		return;

	bias = (signed char) di->current_raw +
		(signed char) di->raw[DS2760_CURRENT_OFFSET_BIAS];

	dev_dbg(di->dev, "%s: bias = %d\n", __func__, bias);

	w1_ds2760_write(di->dev, &bias, DS2760_CURRENT_OFFSET_BIAS, 1);
	w1_ds2760_store_eeprom(di->dev, DS2760_EEPROM_BLOCK1);
	w1_ds2760_recall_eeprom(di->dev, DS2760_EEPROM_BLOCK1);

	/* Write to the di->raw[] buffer directly - the CURRENT_OFFSET_BIAS
	 * value won't be read back by ds2760_battery_read_status() */
	di->raw[DS2760_CURRENT_OFFSET_BIAS] = bias;
}

static void ds2760_battery_set_charged(struct power_supply *psy)
{
	struct ds2760_device_info *di = power_supply_get_drvdata(psy);

	/* postpone the actual work by 20 secs. This is for debouncing GPIO
	 * signals and to let the current value settle. See AN4188. */
	mod_delayed_work(di->monitor_wqueue, &di->set_charged_work, HZ * 20);
}

static int ds2760_battery_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct ds2760_device_info *di = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = di->charge_status;
		return 0;
	default:
		break;
	}

	ds2760_battery_read_status(di);

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = di->voltage_uV;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = di->current_uA;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = di->rated_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = di->full_active_uAh;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
		val->intval = di->empty_uAh;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = di->accum_current_uAh;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = di->temp_C;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		val->intval = di->life_sec;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = di->rem_capacity;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ds2760_battery_set_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct ds2760_device_info *di = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		/* the interface counts in uAh, convert the value */
		ds2760_battery_write_active_full(di, val->intval / 1000L);
		break;

	case POWER_SUPPLY_PROP_CHARGE_NOW:
		/* ds2760_battery_set_current_accum() does the conversion */
		ds2760_battery_set_current_accum(di, val->intval);
		break;

	default:
		return -EPERM;
	}

	return 0;
}

static int ds2760_battery_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		return 1;

	default:
		break;
	}

	return 0;
}

static enum power_supply_property ds2760_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_EMPTY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};

static int ds2760_pm_notifier(struct notifier_block *notifier,
			      unsigned long pm_event,
			      void *unused)
{
	struct ds2760_device_info *di =
		container_of(notifier, struct ds2760_device_info, pm_notifier);

	switch (pm_event) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		di->charge_status = POWER_SUPPLY_STATUS_UNKNOWN;
		break;

	case PM_POST_RESTORE:
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		di->charge_status = POWER_SUPPLY_STATUS_UNKNOWN;
		power_supply_changed(di->bat);
		mod_delayed_work(di->monitor_wqueue, &di->monitor_work, HZ);

		break;

	case PM_RESTORE_PREPARE:
	default:
		break;
	}

	return NOTIFY_DONE;
}

static int w1_ds2760_add_slave(struct w1_slave *sl)
{
	struct power_supply_config psy_cfg = {};
	struct ds2760_device_info *di;
	struct device *dev = &sl->dev;
	int retval = 0;
	char name[32];
	char status;

	di = devm_kzalloc(dev, sizeof(*di), GFP_KERNEL);
	if (!di) {
		retval = -ENOMEM;
		goto di_alloc_failed;
	}

	snprintf(name, sizeof(name), "ds2760-battery.%d", dev->id);

	di->dev				= dev;
	di->bat_desc.name		= name;
	di->bat_desc.type		= POWER_SUPPLY_TYPE_BATTERY;
	di->bat_desc.properties		= ds2760_battery_props;
	di->bat_desc.num_properties	= ARRAY_SIZE(ds2760_battery_props);
	di->bat_desc.get_property	= ds2760_battery_get_property;
	di->bat_desc.set_property	= ds2760_battery_set_property;
	di->bat_desc.property_is_writeable =
				  ds2760_battery_property_is_writeable;
	di->bat_desc.set_charged	= ds2760_battery_set_charged;
	di->bat_desc.external_power_changed =
				  ds2760_battery_external_power_changed;

	psy_cfg.drv_data = di;

	if (dev->of_node) {
		u32 tmp;

		psy_cfg.of_node = dev->of_node;

		if (!of_property_read_bool(dev->of_node, "maxim,pmod-enabled"))
			pmod_enabled = true;

		if (!of_property_read_u32(dev->of_node,
					  "maxim,cache-time-ms", &tmp))
			cache_time = tmp;

		if (!of_property_read_u32(dev->of_node,
					  "rated-capacity-microamp-hours",
					  &tmp))
			rated_capacity = tmp / 10; /* property is in mAh */
	}

	di->charge_status = POWER_SUPPLY_STATUS_UNKNOWN;

	sl->family_data = di;

	/* enable sleep mode feature */
	ds2760_battery_read_status(di);
	status = di->raw[DS2760_STATUS_REG];
	if (pmod_enabled)
		status |= DS2760_STATUS_PMOD;
	else
		status &= ~DS2760_STATUS_PMOD;

	ds2760_battery_write_status(di, status);

	/* set rated capacity from module param or device tree */
	if (rated_capacity)
		ds2760_battery_write_rated_capacity(di, rated_capacity);

	/* set current accumulator if given as parameter.
	 * this should only be done for bootstrapping the value */
	if (current_accum)
		ds2760_battery_set_current_accum(di, current_accum);

	di->bat = power_supply_register(dev, &di->bat_desc, &psy_cfg);
	if (IS_ERR(di->bat)) {
		dev_err(di->dev, "failed to register battery\n");
		retval = PTR_ERR(di->bat);
		goto batt_failed;
	}

	INIT_DELAYED_WORK(&di->monitor_work, ds2760_battery_work);
	INIT_DELAYED_WORK(&di->set_charged_work,
			  ds2760_battery_set_charged_work);
	di->monitor_wqueue = alloc_ordered_workqueue(name, WQ_MEM_RECLAIM);
	if (!di->monitor_wqueue) {
		retval = -ESRCH;
		goto workqueue_failed;
	}
	queue_delayed_work(di->monitor_wqueue, &di->monitor_work, HZ * 1);

	di->pm_notifier.notifier_call = ds2760_pm_notifier;
	register_pm_notifier(&di->pm_notifier);

	goto success;

workqueue_failed:
	power_supply_unregister(di->bat);
batt_failed:
di_alloc_failed:
success:
	return retval;
}

static void w1_ds2760_remove_slave(struct w1_slave *sl)
{
	struct ds2760_device_info *di = sl->family_data;

	unregister_pm_notifier(&di->pm_notifier);
	cancel_delayed_work_sync(&di->monitor_work);
	cancel_delayed_work_sync(&di->set_charged_work);
	destroy_workqueue(di->monitor_wqueue);
	power_supply_unregister(di->bat);
}

#ifdef CONFIG_OF
static const struct of_device_id w1_ds2760_of_ids[] = {
	{ .compatible = "maxim,ds2760" },
	{}
};
#endif

static struct w1_family_ops w1_ds2760_fops = {
	.add_slave	= w1_ds2760_add_slave,
	.remove_slave	= w1_ds2760_remove_slave,
	.groups		= w1_ds2760_groups,
};

static struct w1_family w1_ds2760_family = {
	.fid		= W1_FAMILY_DS2760,
	.fops		= &w1_ds2760_fops,
	.of_match_table	= of_match_ptr(w1_ds2760_of_ids),
};
module_w1_family(w1_ds2760_family);

MODULE_AUTHOR("Szabolcs Gyurko <szabolcs.gyurko@tlt.hu>, "
	      "Matt Reimer <mreimer@vpop.net>, "
	      "Anton Vorontsov <cbou@mail.ru>");
MODULE_DESCRIPTION("1-wire Driver Dallas 2760 battery monitor chip");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_DS2760));
