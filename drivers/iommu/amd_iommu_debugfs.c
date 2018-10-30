// SPDX-License-Identifier: GPL-2.0
/*
 * AMD IOMMU driver
 *
 * Copyright (C) 2018 Advanced Micro Devices, Inc.
 *
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include <linux/debugfs.h>
#include <linux/iommu.h>
#include <linux/pci.h>
#include "amd_iommu_proto.h"
#include "amd_iommu_types.h"

static struct dentry *amd_iommu_debugfs;
static DEFINE_MUTEX(amd_iommu_debugfs_lock);

#define	MAX_NAME_LEN	20

void amd_iommu_debugfs_setup(struct amd_iommu *iommu)
{
	char name[MAX_NAME_LEN + 1];

	mutex_lock(&amd_iommu_debugfs_lock);
	if (!amd_iommu_debugfs)
		amd_iommu_debugfs = debugfs_create_dir("amd",
						       iommu_debugfs_dir);
	mutex_unlock(&amd_iommu_debugfs_lock);

	snprintf(name, MAX_NAME_LEN, "iommu%02d", iommu->index);
	iommu->debugfs = debugfs_create_dir(name, amd_iommu_debugfs);
}
