/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Linux driver for VMware's vmxnet3 ethernet NIC.
 * Copyright (C) 2008-2023, VMware, Inc. All Rights Reserved.
 * Maintained by: pv-drivers@vmware.com
 *
 */

#ifndef _VMXNET3_XDP_H
#define _VMXNET3_XDP_H

#include <linux/filter.h>
#include <linux/bpf_trace.h>
#include <linux/netlink.h>

#include "vmxnet3_int.h"

#define VMXNET3_XDP_HEADROOM	(XDP_PACKET_HEADROOM + NET_IP_ALIGN)
#define VMXNET3_XDP_RX_TAILROOM	SKB_DATA_ALIGN(sizeof(struct skb_shared_info))
#define VMXNET3_XDP_RX_OFFSET	VMXNET3_XDP_HEADROOM
#define VMXNET3_XDP_MAX_FRSIZE	(PAGE_SIZE - VMXNET3_XDP_HEADROOM - \
				 VMXNET3_XDP_RX_TAILROOM)
#define VMXNET3_XDP_MAX_MTU	(VMXNET3_XDP_MAX_FRSIZE - ETH_HLEN - \
				 2 * VLAN_HLEN - ETH_FCS_LEN)

int vmxnet3_xdp(struct net_device *netdev, struct netdev_bpf *bpf);
int vmxnet3_xdp_xmit(struct net_device *dev, int n, struct xdp_frame **frames,
		     u32 flags);
int vmxnet3_process_xdp(struct vmxnet3_adapter *adapter,
			struct vmxnet3_rx_queue *rq,
			struct Vmxnet3_RxCompDesc *rcd,
			struct vmxnet3_rx_buf_info *rbi,
			struct Vmxnet3_RxDesc *rxd,
			struct sk_buff **skb_xdp_pass);
int vmxnet3_process_xdp_small(struct vmxnet3_adapter *adapter,
			      struct vmxnet3_rx_queue *rq,
			      void *data, int len,
			      struct sk_buff **skb_xdp_pass);
void *vmxnet3_pp_get_buff(struct page_pool *pp, dma_addr_t *dma_addr,
			  gfp_t gfp_mask);

static inline bool vmxnet3_xdp_enabled(struct vmxnet3_adapter *adapter)
{
	return !!rcu_access_pointer(adapter->xdp_bpf_prog);
}

#endif
