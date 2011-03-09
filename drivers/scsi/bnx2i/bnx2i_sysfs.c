/* bnx2i_sysfs.c: Broadcom NetXtreme II iSCSI driver.
 *
 * Copyright (c) 2004 - 2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Anil Veerabhadrappa (anilgv@broadcom.com)
 * Maintained by: Eddie Wai (eddie.wai@broadcom.com)
 */

#include "bnx2i.h"

/**
 * bnx2i_dev_to_hba - maps dev pointer to adapter struct
 * @dev:	device pointer
 *
 * Map device to hba structure
 */
static inline struct bnx2i_hba *bnx2i_dev_to_hba(struct device *dev)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	return iscsi_host_priv(shost);
}


/**
 * bnx2i_show_sq_info - return(s currently configured send queue (SQ) size
 * @dev:	device pointer
 * @buf:	buffer to return current SQ size parameter
 *
 * Returns current SQ size parameter, this paramater determines the number
 * outstanding iSCSI commands supported on a connection
 */
static ssize_t bnx2i_show_sq_info(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct bnx2i_hba *hba = bnx2i_dev_to_hba(dev);

	return sprintf(buf, "0x%x\n", hba->max_sqes);
}


/**
 * bnx2i_set_sq_info - update send queue (SQ) size parameter
 * @dev:	device pointer
 * @buf:	buffer to return current SQ size parameter
 * @count:	parameter buffer size
 *
 * Interface for user to change shared queue size allocated for each conn
 * Must be within SQ limits and a power of 2. For the latter this is needed
 * because of how libiscsi preallocates tasks.
 */
static ssize_t bnx2i_set_sq_info(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct bnx2i_hba *hba = bnx2i_dev_to_hba(dev);
	u32 val;
	int max_sq_size;

	if (hba->ofld_conns_active)
		goto skip_config;

	if (test_bit(BNX2I_NX2_DEV_57710, &hba->cnic_dev_type))
		max_sq_size = BNX2I_5770X_SQ_WQES_MAX;
	else
		max_sq_size = BNX2I_570X_SQ_WQES_MAX;

	if (sscanf(buf, " 0x%x ", &val) > 0) {
		if ((val >= BNX2I_SQ_WQES_MIN) && (val <= max_sq_size) &&
		    (is_power_of_2(val)))
			hba->max_sqes = val;
	}

	return count;

skip_config:
	printk(KERN_ERR "bnx2i: device busy, cannot change SQ size\n");
	return 0;
}


/**
 * bnx2i_show_ccell_info - returns command cell (HQ) size
 * @dev:	device pointer
 * @buf:	buffer to return current SQ size parameter
 *
 * returns per-connection TCP history queue size parameter
 */
static ssize_t bnx2i_show_ccell_info(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct bnx2i_hba *hba = bnx2i_dev_to_hba(dev);

	return sprintf(buf, "0x%x\n", hba->num_ccell);
}


/**
 * bnx2i_get_link_state - set command cell (HQ) size
 * @dev:	device pointer
 * @buf:	buffer to return current SQ size parameter
 * @count:	parameter buffer size
 *
 * updates per-connection TCP history queue size parameter
 */
static ssize_t bnx2i_set_ccell_info(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	u32 val;
	struct bnx2i_hba *hba = bnx2i_dev_to_hba(dev);

	if (hba->ofld_conns_active)
		goto skip_config;

	if (sscanf(buf, " 0x%x ", &val) > 0) {
		if ((val >= BNX2I_CCELLS_MIN) &&
		    (val <= BNX2I_CCELLS_MAX)) {
			hba->num_ccell = val;
		}
	}

	return count;

skip_config:
	printk(KERN_ERR "bnx2i: device busy, cannot change CCELL size\n");
	return 0;
}


static DEVICE_ATTR(sq_size, S_IRUGO | S_IWUSR,
		   bnx2i_show_sq_info, bnx2i_set_sq_info);
static DEVICE_ATTR(num_ccell, S_IRUGO | S_IWUSR,
		   bnx2i_show_ccell_info, bnx2i_set_ccell_info);

struct device_attribute *bnx2i_dev_attributes[] = {
	&dev_attr_sq_size,
	&dev_attr_num_ccell,
	NULL
};
