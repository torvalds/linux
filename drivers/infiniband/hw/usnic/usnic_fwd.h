/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#ifndef USNIC_FWD_H_
#define USNIC_FWD_H_

#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/in.h>

#include "usnic_abi.h"
#include "usnic_common_pkt_hdr.h"
#include "vnic_devcmd.h"

struct usnic_fwd_dev {
	struct pci_dev			*pdev;
	struct net_device		*netdev;
	spinlock_t			lock;
	/*
	 * The following fields can be read directly off the device.
	 * However, they should be set by a accessor function, except name,
	 * which cannot be changed.
	 */
	bool				link_up;
	char				mac[ETH_ALEN];
	unsigned int			mtu;
	__be32				inaddr;
	char				name[IFNAMSIZ+1];
};

struct usnic_fwd_flow {
	uint32_t			flow_id;
	struct usnic_fwd_dev		*ufdev;
	unsigned int			vnic_idx;
};

struct usnic_filter_action {
	int				vnic_idx;
	struct filter_action		action;
};

struct usnic_fwd_dev *usnic_fwd_dev_alloc(struct pci_dev *pdev);
void usnic_fwd_dev_free(struct usnic_fwd_dev *ufdev);

void usnic_fwd_set_mac(struct usnic_fwd_dev *ufdev, char mac[ETH_ALEN]);
int usnic_fwd_add_ipaddr(struct usnic_fwd_dev *ufdev, __be32 inaddr);
void usnic_fwd_del_ipaddr(struct usnic_fwd_dev *ufdev);
void usnic_fwd_carrier_up(struct usnic_fwd_dev *ufdev);
void usnic_fwd_carrier_down(struct usnic_fwd_dev *ufdev);
void usnic_fwd_set_mtu(struct usnic_fwd_dev *ufdev, unsigned int mtu);

/*
 * Allocate a flow on this forwarding device. Whoever calls this function,
 * must monitor netdev events on ufdev's netdevice. If NETDEV_REBOOT or
 * NETDEV_DOWN is seen, flow will no longer function and must be
 * immediately freed by calling usnic_dealloc_flow.
 */
struct usnic_fwd_flow*
usnic_fwd_alloc_flow(struct usnic_fwd_dev *ufdev, struct filter *filter,
				struct usnic_filter_action *action);
int usnic_fwd_dealloc_flow(struct usnic_fwd_flow *flow);
int usnic_fwd_enable_qp(struct usnic_fwd_dev *ufdev, int vnic_idx, int qp_idx);
int usnic_fwd_disable_qp(struct usnic_fwd_dev *ufdev, int vnic_idx, int qp_idx);

static inline void usnic_fwd_init_usnic_filter(struct filter *filter,
						uint32_t usnic_id)
{
	filter->type = FILTER_USNIC_ID;
	filter->u.usnic.ethtype = USNIC_ROCE_ETHERTYPE;
	filter->u.usnic.flags = FILTER_FIELD_USNIC_ETHTYPE |
				FILTER_FIELD_USNIC_ID |
				FILTER_FIELD_USNIC_PROTO;
	filter->u.usnic.proto_version = (USNIC_ROCE_GRH_VER <<
					 USNIC_ROCE_GRH_VER_SHIFT) |
					 USNIC_PROTO_VER;
	filter->u.usnic.usnic_id = usnic_id;
}

static inline void usnic_fwd_init_udp_filter(struct filter *filter,
						uint32_t daddr, uint16_t dport)
{
	filter->type = FILTER_IPV4_5TUPLE;
	filter->u.ipv4.flags = FILTER_FIELD_5TUP_PROTO;
	filter->u.ipv4.protocol = PROTO_UDP;

	if (daddr) {
		filter->u.ipv4.flags |= FILTER_FIELD_5TUP_DST_AD;
		filter->u.ipv4.dst_addr = daddr;
	}

	if (dport) {
		filter->u.ipv4.flags |= FILTER_FIELD_5TUP_DST_PT;
		filter->u.ipv4.dst_port = dport;
	}
}

#endif /* !USNIC_FWD_H_ */
