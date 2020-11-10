// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/coupler.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/debug-regulator.h>

#include <trace/events/power.h>

#include "internal.h"

struct debug_regulator {
	struct list_head	list;
	struct regulator	*reg;
	struct device		*dev;
	struct regulator_dev	*rdev;
};

static DEFINE_MUTEX(debug_reg_list_lock);
static LIST_HEAD(debug_reg_list);

static const char *rdev_name(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->name)
		return rdev->constraints->name;
	else if (rdev->desc->name)
		return rdev->desc->name;
	else
		return "";
}

#define dreg_err(dreg, fmt, ...)					\
	pr_err("%s: %s: " fmt, __func__, rdev_name((dreg)->rdev), ##__VA_ARGS__)
#define dreg_dbg(dreg, fmt, ...)					\
	pr_debug("%s: %s: " fmt, __func__, rdev_name((dreg)->rdev),	\
		##__VA_ARGS__)

static struct regulator *reg_debug_get_consumer(struct debug_regulator *dreg)
{
	struct regulator *regulator;

	if (dreg->reg)
		return dreg->reg;

	regulator = regulator_get(NULL, rdev_name(dreg->rdev));
	if (IS_ERR(regulator)) {
		dreg_dbg(dreg, "debug regulator get failed, ret=%ld\n",
			PTR_ERR(regulator));
		return NULL;
	}
	dreg->reg = regulator;

	return regulator;
}

static int reg_debug_enable_get(void *data, u64 *val)
{
	struct debug_regulator *dreg = data;
	struct regulator *regulator = reg_debug_get_consumer(dreg);

	if (!regulator) {
		dreg_err(dreg, "debug consumer missing\n");
		return -ENODEV;
	}

	*val = regulator_is_enabled(regulator);

	return 0;
}

static int reg_debug_enable_set(void *data, u64 val)
{
	struct debug_regulator *dreg = data;
	struct regulator *regulator = reg_debug_get_consumer(dreg);
	int ret;

	if (!regulator) {
		dreg_err(dreg, "debug consumer missing\n");
		return -ENODEV;
	}

	if (val) {
		ret = regulator_enable(regulator);
		if (ret)
			dreg_err(dreg, "enable failed, ret=%d\n", ret);
	} else {
		ret = regulator_disable(regulator);
		if (ret)
			dreg_err(dreg, "disable failed, ret=%d\n", ret);
	}

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(reg_enable_fops, reg_debug_enable_get,
			reg_debug_enable_set, "%llu\n");

static int reg_debug_bypass_enable_get(void *data, u64 *val)
{
	struct debug_regulator *dreg = data;
	struct regulator_dev *rdev = dreg->rdev;
	bool enable = false;
	int ret = 0;

	ww_mutex_lock(&rdev->mutex, NULL);
	if (rdev->desc->ops->get_bypass) {
		ret = rdev->desc->ops->get_bypass(rdev, &enable);
		if (ret)
			dreg_err(dreg, "get_bypass() failed, ret=%d\n", ret);
	} else {
		enable = (rdev->bypass_count == rdev->open_count);
	}
	ww_mutex_unlock(&rdev->mutex);

	*val = enable;

	return ret;
}

static int reg_debug_bypass_enable_set(void *data, u64 val)
{
	struct debug_regulator *dreg = data;
	struct regulator *regulator = reg_debug_get_consumer(dreg);

	if (!regulator) {
		dreg_err(dreg, "debug consumer missing\n");
		return -ENODEV;
	}

	return regulator_allow_bypass(regulator, val);
}
DEFINE_DEBUGFS_ATTRIBUTE(reg_bypass_enable_fops, reg_debug_bypass_enable_get,
			reg_debug_bypass_enable_set, "%llu\n");

static int reg_debug_force_disable_set(void *data, u64 val)
{
	struct debug_regulator *dreg = data;
	struct regulator *regulator = reg_debug_get_consumer(dreg);
	int ret = 0;

	if (!regulator) {
		dreg_err(dreg, "debug consumer missing\n");
		return -ENODEV;
	}

	if (val) {
		ret = regulator_force_disable(regulator);
		if (ret)
			dreg_err(dreg, "force_disable failed, ret=%d\n", ret);
	}

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(reg_force_disable_fops, reg_debug_enable_get,
			reg_debug_force_disable_set, "%llu\n");

#define MAX_DEBUG_BUF_LEN 50

static ssize_t reg_debug_voltage_read(struct file *file, char __user *ubuf,
					size_t count, loff_t *ppos)
{
	struct debug_regulator *dreg = file->private_data;
	struct regulator *regulator = reg_debug_get_consumer(dreg);
	char buf[MAX_DEBUG_BUF_LEN];
	int voltage, ret;

	if (!regulator) {
		dreg_err(dreg, "debug consumer missing\n");
		return -ENODEV;
	}

	voltage = regulator_get_voltage(regulator);
	ret = scnprintf(buf, MAX_DEBUG_BUF_LEN, "%d\n", voltage);

	return simple_read_from_buffer(ubuf, count, ppos, buf, ret);
}

static ssize_t reg_debug_voltage_write(struct file *file,
			const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct debug_regulator *dreg = file->private_data;
	struct regulator *regulator = reg_debug_get_consumer(dreg);
	char buf[MAX_DEBUG_BUF_LEN];
	int ret, filled;
	int min_uV, max_uV = -1;

	if (!regulator) {
		dreg_err(dreg, "debug consumer missing\n");
		return -ENODEV;
	}

	if (count < MAX_DEBUG_BUF_LEN) {
		if (copy_from_user(buf, ubuf, count))
			return -EFAULT;

		buf[count] = '\0';
		filled = sscanf(buf, "%d %d", &min_uV, &max_uV);

		/* Check that both min and max voltage were specified. */
		if (filled < 2 || min_uV < 0 || max_uV < min_uV) {
			dreg_err(dreg, "incorrect values specified: \"%s\"; should be: \"min_uV max_uV\"\n",
				buf);
			return -EINVAL;
		}

		ret = regulator_set_voltage(regulator, min_uV, max_uV);
		if (ret) {
			dreg_err(dreg, "set_voltage(%d, %d) failed, ret=%d\n",
				min_uV, max_uV, ret);
			return ret;
		}
	} else {
		dreg_err(dreg, "voltage request string exceeds maximum buffer size\n");
		return -EINVAL;
	}

	return count;
}

static const struct file_operations reg_voltage_fops = {
	.open	= simple_open,
	.read	= reg_debug_voltage_read,
	.write	= reg_debug_voltage_write,
};

static int reg_debug_mode_get(void *data, u64 *val)
{
	struct debug_regulator *dreg = data;
	struct regulator *regulator = reg_debug_get_consumer(dreg);
	int mode;

	if (!regulator) {
		dreg_err(dreg, "debug consumer missing\n");
		return -ENODEV;
	}

	mode = regulator_get_mode(regulator);
	if (mode < 0) {
		dreg_err(dreg, "get mode failed, ret=%d\n", mode);
		return mode;
	}

	*val = mode;

	return 0;
}

static int reg_debug_mode_set(void *data, u64 val)
{
	struct debug_regulator *dreg = data;
	struct regulator *regulator = reg_debug_get_consumer(dreg);
	unsigned int mode = val;
	int ret;

	if (!regulator) {
		dreg_err(dreg, "debug consumer missing\n");
		return -ENODEV;
	}

	ret = regulator_set_mode(regulator, mode);
	if (ret)
		dreg_err(dreg, "set mode=%u failed, ret=%d\n", mode, ret);

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(reg_mode_fops, reg_debug_mode_get, reg_debug_mode_set,
			"%llu\n");

static int reg_debug_set_load(void *data, u64 val)
{
	struct debug_regulator *dreg = data;
	struct regulator *regulator = reg_debug_get_consumer(dreg);
	int load = val;
	int ret;

	if (!regulator) {
		dreg_err(dreg, "debug consumer missing\n");
		return -ENODEV;
	}

	ret = regulator_set_load(regulator, load);
	if (ret)
		dreg_err(dreg, "set load=%d failed, ret=%d\n", load, ret);

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(reg_set_load_fops, reg_debug_mode_get,
			reg_debug_set_load, "%llu\n");

static int reg_debug_consumers_show(struct seq_file *m, void *v)
{
	struct regulator_dev *rdev = m->private;
	struct regulator *reg;
	const char *supply_name;

	ww_mutex_lock(&rdev->mutex, NULL);

	/* Print a header if there are consumers. */
	if (rdev->open_count)
		seq_printf(m, "%-32s EN    Min_uV   Max_uV  load_uA\n",
			"Device-Supply");

	list_for_each_entry(reg, &rdev->consumer_list, list) {
		if (reg->supply_name)
			supply_name = reg->supply_name;
		else
			supply_name = "(null)-(null)";

		seq_printf(m, "%-32s %c   %8d %8d %8d\n", supply_name,
			(reg->enable_count ? 'Y' : 'N'),
			reg->voltage[PM_SUSPEND_ON].min_uV,
			reg->voltage[PM_SUSPEND_ON].max_uV,
			reg->uA_load);
	}

	ww_mutex_unlock(&rdev->mutex);

	return 0;
}

static int reg_debug_consumers_open(struct inode *inode, struct file *file)
{
	return single_open(file, reg_debug_consumers_show, inode->i_private);
}

static const struct file_operations reg_consumers_fops = {
	.open		= reg_debug_consumers_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * regulator_debug_add() - register a debug regulator for the specified
 *				regulator
 * @dev:		Device pointer of the regulator
 * @rdev:		Regulator dev pointer of the regulator
 *
 * This function adds various debugfs files for the 'rdev' regulator which
 * provide a mechanism for userspace to vote and monitor the state of the
 * regulator.  When one of these debugfs files is accessed,
 * reg_debug_get_consumer() is called which uses regulator_get() to get a handle
 * to the regulator.
 *
 * Returns 0 on success or an errno on failure.
 */
static struct debug_regulator *regulator_debug_add(struct device *dev,
						   struct regulator_dev *rdev)
{
	struct debug_regulator *dreg = NULL;
	const struct regulator_ops *ops;
	struct dentry *dir;
	mode_t mode;

	if (!dev || !rdev) {
		pr_err("%s: dev or rdev is NULL\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	dreg = kzalloc(sizeof(*dreg), GFP_KERNEL);
	if (!dreg)
		return ERR_PTR(-ENOMEM);

	dreg->dev = dev;
	dreg->rdev = rdev;

	mutex_lock(&debug_reg_list_lock);
	list_add(&dreg->list, &debug_reg_list);
	mutex_unlock(&debug_reg_list_lock);

	ops = rdev->desc->ops;
	dir = rdev->debugfs;

	debugfs_create_file_unsafe("enable", 0644, dir, dreg, &reg_enable_fops);
	if (ops->set_bypass)
		debugfs_create_file_unsafe("bypass", 0644, dir, dreg,
						&reg_bypass_enable_fops);

	mode = 0;
	if (ops->is_enabled)
		mode |= 0444;
	if (ops->disable)
		mode |= 0200;
	if (mode)
		debugfs_create_file_unsafe("force_disable", mode, dir, dreg,
						&reg_force_disable_fops);

	mode = 0;
	if (ops->get_voltage || ops->get_voltage_sel)
		mode |= 0444;
	if (ops->set_voltage || ops->set_voltage_sel)
		mode |= 0200;
	if (mode)
		debugfs_create_file_unsafe("voltage", mode, dir, dreg,
						&reg_voltage_fops);

	mode = 0;
	if (ops->get_mode)
		mode |= 0444;
	if (ops->set_mode)
		mode |= 0200;
	if (mode)
		debugfs_create_file_unsafe("mode", mode, dir, dreg,
						&reg_mode_fops);

	mode = 0;
	if (ops->get_mode)
		mode |= 0444;
	if (ops->set_load || (ops->get_optimum_mode && ops->set_mode))
		mode |= 0200;
	if (mode)
		debugfs_create_file_unsafe("load", mode, dir, dreg,
						&reg_set_load_fops);

	debugfs_create_file("consumers", 0444, dir, rdev, &reg_consumers_fops);

	return dreg;
}

/**
 * regulator_debug_register() - register a debug regulator for the specified
 *				regulator
 * @dev:		Device pointer of the regulator
 * @rdev:		Regulator dev pointer of the regulator
 *
 * This function calls regulator_debug_add() which adds several debugfs files
 * for the 'rdev' regulator which allow for userspace regulator state voting and
 * monitoring.
 *
 * Returns 0 on success or an errno on failure.
 */
int regulator_debug_register(struct device *dev, struct regulator_dev *rdev)
{
	return PTR_ERR_OR_ZERO(regulator_debug_add(dev, rdev));
}
EXPORT_SYMBOL(regulator_debug_register);

/* debug_reg_list_lock must be held by caller. */
static void regulator_debug_remove(struct debug_regulator *dreg)
{
	regulator_put(dreg->reg);
	list_del(&dreg->list);
	kfree(dreg);
}

/**
 * regulator_debug_unregister() - unregister the debug regulator associated with
 *				  a regulator
 * @rdev:		Regulator dev pointer of the regulator
 *
 * This function removes the debugfs consumer registered for 'rdev' and then
 * frees the debug regulator's resources.
 */
void regulator_debug_unregister(struct regulator_dev *rdev)
{
	struct debug_regulator *dreg, *temp;

	if (IS_ERR_OR_NULL(rdev)) {
		pr_err("%s: invalid regulator device pointer\n", __func__);
		return;
	}

	mutex_lock(&debug_reg_list_lock);
	list_for_each_entry_safe(dreg, temp, &debug_reg_list, list) {
		if (dreg->rdev == rdev)
			regulator_debug_remove(dreg);
	}
	mutex_unlock(&debug_reg_list_lock);
}
EXPORT_SYMBOL(regulator_debug_unregister);

/* debug_reg_list_lock must be held by caller. */
static void _devm_regulator_debug_release(struct device *dev, void *res)
{
	struct debug_regulator *dreg = *(struct debug_regulator **)res;
	struct debug_regulator *temp;
	bool found = false;

	/*
	 * The debug regulator may have already been removed due to a
	 * devm_regulator_debug_unregister() call.  Therefore, verify that it is
	 * still in the list before attempting to remove it.
	 */
	list_for_each_entry(temp, &debug_reg_list, list) {
		if (temp == dreg) {
			found = true;
			break;
		}
	}

	if (found)
		regulator_debug_remove(dreg);
}

static void devm_regulator_debug_release(struct device *dev, void *res)
{
	mutex_lock(&debug_reg_list_lock);
	_devm_regulator_debug_release(dev, res);
	mutex_unlock(&debug_reg_list_lock);
}

/**
 * devm_regulator_debug_register() - resource managed version of
 *					     regulator_debug_register()
 * @dev:		Device pointer of the regulator
 * @rdev:		Regulator dev pointer of the regulator
 *
 * This is a resource managed version of regulator_debug_register().
 * The debugfs consumer added via this call is automatically removed via
 * regulator_debug_unregister() on driver detach. See regulator_debug_register()
 * for more details.
 *
 * Returns 0 on success or an errno on failure.
 */
int devm_regulator_debug_register(struct device *dev,
				struct regulator_dev *rdev)
{
	struct debug_regulator *dreg;
	struct debug_regulator **ptr;

	ptr = devres_alloc(devm_regulator_debug_release, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	dreg = regulator_debug_add(dev, rdev);
	if (IS_ERR_OR_NULL(dreg)) {
		devres_free(ptr);
		return PTR_ERR(dreg);
	}

	*ptr = dreg;
	devres_add(dev, ptr);

	return 0;
}
EXPORT_SYMBOL(devm_regulator_debug_register);

static int devm_regulator_debug_match(struct device *dev, void *res, void *data)
{
	struct debug_regulator **dreg = res;

	if (!dreg || !*dreg) {
		WARN_ON(!dreg || !*dreg);
		return 0;
	}

	return *dreg == data;
}

/**
 * devm_regulator_debug_unregister() - resource managed version of
 *					regulator_debug_unregister()
 * @rdev:		Regulator dev pointer of the regulator
 *
 * Deallocate the debug regulator allocated for 'rdev' with
 * devm_regulator_debug_register().  Normally this function will not
 * need to be called and the resource management code will ensure that the
 * resource is freed.
 */
void devm_regulator_debug_unregister(struct regulator_dev *rdev)
{
	struct debug_regulator *dreg, *temp;

	if (IS_ERR_OR_NULL(rdev)) {
		pr_err("%s: invalid regulator device pointer\n", __func__);
		return;
	}

	mutex_lock(&debug_reg_list_lock);
	list_for_each_entry_safe(dreg, temp, &debug_reg_list, list) {
		if (dreg->rdev == rdev)
			devres_release(dreg->dev, _devm_regulator_debug_release,
					devm_regulator_debug_match, dreg);
	}
	mutex_unlock(&debug_reg_list_lock);
}
EXPORT_SYMBOL(devm_regulator_debug_unregister);

static int _regulator_is_enabled(struct regulator_dev *rdev)
{
	if (rdev->ena_pin)
		return rdev->ena_gpio_state;

	if (!rdev->desc->ops->is_enabled)
		return 1;

	return rdev->desc->ops->is_enabled(rdev);
}

static void regulator_debug_print_enabled(struct regulator_dev *rdev)
{
	struct regulator *reg;
	const char *supply_name;
	int mode = -EPERM;
	int uV = -EPERM;

	if (_regulator_is_enabled(rdev) <= 0)
		return;

	uV = regulator_get_voltage_rdev(rdev);

	if (rdev->desc->ops->get_mode)
		mode = rdev->desc->ops->get_mode(rdev);

	if (uV != -EPERM && mode != -EPERM)
		pr_info("%s[%u] %d uV, mode=%d\n",
			rdev_name(rdev), rdev->use_count, uV, mode);
	else if (uV != -EPERM)
		pr_info("%s[%u] %d uV\n",
			rdev_name(rdev), rdev->use_count, uV);
	else if (mode != -EPERM)
		pr_info("%s[%u], mode=%d\n",
			rdev_name(rdev), rdev->use_count, mode);
	else
		pr_info("%s[%u]\n", rdev_name(rdev), rdev->use_count);

	/* Print a header if there are consumers. */
	if (rdev->open_count)
		pr_info("  %-32s EN    Min_uV   Max_uV  load_uA\n",
			"Device-Supply");

	list_for_each_entry(reg, &rdev->consumer_list, list) {
		if (reg->supply_name)
			supply_name = reg->supply_name;
		else
			supply_name = "(null)-(null)";

		pr_info("  %-32s %d   %8d %8d %8d\n", supply_name,
			reg->enable_count,
			reg->voltage[PM_SUSPEND_ON].min_uV,
			reg->voltage[PM_SUSPEND_ON].max_uV,
			reg->uA_load);
	}
}

static void regulator_debug_suspend_trace_probe(void *unused,
					const char *action, int val, bool start)
{
	struct debug_regulator *dreg;

	if (start && val > 0 && !strcmp("machine_suspend", action)) {
		pr_info("Enabled regulators:\n");
		list_for_each_entry(dreg, &debug_reg_list, list)
			regulator_debug_print_enabled(dreg->rdev);
	}
}

static bool debug_suspend;
static struct dentry *regulator_suspend_debugfs;

static int reg_debug_suspend_enable_get(void *data, u64 *val)
{
	*val = debug_suspend;

	return 0;
}

static int reg_debug_suspend_enable_set(void *data, u64 val)
{
	int ret;

	val = !!val;
	if (val == debug_suspend)
		return 0;

	if (val)
		ret = register_trace_suspend_resume(
				regulator_debug_suspend_trace_probe, NULL);
	else
		ret = unregister_trace_suspend_resume(
				regulator_debug_suspend_trace_probe, NULL);
	if (ret) {
		pr_err("%s: Failed to %sregister suspend trace callback, ret=%d\n",
			__func__, val ? "" : "un", ret);
		return ret;
	}
	debug_suspend = val;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(reg_debug_suspend_enable_fops,
	reg_debug_suspend_enable_get, reg_debug_suspend_enable_set, "%llu\n");

static int __init regulator_debug_init(void)
{
	static struct dentry *dir;
	int ret;

	dir = debugfs_lookup("regulator", NULL);
	if (IS_ERR_OR_NULL(dir)) {
		ret = PTR_ERR(dir);
		pr_err("%s: unable to find root regulator debugfs directory, ret=%d\n",
			__func__, ret);
		return 0;
	}

	regulator_suspend_debugfs = debugfs_create_file_unsafe("debug_suspend",
						0644, dir, NULL,
						&reg_debug_suspend_enable_fops);
	dput(dir);
	if (IS_ERR(regulator_suspend_debugfs)) {
		ret = PTR_ERR(regulator_suspend_debugfs);
		pr_err("%s: unable to create regulator debug_suspend debugfs directory, ret=%d\n",
			__func__, ret);
	}

	return 0;
}
module_init(regulator_debug_init);

static void __exit regulator_debug_exit(void)
{
	debugfs_remove(regulator_suspend_debugfs);
	if (debug_suspend)
		unregister_trace_suspend_resume(
				regulator_debug_suspend_trace_probe, NULL);
}
module_exit(regulator_debug_exit);

MODULE_DESCRIPTION("Regulator debug control library");
MODULE_LICENSE("GPL v2");
