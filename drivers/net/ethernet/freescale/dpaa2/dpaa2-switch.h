/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DPAA2 Ethernet Switch declarations
 *
 * Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2017-2021 NXP
 *
 */

#ifndef __ETHSW_H
#define __ETHSW_H

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>
#include <uapi/linux/if_bridge.h>
#include <net/switchdev.h>
#include <linux/if_bridge.h>
#include <linux/fsl/mc.h>
#include <net/pkt_cls.h>
#include <soc/fsl/dpaa2-io.h>

#include "dpaa2-mac.h"
#include "dpsw.h"

/* Number of IRQs supported */
#define DPSW_IRQ_NUM	2

/* Port is member of VLAN */
#define ETHSW_VLAN_MEMBER	1
/* VLAN to be treated as untagged on egress */
#define ETHSW_VLAN_UNTAGGED	2
/* Untagged frames will be assigned to this VLAN */
#define ETHSW_VLAN_PVID		4
/* VLAN configured on the switch */
#define ETHSW_VLAN_GLOBAL	8

/* Maximum Frame Length supported by HW (currently 10k) */
#define DPAA2_MFL		(10 * 1024)
#define ETHSW_MAX_FRAME_LENGTH	(DPAA2_MFL - VLAN_ETH_HLEN - ETH_FCS_LEN)
#define ETHSW_L2_MAX_FRM(mtu)	((mtu) + VLAN_ETH_HLEN + ETH_FCS_LEN)

#define ETHSW_FEATURE_MAC_ADDR	BIT(0)

/* Number of receive queues (one RX and one TX_CONF) */
#define DPAA2_SWITCH_RX_NUM_FQS	2

/* Hardware requires alignment for ingress/egress buffer addresses */
#define DPAA2_SWITCH_RX_BUF_RAW_SIZE	PAGE_SIZE
#define DPAA2_SWITCH_RX_BUF_TAILROOM \
	SKB_DATA_ALIGN(sizeof(struct skb_shared_info))
#define DPAA2_SWITCH_RX_BUF_SIZE \
	(DPAA2_SWITCH_RX_BUF_RAW_SIZE - DPAA2_SWITCH_RX_BUF_TAILROOM)

#define DPAA2_SWITCH_STORE_SIZE 16

/* Buffer management */
#define BUFS_PER_CMD			7
#define DPAA2_ETHSW_NUM_BUFS		(1024 * BUFS_PER_CMD)
#define DPAA2_ETHSW_REFILL_THRESH	(DPAA2_ETHSW_NUM_BUFS * 5 / 6)

/* Number of times to retry DPIO portal operations while waiting
 * for portal to finish executing current command and become
 * available. We want to avoid being stuck in a while loop in case
 * hardware becomes unresponsive, but not give up too easily if
 * the portal really is busy for valid reasons
 */
#define DPAA2_SWITCH_SWP_BUSY_RETRIES		1000

/* Hardware annotation buffer size */
#define DPAA2_SWITCH_HWA_SIZE			64
/* Software annotation buffer size */
#define DPAA2_SWITCH_SWA_SIZE			64

#define DPAA2_SWITCH_TX_BUF_ALIGN		64

#define DPAA2_SWITCH_TX_DATA_OFFSET \
	(DPAA2_SWITCH_HWA_SIZE + DPAA2_SWITCH_SWA_SIZE)

#define DPAA2_SWITCH_NEEDED_HEADROOM \
	(DPAA2_SWITCH_TX_DATA_OFFSET + DPAA2_SWITCH_TX_BUF_ALIGN)

#define DPAA2_ETHSW_PORT_MAX_ACL_ENTRIES	16
#define DPAA2_ETHSW_PORT_DEFAULT_TRAPS		1

#define DPAA2_ETHSW_PORT_ACL_CMD_BUF_SIZE	256

extern const struct ethtool_ops dpaa2_switch_port_ethtool_ops;

struct ethsw_core;

struct dpaa2_switch_fq {
	struct ethsw_core *ethsw;
	enum dpsw_queue_type type;
	struct dpaa2_io_store *store;
	struct dpaa2_io_notification_ctx nctx;
	struct napi_struct napi;
	u32 fqid;
};

struct dpaa2_switch_fdb {
	struct net_device	*bridge_dev;
	u16			fdb_id;
	bool			in_use;
};

struct dpaa2_switch_acl_entry {
	struct list_head	list;
	u16			prio;
	unsigned long		cookie;

	struct dpsw_acl_entry_cfg cfg;
	struct dpsw_acl_key	key;
};

struct dpaa2_switch_mirror_entry {
	struct list_head	list;
	struct dpsw_reflection_cfg cfg;
	unsigned long		cookie;
	u16 if_id;
};

struct dpaa2_switch_filter_block {
	struct ethsw_core	*ethsw;
	u64			ports;
	bool			in_use;

	struct list_head	acl_entries;
	u16			acl_id;
	u8			num_acl_rules;

	struct list_head	mirror_entries;
};

static inline bool
dpaa2_switch_acl_tbl_is_full(struct dpaa2_switch_filter_block *filter_block)
{
	if ((filter_block->num_acl_rules + DPAA2_ETHSW_PORT_DEFAULT_TRAPS) >=
	    DPAA2_ETHSW_PORT_MAX_ACL_ENTRIES)
		return true;
	return false;
}

/* Per port private data */
struct ethsw_port_priv {
	struct net_device	*netdev;
	u16			idx;
	struct ethsw_core	*ethsw_data;
	u8			link_state;
	u8			stp_state;

	u8			vlans[VLAN_VID_MASK + 1];
	u16			pvid;
	u16			tx_qdid;

	struct dpaa2_switch_fdb	*fdb;
	bool			bcast_flood;
	bool			ucast_flood;
	bool			learn_ena;

	struct dpaa2_switch_filter_block *filter_block;
	struct dpaa2_mac	*mac;
	/* Protects against changes to port_priv->mac */
	struct mutex		mac_lock;
};

/* Switch data */
struct ethsw_core {
	struct device			*dev;
	struct fsl_mc_io		*mc_io;
	u16				dpsw_handle;
	struct dpsw_attr		sw_attr;
	u16				major, minor;
	unsigned long			features;
	int				dev_id;
	struct ethsw_port_priv		**ports;
	struct iommu_domain		*iommu_domain;

	u8				vlans[VLAN_VID_MASK + 1];

	struct workqueue_struct		*workqueue;

	struct dpaa2_switch_fq		fq[DPAA2_SWITCH_RX_NUM_FQS];
	struct fsl_mc_device		*dpbp_dev;
	int				buf_count;
	u16				bpid;
	int				napi_users;

	struct dpaa2_switch_fdb		*fdbs;
	struct dpaa2_switch_filter_block *filter_blocks;
	u16				mirror_port;
};

static inline int dpaa2_switch_get_index(struct ethsw_core *ethsw,
					 struct net_device *netdev)
{
	int i;

	for (i = 0; i < ethsw->sw_attr.num_ifs; i++)
		if (ethsw->ports[i]->netdev == netdev)
			return ethsw->ports[i]->idx;

	return -EINVAL;
}

static inline bool dpaa2_switch_supports_cpu_traffic(struct ethsw_core *ethsw)
{
	if (ethsw->sw_attr.options & DPSW_OPT_CTRL_IF_DIS) {
		dev_err(ethsw->dev, "Control Interface is disabled, cannot probe\n");
		return false;
	}

	if (ethsw->sw_attr.flooding_cfg != DPSW_FLOODING_PER_FDB) {
		dev_err(ethsw->dev, "Flooding domain is not per FDB, cannot probe\n");
		return false;
	}

	if (ethsw->sw_attr.broadcast_cfg != DPSW_BROADCAST_PER_FDB) {
		dev_err(ethsw->dev, "Broadcast domain is not per FDB, cannot probe\n");
		return false;
	}

	if (ethsw->sw_attr.max_fdbs < ethsw->sw_attr.num_ifs) {
		dev_err(ethsw->dev, "The number of FDBs is lower than the number of ports, cannot probe\n");
		return false;
	}

	return true;
}

static inline bool
dpaa2_switch_port_is_type_phy(struct ethsw_port_priv *port_priv)
{
	return dpaa2_mac_is_type_phy(port_priv->mac);
}

static inline bool dpaa2_switch_port_has_mac(struct ethsw_port_priv *port_priv)
{
	return port_priv->mac ? true : false;
}

bool dpaa2_switch_port_dev_check(const struct net_device *netdev);

int dpaa2_switch_port_vlans_add(struct net_device *netdev,
				const struct switchdev_obj_port_vlan *vlan);

int dpaa2_switch_port_vlans_del(struct net_device *netdev,
				const struct switchdev_obj_port_vlan *vlan);

typedef int dpaa2_switch_fdb_cb_t(struct ethsw_port_priv *port_priv,
				  struct fdb_dump_entry *fdb_entry,
				  void *data);

/* TC offload */

int dpaa2_switch_cls_flower_replace(struct dpaa2_switch_filter_block *block,
				    struct flow_cls_offload *cls);

int dpaa2_switch_cls_flower_destroy(struct dpaa2_switch_filter_block *block,
				    struct flow_cls_offload *cls);

int dpaa2_switch_cls_matchall_replace(struct dpaa2_switch_filter_block *block,
				      struct tc_cls_matchall_offload *cls);

int dpaa2_switch_cls_matchall_destroy(struct dpaa2_switch_filter_block *block,
				      struct tc_cls_matchall_offload *cls);

int dpaa2_switch_acl_entry_add(struct dpaa2_switch_filter_block *block,
			       struct dpaa2_switch_acl_entry *entry);

int dpaa2_switch_block_offload_mirror(struct dpaa2_switch_filter_block *block,
				      struct ethsw_port_priv *port_priv);

int dpaa2_switch_block_unoffload_mirror(struct dpaa2_switch_filter_block *block,
					struct ethsw_port_priv *port_priv);
#endif	/* __ETHSW_H */
