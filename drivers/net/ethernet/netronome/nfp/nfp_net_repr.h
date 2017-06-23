/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef NFP_NET_REPR_H
#define NFP_NET_REPR_H

struct metadata_dst;
struct nfp_net;
struct nfp_port;

/**
 * struct nfp_reprs - container for representor netdevs
 * @num_reprs:	Number of elements in reprs array
 * @reprs:	Array of representor netdevs
 */
struct nfp_reprs {
	unsigned int num_reprs;
	struct net_device *reprs[0];
};

/**
 * struct nfp_repr_pcpu_stats
 * @rx_packets:	Received packets
 * @rx_bytes:	Received bytes
 * @tx_packets:	Transmitted packets
 * @tx_bytes:	Transmitted dropped
 * @tx_drops:	Packets dropped on transmit
 * @syncp:	Reference count
 */
struct nfp_repr_pcpu_stats {
	u64 rx_packets;
	u64 rx_bytes;
	u64 tx_packets;
	u64 tx_bytes;
	u64 tx_drops;
	struct u64_stats_sync syncp;
};

/**
 * struct nfp_repr - priv data for representor netdevs
 * @netdev:	Back pointer to netdev
 * @dst:	Destination for packet TX
 * @port:	Port of representor
 * @app:	APP handle
 * @stats:	Statistic of packets hitting CPU
 */
struct nfp_repr {
	struct net_device *netdev;
	struct metadata_dst *dst;
	struct nfp_port *port;
	struct nfp_app *app;
	struct nfp_repr_pcpu_stats __percpu *stats;
};

/**
 * enum nfp_repr_type - type of representor
 * @NFP_REPR_TYPE_PHYS_PORT:	external NIC port
 * @NFP_REPR_TYPE_PF:		physical function
 * @NFP_REPR_TYPE_VF:		virtual function
 */
enum nfp_repr_type {
	NFP_REPR_TYPE_PHYS_PORT,
	NFP_REPR_TYPE_PF,
	NFP_REPR_TYPE_VF,

	__NFP_REPR_TYPE_MAX,
};
#define NFP_REPR_TYPE_MAX (__NFP_REPR_TYPE_MAX - 1)

void nfp_repr_inc_rx_stats(struct net_device *netdev, unsigned int len);
void
nfp_repr_get_stats64(const struct nfp_app *app, enum nfp_repr_type type,
		     u8 port, struct rtnl_link_stats64 *stats);
bool nfp_repr_has_offload_stats(const struct net_device *dev, int attr_id);
int nfp_repr_get_offload_stats(int attr_id, const struct net_device *dev,
			       void *stats);
netdev_tx_t nfp_repr_xmit(struct sk_buff *skb, struct net_device *netdev);
int nfp_repr_init(struct nfp_app *app, struct net_device *netdev,
		  const struct net_device_ops *netdev_ops,
		  u32 cmsg_port_id, struct nfp_port *port,
		  struct net_device *pf_netdev);
struct net_device *nfp_repr_alloc(struct nfp_app *app);
void
nfp_reprs_clean_and_free(struct nfp_reprs *reprs);
void
nfp_reprs_clean_and_free_by_type(struct nfp_app *app,
				 enum nfp_repr_type type);
struct nfp_reprs *nfp_reprs_alloc(unsigned int num_reprs);

#endif /* NFP_NET_REPR_H */
