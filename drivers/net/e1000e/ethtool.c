/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2008 Intel Corporation.

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
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/* ethtool support for e1000 */

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "e1000.h"

struct e1000_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define E1000_STAT(m) sizeof(((struct e1000_adapter *)0)->m), \
		      offsetof(struct e1000_adapter, m)
static const struct e1000_stats e1000_gstrings_stats[] = {
	{ "rx_packets", E1000_STAT(stats.gprc) },
	{ "tx_packets", E1000_STAT(stats.gptc) },
	{ "rx_bytes", E1000_STAT(stats.gorc) },
	{ "tx_bytes", E1000_STAT(stats.gotc) },
	{ "rx_broadcast", E1000_STAT(stats.bprc) },
	{ "tx_broadcast", E1000_STAT(stats.bptc) },
	{ "rx_multicast", E1000_STAT(stats.mprc) },
	{ "tx_multicast", E1000_STAT(stats.mptc) },
	{ "rx_errors", E1000_STAT(net_stats.rx_errors) },
	{ "tx_errors", E1000_STAT(net_stats.tx_errors) },
	{ "tx_dropped", E1000_STAT(net_stats.tx_dropped) },
	{ "multicast", E1000_STAT(stats.mprc) },
	{ "collisions", E1000_STAT(stats.colc) },
	{ "rx_length_errors", E1000_STAT(net_stats.rx_length_errors) },
	{ "rx_over_errors", E1000_STAT(net_stats.rx_over_errors) },
	{ "rx_crc_errors", E1000_STAT(stats.crcerrs) },
	{ "rx_frame_errors", E1000_STAT(net_stats.rx_frame_errors) },
	{ "rx_no_buffer_count", E1000_STAT(stats.rnbc) },
	{ "rx_missed_errors", E1000_STAT(stats.mpc) },
	{ "tx_aborted_errors", E1000_STAT(stats.ecol) },
	{ "tx_carrier_errors", E1000_STAT(stats.tncrs) },
	{ "tx_fifo_errors", E1000_STAT(net_stats.tx_fifo_errors) },
	{ "tx_heartbeat_errors", E1000_STAT(net_stats.tx_heartbeat_errors) },
	{ "tx_window_errors", E1000_STAT(stats.latecol) },
	{ "tx_abort_late_coll", E1000_STAT(stats.latecol) },
	{ "tx_deferred_ok", E1000_STAT(stats.dc) },
	{ "tx_single_coll_ok", E1000_STAT(stats.scc) },
	{ "tx_multi_coll_ok", E1000_STAT(stats.mcc) },
	{ "tx_timeout_count", E1000_STAT(tx_timeout_count) },
	{ "tx_restart_queue", E1000_STAT(restart_queue) },
	{ "rx_long_length_errors", E1000_STAT(stats.roc) },
	{ "rx_short_length_errors", E1000_STAT(stats.ruc) },
	{ "rx_align_errors", E1000_STAT(stats.algnerrc) },
	{ "tx_tcp_seg_good", E1000_STAT(stats.tsctc) },
	{ "tx_tcp_seg_failed", E1000_STAT(stats.tsctfc) },
	{ "rx_flow_control_xon", E1000_STAT(stats.xonrxc) },
	{ "rx_flow_control_xoff", E1000_STAT(stats.xoffrxc) },
	{ "tx_flow_control_xon", E1000_STAT(stats.xontxc) },
	{ "tx_flow_control_xoff", E1000_STAT(stats.xofftxc) },
	{ "rx_long_byte_count", E1000_STAT(stats.gorc) },
	{ "rx_csum_offload_good", E1000_STAT(hw_csum_good) },
	{ "rx_csum_offload_errors", E1000_STAT(hw_csum_err) },
	{ "rx_header_split", E1000_STAT(rx_hdr_split) },
	{ "alloc_rx_buff_failed", E1000_STAT(alloc_rx_buff_failed) },
	{ "tx_smbus", E1000_STAT(stats.mgptc) },
	{ "rx_smbus", E1000_STAT(stats.mgprc) },
	{ "dropped_smbus", E1000_STAT(stats.mgpdc) },
	{ "rx_dma_failed", E1000_STAT(rx_dma_failed) },
	{ "tx_dma_failed", E1000_STAT(tx_dma_failed) },
};

#define E1000_GLOBAL_STATS_LEN	ARRAY_SIZE(e1000_gstrings_stats)
#define E1000_STATS_LEN (E1000_GLOBAL_STATS_LEN)
static const char e1000_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)", "Eeprom test    (offline)",
	"Interrupt test (offline)", "Loopback test  (offline)",
	"Link test   (on/offline)"
};
#define E1000_TEST_LEN ARRAY_SIZE(e1000_gstrings_test)

static int e1000_get_settings(struct net_device *netdev,
			      struct ethtool_cmd *ecmd)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 status;

	if (hw->phy.media_type == e1000_media_type_copper) {

		ecmd->supported = (SUPPORTED_10baseT_Half |
				   SUPPORTED_10baseT_Full |
				   SUPPORTED_100baseT_Half |
				   SUPPORTED_100baseT_Full |
				   SUPPORTED_1000baseT_Full |
				   SUPPORTED_Autoneg |
				   SUPPORTED_TP);
		if (hw->phy.type == e1000_phy_ife)
			ecmd->supported &= ~SUPPORTED_1000baseT_Full;
		ecmd->advertising = ADVERTISED_TP;

		if (hw->mac.autoneg == 1) {
			ecmd->advertising |= ADVERTISED_Autoneg;
			/* the e1000 autoneg seems to match ethtool nicely */
			ecmd->advertising |= hw->phy.autoneg_advertised;
		}

		ecmd->port = PORT_TP;
		ecmd->phy_address = hw->phy.addr;
		ecmd->transceiver = XCVR_INTERNAL;

	} else {
		ecmd->supported   = (SUPPORTED_1000baseT_Full |
				     SUPPORTED_FIBRE |
				     SUPPORTED_Autoneg);

		ecmd->advertising = (ADVERTISED_1000baseT_Full |
				     ADVERTISED_FIBRE |
				     ADVERTISED_Autoneg);

		ecmd->port = PORT_FIBRE;
		ecmd->transceiver = XCVR_EXTERNAL;
	}

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

	ecmd->autoneg = ((hw->phy.media_type == e1000_media_type_fiber) ||
			 hw->mac.autoneg) ? AUTONEG_ENABLE : AUTONEG_DISABLE;
	return 0;
}

static u32 e1000_get_link(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 status;
	
	status = er32(STATUS);
	return (status & E1000_STATUS_LU);
}

static int e1000_set_spd_dplx(struct e1000_adapter *adapter, u16 spddplx)
{
	struct e1000_mac_info *mac = &adapter->hw.mac;

	mac->autoneg = 0;

	/* Fiber NICs only allow 1000 gbps Full duplex */
	if ((adapter->hw.phy.media_type == e1000_media_type_fiber) &&
		spddplx != (SPEED_1000 + DUPLEX_FULL)) {
		ndev_err(adapter->netdev, "Unsupported Speed/Duplex "
			 "configuration\n");
		return -EINVAL;
	}

	switch (spddplx) {
	case SPEED_10 + DUPLEX_HALF:
		mac->forced_speed_duplex = ADVERTISE_10_HALF;
		break;
	case SPEED_10 + DUPLEX_FULL:
		mac->forced_speed_duplex = ADVERTISE_10_FULL;
		break;
	case SPEED_100 + DUPLEX_HALF:
		mac->forced_speed_duplex = ADVERTISE_100_HALF;
		break;
	case SPEED_100 + DUPLEX_FULL:
		mac->forced_speed_duplex = ADVERTISE_100_FULL;
		break;
	case SPEED_1000 + DUPLEX_FULL:
		mac->autoneg = 1;
		adapter->hw.phy.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	case SPEED_1000 + DUPLEX_HALF: /* not supported */
	default:
		ndev_err(adapter->netdev, "Unsupported Speed/Duplex "
			 "configuration\n");
		return -EINVAL;
	}
	return 0;
}

static int e1000_set_settings(struct net_device *netdev,
			      struct ethtool_cmd *ecmd)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	/*
	 * When SoL/IDER sessions are active, autoneg/speed/duplex
	 * cannot be changed
	 */
	if (e1000_check_reset_block(hw)) {
		ndev_err(netdev, "Cannot change link "
			 "characteristics when SoL/IDER is active.\n");
		return -EINVAL;
	}

	while (test_and_set_bit(__E1000_RESETTING, &adapter->state))
		msleep(1);

	if (ecmd->autoneg == AUTONEG_ENABLE) {
		hw->mac.autoneg = 1;
		if (hw->phy.media_type == e1000_media_type_fiber)
			hw->phy.autoneg_advertised = ADVERTISED_1000baseT_Full |
						     ADVERTISED_FIBRE |
						     ADVERTISED_Autoneg;
		else
			hw->phy.autoneg_advertised = ecmd->advertising |
						     ADVERTISED_TP |
						     ADVERTISED_Autoneg;
		ecmd->advertising = hw->phy.autoneg_advertised;
		if (adapter->fc_autoneg)
			hw->fc.original_type = e1000_fc_default;
	} else {
		if (e1000_set_spd_dplx(adapter, ecmd->speed + ecmd->duplex)) {
			clear_bit(__E1000_RESETTING, &adapter->state);
			return -EINVAL;
		}
	}

	/* reset the link */

	if (netif_running(adapter->netdev)) {
		e1000e_down(adapter);
		e1000e_up(adapter);
	} else {
		e1000e_reset(adapter);
	}

	clear_bit(__E1000_RESETTING, &adapter->state);
	return 0;
}

static void e1000_get_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pause)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	pause->autoneg =
		(adapter->fc_autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE);

	if (hw->fc.type == e1000_fc_rx_pause) {
		pause->rx_pause = 1;
	} else if (hw->fc.type == e1000_fc_tx_pause) {
		pause->tx_pause = 1;
	} else if (hw->fc.type == e1000_fc_full) {
		pause->rx_pause = 1;
		pause->tx_pause = 1;
	}
}

static int e1000_set_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	int retval = 0;

	adapter->fc_autoneg = pause->autoneg;

	while (test_and_set_bit(__E1000_RESETTING, &adapter->state))
		msleep(1);

	if (pause->rx_pause && pause->tx_pause)
		hw->fc.type = e1000_fc_full;
	else if (pause->rx_pause && !pause->tx_pause)
		hw->fc.type = e1000_fc_rx_pause;
	else if (!pause->rx_pause && pause->tx_pause)
		hw->fc.type = e1000_fc_tx_pause;
	else if (!pause->rx_pause && !pause->tx_pause)
		hw->fc.type = e1000_fc_none;

	hw->fc.original_type = hw->fc.type;

	if (adapter->fc_autoneg == AUTONEG_ENABLE) {
		hw->fc.type = e1000_fc_default;
		if (netif_running(adapter->netdev)) {
			e1000e_down(adapter);
			e1000e_up(adapter);
		} else {
			e1000e_reset(adapter);
		}
	} else {
		retval = ((hw->phy.media_type == e1000_media_type_fiber) ?
			  hw->mac.ops.setup_link(hw) : e1000e_force_mac_fc(hw));
	}

	clear_bit(__E1000_RESETTING, &adapter->state);
	return retval;
}

static u32 e1000_get_rx_csum(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	return (adapter->flags & FLAG_RX_CSUM_ENABLED);
}

static int e1000_set_rx_csum(struct net_device *netdev, u32 data)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	if (data)
		adapter->flags |= FLAG_RX_CSUM_ENABLED;
	else
		adapter->flags &= ~FLAG_RX_CSUM_ENABLED;

	if (netif_running(netdev))
		e1000e_reinit_locked(adapter);
	else
		e1000e_reset(adapter);
	return 0;
}

static u32 e1000_get_tx_csum(struct net_device *netdev)
{
	return ((netdev->features & NETIF_F_HW_CSUM) != 0);
}

static int e1000_set_tx_csum(struct net_device *netdev, u32 data)
{
	if (data)
		netdev->features |= NETIF_F_HW_CSUM;
	else
		netdev->features &= ~NETIF_F_HW_CSUM;

	return 0;
}

static int e1000_set_tso(struct net_device *netdev, u32 data)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	if (data) {
		netdev->features |= NETIF_F_TSO;
		netdev->features |= NETIF_F_TSO6;
	} else {
		netdev->features &= ~NETIF_F_TSO;
		netdev->features &= ~NETIF_F_TSO6;
	}

	ndev_info(netdev, "TSO is %s\n",
		  data ? "Enabled" : "Disabled");
	adapter->flags |= FLAG_TSO_FORCE;
	return 0;
}

static u32 e1000_get_msglevel(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	return adapter->msg_enable;
}

static void e1000_set_msglevel(struct net_device *netdev, u32 data)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	adapter->msg_enable = data;
}

static int e1000_get_regs_len(struct net_device *netdev)
{
#define E1000_REGS_LEN 32 /* overestimate */
	return E1000_REGS_LEN * sizeof(u32);
}

static void e1000_get_regs(struct net_device *netdev,
			   struct ethtool_regs *regs, void *p)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u16 phy_data;
	u8 revision_id;

	memset(p, 0, E1000_REGS_LEN * sizeof(u32));

	pci_read_config_byte(adapter->pdev, PCI_REVISION_ID, &revision_id);

	regs->version = (1 << 24) | (revision_id << 16) | adapter->pdev->device;

	regs_buff[0]  = er32(CTRL);
	regs_buff[1]  = er32(STATUS);

	regs_buff[2]  = er32(RCTL);
	regs_buff[3]  = er32(RDLEN);
	regs_buff[4]  = er32(RDH);
	regs_buff[5]  = er32(RDT);
	regs_buff[6]  = er32(RDTR);

	regs_buff[7]  = er32(TCTL);
	regs_buff[8]  = er32(TDLEN);
	regs_buff[9]  = er32(TDH);
	regs_buff[10] = er32(TDT);
	regs_buff[11] = er32(TIDV);

	regs_buff[12] = adapter->hw.phy.type;  /* PHY type (IGP=1, M88=0) */
	if (hw->phy.type == e1000_phy_m88) {
		e1e_rphy(hw, M88E1000_PHY_SPEC_STATUS, &phy_data);
		regs_buff[13] = (u32)phy_data; /* cable length */
		regs_buff[14] = 0;  /* Dummy (to align w/ IGP phy reg dump) */
		regs_buff[15] = 0;  /* Dummy (to align w/ IGP phy reg dump) */
		regs_buff[16] = 0;  /* Dummy (to align w/ IGP phy reg dump) */
		e1e_rphy(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
		regs_buff[17] = (u32)phy_data; /* extended 10bt distance */
		regs_buff[18] = regs_buff[13]; /* cable polarity */
		regs_buff[19] = 0;  /* Dummy (to align w/ IGP phy reg dump) */
		regs_buff[20] = regs_buff[17]; /* polarity correction */
		/* phy receive errors */
		regs_buff[22] = adapter->phy_stats.receive_errors;
		regs_buff[23] = regs_buff[13]; /* mdix mode */
	}
	regs_buff[21] = adapter->phy_stats.idle_errors;  /* phy idle errors */
	e1e_rphy(hw, PHY_1000T_STATUS, &phy_data);
	regs_buff[24] = (u32)phy_data;  /* phy local receiver status */
	regs_buff[25] = regs_buff[24];  /* phy remote receiver status */
}

static int e1000_get_eeprom_len(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	return adapter->hw.nvm.word_size * 2;
}

static int e1000_get_eeprom(struct net_device *netdev,
			    struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u16 *eeprom_buff;
	int first_word;
	int last_word;
	int ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EINVAL;

	eeprom->magic = adapter->pdev->vendor | (adapter->pdev->device << 16);

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;

	eeprom_buff = kmalloc(sizeof(u16) *
			(last_word - first_word + 1), GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	if (hw->nvm.type == e1000_nvm_eeprom_spi) {
		ret_val = e1000_read_nvm(hw, first_word,
					 last_word - first_word + 1,
					 eeprom_buff);
	} else {
		for (i = 0; i < last_word - first_word + 1; i++) {
			ret_val = e1000_read_nvm(hw, first_word + i, 1,
						      &eeprom_buff[i]);
			if (ret_val) {
				/* a read error occurred, throw away the
				 * result */
				memset(eeprom_buff, 0xff, sizeof(eeprom_buff));
				break;
			}
		}
	}

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(bytes, (u8 *)eeprom_buff + (eeprom->offset & 1), eeprom->len);
	kfree(eeprom_buff);

	return ret_val;
}

static int e1000_set_eeprom(struct net_device *netdev,
			    struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u16 *eeprom_buff;
	void *ptr;
	int max_len;
	int first_word;
	int last_word;
	int ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EOPNOTSUPP;

	if (eeprom->magic != (adapter->pdev->vendor | (adapter->pdev->device << 16)))
		return -EFAULT;

	max_len = hw->nvm.word_size * 2;

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;
	eeprom_buff = kmalloc(max_len, GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	ptr = (void *)eeprom_buff;

	if (eeprom->offset & 1) {
		/* need read/modify/write of first changed EEPROM word */
		/* only the second byte of the word is being modified */
		ret_val = e1000_read_nvm(hw, first_word, 1, &eeprom_buff[0]);
		ptr++;
	}
	if (((eeprom->offset + eeprom->len) & 1) && (ret_val == 0))
		/* need read/modify/write of last changed EEPROM word */
		/* only the first byte of the word is being modified */
		ret_val = e1000_read_nvm(hw, last_word, 1,
				  &eeprom_buff[last_word - first_word]);

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(ptr, bytes, eeprom->len);

	for (i = 0; i < last_word - first_word + 1; i++)
		eeprom_buff[i] = cpu_to_le16(eeprom_buff[i]);

	ret_val = e1000_write_nvm(hw, first_word,
				  last_word - first_word + 1, eeprom_buff);

	/*
	 * Update the checksum over the first part of the EEPROM if needed
	 * and flush shadow RAM for 82573 controllers
	 */
	if ((ret_val == 0) && ((first_word <= NVM_CHECKSUM_REG) ||
			       (hw->mac.type == e1000_82573)))
		e1000e_update_nvm_checksum(hw);

	kfree(eeprom_buff);
	return ret_val;
}

static void e1000_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	char firmware_version[32];
	u16 eeprom_data;

	strncpy(drvinfo->driver,  e1000e_driver_name, 32);
	strncpy(drvinfo->version, e1000e_driver_version, 32);

	/*
	 * EEPROM image version # is reported as firmware version # for
	 * PCI-E controllers
	 */
	e1000_read_nvm(&adapter->hw, 5, 1, &eeprom_data);
	sprintf(firmware_version, "%d.%d-%d",
		(eeprom_data & 0xF000) >> 12,
		(eeprom_data & 0x0FF0) >> 4,
		eeprom_data & 0x000F);

	strncpy(drvinfo->fw_version, firmware_version, 32);
	strncpy(drvinfo->bus_info, pci_name(adapter->pdev), 32);
	drvinfo->regdump_len = e1000_get_regs_len(netdev);
	drvinfo->eedump_len = e1000_get_eeprom_len(netdev);
}

static void e1000_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_ring *tx_ring = adapter->tx_ring;
	struct e1000_ring *rx_ring = adapter->rx_ring;

	ring->rx_max_pending = E1000_MAX_RXD;
	ring->tx_max_pending = E1000_MAX_TXD;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = rx_ring->count;
	ring->tx_pending = tx_ring->count;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}

static int e1000_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_ring *tx_ring, *tx_old;
	struct e1000_ring *rx_ring, *rx_old;
	int err;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	while (test_and_set_bit(__E1000_RESETTING, &adapter->state))
		msleep(1);

	if (netif_running(adapter->netdev))
		e1000e_down(adapter);

	tx_old = adapter->tx_ring;
	rx_old = adapter->rx_ring;

	err = -ENOMEM;
	tx_ring = kzalloc(sizeof(struct e1000_ring), GFP_KERNEL);
	if (!tx_ring)
		goto err_alloc_tx;
	/*
	 * use a memcpy to save any previously configured
	 * items like napi structs from having to be
	 * reinitialized
	 */
	memcpy(tx_ring, tx_old, sizeof(struct e1000_ring));

	rx_ring = kzalloc(sizeof(struct e1000_ring), GFP_KERNEL);
	if (!rx_ring)
		goto err_alloc_rx;
	memcpy(rx_ring, rx_old, sizeof(struct e1000_ring));

	adapter->tx_ring = tx_ring;
	adapter->rx_ring = rx_ring;

	rx_ring->count = max(ring->rx_pending, (u32)E1000_MIN_RXD);
	rx_ring->count = min(rx_ring->count, (u32)(E1000_MAX_RXD));
	rx_ring->count = ALIGN(rx_ring->count, REQ_RX_DESCRIPTOR_MULTIPLE);

	tx_ring->count = max(ring->tx_pending, (u32)E1000_MIN_TXD);
	tx_ring->count = min(tx_ring->count, (u32)(E1000_MAX_TXD));
	tx_ring->count = ALIGN(tx_ring->count, REQ_TX_DESCRIPTOR_MULTIPLE);

	if (netif_running(adapter->netdev)) {
		/* Try to get new resources before deleting old */
		err = e1000e_setup_rx_resources(adapter);
		if (err)
			goto err_setup_rx;
		err = e1000e_setup_tx_resources(adapter);
		if (err)
			goto err_setup_tx;

		/*
		 * restore the old in order to free it,
		 * then add in the new
		 */
		adapter->rx_ring = rx_old;
		adapter->tx_ring = tx_old;
		e1000e_free_rx_resources(adapter);
		e1000e_free_tx_resources(adapter);
		kfree(tx_old);
		kfree(rx_old);
		adapter->rx_ring = rx_ring;
		adapter->tx_ring = tx_ring;
		err = e1000e_up(adapter);
		if (err)
			goto err_setup;
	}

	clear_bit(__E1000_RESETTING, &adapter->state);
	return 0;
err_setup_tx:
	e1000e_free_rx_resources(adapter);
err_setup_rx:
	adapter->rx_ring = rx_old;
	adapter->tx_ring = tx_old;
	kfree(rx_ring);
err_alloc_rx:
	kfree(tx_ring);
err_alloc_tx:
	e1000e_up(adapter);
err_setup:
	clear_bit(__E1000_RESETTING, &adapter->state);
	return err;
}

static bool reg_pattern_test(struct e1000_adapter *adapter, u64 *data,
			     int reg, int offset, u32 mask, u32 write)
{
	u32 pat, val;
	static const u32 test[] =
		{0x5A5A5A5A, 0xA5A5A5A5, 0x00000000, 0xFFFFFFFF};
	for (pat = 0; pat < ARRAY_SIZE(test); pat++) {
		E1000_WRITE_REG_ARRAY(&adapter->hw, reg, offset,
				      (test[pat] & write));
		val = E1000_READ_REG_ARRAY(&adapter->hw, reg, offset);
		if (val != (test[pat] & write & mask)) {
			ndev_err(adapter->netdev, "pattern test reg %04X "
				 "failed: got 0x%08X expected 0x%08X\n",
				 reg + offset,
				 val, (test[pat] & write & mask));
			*data = reg;
			return 1;
		}
	}
	return 0;
}

static bool reg_set_and_check(struct e1000_adapter *adapter, u64 *data,
			      int reg, u32 mask, u32 write)
{
	u32 val;
	__ew32(&adapter->hw, reg, write & mask);
	val = __er32(&adapter->hw, reg);
	if ((write & mask) != (val & mask)) {
		ndev_err(adapter->netdev, "set/check reg %04X test failed: "
			 "got 0x%08X expected 0x%08X\n", reg, (val & mask),
			 (write & mask));
		*data = reg;
		return 1;
	}
	return 0;
}
#define REG_PATTERN_TEST_ARRAY(reg, offset, mask, write)                       \
	do {                                                                   \
		if (reg_pattern_test(adapter, data, reg, offset, mask, write)) \
			return 1;                                              \
	} while (0)
#define REG_PATTERN_TEST(reg, mask, write)                                     \
	REG_PATTERN_TEST_ARRAY(reg, 0, mask, write)

#define REG_SET_AND_CHECK(reg, mask, write)                                    \
	do {                                                                   \
		if (reg_set_and_check(adapter, data, reg, mask, write))        \
			return 1;                                              \
	} while (0)

static int e1000_reg_test(struct e1000_adapter *adapter, u64 *data)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_mac_info *mac = &adapter->hw.mac;
	struct net_device *netdev = adapter->netdev;
	u32 value;
	u32 before;
	u32 after;
	u32 i;
	u32 toggle;

	/*
	 * The status register is Read Only, so a write should fail.
	 * Some bits that get toggled are ignored.
	 */
	switch (mac->type) {
	/* there are several bits on newer hardware that are r/w */
	case e1000_82571:
	case e1000_82572:
	case e1000_80003es2lan:
		toggle = 0x7FFFF3FF;
		break;
	case e1000_82573:
	case e1000_ich8lan:
	case e1000_ich9lan:
		toggle = 0x7FFFF033;
		break;
	default:
		toggle = 0xFFFFF833;
		break;
	}

	before = er32(STATUS);
	value = (er32(STATUS) & toggle);
	ew32(STATUS, toggle);
	after = er32(STATUS) & toggle;
	if (value != after) {
		ndev_err(netdev, "failed STATUS register test got: "
			 "0x%08X expected: 0x%08X\n", after, value);
		*data = 1;
		return 1;
	}
	/* restore previous status */
	ew32(STATUS, before);

	if (!(adapter->flags & FLAG_IS_ICH)) {
		REG_PATTERN_TEST(E1000_FCAL, 0xFFFFFFFF, 0xFFFFFFFF);
		REG_PATTERN_TEST(E1000_FCAH, 0x0000FFFF, 0xFFFFFFFF);
		REG_PATTERN_TEST(E1000_FCT, 0x0000FFFF, 0xFFFFFFFF);
		REG_PATTERN_TEST(E1000_VET, 0x0000FFFF, 0xFFFFFFFF);
	}

	REG_PATTERN_TEST(E1000_RDTR, 0x0000FFFF, 0xFFFFFFFF);
	REG_PATTERN_TEST(E1000_RDBAH, 0xFFFFFFFF, 0xFFFFFFFF);
	REG_PATTERN_TEST(E1000_RDLEN, 0x000FFF80, 0x000FFFFF);
	REG_PATTERN_TEST(E1000_RDH, 0x0000FFFF, 0x0000FFFF);
	REG_PATTERN_TEST(E1000_RDT, 0x0000FFFF, 0x0000FFFF);
	REG_PATTERN_TEST(E1000_FCRTH, 0x0000FFF8, 0x0000FFF8);
	REG_PATTERN_TEST(E1000_FCTTV, 0x0000FFFF, 0x0000FFFF);
	REG_PATTERN_TEST(E1000_TIPG, 0x3FFFFFFF, 0x3FFFFFFF);
	REG_PATTERN_TEST(E1000_TDBAH, 0xFFFFFFFF, 0xFFFFFFFF);
	REG_PATTERN_TEST(E1000_TDLEN, 0x000FFF80, 0x000FFFFF);

	REG_SET_AND_CHECK(E1000_RCTL, 0xFFFFFFFF, 0x00000000);

	before = ((adapter->flags & FLAG_IS_ICH) ? 0x06C3B33E : 0x06DFB3FE);
	REG_SET_AND_CHECK(E1000_RCTL, before, 0x003FFFFB);
	REG_SET_AND_CHECK(E1000_TCTL, 0xFFFFFFFF, 0x00000000);

	REG_SET_AND_CHECK(E1000_RCTL, before, 0xFFFFFFFF);
	REG_PATTERN_TEST(E1000_RDBAL, 0xFFFFFFF0, 0xFFFFFFFF);
	if (!(adapter->flags & FLAG_IS_ICH))
		REG_PATTERN_TEST(E1000_TXCW, 0xC000FFFF, 0x0000FFFF);
	REG_PATTERN_TEST(E1000_TDBAL, 0xFFFFFFF0, 0xFFFFFFFF);
	REG_PATTERN_TEST(E1000_TIDV, 0x0000FFFF, 0x0000FFFF);
	for (i = 0; i < mac->rar_entry_count; i++)
		REG_PATTERN_TEST_ARRAY(E1000_RA, ((i << 1) + 1),
				       0x8003FFFF, 0xFFFFFFFF);

	for (i = 0; i < mac->mta_reg_count; i++)
		REG_PATTERN_TEST_ARRAY(E1000_MTA, i, 0xFFFFFFFF, 0xFFFFFFFF);

	*data = 0;
	return 0;
}

static int e1000_eeprom_test(struct e1000_adapter *adapter, u64 *data)
{
	u16 temp;
	u16 checksum = 0;
	u16 i;

	*data = 0;
	/* Read and add up the contents of the EEPROM */
	for (i = 0; i < (NVM_CHECKSUM_REG + 1); i++) {
		if ((e1000_read_nvm(&adapter->hw, i, 1, &temp)) < 0) {
			*data = 1;
			break;
		}
		checksum += temp;
	}

	/* If Checksum is not Correct return error else test passed */
	if ((checksum != (u16) NVM_SUM) && !(*data))
		*data = 2;

	return *data;
}

static irqreturn_t e1000_test_intr(int irq, void *data)
{
	struct net_device *netdev = (struct net_device *) data;
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	adapter->test_icr |= er32(ICR);

	return IRQ_HANDLED;
}

static int e1000_intr_test(struct e1000_adapter *adapter, u64 *data)
{
	struct net_device *netdev = adapter->netdev;
	struct e1000_hw *hw = &adapter->hw;
	u32 mask;
	u32 shared_int = 1;
	u32 irq = adapter->pdev->irq;
	int i;

	*data = 0;

	/* NOTE: we don't test MSI interrupts here, yet */
	/* Hook up test interrupt handler just for this test */
	if (!request_irq(irq, &e1000_test_intr, IRQF_PROBE_SHARED, netdev->name,
			 netdev)) {
		shared_int = 0;
	} else if (request_irq(irq, &e1000_test_intr, IRQF_SHARED,
		 netdev->name, netdev)) {
		*data = 1;
		return -1;
	}
	ndev_info(netdev, "testing %s interrupt\n",
		  (shared_int ? "shared" : "unshared"));

	/* Disable all the interrupts */
	ew32(IMC, 0xFFFFFFFF);
	msleep(10);

	/* Test each interrupt */
	for (i = 0; i < 10; i++) {
		if ((adapter->flags & FLAG_IS_ICH) && (i == 8))
			continue;

		/* Interrupt to test */
		mask = 1 << i;

		if (!shared_int) {
			/*
			 * Disable the interrupt to be reported in
			 * the cause register and then force the same
			 * interrupt and see if one gets posted.  If
			 * an interrupt was posted to the bus, the
			 * test failed.
			 */
			adapter->test_icr = 0;
			ew32(IMC, mask);
			ew32(ICS, mask);
			msleep(10);

			if (adapter->test_icr & mask) {
				*data = 3;
				break;
			}
		}

		/*
		 * Enable the interrupt to be reported in
		 * the cause register and then force the same
		 * interrupt and see if one gets posted.  If
		 * an interrupt was not posted to the bus, the
		 * test failed.
		 */
		adapter->test_icr = 0;
		ew32(IMS, mask);
		ew32(ICS, mask);
		msleep(10);

		if (!(adapter->test_icr & mask)) {
			*data = 4;
			break;
		}

		if (!shared_int) {
			/*
			 * Disable the other interrupts to be reported in
			 * the cause register and then force the other
			 * interrupts and see if any get posted.  If
			 * an interrupt was posted to the bus, the
			 * test failed.
			 */
			adapter->test_icr = 0;
			ew32(IMC, ~mask & 0x00007FFF);
			ew32(ICS, ~mask & 0x00007FFF);
			msleep(10);

			if (adapter->test_icr) {
				*data = 5;
				break;
			}
		}
	}

	/* Disable all the interrupts */
	ew32(IMC, 0xFFFFFFFF);
	msleep(10);

	/* Unhook test interrupt handler */
	free_irq(irq, netdev);

	return *data;
}

static void e1000_free_desc_rings(struct e1000_adapter *adapter)
{
	struct e1000_ring *tx_ring = &adapter->test_tx_ring;
	struct e1000_ring *rx_ring = &adapter->test_rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	int i;

	if (tx_ring->desc && tx_ring->buffer_info) {
		for (i = 0; i < tx_ring->count; i++) {
			if (tx_ring->buffer_info[i].dma)
				pci_unmap_single(pdev,
					tx_ring->buffer_info[i].dma,
					tx_ring->buffer_info[i].length,
					PCI_DMA_TODEVICE);
			if (tx_ring->buffer_info[i].skb)
				dev_kfree_skb(tx_ring->buffer_info[i].skb);
		}
	}

	if (rx_ring->desc && rx_ring->buffer_info) {
		for (i = 0; i < rx_ring->count; i++) {
			if (rx_ring->buffer_info[i].dma)
				pci_unmap_single(pdev,
					rx_ring->buffer_info[i].dma,
					2048, PCI_DMA_FROMDEVICE);
			if (rx_ring->buffer_info[i].skb)
				dev_kfree_skb(rx_ring->buffer_info[i].skb);
		}
	}

	if (tx_ring->desc) {
		dma_free_coherent(&pdev->dev, tx_ring->size, tx_ring->desc,
				  tx_ring->dma);
		tx_ring->desc = NULL;
	}
	if (rx_ring->desc) {
		dma_free_coherent(&pdev->dev, rx_ring->size, rx_ring->desc,
				  rx_ring->dma);
		rx_ring->desc = NULL;
	}

	kfree(tx_ring->buffer_info);
	tx_ring->buffer_info = NULL;
	kfree(rx_ring->buffer_info);
	rx_ring->buffer_info = NULL;
}

static int e1000_setup_desc_rings(struct e1000_adapter *adapter)
{
	struct e1000_ring *tx_ring = &adapter->test_tx_ring;
	struct e1000_ring *rx_ring = &adapter->test_rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;
	int i;
	int ret_val;

	/* Setup Tx descriptor ring and Tx buffers */

	if (!tx_ring->count)
		tx_ring->count = E1000_DEFAULT_TXD;

	tx_ring->buffer_info = kcalloc(tx_ring->count,
				       sizeof(struct e1000_buffer),
				       GFP_KERNEL);
	if (!(tx_ring->buffer_info)) {
		ret_val = 1;
		goto err_nomem;
	}

	tx_ring->size = tx_ring->count * sizeof(struct e1000_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);
	tx_ring->desc = dma_alloc_coherent(&pdev->dev, tx_ring->size,
					   &tx_ring->dma, GFP_KERNEL);
	if (!tx_ring->desc) {
		ret_val = 2;
		goto err_nomem;
	}
	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	ew32(TDBAL, ((u64) tx_ring->dma & 0x00000000FFFFFFFF));
	ew32(TDBAH, ((u64) tx_ring->dma >> 32));
	ew32(TDLEN, tx_ring->count * sizeof(struct e1000_tx_desc));
	ew32(TDH, 0);
	ew32(TDT, 0);
	ew32(TCTL, E1000_TCTL_PSP | E1000_TCTL_EN | E1000_TCTL_MULR |
	     E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT |
	     E1000_COLLISION_DISTANCE << E1000_COLD_SHIFT);

	for (i = 0; i < tx_ring->count; i++) {
		struct e1000_tx_desc *tx_desc = E1000_TX_DESC(*tx_ring, i);
		struct sk_buff *skb;
		unsigned int skb_size = 1024;

		skb = alloc_skb(skb_size, GFP_KERNEL);
		if (!skb) {
			ret_val = 3;
			goto err_nomem;
		}
		skb_put(skb, skb_size);
		tx_ring->buffer_info[i].skb = skb;
		tx_ring->buffer_info[i].length = skb->len;
		tx_ring->buffer_info[i].dma =
			pci_map_single(pdev, skb->data, skb->len,
				       PCI_DMA_TODEVICE);
		if (pci_dma_mapping_error(tx_ring->buffer_info[i].dma)) {
			ret_val = 4;
			goto err_nomem;
		}
		tx_desc->buffer_addr = cpu_to_le64(tx_ring->buffer_info[i].dma);
		tx_desc->lower.data = cpu_to_le32(skb->len);
		tx_desc->lower.data |= cpu_to_le32(E1000_TXD_CMD_EOP |
						   E1000_TXD_CMD_IFCS |
						   E1000_TXD_CMD_RS);
		tx_desc->upper.data = 0;
	}

	/* Setup Rx descriptor ring and Rx buffers */

	if (!rx_ring->count)
		rx_ring->count = E1000_DEFAULT_RXD;

	rx_ring->buffer_info = kcalloc(rx_ring->count,
				       sizeof(struct e1000_buffer),
				       GFP_KERNEL);
	if (!(rx_ring->buffer_info)) {
		ret_val = 5;
		goto err_nomem;
	}

	rx_ring->size = rx_ring->count * sizeof(struct e1000_rx_desc);
	rx_ring->desc = dma_alloc_coherent(&pdev->dev, rx_ring->size,
					   &rx_ring->dma, GFP_KERNEL);
	if (!rx_ring->desc) {
		ret_val = 6;
		goto err_nomem;
	}
	rx_ring->next_to_use = 0;
	rx_ring->next_to_clean = 0;

	rctl = er32(RCTL);
	ew32(RCTL, rctl & ~E1000_RCTL_EN);
	ew32(RDBAL, ((u64) rx_ring->dma & 0xFFFFFFFF));
	ew32(RDBAH, ((u64) rx_ring->dma >> 32));
	ew32(RDLEN, rx_ring->size);
	ew32(RDH, 0);
	ew32(RDT, 0);
	rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SZ_2048 |
		E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_LPE |
		E1000_RCTL_SBP | E1000_RCTL_SECRC |
		E1000_RCTL_LBM_NO | E1000_RCTL_RDMTS_HALF |
		(adapter->hw.mac.mc_filter_type << E1000_RCTL_MO_SHIFT);
	ew32(RCTL, rctl);

	for (i = 0; i < rx_ring->count; i++) {
		struct e1000_rx_desc *rx_desc = E1000_RX_DESC(*rx_ring, i);
		struct sk_buff *skb;

		skb = alloc_skb(2048 + NET_IP_ALIGN, GFP_KERNEL);
		if (!skb) {
			ret_val = 7;
			goto err_nomem;
		}
		skb_reserve(skb, NET_IP_ALIGN);
		rx_ring->buffer_info[i].skb = skb;
		rx_ring->buffer_info[i].dma =
			pci_map_single(pdev, skb->data, 2048,
				       PCI_DMA_FROMDEVICE);
		if (pci_dma_mapping_error(rx_ring->buffer_info[i].dma)) {
			ret_val = 8;
			goto err_nomem;
		}
		rx_desc->buffer_addr =
			cpu_to_le64(rx_ring->buffer_info[i].dma);
		memset(skb->data, 0x00, skb->len);
	}

	return 0;

err_nomem:
	e1000_free_desc_rings(adapter);
	return ret_val;
}

static void e1000_phy_disable_receiver(struct e1000_adapter *adapter)
{
	/* Write out to PHY registers 29 and 30 to disable the Receiver. */
	e1e_wphy(&adapter->hw, 29, 0x001F);
	e1e_wphy(&adapter->hw, 30, 0x8FFC);
	e1e_wphy(&adapter->hw, 29, 0x001A);
	e1e_wphy(&adapter->hw, 30, 0x8FF0);
}

static int e1000_integrated_phy_loopback(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl_reg = 0;
	u32 stat_reg = 0;
	u16 phy_reg = 0;

	hw->mac.autoneg = 0;

	if (hw->phy.type == e1000_phy_m88) {
		/* Auto-MDI/MDIX Off */
		e1e_wphy(hw, M88E1000_PHY_SPEC_CTRL, 0x0808);
		/* reset to update Auto-MDI/MDIX */
		e1e_wphy(hw, PHY_CONTROL, 0x9140);
		/* autoneg off */
		e1e_wphy(hw, PHY_CONTROL, 0x8140);
	} else if (hw->phy.type == e1000_phy_gg82563)
		e1e_wphy(hw, GG82563_PHY_KMRN_MODE_CTRL, 0x1CC);

	ctrl_reg = er32(CTRL);

	switch (hw->phy.type) {
	case e1000_phy_ife:
		/* force 100, set loopback */
		e1e_wphy(hw, PHY_CONTROL, 0x6100);

		/* Now set up the MAC to the same speed/duplex as the PHY. */
		ctrl_reg &= ~E1000_CTRL_SPD_SEL; /* Clear the speed sel bits */
		ctrl_reg |= (E1000_CTRL_FRCSPD | /* Set the Force Speed Bit */
			     E1000_CTRL_FRCDPX | /* Set the Force Duplex Bit */
			     E1000_CTRL_SPD_100 |/* Force Speed to 100 */
			     E1000_CTRL_FD);	 /* Force Duplex to FULL */
		break;
	case e1000_phy_bm:
		/* Set Default MAC Interface speed to 1GB */
		e1e_rphy(hw, PHY_REG(2, 21), &phy_reg);
		phy_reg &= ~0x0007;
		phy_reg |= 0x006;
		e1e_wphy(hw, PHY_REG(2, 21), phy_reg);
		/* Assert SW reset for above settings to take effect */
		e1000e_commit_phy(hw);
		mdelay(1);
		/* Force Full Duplex */
		e1e_rphy(hw, PHY_REG(769, 16), &phy_reg);
		e1e_wphy(hw, PHY_REG(769, 16), phy_reg | 0x000C);
		/* Set Link Up (in force link) */
		e1e_rphy(hw, PHY_REG(776, 16), &phy_reg);
		e1e_wphy(hw, PHY_REG(776, 16), phy_reg | 0x0040);
		/* Force Link */
		e1e_rphy(hw, PHY_REG(769, 16), &phy_reg);
		e1e_wphy(hw, PHY_REG(769, 16), phy_reg | 0x0040);
		/* Set Early Link Enable */
		e1e_rphy(hw, PHY_REG(769, 20), &phy_reg);
		e1e_wphy(hw, PHY_REG(769, 20), phy_reg | 0x0400);
		/* fall through */
	default:
		/* force 1000, set loopback */
		e1e_wphy(hw, PHY_CONTROL, 0x4140);
		mdelay(250);

		/* Now set up the MAC to the same speed/duplex as the PHY. */
		ctrl_reg = er32(CTRL);
		ctrl_reg &= ~E1000_CTRL_SPD_SEL; /* Clear the speed sel bits */
		ctrl_reg |= (E1000_CTRL_FRCSPD | /* Set the Force Speed Bit */
			     E1000_CTRL_FRCDPX | /* Set the Force Duplex Bit */
			     E1000_CTRL_SPD_1000 |/* Force Speed to 1000 */
			     E1000_CTRL_FD);	 /* Force Duplex to FULL */

		if (adapter->flags & FLAG_IS_ICH)
			ctrl_reg |= E1000_CTRL_SLU;	/* Set Link Up */
	}

	if (hw->phy.media_type == e1000_media_type_copper &&
	    hw->phy.type == e1000_phy_m88) {
		ctrl_reg |= E1000_CTRL_ILOS; /* Invert Loss of Signal */
	} else {
		/*
		 * Set the ILOS bit on the fiber Nic if half duplex link is
		 * detected.
		 */
		stat_reg = er32(STATUS);
		if ((stat_reg & E1000_STATUS_FD) == 0)
			ctrl_reg |= (E1000_CTRL_ILOS | E1000_CTRL_SLU);
	}

	ew32(CTRL, ctrl_reg);

	/*
	 * Disable the receiver on the PHY so when a cable is plugged in, the
	 * PHY does not begin to autoneg when a cable is reconnected to the NIC.
	 */
	if (hw->phy.type == e1000_phy_m88)
		e1000_phy_disable_receiver(adapter);

	udelay(500);

	return 0;
}

static int e1000_set_82571_fiber_loopback(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl = er32(CTRL);
	int link = 0;

	/* special requirements for 82571/82572 fiber adapters */

	/*
	 * jump through hoops to make sure link is up because serdes
	 * link is hardwired up
	 */
	ctrl |= E1000_CTRL_SLU;
	ew32(CTRL, ctrl);

	/* disable autoneg */
	ctrl = er32(TXCW);
	ctrl &= ~(1 << 31);
	ew32(TXCW, ctrl);

	link = (er32(STATUS) & E1000_STATUS_LU);

	if (!link) {
		/* set invert loss of signal */
		ctrl = er32(CTRL);
		ctrl |= E1000_CTRL_ILOS;
		ew32(CTRL, ctrl);
	}

	/*
	 * special write to serdes control register to enable SerDes analog
	 * loopback
	 */
#define E1000_SERDES_LB_ON 0x410
	ew32(SCTL, E1000_SERDES_LB_ON);
	msleep(10);

	return 0;
}

/* only call this for fiber/serdes connections to es2lan */
static int e1000_set_es2lan_mac_loopback(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrlext = er32(CTRL_EXT);
	u32 ctrl = er32(CTRL);

	/*
	 * save CTRL_EXT to restore later, reuse an empty variable (unused
	 * on mac_type 80003es2lan)
	 */
	adapter->tx_fifo_head = ctrlext;

	/* clear the serdes mode bits, putting the device into mac loopback */
	ctrlext &= ~E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES;
	ew32(CTRL_EXT, ctrlext);

	/* force speed to 1000/FD, link up */
	ctrl &= ~(E1000_CTRL_SPD_1000 | E1000_CTRL_SPD_100);
	ctrl |= (E1000_CTRL_SLU | E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX |
		 E1000_CTRL_SPD_1000 | E1000_CTRL_FD);
	ew32(CTRL, ctrl);

	/* set mac loopback */
	ctrl = er32(RCTL);
	ctrl |= E1000_RCTL_LBM_MAC;
	ew32(RCTL, ctrl);

	/* set testing mode parameters (no need to reset later) */
#define KMRNCTRLSTA_OPMODE (0x1F << 16)
#define KMRNCTRLSTA_OPMODE_1GB_FD_GMII 0x0582
	ew32(KMRNCTRLSTA,
	     (KMRNCTRLSTA_OPMODE | KMRNCTRLSTA_OPMODE_1GB_FD_GMII));

	return 0;
}

static int e1000_setup_loopback_test(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;

	if (hw->phy.media_type == e1000_media_type_fiber ||
	    hw->phy.media_type == e1000_media_type_internal_serdes) {
		switch (hw->mac.type) {
		case e1000_80003es2lan:
			return e1000_set_es2lan_mac_loopback(adapter);
			break;
		case e1000_82571:
		case e1000_82572:
			return e1000_set_82571_fiber_loopback(adapter);
			break;
		default:
			rctl = er32(RCTL);
			rctl |= E1000_RCTL_LBM_TCVR;
			ew32(RCTL, rctl);
			return 0;
		}
	} else if (hw->phy.media_type == e1000_media_type_copper) {
		return e1000_integrated_phy_loopback(adapter);
	}

	return 7;
}

static void e1000_loopback_cleanup(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;
	u16 phy_reg;

	rctl = er32(RCTL);
	rctl &= ~(E1000_RCTL_LBM_TCVR | E1000_RCTL_LBM_MAC);
	ew32(RCTL, rctl);

	switch (hw->mac.type) {
	case e1000_80003es2lan:
		if (hw->phy.media_type == e1000_media_type_fiber ||
		    hw->phy.media_type == e1000_media_type_internal_serdes) {
			/* restore CTRL_EXT, stealing space from tx_fifo_head */
			ew32(CTRL_EXT, adapter->tx_fifo_head);
			adapter->tx_fifo_head = 0;
		}
		/* fall through */
	case e1000_82571:
	case e1000_82572:
		if (hw->phy.media_type == e1000_media_type_fiber ||
		    hw->phy.media_type == e1000_media_type_internal_serdes) {
#define E1000_SERDES_LB_OFF 0x400
			ew32(SCTL, E1000_SERDES_LB_OFF);
			msleep(10);
			break;
		}
		/* Fall Through */
	default:
		hw->mac.autoneg = 1;
		if (hw->phy.type == e1000_phy_gg82563)
			e1e_wphy(hw, GG82563_PHY_KMRN_MODE_CTRL, 0x180);
		e1e_rphy(hw, PHY_CONTROL, &phy_reg);
		if (phy_reg & MII_CR_LOOPBACK) {
			phy_reg &= ~MII_CR_LOOPBACK;
			e1e_wphy(hw, PHY_CONTROL, phy_reg);
			e1000e_commit_phy(hw);
		}
		break;
	}
}

static void e1000_create_lbtest_frame(struct sk_buff *skb,
				      unsigned int frame_size)
{
	memset(skb->data, 0xFF, frame_size);
	frame_size &= ~1;
	memset(&skb->data[frame_size / 2], 0xAA, frame_size / 2 - 1);
	memset(&skb->data[frame_size / 2 + 10], 0xBE, 1);
	memset(&skb->data[frame_size / 2 + 12], 0xAF, 1);
}

static int e1000_check_lbtest_frame(struct sk_buff *skb,
				    unsigned int frame_size)
{
	frame_size &= ~1;
	if (*(skb->data + 3) == 0xFF)
		if ((*(skb->data + frame_size / 2 + 10) == 0xBE) &&
		   (*(skb->data + frame_size / 2 + 12) == 0xAF))
			return 0;
	return 13;
}

static int e1000_run_loopback_test(struct e1000_adapter *adapter)
{
	struct e1000_ring *tx_ring = &adapter->test_tx_ring;
	struct e1000_ring *rx_ring = &adapter->test_rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_hw *hw = &adapter->hw;
	int i, j, k, l;
	int lc;
	int good_cnt;
	int ret_val = 0;
	unsigned long time;

	ew32(RDT, rx_ring->count - 1);

	/*
	 * Calculate the loop count based on the largest descriptor ring
	 * The idea is to wrap the largest ring a number of times using 64
	 * send/receive pairs during each loop
	 */

	if (rx_ring->count <= tx_ring->count)
		lc = ((tx_ring->count / 64) * 2) + 1;
	else
		lc = ((rx_ring->count / 64) * 2) + 1;

	k = 0;
	l = 0;
	for (j = 0; j <= lc; j++) { /* loop count loop */
		for (i = 0; i < 64; i++) { /* send the packets */
			e1000_create_lbtest_frame(tx_ring->buffer_info[k].skb,
						  1024);
			pci_dma_sync_single_for_device(pdev,
					tx_ring->buffer_info[k].dma,
					tx_ring->buffer_info[k].length,
					PCI_DMA_TODEVICE);
			k++;
			if (k == tx_ring->count)
				k = 0;
		}
		ew32(TDT, k);
		msleep(200);
		time = jiffies; /* set the start time for the receive */
		good_cnt = 0;
		do { /* receive the sent packets */
			pci_dma_sync_single_for_cpu(pdev,
					rx_ring->buffer_info[l].dma, 2048,
					PCI_DMA_FROMDEVICE);

			ret_val = e1000_check_lbtest_frame(
					rx_ring->buffer_info[l].skb, 1024);
			if (!ret_val)
				good_cnt++;
			l++;
			if (l == rx_ring->count)
				l = 0;
			/*
			 * time + 20 msecs (200 msecs on 2.4) is more than
			 * enough time to complete the receives, if it's
			 * exceeded, break and error off
			 */
		} while ((good_cnt < 64) && !time_after(jiffies, time + 20));
		if (good_cnt != 64) {
			ret_val = 13; /* ret_val is the same as mis-compare */
			break;
		}
		if (jiffies >= (time + 20)) {
			ret_val = 14; /* error code for time out error */
			break;
		}
	} /* end loop count loop */
	return ret_val;
}

static int e1000_loopback_test(struct e1000_adapter *adapter, u64 *data)
{
	/*
	 * PHY loopback cannot be performed if SoL/IDER
	 * sessions are active
	 */
	if (e1000_check_reset_block(&adapter->hw)) {
		ndev_err(adapter->netdev, "Cannot do PHY loopback test "
			 "when SoL/IDER is active.\n");
		*data = 0;
		goto out;
	}

	*data = e1000_setup_desc_rings(adapter);
	if (*data)
		goto out;

	*data = e1000_setup_loopback_test(adapter);
	if (*data)
		goto err_loopback;

	*data = e1000_run_loopback_test(adapter);
	e1000_loopback_cleanup(adapter);

err_loopback:
	e1000_free_desc_rings(adapter);
out:
	return *data;
}

static int e1000_link_test(struct e1000_adapter *adapter, u64 *data)
{
	struct e1000_hw *hw = &adapter->hw;

	*data = 0;
	if (hw->phy.media_type == e1000_media_type_internal_serdes) {
		int i = 0;
		hw->mac.serdes_has_link = 0;

		/*
		 * On some blade server designs, link establishment
		 * could take as long as 2-3 minutes
		 */
		do {
			hw->mac.ops.check_for_link(hw);
			if (hw->mac.serdes_has_link)
				return *data;
			msleep(20);
		} while (i++ < 3750);

		*data = 1;
	} else {
		hw->mac.ops.check_for_link(hw);
		if (hw->mac.autoneg)
			msleep(4000);

		if (!(er32(STATUS) &
		      E1000_STATUS_LU))
			*data = 1;
	}
	return *data;
}

static int e1000e_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return E1000_TEST_LEN;
	case ETH_SS_STATS:
		return E1000_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void e1000_diag_test(struct net_device *netdev,
			    struct ethtool_test *eth_test, u64 *data)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	u16 autoneg_advertised;
	u8 forced_speed_duplex;
	u8 autoneg;
	bool if_running = netif_running(netdev);

	set_bit(__E1000_TESTING, &adapter->state);
	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline tests */

		/* save speed, duplex, autoneg settings */
		autoneg_advertised = adapter->hw.phy.autoneg_advertised;
		forced_speed_duplex = adapter->hw.mac.forced_speed_duplex;
		autoneg = adapter->hw.mac.autoneg;

		ndev_info(netdev, "offline testing starting\n");

		/*
		 * Link test performed before hardware reset so autoneg doesn't
		 * interfere with test result
		 */
		if (e1000_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (if_running)
			/* indicate we're in test mode */
			dev_close(netdev);
		else
			e1000e_reset(adapter);

		if (e1000_reg_test(adapter, &data[0]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		e1000e_reset(adapter);
		if (e1000_eeprom_test(adapter, &data[1]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		e1000e_reset(adapter);
		if (e1000_intr_test(adapter, &data[2]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		e1000e_reset(adapter);
		/* make sure the phy is powered up */
		e1000e_power_up_phy(adapter);
		if (e1000_loopback_test(adapter, &data[3]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* restore speed, duplex, autoneg settings */
		adapter->hw.phy.autoneg_advertised = autoneg_advertised;
		adapter->hw.mac.forced_speed_duplex = forced_speed_duplex;
		adapter->hw.mac.autoneg = autoneg;

		/* force this routine to wait until autoneg complete/timeout */
		adapter->hw.phy.autoneg_wait_to_complete = 1;
		e1000e_reset(adapter);
		adapter->hw.phy.autoneg_wait_to_complete = 0;

		clear_bit(__E1000_TESTING, &adapter->state);
		if (if_running)
			dev_open(netdev);
	} else {
		ndev_info(netdev, "online testing starting\n");
		/* Online tests */
		if (e1000_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* Online tests aren't run; pass by default */
		data[0] = 0;
		data[1] = 0;
		data[2] = 0;
		data[3] = 0;

		clear_bit(__E1000_TESTING, &adapter->state);
	}
	msleep_interruptible(4 * 1000);
}

static void e1000_get_wol(struct net_device *netdev,
			  struct ethtool_wolinfo *wol)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	wol->supported = 0;
	wol->wolopts = 0;

	if (!(adapter->flags & FLAG_HAS_WOL))
		return;

	wol->supported = WAKE_UCAST | WAKE_MCAST |
	                 WAKE_BCAST | WAKE_MAGIC |
	                 WAKE_PHY | WAKE_ARP;

	/* apply any specific unsupported masks here */
	if (adapter->flags & FLAG_NO_WAKE_UCAST) {
		wol->supported &= ~WAKE_UCAST;

		if (adapter->wol & E1000_WUFC_EX)
			ndev_err(netdev, "Interface does not support "
				 "directed (unicast) frame wake-up packets\n");
	}

	if (adapter->wol & E1000_WUFC_EX)
		wol->wolopts |= WAKE_UCAST;
	if (adapter->wol & E1000_WUFC_MC)
		wol->wolopts |= WAKE_MCAST;
	if (adapter->wol & E1000_WUFC_BC)
		wol->wolopts |= WAKE_BCAST;
	if (adapter->wol & E1000_WUFC_MAG)
		wol->wolopts |= WAKE_MAGIC;
	if (adapter->wol & E1000_WUFC_LNKC)
		wol->wolopts |= WAKE_PHY;
	if (adapter->wol & E1000_WUFC_ARP)
		wol->wolopts |= WAKE_ARP;
}

static int e1000_set_wol(struct net_device *netdev,
			 struct ethtool_wolinfo *wol)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	if (wol->wolopts & WAKE_MAGICSECURE)
		return -EOPNOTSUPP;

	if (!(adapter->flags & FLAG_HAS_WOL))
		return wol->wolopts ? -EOPNOTSUPP : 0;

	/* these settings will always override what we currently have */
	adapter->wol = 0;

	if (wol->wolopts & WAKE_UCAST)
		adapter->wol |= E1000_WUFC_EX;
	if (wol->wolopts & WAKE_MCAST)
		adapter->wol |= E1000_WUFC_MC;
	if (wol->wolopts & WAKE_BCAST)
		adapter->wol |= E1000_WUFC_BC;
	if (wol->wolopts & WAKE_MAGIC)
		adapter->wol |= E1000_WUFC_MAG;
	if (wol->wolopts & WAKE_PHY)
		adapter->wol |= E1000_WUFC_LNKC;
	if (wol->wolopts & WAKE_ARP)
		adapter->wol |= E1000_WUFC_ARP;

	return 0;
}

/* toggle LED 4 times per second = 2 "blinks" per second */
#define E1000_ID_INTERVAL	(HZ/4)

/* bit defines for adapter->led_status */
#define E1000_LED_ON		0

static void e1000_led_blink_callback(unsigned long data)
{
	struct e1000_adapter *adapter = (struct e1000_adapter *) data;

	if (test_and_change_bit(E1000_LED_ON, &adapter->led_status))
		adapter->hw.mac.ops.led_off(&adapter->hw);
	else
		adapter->hw.mac.ops.led_on(&adapter->hw);

	mod_timer(&adapter->blink_timer, jiffies + E1000_ID_INTERVAL);
}

static int e1000_phys_id(struct net_device *netdev, u32 data)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	if (!data)
		data = INT_MAX;

	if (adapter->hw.phy.type == e1000_phy_ife) {
		if (!adapter->blink_timer.function) {
			init_timer(&adapter->blink_timer);
			adapter->blink_timer.function =
				e1000_led_blink_callback;
			adapter->blink_timer.data = (unsigned long) adapter;
		}
		mod_timer(&adapter->blink_timer, jiffies);
		msleep_interruptible(data * 1000);
		del_timer_sync(&adapter->blink_timer);
		e1e_wphy(&adapter->hw,
				    IFE_PHY_SPECIAL_CONTROL_LED, 0);
	} else {
		e1000e_blink_led(&adapter->hw);
		msleep_interruptible(data * 1000);
	}

	adapter->hw.mac.ops.led_off(&adapter->hw);
	clear_bit(E1000_LED_ON, &adapter->led_status);
	adapter->hw.mac.ops.cleanup_led(&adapter->hw);

	return 0;
}

static int e1000_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	if (adapter->itr_setting <= 3)
		ec->rx_coalesce_usecs = adapter->itr_setting;
	else
		ec->rx_coalesce_usecs = 1000000 / adapter->itr_setting;

	return 0;
}

static int e1000_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	if ((ec->rx_coalesce_usecs > E1000_MAX_ITR_USECS) ||
	    ((ec->rx_coalesce_usecs > 3) &&
	     (ec->rx_coalesce_usecs < E1000_MIN_ITR_USECS)) ||
	    (ec->rx_coalesce_usecs == 2))
		return -EINVAL;

	if (ec->rx_coalesce_usecs <= 3) {
		adapter->itr = 20000;
		adapter->itr_setting = ec->rx_coalesce_usecs;
	} else {
		adapter->itr = (1000000 / ec->rx_coalesce_usecs);
		adapter->itr_setting = adapter->itr & ~3;
	}

	if (adapter->itr_setting != 0)
		ew32(ITR, 1000000000 / (adapter->itr * 256));
	else
		ew32(ITR, 0);

	return 0;
}

static int e1000_nway_reset(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	if (netif_running(netdev))
		e1000e_reinit_locked(adapter);
	return 0;
}

static void e1000_get_ethtool_stats(struct net_device *netdev,
				    struct ethtool_stats *stats,
				    u64 *data)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	int i;

	e1000e_update_stats(adapter);
	for (i = 0; i < E1000_GLOBAL_STATS_LEN; i++) {
		char *p = (char *)adapter+e1000_gstrings_stats[i].stat_offset;
		data[i] = (e1000_gstrings_stats[i].sizeof_stat ==
			sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
}

static void e1000_get_strings(struct net_device *netdev, u32 stringset,
			      u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *e1000_gstrings_test, sizeof(e1000_gstrings_test));
		break;
	case ETH_SS_STATS:
		for (i = 0; i < E1000_GLOBAL_STATS_LEN; i++) {
			memcpy(p, e1000_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static const struct ethtool_ops e1000_ethtool_ops = {
	.get_settings		= e1000_get_settings,
	.set_settings		= e1000_set_settings,
	.get_drvinfo		= e1000_get_drvinfo,
	.get_regs_len		= e1000_get_regs_len,
	.get_regs		= e1000_get_regs,
	.get_wol		= e1000_get_wol,
	.set_wol		= e1000_set_wol,
	.get_msglevel		= e1000_get_msglevel,
	.set_msglevel		= e1000_set_msglevel,
	.nway_reset		= e1000_nway_reset,
	.get_link		= e1000_get_link,
	.get_eeprom_len		= e1000_get_eeprom_len,
	.get_eeprom		= e1000_get_eeprom,
	.set_eeprom		= e1000_set_eeprom,
	.get_ringparam		= e1000_get_ringparam,
	.set_ringparam		= e1000_set_ringparam,
	.get_pauseparam		= e1000_get_pauseparam,
	.set_pauseparam		= e1000_set_pauseparam,
	.get_rx_csum		= e1000_get_rx_csum,
	.set_rx_csum		= e1000_set_rx_csum,
	.get_tx_csum		= e1000_get_tx_csum,
	.set_tx_csum		= e1000_set_tx_csum,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= ethtool_op_set_sg,
	.get_tso		= ethtool_op_get_tso,
	.set_tso		= e1000_set_tso,
	.self_test		= e1000_diag_test,
	.get_strings		= e1000_get_strings,
	.phys_id		= e1000_phys_id,
	.get_ethtool_stats	= e1000_get_ethtool_stats,
	.get_sset_count		= e1000e_get_sset_count,
	.get_coalesce		= e1000_get_coalesce,
	.set_coalesce		= e1000_set_coalesce,
};

void e1000e_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &e1000_ethtool_ops);
}
