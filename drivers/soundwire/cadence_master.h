// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#ifndef __SDW_CADENCE_H
#define __SDW_CADENCE_H

/**
 * struct sdw_cdns - Cadence driver context
 * @dev: Linux device
 * @bus: Bus handle
 * @instance: instance number
 * @registers: Cadence registers
 * @link_up: Link status
 */
struct sdw_cdns {
	struct device *dev;
	struct sdw_bus bus;
	unsigned int instance;

	void __iomem *registers;

	bool link_up;
};

#define bus_to_cdns(_bus) container_of(_bus, struct sdw_cdns, bus)

/* Exported symbols */

irqreturn_t sdw_cdns_irq(int irq, void *dev_id);
irqreturn_t sdw_cdns_thread(int irq, void *dev_id);

int sdw_cdns_init(struct sdw_cdns *cdns);

#endif /* __SDW_CADENCE_H */
