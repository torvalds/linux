/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AMD SFH sysfs attributes
 *
 * Copyright (c) 2026, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */
#ifndef AMD_SFH_SYSFS_H
#define AMD_SFH_SYSFS_H

#include "amd_sfh_common.h"

extern const struct attribute_group *amd_sfh_groups[];

int amd_sfh_update_sysfs(struct amd_mp2_dev *mp2);

#endif
