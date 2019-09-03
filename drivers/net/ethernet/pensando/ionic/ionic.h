/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#ifndef _IONIC_H_
#define _IONIC_H_

#include "ionic_devlink.h"

#define IONIC_DRV_NAME		"ionic"
#define IONIC_DRV_DESCRIPTION	"Pensando Ethernet NIC Driver"
#define IONIC_DRV_VERSION	"0.15.0-k"

#define PCI_VENDOR_ID_PENSANDO			0x1dd8

#define PCI_DEVICE_ID_PENSANDO_IONIC_ETH_PF	0x1002
#define PCI_DEVICE_ID_PENSANDO_IONIC_ETH_VF	0x1003

#define IONIC_SUBDEV_ID_NAPLES_25	0x4000
#define IONIC_SUBDEV_ID_NAPLES_100_4	0x4001
#define IONIC_SUBDEV_ID_NAPLES_100_8	0x4002

struct ionic {
	struct pci_dev *pdev;
	struct device *dev;
};

#endif /* _IONIC_H_ */
