/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Western Digital Corporation
 */

#ifndef __UFS_SYSFS_H__
#define __UFS_SYSFS_H__

#include <linux/sysfs.h>

#include "ufshcd.h"

void ufs_sysfs_add_nodes(struct ufs_hba *hba);
void ufs_sysfs_remove_nodes(struct device *dev);

extern const struct attribute_group ufs_sysfs_unit_descriptor_group;
extern const struct attribute_group ufs_sysfs_lun_attributes_group;
#endif
