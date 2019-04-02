// SPDX-License-Identifier: GPL-2.0
/*
 * AMD IOMMU driver
 *
 * Copyright (C) 2018 Advanced Micro Devices, Inc.
 *
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include <linux/defs.h>
#include <linux/iommu.h>
#include <linux/pci.h>
#include "amd_iommu_proto.h"
#include "amd_iommu_types.h"

static struct dentry *amd_iommu_defs;
static DEFINE_MUTEX(amd_iommu_defs_lock);

#define	MAX_NAME_LEN	20

void amd_iommu_defs_setup(struct amd_iommu *iommu)
{
	char name[MAX_NAME_LEN + 1];

	mutex_lock(&amd_iommu_defs_lock);
	if (!amd_iommu_defs)
		amd_iommu_defs = defs_create_dir("amd",
						       iommu_defs_dir);
	mutex_unlock(&amd_iommu_defs_lock);

	snprintf(name, MAX_NAME_LEN, "iommu%02d", iommu->index);
	iommu->defs = defs_create_dir(name, amd_iommu_defs);
}
