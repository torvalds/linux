/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File hw_atl_b0.h: Declaration of abstract interface for Atlantic hardware
 * specific functions.
 */

#ifndef HW_ATL_B0_H
#define HW_ATL_B0_H

#include "../aq_common.h"

#ifndef PCI_VENDOR_ID_AQUANTIA

#define PCI_VENDOR_ID_AQUANTIA  0x1D6A
#define HW_ATL_DEVICE_ID_0001   0x0001
#define HW_ATL_DEVICE_ID_D100   0xD100
#define HW_ATL_DEVICE_ID_D107   0xD107
#define HW_ATL_DEVICE_ID_D108   0xD108
#define HW_ATL_DEVICE_ID_D109   0xD109

#define HW_ATL_NIC_NAME "aQuantia AQtion 5Gbit Network Adapter"

#endif

struct aq_hw_ops *hw_atl_b0_get_ops_by_id(struct pci_dev *pdev);

#endif /* HW_ATL_B0_H */
