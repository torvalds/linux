/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2023 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/dev_printk.h>
#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/pci.h>

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"
#include "bnxt_hwmon.h"

void bnxt_hwmon_notify_event(struct bnxt *bp)
{
	u32 attr;

	if (!bp->hwmon_dev)
		return;

	switch (bp->thermal_threshold_type) {
	case ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA1_THRESHOLD_TYPE_WARN:
		attr = hwmon_temp_max_alarm;
		break;
	case ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA1_THRESHOLD_TYPE_CRITICAL:
		attr = hwmon_temp_crit_alarm;
		break;
	case ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA1_THRESHOLD_TYPE_FATAL:
	case ASYNC_EVENT_CMPL_ERROR_REPORT_THERMAL_EVENT_DATA1_THRESHOLD_TYPE_SHUTDOWN:
		attr = hwmon_temp_emergency_alarm;
		break;
	default:
		return;
	}

	hwmon_notify_event(&bp->pdev->dev, hwmon_temp, attr, 0);
}

static int bnxt_hwrm_temp_query(struct bnxt *bp, u8 *temp)
{
	struct hwrm_temp_monitor_query_output *resp;
	struct hwrm_temp_monitor_query_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_TEMP_MONITOR_QUERY);
	if (rc)
		return rc;
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send_silent(bp, req);
	if (rc)
		goto drop_req;

	if (temp) {
		*temp = resp->temp;
	} else if (resp->flags &
		   TEMP_MONITOR_QUERY_RESP_FLAGS_THRESHOLD_VALUES_AVAILABLE) {
		bp->fw_cap |= BNXT_FW_CAP_THRESHOLD_TEMP_SUPPORTED;
		bp->warn_thresh_temp = resp->warn_threshold;
		bp->crit_thresh_temp = resp->critical_threshold;
		bp->fatal_thresh_temp = resp->fatal_threshold;
		bp->shutdown_thresh_temp = resp->shutdown_threshold;
	}
drop_req:
	hwrm_req_drop(bp, req);
	return rc;
}

static umode_t bnxt_hwmon_is_visible(const void *_data, enum hwmon_sensor_types type,
				     u32 attr, int channel)
{
	const struct bnxt *bp = _data;

	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	case hwmon_temp_max:
	case hwmon_temp_crit:
	case hwmon_temp_emergency:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_emergency_alarm:
		if (!(bp->fw_cap & BNXT_FW_CAP_THRESHOLD_TEMP_SUPPORTED))
			return 0;
		return 0444;
	default:
		return 0;
	}
}

static int bnxt_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			   int channel, long *val)
{
	struct bnxt *bp = dev_get_drvdata(dev);
	u8 temp = 0;
	int rc;

	switch (attr) {
	case hwmon_temp_input:
		rc = bnxt_hwrm_temp_query(bp, &temp);
		if (!rc)
			*val = temp * 1000;
		return rc;
	case hwmon_temp_max:
		*val = bp->warn_thresh_temp * 1000;
		return 0;
	case hwmon_temp_crit:
		*val = bp->crit_thresh_temp * 1000;
		return 0;
	case hwmon_temp_emergency:
		*val = bp->fatal_thresh_temp * 1000;
		return 0;
	case hwmon_temp_max_alarm:
		rc = bnxt_hwrm_temp_query(bp, &temp);
		if (!rc)
			*val = temp >= bp->warn_thresh_temp;
		return rc;
	case hwmon_temp_crit_alarm:
		rc = bnxt_hwrm_temp_query(bp, &temp);
		if (!rc)
			*val = temp >= bp->crit_thresh_temp;
		return rc;
	case hwmon_temp_emergency_alarm:
		rc = bnxt_hwrm_temp_query(bp, &temp);
		if (!rc)
			*val = temp >= bp->fatal_thresh_temp;
		return rc;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info *bnxt_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_EMERGENCY | HWMON_T_MAX_ALARM |
			   HWMON_T_CRIT_ALARM | HWMON_T_EMERGENCY_ALARM),
	NULL
};

static const struct hwmon_ops bnxt_hwmon_ops = {
	.is_visible     = bnxt_hwmon_is_visible,
	.read           = bnxt_hwmon_read,
};

static const struct hwmon_chip_info bnxt_hwmon_chip_info = {
	.ops    = &bnxt_hwmon_ops,
	.info   = bnxt_hwmon_info,
};

static ssize_t temp1_shutdown_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bnxt *bp = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", bp->shutdown_thresh_temp * 1000);
}

static ssize_t temp1_shutdown_alarm_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	struct bnxt *bp = dev_get_drvdata(dev);
	u8 temp;
	int rc;

	rc = bnxt_hwrm_temp_query(bp, &temp);
	if (rc)
		return -EIO;

	return sysfs_emit(buf, "%u\n", temp >= bp->shutdown_thresh_temp);
}

static DEVICE_ATTR_RO(temp1_shutdown);
static DEVICE_ATTR_RO(temp1_shutdown_alarm);

static struct attribute *bnxt_temp_extra_attrs[] = {
	&dev_attr_temp1_shutdown.attr,
	&dev_attr_temp1_shutdown_alarm.attr,
	NULL,
};

static umode_t bnxt_temp_extra_attrs_visible(struct kobject *kobj,
					     struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct bnxt *bp = dev_get_drvdata(dev);

	/* Shutdown temperature setting in NVM is optional */
	if (!(bp->fw_cap & BNXT_FW_CAP_THRESHOLD_TEMP_SUPPORTED) ||
	    !bp->shutdown_thresh_temp)
		return 0;

	return attr->mode;
}

static const struct attribute_group bnxt_temp_extra_group = {
	.attrs		= bnxt_temp_extra_attrs,
	.is_visible	= bnxt_temp_extra_attrs_visible,
};
__ATTRIBUTE_GROUPS(bnxt_temp_extra);

void bnxt_hwmon_uninit(struct bnxt *bp)
{
	if (bp->hwmon_dev) {
		hwmon_device_unregister(bp->hwmon_dev);
		bp->hwmon_dev = NULL;
	}
}

void bnxt_hwmon_init(struct bnxt *bp)
{
	struct pci_dev *pdev = bp->pdev;
	int rc;

	/* temp1_xxx is only sensor, ensure not registered if it will fail */
	rc = bnxt_hwrm_temp_query(bp, NULL);
	if (rc == -EACCES || rc == -EOPNOTSUPP) {
		bnxt_hwmon_uninit(bp);
		return;
	}

	if (bp->hwmon_dev)
		return;

	bp->hwmon_dev = hwmon_device_register_with_info(&pdev->dev,
							DRV_MODULE_NAME, bp,
							&bnxt_hwmon_chip_info,
							bnxt_temp_extra_groups);
	if (IS_ERR(bp->hwmon_dev)) {
		bp->hwmon_dev = NULL;
		dev_warn(&pdev->dev, "Cannot register hwmon device\n");
	}
}
