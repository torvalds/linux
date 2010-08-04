/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _ENIC_H_
#define _ENIC_H_

#include <linux/inet_lro.h>

#include "vnic_enet.h"
#include "vnic_dev.h"
#include "vnic_wq.h"
#include "vnic_rq.h"
#include "vnic_cq.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "vnic_nic.h"
#include "vnic_rss.h"

#define DRV_NAME		"enic"
#define DRV_DESCRIPTION		"Cisco VIC Ethernet NIC Driver"
#define DRV_VERSION		"1.3.1.1-pp"
#define DRV_COPYRIGHT		"Copyright 2008-2009 Cisco Systems, Inc"
#define PFX			DRV_NAME ": "

#define ENIC_LRO_MAX_DESC	8
#define ENIC_LRO_MAX_AGGR	64

#define ENIC_BARS_MAX		6

#define ENIC_WQ_MAX		8
#define ENIC_RQ_MAX		8
#define ENIC_CQ_MAX		(ENIC_WQ_MAX + ENIC_RQ_MAX)
#define ENIC_INTR_MAX		(ENIC_CQ_MAX + 2)

enum enic_cq_index {
	ENIC_CQ_RQ,
	ENIC_CQ_WQ,
};

enum enic_intx_intr_index {
	ENIC_INTX_WQ_RQ,
	ENIC_INTX_ERR,
	ENIC_INTX_NOTIFY,
};

enum enic_msix_intr_index {
	ENIC_MSIX_RQ,
	ENIC_MSIX_WQ,
	ENIC_MSIX_ERR,
	ENIC_MSIX_NOTIFY,
	ENIC_MSIX_MAX,
};

struct enic_msix_entry {
	int requested;
	char devname[IFNAMSIZ];
	irqreturn_t (*isr)(int, void *);
	void *devid;
};

#define ENIC_SET_APPLIED		(1 << 0)
#define ENIC_SET_REQUEST		(1 << 1)
#define ENIC_SET_NAME			(1 << 2)
#define ENIC_SET_INSTANCE		(1 << 3)
#define ENIC_SET_HOST			(1 << 4)

struct enic_port_profile {
	u32 set;
	u8 request;
	char name[PORT_PROFILE_MAX];
	u8 instance_uuid[PORT_UUID_MAX];
	u8 host_uuid[PORT_UUID_MAX];
};

/* Per-instance private data structure */
struct enic {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct vnic_enet_config config;
	struct vnic_dev_bar bar[ENIC_BARS_MAX];
	struct vnic_dev *vdev;
	struct timer_list notify_timer;
	struct work_struct reset;
	struct msix_entry msix_entry[ENIC_MSIX_MAX];
	struct enic_msix_entry msix[ENIC_MSIX_MAX];
	u32 msg_enable;
	spinlock_t devcmd_lock;
	u8 mac_addr[ETH_ALEN];
	u8 mc_addr[ENIC_MULTICAST_PERFECT_FILTERS][ETH_ALEN];
	unsigned int flags;
	unsigned int mc_count;
	int csum_rx_enabled;
	u32 port_mtu;
	u32 rx_coalesce_usecs;
	u32 tx_coalesce_usecs;
	struct enic_port_profile pp;

	/* work queue cache line section */
	____cacheline_aligned struct vnic_wq wq[ENIC_WQ_MAX];
	spinlock_t wq_lock[ENIC_WQ_MAX];
	unsigned int wq_count;
	struct vlan_group *vlan_group;

	/* receive queue cache line section */
	____cacheline_aligned struct vnic_rq rq[ENIC_RQ_MAX];
	unsigned int rq_count;
	int (*rq_alloc_buf)(struct vnic_rq *rq);
	u64 rq_truncated_pkts;
	u64 rq_bad_fcs;
	struct napi_struct napi;
	struct net_lro_mgr lro_mgr;
	struct net_lro_desc lro_desc[ENIC_LRO_MAX_DESC];

	/* interrupt resource cache line section */
	____cacheline_aligned struct vnic_intr intr[ENIC_INTR_MAX];
	unsigned int intr_count;
	u32 __iomem *legacy_pba;		/* memory-mapped */

	/* completion queue cache line section */
	____cacheline_aligned struct vnic_cq cq[ENIC_CQ_MAX];
	unsigned int cq_count;
};

#endif /* _ENIC_H_ */
