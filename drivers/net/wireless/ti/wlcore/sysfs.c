// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of wlcore
 *
 * Copyright (C) 2013 Texas Instruments Inc.
 */

#include <linux/pm_runtime.h>

#include "acx.h"
#include "wlcore.h"
#include "debug.h"
#include "sysfs.h"

static ssize_t wl1271_sysfs_show_bt_coex_state(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	struct wl1271 *wl = dev_get_drvdata(dev);
	ssize_t len;

	len = PAGE_SIZE;

	mutex_lock(&wl->mutex);
	len = snprintf(buf, len, "%d\n\n0 - off\n1 - on\n",
		       wl->sg_enabled);
	mutex_unlock(&wl->mutex);

	return len;

}

static ssize_t wl1271_sysfs_store_bt_coex_state(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct wl1271 *wl = dev_get_drvdata(dev);
	unsigned long res;
	int ret;

	ret = kstrtoul(buf, 10, &res);
	if (ret < 0) {
		wl1271_warning("incorrect value written to bt_coex_mode");
		return count;
	}

	mutex_lock(&wl->mutex);

	res = !!res;

	if (res == wl->sg_enabled)
		goto out;

	wl->sg_enabled = res;

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = pm_runtime_get_sync(wl->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(wl->dev);
		goto out;
	}

	wl1271_acx_sg_enable(wl, wl->sg_enabled);
	pm_runtime_mark_last_busy(wl->dev);
	pm_runtime_put_autosuspend(wl->dev);

 out:
	mutex_unlock(&wl->mutex);
	return count;
}

static DEVICE_ATTR(bt_coex_state, 0644,
		   wl1271_sysfs_show_bt_coex_state,
		   wl1271_sysfs_store_bt_coex_state);

static ssize_t wl1271_sysfs_show_hw_pg_ver(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct wl1271 *wl = dev_get_drvdata(dev);
	ssize_t len;

	len = PAGE_SIZE;

	mutex_lock(&wl->mutex);
	if (wl->hw_pg_ver >= 0)
		len = snprintf(buf, len, "%d\n", wl->hw_pg_ver);
	else
		len = snprintf(buf, len, "n/a\n");
	mutex_unlock(&wl->mutex);

	return len;
}

static DEVICE_ATTR(hw_pg_ver, 0444, wl1271_sysfs_show_hw_pg_ver, NULL);

static ssize_t wl1271_sysfs_read_fwlog(struct file *filp, struct kobject *kobj,
				       struct bin_attribute *bin_attr,
				       char *buffer, loff_t pos, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct wl1271 *wl = dev_get_drvdata(dev);
	ssize_t len;
	int ret;

	ret = mutex_lock_interruptible(&wl->mutex);
	if (ret < 0)
		return -ERESTARTSYS;

	/* Check if the fwlog is still valid */
	if (wl->fwlog_size < 0) {
		mutex_unlock(&wl->mutex);
		return 0;
	}

	/* Seeking is not supported - old logs are not kept. Disregard pos. */
	len = min_t(size_t, count, wl->fwlog_size);
	wl->fwlog_size -= len;
	memcpy(buffer, wl->fwlog, len);

	/* Make room for new messages */
	memmove(wl->fwlog, wl->fwlog + len, wl->fwlog_size);

	mutex_unlock(&wl->mutex);

	return len;
}

static const struct bin_attribute fwlog_attr = {
	.attr = { .name = "fwlog", .mode = 0400 },
	.read = wl1271_sysfs_read_fwlog,
};

int wlcore_sysfs_init(struct wl1271 *wl)
{
	int ret;

	/* Create sysfs file to control bt coex state */
	ret = device_create_file(wl->dev, &dev_attr_bt_coex_state);
	if (ret < 0) {
		wl1271_error("failed to create sysfs file bt_coex_state");
		goto out;
	}

	/* Create sysfs file to get HW PG version */
	ret = device_create_file(wl->dev, &dev_attr_hw_pg_ver);
	if (ret < 0) {
		wl1271_error("failed to create sysfs file hw_pg_ver");
		goto out_bt_coex_state;
	}

	/* Create sysfs file for the FW log */
	ret = device_create_bin_file(wl->dev, &fwlog_attr);
	if (ret < 0) {
		wl1271_error("failed to create sysfs file fwlog");
		goto out_hw_pg_ver;
	}

	goto out;

out_hw_pg_ver:
	device_remove_file(wl->dev, &dev_attr_hw_pg_ver);

out_bt_coex_state:
	device_remove_file(wl->dev, &dev_attr_bt_coex_state);

out:
	return ret;
}

void wlcore_sysfs_free(struct wl1271 *wl)
{
	device_remove_bin_file(wl->dev, &fwlog_attr);

	device_remove_file(wl->dev, &dev_attr_hw_pg_ver);

	device_remove_file(wl->dev, &dev_attr_bt_coex_state);
}
