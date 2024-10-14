// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024, Intel Corporation
 *
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * Thermal subsystem testing facility.
 *
 * This facility allows the thermal core functionality to be exercised in a
 * controlled way in order to verify its behavior.
 *
 * It resides in the "thermal-testing" directory under the debugfs root and
 * starts with a single file called "command" which can be written a string
 * representing a thermal testing facility command.
 *
 * The currently supported commands are listed in the tt_commands enum below.
 *
 * The "addtz" command causes a new test thermal zone template to be created,
 * for example:
 *
 * # echo addtz > /sys/kernel/debug/thermal-testing/command
 *
 * That template will be represented as a subdirectory in the "thermal-testing"
 * directory, for example
 *
 * # ls /sys/kernel/debug/thermal-testing/
 * command tz0
 *
 * The thermal zone template can be populated with trip points with the help of
 * the "tzaddtrip" command, for example:
 *
 * # echo tzaddtrip:0 > /sys/kernel/debug/thermal-testing/command
 *
 * which causes a trip point template to be added to the test thermal zone
 * template 0 (represented by the tz0 subdirectory in "thermal-testing").
 *
 * # ls /sys/kernel/debug/thermal-testing/tz0
 * init_temp temp trip_0_temp trip_0_hyst
 *
 * The temperature of a trip point template is initially THERMAL_TEMP_INVALID
 * and its hysteresis is initially 0.  They can be adjusted by writing to the
 * "trip_x_temp" and "trip_x_hyst" files correspoinding to that trip point
 * template, respectively.
 *
 * The initial temperature of a thermal zone based on a template can be set by
 * writing to the "init_temp" file in its directory under "thermal-testing", for
 * example:
 *
 * echo 50000 > /sys/kernel/debug/thermal-testing/tz0/init_temp
 *
 * When ready, "tzreg" command can be used for registering and enabling a
 * thermal zone based on a given template with the thermal core, for example
 *
 * # echo tzreg:0 > /sys/kernel/debug/thermal-testing/command
 *
 * In this case, test thermal zone template 0 is used for registering a new
 * thermal zone and the set of trip point templates associated with it is used
 * for populating the new thermal zone's trip points table.  The type of the new
 * thermal zone is "test_tz".
 *
 * The temperature and hysteresis of all of the trip points in that new thermal
 * zone are adjustable via sysfs, so they can be updated at any time.
 *
 * The current temperature of the new thermal zone can be set by writing to the
 * "temp" file in the corresponding thermal zone template's directory under
 * "thermal-testing", for example
 *
 * echo 10000 > /sys/kernel/debug/thermal-testing/tz0/temp
 *
 * which will also trigger a temperature update for this zone in the thermal
 * core, including checking its trip points, sending notifications to user space
 * if any of them have been crossed and so on.
 *
 * When it is not needed any more, a test thermal zone template can be deleted
 * with the help of the "deltz" command, for example
 *
 * # echo deltz:0 > /sys/kernel/debug/thermal-testing/command
 *
 * which will also unregister the thermal zone based on it, if present.
 */

#define pr_fmt(fmt) "thermal-testing: " fmt

#include <linux/debugfs.h>
#include <linux/module.h>

#include "thermal_testing.h"

struct dentry *d_testing;

#define TT_COMMAND_SIZE		16

enum tt_commands {
	TT_CMD_ADDTZ,
	TT_CMD_DELTZ,
	TT_CMD_TZADDTRIP,
	TT_CMD_TZREG,
	TT_CMD_TZUNREG,
};

static const char *tt_command_strings[] = {
	[TT_CMD_ADDTZ] = "addtz",
	[TT_CMD_DELTZ] = "deltz",
	[TT_CMD_TZADDTRIP] = "tzaddtrip",
	[TT_CMD_TZREG] = "tzreg",
	[TT_CMD_TZUNREG] = "tzunreg",
};

static int tt_command_exec(int index, const char *arg)
{
	int ret;

	switch (index) {
	case TT_CMD_ADDTZ:
		ret = tt_add_tz();
		break;

	case TT_CMD_DELTZ:
		ret = tt_del_tz(arg);
		break;

	case TT_CMD_TZADDTRIP:
		ret = tt_zone_add_trip(arg);
		break;

	case TT_CMD_TZREG:
		ret = tt_zone_reg(arg);
		break;

	case TT_CMD_TZUNREG:
		ret = tt_zone_unreg(arg);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static ssize_t tt_command_process(struct dentry *dentry, const char __user *user_buf,
				  size_t count)
{
	char *buf __free(kfree);
	char *arg;
	int i;

	buf = kmalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	buf[count] = '\0';
	strim(buf);

	arg = strstr(buf, ":");
	if (arg) {
		*arg = '\0';
		arg++;
	}

	for (i = 0; i < ARRAY_SIZE(tt_command_strings); i++) {
		if (!strcmp(buf, tt_command_strings[i]))
			return tt_command_exec(i, arg);
	}

	return -EINVAL;
}

static ssize_t tt_command_write(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct dentry *dentry = file->f_path.dentry;
	ssize_t ret;

	if (*ppos)
		return -EINVAL;

	if (count + 1 > TT_COMMAND_SIZE)
		return -E2BIG;

	ret = debugfs_file_get(dentry);
	if (unlikely(ret))
		return ret;

	ret = tt_command_process(dentry, user_buf, count);
	if (ret)
		return ret;

	return count;
}

static const struct file_operations tt_command_fops = {
	.write = tt_command_write,
	.open =	 simple_open,
	.llseek = default_llseek,
};

static int __init thermal_testing_init(void)
{
	d_testing = debugfs_create_dir("thermal-testing", NULL);
	if (!IS_ERR(d_testing))
		debugfs_create_file("command", 0200, d_testing, NULL,
				    &tt_command_fops);

	return 0;
}
module_init(thermal_testing_init);

static void __exit thermal_testing_exit(void)
{
	debugfs_remove(d_testing);
	tt_zone_cleanup();
}
module_exit(thermal_testing_exit);

MODULE_DESCRIPTION("Thermal core testing facility");
MODULE_LICENSE("GPL v2");
