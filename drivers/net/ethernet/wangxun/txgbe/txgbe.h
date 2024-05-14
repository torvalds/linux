/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _TXGBE_H_
#define _TXGBE_H_

#include "txgbe_type.h"

#define TXGBE_MAX_FDIR_INDICES          63

#define TXGBE_MAX_RX_QUEUES   (TXGBE_MAX_FDIR_INDICES + 1)
#define TXGBE_MAX_TX_QUEUES   (TXGBE_MAX_FDIR_INDICES + 1)

/* board specific private data structure */
struct txgbe_adapter {
	u8 __iomem *io_addr;    /* Mainly for iounmap use */
	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;
};

extern char txgbe_driver_name[];

#endif /* _TXGBE_H_ */
