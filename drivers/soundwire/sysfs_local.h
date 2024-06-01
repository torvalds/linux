/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2015-2020 Intel Corporation. */

#ifndef __SDW_SYSFS_LOCAL_H
#define __SDW_SYSFS_LOCAL_H

/*
 * SDW sysfs APIs -
 */

/* basic attributes to report status of Slave (attachment, dev_num) */
extern const struct attribute_group *sdw_slave_status_attr_groups[];

/* attributes for all soundwire devices */
extern const struct attribute_group *sdw_attr_groups[];

/* additional device-managed properties reported after driver probe */
int sdw_slave_sysfs_dpn_init(struct sdw_slave *slave);

#endif /* __SDW_SYSFS_LOCAL_H */
