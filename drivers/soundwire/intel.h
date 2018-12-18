// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#ifndef __SDW_INTEL_LOCAL_H
#define __SDW_INTEL_LOCAL_H

/**
 * struct sdw_intel_res - Soundwire link resources
 * @registers: Link IO registers base
 * @shim: Audio shim pointer
 * @alh: ALH (Audio Link Hub) pointer
 * @irq: Interrupt line
 * @ops: Shim callback ops
 * @arg: Shim callback ops argument
 *
 * This is set as pdata for each link instance.
 */
struct sdw_intel_link_res {
	void __iomem *registers;
	void __iomem *shim;
	void __iomem *alh;
	int irq;
	const struct sdw_intel_ops *ops;
	void *arg;
};

#endif /* __SDW_INTEL_LOCAL_H */
