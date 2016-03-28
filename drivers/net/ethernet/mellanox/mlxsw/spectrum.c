/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum.c
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2015 Ido Schimmel <idosch@mellanox.com>
 * Copyright (c) 2015 Elad Raz <eladr@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <net/devlink.h>
#include <net/switchdev.h>
#include <generated/utsrelease.h>

#include "spectrum.h"
#include "core.h"
#include "reg.h"
#include "port.h"
#include "trap.h"
#include "txheader.h"

static const char mlxsw_sp_driver_name[] = "mlxsw_spectrum";
static const char mlxsw_sp_driver_version[] = "1.0";

/* tx_hdr_version
 * Tx header version.
 * Must be set to 1.
 */
MLXSW_ITEM32(tx, hdr, version, 0x00, 28, 4);

/* tx_hdr_ctl
 * Packet control type.
 * 0 - Ethernet control (e.g. EMADs, LACP)
 * 1 - Ethernet data
 */
MLXSW_ITEM32(tx, hdr, ctl, 0x00, 26, 2);

/* tx_hdr_proto
 * Packet protocol type. Must be set to 1 (Ethernet).
 */
MLXSW_ITEM32(tx, hdr, proto, 0x00, 21, 3);

/* tx_hdr_rx_is_router
 * Packet is sent from the router. Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, rx_is_router, 0x00, 19, 1);

/* tx_hdr_fid_valid
 * Indicates if the 'fid' field is valid and should be used for
 * forwarding lookup. Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, fid_valid, 0x00, 16, 1);

/* tx_hdr_swid
 * Switch partition ID. Must be set to 0.
 */
MLXSW_ITEM32(tx, hdr, swid, 0x00, 12, 3);

/* tx_hdr_control_tclass
 * Indicates if the packet should use the control TClass and not one
 * of the data TClasses.
 */
MLXSW_ITEM32(tx, hdr, control_tclass, 0x00, 6, 1);

/* tx_hdr_etclass
 * Egress TClass to be used on the egress device on the egress port.
 */
MLXSW_ITEM32(tx, hdr, etclass, 0x00, 0, 4);

/* tx_hdr_port_mid
 * Destination local port for unicast packets.
 * Destination multicast ID for multicast packets.
 *
 * Control packets are directed to a specific egress port, while data
 * packets are transmitted through the CPU port (0) into the switch partition,
 * where forwarding rules are applied.
 */
MLXSW_ITEM32(tx, hdr, port_mid, 0x04, 16, 16);

/* tx_hdr_fid
 * Forwarding ID used for L2 forwarding lookup. Valid only if 'fid_valid' is
 * set, otherwise calculated based on the packet's VID using VID to FID mapping.
 * Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, fid, 0x08, 0, 16);

/* tx_hdr_type
 * 0 - Data packets
 * 6 - Control packets
 */
MLXSW_ITEM32(tx, hdr, type, 0x0C, 0, 4);

static void mlxsw_sp_txhdr_construct(struct sk_buff *skb,
				     const struct mlxsw_tx_info *tx_info)
{
	char *txhdr = skb_push(skb, MLXSW_TXHDR_LEN);

	memset(txhdr, 0, MLXSW_TXHDR_LEN);

	mlxsw_tx_hdr_version_set(txhdr, MLXSW_TXHDR_VERSION_1);
	mlxsw_tx_hdr_ctl_set(txhdr, MLXSW_TXHDR_ETH_CTL);
	mlxsw_tx_hdr_proto_set(txhdr, MLXSW_TXHDR_PROTO_ETH);
	mlxsw_tx_hdr_swid_set(txhdr, 0);
	mlxsw_tx_hdr_control_tclass_set(txhdr, 1);
	mlxsw_tx_hdr_port_mid_set(txhdr, tx_info->local_port);
	mlxsw_tx_hdr_type_set(txhdr, MLXSW_TXHDR_TYPE_CONTROL);
}

static int mlxsw_sp_base_mac_get(struct mlxsw_sp *mlxsw_sp)
{
	char spad_pl[MLXSW_REG_SPAD_LEN];
	int err;

	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(spad), spad_pl);
	if (err)
		return err;
	mlxsw_reg_spad_base_mac_memcpy_from(spad_pl, mlxsw_sp->base_mac);
	return 0;
}

static int mlxsw_sp_port_admin_status_set(struct mlxsw_sp_port *mlxsw_sp_port,
					  bool is_up)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char paos_pl[MLXSW_REG_PAOS_LEN];

	mlxsw_reg_paos_pack(paos_pl, mlxsw_sp_port->local_port,
			    is_up ? MLXSW_PORT_ADMIN_STATUS_UP :
			    MLXSW_PORT_ADMIN_STATUS_DOWN);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(paos), paos_pl);
}

static int mlxsw_sp_port_oper_status_get(struct mlxsw_sp_port *mlxsw_sp_port,
					 bool *p_is_up)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char paos_pl[MLXSW_REG_PAOS_LEN];
	u8 oper_status;
	int err;

	mlxsw_reg_paos_pack(paos_pl, mlxsw_sp_port->local_port, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(paos), paos_pl);
	if (err)
		return err;
	oper_status = mlxsw_reg_paos_oper_status_get(paos_pl);
	*p_is_up = oper_status == MLXSW_PORT_ADMIN_STATUS_UP ? true : false;
	return 0;
}

static int mlxsw_sp_port_dev_addr_set(struct mlxsw_sp_port *mlxsw_sp_port,
				      unsigned char *addr)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char ppad_pl[MLXSW_REG_PPAD_LEN];

	mlxsw_reg_ppad_pack(ppad_pl, true, mlxsw_sp_port->local_port);
	mlxsw_reg_ppad_mac_memcpy_to(ppad_pl, addr);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ppad), ppad_pl);
}

static int mlxsw_sp_port_dev_addr_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	unsigned char *addr = mlxsw_sp_port->dev->dev_addr;

	ether_addr_copy(addr, mlxsw_sp->base_mac);
	addr[ETH_ALEN - 1] += mlxsw_sp_port->local_port;
	return mlxsw_sp_port_dev_addr_set(mlxsw_sp_port, addr);
}

static int mlxsw_sp_port_stp_state_set(struct mlxsw_sp_port *mlxsw_sp_port,
				       u16 vid, enum mlxsw_reg_spms_state state)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char *spms_pl;
	int err;

	spms_pl = kmalloc(MLXSW_REG_SPMS_LEN, GFP_KERNEL);
	if (!spms_pl)
		return -ENOMEM;
	mlxsw_reg_spms_pack(spms_pl, mlxsw_sp_port->local_port);
	mlxsw_reg_spms_vid_pack(spms_pl, vid, state);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spms), spms_pl);
	kfree(spms_pl);
	return err;
}

static int mlxsw_sp_port_mtu_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 mtu)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char pmtu_pl[MLXSW_REG_PMTU_LEN];
	int max_mtu;
	int err;

	mtu += MLXSW_TXHDR_LEN + ETH_HLEN;
	mlxsw_reg_pmtu_pack(pmtu_pl, mlxsw_sp_port->local_port, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(pmtu), pmtu_pl);
	if (err)
		return err;
	max_mtu = mlxsw_reg_pmtu_max_mtu_get(pmtu_pl);

	if (mtu > max_mtu)
		return -EINVAL;

	mlxsw_reg_pmtu_pack(pmtu_pl, mlxsw_sp_port->local_port, mtu);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pmtu), pmtu_pl);
}

static int mlxsw_sp_port_swid_set(struct mlxsw_sp_port *mlxsw_sp_port, u8 swid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char pspa_pl[MLXSW_REG_PSPA_LEN];

	mlxsw_reg_pspa_pack(pspa_pl, swid, mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pspa), pspa_pl);
}

static int mlxsw_sp_port_vp_mode_set(struct mlxsw_sp_port *mlxsw_sp_port,
				     bool enable)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char svpe_pl[MLXSW_REG_SVPE_LEN];

	mlxsw_reg_svpe_pack(svpe_pl, mlxsw_sp_port->local_port, enable);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(svpe), svpe_pl);
}

int mlxsw_sp_port_vid_to_fid_set(struct mlxsw_sp_port *mlxsw_sp_port,
				 enum mlxsw_reg_svfa_mt mt, bool valid, u16 fid,
				 u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char svfa_pl[MLXSW_REG_SVFA_LEN];

	mlxsw_reg_svfa_pack(svfa_pl, mlxsw_sp_port->local_port, mt, valid,
			    fid, vid);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(svfa), svfa_pl);
}

static int mlxsw_sp_port_vid_learning_set(struct mlxsw_sp_port *mlxsw_sp_port,
					  u16 vid, bool learn_enable)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char *spvmlr_pl;
	int err;

	spvmlr_pl = kmalloc(MLXSW_REG_SPVMLR_LEN, GFP_KERNEL);
	if (!spvmlr_pl)
		return -ENOMEM;
	mlxsw_reg_spvmlr_pack(spvmlr_pl, mlxsw_sp_port->local_port, vid, vid,
			      learn_enable);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spvmlr), spvmlr_pl);
	kfree(spvmlr_pl);
	return err;
}

static int
mlxsw_sp_port_system_port_mapping_set(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sspr_pl[MLXSW_REG_SSPR_LEN];

	mlxsw_reg_sspr_pack(sspr_pl, mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sspr), sspr_pl);
}

static int mlxsw_sp_port_module_info_get(struct mlxsw_sp *mlxsw_sp,
					 u8 local_port, u8 *p_module,
					 u8 *p_width)
{
	char pmlp_pl[MLXSW_REG_PMLP_LEN];
	int err;

	mlxsw_reg_pmlp_pack(pmlp_pl, local_port);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(pmlp), pmlp_pl);
	if (err)
		return err;
	*p_module = mlxsw_reg_pmlp_module_get(pmlp_pl, 0);
	*p_width = mlxsw_reg_pmlp_width_get(pmlp_pl);
	return 0;
}

static int mlxsw_sp_port_module_map(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				    u8 module, u8 width, u8 lane)
{
	char pmlp_pl[MLXSW_REG_PMLP_LEN];
	int i;

	mlxsw_reg_pmlp_pack(pmlp_pl, local_port);
	mlxsw_reg_pmlp_width_set(pmlp_pl, width);
	for (i = 0; i < width; i++) {
		mlxsw_reg_pmlp_module_set(pmlp_pl, i, module);
		mlxsw_reg_pmlp_tx_lane_set(pmlp_pl, i, lane + i);  /* Rx & Tx */
	}

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pmlp), pmlp_pl);
}

static int mlxsw_sp_port_module_unmap(struct mlxsw_sp *mlxsw_sp, u8 local_port)
{
	char pmlp_pl[MLXSW_REG_PMLP_LEN];

	mlxsw_reg_pmlp_pack(pmlp_pl, local_port);
	mlxsw_reg_pmlp_width_set(pmlp_pl, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pmlp), pmlp_pl);
}

static int mlxsw_sp_port_open(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err;

	err = mlxsw_sp_port_admin_status_set(mlxsw_sp_port, true);
	if (err)
		return err;
	netif_start_queue(dev);
	return 0;
}

static int mlxsw_sp_port_stop(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	netif_stop_queue(dev);
	return mlxsw_sp_port_admin_status_set(mlxsw_sp_port, false);
}

static netdev_tx_t mlxsw_sp_port_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_port_pcpu_stats *pcpu_stats;
	const struct mlxsw_tx_info tx_info = {
		.local_port = mlxsw_sp_port->local_port,
		.is_emad = false,
	};
	u64 len;
	int err;

	if (mlxsw_core_skb_transmit_busy(mlxsw_sp, &tx_info))
		return NETDEV_TX_BUSY;

	if (unlikely(skb_headroom(skb) < MLXSW_TXHDR_LEN)) {
		struct sk_buff *skb_orig = skb;

		skb = skb_realloc_headroom(skb, MLXSW_TXHDR_LEN);
		if (!skb) {
			this_cpu_inc(mlxsw_sp_port->pcpu_stats->tx_dropped);
			dev_kfree_skb_any(skb_orig);
			return NETDEV_TX_OK;
		}
	}

	if (eth_skb_pad(skb)) {
		this_cpu_inc(mlxsw_sp_port->pcpu_stats->tx_dropped);
		return NETDEV_TX_OK;
	}

	mlxsw_sp_txhdr_construct(skb, &tx_info);
	len = skb->len;
	/* Due to a race we might fail here because of a full queue. In that
	 * unlikely case we simply drop the packet.
	 */
	err = mlxsw_core_skb_transmit(mlxsw_sp, skb, &tx_info);

	if (!err) {
		pcpu_stats = this_cpu_ptr(mlxsw_sp_port->pcpu_stats);
		u64_stats_update_begin(&pcpu_stats->syncp);
		pcpu_stats->tx_packets++;
		pcpu_stats->tx_bytes += len;
		u64_stats_update_end(&pcpu_stats->syncp);
	} else {
		this_cpu_inc(mlxsw_sp_port->pcpu_stats->tx_dropped);
		dev_kfree_skb_any(skb);
	}
	return NETDEV_TX_OK;
}

static void mlxsw_sp_set_rx_mode(struct net_device *dev)
{
}

static int mlxsw_sp_port_set_mac_address(struct net_device *dev, void *p)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct sockaddr *addr = p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	err = mlxsw_sp_port_dev_addr_set(mlxsw_sp_port, addr->sa_data);
	if (err)
		return err;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	return 0;
}

static int mlxsw_sp_port_change_mtu(struct net_device *dev, int mtu)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err;

	err = mlxsw_sp_port_mtu_set(mlxsw_sp_port, mtu);
	if (err)
		return err;
	dev->mtu = mtu;
	return 0;
}

static struct rtnl_link_stats64 *
mlxsw_sp_port_get_stats64(struct net_device *dev,
			  struct rtnl_link_stats64 *stats)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp_port_pcpu_stats *p;
	u64 rx_packets, rx_bytes, tx_packets, tx_bytes;
	u32 tx_dropped = 0;
	unsigned int start;
	int i;

	for_each_possible_cpu(i) {
		p = per_cpu_ptr(mlxsw_sp_port->pcpu_stats, i);
		do {
			start = u64_stats_fetch_begin_irq(&p->syncp);
			rx_packets	= p->rx_packets;
			rx_bytes	= p->rx_bytes;
			tx_packets	= p->tx_packets;
			tx_bytes	= p->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&p->syncp, start));

		stats->rx_packets	+= rx_packets;
		stats->rx_bytes		+= rx_bytes;
		stats->tx_packets	+= tx_packets;
		stats->tx_bytes		+= tx_bytes;
		/* tx_dropped is u32, updated without syncp protection. */
		tx_dropped	+= p->tx_dropped;
	}
	stats->tx_dropped	= tx_dropped;
	return stats;
}

int mlxsw_sp_port_vlan_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid_begin,
			   u16 vid_end, bool is_member, bool untagged)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char *spvm_pl;
	int err;

	spvm_pl = kmalloc(MLXSW_REG_SPVM_LEN, GFP_KERNEL);
	if (!spvm_pl)
		return -ENOMEM;

	mlxsw_reg_spvm_pack(spvm_pl, mlxsw_sp_port->local_port,	vid_begin,
			    vid_end, is_member, untagged);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spvm), spvm_pl);
	kfree(spvm_pl);
	return err;
}

static int mlxsw_sp_port_vp_mode_trans(struct mlxsw_sp_port *mlxsw_sp_port)
{
	enum mlxsw_reg_svfa_mt mt = MLXSW_REG_SVFA_MT_PORT_VID_TO_FID;
	u16 vid, last_visited_vid;
	int err;

	for_each_set_bit(vid, mlxsw_sp_port->active_vlans, VLAN_N_VID) {
		err = mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_port, mt, true, vid,
						   vid);
		if (err) {
			last_visited_vid = vid;
			goto err_port_vid_to_fid_set;
		}
	}

	err = mlxsw_sp_port_vp_mode_set(mlxsw_sp_port, true);
	if (err) {
		last_visited_vid = VLAN_N_VID;
		goto err_port_vid_to_fid_set;
	}

	return 0;

err_port_vid_to_fid_set:
	for_each_set_bit(vid, mlxsw_sp_port->active_vlans, last_visited_vid)
		mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_port, mt, false, vid,
					     vid);
	return err;
}

static int mlxsw_sp_port_vlan_mode_trans(struct mlxsw_sp_port *mlxsw_sp_port)
{
	enum mlxsw_reg_svfa_mt mt = MLXSW_REG_SVFA_MT_PORT_VID_TO_FID;
	u16 vid;
	int err;

	err = mlxsw_sp_port_vp_mode_set(mlxsw_sp_port, false);
	if (err)
		return err;

	for_each_set_bit(vid, mlxsw_sp_port->active_vlans, VLAN_N_VID) {
		err = mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_port, mt, false,
						   vid, vid);
		if (err)
			return err;
	}

	return 0;
}

static struct mlxsw_sp_vfid *
mlxsw_sp_vfid_find(const struct mlxsw_sp *mlxsw_sp, u16 vid)
{
	struct mlxsw_sp_vfid *vfid;

	list_for_each_entry(vfid, &mlxsw_sp->port_vfids.list, list) {
		if (vfid->vid == vid)
			return vfid;
	}

	return NULL;
}

static u16 mlxsw_sp_avail_vfid_get(const struct mlxsw_sp *mlxsw_sp)
{
	return find_first_zero_bit(mlxsw_sp->port_vfids.mapped,
				   MLXSW_SP_VFID_PORT_MAX);
}

static int __mlxsw_sp_vfid_create(struct mlxsw_sp *mlxsw_sp, u16 vfid)
{
	u16 fid = mlxsw_sp_vfid_to_fid(vfid);
	char sfmr_pl[MLXSW_REG_SFMR_LEN];

	mlxsw_reg_sfmr_pack(sfmr_pl, MLXSW_REG_SFMR_OP_CREATE_FID, fid, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfmr), sfmr_pl);
}

static void __mlxsw_sp_vfid_destroy(struct mlxsw_sp *mlxsw_sp, u16 vfid)
{
	u16 fid = mlxsw_sp_vfid_to_fid(vfid);
	char sfmr_pl[MLXSW_REG_SFMR_LEN];

	mlxsw_reg_sfmr_pack(sfmr_pl, MLXSW_REG_SFMR_OP_DESTROY_FID, fid, 0);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfmr), sfmr_pl);
}

static struct mlxsw_sp_vfid *mlxsw_sp_vfid_create(struct mlxsw_sp *mlxsw_sp,
						  u16 vid)
{
	struct device *dev = mlxsw_sp->bus_info->dev;
	struct mlxsw_sp_vfid *vfid;
	u16 n_vfid;
	int err;

	n_vfid = mlxsw_sp_avail_vfid_get(mlxsw_sp);
	if (n_vfid == MLXSW_SP_VFID_PORT_MAX) {
		dev_err(dev, "No available vFIDs\n");
		return ERR_PTR(-ERANGE);
	}

	err = __mlxsw_sp_vfid_create(mlxsw_sp, n_vfid);
	if (err) {
		dev_err(dev, "Failed to create vFID=%d\n", n_vfid);
		return ERR_PTR(err);
	}

	vfid = kzalloc(sizeof(*vfid), GFP_KERNEL);
	if (!vfid)
		goto err_allocate_vfid;

	vfid->vfid = n_vfid;
	vfid->vid = vid;

	list_add(&vfid->list, &mlxsw_sp->port_vfids.list);
	set_bit(n_vfid, mlxsw_sp->port_vfids.mapped);

	return vfid;

err_allocate_vfid:
	__mlxsw_sp_vfid_destroy(mlxsw_sp, n_vfid);
	return ERR_PTR(-ENOMEM);
}

static void mlxsw_sp_vfid_destroy(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_vfid *vfid)
{
	clear_bit(vfid->vfid, mlxsw_sp->port_vfids.mapped);
	list_del(&vfid->list);

	__mlxsw_sp_vfid_destroy(mlxsw_sp, vfid->vfid);

	kfree(vfid);
}

static struct mlxsw_sp_port *
mlxsw_sp_port_vport_create(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct mlxsw_sp_vfid *vfid)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;

	mlxsw_sp_vport = kzalloc(sizeof(*mlxsw_sp_vport), GFP_KERNEL);
	if (!mlxsw_sp_vport)
		return NULL;

	/* dev will be set correctly after the VLAN device is linked
	 * with the real device. In case of bridge SELF invocation, dev
	 * will remain as is.
	 */
	mlxsw_sp_vport->dev = mlxsw_sp_port->dev;
	mlxsw_sp_vport->mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	mlxsw_sp_vport->local_port = mlxsw_sp_port->local_port;
	mlxsw_sp_vport->stp_state = BR_STATE_FORWARDING;
	mlxsw_sp_vport->lagged = mlxsw_sp_port->lagged;
	mlxsw_sp_vport->lag_id = mlxsw_sp_port->lag_id;
	mlxsw_sp_vport->vport.vfid = vfid;
	mlxsw_sp_vport->vport.vid = vfid->vid;

	list_add(&mlxsw_sp_vport->vport.list, &mlxsw_sp_port->vports_list);

	return mlxsw_sp_vport;
}

static void mlxsw_sp_port_vport_destroy(struct mlxsw_sp_port *mlxsw_sp_vport)
{
	list_del(&mlxsw_sp_vport->vport.list);
	kfree(mlxsw_sp_vport);
}

int mlxsw_sp_port_add_vid(struct net_device *dev, __be16 __always_unused proto,
			  u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_port *mlxsw_sp_vport;
	struct mlxsw_sp_vfid *vfid;
	int err;

	/* VLAN 0 is added to HW filter when device goes up, but it is
	 * reserved in our case, so simply return.
	 */
	if (!vid)
		return 0;

	if (mlxsw_sp_port_vport_find(mlxsw_sp_port, vid)) {
		netdev_warn(dev, "VID=%d already configured\n", vid);
		return 0;
	}

	vfid = mlxsw_sp_vfid_find(mlxsw_sp, vid);
	if (!vfid) {
		vfid = mlxsw_sp_vfid_create(mlxsw_sp, vid);
		if (IS_ERR(vfid)) {
			netdev_err(dev, "Failed to create vFID for VID=%d\n",
				   vid);
			return PTR_ERR(vfid);
		}
	}

	mlxsw_sp_vport = mlxsw_sp_port_vport_create(mlxsw_sp_port, vfid);
	if (!mlxsw_sp_vport) {
		netdev_err(dev, "Failed to create vPort for VID=%d\n", vid);
		err = -ENOMEM;
		goto err_port_vport_create;
	}

	if (!vfid->nr_vports) {
		err = mlxsw_sp_vport_flood_set(mlxsw_sp_vport, vfid->vfid,
					       true, false);
		if (err) {
			netdev_err(dev, "Failed to setup flooding for vFID=%d\n",
				   vfid->vfid);
			goto err_vport_flood_set;
		}
	}

	/* When adding the first VLAN interface on a bridged port we need to
	 * transition all the active 802.1Q bridge VLANs to use explicit
	 * {Port, VID} to FID mappings and set the port's mode to Virtual mode.
	 */
	if (list_is_singular(&mlxsw_sp_port->vports_list)) {
		err = mlxsw_sp_port_vp_mode_trans(mlxsw_sp_port);
		if (err) {
			netdev_err(dev, "Failed to set to Virtual mode\n");
			goto err_port_vp_mode_trans;
		}
	}

	err = mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_vport,
					   MLXSW_REG_SVFA_MT_PORT_VID_TO_FID,
					   true,
					   mlxsw_sp_vfid_to_fid(vfid->vfid),
					   vid);
	if (err) {
		netdev_err(dev, "Failed to map {Port, VID=%d} to vFID=%d\n",
			   vid, vfid->vfid);
		goto err_port_vid_to_fid_set;
	}

	err = mlxsw_sp_port_vid_learning_set(mlxsw_sp_vport, vid, false);
	if (err) {
		netdev_err(dev, "Failed to disable learning for VID=%d\n", vid);
		goto err_port_vid_learning_set;
	}

	err = mlxsw_sp_port_vlan_set(mlxsw_sp_vport, vid, vid, true, false);
	if (err) {
		netdev_err(dev, "Failed to set VLAN membership for VID=%d\n",
			   vid);
		goto err_port_add_vid;
	}

	err = mlxsw_sp_port_stp_state_set(mlxsw_sp_vport, vid,
					  MLXSW_REG_SPMS_STATE_FORWARDING);
	if (err) {
		netdev_err(dev, "Failed to set STP state for VID=%d\n", vid);
		goto err_port_stp_state_set;
	}

	vfid->nr_vports++;

	return 0;

err_port_stp_state_set:
	mlxsw_sp_port_vlan_set(mlxsw_sp_vport, vid, vid, false, false);
err_port_add_vid:
	mlxsw_sp_port_vid_learning_set(mlxsw_sp_vport, vid, true);
err_port_vid_learning_set:
	mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_vport,
				     MLXSW_REG_SVFA_MT_PORT_VID_TO_FID, false,
				     mlxsw_sp_vfid_to_fid(vfid->vfid), vid);
err_port_vid_to_fid_set:
	if (list_is_singular(&mlxsw_sp_port->vports_list))
		mlxsw_sp_port_vlan_mode_trans(mlxsw_sp_port);
err_port_vp_mode_trans:
	if (!vfid->nr_vports)
		mlxsw_sp_vport_flood_set(mlxsw_sp_vport, vfid->vfid, false,
					 false);
err_vport_flood_set:
	mlxsw_sp_port_vport_destroy(mlxsw_sp_vport);
err_port_vport_create:
	if (!vfid->nr_vports)
		mlxsw_sp_vfid_destroy(mlxsw_sp, vfid);
	return err;
}

int mlxsw_sp_port_kill_vid(struct net_device *dev,
			   __be16 __always_unused proto, u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp_port *mlxsw_sp_vport;
	struct mlxsw_sp_vfid *vfid;
	int err;

	/* VLAN 0 is removed from HW filter when device goes down, but
	 * it is reserved in our case, so simply return.
	 */
	if (!vid)
		return 0;

	mlxsw_sp_vport = mlxsw_sp_port_vport_find(mlxsw_sp_port, vid);
	if (!mlxsw_sp_vport) {
		netdev_warn(dev, "VID=%d does not exist\n", vid);
		return 0;
	}

	vfid = mlxsw_sp_vport->vport.vfid;

	err = mlxsw_sp_port_stp_state_set(mlxsw_sp_vport, vid,
					  MLXSW_REG_SPMS_STATE_DISCARDING);
	if (err) {
		netdev_err(dev, "Failed to set STP state for VID=%d\n", vid);
		return err;
	}

	err = mlxsw_sp_port_vlan_set(mlxsw_sp_vport, vid, vid, false, false);
	if (err) {
		netdev_err(dev, "Failed to set VLAN membership for VID=%d\n",
			   vid);
		return err;
	}

	err = mlxsw_sp_port_vid_learning_set(mlxsw_sp_vport, vid, true);
	if (err) {
		netdev_err(dev, "Failed to enable learning for VID=%d\n", vid);
		return err;
	}

	err = mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_vport,
					   MLXSW_REG_SVFA_MT_PORT_VID_TO_FID,
					   false,
					   mlxsw_sp_vfid_to_fid(vfid->vfid),
					   vid);
	if (err) {
		netdev_err(dev, "Failed to invalidate {Port, VID=%d} to vFID=%d mapping\n",
			   vid, vfid->vfid);
		return err;
	}

	/* When removing the last VLAN interface on a bridged port we need to
	 * transition all active 802.1Q bridge VLANs to use VID to FID
	 * mappings and set port's mode to VLAN mode.
	 */
	if (list_is_singular(&mlxsw_sp_port->vports_list)) {
		err = mlxsw_sp_port_vlan_mode_trans(mlxsw_sp_port);
		if (err) {
			netdev_err(dev, "Failed to set to VLAN mode\n");
			return err;
		}
	}

	vfid->nr_vports--;
	mlxsw_sp_port_vport_destroy(mlxsw_sp_vport);

	/* Destroy the vFID if no vPorts are assigned to it anymore. */
	if (!vfid->nr_vports)
		mlxsw_sp_vfid_destroy(mlxsw_sp_port->mlxsw_sp, vfid);

	return 0;
}

static const struct net_device_ops mlxsw_sp_port_netdev_ops = {
	.ndo_open		= mlxsw_sp_port_open,
	.ndo_stop		= mlxsw_sp_port_stop,
	.ndo_start_xmit		= mlxsw_sp_port_xmit,
	.ndo_set_rx_mode	= mlxsw_sp_set_rx_mode,
	.ndo_set_mac_address	= mlxsw_sp_port_set_mac_address,
	.ndo_change_mtu		= mlxsw_sp_port_change_mtu,
	.ndo_get_stats64	= mlxsw_sp_port_get_stats64,
	.ndo_vlan_rx_add_vid	= mlxsw_sp_port_add_vid,
	.ndo_vlan_rx_kill_vid	= mlxsw_sp_port_kill_vid,
	.ndo_fdb_add		= switchdev_port_fdb_add,
	.ndo_fdb_del		= switchdev_port_fdb_del,
	.ndo_fdb_dump		= switchdev_port_fdb_dump,
	.ndo_bridge_setlink	= switchdev_port_bridge_setlink,
	.ndo_bridge_getlink	= switchdev_port_bridge_getlink,
	.ndo_bridge_dellink	= switchdev_port_bridge_dellink,
};

static void mlxsw_sp_port_get_drvinfo(struct net_device *dev,
				      struct ethtool_drvinfo *drvinfo)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	strlcpy(drvinfo->driver, mlxsw_sp_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, mlxsw_sp_driver_version,
		sizeof(drvinfo->version));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%d.%d.%d",
		 mlxsw_sp->bus_info->fw_rev.major,
		 mlxsw_sp->bus_info->fw_rev.minor,
		 mlxsw_sp->bus_info->fw_rev.subminor);
	strlcpy(drvinfo->bus_info, mlxsw_sp->bus_info->device_name,
		sizeof(drvinfo->bus_info));
}

struct mlxsw_sp_port_hw_stats {
	char str[ETH_GSTRING_LEN];
	u64 (*getter)(char *payload);
};

static const struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_stats[] = {
	{
		.str = "a_frames_transmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_frames_transmitted_ok_get,
	},
	{
		.str = "a_frames_received_ok",
		.getter = mlxsw_reg_ppcnt_a_frames_received_ok_get,
	},
	{
		.str = "a_frame_check_sequence_errors",
		.getter = mlxsw_reg_ppcnt_a_frame_check_sequence_errors_get,
	},
	{
		.str = "a_alignment_errors",
		.getter = mlxsw_reg_ppcnt_a_alignment_errors_get,
	},
	{
		.str = "a_octets_transmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_octets_transmitted_ok_get,
	},
	{
		.str = "a_octets_received_ok",
		.getter = mlxsw_reg_ppcnt_a_octets_received_ok_get,
	},
	{
		.str = "a_multicast_frames_xmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_multicast_frames_xmitted_ok_get,
	},
	{
		.str = "a_broadcast_frames_xmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_broadcast_frames_xmitted_ok_get,
	},
	{
		.str = "a_multicast_frames_received_ok",
		.getter = mlxsw_reg_ppcnt_a_multicast_frames_received_ok_get,
	},
	{
		.str = "a_broadcast_frames_received_ok",
		.getter = mlxsw_reg_ppcnt_a_broadcast_frames_received_ok_get,
	},
	{
		.str = "a_in_range_length_errors",
		.getter = mlxsw_reg_ppcnt_a_in_range_length_errors_get,
	},
	{
		.str = "a_out_of_range_length_field",
		.getter = mlxsw_reg_ppcnt_a_out_of_range_length_field_get,
	},
	{
		.str = "a_frame_too_long_errors",
		.getter = mlxsw_reg_ppcnt_a_frame_too_long_errors_get,
	},
	{
		.str = "a_symbol_error_during_carrier",
		.getter = mlxsw_reg_ppcnt_a_symbol_error_during_carrier_get,
	},
	{
		.str = "a_mac_control_frames_transmitted",
		.getter = mlxsw_reg_ppcnt_a_mac_control_frames_transmitted_get,
	},
	{
		.str = "a_mac_control_frames_received",
		.getter = mlxsw_reg_ppcnt_a_mac_control_frames_received_get,
	},
	{
		.str = "a_unsupported_opcodes_received",
		.getter = mlxsw_reg_ppcnt_a_unsupported_opcodes_received_get,
	},
	{
		.str = "a_pause_mac_ctrl_frames_received",
		.getter = mlxsw_reg_ppcnt_a_pause_mac_ctrl_frames_received_get,
	},
	{
		.str = "a_pause_mac_ctrl_frames_xmitted",
		.getter = mlxsw_reg_ppcnt_a_pause_mac_ctrl_frames_transmitted_get,
	},
};

#define MLXSW_SP_PORT_HW_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_hw_stats)

static void mlxsw_sp_port_get_strings(struct net_device *dev,
				      u32 stringset, u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < MLXSW_SP_PORT_HW_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int mlxsw_sp_port_set_phys_id(struct net_device *dev,
				     enum ethtool_phys_id_state state)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char mlcr_pl[MLXSW_REG_MLCR_LEN];
	bool active;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		active = true;
		break;
	case ETHTOOL_ID_INACTIVE:
		active = false;
		break;
	default:
		return -EOPNOTSUPP;
	}

	mlxsw_reg_mlcr_pack(mlcr_pl, mlxsw_sp_port->local_port, active);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mlcr), mlcr_pl);
}

static void mlxsw_sp_port_get_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];
	int i;
	int err;

	mlxsw_reg_ppcnt_pack(ppcnt_pl, mlxsw_sp_port->local_port);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ppcnt), ppcnt_pl);
	for (i = 0; i < MLXSW_SP_PORT_HW_STATS_LEN; i++)
		data[i] = !err ? mlxsw_sp_port_hw_stats[i].getter(ppcnt_pl) : 0;
}

static int mlxsw_sp_port_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return MLXSW_SP_PORT_HW_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

struct mlxsw_sp_port_link_mode {
	u32 mask;
	u32 supported;
	u32 advertised;
	u32 speed;
};

static const struct mlxsw_sp_port_link_mode mlxsw_sp_port_link_mode[] = {
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100BASE_T,
		.supported	= SUPPORTED_100baseT_Full,
		.advertised	= ADVERTISED_100baseT_Full,
		.speed		= 100,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100BASE_TX,
		.speed		= 100,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_SGMII |
				  MLXSW_REG_PTYS_ETH_SPEED_1000BASE_KX,
		.supported	= SUPPORTED_1000baseKX_Full,
		.advertised	= ADVERTISED_1000baseKX_Full,
		.speed		= 1000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_10GBASE_T,
		.supported	= SUPPORTED_10000baseT_Full,
		.advertised	= ADVERTISED_10000baseT_Full,
		.speed		= 10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CX4 |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4,
		.supported	= SUPPORTED_10000baseKX4_Full,
		.advertised	= ADVERTISED_10000baseKX4_Full,
		.speed		= 10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_SR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_ER_LR,
		.supported	= SUPPORTED_10000baseKR_Full,
		.advertised	= ADVERTISED_10000baseKR_Full,
		.speed		= 10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_20GBASE_KR2,
		.supported	= SUPPORTED_20000baseKR2_Full,
		.advertised	= ADVERTISED_20000baseKR2_Full,
		.speed		= 20000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4,
		.supported	= SUPPORTED_40000baseCR4_Full,
		.advertised	= ADVERTISED_40000baseCR4_Full,
		.speed		= 40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4,
		.supported	= SUPPORTED_40000baseKR4_Full,
		.advertised	= ADVERTISED_40000baseKR4_Full,
		.speed		= 40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_SR4,
		.supported	= SUPPORTED_40000baseSR4_Full,
		.advertised	= ADVERTISED_40000baseSR4_Full,
		.speed		= 40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_LR4_ER4,
		.supported	= SUPPORTED_40000baseLR4_Full,
		.advertised	= ADVERTISED_40000baseLR4_Full,
		.speed		= 40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_25GBASE_CR |
				  MLXSW_REG_PTYS_ETH_SPEED_25GBASE_KR |
				  MLXSW_REG_PTYS_ETH_SPEED_25GBASE_SR,
		.speed		= 25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_50GBASE_KR4 |
				  MLXSW_REG_PTYS_ETH_SPEED_50GBASE_CR2 |
				  MLXSW_REG_PTYS_ETH_SPEED_50GBASE_KR2,
		.speed		= 50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_56GBASE_R4,
		.supported	= SUPPORTED_56000baseKR4_Full,
		.advertised	= ADVERTISED_56000baseKR4_Full,
		.speed		= 56000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_CR4 |
				  MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4 |
				  MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4 |
				  MLXSW_REG_PTYS_ETH_SPEED_100GBASE_LR4_ER4,
		.speed		= 100000,
	},
};

#define MLXSW_SP_PORT_LINK_MODE_LEN ARRAY_SIZE(mlxsw_sp_port_link_mode)

static u32 mlxsw_sp_from_ptys_supported_port(u32 ptys_eth_proto)
{
	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CR |
			      MLXSW_REG_PTYS_ETH_SPEED_10GBASE_SR |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_SGMII))
		return SUPPORTED_FIBRE;

	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR |
			      MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4 |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_1000BASE_KX))
		return SUPPORTED_Backplane;
	return 0;
}

static u32 mlxsw_sp_from_ptys_supported_link(u32 ptys_eth_proto)
{
	u32 modes = 0;
	int i;

	for (i = 0; i < MLXSW_SP_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp_port_link_mode[i].mask)
			modes |= mlxsw_sp_port_link_mode[i].supported;
	}
	return modes;
}

static u32 mlxsw_sp_from_ptys_advert_link(u32 ptys_eth_proto)
{
	u32 modes = 0;
	int i;

	for (i = 0; i < MLXSW_SP_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp_port_link_mode[i].mask)
			modes |= mlxsw_sp_port_link_mode[i].advertised;
	}
	return modes;
}

static void mlxsw_sp_from_ptys_speed_duplex(bool carrier_ok, u32 ptys_eth_proto,
					    struct ethtool_cmd *cmd)
{
	u32 speed = SPEED_UNKNOWN;
	u8 duplex = DUPLEX_UNKNOWN;
	int i;

	if (!carrier_ok)
		goto out;

	for (i = 0; i < MLXSW_SP_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp_port_link_mode[i].mask) {
			speed = mlxsw_sp_port_link_mode[i].speed;
			duplex = DUPLEX_FULL;
			break;
		}
	}
out:
	ethtool_cmd_speed_set(cmd, speed);
	cmd->duplex = duplex;
}

static u8 mlxsw_sp_port_connector_port(u32 ptys_eth_proto)
{
	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_SR |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_SGMII))
		return PORT_FIBRE;

	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CR |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_CR4))
		return PORT_DA;

	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR |
			      MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4 |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4))
		return PORT_NONE;

	return PORT_OTHER;
}

static int mlxsw_sp_port_get_settings(struct net_device *dev,
				      struct ethtool_cmd *cmd)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u32 eth_proto_cap;
	u32 eth_proto_admin;
	u32 eth_proto_oper;
	int err;

	mlxsw_reg_ptys_pack(ptys_pl, mlxsw_sp_port->local_port, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err) {
		netdev_err(dev, "Failed to get proto");
		return err;
	}
	mlxsw_reg_ptys_unpack(ptys_pl, &eth_proto_cap,
			      &eth_proto_admin, &eth_proto_oper);

	cmd->supported = mlxsw_sp_from_ptys_supported_port(eth_proto_cap) |
			 mlxsw_sp_from_ptys_supported_link(eth_proto_cap) |
			 SUPPORTED_Pause | SUPPORTED_Asym_Pause;
	cmd->advertising = mlxsw_sp_from_ptys_advert_link(eth_proto_admin);
	mlxsw_sp_from_ptys_speed_duplex(netif_carrier_ok(dev),
					eth_proto_oper, cmd);

	eth_proto_oper = eth_proto_oper ? eth_proto_oper : eth_proto_cap;
	cmd->port = mlxsw_sp_port_connector_port(eth_proto_oper);
	cmd->lp_advertising = mlxsw_sp_from_ptys_advert_link(eth_proto_oper);

	cmd->transceiver = XCVR_INTERNAL;
	return 0;
}

static u32 mlxsw_sp_to_ptys_advert_link(u32 advertising)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP_PORT_LINK_MODE_LEN; i++) {
		if (advertising & mlxsw_sp_port_link_mode[i].advertised)
			ptys_proto |= mlxsw_sp_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static u32 mlxsw_sp_to_ptys_speed(u32 speed)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP_PORT_LINK_MODE_LEN; i++) {
		if (speed == mlxsw_sp_port_link_mode[i].speed)
			ptys_proto |= mlxsw_sp_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static u32 mlxsw_sp_to_ptys_upper_speed(u32 upper_speed)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP_PORT_LINK_MODE_LEN; i++) {
		if (mlxsw_sp_port_link_mode[i].speed <= upper_speed)
			ptys_proto |= mlxsw_sp_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static int mlxsw_sp_port_set_settings(struct net_device *dev,
				      struct ethtool_cmd *cmd)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u32 speed;
	u32 eth_proto_new;
	u32 eth_proto_cap;
	u32 eth_proto_admin;
	bool is_up;
	int err;

	speed = ethtool_cmd_speed(cmd);

	eth_proto_new = cmd->autoneg == AUTONEG_ENABLE ?
		mlxsw_sp_to_ptys_advert_link(cmd->advertising) :
		mlxsw_sp_to_ptys_speed(speed);

	mlxsw_reg_ptys_pack(ptys_pl, mlxsw_sp_port->local_port, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err) {
		netdev_err(dev, "Failed to get proto");
		return err;
	}
	mlxsw_reg_ptys_unpack(ptys_pl, &eth_proto_cap, &eth_proto_admin, NULL);

	eth_proto_new = eth_proto_new & eth_proto_cap;
	if (!eth_proto_new) {
		netdev_err(dev, "Not supported proto admin requested");
		return -EINVAL;
	}
	if (eth_proto_new == eth_proto_admin)
		return 0;

	mlxsw_reg_ptys_pack(ptys_pl, mlxsw_sp_port->local_port, eth_proto_new);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err) {
		netdev_err(dev, "Failed to set proto admin");
		return err;
	}

	err = mlxsw_sp_port_oper_status_get(mlxsw_sp_port, &is_up);
	if (err) {
		netdev_err(dev, "Failed to get oper status");
		return err;
	}
	if (!is_up)
		return 0;

	err = mlxsw_sp_port_admin_status_set(mlxsw_sp_port, false);
	if (err) {
		netdev_err(dev, "Failed to set admin status");
		return err;
	}

	err = mlxsw_sp_port_admin_status_set(mlxsw_sp_port, true);
	if (err) {
		netdev_err(dev, "Failed to set admin status");
		return err;
	}

	return 0;
}

static const struct ethtool_ops mlxsw_sp_port_ethtool_ops = {
	.get_drvinfo		= mlxsw_sp_port_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_strings		= mlxsw_sp_port_get_strings,
	.set_phys_id		= mlxsw_sp_port_set_phys_id,
	.get_ethtool_stats	= mlxsw_sp_port_get_stats,
	.get_sset_count		= mlxsw_sp_port_get_sset_count,
	.get_settings		= mlxsw_sp_port_get_settings,
	.set_settings		= mlxsw_sp_port_set_settings,
};

static int
mlxsw_sp_port_speed_by_width_set(struct mlxsw_sp_port *mlxsw_sp_port, u8 width)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u32 upper_speed = MLXSW_SP_PORT_BASE_SPEED * width;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u32 eth_proto_admin;

	eth_proto_admin = mlxsw_sp_to_ptys_upper_speed(upper_speed);
	mlxsw_reg_ptys_pack(ptys_pl, mlxsw_sp_port->local_port,
			    eth_proto_admin);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
}

static int __mlxsw_sp_port_create(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				  bool split, u8 module, u8 width)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct devlink_port *devlink_port;
	struct net_device *dev;
	size_t bytes;
	int err;

	dev = alloc_etherdev(sizeof(struct mlxsw_sp_port));
	if (!dev)
		return -ENOMEM;
	mlxsw_sp_port = netdev_priv(dev);
	mlxsw_sp_port->dev = dev;
	mlxsw_sp_port->mlxsw_sp = mlxsw_sp;
	mlxsw_sp_port->local_port = local_port;
	mlxsw_sp_port->split = split;
	bytes = DIV_ROUND_UP(VLAN_N_VID, BITS_PER_BYTE);
	mlxsw_sp_port->active_vlans = kzalloc(bytes, GFP_KERNEL);
	if (!mlxsw_sp_port->active_vlans) {
		err = -ENOMEM;
		goto err_port_active_vlans_alloc;
	}
	mlxsw_sp_port->untagged_vlans = kzalloc(bytes, GFP_KERNEL);
	if (!mlxsw_sp_port->untagged_vlans) {
		err = -ENOMEM;
		goto err_port_untagged_vlans_alloc;
	}
	INIT_LIST_HEAD(&mlxsw_sp_port->vports_list);

	mlxsw_sp_port->pcpu_stats =
		netdev_alloc_pcpu_stats(struct mlxsw_sp_port_pcpu_stats);
	if (!mlxsw_sp_port->pcpu_stats) {
		err = -ENOMEM;
		goto err_alloc_stats;
	}

	dev->netdev_ops = &mlxsw_sp_port_netdev_ops;
	dev->ethtool_ops = &mlxsw_sp_port_ethtool_ops;

	err = mlxsw_sp_port_dev_addr_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Unable to init port mac address\n",
			mlxsw_sp_port->local_port);
		goto err_dev_addr_init;
	}

	netif_carrier_off(dev);

	dev->features |= NETIF_F_NETNS_LOCAL | NETIF_F_LLTX | NETIF_F_SG |
			 NETIF_F_HW_VLAN_CTAG_FILTER;

	/* Each packet needs to have a Tx header (metadata) on top all other
	 * headers.
	 */
	dev->hard_header_len += MLXSW_TXHDR_LEN;

	devlink_port = &mlxsw_sp_port->devlink_port;
	if (mlxsw_sp_port->split)
		devlink_port_split_set(devlink_port, module);
	err = devlink_port_register(devlink, devlink_port, local_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to register devlink port\n",
			mlxsw_sp_port->local_port);
		goto err_devlink_port_register;
	}

	err = mlxsw_sp_port_system_port_mapping_set(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to set system port mapping\n",
			mlxsw_sp_port->local_port);
		goto err_port_system_port_mapping_set;
	}

	err = mlxsw_sp_port_swid_set(mlxsw_sp_port, 0);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to set SWID\n",
			mlxsw_sp_port->local_port);
		goto err_port_swid_set;
	}

	err = mlxsw_sp_port_speed_by_width_set(mlxsw_sp_port, width);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to enable speeds\n",
			mlxsw_sp_port->local_port);
		goto err_port_speed_by_width_set;
	}

	err = mlxsw_sp_port_mtu_set(mlxsw_sp_port, ETH_DATA_LEN);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to set MTU\n",
			mlxsw_sp_port->local_port);
		goto err_port_mtu_set;
	}

	err = mlxsw_sp_port_admin_status_set(mlxsw_sp_port, false);
	if (err)
		goto err_port_admin_status_set;

	err = mlxsw_sp_port_buffers_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to initialize buffers\n",
			mlxsw_sp_port->local_port);
		goto err_port_buffers_init;
	}

	mlxsw_sp_port_switchdev_init(mlxsw_sp_port);
	err = register_netdev(dev);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to register netdev\n",
			mlxsw_sp_port->local_port);
		goto err_register_netdev;
	}

	devlink_port_type_eth_set(devlink_port, dev);

	err = mlxsw_sp_port_vlan_init(mlxsw_sp_port);
	if (err)
		goto err_port_vlan_init;

	mlxsw_sp->ports[local_port] = mlxsw_sp_port;
	return 0;

err_port_vlan_init:
	unregister_netdev(dev);
err_register_netdev:
err_port_buffers_init:
err_port_admin_status_set:
err_port_mtu_set:
err_port_speed_by_width_set:
err_port_swid_set:
err_port_system_port_mapping_set:
	devlink_port_unregister(&mlxsw_sp_port->devlink_port);
err_devlink_port_register:
err_dev_addr_init:
	free_percpu(mlxsw_sp_port->pcpu_stats);
err_alloc_stats:
	kfree(mlxsw_sp_port->untagged_vlans);
err_port_untagged_vlans_alloc:
	kfree(mlxsw_sp_port->active_vlans);
err_port_active_vlans_alloc:
	free_netdev(dev);
	return err;
}

static int mlxsw_sp_port_create(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				bool split, u8 module, u8 width, u8 lane)
{
	int err;

	err = mlxsw_sp_port_module_map(mlxsw_sp, local_port, module, width,
				       lane);
	if (err)
		return err;

	err = __mlxsw_sp_port_create(mlxsw_sp, local_port, split, module,
				     width);
	if (err)
		goto err_port_create;

	return 0;

err_port_create:
	mlxsw_sp_port_module_unmap(mlxsw_sp, local_port);
	return err;
}

static void mlxsw_sp_port_vports_fini(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct net_device *dev = mlxsw_sp_port->dev;
	struct mlxsw_sp_port *mlxsw_sp_vport, *tmp;

	list_for_each_entry_safe(mlxsw_sp_vport, tmp,
				 &mlxsw_sp_port->vports_list, vport.list) {
		u16 vid = mlxsw_sp_vport_vid_get(mlxsw_sp_vport);

		/* vPorts created for VLAN devices should already be gone
		 * by now, since we unregistered the port netdev.
		 */
		WARN_ON(is_vlan_dev(mlxsw_sp_vport->dev));
		mlxsw_sp_port_kill_vid(dev, 0, vid);
	}
}

static void mlxsw_sp_port_remove(struct mlxsw_sp *mlxsw_sp, u8 local_port)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp->ports[local_port];
	struct devlink_port *devlink_port;

	if (!mlxsw_sp_port)
		return;
	mlxsw_sp->ports[local_port] = NULL;
	devlink_port = &mlxsw_sp_port->devlink_port;
	devlink_port_type_clear(devlink_port);
	unregister_netdev(mlxsw_sp_port->dev); /* This calls ndo_stop */
	devlink_port_unregister(devlink_port);
	mlxsw_sp_port_vports_fini(mlxsw_sp_port);
	mlxsw_sp_port_switchdev_fini(mlxsw_sp_port);
	mlxsw_sp_port_swid_set(mlxsw_sp_port, MLXSW_PORT_SWID_DISABLED_PORT);
	mlxsw_sp_port_module_unmap(mlxsw_sp, mlxsw_sp_port->local_port);
	free_percpu(mlxsw_sp_port->pcpu_stats);
	kfree(mlxsw_sp_port->untagged_vlans);
	kfree(mlxsw_sp_port->active_vlans);
	free_netdev(mlxsw_sp_port->dev);
}

static void mlxsw_sp_ports_remove(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	for (i = 1; i < MLXSW_PORT_MAX_PORTS; i++)
		mlxsw_sp_port_remove(mlxsw_sp, i);
	kfree(mlxsw_sp->ports);
}

static int mlxsw_sp_ports_create(struct mlxsw_sp *mlxsw_sp)
{
	size_t alloc_size;
	u8 module, width;
	int i;
	int err;

	alloc_size = sizeof(struct mlxsw_sp_port *) * MLXSW_PORT_MAX_PORTS;
	mlxsw_sp->ports = kzalloc(alloc_size, GFP_KERNEL);
	if (!mlxsw_sp->ports)
		return -ENOMEM;

	for (i = 1; i < MLXSW_PORT_MAX_PORTS; i++) {
		err = mlxsw_sp_port_module_info_get(mlxsw_sp, i, &module,
						    &width);
		if (err)
			goto err_port_module_info_get;
		if (!width)
			continue;
		mlxsw_sp->port_to_module[i] = module;
		err = __mlxsw_sp_port_create(mlxsw_sp, i, false, module, width);
		if (err)
			goto err_port_create;
	}
	return 0;

err_port_create:
err_port_module_info_get:
	for (i--; i >= 1; i--)
		mlxsw_sp_port_remove(mlxsw_sp, i);
	kfree(mlxsw_sp->ports);
	return err;
}

static u8 mlxsw_sp_cluster_base_port_get(u8 local_port)
{
	u8 offset = (local_port - 1) % MLXSW_SP_PORTS_PER_CLUSTER_MAX;

	return local_port - offset;
}

static int mlxsw_sp_port_split(void *priv, u8 local_port, unsigned int count)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	struct mlxsw_sp_port *mlxsw_sp_port;
	u8 width = MLXSW_PORT_MODULE_MAX_WIDTH / count;
	u8 module, cur_width, base_port;
	int i;
	int err;

	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port) {
		dev_err(mlxsw_sp->bus_info->dev, "Port number \"%d\" does not exist\n",
			local_port);
		return -EINVAL;
	}

	if (count != 2 && count != 4) {
		netdev_err(mlxsw_sp_port->dev, "Port can only be split into 2 or 4 ports\n");
		return -EINVAL;
	}

	err = mlxsw_sp_port_module_info_get(mlxsw_sp, local_port, &module,
					    &cur_width);
	if (err) {
		netdev_err(mlxsw_sp_port->dev, "Failed to get port's width\n");
		return err;
	}

	if (cur_width != MLXSW_PORT_MODULE_MAX_WIDTH) {
		netdev_err(mlxsw_sp_port->dev, "Port cannot be split further\n");
		return -EINVAL;
	}

	/* Make sure we have enough slave (even) ports for the split. */
	if (count == 2) {
		base_port = local_port;
		if (mlxsw_sp->ports[base_port + 1]) {
			netdev_err(mlxsw_sp_port->dev, "Invalid split configuration\n");
			return -EINVAL;
		}
	} else {
		base_port = mlxsw_sp_cluster_base_port_get(local_port);
		if (mlxsw_sp->ports[base_port + 1] ||
		    mlxsw_sp->ports[base_port + 3]) {
			netdev_err(mlxsw_sp_port->dev, "Invalid split configuration\n");
			return -EINVAL;
		}
	}

	for (i = 0; i < count; i++)
		mlxsw_sp_port_remove(mlxsw_sp, base_port + i);

	for (i = 0; i < count; i++) {
		err = mlxsw_sp_port_create(mlxsw_sp, base_port + i, true,
					   module, width, i * width);
		if (err) {
			dev_err(mlxsw_sp->bus_info->dev, "Failed to create split port\n");
			goto err_port_create;
		}
	}

	return 0;

err_port_create:
	for (i--; i >= 0; i--)
		mlxsw_sp_port_remove(mlxsw_sp, base_port + i);
	for (i = 0; i < count / 2; i++) {
		module = mlxsw_sp->port_to_module[base_port + i * 2];
		mlxsw_sp_port_create(mlxsw_sp, base_port + i * 2, false,
				     module, MLXSW_PORT_MODULE_MAX_WIDTH, 0);
	}
	return err;
}

static int mlxsw_sp_port_unsplit(void *priv, u8 local_port)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	struct mlxsw_sp_port *mlxsw_sp_port;
	u8 module, cur_width, base_port;
	unsigned int count;
	int i;
	int err;

	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port) {
		dev_err(mlxsw_sp->bus_info->dev, "Port number \"%d\" does not exist\n",
			local_port);
		return -EINVAL;
	}

	if (!mlxsw_sp_port->split) {
		netdev_err(mlxsw_sp_port->dev, "Port wasn't split\n");
		return -EINVAL;
	}

	err = mlxsw_sp_port_module_info_get(mlxsw_sp, local_port, &module,
					    &cur_width);
	if (err) {
		netdev_err(mlxsw_sp_port->dev, "Failed to get port's width\n");
		return err;
	}
	count = cur_width == 1 ? 4 : 2;

	base_port = mlxsw_sp_cluster_base_port_get(local_port);

	/* Determine which ports to remove. */
	if (count == 2 && local_port >= base_port + 2)
		base_port = base_port + 2;

	for (i = 0; i < count; i++)
		mlxsw_sp_port_remove(mlxsw_sp, base_port + i);

	for (i = 0; i < count / 2; i++) {
		module = mlxsw_sp->port_to_module[base_port + i * 2];
		err = mlxsw_sp_port_create(mlxsw_sp, base_port + i * 2, false,
					   module, MLXSW_PORT_MODULE_MAX_WIDTH,
					   0);
		if (err)
			dev_err(mlxsw_sp->bus_info->dev, "Failed to reinstantiate port\n");
	}

	return 0;
}

static void mlxsw_sp_pude_event_func(const struct mlxsw_reg_info *reg,
				     char *pude_pl, void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	struct mlxsw_sp_port *mlxsw_sp_port;
	enum mlxsw_reg_pude_oper_status status;
	u8 local_port;

	local_port = mlxsw_reg_pude_local_port_get(pude_pl);
	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port) {
		dev_warn(mlxsw_sp->bus_info->dev, "Port %d: Link event received for non-existent port\n",
			 local_port);
		return;
	}

	status = mlxsw_reg_pude_oper_status_get(pude_pl);
	if (status == MLXSW_PORT_OPER_STATUS_UP) {
		netdev_info(mlxsw_sp_port->dev, "link up\n");
		netif_carrier_on(mlxsw_sp_port->dev);
	} else {
		netdev_info(mlxsw_sp_port->dev, "link down\n");
		netif_carrier_off(mlxsw_sp_port->dev);
	}
}

static struct mlxsw_event_listener mlxsw_sp_pude_event = {
	.func = mlxsw_sp_pude_event_func,
	.trap_id = MLXSW_TRAP_ID_PUDE,
};

static int mlxsw_sp_event_register(struct mlxsw_sp *mlxsw_sp,
				   enum mlxsw_event_trap_id trap_id)
{
	struct mlxsw_event_listener *el;
	char hpkt_pl[MLXSW_REG_HPKT_LEN];
	int err;

	switch (trap_id) {
	case MLXSW_TRAP_ID_PUDE:
		el = &mlxsw_sp_pude_event;
		break;
	}
	err = mlxsw_core_event_listener_register(mlxsw_sp->core, el, mlxsw_sp);
	if (err)
		return err;

	mlxsw_reg_hpkt_pack(hpkt_pl, MLXSW_REG_HPKT_ACTION_FORWARD, trap_id);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(hpkt), hpkt_pl);
	if (err)
		goto err_event_trap_set;

	return 0;

err_event_trap_set:
	mlxsw_core_event_listener_unregister(mlxsw_sp->core, el, mlxsw_sp);
	return err;
}

static void mlxsw_sp_event_unregister(struct mlxsw_sp *mlxsw_sp,
				      enum mlxsw_event_trap_id trap_id)
{
	struct mlxsw_event_listener *el;

	switch (trap_id) {
	case MLXSW_TRAP_ID_PUDE:
		el = &mlxsw_sp_pude_event;
		break;
	}
	mlxsw_core_event_listener_unregister(mlxsw_sp->core, el, mlxsw_sp);
}

static void mlxsw_sp_rx_listener_func(struct sk_buff *skb, u8 local_port,
				      void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp->ports[local_port];
	struct mlxsw_sp_port_pcpu_stats *pcpu_stats;

	if (unlikely(!mlxsw_sp_port)) {
		dev_warn_ratelimited(mlxsw_sp->bus_info->dev, "Port %d: skb received for non-existent port\n",
				     local_port);
		return;
	}

	skb->dev = mlxsw_sp_port->dev;

	pcpu_stats = this_cpu_ptr(mlxsw_sp_port->pcpu_stats);
	u64_stats_update_begin(&pcpu_stats->syncp);
	pcpu_stats->rx_packets++;
	pcpu_stats->rx_bytes += skb->len;
	u64_stats_update_end(&pcpu_stats->syncp);

	skb->protocol = eth_type_trans(skb, skb->dev);
	netif_receive_skb(skb);
}

static const struct mlxsw_rx_listener mlxsw_sp_rx_listener[] = {
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_FDB_MC,
	},
	/* Traps for specific L2 packet types, not trapped as FDB MC */
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_STP,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_LACP,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_EAPOL,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_LLDP,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_MMRP,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_MVRP,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_RPVST,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_DHCP,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_IGMP_QUERY,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_IGMP_V1_REPORT,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_IGMP_V2_REPORT,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_IGMP_V2_LEAVE,
	},
	{
		.func = mlxsw_sp_rx_listener_func,
		.local_port = MLXSW_PORT_DONT_CARE,
		.trap_id = MLXSW_TRAP_ID_IGMP_V3_REPORT,
	},
};

static int mlxsw_sp_traps_init(struct mlxsw_sp *mlxsw_sp)
{
	char htgt_pl[MLXSW_REG_HTGT_LEN];
	char hpkt_pl[MLXSW_REG_HPKT_LEN];
	int i;
	int err;

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_RX);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(htgt), htgt_pl);
	if (err)
		return err;

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_CTRL);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(htgt), htgt_pl);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_rx_listener); i++) {
		err = mlxsw_core_rx_listener_register(mlxsw_sp->core,
						      &mlxsw_sp_rx_listener[i],
						      mlxsw_sp);
		if (err)
			goto err_rx_listener_register;

		mlxsw_reg_hpkt_pack(hpkt_pl, MLXSW_REG_HPKT_ACTION_TRAP_TO_CPU,
				    mlxsw_sp_rx_listener[i].trap_id);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(hpkt), hpkt_pl);
		if (err)
			goto err_rx_trap_set;
	}
	return 0;

err_rx_trap_set:
	mlxsw_core_rx_listener_unregister(mlxsw_sp->core,
					  &mlxsw_sp_rx_listener[i],
					  mlxsw_sp);
err_rx_listener_register:
	for (i--; i >= 0; i--) {
		mlxsw_reg_hpkt_pack(hpkt_pl, MLXSW_REG_HPKT_ACTION_FORWARD,
				    mlxsw_sp_rx_listener[i].trap_id);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(hpkt), hpkt_pl);

		mlxsw_core_rx_listener_unregister(mlxsw_sp->core,
						  &mlxsw_sp_rx_listener[i],
						  mlxsw_sp);
	}
	return err;
}

static void mlxsw_sp_traps_fini(struct mlxsw_sp *mlxsw_sp)
{
	char hpkt_pl[MLXSW_REG_HPKT_LEN];
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_rx_listener); i++) {
		mlxsw_reg_hpkt_pack(hpkt_pl, MLXSW_REG_HPKT_ACTION_FORWARD,
				    mlxsw_sp_rx_listener[i].trap_id);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(hpkt), hpkt_pl);

		mlxsw_core_rx_listener_unregister(mlxsw_sp->core,
						  &mlxsw_sp_rx_listener[i],
						  mlxsw_sp);
	}
}

static int __mlxsw_sp_flood_init(struct mlxsw_core *mlxsw_core,
				 enum mlxsw_reg_sfgc_type type,
				 enum mlxsw_reg_sfgc_bridge_type bridge_type)
{
	enum mlxsw_flood_table_type table_type;
	enum mlxsw_sp_flood_table flood_table;
	char sfgc_pl[MLXSW_REG_SFGC_LEN];

	if (bridge_type == MLXSW_REG_SFGC_BRIDGE_TYPE_VFID)
		table_type = MLXSW_REG_SFGC_TABLE_TYPE_FID;
	else
		table_type = MLXSW_REG_SFGC_TABLE_TYPE_FID_OFFEST;

	if (type == MLXSW_REG_SFGC_TYPE_UNKNOWN_UNICAST)
		flood_table = MLXSW_SP_FLOOD_TABLE_UC;
	else
		flood_table = MLXSW_SP_FLOOD_TABLE_BM;

	mlxsw_reg_sfgc_pack(sfgc_pl, type, bridge_type, table_type,
			    flood_table);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(sfgc), sfgc_pl);
}

static int mlxsw_sp_flood_init(struct mlxsw_sp *mlxsw_sp)
{
	int type, err;

	for (type = 0; type < MLXSW_REG_SFGC_TYPE_MAX; type++) {
		if (type == MLXSW_REG_SFGC_TYPE_RESERVED)
			continue;

		err = __mlxsw_sp_flood_init(mlxsw_sp->core, type,
					    MLXSW_REG_SFGC_BRIDGE_TYPE_VFID);
		if (err)
			return err;

		err = __mlxsw_sp_flood_init(mlxsw_sp->core, type,
					    MLXSW_REG_SFGC_BRIDGE_TYPE_1Q_FID);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_lag_init(struct mlxsw_sp *mlxsw_sp)
{
	char slcr_pl[MLXSW_REG_SLCR_LEN];

	mlxsw_reg_slcr_pack(slcr_pl, MLXSW_REG_SLCR_LAG_HASH_SMAC |
				     MLXSW_REG_SLCR_LAG_HASH_DMAC |
				     MLXSW_REG_SLCR_LAG_HASH_ETHERTYPE |
				     MLXSW_REG_SLCR_LAG_HASH_VLANID |
				     MLXSW_REG_SLCR_LAG_HASH_SIP |
				     MLXSW_REG_SLCR_LAG_HASH_DIP |
				     MLXSW_REG_SLCR_LAG_HASH_SPORT |
				     MLXSW_REG_SLCR_LAG_HASH_DPORT |
				     MLXSW_REG_SLCR_LAG_HASH_IPPROTO);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcr), slcr_pl);
}

static int mlxsw_sp_init(void *priv, struct mlxsw_core *mlxsw_core,
			 const struct mlxsw_bus_info *mlxsw_bus_info)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	int err;

	mlxsw_sp->core = mlxsw_core;
	mlxsw_sp->bus_info = mlxsw_bus_info;
	INIT_LIST_HEAD(&mlxsw_sp->port_vfids.list);
	INIT_LIST_HEAD(&mlxsw_sp->br_vfids.list);
	INIT_LIST_HEAD(&mlxsw_sp->br_mids.list);

	err = mlxsw_sp_base_mac_get(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to get base mac\n");
		return err;
	}

	err = mlxsw_sp_ports_create(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to create ports\n");
		return err;
	}

	err = mlxsw_sp_event_register(mlxsw_sp, MLXSW_TRAP_ID_PUDE);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to register for PUDE events\n");
		goto err_event_register;
	}

	err = mlxsw_sp_traps_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to set traps for RX\n");
		goto err_rx_listener_register;
	}

	err = mlxsw_sp_flood_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize flood tables\n");
		goto err_flood_init;
	}

	err = mlxsw_sp_buffers_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize buffers\n");
		goto err_buffers_init;
	}

	err = mlxsw_sp_lag_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize LAG\n");
		goto err_lag_init;
	}

	err = mlxsw_sp_switchdev_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize switchdev\n");
		goto err_switchdev_init;
	}

	return 0;

err_switchdev_init:
err_lag_init:
err_buffers_init:
err_flood_init:
	mlxsw_sp_traps_fini(mlxsw_sp);
err_rx_listener_register:
	mlxsw_sp_event_unregister(mlxsw_sp, MLXSW_TRAP_ID_PUDE);
err_event_register:
	mlxsw_sp_ports_remove(mlxsw_sp);
	return err;
}

static void mlxsw_sp_fini(void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp_switchdev_fini(mlxsw_sp);
	mlxsw_sp_traps_fini(mlxsw_sp);
	mlxsw_sp_event_unregister(mlxsw_sp, MLXSW_TRAP_ID_PUDE);
	mlxsw_sp_ports_remove(mlxsw_sp);
}

static struct mlxsw_config_profile mlxsw_sp_config_profile = {
	.used_max_vepa_channels		= 1,
	.max_vepa_channels		= 0,
	.used_max_lag			= 1,
	.max_lag			= MLXSW_SP_LAG_MAX,
	.used_max_port_per_lag		= 1,
	.max_port_per_lag		= MLXSW_SP_PORT_PER_LAG_MAX,
	.used_max_mid			= 1,
	.max_mid			= MLXSW_SP_MID_MAX,
	.used_max_pgt			= 1,
	.max_pgt			= 0,
	.used_max_system_port		= 1,
	.max_system_port		= 64,
	.used_max_vlan_groups		= 1,
	.max_vlan_groups		= 127,
	.used_max_regions		= 1,
	.max_regions			= 400,
	.used_flood_tables		= 1,
	.used_flood_mode		= 1,
	.flood_mode			= 3,
	.max_fid_offset_flood_tables	= 2,
	.fid_offset_flood_table_size	= VLAN_N_VID - 1,
	.max_fid_flood_tables		= 2,
	.fid_flood_table_size		= MLXSW_SP_VFID_MAX,
	.used_max_ib_mc			= 1,
	.max_ib_mc			= 0,
	.used_max_pkey			= 1,
	.max_pkey			= 0,
	.swid_config			= {
		{
			.used_type	= 1,
			.type		= MLXSW_PORT_SWID_TYPE_ETH,
		}
	},
};

static struct mlxsw_driver mlxsw_sp_driver = {
	.kind			= MLXSW_DEVICE_KIND_SPECTRUM,
	.owner			= THIS_MODULE,
	.priv_size		= sizeof(struct mlxsw_sp),
	.init			= mlxsw_sp_init,
	.fini			= mlxsw_sp_fini,
	.port_split		= mlxsw_sp_port_split,
	.port_unsplit		= mlxsw_sp_port_unsplit,
	.txhdr_construct	= mlxsw_sp_txhdr_construct,
	.txhdr_len		= MLXSW_TXHDR_LEN,
	.profile		= &mlxsw_sp_config_profile,
};

static int
mlxsw_sp_port_fdb_flush_by_port(const struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sfdf_pl[MLXSW_REG_SFDF_LEN];

	mlxsw_reg_sfdf_pack(sfdf_pl, MLXSW_REG_SFDF_FLUSH_PER_PORT);
	mlxsw_reg_sfdf_system_port_set(sfdf_pl, mlxsw_sp_port->local_port);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfdf), sfdf_pl);
}

static int
mlxsw_sp_port_fdb_flush_by_port_fid(const struct mlxsw_sp_port *mlxsw_sp_port,
				    u16 fid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sfdf_pl[MLXSW_REG_SFDF_LEN];

	mlxsw_reg_sfdf_pack(sfdf_pl, MLXSW_REG_SFDF_FLUSH_PER_PORT_AND_FID);
	mlxsw_reg_sfdf_fid_set(sfdf_pl, fid);
	mlxsw_reg_sfdf_port_fid_system_port_set(sfdf_pl,
						mlxsw_sp_port->local_port);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfdf), sfdf_pl);
}

static int
mlxsw_sp_port_fdb_flush_by_lag_id(const struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sfdf_pl[MLXSW_REG_SFDF_LEN];

	mlxsw_reg_sfdf_pack(sfdf_pl, MLXSW_REG_SFDF_FLUSH_PER_LAG);
	mlxsw_reg_sfdf_lag_id_set(sfdf_pl, mlxsw_sp_port->lag_id);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfdf), sfdf_pl);
}

static int
mlxsw_sp_port_fdb_flush_by_lag_id_fid(const struct mlxsw_sp_port *mlxsw_sp_port,
				      u16 fid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sfdf_pl[MLXSW_REG_SFDF_LEN];

	mlxsw_reg_sfdf_pack(sfdf_pl, MLXSW_REG_SFDF_FLUSH_PER_LAG_AND_FID);
	mlxsw_reg_sfdf_fid_set(sfdf_pl, fid);
	mlxsw_reg_sfdf_lag_fid_lag_id_set(sfdf_pl, mlxsw_sp_port->lag_id);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfdf), sfdf_pl);
}

static int
__mlxsw_sp_port_fdb_flush(const struct mlxsw_sp_port *mlxsw_sp_port)
{
	int err, last_err = 0;
	u16 vid;

	for (vid = 1; vid < VLAN_N_VID - 1; vid++) {
		err = mlxsw_sp_port_fdb_flush_by_port_fid(mlxsw_sp_port, vid);
		if (err)
			last_err = err;
	}

	return last_err;
}

static int
__mlxsw_sp_port_fdb_flush_lagged(const struct mlxsw_sp_port *mlxsw_sp_port)
{
	int err, last_err = 0;
	u16 vid;

	for (vid = 1; vid < VLAN_N_VID - 1; vid++) {
		err = mlxsw_sp_port_fdb_flush_by_lag_id_fid(mlxsw_sp_port, vid);
		if (err)
			last_err = err;
	}

	return last_err;
}

static int mlxsw_sp_port_fdb_flush(struct mlxsw_sp_port *mlxsw_sp_port)
{
	if (!list_empty(&mlxsw_sp_port->vports_list))
		if (mlxsw_sp_port->lagged)
			return __mlxsw_sp_port_fdb_flush_lagged(mlxsw_sp_port);
		else
			return __mlxsw_sp_port_fdb_flush(mlxsw_sp_port);
	else
		if (mlxsw_sp_port->lagged)
			return mlxsw_sp_port_fdb_flush_by_lag_id(mlxsw_sp_port);
		else
			return mlxsw_sp_port_fdb_flush_by_port(mlxsw_sp_port);
}

static int mlxsw_sp_vport_fdb_flush(struct mlxsw_sp_port *mlxsw_sp_vport)
{
	u16 vfid = mlxsw_sp_vport_vfid_get(mlxsw_sp_vport);
	u16 fid = mlxsw_sp_vfid_to_fid(vfid);

	if (mlxsw_sp_vport->lagged)
		return mlxsw_sp_port_fdb_flush_by_lag_id_fid(mlxsw_sp_vport,
							     fid);
	else
		return mlxsw_sp_port_fdb_flush_by_port_fid(mlxsw_sp_vport, fid);
}

static bool mlxsw_sp_port_dev_check(const struct net_device *dev)
{
	return dev->netdev_ops == &mlxsw_sp_port_netdev_ops;
}

static int mlxsw_sp_port_bridge_join(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct net_device *dev = mlxsw_sp_port->dev;
	int err;

	/* When port is not bridged untagged packets are tagged with
	 * PVID=VID=1, thereby creating an implicit VLAN interface in
	 * the device. Remove it and let bridge code take care of its
	 * own VLANs.
	 */
	err = mlxsw_sp_port_kill_vid(dev, 0, 1);
	if (err)
		return err;

	mlxsw_sp_port->learning = 1;
	mlxsw_sp_port->learning_sync = 1;
	mlxsw_sp_port->uc_flood = 1;
	mlxsw_sp_port->bridged = 1;

	return 0;
}

static int mlxsw_sp_port_bridge_leave(struct mlxsw_sp_port *mlxsw_sp_port,
				      bool flush_fdb)
{
	struct net_device *dev = mlxsw_sp_port->dev;

	if (flush_fdb && mlxsw_sp_port_fdb_flush(mlxsw_sp_port))
		netdev_err(mlxsw_sp_port->dev, "Failed to flush FDB\n");

	mlxsw_sp_port_pvid_set(mlxsw_sp_port, 1);

	mlxsw_sp_port->learning = 0;
	mlxsw_sp_port->learning_sync = 0;
	mlxsw_sp_port->uc_flood = 0;
	mlxsw_sp_port->bridged = 0;

	/* Add implicit VLAN interface in the device, so that untagged
	 * packets will be classified to the default vFID.
	 */
	return mlxsw_sp_port_add_vid(dev, 0, 1);
}

static bool mlxsw_sp_master_bridge_check(struct mlxsw_sp *mlxsw_sp,
					 struct net_device *br_dev)
{
	return !mlxsw_sp->master_bridge.dev ||
	       mlxsw_sp->master_bridge.dev == br_dev;
}

static void mlxsw_sp_master_bridge_inc(struct mlxsw_sp *mlxsw_sp,
				       struct net_device *br_dev)
{
	mlxsw_sp->master_bridge.dev = br_dev;
	mlxsw_sp->master_bridge.ref_count++;
}

static void mlxsw_sp_master_bridge_dec(struct mlxsw_sp *mlxsw_sp,
				       struct net_device *br_dev)
{
	if (--mlxsw_sp->master_bridge.ref_count == 0)
		mlxsw_sp->master_bridge.dev = NULL;
}

static int mlxsw_sp_lag_create(struct mlxsw_sp *mlxsw_sp, u16 lag_id)
{
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_create_pack(sldr_pl, lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int mlxsw_sp_lag_destroy(struct mlxsw_sp *mlxsw_sp, u16 lag_id)
{
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_destroy_pack(sldr_pl, lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int mlxsw_sp_lag_col_port_add(struct mlxsw_sp_port *mlxsw_sp_port,
				     u16 lag_id, u8 port_index)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_port_add_pack(slcor_pl, mlxsw_sp_port->local_port,
				      lag_id, port_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_col_port_remove(struct mlxsw_sp_port *mlxsw_sp_port,
					u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_port_remove_pack(slcor_pl, mlxsw_sp_port->local_port,
					 lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_col_port_enable(struct mlxsw_sp_port *mlxsw_sp_port,
					u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_col_enable_pack(slcor_pl, mlxsw_sp_port->local_port,
					lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_col_port_disable(struct mlxsw_sp_port *mlxsw_sp_port,
					 u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_col_disable_pack(slcor_pl, mlxsw_sp_port->local_port,
					 lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_index_get(struct mlxsw_sp *mlxsw_sp,
				  struct net_device *lag_dev,
				  u16 *p_lag_id)
{
	struct mlxsw_sp_upper *lag;
	int free_lag_id = -1;
	int i;

	for (i = 0; i < MLXSW_SP_LAG_MAX; i++) {
		lag = mlxsw_sp_lag_get(mlxsw_sp, i);
		if (lag->ref_count) {
			if (lag->dev == lag_dev) {
				*p_lag_id = i;
				return 0;
			}
		} else if (free_lag_id < 0) {
			free_lag_id = i;
		}
	}
	if (free_lag_id < 0)
		return -EBUSY;
	*p_lag_id = free_lag_id;
	return 0;
}

static bool
mlxsw_sp_master_lag_check(struct mlxsw_sp *mlxsw_sp,
			  struct net_device *lag_dev,
			  struct netdev_lag_upper_info *lag_upper_info)
{
	u16 lag_id;

	if (mlxsw_sp_lag_index_get(mlxsw_sp, lag_dev, &lag_id) != 0)
		return false;
	if (lag_upper_info->tx_type != NETDEV_LAG_TX_TYPE_HASH)
		return false;
	return true;
}

static int mlxsw_sp_port_lag_index_get(struct mlxsw_sp *mlxsw_sp,
				       u16 lag_id, u8 *p_port_index)
{
	int i;

	for (i = 0; i < MLXSW_SP_PORT_PER_LAG_MAX; i++) {
		if (!mlxsw_sp_port_lagged_get(mlxsw_sp, lag_id, i)) {
			*p_port_index = i;
			return 0;
		}
	}
	return -EBUSY;
}

static int mlxsw_sp_port_lag_join(struct mlxsw_sp_port *mlxsw_sp_port,
				  struct net_device *lag_dev)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_upper *lag;
	u16 lag_id;
	u8 port_index;
	int err;

	err = mlxsw_sp_lag_index_get(mlxsw_sp, lag_dev, &lag_id);
	if (err)
		return err;
	lag = mlxsw_sp_lag_get(mlxsw_sp, lag_id);
	if (!lag->ref_count) {
		err = mlxsw_sp_lag_create(mlxsw_sp, lag_id);
		if (err)
			return err;
		lag->dev = lag_dev;
	}

	err = mlxsw_sp_port_lag_index_get(mlxsw_sp, lag_id, &port_index);
	if (err)
		return err;
	err = mlxsw_sp_lag_col_port_add(mlxsw_sp_port, lag_id, port_index);
	if (err)
		goto err_col_port_add;
	err = mlxsw_sp_lag_col_port_enable(mlxsw_sp_port, lag_id);
	if (err)
		goto err_col_port_enable;

	mlxsw_core_lag_mapping_set(mlxsw_sp->core, lag_id, port_index,
				   mlxsw_sp_port->local_port);
	mlxsw_sp_port->lag_id = lag_id;
	mlxsw_sp_port->lagged = 1;
	lag->ref_count++;
	return 0;

err_col_port_add:
	if (!lag->ref_count)
		mlxsw_sp_lag_destroy(mlxsw_sp, lag_id);
err_col_port_enable:
	mlxsw_sp_lag_col_port_remove(mlxsw_sp_port, lag_id);
	return err;
}

static int mlxsw_sp_vport_bridge_leave(struct mlxsw_sp_port *mlxsw_sp_vport,
				       struct net_device *br_dev,
				       bool flush_fdb);

static int mlxsw_sp_port_lag_leave(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct net_device *lag_dev)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_port *mlxsw_sp_vport;
	struct mlxsw_sp_upper *lag;
	u16 lag_id = mlxsw_sp_port->lag_id;
	int err;

	if (!mlxsw_sp_port->lagged)
		return 0;
	lag = mlxsw_sp_lag_get(mlxsw_sp, lag_id);
	WARN_ON(lag->ref_count == 0);

	err = mlxsw_sp_lag_col_port_disable(mlxsw_sp_port, lag_id);
	if (err)
		return err;
	err = mlxsw_sp_lag_col_port_remove(mlxsw_sp_port, lag_id);
	if (err)
		return err;

	/* In case we leave a LAG device that has bridges built on top,
	 * then their teardown sequence is never issued and we need to
	 * invoke the necessary cleanup routines ourselves.
	 */
	list_for_each_entry(mlxsw_sp_vport, &mlxsw_sp_port->vports_list,
			    vport.list) {
		struct net_device *br_dev;

		if (!mlxsw_sp_vport->bridged)
			continue;

		br_dev = mlxsw_sp_vport_br_get(mlxsw_sp_vport);
		mlxsw_sp_vport_bridge_leave(mlxsw_sp_vport, br_dev, false);
	}

	if (mlxsw_sp_port->bridged) {
		mlxsw_sp_port_active_vlans_del(mlxsw_sp_port);
		mlxsw_sp_port_bridge_leave(mlxsw_sp_port, false);
		mlxsw_sp_master_bridge_dec(mlxsw_sp, NULL);
	}

	if (lag->ref_count == 1) {
		if (mlxsw_sp_port_fdb_flush_by_lag_id(mlxsw_sp_port))
			netdev_err(mlxsw_sp_port->dev, "Failed to flush FDB\n");
		err = mlxsw_sp_lag_destroy(mlxsw_sp, lag_id);
		if (err)
			return err;
	}

	mlxsw_core_lag_mapping_clear(mlxsw_sp->core, lag_id,
				     mlxsw_sp_port->local_port);
	mlxsw_sp_port->lagged = 0;
	lag->ref_count--;
	return 0;
}

static int mlxsw_sp_lag_dist_port_add(struct mlxsw_sp_port *mlxsw_sp_port,
				      u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_add_port_pack(sldr_pl, lag_id,
					 mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int mlxsw_sp_lag_dist_port_remove(struct mlxsw_sp_port *mlxsw_sp_port,
					 u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_remove_port_pack(sldr_pl, lag_id,
					    mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int mlxsw_sp_port_lag_tx_en_set(struct mlxsw_sp_port *mlxsw_sp_port,
				       bool lag_tx_enabled)
{
	if (lag_tx_enabled)
		return mlxsw_sp_lag_dist_port_add(mlxsw_sp_port,
						  mlxsw_sp_port->lag_id);
	else
		return mlxsw_sp_lag_dist_port_remove(mlxsw_sp_port,
						     mlxsw_sp_port->lag_id);
}

static int mlxsw_sp_port_lag_changed(struct mlxsw_sp_port *mlxsw_sp_port,
				     struct netdev_lag_lower_state_info *info)
{
	return mlxsw_sp_port_lag_tx_en_set(mlxsw_sp_port, info->tx_enabled);
}

static int mlxsw_sp_port_vlan_link(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct net_device *vlan_dev)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;
	u16 vid = vlan_dev_vlan_id(vlan_dev);

	mlxsw_sp_vport = mlxsw_sp_port_vport_find(mlxsw_sp_port, vid);
	if (!mlxsw_sp_vport) {
		WARN_ON(!mlxsw_sp_vport);
		return -EINVAL;
	}

	mlxsw_sp_vport->dev = vlan_dev;

	return 0;
}

static int mlxsw_sp_port_vlan_unlink(struct mlxsw_sp_port *mlxsw_sp_port,
				     struct net_device *vlan_dev)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;
	u16 vid = vlan_dev_vlan_id(vlan_dev);

	mlxsw_sp_vport = mlxsw_sp_port_vport_find(mlxsw_sp_port, vid);
	if (!mlxsw_sp_vport) {
		WARN_ON(!mlxsw_sp_vport);
		return -EINVAL;
	}

	/* When removing a VLAN device while still bridged we should first
	 * remove it from the bridge, as we receive the bridge's notification
	 * when the vPort is already gone.
	 */
	if (mlxsw_sp_vport->bridged) {
		struct net_device *br_dev;

		br_dev = mlxsw_sp_vport_br_get(mlxsw_sp_vport);
		mlxsw_sp_vport_bridge_leave(mlxsw_sp_vport, br_dev, true);
	}

	mlxsw_sp_vport->dev = mlxsw_sp_port->dev;

	return 0;
}

static int mlxsw_sp_netdevice_port_upper_event(struct net_device *dev,
					       unsigned long event, void *ptr)
{
	struct netdev_notifier_changeupper_info *info;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct net_device *upper_dev;
	struct mlxsw_sp *mlxsw_sp;
	int err;

	mlxsw_sp_port = netdev_priv(dev);
	mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	info = ptr;

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		upper_dev = info->upper_dev;
		if (!info->master || !info->linking)
			break;
		/* HW limitation forbids to put ports to multiple bridges. */
		if (netif_is_bridge_master(upper_dev) &&
		    !mlxsw_sp_master_bridge_check(mlxsw_sp, upper_dev))
			return NOTIFY_BAD;
		if (netif_is_lag_master(upper_dev) &&
		    !mlxsw_sp_master_lag_check(mlxsw_sp, upper_dev,
					       info->upper_info))
			return NOTIFY_BAD;
		break;
	case NETDEV_CHANGEUPPER:
		upper_dev = info->upper_dev;
		if (is_vlan_dev(upper_dev)) {
			if (info->linking) {
				err = mlxsw_sp_port_vlan_link(mlxsw_sp_port,
							      upper_dev);
				if (err) {
					netdev_err(dev, "Failed to link VLAN device\n");
					return NOTIFY_BAD;
				}
			} else {
				err = mlxsw_sp_port_vlan_unlink(mlxsw_sp_port,
								upper_dev);
				if (err) {
					netdev_err(dev, "Failed to unlink VLAN device\n");
					return NOTIFY_BAD;
				}
			}
		} else if (netif_is_bridge_master(upper_dev)) {
			if (info->linking) {
				err = mlxsw_sp_port_bridge_join(mlxsw_sp_port);
				if (err) {
					netdev_err(dev, "Failed to join bridge\n");
					return NOTIFY_BAD;
				}
				mlxsw_sp_master_bridge_inc(mlxsw_sp, upper_dev);
			} else {
				err = mlxsw_sp_port_bridge_leave(mlxsw_sp_port,
								 true);
				mlxsw_sp_master_bridge_dec(mlxsw_sp, upper_dev);
				if (err) {
					netdev_err(dev, "Failed to leave bridge\n");
					return NOTIFY_BAD;
				}
			}
		} else if (netif_is_lag_master(upper_dev)) {
			if (info->linking) {
				err = mlxsw_sp_port_lag_join(mlxsw_sp_port,
							     upper_dev);
				if (err) {
					netdev_err(dev, "Failed to join link aggregation\n");
					return NOTIFY_BAD;
				}
			} else {
				err = mlxsw_sp_port_lag_leave(mlxsw_sp_port,
							      upper_dev);
				if (err) {
					netdev_err(dev, "Failed to leave link aggregation\n");
					return NOTIFY_BAD;
				}
			}
		}
		break;
	}

	return NOTIFY_DONE;
}

static int mlxsw_sp_netdevice_port_lower_event(struct net_device *dev,
					       unsigned long event, void *ptr)
{
	struct netdev_notifier_changelowerstate_info *info;
	struct mlxsw_sp_port *mlxsw_sp_port;
	int err;

	mlxsw_sp_port = netdev_priv(dev);
	info = ptr;

	switch (event) {
	case NETDEV_CHANGELOWERSTATE:
		if (netif_is_lag_port(dev) && mlxsw_sp_port->lagged) {
			err = mlxsw_sp_port_lag_changed(mlxsw_sp_port,
							info->lower_state_info);
			if (err)
				netdev_err(dev, "Failed to reflect link aggregation lower state change\n");
		}
		break;
	}

	return NOTIFY_DONE;
}

static int mlxsw_sp_netdevice_port_event(struct net_device *dev,
					 unsigned long event, void *ptr)
{
	switch (event) {
	case NETDEV_PRECHANGEUPPER:
	case NETDEV_CHANGEUPPER:
		return mlxsw_sp_netdevice_port_upper_event(dev, event, ptr);
	case NETDEV_CHANGELOWERSTATE:
		return mlxsw_sp_netdevice_port_lower_event(dev, event, ptr);
	}

	return NOTIFY_DONE;
}

static int mlxsw_sp_netdevice_lag_event(struct net_device *lag_dev,
					unsigned long event, void *ptr)
{
	struct net_device *dev;
	struct list_head *iter;
	int ret;

	netdev_for_each_lower_dev(lag_dev, dev, iter) {
		if (mlxsw_sp_port_dev_check(dev)) {
			ret = mlxsw_sp_netdevice_port_event(dev, event, ptr);
			if (ret == NOTIFY_BAD)
				return ret;
		}
	}

	return NOTIFY_DONE;
}

static struct mlxsw_sp_vfid *
mlxsw_sp_br_vfid_find(const struct mlxsw_sp *mlxsw_sp,
		      const struct net_device *br_dev)
{
	struct mlxsw_sp_vfid *vfid;

	list_for_each_entry(vfid, &mlxsw_sp->br_vfids.list, list) {
		if (vfid->br_dev == br_dev)
			return vfid;
	}

	return NULL;
}

static u16 mlxsw_sp_vfid_to_br_vfid(u16 vfid)
{
	return vfid - MLXSW_SP_VFID_PORT_MAX;
}

static u16 mlxsw_sp_br_vfid_to_vfid(u16 br_vfid)
{
	return MLXSW_SP_VFID_PORT_MAX + br_vfid;
}

static u16 mlxsw_sp_avail_br_vfid_get(const struct mlxsw_sp *mlxsw_sp)
{
	return find_first_zero_bit(mlxsw_sp->br_vfids.mapped,
				   MLXSW_SP_VFID_BR_MAX);
}

static struct mlxsw_sp_vfid *mlxsw_sp_br_vfid_create(struct mlxsw_sp *mlxsw_sp,
						     struct net_device *br_dev)
{
	struct device *dev = mlxsw_sp->bus_info->dev;
	struct mlxsw_sp_vfid *vfid;
	u16 n_vfid;
	int err;

	n_vfid = mlxsw_sp_br_vfid_to_vfid(mlxsw_sp_avail_br_vfid_get(mlxsw_sp));
	if (n_vfid == MLXSW_SP_VFID_MAX) {
		dev_err(dev, "No available vFIDs\n");
		return ERR_PTR(-ERANGE);
	}

	err = __mlxsw_sp_vfid_create(mlxsw_sp, n_vfid);
	if (err) {
		dev_err(dev, "Failed to create vFID=%d\n", n_vfid);
		return ERR_PTR(err);
	}

	vfid = kzalloc(sizeof(*vfid), GFP_KERNEL);
	if (!vfid)
		goto err_allocate_vfid;

	vfid->vfid = n_vfid;
	vfid->br_dev = br_dev;

	list_add(&vfid->list, &mlxsw_sp->br_vfids.list);
	set_bit(mlxsw_sp_vfid_to_br_vfid(n_vfid), mlxsw_sp->br_vfids.mapped);

	return vfid;

err_allocate_vfid:
	__mlxsw_sp_vfid_destroy(mlxsw_sp, n_vfid);
	return ERR_PTR(-ENOMEM);
}

static void mlxsw_sp_br_vfid_destroy(struct mlxsw_sp *mlxsw_sp,
				     struct mlxsw_sp_vfid *vfid)
{
	u16 br_vfid = mlxsw_sp_vfid_to_br_vfid(vfid->vfid);

	clear_bit(br_vfid, mlxsw_sp->br_vfids.mapped);
	list_del(&vfid->list);

	__mlxsw_sp_vfid_destroy(mlxsw_sp, vfid->vfid);

	kfree(vfid);
}

static int mlxsw_sp_vport_bridge_leave(struct mlxsw_sp_port *mlxsw_sp_vport,
				       struct net_device *br_dev,
				       bool flush_fdb)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_vport->mlxsw_sp;
	u16 vid = mlxsw_sp_vport_vid_get(mlxsw_sp_vport);
	struct net_device *dev = mlxsw_sp_vport->dev;
	struct mlxsw_sp_vfid *vfid, *new_vfid;
	int err;

	vfid = mlxsw_sp_br_vfid_find(mlxsw_sp, br_dev);
	if (!vfid) {
		WARN_ON(!vfid);
		return -EINVAL;
	}

	/* We need a vFID to go back to after leaving the bridge's vFID. */
	new_vfid = mlxsw_sp_vfid_find(mlxsw_sp, vid);
	if (!new_vfid) {
		new_vfid = mlxsw_sp_vfid_create(mlxsw_sp, vid);
		if (IS_ERR(new_vfid)) {
			netdev_err(dev, "Failed to create vFID for VID=%d\n",
				   vid);
			return PTR_ERR(new_vfid);
		}
	}

	/* Invalidate existing {Port, VID} to vFID mapping and create a new
	 * one for the new vFID.
	 */
	err = mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_vport,
					   MLXSW_REG_SVFA_MT_PORT_VID_TO_FID,
					   false,
					   mlxsw_sp_vfid_to_fid(vfid->vfid),
					   vid);
	if (err) {
		netdev_err(dev, "Failed to invalidate {Port, VID} to vFID=%d mapping\n",
			   vfid->vfid);
		goto err_port_vid_to_fid_invalidate;
	}

	err = mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_vport,
					   MLXSW_REG_SVFA_MT_PORT_VID_TO_FID,
					   true,
					   mlxsw_sp_vfid_to_fid(new_vfid->vfid),
					   vid);
	if (err) {
		netdev_err(dev, "Failed to map {Port, VID} to vFID=%d\n",
			   new_vfid->vfid);
		goto err_port_vid_to_fid_validate;
	}

	err = mlxsw_sp_port_vid_learning_set(mlxsw_sp_vport, vid, false);
	if (err) {
		netdev_err(dev, "Failed to disable learning\n");
		goto err_port_vid_learning_set;
	}

	err = mlxsw_sp_vport_flood_set(mlxsw_sp_vport, vfid->vfid, false,
				       false);
	if (err) {
		netdev_err(dev, "Failed clear to clear flooding\n");
		goto err_vport_flood_set;
	}

	err = mlxsw_sp_port_stp_state_set(mlxsw_sp_vport, vid,
					  MLXSW_REG_SPMS_STATE_FORWARDING);
	if (err) {
		netdev_err(dev, "Failed to set STP state\n");
		goto err_port_stp_state_set;
	}

	if (flush_fdb && mlxsw_sp_vport_fdb_flush(mlxsw_sp_vport))
		netdev_err(dev, "Failed to flush FDB\n");

	/* Switch between the vFIDs and destroy the old one if needed. */
	new_vfid->nr_vports++;
	mlxsw_sp_vport->vport.vfid = new_vfid;
	vfid->nr_vports--;
	if (!vfid->nr_vports)
		mlxsw_sp_br_vfid_destroy(mlxsw_sp, vfid);

	mlxsw_sp_vport->learning = 0;
	mlxsw_sp_vport->learning_sync = 0;
	mlxsw_sp_vport->uc_flood = 0;
	mlxsw_sp_vport->bridged = 0;

	return 0;

err_port_stp_state_set:
err_vport_flood_set:
err_port_vid_learning_set:
err_port_vid_to_fid_validate:
err_port_vid_to_fid_invalidate:
	/* Rollback vFID only if new. */
	if (!new_vfid->nr_vports)
		mlxsw_sp_vfid_destroy(mlxsw_sp, new_vfid);
	return err;
}

static int mlxsw_sp_vport_bridge_join(struct mlxsw_sp_port *mlxsw_sp_vport,
				      struct net_device *br_dev)
{
	struct mlxsw_sp_vfid *old_vfid = mlxsw_sp_vport->vport.vfid;
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_vport->mlxsw_sp;
	u16 vid = mlxsw_sp_vport_vid_get(mlxsw_sp_vport);
	struct net_device *dev = mlxsw_sp_vport->dev;
	struct mlxsw_sp_vfid *vfid;
	int err;

	vfid = mlxsw_sp_br_vfid_find(mlxsw_sp, br_dev);
	if (!vfid) {
		vfid = mlxsw_sp_br_vfid_create(mlxsw_sp, br_dev);
		if (IS_ERR(vfid)) {
			netdev_err(dev, "Failed to create bridge vFID\n");
			return PTR_ERR(vfid);
		}
	}

	err = mlxsw_sp_vport_flood_set(mlxsw_sp_vport, vfid->vfid, true, false);
	if (err) {
		netdev_err(dev, "Failed to setup flooding for vFID=%d\n",
			   vfid->vfid);
		goto err_port_flood_set;
	}

	err = mlxsw_sp_port_vid_learning_set(mlxsw_sp_vport, vid, true);
	if (err) {
		netdev_err(dev, "Failed to enable learning\n");
		goto err_port_vid_learning_set;
	}

	/* We need to invalidate existing {Port, VID} to vFID mapping and
	 * create a new one for the bridge's vFID.
	 */
	err = mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_vport,
					   MLXSW_REG_SVFA_MT_PORT_VID_TO_FID,
					   false,
					   mlxsw_sp_vfid_to_fid(old_vfid->vfid),
					   vid);
	if (err) {
		netdev_err(dev, "Failed to invalidate {Port, VID} to vFID=%d mapping\n",
			   old_vfid->vfid);
		goto err_port_vid_to_fid_invalidate;
	}

	err = mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_vport,
					   MLXSW_REG_SVFA_MT_PORT_VID_TO_FID,
					   true,
					   mlxsw_sp_vfid_to_fid(vfid->vfid),
					   vid);
	if (err) {
		netdev_err(dev, "Failed to map {Port, VID} to vFID=%d\n",
			   vfid->vfid);
		goto err_port_vid_to_fid_validate;
	}

	/* Switch between the vFIDs and destroy the old one if needed. */
	vfid->nr_vports++;
	mlxsw_sp_vport->vport.vfid = vfid;
	old_vfid->nr_vports--;
	if (!old_vfid->nr_vports)
		mlxsw_sp_vfid_destroy(mlxsw_sp, old_vfid);

	mlxsw_sp_vport->learning = 1;
	mlxsw_sp_vport->learning_sync = 1;
	mlxsw_sp_vport->uc_flood = 1;
	mlxsw_sp_vport->bridged = 1;

	return 0;

err_port_vid_to_fid_validate:
	mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_vport,
				     MLXSW_REG_SVFA_MT_PORT_VID_TO_FID, false,
				     mlxsw_sp_vfid_to_fid(old_vfid->vfid), vid);
err_port_vid_to_fid_invalidate:
	mlxsw_sp_port_vid_learning_set(mlxsw_sp_vport, vid, false);
err_port_vid_learning_set:
	mlxsw_sp_vport_flood_set(mlxsw_sp_vport, vfid->vfid, false, false);
err_port_flood_set:
	if (!vfid->nr_vports)
		mlxsw_sp_br_vfid_destroy(mlxsw_sp, vfid);
	return err;
}

static bool
mlxsw_sp_port_master_bridge_check(const struct mlxsw_sp_port *mlxsw_sp_port,
				  const struct net_device *br_dev)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;

	list_for_each_entry(mlxsw_sp_vport, &mlxsw_sp_port->vports_list,
			    vport.list) {
		if (mlxsw_sp_vport_br_get(mlxsw_sp_vport) == br_dev)
			return false;
	}

	return true;
}

static int mlxsw_sp_netdevice_vport_event(struct net_device *dev,
					  unsigned long event, void *ptr,
					  u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct netdev_notifier_changeupper_info *info = ptr;
	struct mlxsw_sp_port *mlxsw_sp_vport;
	struct net_device *upper_dev;
	int err;

	mlxsw_sp_vport = mlxsw_sp_port_vport_find(mlxsw_sp_port, vid);

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		upper_dev = info->upper_dev;
		if (!info->master || !info->linking)
			break;
		if (!netif_is_bridge_master(upper_dev))
			return NOTIFY_BAD;
		/* We can't have multiple VLAN interfaces configured on
		 * the same port and being members in the same bridge.
		 */
		if (!mlxsw_sp_port_master_bridge_check(mlxsw_sp_port,
						       upper_dev))
			return NOTIFY_BAD;
		break;
	case NETDEV_CHANGEUPPER:
		upper_dev = info->upper_dev;
		if (!info->master)
			break;
		if (info->linking) {
			if (!mlxsw_sp_vport) {
				WARN_ON(!mlxsw_sp_vport);
				return NOTIFY_BAD;
			}
			err = mlxsw_sp_vport_bridge_join(mlxsw_sp_vport,
							 upper_dev);
			if (err) {
				netdev_err(dev, "Failed to join bridge\n");
				return NOTIFY_BAD;
			}
		} else {
			/* We ignore bridge's unlinking notifications if vPort
			 * is gone, since we already left the bridge when the
			 * VLAN device was unlinked from the real device.
			 */
			if (!mlxsw_sp_vport)
				return NOTIFY_DONE;
			err = mlxsw_sp_vport_bridge_leave(mlxsw_sp_vport,
							  upper_dev, true);
			if (err) {
				netdev_err(dev, "Failed to leave bridge\n");
				return NOTIFY_BAD;
			}
		}
	}

	return NOTIFY_DONE;
}

static int mlxsw_sp_netdevice_lag_vport_event(struct net_device *lag_dev,
					      unsigned long event, void *ptr,
					      u16 vid)
{
	struct net_device *dev;
	struct list_head *iter;
	int ret;

	netdev_for_each_lower_dev(lag_dev, dev, iter) {
		if (mlxsw_sp_port_dev_check(dev)) {
			ret = mlxsw_sp_netdevice_vport_event(dev, event, ptr,
							     vid);
			if (ret == NOTIFY_BAD)
				return ret;
		}
	}

	return NOTIFY_DONE;
}

static int mlxsw_sp_netdevice_vlan_event(struct net_device *vlan_dev,
					 unsigned long event, void *ptr)
{
	struct net_device *real_dev = vlan_dev_real_dev(vlan_dev);
	u16 vid = vlan_dev_vlan_id(vlan_dev);

	if (mlxsw_sp_port_dev_check(real_dev))
		return mlxsw_sp_netdevice_vport_event(real_dev, event, ptr,
						      vid);
	else if (netif_is_lag_master(real_dev))
		return mlxsw_sp_netdevice_lag_vport_event(real_dev, event, ptr,
							  vid);

	return NOTIFY_DONE;
}

static int mlxsw_sp_netdevice_event(struct notifier_block *unused,
				    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (mlxsw_sp_port_dev_check(dev))
		return mlxsw_sp_netdevice_port_event(dev, event, ptr);

	if (netif_is_lag_master(dev))
		return mlxsw_sp_netdevice_lag_event(dev, event, ptr);

	if (is_vlan_dev(dev))
		return mlxsw_sp_netdevice_vlan_event(dev, event, ptr);

	return NOTIFY_DONE;
}

static struct notifier_block mlxsw_sp_netdevice_nb __read_mostly = {
	.notifier_call = mlxsw_sp_netdevice_event,
};

static int __init mlxsw_sp_module_init(void)
{
	int err;

	register_netdevice_notifier(&mlxsw_sp_netdevice_nb);
	err = mlxsw_core_driver_register(&mlxsw_sp_driver);
	if (err)
		goto err_core_driver_register;
	return 0;

err_core_driver_register:
	unregister_netdevice_notifier(&mlxsw_sp_netdevice_nb);
	return err;
}

static void __exit mlxsw_sp_module_exit(void)
{
	mlxsw_core_driver_unregister(&mlxsw_sp_driver);
	unregister_netdevice_notifier(&mlxsw_sp_netdevice_nb);
}

module_init(mlxsw_sp_module_init);
module_exit(mlxsw_sp_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Mellanox Spectrum driver");
MODULE_MLXSW_DRIVER_ALIAS(MLXSW_DEVICE_KIND_SPECTRUM);
