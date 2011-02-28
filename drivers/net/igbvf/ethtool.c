/*******************************************************************************

  Intel(R) 82576 Virtual Function Linux driver
  Copyright(c) 2009 - 2010 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

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

static int igbvf_get_settings(struct net_device *netdev,
                              struct ethtool_cmd *ecmd)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 status;

	ecmd->supported   = SUPPORTED_1000baseT_Full;

	ecmd->advertising = ADVERTISED_1000baseT_Full;

	ecmd->port = -1;
	ecmd->transceiver = XCVR_DUMMY1;

	status = er32(STATUS);
	if (status & E1000_STATUS_LU) {
		if (status & E1000_STATUS_SPEED_1000)
			ecmd->speed = 1000;
		else if (status & E1000_STATUS_SPEED_100)
			ecmd->speed = 100;
		else
			ecmd->speed = 10;

		if (status & E1000_STATUS_FD)
			ecmd->duplex = DUPLEX_FULL;
		else
			ecmd->duplex = DUPLEX_HALF;
	} else {
		ecmd->speed = -1;
		ecmd->duplex = -1;
	}

	ecmd->autoneg = AUTONEG_DISABLE;

	return 0;
}

static int igbvf_set_settings(struct net_device *netdev,
                              struct ethtool_cmd *ecmd)
{
	return -EOPNOTSUPP;
}

static void igbvf_get_pauseparam(struct net_device *netdev,
                                 struct ethtool_pauseparam *pause)
{
	return;
}

static int igbvf_set_pauseparam(struct net_device *netdev,
                                struct ethtool_pauseparam *pause)
{
	return -EOPNOTSUPP;
}

static u32 igbvf_get_rx_csum(struct net_device *netdev)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	return !(adapter->flags & IGBVF_FLAG_RX_CSUM_DISABLED);
}

static int igbvf_set_rx_csum(struct net_device *netdev, u32 data)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);

	if (data)
		adapter->flags &= ~IGBVF_FLAG_RX_CSUM_DISABLED;
	else
		adapter->flags |= IGBVF_FLAG_RX_CSUM_DISABLED;

	return 0;
}

static u32 igbvf_get_tx_csum(struct net_device *netdev)
{
	return (netdev->features & NETIF_F_IP_CSUM) != 0;
}

static int igbvf_set_tx_csum(struct net_device *netdev, u32 data)
{
	if (data)
		netdev->features |= (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);
	else
		netdev->features &= ~(NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);
	return 0;
}

static int igbvf_set_tso(struct net_device *netdev, u32 data)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);

	if (data) {
		netdev->features |= NETIF_F_TSO;
		netdev->features |= NETIF_F_TSO6;
	} else {
		netdev->features &= ~NETIF_F_TSO;
		netdev->features &= ~NETIF_F_TSO6;
	}

	dev_info(&adapter->pdev->dev, "TSO is %s\n",
	         data ? "Enabled" : "Disabled");
	return 0;
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

	regs->version = (1 << 24) | (adapter->pdev->revision << 16) |
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
	char firmware_version[32] = "N/A";

	strncpy(drvinfo->driver,  igbvf_driver_name, 32);
	strncpy(drvinfo->version, igbvf_driver_version, 32);
	strncpy(drvinfo->fw_version, firmware_version, 32);
	strncpy(drvinfo->bus_info, pci_name(adapter->pdev), 32);
	drvinfo->regdump_len = igbvf_get_regs_len(netdev);
	drvinfo->eedump_len = igbvf_get_eeprom_len(netdev);
}

static void igbvf_get_ringparam(struct net_device *netdev,
                                struct ethtool_ringparam *ring)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	struct igbvf_ring *tx_ring = adapter->tx_ring;
	struct igbvf_ring *rx_ring = adapter->rx_ring;

	ring->rx_max_pending = IGBVF_MAX_RXD;
	ring->tx_max_pending = IGBVF_MAX_TXD;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = rx_ring->count;
	ring->tx_pending = tx_ring->count;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static int igbvf_set_ringparam(struct net_device *netdev,
                               struct ethtool_ringparam *ring)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	struct igbvf_ring *temp_ring;
	int err = 0;
	u32 new_rx_count, new_tx_count;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	new_rx_count = max(ring->rx_pending, (u32)IGBVF_MIN_RXD);
	new_rx_count = min(new_rx_count, (u32)IGBVF_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, REQ_RX_DESCRIPTOR_MULTIPLE);

	new_tx_count = max(ring->tx_pending, (u32)IGBVF_MIN_TXD);
	new_tx_count = min(new_tx_count, (u32)IGBVF_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, REQ_TX_DESCRIPTOR_MULTIPLE);

	if ((new_tx_count == adapter->tx_ring->count) &&
	    (new_rx_count == adapter->rx_ring->count)) {
		/* nothing to do */
		return 0;
	}

	while (test_and_set_bit(__IGBVF_RESETTING, &adapter->state))
		msleep(1);

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

	/*
	 * We can't just free everything and then setup again,
	 * because the ISRs in MSI-X mode get passed pointers
	 * to the tx and rx ring structs.
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

		memcpy(adapter->rx_ring, temp_ring,sizeof(struct igbvf_ring));
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

	hw->mac.ops.check_for_link(hw);

	if (!(er32(STATUS) & E1000_STATUS_LU))
		*data = 1;

	return *data;
}

static void igbvf_diag_test(struct net_device *netdev,
                            struct ethtool_test *eth_test, u64 *data)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);

	set_bit(__IGBVF_TESTING, &adapter->state);

	/*
	 * Link test performed before hardware reset so autoneg doesn't
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

static int igbvf_phys_id(struct net_device *netdev, u32 data)
{
	return 0;
}

static int igbvf_get_coalesce(struct net_device *netdev,
                              struct ethtool_coalesce *ec)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);

	if (adapter->itr_setting <= 3)
		ec->rx_coalesce_usecs = adapter->itr_setting;
	else
		ec->rx_coalesce_usecs = adapter->itr_setting >> 2;

	return 0;
}

static int igbvf_set_coalesce(struct net_device *netdev,
                              struct ethtool_coalesce *ec)
{
	struct igbvf_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	if ((ec->rx_coalesce_usecs > IGBVF_MAX_ITR_USECS) ||
	    ((ec->rx_coalesce_usecs > 3) &&
	     (ec->rx_coalesce_usecs < IGBVF_MIN_ITR_USECS)) ||
	    (ec->rx_coalesce_usecs == 2))
		return -EINVAL;

	/* convert to rate of irq's per second */
	if (ec->rx_coalesce_usecs && ec->rx_coalesce_usecs <= 3) {
		adapter->itr = IGBVF_START_ITR;
		adapter->itr_setting = ec->rx_coalesce_usecs;
	} else {
		adapter->itr = ec->rx_coalesce_usecs << 2;
		adapter->itr_setting = adapter->itr;
	}

	writel(adapter->itr,
	       hw->hw_addr + adapter->rx_ring[0].itr_register);

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
	switch(stringset) {
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
	.get_settings		= igbvf_get_settings,
	.set_settings		= igbvf_set_settings,
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
	.get_rx_csum            = igbvf_get_rx_csum,
	.set_rx_csum            = igbvf_set_rx_csum,
	.get_tx_csum		= igbvf_get_tx_csum,
	.set_tx_csum		= igbvf_set_tx_csum,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= ethtool_op_set_sg,
	.get_tso		= ethtool_op_get_tso,
	.set_tso		= igbvf_set_tso,
	.self_test		= igbvf_diag_test,
	.get_sset_count		= igbvf_get_sset_count,
	.get_strings		= igbvf_get_strings,
	.phys_id		= igbvf_phys_id,
	.get_ethtool_stats	= igbvf_get_ethtool_stats,
	.get_coalesce		= igbvf_get_coalesce,
	.set_coalesce		= igbvf_set_coalesce,
};

void igbvf_set_ethtool_ops(struct net_device *netdev)
{
	/* have to "undeclare" const on this struct to remove warnings */
	SET_ETHTOOL_OPS(netdev, (struct ethtool_ops *)&igbvf_ethtool_ops);
}
