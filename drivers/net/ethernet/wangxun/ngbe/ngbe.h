/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _NGBE_H_
#define _NGBE_H_

#include "ngbe_type.h"

#define NGBE_MAX_FDIR_INDICES		7

#define NGBE_MAX_RX_QUEUES		(NGBE_MAX_FDIR_INDICES + 1)
#define NGBE_MAX_TX_QUEUES		(NGBE_MAX_FDIR_INDICES + 1)

/* board specific private data structure */
struct ngbe_adapter {
	u8 __iomem *io_addr;    /* Mainly for iounmap use */
	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;
};

extern char ngbe_driver_name[];

#endif /* _NGBE_H_ */
