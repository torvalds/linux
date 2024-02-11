/* SPDX-License-Identifier: (GPL-2.0 OR MIT)
 * Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2019 Google, Inc.
 */

#ifndef _GVE_REGISTER_H_
#define _GVE_REGISTER_H_

/* Fixed Configuration Registers */
struct gve_registers {
	__be32	device_status;
	__be32	driver_status;
	__be32	max_tx_queues;
	__be32	max_rx_queues;
	__be32	adminq_pfn;
	__be32	adminq_doorbell;
	__be32	adminq_event_counter;
	u8	reserved[3];
	u8	driver_version;
	__be32	adminq_base_address_hi;
	__be32	adminq_base_address_lo;
	__be16	adminq_length;
};

enum gve_device_status_flags {
	GVE_DEVICE_STATUS_RESET_MASK		= BIT(1),
	GVE_DEVICE_STATUS_LINK_STATUS_MASK	= BIT(2),
	GVE_DEVICE_STATUS_REPORT_STATS_MASK	= BIT(3),
	GVE_DEVICE_STATUS_DEVICE_IS_RESET	= BIT(4),
};

enum gve_driver_status_flags {
	GVE_DRIVER_STATUS_RUN_MASK		= BIT(0),
	GVE_DRIVER_STATUS_RESET_MASK		= BIT(1),
};
#endif /* _GVE_REGISTER_H_ */
