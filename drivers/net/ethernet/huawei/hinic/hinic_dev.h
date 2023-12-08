/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_DEV_H
#define HINIC_DEV_H

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>

#include "hinic_hw_dev.h"
#include "hinic_tx.h"
#include "hinic_rx.h"
#include "hinic_sriov.h"

#define HINIC_DRV_NAME          "hinic"

#define LP_PKT_CNT		64

enum hinic_flags {
	HINIC_LINK_UP = BIT(0),
	HINIC_INTF_UP = BIT(1),
	HINIC_RSS_ENABLE = BIT(2),
	HINIC_LINK_DOWN = BIT(3),
	HINIC_LP_TEST = BIT(4),
};

struct hinic_rx_mode_work {
	struct work_struct      work;
	u32                     rx_mode;
};

struct hinic_rss_type {
	u8 tcp_ipv6_ext;
	u8 ipv6_ext;
	u8 tcp_ipv6;
	u8 ipv6;
	u8 tcp_ipv4;
	u8 ipv4;
	u8 udp_ipv6;
	u8 udp_ipv4;
};

enum hinic_rss_hash_type {
	HINIC_RSS_HASH_ENGINE_TYPE_XOR,
	HINIC_RSS_HASH_ENGINE_TYPE_TOEP,
	HINIC_RSS_HASH_ENGINE_TYPE_MAX,
};

struct hinic_intr_coal_info {
	u8	pending_limt;
	u8	coalesce_timer_cfg;
	u8	resend_timer_cfg;
};

enum hinic_dbg_type {
	HINIC_DBG_SQ_INFO,
	HINIC_DBG_RQ_INFO,
	HINIC_DBG_FUNC_TABLE,
};

struct hinic_debug_priv {
	struct hinic_dev	*dev;
	void			*object;
	enum hinic_dbg_type	type;
	struct dentry		*root;
	int			field_id[64];
};

struct hinic_dev {
	struct net_device               *netdev;
	struct hinic_hwdev              *hwdev;

	u32                             msg_enable;
	unsigned int                    tx_weight;
	unsigned int                    rx_weight;
	u16				num_qps;
	u16				max_qps;

	unsigned int                    flags;

	struct semaphore                mgmt_lock;
	unsigned long                   *vlan_bitmap;

	struct hinic_rx_mode_work       rx_mode_work;
	struct workqueue_struct         *workq;

	struct hinic_txq                *txqs;
	struct hinic_rxq                *rxqs;
	u16				sq_depth;
	u16				rq_depth;

	u8				rss_tmpl_idx;
	u8				rss_hash_engine;
	u16				num_rss;
	u16				rss_limit;
	struct hinic_rss_type		rss_type;
	u8				*rss_hkey_user;
	s32				*rss_indir_user;
	struct hinic_intr_coal_info	*rx_intr_coalesce;
	struct hinic_intr_coal_info	*tx_intr_coalesce;
	struct hinic_sriov_info sriov_info;
	int				lb_test_rx_idx;
	int				lb_pkt_len;
	u8				*lb_test_rx_buf;

	struct dentry			*dbgfs_root;
	struct dentry			*sq_dbgfs;
	struct dentry			*rq_dbgfs;
	struct dentry			*func_tbl_dbgfs;
	struct hinic_debug_priv		*dbg;
	struct devlink			*devlink;
	bool				cable_unplugged;
	bool				module_unrecognized;
};

struct hinic_devlink_priv {
	struct hinic_hwdev		*hwdev;
	struct devlink_health_reporter  *hw_fault_reporter;
	struct devlink_health_reporter  *fw_fault_reporter;
};

#endif
