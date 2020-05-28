// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>

#include "pci.h"
#include "core.h"
#include "reg.h"
#include "port.h"
#include "trap.h"
#include "txheader.h"
#include "ib.h"

static const char mlxsw_sx_driver_name[] = "mlxsw_switchx2";
static const char mlxsw_sx_driver_version[] = "1.0";

struct mlxsw_sx_port;

struct mlxsw_sx {
	struct mlxsw_sx_port **ports;
	struct mlxsw_core *core;
	const struct mlxsw_bus_info *bus_info;
	u8 hw_id[ETH_ALEN];
};

struct mlxsw_sx_port_pcpu_stats {
	u64			rx_packets;
	u64			rx_bytes;
	u64			tx_packets;
	u64			tx_bytes;
	struct u64_stats_sync	syncp;
	u32			tx_dropped;
};

struct mlxsw_sx_port {
	struct net_device *dev;
	struct mlxsw_sx_port_pcpu_stats __percpu *pcpu_stats;
	struct mlxsw_sx *mlxsw_sx;
	u8 local_port;
	struct {
		u8 module;
	} mapping;
};

/* tx_hdr_version
 * Tx header version.
 * Must be set to 0.
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

/* tx_hdr_etclass
 * Egress TClass to be used on the egress device on the egress port.
 * The MSB is specified in the 'ctclass3' field.
 * Range is 0-15, where 15 is the highest priority.
 */
MLXSW_ITEM32(tx, hdr, etclass, 0x00, 18, 3);

/* tx_hdr_swid
 * Switch partition ID.
 */
MLXSW_ITEM32(tx, hdr, swid, 0x00, 12, 3);

/* tx_hdr_port_mid
 * Destination local port for unicast packets.
 * Destination multicast ID for multicast packets.
 *
 * Control packets are directed to a specific egress port, while data
 * packets are transmitted through the CPU port (0) into the switch partition,
 * where forwarding rules are applied.
 */
MLXSW_ITEM32(tx, hdr, port_mid, 0x04, 16, 16);

/* tx_hdr_ctclass3
 * See field 'etclass'.
 */
MLXSW_ITEM32(tx, hdr, ctclass3, 0x04, 14, 1);

/* tx_hdr_rdq
 * RDQ for control packets sent to remote CPU.
 * Must be set to 0x1F for EMADs, otherwise 0.
 */
MLXSW_ITEM32(tx, hdr, rdq, 0x04, 9, 5);

/* tx_hdr_cpu_sig
 * Signature control for packets going to CPU. Must be set to 0.
 */
MLXSW_ITEM32(tx, hdr, cpu_sig, 0x04, 0, 9);

/* tx_hdr_sig
 * Stacking protocl signature. Must be set to 0xE0E0.
 */
MLXSW_ITEM32(tx, hdr, sig, 0x0C, 16, 16);

/* tx_hdr_stclass
 * Stacking TClass.
 */
MLXSW_ITEM32(tx, hdr, stclass, 0x0C, 13, 3);

/* tx_hdr_emad
 * EMAD bit. Must be set for EMADs.
 */
MLXSW_ITEM32(tx, hdr, emad, 0x0C, 5, 1);

/* tx_hdr_type
 * 0 - Data packets
 * 6 - Control packets
 */
MLXSW_ITEM32(tx, hdr, type, 0x0C, 0, 4);

static void mlxsw_sx_txhdr_construct(struct sk_buff *skb,
				     const struct mlxsw_tx_info *tx_info)
{
	char *txhdr = skb_push(skb, MLXSW_TXHDR_LEN);
	bool is_emad = tx_info->is_emad;

	memset(txhdr, 0, MLXSW_TXHDR_LEN);

	/* We currently set default values for the egress tclass (QoS). */
	mlxsw_tx_hdr_version_set(txhdr, MLXSW_TXHDR_VERSION_0);
	mlxsw_tx_hdr_ctl_set(txhdr, MLXSW_TXHDR_ETH_CTL);
	mlxsw_tx_hdr_proto_set(txhdr, MLXSW_TXHDR_PROTO_ETH);
	mlxsw_tx_hdr_etclass_set(txhdr, is_emad ? MLXSW_TXHDR_ETCLASS_6 :
						  MLXSW_TXHDR_ETCLASS_5);
	mlxsw_tx_hdr_swid_set(txhdr, 0);
	mlxsw_tx_hdr_port_mid_set(txhdr, tx_info->local_port);
	mlxsw_tx_hdr_ctclass3_set(txhdr, MLXSW_TXHDR_CTCLASS3);
	mlxsw_tx_hdr_rdq_set(txhdr, is_emad ? MLXSW_TXHDR_RDQ_EMAD :
					      MLXSW_TXHDR_RDQ_OTHER);
	mlxsw_tx_hdr_cpu_sig_set(txhdr, MLXSW_TXHDR_CPU_SIG);
	mlxsw_tx_hdr_sig_set(txhdr, MLXSW_TXHDR_SIG);
	mlxsw_tx_hdr_stclass_set(txhdr, MLXSW_TXHDR_STCLASS_NONE);
	mlxsw_tx_hdr_emad_set(txhdr, is_emad ? MLXSW_TXHDR_EMAD :
					       MLXSW_TXHDR_NOT_EMAD);
	mlxsw_tx_hdr_type_set(txhdr, MLXSW_TXHDR_TYPE_CONTROL);
}

static int mlxsw_sx_port_admin_status_set(struct mlxsw_sx_port *mlxsw_sx_port,
					  bool is_up)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char paos_pl[MLXSW_REG_PAOS_LEN];

	mlxsw_reg_paos_pack(paos_pl, mlxsw_sx_port->local_port,
			    is_up ? MLXSW_PORT_ADMIN_STATUS_UP :
			    MLXSW_PORT_ADMIN_STATUS_DOWN);
	return mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(paos), paos_pl);
}

static int mlxsw_sx_port_oper_status_get(struct mlxsw_sx_port *mlxsw_sx_port,
					 bool *p_is_up)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char paos_pl[MLXSW_REG_PAOS_LEN];
	u8 oper_status;
	int err;

	mlxsw_reg_paos_pack(paos_pl, mlxsw_sx_port->local_port, 0);
	err = mlxsw_reg_query(mlxsw_sx->core, MLXSW_REG(paos), paos_pl);
	if (err)
		return err;
	oper_status = mlxsw_reg_paos_oper_status_get(paos_pl);
	*p_is_up = oper_status == MLXSW_PORT_ADMIN_STATUS_UP;
	return 0;
}

static int __mlxsw_sx_port_mtu_set(struct mlxsw_sx_port *mlxsw_sx_port,
				   u16 mtu)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char pmtu_pl[MLXSW_REG_PMTU_LEN];
	int max_mtu;
	int err;

	mlxsw_reg_pmtu_pack(pmtu_pl, mlxsw_sx_port->local_port, 0);
	err = mlxsw_reg_query(mlxsw_sx->core, MLXSW_REG(pmtu), pmtu_pl);
	if (err)
		return err;
	max_mtu = mlxsw_reg_pmtu_max_mtu_get(pmtu_pl);

	if (mtu > max_mtu)
		return -EINVAL;

	mlxsw_reg_pmtu_pack(pmtu_pl, mlxsw_sx_port->local_port, mtu);
	return mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(pmtu), pmtu_pl);
}

static int mlxsw_sx_port_mtu_eth_set(struct mlxsw_sx_port *mlxsw_sx_port,
				     u16 mtu)
{
	mtu += MLXSW_TXHDR_LEN + ETH_HLEN;
	return __mlxsw_sx_port_mtu_set(mlxsw_sx_port, mtu);
}

static int mlxsw_sx_port_mtu_ib_set(struct mlxsw_sx_port *mlxsw_sx_port,
				    u16 mtu)
{
	return __mlxsw_sx_port_mtu_set(mlxsw_sx_port, mtu);
}

static int mlxsw_sx_port_ib_port_set(struct mlxsw_sx_port *mlxsw_sx_port,
				     u8 ib_port)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char plib_pl[MLXSW_REG_PLIB_LEN] = {0};
	int err;

	mlxsw_reg_plib_local_port_set(plib_pl, mlxsw_sx_port->local_port);
	mlxsw_reg_plib_ib_port_set(plib_pl, ib_port);
	err = mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(plib), plib_pl);
	return err;
}

static int mlxsw_sx_port_swid_set(struct mlxsw_sx_port *mlxsw_sx_port, u8 swid)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char pspa_pl[MLXSW_REG_PSPA_LEN];

	mlxsw_reg_pspa_pack(pspa_pl, swid, mlxsw_sx_port->local_port);
	return mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(pspa), pspa_pl);
}

static int
mlxsw_sx_port_system_port_mapping_set(struct mlxsw_sx_port *mlxsw_sx_port)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char sspr_pl[MLXSW_REG_SSPR_LEN];

	mlxsw_reg_sspr_pack(sspr_pl, mlxsw_sx_port->local_port);
	return mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(sspr), sspr_pl);
}

static int mlxsw_sx_port_module_info_get(struct mlxsw_sx *mlxsw_sx,
					 u8 local_port, u8 *p_module,
					 u8 *p_width)
{
	char pmlp_pl[MLXSW_REG_PMLP_LEN];
	int err;

	mlxsw_reg_pmlp_pack(pmlp_pl, local_port);
	err = mlxsw_reg_query(mlxsw_sx->core, MLXSW_REG(pmlp), pmlp_pl);
	if (err)
		return err;
	*p_module = mlxsw_reg_pmlp_module_get(pmlp_pl, 0);
	*p_width = mlxsw_reg_pmlp_width_get(pmlp_pl);
	return 0;
}

static int mlxsw_sx_port_open(struct net_device *dev)
{
	struct mlxsw_sx_port *mlxsw_sx_port = netdev_priv(dev);
	int err;

	err = mlxsw_sx_port_admin_status_set(mlxsw_sx_port, true);
	if (err)
		return err;
	netif_start_queue(dev);
	return 0;
}

static int mlxsw_sx_port_stop(struct net_device *dev)
{
	struct mlxsw_sx_port *mlxsw_sx_port = netdev_priv(dev);

	netif_stop_queue(dev);
	return mlxsw_sx_port_admin_status_set(mlxsw_sx_port, false);
}

static netdev_tx_t mlxsw_sx_port_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct mlxsw_sx_port *mlxsw_sx_port = netdev_priv(dev);
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	struct mlxsw_sx_port_pcpu_stats *pcpu_stats;
	const struct mlxsw_tx_info tx_info = {
		.local_port = mlxsw_sx_port->local_port,
		.is_emad = false,
	};
	u64 len;
	int err;

	if (skb_cow_head(skb, MLXSW_TXHDR_LEN)) {
		this_cpu_inc(mlxsw_sx_port->pcpu_stats->tx_dropped);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	memset(skb->cb, 0, sizeof(struct mlxsw_skb_cb));

	if (mlxsw_core_skb_transmit_busy(mlxsw_sx->core, &tx_info))
		return NETDEV_TX_BUSY;

	mlxsw_sx_txhdr_construct(skb, &tx_info);
	/* TX header is consumed by HW on the way so we shouldn't count its
	 * bytes as being sent.
	 */
	len = skb->len - MLXSW_TXHDR_LEN;
	/* Due to a race we might fail here because of a full queue. In that
	 * unlikely case we simply drop the packet.
	 */
	err = mlxsw_core_skb_transmit(mlxsw_sx->core, skb, &tx_info);

	if (!err) {
		pcpu_stats = this_cpu_ptr(mlxsw_sx_port->pcpu_stats);
		u64_stats_update_begin(&pcpu_stats->syncp);
		pcpu_stats->tx_packets++;
		pcpu_stats->tx_bytes += len;
		u64_stats_update_end(&pcpu_stats->syncp);
	} else {
		this_cpu_inc(mlxsw_sx_port->pcpu_stats->tx_dropped);
		dev_kfree_skb_any(skb);
	}
	return NETDEV_TX_OK;
}

static int mlxsw_sx_port_change_mtu(struct net_device *dev, int mtu)
{
	struct mlxsw_sx_port *mlxsw_sx_port = netdev_priv(dev);
	int err;

	err = mlxsw_sx_port_mtu_eth_set(mlxsw_sx_port, mtu);
	if (err)
		return err;
	dev->mtu = mtu;
	return 0;
}

static void
mlxsw_sx_port_get_stats64(struct net_device *dev,
			  struct rtnl_link_stats64 *stats)
{
	struct mlxsw_sx_port *mlxsw_sx_port = netdev_priv(dev);
	struct mlxsw_sx_port_pcpu_stats *p;
	u64 rx_packets, rx_bytes, tx_packets, tx_bytes;
	u32 tx_dropped = 0;
	unsigned int start;
	int i;

	for_each_possible_cpu(i) {
		p = per_cpu_ptr(mlxsw_sx_port->pcpu_stats, i);
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
}

static struct devlink_port *
mlxsw_sx_port_get_devlink_port(struct net_device *dev)
{
	struct mlxsw_sx_port *mlxsw_sx_port = netdev_priv(dev);
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;

	return mlxsw_core_port_devlink_port_get(mlxsw_sx->core,
						mlxsw_sx_port->local_port);
}

static const struct net_device_ops mlxsw_sx_port_netdev_ops = {
	.ndo_open		= mlxsw_sx_port_open,
	.ndo_stop		= mlxsw_sx_port_stop,
	.ndo_start_xmit		= mlxsw_sx_port_xmit,
	.ndo_change_mtu		= mlxsw_sx_port_change_mtu,
	.ndo_get_stats64	= mlxsw_sx_port_get_stats64,
	.ndo_get_devlink_port	= mlxsw_sx_port_get_devlink_port,
};

static void mlxsw_sx_port_get_drvinfo(struct net_device *dev,
				      struct ethtool_drvinfo *drvinfo)
{
	struct mlxsw_sx_port *mlxsw_sx_port = netdev_priv(dev);
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;

	strlcpy(drvinfo->driver, mlxsw_sx_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, mlxsw_sx_driver_version,
		sizeof(drvinfo->version));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%d.%d.%d",
		 mlxsw_sx->bus_info->fw_rev.major,
		 mlxsw_sx->bus_info->fw_rev.minor,
		 mlxsw_sx->bus_info->fw_rev.subminor);
	strlcpy(drvinfo->bus_info, mlxsw_sx->bus_info->device_name,
		sizeof(drvinfo->bus_info));
}

struct mlxsw_sx_port_hw_stats {
	char str[ETH_GSTRING_LEN];
	u64 (*getter)(const char *payload);
};

static const struct mlxsw_sx_port_hw_stats mlxsw_sx_port_hw_stats[] = {
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

#define MLXSW_SX_PORT_HW_STATS_LEN ARRAY_SIZE(mlxsw_sx_port_hw_stats)

static void mlxsw_sx_port_get_strings(struct net_device *dev,
				      u32 stringset, u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < MLXSW_SX_PORT_HW_STATS_LEN; i++) {
			memcpy(p, mlxsw_sx_port_hw_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static void mlxsw_sx_port_get_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct mlxsw_sx_port *mlxsw_sx_port = netdev_priv(dev);
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];
	int i;
	int err;

	mlxsw_reg_ppcnt_pack(ppcnt_pl, mlxsw_sx_port->local_port,
			     MLXSW_REG_PPCNT_IEEE_8023_CNT, 0);
	err = mlxsw_reg_query(mlxsw_sx->core, MLXSW_REG(ppcnt), ppcnt_pl);
	for (i = 0; i < MLXSW_SX_PORT_HW_STATS_LEN; i++)
		data[i] = !err ? mlxsw_sx_port_hw_stats[i].getter(ppcnt_pl) : 0;
}

static int mlxsw_sx_port_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return MLXSW_SX_PORT_HW_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

struct mlxsw_sx_port_link_mode {
	u32 mask;
	u32 supported;
	u32 advertised;
	u32 speed;
};

static const struct mlxsw_sx_port_link_mode mlxsw_sx_port_link_mode[] = {
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
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_CR4 |
				  MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4 |
				  MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4 |
				  MLXSW_REG_PTYS_ETH_SPEED_100GBASE_LR4_ER4,
		.speed		= 100000,
	},
};

#define MLXSW_SX_PORT_LINK_MODE_LEN ARRAY_SIZE(mlxsw_sx_port_link_mode)
#define MLXSW_SX_PORT_BASE_SPEED 10000 /* Mb/s */

static u32 mlxsw_sx_from_ptys_supported_port(u32 ptys_eth_proto)
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

static u32 mlxsw_sx_from_ptys_supported_link(u32 ptys_eth_proto)
{
	u32 modes = 0;
	int i;

	for (i = 0; i < MLXSW_SX_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sx_port_link_mode[i].mask)
			modes |= mlxsw_sx_port_link_mode[i].supported;
	}
	return modes;
}

static u32 mlxsw_sx_from_ptys_advert_link(u32 ptys_eth_proto)
{
	u32 modes = 0;
	int i;

	for (i = 0; i < MLXSW_SX_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sx_port_link_mode[i].mask)
			modes |= mlxsw_sx_port_link_mode[i].advertised;
	}
	return modes;
}

static void mlxsw_sx_from_ptys_speed_duplex(bool carrier_ok, u32 ptys_eth_proto,
					    struct ethtool_link_ksettings *cmd)
{
	u32 speed = SPEED_UNKNOWN;
	u8 duplex = DUPLEX_UNKNOWN;
	int i;

	if (!carrier_ok)
		goto out;

	for (i = 0; i < MLXSW_SX_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sx_port_link_mode[i].mask) {
			speed = mlxsw_sx_port_link_mode[i].speed;
			duplex = DUPLEX_FULL;
			break;
		}
	}
out:
	cmd->base.speed = speed;
	cmd->base.duplex = duplex;
}

static u8 mlxsw_sx_port_connector_port(u32 ptys_eth_proto)
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

static int
mlxsw_sx_port_get_link_ksettings(struct net_device *dev,
				 struct ethtool_link_ksettings *cmd)
{
	struct mlxsw_sx_port *mlxsw_sx_port = netdev_priv(dev);
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u32 eth_proto_cap;
	u32 eth_proto_admin;
	u32 eth_proto_oper;
	u32 supported, advertising, lp_advertising;
	int err;

	mlxsw_reg_ptys_eth_pack(ptys_pl, mlxsw_sx_port->local_port, 0, false);
	err = mlxsw_reg_query(mlxsw_sx->core, MLXSW_REG(ptys), ptys_pl);
	if (err) {
		netdev_err(dev, "Failed to get proto");
		return err;
	}
	mlxsw_reg_ptys_eth_unpack(ptys_pl, &eth_proto_cap,
				  &eth_proto_admin, &eth_proto_oper);

	supported = mlxsw_sx_from_ptys_supported_port(eth_proto_cap) |
			 mlxsw_sx_from_ptys_supported_link(eth_proto_cap) |
			 SUPPORTED_Pause | SUPPORTED_Asym_Pause;
	advertising = mlxsw_sx_from_ptys_advert_link(eth_proto_admin);
	mlxsw_sx_from_ptys_speed_duplex(netif_carrier_ok(dev),
					eth_proto_oper, cmd);

	eth_proto_oper = eth_proto_oper ? eth_proto_oper : eth_proto_cap;
	cmd->base.port = mlxsw_sx_port_connector_port(eth_proto_oper);
	lp_advertising = mlxsw_sx_from_ptys_advert_link(eth_proto_oper);

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.lp_advertising,
						lp_advertising);

	return 0;
}

static u32 mlxsw_sx_to_ptys_advert_link(u32 advertising)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SX_PORT_LINK_MODE_LEN; i++) {
		if (advertising & mlxsw_sx_port_link_mode[i].advertised)
			ptys_proto |= mlxsw_sx_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static u32 mlxsw_sx_to_ptys_speed(u32 speed)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SX_PORT_LINK_MODE_LEN; i++) {
		if (speed == mlxsw_sx_port_link_mode[i].speed)
			ptys_proto |= mlxsw_sx_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static u32 mlxsw_sx_to_ptys_upper_speed(u32 upper_speed)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SX_PORT_LINK_MODE_LEN; i++) {
		if (mlxsw_sx_port_link_mode[i].speed <= upper_speed)
			ptys_proto |= mlxsw_sx_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static int
mlxsw_sx_port_set_link_ksettings(struct net_device *dev,
				 const struct ethtool_link_ksettings *cmd)
{
	struct mlxsw_sx_port *mlxsw_sx_port = netdev_priv(dev);
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u32 speed;
	u32 eth_proto_new;
	u32 eth_proto_cap;
	u32 eth_proto_admin;
	u32 advertising;
	bool is_up;
	int err;

	speed = cmd->base.speed;

	ethtool_convert_link_mode_to_legacy_u32(&advertising,
						cmd->link_modes.advertising);

	eth_proto_new = cmd->base.autoneg == AUTONEG_ENABLE ?
		mlxsw_sx_to_ptys_advert_link(advertising) :
		mlxsw_sx_to_ptys_speed(speed);

	mlxsw_reg_ptys_eth_pack(ptys_pl, mlxsw_sx_port->local_port, 0, false);
	err = mlxsw_reg_query(mlxsw_sx->core, MLXSW_REG(ptys), ptys_pl);
	if (err) {
		netdev_err(dev, "Failed to get proto");
		return err;
	}
	mlxsw_reg_ptys_eth_unpack(ptys_pl, &eth_proto_cap, &eth_proto_admin,
				  NULL);

	eth_proto_new = eth_proto_new & eth_proto_cap;
	if (!eth_proto_new) {
		netdev_err(dev, "Not supported proto admin requested");
		return -EINVAL;
	}
	if (eth_proto_new == eth_proto_admin)
		return 0;

	mlxsw_reg_ptys_eth_pack(ptys_pl, mlxsw_sx_port->local_port,
				eth_proto_new, true);
	err = mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(ptys), ptys_pl);
	if (err) {
		netdev_err(dev, "Failed to set proto admin");
		return err;
	}

	err = mlxsw_sx_port_oper_status_get(mlxsw_sx_port, &is_up);
	if (err) {
		netdev_err(dev, "Failed to get oper status");
		return err;
	}
	if (!is_up)
		return 0;

	err = mlxsw_sx_port_admin_status_set(mlxsw_sx_port, false);
	if (err) {
		netdev_err(dev, "Failed to set admin status");
		return err;
	}

	err = mlxsw_sx_port_admin_status_set(mlxsw_sx_port, true);
	if (err) {
		netdev_err(dev, "Failed to set admin status");
		return err;
	}

	return 0;
}

static const struct ethtool_ops mlxsw_sx_port_ethtool_ops = {
	.get_drvinfo		= mlxsw_sx_port_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_strings		= mlxsw_sx_port_get_strings,
	.get_ethtool_stats	= mlxsw_sx_port_get_stats,
	.get_sset_count		= mlxsw_sx_port_get_sset_count,
	.get_link_ksettings	= mlxsw_sx_port_get_link_ksettings,
	.set_link_ksettings	= mlxsw_sx_port_set_link_ksettings,
};

static int mlxsw_sx_hw_id_get(struct mlxsw_sx *mlxsw_sx)
{
	char spad_pl[MLXSW_REG_SPAD_LEN] = {0};
	int err;

	err = mlxsw_reg_query(mlxsw_sx->core, MLXSW_REG(spad), spad_pl);
	if (err)
		return err;
	mlxsw_reg_spad_base_mac_memcpy_from(spad_pl, mlxsw_sx->hw_id);
	return 0;
}

static int mlxsw_sx_port_dev_addr_get(struct mlxsw_sx_port *mlxsw_sx_port)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	struct net_device *dev = mlxsw_sx_port->dev;
	char ppad_pl[MLXSW_REG_PPAD_LEN];
	int err;

	mlxsw_reg_ppad_pack(ppad_pl, false, 0);
	err = mlxsw_reg_query(mlxsw_sx->core, MLXSW_REG(ppad), ppad_pl);
	if (err)
		return err;
	mlxsw_reg_ppad_mac_memcpy_from(ppad_pl, dev->dev_addr);
	/* The last byte value in base mac address is guaranteed
	 * to be such it does not overflow when adding local_port
	 * value.
	 */
	dev->dev_addr[ETH_ALEN - 1] += mlxsw_sx_port->local_port;
	return 0;
}

static int mlxsw_sx_port_stp_state_set(struct mlxsw_sx_port *mlxsw_sx_port,
				       u16 vid, enum mlxsw_reg_spms_state state)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char *spms_pl;
	int err;

	spms_pl = kmalloc(MLXSW_REG_SPMS_LEN, GFP_KERNEL);
	if (!spms_pl)
		return -ENOMEM;
	mlxsw_reg_spms_pack(spms_pl, mlxsw_sx_port->local_port);
	mlxsw_reg_spms_vid_pack(spms_pl, vid, state);
	err = mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(spms), spms_pl);
	kfree(spms_pl);
	return err;
}

static int mlxsw_sx_port_ib_speed_set(struct mlxsw_sx_port *mlxsw_sx_port,
				      u16 speed, u16 width)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char ptys_pl[MLXSW_REG_PTYS_LEN];

	mlxsw_reg_ptys_ib_pack(ptys_pl, mlxsw_sx_port->local_port, speed,
			       width);
	return mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(ptys), ptys_pl);
}

static int
mlxsw_sx_port_speed_by_width_set(struct mlxsw_sx_port *mlxsw_sx_port, u8 width)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	u32 upper_speed = MLXSW_SX_PORT_BASE_SPEED * width;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u32 eth_proto_admin;

	eth_proto_admin = mlxsw_sx_to_ptys_upper_speed(upper_speed);
	mlxsw_reg_ptys_eth_pack(ptys_pl, mlxsw_sx_port->local_port,
				eth_proto_admin, true);
	return mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(ptys), ptys_pl);
}

static int
mlxsw_sx_port_mac_learning_mode_set(struct mlxsw_sx_port *mlxsw_sx_port,
				    enum mlxsw_reg_spmlr_learn_mode mode)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_sx_port->mlxsw_sx;
	char spmlr_pl[MLXSW_REG_SPMLR_LEN];

	mlxsw_reg_spmlr_pack(spmlr_pl, mlxsw_sx_port->local_port, mode);
	return mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(spmlr), spmlr_pl);
}

static int __mlxsw_sx_port_eth_create(struct mlxsw_sx *mlxsw_sx, u8 local_port,
				      u8 module, u8 width)
{
	struct mlxsw_sx_port *mlxsw_sx_port;
	struct net_device *dev;
	int err;

	dev = alloc_etherdev(sizeof(struct mlxsw_sx_port));
	if (!dev)
		return -ENOMEM;
	SET_NETDEV_DEV(dev, mlxsw_sx->bus_info->dev);
	dev_net_set(dev, mlxsw_core_net(mlxsw_sx->core));
	mlxsw_sx_port = netdev_priv(dev);
	mlxsw_sx_port->dev = dev;
	mlxsw_sx_port->mlxsw_sx = mlxsw_sx;
	mlxsw_sx_port->local_port = local_port;
	mlxsw_sx_port->mapping.module = module;

	mlxsw_sx_port->pcpu_stats =
		netdev_alloc_pcpu_stats(struct mlxsw_sx_port_pcpu_stats);
	if (!mlxsw_sx_port->pcpu_stats) {
		err = -ENOMEM;
		goto err_alloc_stats;
	}

	dev->netdev_ops = &mlxsw_sx_port_netdev_ops;
	dev->ethtool_ops = &mlxsw_sx_port_ethtool_ops;

	err = mlxsw_sx_port_dev_addr_get(mlxsw_sx_port);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Unable to get port mac address\n",
			mlxsw_sx_port->local_port);
		goto err_dev_addr_get;
	}

	netif_carrier_off(dev);

	dev->features |= NETIF_F_NETNS_LOCAL | NETIF_F_LLTX | NETIF_F_SG |
			 NETIF_F_VLAN_CHALLENGED;

	dev->min_mtu = 0;
	dev->max_mtu = ETH_MAX_MTU;

	/* Each packet needs to have a Tx header (metadata) on top all other
	 * headers.
	 */
	dev->needed_headroom = MLXSW_TXHDR_LEN;

	err = mlxsw_sx_port_system_port_mapping_set(mlxsw_sx_port);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to set system port mapping\n",
			mlxsw_sx_port->local_port);
		goto err_port_system_port_mapping_set;
	}

	err = mlxsw_sx_port_swid_set(mlxsw_sx_port, 0);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to set SWID\n",
			mlxsw_sx_port->local_port);
		goto err_port_swid_set;
	}

	err = mlxsw_sx_port_speed_by_width_set(mlxsw_sx_port, width);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to set speed\n",
			mlxsw_sx_port->local_port);
		goto err_port_speed_set;
	}

	err = mlxsw_sx_port_mtu_eth_set(mlxsw_sx_port, ETH_DATA_LEN);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to set MTU\n",
			mlxsw_sx_port->local_port);
		goto err_port_mtu_set;
	}

	err = mlxsw_sx_port_admin_status_set(mlxsw_sx_port, false);
	if (err)
		goto err_port_admin_status_set;

	err = mlxsw_sx_port_stp_state_set(mlxsw_sx_port,
					  MLXSW_PORT_DEFAULT_VID,
					  MLXSW_REG_SPMS_STATE_FORWARDING);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to set STP state\n",
			mlxsw_sx_port->local_port);
		goto err_port_stp_state_set;
	}

	err = mlxsw_sx_port_mac_learning_mode_set(mlxsw_sx_port,
						  MLXSW_REG_SPMLR_LEARN_MODE_DISABLE);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to set MAC learning mode\n",
			mlxsw_sx_port->local_port);
		goto err_port_mac_learning_mode_set;
	}

	err = register_netdev(dev);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to register netdev\n",
			mlxsw_sx_port->local_port);
		goto err_register_netdev;
	}

	mlxsw_core_port_eth_set(mlxsw_sx->core, mlxsw_sx_port->local_port,
				mlxsw_sx_port, dev);
	mlxsw_sx->ports[local_port] = mlxsw_sx_port;
	return 0;

err_register_netdev:
err_port_mac_learning_mode_set:
err_port_stp_state_set:
err_port_admin_status_set:
err_port_mtu_set:
err_port_speed_set:
	mlxsw_sx_port_swid_set(mlxsw_sx_port, MLXSW_PORT_SWID_DISABLED_PORT);
err_port_swid_set:
err_port_system_port_mapping_set:
err_dev_addr_get:
	free_percpu(mlxsw_sx_port->pcpu_stats);
err_alloc_stats:
	free_netdev(dev);
	return err;
}

static int mlxsw_sx_port_eth_create(struct mlxsw_sx *mlxsw_sx, u8 local_port,
				    u8 module, u8 width)
{
	int err;

	err = mlxsw_core_port_init(mlxsw_sx->core, local_port,
				   module + 1, false, 0,
				   mlxsw_sx->hw_id, sizeof(mlxsw_sx->hw_id));
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to init core port\n",
			local_port);
		return err;
	}
	err = __mlxsw_sx_port_eth_create(mlxsw_sx, local_port, module, width);
	if (err)
		goto err_port_create;

	return 0;

err_port_create:
	mlxsw_core_port_fini(mlxsw_sx->core, local_port);
	return err;
}

static void __mlxsw_sx_port_eth_remove(struct mlxsw_sx *mlxsw_sx, u8 local_port)
{
	struct mlxsw_sx_port *mlxsw_sx_port = mlxsw_sx->ports[local_port];

	mlxsw_core_port_clear(mlxsw_sx->core, local_port, mlxsw_sx);
	unregister_netdev(mlxsw_sx_port->dev); /* This calls ndo_stop */
	mlxsw_sx->ports[local_port] = NULL;
	mlxsw_sx_port_swid_set(mlxsw_sx_port, MLXSW_PORT_SWID_DISABLED_PORT);
	free_percpu(mlxsw_sx_port->pcpu_stats);
	free_netdev(mlxsw_sx_port->dev);
}

static bool mlxsw_sx_port_created(struct mlxsw_sx *mlxsw_sx, u8 local_port)
{
	return mlxsw_sx->ports[local_port] != NULL;
}

static int __mlxsw_sx_port_ib_create(struct mlxsw_sx *mlxsw_sx, u8 local_port,
				     u8 module, u8 width)
{
	struct mlxsw_sx_port *mlxsw_sx_port;
	int err;

	mlxsw_sx_port = kzalloc(sizeof(*mlxsw_sx_port), GFP_KERNEL);
	if (!mlxsw_sx_port)
		return -ENOMEM;
	mlxsw_sx_port->mlxsw_sx = mlxsw_sx;
	mlxsw_sx_port->local_port = local_port;
	mlxsw_sx_port->mapping.module = module;

	err = mlxsw_sx_port_system_port_mapping_set(mlxsw_sx_port);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to set system port mapping\n",
			mlxsw_sx_port->local_port);
		goto err_port_system_port_mapping_set;
	}

	/* Adding port to Infiniband swid (1) */
	err = mlxsw_sx_port_swid_set(mlxsw_sx_port, 1);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to set SWID\n",
			mlxsw_sx_port->local_port);
		goto err_port_swid_set;
	}

	/* Expose the IB port number as it's front panel name */
	err = mlxsw_sx_port_ib_port_set(mlxsw_sx_port, module + 1);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to set IB port\n",
			mlxsw_sx_port->local_port);
		goto err_port_ib_set;
	}

	/* Supports all speeds from SDR to FDR (bitmask) and support bus width
	 * of 1x, 2x and 4x (3 bits bitmask)
	 */
	err = mlxsw_sx_port_ib_speed_set(mlxsw_sx_port,
					 MLXSW_REG_PTYS_IB_SPEED_EDR - 1,
					 BIT(3) - 1);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to set speed\n",
			mlxsw_sx_port->local_port);
		goto err_port_speed_set;
	}

	/* Change to the maximum MTU the device supports, the SMA will take
	 * care of the active MTU
	 */
	err = mlxsw_sx_port_mtu_ib_set(mlxsw_sx_port, MLXSW_IB_DEFAULT_MTU);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to set MTU\n",
			mlxsw_sx_port->local_port);
		goto err_port_mtu_set;
	}

	err = mlxsw_sx_port_admin_status_set(mlxsw_sx_port, true);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Port %d: Failed to change admin state to UP\n",
			mlxsw_sx_port->local_port);
		goto err_port_admin_set;
	}

	mlxsw_core_port_ib_set(mlxsw_sx->core, mlxsw_sx_port->local_port,
			       mlxsw_sx_port);
	mlxsw_sx->ports[local_port] = mlxsw_sx_port;
	return 0;

err_port_admin_set:
err_port_mtu_set:
err_port_speed_set:
err_port_ib_set:
	mlxsw_sx_port_swid_set(mlxsw_sx_port, MLXSW_PORT_SWID_DISABLED_PORT);
err_port_swid_set:
err_port_system_port_mapping_set:
	kfree(mlxsw_sx_port);
	return err;
}

static void __mlxsw_sx_port_ib_remove(struct mlxsw_sx *mlxsw_sx, u8 local_port)
{
	struct mlxsw_sx_port *mlxsw_sx_port = mlxsw_sx->ports[local_port];

	mlxsw_core_port_clear(mlxsw_sx->core, local_port, mlxsw_sx);
	mlxsw_sx->ports[local_port] = NULL;
	mlxsw_sx_port_admin_status_set(mlxsw_sx_port, false);
	mlxsw_sx_port_swid_set(mlxsw_sx_port, MLXSW_PORT_SWID_DISABLED_PORT);
	kfree(mlxsw_sx_port);
}

static void __mlxsw_sx_port_remove(struct mlxsw_sx *mlxsw_sx, u8 local_port)
{
	enum devlink_port_type port_type =
		mlxsw_core_port_type_get(mlxsw_sx->core, local_port);

	if (port_type == DEVLINK_PORT_TYPE_ETH)
		__mlxsw_sx_port_eth_remove(mlxsw_sx, local_port);
	else if (port_type == DEVLINK_PORT_TYPE_IB)
		__mlxsw_sx_port_ib_remove(mlxsw_sx, local_port);
}

static void mlxsw_sx_port_remove(struct mlxsw_sx *mlxsw_sx, u8 local_port)
{
	__mlxsw_sx_port_remove(mlxsw_sx, local_port);
	mlxsw_core_port_fini(mlxsw_sx->core, local_port);
}

static void mlxsw_sx_ports_remove(struct mlxsw_sx *mlxsw_sx)
{
	int i;

	for (i = 1; i < mlxsw_core_max_ports(mlxsw_sx->core); i++)
		if (mlxsw_sx_port_created(mlxsw_sx, i))
			mlxsw_sx_port_remove(mlxsw_sx, i);
	kfree(mlxsw_sx->ports);
	mlxsw_sx->ports = NULL;
}

static int mlxsw_sx_ports_create(struct mlxsw_sx *mlxsw_sx)
{
	unsigned int max_ports = mlxsw_core_max_ports(mlxsw_sx->core);
	size_t alloc_size;
	u8 module, width;
	int i;
	int err;

	alloc_size = sizeof(struct mlxsw_sx_port *) * max_ports;
	mlxsw_sx->ports = kzalloc(alloc_size, GFP_KERNEL);
	if (!mlxsw_sx->ports)
		return -ENOMEM;

	for (i = 1; i < max_ports; i++) {
		err = mlxsw_sx_port_module_info_get(mlxsw_sx, i, &module,
						    &width);
		if (err)
			goto err_port_module_info_get;
		if (!width)
			continue;
		err = mlxsw_sx_port_eth_create(mlxsw_sx, i, module, width);
		if (err)
			goto err_port_create;
	}
	return 0;

err_port_create:
err_port_module_info_get:
	for (i--; i >= 1; i--)
		if (mlxsw_sx_port_created(mlxsw_sx, i))
			mlxsw_sx_port_remove(mlxsw_sx, i);
	kfree(mlxsw_sx->ports);
	mlxsw_sx->ports = NULL;
	return err;
}

static void mlxsw_sx_pude_eth_event_func(struct mlxsw_sx_port *mlxsw_sx_port,
					 enum mlxsw_reg_pude_oper_status status)
{
	if (status == MLXSW_PORT_OPER_STATUS_UP) {
		netdev_info(mlxsw_sx_port->dev, "link up\n");
		netif_carrier_on(mlxsw_sx_port->dev);
	} else {
		netdev_info(mlxsw_sx_port->dev, "link down\n");
		netif_carrier_off(mlxsw_sx_port->dev);
	}
}

static void mlxsw_sx_pude_ib_event_func(struct mlxsw_sx_port *mlxsw_sx_port,
					enum mlxsw_reg_pude_oper_status status)
{
	if (status == MLXSW_PORT_OPER_STATUS_UP)
		pr_info("ib link for port %d - up\n",
			mlxsw_sx_port->mapping.module + 1);
	else
		pr_info("ib link for port %d - down\n",
			mlxsw_sx_port->mapping.module + 1);
}

static void mlxsw_sx_pude_event_func(const struct mlxsw_reg_info *reg,
				     char *pude_pl, void *priv)
{
	struct mlxsw_sx *mlxsw_sx = priv;
	struct mlxsw_sx_port *mlxsw_sx_port;
	enum mlxsw_reg_pude_oper_status status;
	enum devlink_port_type port_type;
	u8 local_port;

	local_port = mlxsw_reg_pude_local_port_get(pude_pl);
	mlxsw_sx_port = mlxsw_sx->ports[local_port];
	if (!mlxsw_sx_port) {
		dev_warn(mlxsw_sx->bus_info->dev, "Port %d: Link event received for non-existent port\n",
			 local_port);
		return;
	}

	status = mlxsw_reg_pude_oper_status_get(pude_pl);
	port_type = mlxsw_core_port_type_get(mlxsw_sx->core, local_port);
	if (port_type == DEVLINK_PORT_TYPE_ETH)
		mlxsw_sx_pude_eth_event_func(mlxsw_sx_port, status);
	else if (port_type == DEVLINK_PORT_TYPE_IB)
		mlxsw_sx_pude_ib_event_func(mlxsw_sx_port, status);
}

static void mlxsw_sx_rx_listener_func(struct sk_buff *skb, u8 local_port,
				      void *priv)
{
	struct mlxsw_sx *mlxsw_sx = priv;
	struct mlxsw_sx_port *mlxsw_sx_port = mlxsw_sx->ports[local_port];
	struct mlxsw_sx_port_pcpu_stats *pcpu_stats;

	if (unlikely(!mlxsw_sx_port)) {
		dev_warn_ratelimited(mlxsw_sx->bus_info->dev, "Port %d: skb received for non-existent port\n",
				     local_port);
		return;
	}

	skb->dev = mlxsw_sx_port->dev;

	pcpu_stats = this_cpu_ptr(mlxsw_sx_port->pcpu_stats);
	u64_stats_update_begin(&pcpu_stats->syncp);
	pcpu_stats->rx_packets++;
	pcpu_stats->rx_bytes += skb->len;
	u64_stats_update_end(&pcpu_stats->syncp);

	skb->protocol = eth_type_trans(skb, skb->dev);
	netif_receive_skb(skb);
}

static int mlxsw_sx_port_type_set(struct mlxsw_core *mlxsw_core, u8 local_port,
				  enum devlink_port_type new_type)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_core_driver_priv(mlxsw_core);
	u8 module, width;
	int err;

	if (!mlxsw_sx->ports || !mlxsw_sx->ports[local_port]) {
		dev_err(mlxsw_sx->bus_info->dev, "Port number \"%d\" does not exist\n",
			local_port);
		return -EINVAL;
	}

	if (new_type == DEVLINK_PORT_TYPE_AUTO)
		return -EOPNOTSUPP;

	__mlxsw_sx_port_remove(mlxsw_sx, local_port);
	err = mlxsw_sx_port_module_info_get(mlxsw_sx, local_port, &module,
					    &width);
	if (err)
		goto err_port_module_info_get;

	if (new_type == DEVLINK_PORT_TYPE_ETH)
		err = __mlxsw_sx_port_eth_create(mlxsw_sx, local_port, module,
						 width);
	else if (new_type == DEVLINK_PORT_TYPE_IB)
		err = __mlxsw_sx_port_ib_create(mlxsw_sx, local_port, module,
						width);

err_port_module_info_get:
	return err;
}

#define MLXSW_SX_RXL(_trap_id) \
	MLXSW_RXL(mlxsw_sx_rx_listener_func, _trap_id, TRAP_TO_CPU,	\
		  false, SX2_RX, FORWARD)

static const struct mlxsw_listener mlxsw_sx_listener[] = {
	MLXSW_EVENTL(mlxsw_sx_pude_event_func, PUDE, EMAD),
	MLXSW_SX_RXL(FDB_MC),
	MLXSW_SX_RXL(STP),
	MLXSW_SX_RXL(LACP),
	MLXSW_SX_RXL(EAPOL),
	MLXSW_SX_RXL(LLDP),
	MLXSW_SX_RXL(MMRP),
	MLXSW_SX_RXL(MVRP),
	MLXSW_SX_RXL(RPVST),
	MLXSW_SX_RXL(DHCP),
	MLXSW_SX_RXL(IGMP_QUERY),
	MLXSW_SX_RXL(IGMP_V1_REPORT),
	MLXSW_SX_RXL(IGMP_V2_REPORT),
	MLXSW_SX_RXL(IGMP_V2_LEAVE),
	MLXSW_SX_RXL(IGMP_V3_REPORT),
};

static int mlxsw_sx_traps_init(struct mlxsw_sx *mlxsw_sx)
{
	char htgt_pl[MLXSW_REG_HTGT_LEN];
	int i;
	int err;

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_SX2_RX,
			    MLXSW_REG_HTGT_INVALID_POLICER,
			    MLXSW_REG_HTGT_DEFAULT_PRIORITY,
			    MLXSW_REG_HTGT_DEFAULT_TC);
	mlxsw_reg_htgt_local_path_rdq_set(htgt_pl,
					  MLXSW_REG_HTGT_LOCAL_PATH_RDQ_SX2_RX);

	err = mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(htgt), htgt_pl);
	if (err)
		return err;

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_SX2_CTRL,
			    MLXSW_REG_HTGT_INVALID_POLICER,
			    MLXSW_REG_HTGT_DEFAULT_PRIORITY,
			    MLXSW_REG_HTGT_DEFAULT_TC);
	mlxsw_reg_htgt_local_path_rdq_set(htgt_pl,
					MLXSW_REG_HTGT_LOCAL_PATH_RDQ_SX2_CTRL);

	err = mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(htgt), htgt_pl);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sx_listener); i++) {
		err = mlxsw_core_trap_register(mlxsw_sx->core,
					       &mlxsw_sx_listener[i],
					       mlxsw_sx);
		if (err)
			goto err_listener_register;

	}
	return 0;

err_listener_register:
	for (i--; i >= 0; i--) {
		mlxsw_core_trap_unregister(mlxsw_sx->core,
					   &mlxsw_sx_listener[i],
					   mlxsw_sx);
	}
	return err;
}

static void mlxsw_sx_traps_fini(struct mlxsw_sx *mlxsw_sx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sx_listener); i++) {
		mlxsw_core_trap_unregister(mlxsw_sx->core,
					   &mlxsw_sx_listener[i],
					   mlxsw_sx);
	}
}

static int mlxsw_sx_flood_init(struct mlxsw_sx *mlxsw_sx)
{
	char sfgc_pl[MLXSW_REG_SFGC_LEN];
	char sgcr_pl[MLXSW_REG_SGCR_LEN];
	char *sftr_pl;
	int err;

	/* Configure a flooding table, which includes only CPU port. */
	sftr_pl = kmalloc(MLXSW_REG_SFTR_LEN, GFP_KERNEL);
	if (!sftr_pl)
		return -ENOMEM;
	mlxsw_reg_sftr_pack(sftr_pl, 0, 0, MLXSW_REG_SFGC_TABLE_TYPE_SINGLE, 0,
			    MLXSW_PORT_CPU_PORT, true);
	err = mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(sftr), sftr_pl);
	kfree(sftr_pl);
	if (err)
		return err;

	/* Flood different packet types using the flooding table. */
	mlxsw_reg_sfgc_pack(sfgc_pl,
			    MLXSW_REG_SFGC_TYPE_UNKNOWN_UNICAST,
			    MLXSW_REG_SFGC_BRIDGE_TYPE_1Q_FID,
			    MLXSW_REG_SFGC_TABLE_TYPE_SINGLE,
			    0);
	err = mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(sfgc), sfgc_pl);
	if (err)
		return err;

	mlxsw_reg_sfgc_pack(sfgc_pl,
			    MLXSW_REG_SFGC_TYPE_BROADCAST,
			    MLXSW_REG_SFGC_BRIDGE_TYPE_1Q_FID,
			    MLXSW_REG_SFGC_TABLE_TYPE_SINGLE,
			    0);
	err = mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(sfgc), sfgc_pl);
	if (err)
		return err;

	mlxsw_reg_sfgc_pack(sfgc_pl,
			    MLXSW_REG_SFGC_TYPE_UNREGISTERED_MULTICAST_NON_IP,
			    MLXSW_REG_SFGC_BRIDGE_TYPE_1Q_FID,
			    MLXSW_REG_SFGC_TABLE_TYPE_SINGLE,
			    0);
	err = mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(sfgc), sfgc_pl);
	if (err)
		return err;

	mlxsw_reg_sfgc_pack(sfgc_pl,
			    MLXSW_REG_SFGC_TYPE_UNREGISTERED_MULTICAST_IPV6,
			    MLXSW_REG_SFGC_BRIDGE_TYPE_1Q_FID,
			    MLXSW_REG_SFGC_TABLE_TYPE_SINGLE,
			    0);
	err = mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(sfgc), sfgc_pl);
	if (err)
		return err;

	mlxsw_reg_sfgc_pack(sfgc_pl,
			    MLXSW_REG_SFGC_TYPE_UNREGISTERED_MULTICAST_IPV4,
			    MLXSW_REG_SFGC_BRIDGE_TYPE_1Q_FID,
			    MLXSW_REG_SFGC_TABLE_TYPE_SINGLE,
			    0);
	err = mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(sfgc), sfgc_pl);
	if (err)
		return err;

	mlxsw_reg_sgcr_pack(sgcr_pl, true);
	return mlxsw_reg_write(mlxsw_sx->core, MLXSW_REG(sgcr), sgcr_pl);
}

static int mlxsw_sx_basic_trap_groups_set(struct mlxsw_core *mlxsw_core)
{
	char htgt_pl[MLXSW_REG_HTGT_LEN];

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_EMAD,
			    MLXSW_REG_HTGT_INVALID_POLICER,
			    MLXSW_REG_HTGT_DEFAULT_PRIORITY,
			    MLXSW_REG_HTGT_DEFAULT_TC);
	mlxsw_reg_htgt_swid_set(htgt_pl, MLXSW_PORT_SWID_ALL_SWIDS);
	mlxsw_reg_htgt_local_path_rdq_set(htgt_pl,
					MLXSW_REG_HTGT_LOCAL_PATH_RDQ_SX2_EMAD);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(htgt), htgt_pl);
}

static int mlxsw_sx_init(struct mlxsw_core *mlxsw_core,
			 const struct mlxsw_bus_info *mlxsw_bus_info,
			 struct netlink_ext_ack *extack)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_core_driver_priv(mlxsw_core);
	int err;

	mlxsw_sx->core = mlxsw_core;
	mlxsw_sx->bus_info = mlxsw_bus_info;

	err = mlxsw_sx_hw_id_get(mlxsw_sx);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Failed to get switch HW ID\n");
		return err;
	}

	err = mlxsw_sx_ports_create(mlxsw_sx);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Failed to create ports\n");
		return err;
	}

	err = mlxsw_sx_traps_init(mlxsw_sx);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Failed to set traps\n");
		goto err_listener_register;
	}

	err = mlxsw_sx_flood_init(mlxsw_sx);
	if (err) {
		dev_err(mlxsw_sx->bus_info->dev, "Failed to initialize flood tables\n");
		goto err_flood_init;
	}

	return 0;

err_flood_init:
	mlxsw_sx_traps_fini(mlxsw_sx);
err_listener_register:
	mlxsw_sx_ports_remove(mlxsw_sx);
	return err;
}

static void mlxsw_sx_fini(struct mlxsw_core *mlxsw_core)
{
	struct mlxsw_sx *mlxsw_sx = mlxsw_core_driver_priv(mlxsw_core);

	mlxsw_sx_traps_fini(mlxsw_sx);
	mlxsw_sx_ports_remove(mlxsw_sx);
}

static const struct mlxsw_config_profile mlxsw_sx_config_profile = {
	.used_max_vepa_channels		= 1,
	.max_vepa_channels		= 0,
	.used_max_mid			= 1,
	.max_mid			= 7000,
	.used_max_pgt			= 1,
	.max_pgt			= 0,
	.used_max_system_port		= 1,
	.max_system_port		= 48000,
	.used_max_vlan_groups		= 1,
	.max_vlan_groups		= 127,
	.used_max_regions		= 1,
	.max_regions			= 400,
	.used_flood_tables		= 1,
	.max_flood_tables		= 2,
	.max_vid_flood_tables		= 1,
	.used_flood_mode		= 1,
	.flood_mode			= 3,
	.used_max_ib_mc			= 1,
	.max_ib_mc			= 6,
	.used_max_pkey			= 1,
	.max_pkey			= 0,
	.swid_config			= {
		{
			.used_type	= 1,
			.type		= MLXSW_PORT_SWID_TYPE_ETH,
		},
		{
			.used_type	= 1,
			.type		= MLXSW_PORT_SWID_TYPE_IB,
		}
	},
};

static struct mlxsw_driver mlxsw_sx_driver = {
	.kind			= mlxsw_sx_driver_name,
	.priv_size		= sizeof(struct mlxsw_sx),
	.init			= mlxsw_sx_init,
	.fini			= mlxsw_sx_fini,
	.basic_trap_groups_set	= mlxsw_sx_basic_trap_groups_set,
	.txhdr_construct	= mlxsw_sx_txhdr_construct,
	.txhdr_len		= MLXSW_TXHDR_LEN,
	.profile		= &mlxsw_sx_config_profile,
	.port_type_set		= mlxsw_sx_port_type_set,
};

static const struct pci_device_id mlxsw_sx_pci_id_table[] = {
	{PCI_VDEVICE(MELLANOX, PCI_DEVICE_ID_MELLANOX_SWITCHX2), 0},
	{0, },
};

static struct pci_driver mlxsw_sx_pci_driver = {
	.name = mlxsw_sx_driver_name,
	.id_table = mlxsw_sx_pci_id_table,
};

static int __init mlxsw_sx_module_init(void)
{
	int err;

	err = mlxsw_core_driver_register(&mlxsw_sx_driver);
	if (err)
		return err;

	err = mlxsw_pci_driver_register(&mlxsw_sx_pci_driver);
	if (err)
		goto err_pci_driver_register;

	return 0;

err_pci_driver_register:
	mlxsw_core_driver_unregister(&mlxsw_sx_driver);
	return err;
}

static void __exit mlxsw_sx_module_exit(void)
{
	mlxsw_pci_driver_unregister(&mlxsw_sx_pci_driver);
	mlxsw_core_driver_unregister(&mlxsw_sx_driver);
}

module_init(mlxsw_sx_module_init);
module_exit(mlxsw_sx_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Mellanox SwitchX-2 driver");
MODULE_DEVICE_TABLE(pci, mlxsw_sx_pci_id_table);
