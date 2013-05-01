/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2006 Intel Corporation.

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

#include "e1000.h"
#include <asm/uaccess.h>

enum {NETDEV_STATS, E1000_STATS};

struct e1000_stats {
	char stat_string[ETH_GSTRING_LEN];
	int type;
	int sizeof_stat;
	int stat_offset;
};

#define E1000_STAT(m)		E1000_STATS, \
				sizeof(((struct e1000_adapter *)0)->m), \
		      		offsetof(struct e1000_adapter, m)
#define E1000_NETDEV_STAT(m)	NETDEV_STATS, \
				sizeof(((struct net_device *)0)->m), \
				offsetof(struct net_device, m)

static const struct e1000_stats e1000_gstrings_stats[] = {
	{ "rx_packets", E1000_STAT(stats.gprc) },
	{ "tx_packets", E1000_STAT(stats.gptc) },
	{ "rx_bytes", E1000_STAT(stats.gorcl) },
	{ "tx_bytes", E1000_STAT(stats.gotcl) },
	{ "rx_broadcast", E1000_STAT(stats.bprc) },
	{ "tx_broadcast", E1000_STAT(stats.bptc) },
	{ "rx_multicast", E1000_STAT(stats.mprc) },
	{ "tx_multicast", E1000_STAT(stats.mptc) },
	{ "rx_errors", E1000_STAT(stats.rxerrc) },
	{ "tx_errors", E1000_STAT(stats.txerrc) },
	{ "tx_dropped", E1000_NETDEV_STAT(stats.tx_dropped) },
	{ "multicast", E1000_STAT(stats.mprc) },
	{ "collisions", E1000_STAT(stats.colc) },
	{ "rx_length_errors", E1000_STAT(stats.rlerrc) },
	{ "rx_over_errors", E1000_NETDEV_STAT(stats.rx_over_errors) },
	{ "rx_crc_errors", E1000_STAT(stats.crcerrs) },
	{ "rx_frame_errors", E1000_NETDEV_STAT(stats.rx_frame_errors) },
	{ "rx_no_buffer_count", E1000_STAT(stats.rnbc) },
	{ "rx_missed_errors", E1000_STAT(stats.mpc) },
	{ "tx_aborted_errors", E1000_STAT(stats.ecol) },
	{ "tx_carrier_errors", E1000_STAT(stats.tncrs) },
	{ "tx_fifo_errors", E1000_NETDEV_STAT(stats.tx_fifo_errors) },
	{ "tx_heartbeat_errors", E1000_NETDEV_STAT(stats.tx_heartbeat_errors) },
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
	{ "rx_long_byte_count", E1000_STAT(stats.gorcl) },
	{ "rx_csum_offload_good", E1000_STAT(hw_csum_good) },
	{ "rx_csum_offload_errors", E1000_STAT(hw_csum_err) },
	{ "alloc_rx_buff_failed", E1000_STAT(alloc_rx_buff_failed) },
	{ "tx_smbus", E1000_STAT(stats.mgptc) },
	{ "rx_smbus", E1000_STAT(stats.mgprc) },
	{ "dropped_smbus", E1000_STAT(stats.mgpdc) },
};

#define E1000_QUEUE_STATS_LEN 0
#define E1000_GLOBAL_STATS_LEN ARRAY_SIZE(e1000_gstrings_stats)
#define E1000_STATS_LEN (E1000_GLOBAL_STATS_LEN + E1000_QUEUE_STATS_LEN)
static const char e1000_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)", "Eeprom test    (offline)",
	"Interrupt test (offline)", "Loopback test  (offline)",
	"Link test   (on/offline)"
};
#define E1000_TEST_LEN	ARRAY_SIZE(e1000_gstrings_test)

static int e1000_get_settings(struct net_device *netdev,
			      struct ethtool_cmd *ecmd)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	if (hw->media_type == e1000_media_type_copper) {

		ecmd->supported = (SUPPORTED_10baseT_Half |
				   SUPPORTED_10baseT_Full |
				   SUPPORTED_100baseT_Half |
				   SUPPORTED_100baseT_Full |
				   SUPPORTED_1000baseT_Full|
				   SUPPORTED_Autoneg |
				   SUPPORTED_TP);
		ecmd->advertising = ADVERTISED_TP;

		if (hw->autoneg == 1) {
			ecmd->advertising |= ADVERTISED_Autoneg;
			/* the e1000 autoneg seems to match ethtool nicely */
			ecmd->advertising |= hw->autoneg_advertised;
		}

		ecmd->port = PORT_TP;
		ecmd->phy_address = hw->phy_addr;

		if (hw->mac_type == e1000_82543)
			ecmd->transceiver = XCVR_EXTERNAL;
		else
			ecmd->transceiver = XCVR_INTERNAL;

	} else {
		ecmd->supported   = (SUPPORTED_1000baseT_Full |
				     SUPPORTED_FIBRE |
				     SUPPORTED_Autoneg);

		ecmd->advertising = (ADVERTISED_1000baseT_Full |
				     ADVERTISED_FIBRE |
				     ADVERTISED_Autoneg);

		ecmd->port = PORT_FIBRE;

		if (hw->mac_type >= e1000_82545)
			ecmd->transceiver = XCVR_INTERNAL;
		else
			ecmd->transceiver = XCVR_EXTERNAL;
	}

	if (er32(STATUS) & E1000_STATUS_LU) {

		e1000_get_speed_and_duplex(hw, &adapter->link_speed,
		                                   &adapter->link_duplex);
		ethtool_cmd_speed_set(ecmd, adapter->link_speed);

		/* unfortunately FULL_DUPLEX != DUPLEX_FULL
		 * and HALF_DUPLEX != DUPLEX_HALF
		 */
		if (adapter->link_duplex == FULL_DUPLEX)
			ecmd->duplex = DUPLEX_FULL;
		else
			ecmd->duplex = DUPLEX_HALF;
	} else {
		ethtool_cmd_speed_set(ecmd, -1);
		ecmd->duplex = -1;
	}

	ecmd->autoneg = ((hw->media_type == e1000_media_type_fiber) ||
			 hw->autoneg) ? AUTONEG_ENABLE : AUTONEG_DISABLE;

	/* MDI-X => 1; MDI => 0 */
	if ((hw->media_type == e1000_media_type_copper) &&
	    netif_carrier_ok(netdev))
		ecmd->eth_tp_mdix = (!!adapter->phy_info.mdix_mode ?
				     ETH_TP_MDI_X : ETH_TP_MDI);
	else
		ecmd->eth_tp_mdix = ETH_TP_MDI_INVALID;

	if (hw->mdix == AUTO_ALL_MODES)
		ecmd->eth_tp_mdix_ctrl = ETH_TP_MDI_AUTO;
	else
		ecmd->eth_tp_mdix_ctrl = hw->mdix;
	return 0;
}

static int e1000_set_settings(struct net_device *netdev,
			      struct ethtool_cmd *ecmd)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	/* MDI setting is only allowed when autoneg enabled because
	 * some hardware doesn't allow MDI setting when speed or
	 * duplex is forced.
	 */
	if (ecmd->eth_tp_mdix_ctrl) {
		if (hw->media_type != e1000_media_type_copper)
			return -EOPNOTSUPP;

		if ((ecmd->eth_tp_mdix_ctrl != ETH_TP_MDI_AUTO) &&
		    (ecmd->autoneg != AUTONEG_ENABLE)) {
			e_err(drv, "forcing MDI/MDI-X state is not supported when link speed and/or duplex are forced\n");
			return -EINVAL;
		}
	}

	while (test_and_set_bit(__E1000_RESETTING, &adapter->flags))
		msleep(1);

	if (ecmd->autoneg == AUTONEG_ENABLE) {
		hw->autoneg = 1;
		if (hw->media_type == e1000_media_type_fiber)
			hw->autoneg_advertised = ADVERTISED_1000baseT_Full |
				     ADVERTISED_FIBRE |
				     ADVERTISED_Autoneg;
		else
			hw->autoneg_advertised = ecmd->advertising |
						 ADVERTISED_TP |
						 ADVERTISED_Autoneg;
		ecmd->advertising = hw->autoneg_advertised;
	} else {
		u32 speed = ethtool_cmd_speed(ecmd);
		/* calling this overrides forced MDI setting */
		if (e1000_set_spd_dplx(adapter, speed, ecmd->duplex)) {
			clear_bit(__E1000_RESETTING, &adapter->flags);
			return -EINVAL;
		}
	}

	/* MDI-X => 2; MDI => 1; Auto => 3 */
	if (ecmd->eth_tp_mdix_ctrl) {
		if (ecmd->eth_tp_mdix_ctrl == ETH_TP_MDI_AUTO)
			hw->mdix = AUTO_ALL_MODES;
		else
			hw->mdix = ecmd->eth_tp_mdix_ctrl;
	}

	/* reset the link */

	if (netif_running(adapter->netdev)) {
		e1000_down(adapter);
		e1000_up(adapter);
	} else
		e1000_reset(adapter);

	clear_bit(__E1000_RESETTING, &adapter->flags);
	return 0;
}

static u32 e1000_get_link(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	/* If the link is not reported up to netdev, interrupts are disabled,
	 * and so the physical link state may have changed since we last
	 * looked. Set get_link_status to make sure that the true link
	 * state is interrogated, rather than pulling a cached and possibly
	 * stale link state from the driver.
	 */
	if (!netif_carrier_ok(netdev))
		adapter->hw.get_link_status = 1;

	return e1000_has_link(adapter);
}

static void e1000_get_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pause)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	pause->autoneg =
		(adapter->fc_autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE);

	if (hw->fc == E1000_FC_RX_PAUSE)
		pause->rx_pause = 1;
	else if (hw->fc == E1000_FC_TX_PAUSE)
		pause->tx_pause = 1;
	else if (hw->fc == E1000_FC_FULL) {
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

	while (test_and_set_bit(__E1000_RESETTING, &adapter->flags))
		msleep(1);

	if (pause->rx_pause && pause->tx_pause)
		hw->fc = E1000_FC_FULL;
	else if (pause->rx_pause && !pause->tx_pause)
		hw->fc = E1000_FC_RX_PAUSE;
	else if (!pause->rx_pause && pause->tx_pause)
		hw->fc = E1000_FC_TX_PAUSE;
	else if (!pause->rx_pause && !pause->tx_pause)
		hw->fc = E1000_FC_NONE;

	hw->original_fc = hw->fc;

	if (adapter->fc_autoneg == AUTONEG_ENABLE) {
		if (netif_running(adapter->netdev)) {
			e1000_down(adapter);
			e1000_up(adapter);
		} else
			e1000_reset(adapter);
	} else
		retval = ((hw->media_type == e1000_media_type_fiber) ?
			  e1000_setup_link(hw) : e1000_force_mac_fc(hw));

	clear_bit(__E1000_RESETTING, &adapter->flags);
	return retval;
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
#define E1000_REGS_LEN 32
	return E1000_REGS_LEN * sizeof(u32);
}

static void e1000_get_regs(struct net_device *netdev, struct ethtool_regs *regs,
			   void *p)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u16 phy_data;

	memset(p, 0, E1000_REGS_LEN * sizeof(u32));

	regs->version = (1 << 24) | (hw->revision_id << 16) | hw->device_id;

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

	regs_buff[12] = hw->phy_type;  /* PHY type (IGP=1, M88=0) */
	if (hw->phy_type == e1000_phy_igp) {
		e1000_write_phy_reg(hw, IGP01E1000_PHY_PAGE_SELECT,
				    IGP01E1000_PHY_AGC_A);
		e1000_read_phy_reg(hw, IGP01E1000_PHY_AGC_A &
				   IGP01E1000_PHY_PAGE_SELECT, &phy_data);
		regs_buff[13] = (u32)phy_data; /* cable length */
		e1000_write_phy_reg(hw, IGP01E1000_PHY_PAGE_SELECT,
				    IGP01E1000_PHY_AGC_B);
		e1000_read_phy_reg(hw, IGP01E1000_PHY_AGC_B &
				   IGP01E1000_PHY_PAGE_SELECT, &phy_data);
		regs_buff[14] = (u32)phy_data; /* cable length */
		e1000_write_phy_reg(hw, IGP01E1000_PHY_PAGE_SELECT,
				    IGP01E1000_PHY_AGC_C);
		e1000_read_phy_reg(hw, IGP01E1000_PHY_AGC_C &
				   IGP01E1000_PHY_PAGE_SELECT, &phy_data);
		regs_buff[15] = (u32)phy_data; /* cable length */
		e1000_write_phy_reg(hw, IGP01E1000_PHY_PAGE_SELECT,
				    IGP01E1000_PHY_AGC_D);
		e1000_read_phy_reg(hw, IGP01E1000_PHY_AGC_D &
				   IGP01E1000_PHY_PAGE_SELECT, &phy_data);
		regs_buff[16] = (u32)phy_data; /* cable length */
		regs_buff[17] = 0; /* extended 10bt distance (not needed) */
		e1000_write_phy_reg(hw, IGP01E1000_PHY_PAGE_SELECT, 0x0);
		e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_STATUS &
				   IGP01E1000_PHY_PAGE_SELECT, &phy_data);
		regs_buff[18] = (u32)phy_data; /* cable polarity */
		e1000_write_phy_reg(hw, IGP01E1000_PHY_PAGE_SELECT,
				    IGP01E1000_PHY_PCS_INIT_REG);
		e1000_read_phy_reg(hw, IGP01E1000_PHY_PCS_INIT_REG &
				   IGP01E1000_PHY_PAGE_SELECT, &phy_data);
		regs_buff[19] = (u32)phy_data; /* cable polarity */
		regs_buff[20] = 0; /* polarity correction enabled (always) */
		regs_buff[22] = 0; /* phy receive errors (unavailable) */
		regs_buff[23] = regs_buff[18]; /* mdix mode */
		e1000_write_phy_reg(hw, IGP01E1000_PHY_PAGE_SELECT, 0x0);
	} else {
		e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS, &phy_data);
		regs_buff[13] = (u32)phy_data; /* cable length */
		regs_buff[14] = 0;  /* Dummy (to align w/ IGP phy reg dump) */
		regs_buff[15] = 0;  /* Dummy (to align w/ IGP phy reg dump) */
		regs_buff[16] = 0;  /* Dummy (to align w/ IGP phy reg dump) */
		e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
		regs_buff[17] = (u32)phy_data; /* extended 10bt distance */
		regs_buff[18] = regs_buff[13]; /* cable polarity */
		regs_buff[19] = 0;  /* Dummy (to align w/ IGP phy reg dump) */
		regs_buff[20] = regs_buff[17]; /* polarity correction */
		/* phy receive errors */
		regs_buff[22] = adapter->phy_stats.receive_errors;
		regs_buff[23] = regs_buff[13]; /* mdix mode */
	}
	regs_buff[21] = adapter->phy_stats.idle_errors;  /* phy idle errors */
	e1000_read_phy_reg(hw, PHY_1000T_STATUS, &phy_data);
	regs_buff[24] = (u32)phy_data;  /* phy local receiver status */
	regs_buff[25] = regs_buff[24];  /* phy remote receiver status */
	if (hw->mac_type >= e1000_82540 &&
	    hw->media_type == e1000_media_type_copper) {
		regs_buff[26] = er32(MANC);
	}
}

static int e1000_get_eeprom_len(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	return hw->eeprom.word_size * 2;
}

static int e1000_get_eeprom(struct net_device *netdev,
			    struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u16 *eeprom_buff;
	int first_word, last_word;
	int ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EINVAL;

	eeprom->magic = hw->vendor_id | (hw->device_id << 16);

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;

	eeprom_buff = kmalloc(sizeof(u16) *
			(last_word - first_word + 1), GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	if (hw->eeprom.type == e1000_eeprom_spi)
		ret_val = e1000_read_eeprom(hw, first_word,
					    last_word - first_word + 1,
					    eeprom_buff);
	else {
		for (i = 0; i < last_word - first_word + 1; i++) {
			ret_val = e1000_read_eeprom(hw, first_word + i, 1,
						    &eeprom_buff[i]);
			if (ret_val)
				break;
		}
	}

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(bytes, (u8 *)eeprom_buff + (eeprom->offset & 1),
	       eeprom->len);
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
	int max_len, first_word, last_word, ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EOPNOTSUPP;

	if (eeprom->magic != (hw->vendor_id | (hw->device_id << 16)))
		return -EFAULT;

	max_len = hw->eeprom.word_size * 2;

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;
	eeprom_buff = kmalloc(max_len, GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	ptr = (void *)eeprom_buff;

	if (eeprom->offset & 1) {
		/* need read/modify/write of first changed EEPROM word
		 * only the second byte of the word is being modified
		 */
		ret_val = e1000_read_eeprom(hw, first_word, 1,
					    &eeprom_buff[0]);
		ptr++;
	}
	if (((eeprom->offset + eeprom->len) & 1) && (ret_val == 0)) {
		/* need read/modify/write of last changed EEPROM word
		 * only the first byte of the word is being modified
		 */
		ret_val = e1000_read_eeprom(hw, last_word, 1,
		                  &eeprom_buff[last_word - first_word]);
	}

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(ptr, bytes, eeprom->len);

	for (i = 0; i < last_word - first_word + 1; i++)
		eeprom_buff[i] = cpu_to_le16(eeprom_buff[i]);

	ret_val = e1000_write_eeprom(hw, first_word,
				     last_word - first_word + 1, eeprom_buff);

	/* Update the checksum over the first part of the EEPROM if needed */
	if ((ret_val == 0) && (first_word <= EEPROM_CHECKSUM_REG))
		e1000_update_eeprom_checksum(hw);

	kfree(eeprom_buff);
	return ret_val;
}

static void e1000_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	strlcpy(drvinfo->driver,  e1000_driver_name,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, e1000_driver_version,
		sizeof(drvinfo->version));

	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->regdump_len = e1000_get_regs_len(netdev);
	drvinfo->eedump_len = e1000_get_eeprom_len(netdev);
}

static void e1000_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	e1000_mac_type mac_type = hw->mac_type;
	struct e1000_tx_ring *txdr = adapter->tx_ring;
	struct e1000_rx_ring *rxdr = adapter->rx_ring;

	ring->rx_max_pending = (mac_type < e1000_82544) ? E1000_MAX_RXD :
		E1000_MAX_82544_RXD;
	ring->tx_max_pending = (mac_type < e1000_82544) ? E1000_MAX_TXD :
		E1000_MAX_82544_TXD;
	ring->rx_pending = rxdr->count;
	ring->tx_pending = txdr->count;
}

static int e1000_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	e1000_mac_type mac_type = hw->mac_type;
	struct e1000_tx_ring *txdr, *tx_old;
	struct e1000_rx_ring *rxdr, *rx_old;
	int i, err;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	while (test_and_set_bit(__E1000_RESETTING, &adapter->flags))
		msleep(1);

	if (netif_running(adapter->netdev))
		e1000_down(adapter);

	tx_old = adapter->tx_ring;
	rx_old = adapter->rx_ring;

	err = -ENOMEM;
	txdr = kcalloc(adapter->num_tx_queues, sizeof(struct e1000_tx_ring),
		       GFP_KERNEL);
	if (!txdr)
		goto err_alloc_tx;

	rxdr = kcalloc(adapter->num_rx_queues, sizeof(struct e1000_rx_ring),
		       GFP_KERNEL);
	if (!rxdr)
		goto err_alloc_rx;

	adapter->tx_ring = txdr;
	adapter->rx_ring = rxdr;

	rxdr->count = max(ring->rx_pending,(u32)E1000_MIN_RXD);
	rxdr->count = min(rxdr->count,(u32)(mac_type < e1000_82544 ?
			  E1000_MAX_RXD : E1000_MAX_82544_RXD));
	rxdr->count = ALIGN(rxdr->count, REQ_RX_DESCRIPTOR_MULTIPLE);

	txdr->count = max(ring->tx_pending,(u32)E1000_MIN_TXD);
	txdr->count = min(txdr->count,(u32)(mac_type < e1000_82544 ?
			  E1000_MAX_TXD : E1000_MAX_82544_TXD));
	txdr->count = ALIGN(txdr->count, REQ_TX_DESCRIPTOR_MULTIPLE);

	for (i = 0; i < adapter->num_tx_queues; i++)
		txdr[i].count = txdr->count;
	for (i = 0; i < adapter->num_rx_queues; i++)
		rxdr[i].count = rxdr->count;

	if (netif_running(adapter->netdev)) {
		/* Try to get new resources before deleting old */
		err = e1000_setup_all_rx_resources(adapter);
		if (err)
			goto err_setup_rx;
		err = e1000_setup_all_tx_resources(adapter);
		if (err)
			goto err_setup_tx;

		/* save the new, restore the old in order to free it,
		 * then restore the new back again
		 */

		adapter->rx_ring = rx_old;
		adapter->tx_ring = tx_old;
		e1000_free_all_rx_resources(adapter);
		e1000_free_all_tx_resources(adapter);
		kfree(tx_old);
		kfree(rx_old);
		adapter->rx_ring = rxdr;
		adapter->tx_ring = txdr;
		err = e1000_up(adapter);
		if (err)
			goto err_setup;
	}

	clear_bit(__E1000_RESETTING, &adapter->flags);
	return 0;
err_setup_tx:
	e1000_free_all_rx_resources(adapter);
err_setup_rx:
	adapter->rx_ring = rx_old;
	adapter->tx_ring = tx_old;
	kfree(rxdr);
err_alloc_rx:
	kfree(txdr);
err_alloc_tx:
	e1000_up(adapter);
err_setup:
	clear_bit(__E1000_RESETTING, &adapter->flags);
	return err;
}

static bool reg_pattern_test(struct e1000_adapter *adapter, u64 *data, int reg,
			     u32 mask, u32 write)
{
	struct e1000_hw *hw = &adapter->hw;
	static const u32 test[] =
		{0x5A5A5A5A, 0xA5A5A5A5, 0x00000000, 0xFFFFFFFF};
	u8 __iomem *address = hw->hw_addr + reg;
	u32 read;
	int i;

	for (i = 0; i < ARRAY_SIZE(test); i++) {
		writel(write & test[i], address);
		read = readl(address);
		if (read != (write & test[i] & mask)) {
			e_err(drv, "pattern test reg %04X failed: "
			      "got 0x%08X expected 0x%08X\n",
			      reg, read, (write & test[i] & mask));
			*data = reg;
			return true;
		}
	}
	return false;
}

static bool reg_set_and_check(struct e1000_adapter *adapter, u64 *data, int reg,
			      u32 mask, u32 write)
{
	struct e1000_hw *hw = &adapter->hw;
	u8 __iomem *address = hw->hw_addr + reg;
	u32 read;

	writel(write & mask, address);
	read = readl(address);
	if ((read & mask) != (write & mask)) {
		e_err(drv, "set/check reg %04X test failed: "
		      "got 0x%08X expected 0x%08X\n",
		      reg, (read & mask), (write & mask));
		*data = reg;
		return true;
	}
	return false;
}

#define REG_PATTERN_TEST(reg, mask, write)			     \
	do {							     \
		if (reg_pattern_test(adapter, data,		     \
			     (hw->mac_type >= e1000_82543)   \
			     ? E1000_##reg : E1000_82542_##reg,	     \
			     mask, write))			     \
			return 1;				     \
	} while (0)

#define REG_SET_AND_CHECK(reg, mask, write)			     \
	do {							     \
		if (reg_set_and_check(adapter, data,		     \
			      (hw->mac_type >= e1000_82543)  \
			      ? E1000_##reg : E1000_82542_##reg,     \
			      mask, write))			     \
			return 1;				     \
	} while (0)

static int e1000_reg_test(struct e1000_adapter *adapter, u64 *data)
{
	u32 value, before, after;
	u32 i, toggle;
	struct e1000_hw *hw = &adapter->hw;

	/* The status register is Read Only, so a write should fail.
	 * Some bits that get toggled are ignored.
	 */

	/* there are several bits on newer hardware that are r/w */
	toggle = 0xFFFFF833;

	before = er32(STATUS);
	value = (er32(STATUS) & toggle);
	ew32(STATUS, toggle);
	after = er32(STATUS) & toggle;
	if (value != after) {
		e_err(drv, "failed STATUS register test got: "
		      "0x%08X expected: 0x%08X\n", after, value);
		*data = 1;
		return 1;
	}
	/* restore previous status */
	ew32(STATUS, before);

	REG_PATTERN_TEST(FCAL, 0xFFFFFFFF, 0xFFFFFFFF);
	REG_PATTERN_TEST(FCAH, 0x0000FFFF, 0xFFFFFFFF);
	REG_PATTERN_TEST(FCT, 0x0000FFFF, 0xFFFFFFFF);
	REG_PATTERN_TEST(VET, 0x0000FFFF, 0xFFFFFFFF);

	REG_PATTERN_TEST(RDTR, 0x0000FFFF, 0xFFFFFFFF);
	REG_PATTERN_TEST(RDBAH, 0xFFFFFFFF, 0xFFFFFFFF);
	REG_PATTERN_TEST(RDLEN, 0x000FFF80, 0x000FFFFF);
	REG_PATTERN_TEST(RDH, 0x0000FFFF, 0x0000FFFF);
	REG_PATTERN_TEST(RDT, 0x0000FFFF, 0x0000FFFF);
	REG_PATTERN_TEST(FCRTH, 0x0000FFF8, 0x0000FFF8);
	REG_PATTERN_TEST(FCTTV, 0x0000FFFF, 0x0000FFFF);
	REG_PATTERN_TEST(TIPG, 0x3FFFFFFF, 0x3FFFFFFF);
	REG_PATTERN_TEST(TDBAH, 0xFFFFFFFF, 0xFFFFFFFF);
	REG_PATTERN_TEST(TDLEN, 0x000FFF80, 0x000FFFFF);

	REG_SET_AND_CHECK(RCTL, 0xFFFFFFFF, 0x00000000);

	before = 0x06DFB3FE;
	REG_SET_AND_CHECK(RCTL, before, 0x003FFFFB);
	REG_SET_AND_CHECK(TCTL, 0xFFFFFFFF, 0x00000000);

	if (hw->mac_type >= e1000_82543) {
		REG_SET_AND_CHECK(RCTL, before, 0xFFFFFFFF);
		REG_PATTERN_TEST(RDBAL, 0xFFFFFFF0, 0xFFFFFFFF);
		REG_PATTERN_TEST(TXCW, 0xC000FFFF, 0x0000FFFF);
		REG_PATTERN_TEST(TDBAL, 0xFFFFFFF0, 0xFFFFFFFF);
		REG_PATTERN_TEST(TIDV, 0x0000FFFF, 0x0000FFFF);
		value = E1000_RAR_ENTRIES;
		for (i = 0; i < value; i++) {
			REG_PATTERN_TEST(RA + (((i << 1) + 1) << 2), 0x8003FFFF,
			                 0xFFFFFFFF);
		}
	} else {
		REG_SET_AND_CHECK(RCTL, 0xFFFFFFFF, 0x01FFFFFF);
		REG_PATTERN_TEST(RDBAL, 0xFFFFF000, 0xFFFFFFFF);
		REG_PATTERN_TEST(TXCW, 0x0000FFFF, 0x0000FFFF);
		REG_PATTERN_TEST(TDBAL, 0xFFFFF000, 0xFFFFFFFF);
	}

	value = E1000_MC_TBL_SIZE;
	for (i = 0; i < value; i++)
		REG_PATTERN_TEST(MTA + (i << 2), 0xFFFFFFFF, 0xFFFFFFFF);

	*data = 0;
	return 0;
}

static int e1000_eeprom_test(struct e1000_adapter *adapter, u64 *data)
{
	struct e1000_hw *hw = &adapter->hw;
	u16 temp;
	u16 checksum = 0;
	u16 i;

	*data = 0;
	/* Read and add up the contents of the EEPROM */
	for (i = 0; i < (EEPROM_CHECKSUM_REG + 1); i++) {
		if ((e1000_read_eeprom(hw, i, 1, &temp)) < 0) {
			*data = 1;
			break;
		}
		checksum += temp;
	}

	/* If Checksum is not Correct return error else test passed */
	if ((checksum != (u16)EEPROM_SUM) && !(*data))
		*data = 2;

	return *data;
}

static irqreturn_t e1000_test_intr(int irq, void *data)
{
	struct net_device *netdev = (struct net_device *)data;
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	adapter->test_icr |= er32(ICR);

	return IRQ_HANDLED;
}

static int e1000_intr_test(struct e1000_adapter *adapter, u64 *data)
{
	struct net_device *netdev = adapter->netdev;
	u32 mask, i = 0;
	bool shared_int = true;
	u32 irq = adapter->pdev->irq;
	struct e1000_hw *hw = &adapter->hw;

	*data = 0;

	/* NOTE: we don't test MSI interrupts here, yet
	 * Hook up test interrupt handler just for this test
	 */
	if (!request_irq(irq, e1000_test_intr, IRQF_PROBE_SHARED, netdev->name,
			 netdev))
		shared_int = false;
	else if (request_irq(irq, e1000_test_intr, IRQF_SHARED,
			     netdev->name, netdev)) {
		*data = 1;
		return -1;
	}
	e_info(hw, "testing %s interrupt\n", (shared_int ?
	       "shared" : "unshared"));

	/* Disable all the interrupts */
	ew32(IMC, 0xFFFFFFFF);
	E1000_WRITE_FLUSH();
	msleep(10);

	/* Test each interrupt */
	for (; i < 10; i++) {

		/* Interrupt to test */
		mask = 1 << i;

		if (!shared_int) {
			/* Disable the interrupt to be reported in
			 * the cause register and then force the same
			 * interrupt and see if one gets posted.  If
			 * an interrupt was posted to the bus, the
			 * test failed.
			 */
			adapter->test_icr = 0;
			ew32(IMC, mask);
			ew32(ICS, mask);
			E1000_WRITE_FLUSH();
			msleep(10);

			if (adapter->test_icr & mask) {
				*data = 3;
				break;
			}
		}

		/* Enable the interrupt to be reported in
		 * the cause register and then force the same
		 * interrupt and see if one gets posted.  If
		 * an interrupt was not posted to the bus, the
		 * test failed.
		 */
		adapter->test_icr = 0;
		ew32(IMS, mask);
		ew32(ICS, mask);
		E1000_WRITE_FLUSH();
		msleep(10);

		if (!(adapter->test_icr & mask)) {
			*data = 4;
			break;
		}

		if (!shared_int) {
			/* Disable the other interrupts to be reported in
			 * the cause register and then force the other
			 * interrupts and see if any get posted.  If
			 * an interrupt was posted to the bus, the
			 * test failed.
			 */
			adapter->test_icr = 0;
			ew32(IMC, ~mask & 0x00007FFF);
			ew32(ICS, ~mask & 0x00007FFF);
			E1000_WRITE_FLUSH();
			msleep(10);

			if (adapter->test_icr) {
				*data = 5;
				break;
			}
		}
	}

	/* Disable all the interrupts */
	ew32(IMC, 0xFFFFFFFF);
	E1000_WRITE_FLUSH();
	msleep(10);

	/* Unhook test interrupt handler */
	free_irq(irq, netdev);

	return *data;
}

static void e1000_free_desc_rings(struct e1000_adapter *adapter)
{
	struct e1000_tx_ring *txdr = &adapter->test_tx_ring;
	struct e1000_rx_ring *rxdr = &adapter->test_rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	int i;

	if (txdr->desc && txdr->buffer_info) {
		for (i = 0; i < txdr->count; i++) {
			if (txdr->buffer_info[i].dma)
				dma_unmap_single(&pdev->dev,
						 txdr->buffer_info[i].dma,
						 txdr->buffer_info[i].length,
						 DMA_TO_DEVICE);
			if (txdr->buffer_info[i].skb)
				dev_kfree_skb(txdr->buffer_info[i].skb);
		}
	}

	if (rxdr->desc && rxdr->buffer_info) {
		for (i = 0; i < rxdr->count; i++) {
			if (rxdr->buffer_info[i].dma)
				dma_unmap_single(&pdev->dev,
						 rxdr->buffer_info[i].dma,
						 rxdr->buffer_info[i].length,
						 DMA_FROM_DEVICE);
			if (rxdr->buffer_info[i].skb)
				dev_kfree_skb(rxdr->buffer_info[i].skb);
		}
	}

	if (txdr->desc) {
		dma_free_coherent(&pdev->dev, txdr->size, txdr->desc,
				  txdr->dma);
		txdr->desc = NULL;
	}
	if (rxdr->desc) {
		dma_free_coherent(&pdev->dev, rxdr->size, rxdr->desc,
				  rxdr->dma);
		rxdr->desc = NULL;
	}

	kfree(txdr->buffer_info);
	txdr->buffer_info = NULL;
	kfree(rxdr->buffer_info);
	rxdr->buffer_info = NULL;
}

static int e1000_setup_desc_rings(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_tx_ring *txdr = &adapter->test_tx_ring;
	struct e1000_rx_ring *rxdr = &adapter->test_rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	u32 rctl;
	int i, ret_val;

	/* Setup Tx descriptor ring and Tx buffers */

	if (!txdr->count)
		txdr->count = E1000_DEFAULT_TXD;

	txdr->buffer_info = kcalloc(txdr->count, sizeof(struct e1000_buffer),
				    GFP_KERNEL);
	if (!txdr->buffer_info) {
		ret_val = 1;
		goto err_nomem;
	}

	txdr->size = txdr->count * sizeof(struct e1000_tx_desc);
	txdr->size = ALIGN(txdr->size, 4096);
	txdr->desc = dma_alloc_coherent(&pdev->dev, txdr->size, &txdr->dma,
					GFP_KERNEL);
	if (!txdr->desc) {
		ret_val = 2;
		goto err_nomem;
	}
	memset(txdr->desc, 0, txdr->size);
	txdr->next_to_use = txdr->next_to_clean = 0;

	ew32(TDBAL, ((u64)txdr->dma & 0x00000000FFFFFFFF));
	ew32(TDBAH, ((u64)txdr->dma >> 32));
	ew32(TDLEN, txdr->count * sizeof(struct e1000_tx_desc));
	ew32(TDH, 0);
	ew32(TDT, 0);
	ew32(TCTL, E1000_TCTL_PSP | E1000_TCTL_EN |
	     E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT |
	     E1000_FDX_COLLISION_DISTANCE << E1000_COLD_SHIFT);

	for (i = 0; i < txdr->count; i++) {
		struct e1000_tx_desc *tx_desc = E1000_TX_DESC(*txdr, i);
		struct sk_buff *skb;
		unsigned int size = 1024;

		skb = alloc_skb(size, GFP_KERNEL);
		if (!skb) {
			ret_val = 3;
			goto err_nomem;
		}
		skb_put(skb, size);
		txdr->buffer_info[i].skb = skb;
		txdr->buffer_info[i].length = skb->len;
		txdr->buffer_info[i].dma =
			dma_map_single(&pdev->dev, skb->data, skb->len,
				       DMA_TO_DEVICE);
		tx_desc->buffer_addr = cpu_to_le64(txdr->buffer_info[i].dma);
		tx_desc->lower.data = cpu_to_le32(skb->len);
		tx_desc->lower.data |= cpu_to_le32(E1000_TXD_CMD_EOP |
						   E1000_TXD_CMD_IFCS |
						   E1000_TXD_CMD_RPS);
		tx_desc->upper.data = 0;
	}

	/* Setup Rx descriptor ring and Rx buffers */

	if (!rxdr->count)
		rxdr->count = E1000_DEFAULT_RXD;

	rxdr->buffer_info = kcalloc(rxdr->count, sizeof(struct e1000_buffer),
				    GFP_KERNEL);
	if (!rxdr->buffer_info) {
		ret_val = 4;
		goto err_nomem;
	}

	rxdr->size = rxdr->count * sizeof(struct e1000_rx_desc);
	rxdr->desc = dma_alloc_coherent(&pdev->dev, rxdr->size, &rxdr->dma,
					GFP_KERNEL);
	if (!rxdr->desc) {
		ret_val = 5;
		goto err_nomem;
	}
	memset(rxdr->desc, 0, rxdr->size);
	rxdr->next_to_use = rxdr->next_to_clean = 0;

	rctl = er32(RCTL);
	ew32(RCTL, rctl & ~E1000_RCTL_EN);
	ew32(RDBAL, ((u64)rxdr->dma & 0xFFFFFFFF));
	ew32(RDBAH, ((u64)rxdr->dma >> 32));
	ew32(RDLEN, rxdr->size);
	ew32(RDH, 0);
	ew32(RDT, 0);
	rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SZ_2048 |
		E1000_RCTL_LBM_NO | E1000_RCTL_RDMTS_HALF |
		(hw->mc_filter_type << E1000_RCTL_MO_SHIFT);
	ew32(RCTL, rctl);

	for (i = 0; i < rxdr->count; i++) {
		struct e1000_rx_desc *rx_desc = E1000_RX_DESC(*rxdr, i);
		struct sk_buff *skb;

		skb = alloc_skb(E1000_RXBUFFER_2048 + NET_IP_ALIGN, GFP_KERNEL);
		if (!skb) {
			ret_val = 6;
			goto err_nomem;
		}
		skb_reserve(skb, NET_IP_ALIGN);
		rxdr->buffer_info[i].skb = skb;
		rxdr->buffer_info[i].length = E1000_RXBUFFER_2048;
		rxdr->buffer_info[i].dma =
			dma_map_single(&pdev->dev, skb->data,
				       E1000_RXBUFFER_2048, DMA_FROM_DEVICE);
		rx_desc->buffer_addr = cpu_to_le64(rxdr->buffer_info[i].dma);
		memset(skb->data, 0x00, skb->len);
	}

	return 0;

err_nomem:
	e1000_free_desc_rings(adapter);
	return ret_val;
}

static void e1000_phy_disable_receiver(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	/* Write out to PHY registers 29 and 30 to disable the Receiver. */
	e1000_write_phy_reg(hw, 29, 0x001F);
	e1000_write_phy_reg(hw, 30, 0x8FFC);
	e1000_write_phy_reg(hw, 29, 0x001A);
	e1000_write_phy_reg(hw, 30, 0x8FF0);
}

static void e1000_phy_reset_clk_and_crs(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u16 phy_reg;

	/* Because we reset the PHY above, we need to re-force TX_CLK in the
	 * Extended PHY Specific Control Register to 25MHz clock.  This
	 * value defaults back to a 2.5MHz clock when the PHY is reset.
	 */
	e1000_read_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL, &phy_reg);
	phy_reg |= M88E1000_EPSCR_TX_CLK_25;
	e1000_write_phy_reg(hw,
		M88E1000_EXT_PHY_SPEC_CTRL, phy_reg);

	/* In addition, because of the s/w reset above, we need to enable
	 * CRS on TX.  This must be set for both full and half duplex
	 * operation.
	 */
	e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_reg);
	phy_reg |= M88E1000_PSCR_ASSERT_CRS_ON_TX;
	e1000_write_phy_reg(hw,
		M88E1000_PHY_SPEC_CTRL, phy_reg);
}

static int e1000_nonintegrated_phy_loopback(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl_reg;
	u16 phy_reg;

	/* Setup the Device Control Register for PHY loopback test. */

	ctrl_reg = er32(CTRL);
	ctrl_reg |= (E1000_CTRL_ILOS |		/* Invert Loss-Of-Signal */
		     E1000_CTRL_FRCSPD |	/* Set the Force Speed Bit */
		     E1000_CTRL_FRCDPX |	/* Set the Force Duplex Bit */
		     E1000_CTRL_SPD_1000 |	/* Force Speed to 1000 */
		     E1000_CTRL_FD);		/* Force Duplex to FULL */

	ew32(CTRL, ctrl_reg);

	/* Read the PHY Specific Control Register (0x10) */
	e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_reg);

	/* Clear Auto-Crossover bits in PHY Specific Control Register
	 * (bits 6:5).
	 */
	phy_reg &= ~M88E1000_PSCR_AUTO_X_MODE;
	e1000_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_reg);

	/* Perform software reset on the PHY */
	e1000_phy_reset(hw);

	/* Have to setup TX_CLK and TX_CRS after software reset */
	e1000_phy_reset_clk_and_crs(adapter);

	e1000_write_phy_reg(hw, PHY_CTRL, 0x8100);

	/* Wait for reset to complete. */
	udelay(500);

	/* Have to setup TX_CLK and TX_CRS after software reset */
	e1000_phy_reset_clk_and_crs(adapter);

	/* Write out to PHY registers 29 and 30 to disable the Receiver. */
	e1000_phy_disable_receiver(adapter);

	/* Set the loopback bit in the PHY control register. */
	e1000_read_phy_reg(hw, PHY_CTRL, &phy_reg);
	phy_reg |= MII_CR_LOOPBACK;
	e1000_write_phy_reg(hw, PHY_CTRL, phy_reg);

	/* Setup TX_CLK and TX_CRS one more time. */
	e1000_phy_reset_clk_and_crs(adapter);

	/* Check Phy Configuration */
	e1000_read_phy_reg(hw, PHY_CTRL, &phy_reg);
	if (phy_reg != 0x4100)
		 return 9;

	e1000_read_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL, &phy_reg);
	if (phy_reg != 0x0070)
		return 10;

	e1000_read_phy_reg(hw, 29, &phy_reg);
	if (phy_reg != 0x001A)
		return 11;

	return 0;
}

static int e1000_integrated_phy_loopback(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl_reg = 0;
	u32 stat_reg = 0;

	hw->autoneg = false;

	if (hw->phy_type == e1000_phy_m88) {
		/* Auto-MDI/MDIX Off */
		e1000_write_phy_reg(hw,
				    M88E1000_PHY_SPEC_CTRL, 0x0808);
		/* reset to update Auto-MDI/MDIX */
		e1000_write_phy_reg(hw, PHY_CTRL, 0x9140);
		/* autoneg off */
		e1000_write_phy_reg(hw, PHY_CTRL, 0x8140);
	}

	ctrl_reg = er32(CTRL);

	/* force 1000, set loopback */
	e1000_write_phy_reg(hw, PHY_CTRL, 0x4140);

	/* Now set up the MAC to the same speed/duplex as the PHY. */
	ctrl_reg = er32(CTRL);
	ctrl_reg &= ~E1000_CTRL_SPD_SEL; /* Clear the speed sel bits */
	ctrl_reg |= (E1000_CTRL_FRCSPD | /* Set the Force Speed Bit */
			E1000_CTRL_FRCDPX | /* Set the Force Duplex Bit */
			E1000_CTRL_SPD_1000 |/* Force Speed to 1000 */
			E1000_CTRL_FD); /* Force Duplex to FULL */

	if (hw->media_type == e1000_media_type_copper &&
	   hw->phy_type == e1000_phy_m88)
		ctrl_reg |= E1000_CTRL_ILOS; /* Invert Loss of Signal */
	else {
		/* Set the ILOS bit on the fiber Nic is half
		 * duplex link is detected.
		 */
		stat_reg = er32(STATUS);
		if ((stat_reg & E1000_STATUS_FD) == 0)
			ctrl_reg |= (E1000_CTRL_ILOS | E1000_CTRL_SLU);
	}

	ew32(CTRL, ctrl_reg);

	/* Disable the receiver on the PHY so when a cable is plugged in, the
	 * PHY does not begin to autoneg when a cable is reconnected to the NIC.
	 */
	if (hw->phy_type == e1000_phy_m88)
		e1000_phy_disable_receiver(adapter);

	udelay(500);

	return 0;
}

static int e1000_set_phy_loopback(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u16 phy_reg = 0;
	u16 count = 0;

	switch (hw->mac_type) {
	case e1000_82543:
		if (hw->media_type == e1000_media_type_copper) {
			/* Attempt to setup Loopback mode on Non-integrated PHY.
			 * Some PHY registers get corrupted at random, so
			 * attempt this 10 times.
			 */
			while (e1000_nonintegrated_phy_loopback(adapter) &&
			      count++ < 10);
			if (count < 11)
				return 0;
		}
		break;

	case e1000_82544:
	case e1000_82540:
	case e1000_82545:
	case e1000_82545_rev_3:
	case e1000_82546:
	case e1000_82546_rev_3:
	case e1000_82541:
	case e1000_82541_rev_2:
	case e1000_82547:
	case e1000_82547_rev_2:
		return e1000_integrated_phy_loopback(adapter);
		break;
	default:
		/* Default PHY loopback work is to read the MII
		 * control register and assert bit 14 (loopback mode).
		 */
		e1000_read_phy_reg(hw, PHY_CTRL, &phy_reg);
		phy_reg |= MII_CR_LOOPBACK;
		e1000_write_phy_reg(hw, PHY_CTRL, phy_reg);
		return 0;
		break;
	}

	return 8;
}

static int e1000_setup_loopback_test(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;

	if (hw->media_type == e1000_media_type_fiber ||
	    hw->media_type == e1000_media_type_internal_serdes) {
		switch (hw->mac_type) {
		case e1000_82545:
		case e1000_82546:
		case e1000_82545_rev_3:
		case e1000_82546_rev_3:
			return e1000_set_phy_loopback(adapter);
			break;
		default:
			rctl = er32(RCTL);
			rctl |= E1000_RCTL_LBM_TCVR;
			ew32(RCTL, rctl);
			return 0;
		}
	} else if (hw->media_type == e1000_media_type_copper)
		return e1000_set_phy_loopback(adapter);

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

	switch (hw->mac_type) {
	case e1000_82545:
	case e1000_82546:
	case e1000_82545_rev_3:
	case e1000_82546_rev_3:
	default:
		hw->autoneg = true;
		e1000_read_phy_reg(hw, PHY_CTRL, &phy_reg);
		if (phy_reg & MII_CR_LOOPBACK) {
			phy_reg &= ~MII_CR_LOOPBACK;
			e1000_write_phy_reg(hw, PHY_CTRL, phy_reg);
			e1000_phy_reset(hw);
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
	if (*(skb->data + 3) == 0xFF) {
		if ((*(skb->data + frame_size / 2 + 10) == 0xBE) &&
		   (*(skb->data + frame_size / 2 + 12) == 0xAF)) {
			return 0;
		}
	}
	return 13;
}

static int e1000_run_loopback_test(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_tx_ring *txdr = &adapter->test_tx_ring;
	struct e1000_rx_ring *rxdr = &adapter->test_rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	int i, j, k, l, lc, good_cnt, ret_val=0;
	unsigned long time;

	ew32(RDT, rxdr->count - 1);

	/* Calculate the loop count based on the largest descriptor ring
	 * The idea is to wrap the largest ring a number of times using 64
	 * send/receive pairs during each loop
	 */

	if (rxdr->count <= txdr->count)
		lc = ((txdr->count / 64) * 2) + 1;
	else
		lc = ((rxdr->count / 64) * 2) + 1;

	k = l = 0;
	for (j = 0; j <= lc; j++) { /* loop count loop */
		for (i = 0; i < 64; i++) { /* send the packets */
			e1000_create_lbtest_frame(txdr->buffer_info[i].skb,
					1024);
			dma_sync_single_for_device(&pdev->dev,
						   txdr->buffer_info[k].dma,
						   txdr->buffer_info[k].length,
						   DMA_TO_DEVICE);
			if (unlikely(++k == txdr->count)) k = 0;
		}
		ew32(TDT, k);
		E1000_WRITE_FLUSH();
		msleep(200);
		time = jiffies; /* set the start time for the receive */
		good_cnt = 0;
		do { /* receive the sent packets */
			dma_sync_single_for_cpu(&pdev->dev,
						rxdr->buffer_info[l].dma,
						rxdr->buffer_info[l].length,
						DMA_FROM_DEVICE);

			ret_val = e1000_check_lbtest_frame(
					rxdr->buffer_info[l].skb,
					1024);
			if (!ret_val)
				good_cnt++;
			if (unlikely(++l == rxdr->count)) l = 0;
			/* time + 20 msecs (200 msecs on 2.4) is more than
			 * enough time to complete the receives, if it's
			 * exceeded, break and error off
			 */
		} while (good_cnt < 64 && jiffies < (time + 20));
		if (good_cnt != 64) {
			ret_val = 13; /* ret_val is the same as mis-compare */
			break;
		}
		if (jiffies >= (time + 2)) {
			ret_val = 14; /* error code for time out error */
			break;
		}
	} /* end loop count loop */
	return ret_val;
}

static int e1000_loopback_test(struct e1000_adapter *adapter, u64 *data)
{
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
	if (hw->media_type == e1000_media_type_internal_serdes) {
		int i = 0;
		hw->serdes_has_link = false;

		/* On some blade server designs, link establishment
		 * could take as long as 2-3 minutes
		 */
		do {
			e1000_check_for_link(hw);
			if (hw->serdes_has_link)
				return *data;
			msleep(20);
		} while (i++ < 3750);

		*data = 1;
	} else {
		e1000_check_for_link(hw);
		if (hw->autoneg)  /* if auto_neg is set wait for it */
			msleep(4000);

		if (!(er32(STATUS) & E1000_STATUS_LU)) {
			*data = 1;
		}
	}
	return *data;
}

static int e1000_get_sset_count(struct net_device *netdev, int sset)
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
	struct e1000_hw *hw = &adapter->hw;
	bool if_running = netif_running(netdev);

	set_bit(__E1000_TESTING, &adapter->flags);
	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline tests */

		/* save speed, duplex, autoneg settings */
		u16 autoneg_advertised = hw->autoneg_advertised;
		u8 forced_speed_duplex = hw->forced_speed_duplex;
		u8 autoneg = hw->autoneg;

		e_info(hw, "offline testing starting\n");

		/* Link test performed before hardware reset so autoneg doesn't
		 * interfere with test result
		 */
		if (e1000_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (if_running)
			/* indicate we're in test mode */
			dev_close(netdev);
		else
			e1000_reset(adapter);

		if (e1000_reg_test(adapter, &data[0]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		e1000_reset(adapter);
		if (e1000_eeprom_test(adapter, &data[1]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		e1000_reset(adapter);
		if (e1000_intr_test(adapter, &data[2]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		e1000_reset(adapter);
		/* make sure the phy is powered up */
		e1000_power_up_phy(adapter);
		if (e1000_loopback_test(adapter, &data[3]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* restore speed, duplex, autoneg settings */
		hw->autoneg_advertised = autoneg_advertised;
		hw->forced_speed_duplex = forced_speed_duplex;
		hw->autoneg = autoneg;

		e1000_reset(adapter);
		clear_bit(__E1000_TESTING, &adapter->flags);
		if (if_running)
			dev_open(netdev);
	} else {
		e_info(hw, "online testing starting\n");
		/* Online tests */
		if (e1000_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* Online tests aren't run; pass by default */
		data[0] = 0;
		data[1] = 0;
		data[2] = 0;
		data[3] = 0;

		clear_bit(__E1000_TESTING, &adapter->flags);
	}
	msleep_interruptible(4 * 1000);
}

static int e1000_wol_exclusion(struct e1000_adapter *adapter,
			       struct ethtool_wolinfo *wol)
{
	struct e1000_hw *hw = &adapter->hw;
	int retval = 1; /* fail by default */

	switch (hw->device_id) {
	case E1000_DEV_ID_82542:
	case E1000_DEV_ID_82543GC_FIBER:
	case E1000_DEV_ID_82543GC_COPPER:
	case E1000_DEV_ID_82544EI_FIBER:
	case E1000_DEV_ID_82546EB_QUAD_COPPER:
	case E1000_DEV_ID_82545EM_FIBER:
	case E1000_DEV_ID_82545EM_COPPER:
	case E1000_DEV_ID_82546GB_QUAD_COPPER:
	case E1000_DEV_ID_82546GB_PCIE:
		/* these don't support WoL at all */
		wol->supported = 0;
		break;
	case E1000_DEV_ID_82546EB_FIBER:
	case E1000_DEV_ID_82546GB_FIBER:
		/* Wake events not supported on port B */
		if (er32(STATUS) & E1000_STATUS_FUNC_1) {
			wol->supported = 0;
			break;
		}
		/* return success for non excluded adapter ports */
		retval = 0;
		break;
	case E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3:
		/* quad port adapters only support WoL on port A */
		if (!adapter->quad_port_a) {
			wol->supported = 0;
			break;
		}
		/* return success for non excluded adapter ports */
		retval = 0;
		break;
	default:
		/* dual port cards only support WoL on port A from now on
		 * unless it was enabled in the eeprom for port B
		 * so exclude FUNC_1 ports from having WoL enabled
		 */
		if (er32(STATUS) & E1000_STATUS_FUNC_1 &&
		    !adapter->eeprom_wol) {
			wol->supported = 0;
			break;
		}

		retval = 0;
	}

	return retval;
}

static void e1000_get_wol(struct net_device *netdev,
			  struct ethtool_wolinfo *wol)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	wol->supported = WAKE_UCAST | WAKE_MCAST |
	                 WAKE_BCAST | WAKE_MAGIC;
	wol->wolopts = 0;

	/* this function will set ->supported = 0 and return 1 if wol is not
	 * supported by this hardware
	 */
	if (e1000_wol_exclusion(adapter, wol) ||
	    !device_can_wakeup(&adapter->pdev->dev))
		return;

	/* apply any specific unsupported masks here */
	switch (hw->device_id) {
	case E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3:
		/* KSP3 does not support UCAST wake-ups */
		wol->supported &= ~WAKE_UCAST;

		if (adapter->wol & E1000_WUFC_EX)
			e_err(drv, "Interface does not support directed "
			      "(unicast) frame wake-up packets\n");
		break;
	default:
		break;
	}

	if (adapter->wol & E1000_WUFC_EX)
		wol->wolopts |= WAKE_UCAST;
	if (adapter->wol & E1000_WUFC_MC)
		wol->wolopts |= WAKE_MCAST;
	if (adapter->wol & E1000_WUFC_BC)
		wol->wolopts |= WAKE_BCAST;
	if (adapter->wol & E1000_WUFC_MAG)
		wol->wolopts |= WAKE_MAGIC;
}

static int e1000_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	if (wol->wolopts & (WAKE_PHY | WAKE_ARP | WAKE_MAGICSECURE))
		return -EOPNOTSUPP;

	if (e1000_wol_exclusion(adapter, wol) ||
	    !device_can_wakeup(&adapter->pdev->dev))
		return wol->wolopts ? -EOPNOTSUPP : 0;

	switch (hw->device_id) {
	case E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3:
		if (wol->wolopts & WAKE_UCAST) {
			e_err(drv, "Interface does not support directed "
			      "(unicast) frame wake-up packets\n");
			return -EOPNOTSUPP;
		}
		break;
	default:
		break;
	}

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

	device_set_wakeup_enable(&adapter->pdev->dev, adapter->wol);

	return 0;
}

static int e1000_set_phys_id(struct net_device *netdev,
			     enum ethtool_phys_id_state state)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		e1000_setup_led(hw);
		return 2;

	case ETHTOOL_ID_ON:
		e1000_led_on(hw);
		break;

	case ETHTOOL_ID_OFF:
		e1000_led_off(hw);
		break;

	case ETHTOOL_ID_INACTIVE:
		e1000_cleanup_led(hw);
	}

	return 0;
}

static int e1000_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	if (adapter->hw.mac_type < e1000_82545)
		return -EOPNOTSUPP;

	if (adapter->itr_setting <= 4)
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

	if (hw->mac_type < e1000_82545)
		return -EOPNOTSUPP;

	if ((ec->rx_coalesce_usecs > E1000_MAX_ITR_USECS) ||
	    ((ec->rx_coalesce_usecs > 4) &&
	     (ec->rx_coalesce_usecs < E1000_MIN_ITR_USECS)) ||
	    (ec->rx_coalesce_usecs == 2))
		return -EINVAL;

	if (ec->rx_coalesce_usecs == 4) {
		adapter->itr = adapter->itr_setting = 4;
	} else if (ec->rx_coalesce_usecs <= 3) {
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
		e1000_reinit_locked(adapter);
	return 0;
}

static void e1000_get_ethtool_stats(struct net_device *netdev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	int i;
	char *p = NULL;

	e1000_update_stats(adapter);
	for (i = 0; i < E1000_GLOBAL_STATS_LEN; i++) {
		switch (e1000_gstrings_stats[i].type) {
		case NETDEV_STATS:
			p = (char *) netdev +
					e1000_gstrings_stats[i].stat_offset;
			break;
		case E1000_STATS:
			p = (char *) adapter +
					e1000_gstrings_stats[i].stat_offset;
			break;
		}

		data[i] = (e1000_gstrings_stats[i].sizeof_stat ==
			sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
/* BUG_ON(i != E1000_STATS_LEN); */
}

static void e1000_get_strings(struct net_device *netdev, u32 stringset,
			      u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *e1000_gstrings_test,
			sizeof(e1000_gstrings_test));
		break;
	case ETH_SS_STATS:
		for (i = 0; i < E1000_GLOBAL_STATS_LEN; i++) {
			memcpy(p, e1000_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		/* BUG_ON(p - data != E1000_STATS_LEN * ETH_GSTRING_LEN); */
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
	.self_test		= e1000_diag_test,
	.get_strings		= e1000_get_strings,
	.set_phys_id		= e1000_set_phys_id,
	.get_ethtool_stats	= e1000_get_ethtool_stats,
	.get_sset_count		= e1000_get_sset_count,
	.get_coalesce		= e1000_get_coalesce,
	.set_coalesce		= e1000_set_coalesce,
	.get_ts_info		= ethtool_op_get_ts_info,
};

void e1000_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &e1000_ethtool_ops);
}
