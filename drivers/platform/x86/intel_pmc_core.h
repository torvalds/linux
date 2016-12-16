/*
 * Intel Core SoC Power Management Controller Header File
 *
 * Copyright (c) 2016, Intel Corporation.
 * All Rights Reserved.
 *
 * Authors: Rajneesh Bhardwaj <rajneesh.bhardwaj@intel.com>
 *          Vishwanath Somayaji <vishwanath.somayaji@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef PMC_CORE_H
#define PMC_CORE_H

/* Sunrise Point Power Management Controller PCI Device ID */
#define SPT_PMC_PCI_DEVICE_ID			0x9d21

#define SPT_PMC_BASE_ADDR_OFFSET		0x48
#define SPT_PMC_SLP_S0_RES_COUNTER_OFFSET	0x13c
#define SPT_PMC_MMIO_REG_LEN			0x100
#define SPT_PMC_SLP_S0_RES_COUNTER_STEP		0x64

/**
 * struct pmc_dev - pmc device structure
 * @base_addr:		comtains pmc base address
 * @regbase:		pointer to io-remapped memory location
 * @dbgfs_dir:		path to debug fs interface
 * @feature_available:	flag to indicate whether
 *			the feature is available
 *			on a particular platform or not.
 *
 * pmc_dev contains info about power management controller device.
 */
struct pmc_dev {
	u32 base_addr;
	void __iomem *regbase;
	struct dentry *dbgfs_dir;
	bool has_slp_s0_res;
};

#endif /* PMC_CORE_H */
