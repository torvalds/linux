// SPDX-License-Identifier: GPL-2.0-or-later
//
// core.c  --  Voltage/Current Regulator framework.
//
// Copyright 2007, 2008 Wolfson Microelectronics PLC.
// Copyright 2008 SlimLogic Ltd.
//
// Author: Liam Girdwood <lrg@slimlogic.co.uk>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/async.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/coupler.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/regulator.h>

#include "dummy.h"
#include "internal.h"

static DEFINE_WW_CLASS(regulator_ww_class);
static DEFINE_MUTEX(regulator_nesting_mutex);
static DEFINE_MUTEX(regulator_list_mutex);
static LIST_HEAD(regulator_map_list);
static LIST_HEAD(regulator_ena_gpio_list);
static LIST_HEAD(regulator_supply_alias_list);
static LIST_HEAD(regulator_coupler_list);
static bool has_full_constraints;

static struct dentry *debugfs_root;

/*
 * struct regulator_map
 *
 * Used to provide symbolic supply names to devices.
 */
struct regulator_map {
	struct list_head list;
	const char *dev_name;   /* The dev_name() for the consumer */
	const char *supply;
	struct regulator_dev *regulator;
};

/*
 * struct regulator_enable_gpio
 *
 * Management for shared enable GPIO pin
 */
struct regulator_enable_gpio {
	struct list_head list;
	struct gpio_desc *gpiod;
	u32 enable_count;	/* a number of enabled shared GPIO */
	u32 request_count;	/* a number of requested shared GPIO */
};

/*
 * struct regulator_supply_alias
 *
 * Used to map lookups for a supply onto an alternative device.
 */
struct regulator_supply_alias {
	struct list_head list;
	struct device *src_dev;
	const char *src_supply;
	struct device *alias_dev;
	const char *alias_supply;
};

static int _regulator_is_enabled(struct regulator_dev *rdev);
static int _regulator_disable(struct regulator *regulator);
static int _regulator_get_error_flags(struct regulator_dev *rdev, unsigned int *flags);
static int _regulator_get_current_limit(struct regulator_dev *rdev);
static unsigned int _regulator_get_mode(struct regulator_dev *rdev);
static int _notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data);
static int _regulator_do_set_voltage(struct regulator_dev *rdev,
				     int min_uV, int max_uV);
static int regulator_balance_voltage(struct regulator_dev *rdev,
				     suspend_state_t state);
static struct regulator *create_regulator(struct regulator_dev *rdev,
					  struct device *dev,
					  const char *supply_name);
static void destroy_regulator(struct regulator *regulator);
static void _regulator_put(struct regulator *regulator);

const char *rdev_get_name(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->name)
		return rdev->constraints->name;
	else if (rdev->desc->name)
		return rdev->desc->name;
	else
		return "";
}
EXPORT_SYMBOL_GPL(rdev_get_name);

static bool have_full_constraints(void)
{
	return has_full_constraints || of_have_populated_dt();
}

static bool regulator_ops_is_valid(struct regulator_dev *rdev, int ops)
{
	if (!rdev->constraints) {
		rdev_err(rdev, "no constraints\n");
		return false;
	}

	if (rdev->constraints->valid_ops_mask & ops)
		return true;

	return false;
}

/**
 * regulator_lock_nested - lock a single regulator
 * @rdev:		regulator source
 * @ww_ctx:		w/w mutex acquire context
 *
 * This function can be called many times by one task on
 * a single regulator and its mutex will be locked only
 * once. If a task, which is calling this function is other
 * than the one, which initially locked the mutex, it will
 * wait on mutex.
 */
static inline int regulator_lock_nested(struct regulator_dev *rdev,
					struct ww_acquire_ctx *ww_ctx)
{
	bool lock = false;
	int ret = 0;

	mutex_lock(&regulator_nesting_mutex);

	if (!ww_mutex_trylock(&rdev->mutex, ww_ctx)) {
		if (rdev->mutex_owner == current)
			rdev->ref_cnt++;
		else
			lock = true;

		if (lock) {
			mutex_unlock(&regulator_nesting_mutex);
			ret = ww_mutex_lock(&rdev->mutex, ww_ctx);
			mutex_lock(&regulator_nesting_mutex);
		}
	} else {
		lock = true;
	}

	if (lock && ret != -EDEADLK) {
		rdev->ref_cnt++;
		rdev->mutex_owner = current;
	}

	mutex_unlock(&regulator_nesting_mutex);

	return ret;
}

/**
 * regulator_lock - lock a single regulator
 * @rdev:		regulator source
 *
 * This function can be called many times by one task on
 * a single regulator and its mutex will be locked only
 * once. If a task, which is calling this function is other
 * than the one, which initially locked the mutex, it will
 * wait on mutex.
 */
static void regulator_lock(struct regulator_dev *rdev)
{
	regulator_lock_nested(rdev, NULL);
}

/**
 * regulator_unlock - unlock a single regulator
 * @rdev:		regulator_source
 *
 * This function unlocks the mutex when the
 * reference counter reaches 0.
 */
static void regulator_unlock(struct regulator_dev *rdev)
{
	mutex_lock(&regulator_nesting_mutex);

	if (--rdev->ref_cnt == 0) {
		rdev->mutex_owner = NULL;
		ww_mutex_unlock(&rdev->mutex);
	}

	WARN_ON_ONCE(rdev->ref_cnt < 0);

	mutex_unlock(&regulator_nesting_mutex);
}

static bool regulator_supply_is_couple(struct regulator_dev *rdev)
{
	struct regulator_dev *c_rdev;
	int i;

	for (i = 1; i < rdev->coupling_desc.n_coupled; i++) {
		c_rdev = rdev->coupling_desc.coupled_rdevs[i];

		if (rdev->supply->rdev == c_rdev)
			return true;
	}

	return false;
}

static void regulator_unlock_recursive(struct regulator_dev *rdev,
				       unsigned int n_coupled)
{
	struct regulator_dev *c_rdev, *supply_rdev;
	int i, supply_n_coupled;

	for (i = n_coupled; i > 0; i--) {
		c_rdev = rdev->coupling_desc.coupled_rdevs[i - 1];

		if (!c_rdev)
			continue;

		if (c_rdev->supply && !regulator_supply_is_couple(c_rdev)) {
			supply_rdev = c_rdev->supply->rdev;
			supply_n_coupled = supply_rdev->coupling_desc.n_coupled;

			regulator_unlock_recursive(supply_rdev,
						   supply_n_coupled);
		}

		regulator_unlock(c_rdev);
	}
}

static int regulator_lock_recursive(struct regulator_dev *rdev,
				    struct regulator_dev **new_contended_rdev,
				    struct regulator_dev **old_contended_rdev,
				    struct ww_acquire_ctx *ww_ctx)
{
	struct regulator_dev *c_rdev;
	int i, err;

	for (i = 0; i < rdev->coupling_desc.n_coupled; i++) {
		c_rdev = rdev->coupling_desc.coupled_rdevs[i];

		if (!c_rdev)
			continue;

		if (c_rdev != *old_contended_rdev) {
			err = regulator_lock_nested(c_rdev, ww_ctx);
			if (err) {
				if (err == -EDEADLK) {
					*new_contended_rdev = c_rdev;
					goto err_unlock;
				}

				/* shouldn't happen */
				WARN_ON_ONCE(err != -EALREADY);
			}
		} else {
			*old_contended_rdev = NULL;
		}

		if (c_rdev->supply && !regulator_supply_is_couple(c_rdev)) {
			err = regulator_lock_recursive(c_rdev->supply->rdev,
						       new_contended_rdev,
						       old_contended_rdev,
						       ww_ctx);
			if (err) {
				regulator_unlock(c_rdev);
				goto err_unlock;
			}
		}
	}

	return 0;

err_unlock:
	regulator_unlock_recursive(rdev, i);

	return err;
}

/**
 * regulator_unlock_dependent - unlock regulator's suppliers and coupled
 *				regulators
 * @rdev:			regulator source
 * @ww_ctx:			w/w mutex acquire context
 *
 * Unlock all regulators related with rdev by coupling or supplying.
 */
static void regulator_unlock_dependent(struct regulator_dev *rdev,
				       struct ww_acquire_ctx *ww_ctx)
{
	regulator_unlock_recursive(rdev, rdev->coupling_desc.n_coupled);
	ww_acquire_fini(ww_ctx);
}

/**
 * regulator_lock_dependent - lock regulator's suppliers and coupled regulators
 * @rdev:			regulator source
 * @ww_ctx:			w/w mutex acquire context
 *
 * This function as a wrapper on regulator_lock_recursive(), which locks
 * all regulators related with rdev by coupling or supplying.
 */
static void regulator_lock_dependent(struct regulator_dev *rdev,
				     struct ww_acquire_ctx *ww_ctx)
{
	struct regulator_dev *new_contended_rdev = NULL;
	struct regulator_dev *old_contended_rdev = NULL;
	int err;

	mutex_lock(&regulator_list_mutex);

	ww_acquire_init(ww_ctx, &regulator_ww_class);

	do {
		if (new_contended_rdev) {
			ww_mutex_lock_slow(&new_contended_rdev->mutex, ww_ctx);
			old_contended_rdev = new_contended_rdev;
			old_contended_rdev->ref_cnt++;
		}

		err = regulator_lock_recursive(rdev,
					       &new_contended_rdev,
					       &old_contended_rdev,
					       ww_ctx);

		if (old_contended_rdev)
			regulator_unlock(old_contended_rdev);

	} while (err == -EDEADLK);

	ww_acquire_done(ww_ctx);

	mutex_unlock(&regulator_list_mutex);
}

/**
 * of_get_child_regulator - get a child regulator device node
 * based on supply name
 * @parent: Parent device node
 * @prop_name: Combination regulator supply name and "-supply"
 *
 * Traverse all child nodes.
 * Extract the child regulator device node corresponding to the supply name.
 * returns the device node corresponding to the regulator if found, else
 * returns NULL.
 */
static struct device_node *of_get_child_regulator(struct device_node *parent,
						  const char *prop_name)
{
	struct device_node *regnode = NULL;
	struct device_node *child = NULL;

	for_each_child_of_node(parent, child) {
		regnode = of_parse_phandle(child, prop_name, 0);

		if (!regnode) {
			regnode = of_get_child_regulator(child, prop_name);
			if (regnode)
				goto err_node_put;
		} else {
			goto err_node_put;
		}
	}
	return NULL;

err_node_put:
	of_node_put(child);
	return regnode;
}

/**
 * of_get_regulator - get a regulator device node based on supply name
 * @dev: Device pointer for the consumer (of regulator) device
 * @supply: regulator supply name
 *
 * Extract the regulator device node corresponding to the supply name.
 * returns the device node corresponding to the regulator if found, else
 * returns NULL.
 */
static struct device_node *of_get_regulator(struct device *dev, const char *supply)
{
	struct device_node *regnode = NULL;
	char prop_name[64]; /* 64 is max size of property name */

	dev_dbg(dev, "Looking up %s-supply from device tree\n", supply);

	snprintf(prop_name, 64, "%s-supply", supply);
	regnode = of_parse_phandle(dev->of_node, prop_name, 0);

	if (!regnode) {
		regnode = of_get_child_regulator(dev->of_node, prop_name);
		if (regnode)
			return regnode;

		dev_dbg(dev, "Looking up %s property in node %pOF failed\n",
				prop_name, dev->of_node);
		return NULL;
	}
	return regnode;
}

/* Platform voltage constraint check */
int regulator_check_voltage(struct regulator_dev *rdev,
			    int *min_uV, int *max_uV)
{
	BUG_ON(*min_uV > *max_uV);

	if (!regulator_ops_is_valid(rdev, REGULATOR_CHANGE_VOLTAGE)) {
		rdev_err(rdev, "voltage operation not allowed\n");
		return -EPERM;
	}

	if (*max_uV > rdev->constraints->max_uV)
		*max_uV = rdev->constraints->max_uV;
	if (*min_uV < rdev->constraints->min_uV)
		*min_uV = rdev->constraints->min_uV;

	if (*min_uV > *max_uV) {
		rdev_err(rdev, "unsupportable voltage range: %d-%duV\n",
			 *min_uV, *max_uV);
		return -EINVAL;
	}

	return 0;
}

/* return 0 if the state is valid */
static int regulator_check_states(suspend_state_t state)
{
	return (state > PM_SUSPEND_MAX || state == PM_SUSPEND_TO_IDLE);
}

/* Make sure we select a voltage that suits the needs of all
 * regulator consumers
 */
int regulator_check_consumers(struct regulator_dev *rdev,
			      int *min_uV, int *max_uV,
			      suspend_state_t state)
{
	struct regulator *regulator;
	struct regulator_voltage *voltage;

	list_for_each_entry(regulator, &rdev->consumer_list, list) {
		voltage = &regulator->voltage[state];
		/*
		 * Assume consumers that didn't say anything are OK
		 * with anything in the constraint range.
		 */
		if (!voltage->min_uV && !voltage->max_uV)
			continue;

		if (*max_uV > voltage->max_uV)
			*max_uV = voltage->max_uV;
		if (*min_uV < voltage->min_uV)
			*min_uV = voltage->min_uV;
	}

	if (*min_uV > *max_uV) {
		rdev_err(rdev, "Restricting voltage, %u-%uuV\n",
			*min_uV, *max_uV);
		return -EINVAL;
	}

	return 0;
}

/* current constraint check */
static int regulator_check_current_limit(struct regulator_dev *rdev,
					int *min_uA, int *max_uA)
{
	BUG_ON(*min_uA > *max_uA);

	if (!regulator_ops_is_valid(rdev, REGULATOR_CHANGE_CURRENT)) {
		rdev_err(rdev, "current operation not allowed\n");
		return -EPERM;
	}

	if (*max_uA > rdev->constraints->max_uA)
		*max_uA = rdev->constraints->max_uA;
	if (*min_uA < rdev->constraints->min_uA)
		*min_uA = rdev->constraints->min_uA;

	if (*min_uA > *max_uA) {
		rdev_err(rdev, "unsupportable current range: %d-%duA\n",
			 *min_uA, *max_uA);
		return -EINVAL;
	}

	return 0;
}

/* operating mode constraint check */
static int regulator_mode_constrain(struct regulator_dev *rdev,
				    unsigned int *mode)
{
	switch (*mode) {
	case REGULATOR_MODE_FAST:
	case REGULATOR_MODE_NORMAL:
	case REGULATOR_MODE_IDLE:
	case REGULATOR_MODE_STANDBY:
		break;
	default:
		rdev_err(rdev, "invalid mode %x specified\n", *mode);
		return -EINVAL;
	}

	if (!regulator_ops_is_valid(rdev, REGULATOR_CHANGE_MODE)) {
		rdev_err(rdev, "mode operation not allowed\n");
		return -EPERM;
	}

	/* The modes are bitmasks, the most power hungry modes having
	 * the lowest values. If the requested mode isn't supported
	 * try higher modes.
	 */
	while (*mode) {
		if (rdev->constraints->valid_modes_mask & *mode)
			return 0;
		*mode /= 2;
	}

	return -EINVAL;
}

static inline struct regulator_state *
regulator_get_suspend_state(struct regulator_dev *rdev, suspend_state_t state)
{
	if (rdev->constraints == NULL)
		return NULL;

	switch (state) {
	case PM_SUSPEND_STANDBY:
		return &rdev->constraints->state_standby;
	case PM_SUSPEND_MEM:
		return &rdev->constraints->state_mem;
	case PM_SUSPEND_MAX:
		return &rdev->constraints->state_disk;
	default:
		return NULL;
	}
}

static const struct regulator_state *
regulator_get_suspend_state_check(struct regulator_dev *rdev, suspend_state_t state)
{
	const struct regulator_state *rstate;

	rstate = regulator_get_suspend_state(rdev, state);
	if (rstate == NULL)
		return NULL;

	/* If we have no suspend mode configuration don't set anything;
	 * only warn if the driver implements set_suspend_voltage or
	 * set_suspend_mode callback.
	 */
	if (rstate->enabled != ENABLE_IN_SUSPEND &&
	    rstate->enabled != DISABLE_IN_SUSPEND) {
		if (rdev->desc->ops->set_suspend_voltage ||
		    rdev->desc->ops->set_suspend_mode)
			rdev_warn(rdev, "No configuration\n");
		return NULL;
	}

	return rstate;
}

static ssize_t microvolts_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	int uV;

	regulator_lock(rdev);
	uV = regulator_get_voltage_rdev(rdev);
	regulator_unlock(rdev);

	if (uV < 0)
		return uV;
	return sprintf(buf, "%d\n", uV);
}
static DEVICE_ATTR_RO(microvolts);

static ssize_t microamps_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", _regulator_get_current_limit(rdev));
}
static DEVICE_ATTR_RO(microamps);

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", rdev_get_name(rdev));
}
static DEVICE_ATTR_RO(name);

static const char *regulator_opmode_to_str(int mode)
{
	switch (mode) {
	case REGULATOR_MODE_FAST:
		return "fast";
	case REGULATOR_MODE_NORMAL:
		return "normal";
	case REGULATOR_MODE_IDLE:
		return "idle";
	case REGULATOR_MODE_STANDBY:
		return "standby";
	}
	return "unknown";
}

static ssize_t regulator_print_opmode(char *buf, int mode)
{
	return sprintf(buf, "%s\n", regulator_opmode_to_str(mode));
}

static ssize_t opmode_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf, _regulator_get_mode(rdev));
}
static DEVICE_ATTR_RO(opmode);

static ssize_t regulator_print_state(char *buf, int state)
{
	if (state > 0)
		return sprintf(buf, "enabled\n");
	else if (state == 0)
		return sprintf(buf, "disabled\n");
	else
		return sprintf(buf, "unknown\n");
}

static ssize_t state_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	ssize_t ret;

	regulator_lock(rdev);
	ret = regulator_print_state(buf, _regulator_is_enabled(rdev));
	regulator_unlock(rdev);

	return ret;
}
static DEVICE_ATTR_RO(state);

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	int status;
	char *label;

	status = rdev->desc->ops->get_status(rdev);
	if (status < 0)
		return status;

	switch (status) {
	case REGULATOR_STATUS_OFF:
		label = "off";
		break;
	case REGULATOR_STATUS_ON:
		label = "on";
		break;
	case REGULATOR_STATUS_ERROR:
		label = "error";
		break;
	case REGULATOR_STATUS_FAST:
		label = "fast";
		break;
	case REGULATOR_STATUS_NORMAL:
		label = "normal";
		break;
	case REGULATOR_STATUS_IDLE:
		label = "idle";
		break;
	case REGULATOR_STATUS_STANDBY:
		label = "standby";
		break;
	case REGULATOR_STATUS_BYPASS:
		label = "bypass";
		break;
	case REGULATOR_STATUS_UNDEFINED:
		label = "undefined";
		break;
	default:
		return -ERANGE;
	}

	return sprintf(buf, "%s\n", label);
}
static DEVICE_ATTR_RO(status);

static ssize_t min_microamps_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->min_uA);
}
static DEVICE_ATTR_RO(min_microamps);

static ssize_t max_microamps_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->max_uA);
}
static DEVICE_ATTR_RO(max_microamps);

static ssize_t min_microvolts_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->min_uV);
}
static DEVICE_ATTR_RO(min_microvolts);

static ssize_t max_microvolts_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->max_uV);
}
static DEVICE_ATTR_RO(max_microvolts);

static ssize_t requested_microamps_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	struct regulator *regulator;
	int uA = 0;

	regulator_lock(rdev);
	list_for_each_entry(regulator, &rdev->consumer_list, list) {
		if (regulator->enable_count)
			uA += regulator->uA_load;
	}
	regulator_unlock(rdev);
	return sprintf(buf, "%d\n", uA);
}
static DEVICE_ATTR_RO(requested_microamps);

static ssize_t num_users_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", rdev->use_count);
}
static DEVICE_ATTR_RO(num_users);

static ssize_t type_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	switch (rdev->desc->type) {
	case REGULATOR_VOLTAGE:
		return sprintf(buf, "voltage\n");
	case REGULATOR_CURRENT:
		return sprintf(buf, "current\n");
	}
	return sprintf(buf, "unknown\n");
}
static DEVICE_ATTR_RO(type);

static ssize_t suspend_mem_microvolts_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", rdev->constraints->state_mem.uV);
}
static DEVICE_ATTR_RO(suspend_mem_microvolts);

static ssize_t suspend_disk_microvolts_show(struct device *dev,
					    struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", rdev->constraints->state_disk.uV);
}
static DEVICE_ATTR_RO(suspend_disk_microvolts);

static ssize_t suspend_standby_microvolts_show(struct device *dev,
					       struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", rdev->constraints->state_standby.uV);
}
static DEVICE_ATTR_RO(suspend_standby_microvolts);

static ssize_t suspend_mem_mode_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf,
		rdev->constraints->state_mem.mode);
}
static DEVICE_ATTR_RO(suspend_mem_mode);

static ssize_t suspend_disk_mode_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf,
		rdev->constraints->state_disk.mode);
}
static DEVICE_ATTR_RO(suspend_disk_mode);

static ssize_t suspend_standby_mode_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf,
		rdev->constraints->state_standby.mode);
}
static DEVICE_ATTR_RO(suspend_standby_mode);

static ssize_t suspend_mem_state_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_state(buf,
			rdev->constraints->state_mem.enabled);
}
static DEVICE_ATTR_RO(suspend_mem_state);

static ssize_t suspend_disk_state_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_state(buf,
			rdev->constraints->state_disk.enabled);
}
static DEVICE_ATTR_RO(suspend_disk_state);

static ssize_t suspend_standby_state_show(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_state(buf,
			rdev->constraints->state_standby.enabled);
}
static DEVICE_ATTR_RO(suspend_standby_state);

static ssize_t bypass_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	const char *report;
	bool bypass;
	int ret;

	ret = rdev->desc->ops->get_bypass(rdev, &bypass);

	if (ret != 0)
		report = "unknown";
	else if (bypass)
		report = "enabled";
	else
		report = "disabled";

	return sprintf(buf, "%s\n", report);
}
static DEVICE_ATTR_RO(bypass);

#define REGULATOR_ERROR_ATTR(name, bit)							\
	static ssize_t name##_show(struct device *dev, struct device_attribute *attr,	\
				   char *buf)						\
	{										\
		int ret;								\
		unsigned int flags;							\
		struct regulator_dev *rdev = dev_get_drvdata(dev);			\
		ret = _regulator_get_error_flags(rdev, &flags);				\
		if (ret)								\
			return ret;							\
		return sysfs_emit(buf, "%d\n", !!(flags & (bit)));			\
	}										\
	static DEVICE_ATTR_RO(name)

REGULATOR_ERROR_ATTR(under_voltage, REGULATOR_ERROR_UNDER_VOLTAGE);
REGULATOR_ERROR_ATTR(over_current, REGULATOR_ERROR_OVER_CURRENT);
REGULATOR_ERROR_ATTR(regulation_out, REGULATOR_ERROR_REGULATION_OUT);
REGULATOR_ERROR_ATTR(fail, REGULATOR_ERROR_FAIL);
REGULATOR_ERROR_ATTR(over_temp, REGULATOR_ERROR_OVER_TEMP);
REGULATOR_ERROR_ATTR(under_voltage_warn, REGULATOR_ERROR_UNDER_VOLTAGE_WARN);
REGULATOR_ERROR_ATTR(over_current_warn, REGULATOR_ERROR_OVER_CURRENT_WARN);
REGULATOR_ERROR_ATTR(over_voltage_warn, REGULATOR_ERROR_OVER_VOLTAGE_WARN);
REGULATOR_ERROR_ATTR(over_temp_warn, REGULATOR_ERROR_OVER_TEMP_WARN);

/* Calculate the new optimum regulator operating mode based on the new total
 * consumer load. All locks held by caller
 */
static int drms_uA_update(struct regulator_dev *rdev)
{
	struct regulator *sibling;
	int current_uA = 0, output_uV, input_uV, err;
	unsigned int mode;

	/*
	 * first check to see if we can set modes at all, otherwise just
	 * tell the consumer everything is OK.
	 */
	if (!regulator_ops_is_valid(rdev, REGULATOR_CHANGE_DRMS)) {
		rdev_dbg(rdev, "DRMS operation not allowed\n");
		return 0;
	}

	if (!rdev->desc->ops->get_optimum_mode &&
	    !rdev->desc->ops->set_load)
		return 0;

	if (!rdev->desc->ops->set_mode &&
	    !rdev->desc->ops->set_load)
		return -EINVAL;

	/* calc total requested load */
	list_for_each_entry(sibling, &rdev->consumer_list, list) {
		if (sibling->enable_count)
			current_uA += sibling->uA_load;
	}

	current_uA += rdev->constraints->system_load;

	if (rdev->desc->ops->set_load) {
		/* set the optimum mode for our new total regulator load */
		err = rdev->desc->ops->set_load(rdev, current_uA);
		if (err < 0)
			rdev_err(rdev, "failed to set load %d: %pe\n",
				 current_uA, ERR_PTR(err));
	} else {
		/*
		 * Unfortunately in some cases the constraints->valid_ops has
		 * REGULATOR_CHANGE_DRMS but there are no valid modes listed.
		 * That's not really legit but we won't consider it a fatal
		 * error here. We'll treat it as if REGULATOR_CHANGE_DRMS
		 * wasn't set.
		 */
		if (!rdev->constraints->valid_modes_mask) {
			rdev_dbg(rdev, "Can change modes; but no valid mode\n");
			return 0;
		}

		/* get output voltage */
		output_uV = regulator_get_voltage_rdev(rdev);

		/*
		 * Don't return an error; if regulator driver cares about
		 * output_uV then it's up to the driver to validate.
		 */
		if (output_uV <= 0)
			rdev_dbg(rdev, "invalid output voltage found\n");

		/* get input voltage */
		input_uV = 0;
		if (rdev->supply)
			input_uV = regulator_get_voltage(rdev->supply);
		if (input_uV <= 0)
			input_uV = rdev->constraints->input_uV;

		/*
		 * Don't return an error; if regulator driver cares about
		 * input_uV then it's up to the driver to validate.
		 */
		if (input_uV <= 0)
			rdev_dbg(rdev, "invalid input voltage found\n");

		/* now get the optimum mode for our new total regulator load */
		mode = rdev->desc->ops->get_optimum_mode(rdev, input_uV,
							 output_uV, current_uA);

		/* check the new mode is allowed */
		err = regulator_mode_constrain(rdev, &mode);
		if (err < 0) {
			rdev_err(rdev, "failed to get optimum mode @ %d uA %d -> %d uV: %pe\n",
				 current_uA, input_uV, output_uV, ERR_PTR(err));
			return err;
		}

		err = rdev->desc->ops->set_mode(rdev, mode);
		if (err < 0)
			rdev_err(rdev, "failed to set optimum mode %x: %pe\n",
				 mode, ERR_PTR(err));
	}

	return err;
}

static int __suspend_set_state(struct regulator_dev *rdev,
			       const struct regulator_state *rstate)
{
	int ret = 0;

	if (rstate->enabled == ENABLE_IN_SUSPEND &&
		rdev->desc->ops->set_suspend_enable)
		ret = rdev->desc->ops->set_suspend_enable(rdev);
	else if (rstate->enabled == DISABLE_IN_SUSPEND &&
		rdev->desc->ops->set_suspend_disable)
		ret = rdev->desc->ops->set_suspend_disable(rdev);
	else /* OK if set_suspend_enable or set_suspend_disable is NULL */
		ret = 0;

	if (ret < 0) {
		rdev_err(rdev, "failed to enabled/disable: %pe\n", ERR_PTR(ret));
		return ret;
	}

	if (rdev->desc->ops->set_suspend_voltage && rstate->uV > 0) {
		ret = rdev->desc->ops->set_suspend_voltage(rdev, rstate->uV);
		if (ret < 0) {
			rdev_err(rdev, "failed to set voltage: %pe\n", ERR_PTR(ret));
			return ret;
		}
	}

	if (rdev->desc->ops->set_suspend_mode && rstate->mode > 0) {
		ret = rdev->desc->ops->set_suspend_mode(rdev, rstate->mode);
		if (ret < 0) {
			rdev_err(rdev, "failed to set mode: %pe\n", ERR_PTR(ret));
			return ret;
		}
	}

	return ret;
}

static int suspend_set_initial_state(struct regulator_dev *rdev)
{
	const struct regulator_state *rstate;

	rstate = regulator_get_suspend_state_check(rdev,
			rdev->constraints->initial_state);
	if (!rstate)
		return 0;

	return __suspend_set_state(rdev, rstate);
}

#if defined(DEBUG) || defined(CONFIG_DYNAMIC_DEBUG)
static void print_constraints_debug(struct regulator_dev *rdev)
{
	struct regulation_constraints *constraints = rdev->constraints;
	char buf[160] = "";
	size_t len = sizeof(buf) - 1;
	int count = 0;
	int ret;

	if (constraints->min_uV && constraints->max_uV) {
		if (constraints->min_uV == constraints->max_uV)
			count += scnprintf(buf + count, len - count, "%d mV ",
					   constraints->min_uV / 1000);
		else
			count += scnprintf(buf + count, len - count,
					   "%d <--> %d mV ",
					   constraints->min_uV / 1000,
					   constraints->max_uV / 1000);
	}

	if (!constraints->min_uV ||
	    constraints->min_uV != constraints->max_uV) {
		ret = regulator_get_voltage_rdev(rdev);
		if (ret > 0)
			count += scnprintf(buf + count, len - count,
					   "at %d mV ", ret / 1000);
	}

	if (constraints->uV_offset)
		count += scnprintf(buf + count, len - count, "%dmV offset ",
				   constraints->uV_offset / 1000);

	if (constraints->min_uA && constraints->max_uA) {
		if (constraints->min_uA == constraints->max_uA)
			count += scnprintf(buf + count, len - count, "%d mA ",
					   constraints->min_uA / 1000);
		else
			count += scnprintf(buf + count, len - count,
					   "%d <--> %d mA ",
					   constraints->min_uA / 1000,
					   constraints->max_uA / 1000);
	}

	if (!constraints->min_uA ||
	    constraints->min_uA != constraints->max_uA) {
		ret = _regulator_get_current_limit(rdev);
		if (ret > 0)
			count += scnprintf(buf + count, len - count,
					   "at %d mA ", ret / 1000);
	}

	if (constraints->valid_modes_mask & REGULATOR_MODE_FAST)
		count += scnprintf(buf + count, len - count, "fast ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_NORMAL)
		count += scnprintf(buf + count, len - count, "normal ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_IDLE)
		count += scnprintf(buf + count, len - count, "idle ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_STANDBY)
		count += scnprintf(buf + count, len - count, "standby ");

	if (!count)
		count = scnprintf(buf, len, "no parameters");
	else
		--count;

	count += scnprintf(buf + count, len - count, ", %s",
		_regulator_is_enabled(rdev) ? "enabled" : "disabled");

	rdev_dbg(rdev, "%s\n", buf);
}
#else /* !DEBUG && !CONFIG_DYNAMIC_DEBUG */
static inline void print_constraints_debug(struct regulator_dev *rdev) {}
#endif /* !DEBUG && !CONFIG_DYNAMIC_DEBUG */

static void print_constraints(struct regulator_dev *rdev)
{
	struct regulation_constraints *constraints = rdev->constraints;

	print_constraints_debug(rdev);

	if ((constraints->min_uV != constraints->max_uV) &&
	    !regulator_ops_is_valid(rdev, REGULATOR_CHANGE_VOLTAGE))
		rdev_warn(rdev,
			  "Voltage range but no REGULATOR_CHANGE_VOLTAGE\n");
}

static int machine_constraints_voltage(struct regulator_dev *rdev,
	struct regulation_constraints *constraints)
{
	const struct regulator_ops *ops = rdev->desc->ops;
	int ret;

	/* do we need to apply the constraint voltage */
	if (rdev->constraints->apply_uV &&
	    rdev->constraints->min_uV && rdev->constraints->max_uV) {
		int target_min, target_max;
		int current_uV = regulator_get_voltage_rdev(rdev);

		if (current_uV == -ENOTRECOVERABLE) {
			/* This regulator can't be read and must be initialized */
			rdev_info(rdev, "Setting %d-%duV\n",
				  rdev->constraints->min_uV,
				  rdev->constraints->max_uV);
			_regulator_do_set_voltage(rdev,
						  rdev->constraints->min_uV,
						  rdev->constraints->max_uV);
			current_uV = regulator_get_voltage_rdev(rdev);
		}

		if (current_uV < 0) {
			if (current_uV != -EPROBE_DEFER)
				rdev_err(rdev,
					 "failed to get the current voltage: %pe\n",
					 ERR_PTR(current_uV));
			return current_uV;
		}

		/*
		 * If we're below the minimum voltage move up to the
		 * minimum voltage, if we're above the maximum voltage
		 * then move down to the maximum.
		 */
		target_min = current_uV;
		target_max = current_uV;

		if (current_uV < rdev->constraints->min_uV) {
			target_min = rdev->constraints->min_uV;
			target_max = rdev->constraints->min_uV;
		}

		if (current_uV > rdev->constraints->max_uV) {
			target_min = rdev->constraints->max_uV;
			target_max = rdev->constraints->max_uV;
		}

		if (target_min != current_uV || target_max != current_uV) {
			rdev_info(rdev, "Bringing %duV into %d-%duV\n",
				  current_uV, target_min, target_max);
			ret = _regulator_do_set_voltage(
				rdev, target_min, target_max);
			if (ret < 0) {
				rdev_err(rdev,
					"failed to apply %d-%duV constraint: %pe\n",
					target_min, target_max, ERR_PTR(ret));
				return ret;
			}
		}
	}

	/* constrain machine-level voltage specs to fit
	 * the actual range supported by this regulator.
	 */
	if (ops->list_voltage && rdev->desc->n_voltages) {
		int	count = rdev->desc->n_voltages;
		int	i;
		int	min_uV = INT_MAX;
		int	max_uV = INT_MIN;
		int	cmin = constraints->min_uV;
		int	cmax = constraints->max_uV;

		/* it's safe to autoconfigure fixed-voltage supplies
		 * and the constraints are used by list_voltage.
		 */
		if (count == 1 && !cmin) {
			cmin = 1;
			cmax = INT_MAX;
			constraints->min_uV = cmin;
			constraints->max_uV = cmax;
		}

		/* voltage constraints are optional */
		if ((cmin == 0) && (cmax == 0))
			return 0;

		/* else require explicit machine-level constraints */
		if (cmin <= 0 || cmax <= 0 || cmax < cmin) {
			rdev_err(rdev, "invalid voltage constraints\n");
			return -EINVAL;
		}

		/* no need to loop voltages if range is continuous */
		if (rdev->desc->continuous_voltage_range)
			return 0;

		/* initial: [cmin..cmax] valid, [min_uV..max_uV] not */
		for (i = 0; i < count; i++) {
			int	value;

			value = ops->list_voltage(rdev, i);
			if (value <= 0)
				continue;

			/* maybe adjust [min_uV..max_uV] */
			if (value >= cmin && value < min_uV)
				min_uV = value;
			if (value <= cmax && value > max_uV)
				max_uV = value;
		}

		/* final: [min_uV..max_uV] valid iff constraints valid */
		if (max_uV < min_uV) {
			rdev_err(rdev,
				 "unsupportable voltage constraints %u-%uuV\n",
				 min_uV, max_uV);
			return -EINVAL;
		}

		/* use regulator's subset of machine constraints */
		if (constraints->min_uV < min_uV) {
			rdev_dbg(rdev, "override min_uV, %d -> %d\n",
				 constraints->min_uV, min_uV);
			constraints->min_uV = min_uV;
		}
		if (constraints->max_uV > max_uV) {
			rdev_dbg(rdev, "override max_uV, %d -> %d\n",
				 constraints->max_uV, max_uV);
			constraints->max_uV = max_uV;
		}
	}

	return 0;
}

static int machine_constraints_current(struct regulator_dev *rdev,
	struct regulation_constraints *constraints)
{
	const struct regulator_ops *ops = rdev->desc->ops;
	int ret;

	if (!constraints->min_uA && !constraints->max_uA)
		return 0;

	if (constraints->min_uA > constraints->max_uA) {
		rdev_err(rdev, "Invalid current constraints\n");
		return -EINVAL;
	}

	if (!ops->set_current_limit || !ops->get_current_limit) {
		rdev_warn(rdev, "Operation of current configuration missing\n");
		return 0;
	}

	/* Set regulator current in constraints range */
	ret = ops->set_current_limit(rdev, constraints->min_uA,
			constraints->max_uA);
	if (ret < 0) {
		rdev_err(rdev, "Failed to set current constraint, %d\n", ret);
		return ret;
	}

	return 0;
}

static int _regulator_do_enable(struct regulator_dev *rdev);

static int notif_set_limit(struct regulator_dev *rdev,
			   int (*set)(struct regulator_dev *, int, int, bool),
			   int limit, int severity)
{
	bool enable;

	if (limit == REGULATOR_NOTIF_LIMIT_DISABLE) {
		enable = false;
		limit = 0;
	} else {
		enable = true;
	}

	if (limit == REGULATOR_NOTIF_LIMIT_ENABLE)
		limit = 0;

	return set(rdev, limit, severity, enable);
}

static int handle_notify_limits(struct regulator_dev *rdev,
			int (*set)(struct regulator_dev *, int, int, bool),
			struct notification_limit *limits)
{
	int ret = 0;

	if (!set)
		return -EOPNOTSUPP;

	if (limits->prot)
		ret = notif_set_limit(rdev, set, limits->prot,
				      REGULATOR_SEVERITY_PROT);
	if (ret)
		return ret;

	if (limits->err)
		ret = notif_set_limit(rdev, set, limits->err,
				      REGULATOR_SEVERITY_ERR);
	if (ret)
		return ret;

	if (limits->warn)
		ret = notif_set_limit(rdev, set, limits->warn,
				      REGULATOR_SEVERITY_WARN);

	return ret;
}
/**
 * set_machine_constraints - sets regulator constraints
 * @rdev: regulator source
 *
 * Allows platform initialisation code to define and constrain
 * regulator circuits e.g. valid voltage/current ranges, etc.  NOTE:
 * Constraints *must* be set by platform code in order for some
 * regulator operations to proceed i.e. set_voltage, set_current_limit,
 * set_mode.
 */
static int set_machine_constraints(struct regulator_dev *rdev)
{
	int ret = 0;
	const struct regulator_ops *ops = rdev->desc->ops;

	ret = machine_constraints_voltage(rdev, rdev->constraints);
	if (ret != 0)
		return ret;

	ret = machine_constraints_current(rdev, rdev->constraints);
	if (ret != 0)
		return ret;

	if (rdev->constraints->ilim_uA && ops->set_input_current_limit) {
		ret = ops->set_input_current_limit(rdev,
						   rdev->constraints->ilim_uA);
		if (ret < 0) {
			rdev_err(rdev, "failed to set input limit: %pe\n", ERR_PTR(ret));
			return ret;
		}
	}

	/* do we need to setup our suspend state */
	if (rdev->constraints->initial_state) {
		ret = suspend_set_initial_state(rdev);
		if (ret < 0) {
			rdev_err(rdev, "failed to set suspend state: %pe\n", ERR_PTR(ret));
			return ret;
		}
	}

	if (rdev->constraints->initial_mode) {
		if (!ops->set_mode) {
			rdev_err(rdev, "no set_mode operation\n");
			return -EINVAL;
		}

		ret = ops->set_mode(rdev, rdev->constraints->initial_mode);
		if (ret < 0) {
			rdev_err(rdev, "failed to set initial mode: %pe\n", ERR_PTR(ret));
			return ret;
		}
	} else if (rdev->constraints->system_load) {
		/*
		 * We'll only apply the initial system load if an
		 * initial mode wasn't specified.
		 */
		drms_uA_update(rdev);
	}

	if ((rdev->constraints->ramp_delay || rdev->constraints->ramp_disable)
		&& ops->set_ramp_delay) {
		ret = ops->set_ramp_delay(rdev, rdev->constraints->ramp_delay);
		if (ret < 0) {
			rdev_err(rdev, "failed to set ramp_delay: %pe\n", ERR_PTR(ret));
			return ret;
		}
	}

	if (rdev->constraints->pull_down && ops->set_pull_down) {
		ret = ops->set_pull_down(rdev);
		if (ret < 0) {
			rdev_err(rdev, "failed to set pull down: %pe\n", ERR_PTR(ret));
			return ret;
		}
	}

	if (rdev->constraints->soft_start && ops->set_soft_start) {
		ret = ops->set_soft_start(rdev);
		if (ret < 0) {
			rdev_err(rdev, "failed to set soft start: %pe\n", ERR_PTR(ret));
			return ret;
		}
	}

	/*
	 * Existing logic does not warn if over_current_protection is given as
	 * a constraint but driver does not support that. I think we should
	 * warn about this type of issues as it is possible someone changes
	 * PMIC on board to another type - and the another PMIC's driver does
	 * not support setting protection. Board composer may happily believe
	 * the DT limits are respected - especially if the new PMIC HW also
	 * supports protection but the driver does not. I won't change the logic
	 * without hearing more experienced opinion on this though.
	 *
	 * If warning is seen as a good idea then we can merge handling the
	 * over-curret protection and detection and get rid of this special
	 * handling.
	 */
	if (rdev->constraints->over_current_protection
		&& ops->set_over_current_protection) {
		int lim = rdev->constraints->over_curr_limits.prot;

		ret = ops->set_over_current_protection(rdev, lim,
						       REGULATOR_SEVERITY_PROT,
						       true);
		if (ret < 0) {
			rdev_err(rdev, "failed to set over current protection: %pe\n",
				 ERR_PTR(ret));
			return ret;
		}
	}

	if (rdev->constraints->over_current_detection)
		ret = handle_notify_limits(rdev,
					   ops->set_over_current_protection,
					   &rdev->constraints->over_curr_limits);
	if (ret) {
		if (ret != -EOPNOTSUPP) {
			rdev_err(rdev, "failed to set over current limits: %pe\n",
				 ERR_PTR(ret));
			return ret;
		}
		rdev_warn(rdev,
			  "IC does not support requested over-current limits\n");
	}

	if (rdev->constraints->over_voltage_detection)
		ret = handle_notify_limits(rdev,
					   ops->set_over_voltage_protection,
					   &rdev->constraints->over_voltage_limits);
	if (ret) {
		if (ret != -EOPNOTSUPP) {
			rdev_err(rdev, "failed to set over voltage limits %pe\n",
				 ERR_PTR(ret));
			return ret;
		}
		rdev_warn(rdev,
			  "IC does not support requested over voltage limits\n");
	}

	if (rdev->constraints->under_voltage_detection)
		ret = handle_notify_limits(rdev,
					   ops->set_under_voltage_protection,
					   &rdev->constraints->under_voltage_limits);
	if (ret) {
		if (ret != -EOPNOTSUPP) {
			rdev_err(rdev, "failed to set under voltage limits %pe\n",
				 ERR_PTR(ret));
			return ret;
		}
		rdev_warn(rdev,
			  "IC does not support requested under voltage limits\n");
	}

	if (rdev->constraints->over_temp_detection)
		ret = handle_notify_limits(rdev,
					   ops->set_thermal_protection,
					   &rdev->constraints->temp_limits);
	if (ret) {
		if (ret != -EOPNOTSUPP) {
			rdev_err(rdev, "failed to set temperature limits %pe\n",
				 ERR_PTR(ret));
			return ret;
		}
		rdev_warn(rdev,
			  "IC does not support requested temperature limits\n");
	}

	if (rdev->constraints->active_discharge && ops->set_active_discharge) {
		bool ad_state = (rdev->constraints->active_discharge ==
			      REGULATOR_ACTIVE_DISCHARGE_ENABLE) ? true : false;

		ret = ops->set_active_discharge(rdev, ad_state);
		if (ret < 0) {
			rdev_err(rdev, "failed to set active discharge: %pe\n", ERR_PTR(ret));
			return ret;
		}
	}

	/*
	 * If there is no mechanism for controlling the regulator then
	 * flag it as always_on so we don't end up duplicating checks
	 * for this so much.  Note that we could control the state of
	 * a supply to control the output on a regulator that has no
	 * direct control.
	 */
	if (!rdev->ena_pin && !ops->enable) {
		if (rdev->supply_name && !rdev->supply)
			return -EPROBE_DEFER;

		if (rdev->supply)
			rdev->constraints->always_on =
				rdev->supply->rdev->constraints->always_on;
		else
			rdev->constraints->always_on = true;
	}

	if (rdev->desc->off_on_delay)
		rdev->last_off = ktime_get();

	/* If the constraints say the regulator should be on at this point
	 * and we have control then make sure it is enabled.
	 */
	if (rdev->constraints->always_on || rdev->constraints->boot_on) {
		/* If we want to enable this regulator, make sure that we know
		 * the supplying regulator.
		 */
		if (rdev->supply_name && !rdev->supply)
			return -EPROBE_DEFER;

		if (rdev->supply) {
			ret = regulator_enable(rdev->supply);
			if (ret < 0) {
				_regulator_put(rdev->supply);
				rdev->supply = NULL;
				return ret;
			}
		}

		ret = _regulator_do_enable(rdev);
		if (ret < 0 && ret != -EINVAL) {
			rdev_err(rdev, "failed to enable: %pe\n", ERR_PTR(ret));
			return ret;
		}

		if (rdev->constraints->always_on)
			rdev->use_count++;
	}

	print_constraints(rdev);
	return 0;
}

/**
 * set_supply - set regulator supply regulator
 * @rdev: regulator name
 * @supply_rdev: supply regulator name
 *
 * Called by platform initialisation code to set the supply regulator for this
 * regulator. This ensures that a regulators supply will also be enabled by the
 * core if it's child is enabled.
 */
static int set_supply(struct regulator_dev *rdev,
		      struct regulator_dev *supply_rdev)
{
	int err;

	rdev_dbg(rdev, "supplied by %s\n", rdev_get_name(supply_rdev));

	if (!try_module_get(supply_rdev->owner))
		return -ENODEV;

	rdev->supply = create_regulator(supply_rdev, &rdev->dev, "SUPPLY");
	if (rdev->supply == NULL) {
		err = -ENOMEM;
		return err;
	}
	supply_rdev->open_count++;

	return 0;
}

/**
 * set_consumer_device_supply - Bind a regulator to a symbolic supply
 * @rdev:         regulator source
 * @consumer_dev_name: dev_name() string for device supply applies to
 * @supply:       symbolic name for supply
 *
 * Allows platform initialisation code to map physical regulator
 * sources to symbolic names for supplies for use by devices.  Devices
 * should use these symbolic names to request regulators, avoiding the
 * need to provide board-specific regulator names as platform data.
 */
static int set_consumer_device_supply(struct regulator_dev *rdev,
				      const char *consumer_dev_name,
				      const char *supply)
{
	struct regulator_map *node, *new_node;
	int has_dev;

	if (supply == NULL)
		return -EINVAL;

	if (consumer_dev_name != NULL)
		has_dev = 1;
	else
		has_dev = 0;

	new_node = kzalloc(sizeof(struct regulator_map), GFP_KERNEL);
	if (new_node == NULL)
		return -ENOMEM;

	new_node->regulator = rdev;
	new_node->supply = supply;

	if (has_dev) {
		new_node->dev_name = kstrdup(consumer_dev_name, GFP_KERNEL);
		if (new_node->dev_name == NULL) {
			kfree(new_node);
			return -ENOMEM;
		}
	}

	mutex_lock(&regulator_list_mutex);
	list_for_each_entry(node, &regulator_map_list, list) {
		if (node->dev_name && consumer_dev_name) {
			if (strcmp(node->dev_name, consumer_dev_name) != 0)
				continue;
		} else if (node->dev_name || consumer_dev_name) {
			continue;
		}

		if (strcmp(node->supply, supply) != 0)
			continue;

		pr_debug("%s: %s/%s is '%s' supply; fail %s/%s\n",
			 consumer_dev_name,
			 dev_name(&node->regulator->dev),
			 node->regulator->desc->name,
			 supply,
			 dev_name(&rdev->dev), rdev_get_name(rdev));
		goto fail;
	}

	list_add(&new_node->list, &regulator_map_list);
	mutex_unlock(&regulator_list_mutex);

	return 0;

fail:
	mutex_unlock(&regulator_list_mutex);
	kfree(new_node->dev_name);
	kfree(new_node);
	return -EBUSY;
}

static void unset_regulator_supplies(struct regulator_dev *rdev)
{
	struct regulator_map *node, *n;

	list_for_each_entry_safe(node, n, &regulator_map_list, list) {
		if (rdev == node->regulator) {
			list_del(&node->list);
			kfree(node->dev_name);
			kfree(node);
		}
	}
}

#ifdef CONFIG_DEBUG_FS
static ssize_t constraint_flags_read_file(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	const struct regulator *regulator = file->private_data;
	const struct regulation_constraints *c = regulator->rdev->constraints;
	char *buf;
	ssize_t ret;

	if (!c)
		return 0;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = snprintf(buf, PAGE_SIZE,
			"always_on: %u\n"
			"boot_on: %u\n"
			"apply_uV: %u\n"
			"ramp_disable: %u\n"
			"soft_start: %u\n"
			"pull_down: %u\n"
			"over_current_protection: %u\n",
			c->always_on,
			c->boot_on,
			c->apply_uV,
			c->ramp_disable,
			c->soft_start,
			c->pull_down,
			c->over_current_protection);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);
	kfree(buf);

	return ret;
}

#endif

static const struct file_operations constraint_flags_fops = {
#ifdef CONFIG_DEBUG_FS
	.open = simple_open,
	.read = constraint_flags_read_file,
	.llseek = default_llseek,
#endif
};

#define REG_STR_SIZE	64

static struct regulator *create_regulator(struct regulator_dev *rdev,
					  struct device *dev,
					  const char *supply_name)
{
	struct regulator *regulator;
	int err = 0;

	if (dev) {
		char buf[REG_STR_SIZE];
		int size;

		size = snprintf(buf, REG_STR_SIZE, "%s-%s",
				dev->kobj.name, supply_name);
		if (size >= REG_STR_SIZE)
			return NULL;

		supply_name = kstrdup(buf, GFP_KERNEL);
		if (supply_name == NULL)
			return NULL;
	} else {
		supply_name = kstrdup_const(supply_name, GFP_KERNEL);
		if (supply_name == NULL)
			return NULL;
	}

	regulator = kzalloc(sizeof(*regulator), GFP_KERNEL);
	if (regulator == NULL) {
		kfree(supply_name);
		return NULL;
	}

	regulator->rdev = rdev;
	regulator->supply_name = supply_name;

	regulator_lock(rdev);
	list_add(&regulator->list, &rdev->consumer_list);
	regulator_unlock(rdev);

	if (dev) {
		regulator->dev = dev;

		/* Add a link to the device sysfs entry */
		err = sysfs_create_link_nowarn(&rdev->dev.kobj, &dev->kobj,
					       supply_name);
		if (err) {
			rdev_dbg(rdev, "could not add device link %s: %pe\n",
				  dev->kobj.name, ERR_PTR(err));
			/* non-fatal */
		}
	}

	if (err != -EEXIST)
		regulator->debugfs = debugfs_create_dir(supply_name, rdev->debugfs);
	if (!regulator->debugfs) {
		rdev_dbg(rdev, "Failed to create debugfs directory\n");
	} else {
		debugfs_create_u32("uA_load", 0444, regulator->debugfs,
				   &regulator->uA_load);
		debugfs_create_u32("min_uV", 0444, regulator->debugfs,
				   &regulator->voltage[PM_SUSPEND_ON].min_uV);
		debugfs_create_u32("max_uV", 0444, regulator->debugfs,
				   &regulator->voltage[PM_SUSPEND_ON].max_uV);
		debugfs_create_file("constraint_flags", 0444,
				    regulator->debugfs, regulator,
				    &constraint_flags_fops);
	}

	/*
	 * Check now if the regulator is an always on regulator - if
	 * it is then we don't need to do nearly so much work for
	 * enable/disable calls.
	 */
	if (!regulator_ops_is_valid(rdev, REGULATOR_CHANGE_STATUS) &&
	    _regulator_is_enabled(rdev))
		regulator->always_on = true;

	return regulator;
}

static int _regulator_get_enable_time(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->enable_time)
		return rdev->constraints->enable_time;
	if (rdev->desc->ops->enable_time)
		return rdev->desc->ops->enable_time(rdev);
	return rdev->desc->enable_time;
}

static struct regulator_supply_alias *regulator_find_supply_alias(
		struct device *dev, const char *supply)
{
	struct regulator_supply_alias *map;

	list_for_each_entry(map, &regulator_supply_alias_list, list)
		if (map->src_dev == dev && strcmp(map->src_supply, supply) == 0)
			return map;

	return NULL;
}

static void regulator_supply_alias(struct device **dev, const char **supply)
{
	struct regulator_supply_alias *map;

	map = regulator_find_supply_alias(*dev, *supply);
	if (map) {
		dev_dbg(*dev, "Mapping supply %s to %s,%s\n",
				*supply, map->alias_supply,
				dev_name(map->alias_dev));
		*dev = map->alias_dev;
		*supply = map->alias_supply;
	}
}

static int regulator_match(struct device *dev, const void *data)
{
	struct regulator_dev *r = dev_to_rdev(dev);

	return strcmp(rdev_get_name(r), data) == 0;
}

static struct regulator_dev *regulator_lookup_by_name(const char *name)
{
	struct device *dev;

	dev = class_find_device(&regulator_class, NULL, name, regulator_match);

	return dev ? dev_to_rdev(dev) : NULL;
}

/**
 * regulator_dev_lookup - lookup a regulator device.
 * @dev: device for regulator "consumer".
 * @supply: Supply name or regulator ID.
 *
 * If successful, returns a struct regulator_dev that corresponds to the name
 * @supply and with the embedded struct device refcount incremented by one.
 * The refcount must be dropped by calling put_device().
 * On failure one of the following ERR-PTR-encoded values is returned:
 * -ENODEV if lookup fails permanently, -EPROBE_DEFER if lookup could succeed
 * in the future.
 */
static struct regulator_dev *regulator_dev_lookup(struct device *dev,
						  const char *supply)
{
	struct regulator_dev *r = NULL;
	struct device_node *node;
	struct regulator_map *map;
	const char *devname = NULL;

	regulator_supply_alias(&dev, &supply);

	/* first do a dt based lookup */
	if (dev && dev->of_node) {
		node = of_get_regulator(dev, supply);
		if (node) {
			r = of_find_regulator_by_node(node);
			if (r)
				return r;

			/*
			 * We have a node, but there is no device.
			 * assume it has not registered yet.
			 */
			return ERR_PTR(-EPROBE_DEFER);
		}
	}

	/* if not found, try doing it non-dt way */
	if (dev)
		devname = dev_name(dev);

	mutex_lock(&regulator_list_mutex);
	list_for_each_entry(map, &regulator_map_list, list) {
		/* If the mapping has a device set up it must match */
		if (map->dev_name &&
		    (!devname || strcmp(map->dev_name, devname)))
			continue;

		if (strcmp(map->supply, supply) == 0 &&
		    get_device(&map->regulator->dev)) {
			r = map->regulator;
			break;
		}
	}
	mutex_unlock(&regulator_list_mutex);

	if (r)
		return r;

	r = regulator_lookup_by_name(supply);
	if (r)
		return r;

	return ERR_PTR(-ENODEV);
}

static int regulator_resolve_supply(struct regulator_dev *rdev)
{
	struct regulator_dev *r;
	struct device *dev = rdev->dev.parent;
	int ret = 0;

	/* No supply to resolve? */
	if (!rdev->supply_name)
		return 0;

	/* Supply already resolved? (fast-path without locking contention) */
	if (rdev->supply)
		return 0;

	r = regulator_dev_lookup(dev, rdev->supply_name);
	if (IS_ERR(r)) {
		ret = PTR_ERR(r);

		/* Did the lookup explicitly defer for us? */
		if (ret == -EPROBE_DEFER)
			goto out;

		if (have_full_constraints()) {
			r = dummy_regulator_rdev;
			get_device(&r->dev);
		} else {
			dev_err(dev, "Failed to resolve %s-supply for %s\n",
				rdev->supply_name, rdev->desc->name);
			ret = -EPROBE_DEFER;
			goto out;
		}
	}

	if (r == rdev) {
		dev_err(dev, "Supply for %s (%s) resolved to itself\n",
			rdev->desc->name, rdev->supply_name);
		if (!have_full_constraints()) {
			ret = -EINVAL;
			goto out;
		}
		r = dummy_regulator_rdev;
		get_device(&r->dev);
	}

	/*
	 * If the supply's parent device is not the same as the
	 * regulator's parent device, then ensure the parent device
	 * is bound before we resolve the supply, in case the parent
	 * device get probe deferred and unregisters the supply.
	 */
	if (r->dev.parent && r->dev.parent != rdev->dev.parent) {
		if (!device_is_bound(r->dev.parent)) {
			put_device(&r->dev);
			ret = -EPROBE_DEFER;
			goto out;
		}
	}

	/* Recursively resolve the supply of the supply */
	ret = regulator_resolve_supply(r);
	if (ret < 0) {
		put_device(&r->dev);
		goto out;
	}

	/*
	 * Recheck rdev->supply with rdev->mutex lock held to avoid a race
	 * between rdev->supply null check and setting rdev->supply in
	 * set_supply() from concurrent tasks.
	 */
	regulator_lock(rdev);

	/* Supply just resolved by a concurrent task? */
	if (rdev->supply) {
		regulator_unlock(rdev);
		put_device(&r->dev);
		goto out;
	}

	ret = set_supply(rdev, r);
	if (ret < 0) {
		regulator_unlock(rdev);
		put_device(&r->dev);
		goto out;
	}

	regulator_unlock(rdev);

	/*
	 * In set_machine_constraints() we may have turned this regulator on
	 * but we couldn't propagate to the supply if it hadn't been resolved
	 * yet.  Do it now.
	 */
	if (rdev->use_count) {
		ret = regulator_enable(rdev->supply);
		if (ret < 0) {
			_regulator_put(rdev->supply);
			rdev->supply = NULL;
			goto out;
		}
	}

out:
	return ret;
}

/* Internal regulator request function */
struct regulator *_regulator_get(struct device *dev, const char *id,
				 enum regulator_get_type get_type)
{
	struct regulator_dev *rdev;
	struct regulator *regulator;
	struct device_link *link;
	int ret;

	if (get_type >= MAX_GET_TYPE) {
		dev_err(dev, "invalid type %d in %s\n", get_type, __func__);
		return ERR_PTR(-EINVAL);
	}

	if (id == NULL) {
		pr_err("get() with no identifier\n");
		return ERR_PTR(-EINVAL);
	}

	rdev = regulator_dev_lookup(dev, id);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);

		/*
		 * If regulator_dev_lookup() fails with error other
		 * than -ENODEV our job here is done, we simply return it.
		 */
		if (ret != -ENODEV)
			return ERR_PTR(ret);

		if (!have_full_constraints()) {
			dev_warn(dev,
				 "incomplete constraints, dummy supplies not allowed\n");
			return ERR_PTR(-ENODEV);
		}

		switch (get_type) {
		case NORMAL_GET:
			/*
			 * Assume that a regulator is physically present and
			 * enabled, even if it isn't hooked up, and just
			 * provide a dummy.
			 */
			dev_warn(dev, "supply %s not found, using dummy regulator\n", id);
			rdev = dummy_regulator_rdev;
			get_device(&rdev->dev);
			break;

		case EXCLUSIVE_GET:
			dev_warn(dev,
				 "dummy supplies not allowed for exclusive requests\n");
			fallthrough;

		default:
			return ERR_PTR(-ENODEV);
		}
	}

	if (rdev->exclusive) {
		regulator = ERR_PTR(-EPERM);
		put_device(&rdev->dev);
		return regulator;
	}

	if (get_type == EXCLUSIVE_GET && rdev->open_count) {
		regulator = ERR_PTR(-EBUSY);
		put_device(&rdev->dev);
		return regulator;
	}

	mutex_lock(&regulator_list_mutex);
	ret = (rdev->coupling_desc.n_resolved != rdev->coupling_desc.n_coupled);
	mutex_unlock(&regulator_list_mutex);

	if (ret != 0) {
		regulator = ERR_PTR(-EPROBE_DEFER);
		put_device(&rdev->dev);
		return regulator;
	}

	ret = regulator_resolve_supply(rdev);
	if (ret < 0) {
		regulator = ERR_PTR(ret);
		put_device(&rdev->dev);
		return regulator;
	}

	if (!try_module_get(rdev->owner)) {
		regulator = ERR_PTR(-EPROBE_DEFER);
		put_device(&rdev->dev);
		return regulator;
	}

	regulator = create_regulator(rdev, dev, id);
	if (regulator == NULL) {
		regulator = ERR_PTR(-ENOMEM);
		module_put(rdev->owner);
		put_device(&rdev->dev);
		return regulator;
	}

	rdev->open_count++;
	if (get_type == EXCLUSIVE_GET) {
		rdev->exclusive = 1;

		ret = _regulator_is_enabled(rdev);
		if (ret > 0) {
			rdev->use_count = 1;
			regulator->enable_count = 1;
		} else {
			rdev->use_count = 0;
			regulator->enable_count = 0;
		}
	}

	link = device_link_add(dev, &rdev->dev, DL_FLAG_STATELESS);
	if (!IS_ERR_OR_NULL(link))
		regulator->device_link = true;

	return regulator;
}

/**
 * regulator_get - lookup and obtain a reference to a regulator.
 * @dev: device for regulator "consumer"
 * @id: Supply name or regulator ID.
 *
 * Returns a struct regulator corresponding to the regulator producer,
 * or IS_ERR() condition containing errno.
 *
 * Use of supply names configured via set_consumer_device_supply() is
 * strongly encouraged.  It is recommended that the supply name used
 * should match the name used for the supply and/or the relevant
 * device pins in the datasheet.
 */
struct regulator *regulator_get(struct device *dev, const char *id)
{
	return _regulator_get(dev, id, NORMAL_GET);
}
EXPORT_SYMBOL_GPL(regulator_get);

/**
 * regulator_get_exclusive - obtain exclusive access to a regulator.
 * @dev: device for regulator "consumer"
 * @id: Supply name or regulator ID.
 *
 * Returns a struct regulator corresponding to the regulator producer,
 * or IS_ERR() condition containing errno.  Other consumers will be
 * unable to obtain this regulator while this reference is held and the
 * use count for the regulator will be initialised to reflect the current
 * state of the regulator.
 *
 * This is intended for use by consumers which cannot tolerate shared
 * use of the regulator such as those which need to force the
 * regulator off for correct operation of the hardware they are
 * controlling.
 *
 * Use of supply names configured via set_consumer_device_supply() is
 * strongly encouraged.  It is recommended that the supply name used
 * should match the name used for the supply and/or the relevant
 * device pins in the datasheet.
 */
struct regulator *regulator_get_exclusive(struct device *dev, const char *id)
{
	return _regulator_get(dev, id, EXCLUSIVE_GET);
}
EXPORT_SYMBOL_GPL(regulator_get_exclusive);

/**
 * regulator_get_optional - obtain optional access to a regulator.
 * @dev: device for regulator "consumer"
 * @id: Supply name or regulator ID.
 *
 * Returns a struct regulator corresponding to the regulator producer,
 * or IS_ERR() condition containing errno.
 *
 * This is intended for use by consumers for devices which can have
 * some supplies unconnected in normal use, such as some MMC devices.
 * It can allow the regulator core to provide stub supplies for other
 * supplies requested using normal regulator_get() calls without
 * disrupting the operation of drivers that can handle absent
 * supplies.
 *
 * Use of supply names configured via set_consumer_device_supply() is
 * strongly encouraged.  It is recommended that the supply name used
 * should match the name used for the supply and/or the relevant
 * device pins in the datasheet.
 */
struct regulator *regulator_get_optional(struct device *dev, const char *id)
{
	return _regulator_get(dev, id, OPTIONAL_GET);
}
EXPORT_SYMBOL_GPL(regulator_get_optional);

static void destroy_regulator(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;

	debugfs_remove_recursive(regulator->debugfs);

	if (regulator->dev) {
		if (regulator->device_link)
			device_link_remove(regulator->dev, &rdev->dev);

		/* remove any sysfs entries */
		sysfs_remove_link(&rdev->dev.kobj, regulator->supply_name);
	}

	regulator_lock(rdev);
	list_del(&regulator->list);

	rdev->open_count--;
	rdev->exclusive = 0;
	regulator_unlock(rdev);

	kfree_const(regulator->supply_name);
	kfree(regulator);
}

/* regulator_list_mutex lock held by regulator_put() */
static void _regulator_put(struct regulator *regulator)
{
	struct regulator_dev *rdev;

	if (IS_ERR_OR_NULL(regulator))
		return;

	lockdep_assert_held_once(&regulator_list_mutex);

	/* Docs say you must disable before calling regulator_put() */
	WARN_ON(regulator->enable_count);

	rdev = regulator->rdev;

	destroy_regulator(regulator);

	module_put(rdev->owner);
	put_device(&rdev->dev);
}

/**
 * regulator_put - "free" the regulator source
 * @regulator: regulator source
 *
 * Note: drivers must ensure that all regulator_enable calls made on this
 * regulator source are balanced by regulator_disable calls prior to calling
 * this function.
 */
void regulator_put(struct regulator *regulator)
{
	mutex_lock(&regulator_list_mutex);
	_regulator_put(regulator);
	mutex_unlock(&regulator_list_mutex);
}
EXPORT_SYMBOL_GPL(regulator_put);

/**
 * regulator_register_supply_alias - Provide device alias for supply lookup
 *
 * @dev: device that will be given as the regulator "consumer"
 * @id: Supply name or regulator ID
 * @alias_dev: device that should be used to lookup the supply
 * @alias_id: Supply name or regulator ID that should be used to lookup the
 * supply
 *
 * All lookups for id on dev will instead be conducted for alias_id on
 * alias_dev.
 */
int regulator_register_supply_alias(struct device *dev, const char *id,
				    struct device *alias_dev,
				    const char *alias_id)
{
	struct regulator_supply_alias *map;

	map = regulator_find_supply_alias(dev, id);
	if (map)
		return -EEXIST;

	map = kzalloc(sizeof(struct regulator_supply_alias), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	map->src_dev = dev;
	map->src_supply = id;
	map->alias_dev = alias_dev;
	map->alias_supply = alias_id;

	list_add(&map->list, &regulator_supply_alias_list);

	pr_info("Adding alias for supply %s,%s -> %s,%s\n",
		id, dev_name(dev), alias_id, dev_name(alias_dev));

	return 0;
}
EXPORT_SYMBOL_GPL(regulator_register_supply_alias);

/**
 * regulator_unregister_supply_alias - Remove device alias
 *
 * @dev: device that will be given as the regulator "consumer"
 * @id: Supply name or regulator ID
 *
 * Remove a lookup alias if one exists for id on dev.
 */
void regulator_unregister_supply_alias(struct device *dev, const char *id)
{
	struct regulator_supply_alias *map;

	map = regulator_find_supply_alias(dev, id);
	if (map) {
		list_del(&map->list);
		kfree(map);
	}
}
EXPORT_SYMBOL_GPL(regulator_unregister_supply_alias);

/**
 * regulator_bulk_register_supply_alias - register multiple aliases
 *
 * @dev: device that will be given as the regulator "consumer"
 * @id: List of supply names or regulator IDs
 * @alias_dev: device that should be used to lookup the supply
 * @alias_id: List of supply names or regulator IDs that should be used to
 * lookup the supply
 * @num_id: Number of aliases to register
 *
 * @return 0 on success, an errno on failure.
 *
 * This helper function allows drivers to register several supply
 * aliases in one operation.  If any of the aliases cannot be
 * registered any aliases that were registered will be removed
 * before returning to the caller.
 */
int regulator_bulk_register_supply_alias(struct device *dev,
					 const char *const *id,
					 struct device *alias_dev,
					 const char *const *alias_id,
					 int num_id)
{
	int i;
	int ret;

	for (i = 0; i < num_id; ++i) {
		ret = regulator_register_supply_alias(dev, id[i], alias_dev,
						      alias_id[i]);
		if (ret < 0)
			goto err;
	}

	return 0;

err:
	dev_err(dev,
		"Failed to create supply alias %s,%s -> %s,%s\n",
		id[i], dev_name(dev), alias_id[i], dev_name(alias_dev));

	while (--i >= 0)
		regulator_unregister_supply_alias(dev, id[i]);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_register_supply_alias);

/**
 * regulator_bulk_unregister_supply_alias - unregister multiple aliases
 *
 * @dev: device that will be given as the regulator "consumer"
 * @id: List of supply names or regulator IDs
 * @num_id: Number of aliases to unregister
 *
 * This helper function allows drivers to unregister several supply
 * aliases in one operation.
 */
void regulator_bulk_unregister_supply_alias(struct device *dev,
					    const char *const *id,
					    int num_id)
{
	int i;

	for (i = 0; i < num_id; ++i)
		regulator_unregister_supply_alias(dev, id[i]);
}
EXPORT_SYMBOL_GPL(regulator_bulk_unregister_supply_alias);


/* Manage enable GPIO list. Same GPIO pin can be shared among regulators */
static int regulator_ena_gpio_request(struct regulator_dev *rdev,
				const struct regulator_config *config)
{
	struct regulator_enable_gpio *pin, *new_pin;
	struct gpio_desc *gpiod;

	gpiod = config->ena_gpiod;
	new_pin = kzalloc(sizeof(*new_pin), GFP_KERNEL);

	mutex_lock(&regulator_list_mutex);

	list_for_each_entry(pin, &regulator_ena_gpio_list, list) {
		if (pin->gpiod == gpiod) {
			rdev_dbg(rdev, "GPIO is already used\n");
			goto update_ena_gpio_to_rdev;
		}
	}

	if (new_pin == NULL) {
		mutex_unlock(&regulator_list_mutex);
		return -ENOMEM;
	}

	pin = new_pin;
	new_pin = NULL;

	pin->gpiod = gpiod;
	list_add(&pin->list, &regulator_ena_gpio_list);

update_ena_gpio_to_rdev:
	pin->request_count++;
	rdev->ena_pin = pin;

	mutex_unlock(&regulator_list_mutex);
	kfree(new_pin);

	return 0;
}

static void regulator_ena_gpio_free(struct regulator_dev *rdev)
{
	struct regulator_enable_gpio *pin, *n;

	if (!rdev->ena_pin)
		return;

	/* Free the GPIO only in case of no use */
	list_for_each_entry_safe(pin, n, &regulator_ena_gpio_list, list) {
		if (pin != rdev->ena_pin)
			continue;

		if (--pin->request_count)
			break;

		gpiod_put(pin->gpiod);
		list_del(&pin->list);
		kfree(pin);
		break;
	}

	rdev->ena_pin = NULL;
}

/**
 * regulator_ena_gpio_ctrl - balance enable_count of each GPIO and actual GPIO pin control
 * @rdev: regulator_dev structure
 * @enable: enable GPIO at initial use?
 *
 * GPIO is enabled in case of initial use. (enable_count is 0)
 * GPIO is disabled when it is not shared any more. (enable_count <= 1)
 */
static int regulator_ena_gpio_ctrl(struct regulator_dev *rdev, bool enable)
{
	struct regulator_enable_gpio *pin = rdev->ena_pin;

	if (!pin)
		return -EINVAL;

	if (enable) {
		/* Enable GPIO at initial use */
		if (pin->enable_count == 0)
			gpiod_set_value_cansleep(pin->gpiod, 1);

		pin->enable_count++;
	} else {
		if (pin->enable_count > 1) {
			pin->enable_count--;
			return 0;
		}

		/* Disable GPIO if not used */
		if (pin->enable_count <= 1) {
			gpiod_set_value_cansleep(pin->gpiod, 0);
			pin->enable_count = 0;
		}
	}

	return 0;
}

/**
 * _regulator_delay_helper - a delay helper function
 * @delay: time to delay in microseconds
 *
 * Delay for the requested amount of time as per the guidelines in:
 *
 *     Documentation/timers/timers-howto.rst
 *
 * The assumption here is that these regulator operations will never used in
 * atomic context and therefore sleeping functions can be used.
 */
static void _regulator_delay_helper(unsigned int delay)
{
	unsigned int ms = delay / 1000;
	unsigned int us = delay % 1000;

	if (ms > 0) {
		/*
		 * For small enough values, handle super-millisecond
		 * delays in the usleep_range() call below.
		 */
		if (ms < 20)
			us += ms * 1000;
		else
			msleep(ms);
	}

	/*
	 * Give the scheduler some room to coalesce with any other
	 * wakeup sources. For delays shorter than 10 us, don't even
	 * bother setting up high-resolution timers and just busy-
	 * loop.
	 */
	if (us >= 10)
		usleep_range(us, us + 100);
	else
		udelay(us);
}

/**
 * _regulator_check_status_enabled
 *
 * A helper function to check if the regulator status can be interpreted
 * as 'regulator is enabled'.
 * @rdev: the regulator device to check
 *
 * Return:
 * * 1			- if status shows regulator is in enabled state
 * * 0			- if not enabled state
 * * Error Value	- as received from ops->get_status()
 */
static inline int _regulator_check_status_enabled(struct regulator_dev *rdev)
{
	int ret = rdev->desc->ops->get_status(rdev);

	if (ret < 0) {
		rdev_info(rdev, "get_status returned error: %d\n", ret);
		return ret;
	}

	switch (ret) {
	case REGULATOR_STATUS_OFF:
	case REGULATOR_STATUS_ERROR:
	case REGULATOR_STATUS_UNDEFINED:
		return 0;
	default:
		return 1;
	}
}

static int _regulator_do_enable(struct regulator_dev *rdev)
{
	int ret, delay;

	/* Query before enabling in case configuration dependent.  */
	ret = _regulator_get_enable_time(rdev);
	if (ret >= 0) {
		delay = ret;
	} else {
		rdev_warn(rdev, "enable_time() failed: %pe\n", ERR_PTR(ret));
		delay = 0;
	}

	trace_regulator_enable(rdev_get_name(rdev));

	if (rdev->desc->off_on_delay && rdev->last_off) {
		/* if needed, keep a distance of off_on_delay from last time
		 * this regulator was disabled.
		 */
		ktime_t end = ktime_add_us(rdev->last_off, rdev->desc->off_on_delay);
		s64 remaining = ktime_us_delta(end, ktime_get());

		if (remaining > 0)
			_regulator_delay_helper(remaining);
	}

	if (rdev->ena_pin) {
		if (!rdev->ena_gpio_state) {
			ret = regulator_ena_gpio_ctrl(rdev, true);
			if (ret < 0)
				return ret;
			rdev->ena_gpio_state = 1;
		}
	} else if (rdev->desc->ops->enable) {
		ret = rdev->desc->ops->enable(rdev);
		if (ret < 0)
			return ret;
	} else {
		return -EINVAL;
	}

	/* Allow the regulator to ramp; it would be useful to extend
	 * this for bulk operations so that the regulators can ramp
	 * together.
	 */
	trace_regulator_enable_delay(rdev_get_name(rdev));

	/* If poll_enabled_time is set, poll upto the delay calculated
	 * above, delaying poll_enabled_time uS to check if the regulator
	 * actually got enabled.
	 * If the regulator isn't enabled after our delay helper has expired,
	 * return -ETIMEDOUT.
	 */
	if (rdev->desc->poll_enabled_time) {
		int time_remaining = delay;

		while (time_remaining > 0) {
			_regulator_delay_helper(rdev->desc->poll_enabled_time);

			if (rdev->desc->ops->get_status) {
				ret = _regulator_check_status_enabled(rdev);
				if (ret < 0)
					return ret;
				else if (ret)
					break;
			} else if (rdev->desc->ops->is_enabled(rdev))
				break;

			time_remaining -= rdev->desc->poll_enabled_time;
		}

		if (time_remaining <= 0) {
			rdev_err(rdev, "Enabled check timed out\n");
			return -ETIMEDOUT;
		}
	} else {
		_regulator_delay_helper(delay);
	}

	trace_regulator_enable_complete(rdev_get_name(rdev));

	return 0;
}

/**
 * _regulator_handle_consumer_enable - handle that a consumer enabled
 * @regulator: regulator source
 *
 * Some things on a regulator consumer (like the contribution towards total
 * load on the regulator) only have an effect when the consumer wants the
 * regulator enabled.  Explained in example with two consumers of the same
 * regulator:
 *   consumer A: set_load(100);       => total load = 0
 *   consumer A: regulator_enable();  => total load = 100
 *   consumer B: set_load(1000);      => total load = 100
 *   consumer B: regulator_enable();  => total load = 1100
 *   consumer A: regulator_disable(); => total_load = 1000
 *
 * This function (together with _regulator_handle_consumer_disable) is
 * responsible for keeping track of the refcount for a given regulator consumer
 * and applying / unapplying these things.
 *
 * Returns 0 upon no error; -error upon error.
 */
static int _regulator_handle_consumer_enable(struct regulator *regulator)
{
	int ret;
	struct regulator_dev *rdev = regulator->rdev;

	lockdep_assert_held_once(&rdev->mutex.base);

	regulator->enable_count++;
	if (regulator->uA_load && regulator->enable_count == 1) {
		ret = drms_uA_update(rdev);
		if (ret)
			regulator->enable_count--;
		return ret;
	}

	return 0;
}

/**
 * _regulator_handle_consumer_disable - handle that a consumer disabled
 * @regulator: regulator source
 *
 * The opposite of _regulator_handle_consumer_enable().
 *
 * Returns 0 upon no error; -error upon error.
 */
static int _regulator_handle_consumer_disable(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;

	lockdep_assert_held_once(&rdev->mutex.base);

	if (!regulator->enable_count) {
		rdev_err(rdev, "Underflow of regulator enable count\n");
		return -EINVAL;
	}

	regulator->enable_count--;
	if (regulator->uA_load && regulator->enable_count == 0)
		return drms_uA_update(rdev);

	return 0;
}

/* locks held by regulator_enable() */
static int _regulator_enable(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret;

	lockdep_assert_held_once(&rdev->mutex.base);

	if (rdev->use_count == 0 && rdev->supply) {
		ret = _regulator_enable(rdev->supply);
		if (ret < 0)
			return ret;
	}

	/* balance only if there are regulators coupled */
	if (rdev->coupling_desc.n_coupled > 1) {
		ret = regulator_balance_voltage(rdev, PM_SUSPEND_ON);
		if (ret < 0)
			goto err_disable_supply;
	}

	ret = _regulator_handle_consumer_enable(regulator);
	if (ret < 0)
		goto err_disable_supply;

	if (rdev->use_count == 0) {
		/*
		 * The regulator may already be enabled if it's not switchable
		 * or was left on
		 */
		ret = _regulator_is_enabled(rdev);
		if (ret == -EINVAL || ret == 0) {
			if (!regulator_ops_is_valid(rdev,
					REGULATOR_CHANGE_STATUS)) {
				ret = -EPERM;
				goto err_consumer_disable;
			}

			ret = _regulator_do_enable(rdev);
			if (ret < 0)
				goto err_consumer_disable;

			_notifier_call_chain(rdev, REGULATOR_EVENT_ENABLE,
					     NULL);
		} else if (ret < 0) {
			rdev_err(rdev, "is_enabled() failed: %pe\n", ERR_PTR(ret));
			goto err_consumer_disable;
		}
		/* Fallthrough on positive return values - already enabled */
	}

	rdev->use_count++;

	return 0;

err_consumer_disable:
	_regulator_handle_consumer_disable(regulator);

err_disable_supply:
	if (rdev->use_count == 0 && rdev->supply)
		_regulator_disable(rdev->supply);

	return ret;
}

/**
 * regulator_enable - enable regulator output
 * @regulator: regulator source
 *
 * Request that the regulator be enabled with the regulator output at
 * the predefined voltage or current value.  Calls to regulator_enable()
 * must be balanced with calls to regulator_disable().
 *
 * NOTE: the output value can be set by other drivers, boot loader or may be
 * hardwired in the regulator.
 */
int regulator_enable(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	struct ww_acquire_ctx ww_ctx;
	int ret;

	regulator_lock_dependent(rdev, &ww_ctx);
	ret = _regulator_enable(regulator);
	regulator_unlock_dependent(rdev, &ww_ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_enable);

static int _regulator_do_disable(struct regulator_dev *rdev)
{
	int ret;

	trace_regulator_disable(rdev_get_name(rdev));

	if (rdev->ena_pin) {
		if (rdev->ena_gpio_state) {
			ret = regulator_ena_gpio_ctrl(rdev, false);
			if (ret < 0)
				return ret;
			rdev->ena_gpio_state = 0;
		}

	} else if (rdev->desc->ops->disable) {
		ret = rdev->desc->ops->disable(rdev);
		if (ret != 0)
			return ret;
	}

	if (rdev->desc->off_on_delay)
		rdev->last_off = ktime_get();

	trace_regulator_disable_complete(rdev_get_name(rdev));

	return 0;
}

/* locks held by regulator_disable() */
static int _regulator_disable(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret = 0;

	lockdep_assert_held_once(&rdev->mutex.base);

	if (WARN(rdev->use_count <= 0,
		 "unbalanced disables for %s\n", rdev_get_name(rdev)))
		return -EIO;

	/* are we the last user and permitted to disable ? */
	if (rdev->use_count == 1 &&
	    (rdev->constraints && !rdev->constraints->always_on)) {

		/* we are last user */
		if (regulator_ops_is_valid(rdev, REGULATOR_CHANGE_STATUS)) {
			ret = _notifier_call_chain(rdev,
						   REGULATOR_EVENT_PRE_DISABLE,
						   NULL);
			if (ret & NOTIFY_STOP_MASK)
				return -EINVAL;

			ret = _regulator_do_disable(rdev);
			if (ret < 0) {
				rdev_err(rdev, "failed to disable: %pe\n", ERR_PTR(ret));
				_notifier_call_chain(rdev,
						REGULATOR_EVENT_ABORT_DISABLE,
						NULL);
				return ret;
			}
			_notifier_call_chain(rdev, REGULATOR_EVENT_DISABLE,
					NULL);
		}

		rdev->use_count = 0;
	} else if (rdev->use_count > 1) {
		rdev->use_count--;
	}

	if (ret == 0)
		ret = _regulator_handle_consumer_disable(regulator);

	if (ret == 0 && rdev->coupling_desc.n_coupled > 1)
		ret = regulator_balance_voltage(rdev, PM_SUSPEND_ON);

	if (ret == 0 && rdev->use_count == 0 && rdev->supply)
		ret = _regulator_disable(rdev->supply);

	return ret;
}

/**
 * regulator_disable - disable regulator output
 * @regulator: regulator source
 *
 * Disable the regulator output voltage or current.  Calls to
 * regulator_enable() must be balanced with calls to
 * regulator_disable().
 *
 * NOTE: this will only disable the regulator output if no other consumer
 * devices have it enabled, the regulator device supports disabling and
 * machine constraints permit this operation.
 */
int regulator_disable(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	struct ww_acquire_ctx ww_ctx;
	int ret;

	regulator_lock_dependent(rdev, &ww_ctx);
	ret = _regulator_disable(regulator);
	regulator_unlock_dependent(rdev, &ww_ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_disable);

/* locks held by regulator_force_disable() */
static int _regulator_force_disable(struct regulator_dev *rdev)
{
	int ret = 0;

	lockdep_assert_held_once(&rdev->mutex.base);

	ret = _notifier_call_chain(rdev, REGULATOR_EVENT_FORCE_DISABLE |
			REGULATOR_EVENT_PRE_DISABLE, NULL);
	if (ret & NOTIFY_STOP_MASK)
		return -EINVAL;

	ret = _regulator_do_disable(rdev);
	if (ret < 0) {
		rdev_err(rdev, "failed to force disable: %pe\n", ERR_PTR(ret));
		_notifier_call_chain(rdev, REGULATOR_EVENT_FORCE_DISABLE |
				REGULATOR_EVENT_ABORT_DISABLE, NULL);
		return ret;
	}

	_notifier_call_chain(rdev, REGULATOR_EVENT_FORCE_DISABLE |
			REGULATOR_EVENT_DISABLE, NULL);

	return 0;
}

/**
 * regulator_force_disable - force disable regulator output
 * @regulator: regulator source
 *
 * Forcibly disable the regulator output voltage or current.
 * NOTE: this *will* disable the regulator output even if other consumer
 * devices have it enabled. This should be used for situations when device
 * damage will likely occur if the regulator is not disabled (e.g. over temp).
 */
int regulator_force_disable(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	struct ww_acquire_ctx ww_ctx;
	int ret;

	regulator_lock_dependent(rdev, &ww_ctx);

	ret = _regulator_force_disable(regulator->rdev);

	if (rdev->coupling_desc.n_coupled > 1)
		regulator_balance_voltage(rdev, PM_SUSPEND_ON);

	if (regulator->uA_load) {
		regulator->uA_load = 0;
		ret = drms_uA_update(rdev);
	}

	if (rdev->use_count != 0 && rdev->supply)
		_regulator_disable(rdev->supply);

	regulator_unlock_dependent(rdev, &ww_ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_force_disable);

static void regulator_disable_work(struct work_struct *work)
{
	struct regulator_dev *rdev = container_of(work, struct regulator_dev,
						  disable_work.work);
	struct ww_acquire_ctx ww_ctx;
	int count, i, ret;
	struct regulator *regulator;
	int total_count = 0;

	regulator_lock_dependent(rdev, &ww_ctx);

	/*
	 * Workqueue functions queue the new work instance while the previous
	 * work instance is being processed. Cancel the queued work instance
	 * as the work instance under processing does the job of the queued
	 * work instance.
	 */
	cancel_delayed_work(&rdev->disable_work);

	list_for_each_entry(regulator, &rdev->consumer_list, list) {
		count = regulator->deferred_disables;

		if (!count)
			continue;

		total_count += count;
		regulator->deferred_disables = 0;

		for (i = 0; i < count; i++) {
			ret = _regulator_disable(regulator);
			if (ret != 0)
				rdev_err(rdev, "Deferred disable failed: %pe\n",
					 ERR_PTR(ret));
		}
	}
	WARN_ON(!total_count);

	if (rdev->coupling_desc.n_coupled > 1)
		regulator_balance_voltage(rdev, PM_SUSPEND_ON);

	regulator_unlock_dependent(rdev, &ww_ctx);
}

/**
 * regulator_disable_deferred - disable regulator output with delay
 * @regulator: regulator source
 * @ms: milliseconds until the regulator is disabled
 *
 * Execute regulator_disable() on the regulator after a delay.  This
 * is intended for use with devices that require some time to quiesce.
 *
 * NOTE: this will only disable the regulator output if no other consumer
 * devices have it enabled, the regulator device supports disabling and
 * machine constraints permit this operation.
 */
int regulator_disable_deferred(struct regulator *regulator, int ms)
{
	struct regulator_dev *rdev = regulator->rdev;

	if (!ms)
		return regulator_disable(regulator);

	regulator_lock(rdev);
	regulator->deferred_disables++;
	mod_delayed_work(system_power_efficient_wq, &rdev->disable_work,
			 msecs_to_jiffies(ms));
	regulator_unlock(rdev);

	return 0;
}
EXPORT_SYMBOL_GPL(regulator_disable_deferred);

static int _regulator_is_enabled(struct regulator_dev *rdev)
{
	/* A GPIO control always takes precedence */
	if (rdev->ena_pin)
		return rdev->ena_gpio_state;

	/* If we don't know then assume that the regulator is always on */
	if (!rdev->desc->ops->is_enabled)
		return 1;

	return rdev->desc->ops->is_enabled(rdev);
}

static int _regulator_list_voltage(struct regulator_dev *rdev,
				   unsigned selector, int lock)
{
	const struct regulator_ops *ops = rdev->desc->ops;
	int ret;

	if (rdev->desc->fixed_uV && rdev->desc->n_voltages == 1 && !selector)
		return rdev->desc->fixed_uV;

	if (ops->list_voltage) {
		if (selector >= rdev->desc->n_voltages)
			return -EINVAL;
		if (selector < rdev->desc->linear_min_sel)
			return 0;
		if (lock)
			regulator_lock(rdev);
		ret = ops->list_voltage(rdev, selector);
		if (lock)
			regulator_unlock(rdev);
	} else if (rdev->is_switch && rdev->supply) {
		ret = _regulator_list_voltage(rdev->supply->rdev,
					      selector, lock);
	} else {
		return -EINVAL;
	}

	if (ret > 0) {
		if (ret < rdev->constraints->min_uV)
			ret = 0;
		else if (ret > rdev->constraints->max_uV)
			ret = 0;
	}

	return ret;
}

/**
 * regulator_is_enabled - is the regulator output enabled
 * @regulator: regulator source
 *
 * Returns positive if the regulator driver backing the source/client
 * has requested that the device be enabled, zero if it hasn't, else a
 * negative errno code.
 *
 * Note that the device backing this regulator handle can have multiple
 * users, so it might be enabled even if regulator_enable() was never
 * called for this particular source.
 */
int regulator_is_enabled(struct regulator *regulator)
{
	int ret;

	if (regulator->always_on)
		return 1;

	regulator_lock(regulator->rdev);
	ret = _regulator_is_enabled(regulator->rdev);
	regulator_unlock(regulator->rdev);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_is_enabled);

/**
 * regulator_count_voltages - count regulator_list_voltage() selectors
 * @regulator: regulator source
 *
 * Returns number of selectors, or negative errno.  Selectors are
 * numbered starting at zero, and typically correspond to bitfields
 * in hardware registers.
 */
int regulator_count_voltages(struct regulator *regulator)
{
	struct regulator_dev	*rdev = regulator->rdev;

	if (rdev->desc->n_voltages)
		return rdev->desc->n_voltages;

	if (!rdev->is_switch || !rdev->supply)
		return -EINVAL;

	return regulator_count_voltages(rdev->supply);
}
EXPORT_SYMBOL_GPL(regulator_count_voltages);

/**
 * regulator_list_voltage - enumerate supported voltages
 * @regulator: regulator source
 * @selector: identify voltage to list
 * Context: can sleep
 *
 * Returns a voltage that can be passed to @regulator_set_voltage(),
 * zero if this selector code can't be used on this system, or a
 * negative errno.
 */
int regulator_list_voltage(struct regulator *regulator, unsigned selector)
{
	return _regulator_list_voltage(regulator->rdev, selector, 1);
}
EXPORT_SYMBOL_GPL(regulator_list_voltage);

/**
 * regulator_get_regmap - get the regulator's register map
 * @regulator: regulator source
 *
 * Returns the register map for the given regulator, or an ERR_PTR value
 * if the regulator doesn't use regmap.
 */
struct regmap *regulator_get_regmap(struct regulator *regulator)
{
	struct regmap *map = regulator->rdev->regmap;

	return map ? map : ERR_PTR(-EOPNOTSUPP);
}

/**
 * regulator_get_hardware_vsel_register - get the HW voltage selector register
 * @regulator: regulator source
 * @vsel_reg: voltage selector register, output parameter
 * @vsel_mask: mask for voltage selector bitfield, output parameter
 *
 * Returns the hardware register offset and bitmask used for setting the
 * regulator voltage. This might be useful when configuring voltage-scaling
 * hardware or firmware that can make I2C requests behind the kernel's back,
 * for example.
 *
 * On success, the output parameters @vsel_reg and @vsel_mask are filled in
 * and 0 is returned, otherwise a negative errno is returned.
 */
int regulator_get_hardware_vsel_register(struct regulator *regulator,
					 unsigned *vsel_reg,
					 unsigned *vsel_mask)
{
	struct regulator_dev *rdev = regulator->rdev;
	const struct regulator_ops *ops = rdev->desc->ops;

	if (ops->set_voltage_sel != regulator_set_voltage_sel_regmap)
		return -EOPNOTSUPP;

	*vsel_reg = rdev->desc->vsel_reg;
	*vsel_mask = rdev->desc->vsel_mask;

	return 0;
}
EXPORT_SYMBOL_GPL(regulator_get_hardware_vsel_register);

/**
 * regulator_list_hardware_vsel - get the HW-specific register value for a selector
 * @regulator: regulator source
 * @selector: identify voltage to list
 *
 * Converts the selector to a hardware-specific voltage selector that can be
 * directly written to the regulator registers. The address of the voltage
 * register can be determined by calling @regulator_get_hardware_vsel_register.
 *
 * On error a negative errno is returned.
 */
int regulator_list_hardware_vsel(struct regulator *regulator,
				 unsigned selector)
{
	struct regulator_dev *rdev = regulator->rdev;
	const struct regulator_ops *ops = rdev->desc->ops;

	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;
	if (selector < rdev->desc->linear_min_sel)
		return 0;
	if (ops->set_voltage_sel != regulator_set_voltage_sel_regmap)
		return -EOPNOTSUPP;

	return selector;
}
EXPORT_SYMBOL_GPL(regulator_list_hardware_vsel);

/**
 * regulator_get_linear_step - return the voltage step size between VSEL values
 * @regulator: regulator source
 *
 * Returns the voltage step size between VSEL values for linear
 * regulators, or return 0 if the regulator isn't a linear regulator.
 */
unsigned int regulator_get_linear_step(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;

	return rdev->desc->uV_step;
}
EXPORT_SYMBOL_GPL(regulator_get_linear_step);

/**
 * regulator_is_supported_voltage - check if a voltage range can be supported
 *
 * @regulator: Regulator to check.
 * @min_uV: Minimum required voltage in uV.
 * @max_uV: Maximum required voltage in uV.
 *
 * Returns a boolean.
 */
int regulator_is_supported_voltage(struct regulator *regulator,
				   int min_uV, int max_uV)
{
	struct regulator_dev *rdev = regulator->rdev;
	int i, voltages, ret;

	/* If we can't change voltage check the current voltage */
	if (!regulator_ops_is_valid(rdev, REGULATOR_CHANGE_VOLTAGE)) {
		ret = regulator_get_voltage(regulator);
		if (ret >= 0)
			return min_uV <= ret && ret <= max_uV;
		else
			return ret;
	}

	/* Any voltage within constrains range is fine? */
	if (rdev->desc->continuous_voltage_range)
		return min_uV >= rdev->constraints->min_uV &&
				max_uV <= rdev->constraints->max_uV;

	ret = regulator_count_voltages(regulator);
	if (ret < 0)
		return 0;
	voltages = ret;

	for (i = 0; i < voltages; i++) {
		ret = regulator_list_voltage(regulator, i);

		if (ret >= min_uV && ret <= max_uV)
			return 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(regulator_is_supported_voltage);

static int regulator_map_voltage(struct regulator_dev *rdev, int min_uV,
				 int max_uV)
{
	const struct regulator_desc *desc = rdev->desc;

	if (desc->ops->map_voltage)
		return desc->ops->map_voltage(rdev, min_uV, max_uV);

	if (desc->ops->list_voltage == regulator_list_voltage_linear)
		return regulator_map_voltage_linear(rdev, min_uV, max_uV);

	if (desc->ops->list_voltage == regulator_list_voltage_linear_range)
		return regulator_map_voltage_linear_range(rdev, min_uV, max_uV);

	if (desc->ops->list_voltage ==
		regulator_list_voltage_pickable_linear_range)
		return regulator_map_voltage_pickable_linear_range(rdev,
							min_uV, max_uV);

	return regulator_map_voltage_iterate(rdev, min_uV, max_uV);
}

static int _regulator_call_set_voltage(struct regulator_dev *rdev,
				       int min_uV, int max_uV,
				       unsigned *selector)
{
	struct pre_voltage_change_data data;
	int ret;

	data.old_uV = regulator_get_voltage_rdev(rdev);
	data.min_uV = min_uV;
	data.max_uV = max_uV;
	ret = _notifier_call_chain(rdev, REGULATOR_EVENT_PRE_VOLTAGE_CHANGE,
				   &data);
	if (ret & NOTIFY_STOP_MASK)
		return -EINVAL;

	ret = rdev->desc->ops->set_voltage(rdev, min_uV, max_uV, selector);
	if (ret >= 0)
		return ret;

	_notifier_call_chain(rdev, REGULATOR_EVENT_ABORT_VOLTAGE_CHANGE,
			     (void *)data.old_uV);

	return ret;
}

static int _regulator_call_set_voltage_sel(struct regulator_dev *rdev,
					   int uV, unsigned selector)
{
	struct pre_voltage_change_data data;
	int ret;

	data.old_uV = regulator_get_voltage_rdev(rdev);
	data.min_uV = uV;
	data.max_uV = uV;
	ret = _notifier_call_chain(rdev, REGULATOR_EVENT_PRE_VOLTAGE_CHANGE,
				   &data);
	if (ret & NOTIFY_STOP_MASK)
		return -EINVAL;

	ret = rdev->desc->ops->set_voltage_sel(rdev, selector);
	if (ret >= 0)
		return ret;

	_notifier_call_chain(rdev, REGULATOR_EVENT_ABORT_VOLTAGE_CHANGE,
			     (void *)data.old_uV);

	return ret;
}

static int _regulator_set_voltage_sel_step(struct regulator_dev *rdev,
					   int uV, int new_selector)
{
	const struct regulator_ops *ops = rdev->desc->ops;
	int diff, old_sel, curr_sel, ret;

	/* Stepping is only needed if the regulator is enabled. */
	if (!_regulator_is_enabled(rdev))
		goto final_set;

	if (!ops->get_voltage_sel)
		return -EINVAL;

	old_sel = ops->get_voltage_sel(rdev);
	if (old_sel < 0)
		return old_sel;

	diff = new_selector - old_sel;
	if (diff == 0)
		return 0; /* No change needed. */

	if (diff > 0) {
		/* Stepping up. */
		for (curr_sel = old_sel + rdev->desc->vsel_step;
		     curr_sel < new_selector;
		     curr_sel += rdev->desc->vsel_step) {
			/*
			 * Call the callback directly instead of using
			 * _regulator_call_set_voltage_sel() as we don't
			 * want to notify anyone yet. Same in the branch
			 * below.
			 */
			ret = ops->set_voltage_sel(rdev, curr_sel);
			if (ret)
				goto try_revert;
		}
	} else {
		/* Stepping down. */
		for (curr_sel = old_sel - rdev->desc->vsel_step;
		     curr_sel > new_selector;
		     curr_sel -= rdev->desc->vsel_step) {
			ret = ops->set_voltage_sel(rdev, curr_sel);
			if (ret)
				goto try_revert;
		}
	}

final_set:
	/* The final selector will trigger the notifiers. */
	return _regulator_call_set_voltage_sel(rdev, uV, new_selector);

try_revert:
	/*
	 * At least try to return to the previous voltage if setting a new
	 * one failed.
	 */
	(void)ops->set_voltage_sel(rdev, old_sel);
	return ret;
}

static int _regulator_set_voltage_time(struct regulator_dev *rdev,
				       int old_uV, int new_uV)
{
	unsigned int ramp_delay = 0;

	if (rdev->constraints->ramp_delay)
		ramp_delay = rdev->constraints->ramp_delay;
	else if (rdev->desc->ramp_delay)
		ramp_delay = rdev->desc->ramp_delay;
	else if (rdev->constraints->settling_time)
		return rdev->constraints->settling_time;
	else if (rdev->constraints->settling_time_up &&
		 (new_uV > old_uV))
		return rdev->constraints->settling_time_up;
	else if (rdev->constraints->settling_time_down &&
		 (new_uV < old_uV))
		return rdev->constraints->settling_time_down;

	if (ramp_delay == 0)
		return 0;

	return DIV_ROUND_UP(abs(new_uV - old_uV), ramp_delay);
}

static int _regulator_do_set_voltage(struct regulator_dev *rdev,
				     int min_uV, int max_uV)
{
	int ret;
	int delay = 0;
	int best_val = 0;
	unsigned int selector;
	int old_selector = -1;
	const struct regulator_ops *ops = rdev->desc->ops;
	int old_uV = regulator_get_voltage_rdev(rdev);

	trace_regulator_set_voltage(rdev_get_name(rdev), min_uV, max_uV);

	min_uV += rdev->constraints->uV_offset;
	max_uV += rdev->constraints->uV_offset;

	/*
	 * If we can't obtain the old selector there is not enough
	 * info to call set_voltage_time_sel().
	 */
	if (_regulator_is_enabled(rdev) &&
	    ops->set_voltage_time_sel && ops->get_voltage_sel) {
		old_selector = ops->get_voltage_sel(rdev);
		if (old_selector < 0)
			return old_selector;
	}

	if (ops->set_voltage) {
		ret = _regulator_call_set_voltage(rdev, min_uV, max_uV,
						  &selector);

		if (ret >= 0) {
			if (ops->list_voltage)
				best_val = ops->list_voltage(rdev,
							     selector);
			else
				best_val = regulator_get_voltage_rdev(rdev);
		}

	} else if (ops->set_voltage_sel) {
		ret = regulator_map_voltage(rdev, min_uV, max_uV);
		if (ret >= 0) {
			best_val = ops->list_voltage(rdev, ret);
			if (min_uV <= best_val && max_uV >= best_val) {
				selector = ret;
				if (old_selector == selector)
					ret = 0;
				else if (rdev->desc->vsel_step)
					ret = _regulator_set_voltage_sel_step(
						rdev, best_val, selector);
				else
					ret = _regulator_call_set_voltage_sel(
						rdev, best_val, selector);
			} else {
				ret = -EINVAL;
			}
		}
	} else {
		ret = -EINVAL;
	}

	if (ret)
		goto out;

	if (ops->set_voltage_time_sel) {
		/*
		 * Call set_voltage_time_sel if successfully obtained
		 * old_selector
		 */
		if (old_selector >= 0 && old_selector != selector)
			delay = ops->set_voltage_time_sel(rdev, old_selector,
							  selector);
	} else {
		if (old_uV != best_val) {
			if (ops->set_voltage_time)
				delay = ops->set_voltage_time(rdev, old_uV,
							      best_val);
			else
				delay = _regulator_set_voltage_time(rdev,
								    old_uV,
								    best_val);
		}
	}

	if (delay < 0) {
		rdev_warn(rdev, "failed to get delay: %pe\n", ERR_PTR(delay));
		delay = 0;
	}

	/* Insert any necessary delays */
	_regulator_delay_helper(delay);

	if (best_val >= 0) {
		unsigned long data = best_val;

		_notifier_call_chain(rdev, REGULATOR_EVENT_VOLTAGE_CHANGE,
				     (void *)data);
	}

out:
	trace_regulator_set_voltage_complete(rdev_get_name(rdev), best_val);

	return ret;
}

static int _regulator_do_set_suspend_voltage(struct regulator_dev *rdev,
				  int min_uV, int max_uV, suspend_state_t state)
{
	struct regulator_state *rstate;
	int uV, sel;

	rstate = regulator_get_suspend_state(rdev, state);
	if (rstate == NULL)
		return -EINVAL;

	if (min_uV < rstate->min_uV)
		min_uV = rstate->min_uV;
	if (max_uV > rstate->max_uV)
		max_uV = rstate->max_uV;

	sel = regulator_map_voltage(rdev, min_uV, max_uV);
	if (sel < 0)
		return sel;

	uV = rdev->desc->ops->list_voltage(rdev, sel);
	if (uV >= min_uV && uV <= max_uV)
		rstate->uV = uV;

	return 0;
}

static int regulator_set_voltage_unlocked(struct regulator *regulator,
					  int min_uV, int max_uV,
					  suspend_state_t state)
{
	struct regulator_dev *rdev = regulator->rdev;
	struct regulator_voltage *voltage = &regulator->voltage[state];
	int ret = 0;
	int old_min_uV, old_max_uV;
	int current_uV;

	/* If we're setting the same range as last time the change
	 * should be a noop (some cpufreq implementations use the same
	 * voltage for multiple frequencies, for example).
	 */
	if (voltage->min_uV == min_uV && voltage->max_uV == max_uV)
		goto out;

	/* If we're trying to set a range that overlaps the current voltage,
	 * return successfully even though the regulator does not support
	 * changing the voltage.
	 */
	if (!regulator_ops_is_valid(rdev, REGULATOR_CHANGE_VOLTAGE)) {
		current_uV = regulator_get_voltage_rdev(rdev);
		if (min_uV <= current_uV && current_uV <= max_uV) {
			voltage->min_uV = min_uV;
			voltage->max_uV = max_uV;
			goto out;
		}
	}

	/* sanity check */
	if (!rdev->desc->ops->set_voltage &&
	    !rdev->desc->ops->set_voltage_sel) {
		ret = -EINVAL;
		goto out;
	}

	/* constraints check */
	ret = regulator_check_voltage(rdev, &min_uV, &max_uV);
	if (ret < 0)
		goto out;

	/* restore original values in case of error */
	old_min_uV = voltage->min_uV;
	old_max_uV = voltage->max_uV;
	voltage->min_uV = min_uV;
	voltage->max_uV = max_uV;

	/* for not coupled regulators this will just set the voltage */
	ret = regulator_balance_voltage(rdev, state);
	if (ret < 0) {
		voltage->min_uV = old_min_uV;
		voltage->max_uV = old_max_uV;
	}

out:
	return ret;
}

int regulator_set_voltage_rdev(struct regulator_dev *rdev, int min_uV,
			       int max_uV, suspend_state_t state)
{
	int best_supply_uV = 0;
	int supply_change_uV = 0;
	int ret;

	if (rdev->supply &&
	    regulator_ops_is_valid(rdev->supply->rdev,
				   REGULATOR_CHANGE_VOLTAGE) &&
	    (rdev->desc->min_dropout_uV || !(rdev->desc->ops->get_voltage ||
					   rdev->desc->ops->get_voltage_sel))) {
		int current_supply_uV;
		int selector;

		selector = regulator_map_voltage(rdev, min_uV, max_uV);
		if (selector < 0) {
			ret = selector;
			goto out;
		}

		best_supply_uV = _regulator_list_voltage(rdev, selector, 0);
		if (best_supply_uV < 0) {
			ret = best_supply_uV;
			goto out;
		}

		best_supply_uV += rdev->desc->min_dropout_uV;

		current_supply_uV = regulator_get_voltage_rdev(rdev->supply->rdev);
		if (current_supply_uV < 0) {
			ret = current_supply_uV;
			goto out;
		}

		supply_change_uV = best_supply_uV - current_supply_uV;
	}

	if (supply_change_uV > 0) {
		ret = regulator_set_voltage_unlocked(rdev->supply,
				best_supply_uV, INT_MAX, state);
		if (ret) {
			dev_err(&rdev->dev, "Failed to increase supply voltage: %pe\n",
				ERR_PTR(ret));
			goto out;
		}
	}

	if (state == PM_SUSPEND_ON)
		ret = _regulator_do_set_voltage(rdev, min_uV, max_uV);
	else
		ret = _regulator_do_set_suspend_voltage(rdev, min_uV,
							max_uV, state);
	if (ret < 0)
		goto out;

	if (supply_change_uV < 0) {
		ret = regulator_set_voltage_unlocked(rdev->supply,
				best_supply_uV, INT_MAX, state);
		if (ret)
			dev_warn(&rdev->dev, "Failed to decrease supply voltage: %pe\n",
				 ERR_PTR(ret));
		/* No need to fail here */
		ret = 0;
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_voltage_rdev);

static int regulator_limit_voltage_step(struct regulator_dev *rdev,
					int *current_uV, int *min_uV)
{
	struct regulation_constraints *constraints = rdev->constraints;

	/* Limit voltage change only if necessary */
	if (!constraints->max_uV_step || !_regulator_is_enabled(rdev))
		return 1;

	if (*current_uV < 0) {
		*current_uV = regulator_get_voltage_rdev(rdev);

		if (*current_uV < 0)
			return *current_uV;
	}

	if (abs(*current_uV - *min_uV) <= constraints->max_uV_step)
		return 1;

	/* Clamp target voltage within the given step */
	if (*current_uV < *min_uV)
		*min_uV = min(*current_uV + constraints->max_uV_step,
			      *min_uV);
	else
		*min_uV = max(*current_uV - constraints->max_uV_step,
			      *min_uV);

	return 0;
}

static int regulator_get_optimal_voltage(struct regulator_dev *rdev,
					 int *current_uV,
					 int *min_uV, int *max_uV,
					 suspend_state_t state,
					 int n_coupled)
{
	struct coupling_desc *c_desc = &rdev->coupling_desc;
	struct regulator_dev **c_rdevs = c_desc->coupled_rdevs;
	struct regulation_constraints *constraints = rdev->constraints;
	int desired_min_uV = 0, desired_max_uV = INT_MAX;
	int max_current_uV = 0, min_current_uV = INT_MAX;
	int highest_min_uV = 0, target_uV, possible_uV;
	int i, ret, max_spread;
	bool done;

	*current_uV = -1;

	/*
	 * If there are no coupled regulators, simply set the voltage
	 * demanded by consumers.
	 */
	if (n_coupled == 1) {
		/*
		 * If consumers don't provide any demands, set voltage
		 * to min_uV
		 */
		desired_min_uV = constraints->min_uV;
		desired_max_uV = constraints->max_uV;

		ret = regulator_check_consumers(rdev,
						&desired_min_uV,
						&desired_max_uV, state);
		if (ret < 0)
			return ret;

		possible_uV = desired_min_uV;
		done = true;

		goto finish;
	}

	/* Find highest min desired voltage */
	for (i = 0; i < n_coupled; i++) {
		int tmp_min = 0;
		int tmp_max = INT_MAX;

		lockdep_assert_held_once(&c_rdevs[i]->mutex.base);

		ret = regulator_check_consumers(c_rdevs[i],
						&tmp_min,
						&tmp_max, state);
		if (ret < 0)
			return ret;

		ret = regulator_check_voltage(c_rdevs[i], &tmp_min, &tmp_max);
		if (ret < 0)
			return ret;

		highest_min_uV = max(highest_min_uV, tmp_min);

		if (i == 0) {
			desired_min_uV = tmp_min;
			desired_max_uV = tmp_max;
		}
	}

	max_spread = constraints->max_spread[0];

	/*
	 * Let target_uV be equal to the desired one if possible.
	 * If not, set it to minimum voltage, allowed by other coupled
	 * regulators.
	 */
	target_uV = max(desired_min_uV, highest_min_uV - max_spread);

	/*
	 * Find min and max voltages, which currently aren't violating
	 * max_spread.
	 */
	for (i = 1; i < n_coupled; i++) {
		int tmp_act;

		if (!_regulator_is_enabled(c_rdevs[i]))
			continue;

		tmp_act = regulator_get_voltage_rdev(c_rdevs[i]);
		if (tmp_act < 0)
			return tmp_act;

		min_current_uV = min(tmp_act, min_current_uV);
		max_current_uV = max(tmp_act, max_current_uV);
	}

	/* There aren't any other regulators enabled */
	if (max_current_uV == 0) {
		possible_uV = target_uV;
	} else {
		/*
		 * Correct target voltage, so as it currently isn't
		 * violating max_spread
		 */
		possible_uV = max(target_uV, max_current_uV - max_spread);
		possible_uV = min(possible_uV, min_current_uV + max_spread);
	}

	if (possible_uV > desired_max_uV)
		return -EINVAL;

	done = (possible_uV == target_uV);
	desired_min_uV = possible_uV;

finish:
	/* Apply max_uV_step constraint if necessary */
	if (state == PM_SUSPEND_ON) {
		ret = regulator_limit_voltage_step(rdev, current_uV,
						   &desired_min_uV);
		if (ret < 0)
			return ret;

		if (ret == 0)
			done = false;
	}

	/* Set current_uV if wasn't done earlier in the code and if necessary */
	if (n_coupled > 1 && *current_uV == -1) {

		if (_regulator_is_enabled(rdev)) {
			ret = regulator_get_voltage_rdev(rdev);
			if (ret < 0)
				return ret;

			*current_uV = ret;
		} else {
			*current_uV = desired_min_uV;
		}
	}

	*min_uV = desired_min_uV;
	*max_uV = desired_max_uV;

	return done;
}

int regulator_do_balance_voltage(struct regulator_dev *rdev,
				 suspend_state_t state, bool skip_coupled)
{
	struct regulator_dev **c_rdevs;
	struct regulator_dev *best_rdev;
	struct coupling_desc *c_desc = &rdev->coupling_desc;
	int i, ret, n_coupled, best_min_uV, best_max_uV, best_c_rdev;
	unsigned int delta, best_delta;
	unsigned long c_rdev_done = 0;
	bool best_c_rdev_done;

	c_rdevs = c_desc->coupled_rdevs;
	n_coupled = skip_coupled ? 1 : c_desc->n_coupled;

	/*
	 * Find the best possible voltage change on each loop. Leave the loop
	 * if there isn't any possible change.
	 */
	do {
		best_c_rdev_done = false;
		best_delta = 0;
		best_min_uV = 0;
		best_max_uV = 0;
		best_c_rdev = 0;
		best_rdev = NULL;

		/*
		 * Find highest difference between optimal voltage
		 * and current voltage.
		 */
		for (i = 0; i < n_coupled; i++) {
			/*
			 * optimal_uV is the best voltage that can be set for
			 * i-th regulator at the moment without violating
			 * max_spread constraint in order to balance
			 * the coupled voltages.
			 */
			int optimal_uV = 0, optimal_max_uV = 0, current_uV = 0;

			if (test_bit(i, &c_rdev_done))
				continue;

			ret = regulator_get_optimal_voltage(c_rdevs[i],
							    &current_uV,
							    &optimal_uV,
							    &optimal_max_uV,
							    state, n_coupled);
			if (ret < 0)
				goto out;

			delta = abs(optimal_uV - current_uV);

			if (delta && best_delta <= delta) {
				best_c_rdev_done = ret;
				best_delta = delta;
				best_rdev = c_rdevs[i];
				best_min_uV = optimal_uV;
				best_max_uV = optimal_max_uV;
				best_c_rdev = i;
			}
		}

		/* Nothing to change, return successfully */
		if (!best_rdev) {
			ret = 0;
			goto out;
		}

		ret = regulator_set_voltage_rdev(best_rdev, best_min_uV,
						 best_max_uV, state);

		if (ret < 0)
			goto out;

		if (best_c_rdev_done)
			set_bit(best_c_rdev, &c_rdev_done);

	} while (n_coupled > 1);

out:
	return ret;
}

static int regulator_balance_voltage(struct regulator_dev *rdev,
				     suspend_state_t state)
{
	struct coupling_desc *c_desc = &rdev->coupling_desc;
	struct regulator_coupler *coupler = c_desc->coupler;
	bool skip_coupled = false;

	/*
	 * If system is in a state other than PM_SUSPEND_ON, don't check
	 * other coupled regulators.
	 */
	if (state != PM_SUSPEND_ON)
		skip_coupled = true;

	if (c_desc->n_resolved < c_desc->n_coupled) {
		rdev_err(rdev, "Not all coupled regulators registered\n");
		return -EPERM;
	}

	/* Invoke custom balancer for customized couplers */
	if (coupler && coupler->balance_voltage)
		return coupler->balance_voltage(coupler, rdev, state);

	return regulator_do_balance_voltage(rdev, state, skip_coupled);
}

/**
 * regulator_set_voltage - set regulator output voltage
 * @regulator: regulator source
 * @min_uV: Minimum required voltage in uV
 * @max_uV: Maximum acceptable voltage in uV
 *
 * Sets a voltage regulator to the desired output voltage. This can be set
 * during any regulator state. IOW, regulator can be disabled or enabled.
 *
 * If the regulator is enabled then the voltage will change to the new value
 * immediately otherwise if the regulator is disabled the regulator will
 * output at the new voltage when enabled.
 *
 * NOTE: If the regulator is shared between several devices then the lowest
 * request voltage that meets the system constraints will be used.
 * Regulator system constraints must be set for this regulator before
 * calling this function otherwise this call will fail.
 */
int regulator_set_voltage(struct regulator *regulator, int min_uV, int max_uV)
{
	struct ww_acquire_ctx ww_ctx;
	int ret;

	regulator_lock_dependent(regulator->rdev, &ww_ctx);

	ret = regulator_set_voltage_unlocked(regulator, min_uV, max_uV,
					     PM_SUSPEND_ON);

	regulator_unlock_dependent(regulator->rdev, &ww_ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_voltage);

static inline int regulator_suspend_toggle(struct regulator_dev *rdev,
					   suspend_state_t state, bool en)
{
	struct regulator_state *rstate;

	rstate = regulator_get_suspend_state(rdev, state);
	if (rstate == NULL)
		return -EINVAL;

	if (!rstate->changeable)
		return -EPERM;

	rstate->enabled = (en) ? ENABLE_IN_SUSPEND : DISABLE_IN_SUSPEND;

	return 0;
}

int regulator_suspend_enable(struct regulator_dev *rdev,
				    suspend_state_t state)
{
	return regulator_suspend_toggle(rdev, state, true);
}
EXPORT_SYMBOL_GPL(regulator_suspend_enable);

int regulator_suspend_disable(struct regulator_dev *rdev,
				     suspend_state_t state)
{
	struct regulator *regulator;
	struct regulator_voltage *voltage;

	/*
	 * if any consumer wants this regulator device keeping on in
	 * suspend states, don't set it as disabled.
	 */
	list_for_each_entry(regulator, &rdev->consumer_list, list) {
		voltage = &regulator->voltage[state];
		if (voltage->min_uV || voltage->max_uV)
			return 0;
	}

	return regulator_suspend_toggle(rdev, state, false);
}
EXPORT_SYMBOL_GPL(regulator_suspend_disable);

static int _regulator_set_suspend_voltage(struct regulator *regulator,
					  int min_uV, int max_uV,
					  suspend_state_t state)
{
	struct regulator_dev *rdev = regulator->rdev;
	struct regulator_state *rstate;

	rstate = regulator_get_suspend_state(rdev, state);
	if (rstate == NULL)
		return -EINVAL;

	if (rstate->min_uV == rstate->max_uV) {
		rdev_err(rdev, "The suspend voltage can't be changed!\n");
		return -EPERM;
	}

	return regulator_set_voltage_unlocked(regulator, min_uV, max_uV, state);
}

int regulator_set_suspend_voltage(struct regulator *regulator, int min_uV,
				  int max_uV, suspend_state_t state)
{
	struct ww_acquire_ctx ww_ctx;
	int ret;

	/* PM_SUSPEND_ON is handled by regulator_set_voltage() */
	if (regulator_check_states(state) || state == PM_SUSPEND_ON)
		return -EINVAL;

	regulator_lock_dependent(regulator->rdev, &ww_ctx);

	ret = _regulator_set_suspend_voltage(regulator, min_uV,
					     max_uV, state);

	regulator_unlock_dependent(regulator->rdev, &ww_ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_suspend_voltage);

/**
 * regulator_set_voltage_time - get raise/fall time
 * @regulator: regulator source
 * @old_uV: starting voltage in microvolts
 * @new_uV: target voltage in microvolts
 *
 * Provided with the starting and ending voltage, this function attempts to
 * calculate the time in microseconds required to rise or fall to this new
 * voltage.
 */
int regulator_set_voltage_time(struct regulator *regulator,
			       int old_uV, int new_uV)
{
	struct regulator_dev *rdev = regulator->rdev;
	const struct regulator_ops *ops = rdev->desc->ops;
	int old_sel = -1;
	int new_sel = -1;
	int voltage;
	int i;

	if (ops->set_voltage_time)
		return ops->set_voltage_time(rdev, old_uV, new_uV);
	else if (!ops->set_voltage_time_sel)
		return _regulator_set_voltage_time(rdev, old_uV, new_uV);

	/* Currently requires operations to do this */
	if (!ops->list_voltage || !rdev->desc->n_voltages)
		return -EINVAL;

	for (i = 0; i < rdev->desc->n_voltages; i++) {
		/* We only look for exact voltage matches here */
		if (i < rdev->desc->linear_min_sel)
			continue;

		if (old_sel >= 0 && new_sel >= 0)
			break;

		voltage = regulator_list_voltage(regulator, i);
		if (voltage < 0)
			return -EINVAL;
		if (voltage == 0)
			continue;
		if (voltage == old_uV)
			old_sel = i;
		if (voltage == new_uV)
			new_sel = i;
	}

	if (old_sel < 0 || new_sel < 0)
		return -EINVAL;

	return ops->set_voltage_time_sel(rdev, old_sel, new_sel);
}
EXPORT_SYMBOL_GPL(regulator_set_voltage_time);

/**
 * regulator_set_voltage_time_sel - get raise/fall time
 * @rdev: regulator source device
 * @old_selector: selector for starting voltage
 * @new_selector: selector for target voltage
 *
 * Provided with the starting and target voltage selectors, this function
 * returns time in microseconds required to rise or fall to this new voltage
 *
 * Drivers providing ramp_delay in regulation_constraints can use this as their
 * set_voltage_time_sel() operation.
 */
int regulator_set_voltage_time_sel(struct regulator_dev *rdev,
				   unsigned int old_selector,
				   unsigned int new_selector)
{
	int old_volt, new_volt;

	/* sanity check */
	if (!rdev->desc->ops->list_voltage)
		return -EINVAL;

	old_volt = rdev->desc->ops->list_voltage(rdev, old_selector);
	new_volt = rdev->desc->ops->list_voltage(rdev, new_selector);

	if (rdev->desc->ops->set_voltage_time)
		return rdev->desc->ops->set_voltage_time(rdev, old_volt,
							 new_volt);
	else
		return _regulator_set_voltage_time(rdev, old_volt, new_volt);
}
EXPORT_SYMBOL_GPL(regulator_set_voltage_time_sel);

int regulator_sync_voltage_rdev(struct regulator_dev *rdev)
{
	int ret;

	regulator_lock(rdev);

	if (!rdev->desc->ops->set_voltage &&
	    !rdev->desc->ops->set_voltage_sel) {
		ret = -EINVAL;
		goto out;
	}

	/* balance only, if regulator is coupled */
	if (rdev->coupling_desc.n_coupled > 1)
		ret = regulator_balance_voltage(rdev, PM_SUSPEND_ON);
	else
		ret = -EOPNOTSUPP;

out:
	regulator_unlock(rdev);
	return ret;
}

/**
 * regulator_sync_voltage - re-apply last regulator output voltage
 * @regulator: regulator source
 *
 * Re-apply the last configured voltage.  This is intended to be used
 * where some external control source the consumer is cooperating with
 * has caused the configured voltage to change.
 */
int regulator_sync_voltage(struct regulator *regulator)
{
	struct regulator_dev *rdev = regulator->rdev;
	struct regulator_voltage *voltage = &regulator->voltage[PM_SUSPEND_ON];
	int ret, min_uV, max_uV;

	if (!regulator_ops_is_valid(rdev, REGULATOR_CHANGE_VOLTAGE))
		return 0;

	regulator_lock(rdev);

	if (!rdev->desc->ops->set_voltage &&
	    !rdev->desc->ops->set_voltage_sel) {
		ret = -EINVAL;
		goto out;
	}

	/* This is only going to work if we've had a voltage configured. */
	if (!voltage->min_uV && !voltage->max_uV) {
		ret = -EINVAL;
		goto out;
	}

	min_uV = voltage->min_uV;
	max_uV = voltage->max_uV;

	/* This should be a paranoia check... */
	ret = regulator_check_voltage(rdev, &min_uV, &max_uV);
	if (ret < 0)
		goto out;

	ret = regulator_check_consumers(rdev, &min_uV, &max_uV, 0);
	if (ret < 0)
		goto out;

	/* balance only, if regulator is coupled */
	if (rdev->coupling_desc.n_coupled > 1)
		ret = regulator_balance_voltage(rdev, PM_SUSPEND_ON);
	else
		ret = _regulator_do_set_voltage(rdev, min_uV, max_uV);

out:
	regulator_unlock(rdev);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_sync_voltage);

int regulator_get_voltage_rdev(struct regulator_dev *rdev)
{
	int sel, ret;
	bool bypassed;

	if (rdev->desc->ops->get_bypass) {
		ret = rdev->desc->ops->get_bypass(rdev, &bypassed);
		if (ret < 0)
			return ret;
		if (bypassed) {
			/* if bypassed the regulator must have a supply */
			if (!rdev->supply) {
				rdev_err(rdev,
					 "bypassed regulator has no supply!\n");
				return -EPROBE_DEFER;
			}

			return regulator_get_voltage_rdev(rdev->supply->rdev);
		}
	}

	if (rdev->desc->ops->get_voltage_sel) {
		sel = rdev->desc->ops->get_voltage_sel(rdev);
		if (sel < 0)
			return sel;
		ret = rdev->desc->ops->list_voltage(rdev, sel);
	} else if (rdev->desc->ops->get_voltage) {
		ret = rdev->desc->ops->get_voltage(rdev);
	} else if (rdev->desc->ops->list_voltage) {
		ret = rdev->desc->ops->list_voltage(rdev, 0);
	} else if (rdev->desc->fixed_uV && (rdev->desc->n_voltages == 1)) {
		ret = rdev->desc->fixed_uV;
	} else if (rdev->supply) {
		ret = regulator_get_voltage_rdev(rdev->supply->rdev);
	} else if (rdev->supply_name) {
		return -EPROBE_DEFER;
	} else {
		return -EINVAL;
	}

	if (ret < 0)
		return ret;
	return ret - rdev->constraints->uV_offset;
}
EXPORT_SYMBOL_GPL(regulator_get_voltage_rdev);

/**
 * regulator_get_voltage - get regulator output voltage
 * @regulator: regulator source
 *
 * This returns the current regulator voltage in uV.
 *
 * NOTE: If the regulator is disabled it will return the voltage value. This
 * function should not be used to determine regulator state.
 */
int regulator_get_voltage(struct regulator *regulator)
{
	struct ww_acquire_ctx ww_ctx;
	int ret;

	regulator_lock_dependent(regulator->rdev, &ww_ctx);
	ret = regulator_get_voltage_rdev(regulator->rdev);
	regulator_unlock_dependent(regulator->rdev, &ww_ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_get_voltage);

/**
 * regulator_set_current_limit - set regulator output current limit
 * @regulator: regulator source
 * @min_uA: Minimum supported current in uA
 * @max_uA: Maximum supported current in uA
 *
 * Sets current sink to the desired output current. This can be set during
 * any regulator state. IOW, regulator can be disabled or enabled.
 *
 * If the regulator is enabled then the current will change to the new value
 * immediately otherwise if the regulator is disabled the regulator will
 * output at the new current when enabled.
 *
 * NOTE: Regulator system constraints must be set for this regulator before
 * calling this function otherwise this call will fail.
 */
int regulator_set_current_limit(struct regulator *regulator,
			       int min_uA, int max_uA)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret;

	regulator_lock(rdev);

	/* sanity check */
	if (!rdev->desc->ops->set_current_limit) {
		ret = -EINVAL;
		goto out;
	}

	/* constraints check */
	ret = regulator_check_current_limit(rdev, &min_uA, &max_uA);
	if (ret < 0)
		goto out;

	ret = rdev->desc->ops->set_current_limit(rdev, min_uA, max_uA);
out:
	regulator_unlock(rdev);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_current_limit);

static int _regulator_get_current_limit_unlocked(struct regulator_dev *rdev)
{
	/* sanity check */
	if (!rdev->desc->ops->get_current_limit)
		return -EINVAL;

	return rdev->desc->ops->get_current_limit(rdev);
}

static int _regulator_get_current_limit(struct regulator_dev *rdev)
{
	int ret;

	regulator_lock(rdev);
	ret = _regulator_get_current_limit_unlocked(rdev);
	regulator_unlock(rdev);

	return ret;
}

/**
 * regulator_get_current_limit - get regulator output current
 * @regulator: regulator source
 *
 * This returns the current supplied by the specified current sink in uA.
 *
 * NOTE: If the regulator is disabled it will return the current value. This
 * function should not be used to determine regulator state.
 */
int regulator_get_current_limit(struct regulator *regulator)
{
	return _regulator_get_current_limit(regulator->rdev);
}
EXPORT_SYMBOL_GPL(regulator_get_current_limit);

/**
 * regulator_set_mode - set regulator operating mode
 * @regulator: regulator source
 * @mode: operating mode - one of the REGULATOR_MODE constants
 *
 * Set regulator operating mode to increase regulator efficiency or improve
 * regulation performance.
 *
 * NOTE: Regulator system constraints must be set for this regulator before
 * calling this function otherwise this call will fail.
 */
int regulator_set_mode(struct regulator *regulator, unsigned int mode)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret;
	int regulator_curr_mode;

	regulator_lock(rdev);

	/* sanity check */
	if (!rdev->desc->ops->set_mode) {
		ret = -EINVAL;
		goto out;
	}

	/* return if the same mode is requested */
	if (rdev->desc->ops->get_mode) {
		regulator_curr_mode = rdev->desc->ops->get_mode(rdev);
		if (regulator_curr_mode == mode) {
			ret = 0;
			goto out;
		}
	}

	/* constraints check */
	ret = regulator_mode_constrain(rdev, &mode);
	if (ret < 0)
		goto out;

	ret = rdev->desc->ops->set_mode(rdev, mode);
out:
	regulator_unlock(rdev);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_mode);

static unsigned int _regulator_get_mode_unlocked(struct regulator_dev *rdev)
{
	/* sanity check */
	if (!rdev->desc->ops->get_mode)
		return -EINVAL;

	return rdev->desc->ops->get_mode(rdev);
}

static unsigned int _regulator_get_mode(struct regulator_dev *rdev)
{
	int ret;

	regulator_lock(rdev);
	ret = _regulator_get_mode_unlocked(rdev);
	regulator_unlock(rdev);

	return ret;
}

/**
 * regulator_get_mode - get regulator operating mode
 * @regulator: regulator source
 *
 * Get the current regulator operating mode.
 */
unsigned int regulator_get_mode(struct regulator *regulator)
{
	return _regulator_get_mode(regulator->rdev);
}
EXPORT_SYMBOL_GPL(regulator_get_mode);

static int rdev_get_cached_err_flags(struct regulator_dev *rdev)
{
	int ret = 0;

	if (rdev->use_cached_err) {
		spin_lock(&rdev->err_lock);
		ret = rdev->cached_err;
		spin_unlock(&rdev->err_lock);
	}
	return ret;
}

static int _regulator_get_error_flags(struct regulator_dev *rdev,
					unsigned int *flags)
{
	int cached_flags, ret = 0;

	regulator_lock(rdev);

	cached_flags = rdev_get_cached_err_flags(rdev);

	if (rdev->desc->ops->get_error_flags)
		ret = rdev->desc->ops->get_error_flags(rdev, flags);
	else if (!rdev->use_cached_err)
		ret = -EINVAL;

	*flags |= cached_flags;

	regulator_unlock(rdev);

	return ret;
}

/**
 * regulator_get_error_flags - get regulator error information
 * @regulator: regulator source
 * @flags: pointer to store error flags
 *
 * Get the current regulator error information.
 */
int regulator_get_error_flags(struct regulator *regulator,
				unsigned int *flags)
{
	return _regulator_get_error_flags(regulator->rdev, flags);
}
EXPORT_SYMBOL_GPL(regulator_get_error_flags);

/**
 * regulator_set_load - set regulator load
 * @regulator: regulator source
 * @uA_load: load current
 *
 * Notifies the regulator core of a new device load. This is then used by
 * DRMS (if enabled by constraints) to set the most efficient regulator
 * operating mode for the new regulator loading.
 *
 * Consumer devices notify their supply regulator of the maximum power
 * they will require (can be taken from device datasheet in the power
 * consumption tables) when they change operational status and hence power
 * state. Examples of operational state changes that can affect power
 * consumption are :-
 *
 *    o Device is opened / closed.
 *    o Device I/O is about to begin or has just finished.
 *    o Device is idling in between work.
 *
 * This information is also exported via sysfs to userspace.
 *
 * DRMS will sum the total requested load on the regulator and change
 * to the most efficient operating mode if platform constraints allow.
 *
 * NOTE: when a regulator consumer requests to have a regulator
 * disabled then any load that consumer requested no longer counts
 * toward the total requested load.  If the regulator is re-enabled
 * then the previously requested load will start counting again.
 *
 * If a regulator is an always-on regulator then an individual consumer's
 * load will still be removed if that consumer is fully disabled.
 *
 * On error a negative errno is returned.
 */
int regulator_set_load(struct regulator *regulator, int uA_load)
{
	struct regulator_dev *rdev = regulator->rdev;
	int old_uA_load;
	int ret = 0;

	regulator_lock(rdev);
	old_uA_load = regulator->uA_load;
	regulator->uA_load = uA_load;
	if (regulator->enable_count && old_uA_load != uA_load) {
		ret = drms_uA_update(rdev);
		if (ret < 0)
			regulator->uA_load = old_uA_load;
	}
	regulator_unlock(rdev);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_load);

/**
 * regulator_allow_bypass - allow the regulator to go into bypass mode
 *
 * @regulator: Regulator to configure
 * @enable: enable or disable bypass mode
 *
 * Allow the regulator to go into bypass mode if all other consumers
 * for the regulator also enable bypass mode and the machine
 * constraints allow this.  Bypass mode means that the regulator is
 * simply passing the input directly to the output with no regulation.
 */
int regulator_allow_bypass(struct regulator *regulator, bool enable)
{
	struct regulator_dev *rdev = regulator->rdev;
	const char *name = rdev_get_name(rdev);
	int ret = 0;

	if (!rdev->desc->ops->set_bypass)
		return 0;

	if (!regulator_ops_is_valid(rdev, REGULATOR_CHANGE_BYPASS))
		return 0;

	regulator_lock(rdev);

	if (enable && !regulator->bypass) {
		rdev->bypass_count++;

		if (rdev->bypass_count == rdev->open_count) {
			trace_regulator_bypass_enable(name);

			ret = rdev->desc->ops->set_bypass(rdev, enable);
			if (ret != 0)
				rdev->bypass_count--;
			else
				trace_regulator_bypass_enable_complete(name);
		}

	} else if (!enable && regulator->bypass) {
		rdev->bypass_count--;

		if (rdev->bypass_count != rdev->open_count) {
			trace_regulator_bypass_disable(name);

			ret = rdev->desc->ops->set_bypass(rdev, enable);
			if (ret != 0)
				rdev->bypass_count++;
			else
				trace_regulator_bypass_disable_complete(name);
		}
	}

	if (ret == 0)
		regulator->bypass = enable;

	regulator_unlock(rdev);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_allow_bypass);

/**
 * regulator_register_notifier - register regulator event notifier
 * @regulator: regulator source
 * @nb: notifier block
 *
 * Register notifier block to receive regulator events.
 */
int regulator_register_notifier(struct regulator *regulator,
			      struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&regulator->rdev->notifier,
						nb);
}
EXPORT_SYMBOL_GPL(regulator_register_notifier);

/**
 * regulator_unregister_notifier - unregister regulator event notifier
 * @regulator: regulator source
 * @nb: notifier block
 *
 * Unregister regulator event notifier block.
 */
int regulator_unregister_notifier(struct regulator *regulator,
				struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&regulator->rdev->notifier,
						  nb);
}
EXPORT_SYMBOL_GPL(regulator_unregister_notifier);

/* notify regulator consumers and downstream regulator consumers.
 * Note mutex must be held by caller.
 */
static int _notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data)
{
	/* call rdev chain first */
	return blocking_notifier_call_chain(&rdev->notifier, event, data);
}

/**
 * regulator_bulk_get - get multiple regulator consumers
 *
 * @dev:           Device to supply
 * @num_consumers: Number of consumers to register
 * @consumers:     Configuration of consumers; clients are stored here.
 *
 * @return 0 on success, an errno on failure.
 *
 * This helper function allows drivers to get several regulator
 * consumers in one operation.  If any of the regulators cannot be
 * acquired then any regulators that were allocated will be freed
 * before returning to the caller.
 */
int regulator_bulk_get(struct device *dev, int num_consumers,
		       struct regulator_bulk_data *consumers)
{
	int i;
	int ret;

	for (i = 0; i < num_consumers; i++)
		consumers[i].consumer = NULL;

	for (i = 0; i < num_consumers; i++) {
		consumers[i].consumer = regulator_get(dev,
						      consumers[i].supply);
		if (IS_ERR(consumers[i].consumer)) {
			ret = dev_err_probe(dev, PTR_ERR(consumers[i].consumer),
					    "Failed to get supply '%s'",
					    consumers[i].supply);
			consumers[i].consumer = NULL;
			goto err;
		}

		if (consumers[i].init_load_uA > 0) {
			ret = regulator_set_load(consumers[i].consumer,
						 consumers[i].init_load_uA);
			if (ret) {
				i++;
				goto err;
			}
		}
	}

	return 0;

err:
	while (--i >= 0)
		regulator_put(consumers[i].consumer);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_get);

static void regulator_bulk_enable_async(void *data, async_cookie_t cookie)
{
	struct regulator_bulk_data *bulk = data;

	bulk->ret = regulator_enable(bulk->consumer);
}

/**
 * regulator_bulk_enable - enable multiple regulator consumers
 *
 * @num_consumers: Number of consumers
 * @consumers:     Consumer data; clients are stored here.
 * @return         0 on success, an errno on failure
 *
 * This convenience API allows consumers to enable multiple regulator
 * clients in a single API call.  If any consumers cannot be enabled
 * then any others that were enabled will be disabled again prior to
 * return.
 */
int regulator_bulk_enable(int num_consumers,
			  struct regulator_bulk_data *consumers)
{
	ASYNC_DOMAIN_EXCLUSIVE(async_domain);
	int i;
	int ret = 0;

	for (i = 0; i < num_consumers; i++) {
		async_schedule_domain(regulator_bulk_enable_async,
				      &consumers[i], &async_domain);
	}

	async_synchronize_full_domain(&async_domain);

	/* If any consumer failed we need to unwind any that succeeded */
	for (i = 0; i < num_consumers; i++) {
		if (consumers[i].ret != 0) {
			ret = consumers[i].ret;
			goto err;
		}
	}

	return 0;

err:
	for (i = 0; i < num_consumers; i++) {
		if (consumers[i].ret < 0)
			pr_err("Failed to enable %s: %pe\n", consumers[i].supply,
			       ERR_PTR(consumers[i].ret));
		else
			regulator_disable(consumers[i].consumer);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_enable);

/**
 * regulator_bulk_disable - disable multiple regulator consumers
 *
 * @num_consumers: Number of consumers
 * @consumers:     Consumer data; clients are stored here.
 * @return         0 on success, an errno on failure
 *
 * This convenience API allows consumers to disable multiple regulator
 * clients in a single API call.  If any consumers cannot be disabled
 * then any others that were disabled will be enabled again prior to
 * return.
 */
int regulator_bulk_disable(int num_consumers,
			   struct regulator_bulk_data *consumers)
{
	int i;
	int ret, r;

	for (i = num_consumers - 1; i >= 0; --i) {
		ret = regulator_disable(consumers[i].consumer);
		if (ret != 0)
			goto err;
	}

	return 0;

err:
	pr_err("Failed to disable %s: %pe\n", consumers[i].supply, ERR_PTR(ret));
	for (++i; i < num_consumers; ++i) {
		r = regulator_enable(consumers[i].consumer);
		if (r != 0)
			pr_err("Failed to re-enable %s: %pe\n",
			       consumers[i].supply, ERR_PTR(r));
	}

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_disable);

/**
 * regulator_bulk_force_disable - force disable multiple regulator consumers
 *
 * @num_consumers: Number of consumers
 * @consumers:     Consumer data; clients are stored here.
 * @return         0 on success, an errno on failure
 *
 * This convenience API allows consumers to forcibly disable multiple regulator
 * clients in a single API call.
 * NOTE: This should be used for situations when device damage will
 * likely occur if the regulators are not disabled (e.g. over temp).
 * Although regulator_force_disable function call for some consumers can
 * return error numbers, the function is called for all consumers.
 */
int regulator_bulk_force_disable(int num_consumers,
			   struct regulator_bulk_data *consumers)
{
	int i;
	int ret = 0;

	for (i = 0; i < num_consumers; i++) {
		consumers[i].ret =
			    regulator_force_disable(consumers[i].consumer);

		/* Store first error for reporting */
		if (consumers[i].ret && !ret)
			ret = consumers[i].ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_force_disable);

/**
 * regulator_bulk_free - free multiple regulator consumers
 *
 * @num_consumers: Number of consumers
 * @consumers:     Consumer data; clients are stored here.
 *
 * This convenience API allows consumers to free multiple regulator
 * clients in a single API call.
 */
void regulator_bulk_free(int num_consumers,
			 struct regulator_bulk_data *consumers)
{
	int i;

	for (i = 0; i < num_consumers; i++) {
		regulator_put(consumers[i].consumer);
		consumers[i].consumer = NULL;
	}
}
EXPORT_SYMBOL_GPL(regulator_bulk_free);

/**
 * regulator_notifier_call_chain - call regulator event notifier
 * @rdev: regulator source
 * @event: notifier block
 * @data: callback-specific data.
 *
 * Called by regulator drivers to notify clients a regulator event has
 * occurred.
 */
int regulator_notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data)
{
	_notifier_call_chain(rdev, event, data);
	return NOTIFY_DONE;

}
EXPORT_SYMBOL_GPL(regulator_notifier_call_chain);

/**
 * regulator_mode_to_status - convert a regulator mode into a status
 *
 * @mode: Mode to convert
 *
 * Convert a regulator mode into a status.
 */
int regulator_mode_to_status(unsigned int mode)
{
	switch (mode) {
	case REGULATOR_MODE_FAST:
		return REGULATOR_STATUS_FAST;
	case REGULATOR_MODE_NORMAL:
		return REGULATOR_STATUS_NORMAL;
	case REGULATOR_MODE_IDLE:
		return REGULATOR_STATUS_IDLE;
	case REGULATOR_MODE_STANDBY:
		return REGULATOR_STATUS_STANDBY;
	default:
		return REGULATOR_STATUS_UNDEFINED;
	}
}
EXPORT_SYMBOL_GPL(regulator_mode_to_status);

static struct attribute *regulator_dev_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_num_users.attr,
	&dev_attr_type.attr,
	&dev_attr_microvolts.attr,
	&dev_attr_microamps.attr,
	&dev_attr_opmode.attr,
	&dev_attr_state.attr,
	&dev_attr_status.attr,
	&dev_attr_bypass.attr,
	&dev_attr_requested_microamps.attr,
	&dev_attr_min_microvolts.attr,
	&dev_attr_max_microvolts.attr,
	&dev_attr_min_microamps.attr,
	&dev_attr_max_microamps.attr,
	&dev_attr_under_voltage.attr,
	&dev_attr_over_current.attr,
	&dev_attr_regulation_out.attr,
	&dev_attr_fail.attr,
	&dev_attr_over_temp.attr,
	&dev_attr_under_voltage_warn.attr,
	&dev_attr_over_current_warn.attr,
	&dev_attr_over_voltage_warn.attr,
	&dev_attr_over_temp_warn.attr,
	&dev_attr_suspend_standby_state.attr,
	&dev_attr_suspend_mem_state.attr,
	&dev_attr_suspend_disk_state.attr,
	&dev_attr_suspend_standby_microvolts.attr,
	&dev_attr_suspend_mem_microvolts.attr,
	&dev_attr_suspend_disk_microvolts.attr,
	&dev_attr_suspend_standby_mode.attr,
	&dev_attr_suspend_mem_mode.attr,
	&dev_attr_suspend_disk_mode.attr,
	NULL
};

/*
 * To avoid cluttering sysfs (and memory) with useless state, only
 * create attributes that can be meaningfully displayed.
 */
static umode_t regulator_attr_is_visible(struct kobject *kobj,
					 struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct regulator_dev *rdev = dev_to_rdev(dev);
	const struct regulator_ops *ops = rdev->desc->ops;
	umode_t mode = attr->mode;

	/* these three are always present */
	if (attr == &dev_attr_name.attr ||
	    attr == &dev_attr_num_users.attr ||
	    attr == &dev_attr_type.attr)
		return mode;

	/* some attributes need specific methods to be displayed */
	if (attr == &dev_attr_microvolts.attr) {
		if ((ops->get_voltage && ops->get_voltage(rdev) >= 0) ||
		    (ops->get_voltage_sel && ops->get_voltage_sel(rdev) >= 0) ||
		    (ops->list_voltage && ops->list_voltage(rdev, 0) >= 0) ||
		    (rdev->desc->fixed_uV && rdev->desc->n_voltages == 1))
			return mode;
		return 0;
	}

	if (attr == &dev_attr_microamps.attr)
		return ops->get_current_limit ? mode : 0;

	if (attr == &dev_attr_opmode.attr)
		return ops->get_mode ? mode : 0;

	if (attr == &dev_attr_state.attr)
		return (rdev->ena_pin || ops->is_enabled) ? mode : 0;

	if (attr == &dev_attr_status.attr)
		return ops->get_status ? mode : 0;

	if (attr == &dev_attr_bypass.attr)
		return ops->get_bypass ? mode : 0;

	if (attr == &dev_attr_under_voltage.attr ||
	    attr == &dev_attr_over_current.attr ||
	    attr == &dev_attr_regulation_out.attr ||
	    attr == &dev_attr_fail.attr ||
	    attr == &dev_attr_over_temp.attr ||
	    attr == &dev_attr_under_voltage_warn.attr ||
	    attr == &dev_attr_over_current_warn.attr ||
	    attr == &dev_attr_over_voltage_warn.attr ||
	    attr == &dev_attr_over_temp_warn.attr)
		return ops->get_error_flags ? mode : 0;

	/* constraints need specific supporting methods */
	if (attr == &dev_attr_min_microvolts.attr ||
	    attr == &dev_attr_max_microvolts.attr)
		return (ops->set_voltage || ops->set_voltage_sel) ? mode : 0;

	if (attr == &dev_attr_min_microamps.attr ||
	    attr == &dev_attr_max_microamps.attr)
		return ops->set_current_limit ? mode : 0;

	if (attr == &dev_attr_suspend_standby_state.attr ||
	    attr == &dev_attr_suspend_mem_state.attr ||
	    attr == &dev_attr_suspend_disk_state.attr)
		return mode;

	if (attr == &dev_attr_suspend_standby_microvolts.attr ||
	    attr == &dev_attr_suspend_mem_microvolts.attr ||
	    attr == &dev_attr_suspend_disk_microvolts.attr)
		return ops->set_suspend_voltage ? mode : 0;

	if (attr == &dev_attr_suspend_standby_mode.attr ||
	    attr == &dev_attr_suspend_mem_mode.attr ||
	    attr == &dev_attr_suspend_disk_mode.attr)
		return ops->set_suspend_mode ? mode : 0;

	return mode;
}

static const struct attribute_group regulator_dev_group = {
	.attrs = regulator_dev_attrs,
	.is_visible = regulator_attr_is_visible,
};

static const struct attribute_group *regulator_dev_groups[] = {
	&regulator_dev_group,
	NULL
};

static void regulator_dev_release(struct device *dev)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	debugfs_remove_recursive(rdev->debugfs);
	kfree(rdev->constraints);
	of_node_put(rdev->dev.of_node);
	kfree(rdev);
}

static void rdev_init_debugfs(struct regulator_dev *rdev)
{
	struct device *parent = rdev->dev.parent;
	const char *rname = rdev_get_name(rdev);
	char name[NAME_MAX];

	/* Avoid duplicate debugfs directory names */
	if (parent && rname == rdev->desc->name) {
		snprintf(name, sizeof(name), "%s-%s", dev_name(parent),
			 rname);
		rname = name;
	}

	rdev->debugfs = debugfs_create_dir(rname, debugfs_root);
	if (!rdev->debugfs) {
		rdev_warn(rdev, "Failed to create debugfs directory\n");
		return;
	}

	debugfs_create_u32("use_count", 0444, rdev->debugfs,
			   &rdev->use_count);
	debugfs_create_u32("open_count", 0444, rdev->debugfs,
			   &rdev->open_count);
	debugfs_create_u32("bypass_count", 0444, rdev->debugfs,
			   &rdev->bypass_count);
}

static int regulator_register_resolve_supply(struct device *dev, void *data)
{
	struct regulator_dev *rdev = dev_to_rdev(dev);

	if (regulator_resolve_supply(rdev))
		rdev_dbg(rdev, "unable to resolve supply\n");

	return 0;
}

int regulator_coupler_register(struct regulator_coupler *coupler)
{
	mutex_lock(&regulator_list_mutex);
	list_add_tail(&coupler->list, &regulator_coupler_list);
	mutex_unlock(&regulator_list_mutex);

	return 0;
}

static struct regulator_coupler *
regulator_find_coupler(struct regulator_dev *rdev)
{
	struct regulator_coupler *coupler;
	int err;

	/*
	 * Note that regulators are appended to the list and the generic
	 * coupler is registered first, hence it will be attached at last
	 * if nobody cared.
	 */
	list_for_each_entry_reverse(coupler, &regulator_coupler_list, list) {
		err = coupler->attach_regulator(coupler, rdev);
		if (!err) {
			if (!coupler->balance_voltage &&
			    rdev->coupling_desc.n_coupled > 2)
				goto err_unsupported;

			return coupler;
		}

		if (err < 0)
			return ERR_PTR(err);

		if (err == 1)
			continue;

		break;
	}

	return ERR_PTR(-EINVAL);

err_unsupported:
	if (coupler->detach_regulator)
		coupler->detach_regulator(coupler, rdev);

	rdev_err(rdev,
		"Voltage balancing for multiple regulator couples is unimplemented\n");

	return ERR_PTR(-EPERM);
}

static void regulator_resolve_coupling(struct regulator_dev *rdev)
{
	struct regulator_coupler *coupler = rdev->coupling_desc.coupler;
	struct coupling_desc *c_desc = &rdev->coupling_desc;
	int n_coupled = c_desc->n_coupled;
	struct regulator_dev *c_rdev;
	int i;

	for (i = 1; i < n_coupled; i++) {
		/* already resolved */
		if (c_desc->coupled_rdevs[i])
			continue;

		c_rdev = of_parse_coupled_regulator(rdev, i - 1);

		if (!c_rdev)
			continue;

		if (c_rdev->coupling_desc.coupler != coupler) {
			rdev_err(rdev, "coupler mismatch with %s\n",
				 rdev_get_name(c_rdev));
			return;
		}

		c_desc->coupled_rdevs[i] = c_rdev;
		c_desc->n_resolved++;

		regulator_resolve_coupling(c_rdev);
	}
}

static void regulator_remove_coupling(struct regulator_dev *rdev)
{
	struct regulator_coupler *coupler = rdev->coupling_desc.coupler;
	struct coupling_desc *__c_desc, *c_desc = &rdev->coupling_desc;
	struct regulator_dev *__c_rdev, *c_rdev;
	unsigned int __n_coupled, n_coupled;
	int i, k;
	int err;

	n_coupled = c_desc->n_coupled;

	for (i = 1; i < n_coupled; i++) {
		c_rdev = c_desc->coupled_rdevs[i];

		if (!c_rdev)
			continue;

		regulator_lock(c_rdev);

		__c_desc = &c_rdev->coupling_desc;
		__n_coupled = __c_desc->n_coupled;

		for (k = 1; k < __n_coupled; k++) {
			__c_rdev = __c_desc->coupled_rdevs[k];

			if (__c_rdev == rdev) {
				__c_desc->coupled_rdevs[k] = NULL;
				__c_desc->n_resolved--;
				break;
			}
		}

		regulator_unlock(c_rdev);

		c_desc->coupled_rdevs[i] = NULL;
		c_desc->n_resolved--;
	}

	if (coupler && coupler->detach_regulator) {
		err = coupler->detach_regulator(coupler, rdev);
		if (err)
			rdev_err(rdev, "failed to detach from coupler: %pe\n",
				 ERR_PTR(err));
	}

	kfree(rdev->coupling_desc.coupled_rdevs);
	rdev->coupling_desc.coupled_rdevs = NULL;
}

static int regulator_init_coupling(struct regulator_dev *rdev)
{
	struct regulator_dev **coupled;
	int err, n_phandles;

	if (!IS_ENABLED(CONFIG_OF))
		n_phandles = 0;
	else
		n_phandles = of_get_n_coupled(rdev);

	coupled = kcalloc(n_phandles + 1, sizeof(*coupled), GFP_KERNEL);
	if (!coupled)
		return -ENOMEM;

	rdev->coupling_desc.coupled_rdevs = coupled;

	/*
	 * Every regulator should always have coupling descriptor filled with
	 * at least pointer to itself.
	 */
	rdev->coupling_desc.coupled_rdevs[0] = rdev;
	rdev->coupling_desc.n_coupled = n_phandles + 1;
	rdev->coupling_desc.n_resolved++;

	/* regulator isn't coupled */
	if (n_phandles == 0)
		return 0;

	if (!of_check_coupling_data(rdev))
		return -EPERM;

	mutex_lock(&regulator_list_mutex);
	rdev->coupling_desc.coupler = regulator_find_coupler(rdev);
	mutex_unlock(&regulator_list_mutex);

	if (IS_ERR(rdev->coupling_desc.coupler)) {
		err = PTR_ERR(rdev->coupling_desc.coupler);
		rdev_err(rdev, "failed to get coupler: %pe\n", ERR_PTR(err));
		return err;
	}

	return 0;
}

static int generic_coupler_attach(struct regulator_coupler *coupler,
				  struct regulator_dev *rdev)
{
	if (rdev->coupling_desc.n_coupled > 2) {
		rdev_err(rdev,
			 "Voltage balancing for multiple regulator couples is unimplemented\n");
		return -EPERM;
	}

	if (!rdev->constraints->always_on) {
		rdev_err(rdev,
			 "Coupling of a non always-on regulator is unimplemented\n");
		return -ENOTSUPP;
	}

	return 0;
}

static struct regulator_coupler generic_regulator_coupler = {
	.attach_regulator = generic_coupler_attach,
};

/**
 * regulator_register - register regulator
 * @regulator_desc: regulator to register
 * @cfg: runtime configuration for regulator
 *
 * Called by regulator drivers to register a regulator.
 * Returns a valid pointer to struct regulator_dev on success
 * or an ERR_PTR() on error.
 */
struct regulator_dev *
regulator_register(const struct regulator_desc *regulator_desc,
		   const struct regulator_config *cfg)
{
	const struct regulator_init_data *init_data;
	struct regulator_config *config = NULL;
	static atomic_t regulator_no = ATOMIC_INIT(-1);
	struct regulator_dev *rdev;
	bool dangling_cfg_gpiod = false;
	bool dangling_of_gpiod = false;
	struct device *dev;
	int ret, i;
	bool resolved_early = false;

	if (cfg == NULL)
		return ERR_PTR(-EINVAL);
	if (cfg->ena_gpiod)
		dangling_cfg_gpiod = true;
	if (regulator_desc == NULL) {
		ret = -EINVAL;
		goto rinse;
	}

	dev = cfg->dev;
	WARN_ON(!dev);

	if (regulator_desc->name == NULL || regulator_desc->ops == NULL) {
		ret = -EINVAL;
		goto rinse;
	}

	if (regulator_desc->type != REGULATOR_VOLTAGE &&
	    regulator_desc->type != REGULATOR_CURRENT) {
		ret = -EINVAL;
		goto rinse;
	}

	/* Only one of each should be implemented */
	WARN_ON(regulator_desc->ops->get_voltage &&
		regulator_desc->ops->get_voltage_sel);
	WARN_ON(regulator_desc->ops->set_voltage &&
		regulator_desc->ops->set_voltage_sel);

	/* If we're using selectors we must implement list_voltage. */
	if (regulator_desc->ops->get_voltage_sel &&
	    !regulator_desc->ops->list_voltage) {
		ret = -EINVAL;
		goto rinse;
	}
	if (regulator_desc->ops->set_voltage_sel &&
	    !regulator_desc->ops->list_voltage) {
		ret = -EINVAL;
		goto rinse;
	}

	rdev = kzalloc(sizeof(struct regulator_dev), GFP_KERNEL);
	if (rdev == NULL) {
		ret = -ENOMEM;
		goto rinse;
	}
	device_initialize(&rdev->dev);
	spin_lock_init(&rdev->err_lock);

	/*
	 * Duplicate the config so the driver could override it after
	 * parsing init data.
	 */
	config = kmemdup(cfg, sizeof(*cfg), GFP_KERNEL);
	if (config == NULL) {
		ret = -ENOMEM;
		goto clean;
	}

	init_data = regulator_of_get_init_data(dev, regulator_desc, config,
					       &rdev->dev.of_node);

	/*
	 * Sometimes not all resources are probed already so we need to take
	 * that into account. This happens most the time if the ena_gpiod comes
	 * from a gpio extender or something else.
	 */
	if (PTR_ERR(init_data) == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto clean;
	}

	/*
	 * We need to keep track of any GPIO descriptor coming from the
	 * device tree until we have handled it over to the core. If the
	 * config that was passed in to this function DOES NOT contain
	 * a descriptor, and the config after this call DOES contain
	 * a descriptor, we definitely got one from parsing the device
	 * tree.
	 */
	if (!cfg->ena_gpiod && config->ena_gpiod)
		dangling_of_gpiod = true;
	if (!init_data) {
		init_data = config->init_data;
		rdev->dev.of_node = of_node_get(config->of_node);
	}

	ww_mutex_init(&rdev->mutex, &regulator_ww_class);
	rdev->reg_data = config->driver_data;
	rdev->owner = regulator_desc->owner;
	rdev->desc = regulator_desc;
	if (config->regmap)
		rdev->regmap = config->regmap;
	else if (dev_get_regmap(dev, NULL))
		rdev->regmap = dev_get_regmap(dev, NULL);
	else if (dev->parent)
		rdev->regmap = dev_get_regmap(dev->parent, NULL);
	INIT_LIST_HEAD(&rdev->consumer_list);
	INIT_LIST_HEAD(&rdev->list);
	BLOCKING_INIT_NOTIFIER_HEAD(&rdev->notifier);
	INIT_DELAYED_WORK(&rdev->disable_work, regulator_disable_work);

	if (init_data && init_data->supply_regulator)
		rdev->supply_name = init_data->supply_regulator;
	else if (regulator_desc->supply_name)
		rdev->supply_name = regulator_desc->supply_name;

	/* register with sysfs */
	rdev->dev.class = &regulator_class;
	rdev->dev.parent = dev;
	dev_set_name(&rdev->dev, "regulator.%lu",
		    (unsigned long) atomic_inc_return(&regulator_no));
	dev_set_drvdata(&rdev->dev, rdev);

	/* set regulator constraints */
	if (init_data)
		rdev->constraints = kmemdup(&init_data->constraints,
					    sizeof(*rdev->constraints),
					    GFP_KERNEL);
	else
		rdev->constraints = kzalloc(sizeof(*rdev->constraints),
					    GFP_KERNEL);
	if (!rdev->constraints) {
		ret = -ENOMEM;
		goto wash;
	}

	if ((rdev->supply_name && !rdev->supply) &&
		(rdev->constraints->always_on ||
		 rdev->constraints->boot_on)) {
		ret = regulator_resolve_supply(rdev);
		if (ret)
			rdev_dbg(rdev, "unable to resolve supply early: %pe\n",
					 ERR_PTR(ret));

		resolved_early = true;
	}

	/* perform any regulator specific init */
	if (init_data && init_data->regulator_init) {
		ret = init_data->regulator_init(rdev->reg_data);
		if (ret < 0)
			goto wash;
	}

	if (config->ena_gpiod) {
		ret = regulator_ena_gpio_request(rdev, config);
		if (ret != 0) {
			rdev_err(rdev, "Failed to request enable GPIO: %pe\n",
				 ERR_PTR(ret));
			goto wash;
		}
		/* The regulator core took over the GPIO descriptor */
		dangling_cfg_gpiod = false;
		dangling_of_gpiod = false;
	}

	ret = set_machine_constraints(rdev);
	if (ret == -EPROBE_DEFER && !resolved_early) {
		/* Regulator might be in bypass mode and so needs its supply
		 * to set the constraints
		 */
		/* FIXME: this currently triggers a chicken-and-egg problem
		 * when creating -SUPPLY symlink in sysfs to a regulator
		 * that is just being created
		 */
		rdev_dbg(rdev, "will resolve supply early: %s\n",
			 rdev->supply_name);
		ret = regulator_resolve_supply(rdev);
		if (!ret)
			ret = set_machine_constraints(rdev);
		else
			rdev_dbg(rdev, "unable to resolve supply early: %pe\n",
				 ERR_PTR(ret));
	}
	if (ret < 0)
		goto wash;

	ret = regulator_init_coupling(rdev);
	if (ret < 0)
		goto wash;

	/* add consumers devices */
	if (init_data) {
		for (i = 0; i < init_data->num_consumer_supplies; i++) {
			ret = set_consumer_device_supply(rdev,
				init_data->consumer_supplies[i].dev_name,
				init_data->consumer_supplies[i].supply);
			if (ret < 0) {
				dev_err(dev, "Failed to set supply %s\n",
					init_data->consumer_supplies[i].supply);
				goto unset_supplies;
			}
		}
	}

	if (!rdev->desc->ops->get_voltage &&
	    !rdev->desc->ops->list_voltage &&
	    !rdev->desc->fixed_uV)
		rdev->is_switch = true;

	ret = device_add(&rdev->dev);
	if (ret != 0)
		goto unset_supplies;

	rdev_init_debugfs(rdev);

	/* try to resolve regulators coupling since a new one was registered */
	mutex_lock(&regulator_list_mutex);
	regulator_resolve_coupling(rdev);
	mutex_unlock(&regulator_list_mutex);

	/* try to resolve regulators supply since a new one was registered */
	class_for_each_device(&regulator_class, NULL, NULL,
			      regulator_register_resolve_supply);
	kfree(config);
	return rdev;

unset_supplies:
	mutex_lock(&regulator_list_mutex);
	unset_regulator_supplies(rdev);
	regulator_remove_coupling(rdev);
	mutex_unlock(&regulator_list_mutex);
wash:
	kfree(rdev->coupling_desc.coupled_rdevs);
	mutex_lock(&regulator_list_mutex);
	regulator_ena_gpio_free(rdev);
	mutex_unlock(&regulator_list_mutex);
	put_device(&rdev->dev);
	rdev = NULL;
clean:
	if (dangling_of_gpiod)
		gpiod_put(config->ena_gpiod);
	if (rdev && rdev->dev.of_node)
		of_node_put(rdev->dev.of_node);
	kfree(rdev);
	kfree(config);
rinse:
	if (dangling_cfg_gpiod)
		gpiod_put(cfg->ena_gpiod);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(regulator_register);

/**
 * regulator_unregister - unregister regulator
 * @rdev: regulator to unregister
 *
 * Called by regulator drivers to unregister a regulator.
 */
void regulator_unregister(struct regulator_dev *rdev)
{
	if (rdev == NULL)
		return;

	if (rdev->supply) {
		while (rdev->use_count--)
			regulator_disable(rdev->supply);
		regulator_put(rdev->supply);
	}

	flush_work(&rdev->disable_work.work);

	mutex_lock(&regulator_list_mutex);

	WARN_ON(rdev->open_count);
	regulator_remove_coupling(rdev);
	unset_regulator_supplies(rdev);
	list_del(&rdev->list);
	regulator_ena_gpio_free(rdev);
	device_unregister(&rdev->dev);

	mutex_unlock(&regulator_list_mutex);
}
EXPORT_SYMBOL_GPL(regulator_unregister);

#ifdef CONFIG_SUSPEND
/**
 * regulator_suspend - prepare regulators for system wide suspend
 * @dev: ``&struct device`` pointer that is passed to _regulator_suspend()
 *
 * Configure each regulator with it's suspend operating parameters for state.
 */
static int regulator_suspend(struct device *dev)
{
	struct regulator_dev *rdev = dev_to_rdev(dev);
	suspend_state_t state = pm_suspend_target_state;
	int ret;
	const struct regulator_state *rstate;

	rstate = regulator_get_suspend_state_check(rdev, state);
	if (!rstate)
		return 0;

	regulator_lock(rdev);
	ret = __suspend_set_state(rdev, rstate);
	regulator_unlock(rdev);

	return ret;
}

static int regulator_resume(struct device *dev)
{
	suspend_state_t state = pm_suspend_target_state;
	struct regulator_dev *rdev = dev_to_rdev(dev);
	struct regulator_state *rstate;
	int ret = 0;

	rstate = regulator_get_suspend_state(rdev, state);
	if (rstate == NULL)
		return 0;

	/* Avoid grabbing the lock if we don't need to */
	if (!rdev->desc->ops->resume)
		return 0;

	regulator_lock(rdev);

	if (rstate->enabled == ENABLE_IN_SUSPEND ||
	    rstate->enabled == DISABLE_IN_SUSPEND)
		ret = rdev->desc->ops->resume(rdev);

	regulator_unlock(rdev);

	return ret;
}
#else /* !CONFIG_SUSPEND */

#define regulator_suspend	NULL
#define regulator_resume	NULL

#endif /* !CONFIG_SUSPEND */

#ifdef CONFIG_PM
static const struct dev_pm_ops __maybe_unused regulator_pm_ops = {
	.suspend	= regulator_suspend,
	.resume		= regulator_resume,
};
#endif

struct class regulator_class = {
	.name = "regulator",
	.dev_release = regulator_dev_release,
	.dev_groups = regulator_dev_groups,
#ifdef CONFIG_PM
	.pm = &regulator_pm_ops,
#endif
};
/**
 * regulator_has_full_constraints - the system has fully specified constraints
 *
 * Calling this function will cause the regulator API to disable all
 * regulators which have a zero use count and don't have an always_on
 * constraint in a late_initcall.
 *
 * The intention is that this will become the default behaviour in a
 * future kernel release so users are encouraged to use this facility
 * now.
 */
void regulator_has_full_constraints(void)
{
	has_full_constraints = 1;
}
EXPORT_SYMBOL_GPL(regulator_has_full_constraints);

/**
 * rdev_get_drvdata - get rdev regulator driver data
 * @rdev: regulator
 *
 * Get rdev regulator driver private data. This call can be used in the
 * regulator driver context.
 */
void *rdev_get_drvdata(struct regulator_dev *rdev)
{
	return rdev->reg_data;
}
EXPORT_SYMBOL_GPL(rdev_get_drvdata);

/**
 * regulator_get_drvdata - get regulator driver data
 * @regulator: regulator
 *
 * Get regulator driver private data. This call can be used in the consumer
 * driver context when non API regulator specific functions need to be called.
 */
void *regulator_get_drvdata(struct regulator *regulator)
{
	return regulator->rdev->reg_data;
}
EXPORT_SYMBOL_GPL(regulator_get_drvdata);

/**
 * regulator_set_drvdata - set regulator driver data
 * @regulator: regulator
 * @data: data
 */
void regulator_set_drvdata(struct regulator *regulator, void *data)
{
	regulator->rdev->reg_data = data;
}
EXPORT_SYMBOL_GPL(regulator_set_drvdata);

/**
 * rdev_get_id - get regulator ID
 * @rdev: regulator
 */
int rdev_get_id(struct regulator_dev *rdev)
{
	return rdev->desc->id;
}
EXPORT_SYMBOL_GPL(rdev_get_id);

struct device *rdev_get_dev(struct regulator_dev *rdev)
{
	return &rdev->dev;
}
EXPORT_SYMBOL_GPL(rdev_get_dev);

struct regmap *rdev_get_regmap(struct regulator_dev *rdev)
{
	return rdev->regmap;
}
EXPORT_SYMBOL_GPL(rdev_get_regmap);

void *regulator_get_init_drvdata(struct regulator_init_data *reg_init_data)
{
	return reg_init_data->driver_data;
}
EXPORT_SYMBOL_GPL(regulator_get_init_drvdata);

#ifdef CONFIG_DEBUG_FS
static int supply_map_show(struct seq_file *sf, void *data)
{
	struct regulator_map *map;

	list_for_each_entry(map, &regulator_map_list, list) {
		seq_printf(sf, "%s -> %s.%s\n",
				rdev_get_name(map->regulator), map->dev_name,
				map->supply);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(supply_map);

struct summary_data {
	struct seq_file *s;
	struct regulator_dev *parent;
	int level;
};

static void regulator_summary_show_subtree(struct seq_file *s,
					   struct regulator_dev *rdev,
					   int level);

static int regulator_summary_show_children(struct device *dev, void *data)
{
	struct regulator_dev *rdev = dev_to_rdev(dev);
	struct summary_data *summary_data = data;

	if (rdev->supply && rdev->supply->rdev == summary_data->parent)
		regulator_summary_show_subtree(summary_data->s, rdev,
					       summary_data->level + 1);

	return 0;
}

static void regulator_summary_show_subtree(struct seq_file *s,
					   struct regulator_dev *rdev,
					   int level)
{
	struct regulation_constraints *c;
	struct regulator *consumer;
	struct summary_data summary_data;
	unsigned int opmode;

	if (!rdev)
		return;

	opmode = _regulator_get_mode_unlocked(rdev);
	seq_printf(s, "%*s%-*s %3d %4d %6d %7s ",
		   level * 3 + 1, "",
		   30 - level * 3, rdev_get_name(rdev),
		   rdev->use_count, rdev->open_count, rdev->bypass_count,
		   regulator_opmode_to_str(opmode));

	seq_printf(s, "%5dmV ", regulator_get_voltage_rdev(rdev) / 1000);
	seq_printf(s, "%5dmA ",
		   _regulator_get_current_limit_unlocked(rdev) / 1000);

	c = rdev->constraints;
	if (c) {
		switch (rdev->desc->type) {
		case REGULATOR_VOLTAGE:
			seq_printf(s, "%5dmV %5dmV ",
				   c->min_uV / 1000, c->max_uV / 1000);
			break;
		case REGULATOR_CURRENT:
			seq_printf(s, "%5dmA %5dmA ",
				   c->min_uA / 1000, c->max_uA / 1000);
			break;
		}
	}

	seq_puts(s, "\n");

	list_for_each_entry(consumer, &rdev->consumer_list, list) {
		if (consumer->dev && consumer->dev->class == &regulator_class)
			continue;

		seq_printf(s, "%*s%-*s ",
			   (level + 1) * 3 + 1, "",
			   30 - (level + 1) * 3,
			   consumer->supply_name ? consumer->supply_name :
			   consumer->dev ? dev_name(consumer->dev) : "deviceless");

		switch (rdev->desc->type) {
		case REGULATOR_VOLTAGE:
			seq_printf(s, "%3d %33dmA%c%5dmV %5dmV",
				   consumer->enable_count,
				   consumer->uA_load / 1000,
				   consumer->uA_load && !consumer->enable_count ?
				   '*' : ' ',
				   consumer->voltage[PM_SUSPEND_ON].min_uV / 1000,
				   consumer->voltage[PM_SUSPEND_ON].max_uV / 1000);
			break;
		case REGULATOR_CURRENT:
			break;
		}

		seq_puts(s, "\n");
	}

	summary_data.s = s;
	summary_data.level = level;
	summary_data.parent = rdev;

	class_for_each_device(&regulator_class, NULL, &summary_data,
			      regulator_summary_show_children);
}

struct summary_lock_data {
	struct ww_acquire_ctx *ww_ctx;
	struct regulator_dev **new_contended_rdev;
	struct regulator_dev **old_contended_rdev;
};

static int regulator_summary_lock_one(struct device *dev, void *data)
{
	struct regulator_dev *rdev = dev_to_rdev(dev);
	struct summary_lock_data *lock_data = data;
	int ret = 0;

	if (rdev != *lock_data->old_contended_rdev) {
		ret = regulator_lock_nested(rdev, lock_data->ww_ctx);

		if (ret == -EDEADLK)
			*lock_data->new_contended_rdev = rdev;
		else
			WARN_ON_ONCE(ret);
	} else {
		*lock_data->old_contended_rdev = NULL;
	}

	return ret;
}

static int regulator_summary_unlock_one(struct device *dev, void *data)
{
	struct regulator_dev *rdev = dev_to_rdev(dev);
	struct summary_lock_data *lock_data = data;

	if (lock_data) {
		if (rdev == *lock_data->new_contended_rdev)
			return -EDEADLK;
	}

	regulator_unlock(rdev);

	return 0;
}

static int regulator_summary_lock_all(struct ww_acquire_ctx *ww_ctx,
				      struct regulator_dev **new_contended_rdev,
				      struct regulator_dev **old_contended_rdev)
{
	struct summary_lock_data lock_data;
	int ret;

	lock_data.ww_ctx = ww_ctx;
	lock_data.new_contended_rdev = new_contended_rdev;
	lock_data.old_contended_rdev = old_contended_rdev;

	ret = class_for_each_device(&regulator_class, NULL, &lock_data,
				    regulator_summary_lock_one);
	if (ret)
		class_for_each_device(&regulator_class, NULL, &lock_data,
				      regulator_summary_unlock_one);

	return ret;
}

static void regulator_summary_lock(struct ww_acquire_ctx *ww_ctx)
{
	struct regulator_dev *new_contended_rdev = NULL;
	struct regulator_dev *old_contended_rdev = NULL;
	int err;

	mutex_lock(&regulator_list_mutex);

	ww_acquire_init(ww_ctx, &regulator_ww_class);

	do {
		if (new_contended_rdev) {
			ww_mutex_lock_slow(&new_contended_rdev->mutex, ww_ctx);
			old_contended_rdev = new_contended_rdev;
			old_contended_rdev->ref_cnt++;
		}

		err = regulator_summary_lock_all(ww_ctx,
						 &new_contended_rdev,
						 &old_contended_rdev);

		if (old_contended_rdev)
			regulator_unlock(old_contended_rdev);

	} while (err == -EDEADLK);

	ww_acquire_done(ww_ctx);
}

static void regulator_summary_unlock(struct ww_acquire_ctx *ww_ctx)
{
	class_for_each_device(&regulator_class, NULL, NULL,
			      regulator_summary_unlock_one);
	ww_acquire_fini(ww_ctx);

	mutex_unlock(&regulator_list_mutex);
}

static int regulator_summary_show_roots(struct device *dev, void *data)
{
	struct regulator_dev *rdev = dev_to_rdev(dev);
	struct seq_file *s = data;

	if (!rdev->supply)
		regulator_summary_show_subtree(s, rdev, 0);

	return 0;
}

static int regulator_summary_show(struct seq_file *s, void *data)
{
	struct ww_acquire_ctx ww_ctx;

	seq_puts(s, " regulator                      use open bypass  opmode voltage current     min     max\n");
	seq_puts(s, "---------------------------------------------------------------------------------------\n");

	regulator_summary_lock(&ww_ctx);

	class_for_each_device(&regulator_class, NULL, s,
			      regulator_summary_show_roots);

	regulator_summary_unlock(&ww_ctx);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(regulator_summary);
#endif /* CONFIG_DEBUG_FS */

static int __init regulator_init(void)
{
	int ret;

	ret = class_register(&regulator_class);

	debugfs_root = debugfs_create_dir("regulator", NULL);
	if (!debugfs_root)
		pr_warn("regulator: Failed to create debugfs directory\n");

#ifdef CONFIG_DEBUG_FS
	debugfs_create_file("supply_map", 0444, debugfs_root, NULL,
			    &supply_map_fops);

	debugfs_create_file("regulator_summary", 0444, debugfs_root,
			    NULL, &regulator_summary_fops);
#endif
	regulator_dummy_init();

	regulator_coupler_register(&generic_regulator_coupler);

	return ret;
}

/* init early to allow our consumers to complete system booting */
core_initcall(regulator_init);

static int regulator_late_cleanup(struct device *dev, void *data)
{
	struct regulator_dev *rdev = dev_to_rdev(dev);
	struct regulation_constraints *c = rdev->constraints;
	int ret;

	if (c && c->always_on)
		return 0;

	if (!regulator_ops_is_valid(rdev, REGULATOR_CHANGE_STATUS))
		return 0;

	regulator_lock(rdev);

	if (rdev->use_count)
		goto unlock;

	/* If reading the status failed, assume that it's off. */
	if (_regulator_is_enabled(rdev) <= 0)
		goto unlock;

	if (have_full_constraints()) {
		/* We log since this may kill the system if it goes
		 * wrong.
		 */
		rdev_info(rdev, "disabling\n");
		ret = _regulator_do_disable(rdev);
		if (ret != 0)
			rdev_err(rdev, "couldn't disable: %pe\n", ERR_PTR(ret));
	} else {
		/* The intention is that in future we will
		 * assume that full constraints are provided
		 * so warn even if we aren't going to do
		 * anything here.
		 */
		rdev_warn(rdev, "incomplete constraints, leaving on\n");
	}

unlock:
	regulator_unlock(rdev);

	return 0;
}

static void regulator_init_complete_work_function(struct work_struct *work)
{
	/*
	 * Regulators may had failed to resolve their input supplies
	 * when were registered, either because the input supply was
	 * not registered yet or because its parent device was not
	 * bound yet. So attempt to resolve the input supplies for
	 * pending regulators before trying to disable unused ones.
	 */
	class_for_each_device(&regulator_class, NULL, NULL,
			      regulator_register_resolve_supply);

	/* If we have a full configuration then disable any regulators
	 * we have permission to change the status for and which are
	 * not in use or always_on.  This is effectively the default
	 * for DT and ACPI as they have full constraints.
	 */
	class_for_each_device(&regulator_class, NULL, NULL,
			      regulator_late_cleanup);
}

static DECLARE_DELAYED_WORK(regulator_init_complete_work,
			    regulator_init_complete_work_function);

static int __init regulator_init_complete(void)
{
	/*
	 * Since DT doesn't provide an idiomatic mechanism for
	 * enabling full constraints and since it's much more natural
	 * with DT to provide them just assume that a DT enabled
	 * system has full constraints.
	 */
	if (of_have_populated_dt())
		has_full_constraints = true;

	/*
	 * We punt completion for an arbitrary amount of time since
	 * systems like distros will load many drivers from userspace
	 * so consumers might not always be ready yet, this is
	 * particularly an issue with laptops where this might bounce
	 * the display off then on.  Ideally we'd get a notification
	 * from userspace when this happens but we don't so just wait
	 * a bit and hope we waited long enough.  It'd be better if
	 * we'd only do this on systems that need it, and a kernel
	 * command line option might be useful.
	 */
	schedule_delayed_work(&regulator_init_complete_work,
			      msecs_to_jiffies(30000));

	return 0;
}
late_initcall_sync(regulator_init_complete);
