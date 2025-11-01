/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_NIC_DEV_H_
#define _HINIC3_NIC_DEV_H_

#include <linux/netdevice.h>

#include "hinic3_hw_cfg.h"
#include "hinic3_mgmt_interface.h"

enum hinic3_flags {
	HINIC3_RSS_ENABLE,
};

enum hinic3_rss_hash_type {
	HINIC3_RSS_HASH_ENGINE_TYPE_XOR  = 0,
	HINIC3_RSS_HASH_ENGINE_TYPE_TOEP = 1,
};

struct hinic3_rss_type {
	u8 tcp_ipv6_ext;
	u8 ipv6_ext;
	u8 tcp_ipv6;
	u8 ipv6;
	u8 tcp_ipv4;
	u8 ipv4;
	u8 udp_ipv6;
	u8 udp_ipv4;
};

struct hinic3_irq_cfg {
	struct net_device  *netdev;
	u16                msix_entry_idx;
	/* provided by OS */
	u32                irq_id;
	char               irq_name[IFNAMSIZ + 16];
	struct napi_struct napi;
	cpumask_t          affinity_mask;
	struct hinic3_txq  *txq;
	struct hinic3_rxq  *rxq;
};

struct hinic3_dyna_txrxq_params {
	u16                        num_qps;
	u32                        sq_depth;
	u32                        rq_depth;

	struct hinic3_dyna_txq_res *txqs_res;
	struct hinic3_dyna_rxq_res *rxqs_res;
	struct hinic3_irq_cfg      *irq_cfg;
};

struct hinic3_intr_coal_info {
	u8 pending_limit;
	u8 coalesce_timer_cfg;
	u8 resend_timer_cfg;
};

struct hinic3_nic_dev {
	struct pci_dev                  *pdev;
	struct net_device               *netdev;
	struct hinic3_hwdev             *hwdev;
	struct hinic3_nic_io            *nic_io;

	u16                             max_qps;
	u16                             rx_buf_len;
	u32                             lro_replenish_thld;
	unsigned long                   flags;
	struct hinic3_nic_service_cap   nic_svc_cap;

	struct hinic3_dyna_txrxq_params q_params;
	struct hinic3_txq               *txqs;
	struct hinic3_rxq               *rxqs;

	enum hinic3_rss_hash_type       rss_hash_type;
	struct hinic3_rss_type          rss_type;
	u8                              *rss_hkey;
	u16                             *rss_indir;

	u16                             num_qp_irq;
	struct msix_entry               *qps_msix_entries;

	struct hinic3_intr_coal_info    *intr_coalesce;

	bool                            link_status_up;
};

void hinic3_set_netdev_ops(struct net_device *netdev);
int hinic3_qps_irq_init(struct net_device *netdev);
void hinic3_qps_irq_uninit(struct net_device *netdev);

#endif
