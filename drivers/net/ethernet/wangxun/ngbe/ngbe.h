/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _NGBE_H_
#define _NGBE_H_

#include "ngbe_type.h"

#define NGBE_MAX_FDIR_INDICES		7

#define NGBE_MAX_RX_QUEUES		(NGBE_MAX_FDIR_INDICES + 1)
#define NGBE_MAX_TX_QUEUES		(NGBE_MAX_FDIR_INDICES + 1)

#define NGBE_ETH_LENGTH_OF_ADDRESS	6
#define NGBE_MAX_MSIX_VECTORS		0x09
#define NGBE_RAR_ENTRIES		32

/* TX/RX descriptor defines */
#define NGBE_DEFAULT_TXD		512 /* default ring size */
#define NGBE_DEFAULT_TX_WORK		256
#define NGBE_MAX_TXD			8192
#define NGBE_MIN_TXD			128

#define NGBE_DEFAULT_RXD		512 /* default ring size */
#define NGBE_DEFAULT_RX_WORK		256
#define NGBE_MAX_RXD			8192
#define NGBE_MIN_RXD			128

#define NGBE_MAC_STATE_DEFAULT		0x1
#define NGBE_MAC_STATE_MODIFIED		0x2
#define NGBE_MAC_STATE_IN_USE		0x4

struct ngbe_mac_addr {
	u8 addr[ETH_ALEN];
	u16 state; /* bitmask */
	u64 pools;
};

/* board specific private data structure */
struct ngbe_adapter {
	u8 __iomem *io_addr;    /* Mainly for iounmap use */
	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;

	/* structs defined in ngbe_hw.h */
	struct ngbe_hw hw;
	struct ngbe_mac_addr *mac_table;
	u16 msg_enable;

	/* Tx fast path data */
	int num_tx_queues;
	u16 tx_itr_setting;
	u16 tx_work_limit;

	/* Rx fast path data */
	int num_rx_queues;
	u16 rx_itr_setting;
	u16 rx_work_limit;

	int num_q_vectors;      /* current number of q_vectors for device */
	int max_q_vectors;      /* upper limit of q_vectors for device */

	u32 tx_ring_count;
	u32 rx_ring_count;

#define NGBE_MAX_RETA_ENTRIES 128
	u8 rss_indir_tbl[NGBE_MAX_RETA_ENTRIES];

#define NGBE_RSS_KEY_SIZE     40  /* size of RSS Hash Key in bytes */
	u32 *rss_key;
	u32 wol;

	u16 bd_number;
};

extern char ngbe_driver_name[];

#endif /* _NGBE_H_ */
