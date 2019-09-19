// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 1999 - 2018 Intel Corporation. */

/* ethtool support for e1000 */

#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/pm_runtime.h>

#include "e1000.h"

enum { NETDEV_STATS, E1000_STATS };

struct e1000_stats {
	char stat_string[ETH_GSTRING_LEN];
	int type;
	int sizeof_stat;
	int stat_offset;
};

#define E1000_STAT(str, m) { \
		.stat_string = str, \
		.type = E1000_STATS, \
		.sizeof_stat = sizeof(((struct e1000_adapter *)0)->m), \
		.stat_offset = offsetof(struct e1000_adapter, m) }
#define E1000_NETDEV_STAT(str, m) { \
		.stat_string = str, \
		.type = NETDEV_STATS, \
		.sizeof_stat = sizeof(((struct rtnl_link_stats64 *)0)->m), \
		.stat_offset = offsetof(struct rtnl_link_stats64, m) }

static const struct e1000_stats e1000_gstrings_stats[] = {
	E1000_STAT("rx_packets", stats.gprc),
	E1000_STAT("tx_packets", stats.gptc),
	E1000_STAT("rx_bytes", stats.gorc),
	E1000_STAT("tx_bytes", stats.gotc),
	E1000_STAT("rx_broadcast", stats.bprc),
	E1000_STAT("tx_broadcast", stats.bptc),
	E1000_STAT("rx_multicast", stats.mprc),
	E1000_STAT("tx_multicast", stats.mptc),
	E1000_NETDEV_STAT("rx_errors", rx_errors),
	E1000_NETDEV_STAT("tx_errors", tx_errors),
	E1000_NETDEV_STAT("tx_dropped", tx_dropped),
	E1000_STAT("multicast", stats.mprc),
	E1000_STAT("collisions", stats.colc),
	E1000_NETDEV_STAT("rx_length_errors", rx_length_errors),
	E1000_NETDEV_STAT("rx_over_errors", rx_over_errors),
	E1000_STAT("rx_crc_errors", stats.crcerrs),
	E1000_NETDEV_STAT("rx_frame_errors", rx_frame_errors),
	E1000_STAT("rx_no_buffer_count", stats.rnbc),
	E1000_STAT("rx_missed_errors", stats.mpc),
	E1000_STAT("tx_aborted_errors", stats.ecol),
	E1000_STAT("tx_carrier_errors", stats.tncrs),
	E1000_NETDEV_STAT("tx_fifo_errors", tx_fifo_errors),
	E1000_NETDEV_STAT("tx_heartbeat_errors", tx_heartbeat_errors),
	E1000_STAT("tx_window_errors", stats.latecol),
	E1000_STAT("tx_abort_late_coll", stats.latecol),
	E1000_STAT("tx_deferred_ok", stats.dc),
	E1000_STAT("tx_single_coll_ok", stats.scc),
	E1000_STAT("tx_multi_coll_ok", stats.mcc),
	E1000_STAT("tx_timeout_count", tx_timeout_count),
	E1000_STAT("tx_restart_queue", restart_queue),
	E1000_STAT("rx_long_length_errors", stats.roc),
	E1000_STAT("rx_short_length_errors", stats.ruc),
	E1000_STAT("rx_align_errors", stats.algnerrc),
	E1000_STAT("tx_tcp_seg_good", stats.tsctc),
	E1000_STAT("tx_tcp_seg_failed", stats.tsctfc),
	E1000_STAT("rx_flow_control_xon", stats.xonrxc),
	E1000_STAT("rx_flow_control_xoff", stats.xoffrxc),
	E1000_STAT("tx_flow_control_xon", stats.xontxc),
	E1000_STAT("tx_flow_control_xoff", stats.xofftxc),
	E1000_STAT("rx_csum_offload_good", hw_csum_good),
	E1000_STAT("rx_csum_offload_errors", hw_csum_err),
	E1000_STAT("rx_header_split", rx_hdr_split),
	E1000_STAT("alloc_rx_buff_failed", alloc_rx_buff_failed),
	E1000_STAT("tx_smbus", stats.mgptc),
	E1000_STAT("rx_smbus", stats.mgprc),
	E1000_STAT("dropped_smbus", stats.mgpdc),
	E1000_STAT("rx_dma_failed", rx_dma_failed),
	E1000_STAT("tx_dma_failed", tx_dma_failed),
	E1000_STAT("rx_hwtstamp_cleared", rx_hwtstamp_cleared),
	E1000_STAT("uncorr_ecc_errors", uncorr_errors),
	E1000_STAT("corr_ecc_errors", corr_errors),
	E1000_STAT("tx_hwtstamp_timeouts", tx_hwtstamp_timeouts),
	E1000_STAT("tx_hwtstamp_skipped", tx_hwtstamp_skipped),
};

#define E1000_GLOBAL_STATS_LEN	ARRAY_SIZE(e1000_gstrings_stats)
#define E1000_STATS_LEN (E1000_GLOBAL_STATS_LEN)
static const char e1000_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)", "Eeprom test    (offline)",
	"Interrupt test (offline)", "Loopback test  (offline)",
	"Link test   (on/offline)"
};

#define E1000_TEST_LEN ARRAY_SIZE(e1000_gstrings_test)

static int e1000_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings *cmd)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 speed, supported, advertising;

	if (hw->phy.media_type == e1000_media_type_copper) {
		supported = (SUPPORTED_10baseT_Half |
			     SUPPORTED_10baseT_Full |
			     SUPPORTED_100baseT_Half |
			     SUPPORTED_100baseT_Full |
			     SUPPORTED_1000baseT_Full |
			     SUPPORTED_Autoneg |
			     SUPPORTED_TP);
		if (hw->phy.type == e1000_phy_ife)
			supported &= ~SUPPORTED_1000baseT_Full;
		advertising = ADVERTISED_TP;

		if (hw->mac.autoneg == 1) {
			advertising |= ADVERTISED_Autoneg;
			/* the e1000 autoneg seems to match ethtool nicely */
			advertising |= hw->phy.autoneg_advertised;
		}

		cmd->base.port = PORT_TP;
		cmd->base.phy_address = hw->phy.addr;
	} else {
		supported   = (SUPPORTED_1000baseT_Full |
			       SUPPORTED_FIBRE |
			       SUPPORTED_Autoneg);

		advertising = (ADVERTISED_1000baseT_Full |
			       ADVERTISED_FIBRE |
			       ADVERTISED_Autoneg);

		cmd->base.port = PORT_FIBRE;
	}

	speed = SPEED_UNKNOWN;
	cmd->base.duplex = DUPLEX_UNKNOWN;

	if (netif_running(netdev)) {
		if (netif_carrier_ok(netdev)) {
			speed = adapter->link_speed;
			cmd->base.duplex = adapter->link_duplex - 1;
		}
	} else if (!pm_runtime_suspended(netdev->dev.parent)) {
		u32 status = er32(STATUS);

		if (status & E1000_STATUS_LU) {
			if (status & E1000_STATUS_SPEED_1000)
				speed = SPEED_1000;
			else if (status & E1000_STATUS_SPEED_100)
				speed = SPEED_100;
			else
				speed = SPEED_10;

			if (status & E1000_STATUS_FD)
				cmd->base.duplex = DUPLEX_FULL;
			else
				cmd->base.duplex = DUPLEX_HALF;
		}
	}

	cmd->base.speed = speed;
	cmd->base.autoneg = ((hw->phy.media_type == e1000_media_type_fiber) ||
			 hw->mac.autoneg) ? AUTONEG_ENABLE : AUTONEG_DISABLE;

	/* MDI-X => 2; MDI =>1; Invalid =>0 */
	if ((hw->phy.media_type == e1000_media_type_copper) &&
	    netif_carrier_ok(netdev))
		cmd->base.eth_tp_mdix = hw->phy.is_mdix ?
			ETH_TP_MDI_X : ETH_TP_MDI;
	else
		cmd->base.eth_tp_mdix = ETH_TP_MDI_INVALID;

	if (hw->phy.mdix == AUTO_ALL_MODES)
		cmd->base.eth_tp_mdix_ctrl = ETH_TP_MDI_AUTO;
	else
		cmd->base.eth_tp_mdix_ctrl = hw->phy.mdix;

	if (hw->phy.media_type != e1000_media_type_copper)
		cmd->base.eth_tp_mdix_ctrl = ETH_TP_MDI_INVALID;

	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.supported,
						supported);
	ethtool_convert_legacy_u32_to_link_mode(cmd->link_modes.advertising,
						advertising);

	return 0;
}

static int e1000_set_spd_dplx(struct e1000_adapter *adapter, u32 spd, u8 dplx)
{
	struct e1000_mac_info *mac = &adapter->hw.mac;

	mac->autoneg = 0;

	/* Make sure dplx is at most 1 bit and lsb of speed is not set
	 * for the switch() below to work
	 */
	if ((spd & 1) || (dplx & ~1))
		goto err_inval;

	/* Fiber NICs only allow 1000 gbps Full duplex */
	if ((adapter->hw.phy.media_type == e1000_media_type_fiber) &&
	    (spd != SPEED_1000) && (dplx != DUPLEX_FULL)) {
		goto err_inval;
	}

	switch (spd + dplx) {
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
		if (adapter->hw.phy.media_type == e1000_media_type_copper) {
			mac->autoneg = 1;
			adapter->hw.phy.autoneg_advertised =
				ADVERTISE_1000_FULL;
		} else {
			mac->forced_speed_duplex = ADVERTISE_1000_FULL;
		}
		break;
	case SPEED_1000 + DUPLEX_HALF:	/* not supported */
	default:
		goto err_inval;
	}

	/* clear MDI, MDI(-X) override is only allowed when autoneg enabled */
	adapter->hw.phy.mdix = AUTO_ALL_MODES;

	return 0;

err_inval:
	e_err("Unsupported Speed/Duplex configuration\n");
	return -EINVAL;
}

static int e1000_set_link_ksettings(struct net_device *netdev,
				    const struct ethtool_link_ksettings *cmd)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	int ret_val = 0;
	u32 advertising;

	ethtool_convert_link_mode_to_legacy_u32(&advertising,
						cmd->link_modes.advertising);

	pm_runtime_get_sync(netdev->dev.parent);

	/* When SoL/IDER sessions are active, autoneg/speed/duplex
	 * cannot be changed
	 */
	if (hw->phy.ops.check_reset_block &&
	    hw->phy.ops.check_reset_block(hw)) {
		e_err("Cannot change link characteristics when SoL/IDER is active.\n");
		ret_val = -EINVAL;
		goto out;
	}

	/* MDI setting is only allowed when autoneg enabled because
	 * some hardware doesn't allow MDI setting when speed or
	 * duplex is forced.
	 */
	if (cmd->base.eth_tp_mdix_ctrl) {
		if (hw->phy.media_type != e1000_media_type_copper) {
			ret_val = -EOPNOTSUPP;
			goto out;
		}

		if ((cmd->base.eth_tp_mdix_ctrl != ETH_TP_MDI_AUTO) &&
		    (cmd->base.autoneg != AUTONEG_ENABLE)) {
			e_err("forcing MDI/MDI-X state is not supported when link speed and/or duplex are forced\n");
			ret_val = -EINVAL;
			goto out;
		}
	}

	while (test_and_set_bit(__E1000_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		hw->mac.autoneg = 1;
		if (hw->phy.media_type == e1000_media_type_fiber)
			hw->phy.autoneg_advertised = ADVERTISED_1000baseT_Full |
			    ADVERTISED_FIBRE | ADVERTISED_Autoneg;
		else
			hw->phy.autoneg_advertised = advertising |
			    ADVERTISED_TP | ADVERTISED_Autoneg;
		advertising = hw->phy.autoneg_advertised;
		if (adapter->fc_autoneg)
			hw->fc.requested_mode = e1000_fc_default;
	} else {
		u32 speed = cmd->base.speed;
		/* calling this overrides forced MDI setting */
		if (e1000_set_spd_dplx(adapter, speed, cmd->base.duplex)) {
			ret_val = -EINVAL;
			goto out;
		}
	}

	/* MDI-X => 2; MDI => 1; Auto => 3 */
	if (cmd->base.eth_tp_mdix_ctrl) {
		/* fix up the value for auto (3 => 0) as zero is mapped
		 * internally to auto
		 */
		if (cmd->base.eth_tp_mdix_ctrl == ETH_TP_MDI_AUTO)
			hw->phy.mdix = AUTO_ALL_MODES;
		else
			hw->phy.mdix = cmd->base.eth_tp_mdix_ctrl;
	}

	/* reset the link */
	if (netif_running(adapter->netdev)) {
		e1000e_down(adapter, true);
		e1000e_up(adapter);
	} else {
		e1000e_reset(adapter);
	}

out:
	pm_runtime_put_sync(netdev->dev.parent);
	clear_bit(__E1000_RESETTING, &adapter->state);
	return ret_val;
}

static void e1000_get_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pause)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	pause->autoneg =
	    (adapter->fc_autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE);

	if (hw->fc.current_mode == e1000_fc_rx_pause) {
		pause->rx_pause = 1;
	} else if (hw->fc.current_mode == e1000_fc_tx_pause) {
		pause->tx_pause = 1;
	} else if (hw->fc.current_mode == e1000_fc_full) {
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
		usleep_range(1000, 2000);

	pm_runtime_get_sync(netdev->dev.parent);

	if (adapter->fc_autoneg == AUTONEG_ENABLE) {
		hw->fc.requested_mode = e1000_fc_default;
		if (netif_running(adapter->netdev)) {
			e1000e_down(adapter, true);
			e1000e_up(adapter);
		} else {
			e1000e_reset(adapter);
		}
	} else {
		if (pause->rx_pause && pause->tx_pause)
			hw->fc.requested_mode = e1000_fc_full;
		else if (pause->rx_pause && !pause->tx_pause)
			hw->fc.requested_mode = e1000_fc_rx_pause;
		else if (!pause->rx_pause && pause->tx_pause)
			hw->fc.requested_mode = e1000_fc_tx_pause;
		else if (!pause->rx_pause && !pause->tx_pause)
			hw->fc.requested_mode = e1000_fc_none;

		hw->fc.current_mode = hw->fc.requested_mode;

		if (hw->phy.media_type == e1000_media_type_fiber) {
			retval = hw->mac.ops.setup_link(hw);
			/* implicit goto out */
		} else {
			retval = e1000e_force_mac_fc(hw);
			if (retval)
				goto out;
			e1000e_set_fc_watermarks(hw);
		}
	}

out:
	pm_runtime_put_sync(netdev->dev.parent);
	clear_bit(__E1000_RESETTING, &adapter->state);
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

static int e1000_get_regs_len(struct net_device __always_unused *netdev)
{
#define E1000_REGS_LEN 32	/* overestimate */
	return E1000_REGS_LEN * sizeof(u32);
}

static void e1000_get_regs(struct net_device *netdev,
			   struct ethtool_regs *regs, void *p)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u16 phy_data;

	pm_runtime_get_sync(netdev->dev.parent);

	memset(p, 0, E1000_REGS_LEN * sizeof(u32));

	regs->version = (1u << 24) |
			(adapter->pdev->revision << 16) |
			adapter->pdev->device;

	regs_buff[0] = er32(CTRL);
	regs_buff[1] = er32(STATUS);

	regs_buff[2] = er32(RCTL);
	regs_buff[3] = er32(RDLEN(0));
	regs_buff[4] = er32(RDH(0));
	regs_buff[5] = er32(RDT(0));
	regs_buff[6] = er32(RDTR);

	regs_buff[7] = er32(TCTL);
	regs_buff[8] = er32(TDLEN(0));
	regs_buff[9] = er32(TDH(0));
	regs_buff[10] = er32(TDT(0));
	regs_buff[11] = er32(TIDV);

	regs_buff[12] = adapter->hw.phy.type;	/* PHY type (IGP=1, M88=0) */

	/* ethtool doesn't use anything past this point, so all this
	 * code is likely legacy junk for apps that may or may not exist
	 */
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
	regs_buff[21] = 0;	/* was idle_errors */
	e1e_rphy(hw, MII_STAT1000, &phy_data);
	regs_buff[24] = (u32)phy_data;	/* phy local receiver status */
	regs_buff[25] = regs_buff[24];	/* phy remote receiver status */

	pm_runtime_put_sync(netdev->dev.parent);
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

	eeprom_buff = kmalloc_array(last_word - first_word + 1, sizeof(u16),
				    GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	pm_runtime_get_sync(netdev->dev.parent);

	if (hw->nvm.type == e1000_nvm_eeprom_spi) {
		ret_val = e1000_read_nvm(hw, first_word,
					 last_word - first_word + 1,
					 eeprom_buff);
	} else {
		for (i = 0; i < last_word - first_word + 1; i++) {
			ret_val = e1000_read_nvm(hw, first_word + i, 1,
						 &eeprom_buff[i]);
			if (ret_val)
				break;
		}
	}

	pm_runtime_put_sync(netdev->dev.parent);

	if (ret_val) {
		/* a read error occurred, throw away the result */
		memset(eeprom_buff, 0xff, sizeof(u16) *
		       (last_word - first_word + 1));
	} else {
		/* Device's eeprom is always little-endian, word addressable */
		for (i = 0; i < last_word - first_word + 1; i++)
			le16_to_cpus(&eeprom_buff[i]);
	}

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

	if (eeprom->magic !=
	    (adapter->pdev->vendor | (adapter->pdev->device << 16)))
		return -EFAULT;

	if (adapter->flags & FLAG_READ_ONLY_NVM)
		return -EINVAL;

	max_len = hw->nvm.word_size * 2;

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;
	eeprom_buff = kmalloc(max_len, GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	ptr = (void *)eeprom_buff;

	pm_runtime_get_sync(netdev->dev.parent);

	if (eeprom->offset & 1) {
		/* need read/modify/write of first changed EEPROM word */
		/* only the second byte of the word is being modified */
		ret_val = e1000_read_nvm(hw, first_word, 1, &eeprom_buff[0]);
		ptr++;
	}
	if (((eeprom->offset + eeprom->len) & 1) && (!ret_val))
		/* need read/modify/write of last changed EEPROM word */
		/* only the first byte of the word is being modified */
		ret_val = e1000_read_nvm(hw, last_word, 1,
					 &eeprom_buff[last_word - first_word]);

	if (ret_val)
		goto out;

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(ptr, bytes, eeprom->len);

	for (i = 0; i < last_word - first_word + 1; i++)
		cpu_to_le16s(&eeprom_buff[i]);

	ret_val = e1000_write_nvm(hw, first_word,
				  last_word - first_word + 1, eeprom_buff);

	if (ret_val)
		goto out;

	/* Update the checksum over the first part of the EEPROM if needed
	 * and flush shadow RAM for applicable controllers
	 */
	if ((first_word <= NVM_CHECKSUM_REG) ||
	    (hw->mac.type == e1000_82583) ||
	    (hw->mac.type == e1000_82574) ||
	    (hw->mac.type == e1000_82573))
		ret_val = e1000e_update_nvm_checksum(hw);

out:
	pm_runtime_put_sync(netdev->dev.parent);
	kfree(eeprom_buff);
	return ret_val;
}

static void e1000_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	strlcpy(drvinfo->driver, e1000e_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, e1000e_driver_version,
		sizeof(drvinfo->version));

	/* EEPROM image version # is reported as firmware version # for
	 * PCI-E controllers
	 */
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%d.%d-%d",
		 (adapter->eeprom_vers & 0xF000) >> 12,
		 (adapter->eeprom_vers & 0x0FF0) >> 4,
		 (adapter->eeprom_vers & 0x000F));

	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
}

static void e1000_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	ring->rx_max_pending = E1000_MAX_RXD;
	ring->tx_max_pending = E1000_MAX_TXD;
	ring->rx_pending = adapter->rx_ring_count;
	ring->tx_pending = adapter->tx_ring_count;
}

static int e1000_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_ring *temp_tx = NULL, *temp_rx = NULL;
	int err = 0, size = sizeof(struct e1000_ring);
	bool set_tx = false, set_rx = false;
	u16 new_rx_count, new_tx_count;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	new_rx_count = clamp_t(u32, ring->rx_pending, E1000_MIN_RXD,
			       E1000_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, REQ_RX_DESCRIPTOR_MULTIPLE);

	new_tx_count = clamp_t(u32, ring->tx_pending, E1000_MIN_TXD,
			       E1000_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, REQ_TX_DESCRIPTOR_MULTIPLE);

	if ((new_tx_count == adapter->tx_ring_count) &&
	    (new_rx_count == adapter->rx_ring_count))
		/* nothing to do */
		return 0;

	while (test_and_set_bit(__E1000_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	if (!netif_running(adapter->netdev)) {
		/* Set counts now and allocate resources during open() */
		adapter->tx_ring->count = new_tx_count;
		adapter->rx_ring->count = new_rx_count;
		adapter->tx_ring_count = new_tx_count;
		adapter->rx_ring_count = new_rx_count;
		goto clear_reset;
	}

	set_tx = (new_tx_count != adapter->tx_ring_count);
	set_rx = (new_rx_count != adapter->rx_ring_count);

	/* Allocate temporary storage for ring updates */
	if (set_tx) {
		temp_tx = vmalloc(size);
		if (!temp_tx) {
			err = -ENOMEM;
			goto free_temp;
		}
	}
	if (set_rx) {
		temp_rx = vmalloc(size);
		if (!temp_rx) {
			err = -ENOMEM;
			goto free_temp;
		}
	}

	pm_runtime_get_sync(netdev->dev.parent);

	e1000e_down(adapter, true);

	/* We can't just free everything and then setup again, because the
	 * ISRs in MSI-X mode get passed pointers to the Tx and Rx ring
	 * structs.  First, attempt to allocate new resources...
	 */
	if (set_tx) {
		memcpy(temp_tx, adapter->tx_ring, size);
		temp_tx->count = new_tx_count;
		err = e1000e_setup_tx_resources(temp_tx);
		if (err)
			goto err_setup;
	}
	if (set_rx) {
		memcpy(temp_rx, adapter->rx_ring, size);
		temp_rx->count = new_rx_count;
		err = e1000e_setup_rx_resources(temp_rx);
		if (err)
			goto err_setup_rx;
	}

	/* ...then free the old resources and copy back any new ring data */
	if (set_tx) {
		e1000e_free_tx_resources(adapter->tx_ring);
		memcpy(adapter->tx_ring, temp_tx, size);
		adapter->tx_ring_count = new_tx_count;
	}
	if (set_rx) {
		e1000e_free_rx_resources(adapter->rx_ring);
		memcpy(adapter->rx_ring, temp_rx, size);
		adapter->rx_ring_count = new_rx_count;
	}

err_setup_rx:
	if (err && set_tx)
		e1000e_free_tx_resources(temp_tx);
err_setup:
	e1000e_up(adapter);
	pm_runtime_put_sync(netdev->dev.parent);
free_temp:
	vfree(temp_tx);
	vfree(temp_rx);
clear_reset:
	clear_bit(__E1000_RESETTING, &adapter->state);
	return err;
}

static bool reg_pattern_test(struct e1000_adapter *adapter, u64 *data,
			     int reg, int offset, u32 mask, u32 write)
{
	u32 pat, val;
	static const u32 test[] = {
		0x5A5A5A5A, 0xA5A5A5A5, 0x00000000, 0xFFFFFFFF
	};
	for (pat = 0; pat < ARRAY_SIZE(test); pat++) {
		E1000_WRITE_REG_ARRAY(&adapter->hw, reg, offset,
				      (test[pat] & write));
		val = E1000_READ_REG_ARRAY(&adapter->hw, reg, offset);
		if (val != (test[pat] & write & mask)) {
			e_err("pattern test failed (reg 0x%05X): got 0x%08X expected 0x%08X\n",
			      reg + (offset << 2), val,
			      (test[pat] & write & mask));
			*data = reg;
			return true;
		}
	}
	return false;
}

static bool reg_set_and_check(struct e1000_adapter *adapter, u64 *data,
			      int reg, u32 mask, u32 write)
{
	u32 val;

	__ew32(&adapter->hw, reg, write & mask);
	val = __er32(&adapter->hw, reg);
	if ((write & mask) != (val & mask)) {
		e_err("set/check test failed (reg 0x%05X): got 0x%08X expected 0x%08X\n",
		      reg, (val & mask), (write & mask));
		*data = reg;
		return true;
	}
	return false;
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
	u32 value;
	u32 before;
	u32 after;
	u32 i;
	u32 toggle;
	u32 mask;
	u32 wlock_mac = 0;

	/* The status register is Read Only, so a write should fail.
	 * Some bits that get toggled are ignored.  There are several bits
	 * on newer hardware that are r/w.
	 */
	switch (mac->type) {
	case e1000_82571:
	case e1000_82572:
	case e1000_80003es2lan:
		toggle = 0x7FFFF3FF;
		break;
	default:
		toggle = 0x7FFFF033;
		break;
	}

	before = er32(STATUS);
	value = (er32(STATUS) & toggle);
	ew32(STATUS, toggle);
	after = er32(STATUS) & toggle;
	if (value != after) {
		e_err("failed STATUS register test got: 0x%08X expected: 0x%08X\n",
		      after, value);
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
	REG_PATTERN_TEST(E1000_RDBAH(0), 0xFFFFFFFF, 0xFFFFFFFF);
	REG_PATTERN_TEST(E1000_RDLEN(0), 0x000FFF80, 0x000FFFFF);
	REG_PATTERN_TEST(E1000_RDH(0), 0x0000FFFF, 0x0000FFFF);
	REG_PATTERN_TEST(E1000_RDT(0), 0x0000FFFF, 0x0000FFFF);
	REG_PATTERN_TEST(E1000_FCRTH, 0x0000FFF8, 0x0000FFF8);
	REG_PATTERN_TEST(E1000_FCTTV, 0x0000FFFF, 0x0000FFFF);
	REG_PATTERN_TEST(E1000_TIPG, 0x3FFFFFFF, 0x3FFFFFFF);
	REG_PATTERN_TEST(E1000_TDBAH(0), 0xFFFFFFFF, 0xFFFFFFFF);
	REG_PATTERN_TEST(E1000_TDLEN(0), 0x000FFF80, 0x000FFFFF);

	REG_SET_AND_CHECK(E1000_RCTL, 0xFFFFFFFF, 0x00000000);

	before = ((adapter->flags & FLAG_IS_ICH) ? 0x06C3B33E : 0x06DFB3FE);
	REG_SET_AND_CHECK(E1000_RCTL, before, 0x003FFFFB);
	REG_SET_AND_CHECK(E1000_TCTL, 0xFFFFFFFF, 0x00000000);

	REG_SET_AND_CHECK(E1000_RCTL, before, 0xFFFFFFFF);
	REG_PATTERN_TEST(E1000_RDBAL(0), 0xFFFFFFF0, 0xFFFFFFFF);
	if (!(adapter->flags & FLAG_IS_ICH))
		REG_PATTERN_TEST(E1000_TXCW, 0xC000FFFF, 0x0000FFFF);
	REG_PATTERN_TEST(E1000_TDBAL(0), 0xFFFFFFF0, 0xFFFFFFFF);
	REG_PATTERN_TEST(E1000_TIDV, 0x0000FFFF, 0x0000FFFF);
	mask = 0x8003FFFF;
	switch (mac->type) {
	case e1000_ich10lan:
	case e1000_pchlan:
	case e1000_pch2lan:
	case e1000_pch_lpt:
	case e1000_pch_spt:
		/* fall through */
	case e1000_pch_cnp:
		mask |= BIT(18);
		break;
	default:
		break;
	}

	if (mac->type >= e1000_pch_lpt)
		wlock_mac = (er32(FWSM) & E1000_FWSM_WLOCK_MAC_MASK) >>
		    E1000_FWSM_WLOCK_MAC_SHIFT;

	for (i = 0; i < mac->rar_entry_count; i++) {
		if (mac->type >= e1000_pch_lpt) {
			/* Cannot test write-protected SHRAL[n] registers */
			if ((wlock_mac == 1) || (wlock_mac && (i > wlock_mac)))
				continue;

			/* SHRAH[9] different than the others */
			if (i == 10)
				mask |= BIT(30);
			else
				mask &= ~BIT(30);
		}
		if (mac->type == e1000_pch2lan) {
			/* SHRAH[0,1,2] different than previous */
			if (i == 1)
				mask &= 0xFFF4FFFF;
			/* SHRAH[3] different than SHRAH[0,1,2] */
			if (i == 4)
				mask |= BIT(30);
			/* RAR[1-6] owned by management engine - skipping */
			if (i > 0)
				i += 6;
		}

		REG_PATTERN_TEST_ARRAY(E1000_RA, ((i << 1) + 1), mask,
				       0xFFFFFFFF);
		/* reset index to actual value */
		if ((mac->type == e1000_pch2lan) && (i > 6))
			i -= 6;
	}

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
			return *data;
		}
		checksum += temp;
	}

	/* If Checksum is not Correct return error else test passed */
	if ((checksum != (u16)NVM_SUM) && !(*data))
		*data = 2;

	return *data;
}

static irqreturn_t e1000_test_intr(int __always_unused irq, void *data)
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
	struct e1000_hw *hw = &adapter->hw;
	u32 mask;
	u32 shared_int = 1;
	u32 irq = adapter->pdev->irq;
	int i;
	int ret_val = 0;
	int int_mode = E1000E_INT_MODE_LEGACY;

	*data = 0;

	/* NOTE: we don't test MSI/MSI-X interrupts here, yet */
	if (adapter->int_mode == E1000E_INT_MODE_MSIX) {
		int_mode = adapter->int_mode;
		e1000e_reset_interrupt_capability(adapter);
		adapter->int_mode = E1000E_INT_MODE_LEGACY;
		e1000e_set_interrupt_capability(adapter);
	}
	/* Hook up test interrupt handler just for this test */
	if (!request_irq(irq, e1000_test_intr, IRQF_PROBE_SHARED, netdev->name,
			 netdev)) {
		shared_int = 0;
	} else if (request_irq(irq, e1000_test_intr, IRQF_SHARED, netdev->name,
			       netdev)) {
		*data = 1;
		ret_val = -1;
		goto out;
	}
	e_info("testing %s interrupt\n", (shared_int ? "shared" : "unshared"));

	/* Disable all the interrupts */
	ew32(IMC, 0xFFFFFFFF);
	e1e_flush();
	usleep_range(10000, 11000);

	/* Test each interrupt */
	for (i = 0; i < 10; i++) {
		/* Interrupt to test */
		mask = BIT(i);

		if (adapter->flags & FLAG_IS_ICH) {
			switch (mask) {
			case E1000_ICR_RXSEQ:
				continue;
			case 0x00000100:
				if (adapter->hw.mac.type == e1000_ich8lan ||
				    adapter->hw.mac.type == e1000_ich9lan)
					continue;
				break;
			default:
				break;
			}
		}

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
			e1e_flush();
			usleep_range(10000, 11000);

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
		e1e_flush();
		usleep_range(10000, 11000);

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
			e1e_flush();
			usleep_range(10000, 11000);

			if (adapter->test_icr) {
				*data = 5;
				break;
			}
		}
	}

	/* Disable all the interrupts */
	ew32(IMC, 0xFFFFFFFF);
	e1e_flush();
	usleep_range(10000, 11000);

	/* Unhook test interrupt handler */
	free_irq(irq, netdev);

out:
	if (int_mode == E1000E_INT_MODE_MSIX) {
		e1000e_reset_interrupt_capability(adapter);
		adapter->int_mode = int_mode;
		e1000e_set_interrupt_capability(adapter);
	}

	return ret_val;
}

static void e1000_free_desc_rings(struct e1000_adapter *adapter)
{
	struct e1000_ring *tx_ring = &adapter->test_tx_ring;
	struct e1000_ring *rx_ring = &adapter->test_rx_ring;
	struct pci_dev *pdev = adapter->pdev;
	struct e1000_buffer *buffer_info;
	int i;

	if (tx_ring->desc && tx_ring->buffer_info) {
		for (i = 0; i < tx_ring->count; i++) {
			buffer_info = &tx_ring->buffer_info[i];

			if (buffer_info->dma)
				dma_unmap_single(&pdev->dev,
						 buffer_info->dma,
						 buffer_info->length,
						 DMA_TO_DEVICE);
			dev_kfree_skb(buffer_info->skb);
		}
	}

	if (rx_ring->desc && rx_ring->buffer_info) {
		for (i = 0; i < rx_ring->count; i++) {
			buffer_info = &rx_ring->buffer_info[i];

			if (buffer_info->dma)
				dma_unmap_single(&pdev->dev,
						 buffer_info->dma,
						 2048, DMA_FROM_DEVICE);
			dev_kfree_skb(buffer_info->skb);
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
				       sizeof(struct e1000_buffer), GFP_KERNEL);
	if (!tx_ring->buffer_info) {
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

	ew32(TDBAL(0), ((u64)tx_ring->dma & 0x00000000FFFFFFFF));
	ew32(TDBAH(0), ((u64)tx_ring->dma >> 32));
	ew32(TDLEN(0), tx_ring->count * sizeof(struct e1000_tx_desc));
	ew32(TDH(0), 0);
	ew32(TDT(0), 0);
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
		    dma_map_single(&pdev->dev, skb->data, skb->len,
				   DMA_TO_DEVICE);
		if (dma_mapping_error(&pdev->dev,
				      tx_ring->buffer_info[i].dma)) {
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
				       sizeof(struct e1000_buffer), GFP_KERNEL);
	if (!rx_ring->buffer_info) {
		ret_val = 5;
		goto err_nomem;
	}

	rx_ring->size = rx_ring->count * sizeof(union e1000_rx_desc_extended);
	rx_ring->desc = dma_alloc_coherent(&pdev->dev, rx_ring->size,
					   &rx_ring->dma, GFP_KERNEL);
	if (!rx_ring->desc) {
		ret_val = 6;
		goto err_nomem;
	}
	rx_ring->next_to_use = 0;
	rx_ring->next_to_clean = 0;

	rctl = er32(RCTL);
	if (!(adapter->flags2 & FLAG2_NO_DISABLE_RX))
		ew32(RCTL, rctl & ~E1000_RCTL_EN);
	ew32(RDBAL(0), ((u64)rx_ring->dma & 0xFFFFFFFF));
	ew32(RDBAH(0), ((u64)rx_ring->dma >> 32));
	ew32(RDLEN(0), rx_ring->size);
	ew32(RDH(0), 0);
	ew32(RDT(0), 0);
	rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SZ_2048 |
	    E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_LPE |
	    E1000_RCTL_SBP | E1000_RCTL_SECRC |
	    E1000_RCTL_LBM_NO | E1000_RCTL_RDMTS_HALF |
	    (adapter->hw.mac.mc_filter_type << E1000_RCTL_MO_SHIFT);
	ew32(RCTL, rctl);

	for (i = 0; i < rx_ring->count; i++) {
		union e1000_rx_desc_extended *rx_desc;
		struct sk_buff *skb;

		skb = alloc_skb(2048 + NET_IP_ALIGN, GFP_KERNEL);
		if (!skb) {
			ret_val = 7;
			goto err_nomem;
		}
		skb_reserve(skb, NET_IP_ALIGN);
		rx_ring->buffer_info[i].skb = skb;
		rx_ring->buffer_info[i].dma =
		    dma_map_single(&pdev->dev, skb->data, 2048,
				   DMA_FROM_DEVICE);
		if (dma_mapping_error(&pdev->dev,
				      rx_ring->buffer_info[i].dma)) {
			ret_val = 8;
			goto err_nomem;
		}
		rx_desc = E1000_RX_DESC_EXT(*rx_ring, i);
		rx_desc->read.buffer_addr =
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
	u16 phy_reg = 0;
	s32 ret_val = 0;

	hw->mac.autoneg = 0;

	if (hw->phy.type == e1000_phy_ife) {
		/* force 100, set loopback */
		e1e_wphy(hw, MII_BMCR, 0x6100);

		/* Now set up the MAC to the same speed/duplex as the PHY. */
		ctrl_reg = er32(CTRL);
		ctrl_reg &= ~E1000_CTRL_SPD_SEL; /* Clear the speed sel bits */
		ctrl_reg |= (E1000_CTRL_FRCSPD | /* Set the Force Speed Bit */
			     E1000_CTRL_FRCDPX | /* Set the Force Duplex Bit */
			     E1000_CTRL_SPD_100 |/* Force Speed to 100 */
			     E1000_CTRL_FD);	 /* Force Duplex to FULL */

		ew32(CTRL, ctrl_reg);
		e1e_flush();
		usleep_range(500, 1000);

		return 0;
	}

	/* Specific PHY configuration for loopback */
	switch (hw->phy.type) {
	case e1000_phy_m88:
		/* Auto-MDI/MDIX Off */
		e1e_wphy(hw, M88E1000_PHY_SPEC_CTRL, 0x0808);
		/* reset to update Auto-MDI/MDIX */
		e1e_wphy(hw, MII_BMCR, 0x9140);
		/* autoneg off */
		e1e_wphy(hw, MII_BMCR, 0x8140);
		break;
	case e1000_phy_gg82563:
		e1e_wphy(hw, GG82563_PHY_KMRN_MODE_CTRL, 0x1CC);
		break;
	case e1000_phy_bm:
		/* Set Default MAC Interface speed to 1GB */
		e1e_rphy(hw, PHY_REG(2, 21), &phy_reg);
		phy_reg &= ~0x0007;
		phy_reg |= 0x006;
		e1e_wphy(hw, PHY_REG(2, 21), phy_reg);
		/* Assert SW reset for above settings to take effect */
		hw->phy.ops.commit(hw);
		usleep_range(1000, 2000);
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
		break;
	case e1000_phy_82577:
	case e1000_phy_82578:
		/* Workaround: K1 must be disabled for stable 1Gbps operation */
		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val) {
			e_err("Cannot setup 1Gbps loopback.\n");
			return ret_val;
		}
		e1000_configure_k1_ich8lan(hw, false);
		hw->phy.ops.release(hw);
		break;
	case e1000_phy_82579:
		/* Disable PHY energy detect power down */
		e1e_rphy(hw, PHY_REG(0, 21), &phy_reg);
		e1e_wphy(hw, PHY_REG(0, 21), phy_reg & ~BIT(3));
		/* Disable full chip energy detect */
		e1e_rphy(hw, PHY_REG(776, 18), &phy_reg);
		e1e_wphy(hw, PHY_REG(776, 18), phy_reg | 1);
		/* Enable loopback on the PHY */
		e1e_wphy(hw, I82577_PHY_LBK_CTRL, 0x8001);
		break;
	default:
		break;
	}

	/* force 1000, set loopback */
	e1e_wphy(hw, MII_BMCR, 0x4140);
	msleep(250);

	/* Now set up the MAC to the same speed/duplex as the PHY. */
	ctrl_reg = er32(CTRL);
	ctrl_reg &= ~E1000_CTRL_SPD_SEL; /* Clear the speed sel bits */
	ctrl_reg |= (E1000_CTRL_FRCSPD | /* Set the Force Speed Bit */
		     E1000_CTRL_FRCDPX | /* Set the Force Duplex Bit */
		     E1000_CTRL_SPD_1000 |/* Force Speed to 1000 */
		     E1000_CTRL_FD);	 /* Force Duplex to FULL */

	if (adapter->flags & FLAG_IS_ICH)
		ctrl_reg |= E1000_CTRL_SLU;	/* Set Link Up */

	if (hw->phy.media_type == e1000_media_type_copper &&
	    hw->phy.type == e1000_phy_m88) {
		ctrl_reg |= E1000_CTRL_ILOS;	/* Invert Loss of Signal */
	} else {
		/* Set the ILOS bit on the fiber Nic if half duplex link is
		 * detected.
		 */
		if ((er32(STATUS) & E1000_STATUS_FD) == 0)
			ctrl_reg |= (E1000_CTRL_ILOS | E1000_CTRL_SLU);
	}

	ew32(CTRL, ctrl_reg);

	/* Disable the receiver on the PHY so when a cable is plugged in, the
	 * PHY does not begin to autoneg when a cable is reconnected to the NIC.
	 */
	if (hw->phy.type == e1000_phy_m88)
		e1000_phy_disable_receiver(adapter);

	usleep_range(500, 1000);

	return 0;
}

static int e1000_set_82571_fiber_loopback(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrl = er32(CTRL);
	int link;

	/* special requirements for 82571/82572 fiber adapters */

	/* jump through hoops to make sure link is up because serdes
	 * link is hardwired up
	 */
	ctrl |= E1000_CTRL_SLU;
	ew32(CTRL, ctrl);

	/* disable autoneg */
	ctrl = er32(TXCW);
	ctrl &= ~BIT(31);
	ew32(TXCW, ctrl);

	link = (er32(STATUS) & E1000_STATUS_LU);

	if (!link) {
		/* set invert loss of signal */
		ctrl = er32(CTRL);
		ctrl |= E1000_CTRL_ILOS;
		ew32(CTRL, ctrl);
	}

	/* special write to serdes control register to enable SerDes analog
	 * loopback
	 */
	ew32(SCTL, E1000_SCTL_ENABLE_SERDES_LOOPBACK);
	e1e_flush();
	usleep_range(10000, 11000);

	return 0;
}

/* only call this for fiber/serdes connections to es2lan */
static int e1000_set_es2lan_mac_loopback(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 ctrlext = er32(CTRL_EXT);
	u32 ctrl = er32(CTRL);

	/* save CTRL_EXT to restore later, reuse an empty variable (unused
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
	u32 rctl, fext_nvm11, tarc0;

	if (hw->mac.type >= e1000_pch_spt) {
		fext_nvm11 = er32(FEXTNVM11);
		fext_nvm11 |= E1000_FEXTNVM11_DISABLE_MULR_FIX;
		ew32(FEXTNVM11, fext_nvm11);
		tarc0 = er32(TARC(0));
		/* clear bits 28 & 29 (control of MULR concurrent requests) */
		tarc0 &= 0xcfffffff;
		/* set bit 29 (value of MULR requests is now 2) */
		tarc0 |= 0x20000000;
		ew32(TARC(0), tarc0);
	}
	if (hw->phy.media_type == e1000_media_type_fiber ||
	    hw->phy.media_type == e1000_media_type_internal_serdes) {
		switch (hw->mac.type) {
		case e1000_80003es2lan:
			return e1000_set_es2lan_mac_loopback(adapter);
		case e1000_82571:
		case e1000_82572:
			return e1000_set_82571_fiber_loopback(adapter);
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
	u32 rctl, fext_nvm11, tarc0;
	u16 phy_reg;

	rctl = er32(RCTL);
	rctl &= ~(E1000_RCTL_LBM_TCVR | E1000_RCTL_LBM_MAC);
	ew32(RCTL, rctl);

	switch (hw->mac.type) {
	case e1000_pch_spt:
	case e1000_pch_cnp:
		fext_nvm11 = er32(FEXTNVM11);
		fext_nvm11 &= ~E1000_FEXTNVM11_DISABLE_MULR_FIX;
		ew32(FEXTNVM11, fext_nvm11);
		tarc0 = er32(TARC(0));
		/* clear bits 28 & 29 (control of MULR concurrent requests) */
		/* set bit 29 (value of MULR requests is now 0) */
		tarc0 &= 0xcfffffff;
		ew32(TARC(0), tarc0);
		/* fall through */
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
			ew32(SCTL, E1000_SCTL_DISABLE_SERDES_LOOPBACK);
			e1e_flush();
			usleep_range(10000, 11000);
			break;
		}
		/* Fall Through */
	default:
		hw->mac.autoneg = 1;
		if (hw->phy.type == e1000_phy_gg82563)
			e1e_wphy(hw, GG82563_PHY_KMRN_MODE_CTRL, 0x180);
		e1e_rphy(hw, MII_BMCR, &phy_reg);
		if (phy_reg & BMCR_LOOPBACK) {
			phy_reg &= ~BMCR_LOOPBACK;
			e1e_wphy(hw, MII_BMCR, phy_reg);
			if (hw->phy.ops.commit)
				hw->phy.ops.commit(hw);
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
	struct e1000_buffer *buffer_info;
	int i, j, k, l;
	int lc;
	int good_cnt;
	int ret_val = 0;
	unsigned long time;

	ew32(RDT(0), rx_ring->count - 1);

	/* Calculate the loop count based on the largest descriptor ring
	 * The idea is to wrap the largest ring a number of times using 64
	 * send/receive pairs during each loop
	 */

	if (rx_ring->count <= tx_ring->count)
		lc = ((tx_ring->count / 64) * 2) + 1;
	else
		lc = ((rx_ring->count / 64) * 2) + 1;

	k = 0;
	l = 0;
	/* loop count loop */
	for (j = 0; j <= lc; j++) {
		/* send the packets */
		for (i = 0; i < 64; i++) {
			buffer_info = &tx_ring->buffer_info[k];

			e1000_create_lbtest_frame(buffer_info->skb, 1024);
			dma_sync_single_for_device(&pdev->dev,
						   buffer_info->dma,
						   buffer_info->length,
						   DMA_TO_DEVICE);
			k++;
			if (k == tx_ring->count)
				k = 0;
		}
		ew32(TDT(0), k);
		e1e_flush();
		msleep(200);
		time = jiffies;	/* set the start time for the receive */
		good_cnt = 0;
		/* receive the sent packets */
		do {
			buffer_info = &rx_ring->buffer_info[l];

			dma_sync_single_for_cpu(&pdev->dev,
						buffer_info->dma, 2048,
						DMA_FROM_DEVICE);

			ret_val = e1000_check_lbtest_frame(buffer_info->skb,
							   1024);
			if (!ret_val)
				good_cnt++;
			l++;
			if (l == rx_ring->count)
				l = 0;
			/* time + 20 msecs (200 msecs on 2.4) is more than
			 * enough time to complete the receives, if it's
			 * exceeded, break and error off
			 */
		} while ((good_cnt < 64) && !time_after(jiffies, time + 20));
		if (good_cnt != 64) {
			ret_val = 13;	/* ret_val is the same as mis-compare */
			break;
		}
		if (time_after(jiffies, time + 20)) {
			ret_val = 14;	/* error code for time out error */
			break;
		}
	}
	return ret_val;
}

static int e1000_loopback_test(struct e1000_adapter *adapter, u64 *data)
{
	struct e1000_hw *hw = &adapter->hw;

	/* PHY loopback cannot be performed if SoL/IDER sessions are active */
	if (hw->phy.ops.check_reset_block &&
	    hw->phy.ops.check_reset_block(hw)) {
		e_err("Cannot do PHY loopback test when SoL/IDER is active.\n");
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

		hw->mac.serdes_has_link = false;

		/* On some blade server designs, link establishment
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
			/* On some Phy/switch combinations, link establishment
			 * can take a few seconds more than expected.
			 */
			msleep_interruptible(5000);

		if (!(er32(STATUS) & E1000_STATUS_LU))
			*data = 1;
	}
	return *data;
}

static int e1000e_get_sset_count(struct net_device __always_unused *netdev,
				 int sset)
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

	pm_runtime_get_sync(netdev->dev.parent);

	set_bit(__E1000_TESTING, &adapter->state);

	if (!if_running) {
		/* Get control of and reset hardware */
		if (adapter->flags & FLAG_HAS_AMT)
			e1000e_get_hw_control(adapter);

		e1000e_power_up_phy(adapter);

		adapter->hw.phy.autoneg_wait_to_complete = 1;
		e1000e_reset(adapter);
		adapter->hw.phy.autoneg_wait_to_complete = 0;
	}

	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline tests */

		/* save speed, duplex, autoneg settings */
		autoneg_advertised = adapter->hw.phy.autoneg_advertised;
		forced_speed_duplex = adapter->hw.mac.forced_speed_duplex;
		autoneg = adapter->hw.mac.autoneg;

		e_info("offline testing starting\n");

		if (if_running)
			/* indicate we're in test mode */
			e1000e_close(netdev);

		if (e1000_reg_test(adapter, &data[0]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		e1000e_reset(adapter);
		if (e1000_eeprom_test(adapter, &data[1]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		e1000e_reset(adapter);
		if (e1000_intr_test(adapter, &data[2]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		e1000e_reset(adapter);
		if (e1000_loopback_test(adapter, &data[3]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* force this routine to wait until autoneg complete/timeout */
		adapter->hw.phy.autoneg_wait_to_complete = 1;
		e1000e_reset(adapter);
		adapter->hw.phy.autoneg_wait_to_complete = 0;

		if (e1000_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* restore speed, duplex, autoneg settings */
		adapter->hw.phy.autoneg_advertised = autoneg_advertised;
		adapter->hw.mac.forced_speed_duplex = forced_speed_duplex;
		adapter->hw.mac.autoneg = autoneg;
		e1000e_reset(adapter);

		clear_bit(__E1000_TESTING, &adapter->state);
		if (if_running)
			e1000e_open(netdev);
	} else {
		/* Online tests */

		e_info("online testing starting\n");

		/* register, eeprom, intr and loopback tests not run online */
		data[0] = 0;
		data[1] = 0;
		data[2] = 0;
		data[3] = 0;

		if (e1000_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		clear_bit(__E1000_TESTING, &adapter->state);
	}

	if (!if_running) {
		e1000e_reset(adapter);

		if (adapter->flags & FLAG_HAS_AMT)
			e1000e_release_hw_control(adapter);
	}

	msleep_interruptible(4 * 1000);

	pm_runtime_put_sync(netdev->dev.parent);
}

static void e1000_get_wol(struct net_device *netdev,
			  struct ethtool_wolinfo *wol)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	wol->supported = 0;
	wol->wolopts = 0;

	if (!(adapter->flags & FLAG_HAS_WOL) ||
	    !device_can_wakeup(&adapter->pdev->dev))
		return;

	wol->supported = WAKE_UCAST | WAKE_MCAST |
	    WAKE_BCAST | WAKE_MAGIC | WAKE_PHY;

	/* apply any specific unsupported masks here */
	if (adapter->flags & FLAG_NO_WAKE_UCAST) {
		wol->supported &= ~WAKE_UCAST;

		if (adapter->wol & E1000_WUFC_EX)
			e_err("Interface does not support directed (unicast) frame wake-up packets\n");
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
}

static int e1000_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	if (!(adapter->flags & FLAG_HAS_WOL) ||
	    !device_can_wakeup(&adapter->pdev->dev) ||
	    (wol->wolopts & ~(WAKE_UCAST | WAKE_MCAST | WAKE_BCAST |
			      WAKE_MAGIC | WAKE_PHY)))
		return -EOPNOTSUPP;

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
		pm_runtime_get_sync(netdev->dev.parent);

		if (!hw->mac.ops.blink_led)
			return 2;	/* cycle on/off twice per second */

		hw->mac.ops.blink_led(hw);
		break;

	case ETHTOOL_ID_INACTIVE:
		if (hw->phy.type == e1000_phy_ife)
			e1e_wphy(hw, IFE_PHY_SPECIAL_CONTROL_LED, 0);
		hw->mac.ops.led_off(hw);
		hw->mac.ops.cleanup_led(hw);
		pm_runtime_put_sync(netdev->dev.parent);
		break;

	case ETHTOOL_ID_ON:
		hw->mac.ops.led_on(hw);
		break;

	case ETHTOOL_ID_OFF:
		hw->mac.ops.led_off(hw);
		break;
	}

	return 0;
}

static int e1000_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

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

	if ((ec->rx_coalesce_usecs > E1000_MAX_ITR_USECS) ||
	    ((ec->rx_coalesce_usecs > 4) &&
	     (ec->rx_coalesce_usecs < E1000_MIN_ITR_USECS)) ||
	    (ec->rx_coalesce_usecs == 2))
		return -EINVAL;

	if (ec->rx_coalesce_usecs == 4) {
		adapter->itr_setting = 4;
		adapter->itr = adapter->itr_setting;
	} else if (ec->rx_coalesce_usecs <= 3) {
		adapter->itr = 20000;
		adapter->itr_setting = ec->rx_coalesce_usecs;
	} else {
		adapter->itr = (1000000 / ec->rx_coalesce_usecs);
		adapter->itr_setting = adapter->itr & ~3;
	}

	pm_runtime_get_sync(netdev->dev.parent);

	if (adapter->itr_setting != 0)
		e1000e_write_itr(adapter, adapter->itr);
	else
		e1000e_write_itr(adapter, 0);

	pm_runtime_put_sync(netdev->dev.parent);

	return 0;
}

static int e1000_nway_reset(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	if (!netif_running(netdev))
		return -EAGAIN;

	if (!adapter->hw.mac.autoneg)
		return -EINVAL;

	pm_runtime_get_sync(netdev->dev.parent);
	e1000e_reinit_locked(adapter);
	pm_runtime_put_sync(netdev->dev.parent);

	return 0;
}

static void e1000_get_ethtool_stats(struct net_device *netdev,
				    struct ethtool_stats __always_unused *stats,
				    u64 *data)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct rtnl_link_stats64 net_stats;
	int i;
	char *p = NULL;

	pm_runtime_get_sync(netdev->dev.parent);

	dev_get_stats(netdev, &net_stats);

	pm_runtime_put_sync(netdev->dev.parent);

	for (i = 0; i < E1000_GLOBAL_STATS_LEN; i++) {
		switch (e1000_gstrings_stats[i].type) {
		case NETDEV_STATS:
			p = (char *)&net_stats +
			    e1000_gstrings_stats[i].stat_offset;
			break;
		case E1000_STATS:
			p = (char *)adapter +
			    e1000_gstrings_stats[i].stat_offset;
			break;
		default:
			data[i] = 0;
			continue;
		}

		data[i] = (e1000_gstrings_stats[i].sizeof_stat ==
			   sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
}

static void e1000_get_strings(struct net_device __always_unused *netdev,
			      u32 stringset, u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, e1000_gstrings_test, sizeof(e1000_gstrings_test));
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

static int e1000_get_rxnfc(struct net_device *netdev,
			   struct ethtool_rxnfc *info,
			   u32 __always_unused *rule_locs)
{
	info->data = 0;

	switch (info->cmd) {
	case ETHTOOL_GRXFH: {
		struct e1000_adapter *adapter = netdev_priv(netdev);
		struct e1000_hw *hw = &adapter->hw;
		u32 mrqc;

		pm_runtime_get_sync(netdev->dev.parent);
		mrqc = er32(MRQC);
		pm_runtime_put_sync(netdev->dev.parent);

		if (!(mrqc & E1000_MRQC_RSS_FIELD_MASK))
			return 0;

		switch (info->flow_type) {
		case TCP_V4_FLOW:
			if (mrqc & E1000_MRQC_RSS_FIELD_IPV4_TCP)
				info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
			/* fall through */
		case UDP_V4_FLOW:
		case SCTP_V4_FLOW:
		case AH_ESP_V4_FLOW:
		case IPV4_FLOW:
			if (mrqc & E1000_MRQC_RSS_FIELD_IPV4)
				info->data |= RXH_IP_SRC | RXH_IP_DST;
			break;
		case TCP_V6_FLOW:
			if (mrqc & E1000_MRQC_RSS_FIELD_IPV6_TCP)
				info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
			/* fall through */
		case UDP_V6_FLOW:
		case SCTP_V6_FLOW:
		case AH_ESP_V6_FLOW:
		case IPV6_FLOW:
			if (mrqc & E1000_MRQC_RSS_FIELD_IPV6)
				info->data |= RXH_IP_SRC | RXH_IP_DST;
			break;
		default:
			break;
		}
		return 0;
	}
	default:
		return -EOPNOTSUPP;
	}
}

static int e1000e_get_eee(struct net_device *netdev, struct ethtool_eee *edata)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u16 cap_addr, lpa_addr, pcs_stat_addr, phy_data;
	u32 ret_val;

	if (!(adapter->flags2 & FLAG2_HAS_EEE))
		return -EOPNOTSUPP;

	switch (hw->phy.type) {
	case e1000_phy_82579:
		cap_addr = I82579_EEE_CAPABILITY;
		lpa_addr = I82579_EEE_LP_ABILITY;
		pcs_stat_addr = I82579_EEE_PCS_STATUS;
		break;
	case e1000_phy_i217:
		cap_addr = I217_EEE_CAPABILITY;
		lpa_addr = I217_EEE_LP_ABILITY;
		pcs_stat_addr = I217_EEE_PCS_STATUS;
		break;
	default:
		return -EOPNOTSUPP;
	}

	pm_runtime_get_sync(netdev->dev.parent);

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val) {
		pm_runtime_put_sync(netdev->dev.parent);
		return -EBUSY;
	}

	/* EEE Capability */
	ret_val = e1000_read_emi_reg_locked(hw, cap_addr, &phy_data);
	if (ret_val)
		goto release;
	edata->supported = mmd_eee_cap_to_ethtool_sup_t(phy_data);

	/* EEE Advertised */
	edata->advertised = mmd_eee_adv_to_ethtool_adv_t(adapter->eee_advert);

	/* EEE Link Partner Advertised */
	ret_val = e1000_read_emi_reg_locked(hw, lpa_addr, &phy_data);
	if (ret_val)
		goto release;
	edata->lp_advertised = mmd_eee_adv_to_ethtool_adv_t(phy_data);

	/* EEE PCS Status */
	ret_val = e1000_read_emi_reg_locked(hw, pcs_stat_addr, &phy_data);
	if (ret_val)
		goto release;
	if (hw->phy.type == e1000_phy_82579)
		phy_data <<= 8;

	/* Result of the EEE auto negotiation - there is no register that
	 * has the status of the EEE negotiation so do a best-guess based
	 * on whether Tx or Rx LPI indications have been received.
	 */
	if (phy_data & (E1000_EEE_TX_LPI_RCVD | E1000_EEE_RX_LPI_RCVD))
		edata->eee_active = true;

	edata->eee_enabled = !hw->dev_spec.ich8lan.eee_disable;
	edata->tx_lpi_enabled = true;
	edata->tx_lpi_timer = er32(LPIC) >> E1000_LPIC_LPIET_SHIFT;

release:
	hw->phy.ops.release(hw);
	if (ret_val)
		ret_val = -ENODATA;

	pm_runtime_put_sync(netdev->dev.parent);

	return ret_val;
}

static int e1000e_set_eee(struct net_device *netdev, struct ethtool_eee *edata)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct ethtool_eee eee_curr;
	s32 ret_val;

	ret_val = e1000e_get_eee(netdev, &eee_curr);
	if (ret_val)
		return ret_val;

	if (eee_curr.tx_lpi_enabled != edata->tx_lpi_enabled) {
		e_err("Setting EEE tx-lpi is not supported\n");
		return -EINVAL;
	}

	if (eee_curr.tx_lpi_timer != edata->tx_lpi_timer) {
		e_err("Setting EEE Tx LPI timer is not supported\n");
		return -EINVAL;
	}

	if (edata->advertised & ~(ADVERTISE_100_FULL | ADVERTISE_1000_FULL)) {
		e_err("EEE advertisement supports only 100TX and/or 1000T full-duplex\n");
		return -EINVAL;
	}

	adapter->eee_advert = ethtool_adv_to_mmd_eee_adv_t(edata->advertised);

	hw->dev_spec.ich8lan.eee_disable = !edata->eee_enabled;

	pm_runtime_get_sync(netdev->dev.parent);

	/* reset the link */
	if (netif_running(netdev))
		e1000e_reinit_locked(adapter);
	else
		e1000e_reset(adapter);

	pm_runtime_put_sync(netdev->dev.parent);

	return 0;
}

static int e1000e_get_ts_info(struct net_device *netdev,
			      struct ethtool_ts_info *info)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	ethtool_op_get_ts_info(netdev, info);

	if (!(adapter->flags & FLAG_HAS_HW_TIMESTAMP))
		return 0;

	info->so_timestamping |= (SOF_TIMESTAMPING_TX_HARDWARE |
				  SOF_TIMESTAMPING_RX_HARDWARE |
				  SOF_TIMESTAMPING_RAW_HARDWARE);

	info->tx_types = BIT(HWTSTAMP_TX_OFF) | BIT(HWTSTAMP_TX_ON);

	info->rx_filters = (BIT(HWTSTAMP_FILTER_NONE) |
			    BIT(HWTSTAMP_FILTER_PTP_V1_L4_SYNC) |
			    BIT(HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_EVENT) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_SYNC) |
			    BIT(HWTSTAMP_FILTER_PTP_V2_DELAY_REQ) |
			    BIT(HWTSTAMP_FILTER_ALL));

	if (adapter->ptp_clock)
		info->phc_index = ptp_clock_index(adapter->ptp_clock);

	return 0;
}

static const struct ethtool_ops e1000_ethtool_ops = {
	.get_drvinfo		= e1000_get_drvinfo,
	.get_regs_len		= e1000_get_regs_len,
	.get_regs		= e1000_get_regs,
	.get_wol		= e1000_get_wol,
	.set_wol		= e1000_set_wol,
	.get_msglevel		= e1000_get_msglevel,
	.set_msglevel		= e1000_set_msglevel,
	.nway_reset		= e1000_nway_reset,
	.get_link		= ethtool_op_get_link,
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
	.get_sset_count		= e1000e_get_sset_count,
	.get_coalesce		= e1000_get_coalesce,
	.set_coalesce		= e1000_set_coalesce,
	.get_rxnfc		= e1000_get_rxnfc,
	.get_ts_info		= e1000e_get_ts_info,
	.get_eee		= e1000e_get_eee,
	.set_eee		= e1000e_set_eee,
	.get_link_ksettings	= e1000_get_link_ksettings,
	.set_link_ksettings	= e1000_set_link_ksettings,
};

void e1000e_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &e1000_ethtool_ops;
}
