/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell RVU REPRESENTOR driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#ifndef REP_H
#define REP_H

#include <linux/pci.h>

#include "otx2_reg.h"
#include "otx2_txrx.h"
#include "otx2_common.h"

#define PCI_DEVID_RVU_REP	0xA0E0

#define RVU_MAX_REP	OTX2_MAX_CQ_CNT
struct rep_dev {
	struct otx2_nic *mdev;
	struct net_device *netdev;
	u16 rep_id;
	u16 pcifunc;
};

static inline bool otx2_rep_dev(struct pci_dev *pdev)
{
	return pdev->device == PCI_DEVID_RVU_REP;
}
#endif /* REP_H */
