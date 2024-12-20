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

struct rep_stats {
	u64 rx_bytes;
	u64 rx_frames;
	u64 rx_drops;
	u64 rx_mcast_frames;

	u64 tx_bytes;
	u64 tx_frames;
	u64 tx_drops;
};

struct rep_dev {
	struct otx2_nic *mdev;
	struct net_device *netdev;
	struct rep_stats stats;
	struct delayed_work stats_wrk;
	struct devlink_port dl_port;
	struct otx2_flow_config	*flow_cfg;
#define RVU_REP_VF_INITIALIZED		BIT_ULL(0)
	u64 flags;
	u16 rep_id;
	u16 pcifunc;
	u8 mac[ETH_ALEN];
};

static inline bool otx2_rep_dev(struct pci_dev *pdev)
{
	return pdev->device == PCI_DEVID_RVU_REP;
}

int rvu_rep_create(struct otx2_nic *priv, struct netlink_ext_ack *extack);
void rvu_rep_destroy(struct otx2_nic *priv);
int rvu_event_up_notify(struct otx2_nic *pf, struct rep_event *info);
#endif /* REP_H */
