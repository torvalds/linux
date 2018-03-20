/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_TYPE_H_
#define _ICE_TYPE_H_

/* Bus parameters */
struct ice_bus_info {
	u16 device;
	u8 func;
};

/* Port hardware description */
struct ice_hw {
	u8 __iomem *hw_addr;
	void *back;

	/* pci info */
	u16 device_id;
	u16 vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_vendor_id;
	u8 revision_id;

	struct ice_bus_info bus;
};

#endif /* _ICE_TYPE_H_ */
