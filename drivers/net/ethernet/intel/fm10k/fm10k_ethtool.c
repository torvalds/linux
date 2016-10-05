/* Intel(R) Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include <linux/vmalloc.h>

#include "fm10k.h"

struct fm10k_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define FM10K_NETDEV_STAT(_net_stat) { \
	.stat_string = #_net_stat, \
	.sizeof_stat = FIELD_SIZEOF(struct net_device_stats, _net_stat), \
	.stat_offset = offsetof(struct net_device_stats, _net_stat) \
}

static const struct fm10k_stats fm10k_gstrings_net_stats[] = {
	FM10K_NETDEV_STAT(tx_packets),
	FM10K_NETDEV_STAT(tx_bytes),
	FM10K_NETDEV_STAT(tx_errors),
	FM10K_NETDEV_STAT(rx_packets),
	FM10K_NETDEV_STAT(rx_bytes),
	FM10K_NETDEV_STAT(rx_errors),
	FM10K_NETDEV_STAT(rx_dropped),

	/* detailed Rx errors */
	FM10K_NETDEV_STAT(rx_length_errors),
	FM10K_NETDEV_STAT(rx_crc_errors),
	FM10K_NETDEV_STAT(rx_fifo_errors),
};

#define FM10K_NETDEV_STATS_LEN	ARRAY_SIZE(fm10k_gstrings_net_stats)

#define FM10K_STAT(_name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = FIELD_SIZEOF(struct fm10k_intfc, _stat), \
	.stat_offset = offsetof(struct fm10k_intfc, _stat) \
}

static const struct fm10k_stats fm10k_gstrings_global_stats[] = {
	FM10K_STAT("tx_restart_queue", restart_queue),
	FM10K_STAT("tx_busy", tx_busy),
	FM10K_STAT("tx_csum_errors", tx_csum_errors),
	FM10K_STAT("rx_alloc_failed", alloc_failed),
	FM10K_STAT("rx_csum_errors", rx_csum_errors),

	FM10K_STAT("tx_packets_nic", tx_packets_nic),
	FM10K_STAT("tx_bytes_nic", tx_bytes_nic),
	FM10K_STAT("rx_packets_nic", rx_packets_nic),
	FM10K_STAT("rx_bytes_nic", rx_bytes_nic),
	FM10K_STAT("rx_drops_nic", rx_drops_nic),
	FM10K_STAT("rx_overrun_pf", rx_overrun_pf),
	FM10K_STAT("rx_overrun_vf", rx_overrun_vf),

	FM10K_STAT("swapi_status", hw.swapi.status),
	FM10K_STAT("mac_rules_used", hw.swapi.mac.used),
	FM10K_STAT("mac_rules_avail", hw.swapi.mac.avail),

	FM10K_STAT("reset_while_pending", hw.mac.reset_while_pending),

	FM10K_STAT("tx_hang_count", tx_timeout_count),
};

static const struct fm10k_stats fm10k_gstrings_pf_stats[] = {
	FM10K_STAT("timeout", stats.timeout.count),
	FM10K_STAT("ur", stats.ur.count),
	FM10K_STAT("ca", stats.ca.count),
	FM10K_STAT("um", stats.um.count),
	FM10K_STAT("xec", stats.xec.count),
	FM10K_STAT("vlan_drop", stats.vlan_drop.count),
	FM10K_STAT("loopback_drop", stats.loopback_drop.count),
	FM10K_STAT("nodesc_drop", stats.nodesc_drop.count),
};

#define FM10K_MBX_STAT(_name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = FIELD_SIZEOF(struct fm10k_mbx_info, _stat), \
	.stat_offset = offsetof(struct fm10k_mbx_info, _stat) \
}

static const struct fm10k_stats fm10k_gstrings_mbx_stats[] = {
	FM10K_MBX_STAT("mbx_tx_busy", tx_busy),
	FM10K_MBX_STAT("mbx_tx_dropped", tx_dropped),
	FM10K_MBX_STAT("mbx_tx_messages", tx_messages),
	FM10K_MBX_STAT("mbx_tx_dwords", tx_dwords),
	FM10K_MBX_STAT("mbx_tx_mbmem_pulled", tx_mbmem_pulled),
	FM10K_MBX_STAT("mbx_rx_messages", rx_messages),
	FM10K_MBX_STAT("mbx_rx_dwords", rx_dwords),
	FM10K_MBX_STAT("mbx_rx_parse_err", rx_parse_err),
	FM10K_MBX_STAT("mbx_rx_mbmem_pushed", rx_mbmem_pushed),
};

#define FM10K_QUEUE_STAT(_name, _stat) { \
	.stat_string = _name, \
	.sizeof_stat = FIELD_SIZEOF(struct fm10k_ring, _stat), \
	.stat_offset = offsetof(struct fm10k_ring, _stat) \
}

static const struct fm10k_stats fm10k_gstrings_queue_stats[] = {
	FM10K_QUEUE_STAT("packets", stats.packets),
	FM10K_QUEUE_STAT("bytes", stats.bytes),
};

#define FM10K_GLOBAL_STATS_LEN ARRAY_SIZE(fm10k_gstrings_global_stats)
#define FM10K_PF_STATS_LEN ARRAY_SIZE(fm10k_gstrings_pf_stats)
#define FM10K_MBX_STATS_LEN ARRAY_SIZE(fm10k_gstrings_mbx_stats)
#define FM10K_QUEUE_STATS_LEN ARRAY_SIZE(fm10k_gstrings_queue_stats)

#define FM10K_STATIC_STATS_LEN (FM10K_GLOBAL_STATS_LEN + \
				FM10K_NETDEV_STATS_LEN + \
				FM10K_MBX_STATS_LEN)

static const char fm10k_gstrings_test[][ETH_GSTRING_LEN] = {
	"Mailbox test (on/offline)"
};

#define FM10K_TEST_LEN (sizeof(fm10k_gstrings_test) / ETH_GSTRING_LEN)

enum fm10k_self_test_types {
	FM10K_TEST_MBX,
	FM10K_TEST_MAX = FM10K_TEST_LEN
};

enum {
	FM10K_PRV_FLAG_LEN,
};

static const char fm10k_prv_flags[FM10K_PRV_FLAG_LEN][ETH_GSTRING_LEN] = {
};

static void fm10k_add_stat_strings(char **p, const char *prefix,
				   const struct fm10k_stats stats[],
				   const unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++) {
		snprintf(*p, ETH_GSTRING_LEN, "%s%s",
			 prefix, stats[i].stat_string);
		*p += ETH_GSTRING_LEN;
	}
}

static void fm10k_get_stat_strings(struct net_device *dev, u8 *data)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	char *p = (char *)data;
	unsigned int i;

	fm10k_add_stat_strings(&p, "", fm10k_gstrings_net_stats,
			       FM10K_NETDEV_STATS_LEN);

	fm10k_add_stat_strings(&p, "", fm10k_gstrings_global_stats,
			       FM10K_GLOBAL_STATS_LEN);

	fm10k_add_stat_strings(&p, "", fm10k_gstrings_mbx_stats,
			       FM10K_MBX_STATS_LEN);

	if (interface->hw.mac.type != fm10k_mac_vf)
		fm10k_add_stat_strings(&p, "", fm10k_gstrings_pf_stats,
				       FM10K_PF_STATS_LEN);

	for (i = 0; i < interface->hw.mac.max_queues; i++) {
		char prefix[ETH_GSTRING_LEN];

		snprintf(prefix, ETH_GSTRING_LEN, "tx_queue_%u_", i);
		fm10k_add_stat_strings(&p, prefix,
				       fm10k_gstrings_queue_stats,
				       FM10K_QUEUE_STATS_LEN);

		snprintf(prefix, ETH_GSTRING_LEN, "rx_queue_%u_", i);
		fm10k_add_stat_strings(&p, prefix,
				       fm10k_gstrings_queue_stats,
				       FM10K_QUEUE_STATS_LEN);
	}
}

static void fm10k_get_strings(struct net_device *dev,
			      u32 stringset, u8 *data)
{
	char *p = (char *)data;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *fm10k_gstrings_test,
		       FM10K_TEST_LEN * ETH_GSTRING_LEN);
		break;
	case ETH_SS_STATS:
		fm10k_get_stat_strings(dev, data);
		break;
	case ETH_SS_PRIV_FLAGS:
		memcpy(p, fm10k_prv_flags,
		       FM10K_PRV_FLAG_LEN * ETH_GSTRING_LEN);
		break;
	}
}

static int fm10k_get_sset_count(struct net_device *dev, int sset)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_hw *hw = &interface->hw;
	int stats_len = FM10K_STATIC_STATS_LEN;

	switch (sset) {
	case ETH_SS_TEST:
		return FM10K_TEST_LEN;
	case ETH_SS_STATS:
		stats_len += hw->mac.max_queues * 2 * FM10K_QUEUE_STATS_LEN;

		if (hw->mac.type != fm10k_mac_vf)
			stats_len += FM10K_PF_STATS_LEN;

		return stats_len;
	case ETH_SS_PRIV_FLAGS:
		return FM10K_PRV_FLAG_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void fm10k_add_ethtool_stats(u64 **data, void *pointer,
				    const struct fm10k_stats stats[],
				    const unsigned int size)
{
	unsigned int i;
	char *p;

	if (!pointer) {
		/* memory is not zero allocated so we have to clear it */
		for (i = 0; i < size; i++)
			*((*data)++) = 0;
		return;
	}

	for (i = 0; i < size; i++) {
		p = (char *)pointer + stats[i].stat_offset;

		switch (stats[i].sizeof_stat) {
		case sizeof(u64):
			*((*data)++) = *(u64 *)p;
			break;
		case sizeof(u32):
			*((*data)++) = *(u32 *)p;
			break;
		case sizeof(u16):
			*((*data)++) = *(u16 *)p;
			break;
		case sizeof(u8):
			*((*data)++) = *(u8 *)p;
			break;
		default:
			*((*data)++) = 0;
		}
	}
}

static void fm10k_get_ethtool_stats(struct net_device *netdev,
				    struct ethtool_stats __always_unused *stats,
				    u64 *data)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	struct net_device_stats *net_stats = &netdev->stats;
	int i;

	fm10k_update_stats(interface);

	fm10k_add_ethtool_stats(&data, net_stats, fm10k_gstrings_net_stats,
				FM10K_NETDEV_STATS_LEN);

	fm10k_add_ethtool_stats(&data, interface, fm10k_gstrings_global_stats,
				FM10K_GLOBAL_STATS_LEN);

	fm10k_add_ethtool_stats(&data, &interface->hw.mbx,
				fm10k_gstrings_mbx_stats,
				FM10K_MBX_STATS_LEN);

	if (interface->hw.mac.type != fm10k_mac_vf) {
		fm10k_add_ethtool_stats(&data, interface,
					fm10k_gstrings_pf_stats,
					FM10K_PF_STATS_LEN);
	}

	for (i = 0; i < interface->hw.mac.max_queues; i++) {
		struct fm10k_ring *ring;

		ring = interface->tx_ring[i];
		fm10k_add_ethtool_stats(&data, ring,
					fm10k_gstrings_queue_stats,
					FM10K_QUEUE_STATS_LEN);

		ring = interface->rx_ring[i];
		fm10k_add_ethtool_stats(&data, ring,
					fm10k_gstrings_queue_stats,
					FM10K_QUEUE_STATS_LEN);
	}
}

/* If function below adds more registers this define needs to be updated */
#define FM10K_REGS_LEN_Q 29

static void fm10k_get_reg_q(struct fm10k_hw *hw, u32 *buff, int i)
{
	int idx = 0;

	buff[idx++] = fm10k_read_reg(hw, FM10K_RDBAL(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_RDBAH(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_RDLEN(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TPH_RXCTRL(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_RDH(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_RDT(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_RXQCTL(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_RXDCTL(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_RXINT(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_SRRCTL(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_QPRC(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_QPRDC(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_QBRC_L(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_QBRC_H(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TDBAL(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TDBAH(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TDLEN(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TPH_TXCTRL(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TDH(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TDT(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TXDCTL(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TXQCTL(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TXINT(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_QPTC(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_QBTC_L(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_QBTC_H(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TQDLOC(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_TX_SGLORT(i));
	buff[idx++] = fm10k_read_reg(hw, FM10K_PFVTCTL(i));

	BUG_ON(idx != FM10K_REGS_LEN_Q);
}

/* If function above adds more registers this define needs to be updated */
#define FM10K_REGS_LEN_VSI 43

static void fm10k_get_reg_vsi(struct fm10k_hw *hw, u32 *buff, int i)
{
	int idx = 0, j;

	buff[idx++] = fm10k_read_reg(hw, FM10K_MRQC(i));
	for (j = 0; j < 10; j++)
		buff[idx++] = fm10k_read_reg(hw, FM10K_RSSRK(i, j));
	for (j = 0; j < 32; j++)
		buff[idx++] = fm10k_read_reg(hw, FM10K_RETA(i, j));

	BUG_ON(idx != FM10K_REGS_LEN_VSI);
}

static void fm10k_get_regs(struct net_device *netdev,
			   struct ethtool_regs *regs, void *p)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	struct fm10k_hw *hw = &interface->hw;
	u32 *buff = p;
	u16 i;

	regs->version = BIT(24) | (hw->revision_id << 16) | hw->device_id;

	switch (hw->mac.type) {
	case fm10k_mac_pf:
		/* General PF Registers */
		*(buff++) = fm10k_read_reg(hw, FM10K_CTRL);
		*(buff++) = fm10k_read_reg(hw, FM10K_CTRL_EXT);
		*(buff++) = fm10k_read_reg(hw, FM10K_GCR);
		*(buff++) = fm10k_read_reg(hw, FM10K_GCR_EXT);

		for (i = 0; i < 8; i++) {
			*(buff++) = fm10k_read_reg(hw, FM10K_DGLORTMAP(i));
			*(buff++) = fm10k_read_reg(hw, FM10K_DGLORTDEC(i));
		}

		for (i = 0; i < 65; i++) {
			fm10k_get_reg_vsi(hw, buff, i);
			buff += FM10K_REGS_LEN_VSI;
		}

		*(buff++) = fm10k_read_reg(hw, FM10K_DMA_CTRL);
		*(buff++) = fm10k_read_reg(hw, FM10K_DMA_CTRL2);

		for (i = 0; i < FM10K_MAX_QUEUES_PF; i++) {
			fm10k_get_reg_q(hw, buff, i);
			buff += FM10K_REGS_LEN_Q;
		}

		*(buff++) = fm10k_read_reg(hw, FM10K_TPH_CTRL);

		for (i = 0; i < 8; i++)
			*(buff++) = fm10k_read_reg(hw, FM10K_INT_MAP(i));

		/* Interrupt Throttling Registers */
		for (i = 0; i < 130; i++)
			*(buff++) = fm10k_read_reg(hw, FM10K_ITR(i));

		break;
	case fm10k_mac_vf:
		/* General VF registers */
		*(buff++) = fm10k_read_reg(hw, FM10K_VFCTRL);
		*(buff++) = fm10k_read_reg(hw, FM10K_VFINT_MAP);
		*(buff++) = fm10k_read_reg(hw, FM10K_VFSYSTIME);

		/* Interrupt Throttling Registers */
		for (i = 0; i < 8; i++)
			*(buff++) = fm10k_read_reg(hw, FM10K_VFITR(i));

		fm10k_get_reg_vsi(hw, buff, 0);
		buff += FM10K_REGS_LEN_VSI;

		for (i = 0; i < FM10K_MAX_QUEUES_POOL; i++) {
			if (i < hw->mac.max_queues)
				fm10k_get_reg_q(hw, buff, i);
			else
				memset(buff, 0, sizeof(u32) * FM10K_REGS_LEN_Q);
			buff += FM10K_REGS_LEN_Q;
		}

		break;
	default:
		return;
	}
}

/* If function above adds more registers these define need to be updated */
#define FM10K_REGS_LEN_PF \
(162 + (65 * FM10K_REGS_LEN_VSI) + (FM10K_MAX_QUEUES_PF * FM10K_REGS_LEN_Q))
#define FM10K_REGS_LEN_VF \
(11 + FM10K_REGS_LEN_VSI + (FM10K_MAX_QUEUES_POOL * FM10K_REGS_LEN_Q))

static int fm10k_get_regs_len(struct net_device *netdev)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	struct fm10k_hw *hw = &interface->hw;

	switch (hw->mac.type) {
	case fm10k_mac_pf:
		return FM10K_REGS_LEN_PF * sizeof(u32);
	case fm10k_mac_vf:
		return FM10K_REGS_LEN_VF * sizeof(u32);
	default:
		return 0;
	}
}

static void fm10k_get_drvinfo(struct net_device *dev,
			      struct ethtool_drvinfo *info)
{
	struct fm10k_intfc *interface = netdev_priv(dev);

	strncpy(info->driver, fm10k_driver_name,
		sizeof(info->driver) - 1);
	strncpy(info->version, fm10k_driver_version,
		sizeof(info->version) - 1);
	strncpy(info->bus_info, pci_name(interface->pdev),
		sizeof(info->bus_info) - 1);
}

static void fm10k_get_pauseparam(struct net_device *dev,
				 struct ethtool_pauseparam *pause)
{
	struct fm10k_intfc *interface = netdev_priv(dev);

	/* record fixed values for autoneg and tx pause */
	pause->autoneg = 0;
	pause->tx_pause = 1;

	pause->rx_pause = interface->rx_pause ? 1 : 0;
}

static int fm10k_set_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *pause)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_hw *hw = &interface->hw;

	if (pause->autoneg || !pause->tx_pause)
		return -EINVAL;

	/* we can only support pause on the PF to avoid head-of-line blocking */
	if (hw->mac.type == fm10k_mac_pf)
		interface->rx_pause = pause->rx_pause ? ~0 : 0;
	else if (pause->rx_pause)
		return -EINVAL;

	if (netif_running(dev))
		fm10k_update_rx_drop_en(interface);

	return 0;
}

static u32 fm10k_get_msglevel(struct net_device *netdev)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);

	return interface->msg_enable;
}

static void fm10k_set_msglevel(struct net_device *netdev, u32 data)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);

	interface->msg_enable = data;
}

static void fm10k_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);

	ring->rx_max_pending = FM10K_MAX_RXD;
	ring->tx_max_pending = FM10K_MAX_TXD;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = interface->rx_ring_count;
	ring->tx_pending = interface->tx_ring_count;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static int fm10k_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	struct fm10k_ring *temp_ring;
	int i, err = 0;
	u32 new_rx_count, new_tx_count;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	new_tx_count = clamp_t(u32, ring->tx_pending,
			       FM10K_MIN_TXD, FM10K_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, FM10K_REQ_TX_DESCRIPTOR_MULTIPLE);

	new_rx_count = clamp_t(u32, ring->rx_pending,
			       FM10K_MIN_RXD, FM10K_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, FM10K_REQ_RX_DESCRIPTOR_MULTIPLE);

	if ((new_tx_count == interface->tx_ring_count) &&
	    (new_rx_count == interface->rx_ring_count)) {
		/* nothing to do */
		return 0;
	}

	while (test_and_set_bit(__FM10K_RESETTING, &interface->state))
		usleep_range(1000, 2000);

	if (!netif_running(interface->netdev)) {
		for (i = 0; i < interface->num_tx_queues; i++)
			interface->tx_ring[i]->count = new_tx_count;
		for (i = 0; i < interface->num_rx_queues; i++)
			interface->rx_ring[i]->count = new_rx_count;
		interface->tx_ring_count = new_tx_count;
		interface->rx_ring_count = new_rx_count;
		goto clear_reset;
	}

	/* allocate temporary buffer to store rings in */
	i = max_t(int, interface->num_tx_queues, interface->num_rx_queues);
	temp_ring = vmalloc(i * sizeof(struct fm10k_ring));

	if (!temp_ring) {
		err = -ENOMEM;
		goto clear_reset;
	}

	fm10k_down(interface);

	/* Setup new Tx resources and free the old Tx resources in that order.
	 * We can then assign the new resources to the rings via a memcpy.
	 * The advantage to this approach is that we are guaranteed to still
	 * have resources even in the case of an allocation failure.
	 */
	if (new_tx_count != interface->tx_ring_count) {
		for (i = 0; i < interface->num_tx_queues; i++) {
			memcpy(&temp_ring[i], interface->tx_ring[i],
			       sizeof(struct fm10k_ring));

			temp_ring[i].count = new_tx_count;
			err = fm10k_setup_tx_resources(&temp_ring[i]);
			if (err) {
				while (i) {
					i--;
					fm10k_free_tx_resources(&temp_ring[i]);
				}
				goto err_setup;
			}
		}

		for (i = 0; i < interface->num_tx_queues; i++) {
			fm10k_free_tx_resources(interface->tx_ring[i]);

			memcpy(interface->tx_ring[i], &temp_ring[i],
			       sizeof(struct fm10k_ring));
		}

		interface->tx_ring_count = new_tx_count;
	}

	/* Repeat the process for the Rx rings if needed */
	if (new_rx_count != interface->rx_ring_count) {
		for (i = 0; i < interface->num_rx_queues; i++) {
			memcpy(&temp_ring[i], interface->rx_ring[i],
			       sizeof(struct fm10k_ring));

			temp_ring[i].count = new_rx_count;
			err = fm10k_setup_rx_resources(&temp_ring[i]);
			if (err) {
				while (i) {
					i--;
					fm10k_free_rx_resources(&temp_ring[i]);
				}
				goto err_setup;
			}
		}

		for (i = 0; i < interface->num_rx_queues; i++) {
			fm10k_free_rx_resources(interface->rx_ring[i]);

			memcpy(interface->rx_ring[i], &temp_ring[i],
			       sizeof(struct fm10k_ring));
		}

		interface->rx_ring_count = new_rx_count;
	}

err_setup:
	fm10k_up(interface);
	vfree(temp_ring);
clear_reset:
	clear_bit(__FM10K_RESETTING, &interface->state);
	return err;
}

static int fm10k_get_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *ec)
{
	struct fm10k_intfc *interface = netdev_priv(dev);

	ec->use_adaptive_tx_coalesce = ITR_IS_ADAPTIVE(interface->tx_itr);
	ec->tx_coalesce_usecs = interface->tx_itr & ~FM10K_ITR_ADAPTIVE;

	ec->use_adaptive_rx_coalesce = ITR_IS_ADAPTIVE(interface->rx_itr);
	ec->rx_coalesce_usecs = interface->rx_itr & ~FM10K_ITR_ADAPTIVE;

	return 0;
}

static int fm10k_set_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *ec)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_q_vector *qv;
	u16 tx_itr, rx_itr;
	int i;

	/* verify limits */
	if ((ec->rx_coalesce_usecs > FM10K_ITR_MAX) ||
	    (ec->tx_coalesce_usecs > FM10K_ITR_MAX))
		return -EINVAL;

	/* record settings */
	tx_itr = ec->tx_coalesce_usecs;
	rx_itr = ec->rx_coalesce_usecs;

	/* set initial values for adaptive ITR */
	if (ec->use_adaptive_tx_coalesce)
		tx_itr = FM10K_ITR_ADAPTIVE | FM10K_TX_ITR_DEFAULT;

	if (ec->use_adaptive_rx_coalesce)
		rx_itr = FM10K_ITR_ADAPTIVE | FM10K_RX_ITR_DEFAULT;

	/* update interface */
	interface->tx_itr = tx_itr;
	interface->rx_itr = rx_itr;

	/* update q_vectors */
	for (i = 0; i < interface->num_q_vectors; i++) {
		qv = interface->q_vector[i];
		qv->tx.itr = tx_itr;
		qv->rx.itr = rx_itr;
	}

	return 0;
}

static int fm10k_get_rss_hash_opts(struct fm10k_intfc *interface,
				   struct ethtool_rxnfc *cmd)
{
	cmd->data = 0;

	/* Report default options for RSS on fm10k */
	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		/* fall through */
	case UDP_V4_FLOW:
		if (interface->flags & FM10K_FLAG_RSS_FIELD_IPV4_UDP)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		/* fall through */
	case SCTP_V4_FLOW:
	case SCTP_V6_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V4_FLOW:
	case AH_V6_FLOW:
	case ESP_V4_FLOW:
	case ESP_V6_FLOW:
	case IPV4_FLOW:
	case IPV6_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	case UDP_V6_FLOW:
		if (interface->flags & FM10K_FLAG_RSS_FIELD_IPV6_UDP)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fm10k_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
			   u32 __always_unused *rule_locs)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = interface->num_rx_queues;
		ret = 0;
		break;
	case ETHTOOL_GRXFH:
		ret = fm10k_get_rss_hash_opts(interface, cmd);
		break;
	default:
		break;
	}

	return ret;
}

#define UDP_RSS_FLAGS (FM10K_FLAG_RSS_FIELD_IPV4_UDP | \
		       FM10K_FLAG_RSS_FIELD_IPV6_UDP)
static int fm10k_set_rss_hash_opt(struct fm10k_intfc *interface,
				  struct ethtool_rxnfc *nfc)
{
	u32 flags = interface->flags;

	/* RSS does not support anything other than hashing
	 * to queues on src and dst IPs and ports
	 */
	if (nfc->data & ~(RXH_IP_SRC | RXH_IP_DST |
			  RXH_L4_B_0_1 | RXH_L4_B_2_3))
		return -EINVAL;

	switch (nfc->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST) ||
		    !(nfc->data & RXH_L4_B_0_1) ||
		    !(nfc->data & RXH_L4_B_2_3))
			return -EINVAL;
		break;
	case UDP_V4_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST))
			return -EINVAL;
		switch (nfc->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
		case 0:
			flags &= ~FM10K_FLAG_RSS_FIELD_IPV4_UDP;
			break;
		case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
			flags |= FM10K_FLAG_RSS_FIELD_IPV4_UDP;
			break;
		default:
			return -EINVAL;
		}
		break;
	case UDP_V6_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST))
			return -EINVAL;
		switch (nfc->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
		case 0:
			flags &= ~FM10K_FLAG_RSS_FIELD_IPV6_UDP;
			break;
		case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
			flags |= FM10K_FLAG_RSS_FIELD_IPV6_UDP;
			break;
		default:
			return -EINVAL;
		}
		break;
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case SCTP_V4_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case SCTP_V6_FLOW:
		if (!(nfc->data & RXH_IP_SRC) ||
		    !(nfc->data & RXH_IP_DST) ||
		    (nfc->data & RXH_L4_B_0_1) ||
		    (nfc->data & RXH_L4_B_2_3))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	/* if we changed something we need to update flags */
	if (flags != interface->flags) {
		struct fm10k_hw *hw = &interface->hw;
		u32 mrqc;

		if ((flags & UDP_RSS_FLAGS) &&
		    !(interface->flags & UDP_RSS_FLAGS))
			netif_warn(interface, drv, interface->netdev,
				   "enabling UDP RSS: fragmented packets may arrive out of order to the stack above\n");

		interface->flags = flags;

		/* Perform hash on these packet types */
		mrqc = FM10K_MRQC_IPV4 |
		       FM10K_MRQC_TCP_IPV4 |
		       FM10K_MRQC_IPV6 |
		       FM10K_MRQC_TCP_IPV6;

		if (flags & FM10K_FLAG_RSS_FIELD_IPV4_UDP)
			mrqc |= FM10K_MRQC_UDP_IPV4;
		if (flags & FM10K_FLAG_RSS_FIELD_IPV6_UDP)
			mrqc |= FM10K_MRQC_UDP_IPV6;

		fm10k_write_reg(hw, FM10K_MRQC(0), mrqc);
	}

	return 0;
}

static int fm10k_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		ret = fm10k_set_rss_hash_opt(interface, cmd);
		break;
	default:
		break;
	}

	return ret;
}

static int fm10k_mbx_test(struct fm10k_intfc *interface, u64 *data)
{
	struct fm10k_hw *hw = &interface->hw;
	struct fm10k_mbx_info *mbx = &hw->mbx;
	u32 attr_flag, test_msg[6];
	unsigned long timeout;
	int err = -EINVAL;

	/* For now this is a VF only feature */
	if (hw->mac.type != fm10k_mac_vf)
		return 0;

	/* loop through both nested and unnested attribute types */
	for (attr_flag = BIT(FM10K_TEST_MSG_UNSET);
	     attr_flag < BIT(2 * FM10K_TEST_MSG_NESTED);
	     attr_flag += attr_flag) {
		/* generate message to be tested */
		fm10k_tlv_msg_test_create(test_msg, attr_flag);

		fm10k_mbx_lock(interface);
		mbx->test_result = FM10K_NOT_IMPLEMENTED;
		err = mbx->ops.enqueue_tx(hw, mbx, test_msg);
		fm10k_mbx_unlock(interface);

		/* wait up to 1 second for response */
		timeout = jiffies + HZ;
		do {
			if (err < 0)
				goto err_out;

			usleep_range(500, 1000);

			fm10k_mbx_lock(interface);
			mbx->ops.process(hw, mbx);
			fm10k_mbx_unlock(interface);

			err = mbx->test_result;
			if (!err)
				break;
		} while (time_is_after_jiffies(timeout));

		/* reporting errors */
		if (err)
			goto err_out;
	}

err_out:
	*data = err < 0 ? (attr_flag) : (err > 0);
	return err;
}

static void fm10k_self_test(struct net_device *dev,
			    struct ethtool_test *eth_test, u64 *data)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_hw *hw = &interface->hw;

	memset(data, 0, sizeof(*data) * FM10K_TEST_LEN);

	if (FM10K_REMOVED(hw)) {
		netif_err(interface, drv, dev,
			  "Interface removed - test blocked\n");
		eth_test->flags |= ETH_TEST_FL_FAILED;
		return;
	}

	if (fm10k_mbx_test(interface, &data[FM10K_TEST_MBX]))
		eth_test->flags |= ETH_TEST_FL_FAILED;
}

static u32 fm10k_get_priv_flags(struct net_device *netdev)
{
	return 0;
}

static int fm10k_set_priv_flags(struct net_device *netdev, u32 priv_flags)
{
	if (priv_flags >= BIT(FM10K_PRV_FLAG_LEN))
		return -EINVAL;

	return 0;
}

static u32 fm10k_get_reta_size(struct net_device __always_unused *netdev)
{
	return FM10K_RETA_SIZE * FM10K_RETA_ENTRIES_PER_REG;
}

void fm10k_write_reta(struct fm10k_intfc *interface, const u32 *indir)
{
	u16 rss_i = interface->ring_feature[RING_F_RSS].indices;
	struct fm10k_hw *hw = &interface->hw;
	u32 table[4];
	int i, j;

	/* record entries to reta table */
	for (i = 0; i < FM10K_RETA_SIZE; i++) {
		u32 reta, n;

		/* generate a new table if we weren't given one */
		for (j = 0; j < 4; j++) {
			if (indir)
				n = indir[4 * i + j];
			else
				n = ethtool_rxfh_indir_default(4 * i + j,
							       rss_i);

			table[j] = n;
		}

		reta = table[0] |
			(table[1] << 8) |
			(table[2] << 16) |
			(table[3] << 24);

		if (interface->reta[i] == reta)
			continue;

		interface->reta[i] = reta;
		fm10k_write_reg(hw, FM10K_RETA(0, i), reta);
	}
}

static int fm10k_get_reta(struct net_device *netdev, u32 *indir)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	int i;

	if (!indir)
		return 0;

	for (i = 0; i < FM10K_RETA_SIZE; i++, indir += 4) {
		u32 reta = interface->reta[i];

		indir[0] = (reta << 24) >> 24;
		indir[1] = (reta << 16) >> 24;
		indir[2] = (reta <<  8) >> 24;
		indir[3] = (reta) >> 24;
	}

	return 0;
}

static int fm10k_set_reta(struct net_device *netdev, const u32 *indir)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	int i;
	u16 rss_i;

	if (!indir)
		return 0;

	/* Verify user input. */
	rss_i = interface->ring_feature[RING_F_RSS].indices;
	for (i = fm10k_get_reta_size(netdev); i--;) {
		if (indir[i] < rss_i)
			continue;
		return -EINVAL;
	}

	fm10k_write_reta(interface, indir);

	return 0;
}

static u32 fm10k_get_rssrk_size(struct net_device __always_unused *netdev)
{
	return FM10K_RSSRK_SIZE * FM10K_RSSRK_ENTRIES_PER_REG;
}

static int fm10k_get_rssh(struct net_device *netdev, u32 *indir, u8 *key,
			  u8 *hfunc)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	int i, err;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	err = fm10k_get_reta(netdev, indir);
	if (err || !key)
		return err;

	for (i = 0; i < FM10K_RSSRK_SIZE; i++, key += 4)
		*(__le32 *)key = cpu_to_le32(interface->rssrk[i]);

	return 0;
}

static int fm10k_set_rssh(struct net_device *netdev, const u32 *indir,
			  const u8 *key, const u8 hfunc)
{
	struct fm10k_intfc *interface = netdev_priv(netdev);
	struct fm10k_hw *hw = &interface->hw;
	int i, err;

	/* We do not allow change in unsupported parameters */
	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	err = fm10k_set_reta(netdev, indir);
	if (err || !key)
		return err;

	for (i = 0; i < FM10K_RSSRK_SIZE; i++, key += 4) {
		u32 rssrk = le32_to_cpu(*(__le32 *)key);

		if (interface->rssrk[i] == rssrk)
			continue;

		interface->rssrk[i] = rssrk;
		fm10k_write_reg(hw, FM10K_RSSRK(0, i), rssrk);
	}

	return 0;
}

static unsigned int fm10k_max_channels(struct net_device *dev)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	unsigned int max_combined = interface->hw.mac.max_queues;
	u8 tcs = netdev_get_num_tc(dev);

	/* For QoS report channels per traffic class */
	if (tcs > 1)
		max_combined = BIT((fls(max_combined / tcs) - 1));

	return max_combined;
}

static void fm10k_get_channels(struct net_device *dev,
			       struct ethtool_channels *ch)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	struct fm10k_hw *hw = &interface->hw;

	/* report maximum channels */
	ch->max_combined = fm10k_max_channels(dev);

	/* report info for other vector */
	ch->max_other = NON_Q_VECTORS(hw);
	ch->other_count = ch->max_other;

	/* record RSS queues */
	ch->combined_count = interface->ring_feature[RING_F_RSS].indices;
}

static int fm10k_set_channels(struct net_device *dev,
			      struct ethtool_channels *ch)
{
	struct fm10k_intfc *interface = netdev_priv(dev);
	unsigned int count = ch->combined_count;
	struct fm10k_hw *hw = &interface->hw;

	/* verify they are not requesting separate vectors */
	if (!count || ch->rx_count || ch->tx_count)
		return -EINVAL;

	/* verify other_count has not changed */
	if (ch->other_count != NON_Q_VECTORS(hw))
		return -EINVAL;

	/* verify the number of channels does not exceed hardware limits */
	if (count > fm10k_max_channels(dev))
		return -EINVAL;

	interface->ring_feature[RING_F_RSS].limit = count;

	/* use setup TC to update any traffic class queue mapping */
	return fm10k_setup_tc(dev, netdev_get_num_tc(dev));
}

static const struct ethtool_ops fm10k_ethtool_ops = {
	.get_strings		= fm10k_get_strings,
	.get_sset_count		= fm10k_get_sset_count,
	.get_ethtool_stats      = fm10k_get_ethtool_stats,
	.get_drvinfo		= fm10k_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_pauseparam		= fm10k_get_pauseparam,
	.set_pauseparam		= fm10k_set_pauseparam,
	.get_msglevel		= fm10k_get_msglevel,
	.set_msglevel		= fm10k_set_msglevel,
	.get_ringparam		= fm10k_get_ringparam,
	.set_ringparam		= fm10k_set_ringparam,
	.get_coalesce		= fm10k_get_coalesce,
	.set_coalesce		= fm10k_set_coalesce,
	.get_rxnfc		= fm10k_get_rxnfc,
	.set_rxnfc		= fm10k_set_rxnfc,
	.get_regs               = fm10k_get_regs,
	.get_regs_len           = fm10k_get_regs_len,
	.self_test		= fm10k_self_test,
	.get_priv_flags		= fm10k_get_priv_flags,
	.set_priv_flags		= fm10k_set_priv_flags,
	.get_rxfh_indir_size	= fm10k_get_reta_size,
	.get_rxfh_key_size	= fm10k_get_rssrk_size,
	.get_rxfh		= fm10k_get_rssh,
	.set_rxfh		= fm10k_set_rssh,
	.get_channels		= fm10k_get_channels,
	.set_channels		= fm10k_set_channels,
	.get_ts_info		= ethtool_op_get_ts_info,
};

void fm10k_set_ethtool_ops(struct net_device *dev)
{
	dev->ethtool_ops = &fm10k_ethtool_ops;
}
