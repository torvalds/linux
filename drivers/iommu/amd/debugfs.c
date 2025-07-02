// SPDX-License-Identifier: GPL-2.0
/*
 * AMD IOMMU driver
 *
 * Copyright (C) 2018 Advanced Micro Devices, Inc.
 *
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include <linux/debugfs.h>
#include <linux/pci.h>

#include "amd_iommu.h"

static struct dentry *amd_iommu_debugfs;

#define	MAX_NAME_LEN	20

void amd_iommu_debugfs_setup(void)
{
	struct amd_iommu *iommu;
	char name[MAX_NAME_LEN + 1];

	amd_iommu_debugfs = debugfs_create_dir("amd", iommu_debugfs_dir);

	for_each_iommu(iommu) {
		snprintf(name, MAX_NAME_LEN, "iommu%02d", iommu->index);
		iommu->debugfs = debugfs_create_dir(name, amd_iommu_debugfs);
	}
}
