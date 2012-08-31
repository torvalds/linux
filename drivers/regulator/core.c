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
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/async.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/regulator.h>

#include "dummy.h"

#define rdev_crit(rdev, fmt, ...)					\
	pr_crit("%s: " fmt, rdev_get_name(rdev), ##__VA_ARGS__)
#define rdev_err(rdev, fmt, ...)					\
	pr_err("%s: " fmt, rdev_get_name(rdev), ##__VA_ARGS__)
#define rdev_warn(rdev, fmt, ...)					\
	pr_warn("%s: " fmt, rdev_get_name(rdev), ##__VA_ARGS__)
#define rdev_info(rdev, fmt, ...)					\
	pr_info("%s: " fmt, rdev_get_name(rdev), ##__VA_ARGS__)
#define rdev_dbg(rdev, fmt, ...)					\
	pr_debug("%s: " fmt, rdev_get_name(rdev), ##__VA_ARGS__)

static DEFINE_MUTEX(regulator_list_mutex);
static LIST_HEAD(regulator_list);
static LIST_HEAD(regulator_map_list);
static bool has_full_constraints;
static bool board_wants_dummy_regulator;

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
 * struct regulator
 *
 * One for each consumer device.
 */
struct regulator {
	struct device *dev;
	struct list_head list;
	unsigned int always_on:1;
	unsigned int bypass:1;
	int uA_load;
	int min_uV;
	int max_uV;
	char *supply_name;
	struct device_attribute dev_attr;
	struct regulator_dev *rdev;
	struct dentry *debugfs;
};

static int _regulator_is_enabled(struct regulator_dev *rdev);
static int _regulator_disable(struct regulator_dev *rdev);
static int _regulator_get_voltage(struct regulator_dev *rdev);
static int _regulator_get_current_limit(struct regulator_dev *rdev);
static unsigned int _regulator_get_mode(struct regulator_dev *rdev);
static void _notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data);
static int _regulator_do_set_voltage(struct regulator_dev *rdev,
				     int min_uV, int max_uV);
static struct regulator *create_regulator(struct regulator_dev *rdev,
					  struct device *dev,
					  const char *supply_name);

static const char *rdev_get_name(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->name)
		return rdev->constraints->name;
	else if (rdev->desc->name)
		return rdev->desc->name;
	else
		return "";
}

/**
 * of_get_regulator - get a regulator device node based on supply name
 * @dev: Device pointer for the consumer (of regulator) device
 * @supply: regulator supply name
 *
 * Extract the regulator device node corresponding to the supply name.
 * retruns the device node corresponding to the regulator if found, else
 * returns NULL.
 */
static struct device_node *of_get_regulator(struct device *dev, const char *supply)
{
	struct device_node *regnode = NULL;
	char prop_name[32]; /* 32 is max size of property name */

	dev_dbg(dev, "Looking up %s-supply from device tree\n", supply);

	snprintf(prop_name, 32, "%s-supply", supply);
	regnode = of_parse_phandle(dev->of_node, prop_name, 0);

	if (!regnode) {
		dev_dbg(dev, "Looking up %s property in node %s failed",
				prop_name, dev->of_node->full_name);
		return NULL;
	}
	return regnode;
}

static int _regulator_can_change_status(struct regulator_dev *rdev)
{
	if (!rdev->constraints)
		return 0;

	if (rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_STATUS)
		return 1;
	else
		return 0;
}

/* Platform voltage constraint check */
static int regulator_check_voltage(struct regulator_dev *rdev,
				   int *min_uV, int *max_uV)
{
	BUG_ON(*min_uV > *max_uV);

	if (!rdev->constraints) {
		rdev_err(rdev, "no constraints\n");
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_VOLTAGE)) {
		rdev_err(rdev, "operation not allowed\n");
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

/* Make sure we select a voltage that suits the needs of all
 * regulator consumers
 */
static int regulator_check_consumers(struct regulator_dev *rdev,
				     int *min_uV, int *max_uV)
{
	struct regulator *regulator;

	list_for_each_entry(regulator, &rdev->consumer_list, list) {
		/*
		 * Assume consumers that didn't say anything are OK
		 * with anything in the constraint range.
		 */
		if (!regulator->min_uV && !regulator->max_uV)
			continue;

		if (*max_uV > regulator->max_uV)
			*max_uV = regulator->max_uV;
		if (*min_uV < regulator->min_uV)
			*min_uV = regulator->min_uV;
	}

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
		rdev_err(rdev, "no constraints\n");
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_CURRENT)) {
		rdev_err(rdev, "operation not allowed\n");
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
static int regulator_mode_constrain(struct regulator_dev *rdev, int *mode)
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

	if (!rdev->constraints) {
		rdev_err(rdev, "no constraints\n");
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_MODE)) {
		rdev_err(rdev, "operation not allowed\n");
		return -EPERM;
	}

	/* The modes are bitmasks, the most power hungry modes having
	 * the lowest values. If the requested mode isn't supported
	 * try higher modes. */
	while (*mode) {
		if (rdev->constraints->valid_modes_mask & *mode)
			return 0;
		*mode /= 2;
	}

	return -EINVAL;
}

/* dynamic regulator mode switching constraint check */
static int regulator_check_drms(struct regulator_dev *rdev)
{
	if (!rdev->constraints) {
		rdev_err(rdev, "no constraints\n");
		return -ENODEV;
	}
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_DRMS)) {
		rdev_err(rdev, "operation not allowed\n");
		return -EPERM;
	}
	return 0;
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

static ssize_t regulator_bypass_show(struct device *dev,
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
static DEVICE_ATTR(bypass, 0444,
		   regulator_bypass_show, NULL);

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
	    (!rdev->desc->ops->get_voltage &&
	     !rdev->desc->ops->get_voltage_sel) ||
	    !rdev->desc->ops->set_mode)
		return;

	/* get output voltage */
	output_uV = _regulator_get_voltage(rdev);
	if (output_uV <= 0)
		return;

	/* get input voltage */
	input_uV = 0;
	if (rdev->supply)
		input_uV = regulator_get_voltage(rdev->supply);
	if (input_uV <= 0)
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
	err = regulator_mode_constrain(rdev, &mode);
	if (err == 0)
		rdev->desc->ops->set_mode(rdev, mode);
}

static int suspend_set_state(struct regulator_dev *rdev,
	struct regulator_state *rstate)
{
	int ret = 0;

	/* If we have no suspend mode configration don't set anything;
	 * only warn if the driver implements set_suspend_voltage or
	 * set_suspend_mode callback.
	 */
	if (!rstate->enabled && !rstate->disabled) {
		if (rdev->desc->ops->set_suspend_voltage ||
		    rdev->desc->ops->set_suspend_mode)
			rdev_warn(rdev, "No configuration\n");
		return 0;
	}

	if (rstate->enabled && rstate->disabled) {
		rdev_err(rdev, "invalid configuration\n");
		return -EINVAL;
	}

	if (rstate->enabled && rdev->desc->ops->set_suspend_enable)
		ret = rdev->desc->ops->set_suspend_enable(rdev);
	else if (rstate->disabled && rdev->desc->ops->set_suspend_disable)
		ret = rdev->desc->ops->set_suspend_disable(rdev);
	else /* OK if set_suspend_enable or set_suspend_disable is NULL */
		ret = 0;

	if (ret < 0) {
		rdev_err(rdev, "failed to enabled/disable\n");
		return ret;
	}

	if (rdev->desc->ops->set_suspend_voltage && rstate->uV > 0) {
		ret = rdev->desc->ops->set_suspend_voltage(rdev, rstate->uV);
		if (ret < 0) {
			rdev_err(rdev, "failed to set voltage\n");
			return ret;
		}
	}

	if (rdev->desc->ops->set_suspend_mode && rstate->mode > 0) {
		ret = rdev->desc->ops->set_suspend_mode(rdev, rstate->mode);
		if (ret < 0) {
			rdev_err(rdev, "failed to set mode\n");
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

	if (constraints->uV_offset)
		count += sprintf(buf, "%dmV offset ",
				 constraints->uV_offset / 1000);

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
			count += sprintf(buf + count, "at %d mA ", ret / 1000);
	}

	if (constraints->valid_modes_mask & REGULATOR_MODE_FAST)
		count += sprintf(buf + count, "fast ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_NORMAL)
		count += sprintf(buf + count, "normal ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_IDLE)
		count += sprintf(buf + count, "idle ");
	if (constraints->valid_modes_mask & REGULATOR_MODE_STANDBY)
		count += sprintf(buf + count, "standby");

	rdev_info(rdev, "%s\n", buf);

	if ((constraints->min_uV != constraints->max_uV) &&
	    !(constraints->valid_ops_mask & REGULATOR_CHANGE_VOLTAGE))
		rdev_warn(rdev,
			  "Voltage range but no REGULATOR_CHANGE_VOLTAGE\n");
}

static int machine_constraints_voltage(struct regulator_dev *rdev,
	struct regulation_constraints *constraints)
{
	struct regulator_ops *ops = rdev->desc->ops;
	int ret;

	/* do we need to apply the constraint voltage */
	if (rdev->constraints->apply_uV &&
	    rdev->constraints->min_uV == rdev->constraints->max_uV) {
		ret = _regulator_do_set_voltage(rdev,
						rdev->constraints->min_uV,
						rdev->constraints->max_uV);
		if (ret < 0) {
			rdev_err(rdev, "failed to apply %duV constraint\n",
				 rdev->constraints->min_uV);
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
			rdev_err(rdev, "invalid voltage constraints\n");
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
			rdev_err(rdev, "unsupportable voltage constraints\n");
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
	const struct regulation_constraints *constraints)
{
	int ret = 0;
	struct regulator_ops *ops = rdev->desc->ops;

	if (constraints)
		rdev->constraints = kmemdup(constraints, sizeof(*constraints),
					    GFP_KERNEL);
	else
		rdev->constraints = kzalloc(sizeof(*constraints),
					    GFP_KERNEL);
	if (!rdev->constraints)
		return -ENOMEM;

	ret = machine_constraints_voltage(rdev, rdev->constraints);
	if (ret != 0)
		goto out;

	/* do we need to setup our suspend state */
	if (rdev->constraints->initial_state) {
		ret = suspend_prepare(rdev, rdev->constraints->initial_state);
		if (ret < 0) {
			rdev_err(rdev, "failed to set suspend state\n");
			goto out;
		}
	}

	if (rdev->constraints->initial_mode) {
		if (!ops->set_mode) {
			rdev_err(rdev, "no set_mode operation\n");
			ret = -EINVAL;
			goto out;
		}

		ret = ops->set_mode(rdev, rdev->constraints->initial_mode);
		if (ret < 0) {
			rdev_err(rdev, "failed to set initial mode: %d\n", ret);
			goto out;
		}
	}

	/* If the constraints say the regulator should be on at this point
	 * and we have control then make sure it is enabled.
	 */
	if ((rdev->constraints->always_on || rdev->constraints->boot_on) &&
	    ops->enable) {
		ret = ops->enable(rdev);
		if (ret < 0) {
			rdev_err(rdev, "failed to enable\n");
			goto out;
		}
	}

	if (rdev->constraints->ramp_delay && ops->set_ramp_delay) {
		ret = ops->set_ramp_delay(rdev, rdev->constraints->ramp_delay);
		if (ret < 0) {
			rdev_err(rdev, "failed to set ramp_delay\n");
			goto out;
		}
	}

	print_constraints(rdev);
	return 0;
out:
	kfree(rdev->constraints);
	rdev->constraints = NULL;
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

	rdev_info(rdev, "supplied by %s\n", rdev_get_name(supply_rdev));

	rdev->supply = create_regulator(supply_rdev, &rdev->dev, "SUPPLY");
	if (rdev->supply == NULL) {
		err = -ENOMEM;
		return err;
	}

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
	struct regulator_map *node;
	int has_dev;

	if (supply == NULL)
		return -EINVAL;

	if (consumer_dev_name != NULL)
		has_dev = 1;
	else
		has_dev = 0;

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

#define REG_STR_SIZE	64

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
		regulator->dev = dev;

		/* Add a link to the device sysfs entry */
		size = scnprintf(buf, REG_STR_SIZE, "%s-%s",
				 dev->kobj.name, supply_name);
		if (size >= REG_STR_SIZE)
			goto overflow_err;

		regulator->supply_name = kstrdup(buf, GFP_KERNEL);
		if (regulator->supply_name == NULL)
			goto overflow_err;

		err = sysfs_create_link(&rdev->dev.kobj, &dev->kobj,
					buf);
		if (err) {
			rdev_warn(rdev, "could not add device link %s err %d\n",
				  dev->kobj.name, err);
			/* non-fatal */
		}
	} else {
		regulator->supply_name = kstrdup(supply_name, GFP_KERNEL);
		if (regulator->supply_name == NULL)
			goto overflow_err;
	}

	regulator->debugfs = debugfs_create_dir(regulator->supply_name,
						rdev->debugfs);
	if (!regulator->debugfs) {
		rdev_warn(rdev, "Failed to create debugfs directory\n");
	} else {
		debugfs_create_u32("uA_load", 0444, regulator->debugfs,
				   &regulator->uA_load);
		debugfs_create_u32("min_uV", 0444, regulator->debugfs,
				   &regulator->min_uV);
		debugfs_create_u32("max_uV", 0444, regulator->debugfs,
				   &regulator->max_uV);
	}

	/*
	 * Check now if the regulator is an always on regulator - if
	 * it is then we don't need to do nearly so much work for
	 * enable/disable calls.
	 */
	if (!_regulator_can_change_status(rdev) &&
	    _regulator_is_enabled(rdev))
		regulator->always_on = true;

	mutex_unlock(&rdev->mutex);
	return regulator;
overflow_err:
	list_del(&regulator->list);
	kfree(regulator);
	mutex_unlock(&rdev->mutex);
	return NULL;
}

static int _regulator_get_enable_time(struct regulator_dev *rdev)
{
	if (!rdev->desc->ops->enable_time)
		return rdev->desc->enable_time;
	return rdev->desc->ops->enable_time(rdev);
}

static struct regulator_dev *regulator_dev_lookup(struct device *dev,
						  const char *supply,
						  int *ret)
{
	struct regulator_dev *r;
	struct device_node *node;
	struct regulator_map *map;
	const char *devname = NULL;

	/* first do a dt based lookup */
	if (dev && dev->of_node) {
		node = of_get_regulator(dev, supply);
		if (node) {
			list_for_each_entry(r, &regulator_list, list)
				if (r->dev.parent &&
					node == r->dev.of_node)
					return r;
		} else {
			/*
			 * If we couldn't even get the node then it's
			 * not just that the device didn't register
			 * yet, there's no node and we'll never
			 * succeed.
			 */
			*ret = -ENODEV;
		}
	}

	/* if not found, try doing it non-dt way */
	if (dev)
		devname = dev_name(dev);

	list_for_each_entry(r, &regulator_list, list)
		if (strcmp(rdev_get_name(r), supply) == 0)
			return r;

	list_for_each_entry(map, &regulator_map_list, list) {
		/* If the mapping has a device set up it must match */
		if (map->dev_name &&
		    (!devname || strcmp(map->dev_name, devname)))
			continue;

		if (strcmp(map->supply, supply) == 0)
			return map->regulator;
	}


	return NULL;
}

/* Internal regulator request function */
static struct regulator *_regulator_get(struct device *dev, const char *id,
					int exclusive)
{
	struct regulator_dev *rdev;
	struct regulator *regulator = ERR_PTR(-EPROBE_DEFER);
	const char *devname = NULL;
	int ret;

	if (id == NULL) {
		pr_err("get() with no identifier\n");
		return regulator;
	}

	if (dev)
		devname = dev_name(dev);

	mutex_lock(&regulator_list_mutex);

	rdev = regulator_dev_lookup(dev, id, &ret);
	if (rdev)
		goto found;

	if (board_wants_dummy_regulator) {
		rdev = dummy_regulator_rdev;
		goto found;
	}

#ifdef CONFIG_REGULATOR_DUMMY
	if (!devname)
		devname = "deviceless";

	/* If the board didn't flag that it was fully constrained then
	 * substitute in a dummy regulator so consumers can continue.
	 */
	if (!has_full_constraints) {
		pr_warn("%s supply %s not found, using dummy regulator\n",
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
		goto out;
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

static void devm_regulator_release(struct device *dev, void *res)
{
	regulator_put(*(struct regulator **)res);
}

/**
 * devm_regulator_get - Resource managed regulator_get()
 * @dev: device for regulator "consumer"
 * @id: Supply name or regulator ID.
 *
 * Managed regulator_get(). Regulators returned from this function are
 * automatically regulator_put() on driver detach. See regulator_get() for more
 * information.
 */
struct regulator *devm_regulator_get(struct device *dev, const char *id)
{
	struct regulator **ptr, *regulator;

	ptr = devres_alloc(devm_regulator_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	regulator = regulator_get(dev, id);
	if (!IS_ERR(regulator)) {
		*ptr = regulator;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return regulator;
}
EXPORT_SYMBOL_GPL(devm_regulator_get);

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

	debugfs_remove_recursive(regulator->debugfs);

	/* remove any sysfs entries */
	if (regulator->dev)
		sysfs_remove_link(&rdev->dev.kobj, regulator->supply_name);
	kfree(regulator->supply_name);
	list_del(&regulator->list);
	kfree(regulator);

	rdev->open_count--;
	rdev->exclusive = 0;

	module_put(rdev->owner);
	mutex_unlock(&regulator_list_mutex);
}
EXPORT_SYMBOL_GPL(regulator_put);

static int devm_regulator_match(struct device *dev, void *res, void *data)
{
	struct regulator **r = res;
	if (!r || !*r) {
		WARN_ON(!r || !*r);
		return 0;
	}
	return *r == data;
}

/**
 * devm_regulator_put - Resource managed regulator_put()
 * @regulator: regulator to free
 *
 * Deallocate a regulator allocated with devm_regulator_get(). Normally
 * this function will not need to be called and the resource management
 * code will ensure that the resource is freed.
 */
void devm_regulator_put(struct regulator *regulator)
{
	int rc;

	rc = devres_release(regulator->dev, devm_regulator_release,
			    devm_regulator_match, regulator);
	if (rc != 0)
		WARN_ON(rc);
}
EXPORT_SYMBOL_GPL(devm_regulator_put);

static int _regulator_do_enable(struct regulator_dev *rdev)
{
	int ret, delay;

	/* Query before enabling in case configuration dependent.  */
	ret = _regulator_get_enable_time(rdev);
	if (ret >= 0) {
		delay = ret;
	} else {
		rdev_warn(rdev, "enable_time() failed: %d\n", ret);
		delay = 0;
	}

	trace_regulator_enable(rdev_get_name(rdev));

	if (rdev->ena_gpio) {
		gpio_set_value_cansleep(rdev->ena_gpio,
					!rdev->ena_gpio_invert);
		rdev->ena_gpio_state = 1;
	} else if (rdev->desc->ops->enable) {
		ret = rdev->desc->ops->enable(rdev);
		if (ret < 0)
			return ret;
	} else {
		return -EINVAL;
	}

	/* Allow the regulator to ramp; it would be useful to extend
	 * this for bulk operations so that the regulators can ramp
	 * together.  */
	trace_regulator_enable_delay(rdev_get_name(rdev));

	if (delay >= 1000) {
		mdelay(delay / 1000);
		udelay(delay % 1000);
	} else if (delay) {
		udelay(delay);
	}

	trace_regulator_enable_complete(rdev_get_name(rdev));

	return 0;
}

/* locks held by regulator_enable() */
static int _regulator_enable(struct regulator_dev *rdev)
{
	int ret;

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

			ret = _regulator_do_enable(rdev);
			if (ret < 0)
				return ret;

		} else if (ret < 0) {
			rdev_err(rdev, "is_enabled() failed: %d\n", ret);
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

	if (regulator->always_on)
		return 0;

	if (rdev->supply) {
		ret = regulator_enable(rdev->supply);
		if (ret != 0)
			return ret;
	}

	mutex_lock(&rdev->mutex);
	ret = _regulator_enable(rdev);
	mutex_unlock(&rdev->mutex);

	if (ret != 0 && rdev->supply)
		regulator_disable(rdev->supply);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_enable);

static int _regulator_do_disable(struct regulator_dev *rdev)
{
	int ret;

	trace_regulator_disable(rdev_get_name(rdev));

	if (rdev->ena_gpio) {
		gpio_set_value_cansleep(rdev->ena_gpio,
					rdev->ena_gpio_invert);
		rdev->ena_gpio_state = 0;

	} else if (rdev->desc->ops->disable) {
		ret = rdev->desc->ops->disable(rdev);
		if (ret != 0)
			return ret;
	}

	trace_regulator_disable_complete(rdev_get_name(rdev));

	_notifier_call_chain(rdev, REGULATOR_EVENT_DISABLE,
			     NULL);
	return 0;
}

/* locks held by regulator_disable() */
static int _regulator_disable(struct regulator_dev *rdev)
{
	int ret = 0;

	if (WARN(rdev->use_count <= 0,
		 "unbalanced disables for %s\n", rdev_get_name(rdev)))
		return -EIO;

	/* are we the last user and permitted to disable ? */
	if (rdev->use_count == 1 &&
	    (rdev->constraints && !rdev->constraints->always_on)) {

		/* we are last user */
		if (_regulator_can_change_status(rdev)) {
			ret = _regulator_do_disable(rdev);
			if (ret < 0) {
				rdev_err(rdev, "failed to disable\n");
				return ret;
			}
		}

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

	if (regulator->always_on)
		return 0;

	mutex_lock(&rdev->mutex);
	ret = _regulator_disable(rdev);
	mutex_unlock(&rdev->mutex);

	if (ret == 0 && rdev->supply)
		regulator_disable(rdev->supply);

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
			rdev_err(rdev, "failed to force disable\n");
			return ret;
		}
		/* notify other consumers that power has been forced off */
		_notifier_call_chain(rdev, REGULATOR_EVENT_FORCE_DISABLE |
			REGULATOR_EVENT_DISABLE, NULL);
	}

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
	struct regulator_dev *rdev = regulator->rdev;
	int ret;

	mutex_lock(&rdev->mutex);
	regulator->uA_load = 0;
	ret = _regulator_force_disable(regulator->rdev);
	mutex_unlock(&rdev->mutex);

	if (rdev->supply)
		while (rdev->open_count--)
			regulator_disable(rdev->supply);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_force_disable);

static void regulator_disable_work(struct work_struct *work)
{
	struct regulator_dev *rdev = container_of(work, struct regulator_dev,
						  disable_work.work);
	int count, i, ret;

	mutex_lock(&rdev->mutex);

	BUG_ON(!rdev->deferred_disables);

	count = rdev->deferred_disables;
	rdev->deferred_disables = 0;

	for (i = 0; i < count; i++) {
		ret = _regulator_disable(rdev);
		if (ret != 0)
			rdev_err(rdev, "Deferred disable failed: %d\n", ret);
	}

	mutex_unlock(&rdev->mutex);

	if (rdev->supply) {
		for (i = 0; i < count; i++) {
			ret = regulator_disable(rdev->supply);
			if (ret != 0) {
				rdev_err(rdev,
					 "Supply disable failed: %d\n", ret);
			}
		}
	}
}

/**
 * regulator_disable_deferred - disable regulator output with delay
 * @regulator: regulator source
 * @ms: miliseconds until the regulator is disabled
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
	int ret;

	if (regulator->always_on)
		return 0;

	mutex_lock(&rdev->mutex);
	rdev->deferred_disables++;
	mutex_unlock(&rdev->mutex);

	ret = schedule_delayed_work(&rdev->disable_work,
				    msecs_to_jiffies(ms));
	if (ret < 0)
		return ret;
	else
		return 0;
}
EXPORT_SYMBOL_GPL(regulator_disable_deferred);

/**
 * regulator_is_enabled_regmap - standard is_enabled() for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their descriptor and then use
 * this as their is_enabled operation, saving some code.
 */
int regulator_is_enabled_regmap(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, rdev->desc->enable_reg, &val);
	if (ret != 0)
		return ret;

	return (val & rdev->desc->enable_mask) != 0;
}
EXPORT_SYMBOL_GPL(regulator_is_enabled_regmap);

/**
 * regulator_enable_regmap - standard enable() for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their descriptor and then use
 * this as their enable() operation, saving some code.
 */
int regulator_enable_regmap(struct regulator_dev *rdev)
{
	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask,
				  rdev->desc->enable_mask);
}
EXPORT_SYMBOL_GPL(regulator_enable_regmap);

/**
 * regulator_disable_regmap - standard disable() for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * enable_reg and enable_mask fields in their descriptor and then use
 * this as their disable() operation, saving some code.
 */
int regulator_disable_regmap(struct regulator_dev *rdev)
{
	return regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, 0);
}
EXPORT_SYMBOL_GPL(regulator_disable_regmap);

static int _regulator_is_enabled(struct regulator_dev *rdev)
{
	/* A GPIO control always takes precedence */
	if (rdev->ena_gpio)
		return rdev->ena_gpio_state;

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

	if (regulator->always_on)
		return 1;

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
 * regulator_list_voltage_linear - List voltages with simple calculation
 *
 * @rdev: Regulator device
 * @selector: Selector to convert into a voltage
 *
 * Regulators with a simple linear mapping between voltages and
 * selectors can set min_uV and uV_step in the regulator descriptor
 * and then use this function as their list_voltage() operation,
 */
int regulator_list_voltage_linear(struct regulator_dev *rdev,
				  unsigned int selector)
{
	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;

	return rdev->desc->min_uV + (rdev->desc->uV_step * selector);
}
EXPORT_SYMBOL_GPL(regulator_list_voltage_linear);

/**
 * regulator_list_voltage_table - List voltages with table based mapping
 *
 * @rdev: Regulator device
 * @selector: Selector to convert into a voltage
 *
 * Regulators with table based mapping between voltages and
 * selectors can set volt_table in the regulator descriptor
 * and then use this function as their list_voltage() operation.
 */
int regulator_list_voltage_table(struct regulator_dev *rdev,
				 unsigned int selector)
{
	if (!rdev->desc->volt_table) {
		BUG_ON(!rdev->desc->volt_table);
		return -EINVAL;
	}

	if (selector >= rdev->desc->n_voltages)
		return -EINVAL;

	return rdev->desc->volt_table[selector];
}
EXPORT_SYMBOL_GPL(regulator_list_voltage_table);

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
	struct regulator_dev *rdev = regulator->rdev;
	int i, voltages, ret;

	/* If we can't change voltage check the current voltage */
	if (!(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_VOLTAGE)) {
		ret = regulator_get_voltage(regulator);
		if (ret >= 0)
			return (min_uV >= ret && ret <= max_uV);
		else
			return ret;
	}

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
EXPORT_SYMBOL_GPL(regulator_is_supported_voltage);

/**
 * regulator_get_voltage_sel_regmap - standard get_voltage_sel for regmap users
 *
 * @rdev: regulator to operate on
 *
 * Regulators that use regmap for their register I/O can set the
 * vsel_reg and vsel_mask fields in their descriptor and then use this
 * as their get_voltage_vsel operation, saving some code.
 */
int regulator_get_voltage_sel_regmap(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (ret != 0)
		return ret;

	val &= rdev->desc->vsel_mask;
	val >>= ffs(rdev->desc->vsel_mask) - 1;

	return val;
}
EXPORT_SYMBOL_GPL(regulator_get_voltage_sel_regmap);

/**
 * regulator_set_voltage_sel_regmap - standard set_voltage_sel for regmap users
 *
 * @rdev: regulator to operate on
 * @sel: Selector to set
 *
 * Regulators that use regmap for their register I/O can set the
 * vsel_reg and vsel_mask fields in their descriptor and then use this
 * as their set_voltage_vsel operation, saving some code.
 */
int regulator_set_voltage_sel_regmap(struct regulator_dev *rdev, unsigned sel)
{
	sel <<= ffs(rdev->desc->vsel_mask) - 1;

	return regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
				  rdev->desc->vsel_mask, sel);
}
EXPORT_SYMBOL_GPL(regulator_set_voltage_sel_regmap);

/**
 * regulator_map_voltage_iterate - map_voltage() based on list_voltage()
 *
 * @rdev: Regulator to operate on
 * @min_uV: Lower bound for voltage
 * @max_uV: Upper bound for voltage
 *
 * Drivers implementing set_voltage_sel() and list_voltage() can use
 * this as their map_voltage() operation.  It will find a suitable
 * voltage by calling list_voltage() until it gets something in bounds
 * for the requested voltages.
 */
int regulator_map_voltage_iterate(struct regulator_dev *rdev,
				  int min_uV, int max_uV)
{
	int best_val = INT_MAX;
	int selector = 0;
	int i, ret;

	/* Find the smallest voltage that falls within the specified
	 * range.
	 */
	for (i = 0; i < rdev->desc->n_voltages; i++) {
		ret = rdev->desc->ops->list_voltage(rdev, i);
		if (ret < 0)
			continue;

		if (ret < best_val && ret >= min_uV && ret <= max_uV) {
			best_val = ret;
			selector = i;
		}
	}

	if (best_val != INT_MAX)
		return selector;
	else
		return -EINVAL;
}
EXPORT_SYMBOL_GPL(regulator_map_voltage_iterate);

/**
 * regulator_map_voltage_linear - map_voltage() for simple linear mappings
 *
 * @rdev: Regulator to operate on
 * @min_uV: Lower bound for voltage
 * @max_uV: Upper bound for voltage
 *
 * Drivers providing min_uV and uV_step in their regulator_desc can
 * use this as their map_voltage() operation.
 */
int regulator_map_voltage_linear(struct regulator_dev *rdev,
				 int min_uV, int max_uV)
{
	int ret, voltage;

	/* Allow uV_step to be 0 for fixed voltage */
	if (rdev->desc->n_voltages == 1 && rdev->desc->uV_step == 0) {
		if (min_uV <= rdev->desc->min_uV && rdev->desc->min_uV <= max_uV)
			return 0;
		else
			return -EINVAL;
	}

	if (!rdev->desc->uV_step) {
		BUG_ON(!rdev->desc->uV_step);
		return -EINVAL;
	}

	if (min_uV < rdev->desc->min_uV)
		min_uV = rdev->desc->min_uV;

	ret = DIV_ROUND_UP(min_uV - rdev->desc->min_uV, rdev->desc->uV_step);
	if (ret < 0)
		return ret;

	/* Map back into a voltage to verify we're still in bounds */
	voltage = rdev->desc->ops->list_voltage(rdev, ret);
	if (voltage < min_uV || voltage > max_uV)
		return -EINVAL;

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_map_voltage_linear);

static int _regulator_do_set_voltage(struct regulator_dev *rdev,
				     int min_uV, int max_uV)
{
	int ret;
	int delay = 0;
	int best_val = 0;
	unsigned int selector;
	int old_selector = -1;

	trace_regulator_set_voltage(rdev_get_name(rdev), min_uV, max_uV);

	min_uV += rdev->constraints->uV_offset;
	max_uV += rdev->constraints->uV_offset;

	/*
	 * If we can't obtain the old selector there is not enough
	 * info to call set_voltage_time_sel().
	 */
	if (_regulator_is_enabled(rdev) &&
	    rdev->desc->ops->set_voltage_time_sel &&
	    rdev->desc->ops->get_voltage_sel) {
		old_selector = rdev->desc->ops->get_voltage_sel(rdev);
		if (old_selector < 0)
			return old_selector;
	}

	if (rdev->desc->ops->set_voltage) {
		ret = rdev->desc->ops->set_voltage(rdev, min_uV, max_uV,
						   &selector);

		if (ret >= 0) {
			if (rdev->desc->ops->list_voltage)
				best_val = rdev->desc->ops->list_voltage(rdev,
									 selector);
			else
				best_val = _regulator_get_voltage(rdev);
		}

	} else if (rdev->desc->ops->set_voltage_sel) {
		if (rdev->desc->ops->map_voltage) {
			ret = rdev->desc->ops->map_voltage(rdev, min_uV,
							   max_uV);
		} else {
			if (rdev->desc->ops->list_voltage ==
			    regulator_list_voltage_linear)
				ret = regulator_map_voltage_linear(rdev,
								min_uV, max_uV);
			else
				ret = regulator_map_voltage_iterate(rdev,
								min_uV, max_uV);
		}

		if (ret >= 0) {
			best_val = rdev->desc->ops->list_voltage(rdev, ret);
			if (min_uV <= best_val && max_uV >= best_val) {
				selector = ret;
				ret = rdev->desc->ops->set_voltage_sel(rdev,
								       ret);
			} else {
				ret = -EINVAL;
			}
		}
	} else {
		ret = -EINVAL;
	}

	/* Call set_voltage_time_sel if successfully obtained old_selector */
	if (ret == 0 && _regulator_is_enabled(rdev) && old_selector >= 0 &&
	    rdev->desc->ops->set_voltage_time_sel) {

		delay = rdev->desc->ops->set_voltage_time_sel(rdev,
						old_selector, selector);
		if (delay < 0) {
			rdev_warn(rdev, "set_voltage_time_sel() failed: %d\n",
				  delay);
			delay = 0;
		}

		/* Insert any necessary delays */
		if (delay >= 1000) {
			mdelay(delay / 1000);
			udelay(delay % 1000);
		} else if (delay) {
			udelay(delay);
		}
	}

	if (ret == 0 && best_val >= 0)
		_notifier_call_chain(rdev, REGULATOR_EVENT_VOLTAGE_CHANGE,
				     (void *)best_val);

	trace_regulator_set_voltage_complete(rdev_get_name(rdev), best_val);

	return ret;
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
	int ret = 0;

	mutex_lock(&rdev->mutex);

	/* If we're setting the same range as last time the change
	 * should be a noop (some cpufreq implementations use the same
	 * voltage for multiple frequencies, for example).
	 */
	if (regulator->min_uV == min_uV && regulator->max_uV == max_uV)
		goto out;

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
	regulator->min_uV = min_uV;
	regulator->max_uV = max_uV;

	ret = regulator_check_consumers(rdev, &min_uV, &max_uV);
	if (ret < 0)
		goto out;

	ret = _regulator_do_set_voltage(rdev, min_uV, max_uV);

out:
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_voltage);

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
	struct regulator_dev	*rdev = regulator->rdev;
	struct regulator_ops	*ops = rdev->desc->ops;
	int old_sel = -1;
	int new_sel = -1;
	int voltage;
	int i;

	/* Currently requires operations to do this */
	if (!ops->list_voltage || !ops->set_voltage_time_sel
	    || !rdev->desc->n_voltages)
		return -EINVAL;

	for (i = 0; i < rdev->desc->n_voltages; i++) {
		/* We only look for exact voltage matches here */
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
 *regulator_set_voltage_time_sel - get raise/fall time
 * @regulator: regulator source
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
	unsigned int ramp_delay = 0;
	int old_volt, new_volt;

	if (rdev->constraints->ramp_delay)
		ramp_delay = rdev->constraints->ramp_delay;
	else if (rdev->desc->ramp_delay)
		ramp_delay = rdev->desc->ramp_delay;

	if (ramp_delay == 0) {
		rdev_warn(rdev, "ramp_delay not set\n");
		return 0;
	}

	/* sanity check */
	if (!rdev->desc->ops->list_voltage)
		return -EINVAL;

	old_volt = rdev->desc->ops->list_voltage(rdev, old_selector);
	new_volt = rdev->desc->ops->list_voltage(rdev, new_selector);

	return DIV_ROUND_UP(abs(new_volt - old_volt), ramp_delay);
}
EXPORT_SYMBOL_GPL(regulator_set_voltage_time_sel);

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
	int ret, min_uV, max_uV;

	mutex_lock(&rdev->mutex);

	if (!rdev->desc->ops->set_voltage &&
	    !rdev->desc->ops->set_voltage_sel) {
		ret = -EINVAL;
		goto out;
	}

	/* This is only going to work if we've had a voltage configured. */
	if (!regulator->min_uV && !regulator->max_uV) {
		ret = -EINVAL;
		goto out;
	}

	min_uV = regulator->min_uV;
	max_uV = regulator->max_uV;

	/* This should be a paranoia check... */
	ret = regulator_check_voltage(rdev, &min_uV, &max_uV);
	if (ret < 0)
		goto out;

	ret = regulator_check_consumers(rdev, &min_uV, &max_uV);
	if (ret < 0)
		goto out;

	ret = _regulator_do_set_voltage(rdev, min_uV, max_uV);

out:
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_sync_voltage);

static int _regulator_get_voltage(struct regulator_dev *rdev)
{
	int sel, ret;

	if (rdev->desc->ops->get_voltage_sel) {
		sel = rdev->desc->ops->get_voltage_sel(rdev);
		if (sel < 0)
			return sel;
		ret = rdev->desc->ops->list_voltage(rdev, sel);
	} else if (rdev->desc->ops->get_voltage) {
		ret = rdev->desc->ops->get_voltage(rdev);
	} else {
		return -EINVAL;
	}

	if (ret < 0)
		return ret;
	return ret - rdev->constraints->uV_offset;
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
	int regulator_curr_mode;

	mutex_lock(&rdev->mutex);

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
	int ret, output_uV, input_uV = 0, total_uA_load = 0;
	unsigned int mode;

	if (rdev->supply)
		input_uV = regulator_get_voltage(rdev->supply);

	mutex_lock(&rdev->mutex);

	/*
	 * first check to see if we can set modes at all, otherwise just
	 * tell the consumer everything is OK.
	 */
	regulator->uA_load = uA_load;
	ret = regulator_check_drms(rdev);
	if (ret < 0) {
		ret = 0;
		goto out;
	}

	if (!rdev->desc->ops->get_optimum_mode)
		goto out;

	/*
	 * we can actually do this so any errors are indicators of
	 * potential real failure.
	 */
	ret = -EINVAL;

	if (!rdev->desc->ops->set_mode)
		goto out;

	/* get output voltage */
	output_uV = _regulator_get_voltage(rdev);
	if (output_uV <= 0) {
		rdev_err(rdev, "invalid output voltage found\n");
		goto out;
	}

	/* No supply? Use constraint voltage */
	if (input_uV <= 0)
		input_uV = rdev->constraints->input_uV;
	if (input_uV <= 0) {
		rdev_err(rdev, "invalid input voltage found\n");
		goto out;
	}

	/* calc total requested load for this regulator */
	list_for_each_entry(consumer, &rdev->consumer_list, list)
		total_uA_load += consumer->uA_load;

	mode = rdev->desc->ops->get_optimum_mode(rdev,
						 input_uV, output_uV,
						 total_uA_load);
	ret = regulator_mode_constrain(rdev, &mode);
	if (ret < 0) {
		rdev_err(rdev, "failed to get optimum mode @ %d uA %d -> %d uV\n",
			 total_uA_load, input_uV, output_uV);
		goto out;
	}

	ret = rdev->desc->ops->set_mode(rdev, mode);
	if (ret < 0) {
		rdev_err(rdev, "failed to set optimum mode %x\n", mode);
		goto out;
	}
	ret = mode;
out:
	mutex_unlock(&rdev->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_set_optimum_mode);

/**
 * regulator_allow_bypass - allow the regulator to go into bypass mode
 *
 * @regulator: Regulator to configure
 * @allow: enable or disable bypass mode
 *
 * Allow the regulator to go into bypass mode if all other consumers
 * for the regulator also enable bypass mode and the machine
 * constraints allow this.  Bypass mode means that the regulator is
 * simply passing the input directly to the output with no regulation.
 */
int regulator_allow_bypass(struct regulator *regulator, bool enable)
{
	struct regulator_dev *rdev = regulator->rdev;
	int ret = 0;

	if (!rdev->desc->ops->set_bypass)
		return 0;

	if (rdev->constraints &&
	    !(rdev->constraints->valid_ops_mask & REGULATOR_CHANGE_BYPASS))
		return 0;

	mutex_lock(&rdev->mutex);

	if (enable && !regulator->bypass) {
		rdev->bypass_count++;

		if (rdev->bypass_count == rdev->open_count) {
			ret = rdev->desc->ops->set_bypass(rdev, enable);
			if (ret != 0)
				rdev->bypass_count--;
		}

	} else if (!enable && regulator->bypass) {
		rdev->bypass_count--;

		if (rdev->bypass_count != rdev->open_count) {
			ret = rdev->desc->ops->set_bypass(rdev, enable);
			if (ret != 0)
				rdev->bypass_count++;
		}
	}

	if (ret == 0)
		regulator->bypass = enable;

	mutex_unlock(&rdev->mutex);

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
static void _notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data)
{
	/* call rdev chain first */
	blocking_notifier_call_chain(&rdev->notifier, event, data);
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
	while (--i >= 0)
		regulator_put(consumers[i].consumer);

	return ret;
}
EXPORT_SYMBOL_GPL(regulator_bulk_get);

/**
 * devm_regulator_bulk_get - managed get multiple regulator consumers
 *
 * @dev:           Device to supply
 * @num_consumers: Number of consumers to register
 * @consumers:     Configuration of consumers; clients are stored here.
 *
 * @return 0 on success, an errno on failure.
 *
 * This helper function allows drivers to get several regulator
 * consumers in one operation with management, the regulators will
 * automatically be freed when the device is unbound.  If any of the
 * regulators cannot be acquired then any regulators that were
 * allocated will be freed before returning to the caller.
 */
int devm_regulator_bulk_get(struct device *dev, int num_consumers,
			    struct regulator_bulk_data *consumers)
{
	int i;
	int ret;

	for (i = 0; i < num_consumers; i++)
		consumers[i].consumer = NULL;

	for (i = 0; i < num_consumers; i++) {
		consumers[i].consumer = devm_regulator_get(dev,
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
		devm_regulator_put(consumers[i].consumer);

	return ret;
}
EXPORT_SYMBOL_GPL(devm_regulator_bulk_get);

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
		if (consumers[i].consumer->always_on)
			consumers[i].ret = 0;
		else
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
	pr_err("Failed to enable %s: %d\n", consumers[i].supply, ret);
	while (--i >= 0)
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
	pr_err("Failed to disable %s: %d\n", consumers[i].supply, ret);
	for (++i; i < num_consumers; ++i) {
		r = regulator_enable(consumers[i].consumer);
		if (r != 0)
			pr_err("Failed to reename %s: %d\n",
			       consumers[i].supply, r);
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
	int ret;

	for (i = 0; i < num_consumers; i++)
		consumers[i].ret =
			    regulator_force_disable(consumers[i].consumer);

	for (i = 0; i < num_consumers; i++) {
		if (consumers[i].ret != 0) {
			ret = consumers[i].ret;
			goto out;
		}
	}

	return 0;
out:
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
	case REGULATOR_MODE_STANDBY:
		return REGULATOR_STATUS_STANDBY;
	default:
		return REGULATOR_STATUS_UNDEFINED;
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
	if ((ops->get_voltage && ops->get_voltage(rdev) >= 0) ||
	    (ops->get_voltage_sel && ops->get_voltage_sel(rdev) >= 0)) {
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
	if (ops->get_bypass) {
		status = device_create_file(dev, &dev_attr_bypass);
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
	if (ops->set_voltage || ops->set_voltage_sel) {
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

static void rdev_init_debugfs(struct regulator_dev *rdev)
{
	rdev->debugfs = debugfs_create_dir(rdev_get_name(rdev), debugfs_root);
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

/**
 * regulator_register - register regulator
 * @regulator_desc: regulator to register
 * @config: runtime configuration for regulator
 *
 * Called by regulator drivers to register a regulator.
 * Returns 0 on success.
 */
struct regulator_dev *
regulator_register(const struct regulator_desc *regulator_desc,
		   const struct regulator_config *config)
{
	const struct regulation_constraints *constraints = NULL;
	const struct regulator_init_data *init_data;
	static atomic_t regulator_no = ATOMIC_INIT(0);
	struct regulator_dev *rdev;
	struct device *dev;
	int ret, i;
	const char *supply = NULL;

	if (regulator_desc == NULL || config == NULL)
		return ERR_PTR(-EINVAL);

	dev = config->dev;
	WARN_ON(!dev);

	if (regulator_desc->name == NULL || regulator_desc->ops == NULL)
		return ERR_PTR(-EINVAL);

	if (regulator_desc->type != REGULATOR_VOLTAGE &&
	    regulator_desc->type != REGULATOR_CURRENT)
		return ERR_PTR(-EINVAL);

	/* Only one of each should be implemented */
	WARN_ON(regulator_desc->ops->get_voltage &&
		regulator_desc->ops->get_voltage_sel);
	WARN_ON(regulator_desc->ops->set_voltage &&
		regulator_desc->ops->set_voltage_sel);

	/* If we're using selectors we must implement list_voltage. */
	if (regulator_desc->ops->get_voltage_sel &&
	    !regulator_desc->ops->list_voltage) {
		return ERR_PTR(-EINVAL);
	}
	if (regulator_desc->ops->set_voltage_sel &&
	    !regulator_desc->ops->list_voltage) {
		return ERR_PTR(-EINVAL);
	}

	init_data = config->init_data;

	rdev = kzalloc(sizeof(struct regulator_dev), GFP_KERNEL);
	if (rdev == NULL)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&regulator_list_mutex);

	mutex_init(&rdev->mutex);
	rdev->reg_data = config->driver_data;
	rdev->owner = regulator_desc->owner;
	rdev->desc = regulator_desc;
	if (config->regmap)
		rdev->regmap = config->regmap;
	else
		rdev->regmap = dev_get_regmap(dev, NULL);
	INIT_LIST_HEAD(&rdev->consumer_list);
	INIT_LIST_HEAD(&rdev->list);
	BLOCKING_INIT_NOTIFIER_HEAD(&rdev->notifier);
	INIT_DELAYED_WORK(&rdev->disable_work, regulator_disable_work);

	/* preform any regulator specific init */
	if (init_data && init_data->regulator_init) {
		ret = init_data->regulator_init(rdev->reg_data);
		if (ret < 0)
			goto clean;
	}

	/* register with sysfs */
	rdev->dev.class = &regulator_class;
	rdev->dev.of_node = config->of_node;
	rdev->dev.parent = dev;
	dev_set_name(&rdev->dev, "regulator.%d",
		     atomic_inc_return(&regulator_no) - 1);
	ret = device_register(&rdev->dev);
	if (ret != 0) {
		put_device(&rdev->dev);
		goto clean;
	}

	dev_set_drvdata(&rdev->dev, rdev);

	if (config->ena_gpio && gpio_is_valid(config->ena_gpio)) {
		ret = gpio_request_one(config->ena_gpio,
				       GPIOF_DIR_OUT | config->ena_gpio_flags,
				       rdev_get_name(rdev));
		if (ret != 0) {
			rdev_err(rdev, "Failed to request enable GPIO%d: %d\n",
				 config->ena_gpio, ret);
			goto clean;
		}

		rdev->ena_gpio = config->ena_gpio;
		rdev->ena_gpio_invert = config->ena_gpio_invert;

		if (config->ena_gpio_flags & GPIOF_OUT_INIT_HIGH)
			rdev->ena_gpio_state = 1;

		if (rdev->ena_gpio_invert)
			rdev->ena_gpio_state = !rdev->ena_gpio_state;
	}

	/* set regulator constraints */
	if (init_data)
		constraints = &init_data->constraints;

	ret = set_machine_constraints(rdev, constraints);
	if (ret < 0)
		goto scrub;

	/* add attributes supported by this regulator */
	ret = add_regulator_attributes(rdev);
	if (ret < 0)
		goto scrub;

	if (init_data && init_data->supply_regulator)
		supply = init_data->supply_regulator;
	else if (regulator_desc->supply_name)
		supply = regulator_desc->supply_name;

	if (supply) {
		struct regulator_dev *r;

		r = regulator_dev_lookup(dev, supply, &ret);

		if (!r) {
			dev_err(dev, "Failed to find supply %s\n", supply);
			ret = -EPROBE_DEFER;
			goto scrub;
		}

		ret = set_supply(rdev, r);
		if (ret < 0)
			goto scrub;

		/* Enable supply if rail is enabled */
		if (_regulator_is_enabled(rdev)) {
			ret = regulator_enable(rdev->supply);
			if (ret < 0)
				goto scrub;
		}
	}

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

	list_add(&rdev->list, &regulator_list);

	rdev_init_debugfs(rdev);
out:
	mutex_unlock(&regulator_list_mutex);
	return rdev;

unset_supplies:
	unset_regulator_supplies(rdev);

scrub:
	if (rdev->supply)
		regulator_put(rdev->supply);
	if (rdev->ena_gpio)
		gpio_free(rdev->ena_gpio);
	kfree(rdev->constraints);
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

	if (rdev->supply)
		regulator_put(rdev->supply);
	mutex_lock(&regulator_list_mutex);
	debugfs_remove_recursive(rdev->debugfs);
	flush_work_sync(&rdev->disable_work.work);
	WARN_ON(rdev->open_count);
	unset_regulator_supplies(rdev);
	list_del(&rdev->list);
	kfree(rdev->constraints);
	if (rdev->ena_gpio)
		gpio_free(rdev->ena_gpio);
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
			rdev_err(rdev, "failed to prepare\n");
			goto out;
		}
	}
out:
	mutex_unlock(&regulator_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_suspend_prepare);

/**
 * regulator_suspend_finish - resume regulators from system wide suspend
 *
 * Turn on regulators that might be turned off by regulator_suspend_prepare
 * and that should be turned on according to the regulators properties.
 */
int regulator_suspend_finish(void)
{
	struct regulator_dev *rdev;
	int ret = 0, error;

	mutex_lock(&regulator_list_mutex);
	list_for_each_entry(rdev, &regulator_list, list) {
		struct regulator_ops *ops = rdev->desc->ops;

		mutex_lock(&rdev->mutex);
		if ((rdev->use_count > 0  || rdev->constraints->always_on) &&
				ops->enable) {
			error = ops->enable(rdev);
			if (error)
				ret = error;
		} else {
			if (!has_full_constraints)
				goto unlock;
			if (!ops->disable)
				goto unlock;
			if (!_regulator_is_enabled(rdev))
				goto unlock;

			error = ops->disable(rdev);
			if (error)
				ret = error;
		}
unlock:
		mutex_unlock(&rdev->mutex);
	}
	mutex_unlock(&regulator_list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(regulator_suspend_finish);

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
 * regulator_use_dummy_regulator - Provide a dummy regulator when none is found
 *
 * Calling this function will cause the regulator API to provide a
 * dummy regulator to consumers if no physical regulator is found,
 * allowing most consumers to proceed as though a regulator were
 * configured.  This allows systems such as those with software
 * controllable regulators for the CPU core only to be brought up more
 * readily.
 */
void regulator_use_dummy_regulator(void)
{
	board_wants_dummy_regulator = true;
}
EXPORT_SYMBOL_GPL(regulator_use_dummy_regulator);

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

#ifdef CONFIG_DEBUG_FS
static ssize_t supply_map_read_file(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	char *buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	ssize_t len, ret = 0;
	struct regulator_map *map;

	if (!buf)
		return -ENOMEM;

	list_for_each_entry(map, &regulator_map_list, list) {
		len = snprintf(buf + ret, PAGE_SIZE - ret,
			       "%s -> %s.%s\n",
			       rdev_get_name(map->regulator), map->dev_name,
			       map->supply);
		if (len >= 0)
			ret += len;
		if (ret > PAGE_SIZE) {
			ret = PAGE_SIZE;
			break;
		}
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return ret;
}
#endif

static const struct file_operations supply_map_fops = {
#ifdef CONFIG_DEBUG_FS
	.read = supply_map_read_file,
	.llseek = default_llseek,
#endif
};

static int __init regulator_init(void)
{
	int ret;

	ret = class_register(&regulator_class);

	debugfs_root = debugfs_create_dir("regulator", NULL);
	if (!debugfs_root)
		pr_warn("regulator: Failed to create debugfs directory\n");

	debugfs_create_file("supply_map", 0444, debugfs_root, NULL,
			    &supply_map_fops);

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

	/*
	 * Since DT doesn't provide an idiomatic mechanism for
	 * enabling full constraints and since it's much more natural
	 * with DT to provide them just assume that a DT enabled
	 * system has full constraints.
	 */
	if (of_have_populated_dt())
		has_full_constraints = true;

	mutex_lock(&regulator_list_mutex);

	/* If we have a full configuration then disable any regulators
	 * which are not in use or always_on.  This will become the
	 * default behaviour in the future.
	 */
	list_for_each_entry(rdev, &regulator_list, list) {
		ops = rdev->desc->ops;
		c = rdev->constraints;

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
			rdev_info(rdev, "disabling\n");
			ret = ops->disable(rdev);
			if (ret != 0) {
				rdev_err(rdev, "couldn't disable: %d\n", ret);
			}
		} else {
			/* The intention is that in future we will
			 * assume that full constraints are provided
			 * so warn even if we aren't going to do
			 * anything here.
			 */
			rdev_warn(rdev, "incomplete constraints, leaving on\n");
		}

unlock:
		mutex_unlock(&rdev->mutex);
	}

	mutex_unlock(&regulator_list_mutex);

	return 0;
}
late_initcall(regulator_init_complete);
