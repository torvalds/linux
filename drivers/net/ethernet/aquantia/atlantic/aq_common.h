/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_common.h: Basic includes for all files in project. */

#ifndef AQ_COMMON_H
#define AQ_COMMON_H

#include <linux/etherdevice.h>
#include <linux/pci.h>

#include "ver.h"
#include "aq_cfg.h"
#include "aq_utils.h"

#define PCI_VENDOR_ID_AQUANTIA  0x1D6A

#define AQ_DEVICE_ID_0001	0x0001
#define AQ_DEVICE_ID_D100	0xD100
#define AQ_DEVICE_ID_D107	0xD107
#define AQ_DEVICE_ID_D108	0xD108
#define AQ_DEVICE_ID_D109	0xD109

#define AQ_DEVICE_ID_AQC100	0x00B1
#define AQ_DEVICE_ID_AQC107	0x07B1
#define AQ_DEVICE_ID_AQC108	0x08B1
#define AQ_DEVICE_ID_AQC109	0x09B1
#define AQ_DEVICE_ID_AQC111	0x11B1
#define AQ_DEVICE_ID_AQC112	0x12B1

#define AQ_DEVICE_ID_AQC100S	0x80B1
#define AQ_DEVICE_ID_AQC107S	0x87B1
#define AQ_DEVICE_ID_AQC108S	0x88B1
#define AQ_DEVICE_ID_AQC109S	0x89B1
#define AQ_DEVICE_ID_AQC111S	0x91B1
#define AQ_DEVICE_ID_AQC112S	0x92B1

#define AQ_DEVICE_ID_AQC111E	0x51B1
#define AQ_DEVICE_ID_AQC112E	0x52B1

#define HW_ATL_NIC_NAME "aQuantia AQtion 10Gbit Network Adapter"

#define AQ_HWREV_ANY	0
#define AQ_HWREV_1	1
#define AQ_HWREV_2	2

#define AQ_NIC_RATE_10G        BIT(0)
#define AQ_NIC_RATE_5G         BIT(1)
#define AQ_NIC_RATE_5GSR       BIT(2)
#define AQ_NIC_RATE_2GS        BIT(3)
#define AQ_NIC_RATE_1G         BIT(4)
#define AQ_NIC_RATE_100M       BIT(5)

#define AQ_NIC_RATE_EEE_10G	BIT(6)
#define AQ_NIC_RATE_EEE_5G	BIT(7)
#define AQ_NIC_RATE_EEE_2GS	BIT(8)
#define AQ_NIC_RATE_EEE_1G	BIT(9)

#endif /* AQ_COMMON_H */
