/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel TPMI core interface for TPMI enumeration methods
 *
 * Copyright (c) 2026, Intel Corporation.
 * All Rights Reserved.
 */

#ifndef _TPMI_COMMON_H_
#define _TPMI_COMMON_H_

#include <linux/intel_vsec.h>

struct intel_tpmi_pm_feature;

/**
 * struct intel_tpmi_info - TPMI information for all IDs in an instance
 * @tpmi_features:	Pointer to a list of TPMI feature instances
 * @feature_count:	Number of TPMI of TPMI instances pointed by tpmi_features
 * @pfs_start:		Start of PFS offset for the TPMI instances in this device
 * @plat_info:		Stores platform info which can be used by the client drivers
 * @tpmi_control_mem:	Memory mapped IO for getting control information
 * @dbgfs_dir:		debugfs entry pointer
 * @resource:		Array of feature_count MMIO resources, one per PFS entry
 * @tpmi_dev:		Device this TPMI instance is bound to. It backs the devm
 *			allocations, holds this structure as its driver data and
 *			the TPMI feature devices are created under it.
 * @parent:		Device this TPMI instance was enumerated from. It names
 *			the debugfs directory and becomes intel_vsec_device::dev
 *			of every TPMI feature device, so that the enumerating bus
 *			can find them again, for example during PCI error
 *			recovery.
 *
 * Stores the information for all TPMI devices of one TPMI instance.
 */
struct intel_tpmi_info {
	struct intel_tpmi_pm_feature *tpmi_features;
	int feature_count;
	u64 pfs_start;
	struct oobmsm_plat_info plat_info;
	void __iomem *tpmi_control_mem;
	struct dentry *dbgfs_dir;
	struct resource *resource;
	struct device *tpmi_dev;
	struct device *parent;
};

int intel_tpmi_init(struct intel_tpmi_info *tpmi_info, struct device *tpmi_dev,
		    struct device *parent, struct resource *resource, int feature_count);
void intel_tpmi_deinit(struct intel_tpmi_info *tpmi_info);

#endif /* _TPMI_COMMON_H_ */
