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
#include <soc/fsl/dpaa2-io.h>

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

extern const struct ethtool_ops dpaa2_switch_port_ethtool_ops;

struct ethsw_core;

struct dpaa2_switch_fq {
	struct ethsw_core *ethsw;
	enum dpsw_queue_type type;
	struct dpaa2_io_store *store;
	u32 fqid;
};

/* Per port private data */
struct ethsw_port_priv {
	struct net_device	*netdev;
	u16			idx;
	struct ethsw_core	*ethsw_data;
	u8			link_state;
	u8			stp_state;
	bool			flood;

	u8			vlans[VLAN_VID_MASK + 1];
	u16			pvid;
	struct net_device	*bridge_dev;
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

	u8				vlans[VLAN_VID_MASK + 1];

	struct notifier_block		port_nb;
	struct notifier_block		port_switchdev_nb;
	struct notifier_block		port_switchdevb_nb;
	struct workqueue_struct		*workqueue;

	struct dpaa2_switch_fq		fq[DPAA2_SWITCH_RX_NUM_FQS];
	struct fsl_mc_device		*dpbp_dev;
	u16				bpid;
};

static inline bool dpaa2_switch_supports_cpu_traffic(struct ethsw_core *ethsw)
{
	if (ethsw->sw_attr.options & DPSW_OPT_CTRL_IF_DIS) {
		dev_err(ethsw->dev, "Control Interface is disabled, cannot probe\n");
		return false;
	}

	return true;
}
#endif	/* __ETHSW_H */
