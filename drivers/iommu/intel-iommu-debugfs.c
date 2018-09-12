// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2018 Intel Corporation.
 *
 * Authors: Gayatri Kammela <gayatri.kammela@intel.com>
 *	    Sohil Mehta <sohil.mehta@intel.com>
 *	    Jacob Pan <jacob.jun.pan@linux.intel.com>
 */

#include <linux/debugfs.h>
#include <linux/dmar.h>
#include <linux/intel-iommu.h>
#include <linux/pci.h>

#include <asm/irq_remapping.h>

void __init intel_iommu_debugfs_init(void)
{
	debugfs_create_dir("intel", iommu_debugfs_dir);
}
