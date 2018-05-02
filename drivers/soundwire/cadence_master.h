// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#ifndef __SDW_CADENCE_H
#define __SDW_CADENCE_H

/**
 * struct sdw_cdns - Cadence driver context
 * @dev: Linux device
 * @bus: Bus handle
 * @instance: instance number
 * @response_buf: SoundWire response buffer
 * @tx_complete: Tx completion
 * @defer: Defer pointer
 * @registers: Cadence registers
 * @link_up: Link status
 * @msg_count: Messages sent on bus
 */
struct sdw_cdns {
	struct device *dev;
	struct sdw_bus bus;
	unsigned int instance;

	u32 response_buf[0x80];
	struct completion tx_complete;
	struct sdw_defer *defer;

	void __iomem *registers;

	bool link_up;
	unsigned int msg_count;
};

#define bus_to_cdns(_bus) container_of(_bus, struct sdw_cdns, bus)

/* Exported symbols */

int sdw_cdns_probe(struct sdw_cdns *cdns);
extern struct sdw_master_ops sdw_cdns_master_ops;

irqreturn_t sdw_cdns_irq(int irq, void *dev_id);
irqreturn_t sdw_cdns_thread(int irq, void *dev_id);

int sdw_cdns_init(struct sdw_cdns *cdns);
int sdw_cdns_enable_interrupt(struct sdw_cdns *cdns);


#endif /* __SDW_CADENCE_H */
