/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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

#include <linux/bpf.h>
#include <linux/etherdevice.h>
#include <linux/tcp.h>
#include <linux/if_vlan.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/hash.h>
#include <net/ip.h>
#include <net/busy_poll.h>
#include <net/vxlan.h>
#include <net/devlink.h>

#include <linux/mlx4/driver.h>
#include <linux/mlx4/device.h>
#include <linux/mlx4/cmd.h>
#include <linux/mlx4/cq.h>

#include "mlx4_en.h"
#include "en_port.h"

#define MLX4_EN_MAX_XDP_MTU ((int)(PAGE_SIZE - ETH_HLEN - (2 * VLAN_HLEN) - \
				   XDP_PACKET_HEADROOM))

int mlx4_en_setup_tc(struct net_device *dev, u8 up)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	int i;
	unsigned int offset = 0;

	if (up && up != MLX4_EN_NUM_UP)
		return -EINVAL;

	netdev_set_num_tc(dev, up);

	/* Partition Tx queues evenly amongst UP's */
	for (i = 0; i < up; i++) {
		netdev_set_tc_queue(dev, i, priv->num_tx_rings_p_up, offset);
		offset += priv->num_tx_rings_p_up;
	}

#ifdef CONFIG_MLX4_EN_DCB
	if (!mlx4_is_slave(priv->mdev->dev)) {
		if (up) {
			if (priv->dcbx_cap)
				priv->flags |= MLX4_EN_FLAG_DCB_ENABLED;
		} else {
			priv->flags &= ~MLX4_EN_FLAG_DCB_ENABLED;
			priv->cee_config.pfc_state = false;
		}
	}
#endif /* CONFIG_MLX4_EN_DCB */

	return 0;
}

static int __mlx4_en_setup_tc(struct net_device *dev, u32 handle, __be16 proto,
			      struct tc_to_netdev *tc)
{
	if (tc->type != TC_SETUP_MQPRIO)
		return -EINVAL;

	return mlx4_en_setup_tc(dev, tc->tc);
}

#ifdef CONFIG_RFS_ACCEL

struct mlx4_en_filter {
	struct list_head next;
	struct work_struct work;

	u8     ip_proto;
	__be32 src_ip;
	__be32 dst_ip;
	__be16 src_port;
	__be16 dst_port;

	int rxq_index;
	struct mlx4_en_priv *priv;
	u32 flow_id;			/* RFS infrastructure id */
	int id;				/* mlx4_en driver id */
	u64 reg_id;			/* Flow steering API id */
	u8 activated;			/* Used to prevent expiry before filter
					 * is attached
					 */
	struct hlist_node filter_chain;
};

static void mlx4_en_filter_rfs_expire(struct mlx4_en_priv *priv);

static enum mlx4_net_trans_rule_id mlx4_ip_proto_to_trans_rule_id(u8 ip_proto)
{
	switch (ip_proto) {
	case IPPROTO_UDP:
		return MLX4_NET_TRANS_RULE_ID_UDP;
	case IPPROTO_TCP:
		return MLX4_NET_TRANS_RULE_ID_TCP;
	default:
		return MLX4_NET_TRANS_RULE_NUM;
	}
};

/* Must not acquire state_lock, as its corresponding work_sync
 * is done under it.
 */
static void mlx4_en_filter_work(struct work_struct *work)
{
	struct mlx4_en_filter *filter = container_of(work,
						     struct mlx4_en_filter,
						     work);
	struct mlx4_en_priv *priv = filter->priv;
	struct mlx4_spec_list spec_tcp_udp = {
		.id = mlx4_ip_proto_to_trans_rule_id(filter->ip_proto),
		{
			.tcp_udp = {
				.dst_port = filter->dst_port,
				.dst_port_msk = (__force __be16)-1,
				.src_port = filter->src_port,
				.src_port_msk = (__force __be16)-1,
			},
		},
	};
	struct mlx4_spec_list spec_ip = {
		.id = MLX4_NET_TRANS_RULE_ID_IPV4,
		{
			.ipv4 = {
				.dst_ip = filter->dst_ip,
				.dst_ip_msk = (__force __be32)-1,
				.src_ip = filter->src_ip,
				.src_ip_msk = (__force __be32)-1,
			},
		},
	};
	struct mlx4_spec_list spec_eth = {
		.id = MLX4_NET_TRANS_RULE_ID_ETH,
	};
	struct mlx4_net_trans_rule rule = {
		.list = LIST_HEAD_INIT(rule.list),
		.queue_mode = MLX4_NET_TRANS_Q_LIFO,
		.exclusive = 1,
		.allow_loopback = 1,
		.promisc_mode = MLX4_FS_REGULAR,
		.port = priv->port,
		.priority = MLX4_DOMAIN_RFS,
	};
	int rc;
	__be64 mac_mask = cpu_to_be64(MLX4_MAC_MASK << 16);

	if (spec_tcp_udp.id >= MLX4_NET_TRANS_RULE_NUM) {
		en_warn(priv, "RFS: ignoring unsupported ip protocol (%d)\n",
			filter->ip_proto);
		goto ignore;
	}
	list_add_tail(&spec_eth.list, &rule.list);
	list_add_tail(&spec_ip.list, &rule.list);
	list_add_tail(&spec_tcp_udp.list, &rule.list);

	rule.qpn = priv->rss_map.qps[filter->rxq_index].qpn;
	memcpy(spec_eth.eth.dst_mac, priv->dev->dev_addr, ETH_ALEN);
	memcpy(spec_eth.eth.dst_mac_msk, &mac_mask, ETH_ALEN);

	filter->activated = 0;

	if (filter->reg_id) {
		rc = mlx4_flow_detach(priv->mdev->dev, filter->reg_id);
		if (rc && rc != -ENOENT)
			en_err(priv, "Error detaching flow. rc = %d\n", rc);
	}

	rc = mlx4_flow_attach(priv->mdev->dev, &rule, &filter->reg_id);
	if (rc)
		en_err(priv, "Error attaching flow. err = %d\n", rc);

ignore:
	mlx4_en_filter_rfs_expire(priv);

	filter->activated = 1;
}

static inline struct hlist_head *
filter_hash_bucket(struct mlx4_en_priv *priv, __be32 src_ip, __be32 dst_ip,
		   __be16 src_port, __be16 dst_port)
{
	unsigned long l;
	int bucket_idx;

	l = (__force unsigned long)src_port |
	    ((__force unsigned long)dst_port << 2);
	l ^= (__force unsigned long)(src_ip ^ dst_ip);

	bucket_idx = hash_long(l, MLX4_EN_FILTER_HASH_SHIFT);

	return &priv->filter_hash[bucket_idx];
}

static struct mlx4_en_filter *
mlx4_en_filter_alloc(struct mlx4_en_priv *priv, int rxq_index, __be32 src_ip,
		     __be32 dst_ip, u8 ip_proto, __be16 src_port,
		     __be16 dst_port, u32 flow_id)
{
	struct mlx4_en_filter *filter = NULL;

	filter = kzalloc(sizeof(struct mlx4_en_filter), GFP_ATOMIC);
	if (!filter)
		return NULL;

	filter->priv = priv;
	filter->rxq_index = rxq_index;
	INIT_WORK(&filter->work, mlx4_en_filter_work);

	filter->src_ip = src_ip;
	filter->dst_ip = dst_ip;
	filter->ip_proto = ip_proto;
	filter->src_port = src_port;
	filter->dst_port = dst_port;

	filter->flow_id = flow_id;

	filter->id = priv->last_filter_id++ % RPS_NO_FILTER;

	list_add_tail(&filter->next, &priv->filters);
	hlist_add_head(&filter->filter_chain,
		       filter_hash_bucket(priv, src_ip, dst_ip, src_port,
					  dst_port));

	return filter;
}

static void mlx4_en_filter_free(struct mlx4_en_filter *filter)
{
	struct mlx4_en_priv *priv = filter->priv;
	int rc;

	list_del(&filter->next);

	rc = mlx4_flow_detach(priv->mdev->dev, filter->reg_id);
	if (rc && rc != -ENOENT)
		en_err(priv, "Error detaching flow. rc = %d\n", rc);

	kfree(filter);
}

static inline struct mlx4_en_filter *
mlx4_en_filter_find(struct mlx4_en_priv *priv, __be32 src_ip, __be32 dst_ip,
		    u8 ip_proto, __be16 src_port, __be16 dst_port)
{
	struct mlx4_en_filter *filter;
	struct mlx4_en_filter *ret = NULL;

	hlist_for_each_entry(filter,
			     filter_hash_bucket(priv, src_ip, dst_ip,
						src_port, dst_port),
			     filter_chain) {
		if (filter->src_ip == src_ip &&
		    filter->dst_ip == dst_ip &&
		    filter->ip_proto == ip_proto &&
		    filter->src_port == src_port &&
		    filter->dst_port == dst_port) {
			ret = filter;
			break;
		}
	}

	return ret;
}

static int
mlx4_en_filter_rfs(struct net_device *net_dev, const struct sk_buff *skb,
		   u16 rxq_index, u32 flow_id)
{
	struct mlx4_en_priv *priv = netdev_priv(net_dev);
	struct mlx4_en_filter *filter;
	const struct iphdr *ip;
	const __be16 *ports;
	u8 ip_proto;
	__be32 src_ip;
	__be32 dst_ip;
	__be16 src_port;
	__be16 dst_port;
	int nhoff = skb_network_offset(skb);
	int ret = 0;

	if (skb->protocol != htons(ETH_P_IP))
		return -EPROTONOSUPPORT;

	ip = (const struct iphdr *)(skb->data + nhoff);
	if (ip_is_fragment(ip))
		return -EPROTONOSUPPORT;

	if ((ip->protocol != IPPROTO_TCP) && (ip->protocol != IPPROTO_UDP))
		return -EPROTONOSUPPORT;
	ports = (const __be16 *)(skb->data + nhoff + 4 * ip->ihl);

	ip_proto = ip->protocol;
	src_ip = ip->saddr;
	dst_ip = ip->daddr;
	src_port = ports[0];
	dst_port = ports[1];

	spin_lock_bh(&priv->filters_lock);
	filter = mlx4_en_filter_find(priv, src_ip, dst_ip, ip_proto,
				     src_port, dst_port);
	if (filter) {
		if (filter->rxq_index == rxq_index)
			goto out;

		filter->rxq_index = rxq_index;
	} else {
		filter = mlx4_en_filter_alloc(priv, rxq_index,
					      src_ip, dst_ip, ip_proto,
					      src_port, dst_port, flow_id);
		if (!filter) {
			ret = -ENOMEM;
			goto err;
		}
	}

	queue_work(priv->mdev->workqueue, &filter->work);

out:
	ret = filter->id;
err:
	spin_unlock_bh(&priv->filters_lock);

	return ret;
}

void mlx4_en_cleanup_filters(struct mlx4_en_priv *priv)
{
	struct mlx4_en_filter *filter, *tmp;
	LIST_HEAD(del_list);

	spin_lock_bh(&priv->filters_lock);
	list_for_each_entry_safe(filter, tmp, &priv->filters, next) {
		list_move(&filter->next, &del_list);
		hlist_del(&filter->filter_chain);
	}
	spin_unlock_bh(&priv->filters_lock);

	list_for_each_entry_safe(filter, tmp, &del_list, next) {
		cancel_work_sync(&filter->work);
		mlx4_en_filter_free(filter);
	}
}

static void mlx4_en_filter_rfs_expire(struct mlx4_en_priv *priv)
{
	struct mlx4_en_filter *filter = NULL, *tmp, *last_filter = NULL;
	LIST_HEAD(del_list);
	int i = 0;

	spin_lock_bh(&priv->filters_lock);
	list_for_each_entry_safe(filter, tmp, &priv->filters, next) {
		if (i > MLX4_EN_FILTER_EXPIRY_QUOTA)
			break;

		if (filter->activated &&
		    !work_pending(&filter->work) &&
		    rps_may_expire_flow(priv->dev,
					filter->rxq_index, filter->flow_id,
					filter->id)) {
			list_move(&filter->next, &del_list);
			hlist_del(&filter->filter_chain);
		} else
			last_filter = filter;

		i++;
	}

	if (last_filter && (&last_filter->next != priv->filters.next))
		list_move(&priv->filters, &last_filter->next);

	spin_unlock_bh(&priv->filters_lock);

	list_for_each_entry_safe(filter, tmp, &del_list, next)
		mlx4_en_filter_free(filter);
}
#endif

static int mlx4_en_vlan_rx_add_vid(struct net_device *dev,
				   __be16 proto, u16 vid)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;
	int idx;

	en_dbg(HW, priv, "adding VLAN:%d\n", vid);

	set_bit(vid, priv->active_vlans);

	/* Add VID to port VLAN filter */
	mutex_lock(&mdev->state_lock);
	if (mdev->device_up && priv->port_up) {
		err = mlx4_SET_VLAN_FLTR(mdev->dev, priv);
		if (err) {
			en_err(priv, "Failed configuring VLAN filter\n");
			goto out;
		}
	}
	err = mlx4_register_vlan(mdev->dev, priv->port, vid, &idx);
	if (err)
		en_dbg(HW, priv, "Failed adding vlan %d\n", vid);

out:
	mutex_unlock(&mdev->state_lock);
	return err;
}

static int mlx4_en_vlan_rx_kill_vid(struct net_device *dev,
				    __be16 proto, u16 vid)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err = 0;

	en_dbg(HW, priv, "Killing VID:%d\n", vid);

	clear_bit(vid, priv->active_vlans);

	/* Remove VID from port VLAN filter */
	mutex_lock(&mdev->state_lock);
	mlx4_unregister_vlan(mdev->dev, priv->port, vid);

	if (mdev->device_up && priv->port_up) {
		err = mlx4_SET_VLAN_FLTR(mdev->dev, priv);
		if (err)
			en_err(priv, "Failed configuring VLAN filter\n");
	}
	mutex_unlock(&mdev->state_lock);

	return err;
}

static void mlx4_en_u64_to_mac(unsigned char dst_mac[ETH_ALEN + 2], u64 src_mac)
{
	int i;
	for (i = ETH_ALEN - 1; i >= 0; --i) {
		dst_mac[i] = src_mac & 0xff;
		src_mac >>= 8;
	}
	memset(&dst_mac[ETH_ALEN], 0, 2);
}


static int mlx4_en_tunnel_steer_add(struct mlx4_en_priv *priv, unsigned char *addr,
				    int qpn, u64 *reg_id)
{
	int err;

	if (priv->mdev->dev->caps.tunnel_offload_mode != MLX4_TUNNEL_OFFLOAD_MODE_VXLAN ||
	    priv->mdev->dev->caps.dmfs_high_steer_mode == MLX4_STEERING_DMFS_A0_STATIC)
		return 0; /* do nothing */

	err = mlx4_tunnel_steer_add(priv->mdev->dev, addr, priv->port, qpn,
				    MLX4_DOMAIN_NIC, reg_id);
	if (err) {
		en_err(priv, "failed to add vxlan steering rule, err %d\n", err);
		return err;
	}
	en_dbg(DRV, priv, "added vxlan steering rule, mac %pM reg_id %llx\n", addr, *reg_id);
	return 0;
}


static int mlx4_en_uc_steer_add(struct mlx4_en_priv *priv,
				unsigned char *mac, int *qpn, u64 *reg_id)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_dev *dev = mdev->dev;
	int err;

	switch (dev->caps.steering_mode) {
	case MLX4_STEERING_MODE_B0: {
		struct mlx4_qp qp;
		u8 gid[16] = {0};

		qp.qpn = *qpn;
		memcpy(&gid[10], mac, ETH_ALEN);
		gid[5] = priv->port;

		err = mlx4_unicast_attach(dev, &qp, gid, 0, MLX4_PROT_ETH);
		break;
	}
	case MLX4_STEERING_MODE_DEVICE_MANAGED: {
		struct mlx4_spec_list spec_eth = { {NULL} };
		__be64 mac_mask = cpu_to_be64(MLX4_MAC_MASK << 16);

		struct mlx4_net_trans_rule rule = {
			.queue_mode = MLX4_NET_TRANS_Q_FIFO,
			.exclusive = 0,
			.allow_loopback = 1,
			.promisc_mode = MLX4_FS_REGULAR,
			.priority = MLX4_DOMAIN_NIC,
		};

		rule.port = priv->port;
		rule.qpn = *qpn;
		INIT_LIST_HEAD(&rule.list);

		spec_eth.id = MLX4_NET_TRANS_RULE_ID_ETH;
		memcpy(spec_eth.eth.dst_mac, mac, ETH_ALEN);
		memcpy(spec_eth.eth.dst_mac_msk, &mac_mask, ETH_ALEN);
		list_add_tail(&spec_eth.list, &rule.list);

		err = mlx4_flow_attach(dev, &rule, reg_id);
		break;
	}
	default:
		return -EINVAL;
	}
	if (err)
		en_warn(priv, "Failed Attaching Unicast\n");

	return err;
}

static void mlx4_en_uc_steer_release(struct mlx4_en_priv *priv,
				     unsigned char *mac, int qpn, u64 reg_id)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_dev *dev = mdev->dev;

	switch (dev->caps.steering_mode) {
	case MLX4_STEERING_MODE_B0: {
		struct mlx4_qp qp;
		u8 gid[16] = {0};

		qp.qpn = qpn;
		memcpy(&gid[10], mac, ETH_ALEN);
		gid[5] = priv->port;

		mlx4_unicast_detach(dev, &qp, gid, MLX4_PROT_ETH);
		break;
	}
	case MLX4_STEERING_MODE_DEVICE_MANAGED: {
		mlx4_flow_detach(dev, reg_id);
		break;
	}
	default:
		en_err(priv, "Invalid steering mode.\n");
	}
}

static int mlx4_en_get_qp(struct mlx4_en_priv *priv)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_dev *dev = mdev->dev;
	int index = 0;
	int err = 0;
	int *qpn = &priv->base_qpn;
	u64 mac = mlx4_mac_to_u64(priv->dev->dev_addr);

	en_dbg(DRV, priv, "Registering MAC: %pM for adding\n",
	       priv->dev->dev_addr);
	index = mlx4_register_mac(dev, priv->port, mac);
	if (index < 0) {
		err = index;
		en_err(priv, "Failed adding MAC: %pM\n",
		       priv->dev->dev_addr);
		return err;
	}

	if (dev->caps.steering_mode == MLX4_STEERING_MODE_A0) {
		int base_qpn = mlx4_get_base_qpn(dev, priv->port);
		*qpn = base_qpn + index;
		return 0;
	}

	err = mlx4_qp_reserve_range(dev, 1, 1, qpn, MLX4_RESERVE_A0_QP);
	en_dbg(DRV, priv, "Reserved qp %d\n", *qpn);
	if (err) {
		en_err(priv, "Failed to reserve qp for mac registration\n");
		mlx4_unregister_mac(dev, priv->port, mac);
		return err;
	}

	return 0;
}

static void mlx4_en_put_qp(struct mlx4_en_priv *priv)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_dev *dev = mdev->dev;
	int qpn = priv->base_qpn;

	if (dev->caps.steering_mode == MLX4_STEERING_MODE_A0) {
		u64 mac = mlx4_mac_to_u64(priv->dev->dev_addr);
		en_dbg(DRV, priv, "Registering MAC: %pM for deleting\n",
		       priv->dev->dev_addr);
		mlx4_unregister_mac(dev, priv->port, mac);
	} else {
		en_dbg(DRV, priv, "Releasing qp: port %d, qpn %d\n",
		       priv->port, qpn);
		mlx4_qp_release_range(dev, qpn, 1);
		priv->flags &= ~MLX4_EN_FLAG_FORCE_PROMISC;
	}
}

static int mlx4_en_replace_mac(struct mlx4_en_priv *priv, int qpn,
			       unsigned char *new_mac, unsigned char *prev_mac)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_dev *dev = mdev->dev;
	int err = 0;
	u64 new_mac_u64 = mlx4_mac_to_u64(new_mac);

	if (dev->caps.steering_mode != MLX4_STEERING_MODE_A0) {
		struct hlist_head *bucket;
		unsigned int mac_hash;
		struct mlx4_mac_entry *entry;
		struct hlist_node *tmp;
		u64 prev_mac_u64 = mlx4_mac_to_u64(prev_mac);

		bucket = &priv->mac_hash[prev_mac[MLX4_EN_MAC_HASH_IDX]];
		hlist_for_each_entry_safe(entry, tmp, bucket, hlist) {
			if (ether_addr_equal_64bits(entry->mac, prev_mac)) {
				mlx4_en_uc_steer_release(priv, entry->mac,
							 qpn, entry->reg_id);
				mlx4_unregister_mac(dev, priv->port,
						    prev_mac_u64);
				hlist_del_rcu(&entry->hlist);
				synchronize_rcu();
				memcpy(entry->mac, new_mac, ETH_ALEN);
				entry->reg_id = 0;
				mac_hash = new_mac[MLX4_EN_MAC_HASH_IDX];
				hlist_add_head_rcu(&entry->hlist,
						   &priv->mac_hash[mac_hash]);
				mlx4_register_mac(dev, priv->port, new_mac_u64);
				err = mlx4_en_uc_steer_add(priv, new_mac,
							   &qpn,
							   &entry->reg_id);
				if (err)
					return err;
				if (priv->tunnel_reg_id) {
					mlx4_flow_detach(priv->mdev->dev, priv->tunnel_reg_id);
					priv->tunnel_reg_id = 0;
				}
				err = mlx4_en_tunnel_steer_add(priv, new_mac, qpn,
							       &priv->tunnel_reg_id);
				return err;
			}
		}
		return -EINVAL;
	}

	return __mlx4_replace_mac(dev, priv->port, qpn, new_mac_u64);
}

static int mlx4_en_do_set_mac(struct mlx4_en_priv *priv,
			      unsigned char new_mac[ETH_ALEN + 2])
{
	int err = 0;

	if (priv->port_up) {
		/* Remove old MAC and insert the new one */
		err = mlx4_en_replace_mac(priv, priv->base_qpn,
					  new_mac, priv->current_mac);
		if (err)
			en_err(priv, "Failed changing HW MAC address\n");
	} else
		en_dbg(HW, priv, "Port is down while registering mac, exiting...\n");

	if (!err)
		memcpy(priv->current_mac, new_mac, sizeof(priv->current_mac));

	return err;
}

static int mlx4_en_set_mac(struct net_device *dev, void *addr)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct sockaddr *saddr = addr;
	unsigned char new_mac[ETH_ALEN + 2];
	int err;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	mutex_lock(&mdev->state_lock);
	memcpy(new_mac, saddr->sa_data, ETH_ALEN);
	err = mlx4_en_do_set_mac(priv, new_mac);
	if (!err)
		memcpy(dev->dev_addr, saddr->sa_data, ETH_ALEN);
	mutex_unlock(&mdev->state_lock);

	return err;
}

static void mlx4_en_clear_list(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_mc_list *tmp, *mc_to_del;

	list_for_each_entry_safe(mc_to_del, tmp, &priv->mc_list, list) {
		list_del(&mc_to_del->list);
		kfree(mc_to_del);
	}
}

static void mlx4_en_cache_mclist(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	struct mlx4_en_mc_list *tmp;

	mlx4_en_clear_list(dev);
	netdev_for_each_mc_addr(ha, dev) {
		tmp = kzalloc(sizeof(struct mlx4_en_mc_list), GFP_ATOMIC);
		if (!tmp) {
			mlx4_en_clear_list(dev);
			return;
		}
		memcpy(tmp->addr, ha->addr, ETH_ALEN);
		list_add_tail(&tmp->list, &priv->mc_list);
	}
}

static void update_mclist_flags(struct mlx4_en_priv *priv,
				struct list_head *dst,
				struct list_head *src)
{
	struct mlx4_en_mc_list *dst_tmp, *src_tmp, *new_mc;
	bool found;

	/* Find all the entries that should be removed from dst,
	 * These are the entries that are not found in src
	 */
	list_for_each_entry(dst_tmp, dst, list) {
		found = false;
		list_for_each_entry(src_tmp, src, list) {
			if (ether_addr_equal(dst_tmp->addr, src_tmp->addr)) {
				found = true;
				break;
			}
		}
		if (!found)
			dst_tmp->action = MCLIST_REM;
	}

	/* Add entries that exist in src but not in dst
	 * mark them as need to add
	 */
	list_for_each_entry(src_tmp, src, list) {
		found = false;
		list_for_each_entry(dst_tmp, dst, list) {
			if (ether_addr_equal(dst_tmp->addr, src_tmp->addr)) {
				dst_tmp->action = MCLIST_NONE;
				found = true;
				break;
			}
		}
		if (!found) {
			new_mc = kmemdup(src_tmp,
					 sizeof(struct mlx4_en_mc_list),
					 GFP_KERNEL);
			if (!new_mc)
				return;

			new_mc->action = MCLIST_ADD;
			list_add_tail(&new_mc->list, dst);
		}
	}
}

static void mlx4_en_set_rx_mode(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	if (!priv->port_up)
		return;

	queue_work(priv->mdev->workqueue, &priv->rx_mode_task);
}

static void mlx4_en_set_promisc_mode(struct mlx4_en_priv *priv,
				     struct mlx4_en_dev *mdev)
{
	int err = 0;

	if (!(priv->flags & MLX4_EN_FLAG_PROMISC)) {
		if (netif_msg_rx_status(priv))
			en_warn(priv, "Entering promiscuous mode\n");
		priv->flags |= MLX4_EN_FLAG_PROMISC;

		/* Enable promiscouos mode */
		switch (mdev->dev->caps.steering_mode) {
		case MLX4_STEERING_MODE_DEVICE_MANAGED:
			err = mlx4_flow_steer_promisc_add(mdev->dev,
							  priv->port,
							  priv->base_qpn,
							  MLX4_FS_ALL_DEFAULT);
			if (err)
				en_err(priv, "Failed enabling promiscuous mode\n");
			priv->flags |= MLX4_EN_FLAG_MC_PROMISC;
			break;

		case MLX4_STEERING_MODE_B0:
			err = mlx4_unicast_promisc_add(mdev->dev,
						       priv->base_qpn,
						       priv->port);
			if (err)
				en_err(priv, "Failed enabling unicast promiscuous mode\n");

			/* Add the default qp number as multicast
			 * promisc
			 */
			if (!(priv->flags & MLX4_EN_FLAG_MC_PROMISC)) {
				err = mlx4_multicast_promisc_add(mdev->dev,
								 priv->base_qpn,
								 priv->port);
				if (err)
					en_err(priv, "Failed enabling multicast promiscuous mode\n");
				priv->flags |= MLX4_EN_FLAG_MC_PROMISC;
			}
			break;

		case MLX4_STEERING_MODE_A0:
			err = mlx4_SET_PORT_qpn_calc(mdev->dev,
						     priv->port,
						     priv->base_qpn,
						     1);
			if (err)
				en_err(priv, "Failed enabling promiscuous mode\n");
			break;
		}

		/* Disable port multicast filter (unconditionally) */
		err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
					  0, MLX4_MCAST_DISABLE);
		if (err)
			en_err(priv, "Failed disabling multicast filter\n");
	}
}

static void mlx4_en_clear_promisc_mode(struct mlx4_en_priv *priv,
				       struct mlx4_en_dev *mdev)
{
	int err = 0;

	if (netif_msg_rx_status(priv))
		en_warn(priv, "Leaving promiscuous mode\n");
	priv->flags &= ~MLX4_EN_FLAG_PROMISC;

	/* Disable promiscouos mode */
	switch (mdev->dev->caps.steering_mode) {
	case MLX4_STEERING_MODE_DEVICE_MANAGED:
		err = mlx4_flow_steer_promisc_remove(mdev->dev,
						     priv->port,
						     MLX4_FS_ALL_DEFAULT);
		if (err)
			en_err(priv, "Failed disabling promiscuous mode\n");
		priv->flags &= ~MLX4_EN_FLAG_MC_PROMISC;
		break;

	case MLX4_STEERING_MODE_B0:
		err = mlx4_unicast_promisc_remove(mdev->dev,
						  priv->base_qpn,
						  priv->port);
		if (err)
			en_err(priv, "Failed disabling unicast promiscuous mode\n");
		/* Disable Multicast promisc */
		if (priv->flags & MLX4_EN_FLAG_MC_PROMISC) {
			err = mlx4_multicast_promisc_remove(mdev->dev,
							    priv->base_qpn,
							    priv->port);
			if (err)
				en_err(priv, "Failed disabling multicast promiscuous mode\n");
			priv->flags &= ~MLX4_EN_FLAG_MC_PROMISC;
		}
		break;

	case MLX4_STEERING_MODE_A0:
		err = mlx4_SET_PORT_qpn_calc(mdev->dev,
					     priv->port,
					     priv->base_qpn, 0);
		if (err)
			en_err(priv, "Failed disabling promiscuous mode\n");
		break;
	}
}

static void mlx4_en_do_multicast(struct mlx4_en_priv *priv,
				 struct net_device *dev,
				 struct mlx4_en_dev *mdev)
{
	struct mlx4_en_mc_list *mclist, *tmp;
	u64 mcast_addr = 0;
	u8 mc_list[16] = {0};
	int err = 0;

	/* Enable/disable the multicast filter according to IFF_ALLMULTI */
	if (dev->flags & IFF_ALLMULTI) {
		err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
					  0, MLX4_MCAST_DISABLE);
		if (err)
			en_err(priv, "Failed disabling multicast filter\n");

		/* Add the default qp number as multicast promisc */
		if (!(priv->flags & MLX4_EN_FLAG_MC_PROMISC)) {
			switch (mdev->dev->caps.steering_mode) {
			case MLX4_STEERING_MODE_DEVICE_MANAGED:
				err = mlx4_flow_steer_promisc_add(mdev->dev,
								  priv->port,
								  priv->base_qpn,
								  MLX4_FS_MC_DEFAULT);
				break;

			case MLX4_STEERING_MODE_B0:
				err = mlx4_multicast_promisc_add(mdev->dev,
								 priv->base_qpn,
								 priv->port);
				break;

			case MLX4_STEERING_MODE_A0:
				break;
			}
			if (err)
				en_err(priv, "Failed entering multicast promisc mode\n");
			priv->flags |= MLX4_EN_FLAG_MC_PROMISC;
		}
	} else {
		/* Disable Multicast promisc */
		if (priv->flags & MLX4_EN_FLAG_MC_PROMISC) {
			switch (mdev->dev->caps.steering_mode) {
			case MLX4_STEERING_MODE_DEVICE_MANAGED:
				err = mlx4_flow_steer_promisc_remove(mdev->dev,
								     priv->port,
								     MLX4_FS_MC_DEFAULT);
				break;

			case MLX4_STEERING_MODE_B0:
				err = mlx4_multicast_promisc_remove(mdev->dev,
								    priv->base_qpn,
								    priv->port);
				break;

			case MLX4_STEERING_MODE_A0:
				break;
			}
			if (err)
				en_err(priv, "Failed disabling multicast promiscuous mode\n");
			priv->flags &= ~MLX4_EN_FLAG_MC_PROMISC;
		}

		err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
					  0, MLX4_MCAST_DISABLE);
		if (err)
			en_err(priv, "Failed disabling multicast filter\n");

		/* Flush mcast filter and init it with broadcast address */
		mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, ETH_BCAST,
				    1, MLX4_MCAST_CONFIG);

		/* Update multicast list - we cache all addresses so they won't
		 * change while HW is updated holding the command semaphor */
		netif_addr_lock_bh(dev);
		mlx4_en_cache_mclist(dev);
		netif_addr_unlock_bh(dev);
		list_for_each_entry(mclist, &priv->mc_list, list) {
			mcast_addr = mlx4_mac_to_u64(mclist->addr);
			mlx4_SET_MCAST_FLTR(mdev->dev, priv->port,
					    mcast_addr, 0, MLX4_MCAST_CONFIG);
		}
		err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
					  0, MLX4_MCAST_ENABLE);
		if (err)
			en_err(priv, "Failed enabling multicast filter\n");

		update_mclist_flags(priv, &priv->curr_list, &priv->mc_list);
		list_for_each_entry_safe(mclist, tmp, &priv->curr_list, list) {
			if (mclist->action == MCLIST_REM) {
				/* detach this address and delete from list */
				memcpy(&mc_list[10], mclist->addr, ETH_ALEN);
				mc_list[5] = priv->port;
				err = mlx4_multicast_detach(mdev->dev,
							    &priv->rss_map.indir_qp,
							    mc_list,
							    MLX4_PROT_ETH,
							    mclist->reg_id);
				if (err)
					en_err(priv, "Fail to detach multicast address\n");

				if (mclist->tunnel_reg_id) {
					err = mlx4_flow_detach(priv->mdev->dev, mclist->tunnel_reg_id);
					if (err)
						en_err(priv, "Failed to detach multicast address\n");
				}

				/* remove from list */
				list_del(&mclist->list);
				kfree(mclist);
			} else if (mclist->action == MCLIST_ADD) {
				/* attach the address */
				memcpy(&mc_list[10], mclist->addr, ETH_ALEN);
				/* needed for B0 steering support */
				mc_list[5] = priv->port;
				err = mlx4_multicast_attach(mdev->dev,
							    &priv->rss_map.indir_qp,
							    mc_list,
							    priv->port, 0,
							    MLX4_PROT_ETH,
							    &mclist->reg_id);
				if (err)
					en_err(priv, "Fail to attach multicast address\n");

				err = mlx4_en_tunnel_steer_add(priv, &mc_list[10], priv->base_qpn,
							       &mclist->tunnel_reg_id);
				if (err)
					en_err(priv, "Failed to attach multicast address\n");
			}
		}
	}
}

static void mlx4_en_do_uc_filter(struct mlx4_en_priv *priv,
				 struct net_device *dev,
				 struct mlx4_en_dev *mdev)
{
	struct netdev_hw_addr *ha;
	struct mlx4_mac_entry *entry;
	struct hlist_node *tmp;
	bool found;
	u64 mac;
	int err = 0;
	struct hlist_head *bucket;
	unsigned int i;
	int removed = 0;
	u32 prev_flags;

	/* Note that we do not need to protect our mac_hash traversal with rcu,
	 * since all modification code is protected by mdev->state_lock
	 */

	/* find what to remove */
	for (i = 0; i < MLX4_EN_MAC_HASH_SIZE; ++i) {
		bucket = &priv->mac_hash[i];
		hlist_for_each_entry_safe(entry, tmp, bucket, hlist) {
			found = false;
			netdev_for_each_uc_addr(ha, dev) {
				if (ether_addr_equal_64bits(entry->mac,
							    ha->addr)) {
					found = true;
					break;
				}
			}

			/* MAC address of the port is not in uc list */
			if (ether_addr_equal_64bits(entry->mac,
						    priv->current_mac))
				found = true;

			if (!found) {
				mac = mlx4_mac_to_u64(entry->mac);
				mlx4_en_uc_steer_release(priv, entry->mac,
							 priv->base_qpn,
							 entry->reg_id);
				mlx4_unregister_mac(mdev->dev, priv->port, mac);

				hlist_del_rcu(&entry->hlist);
				kfree_rcu(entry, rcu);
				en_dbg(DRV, priv, "Removed MAC %pM on port:%d\n",
				       entry->mac, priv->port);
				++removed;
			}
		}
	}

	/* if we didn't remove anything, there is no use in trying to add
	 * again once we are in a forced promisc mode state
	 */
	if ((priv->flags & MLX4_EN_FLAG_FORCE_PROMISC) && 0 == removed)
		return;

	prev_flags = priv->flags;
	priv->flags &= ~MLX4_EN_FLAG_FORCE_PROMISC;

	/* find what to add */
	netdev_for_each_uc_addr(ha, dev) {
		found = false;
		bucket = &priv->mac_hash[ha->addr[MLX4_EN_MAC_HASH_IDX]];
		hlist_for_each_entry(entry, bucket, hlist) {
			if (ether_addr_equal_64bits(entry->mac, ha->addr)) {
				found = true;
				break;
			}
		}

		if (!found) {
			entry = kmalloc(sizeof(*entry), GFP_KERNEL);
			if (!entry) {
				en_err(priv, "Failed adding MAC %pM on port:%d (out of memory)\n",
				       ha->addr, priv->port);
				priv->flags |= MLX4_EN_FLAG_FORCE_PROMISC;
				break;
			}
			mac = mlx4_mac_to_u64(ha->addr);
			memcpy(entry->mac, ha->addr, ETH_ALEN);
			err = mlx4_register_mac(mdev->dev, priv->port, mac);
			if (err < 0) {
				en_err(priv, "Failed registering MAC %pM on port %d: %d\n",
				       ha->addr, priv->port, err);
				kfree(entry);
				priv->flags |= MLX4_EN_FLAG_FORCE_PROMISC;
				break;
			}
			err = mlx4_en_uc_steer_add(priv, ha->addr,
						   &priv->base_qpn,
						   &entry->reg_id);
			if (err) {
				en_err(priv, "Failed adding MAC %pM on port %d: %d\n",
				       ha->addr, priv->port, err);
				mlx4_unregister_mac(mdev->dev, priv->port, mac);
				kfree(entry);
				priv->flags |= MLX4_EN_FLAG_FORCE_PROMISC;
				break;
			} else {
				unsigned int mac_hash;
				en_dbg(DRV, priv, "Added MAC %pM on port:%d\n",
				       ha->addr, priv->port);
				mac_hash = ha->addr[MLX4_EN_MAC_HASH_IDX];
				bucket = &priv->mac_hash[mac_hash];
				hlist_add_head_rcu(&entry->hlist, bucket);
			}
		}
	}

	if (priv->flags & MLX4_EN_FLAG_FORCE_PROMISC) {
		en_warn(priv, "Forcing promiscuous mode on port:%d\n",
			priv->port);
	} else if (prev_flags & MLX4_EN_FLAG_FORCE_PROMISC) {
		en_warn(priv, "Stop forcing promiscuous mode on port:%d\n",
			priv->port);
	}
}

static void mlx4_en_do_set_rx_mode(struct work_struct *work)
{
	struct mlx4_en_priv *priv = container_of(work, struct mlx4_en_priv,
						 rx_mode_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct net_device *dev = priv->dev;

	mutex_lock(&mdev->state_lock);
	if (!mdev->device_up) {
		en_dbg(HW, priv, "Card is not up, ignoring rx mode change.\n");
		goto out;
	}
	if (!priv->port_up) {
		en_dbg(HW, priv, "Port is down, ignoring rx mode change.\n");
		goto out;
	}

	if (!netif_carrier_ok(dev)) {
		if (!mlx4_en_QUERY_PORT(mdev, priv->port)) {
			if (priv->port_state.link_state) {
				priv->last_link_state = MLX4_DEV_EVENT_PORT_UP;
				netif_carrier_on(dev);
				en_dbg(LINK, priv, "Link Up\n");
			}
		}
	}

	if (dev->priv_flags & IFF_UNICAST_FLT)
		mlx4_en_do_uc_filter(priv, dev, mdev);

	/* Promsicuous mode: disable all filters */
	if ((dev->flags & IFF_PROMISC) ||
	    (priv->flags & MLX4_EN_FLAG_FORCE_PROMISC)) {
		mlx4_en_set_promisc_mode(priv, mdev);
		goto out;
	}

	/* Not in promiscuous mode */
	if (priv->flags & MLX4_EN_FLAG_PROMISC)
		mlx4_en_clear_promisc_mode(priv, mdev);

	mlx4_en_do_multicast(priv, dev, mdev);
out:
	mutex_unlock(&mdev->state_lock);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mlx4_en_netpoll(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_cq *cq;
	int i;

	for (i = 0; i < priv->tx_ring_num[TX]; i++) {
		cq = priv->tx_cq[TX][i];
		napi_schedule(&cq->napi);
	}
}
#endif

static int mlx4_en_set_rss_steer_rules(struct mlx4_en_priv *priv)
{
	u64 reg_id;
	int err = 0;
	int *qpn = &priv->base_qpn;
	struct mlx4_mac_entry *entry;

	err = mlx4_en_uc_steer_add(priv, priv->dev->dev_addr, qpn, &reg_id);
	if (err)
		return err;

	err = mlx4_en_tunnel_steer_add(priv, priv->dev->dev_addr, *qpn,
				       &priv->tunnel_reg_id);
	if (err)
		goto tunnel_err;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		err = -ENOMEM;
		goto alloc_err;
	}

	memcpy(entry->mac, priv->dev->dev_addr, sizeof(entry->mac));
	memcpy(priv->current_mac, entry->mac, sizeof(priv->current_mac));
	entry->reg_id = reg_id;
	hlist_add_head_rcu(&entry->hlist,
			   &priv->mac_hash[entry->mac[MLX4_EN_MAC_HASH_IDX]]);

	return 0;

alloc_err:
	if (priv->tunnel_reg_id)
		mlx4_flow_detach(priv->mdev->dev, priv->tunnel_reg_id);

tunnel_err:
	mlx4_en_uc_steer_release(priv, priv->dev->dev_addr, *qpn, reg_id);
	return err;
}

static void mlx4_en_delete_rss_steer_rules(struct mlx4_en_priv *priv)
{
	u64 mac;
	unsigned int i;
	int qpn = priv->base_qpn;
	struct hlist_head *bucket;
	struct hlist_node *tmp;
	struct mlx4_mac_entry *entry;

	for (i = 0; i < MLX4_EN_MAC_HASH_SIZE; ++i) {
		bucket = &priv->mac_hash[i];
		hlist_for_each_entry_safe(entry, tmp, bucket, hlist) {
			mac = mlx4_mac_to_u64(entry->mac);
			en_dbg(DRV, priv, "Registering MAC:%pM for deleting\n",
			       entry->mac);
			mlx4_en_uc_steer_release(priv, entry->mac,
						 qpn, entry->reg_id);

			mlx4_unregister_mac(priv->mdev->dev, priv->port, mac);
			hlist_del_rcu(&entry->hlist);
			kfree_rcu(entry, rcu);
		}
	}

	if (priv->tunnel_reg_id) {
		mlx4_flow_detach(priv->mdev->dev, priv->tunnel_reg_id);
		priv->tunnel_reg_id = 0;
	}
}

static void mlx4_en_tx_timeout(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int i;

	if (netif_msg_timer(priv))
		en_warn(priv, "Tx timeout called on port:%d\n", priv->port);

	for (i = 0; i < priv->tx_ring_num[TX]; i++) {
		struct mlx4_en_tx_ring *tx_ring = priv->tx_ring[TX][i];

		if (!netif_tx_queue_stopped(netdev_get_tx_queue(dev, i)))
			continue;
		en_warn(priv, "TX timeout on queue: %d, QP: 0x%x, CQ: 0x%x, Cons: 0x%x, Prod: 0x%x\n",
			i, tx_ring->qpn, tx_ring->sp_cqn,
			tx_ring->cons, tx_ring->prod);
	}

	priv->port_stats.tx_timeout++;
	en_dbg(DRV, priv, "Scheduling watchdog\n");
	queue_work(mdev->workqueue, &priv->watchdog_task);
}


static struct rtnl_link_stats64 *
mlx4_en_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	spin_lock_bh(&priv->stats_lock);
	mlx4_en_fold_software_stats(dev);
	netdev_stats_to_stats64(stats, &dev->stats);
	spin_unlock_bh(&priv->stats_lock);

	return stats;
}

static void mlx4_en_set_default_moderation(struct mlx4_en_priv *priv)
{
	struct mlx4_en_cq *cq;
	int i, t;

	/* If we haven't received a specific coalescing setting
	 * (module param), we set the moderation parameters as follows:
	 * - moder_cnt is set to the number of mtu sized packets to
	 *   satisfy our coalescing target.
	 * - moder_time is set to a fixed value.
	 */
	priv->rx_frames = MLX4_EN_RX_COAL_TARGET;
	priv->rx_usecs = MLX4_EN_RX_COAL_TIME;
	priv->tx_frames = MLX4_EN_TX_COAL_PKTS;
	priv->tx_usecs = MLX4_EN_TX_COAL_TIME;
	en_dbg(INTR, priv, "Default coalesing params for mtu:%d - rx_frames:%d rx_usecs:%d\n",
	       priv->dev->mtu, priv->rx_frames, priv->rx_usecs);

	/* Setup cq moderation params */
	for (i = 0; i < priv->rx_ring_num; i++) {
		cq = priv->rx_cq[i];
		cq->moder_cnt = priv->rx_frames;
		cq->moder_time = priv->rx_usecs;
		priv->last_moder_time[i] = MLX4_EN_AUTO_CONF;
		priv->last_moder_packets[i] = 0;
		priv->last_moder_bytes[i] = 0;
	}

	for (t = 0 ; t < MLX4_EN_NUM_TX_TYPES; t++) {
		for (i = 0; i < priv->tx_ring_num[t]; i++) {
			cq = priv->tx_cq[t][i];
			cq->moder_cnt = priv->tx_frames;
			cq->moder_time = priv->tx_usecs;
		}
	}

	/* Reset auto-moderation params */
	priv->pkt_rate_low = MLX4_EN_RX_RATE_LOW;
	priv->rx_usecs_low = MLX4_EN_RX_COAL_TIME_LOW;
	priv->pkt_rate_high = MLX4_EN_RX_RATE_HIGH;
	priv->rx_usecs_high = MLX4_EN_RX_COAL_TIME_HIGH;
	priv->sample_interval = MLX4_EN_SAMPLE_INTERVAL;
	priv->adaptive_rx_coal = 1;
	priv->last_moder_jiffies = 0;
	priv->last_moder_tx_packets = 0;
}

static void mlx4_en_auto_moderation(struct mlx4_en_priv *priv)
{
	unsigned long period = (unsigned long) (jiffies - priv->last_moder_jiffies);
	struct mlx4_en_cq *cq;
	unsigned long packets;
	unsigned long rate;
	unsigned long avg_pkt_size;
	unsigned long rx_packets;
	unsigned long rx_bytes;
	unsigned long rx_pkt_diff;
	int moder_time;
	int ring, err;

	if (!priv->adaptive_rx_coal || period < priv->sample_interval * HZ)
		return;

	for (ring = 0; ring < priv->rx_ring_num; ring++) {
		rx_packets = READ_ONCE(priv->rx_ring[ring]->packets);
		rx_bytes = READ_ONCE(priv->rx_ring[ring]->bytes);

		rx_pkt_diff = ((unsigned long) (rx_packets -
				priv->last_moder_packets[ring]));
		packets = rx_pkt_diff;
		rate = packets * HZ / period;
		avg_pkt_size = packets ? ((unsigned long) (rx_bytes -
				priv->last_moder_bytes[ring])) / packets : 0;

		/* Apply auto-moderation only when packet rate
		 * exceeds a rate that it matters */
		if (rate > (MLX4_EN_RX_RATE_THRESH / priv->rx_ring_num) &&
		    avg_pkt_size > MLX4_EN_AVG_PKT_SMALL) {
			if (rate < priv->pkt_rate_low)
				moder_time = priv->rx_usecs_low;
			else if (rate > priv->pkt_rate_high)
				moder_time = priv->rx_usecs_high;
			else
				moder_time = (rate - priv->pkt_rate_low) *
					(priv->rx_usecs_high - priv->rx_usecs_low) /
					(priv->pkt_rate_high - priv->pkt_rate_low) +
					priv->rx_usecs_low;
		} else {
			moder_time = priv->rx_usecs_low;
		}

		if (moder_time != priv->last_moder_time[ring]) {
			priv->last_moder_time[ring] = moder_time;
			cq = priv->rx_cq[ring];
			cq->moder_time = moder_time;
			cq->moder_cnt = priv->rx_frames;
			err = mlx4_en_set_cq_moder(priv, cq);
			if (err)
				en_err(priv, "Failed modifying moderation for cq:%d\n",
				       ring);
		}
		priv->last_moder_packets[ring] = rx_packets;
		priv->last_moder_bytes[ring] = rx_bytes;
	}

	priv->last_moder_jiffies = jiffies;
}

static void mlx4_en_do_get_stats(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	struct mlx4_en_priv *priv = container_of(delay, struct mlx4_en_priv,
						 stats_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;

	mutex_lock(&mdev->state_lock);
	if (mdev->device_up) {
		if (priv->port_up) {
			err = mlx4_en_DUMP_ETH_STATS(mdev, priv->port, 0);
			if (err)
				en_dbg(HW, priv, "Could not update stats\n");

			mlx4_en_auto_moderation(priv);
		}

		queue_delayed_work(mdev->workqueue, &priv->stats_task, STATS_DELAY);
	}
	if (mdev->mac_removed[MLX4_MAX_PORTS + 1 - priv->port]) {
		mlx4_en_do_set_mac(priv, priv->current_mac);
		mdev->mac_removed[MLX4_MAX_PORTS + 1 - priv->port] = 0;
	}
	mutex_unlock(&mdev->state_lock);
}

/* mlx4_en_service_task - Run service task for tasks that needed to be done
 * periodically
 */
static void mlx4_en_service_task(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	struct mlx4_en_priv *priv = container_of(delay, struct mlx4_en_priv,
						 service_task);
	struct mlx4_en_dev *mdev = priv->mdev;

	mutex_lock(&mdev->state_lock);
	if (mdev->device_up) {
		if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_TS)
			mlx4_en_ptp_overflow_check(mdev);

		mlx4_en_recover_from_oom(priv);
		queue_delayed_work(mdev->workqueue, &priv->service_task,
				   SERVICE_TASK_DELAY);
	}
	mutex_unlock(&mdev->state_lock);
}

static void mlx4_en_linkstate(struct work_struct *work)
{
	struct mlx4_en_priv *priv = container_of(work, struct mlx4_en_priv,
						 linkstate_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	int linkstate = priv->link_state;

	mutex_lock(&mdev->state_lock);
	/* If observable port state changed set carrier state and
	 * report to system log */
	if (priv->last_link_state != linkstate) {
		if (linkstate == MLX4_DEV_EVENT_PORT_DOWN) {
			en_info(priv, "Link Down\n");
			netif_carrier_off(priv->dev);
		} else {
			en_info(priv, "Link Up\n");
			netif_carrier_on(priv->dev);
		}
	}
	priv->last_link_state = linkstate;
	mutex_unlock(&mdev->state_lock);
}

static int mlx4_en_init_affinity_hint(struct mlx4_en_priv *priv, int ring_idx)
{
	struct mlx4_en_rx_ring *ring = priv->rx_ring[ring_idx];
	int numa_node = priv->mdev->dev->numa_node;

	if (!zalloc_cpumask_var(&ring->affinity_mask, GFP_KERNEL))
		return -ENOMEM;

	cpumask_set_cpu(cpumask_local_spread(ring_idx, numa_node),
			ring->affinity_mask);
	return 0;
}

static void mlx4_en_free_affinity_hint(struct mlx4_en_priv *priv, int ring_idx)
{
	free_cpumask_var(priv->rx_ring[ring_idx]->affinity_mask);
}

static void mlx4_en_init_recycle_ring(struct mlx4_en_priv *priv,
				      int tx_ring_idx)
{
	struct mlx4_en_tx_ring *tx_ring = priv->tx_ring[TX_XDP][tx_ring_idx];
	int rr_index = tx_ring_idx;

	tx_ring->free_tx_desc = mlx4_en_recycle_tx_desc;
	tx_ring->recycle_ring = priv->rx_ring[rr_index];
	en_dbg(DRV, priv, "Set tx_ring[%d][%d]->recycle_ring = rx_ring[%d]\n",
	       TX_XDP, tx_ring_idx, rr_index);
}

int mlx4_en_start_port(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_cq *cq;
	struct mlx4_en_tx_ring *tx_ring;
	int rx_index = 0;
	int err = 0;
	int i, t;
	int j;
	u8 mc_list[16] = {0};

	if (priv->port_up) {
		en_dbg(DRV, priv, "start port called while port already up\n");
		return 0;
	}

	INIT_LIST_HEAD(&priv->mc_list);
	INIT_LIST_HEAD(&priv->curr_list);
	INIT_LIST_HEAD(&priv->ethtool_list);
	memset(&priv->ethtool_rules[0], 0,
	       sizeof(struct ethtool_flow_id) * MAX_NUM_OF_FS_RULES);

	/* Calculate Rx buf size */
	dev->mtu = min(dev->mtu, priv->max_mtu);
	mlx4_en_calc_rx_buf(dev);
	en_dbg(DRV, priv, "Rx buf size:%d\n", priv->rx_skb_size);

	/* Configure rx cq's and rings */
	err = mlx4_en_activate_rx_rings(priv);
	if (err) {
		en_err(priv, "Failed to activate RX rings\n");
		return err;
	}
	for (i = 0; i < priv->rx_ring_num; i++) {
		cq = priv->rx_cq[i];

		err = mlx4_en_init_affinity_hint(priv, i);
		if (err) {
			en_err(priv, "Failed preparing IRQ affinity hint\n");
			goto cq_err;
		}

		err = mlx4_en_activate_cq(priv, cq, i);
		if (err) {
			en_err(priv, "Failed activating Rx CQ\n");
			mlx4_en_free_affinity_hint(priv, i);
			goto cq_err;
		}

		for (j = 0; j < cq->size; j++) {
			struct mlx4_cqe *cqe = NULL;

			cqe = mlx4_en_get_cqe(cq->buf, j, priv->cqe_size) +
			      priv->cqe_factor;
			cqe->owner_sr_opcode = MLX4_CQE_OWNER_MASK;
		}

		err = mlx4_en_set_cq_moder(priv, cq);
		if (err) {
			en_err(priv, "Failed setting cq moderation parameters\n");
			mlx4_en_deactivate_cq(priv, cq);
			mlx4_en_free_affinity_hint(priv, i);
			goto cq_err;
		}
		mlx4_en_arm_cq(priv, cq);
		priv->rx_ring[i]->cqn = cq->mcq.cqn;
		++rx_index;
	}

	/* Set qp number */
	en_dbg(DRV, priv, "Getting qp number for port %d\n", priv->port);
	err = mlx4_en_get_qp(priv);
	if (err) {
		en_err(priv, "Failed getting eth qp\n");
		goto cq_err;
	}
	mdev->mac_removed[priv->port] = 0;

	priv->counter_index =
			mlx4_get_default_counter_index(mdev->dev, priv->port);

	err = mlx4_en_config_rss_steer(priv);
	if (err) {
		en_err(priv, "Failed configuring rss steering\n");
		goto mac_err;
	}

	err = mlx4_en_create_drop_qp(priv);
	if (err)
		goto rss_err;

	/* Configure tx cq's and rings */
	for (t = 0 ; t < MLX4_EN_NUM_TX_TYPES; t++) {
		u8 num_tx_rings_p_up = t == TX ?
			priv->num_tx_rings_p_up : priv->tx_ring_num[t];

		for (i = 0; i < priv->tx_ring_num[t]; i++) {
			/* Configure cq */
			cq = priv->tx_cq[t][i];
			err = mlx4_en_activate_cq(priv, cq, i);
			if (err) {
				en_err(priv, "Failed allocating Tx CQ\n");
				goto tx_err;
			}
			err = mlx4_en_set_cq_moder(priv, cq);
			if (err) {
				en_err(priv, "Failed setting cq moderation parameters\n");
				mlx4_en_deactivate_cq(priv, cq);
				goto tx_err;
			}
			en_dbg(DRV, priv,
			       "Resetting index of collapsed CQ:%d to -1\n", i);
			cq->buf->wqe_index = cpu_to_be16(0xffff);

			/* Configure ring */
			tx_ring = priv->tx_ring[t][i];
			err = mlx4_en_activate_tx_ring(priv, tx_ring,
						       cq->mcq.cqn,
						       i / num_tx_rings_p_up);
			if (err) {
				en_err(priv, "Failed allocating Tx ring\n");
				mlx4_en_deactivate_cq(priv, cq);
				goto tx_err;
			}
			if (t != TX_XDP) {
				tx_ring->tx_queue = netdev_get_tx_queue(dev, i);
				tx_ring->recycle_ring = NULL;
			} else {
				mlx4_en_init_recycle_ring(priv, i);
			}

			/* Arm CQ for TX completions */
			mlx4_en_arm_cq(priv, cq);

			/* Set initial ownership of all Tx TXBBs to SW (1) */
			for (j = 0; j < tx_ring->buf_size; j += STAMP_STRIDE)
				*((u32 *)(tx_ring->buf + j)) = 0xffffffff;
		}
	}

	/* Configure port */
	err = mlx4_SET_PORT_general(mdev->dev, priv->port,
				    priv->rx_skb_size + ETH_FCS_LEN,
				    priv->prof->tx_pause,
				    priv->prof->tx_ppp,
				    priv->prof->rx_pause,
				    priv->prof->rx_ppp);
	if (err) {
		en_err(priv, "Failed setting port general configurations for port %d, with error %d\n",
		       priv->port, err);
		goto tx_err;
	}
	/* Set default qp number */
	err = mlx4_SET_PORT_qpn_calc(mdev->dev, priv->port, priv->base_qpn, 0);
	if (err) {
		en_err(priv, "Failed setting default qp numbers\n");
		goto tx_err;
	}

	if (mdev->dev->caps.tunnel_offload_mode == MLX4_TUNNEL_OFFLOAD_MODE_VXLAN) {
		err = mlx4_SET_PORT_VXLAN(mdev->dev, priv->port, VXLAN_STEER_BY_OUTER_MAC, 1);
		if (err) {
			en_err(priv, "Failed setting port L2 tunnel configuration, err %d\n",
			       err);
			goto tx_err;
		}
	}

	/* Init port */
	en_dbg(HW, priv, "Initializing port\n");
	err = mlx4_INIT_PORT(mdev->dev, priv->port);
	if (err) {
		en_err(priv, "Failed Initializing port\n");
		goto tx_err;
	}

	/* Set Unicast and VXLAN steering rules */
	if (mdev->dev->caps.steering_mode != MLX4_STEERING_MODE_A0 &&
	    mlx4_en_set_rss_steer_rules(priv))
		mlx4_warn(mdev, "Failed setting steering rules\n");

	/* Attach rx QP to bradcast address */
	eth_broadcast_addr(&mc_list[10]);
	mc_list[5] = priv->port; /* needed for B0 steering support */
	if (mlx4_multicast_attach(mdev->dev, &priv->rss_map.indir_qp, mc_list,
				  priv->port, 0, MLX4_PROT_ETH,
				  &priv->broadcast_id))
		mlx4_warn(mdev, "Failed Attaching Broadcast\n");

	/* Must redo promiscuous mode setup. */
	priv->flags &= ~(MLX4_EN_FLAG_PROMISC | MLX4_EN_FLAG_MC_PROMISC);

	/* Schedule multicast task to populate multicast list */
	queue_work(mdev->workqueue, &priv->rx_mode_task);

	if (priv->mdev->dev->caps.tunnel_offload_mode == MLX4_TUNNEL_OFFLOAD_MODE_VXLAN)
		udp_tunnel_get_rx_info(dev);

	priv->port_up = true;

	/* Process all completions if exist to prevent
	 * the queues freezing if they are full
	 */
	for (i = 0; i < priv->rx_ring_num; i++) {
		local_bh_disable();
		napi_schedule(&priv->rx_cq[i]->napi);
		local_bh_enable();
	}

	netif_tx_start_all_queues(dev);
	netif_device_attach(dev);

	return 0;

tx_err:
	if (t == MLX4_EN_NUM_TX_TYPES) {
		t--;
		i = priv->tx_ring_num[t];
	}
	while (t >= 0) {
		while (i--) {
			mlx4_en_deactivate_tx_ring(priv, priv->tx_ring[t][i]);
			mlx4_en_deactivate_cq(priv, priv->tx_cq[t][i]);
		}
		if (!t--)
			break;
		i = priv->tx_ring_num[t];
	}
	mlx4_en_destroy_drop_qp(priv);
rss_err:
	mlx4_en_release_rss_steer(priv);
mac_err:
	mlx4_en_put_qp(priv);
cq_err:
	while (rx_index--) {
		mlx4_en_deactivate_cq(priv, priv->rx_cq[rx_index]);
		mlx4_en_free_affinity_hint(priv, rx_index);
	}
	for (i = 0; i < priv->rx_ring_num; i++)
		mlx4_en_deactivate_rx_ring(priv, priv->rx_ring[i]);

	return err; /* need to close devices */
}


void mlx4_en_stop_port(struct net_device *dev, int detach)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_mc_list *mclist, *tmp;
	struct ethtool_flow_id *flow, *tmp_flow;
	int i, t;
	u8 mc_list[16] = {0};

	if (!priv->port_up) {
		en_dbg(DRV, priv, "stop port called while port already down\n");
		return;
	}

	/* close port*/
	mlx4_CLOSE_PORT(mdev->dev, priv->port);

	/* Synchronize with tx routine */
	netif_tx_lock_bh(dev);
	if (detach)
		netif_device_detach(dev);
	netif_tx_stop_all_queues(dev);
	netif_tx_unlock_bh(dev);

	netif_tx_disable(dev);

	spin_lock_bh(&priv->stats_lock);
	mlx4_en_fold_software_stats(dev);
	/* Set port as not active */
	priv->port_up = false;
	spin_unlock_bh(&priv->stats_lock);

	priv->counter_index = MLX4_SINK_COUNTER_INDEX(mdev->dev);

	/* Promsicuous mode */
	if (mdev->dev->caps.steering_mode ==
	    MLX4_STEERING_MODE_DEVICE_MANAGED) {
		priv->flags &= ~(MLX4_EN_FLAG_PROMISC |
				 MLX4_EN_FLAG_MC_PROMISC);
		mlx4_flow_steer_promisc_remove(mdev->dev,
					       priv->port,
					       MLX4_FS_ALL_DEFAULT);
		mlx4_flow_steer_promisc_remove(mdev->dev,
					       priv->port,
					       MLX4_FS_MC_DEFAULT);
	} else if (priv->flags & MLX4_EN_FLAG_PROMISC) {
		priv->flags &= ~MLX4_EN_FLAG_PROMISC;

		/* Disable promiscouos mode */
		mlx4_unicast_promisc_remove(mdev->dev, priv->base_qpn,
					    priv->port);

		/* Disable Multicast promisc */
		if (priv->flags & MLX4_EN_FLAG_MC_PROMISC) {
			mlx4_multicast_promisc_remove(mdev->dev, priv->base_qpn,
						      priv->port);
			priv->flags &= ~MLX4_EN_FLAG_MC_PROMISC;
		}
	}

	/* Detach All multicasts */
	eth_broadcast_addr(&mc_list[10]);
	mc_list[5] = priv->port; /* needed for B0 steering support */
	mlx4_multicast_detach(mdev->dev, &priv->rss_map.indir_qp, mc_list,
			      MLX4_PROT_ETH, priv->broadcast_id);
	list_for_each_entry(mclist, &priv->curr_list, list) {
		memcpy(&mc_list[10], mclist->addr, ETH_ALEN);
		mc_list[5] = priv->port;
		mlx4_multicast_detach(mdev->dev, &priv->rss_map.indir_qp,
				      mc_list, MLX4_PROT_ETH, mclist->reg_id);
		if (mclist->tunnel_reg_id)
			mlx4_flow_detach(mdev->dev, mclist->tunnel_reg_id);
	}
	mlx4_en_clear_list(dev);
	list_for_each_entry_safe(mclist, tmp, &priv->curr_list, list) {
		list_del(&mclist->list);
		kfree(mclist);
	}

	/* Flush multicast filter */
	mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0, 1, MLX4_MCAST_CONFIG);

	/* Remove flow steering rules for the port*/
	if (mdev->dev->caps.steering_mode ==
	    MLX4_STEERING_MODE_DEVICE_MANAGED) {
		ASSERT_RTNL();
		list_for_each_entry_safe(flow, tmp_flow,
					 &priv->ethtool_list, list) {
			mlx4_flow_detach(mdev->dev, flow->id);
			list_del(&flow->list);
		}
	}

	mlx4_en_destroy_drop_qp(priv);

	/* Free TX Rings */
	for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++) {
		for (i = 0; i < priv->tx_ring_num[t]; i++) {
			mlx4_en_deactivate_tx_ring(priv, priv->tx_ring[t][i]);
			mlx4_en_deactivate_cq(priv, priv->tx_cq[t][i]);
		}
	}
	msleep(10);

	for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++)
		for (i = 0; i < priv->tx_ring_num[t]; i++)
			mlx4_en_free_tx_buf(dev, priv->tx_ring[t][i]);

	if (mdev->dev->caps.steering_mode != MLX4_STEERING_MODE_A0)
		mlx4_en_delete_rss_steer_rules(priv);

	/* Free RSS qps */
	mlx4_en_release_rss_steer(priv);

	/* Unregister Mac address for the port */
	mlx4_en_put_qp(priv);
	if (!(mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_REASSIGN_MAC_EN))
		mdev->mac_removed[priv->port] = 1;

	/* Free RX Rings */
	for (i = 0; i < priv->rx_ring_num; i++) {
		struct mlx4_en_cq *cq = priv->rx_cq[i];

		napi_synchronize(&cq->napi);
		mlx4_en_deactivate_rx_ring(priv, priv->rx_ring[i]);
		mlx4_en_deactivate_cq(priv, cq);

		mlx4_en_free_affinity_hint(priv, i);
	}
}

static void mlx4_en_restart(struct work_struct *work)
{
	struct mlx4_en_priv *priv = container_of(work, struct mlx4_en_priv,
						 watchdog_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct net_device *dev = priv->dev;

	en_dbg(DRV, priv, "Watchdog task called for port %d\n", priv->port);

	rtnl_lock();
	mutex_lock(&mdev->state_lock);
	if (priv->port_up) {
		mlx4_en_stop_port(dev, 1);
		if (mlx4_en_start_port(dev))
			en_err(priv, "Failed restarting port %d\n", priv->port);
	}
	mutex_unlock(&mdev->state_lock);
	rtnl_unlock();
}

static void mlx4_en_clear_stats(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_tx_ring **tx_ring;
	int i;

	if (!mlx4_is_slave(mdev->dev))
		if (mlx4_en_DUMP_ETH_STATS(mdev, priv->port, 1))
			en_dbg(HW, priv, "Failed dumping statistics\n");

	memset(&priv->pstats, 0, sizeof(priv->pstats));
	memset(&priv->pkstats, 0, sizeof(priv->pkstats));
	memset(&priv->port_stats, 0, sizeof(priv->port_stats));
	memset(&priv->rx_flowstats, 0, sizeof(priv->rx_flowstats));
	memset(&priv->tx_flowstats, 0, sizeof(priv->tx_flowstats));
	memset(&priv->rx_priority_flowstats, 0,
	       sizeof(priv->rx_priority_flowstats));
	memset(&priv->tx_priority_flowstats, 0,
	       sizeof(priv->tx_priority_flowstats));
	memset(&priv->pf_stats, 0, sizeof(priv->pf_stats));

	tx_ring = priv->tx_ring[TX];
	for (i = 0; i < priv->tx_ring_num[TX]; i++) {
		tx_ring[i]->bytes = 0;
		tx_ring[i]->packets = 0;
		tx_ring[i]->tx_csum = 0;
		tx_ring[i]->tx_dropped = 0;
		tx_ring[i]->queue_stopped = 0;
		tx_ring[i]->wake_queue = 0;
		tx_ring[i]->tso_packets = 0;
		tx_ring[i]->xmit_more = 0;
	}
	for (i = 0; i < priv->rx_ring_num; i++) {
		priv->rx_ring[i]->bytes = 0;
		priv->rx_ring[i]->packets = 0;
		priv->rx_ring[i]->csum_ok = 0;
		priv->rx_ring[i]->csum_none = 0;
		priv->rx_ring[i]->csum_complete = 0;
	}
}

static int mlx4_en_open(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err = 0;

	mutex_lock(&mdev->state_lock);

	if (!mdev->device_up) {
		en_err(priv, "Cannot open - device down/disabled\n");
		err = -EBUSY;
		goto out;
	}

	/* Reset HW statistics and SW counters */
	mlx4_en_clear_stats(dev);

	err = mlx4_en_start_port(dev);
	if (err)
		en_err(priv, "Failed starting port:%d\n", priv->port);

out:
	mutex_unlock(&mdev->state_lock);
	return err;
}


static int mlx4_en_close(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;

	en_dbg(IFDOWN, priv, "Close port called\n");

	mutex_lock(&mdev->state_lock);

	mlx4_en_stop_port(dev, 0);
	netif_carrier_off(dev);

	mutex_unlock(&mdev->state_lock);
	return 0;
}

static void mlx4_en_free_resources(struct mlx4_en_priv *priv)
{
	int i, t;

#ifdef CONFIG_RFS_ACCEL
	priv->dev->rx_cpu_rmap = NULL;
#endif

	for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++) {
		for (i = 0; i < priv->tx_ring_num[t]; i++) {
			if (priv->tx_ring[t] && priv->tx_ring[t][i])
				mlx4_en_destroy_tx_ring(priv,
							&priv->tx_ring[t][i]);
			if (priv->tx_cq[t] && priv->tx_cq[t][i])
				mlx4_en_destroy_cq(priv, &priv->tx_cq[t][i]);
		}
	}

	for (i = 0; i < priv->rx_ring_num; i++) {
		if (priv->rx_ring[i])
			mlx4_en_destroy_rx_ring(priv, &priv->rx_ring[i],
				priv->prof->rx_ring_size, priv->stride);
		if (priv->rx_cq[i])
			mlx4_en_destroy_cq(priv, &priv->rx_cq[i]);
	}

}

static int mlx4_en_alloc_resources(struct mlx4_en_priv *priv)
{
	struct mlx4_en_port_profile *prof = priv->prof;
	int i, t;
	int node;

	/* Create tx Rings */
	for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++) {
		for (i = 0; i < priv->tx_ring_num[t]; i++) {
			node = cpu_to_node(i % num_online_cpus());
			if (mlx4_en_create_cq(priv, &priv->tx_cq[t][i],
					      prof->tx_ring_size, i, t, node))
				goto err;

			if (mlx4_en_create_tx_ring(priv, &priv->tx_ring[t][i],
						   prof->tx_ring_size,
						   TXBB_SIZE, node, i))
				goto err;
		}
	}

	/* Create rx Rings */
	for (i = 0; i < priv->rx_ring_num; i++) {
		node = cpu_to_node(i % num_online_cpus());
		if (mlx4_en_create_cq(priv, &priv->rx_cq[i],
				      prof->rx_ring_size, i, RX, node))
			goto err;

		if (mlx4_en_create_rx_ring(priv, &priv->rx_ring[i],
					   prof->rx_ring_size, priv->stride,
					   node))
			goto err;
	}

#ifdef CONFIG_RFS_ACCEL
	priv->dev->rx_cpu_rmap = mlx4_get_cpu_rmap(priv->mdev->dev, priv->port);
#endif

	return 0;

err:
	en_err(priv, "Failed to allocate NIC resources\n");
	for (i = 0; i < priv->rx_ring_num; i++) {
		if (priv->rx_ring[i])
			mlx4_en_destroy_rx_ring(priv, &priv->rx_ring[i],
						prof->rx_ring_size,
						priv->stride);
		if (priv->rx_cq[i])
			mlx4_en_destroy_cq(priv, &priv->rx_cq[i]);
	}
	for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++) {
		for (i = 0; i < priv->tx_ring_num[t]; i++) {
			if (priv->tx_ring[t][i])
				mlx4_en_destroy_tx_ring(priv,
							&priv->tx_ring[t][i]);
			if (priv->tx_cq[t][i])
				mlx4_en_destroy_cq(priv, &priv->tx_cq[t][i]);
		}
	}
	return -ENOMEM;
}


static int mlx4_en_copy_priv(struct mlx4_en_priv *dst,
			     struct mlx4_en_priv *src,
			     struct mlx4_en_port_profile *prof)
{
	int t;

	memcpy(&dst->hwtstamp_config, &prof->hwtstamp_config,
	       sizeof(dst->hwtstamp_config));
	dst->num_tx_rings_p_up = src->mdev->profile.num_tx_rings_p_up;
	dst->rx_ring_num = prof->rx_ring_num;
	dst->flags = prof->flags;
	dst->mdev = src->mdev;
	dst->port = src->port;
	dst->dev = src->dev;
	dst->prof = prof;
	dst->stride = roundup_pow_of_two(sizeof(struct mlx4_en_rx_desc) +
					 DS_SIZE * MLX4_EN_MAX_RX_FRAGS);

	for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++) {
		dst->tx_ring_num[t] = prof->tx_ring_num[t];
		if (!dst->tx_ring_num[t])
			continue;

		dst->tx_ring[t] = kzalloc(sizeof(struct mlx4_en_tx_ring *) *
					  MAX_TX_RINGS, GFP_KERNEL);
		if (!dst->tx_ring[t])
			goto err_free_tx;

		dst->tx_cq[t] = kzalloc(sizeof(struct mlx4_en_cq *) *
					MAX_TX_RINGS, GFP_KERNEL);
		if (!dst->tx_cq[t]) {
			kfree(dst->tx_ring[t]);
			goto err_free_tx;
		}
	}

	return 0;

err_free_tx:
	while (t--) {
		kfree(dst->tx_ring[t]);
		kfree(dst->tx_cq[t]);
	}
	return -ENOMEM;
}

static void mlx4_en_update_priv(struct mlx4_en_priv *dst,
				struct mlx4_en_priv *src)
{
	int t;
	memcpy(dst->rx_ring, src->rx_ring,
	       sizeof(struct mlx4_en_rx_ring *) * src->rx_ring_num);
	memcpy(dst->rx_cq, src->rx_cq,
	       sizeof(struct mlx4_en_cq *) * src->rx_ring_num);
	memcpy(&dst->hwtstamp_config, &src->hwtstamp_config,
	       sizeof(dst->hwtstamp_config));
	for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++) {
		dst->tx_ring_num[t] = src->tx_ring_num[t];
		dst->tx_ring[t] = src->tx_ring[t];
		dst->tx_cq[t] = src->tx_cq[t];
	}
	dst->rx_ring_num = src->rx_ring_num;
	memcpy(dst->prof, src->prof, sizeof(struct mlx4_en_port_profile));
}

int mlx4_en_try_alloc_resources(struct mlx4_en_priv *priv,
				struct mlx4_en_priv *tmp,
				struct mlx4_en_port_profile *prof)
{
	int t;

	mlx4_en_copy_priv(tmp, priv, prof);

	if (mlx4_en_alloc_resources(tmp)) {
		en_warn(priv,
			"%s: Resource allocation failed, using previous configuration\n",
			__func__);
		for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++) {
			kfree(tmp->tx_ring[t]);
			kfree(tmp->tx_cq[t]);
		}
		return -ENOMEM;
	}
	return 0;
}

void mlx4_en_safe_replace_resources(struct mlx4_en_priv *priv,
				    struct mlx4_en_priv *tmp)
{
	mlx4_en_free_resources(priv);
	mlx4_en_update_priv(priv, tmp);
}

void mlx4_en_destroy_netdev(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int t;

	en_dbg(DRV, priv, "Destroying netdev on port:%d\n", priv->port);

	/* Unregister device - this will close the port if it was up */
	if (priv->registered) {
		devlink_port_type_clear(mlx4_get_devlink_port(mdev->dev,
							      priv->port));
		unregister_netdev(dev);
	}

	if (priv->allocated)
		mlx4_free_hwq_res(mdev->dev, &priv->res, MLX4_EN_PAGE_SIZE);

	cancel_delayed_work(&priv->stats_task);
	cancel_delayed_work(&priv->service_task);
	/* flush any pending task for this netdev */
	flush_workqueue(mdev->workqueue);

	if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_TS)
		mlx4_en_remove_timestamp(mdev);

	/* Detach the netdev so tasks would not attempt to access it */
	mutex_lock(&mdev->state_lock);
	mdev->pndev[priv->port] = NULL;
	mdev->upper[priv->port] = NULL;

#ifdef CONFIG_RFS_ACCEL
	mlx4_en_cleanup_filters(priv);
#endif

	mlx4_en_free_resources(priv);
	mutex_unlock(&mdev->state_lock);

	for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++) {
		kfree(priv->tx_ring[t]);
		kfree(priv->tx_cq[t]);
	}

	free_netdev(dev);
}

static bool mlx4_en_check_xdp_mtu(struct net_device *dev, int mtu)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	if (mtu > MLX4_EN_MAX_XDP_MTU) {
		en_err(priv, "mtu:%d > max:%d when XDP prog is attached\n",
		       mtu, MLX4_EN_MAX_XDP_MTU);
		return false;
	}

	return true;
}

static int mlx4_en_change_mtu(struct net_device *dev, int new_mtu)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err = 0;

	en_dbg(DRV, priv, "Change MTU called - current:%d new:%d\n",
		 dev->mtu, new_mtu);

	if (priv->tx_ring_num[TX_XDP] &&
	    !mlx4_en_check_xdp_mtu(dev, new_mtu))
		return -EOPNOTSUPP;

	dev->mtu = new_mtu;

	if (netif_running(dev)) {
		mutex_lock(&mdev->state_lock);
		if (!mdev->device_up) {
			/* NIC is probably restarting - let watchdog task reset
			 * the port */
			en_dbg(DRV, priv, "Change MTU called with card down!?\n");
		} else {
			mlx4_en_stop_port(dev, 1);
			err = mlx4_en_start_port(dev);
			if (err) {
				en_err(priv, "Failed restarting port:%d\n",
					 priv->port);
				queue_work(mdev->workqueue, &priv->watchdog_task);
			}
		}
		mutex_unlock(&mdev->state_lock);
	}
	return 0;
}

static int mlx4_en_hwtstamp_set(struct net_device *dev, struct ifreq *ifr)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct hwtstamp_config config;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	/* reserved for future extensions */
	if (config.flags)
		return -EINVAL;

	/* device doesn't support time stamping */
	if (!(mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_TS))
		return -EINVAL;

	/* TX HW timestamp */
	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	/* RX HW timestamp */
	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	if (mlx4_en_reset_config(dev, config, dev->features)) {
		config.tx_type = HWTSTAMP_TX_OFF;
		config.rx_filter = HWTSTAMP_FILTER_NONE;
	}

	return copy_to_user(ifr->ifr_data, &config,
			    sizeof(config)) ? -EFAULT : 0;
}

static int mlx4_en_hwtstamp_get(struct net_device *dev, struct ifreq *ifr)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	return copy_to_user(ifr->ifr_data, &priv->hwtstamp_config,
			    sizeof(priv->hwtstamp_config)) ? -EFAULT : 0;
}

static int mlx4_en_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCSHWTSTAMP:
		return mlx4_en_hwtstamp_set(dev, ifr);
	case SIOCGHWTSTAMP:
		return mlx4_en_hwtstamp_get(dev, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

static netdev_features_t mlx4_en_fix_features(struct net_device *netdev,
					      netdev_features_t features)
{
	struct mlx4_en_priv *en_priv = netdev_priv(netdev);
	struct mlx4_en_dev *mdev = en_priv->mdev;

	/* Since there is no support for separate RX C-TAG/S-TAG vlan accel
	 * enable/disable make sure S-TAG flag is always in same state as
	 * C-TAG.
	 */
	if (features & NETIF_F_HW_VLAN_CTAG_RX &&
	    !(mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_SKIP_OUTER_VLAN))
		features |= NETIF_F_HW_VLAN_STAG_RX;
	else
		features &= ~NETIF_F_HW_VLAN_STAG_RX;

	return features;
}

static int mlx4_en_set_features(struct net_device *netdev,
		netdev_features_t features)
{
	struct mlx4_en_priv *priv = netdev_priv(netdev);
	bool reset = false;
	int ret = 0;

	if (DEV_FEATURE_CHANGED(netdev, features, NETIF_F_RXFCS)) {
		en_info(priv, "Turn %s RX-FCS\n",
			(features & NETIF_F_RXFCS) ? "ON" : "OFF");
		reset = true;
	}

	if (DEV_FEATURE_CHANGED(netdev, features, NETIF_F_RXALL)) {
		u8 ignore_fcs_value = (features & NETIF_F_RXALL) ? 1 : 0;

		en_info(priv, "Turn %s RX-ALL\n",
			ignore_fcs_value ? "ON" : "OFF");
		ret = mlx4_SET_PORT_fcs_check(priv->mdev->dev,
					      priv->port, ignore_fcs_value);
		if (ret)
			return ret;
	}

	if (DEV_FEATURE_CHANGED(netdev, features, NETIF_F_HW_VLAN_CTAG_RX)) {
		en_info(priv, "Turn %s RX vlan strip offload\n",
			(features & NETIF_F_HW_VLAN_CTAG_RX) ? "ON" : "OFF");
		reset = true;
	}

	if (DEV_FEATURE_CHANGED(netdev, features, NETIF_F_HW_VLAN_CTAG_TX))
		en_info(priv, "Turn %s TX vlan strip offload\n",
			(features & NETIF_F_HW_VLAN_CTAG_TX) ? "ON" : "OFF");

	if (DEV_FEATURE_CHANGED(netdev, features, NETIF_F_HW_VLAN_STAG_TX))
		en_info(priv, "Turn %s TX S-VLAN strip offload\n",
			(features & NETIF_F_HW_VLAN_STAG_TX) ? "ON" : "OFF");

	if (DEV_FEATURE_CHANGED(netdev, features, NETIF_F_LOOPBACK)) {
		en_info(priv, "Turn %s loopback\n",
			(features & NETIF_F_LOOPBACK) ? "ON" : "OFF");
		mlx4_en_update_loopback_state(netdev, features);
	}

	if (reset) {
		ret = mlx4_en_reset_config(netdev, priv->hwtstamp_config,
					   features);
		if (ret)
			return ret;
	}

	return 0;
}

static int mlx4_en_set_vf_mac(struct net_device *dev, int queue, u8 *mac)
{
	struct mlx4_en_priv *en_priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = en_priv->mdev;
	u64 mac_u64 = mlx4_mac_to_u64(mac);

	if (is_multicast_ether_addr(mac))
		return -EINVAL;

	return mlx4_set_vf_mac(mdev->dev, en_priv->port, queue, mac_u64);
}

static int mlx4_en_set_vf_vlan(struct net_device *dev, int vf, u16 vlan, u8 qos,
			       __be16 vlan_proto)
{
	struct mlx4_en_priv *en_priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = en_priv->mdev;

	return mlx4_set_vf_vlan(mdev->dev, en_priv->port, vf, vlan, qos,
				vlan_proto);
}

static int mlx4_en_set_vf_rate(struct net_device *dev, int vf, int min_tx_rate,
			       int max_tx_rate)
{
	struct mlx4_en_priv *en_priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = en_priv->mdev;

	return mlx4_set_vf_rate(mdev->dev, en_priv->port, vf, min_tx_rate,
				max_tx_rate);
}

static int mlx4_en_set_vf_spoofchk(struct net_device *dev, int vf, bool setting)
{
	struct mlx4_en_priv *en_priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = en_priv->mdev;

	return mlx4_set_vf_spoofchk(mdev->dev, en_priv->port, vf, setting);
}

static int mlx4_en_get_vf_config(struct net_device *dev, int vf, struct ifla_vf_info *ivf)
{
	struct mlx4_en_priv *en_priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = en_priv->mdev;

	return mlx4_get_vf_config(mdev->dev, en_priv->port, vf, ivf);
}

static int mlx4_en_set_vf_link_state(struct net_device *dev, int vf, int link_state)
{
	struct mlx4_en_priv *en_priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = en_priv->mdev;

	return mlx4_set_vf_link_state(mdev->dev, en_priv->port, vf, link_state);
}

static int mlx4_en_get_vf_stats(struct net_device *dev, int vf,
				struct ifla_vf_stats *vf_stats)
{
	struct mlx4_en_priv *en_priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = en_priv->mdev;

	return mlx4_get_vf_stats(mdev->dev, en_priv->port, vf, vf_stats);
}

#define PORT_ID_BYTE_LEN 8
static int mlx4_en_get_phys_port_id(struct net_device *dev,
				    struct netdev_phys_item_id *ppid)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_dev *mdev = priv->mdev->dev;
	int i;
	u64 phys_port_id = mdev->caps.phys_port_id[priv->port];

	if (!phys_port_id)
		return -EOPNOTSUPP;

	ppid->id_len = sizeof(phys_port_id);
	for (i = PORT_ID_BYTE_LEN - 1; i >= 0; --i) {
		ppid->id[i] =  phys_port_id & 0xff;
		phys_port_id >>= 8;
	}
	return 0;
}

static void mlx4_en_add_vxlan_offloads(struct work_struct *work)
{
	int ret;
	struct mlx4_en_priv *priv = container_of(work, struct mlx4_en_priv,
						 vxlan_add_task);

	ret = mlx4_config_vxlan_port(priv->mdev->dev, priv->vxlan_port);
	if (ret)
		goto out;

	ret = mlx4_SET_PORT_VXLAN(priv->mdev->dev, priv->port,
				  VXLAN_STEER_BY_OUTER_MAC, 1);
out:
	if (ret) {
		en_err(priv, "failed setting L2 tunnel configuration ret %d\n", ret);
		return;
	}

	/* set offloads */
	priv->dev->hw_enc_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
				      NETIF_F_RXCSUM |
				      NETIF_F_TSO | NETIF_F_TSO6 |
				      NETIF_F_GSO_UDP_TUNNEL |
				      NETIF_F_GSO_UDP_TUNNEL_CSUM |
				      NETIF_F_GSO_PARTIAL;
}

static void mlx4_en_del_vxlan_offloads(struct work_struct *work)
{
	int ret;
	struct mlx4_en_priv *priv = container_of(work, struct mlx4_en_priv,
						 vxlan_del_task);
	/* unset offloads */
	priv->dev->hw_enc_features &= ~(NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
					NETIF_F_RXCSUM |
					NETIF_F_TSO | NETIF_F_TSO6 |
					NETIF_F_GSO_UDP_TUNNEL |
					NETIF_F_GSO_UDP_TUNNEL_CSUM |
					NETIF_F_GSO_PARTIAL);

	ret = mlx4_SET_PORT_VXLAN(priv->mdev->dev, priv->port,
				  VXLAN_STEER_BY_OUTER_MAC, 0);
	if (ret)
		en_err(priv, "failed setting L2 tunnel configuration ret %d\n", ret);

	priv->vxlan_port = 0;
}

static void mlx4_en_add_vxlan_port(struct  net_device *dev,
				   struct udp_tunnel_info *ti)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	__be16 port = ti->port;
	__be16 current_port;

	if (ti->type != UDP_TUNNEL_TYPE_VXLAN)
		return;

	if (ti->sa_family != AF_INET)
		return;

	if (priv->mdev->dev->caps.tunnel_offload_mode != MLX4_TUNNEL_OFFLOAD_MODE_VXLAN)
		return;

	current_port = priv->vxlan_port;
	if (current_port && current_port != port) {
		en_warn(priv, "vxlan port %d configured, can't add port %d\n",
			ntohs(current_port), ntohs(port));
		return;
	}

	priv->vxlan_port = port;
	queue_work(priv->mdev->workqueue, &priv->vxlan_add_task);
}

static void mlx4_en_del_vxlan_port(struct  net_device *dev,
				   struct udp_tunnel_info *ti)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	__be16 port = ti->port;
	__be16 current_port;

	if (ti->type != UDP_TUNNEL_TYPE_VXLAN)
		return;

	if (ti->sa_family != AF_INET)
		return;

	if (priv->mdev->dev->caps.tunnel_offload_mode != MLX4_TUNNEL_OFFLOAD_MODE_VXLAN)
		return;

	current_port = priv->vxlan_port;
	if (current_port != port) {
		en_dbg(DRV, priv, "vxlan port %d isn't configured, ignoring\n", ntohs(port));
		return;
	}

	queue_work(priv->mdev->workqueue, &priv->vxlan_del_task);
}

static netdev_features_t mlx4_en_features_check(struct sk_buff *skb,
						struct net_device *dev,
						netdev_features_t features)
{
	features = vlan_features_check(skb, features);
	features = vxlan_features_check(skb, features);

	/* The ConnectX-3 doesn't support outer IPv6 checksums but it does
	 * support inner IPv6 checksums and segmentation so  we need to
	 * strip that feature if this is an IPv6 encapsulated frame.
	 */
	if (skb->encapsulation &&
	    (skb->ip_summed == CHECKSUM_PARTIAL)) {
		struct mlx4_en_priv *priv = netdev_priv(dev);

		if (!priv->vxlan_port ||
		    (ip_hdr(skb)->version != 4) ||
		    (udp_hdr(skb)->dest != priv->vxlan_port))
			features &= ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
	}

	return features;
}

static int mlx4_en_set_tx_maxrate(struct net_device *dev, int queue_index, u32 maxrate)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_tx_ring *tx_ring = priv->tx_ring[TX][queue_index];
	struct mlx4_update_qp_params params;
	int err;

	if (!(priv->mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_QP_RATE_LIMIT))
		return -EOPNOTSUPP;

	/* rate provided to us in Mbs, check if it fits into 12 bits, if not use Gbs */
	if (maxrate >> 12) {
		params.rate_unit = MLX4_QP_RATE_LIMIT_GBS;
		params.rate_val  = maxrate / 1000;
	} else if (maxrate) {
		params.rate_unit = MLX4_QP_RATE_LIMIT_MBS;
		params.rate_val  = maxrate;
	} else { /* zero serves to revoke the QP rate-limitation */
		params.rate_unit = 0;
		params.rate_val  = 0;
	}

	err = mlx4_update_qp(priv->mdev->dev, tx_ring->qpn, MLX4_UPDATE_QP_RATE_LIMIT,
			     &params);
	return err;
}

static int mlx4_xdp_set(struct net_device *dev, struct bpf_prog *prog)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_port_profile new_prof;
	struct bpf_prog *old_prog;
	struct mlx4_en_priv *tmp;
	int tx_changed = 0;
	int xdp_ring_num;
	int port_up = 0;
	int err;
	int i;

	xdp_ring_num = prog ? priv->rx_ring_num : 0;

	/* No need to reconfigure buffers when simply swapping the
	 * program for a new one.
	 */
	if (priv->tx_ring_num[TX_XDP] == xdp_ring_num) {
		if (prog) {
			prog = bpf_prog_add(prog, priv->rx_ring_num - 1);
			if (IS_ERR(prog))
				return PTR_ERR(prog);
		}
		mutex_lock(&mdev->state_lock);
		for (i = 0; i < priv->rx_ring_num; i++) {
			old_prog = rcu_dereference_protected(
					priv->rx_ring[i]->xdp_prog,
					lockdep_is_held(&mdev->state_lock));
			rcu_assign_pointer(priv->rx_ring[i]->xdp_prog, prog);
			if (old_prog)
				bpf_prog_put(old_prog);
		}
		mutex_unlock(&mdev->state_lock);
		return 0;
	}

	if (!mlx4_en_check_xdp_mtu(dev, dev->mtu))
		return -EOPNOTSUPP;

	tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (prog) {
		prog = bpf_prog_add(prog, priv->rx_ring_num - 1);
		if (IS_ERR(prog)) {
			err = PTR_ERR(prog);
			goto out;
		}
	}

	mutex_lock(&mdev->state_lock);
	memcpy(&new_prof, priv->prof, sizeof(struct mlx4_en_port_profile));
	new_prof.tx_ring_num[TX_XDP] = xdp_ring_num;

	if (priv->tx_ring_num[TX] + xdp_ring_num > MAX_TX_RINGS) {
		tx_changed = 1;
		new_prof.tx_ring_num[TX] =
			MAX_TX_RINGS - ALIGN(xdp_ring_num, MLX4_EN_NUM_UP);
		en_warn(priv, "Reducing the number of TX rings, to not exceed the max total rings number.\n");
	}

	err = mlx4_en_try_alloc_resources(priv, tmp, &new_prof);
	if (err) {
		if (prog)
			bpf_prog_sub(prog, priv->rx_ring_num - 1);
		goto unlock_out;
	}

	if (priv->port_up) {
		port_up = 1;
		mlx4_en_stop_port(dev, 1);
	}

	mlx4_en_safe_replace_resources(priv, tmp);
	if (tx_changed)
		netif_set_real_num_tx_queues(dev, priv->tx_ring_num[TX]);

	for (i = 0; i < priv->rx_ring_num; i++) {
		old_prog = rcu_dereference_protected(
					priv->rx_ring[i]->xdp_prog,
					lockdep_is_held(&mdev->state_lock));
		rcu_assign_pointer(priv->rx_ring[i]->xdp_prog, prog);
		if (old_prog)
			bpf_prog_put(old_prog);
	}

	if (port_up) {
		err = mlx4_en_start_port(dev);
		if (err) {
			en_err(priv, "Failed starting port %d for XDP change\n",
			       priv->port);
			queue_work(mdev->workqueue, &priv->watchdog_task);
		}
	}

unlock_out:
	mutex_unlock(&mdev->state_lock);
out:
	kfree(tmp);
	return err;
}

static bool mlx4_xdp_attached(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	return !!priv->tx_ring_num[TX_XDP];
}

static int mlx4_xdp(struct net_device *dev, struct netdev_xdp *xdp)
{
	switch (xdp->command) {
	case XDP_SETUP_PROG:
		return mlx4_xdp_set(dev, xdp->prog);
	case XDP_QUERY_PROG:
		xdp->prog_attached = mlx4_xdp_attached(dev);
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct net_device_ops mlx4_netdev_ops = {
	.ndo_open		= mlx4_en_open,
	.ndo_stop		= mlx4_en_close,
	.ndo_start_xmit		= mlx4_en_xmit,
	.ndo_select_queue	= mlx4_en_select_queue,
	.ndo_get_stats64	= mlx4_en_get_stats64,
	.ndo_set_rx_mode	= mlx4_en_set_rx_mode,
	.ndo_set_mac_address	= mlx4_en_set_mac,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= mlx4_en_change_mtu,
	.ndo_do_ioctl		= mlx4_en_ioctl,
	.ndo_tx_timeout		= mlx4_en_tx_timeout,
	.ndo_vlan_rx_add_vid	= mlx4_en_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= mlx4_en_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= mlx4_en_netpoll,
#endif
	.ndo_set_features	= mlx4_en_set_features,
	.ndo_fix_features	= mlx4_en_fix_features,
	.ndo_setup_tc		= __mlx4_en_setup_tc,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer	= mlx4_en_filter_rfs,
#endif
	.ndo_get_phys_port_id	= mlx4_en_get_phys_port_id,
	.ndo_udp_tunnel_add	= mlx4_en_add_vxlan_port,
	.ndo_udp_tunnel_del	= mlx4_en_del_vxlan_port,
	.ndo_features_check	= mlx4_en_features_check,
	.ndo_set_tx_maxrate	= mlx4_en_set_tx_maxrate,
	.ndo_xdp		= mlx4_xdp,
};

static const struct net_device_ops mlx4_netdev_ops_master = {
	.ndo_open		= mlx4_en_open,
	.ndo_stop		= mlx4_en_close,
	.ndo_start_xmit		= mlx4_en_xmit,
	.ndo_select_queue	= mlx4_en_select_queue,
	.ndo_get_stats64	= mlx4_en_get_stats64,
	.ndo_set_rx_mode	= mlx4_en_set_rx_mode,
	.ndo_set_mac_address	= mlx4_en_set_mac,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= mlx4_en_change_mtu,
	.ndo_tx_timeout		= mlx4_en_tx_timeout,
	.ndo_vlan_rx_add_vid	= mlx4_en_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= mlx4_en_vlan_rx_kill_vid,
	.ndo_set_vf_mac		= mlx4_en_set_vf_mac,
	.ndo_set_vf_vlan	= mlx4_en_set_vf_vlan,
	.ndo_set_vf_rate	= mlx4_en_set_vf_rate,
	.ndo_set_vf_spoofchk	= mlx4_en_set_vf_spoofchk,
	.ndo_set_vf_link_state	= mlx4_en_set_vf_link_state,
	.ndo_get_vf_stats       = mlx4_en_get_vf_stats,
	.ndo_get_vf_config	= mlx4_en_get_vf_config,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= mlx4_en_netpoll,
#endif
	.ndo_set_features	= mlx4_en_set_features,
	.ndo_fix_features	= mlx4_en_fix_features,
	.ndo_setup_tc		= __mlx4_en_setup_tc,
#ifdef CONFIG_RFS_ACCEL
	.ndo_rx_flow_steer	= mlx4_en_filter_rfs,
#endif
	.ndo_get_phys_port_id	= mlx4_en_get_phys_port_id,
	.ndo_udp_tunnel_add	= mlx4_en_add_vxlan_port,
	.ndo_udp_tunnel_del	= mlx4_en_del_vxlan_port,
	.ndo_features_check	= mlx4_en_features_check,
	.ndo_set_tx_maxrate	= mlx4_en_set_tx_maxrate,
	.ndo_xdp		= mlx4_xdp,
};

struct mlx4_en_bond {
	struct work_struct work;
	struct mlx4_en_priv *priv;
	int is_bonded;
	struct mlx4_port_map port_map;
};

static void mlx4_en_bond_work(struct work_struct *work)
{
	struct mlx4_en_bond *bond = container_of(work,
						     struct mlx4_en_bond,
						     work);
	int err = 0;
	struct mlx4_dev *dev = bond->priv->mdev->dev;

	if (bond->is_bonded) {
		if (!mlx4_is_bonded(dev)) {
			err = mlx4_bond(dev);
			if (err)
				en_err(bond->priv, "Fail to bond device\n");
		}
		if (!err) {
			err = mlx4_port_map_set(dev, &bond->port_map);
			if (err)
				en_err(bond->priv, "Fail to set port map [%d][%d]: %d\n",
				       bond->port_map.port1,
				       bond->port_map.port2,
				       err);
		}
	} else if (mlx4_is_bonded(dev)) {
		err = mlx4_unbond(dev);
		if (err)
			en_err(bond->priv, "Fail to unbond device\n");
	}
	dev_put(bond->priv->dev);
	kfree(bond);
}

static int mlx4_en_queue_bond_work(struct mlx4_en_priv *priv, int is_bonded,
				   u8 v2p_p1, u8 v2p_p2)
{
	struct mlx4_en_bond *bond = NULL;

	bond = kzalloc(sizeof(*bond), GFP_ATOMIC);
	if (!bond)
		return -ENOMEM;

	INIT_WORK(&bond->work, mlx4_en_bond_work);
	bond->priv = priv;
	bond->is_bonded = is_bonded;
	bond->port_map.port1 = v2p_p1;
	bond->port_map.port2 = v2p_p2;
	dev_hold(priv->dev);
	queue_work(priv->mdev->workqueue, &bond->work);
	return 0;
}

int mlx4_en_netdev_event(struct notifier_block *this,
			 unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	u8 port = 0;
	struct mlx4_en_dev *mdev;
	struct mlx4_dev *dev;
	int i, num_eth_ports = 0;
	bool do_bond = true;
	struct mlx4_en_priv *priv;
	u8 v2p_port1 = 0;
	u8 v2p_port2 = 0;

	if (!net_eq(dev_net(ndev), &init_net))
		return NOTIFY_DONE;

	mdev = container_of(this, struct mlx4_en_dev, nb);
	dev = mdev->dev;

	/* Go into this mode only when two network devices set on two ports
	 * of the same mlx4 device are slaves of the same bonding master
	 */
	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_ETH) {
		++num_eth_ports;
		if (!port && (mdev->pndev[i] == ndev))
			port = i;
		mdev->upper[i] = mdev->pndev[i] ?
			netdev_master_upper_dev_get(mdev->pndev[i]) : NULL;
		/* condition not met: network device is a slave */
		if (!mdev->upper[i])
			do_bond = false;
		if (num_eth_ports < 2)
			continue;
		/* condition not met: same master */
		if (mdev->upper[i] != mdev->upper[i-1])
			do_bond = false;
	}
	/* condition not met: 2 salves */
	do_bond = (num_eth_ports ==  2) ? do_bond : false;

	/* handle only events that come with enough info */
	if ((do_bond && (event != NETDEV_BONDING_INFO)) || !port)
		return NOTIFY_DONE;

	priv = netdev_priv(ndev);
	if (do_bond) {
		struct netdev_notifier_bonding_info *notifier_info = ptr;
		struct netdev_bonding_info *bonding_info =
			&notifier_info->bonding_info;

		/* required mode 1, 2 or 4 */
		if ((bonding_info->master.bond_mode != BOND_MODE_ACTIVEBACKUP) &&
		    (bonding_info->master.bond_mode != BOND_MODE_XOR) &&
		    (bonding_info->master.bond_mode != BOND_MODE_8023AD))
			do_bond = false;

		/* require exactly 2 slaves */
		if (bonding_info->master.num_slaves != 2)
			do_bond = false;

		/* calc v2p */
		if (do_bond) {
			if (bonding_info->master.bond_mode ==
			    BOND_MODE_ACTIVEBACKUP) {
				/* in active-backup mode virtual ports are
				 * mapped to the physical port of the active
				 * slave */
				if (bonding_info->slave.state ==
				    BOND_STATE_BACKUP) {
					if (port == 1) {
						v2p_port1 = 2;
						v2p_port2 = 2;
					} else {
						v2p_port1 = 1;
						v2p_port2 = 1;
					}
				} else { /* BOND_STATE_ACTIVE */
					if (port == 1) {
						v2p_port1 = 1;
						v2p_port2 = 1;
					} else {
						v2p_port1 = 2;
						v2p_port2 = 2;
					}
				}
			} else { /* Active-Active */
				/* in active-active mode a virtual port is
				 * mapped to the native physical port if and only
				 * if the physical port is up */
				__s8 link = bonding_info->slave.link;

				if (port == 1)
					v2p_port2 = 2;
				else
					v2p_port1 = 1;
				if ((link == BOND_LINK_UP) ||
				    (link == BOND_LINK_FAIL)) {
					if (port == 1)
						v2p_port1 = 1;
					else
						v2p_port2 = 2;
				} else { /* BOND_LINK_DOWN || BOND_LINK_BACK */
					if (port == 1)
						v2p_port1 = 2;
					else
						v2p_port2 = 1;
				}
			}
		}
	}

	mlx4_en_queue_bond_work(priv, do_bond,
				v2p_port1, v2p_port2);

	return NOTIFY_DONE;
}

void mlx4_en_update_pfc_stats_bitmap(struct mlx4_dev *dev,
				     struct mlx4_en_stats_bitmap *stats_bitmap,
				     u8 rx_ppp, u8 rx_pause,
				     u8 tx_ppp, u8 tx_pause)
{
	int last_i = NUM_MAIN_STATS + NUM_PORT_STATS + NUM_PF_STATS;

	if (!mlx4_is_slave(dev) &&
	    (dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_FLOWSTATS_EN)) {
		mutex_lock(&stats_bitmap->mutex);
		bitmap_clear(stats_bitmap->bitmap, last_i, NUM_FLOW_STATS);

		if (rx_ppp)
			bitmap_set(stats_bitmap->bitmap, last_i,
				   NUM_FLOW_PRIORITY_STATS_RX);
		last_i += NUM_FLOW_PRIORITY_STATS_RX;

		if (rx_pause && !(rx_ppp))
			bitmap_set(stats_bitmap->bitmap, last_i,
				   NUM_FLOW_STATS_RX);
		last_i += NUM_FLOW_STATS_RX;

		if (tx_ppp)
			bitmap_set(stats_bitmap->bitmap, last_i,
				   NUM_FLOW_PRIORITY_STATS_TX);
		last_i += NUM_FLOW_PRIORITY_STATS_TX;

		if (tx_pause && !(tx_ppp))
			bitmap_set(stats_bitmap->bitmap, last_i,
				   NUM_FLOW_STATS_TX);
		last_i += NUM_FLOW_STATS_TX;

		mutex_unlock(&stats_bitmap->mutex);
	}
}

void mlx4_en_set_stats_bitmap(struct mlx4_dev *dev,
			      struct mlx4_en_stats_bitmap *stats_bitmap,
			      u8 rx_ppp, u8 rx_pause,
			      u8 tx_ppp, u8 tx_pause)
{
	int last_i = 0;

	mutex_init(&stats_bitmap->mutex);
	bitmap_zero(stats_bitmap->bitmap, NUM_ALL_STATS);

	if (mlx4_is_slave(dev)) {
		bitmap_set(stats_bitmap->bitmap, last_i +
					 MLX4_FIND_NETDEV_STAT(rx_packets), 1);
		bitmap_set(stats_bitmap->bitmap, last_i +
					 MLX4_FIND_NETDEV_STAT(tx_packets), 1);
		bitmap_set(stats_bitmap->bitmap, last_i +
					 MLX4_FIND_NETDEV_STAT(rx_bytes), 1);
		bitmap_set(stats_bitmap->bitmap, last_i +
					 MLX4_FIND_NETDEV_STAT(tx_bytes), 1);
		bitmap_set(stats_bitmap->bitmap, last_i +
					 MLX4_FIND_NETDEV_STAT(rx_dropped), 1);
		bitmap_set(stats_bitmap->bitmap, last_i +
					 MLX4_FIND_NETDEV_STAT(tx_dropped), 1);
	} else {
		bitmap_set(stats_bitmap->bitmap, last_i, NUM_MAIN_STATS);
	}
	last_i += NUM_MAIN_STATS;

	bitmap_set(stats_bitmap->bitmap, last_i, NUM_PORT_STATS);
	last_i += NUM_PORT_STATS;

	if (mlx4_is_master(dev))
		bitmap_set(stats_bitmap->bitmap, last_i,
			   NUM_PF_STATS);
	last_i += NUM_PF_STATS;

	mlx4_en_update_pfc_stats_bitmap(dev, stats_bitmap,
					rx_ppp, rx_pause,
					tx_ppp, tx_pause);
	last_i += NUM_FLOW_STATS;

	if (!mlx4_is_slave(dev))
		bitmap_set(stats_bitmap->bitmap, last_i, NUM_PKT_STATS);
	last_i += NUM_PKT_STATS;

	bitmap_set(stats_bitmap->bitmap, last_i, NUM_XDP_STATS);
	last_i += NUM_XDP_STATS;
}

int mlx4_en_init_netdev(struct mlx4_en_dev *mdev, int port,
			struct mlx4_en_port_profile *prof)
{
	struct net_device *dev;
	struct mlx4_en_priv *priv;
	int i, t;
	int err;

	dev = alloc_etherdev_mqs(sizeof(struct mlx4_en_priv),
				 MAX_TX_RINGS, MAX_RX_RINGS);
	if (dev == NULL)
		return -ENOMEM;

	netif_set_real_num_tx_queues(dev, prof->tx_ring_num[TX]);
	netif_set_real_num_rx_queues(dev, prof->rx_ring_num);

	SET_NETDEV_DEV(dev, &mdev->dev->persist->pdev->dev);
	dev->dev_port = port - 1;

	/*
	 * Initialize driver private data
	 */

	priv = netdev_priv(dev);
	memset(priv, 0, sizeof(struct mlx4_en_priv));
	priv->counter_index = MLX4_SINK_COUNTER_INDEX(mdev->dev);
	spin_lock_init(&priv->stats_lock);
	INIT_WORK(&priv->rx_mode_task, mlx4_en_do_set_rx_mode);
	INIT_WORK(&priv->watchdog_task, mlx4_en_restart);
	INIT_WORK(&priv->linkstate_task, mlx4_en_linkstate);
	INIT_DELAYED_WORK(&priv->stats_task, mlx4_en_do_get_stats);
	INIT_DELAYED_WORK(&priv->service_task, mlx4_en_service_task);
	INIT_WORK(&priv->vxlan_add_task, mlx4_en_add_vxlan_offloads);
	INIT_WORK(&priv->vxlan_del_task, mlx4_en_del_vxlan_offloads);
#ifdef CONFIG_RFS_ACCEL
	INIT_LIST_HEAD(&priv->filters);
	spin_lock_init(&priv->filters_lock);
#endif

	priv->dev = dev;
	priv->mdev = mdev;
	priv->ddev = &mdev->pdev->dev;
	priv->prof = prof;
	priv->port = port;
	priv->port_up = false;
	priv->flags = prof->flags;
	priv->pflags = MLX4_EN_PRIV_FLAGS_BLUEFLAME;
	priv->ctrl_flags = cpu_to_be32(MLX4_WQE_CTRL_CQ_UPDATE |
			MLX4_WQE_CTRL_SOLICITED);
	priv->num_tx_rings_p_up = mdev->profile.num_tx_rings_p_up;
	priv->tx_work_limit = MLX4_EN_DEFAULT_TX_WORK;
	netdev_rss_key_fill(priv->rss_key, sizeof(priv->rss_key));

	for (t = 0; t < MLX4_EN_NUM_TX_TYPES; t++) {
		priv->tx_ring_num[t] = prof->tx_ring_num[t];
		if (!priv->tx_ring_num[t])
			continue;

		priv->tx_ring[t] = kzalloc(sizeof(struct mlx4_en_tx_ring *) *
					   MAX_TX_RINGS, GFP_KERNEL);
		if (!priv->tx_ring[t]) {
			err = -ENOMEM;
			goto err_free_tx;
		}
		priv->tx_cq[t] = kzalloc(sizeof(struct mlx4_en_cq *) *
					 MAX_TX_RINGS, GFP_KERNEL);
		if (!priv->tx_cq[t]) {
			kfree(priv->tx_ring[t]);
			err = -ENOMEM;
			goto out;
		}
	}
	priv->rx_ring_num = prof->rx_ring_num;
	priv->cqe_factor = (mdev->dev->caps.cqe_size == 64) ? 1 : 0;
	priv->cqe_size = mdev->dev->caps.cqe_size;
	priv->mac_index = -1;
	priv->msg_enable = MLX4_EN_MSG_LEVEL;
#ifdef CONFIG_MLX4_EN_DCB
	if (!mlx4_is_slave(priv->mdev->dev)) {
		priv->dcbx_cap = DCB_CAP_DCBX_VER_CEE | DCB_CAP_DCBX_HOST |
			DCB_CAP_DCBX_VER_IEEE;
		priv->flags |= MLX4_EN_DCB_ENABLED;
		priv->cee_config.pfc_state = false;

		for (i = 0; i < MLX4_EN_NUM_UP; i++)
			priv->cee_config.dcb_pfc[i] = pfc_disabled;

		if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_ETS_CFG) {
			dev->dcbnl_ops = &mlx4_en_dcbnl_ops;
		} else {
			en_info(priv, "enabling only PFC DCB ops\n");
			dev->dcbnl_ops = &mlx4_en_dcbnl_pfc_ops;
		}
	}
#endif

	for (i = 0; i < MLX4_EN_MAC_HASH_SIZE; ++i)
		INIT_HLIST_HEAD(&priv->mac_hash[i]);

	/* Query for default mac and max mtu */
	priv->max_mtu = mdev->dev->caps.eth_mtu_cap[priv->port];

	if (mdev->dev->caps.rx_checksum_flags_port[priv->port] &
	    MLX4_RX_CSUM_MODE_VAL_NON_TCP_UDP)
		priv->flags |= MLX4_EN_FLAG_RX_CSUM_NON_TCP_UDP;

	/* Set default MAC */
	dev->addr_len = ETH_ALEN;
	mlx4_en_u64_to_mac(dev->dev_addr, mdev->dev->caps.def_mac[priv->port]);
	if (!is_valid_ether_addr(dev->dev_addr)) {
		en_err(priv, "Port: %d, invalid mac burned: %pM, quiting\n",
		       priv->port, dev->dev_addr);
		err = -EINVAL;
		goto out;
	} else if (mlx4_is_slave(priv->mdev->dev) &&
		   (priv->mdev->dev->port_random_macs & 1 << priv->port)) {
		/* Random MAC was assigned in mlx4_slave_cap
		 * in mlx4_core module
		 */
		dev->addr_assign_type |= NET_ADDR_RANDOM;
		en_warn(priv, "Assigned random MAC address %pM\n", dev->dev_addr);
	}

	memcpy(priv->current_mac, dev->dev_addr, sizeof(priv->current_mac));

	priv->stride = roundup_pow_of_two(sizeof(struct mlx4_en_rx_desc) +
					  DS_SIZE * MLX4_EN_MAX_RX_FRAGS);
	err = mlx4_en_alloc_resources(priv);
	if (err)
		goto out;

	/* Initialize time stamping config */
	priv->hwtstamp_config.flags = 0;
	priv->hwtstamp_config.tx_type = HWTSTAMP_TX_OFF;
	priv->hwtstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;

	/* Allocate page for receive rings */
	err = mlx4_alloc_hwq_res(mdev->dev, &priv->res,
				MLX4_EN_PAGE_SIZE);
	if (err) {
		en_err(priv, "Failed to allocate page for rx qps\n");
		goto out;
	}
	priv->allocated = 1;

	/*
	 * Initialize netdev entry points
	 */
	if (mlx4_is_master(priv->mdev->dev))
		dev->netdev_ops = &mlx4_netdev_ops_master;
	else
		dev->netdev_ops = &mlx4_netdev_ops;
	dev->watchdog_timeo = MLX4_EN_WATCHDOG_TIMEOUT;
	netif_set_real_num_tx_queues(dev, priv->tx_ring_num[TX]);
	netif_set_real_num_rx_queues(dev, priv->rx_ring_num);

	dev->ethtool_ops = &mlx4_en_ethtool_ops;

	/*
	 * Set driver features
	 */
	dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	if (mdev->LSO_support)
		dev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;

	dev->vlan_features = dev->hw_features;

	dev->hw_features |= NETIF_F_RXCSUM | NETIF_F_RXHASH;
	dev->features = dev->hw_features | NETIF_F_HIGHDMA |
			NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX |
			NETIF_F_HW_VLAN_CTAG_FILTER;
	dev->hw_features |= NETIF_F_LOOPBACK |
			NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;

	if (!(mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_SKIP_OUTER_VLAN)) {
		dev->features |= NETIF_F_HW_VLAN_STAG_RX |
			NETIF_F_HW_VLAN_STAG_FILTER;
		dev->hw_features |= NETIF_F_HW_VLAN_STAG_RX;
	}

	if (mlx4_is_slave(mdev->dev)) {
		bool vlan_offload_disabled;
		int phv;

		err = get_phv_bit(mdev->dev, port, &phv);
		if (!err && phv) {
			dev->hw_features |= NETIF_F_HW_VLAN_STAG_TX;
			priv->pflags |= MLX4_EN_PRIV_FLAGS_PHV;
		}
		err = mlx4_get_is_vlan_offload_disabled(mdev->dev, port,
							&vlan_offload_disabled);
		if (!err && vlan_offload_disabled) {
			dev->hw_features &= ~(NETIF_F_HW_VLAN_CTAG_TX |
					      NETIF_F_HW_VLAN_CTAG_RX |
					      NETIF_F_HW_VLAN_STAG_TX |
					      NETIF_F_HW_VLAN_STAG_RX);
			dev->features &= ~(NETIF_F_HW_VLAN_CTAG_TX |
					   NETIF_F_HW_VLAN_CTAG_RX |
					   NETIF_F_HW_VLAN_STAG_TX |
					   NETIF_F_HW_VLAN_STAG_RX);
		}
	} else {
		if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_PHV_EN &&
		    !(mdev->dev->caps.flags2 &
		      MLX4_DEV_CAP_FLAG2_SKIP_OUTER_VLAN))
			dev->hw_features |= NETIF_F_HW_VLAN_STAG_TX;
	}

	if (mdev->dev->caps.flags & MLX4_DEV_CAP_FLAG_FCS_KEEP)
		dev->hw_features |= NETIF_F_RXFCS;

	if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_IGNORE_FCS)
		dev->hw_features |= NETIF_F_RXALL;

	if (mdev->dev->caps.steering_mode ==
	    MLX4_STEERING_MODE_DEVICE_MANAGED &&
	    mdev->dev->caps.dmfs_high_steer_mode != MLX4_STEERING_DMFS_A0_STATIC)
		dev->hw_features |= NETIF_F_NTUPLE;

	if (mdev->dev->caps.steering_mode != MLX4_STEERING_MODE_A0)
		dev->priv_flags |= IFF_UNICAST_FLT;

	/* Setting a default hash function value */
	if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_RSS_TOP) {
		priv->rss_hash_fn = ETH_RSS_HASH_TOP;
	} else if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_RSS_XOR) {
		priv->rss_hash_fn = ETH_RSS_HASH_XOR;
	} else {
		en_warn(priv,
			"No RSS hash capabilities exposed, using Toeplitz\n");
		priv->rss_hash_fn = ETH_RSS_HASH_TOP;
	}

	if (mdev->dev->caps.tunnel_offload_mode == MLX4_TUNNEL_OFFLOAD_MODE_VXLAN) {
		dev->hw_features |= NETIF_F_GSO_UDP_TUNNEL |
				    NETIF_F_GSO_UDP_TUNNEL_CSUM |
				    NETIF_F_GSO_PARTIAL;
		dev->features    |= NETIF_F_GSO_UDP_TUNNEL |
				    NETIF_F_GSO_UDP_TUNNEL_CSUM |
				    NETIF_F_GSO_PARTIAL;
		dev->gso_partial_features = NETIF_F_GSO_UDP_TUNNEL_CSUM;
	}

	/* MTU range: 46 - hw-specific max */
	dev->min_mtu = MLX4_EN_MIN_MTU;
	dev->max_mtu = priv->max_mtu;

	mdev->pndev[port] = dev;
	mdev->upper[port] = NULL;

	netif_carrier_off(dev);
	mlx4_en_set_default_moderation(priv);

	en_warn(priv, "Using %d TX rings\n", prof->tx_ring_num[TX]);
	en_warn(priv, "Using %d RX rings\n", prof->rx_ring_num);

	mlx4_en_update_loopback_state(priv->dev, priv->dev->features);

	/* Configure port */
	mlx4_en_calc_rx_buf(dev);
	err = mlx4_SET_PORT_general(mdev->dev, priv->port,
				    priv->rx_skb_size + ETH_FCS_LEN,
				    prof->tx_pause, prof->tx_ppp,
				    prof->rx_pause, prof->rx_ppp);
	if (err) {
		en_err(priv, "Failed setting port general configurations for port %d, with error %d\n",
		       priv->port, err);
		goto out;
	}

	if (mdev->dev->caps.tunnel_offload_mode == MLX4_TUNNEL_OFFLOAD_MODE_VXLAN) {
		err = mlx4_SET_PORT_VXLAN(mdev->dev, priv->port, VXLAN_STEER_BY_OUTER_MAC, 1);
		if (err) {
			en_err(priv, "Failed setting port L2 tunnel configuration, err %d\n",
			       err);
			goto out;
		}
	}

	/* Init port */
	en_warn(priv, "Initializing port\n");
	err = mlx4_INIT_PORT(mdev->dev, priv->port);
	if (err) {
		en_err(priv, "Failed Initializing port\n");
		goto out;
	}
	queue_delayed_work(mdev->workqueue, &priv->stats_task, STATS_DELAY);

	/* Initialize time stamp mechanism */
	if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_TS)
		mlx4_en_init_timestamp(mdev);

	queue_delayed_work(mdev->workqueue, &priv->service_task,
			   SERVICE_TASK_DELAY);

	mlx4_en_set_stats_bitmap(mdev->dev, &priv->stats_bitmap,
				 mdev->profile.prof[priv->port].rx_ppp,
				 mdev->profile.prof[priv->port].rx_pause,
				 mdev->profile.prof[priv->port].tx_ppp,
				 mdev->profile.prof[priv->port].tx_pause);

	err = register_netdev(dev);
	if (err) {
		en_err(priv, "Netdev registration failed for port %d\n", port);
		goto out;
	}

	priv->registered = 1;
	devlink_port_type_eth_set(mlx4_get_devlink_port(mdev->dev, priv->port),
				  dev);

	return 0;

err_free_tx:
	while (t--) {
		kfree(priv->tx_ring[t]);
		kfree(priv->tx_cq[t]);
	}
out:
	mlx4_en_destroy_netdev(dev);
	return err;
}

int mlx4_en_reset_config(struct net_device *dev,
			 struct hwtstamp_config ts_config,
			 netdev_features_t features)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_port_profile new_prof;
	struct mlx4_en_priv *tmp;
	int port_up = 0;
	int err = 0;

	if (priv->hwtstamp_config.tx_type == ts_config.tx_type &&
	    priv->hwtstamp_config.rx_filter == ts_config.rx_filter &&
	    !DEV_FEATURE_CHANGED(dev, features, NETIF_F_HW_VLAN_CTAG_RX) &&
	    !DEV_FEATURE_CHANGED(dev, features, NETIF_F_RXFCS))
		return 0; /* Nothing to change */

	if (DEV_FEATURE_CHANGED(dev, features, NETIF_F_HW_VLAN_CTAG_RX) &&
	    (features & NETIF_F_HW_VLAN_CTAG_RX) &&
	    (priv->hwtstamp_config.rx_filter != HWTSTAMP_FILTER_NONE)) {
		en_warn(priv, "Can't turn ON rx vlan offload while time-stamping rx filter is ON\n");
		return -EINVAL;
	}

	tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	mutex_lock(&mdev->state_lock);

	memcpy(&new_prof, priv->prof, sizeof(struct mlx4_en_port_profile));
	memcpy(&new_prof.hwtstamp_config, &ts_config, sizeof(ts_config));

	err = mlx4_en_try_alloc_resources(priv, tmp, &new_prof);
	if (err)
		goto out;

	if (priv->port_up) {
		port_up = 1;
		mlx4_en_stop_port(dev, 1);
	}

	en_warn(priv, "Changing device configuration rx filter(%x) rx vlan(%x)\n",
		ts_config.rx_filter,
		!!(features & NETIF_F_HW_VLAN_CTAG_RX));

	mlx4_en_safe_replace_resources(priv, tmp);

	if (DEV_FEATURE_CHANGED(dev, features, NETIF_F_HW_VLAN_CTAG_RX)) {
		if (features & NETIF_F_HW_VLAN_CTAG_RX)
			dev->features |= NETIF_F_HW_VLAN_CTAG_RX;
		else
			dev->features &= ~NETIF_F_HW_VLAN_CTAG_RX;
	} else if (ts_config.rx_filter == HWTSTAMP_FILTER_NONE) {
		/* RX time-stamping is OFF, update the RX vlan offload
		 * to the latest wanted state
		 */
		if (dev->wanted_features & NETIF_F_HW_VLAN_CTAG_RX)
			dev->features |= NETIF_F_HW_VLAN_CTAG_RX;
		else
			dev->features &= ~NETIF_F_HW_VLAN_CTAG_RX;
	}

	if (DEV_FEATURE_CHANGED(dev, features, NETIF_F_RXFCS)) {
		if (features & NETIF_F_RXFCS)
			dev->features |= NETIF_F_RXFCS;
		else
			dev->features &= ~NETIF_F_RXFCS;
	}

	/* RX vlan offload and RX time-stamping can't co-exist !
	 * Regardless of the caller's choice,
	 * Turn Off RX vlan offload in case of time-stamping is ON
	 */
	if (ts_config.rx_filter != HWTSTAMP_FILTER_NONE) {
		if (dev->features & NETIF_F_HW_VLAN_CTAG_RX)
			en_warn(priv, "Turning off RX vlan offload since RX time-stamping is ON\n");
		dev->features &= ~NETIF_F_HW_VLAN_CTAG_RX;
	}

	if (port_up) {
		err = mlx4_en_start_port(dev);
		if (err)
			en_err(priv, "Failed starting port\n");
	}

out:
	mutex_unlock(&mdev->state_lock);
	kfree(tmp);
	if (!err)
		netdev_features_change(dev);
	return err;
}
