/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __PPE_H__
#define __PPE_H__

#include <linux/compiler.h>
#include <linux/interconnect.h>

struct device;
struct regmap;
struct dentry;

/**
 * struct ppe_device - PPE device private data.
 * @dev: PPE device structure.
 * @regmap: PPE register map.
 * @clk_rate: PPE clock rate.
 * @num_ports: Number of PPE ports.
 * @debugfs_root: Debugfs root entry.
 * @num_icc_paths: Number of interconnect paths.
 * @icc_paths: Interconnect path array.
 *
 * PPE device is the instance of PPE hardware, which is used to
 * configure PPE packet process modules such as BM (buffer management),
 * QM (queue management), and scheduler.
 */
struct ppe_device {
	struct device *dev;
	struct regmap *regmap;
	unsigned long clk_rate;
	unsigned int num_ports;
	struct dentry *debugfs_root;
	unsigned int num_icc_paths;
	struct icc_bulk_data icc_paths[] __counted_by(num_icc_paths);
};
#endif
