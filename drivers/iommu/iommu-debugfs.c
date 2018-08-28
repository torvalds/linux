// SPDX-License-Identifier: GPL-2.0
/*
 * IOMMU debugfs core infrastructure
 *
 * Copyright (C) 2018 Advanced Micro Devices, Inc.
 *
 * Author: Gary R Hook <gary.hook@amd.com>
 */

#include <linux/pci.h>
#include <linux/iommu.h>
#include <linux/debugfs.h>

struct dentry *iommu_debugfs_dir;

/**
 * iommu_debugfs_setup - create the top-level iommu directory in debugfs
 *
 * Provide base enablement for using debugfs to expose internal data of an
 * IOMMU driver. When called, this function creates the
 * /sys/kernel/debug/iommu directory.
 *
 * Emit a strong warning at boot time to indicate that this feature is
 * enabled.
 *
 * This function is called from iommu_init; drivers may then call
 * iommu_debugfs_new_driver_dir() to instantiate a vendor-specific
 * directory to be used to expose internal data.
 */
void iommu_debugfs_setup(void)
{
	if (!iommu_debugfs_dir) {
		iommu_debugfs_dir = debugfs_create_dir("iommu", NULL);
		pr_warn("\n");
		pr_warn("*************************************************************\n");
		pr_warn("**     NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE    **\n");
		pr_warn("**                                                         **\n");
		pr_warn("**  IOMMU DebugFS SUPPORT HAS BEEN ENABLED IN THIS KERNEL  **\n");
		pr_warn("**                                                         **\n");
		pr_warn("** This means that this kernel is built to expose internal **\n");
		pr_warn("** IOMMU data structures, which may compromise security on **\n");
		pr_warn("** your system.                                            **\n");
		pr_warn("**                                                         **\n");
		pr_warn("** If you see this message and you are not debugging the   **\n");
		pr_warn("** kernel, report this immediately to your vendor!         **\n");
		pr_warn("**                                                         **\n");
		pr_warn("**     NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE    **\n");
		pr_warn("*************************************************************\n");
	}
}

/**
 * iommu_debugfs_new_driver_dir - create a vendor directory under debugfs/iommu
 * @vendor: name of the vendor-specific subdirectory to create
 *
 * This function is called by an IOMMU driver to create the top-level debugfs
 * directory for that driver.
 *
 * Return: upon success, a pointer to the dentry for the new directory.
 *         NULL in case of failure.
 */
struct dentry *iommu_debugfs_new_driver_dir(const char *vendor)
{
	return debugfs_create_dir(vendor, iommu_debugfs_dir);
}
EXPORT_SYMBOL_GPL(iommu_debugfs_new_driver_dir);
