/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2021 Hisilicon Limited.

#ifndef __HNS3_ETHTOOL_H
#define __HNS3_ETHTOOL_H

#include <linux/ethtool.h>
#include <linux/netdevice.h>

struct hns3_stats {
	char stats_string[ETH_GSTRING_LEN];
	int stats_offset;
};

struct hns3_sfp_type {
	u8 type;
	u8 ext_type;
};

struct hns3_pflag_desc {
	char name[ETH_GSTRING_LEN];
	void (*handler)(struct net_device *netdev, bool enable);
};

struct hns3_ethtool_link_ext_state_mapping {
	u32 status_code;
	enum ethtool_link_ext_state link_ext_state;
	u8 link_ext_substate;
};

struct hns3_ring_param {
	u32 tx_desc_num;
	u32 rx_desc_num;
	u32 rx_buf_len;
};

#endif
