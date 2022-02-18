// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009 - 2018 Intel Corporation. */

/* ethtool support for igbvf */

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>

#include "igbvf.h"
#include <linux/if_vlan.h>

struct igbvf_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
	int base_stat_offset;
};

#define IGBVF_STAT(current, base) \
		sizeof(((struct igbvf_adapter *)0)->current), \
		offsetof(struct igbvf_adapter, current), \
		offsetof(struct igbvf_adapter, base)

static const struct igbvf_stats igbvf_gstrings_stats[] = {
	{ "rx_packets", IGBVF_STAT(stats.gprc, stats.base_gprc) },
	{ "tx_packets", IGBVF_STAT(stats.gptc, stats.base_gptc) },
	{ "rx_bytes", IGBVF_STAT(stats.gorc, stats.base_gorc) },
	{ "tx_bytes", IGBVF_STAT(stats.gotc, stats.base_gotc) },
	{ "multicast", IGBVF_STAT(stats.mprc, stats.base_mprc) },
	{ "lbrx_bytes", IGBVF_STAT(stats.gorlbc, stats.base_gorlbc) },
	{ "lbrx_packets", IGBVF_STAT(stats.gprlbc, stats.base_gprlbc) },
	{ "tx_restart_queue", IGBVF_STAT(restart_queue, zero_base) },
	{ "rx_long_byte_count", IGBVF_STAT(stats.gorc, stats.base_gorc) },
	{ "rx_csum_offload_good", IGBVF_STAT(hw_csum_good, zero_base) },
	{ "rx_csum_offload_errors", IGBVF_STAT(hw_csum_err, zero_base) },
	{ "rx_header_split", IGBVF_STAT(rx_hdr_split, zero_base) },
	{ "alloc_rx_buff_failed", IGBVF_STAT(alloc_rx_buff_failed, zero_base) },
};

#define IGBVF_GLOBAL_STATS_LEN ARRAY_SIZE(igbvf_gstrings_stats)

static const char igbvf_gstrings_test[][ETH_GSTRING_LEN] = {
	"Link test   (on/offline)"
};

#define IGBVF_TEST_LEN ARRAY_SIZE(igbvf_gstrings_test)

static int igbvf_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings *cmd)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 status;

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_add_link_mode(cmd, supported, 1000baseT_Full);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, 1000baseT_Full);

	cmd->base.port = -1;

	status = er32(STATUS);
	if (status & E1000_STATUS_LU) {
		if (status & E1000_STATUS_SPEED_1000)
			cmd->base.speed = SPEED_1000;
		else if (status & E1000_STATUS_SPEED_100)
			cmd->base.speed = SPEED_100;
		else
			cmd->base.speed = SPEED_10;

		if (status & E1000_STATUS_FD)
			cmd->base.duplex = DUPLEX_FULL;
		else
			cmd->base.duplex = DUPLEX_HALF;
	} else {
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}

	cmd->base.autoneg = AUTONEG_DISABLE;

	return 0;
}

static int igbvf_set_link_ksettings(struct net_device *netdev,
				    const struct ethtool_link_ksettings *cmd)
{
	return -EOPNOTSUPP;
}

static void igbvf_get_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pause)
{
}

static int igbvf_set_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	return -EOPNOTSUPP;
}

static u32 igbvf_get_msglevel(struct net_device *netdev)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void igbvf_set_msglevel(struct net_device *netdev, u32 data)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = data;
}

static int igbvf_get_regs_len(struct net_device *netdev)
{
#define IGBVF_REGS_LEN 8
	return IGBVF_REGS_LEN * sizeof(u32);
}

static void igbvf_get_regs(struct net_device *netdev,
			   struct ethtool_regs *regs, void *p)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 *regs_buff = p;

	memset(p, 0, IGBVF_REGS_LEN * sizeof(u32));

	regs->version = (1u << 24) |
			(adapter->pdev->revision << 16) |
			adapter->pdev->device;

	regs_buff[0] = er32(CTRL);
	regs_buff[1] = er32(STATUS);

	regs_buff[2] = er32(RDLEN(0));
	regs_buff[3] = er32(RDH(0));
	regs_buff[4] = er32(RDT(0));

	regs_buff[5] = er32(TDLEN(0));
	regs_buff[6] = er32(TDH(0));
	regs_buff[7] = er32(TDT(0));
}

static int igbvf_get_eeprom_len(struct net_device *netdev)
{
	return 0;
}

static int igbvf_get_eeprom(struct net_device *netdev,
			    struct ethtool_eeprom *eeprom, u8 *bytes)
{
	return -EOPNOTSUPP;
}

static int igbvf_set_eeprom(struct net_device *netdev,
			    struct ethtool_eeprom *eeprom, u8 *bytes)
{
	return -EOPNOTSUPP;
}

static void igbvf_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);

	strlcpy(drvinfo->driver,  igbvf_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
}

static void igbvf_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring,
				struct kernel_ethtool_ringparam *kernel_ring,
				struct netlink_ext_ack *extack)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	struct igbvf_ring *tx_ring = adapter->tx_ring;
	struct igbvf_ring *rx_ring = adapter->rx_ring;

	ring->rx_max_pending = IGBVF_MAX_RXD;
	ring->tx_max_pending = IGBVF_MAX_TXD;
	ring->rx_pending = rx_ring->count;
	ring->tx_pending = tx_ring->count;
}

static int igbvf_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring,
			       struct kernel_ethtool_ringparam *kernel_ring,
			       struct netlink_ext_ack *extack)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	struct igbvf_ring *temp_ring;
	int err = 0;
	u32 new_rx_count, new_tx_count;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	new_rx_count = max_t(u32, ring->rx_pending, IGBVF_MIN_RXD);
	new_rx_count = min_t(u32, new_rx_count, IGBVF_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, REQ_RX_DESCRIPTOR_MULTIPLE);

	new_tx_count = max_t(u32, ring->tx_pending, IGBVF_MIN_TXD);
	new_tx_count = min_t(u32, new_tx_count, IGBVF_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, REQ_TX_DESCRIPTOR_MULTIPLE);

	if ((new_tx_count == adapter->tx_ring->count) &&
	    (new_rx_count == adapter->rx_ring->count)) {
		/* nothing to do */
		return 0;
	}

	while (test_and_set_bit(__IGBVF_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	if (!netif_running(adapter->netdev)) {
		adapter->tx_ring->count = new_tx_count;
		adapter->rx_ring->count = new_rx_count;
		goto clear_reset;
	}

	temp_ring = vmalloc(sizeof(struct igbvf_ring));
	if (!temp_ring) {
		err = -ENOMEM;
		goto clear_reset;
	}

	igbvf_down(adapter);

	/* We can't just free everything and then setup again,
	 * because the ISRs in MSI-X mode get passed pointers
	 * to the Tx and Rx ring structs.
	 */
	if (new_tx_count != adapter->tx_ring->count) {
		memcpy(temp_ring, adapter->tx_ring, sizeof(struct igbvf_ring));

		temp_ring->count = new_tx_count;
		err = igbvf_setup_tx_resources(adapter, temp_ring);
		if (err)
			goto err_setup;

		igbvf_free_tx_resources(adapter->tx_ring);

		memcpy(adapter->tx_ring, temp_ring, sizeof(struct igbvf_ring));
	}

	if (new_rx_count != adapter->rx_ring->count) {
		memcpy(temp_ring, adapter->rx_ring, sizeof(struct igbvf_ring));

		temp_ring->count = new_rx_count;
		err = igbvf_setup_rx_resources(adapter, temp_ring);
		if (err)
			goto err_setup;

		igbvf_free_rx_resources(adapter->rx_ring);

		memcpy(adapter->rx_ring, temp_ring, sizeof(struct igbvf_ring));
	}
err_setup:
	igbvf_up(adapter);
	vfree(temp_ring);
clear_reset:
	clear_bit(__IGBVF_RESETTING, &adapter->state);
	return err;
}

static int igbvf_link_test(struct igbvf_adapter *adapter, u64 *data)
{
	struct e1000_hw *hw = &adapter->hw;
	*data = 0;

	spin_lock_bh(&hw->mbx_lock);

	hw->mac.ops.check_for_link(hw);

	spin_unlock_bh(&hw->mbx_lock);

	if (!(er32(STATUS) & E1000_STATUS_LU))
		*data = 1;

	return *data;
}

static void igbvf_diag_test(struct net_device *netdev,
			    struct ethtool_test *eth_test, u64 *data)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);

	set_bit(__IGBVF_TESTING, &adapter->state);

	/* Link test performed before hardware reset so autoneg doesn't
	 * interfere with test result
	 */
	if (igbvf_link_test(adapter, &data[0]))
		eth_test->flags |= ETH_TEST_FL_FAILED;

	clear_bit(__IGBVF_TESTING, &adapter->state);
	msleep_interruptible(4 * 1000);
}

static void igbvf_get_wol(struct net_device *netdev,
			  struct ethtool_wolinfo *wol)
{
	wol->supported = 0;
	wol->wolopts = 0;
}

static int igbvf_set_wol(struct net_device *netdev,
			 struct ethtool_wolinfo *wol)
{
	return -EOPNOTSUPP;
}

static int igbvf_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);

	if (adapter->requested_itr <= 3)
		ec->rx_coalesce_usecs = adapter->requested_itr;
	else
		ec->rx_coalesce_usecs = adapter->current_itr >> 2;

	return 0;
}

static int igbvf_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec,
			      struct kernel_ethtool_coalesce *kernel_coal,
			      struct netlink_ext_ack *extack)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	if ((ec->rx_coalesce_usecs >= IGBVF_MIN_ITR_USECS) &&
	    (ec->rx_coalesce_usecs <= IGBVF_MAX_ITR_USECS)) {
		adapter->current_itr = ec->rx_coalesce_usecs << 2;
		adapter->requested_itr = 1000000000 /
					(adapter->current_itr * 256);
	} else if ((ec->rx_coalesce_usecs == 3) ||
		   (ec->rx_coalesce_usecs == 2)) {
		adapter->current_itr = IGBVF_START_ITR;
		adapter->requested_itr = ec->rx_coalesce_usecs;
	} else if (ec->rx_coalesce_usecs == 0) {
		/* The user's desire is to turn off interrupt throttling
		 * altogether, but due to HW limitations, we can't do that.
		 * Instead we set a very small value in EITR, which would
		 * allow ~967k interrupts per second, but allow the adapter's
		 * internal clocking to still function properly.
		 */
		adapter->current_itr = 4;
		adapter->requested_itr = 1000000000 /
					(adapter->current_itr * 256);
	} else {
		return -EINVAL;
	}

	writel(adapter->current_itr,
	       hw->hw_addr + adapter->rx_ring->itr_register);

	return 0;
}

static int igbvf_nway_reset(struct net_device *netdev)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev))
		igbvf_reinit_locked(adapter);
	return 0;
}

static void igbvf_get_ethtool_stats(struct net_device *netdev,
				    struct ethtool_stats *stats,
				    u64 *data)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	int i;

	igbvf_update_stats(adapter);
	for (i = 0; i < IGBVF_GLOBAL_STATS_LEN; i++) {
		char *p = (char *)adapter +
			  igbvf_gstrings_stats[i].stat_offset;
		char *b = (char *)adapter +
			  igbvf_gstrings_stats[i].base_stat_offset;
		data[i] = ((igbvf_gstrings_stats[i].sizeof_stat ==
			    sizeof(u64)) ? (*(u64 *)p - *(u64 *)b) :
			    (*(u32 *)p - *(u32 *)b));
	}
}

static int igbvf_get_sset_count(struct net_device *dev, int stringset)
{
	switch (stringset) {
	case ETH_SS_TEST:
		return IGBVF_TEST_LEN;
	case ETH_SS_STATS:
		return IGBVF_GLOBAL_STATS_LEN;
	default:
		return -EINVAL;
	}
}

static void igbvf_get_strings(struct net_device *netdev, u32 stringset,
			      u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *igbvf_gstrings_test, sizeof(igbvf_gstrings_test));
		break;
	case ETH_SS_STATS:
		for (i = 0; i < IGBVF_GLOBAL_STATS_LEN; i++) {
			memcpy(p, igbvf_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static const struct ethtool_ops igbvf_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS,
	.get_drvinfo		= igbvf_get_drvinfo,
	.get_regs_len		= igbvf_get_regs_len,
	.get_regs		= igbvf_get_regs,
	.get_wol		= igbvf_get_wol,
	.set_wol		= igbvf_set_wol,
	.get_msglevel		= igbvf_get_msglevel,
	.set_msglevel		= igbvf_set_msglevel,
	.nway_reset		= igbvf_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_eeprom_len		= igbvf_get_eeprom_len,
	.get_eeprom		= igbvf_get_eeprom,
	.set_eeprom		= igbvf_set_eeprom,
	.get_ringparam		= igbvf_get_ringparam,
	.set_ringparam		= igbvf_set_ringparam,
	.get_pauseparam		= igbvf_get_pauseparam,
	.set_pauseparam		= igbvf_set_pauseparam,
	.self_test		= igbvf_diag_test,
	.get_sset_count		= igbvf_get_sset_count,
	.get_strings		= igbvf_get_strings,
	.get_ethtool_stats	= igbvf_get_ethtool_stats,
	.get_coalesce		= igbvf_get_coalesce,
	.set_coalesce		= igbvf_set_coalesce,
	.get_link_ksettings	= igbvf_get_link_ksettings,
	.set_link_ksettings	= igbvf_set_link_ksettings,
};

void igbvf_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &igbvf_ethtool_ops;
}
