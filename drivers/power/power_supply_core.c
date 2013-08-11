/*
 *  Universal power supply monitor class
 *
 *  Copyright © 2007  Anton Vorontsov <cbou@mail.ru>
 *  Copyright © 2004  Szabolcs Gyurko
 *  Copyright © 2003  Ian Molton <spyro@f2s.com>
 *
 *  Modified: 2004, Oct     Szabolcs Gyurko
 *
 *  You may use this code as per GPL version 2
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/power_supply.h>
#include <linux/thermal.h>
#include "power_supply.h"

/* exported for the APM Power driver, APM emulation */
struct class *power_supply_class;
EXPORT_SYMBOL_GPL(power_supply_class);

static struct device_type power_supply_dev_type;

static bool __power_supply_is_supplied_by(struct power_supply *supplier,
					 struct power_supply *supply)
{
	int i;

	if (!supply->supplied_from && !supplier->supplied_to)
		return false;

	/* Support both supplied_to and supplied_from modes */
	if (supply->supplied_from) {
		if (!supplier->name)
			return false;
		for (i = 0; i < supply->num_supplies; i++)
			if (!strcmp(supplier->name, supply->supplied_from[i]))
				return true;
	} else {
		if (!supply->name)
			return false;
		for (i = 0; i < supplier->num_supplicants; i++)
			if (!strcmp(supplier->supplied_to[i], supply->name))
				return true;
	}

	return false;
}

static int __power_supply_changed_work(struct device *dev, void *data)
{
	struct power_supply *psy = (struct power_supply *)data;
	struct power_supply *pst = dev_get_drvdata(dev);

	if (__power_supply_is_supplied_by(psy, pst)) {
		if (pst->external_power_changed)
			pst->external_power_changed(pst);
	}

	return 0;
}

static void power_supply_changed_work(struct work_struct *work)
{
	unsigned long flags;
	struct power_supply *psy = container_of(work, struct power_supply,
						changed_work);

	dev_dbg(psy->dev, "%s\n", __func__);

	spin_lock_irqsave(&psy->changed_lock, flags);
	if (psy->changed) {
		psy->changed = false;
		spin_unlock_irqrestore(&psy->changed_lock, flags);

		class_for_each_device(power_supply_class, NULL, psy,
				      __power_supply_changed_work);

		power_supply_update_leds(psy);

		kobject_uevent(&psy->dev->kobj, KOBJ_CHANGE);
		spin_lock_irqsave(&psy->changed_lock, flags);
	}
	if (!psy->changed)
		pm_relax(psy->dev);
	spin_unlock_irqrestore(&psy->changed_lock, flags);
}

void power_supply_changed(struct power_supply *psy)
{
	unsigned long flags;

	dev_dbg(psy->dev, "%s\n", __func__);

	spin_lock_irqsave(&psy->changed_lock, flags);
	psy->changed = true;
	pm_stay_awake(psy->dev);
	spin_unlock_irqrestore(&psy->changed_lock, flags);
	schedule_work(&psy->changed_work);
}
EXPORT_SYMBOL_GPL(power_supply_changed);

#ifdef CONFIG_OF
#include <linux/of.h>

static int __power_supply_populate_supplied_from(struct device *dev,
						 void *data)
{
	struct power_supply *psy = (struct power_supply *)data;
	struct power_supply *epsy = dev_get_drvdata(dev);
	struct device_node *np;
	int i = 0;

	do {
		np = of_parse_phandle(psy->of_node, "power-supplies", i++);
		if (!np)
			continue;

		if (np == epsy->of_node) {
			dev_info(psy->dev, "%s: Found supply : %s\n",
				psy->name, epsy->name);
			psy->supplied_from[i-1] = (char *)epsy->name;
			psy->num_supplies++;
			break;
		}
	} while (np);

	return 0;
}

static int power_supply_populate_supplied_from(struct power_supply *psy)
{
	int error;

	error = class_for_each_device(power_supply_class, NULL, psy,
				      __power_supply_populate_supplied_from);

	dev_dbg(psy->dev, "%s %d\n", __func__, error);

	return error;
}

static int  __power_supply_find_supply_from_node(struct device *dev,
						 void *data)
{
	struct device_node *np = (struct device_node *)data;
	struct power_supply *epsy = dev_get_drvdata(dev);

	/* return error breaks out of class_for_each_device loop */
	if (epsy->of_node == np)
		return -EINVAL;

	return 0;
}

static int power_supply_find_supply_from_node(struct device_node *supply_node)
{
	int error;
	struct device *dev;
	struct class_dev_iter iter;

	/*
	 * Use iterator to see if any other device is registered.
	 * This is required since class_for_each_device returns 0
	 * if there are no devices registered.
	 */
	class_dev_iter_init(&iter, power_supply_class, NULL, NULL);
	dev = class_dev_iter_next(&iter);

	if (!dev)
		return -EPROBE_DEFER;

	/*
	 * We have to treat the return value as inverted, because if
	 * we return error on not found, then it won't continue looking.
	 * So we trick it by returning error on success to stop looking
	 * once the matching device is found.
	 */
	error = class_for_each_device(power_supply_class, NULL, supply_node,
				       __power_supply_find_supply_from_node);

	return error ? 0 : -EPROBE_DEFER;
}

static int power_supply_check_supplies(struct power_supply *psy)
{
	struct device_node *np;
	int cnt = 0;

	/* If there is already a list honor it */
	if (psy->supplied_from && psy->num_supplies > 0)
		return 0;

	/* No device node found, nothing to do */
	if (!psy->of_node)
		return 0;

	do {
		int ret;

		np = of_parse_phandle(psy->of_node, "power-supplies", cnt++);
		if (!np)
			continue;

		ret = power_supply_find_supply_from_node(np);
		if (ret) {
			dev_dbg(psy->dev, "Failed to find supply, defer!\n");
			return -EPROBE_DEFER;
		}
	} while (np);

	/* All supplies found, allocate char ** array for filling */
	psy->supplied_from = devm_kzalloc(psy->dev, sizeof(psy->supplied_from),
					  GFP_KERNEL);
	if (!psy->supplied_from) {
		dev_err(psy->dev, "Couldn't allocate memory for supply list\n");
		return -ENOMEM;
	}

	*psy->supplied_from = devm_kzalloc(psy->dev, sizeof(char *) * cnt,
					   GFP_KERNEL);
	if (!*psy->supplied_from) {
		dev_err(psy->dev, "Couldn't allocate memory for supply list\n");
		return -ENOMEM;
	}

	return power_supply_populate_supplied_from(psy);
}
#else
static inline int power_supply_check_supplies(struct power_supply *psy)
{
	return 0;
}
#endif

static int __power_supply_am_i_supplied(struct device *dev, void *data)
{
	union power_supply_propval ret = {0,};
	struct power_supply *psy = (struct power_supply *)data;
	struct power_supply *epsy = dev_get_drvdata(dev);

	if (__power_supply_is_supplied_by(epsy, psy))
		if (!epsy->get_property(epsy, POWER_SUPPLY_PROP_ONLINE, &ret)) {
			if (ret.intval)
				return ret.intval;
		}

	return 0;
}

int power_supply_am_i_supplied(struct power_supply *psy)
{
	int error;

	error = class_for_each_device(power_supply_class, NULL, psy,
				      __power_supply_am_i_supplied);

	dev_dbg(psy->dev, "%s %d\n", __func__, error);

	return error;
}
EXPORT_SYMBOL_GPL(power_supply_am_i_supplied);

static int __power_supply_is_system_supplied(struct device *dev, void *data)
{
	union power_supply_propval ret = {0,};
	struct power_supply *psy = dev_get_drvdata(dev);
	unsigned int *count = data;

	(*count)++;
	if (psy->type != POWER_SUPPLY_TYPE_BATTERY) {
		if (psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &ret))
			return 0;
		if (ret.intval)
			return ret.intval;
	}
	return 0;
}

int power_supply_is_system_supplied(void)
{
	int error;
	unsigned int count = 0;

	error = class_for_each_device(power_supply_class, NULL, &count,
				      __power_supply_is_system_supplied);

	/*
	 * If no power class device was found at all, most probably we are
	 * running on a desktop system, so assume we are on mains power.
	 */
	if (count == 0)
		return 1;

	return error;
}
EXPORT_SYMBOL_GPL(power_supply_is_system_supplied);

int power_supply_set_battery_charged(struct power_supply *psy)
{
	if (psy->type == POWER_SUPPLY_TYPE_BATTERY && psy->set_charged) {
		psy->set_charged(psy);
		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(power_supply_set_battery_charged);

static int power_supply_match_device_by_name(struct device *dev, const void *data)
{
	const char *name = data;
	struct power_supply *psy = dev_get_drvdata(dev);

	return strcmp(psy->name, name) == 0;
}

struct power_supply *power_supply_get_by_name(const char *name)
{
	struct device *dev = class_find_device(power_supply_class, NULL, name,
					power_supply_match_device_by_name);

	return dev ? dev_get_drvdata(dev) : NULL;
}
EXPORT_SYMBOL_GPL(power_supply_get_by_name);

int power_supply_powers(struct power_supply *psy, struct device *dev)
{
	return sysfs_create_link(&psy->dev->kobj, &dev->kobj, "powers");
}
EXPORT_SYMBOL_GPL(power_supply_powers);

static void power_supply_dev_release(struct device *dev)
{
	pr_debug("device: '%s': %s\n", dev_name(dev), __func__);
	kfree(dev);
}

#ifdef CONFIG_THERMAL
static int power_supply_read_temp(struct thermal_zone_device *tzd,
		unsigned long *temp)
{
	struct power_supply *psy;
	union power_supply_propval val;
	int ret;

	WARN_ON(tzd == NULL);
	psy = tzd->devdata;
	ret = psy->get_property(psy, POWER_SUPPLY_PROP_TEMP, &val);

	/* Convert tenths of degree Celsius to milli degree Celsius. */
	if (!ret)
		*temp = val.intval * 100;

	return ret;
}

static struct thermal_zone_device_ops psy_tzd_ops = {
	.get_temp = power_supply_read_temp,
};

static int psy_register_thermal(struct power_supply *psy)
{
	int i;

	/* Register battery zone device psy reports temperature */
	for (i = 0; i < psy->num_properties; i++) {
		if (psy->properties[i] == POWER_SUPPLY_PROP_TEMP) {
			psy->tzd = thermal_zone_device_register(psy->name, 0, 0,
					psy, &psy_tzd_ops, NULL, 0, 0);
			if (IS_ERR(psy->tzd))
				return PTR_ERR(psy->tzd);
			break;
		}
	}
	return 0;
}

static void psy_unregister_thermal(struct power_supply *psy)
{
	if (IS_ERR_OR_NULL(psy->tzd))
		return;
	thermal_zone_device_unregister(psy->tzd);
}

/* thermal cooling device callbacks */
static int ps_get_max_charge_cntl_limit(struct thermal_cooling_device *tcd,
					unsigned long *state)
{
	struct power_supply *psy;
	union power_supply_propval val;
	int ret;

	psy = tcd->devdata;
	ret = psy->get_property(psy,
		POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX, &val);
	if (!ret)
		*state = val.intval;

	return ret;
}

static int ps_get_cur_chrage_cntl_limit(struct thermal_cooling_device *tcd,
					unsigned long *state)
{
	struct power_supply *psy;
	union power_supply_propval val;
	int ret;

	psy = tcd->devdata;
	ret = psy->get_property(psy,
		POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT, &val);
	if (!ret)
		*state = val.intval;

	return ret;
}

static int ps_set_cur_charge_cntl_limit(struct thermal_cooling_device *tcd,
					unsigned long state)
{
	struct power_supply *psy;
	union power_supply_propval val;
	int ret;

	psy = tcd->devdata;
	val.intval = state;
	ret = psy->set_property(psy,
		POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT, &val);

	return ret;
}

static struct thermal_cooling_device_ops psy_tcd_ops = {
	.get_max_state = ps_get_max_charge_cntl_limit,
	.get_cur_state = ps_get_cur_chrage_cntl_limit,
	.set_cur_state = ps_set_cur_charge_cntl_limit,
};

static int psy_register_cooler(struct power_supply *psy)
{
	int i;

	/* Register for cooling device if psy can control charging */
	for (i = 0; i < psy->num_properties; i++) {
		if (psy->properties[i] ==
				POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT) {
			psy->tcd = thermal_cooling_device_register(
							(char *)psy->name,
							psy, &psy_tcd_ops);
			if (IS_ERR(psy->tcd))
				return PTR_ERR(psy->tcd);
			break;
		}
	}
	return 0;
}

static void psy_unregister_cooler(struct power_supply *psy)
{
	if (IS_ERR_OR_NULL(psy->tcd))
		return;
	thermal_cooling_device_unregister(psy->tcd);
}
#else
static int psy_register_thermal(struct power_supply *psy)
{
	return 0;
}

static void psy_unregister_thermal(struct power_supply *psy)
{
}

static int psy_register_cooler(struct power_supply *psy)
{
	return 0;
}

static void psy_unregister_cooler(struct power_supply *psy)
{
}
#endif

int power_supply_register(struct device *parent, struct power_supply *psy)
{
	struct device *dev;
	int rc;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	device_initialize(dev);

	dev->class = power_supply_class;
	dev->type = &power_supply_dev_type;
	dev->parent = parent;
	dev->release = power_supply_dev_release;
	dev_set_drvdata(dev, psy);
	psy->dev = dev;

	INIT_WORK(&psy->changed_work, power_supply_changed_work);

	rc = power_supply_check_supplies(psy);
	if (rc) {
		dev_info(dev, "Not all required supplies found, defer probe\n");
		goto check_supplies_failed;
	}

	rc = kobject_set_name(&dev->kobj, "%s", psy->name);
	if (rc)
		goto kobject_set_name_failed;

	rc = device_add(dev);
	if (rc)
		goto device_add_failed;

	spin_lock_init(&psy->changed_lock);
	rc = device_init_wakeup(dev, true);
	if (rc)
		goto wakeup_init_failed;

	rc = psy_register_thermal(psy);
	if (rc)
		goto register_thermal_failed;

	rc = psy_register_cooler(psy);
	if (rc)
		goto register_cooler_failed;

	rc = power_supply_create_triggers(psy);
	if (rc)
		goto create_triggers_failed;

	power_supply_changed(psy);

	goto success;

create_triggers_failed:
	psy_unregister_cooler(psy);
register_cooler_failed:
	psy_unregister_thermal(psy);
register_thermal_failed:
wakeup_init_failed:
	device_del(dev);
kobject_set_name_failed:
device_add_failed:
check_supplies_failed:
	put_device(dev);
success:
	return rc;
}
EXPORT_SYMBOL_GPL(power_supply_register);

void power_supply_unregister(struct power_supply *psy)
{
	cancel_work_sync(&psy->changed_work);
	sysfs_remove_link(&psy->dev->kobj, "powers");
	power_supply_remove_triggers(psy);
	psy_unregister_cooler(psy);
	psy_unregister_thermal(psy);
	device_unregister(psy->dev);
}
EXPORT_SYMBOL_GPL(power_supply_unregister);

static int __init power_supply_class_init(void)
{
	power_supply_class = class_create(THIS_MODULE, "power_supply");

	if (IS_ERR(power_supply_class))
		return PTR_ERR(power_supply_class);

	power_supply_class->dev_uevent = power_supply_uevent;
	power_supply_init_attrs(&power_supply_dev_type);

	return 0;
}

static void __exit power_supply_class_exit(void)
{
	class_destroy(power_supply_class);
}

subsys_initcall(power_supply_class_init);
module_exit(power_supply_class_exit);

MODULE_DESCRIPTION("Universal power supply monitor class");
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>, "
	      "Szabolcs Gyurko, "
	      "Anton Vorontsov <cbou@mail.ru>");
MODULE_LICENSE("GPL");
