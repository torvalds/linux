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

	*temp = resp->temp;

drop_req:
	hwrm_req_drop(bp, req);
	return rc;
}

static umode_t bnxt_hwmon_is_visible(const void *_data, enum hwmon_sensor_types type,
				     u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
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
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info *bnxt_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
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

void bnxt_hwmon_uninit(struct bnxt *bp)
{
	if (bp->hwmon_dev) {
		hwmon_device_unregister(bp->hwmon_dev);
		bp->hwmon_dev = NULL;
	}
}

void bnxt_hwmon_init(struct bnxt *bp)
{
	struct hwrm_temp_monitor_query_input *req;
	struct pci_dev *pdev = bp->pdev;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_TEMP_MONITOR_QUERY);
	if (!rc)
		rc = hwrm_req_send_silent(bp, req);
	if (rc == -EACCES || rc == -EOPNOTSUPP) {
		bnxt_hwmon_uninit(bp);
		return;
	}

	if (bp->hwmon_dev)
		return;

	bp->hwmon_dev = hwmon_device_register_with_info(&pdev->dev,
							DRV_MODULE_NAME, bp,
							&bnxt_hwmon_chip_info, NULL);
	if (IS_ERR(bp->hwmon_dev)) {
		bp->hwmon_dev = NULL;
		dev_warn(&pdev->dev, "Cannot register hwmon device\n");
	}
}
