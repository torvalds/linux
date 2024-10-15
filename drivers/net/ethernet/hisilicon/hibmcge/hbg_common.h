/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef __HBG_COMMON_H
#define __HBG_COMMON_H

#include <linux/netdevice.h>
#include <linux/pci.h>

struct hbg_priv {
	struct net_device *netdev;
	struct pci_dev *pdev;
	u8 __iomem *io_base;
};

#endif
