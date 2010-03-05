/*
 * core.c  --  Voltage/Current Regulator framework.
 *
 * Copyright 2007, 2008 Wolfson Microelectronics PLC.
 * Copyright 2008 SlimLogic Ltd.
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include "dummy.h"

#define REGULATOR_VERSION "0.5"

static DEFINE_MUTEX(regulator_list_mutex);
static LIST_HEAD(regulator_list);
static LIST_HEAD(regulator_map_list);
static int has_full_constraints;

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
 * struct regulator
 *
 * One for each consumer device.
 */
struct regulator {
	struct device *dev;
	struct list_head list;
	int uA_load;
	int min_uV;
	int max_uV;
	char *supply_name;
	struct device_attribute dev_attr;
	struct regulator_dev *rdev;
};

static int _regulator_is_enabled(struct regulator_dev *rdev);
static int _regulator_disable(struct regulator_dev *rdev);
static int _regulator_get_voltage(struct regulator_dev *rdev);
static int _regulator_get_current_limit(struct regulator_dev *rdev);
static unsigned int _regulator_get_mode(struct regulator_dev *rdev);
static void _notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data);

static const char *rdev_get_name(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->name)
		return rdev->constraints->name;
	else if (rdev->desc->name)
		return rdev->desc->name;
	else
		return "";
}

/* gets the regulator for a given consumer device */
static struct regulator *get_device_regulator(struct device *dev)
{
	struct regulator *regulator = NULL;
	struct regulator_dev *rdev;

	mutex_lock(&regulator_list_mutex);
	list_for_each_entry(rdev, &regulator_list, list) {
		mutex_lock(&rdev->mutex);
		list_for_each_entry(regulator, &rdev->consumer_list, list) {
			if (regulator->dev == dev) {
				mutex_unlock(&rdev->mutex);
				mutex_unlock(&regulator_list_mutex);
				return regulator;
			}
		}
		mutex_unlock(&rdev->mutex);
	}
	mutex_unlock(&regulator_list_mutex);
	return NULL;
}

/* Platform voltage constraint check */
static int regulator_check_voltage(struct regulator_dev *rdev,
				   int *min_uV, int *max_uV)
{
	BUG_ON(*min_uV > *max_uV);

	if (!rdev->constraints) {
		printk(KERN_ERR "%s: no constraints for %s\n", __func__,
		       rdev_get_name(rdev));
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_VOLTAGE)) {
		printk(KERN_ERR "%s: operation not allowed for %s\n",
		       __func__, rdev_get_name(rdev));
		return -EPERM;
	}

	if (*max_uV > rdev->constraints->max_uV)
		*max_uV = rdev->constraints->max_uV;
	if (*min_uV < rdev->constraints->min_uV)
		*min_uV = rdev->constraints->min_uV;

	if (*min_uV > *max_uV)
		return -EINVAL;

	return 0;
}

/* current constraint check */
static int regulator_check_current_limit(struct regulator_dev *rdev,
					int *min_uA, int *max_uA)
{
	BUG_ON(*min_uA > *max_uA);

	if (!rdev->constraints) {
		printk(KERN_ERR "%s: no constraints for %s\n", __func__,
		       rdev_get_name(rdev));
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_CURRENT)) {
		printk(KERN_ERR "%s: operation not allowed for %s\n",
		       __func__, rdev_get_name(rdev));
		return -EPERM;
	}

	if (*max_uA > rdev->constraints->max_uA)
		*max_uA = rdev->constraints->max_uA;
	if (*min_uA < rdev->constraints->min_uA)
		*min_uA = rdev->constraints->min_uA;

	if (*min_uA > *max_uA)
		return -EINVAL;

	return 0;
}

/* operating mode constraint check */
static int regulator_check_mode(struct regulator_dev *rdev, int mode)
{
	switch (mode) {
	case REGULATOR_MODE_FAST:
	case REGULATOR_MODE_NORMAL:
	case REGULATOR_MODE_IDLE:
	case REGULATOR_MODE_STANDBY:
		break;
	default:
		return -EINVAL;
	}

	if (!rdev->constraints) {
		printk(KERN_ERR "%s: no constraints for %s\n", __func__,
		       rdev_get_name(rdev));
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_MODE)) {
		printk(KERN_ERR "%s: operation not allowed for %s\n",
		       __func__, rdev_get_name(rdev));
		return -EPERM;
	}
	if (!(rdev->constraints->valid_modes_mask & mode)) {
		printk(KERN_ERR "%s: invalid mode %x for %s\n",
		       __func__, mode, rdev_get_name(rdev));
		return -EINVAL;
	}
	return 0;
}

/* dynamic regulator mode switching constraint check */
static int regulator_check_drms(struct regulator_dev *rdev)
{
	if (!rdev->constraints) {
		printk(KERN_ERR "%s: no constraints for %s\n", __func__,
		       rdev_get_name(rdev));
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_DRMS)) {
		printk(KERN_ERR "%s: operation not allowed for %s\n",
		       __func__, rdev_get_name(rdev));
		return -EPERM;
	}
	return 0;
}

static ssize_t device_requested_uA_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct regulator *regulator;

	regulator = get_device_regulator(dev);
	if (regulator == NULL)
		return 0;

	return sprintf(buf, "%d\n", regulator->uA_load);
}

static ssize_t regulator_uV_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&rdev->mutex);
	ret = sprintf(buf, "%d\n", _regulator_get_voltage(rdev));
	mutex_unlock(&rdev->mutex);

	return ret;
}
static DEVICE_ATTR(microvolts, 0444, regulator_uV_show, NULL);

static ssize_t regulator_uA_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", _regulator_get_current_limit(rdev));
}
static DEVICE_ATTR(microamps, 0444, regulator_uA_show, NULL);

static ssize_t regulator_name_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", rdev_get_name(rdev));
}

static ssize_t regulator_print_opmode(char *buf, int mode)
{
	switch (mode) {
	case REGULATOR_MODE_FAST:
		return sprintf(buf, "fast\n");
	case REGULATOR_MODE_NORMAL:
		return sprintf(buf, "normal\n");
	case REGULATOR_MODE_IDLE:
		return sprintf(buf, "idle\n");
	case REGULATOR_MODE_STANDBY:
		return sprintf(buf, "standby\n");
	}
	return sprintf(buf, "unknown\n");
}

static ssize_t regulator_opmode_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf, _regulator_get_mode(rdev));
}
static DEVICE_ATTR(opmode, 0444, regulator_opmode_show, NULL);

static ssize_t regulator_print_state(char *buf, int state)
{
	if (state > 0)
		return sprintf(buf, "enabled\n");
	else if (state == 0)
		return sprintf(buf, "disabled\n");
	else
		return sprintf(buf, "unknown\n");
}

static ssize_t regulator_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&rdev->mutex);
	ret = regulator_print_state(buf, _regulator_is_enabled(rdev));
	mutex_unlock(&rdev->mutex);

	return ret;
}
static DEVICE_ATTR(state, 0444, regulator_state_show, NULL);

static ssize_t regulator_status_show(struct device *dev,
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
	default:
		return -ERANGE;
	}

	return sprintf(buf, "%s\n", label);
}
static DEVICE_ATTR(status, 0444, regulator_status_show, NULL);

static ssize_t regulator_min_uA_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->min_uA);
}
static DEVICE_ATTR(min_microamps, 0444, regulator_min_uA_show, NULL);

static ssize_t regulator_max_uA_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->max_uA);
}
static DEVICE_ATTR(max_microamps, 0444, regulator_max_uA_show, NULL);

static ssize_t regulator_min_uV_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->min_uV);
}
static DEVICE_ATTR(min_microvolts, 0444, regulator_min_uV_show, NULL);

static ssize_t regulator_max_uV_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	if (!rdev->constraints)
		return sprintf(buf, "constraint not defined\n");

	return sprintf(buf, "%d\n", rdev->constraints->max_uV);
}
static DEVICE_ATTR(max_microvolts, 0444, regulator_max_uV_show, NULL);

static ssize_t regulator_total_uA_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	struct regulator *regulator;
	int uA = 0;

	mutex_lock(&rdev->mutex);
	list_for_each_entry(regulator, &rdev->consumer_list, list)
		uA += regulator->uA_load;
	mutex_unlock(&rdev->mutex);
	return sprintf(buf, "%d\n", uA);
}
static DEVICE_ATTR(requested_microamps, 0444, regulator_total_uA_show, NULL);

static ssize_t regulator_num_users_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", rdev->use_count);
}

static ssize_t regulator_type_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
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

static ssize_t regulator_suspend_mem_uV_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", rdev->constraints->state_mem.uV);
}
static DEVICE_ATTR(suspend_mem_microvolts, 0444,
		regulator_suspend_mem_uV_show, NULL);

static ssize_t regulator_suspend_disk_uV_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", rdev->constraints->state_disk.uV);
}
static DEVICE_ATTR(suspend_disk_microvolts, 0444,
		regulator_suspend_disk_uV_show, NULL);

static ssize_t regulator_suspend_standby_uV_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", rdev->constraints->state_standby.uV);
}
static DEVICE_ATTR(suspend_standby_microvolts, 0444,
		regulator_suspend_standby_uV_show, NULL);

static ssize_t regulator_suspend_mem_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf,
		rdev->constraints->state_mem.mode);
}
static DEVICE_ATTR(suspend_mem_mode, 0444,
		regulator_suspend_mem_mode_show, NULL);

static ssize_t regulator_suspend_disk_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf,
		rdev->constraints->state_disk.mode);
}
static DEVICE_ATTR(suspend_disk_mode, 0444,
		regulator_suspend_disk_mode_show, NULL);

static ssize_t regulator_suspend_standby_mode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_opmode(buf,
		rdev->constraints->state_standby.mode);
}
static DEVICE_ATTR(suspend_standby_mode, 0444,
		regulator_suspend_standby_mode_show, NULL);

static ssize_t regulator_suspend_mem_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_state(buf,
			rdev->constraints->state_mem.enabled);
}
static DEVICE_ATTR(suspend_mem_state, 0444,
		regulator_suspend_mem_state_show, NULL);

static ssize_t regulator_suspend_disk_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_state(buf,
			rdev->constraints->state_disk.enabled);
}
static DEVICE_ATTR(suspend_disk_state, 0444,
		regulator_suspend_disk_state_show, NULL);

static ssize_t regulator_suspend_standby_state_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	return regulator_print_state(buf,
			rdev->constraints->state_standby.enabled);
}
static DEVICE_ATTR(suspend_standby_state, 0444,
		regulator_suspend_standby_state_show, NULL);


/*
 * These are the only attributes are present for all regulators.
 * Other attributes are a function of regulator functionality.
 */
static struct device_attribute regulator_dev_attrs[] = {
	__ATTR(name, 0444, regulator_name_show, NULL),
	__ATTR(num_users, 0444, regulator_num_users_show, NULL),
	__ATTR(type, 0444, regulator_type_show, NULL),
	__ATTR_NULL,
};

static void regulator_dev_release(struct device *dev)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	kfree(rdev);
}

static struct class regulator_class = {
	.name = "regulator",
	.dev_release = regulator_dev_release,
	.dev_attrs = regulator_dev_attrs,
};

/* Calculate the new optimum regulator operating mode based on the new total
 * consumer load. All locks held by caller */
static void drms_uA_update(struct regulator_dev *rdev)
{
	struct regulator *sibling;
	int current_uA = 0, output_uV, input_uV, err;
	unsigned int mode;

	err = regulator_check_drms(rdev);
	if (err < 0 || !rdev->desc->ops->get_optimum_mode ||
	    !rdev->desc->ops->get_voltage || !rdev->desc->ops->set_mode)
		return;

	/* get output voltage */
	output_uV = rdev->desc->ops->get_voltage(rdev);
	if (output_uV <= 0)
		return;

	/* get input voltage */
	if (rdev->supply && rdev->supply->desc->ops->get_voltage)
		input_uV = rdev->supply->desc->ops->get_voltage(rdev->supply);
	else
		input_uV = rdev->constraints->input_uV;
	if (input_uV <= 0)
		return;

	/* calc total requested load */
	list_for_each_entry(sibling, &rdev->consumer_list, list)
		current_uA += sibling->uA_load;

	/* now get the optimum mode for our new total regulator load */
	mode = rdev->desc->ops->get_optimum_mode(rdev, input_uV,
						  output_uV, current_uA);

	/* check the new mode is allowed */
	err = regulator_check_mode(rdev, mode);
	if (err == 0)
		rdev->desc->ops->set_mode(rdev, mode);
}

static int suspend_set_state(struct regulator_dev *rdev,
	struct regulator_state *rstate)
{
	int ret = 0;
	bool can_set_state;

	can_set_state = rdev->desc->ops->set_suspend_enable &&
		rdev->desc->ops->set_suspend_disable;

	/* If we have no suspend mode configration don't set anything;
	 * only warn if the driver actually makes the suspend mode
	 * configurable.
	 */
	if (!rstate->enabled && !rstate->disabled) {
		if (can_set_state)
			printk(KERN_WARNING "%s: No configuration for %s\n",
			       __func__, rdev_get_name(rdev));
		return 0;
	}

	if (rstate->enabled && rstate->disabled) {
		printk(KERN_ERR "%s: invalid configuration for %s\n",
		       __func__, rdev_get_name(rdev));
		return -EINVAL;
	}

	if (!can_set_state) {
		printk(KERN_ERR "%s: no way to set suspend state\n",
			__func__);
		return -EINVAL;
	}

	if (rstate->enabled)
		ret = rdev->desc->ops->set_suspend_enable(rdev);
	else
		ret = rdev->desc->ops->set_suspend_disable(rdev);
	if (ret < 0) {
		printk(KERN_ERR "%s: failed to enabled/disable\n", __func__);
		return ret;
	}

	if (rdev->desc->ops->set_suspend_voltage && rstate->uV > 0) {
		ret = rdev->desc->ops->set_suspend_voltage(rdev, rstate->uV);
		if (ret < 0) {
			printk(KERN_ERR "%s: failed to set voltage\n",
				__func__);
			return ret;
		}
	}

	if (rdev->desc->ops->set_suspend_mode && rstate->mode > 0) {
		ret = rdev->desc->ops->set_suspend_mode(rdev, rstate->mode);
		if (ret < 0) {
			printk(KERN_ERR "%s: failed to set mode\n", __func__);
			return ret;
		}
	}
	return ret;
}

/* locks held by caller */
static int suspend_prepare(struct regulator_dev *rdev, suspend_state_t state)
{
	if (!rdev->constraints)
		return -EINVAL;

	switch (state) {
	case PM_SUSPEND_STANDBY:
		return suspend_set_state(rdev,
			&rdev->constraints->state_standby);
	case PM_SUSPEND_MEM:
		return suspend_set_state(rdev,
			&rdev->constraints->state_mem);
	case PM_SUSPEND_MAX:
		return suspend_set_state(rdev,
			&rdev->constraints->state_disk);
	default:
		return -EINVAL;
	}
}

static void print_constraints(struct regulator_dev *rdev)
{
	struct regulation_constraints *constraints = rdev->constraints;
	char buf[80] = "";
	int count = 0;
	int ret;

	if (constraints->min_uV && constraints->max_uV) {
		if (constraints->min_uV == constraints->max_uV)
			count += sprintf(buf + count, "%d mV ",
					 constraints->min_uV / 1000);
		else
			count += sprintf(buf + count, "%d <--> %d mV ",
					 constraints->min_uV / 1000,
					 constraints->max_uV / 1000);
	}

	if (!constraints->min_uV ||
	    constraints->min_uV != constraints->max_uV) {
		ret = _regulator_get_voltage(rdev);
		if (ret > 0)
			count += sprintf(buf + count, "at %d mV ", ret / 1000);
	}

	if (constraints->min_uA && constraints->max_uA) {
		if (constraints->min_uA == constraints->max_uA)
			count += sprintf(buf + count, "%d mA ",
					 constraints->min_uA / 1000);
		else
			count += sprintf(buf + count, "%d <--> %d mA ",
					 constraints->min_uA / 1000,
					 constraints->max_uA / 1000);
	}

	if (!constraints->min_uA ||
	    constraints->min_uA != constraints->max_uA) {
		ret = _regulator_get_current_limit(rdev);
		if (ret > 0)
			count += sprintf(buf + count, "at %d uA ", ret / 1000);
	}

	if (constraints->valid_modes_mask & REGULATOR_MODE_FAST)
		count += sprintf(buf + count, "fast ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_NORMAL)
		count += sprintf(buf + count, "normal ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_IDLE)
		count += sprintf(buf + count, "idle ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_STANDBY)
		count += sprintf(buf + count, "standby");

	printk(KERN_INFO "regulator: %s: %s\n", rdev_get_name(rdev), buf);
}

static int machine_constraints_voltage(struct regulator_dev *rdev,
	struct regulation_constraints *constraints)
{
	struct regulator_ops *ops = rdev->desc->ops;
	const char *name = rdev_get_name(rdev);
	int ret;

	/* do we need to apply the constraint voltage */
	if (rdev->constraints->apply_uV &&
		rdev->constraints->min_uV == rdev->constraints->max_uV &&
		ops->set_voltage) {
		ret = ops->set_voltage(rdev,
			rdev->constraints->min_uV, rdev->constraints->max_uV);
			if (ret < 0) {
				printk(KERN_ERR "%s: failed to apply %duV constraint to %s\n",
				       __func__,
				       rdev->constraints->min_uV, name);
				rdev->constraints = NULL;
				return ret;
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
		   and the constraints are used by list_voltage. */
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
			pr_err("%s: %s '%s' voltage constraints\n",
				       __func__, "invalid", name);
			return -EINVAL;
		}

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
			pr_err("%s: %s '%s' voltage constraints\n",
				       __func__, "unsupportable", name);
			return -EINVAL;
		}

		/* use regulator's subset of machine constraints */
		if (constraints->min_uV < min_uV) {
			pr_debug("%s: override '%s' %s, %d -> %d\n",
				       __func__, name, "min_uV",
					constraints->min_uV, min_uV);
			constraints->min_uV = min_uV;
		}
		if (constraints->max_uV > max_uV) {
			pr_debug("%s: override '%s' %s, %d -> %d\n",
				       __func__, name, "max_uV",
					constraints->max_uV, max_uV);
			constraints->max_uV = max_uV;
		}
	}

	return 0;
}

/**
 * set_machine_constraints - sets regulator constraints
 * @rdev: regulator source
 * @constraints: constraints to apply
 *
 * Allows platform initialisation code to define and constrain
 * regulator circuits e.g. valid voltage/current ranges, etc.  NOTE:
 * Constraints *must* be set by platform code in order for some
 * regulator operations to proceed i.e. set_voltage, set_current_limit,
 * set_mode.
 */
static int set_machine_constraints(struct regulator_dev *rdev,
	struct regulation_constraints *constraints)
{
	int ret = 0;
	const char *name;
	struct regulator_ops *ops = rdev->desc->ops;

	rdev->constraints = constraints;

	name = rdev_get_name(rdev);

	ret = machine_constraints_voltage(rdev, constraints);
	if (ret != 0)
		goto out;

	/* do we need to setup our suspend state */
	if (constraints->initial_state) {
		ret = suspend_prepare(rdev, constraints->initial_state);
		if (ret < 0) {
			printk(KERN_ERR "%s: failed to set suspend state for %s\n",
			       __func__, name);
			rdev->constraints = NULL;
			goto out;
		}
	}

	if (constraints->initial_mode) {
		if (!ops->set_mode) {
			printk(KERN_ERR "%s: no set_mode operation for %s\n",
			       __func__, name);
			ret = -EINVAL;
			goto out;
		}

		ret = ops->set_mode(rdev, constraints->initial_mode);
		if (ret < 0) {
			printk(KERN_ERR
			       "%s: failed to set initial mode for %s: %d\n",
			       __func__, name, ret);
			goto out;
		}
	}

	/* If the constraints say the regulator should be on at this point
	 * and we have control then make sure it is enabled.
	 */
	if ((constraints->always_on || constraints->boot_on) && ops->enable) {
		ret = ops->enable(rdev);
		if (ret < 0) {
			printk(KERN_ERR "%s: failed to enable %s\n",
			       __func__, name);
			rdev->constraints = NULL;
			goto out;
		}
	}

	print_constraints(rdev);
out:
	return ret;
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

	err = sysfs_create_link(&rdev->dev.kobj, &supply_rdev->dev.kobj,
				"supply");
	if (err) {
		printk(KERN_ERR
		       "%s: could not add device link %s err %d\n",
		       __func__, supply_rdev->dev.kobj.name, err);
		       goto out;
	}
	rdev->supply = supply_rdev;
	list_add(&rdev->slist, &supply_rdev->supply_list);
out:
	return err;
}

/**
 * set_consumer_device_supply: Bind a regulator to a symbolic supply
 * @rdev:         regulator source
 * @consumer_dev: device the supply applies to
 * @consumer_dev_name: dev_name() string for device supply applies to
 * @supply:       symbolic name for supply
 *
 * Allows platform initialisation code to map physical regulator
 * sources to symbolic names for supplies for use by devices.  Devices
 * should use these symbolic names to request regulators, avoiding the
 * need to provide board-specific regulator names as platform data.
 *
 * Only one of consumer_dev and consumer_dev_name may be specified.
 */
static int set_consumer_device_supply(struct regulator_dev *rdev,
	struct device *consumer_dev, const char *consumer_dev_name,
	const char *supply)
{
	struct regulator_map *node;
	int has_dev;

	if (consumer_dev && consumer_dev_name)
		return -EINVAL;

	if (!consumer_dev_name && consumer_dev)
		consumer_dev_name = dev_name(consumer_dev);

	if (supply == NULL)
		return -EINVAL;

	if (consumer_dev_name != NULL)
		has_dev = 1;
	else
		has_dev = 0;

	list_for_each_entry(node, &regulator_map_list, list) {
		if (consumer_dev_name != node->dev_name)
			continue;
		if (strcmp(node->supply, supply) != 0)
			continue;

		dev_dbg(consumer_dev, "%s/%s is '%s' supply; fail %s/%s\n",
				dev_name(&node->regulator->dev),
				node->regulator->desc->name,
				supply,
				dev_name(&rdev->dev), rdev_get_name(rdev));
		return -EBUSY;
	}

	node = kzalloc(sizeof(struct regulator_map), GFP_KERNEL);
	if (node == NULL)
		return -ENOMEM;

	node->regulator = rdev;
	node->supply = supply;

	if (has_dev) {
		node->dev_name = kstrdup(consumer_dev_name, GFP_KERNEL);
		if (node->dev_name == NULL) {
			kfree(node);
			return -ENOMEM;
		}
	}

	list_add(&node->list, &regulator_map_list);
	return 0;
}

static void unset_consumer_device_supply(struct regulator_dev *rdev,
	const char *consumer_dev_name, struct device *consumer_dev)
{
	struct regulator_map *node, *n;

	if (consumer_dev && !consumer_dev_name)
		consumer_dev_name = dev_name(consumer_dev);

	list_for_each_entry_safe(node, n, &regulator_map_list, list) {
		if (rdev != node->regulator)
			continue;

		if (consumer_dev_name && node->dev_name &&
		    strcmp(consumer_dev_name, node->dev_name))
			continue;

		list_del(&node->list);
		kfree(node->dev_name);
		kfree(node);
		return;
	}
}

static void unset_regulator_supplies(struct regulator_dev *rdev)
{
	struct regulator_map *node, *n;

	list_for_each_entry_safe(node, n, &regulator_map_list, list) {
		if (rdev == node->regulator) {
			list_del(&node->list);
			kfree(node->dev_name);
			kfree(node);
			return;
		}
	}
}

#define REG_STR_SIZE	32

static struct regulator *create_regulator(struct regulator_dev *rdev,
					  struct device *dev,
					  const char *supply_name)
{
	struct regulator *regulator;
	char buf[REG_STR_SIZE];
	int err, size;

	regulator = kzalloc(sizeof(*regulator), GFP_KERNEL);
	if (regulator == NULL)
		return NULL;

	mutex_lock(&rdev->mutex);
	regulator->rdev = rdev;
	list_add(&regulator->list, &rdev->consumer_list);

	if (dev) {
		/* create a 'requested_microamps_name' sysfs entry */
		size = scnprintf(buf, REG_STR_SIZE, "microamps_requested_%s",
			supply_name);
		if (size >= REG_STR_SIZE)
			goto overflow_err;

		regulator->dev = dev;
		regulator->dev_attr.attr.name = kstrdup(buf, GFP_KERNEL);
		if (regulator->dev_attr.attr.name == NULL)
			goto attr_name_err;

		regulator->dev_attr.attr.owner = THIS_MODULE;
		regulator->dev_attr.attr.mode = 0444;
		regulator->dev_attr.show = device_requested_uA_show;
		err = device_create_file(dev, &regulator->dev_attr);
		if (err < 0) {
			printk(KERN_WARNING "%s: could not add regulator_dev"
				" load sysfs\n", __func__);
			goto attr_name_err;
		}

		/* also add a link to the device sysfs entry */
		size = scnprintf(buf, REG_STR_SIZE, "%s-%s",
				 dev->kobj.name, supply_name);
		if (size >= REG_STR_SIZE)
			goto attr_err;

		regulator->supply_name = kstrdup(buf, GFP_KERNEL);
		if (regulator->supply_name == NULL)
			goto attr_err;

		err = sysfs_create_link(&rdev->dev.kobj, &dev->kobj,
					buf);
		if (err) {
			printk(KERN_WARNING
			       "%s: could not add device link %s err %d\n",
			       __func__, dev->kobj.name, err);
			device_remove_file(dev, &regulator->dev_attr);
			goto link_name_err;
		}
	}
	mutex_unlock(&rdev->mutex);
	return regulator;
link_name_err:
	kfree(regulator->supply_name);
attr_err:
	device_remove_file(regulator->dev, &regulator->dev_attr);
attr_name_err:
	kfree(regulator->dev_attr.attr.name);
overflow_err:
	list_del(&regulator->list);
	kfree(regulator);
	mutex_unlock(&rdev->mutex);
	return NULL;
}

static int _regulator_get_enable_time(struct regulator_dev *rdev)
{
	if (!rdev->desc->ops->enable_time)
		return 0;
	return rdev->desc->ops->enable_time(rdev);
}

/* Internal regulator request function */
static struct regulator *_regulator_get(struct device *dev, const char *id,
					int exclusive)
{
	struct regulator_dev *rdev;
	struct regulator_map *map;
	struct regulator *regulator = ERR_PTR(-ENODEV);
	const char *devname = NULL;
	int ret;

	if (id == NULL) {
		printk(KERN_ERR "regulator: get() with no identifier\n");
		return regulator;
	}

	if (dev)
		devname = dev_name(dev);

	mutex_lock(&regulator_list_mutex);

	list_for_each_entry(map, &regulator_map_list, list) {
		/* If the mapping has a device set up it must match */
		if (map->dev_name &&
		    (!devname || strcmp(map->dev_name, devname)))
			continue;

		if (strcmp(map->supply, id) == 0) {
			rdev = map->regulator;
			goto found;
		}
	}

#ifdef CONFIG_REGULATOR_DUMMY
	if (!devname)
		devname = "deviceless";

	/* If the board didn't flag that it was fully constrained then
	 * substitute in a dummy regulator so consumers can continue.
	 */
	if (!has_full_constraints) {
		pr_warning("%s supply %s not found, using dummy regulator\n",
			   devname, id);
		rdev = dummy_regulator_rdev;
		goto found;
	}
#endif

	mutex_unlock(&regulator_list_mutex);
	return regulator;

found:
	if (rdev->exclusive) {
		regulator = ERR_PTR(-EPERM);
		goto out;
	}

	if (exclusive && rdev->open_count) {
		regulator = ERR_PTR(-EBUSY);
		goto out;
	}

	if (!try_module_get(rdev->owner))
		goto out;

	regulator = create_regulator(rdev, dev, id);
	if (regulator == NULL) {
		regulator = ERR_PTR(-ENOMEM);
		module_put(rdev->owner);
	}

	rdev->open_count++;
	if (exclusive) {
		rdev->exclusive = 1;

		ret = _regulator_is_enabled(rdev);
		if (ret > 0)
			rdev->use_count = 1;
		else
			rdev->use_count = 0;
	}

out:
	mutex_unlock(&regulator_list_mutex);

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
 * Use of supply names configured via regulator_set_device_supply() is
 * strongly encouraged.  It is recommended that the supply name used
 * should match the name used for the supply and/or the relevant
 * device pins in the datasheet.
 */
struct regulator *regulator_get(struct device *dev, const char *id)
{
	return _regulator_get(dev, id, 0);
}
EXPORT_SYMBOL_GPL(regulator_get);

/**
 * regulator_get_exclusive - obtain exclusive access to a regulator.
 * @dev: device for regulator "consumer"
 * @id: Supply name or regulator ID.
 *
 * Returns a struct regulator corresponding to the regulator producer,
 * or IS_ERR() condition containing errno.  Other consumers will be
 * unable to obtain this reference is held and the use count for the
 * regulator will be initialised to reflect the current state of the
 * regulator.
 *
 * This is intended for use by consumers which cannot tolerate shared
 * use of the regulator such as those which need to force the
 * regulator off for correct operation of the hardware they are
 * controlling.
 *
 * Use of supply names configured via regulator_set_device_supply() is
 * strongly encouraged.  It is recommended that the supply name used
 * should match the name used for the supply and/or the relevant
 * device pins in the datasheet.
 */
struct regulator *regulator_get_exclusive(struct device *dev, const char *id)
{
	return _regulator_get(dev, id, 1);
}
EXPORT_SYMBOL_GPL(regulator_get_exclusive);

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
	struct regulator_dev *rdev;

	if (regulator == NULL || IS_ERR(regulator))
		return;

	mutex_lock(&regulator_list_mutex);
	rdev = regulator->rdev;

	/* remove any sysfs entries */
	if (regulator->dev) {
		sysfs_remove_link(&rdev->dev.kobj, regulator->supply_name);
		kfree(regulator->supply_name);
		device_remove_file(regulator->dev, &regulator->dev_attr);
		kfree(regulator->dev_attr.attr.name);
	}
	list_del(&regulator->list);
	kfree(regulator);

	rdev->open_count--;
	rdev->exclusive = 0;

	module_put(rdev->owner);
	mutex_unlock(&regulator_list_mutex);
}
EXPORT_SYMBOL_GPL(regulator_put);

static int _regulator_can_change_status(struct regulator_dev *rdev)
{
	if (!rdev->constraints)
		return 0;

	if (rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_STATUS)
		return 1;
	else
		return 0;
}

/* locks held by regulator_enable() */
static int _regulator_enable(struct regulator_dev *rdev)
{
	int ret, delay;

	/* do we need to enable the supply regulator first */
	if (rdev->supply) {
		ret = _regulator_enable(rdev->supply);
		if (ret < 0) {
			printk(KERN_ERR "%s: failed to enable %s: %d\n",
			       __func__, rdev_get_name(rdev), ret);
			return ret;
		}
	}

	/* check voltage and requested load before enabling */
	if (rdev->constraints &&
	    (rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_DRMS))
		drms_uA_update(rdev);

	if (rdev->use_count == 0) {
		/* The regulator may on if it's not switchable or left on */
		ret = _regulator_is_enabled(rdev);
		if (ret == -EINVAL || ret == 0) {
			if (!_regulator_can_change_status(rdev))
				return -EPERM;

			if (!rdev->desc->ops->enable)
				return -EINVAL;

			/* Query before enabling in case configuration
			 * dependant.  */
			ret = _regulator_get_enable_time(rdev);
			if (ret >= 0) {
				delay = ret;
			} else {
				printk(KERN_WARNING
					"%s: enable_time() failed for %s: %d\n",
					__func__, rdev_get_name(rdev),
					ret);
				delay = 0;
			}

			/* Allow the regulator to ramp; it would be useful
			 * to extend this for bulk operations so that the
			 * regulators can ramp together.  */
			ret = rdev->desc->ops->enable(rdev);
			if (ret < 0)
				return ret;

			if (delay >= 1000)
				mdelay(delay / 1000);
			else if (delay)
				udelay(delay);

		} else if (ret < 0) {
			printk(KERN_ERR "%s: is_enabled() failed for %s: %d\n",
			       __func__, rdev_get_name(rdev), ret);
			return ret;
		}
		/* Fallthrough on positive return values - already enabled */
	}

	rdev->use_count++;

	return 0;
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
	int ret = 0;

	mutex_lock(&rdev->mutex);
	ret = _regulator_enable(rdev);
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_enable);

/* locks held by regulator_disable() */
static int _regulator_disable(struct regulator_dev *rdev)
{
	int ret = 0;

	if (WARN(rdev->use_count <= 0,
			"unbalanced disables for %s\n",
			rdev_get_name(rdev)))
		return -EIO;

	/* are we the last user and permitted to disable ? */
	if (rdev->use_count == 1 &&
	    (rdev->constraints && !rdev->constraints->always_on)) {

		/* we are last user */
		if (_regulator_can_change_status(rdev) &&
		    rdev->desc->ops->disable) {
			ret = rdev->desc->ops->disable(rdev);
			if (ret < 0) {
				printk(KERN_ERR "%s: failed to disable %s\n",
				       __func__, rdev_get_name(rdev));
				return ret;
			}

			_notifier_call_chain(rdev, REGULATOR_EVENT_DISABLE,
					     NULL);
		}

		/* decrease our supplies ref count and disable if required */
		if (rdev->supply)
			_regulator_disable(rdev->supply);

		rdev->use_count = 0;
	} else if (rdev->use_count > 1) {

		if (rdev->constraints &&
			(rdev->constraints->valid_ops_mask &
			REGULATOR_CHANGE_DRMS))
			drms_uA_update(rdev);

		rdev->use_count--;
	}
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
	int ret = 0;

	mutex_lock(&rdev->mutex);
	ret = _regulator_disable(rdev);
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_disable);

/* locks held by regulator_force_disable() */
static int _regulator_force_disable(struct regulator_dev *rdev)
{
	int ret = 0;

	/* force disable */
	if (rdev->desc->ops->disable) {
		/* ah well, who wants to live forever... */
		ret = rdev->desc->ops->disable(rdev);
		if (ret < 0) {
			printk(KERN_ERR "%s: failed to force disable %s\n",
			       __func__, rdev_get_name(rdev));
			return ret;
		}
		/* notify other consumers that power has been forced off */
		_notifier_call_chain(rdev, REGULATOR_EVENT_FORCE_DISABLE |
			REGULATOR_EVENT_DISABLE, NULL);
	}

	/* decrease our supplies ref count and disable if required */
	if (rdev->supply)
		_regulator_disable(rdev->supply);

	rdev->use_count = 0;
	return ret;
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
	int ret;

	mutex_lock(&regulator->rdev->mutex);
	regulator->uA_load = 0;
	ret = _regulator_force_disable(regulator->rdev);
	mutex_unlock(&regulator->rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_force_disable);

static int _regulator_is_enabled(struct regulator_dev *rdev)
{
	/* If we don't know then assume that the regulator is always on */
	if (!rdev->desc->ops->is_enabled)
		return 1;

	return rdev->desc->ops->is_enabled(rdev);
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

	mutex_lock(&regulator->rdev->mutex);
	ret = _regulator_is_enabled(regulator->rdev);
	mutex_unlock(&regulator->rdev->mutex);

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

	return rdev->desc->n_voltages ? : -EINVAL;
}
EXPORT_SYMBOL_GPL(regulator_count_voltages);

/**
 * regulator_list_voltage - enumerate supported voltages
 * @regulator: regulator source
 * @selector: identify voltage to list
 * Context: can sleep
 *
 * Returns a voltage that can be passed to @regulator_set_voltage(),
 * zero if this selector code can't be used on this sytem, or a
 * negative errno.
 */
int regulator_list_voltage(struct regulator *regulator, unsigned selector)
{
	struct regulator_dev	*rdev = regulator->rdev;
	struct regulator_ops	*ops = rdev->desc->ops;
	int			ret;

	if (!ops->list_voltage || selector >= rdev->desc->n_voltages)
		return -EINVAL;

	mutex_lock(&rdev->mutex);
	ret = ops->list_voltage(rdev, selector);
	mutex_unlock(&rdev->mutex);

	if (ret > 0) {
		if (ret < rdev->constraints->min_uV)
			ret = 0;
		else if (ret > rdev->constraints->max_uV)
			ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_list_voltage);

/**
 * regulator_is_supported_voltage - check if a voltage range can be supported
 *
 * @regulator: Regulator to check.
 * @min_uV: Minimum required voltage in uV.
 * @max_uV: Maximum required voltage in uV.
 *
 * Returns a boolean or a negative error code.
 */
int regulator_is_supported_voltage(struct regulator *regulator,
				   int min_uV, int max_uV)
{
	int i, voltages, ret;

	ret = regulator_count_voltages(regulator);
	if (ret < 0)
		return ret;
	voltages = ret;

	for (i = 0; i < voltages; i++) {
		ret = regulator_list_voltage(regulator, i);

		if (ret >= min_uV && ret <= max_uV)
			return 1;
	}

	return 0;
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
	struct regulator_dev *rdev = regulator->rdev;
	int ret;

	mutex_lock(&rdev->mutex);

	/* sanity check */
	if (!rdev->desc->ops->set_voltage) {
		ret = -EINVAL;
		goto out;
	}

	/* constraints check */
	ret = regulator_check_voltage(rdev, &min_uV, &max_uV);
	if (ret < 0)
		goto out;
	regulator->min_uV = min_uV;
	regulator->max_uV = max_uV;
	ret = rdev->desc->ops->set_voltage(rdev, min_uV, max_uV);

out:
	_notifier_call_chain(rdev, REGULATOR_EVENT_VOLTAGE_CHANGE, NULL);
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_voltage);

static int _regulator_get_voltage(struct regulator_dev *rdev)
{
	/* sanity check */
	if (rdev->desc->ops->get_voltage)
		return rdev->desc->ops->get_voltage(rdev);
	else
		return -EINVAL;
}

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
	int ret;

	mutex_lock(&regulator->rdev->mutex);

	ret = _regulator_get_voltage(regulator->rdev);

	mutex_unlock(&regulator->rdev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_get_voltage);

/**
 * regulator_set_current_limit - set regulator output current limit
 * @regulator: regulator source
 * @min_uA: Minimuum supported current in uA
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

	mutex_lock(&rdev->mutex);

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
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_current_limit);

static int _regulator_get_current_limit(struct regulator_dev *rdev)
{
	int ret;

	mutex_lock(&rdev->mutex);

	/* sanity check */
	if (!rdev->desc->ops->get_current_limit) {
		ret = -EINVAL;
		goto out;
	}

	ret = rdev->desc->ops->get_current_limit(rdev);
out:
	mutex_unlock(&rdev->mutex);
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

	mutex_lock(&rdev->mutex);

	/* sanity check */
	if (!rdev->desc->ops->set_mode) {
		ret = -EINVAL;
		goto out;
	}

	/* constraints check */
	ret = regulator_check_mode(rdev, mode);
	if (ret < 0)
		goto out;

	ret = rdev->desc->ops->set_mode(rdev, mode);
out:
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_mode);

static unsigned int _regulator_get_mode(struct regulator_dev *rdev)
{
	int ret;

	mutex_lock(&rdev->mutex);

	/* sanity check */
	if (!rdev->desc->ops->get_mode) {
		ret = -EINVAL;
		goto out;
	}

	ret = rdev->desc->ops->get_mode(rdev);
out:
	mutex_unlock(&rdev->mutex);
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

/**
 * regulator_set_optimum_mode - set regulator optimum operating mode
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
 * Returns the new regulator mode or error.
 */
int regulator_set_optimum_mode(struct regulator *regulator, int uA_load)
{
	struct regulator_dev *rdev = regulator->rdev;
	struct regulator *consumer;
	int ret, output_uV, input_uV, total_uA_load = 0;
	unsigned int mode;

	mutex_lock(&rdev->mutex);

	regulator->uA_load = uA_load;
	ret = regulator_check_drms(rdev);
	if (ret < 0)
		goto out;
	ret = -EINVAL;

	/* sanity check */
	if (!rdev->desc->ops->get_optimum_mode)
		goto out;

	/* get output voltage */
	output_uV = rdev->desc->ops->get_voltage(rdev);
	if (output_uV <= 0) {
		printk(KERN_ERR "%s: invalid output voltage found for %s\n",
			__func__, rdev_get_name(rdev));
		goto out;
	}

	/* get input voltage */
	if (rdev->supply && rdev->supply->desc->ops->get_voltage)
		input_uV = rdev->supply->desc->ops->get_voltage(rdev->supply);
	else
		input_uV = rdev->constraints->input_uV;
	if (input_uV <= 0) {
		printk(KERN_ERR "%s: invalid input voltage found for %s\n",
			__func__, rdev_get_name(rdev));
		goto out;
	}

	/* calc total requested load for this regulator */
	list_for_each_entry(consumer, &rdev->consumer_list, list)
		total_uA_load += consumer->uA_load;

	mode = rdev->desc->ops->get_optimum_mode(rdev,
						 input_uV, output_uV,
						 total_uA_load);
	ret = regulator_check_mode(rdev, mode);
	if (ret < 0) {
		printk(KERN_ERR "%s: failed to get optimum mode for %s @"
			" %d uA %d -> %d uV\n", __func__, rdev_get_name(rdev),
			total_uA_load, input_uV, output_uV);
		goto out;
	}

	ret = rdev->desc->ops->set_mode(rdev, mode);
	if (ret < 0) {
		printk(KERN_ERR "%s: failed to set optimum mode %x for %s\n",
			__func__, mode, rdev_get_name(rdev));
		goto out;
	}
	ret = mode;
out:
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_optimum_mode);

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
static void _notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data)
{
	struct regulator_dev *_rdev;

	/* call rdev chain first */
	blocking_notifier_call_chain(&rdev->notifier, event, NULL);

	/* now notify regulator we supply */
	list_for_each_entry(_rdev, &rdev->supply_list, slist) {
		mutex_lock(&_rdev->mutex);
		_notifier_call_chain(_rdev, event, data);
		mutex_unlock(&_rdev->mutex);
	}
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
			ret = PTR_ERR(consumers[i].consumer);
			dev_err(dev, "Failed to get supply '%s': %d\n",
				consumers[i].supply, ret);
			consumers[i].consumer = NULL;
			goto err;
		}
	}

	return 0;

err:
	for (i = 0; i < num_consumers && consumers[i].consumer; i++)
		regulator_put(consumers[i].consumer);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_get);

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
	int i;
	int ret;

	for (i = 0; i < num_consumers; i++) {
		ret = regulator_enable(consumers[i].consumer);
		if (ret != 0)
			goto err;
	}

	return 0;

err:
	printk(KERN_ERR "Failed to enable %s: %d\n", consumers[i].supply, ret);
	for (--i; i >= 0; --i)
		regulator_disable(consumers[i].consumer);

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
 * clients in a single API call.  If any consumers cannot be enabled
 * then any others that were disabled will be disabled again prior to
 * return.
 */
int regulator_bulk_disable(int num_consumers,
			   struct regulator_bulk_data *consumers)
{
	int i;
	int ret;

	for (i = 0; i < num_consumers; i++) {
		ret = regulator_disable(consumers[i].consumer);
		if (ret != 0)
			goto err;
	}

	return 0;

err:
	printk(KERN_ERR "Failed to disable %s: %d\n", consumers[i].supply,
	       ret);
	for (--i; i >= 0; --i)
		regulator_enable(consumers[i].consumer);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_disable);

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
 * occurred. We also notify regulator clients downstream.
 * Note lock must be held by caller.
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
	case REGULATOR_STATUS_STANDBY:
		return REGULATOR_STATUS_STANDBY;
	default:
		return 0;
	}
}
EXPORT_SYMBOL_GPL(regulator_mode_to_status);

/*
 * To avoid cluttering sysfs (and memory) with useless state, only
 * create attributes that can be meaningfully displayed.
 */
static int add_regulator_attributes(struct regulator_dev *rdev)
{
	struct device		*dev = &rdev->dev;
	struct regulator_ops	*ops = rdev->desc->ops;
	int			status = 0;

	/* some attributes need specific methods to be displayed */
	if (ops->get_voltage) {
		status = device_create_file(dev, &dev_attr_microvolts);
		if (status < 0)
			return status;
	}
	if (ops->get_current_limit) {
		status = device_create_file(dev, &dev_attr_microamps);
		if (status < 0)
			return status;
	}
	if (ops->get_mode) {
		status = device_create_file(dev, &dev_attr_opmode);
		if (status < 0)
			return status;
	}
	if (ops->is_enabled) {
		status = device_create_file(dev, &dev_attr_state);
		if (status < 0)
			return status;
	}
	if (ops->get_status) {
		status = device_create_file(dev, &dev_attr_status);
		if (status < 0)
			return status;
	}

	/* some attributes are type-specific */
	if (rdev->desc->type == REGULATOR_CURRENT) {
		status = device_create_file(dev, &dev_attr_requested_microamps);
		if (status < 0)
			return status;
	}

	/* all the other attributes exist to support constraints;
	 * don't show them if there are no constraints, or if the
	 * relevant supporting methods are missing.
	 */
	if (!rdev->constraints)
		return status;

	/* constraints need specific supporting methods */
	if (ops->set_voltage) {
		status = device_create_file(dev, &dev_attr_min_microvolts);
		if (status < 0)
			return status;
		status = device_create_file(dev, &dev_attr_max_microvolts);
		if (status < 0)
			return status;
	}
	if (ops->set_current_limit) {
		status = device_create_file(dev, &dev_attr_min_microamps);
		if (status < 0)
			return status;
		status = device_create_file(dev, &dev_attr_max_microamps);
		if (status < 0)
			return status;
	}

	/* suspend mode constraints need multiple supporting methods */
	if (!(ops->set_suspend_enable && ops->set_suspend_disable))
		return status;

	status = device_create_file(dev, &dev_attr_suspend_standby_state);
	if (status < 0)
		return status;
	status = device_create_file(dev, &dev_attr_suspend_mem_state);
	if (status < 0)
		return status;
	status = device_create_file(dev, &dev_attr_suspend_disk_state);
	if (status < 0)
		return status;

	if (ops->set_suspend_voltage) {
		status = device_create_file(dev,
				&dev_attr_suspend_standby_microvolts);
		if (status < 0)
			return status;
		status = device_create_file(dev,
				&dev_attr_suspend_mem_microvolts);
		if (status < 0)
			return status;
		status = device_create_file(dev,
				&dev_attr_suspend_disk_microvolts);
		if (status < 0)
			return status;
	}

	if (ops->set_suspend_mode) {
		status = device_create_file(dev,
				&dev_attr_suspend_standby_mode);
		if (status < 0)
			return status;
		status = device_create_file(dev,
				&dev_attr_suspend_mem_mode);
		if (status < 0)
			return status;
		status = device_create_file(dev,
				&dev_attr_suspend_disk_mode);
		if (status < 0)
			return status;
	}

	return status;
}

/**
 * regulator_register - register regulator
 * @regulator_desc: regulator to register
 * @dev: struct device for the regulator
 * @init_data: platform provided init data, passed through by driver
 * @driver_data: private regulator data
 *
 * Called by regulator drivers to register a regulator.
 * Returns 0 on success.
 */
struct regulator_dev *regulator_register(struct regulator_desc *regulator_desc,
	struct device *dev, struct regulator_init_data *init_data,
	void *driver_data)
{
	static atomic_t regulator_no = ATOMIC_INIT(0);
	struct regulator_dev *rdev;
	int ret, i;

	if (regulator_desc == NULL)
		return ERR_PTR(-EINVAL);

	if (regulator_desc->name == NULL || regulator_desc->ops == NULL)
		return ERR_PTR(-EINVAL);

	if (regulator_desc->type != REGULATOR_VOLTAGE &&
	    regulator_desc->type != REGULATOR_CURRENT)
		return ERR_PTR(-EINVAL);

	if (!init_data)
		return ERR_PTR(-EINVAL);

	rdev = kzalloc(sizeof(struct regulator_dev), GFP_KERNEL);
	if (rdev == NULL)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&regulator_list_mutex);

	mutex_init(&rdev->mutex);
	rdev->reg_data = driver_data;
	rdev->owner = regulator_desc->owner;
	rdev->desc = regulator_desc;
	INIT_LIST_HEAD(&rdev->consumer_list);
	INIT_LIST_HEAD(&rdev->supply_list);
	INIT_LIST_HEAD(&rdev->list);
	INIT_LIST_HEAD(&rdev->slist);
	BLOCKING_INIT_NOTIFIER_HEAD(&rdev->notifier);

	/* preform any regulator specific init */
	if (init_data->regulator_init) {
		ret = init_data->regulator_init(rdev->reg_data);
		if (ret < 0)
			goto clean;
	}

	/* register with sysfs */
	rdev->dev.class = &regulator_class;
	rdev->dev.parent = dev;
	dev_set_name(&rdev->dev, "regulator.%d",
		     atomic_inc_return(&regulator_no) - 1);
	ret = device_register(&rdev->dev);
	if (ret != 0)
		goto clean;

	dev_set_drvdata(&rdev->dev, rdev);

	/* set regulator constraints */
	ret = set_machine_constraints(rdev, &init_data->constraints);
	if (ret < 0)
		goto scrub;

	/* add attributes supported by this regulator */
	ret = add_regulator_attributes(rdev);
	if (ret < 0)
		goto scrub;

	/* set supply regulator if it exists */
	if (init_data->supply_regulator_dev) {
		ret = set_supply(rdev,
			dev_get_drvdata(init_data->supply_regulator_dev));
		if (ret < 0)
			goto scrub;
	}

	/* add consumers devices */
	for (i = 0; i < init_data->num_consumer_supplies; i++) {
		ret = set_consumer_device_supply(rdev,
			init_data->consumer_supplies[i].dev,
			init_data->consumer_supplies[i].dev_name,
			init_data->consumer_supplies[i].supply);
		if (ret < 0) {
			for (--i; i >= 0; i--)
				unset_consumer_device_supply(rdev,
				    init_data->consumer_supplies[i].dev_name,
				    init_data->consumer_supplies[i].dev);
			goto scrub;
		}
	}

	list_add(&rdev->list, &regulator_list);
out:
	mutex_unlock(&regulator_list_mutex);
	return rdev;

scrub:
	device_unregister(&rdev->dev);
	/* device core frees rdev */
	rdev = ERR_PTR(ret);
	goto out;

clean:
	kfree(rdev);
	rdev = ERR_PTR(ret);
	goto out;
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

	mutex_lock(&regulator_list_mutex);
	WARN_ON(rdev->open_count);
	unset_regulator_supplies(rdev);
	list_del(&rdev->list);
	if (rdev->supply)
		sysfs_remove_link(&rdev->dev.kobj, "supply");
	device_unregister(&rdev->dev);
	mutex_unlock(&regulator_list_mutex);
}
EXPORT_SYMBOL_GPL(regulator_unregister);

/**
 * regulator_suspend_prepare - prepare regulators for system wide suspend
 * @state: system suspend state
 *
 * Configure each regulator with it's suspend operating parameters for state.
 * This will usually be called by machine suspend code prior to supending.
 */
int regulator_suspend_prepare(suspend_state_t state)
{
	struct regulator_dev *rdev;
	int ret = 0;

	/* ON is handled by regulator active state */
	if (state == PM_SUSPEND_ON)
		return -EINVAL;

	mutex_lock(&regulator_list_mutex);
	list_for_each_entry(rdev, &regulator_list, list) {

		mutex_lock(&rdev->mutex);
		ret = suspend_prepare(rdev, state);
		mutex_unlock(&rdev->mutex);

		if (ret < 0) {
			printk(KERN_ERR "%s: failed to prepare %s\n",
				__func__, rdev_get_name(rdev));
			goto out;
		}
	}
out:
	mutex_unlock(&regulator_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_suspend_prepare);

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
 * regulator_get_id - get regulator ID
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

void *regulator_get_init_drvdata(struct regulator_init_data *reg_init_data)
{
	return reg_init_data->driver_data;
}
EXPORT_SYMBOL_GPL(regulator_get_init_drvdata);

static int __init regulator_init(void)
{
	int ret;

	printk(KERN_INFO "regulator: core version %s\n", REGULATOR_VERSION);

	ret = class_register(&regulator_class);

	regulator_dummy_init();

	return ret;
}

/* init early to allow our consumers to complete system booting */
core_initcall(regulator_init);

static int __init regulator_init_complete(void)
{
	struct regulator_dev *rdev;
	struct regulator_ops *ops;
	struct regulation_constraints *c;
	int enabled, ret;
	const char *name;

	mutex_lock(&regulator_list_mutex);

	/* If we have a full configuration then disable any regulators
	 * which are not in use or always_on.  This will become the
	 * default behaviour in the future.
	 */
	list_for_each_entry(rdev, &regulator_list, list) {
		ops = rdev->desc->ops;
		c = rdev->constraints;

		name = rdev_get_name(rdev);

		if (!ops->disable || (c && c->always_on))
			continue;

		mutex_lock(&rdev->mutex);

		if (rdev->use_count)
			goto unlock;

		/* If we can't read the status assume it's on. */
		if (ops->is_enabled)
			enabled = ops->is_enabled(rdev);
		else
			enabled = 1;

		if (!enabled)
			goto unlock;

		if (has_full_constraints) {
			/* We log since this may kill the system if it
			 * goes wrong. */
			printk(KERN_INFO "%s: disabling %s\n",
			       __func__, name);
			ret = ops->disable(rdev);
			if (ret != 0) {
				printk(KERN_ERR
				       "%s: couldn't disable %s: %d\n",
				       __func__, name, ret);
			}
		} else {
			/* The intention is that in future we will
			 * assume that full constraints are provided
			 * so warn even if we aren't going to do
			 * anything here.
			 */
			printk(KERN_WARNING
			       "%s: incomplete constraints, leaving %s on\n",
			       __func__, name);
		}

unlock:
		mutex_unlock(&rdev->mutex);
	}

	mutex_unlock(&regulator_list_mutex);

	return 0;
}
late_initcall(regulator_init_complete);
