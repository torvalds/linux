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

#include <linux/etherdevice.h>
#include <linux/tcp.h>
#include <linux/if_vlan.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <linux/mlx4/driver.h>
#include <linux/mlx4/device.h>
#include <linux/mlx4/cmd.h>
#include <linux/mlx4/cq.h>

#include "mlx4_en.h"
#include "en_port.h"

static int mlx4_en_vlan_rx_add_vid(struct net_device *dev, unsigned short vid)
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
		if (err)
			en_err(priv, "Failed configuring VLAN filter\n");
	}
	if (mlx4_register_vlan(mdev->dev, priv->port, vid, &idx))
		en_err(priv, "failed adding vlan %d\n", vid);
	mutex_unlock(&mdev->state_lock);

	return 0;
}

static int mlx4_en_vlan_rx_kill_vid(struct net_device *dev, unsigned short vid)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;
	int idx;

	en_dbg(HW, priv, "Killing VID:%d\n", vid);

	clear_bit(vid, priv->active_vlans);

	/* Remove VID from port VLAN filter */
	mutex_lock(&mdev->state_lock);
	if (!mlx4_find_cached_vlan(mdev->dev, priv->port, vid, &idx))
		mlx4_unregister_vlan(mdev->dev, priv->port, idx);
	else
		en_err(priv, "could not find vid %d in cache\n", vid);

	if (mdev->device_up && priv->port_up) {
		err = mlx4_SET_VLAN_FLTR(mdev->dev, priv);
		if (err)
			en_err(priv, "Failed configuring VLAN filter\n");
	}
	mutex_unlock(&mdev->state_lock);

	return 0;
}

u64 mlx4_en_mac_to_u64(u8 *addr)
{
	u64 mac = 0;
	int i;

	for (i = 0; i < ETH_ALEN; i++) {
		mac <<= 8;
		mac |= addr[i];
	}
	return mac;
}

static int mlx4_en_set_mac(struct net_device *dev, void *addr)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct sockaddr *saddr = addr;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, saddr->sa_data, ETH_ALEN);
	priv->mac = mlx4_en_mac_to_u64(dev->dev_addr);
	queue_work(mdev->workqueue, &priv->mac_task);
	return 0;
}

static void mlx4_en_do_set_mac(struct work_struct *work)
{
	struct mlx4_en_priv *priv = container_of(work, struct mlx4_en_priv,
						 mac_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err = 0;

	mutex_lock(&mdev->state_lock);
	if (priv->port_up) {
		/* Remove old MAC and insert the new one */
		err = mlx4_replace_mac(mdev->dev, priv->port,
				       priv->base_qpn, priv->mac);
		if (err)
			en_err(priv, "Failed changing HW MAC address\n");
	} else
		en_dbg(HW, priv, "Port is down while "
				 "registering mac, exiting...\n");

	mutex_unlock(&mdev->state_lock);
}

static void mlx4_en_clear_list(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	kfree(priv->mc_addrs);
	priv->mc_addrs = NULL;
	priv->mc_addrs_cnt = 0;
}

static void mlx4_en_cache_mclist(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	char *mc_addrs;
	int mc_addrs_cnt = netdev_mc_count(dev);
	int i;

	mc_addrs = kmalloc(mc_addrs_cnt * ETH_ALEN, GFP_ATOMIC);
	if (!mc_addrs) {
		en_err(priv, "failed to allocate multicast list\n");
		return;
	}
	i = 0;
	netdev_for_each_mc_addr(ha, dev)
		memcpy(mc_addrs + i++ * ETH_ALEN, ha->addr, ETH_ALEN);
	mlx4_en_clear_list(dev);
	priv->mc_addrs = mc_addrs;
	priv->mc_addrs_cnt = mc_addrs_cnt;
}


static void mlx4_en_set_multicast(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	if (!priv->port_up)
		return;

	queue_work(priv->mdev->workqueue, &priv->mcast_task);
}

static void mlx4_en_do_set_multicast(struct work_struct *work)
{
	struct mlx4_en_priv *priv = container_of(work, struct mlx4_en_priv,
						 mcast_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct net_device *dev = priv->dev;
	u64 mcast_addr = 0;
	u8 mc_list[16] = {0};
	int err;

	mutex_lock(&mdev->state_lock);
	if (!mdev->device_up) {
		en_dbg(HW, priv, "Card is not up, "
				 "ignoring multicast change.\n");
		goto out;
	}
	if (!priv->port_up) {
		en_dbg(HW, priv, "Port is down, "
				 "ignoring  multicast change.\n");
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

	/*
	 * Promsicuous mode: disable all filters
	 */

	if (dev->flags & IFF_PROMISC) {
		if (!(priv->flags & MLX4_EN_FLAG_PROMISC)) {
			if (netif_msg_rx_status(priv))
				en_warn(priv, "Entering promiscuous mode\n");
			priv->flags |= MLX4_EN_FLAG_PROMISC;

			/* Enable promiscouos mode */
			if (!(mdev->dev->caps.flags &
						MLX4_DEV_CAP_FLAG_VEP_UC_STEER))
				err = mlx4_SET_PORT_qpn_calc(mdev->dev, priv->port,
							     priv->base_qpn, 1);
			else
				err = mlx4_unicast_promisc_add(mdev->dev, priv->base_qpn,
							       priv->port);
			if (err)
				en_err(priv, "Failed enabling "
					     "promiscuous mode\n");

			/* Disable port multicast filter (unconditionally) */
			err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
						  0, MLX4_MCAST_DISABLE);
			if (err)
				en_err(priv, "Failed disabling "
					     "multicast filter\n");

			/* Add the default qp number as multicast promisc */
			if (!(priv->flags & MLX4_EN_FLAG_MC_PROMISC)) {
				err = mlx4_multicast_promisc_add(mdev->dev, priv->base_qpn,
								 priv->port);
				if (err)
					en_err(priv, "Failed entering multicast promisc mode\n");
				priv->flags |= MLX4_EN_FLAG_MC_PROMISC;
			}

			/* Disable port VLAN filter */
			err = mlx4_SET_VLAN_FLTR(mdev->dev, priv);
			if (err)
				en_err(priv, "Failed disabling VLAN filter\n");
		}
		goto out;
	}

	/*
	 * Not in promiscuous mode
	 */

	if (priv->flags & MLX4_EN_FLAG_PROMISC) {
		if (netif_msg_rx_status(priv))
			en_warn(priv, "Leaving promiscuous mode\n");
		priv->flags &= ~MLX4_EN_FLAG_PROMISC;

		/* Disable promiscouos mode */
		if (!(mdev->dev->caps.flags & MLX4_DEV_CAP_FLAG_VEP_UC_STEER))
			err = mlx4_SET_PORT_qpn_calc(mdev->dev, priv->port,
						     priv->base_qpn, 0);
		else
			err = mlx4_unicast_promisc_remove(mdev->dev, priv->base_qpn,
							  priv->port);
		if (err)
			en_err(priv, "Failed disabling promiscuous mode\n");

		/* Disable Multicast promisc */
		if (priv->flags & MLX4_EN_FLAG_MC_PROMISC) {
			err = mlx4_multicast_promisc_remove(mdev->dev, priv->base_qpn,
							    priv->port);
			if (err)
				en_err(priv, "Failed disabling multicast promiscuous mode\n");
			priv->flags &= ~MLX4_EN_FLAG_MC_PROMISC;
		}

		/* Enable port VLAN filter */
		err = mlx4_SET_VLAN_FLTR(mdev->dev, priv);
		if (err)
			en_err(priv, "Failed enabling VLAN filter\n");
	}

	/* Enable/disable the multicast filter according to IFF_ALLMULTI */
	if (dev->flags & IFF_ALLMULTI) {
		err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
					  0, MLX4_MCAST_DISABLE);
		if (err)
			en_err(priv, "Failed disabling multicast filter\n");

		/* Add the default qp number as multicast promisc */
		if (!(priv->flags & MLX4_EN_FLAG_MC_PROMISC)) {
			err = mlx4_multicast_promisc_add(mdev->dev, priv->base_qpn,
							 priv->port);
			if (err)
				en_err(priv, "Failed entering multicast promisc mode\n");
			priv->flags |= MLX4_EN_FLAG_MC_PROMISC;
		}
	} else {
		int i;
		/* Disable Multicast promisc */
		if (priv->flags & MLX4_EN_FLAG_MC_PROMISC) {
			err = mlx4_multicast_promisc_remove(mdev->dev, priv->base_qpn,
							    priv->port);
			if (err)
				en_err(priv, "Failed disabling multicast promiscuous mode\n");
			priv->flags &= ~MLX4_EN_FLAG_MC_PROMISC;
		}

		err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
					  0, MLX4_MCAST_DISABLE);
		if (err)
			en_err(priv, "Failed disabling multicast filter\n");

		/* Detach our qp from all the multicast addresses */
		for (i = 0; i < priv->mc_addrs_cnt; i++) {
			memcpy(&mc_list[10], priv->mc_addrs + i * ETH_ALEN, ETH_ALEN);
			mc_list[5] = priv->port;
			mlx4_multicast_detach(mdev->dev, &priv->rss_map.indir_qp,
					      mc_list, MLX4_PROT_ETH);
		}
		/* Flush mcast filter and init it with broadcast address */
		mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, ETH_BCAST,
				    1, MLX4_MCAST_CONFIG);

		/* Update multicast list - we cache all addresses so they won't
		 * change while HW is updated holding the command semaphor */
		netif_tx_lock_bh(dev);
		mlx4_en_cache_mclist(dev);
		netif_tx_unlock_bh(dev);
		for (i = 0; i < priv->mc_addrs_cnt; i++) {
			mcast_addr =
			      mlx4_en_mac_to_u64(priv->mc_addrs + i * ETH_ALEN);
			memcpy(&mc_list[10], priv->mc_addrs + i * ETH_ALEN, ETH_ALEN);
			mc_list[5] = priv->port;
			mlx4_multicast_attach(mdev->dev, &priv->rss_map.indir_qp,
					      mc_list, 0, MLX4_PROT_ETH);
			mlx4_SET_MCAST_FLTR(mdev->dev, priv->port,
					    mcast_addr, 0, MLX4_MCAST_CONFIG);
		}
		err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
					  0, MLX4_MCAST_ENABLE);
		if (err)
			en_err(priv, "Failed enabling multicast filter\n");
	}
out:
	mutex_unlock(&mdev->state_lock);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mlx4_en_netpoll(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_cq *cq;
	unsigned long flags;
	int i;

	for (i = 0; i < priv->rx_ring_num; i++) {
		cq = &priv->rx_cq[i];
		spin_lock_irqsave(&cq->lock, flags);
		napi_synchronize(&cq->napi);
		mlx4_en_process_rx_cq(dev, cq, 0);
		spin_unlock_irqrestore(&cq->lock, flags);
	}
}
#endif

static void mlx4_en_tx_timeout(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;

	if (netif_msg_timer(priv))
		en_warn(priv, "Tx timeout called on port:%d\n", priv->port);

	priv->port_stats.tx_timeout++;
	en_dbg(DRV, priv, "Scheduling watchdog\n");
	queue_work(mdev->workqueue, &priv->watchdog_task);
}


static struct net_device_stats *mlx4_en_get_stats(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	spin_lock_bh(&priv->stats_lock);
	memcpy(&priv->ret_stats, &priv->stats, sizeof(priv->stats));
	spin_unlock_bh(&priv->stats_lock);

	return &priv->ret_stats;
}

static void mlx4_en_set_default_moderation(struct mlx4_en_priv *priv)
{
	struct mlx4_en_cq *cq;
	int i;

	/* If we haven't received a specific coalescing setting
	 * (module param), we set the moderation parameters as follows:
	 * - moder_cnt is set to the number of mtu sized packets to
	 *   satisfy our coelsing target.
	 * - moder_time is set to a fixed value.
	 */
	priv->rx_frames = MLX4_EN_RX_COAL_TARGET;
	priv->rx_usecs = MLX4_EN_RX_COAL_TIME;
	en_dbg(INTR, priv, "Default coalesing params for mtu:%d - "
			   "rx_frames:%d rx_usecs:%d\n",
		 priv->dev->mtu, priv->rx_frames, priv->rx_usecs);

	/* Setup cq moderation params */
	for (i = 0; i < priv->rx_ring_num; i++) {
		cq = &priv->rx_cq[i];
		cq->moder_cnt = priv->rx_frames;
		cq->moder_time = priv->rx_usecs;
		priv->last_moder_time[i] = MLX4_EN_AUTO_CONF;
		priv->last_moder_packets[i] = 0;
		priv->last_moder_bytes[i] = 0;
	}

	for (i = 0; i < priv->tx_ring_num; i++) {
		cq = &priv->tx_cq[i];
		cq->moder_cnt = MLX4_EN_TX_COAL_PKTS;
		cq->moder_time = MLX4_EN_TX_COAL_TIME;
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
		spin_lock_bh(&priv->stats_lock);
		rx_packets = priv->rx_ring[ring].packets;
		rx_bytes = priv->rx_ring[ring].bytes;
		spin_unlock_bh(&priv->stats_lock);

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
			cq = &priv->rx_cq[ring];
			cq->moder_time = moder_time;
			err = mlx4_en_set_cq_moder(priv, cq);
			if (err)
				en_err(priv, "Failed modifying moderation "
					     "for cq:%d\n", ring);
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

	err = mlx4_en_DUMP_ETH_STATS(mdev, priv->port, 0);
	if (err)
		en_dbg(HW, priv, "Could not update stats\n");

	mutex_lock(&mdev->state_lock);
	if (mdev->device_up) {
		if (priv->port_up)
			mlx4_en_auto_moderation(priv);

		queue_delayed_work(mdev->workqueue, &priv->stats_task, STATS_DELAY);
	}
	if (mdev->mac_removed[MLX4_MAX_PORTS + 1 - priv->port]) {
		queue_work(mdev->workqueue, &priv->mac_task);
		mdev->mac_removed[MLX4_MAX_PORTS + 1 - priv->port] = 0;
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


int mlx4_en_start_port(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_cq *cq;
	struct mlx4_en_tx_ring *tx_ring;
	int rx_index = 0;
	int tx_index = 0;
	int err = 0;
	int i;
	int j;
	u8 mc_list[16] = {0};

	if (priv->port_up) {
		en_dbg(DRV, priv, "start port called while port already up\n");
		return 0;
	}

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
		cq = &priv->rx_cq[i];

		err = mlx4_en_activate_cq(priv, cq, i);
		if (err) {
			en_err(priv, "Failed activating Rx CQ\n");
			goto cq_err;
		}
		for (j = 0; j < cq->size; j++)
			cq->buf[j].owner_sr_opcode = MLX4_CQE_OWNER_MASK;
		err = mlx4_en_set_cq_moder(priv, cq);
		if (err) {
			en_err(priv, "Failed setting cq moderation parameters");
			mlx4_en_deactivate_cq(priv, cq);
			goto cq_err;
		}
		mlx4_en_arm_cq(priv, cq);
		priv->rx_ring[i].cqn = cq->mcq.cqn;
		++rx_index;
	}

	/* Set qp number */
	en_dbg(DRV, priv, "Getting qp number for port %d\n", priv->port);
	err = mlx4_get_eth_qp(mdev->dev, priv->port,
				priv->mac, &priv->base_qpn);
	if (err) {
		en_err(priv, "Failed getting eth qp\n");
		goto cq_err;
	}
	mdev->mac_removed[priv->port] = 0;

	err = mlx4_en_config_rss_steer(priv);
	if (err) {
		en_err(priv, "Failed configuring rss steering\n");
		goto mac_err;
	}

	/* Configure tx cq's and rings */
	for (i = 0; i < priv->tx_ring_num; i++) {
		/* Configure cq */
		cq = &priv->tx_cq[i];
		err = mlx4_en_activate_cq(priv, cq, i);
		if (err) {
			en_err(priv, "Failed allocating Tx CQ\n");
			goto tx_err;
		}
		err = mlx4_en_set_cq_moder(priv, cq);
		if (err) {
			en_err(priv, "Failed setting cq moderation parameters");
			mlx4_en_deactivate_cq(priv, cq);
			goto tx_err;
		}
		en_dbg(DRV, priv, "Resetting index of collapsed CQ:%d to -1\n", i);
		cq->buf->wqe_index = cpu_to_be16(0xffff);

		/* Configure ring */
		tx_ring = &priv->tx_ring[i];
		err = mlx4_en_activate_tx_ring(priv, tx_ring, cq->mcq.cqn);
		if (err) {
			en_err(priv, "Failed allocating Tx ring\n");
			mlx4_en_deactivate_cq(priv, cq);
			goto tx_err;
		}
		/* Set initial ownership of all Tx TXBBs to SW (1) */
		for (j = 0; j < tx_ring->buf_size; j += STAMP_STRIDE)
			*((u32 *) (tx_ring->buf + j)) = 0xffffffff;
		++tx_index;
	}

	/* Configure port */
	err = mlx4_SET_PORT_general(mdev->dev, priv->port,
				    priv->rx_skb_size + ETH_FCS_LEN,
				    priv->prof->tx_pause,
				    priv->prof->tx_ppp,
				    priv->prof->rx_pause,
				    priv->prof->rx_ppp);
	if (err) {
		en_err(priv, "Failed setting port general configurations "
			     "for port %d, with error %d\n", priv->port, err);
		goto tx_err;
	}
	/* Set default qp number */
	err = mlx4_SET_PORT_qpn_calc(mdev->dev, priv->port, priv->base_qpn, 0);
	if (err) {
		en_err(priv, "Failed setting default qp numbers\n");
		goto tx_err;
	}

	/* Init port */
	en_dbg(HW, priv, "Initializing port\n");
	err = mlx4_INIT_PORT(mdev->dev, priv->port);
	if (err) {
		en_err(priv, "Failed Initializing port\n");
		goto tx_err;
	}

	/* Attach rx QP to bradcast address */
	memset(&mc_list[10], 0xff, ETH_ALEN);
	mc_list[5] = priv->port;
	if (mlx4_multicast_attach(mdev->dev, &priv->rss_map.indir_qp, mc_list,
				  0, MLX4_PROT_ETH))
		mlx4_warn(mdev, "Failed Attaching Broadcast\n");

	/* Must redo promiscuous mode setup. */
	priv->flags &= ~(MLX4_EN_FLAG_PROMISC | MLX4_EN_FLAG_MC_PROMISC);

	/* Schedule multicast task to populate multicast list */
	queue_work(mdev->workqueue, &priv->mcast_task);

	mlx4_set_stats_bitmap(mdev->dev, &priv->stats_bitmap);

	priv->port_up = true;
	netif_tx_start_all_queues(dev);
	return 0;

tx_err:
	while (tx_index--) {
		mlx4_en_deactivate_tx_ring(priv, &priv->tx_ring[tx_index]);
		mlx4_en_deactivate_cq(priv, &priv->tx_cq[tx_index]);
	}

	mlx4_en_release_rss_steer(priv);
mac_err:
	mlx4_put_eth_qp(mdev->dev, priv->port, priv->mac, priv->base_qpn);
cq_err:
	while (rx_index--)
		mlx4_en_deactivate_cq(priv, &priv->rx_cq[rx_index]);
	for (i = 0; i < priv->rx_ring_num; i++)
		mlx4_en_deactivate_rx_ring(priv, &priv->rx_ring[i]);

	return err; /* need to close devices */
}


void mlx4_en_stop_port(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int i;
	u8 mc_list[16] = {0};

	if (!priv->port_up) {
		en_dbg(DRV, priv, "stop port called while port already down\n");
		return;
	}

	/* Synchronize with tx routine */
	netif_tx_lock_bh(dev);
	netif_tx_stop_all_queues(dev);
	netif_tx_unlock_bh(dev);

	/* Set port as not active */
	priv->port_up = false;

	/* Detach All multicasts */
	memset(&mc_list[10], 0xff, ETH_ALEN);
	mc_list[5] = priv->port;
	mlx4_multicast_detach(mdev->dev, &priv->rss_map.indir_qp, mc_list,
			      MLX4_PROT_ETH);
	for (i = 0; i < priv->mc_addrs_cnt; i++) {
		memcpy(&mc_list[10], priv->mc_addrs + i * ETH_ALEN, ETH_ALEN);
		mc_list[5] = priv->port;
		mlx4_multicast_detach(mdev->dev, &priv->rss_map.indir_qp,
				      mc_list, MLX4_PROT_ETH);
	}
	mlx4_en_clear_list(dev);
	/* Flush multicast filter */
	mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0, 1, MLX4_MCAST_CONFIG);

	/* Free TX Rings */
	for (i = 0; i < priv->tx_ring_num; i++) {
		mlx4_en_deactivate_tx_ring(priv, &priv->tx_ring[i]);
		mlx4_en_deactivate_cq(priv, &priv->tx_cq[i]);
	}
	msleep(10);

	for (i = 0; i < priv->tx_ring_num; i++)
		mlx4_en_free_tx_buf(dev, &priv->tx_ring[i]);

	/* Free RSS qps */
	mlx4_en_release_rss_steer(priv);

	/* Unregister Mac address for the port */
	mlx4_put_eth_qp(mdev->dev, priv->port, priv->mac, priv->base_qpn);
	mdev->mac_removed[priv->port] = 1;

	/* Free RX Rings */
	for (i = 0; i < priv->rx_ring_num; i++) {
		mlx4_en_deactivate_rx_ring(priv, &priv->rx_ring[i]);
		while (test_bit(NAPI_STATE_SCHED, &priv->rx_cq[i].napi.state))
			msleep(1);
		mlx4_en_deactivate_cq(priv, &priv->rx_cq[i]);
	}

	/* close port*/
	mlx4_CLOSE_PORT(mdev->dev, priv->port);
}

static void mlx4_en_restart(struct work_struct *work)
{
	struct mlx4_en_priv *priv = container_of(work, struct mlx4_en_priv,
						 watchdog_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct net_device *dev = priv->dev;

	en_dbg(DRV, priv, "Watchdog task called for port %d\n", priv->port);

	mutex_lock(&mdev->state_lock);
	if (priv->port_up) {
		mlx4_en_stop_port(dev);
		if (mlx4_en_start_port(dev))
			en_err(priv, "Failed restarting port %d\n", priv->port);
	}
	mutex_unlock(&mdev->state_lock);
}

static void mlx4_en_clear_stats(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int i;

	if (mlx4_en_DUMP_ETH_STATS(mdev, priv->port, 1))
		en_dbg(HW, priv, "Failed dumping statistics\n");

	memset(&priv->stats, 0, sizeof(priv->stats));
	memset(&priv->pstats, 0, sizeof(priv->pstats));
	memset(&priv->pkstats, 0, sizeof(priv->pkstats));
	memset(&priv->port_stats, 0, sizeof(priv->port_stats));

	for (i = 0; i < priv->tx_ring_num; i++) {
		priv->tx_ring[i].bytes = 0;
		priv->tx_ring[i].packets = 0;
		priv->tx_ring[i].tx_csum = 0;
	}
	for (i = 0; i < priv->rx_ring_num; i++) {
		priv->rx_ring[i].bytes = 0;
		priv->rx_ring[i].packets = 0;
		priv->rx_ring[i].csum_ok = 0;
		priv->rx_ring[i].csum_none = 0;
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

	mlx4_en_stop_port(dev);
	netif_carrier_off(dev);

	mutex_unlock(&mdev->state_lock);
	return 0;
}

void mlx4_en_free_resources(struct mlx4_en_priv *priv)
{
	int i;

	for (i = 0; i < priv->tx_ring_num; i++) {
		if (priv->tx_ring[i].tx_info)
			mlx4_en_destroy_tx_ring(priv, &priv->tx_ring[i]);
		if (priv->tx_cq[i].buf)
			mlx4_en_destroy_cq(priv, &priv->tx_cq[i]);
	}

	for (i = 0; i < priv->rx_ring_num; i++) {
		if (priv->rx_ring[i].rx_info)
			mlx4_en_destroy_rx_ring(priv, &priv->rx_ring[i],
				priv->prof->rx_ring_size, priv->stride);
		if (priv->rx_cq[i].buf)
			mlx4_en_destroy_cq(priv, &priv->rx_cq[i]);
	}
}

int mlx4_en_alloc_resources(struct mlx4_en_priv *priv)
{
	struct mlx4_en_port_profile *prof = priv->prof;
	int i;
	int base_tx_qpn, err;

	err = mlx4_qp_reserve_range(priv->mdev->dev, priv->tx_ring_num, 256, &base_tx_qpn);
	if (err) {
		en_err(priv, "failed reserving range for TX rings\n");
		return err;
	}

	/* Create tx Rings */
	for (i = 0; i < priv->tx_ring_num; i++) {
		if (mlx4_en_create_cq(priv, &priv->tx_cq[i],
				      prof->tx_ring_size, i, TX))
			goto err;

		if (mlx4_en_create_tx_ring(priv, &priv->tx_ring[i], base_tx_qpn + i,
					   prof->tx_ring_size, TXBB_SIZE))
			goto err;
	}

	/* Create rx Rings */
	for (i = 0; i < priv->rx_ring_num; i++) {
		if (mlx4_en_create_cq(priv, &priv->rx_cq[i],
				      prof->rx_ring_size, i, RX))
			goto err;

		if (mlx4_en_create_rx_ring(priv, &priv->rx_ring[i],
					   prof->rx_ring_size, priv->stride))
			goto err;
	}

	return 0;

err:
	en_err(priv, "Failed to allocate NIC resources\n");
	mlx4_qp_release_range(priv->mdev->dev, base_tx_qpn, priv->tx_ring_num);
	return -ENOMEM;
}


void mlx4_en_destroy_netdev(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;

	en_dbg(DRV, priv, "Destroying netdev on port:%d\n", priv->port);

	/* Unregister device - this will close the port if it was up */
	if (priv->registered)
		unregister_netdev(dev);

	if (priv->allocated)
		mlx4_free_hwq_res(mdev->dev, &priv->res, MLX4_EN_PAGE_SIZE);

	cancel_delayed_work(&priv->stats_task);
	/* flush any pending task for this netdev */
	flush_workqueue(mdev->workqueue);

	/* Detach the netdev so tasks would not attempt to access it */
	mutex_lock(&mdev->state_lock);
	mdev->pndev[priv->port] = NULL;
	mutex_unlock(&mdev->state_lock);

	mlx4_en_free_resources(priv);
	free_netdev(dev);
}

static int mlx4_en_change_mtu(struct net_device *dev, int new_mtu)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err = 0;

	en_dbg(DRV, priv, "Change MTU called - current:%d new:%d\n",
		 dev->mtu, new_mtu);

	if ((new_mtu < MLX4_EN_MIN_MTU) || (new_mtu > priv->max_mtu)) {
		en_err(priv, "Bad MTU size:%d.\n", new_mtu);
		return -EPERM;
	}
	dev->mtu = new_mtu;

	if (netif_running(dev)) {
		mutex_lock(&mdev->state_lock);
		if (!mdev->device_up) {
			/* NIC is probably restarting - let watchdog task reset
			 * the port */
			en_dbg(DRV, priv, "Change MTU called with card down!?\n");
		} else {
			mlx4_en_stop_port(dev);
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

static int mlx4_en_set_features(struct net_device *netdev,
		netdev_features_t features)
{
	struct mlx4_en_priv *priv = netdev_priv(netdev);

	if (features & NETIF_F_LOOPBACK)
		priv->ctrl_flags |= cpu_to_be32(MLX4_WQE_CTRL_FORCE_LOOPBACK);
	else
		priv->ctrl_flags &=
			cpu_to_be32(~MLX4_WQE_CTRL_FORCE_LOOPBACK);

	return 0;

}

static const struct net_device_ops mlx4_netdev_ops = {
	.ndo_open		= mlx4_en_open,
	.ndo_stop		= mlx4_en_close,
	.ndo_start_xmit		= mlx4_en_xmit,
	.ndo_select_queue	= mlx4_en_select_queue,
	.ndo_get_stats		= mlx4_en_get_stats,
	.ndo_set_rx_mode	= mlx4_en_set_multicast,
	.ndo_set_mac_address	= mlx4_en_set_mac,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= mlx4_en_change_mtu,
	.ndo_tx_timeout		= mlx4_en_tx_timeout,
	.ndo_vlan_rx_add_vid	= mlx4_en_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= mlx4_en_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= mlx4_en_netpoll,
#endif
	.ndo_set_features	= mlx4_en_set_features,
};

int mlx4_en_init_netdev(struct mlx4_en_dev *mdev, int port,
			struct mlx4_en_port_profile *prof)
{
	struct net_device *dev;
	struct mlx4_en_priv *priv;
	int i;
	int err;

	dev = alloc_etherdev_mqs(sizeof(struct mlx4_en_priv),
	    prof->tx_ring_num, prof->rx_ring_num);
	if (dev == NULL) {
		mlx4_err(mdev, "Net device allocation failed\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(dev, &mdev->dev->pdev->dev);
	dev->dev_id =  port - 1;

	/*
	 * Initialize driver private data
	 */

	priv = netdev_priv(dev);
	memset(priv, 0, sizeof(struct mlx4_en_priv));
	priv->dev = dev;
	priv->mdev = mdev;
	priv->prof = prof;
	priv->port = port;
	priv->port_up = false;
	priv->flags = prof->flags;
	priv->ctrl_flags = cpu_to_be32(MLX4_WQE_CTRL_CQ_UPDATE |
			MLX4_WQE_CTRL_SOLICITED);
	priv->tx_ring_num = prof->tx_ring_num;
	priv->rx_ring_num = prof->rx_ring_num;
	priv->mac_index = -1;
	priv->msg_enable = MLX4_EN_MSG_LEVEL;
	spin_lock_init(&priv->stats_lock);
	INIT_WORK(&priv->mcast_task, mlx4_en_do_set_multicast);
	INIT_WORK(&priv->mac_task, mlx4_en_do_set_mac);
	INIT_WORK(&priv->watchdog_task, mlx4_en_restart);
	INIT_WORK(&priv->linkstate_task, mlx4_en_linkstate);
	INIT_DELAYED_WORK(&priv->stats_task, mlx4_en_do_get_stats);

	/* Query for default mac and max mtu */
	priv->max_mtu = mdev->dev->caps.eth_mtu_cap[priv->port];
	priv->mac = mdev->dev->caps.def_mac[priv->port];
	if (ILLEGAL_MAC(priv->mac)) {
		en_err(priv, "Port: %d, invalid mac burned: 0x%llx, quiting\n",
			 priv->port, priv->mac);
		err = -EINVAL;
		goto out;
	}

	priv->stride = roundup_pow_of_two(sizeof(struct mlx4_en_rx_desc) +
					  DS_SIZE * MLX4_EN_MAX_RX_FRAGS);
	err = mlx4_en_alloc_resources(priv);
	if (err)
		goto out;

	/* Allocate page for receive rings */
	err = mlx4_alloc_hwq_res(mdev->dev, &priv->res,
				MLX4_EN_PAGE_SIZE, MLX4_EN_PAGE_SIZE);
	if (err) {
		en_err(priv, "Failed to allocate page for rx qps\n");
		goto out;
	}
	priv->allocated = 1;

	/*
	 * Initialize netdev entry points
	 */
	dev->netdev_ops = &mlx4_netdev_ops;
	dev->watchdog_timeo = MLX4_EN_WATCHDOG_TIMEOUT;
	netif_set_real_num_tx_queues(dev, priv->tx_ring_num);
	netif_set_real_num_rx_queues(dev, priv->rx_ring_num);

	SET_ETHTOOL_OPS(dev, &mlx4_en_ethtool_ops);

	/* Set defualt MAC */
	dev->addr_len = ETH_ALEN;
	for (i = 0; i < ETH_ALEN; i++) {
		dev->dev_addr[ETH_ALEN - 1 - i] = (u8) (priv->mac >> (8 * i));
		dev->perm_addr[ETH_ALEN - 1 - i] = (u8) (priv->mac >> (8 * i));
	}

	/*
	 * Set driver features
	 */
	dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;
	if (mdev->LSO_support)
		dev->hw_features |= NETIF_F_TSO | NETIF_F_TSO6;

	dev->vlan_features = dev->hw_features;

	dev->hw_features |= NETIF_F_RXCSUM | NETIF_F_RXHASH;
	dev->features = dev->hw_features | NETIF_F_HIGHDMA |
			NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX |
			NETIF_F_HW_VLAN_FILTER;
	dev->hw_features |= NETIF_F_LOOPBACK;

	mdev->pndev[port] = dev;

	netif_carrier_off(dev);
	err = register_netdev(dev);
	if (err) {
		en_err(priv, "Netdev registration failed for port %d\n", port);
		goto out;
	}
	priv->registered = 1;

	en_warn(priv, "Using %d TX rings\n", prof->tx_ring_num);
	en_warn(priv, "Using %d RX rings\n", prof->rx_ring_num);

	/* Configure port */
	err = mlx4_SET_PORT_general(mdev->dev, priv->port,
				    MLX4_EN_MIN_MTU,
				    0, 0, 0, 0);
	if (err) {
		en_err(priv, "Failed setting port general configurations "
		       "for port %d, with error %d\n", priv->port, err);
		goto out;
	}

	/* Init port */
	en_warn(priv, "Initializing port\n");
	err = mlx4_INIT_PORT(mdev->dev, priv->port);
	if (err) {
		en_err(priv, "Failed Initializing port\n");
		goto out;
	}
	mlx4_en_set_default_moderation(priv);
	queue_delayed_work(mdev->workqueue, &priv->stats_task, STATS_DELAY);
	return 0;

out:
	mlx4_en_destroy_netdev(dev);
	return err;
}

