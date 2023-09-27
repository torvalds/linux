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

static ssize_t bnxt_show_temp(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct hwrm_temp_monitor_query_output *resp;
	struct hwrm_temp_monitor_query_input *req;
	struct bnxt *bp = dev_get_drvdata(dev);
	u32 len = 0;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_TEMP_MONITOR_QUERY);
	if (rc)
		return rc;
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc)
		len = sprintf(buf, "%u\n", resp->temp * 1000); /* display millidegree */
	hwrm_req_drop(bp, req);
	if (rc)
		return rc;
	return len;
}
static SENSOR_DEVICE_ATTR(temp1_input, 0444, bnxt_show_temp, NULL, 0);

static struct attribute *bnxt_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(bnxt);

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

	bp->hwmon_dev = hwmon_device_register_with_groups(&pdev->dev,
							  DRV_MODULE_NAME, bp,
							  bnxt_groups);
	if (IS_ERR(bp->hwmon_dev)) {
		bp->hwmon_dev = NULL;
		dev_warn(&pdev->dev, "Cannot register hwmon device\n");
	}
}
