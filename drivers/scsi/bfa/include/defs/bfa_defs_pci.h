/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __BFA_DEFS_PCI_H__
#define __BFA_DEFS_PCI_H__

/**
 * PCI device and vendor ID information
 */
enum {
	BFA_PCI_VENDOR_ID_BROCADE	= 0x1657,
	BFA_PCI_DEVICE_ID_FC_8G2P	= 0x13,
	BFA_PCI_DEVICE_ID_FC_8G1P	= 0x17,
	BFA_PCI_DEVICE_ID_CT		= 0x14,
};

/**
 * PCI sub-system device and vendor ID information
 */
enum {
	BFA_PCI_FCOE_SSDEVICE_ID	= 0x14,
};

#define BFA_PCI_ACCESS_RANGES 1	/* Maximum number of device address ranges
				 * mapped through different BAR(s). */

#endif /* __BFA_DEFS_PCI_H__ */
