// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "../thermal_core.h"

static struct dentry *thermal_debugfs_parent;
static struct dentry *thermal_debugfs_config;

#define UINT_MAX_CHARACTER 11
static char tzone_sensor_name[THERMAL_NAME_LENGTH] = "";

int buffer_overflow_check(char *buf, int offset, int buf_size)
{
	if ((buf == NULL) || (offset >= buf_size) || (strlen(buf) >= buf_size))
		return -EINVAL;
	return 0;
}

static int fetch_and_populate_trip_data(char *buf, struct thermal_zone_device *tz,
		int idx, int offset, size_t size, bool is_hyst)
{
	int ret = 0, temp;

	if (!is_hyst)
		ret = tz->ops->get_trip_temp(tz, idx, &temp);
	else
		ret = tz->ops->get_trip_hyst(tz, idx, &temp);
	if (ret)
		return ret;
	offset += scnprintf(buf + offset, size - offset, "%d ", temp);

	return offset;
}

static int fetch_and_populate_trips(char *config_buf, struct thermal_zone_device *tz,
		int offset)
{
	size_t buf_size = 0;
	int i = 0, ret = 0;
	int buf1_offset = 0, buf2_offset = 0;
	char *buf_temp = NULL, *buf_hyst = NULL;

	buf_size = tz->num_trips * UINT_MAX_CHARACTER;
	buf_temp = kzalloc(buf_size, GFP_KERNEL);
	buf_hyst = kzalloc(buf_size, GFP_KERNEL);
	if (!buf_hyst || !buf_temp) {
		kfree(buf_temp);
		kfree(buf_hyst);
		return -ENOMEM;
	}

	for (i = 0; i < tz->num_trips; i++) {
		ret = fetch_and_populate_trip_data(buf_temp, tz, i, buf1_offset,
				buf_size, false);
		if (ret < 0)
			goto config_exit;

		buf1_offset = ret;

		if (!tz->ops->get_trip_hyst)
			continue;

		ret = fetch_and_populate_trip_data(buf_hyst, tz, i, buf2_offset,
				buf_size, true);
		if (ret < 0)
			goto config_exit;

		buf2_offset = ret;
	}

	ret = buffer_overflow_check(buf_temp, offset, PAGE_SIZE-offset);
	if (ret)
		goto config_exit;
	offset += scnprintf(config_buf + offset, PAGE_SIZE - offset,
				"%*s%s\n", -15, "set_temp", buf_temp);
	if (buf2_offset) {
		ret = buffer_overflow_check(buf_hyst, offset, PAGE_SIZE-offset);
		if (ret)
			goto config_exit;
		offset += scnprintf(config_buf + offset, PAGE_SIZE - offset,
				"%*s%s\n", -15, "clr_temp", buf_hyst);
	}

config_exit:
	kfree(buf_temp);
	kfree(buf_hyst);
	return (ret < 0) ? ret : offset;
}

static int fetch_and_populate_cdevs(char *config_buf, struct thermal_zone_device *tz,
		int offset)
{
	int ret = 0, i = 0;
	int buf_size = 0, buf_offset = 0, buf1_offset = 0, buf2_offset = 0;
	char *buf_cdev = NULL, *buf_cdev_upper = NULL, *buf_cdev_lower = NULL;
	struct thermal_instance *instance;

	mutex_lock(&tz->lock);
	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (instance->cdev)
			buf_size++;
	}
	if (!buf_size) {
		mutex_unlock(&tz->lock);
		return offset;
	}
	buf_size = (buf_size + tz->num_trips) * THERMAL_NAME_LENGTH;
	buf_cdev =  kzalloc(buf_size, GFP_KERNEL);
	buf_cdev_upper = kzalloc(buf_size, GFP_KERNEL);
	buf_cdev_lower = kzalloc(buf_size, GFP_KERNEL);
	if (!buf_cdev || !buf_cdev_upper || !buf_cdev_lower) {
		mutex_unlock(&tz->lock);
		kfree(buf_cdev);
		kfree(buf_cdev_upper);
		kfree(buf_cdev_lower);
		return -ENOMEM;
	}

	for (i = 0; i < tz->num_trips; i++) {
		bool first_entry = true;
		bool no_cdevs = true;

		list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
			if (!instance->cdev || instance->trip != i)
				continue;

			no_cdevs = false;
			if (first_entry) {
				first_entry = false;
				buf_offset += scnprintf(
						buf_cdev + buf_offset,
						buf_size - buf_offset,
						" %s", instance->cdev->type);
				buf1_offset += scnprintf(
						buf_cdev_upper + buf1_offset,
						buf_size - buf1_offset,
						" %d", instance->upper);
				buf2_offset += scnprintf(
						buf_cdev_lower + buf2_offset,
						buf_size - buf2_offset,
						" %d", instance->lower);
			} else {
				buf_offset += scnprintf(
						buf_cdev + buf_offset,
						buf_size - buf_offset,
						"+%s", instance->cdev->type);
				buf1_offset += scnprintf(
						buf_cdev_upper + buf1_offset,
						buf_size - buf1_offset,
						"+%d", instance->upper);
				buf2_offset += scnprintf(
						buf_cdev_lower + buf2_offset,
						buf_size - buf2_offset,
						"+%d", instance->lower);
			}
		}

		if (no_cdevs) {
			buf_offset += scnprintf(
					buf_cdev + buf_offset,
					buf_size - buf_offset,
					" %s", "-");
			buf1_offset += scnprintf(
					buf_cdev_upper + buf1_offset,
					buf_size - buf1_offset,
					" %s", "-");
			buf2_offset += scnprintf(
					buf_cdev_lower + buf2_offset,
					buf_size - buf2_offset,
					" %s", "-");
		}
	}
	mutex_unlock(&tz->lock);

	ret = buffer_overflow_check(buf_cdev, offset, PAGE_SIZE-offset);
	if (ret)
		goto config_exit;
	offset += scnprintf(config_buf + offset, PAGE_SIZE - offset,
				"%*s%s\n", -14, "device", buf_cdev);
	ret = buffer_overflow_check(buf_cdev_upper, offset, PAGE_SIZE-offset);
	if (ret)
		goto config_exit;
	offset += scnprintf(config_buf + offset, PAGE_SIZE - offset,
				"%*s%s\n", -14, "upper_limit", buf_cdev_upper);
	ret = buffer_overflow_check(buf_cdev_lower, offset, PAGE_SIZE-offset);
	if (ret)
		goto config_exit;
	offset += scnprintf(config_buf + offset, PAGE_SIZE - offset,
				"%*s%s\n", -14, "lower_limit", buf_cdev_lower);

config_exit:
	kfree(buf_cdev);
	kfree(buf_cdev_upper);
	kfree(buf_cdev_lower);
	return (ret < 0) ? ret : offset;
}

ssize_t thermal_dbgfs_config_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct thermal_zone_device *tz = NULL;
	int offset = 0, buf_count = 0, ret;
	char *config_buf = NULL;

	tz = thermal_zone_get_zone_by_name((const char *)tzone_sensor_name);
	if (IS_ERR(tz)) {
		ret = PTR_ERR(tz);
		pr_err("No thermal zone for sensor:%s. err:%d\n",
					tzone_sensor_name, ret);
		return ret;
	}

	config_buf = kzalloc(sizeof(char) * PAGE_SIZE, GFP_KERNEL);
	if (!config_buf)
		return -ENOMEM;

	offset += scnprintf(config_buf + offset, PAGE_SIZE - offset, "%*s%s\n",
				-15, "sensor", tz->type);
	offset += scnprintf(config_buf + offset, PAGE_SIZE - offset, "%*s%s\n",
				-15, "algo_type", tz->governor->name);
	offset += scnprintf(config_buf + offset, PAGE_SIZE - offset, "%*s%s\n",
				-15, "mode",
				(tz->mode == THERMAL_DEVICE_DISABLED)?"disabled":"enabled");
	offset += scnprintf(config_buf + offset, PAGE_SIZE - offset, "%*s%d\n",
				-15, "polling_delay",
				jiffies_to_msecs(tz->polling_delay_jiffies));
	offset += scnprintf(config_buf + offset, PAGE_SIZE - offset, "%*s%d\n",
				-15, "passive_delay",
				jiffies_to_msecs(tz->passive_delay_jiffies));
	if (!tz->num_trips || !tz->ops->get_trip_temp) {
		if (offset >= PAGE_SIZE) {
			pr_err("%s sensor config rule length is more than buffer size\n",
					tz->type);
			ret = -ENOMEM;
			goto config_exit;
		}
		config_buf[offset] = '\0';
		ret = simple_read_from_buffer(buf, count, ppos, config_buf, offset);
		kfree(config_buf);
		return ret;
	}

	ret = fetch_and_populate_trips(config_buf, tz, offset);
	if (ret < 0)
		goto config_exit;
	offset = ret;

	ret = fetch_and_populate_cdevs(config_buf, tz, offset);
	if (ret < 0)
		goto config_exit;
	offset = ret;

	if (offset >= PAGE_SIZE) {
		pr_err("%s sensor config rule length is more than buffer size\n",
				tz->type);
		ret = -ENOMEM;
		goto config_exit;
	}
	config_buf[offset] = '\0';
	buf_count = simple_read_from_buffer(buf, count, ppos, config_buf, offset);

config_exit:
	kfree(config_buf);

	return (ret < 0) ? ret : buf_count;
}

static ssize_t thermal_dbgfs_config_write(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct thermal_zone_device *tz = NULL;
	char sensor_name[THERMAL_NAME_LENGTH] = "";

	if (!count || (count > THERMAL_NAME_LENGTH))
		return -EINVAL;

	if (copy_from_user(sensor_name, user_buf, count))
		return -EFAULT;

	if (sscanf(sensor_name, "%19[^\n\t ]", tzone_sensor_name) != 1)
		return -EINVAL;

	tz = thermal_zone_get_zone_by_name((const char *)tzone_sensor_name);
	if (IS_ERR(tz)) {
		pr_err("No thermal zone for sensor:%s. err:%d\n",
					tzone_sensor_name, PTR_ERR(tz));
		return PTR_ERR(tz);
	}

	return count;
}

static const struct file_operations thermal_dbgfs_config_fops = {
	.write = thermal_dbgfs_config_write,
	.read = thermal_dbgfs_config_read,
};

static int thermal_config_init(void)
{
	int ret = 0;

	thermal_debugfs_parent = debugfs_create_dir("thermal", NULL);
	if (IS_ERR_OR_NULL(thermal_debugfs_parent)) {
		ret = PTR_ERR(thermal_debugfs_parent);
		pr_err("Error creating thermal debugfs directory. err:%d\n",
				ret);
		return ret;
	}
	thermal_debugfs_config = debugfs_create_file("config", 0600,
					thermal_debugfs_parent, NULL,
					&thermal_dbgfs_config_fops);
	if (IS_ERR_OR_NULL(thermal_debugfs_config)) {
		ret = PTR_ERR(thermal_debugfs_config);
		pr_err("Error creating thermal config debugfs. err:%d\n",
				ret);
		return ret;
	}

	return ret;
}

static void thermal_config_exit(void)
{
	debugfs_remove_recursive(thermal_debugfs_parent);
}

module_init(thermal_config_init);
module_exit(thermal_config_exit);
MODULE_DESCRIPTION("Thermal Zone config debug driver");
MODULE_LICENSE("GPL");
