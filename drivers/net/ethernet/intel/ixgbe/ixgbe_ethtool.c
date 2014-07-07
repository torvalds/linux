/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2014 Intel Corporation.

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

/* ethtool support for ixgbe */

#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/uaccess.h>

#include "ixgbe.h"
#include "ixgbe_phy.h"


#define IXGBE_ALL_RAR_ENTRIES 16

enum {NETDEV_STATS, IXGBE_STATS};

struct ixgbe_stats {
	char stat_string[ETH_GSTRING_LEN];
	int type;
	int sizeof_stat;
	int stat_offset;
};

#define IXGBE_STAT(m)		IXGBE_STATS, \
				sizeof(((struct ixgbe_adapter *)0)->m), \
				offsetof(struct ixgbe_adapter, m)
#define IXGBE_NETDEV_STAT(m)	NETDEV_STATS, \
				sizeof(((struct rtnl_link_stats64 *)0)->m), \
				offsetof(struct rtnl_link_stats64, m)

static const struct ixgbe_stats ixgbe_gstrings_stats[] = {
	{"rx_packets", IXGBE_NETDEV_STAT(rx_packets)},
	{"tx_packets", IXGBE_NETDEV_STAT(tx_packets)},
	{"rx_bytes", IXGBE_NETDEV_STAT(rx_bytes)},
	{"tx_bytes", IXGBE_NETDEV_STAT(tx_bytes)},
	{"rx_pkts_nic", IXGBE_STAT(stats.gprc)},
	{"tx_pkts_nic", IXGBE_STAT(stats.gptc)},
	{"rx_bytes_nic", IXGBE_STAT(stats.gorc)},
	{"tx_bytes_nic", IXGBE_STAT(stats.gotc)},
	{"lsc_int", IXGBE_STAT(lsc_int)},
	{"tx_busy", IXGBE_STAT(tx_busy)},
	{"non_eop_descs", IXGBE_STAT(non_eop_descs)},
	{"rx_errors", IXGBE_NETDEV_STAT(rx_errors)},
	{"tx_errors", IXGBE_NETDEV_STAT(tx_errors)},
	{"rx_dropped", IXGBE_NETDEV_STAT(rx_dropped)},
	{"tx_dropped", IXGBE_NETDEV_STAT(tx_dropped)},
	{"multicast", IXGBE_NETDEV_STAT(multicast)},
	{"broadcast", IXGBE_STAT(stats.bprc)},
	{"rx_no_buffer_count", IXGBE_STAT(stats.rnbc[0]) },
	{"collisions", IXGBE_NETDEV_STAT(collisions)},
	{"rx_over_errors", IXGBE_NETDEV_STAT(rx_over_errors)},
	{"rx_crc_errors", IXGBE_NETDEV_STAT(rx_crc_errors)},
	{"rx_frame_errors", IXGBE_NETDEV_STAT(rx_frame_errors)},
	{"hw_rsc_aggregated", IXGBE_STAT(rsc_total_count)},
	{"hw_rsc_flushed", IXGBE_STAT(rsc_total_flush)},
	{"fdir_match", IXGBE_STAT(stats.fdirmatch)},
	{"fdir_miss", IXGBE_STAT(stats.fdirmiss)},
	{"fdir_overflow", IXGBE_STAT(fdir_overflow)},
	{"rx_fifo_errors", IXGBE_NETDEV_STAT(rx_fifo_errors)},
	{"rx_missed_errors", IXGBE_NETDEV_STAT(rx_missed_errors)},
	{"tx_aborted_errors", IXGBE_NETDEV_STAT(tx_aborted_errors)},
	{"tx_carrier_errors", IXGBE_NETDEV_STAT(tx_carrier_errors)},
	{"tx_fifo_errors", IXGBE_NETDEV_STAT(tx_fifo_errors)},
	{"tx_heartbeat_errors", IXGBE_NETDEV_STAT(tx_heartbeat_errors)},
	{"tx_timeout_count", IXGBE_STAT(tx_timeout_count)},
	{"tx_restart_queue", IXGBE_STAT(restart_queue)},
	{"rx_long_length_errors", IXGBE_STAT(stats.roc)},
	{"rx_short_length_errors", IXGBE_STAT(stats.ruc)},
	{"tx_flow_control_xon", IXGBE_STAT(stats.lxontxc)},
	{"rx_flow_control_xon", IXGBE_STAT(stats.lxonrxc)},
	{"tx_flow_control_xoff", IXGBE_STAT(stats.lxofftxc)},
	{"rx_flow_control_xoff", IXGBE_STAT(stats.lxoffrxc)},
	{"rx_csum_offload_errors", IXGBE_STAT(hw_csum_rx_error)},
	{"alloc_rx_page_failed", IXGBE_STAT(alloc_rx_page_failed)},
	{"alloc_rx_buff_failed", IXGBE_STAT(alloc_rx_buff_failed)},
	{"rx_no_dma_resources", IXGBE_STAT(hw_rx_no_dma_resources)},
	{"os2bmc_rx_by_bmc", IXGBE_STAT(stats.o2bgptc)},
	{"os2bmc_tx_by_bmc", IXGBE_STAT(stats.b2ospc)},
	{"os2bmc_tx_by_host", IXGBE_STAT(stats.o2bspc)},
	{"os2bmc_rx_by_host", IXGBE_STAT(stats.b2ogprc)},
#ifdef IXGBE_FCOE
	{"fcoe_bad_fccrc", IXGBE_STAT(stats.fccrc)},
	{"rx_fcoe_dropped", IXGBE_STAT(stats.fcoerpdc)},
	{"rx_fcoe_packets", IXGBE_STAT(stats.fcoeprc)},
	{"rx_fcoe_dwords", IXGBE_STAT(stats.fcoedwrc)},
	{"fcoe_noddp", IXGBE_STAT(stats.fcoe_noddp)},
	{"fcoe_noddp_ext_buff", IXGBE_STAT(stats.fcoe_noddp_ext_buff)},
	{"tx_fcoe_packets", IXGBE_STAT(stats.fcoeptc)},
	{"tx_fcoe_dwords", IXGBE_STAT(stats.fcoedwtc)},
#endif /* IXGBE_FCOE */
};

/* ixgbe allocates num_tx_queues and num_rx_queues symmetrically so
 * we set the num_rx_queues to evaluate to num_tx_queues. This is
 * used because we do not have a good way to get the max number of
 * rx queues with CONFIG_RPS disabled.
 */
#define IXGBE_NUM_RX_QUEUES netdev->num_tx_queues

#define IXGBE_QUEUE_STATS_LEN ( \
	(netdev->num_tx_queues + IXGBE_NUM_RX_QUEUES) * \
	(sizeof(struct ixgbe_queue_stats) / sizeof(u64)))
#define IXGBE_GLOBAL_STATS_LEN ARRAY_SIZE(ixgbe_gstrings_stats)
#define IXGBE_PB_STATS_LEN ( \
			(sizeof(((struct ixgbe_adapter *)0)->stats.pxonrxc) + \
			 sizeof(((struct ixgbe_adapter *)0)->stats.pxontxc) + \
			 sizeof(((struct ixgbe_adapter *)0)->stats.pxoffrxc) + \
			 sizeof(((struct ixgbe_adapter *)0)->stats.pxofftxc)) \
			/ sizeof(u64))
#define IXGBE_STATS_LEN (IXGBE_GLOBAL_STATS_LEN + \
			 IXGBE_PB_STATS_LEN + \
			 IXGBE_QUEUE_STATS_LEN)

static const char ixgbe_gstrings_test[][ETH_GSTRING_LEN] = {
	"Register test  (offline)", "Eeprom test    (offline)",
	"Interrupt test (offline)", "Loopback test  (offline)",
	"Link test   (on/offline)"
};
#define IXGBE_TEST_LEN sizeof(ixgbe_gstrings_test) / ETH_GSTRING_LEN

static int ixgbe_get_settings(struct net_device *netdev,
			      struct ethtool_cmd *ecmd)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	ixgbe_link_speed supported_link;
	u32 link_speed = 0;
	bool autoneg = false;
	bool link_up;

	hw->mac.ops.get_link_capabilities(hw, &supported_link, &autoneg);

	/* set the supported link speeds */
	if (supported_link & IXGBE_LINK_SPEED_10GB_FULL)
		ecmd->supported |= SUPPORTED_10000baseT_Full;
	if (supported_link & IXGBE_LINK_SPEED_1GB_FULL)
		ecmd->supported |= SUPPORTED_1000baseT_Full;
	if (supported_link & IXGBE_LINK_SPEED_100_FULL)
		ecmd->supported |= SUPPORTED_100baseT_Full;

	/* set the advertised speeds */
	if (hw->phy.autoneg_advertised) {
		if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_100_FULL)
			ecmd->advertising |= ADVERTISED_100baseT_Full;
		if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_10GB_FULL)
			ecmd->advertising |= ADVERTISED_10000baseT_Full;
		if (hw->phy.autoneg_advertised & IXGBE_LINK_SPEED_1GB_FULL)
			ecmd->advertising |= ADVERTISED_1000baseT_Full;
	} else {
		/* default modes in case phy.autoneg_advertised isn't set */
		if (supported_link & IXGBE_LINK_SPEED_10GB_FULL)
			ecmd->advertising |= ADVERTISED_10000baseT_Full;
		if (supported_link & IXGBE_LINK_SPEED_1GB_FULL)
			ecmd->advertising |= ADVERTISED_1000baseT_Full;
		if (supported_link & IXGBE_LINK_SPEED_100_FULL)
			ecmd->advertising |= ADVERTISED_100baseT_Full;

		if (hw->phy.multispeed_fiber && !autoneg) {
			if (supported_link & IXGBE_LINK_SPEED_10GB_FULL)
				ecmd->advertising = ADVERTISED_10000baseT_Full;
		}
	}

	if (autoneg) {
		ecmd->supported |= SUPPORTED_Autoneg;
		ecmd->advertising |= ADVERTISED_Autoneg;
		ecmd->autoneg = AUTONEG_ENABLE;
	} else
		ecmd->autoneg = AUTONEG_DISABLE;

	ecmd->transceiver = XCVR_EXTERNAL;

	/* Determine the remaining settings based on the PHY type. */
	switch (adapter->hw.phy.type) {
	case ixgbe_phy_tn:
	case ixgbe_phy_aq:
	case ixgbe_phy_cu_unknown:
		ecmd->supported |= SUPPORTED_TP;
		ecmd->advertising |= ADVERTISED_TP;
		ecmd->port = PORT_TP;
		break;
	case ixgbe_phy_qt:
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_FIBRE;
		break;
	case ixgbe_phy_nl:
	case ixgbe_phy_sfp_passive_tyco:
	case ixgbe_phy_sfp_passive_unknown:
	case ixgbe_phy_sfp_ftl:
	case ixgbe_phy_sfp_avago:
	case ixgbe_phy_sfp_intel:
	case ixgbe_phy_sfp_unknown:
		/* SFP+ devices, further checking needed */
		switch (adapter->hw.phy.sfp_type) {
		case ixgbe_sfp_type_da_cu:
		case ixgbe_sfp_type_da_cu_core0:
		case ixgbe_sfp_type_da_cu_core1:
			ecmd->supported |= SUPPORTED_FIBRE;
			ecmd->advertising |= ADVERTISED_FIBRE;
			ecmd->port = PORT_DA;
			break;
		case ixgbe_sfp_type_sr:
		case ixgbe_sfp_type_lr:
		case ixgbe_sfp_type_srlr_core0:
		case ixgbe_sfp_type_srlr_core1:
		case ixgbe_sfp_type_1g_sx_core0:
		case ixgbe_sfp_type_1g_sx_core1:
		case ixgbe_sfp_type_1g_lx_core0:
		case ixgbe_sfp_type_1g_lx_core1:
			ecmd->supported |= SUPPORTED_FIBRE;
			ecmd->advertising |= ADVERTISED_FIBRE;
			ecmd->port = PORT_FIBRE;
			break;
		case ixgbe_sfp_type_not_present:
			ecmd->supported |= SUPPORTED_FIBRE;
			ecmd->advertising |= ADVERTISED_FIBRE;
			ecmd->port = PORT_NONE;
			break;
		case ixgbe_sfp_type_1g_cu_core0:
		case ixgbe_sfp_type_1g_cu_core1:
			ecmd->supported |= SUPPORTED_TP;
			ecmd->advertising |= ADVERTISED_TP;
			ecmd->port = PORT_TP;
			break;
		case ixgbe_sfp_type_unknown:
		default:
			ecmd->supported |= SUPPORTED_FIBRE;
			ecmd->advertising |= ADVERTISED_FIBRE;
			ecmd->port = PORT_OTHER;
			break;
		}
		break;
	case ixgbe_phy_xaui:
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_NONE;
		break;
	case ixgbe_phy_unknown:
	case ixgbe_phy_generic:
	case ixgbe_phy_sfp_unsupported:
	default:
		ecmd->supported |= SUPPORTED_FIBRE;
		ecmd->advertising |= ADVERTISED_FIBRE;
		ecmd->port = PORT_OTHER;
		break;
	}

	hw->mac.ops.check_link(hw, &link_speed, &link_up, false);
	if (link_up) {
		switch (link_speed) {
		case IXGBE_LINK_SPEED_10GB_FULL:
			ethtool_cmd_speed_set(ecmd, SPEED_10000);
			break;
		case IXGBE_LINK_SPEED_1GB_FULL:
			ethtool_cmd_speed_set(ecmd, SPEED_1000);
			break;
		case IXGBE_LINK_SPEED_100_FULL:
			ethtool_cmd_speed_set(ecmd, SPEED_100);
			break;
		default:
			break;
		}
		ecmd->duplex = DUPLEX_FULL;
	} else {
		ethtool_cmd_speed_set(ecmd, SPEED_UNKNOWN);
		ecmd->duplex = DUPLEX_UNKNOWN;
	}

	return 0;
}

static int ixgbe_set_settings(struct net_device *netdev,
			      struct ethtool_cmd *ecmd)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 advertised, old;
	s32 err = 0;

	if ((hw->phy.media_type == ixgbe_media_type_copper) ||
	    (hw->phy.multispeed_fiber)) {
		/*
		 * this function does not support duplex forcing, but can
		 * limit the advertising of the adapter to the specified speed
		 */
		if (ecmd->advertising & ~ecmd->supported)
			return -EINVAL;

		/* only allow one speed at a time if no autoneg */
		if (!ecmd->autoneg && hw->phy.multispeed_fiber) {
			if (ecmd->advertising ==
			    (ADVERTISED_10000baseT_Full |
			     ADVERTISED_1000baseT_Full))
				return -EINVAL;
		}

		old = hw->phy.autoneg_advertised;
		advertised = 0;
		if (ecmd->advertising & ADVERTISED_10000baseT_Full)
			advertised |= IXGBE_LINK_SPEED_10GB_FULL;

		if (ecmd->advertising & ADVERTISED_1000baseT_Full)
			advertised |= IXGBE_LINK_SPEED_1GB_FULL;

		if (ecmd->advertising & ADVERTISED_100baseT_Full)
			advertised |= IXGBE_LINK_SPEED_100_FULL;

		if (old == advertised)
			return err;
		/* this sets the link speed and restarts auto-neg */
		hw->mac.autotry_restart = true;
		err = hw->mac.ops.setup_link(hw, advertised, true);
		if (err) {
			e_info(probe, "setup link failed with code %d\n", err);
			hw->mac.ops.setup_link(hw, old, true);
		}
	} else {
		/* in this case we currently only support 10Gb/FULL */
		u32 speed = ethtool_cmd_speed(ecmd);
		if ((ecmd->autoneg == AUTONEG_ENABLE) ||
		    (ecmd->advertising != ADVERTISED_10000baseT_Full) ||
		    (speed + ecmd->duplex != SPEED_10000 + DUPLEX_FULL))
			return -EINVAL;
	}

	return err;
}

static void ixgbe_get_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pause)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;

	if (ixgbe_device_supports_autoneg_fc(hw) &&
	    !hw->fc.disable_fc_autoneg)
		pause->autoneg = 1;
	else
		pause->autoneg = 0;

	if (hw->fc.current_mode == ixgbe_fc_rx_pause) {
		pause->rx_pause = 1;
	} else if (hw->fc.current_mode == ixgbe_fc_tx_pause) {
		pause->tx_pause = 1;
	} else if (hw->fc.current_mode == ixgbe_fc_full) {
		pause->rx_pause = 1;
		pause->tx_pause = 1;
	}
}

static int ixgbe_set_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_fc_info fc = hw->fc;

	/* 82598 does no support link flow control with DCB enabled */
	if ((hw->mac.type == ixgbe_mac_82598EB) &&
	    (adapter->flags & IXGBE_FLAG_DCB_ENABLED))
		return -EINVAL;

	/* some devices do not support autoneg of link flow control */
	if ((pause->autoneg == AUTONEG_ENABLE) &&
	    !ixgbe_device_supports_autoneg_fc(hw))
		return -EINVAL;

	fc.disable_fc_autoneg = (pause->autoneg != AUTONEG_ENABLE);

	if ((pause->rx_pause && pause->tx_pause) || pause->autoneg)
		fc.requested_mode = ixgbe_fc_full;
	else if (pause->rx_pause && !pause->tx_pause)
		fc.requested_mode = ixgbe_fc_rx_pause;
	else if (!pause->rx_pause && pause->tx_pause)
		fc.requested_mode = ixgbe_fc_tx_pause;
	else
		fc.requested_mode = ixgbe_fc_none;

	/* if the thing changed then we'll update and use new autoneg */
	if (memcmp(&fc, &hw->fc, sizeof(struct ixgbe_fc_info))) {
		hw->fc = fc;
		if (netif_running(netdev))
			ixgbe_reinit_locked(adapter);
		else
			ixgbe_reset(adapter);
	}

	return 0;
}

static u32 ixgbe_get_msglevel(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	return adapter->msg_enable;
}

static void ixgbe_set_msglevel(struct net_device *netdev, u32 data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	adapter->msg_enable = data;
}

static int ixgbe_get_regs_len(struct net_device *netdev)
{
#define IXGBE_REGS_LEN  1139
	return IXGBE_REGS_LEN * sizeof(u32);
}

#define IXGBE_GET_STAT(_A_, _R_) _A_->stats._R_

static void ixgbe_get_regs(struct net_device *netdev,
			   struct ethtool_regs *regs, void *p)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 *regs_buff = p;
	u8 i;

	memset(p, 0, IXGBE_REGS_LEN * sizeof(u32));

	regs->version = hw->mac.type << 24 | hw->revision_id << 16 |
			hw->device_id;

	/* General Registers */
	regs_buff[0] = IXGBE_READ_REG(hw, IXGBE_CTRL);
	regs_buff[1] = IXGBE_READ_REG(hw, IXGBE_STATUS);
	regs_buff[2] = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	regs_buff[3] = IXGBE_READ_REG(hw, IXGBE_ESDP);
	regs_buff[4] = IXGBE_READ_REG(hw, IXGBE_EODSDP);
	regs_buff[5] = IXGBE_READ_REG(hw, IXGBE_LEDCTL);
	regs_buff[6] = IXGBE_READ_REG(hw, IXGBE_FRTIMER);
	regs_buff[7] = IXGBE_READ_REG(hw, IXGBE_TCPTIMER);

	/* NVM Register */
	regs_buff[8] = IXGBE_READ_REG(hw, IXGBE_EEC);
	regs_buff[9] = IXGBE_READ_REG(hw, IXGBE_EERD);
	regs_buff[10] = IXGBE_READ_REG(hw, IXGBE_FLA);
	regs_buff[11] = IXGBE_READ_REG(hw, IXGBE_EEMNGCTL);
	regs_buff[12] = IXGBE_READ_REG(hw, IXGBE_EEMNGDATA);
	regs_buff[13] = IXGBE_READ_REG(hw, IXGBE_FLMNGCTL);
	regs_buff[14] = IXGBE_READ_REG(hw, IXGBE_FLMNGDATA);
	regs_buff[15] = IXGBE_READ_REG(hw, IXGBE_FLMNGCNT);
	regs_buff[16] = IXGBE_READ_REG(hw, IXGBE_FLOP);
	regs_buff[17] = IXGBE_READ_REG(hw, IXGBE_GRC);

	/* Interrupt */
	/* don't read EICR because it can clear interrupt causes, instead
	 * read EICS which is a shadow but doesn't clear EICR */
	regs_buff[18] = IXGBE_READ_REG(hw, IXGBE_EICS);
	regs_buff[19] = IXGBE_READ_REG(hw, IXGBE_EICS);
	regs_buff[20] = IXGBE_READ_REG(hw, IXGBE_EIMS);
	regs_buff[21] = IXGBE_READ_REG(hw, IXGBE_EIMC);
	regs_buff[22] = IXGBE_READ_REG(hw, IXGBE_EIAC);
	regs_buff[23] = IXGBE_READ_REG(hw, IXGBE_EIAM);
	regs_buff[24] = IXGBE_READ_REG(hw, IXGBE_EITR(0));
	regs_buff[25] = IXGBE_READ_REG(hw, IXGBE_IVAR(0));
	regs_buff[26] = IXGBE_READ_REG(hw, IXGBE_MSIXT);
	regs_buff[27] = IXGBE_READ_REG(hw, IXGBE_MSIXPBA);
	regs_buff[28] = IXGBE_READ_REG(hw, IXGBE_PBACL(0));
	regs_buff[29] = IXGBE_READ_REG(hw, IXGBE_GPIE);

	/* Flow Control */
	regs_buff[30] = IXGBE_READ_REG(hw, IXGBE_PFCTOP);
	regs_buff[31] = IXGBE_READ_REG(hw, IXGBE_FCTTV(0));
	regs_buff[32] = IXGBE_READ_REG(hw, IXGBE_FCTTV(1));
	regs_buff[33] = IXGBE_READ_REG(hw, IXGBE_FCTTV(2));
	regs_buff[34] = IXGBE_READ_REG(hw, IXGBE_FCTTV(3));
	for (i = 0; i < 8; i++) {
		switch (hw->mac.type) {
		case ixgbe_mac_82598EB:
			regs_buff[35 + i] = IXGBE_READ_REG(hw, IXGBE_FCRTL(i));
			regs_buff[43 + i] = IXGBE_READ_REG(hw, IXGBE_FCRTH(i));
			break;
		case ixgbe_mac_82599EB:
		case ixgbe_mac_X540:
			regs_buff[35 + i] = IXGBE_READ_REG(hw, IXGBE_FCRTL_82599(i));
			regs_buff[43 + i] = IXGBE_READ_REG(hw, IXGBE_FCRTH_82599(i));
			break;
		default:
			break;
		}
	}
	regs_buff[51] = IXGBE_READ_REG(hw, IXGBE_FCRTV);
	regs_buff[52] = IXGBE_READ_REG(hw, IXGBE_TFCS);

	/* Receive DMA */
	for (i = 0; i < 64; i++)
		regs_buff[53 + i] = IXGBE_READ_REG(hw, IXGBE_RDBAL(i));
	for (i = 0; i < 64; i++)
		regs_buff[117 + i] = IXGBE_READ_REG(hw, IXGBE_RDBAH(i));
	for (i = 0; i < 64; i++)
		regs_buff[181 + i] = IXGBE_READ_REG(hw, IXGBE_RDLEN(i));
	for (i = 0; i < 64; i++)
		regs_buff[245 + i] = IXGBE_READ_REG(hw, IXGBE_RDH(i));
	for (i = 0; i < 64; i++)
		regs_buff[309 + i] = IXGBE_READ_REG(hw, IXGBE_RDT(i));
	for (i = 0; i < 64; i++)
		regs_buff[373 + i] = IXGBE_READ_REG(hw, IXGBE_RXDCTL(i));
	for (i = 0; i < 16; i++)
		regs_buff[437 + i] = IXGBE_READ_REG(hw, IXGBE_SRRCTL(i));
	for (i = 0; i < 16; i++)
		regs_buff[453 + i] = IXGBE_READ_REG(hw, IXGBE_DCA_RXCTRL(i));
	regs_buff[469] = IXGBE_READ_REG(hw, IXGBE_RDRXCTL);
	for (i = 0; i < 8; i++)
		regs_buff[470 + i] = IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(i));
	regs_buff[478] = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	regs_buff[479] = IXGBE_READ_REG(hw, IXGBE_DROPEN);

	/* Receive */
	regs_buff[480] = IXGBE_READ_REG(hw, IXGBE_RXCSUM);
	regs_buff[481] = IXGBE_READ_REG(hw, IXGBE_RFCTL);
	for (i = 0; i < 16; i++)
		regs_buff[482 + i] = IXGBE_READ_REG(hw, IXGBE_RAL(i));
	for (i = 0; i < 16; i++)
		regs_buff[498 + i] = IXGBE_READ_REG(hw, IXGBE_RAH(i));
	regs_buff[514] = IXGBE_READ_REG(hw, IXGBE_PSRTYPE(0));
	regs_buff[515] = IXGBE_READ_REG(hw, IXGBE_FCTRL);
	regs_buff[516] = IXGBE_READ_REG(hw, IXGBE_VLNCTRL);
	regs_buff[517] = IXGBE_READ_REG(hw, IXGBE_MCSTCTRL);
	regs_buff[518] = IXGBE_READ_REG(hw, IXGBE_MRQC);
	regs_buff[519] = IXGBE_READ_REG(hw, IXGBE_VMD_CTL);
	for (i = 0; i < 8; i++)
		regs_buff[520 + i] = IXGBE_READ_REG(hw, IXGBE_IMIR(i));
	for (i = 0; i < 8; i++)
		regs_buff[528 + i] = IXGBE_READ_REG(hw, IXGBE_IMIREXT(i));
	regs_buff[536] = IXGBE_READ_REG(hw, IXGBE_IMIRVP);

	/* Transmit */
	for (i = 0; i < 32; i++)
		regs_buff[537 + i] = IXGBE_READ_REG(hw, IXGBE_TDBAL(i));
	for (i = 0; i < 32; i++)
		regs_buff[569 + i] = IXGBE_READ_REG(hw, IXGBE_TDBAH(i));
	for (i = 0; i < 32; i++)
		regs_buff[601 + i] = IXGBE_READ_REG(hw, IXGBE_TDLEN(i));
	for (i = 0; i < 32; i++)
		regs_buff[633 + i] = IXGBE_READ_REG(hw, IXGBE_TDH(i));
	for (i = 0; i < 32; i++)
		regs_buff[665 + i] = IXGBE_READ_REG(hw, IXGBE_TDT(i));
	for (i = 0; i < 32; i++)
		regs_buff[697 + i] = IXGBE_READ_REG(hw, IXGBE_TXDCTL(i));
	for (i = 0; i < 32; i++)
		regs_buff[729 + i] = IXGBE_READ_REG(hw, IXGBE_TDWBAL(i));
	for (i = 0; i < 32; i++)
		regs_buff[761 + i] = IXGBE_READ_REG(hw, IXGBE_TDWBAH(i));
	regs_buff[793] = IXGBE_READ_REG(hw, IXGBE_DTXCTL);
	for (i = 0; i < 16; i++)
		regs_buff[794 + i] = IXGBE_READ_REG(hw, IXGBE_DCA_TXCTRL(i));
	regs_buff[810] = IXGBE_READ_REG(hw, IXGBE_TIPG);
	for (i = 0; i < 8; i++)
		regs_buff[811 + i] = IXGBE_READ_REG(hw, IXGBE_TXPBSIZE(i));
	regs_buff[819] = IXGBE_READ_REG(hw, IXGBE_MNGTXMAP);

	/* Wake Up */
	regs_buff[820] = IXGBE_READ_REG(hw, IXGBE_WUC);
	regs_buff[821] = IXGBE_READ_REG(hw, IXGBE_WUFC);
	regs_buff[822] = IXGBE_READ_REG(hw, IXGBE_WUS);
	regs_buff[823] = IXGBE_READ_REG(hw, IXGBE_IPAV);
	regs_buff[824] = IXGBE_READ_REG(hw, IXGBE_IP4AT);
	regs_buff[825] = IXGBE_READ_REG(hw, IXGBE_IP6AT);
	regs_buff[826] = IXGBE_READ_REG(hw, IXGBE_WUPL);
	regs_buff[827] = IXGBE_READ_REG(hw, IXGBE_WUPM);
	regs_buff[828] = IXGBE_READ_REG(hw, IXGBE_FHFT(0));

	/* DCB */
	regs_buff[829] = IXGBE_READ_REG(hw, IXGBE_RMCS);   /* same as FCCFG  */
	regs_buff[831] = IXGBE_READ_REG(hw, IXGBE_PDPMCS); /* same as RTTPCS */

	switch (hw->mac.type) {
	case ixgbe_mac_82598EB:
		regs_buff[830] = IXGBE_READ_REG(hw, IXGBE_DPMCS);
		regs_buff[832] = IXGBE_READ_REG(hw, IXGBE_RUPPBMR);
		for (i = 0; i < 8; i++)
			regs_buff[833 + i] =
				IXGBE_READ_REG(hw, IXGBE_RT2CR(i));
		for (i = 0; i < 8; i++)
			regs_buff[841 + i] =
				IXGBE_READ_REG(hw, IXGBE_RT2SR(i));
		for (i = 0; i < 8; i++)
			regs_buff[849 + i] =
				IXGBE_READ_REG(hw, IXGBE_TDTQ2TCCR(i));
		for (i = 0; i < 8; i++)
			regs_buff[857 + i] =
				IXGBE_READ_REG(hw, IXGBE_TDTQ2TCSR(i));
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		regs_buff[830] = IXGBE_READ_REG(hw, IXGBE_RTTDCS);
		regs_buff[832] = IXGBE_READ_REG(hw, IXGBE_RTRPCS);
		for (i = 0; i < 8; i++)
			regs_buff[833 + i] =
				IXGBE_READ_REG(hw, IXGBE_RTRPT4C(i));
		for (i = 0; i < 8; i++)
			regs_buff[841 + i] =
				IXGBE_READ_REG(hw, IXGBE_RTRPT4S(i));
		for (i = 0; i < 8; i++)
			regs_buff[849 + i] =
				IXGBE_READ_REG(hw, IXGBE_RTTDT2C(i));
		for (i = 0; i < 8; i++)
			regs_buff[857 + i] =
				IXGBE_READ_REG(hw, IXGBE_RTTDT2S(i));
		break;
	default:
		break;
	}

	for (i = 0; i < 8; i++)
		regs_buff[865 + i] =
		IXGBE_READ_REG(hw, IXGBE_TDPT2TCCR(i)); /* same as RTTPT2C */
	for (i = 0; i < 8; i++)
		regs_buff[873 + i] =
		IXGBE_READ_REG(hw, IXGBE_TDPT2TCSR(i)); /* same as RTTPT2S */

	/* Statistics */
	regs_buff[881] = IXGBE_GET_STAT(adapter, crcerrs);
	regs_buff[882] = IXGBE_GET_STAT(adapter, illerrc);
	regs_buff[883] = IXGBE_GET_STAT(adapter, errbc);
	regs_buff[884] = IXGBE_GET_STAT(adapter, mspdc);
	for (i = 0; i < 8; i++)
		regs_buff[885 + i] = IXGBE_GET_STAT(adapter, mpc[i]);
	regs_buff[893] = IXGBE_GET_STAT(adapter, mlfc);
	regs_buff[894] = IXGBE_GET_STAT(adapter, mrfc);
	regs_buff[895] = IXGBE_GET_STAT(adapter, rlec);
	regs_buff[896] = IXGBE_GET_STAT(adapter, lxontxc);
	regs_buff[897] = IXGBE_GET_STAT(adapter, lxonrxc);
	regs_buff[898] = IXGBE_GET_STAT(adapter, lxofftxc);
	regs_buff[899] = IXGBE_GET_STAT(adapter, lxoffrxc);
	for (i = 0; i < 8; i++)
		regs_buff[900 + i] = IXGBE_GET_STAT(adapter, pxontxc[i]);
	for (i = 0; i < 8; i++)
		regs_buff[908 + i] = IXGBE_GET_STAT(adapter, pxonrxc[i]);
	for (i = 0; i < 8; i++)
		regs_buff[916 + i] = IXGBE_GET_STAT(adapter, pxofftxc[i]);
	for (i = 0; i < 8; i++)
		regs_buff[924 + i] = IXGBE_GET_STAT(adapter, pxoffrxc[i]);
	regs_buff[932] = IXGBE_GET_STAT(adapter, prc64);
	regs_buff[933] = IXGBE_GET_STAT(adapter, prc127);
	regs_buff[934] = IXGBE_GET_STAT(adapter, prc255);
	regs_buff[935] = IXGBE_GET_STAT(adapter, prc511);
	regs_buff[936] = IXGBE_GET_STAT(adapter, prc1023);
	regs_buff[937] = IXGBE_GET_STAT(adapter, prc1522);
	regs_buff[938] = IXGBE_GET_STAT(adapter, gprc);
	regs_buff[939] = IXGBE_GET_STAT(adapter, bprc);
	regs_buff[940] = IXGBE_GET_STAT(adapter, mprc);
	regs_buff[941] = IXGBE_GET_STAT(adapter, gptc);
	regs_buff[942] = IXGBE_GET_STAT(adapter, gorc);
	regs_buff[944] = IXGBE_GET_STAT(adapter, gotc);
	for (i = 0; i < 8; i++)
		regs_buff[946 + i] = IXGBE_GET_STAT(adapter, rnbc[i]);
	regs_buff[954] = IXGBE_GET_STAT(adapter, ruc);
	regs_buff[955] = IXGBE_GET_STAT(adapter, rfc);
	regs_buff[956] = IXGBE_GET_STAT(adapter, roc);
	regs_buff[957] = IXGBE_GET_STAT(adapter, rjc);
	regs_buff[958] = IXGBE_GET_STAT(adapter, mngprc);
	regs_buff[959] = IXGBE_GET_STAT(adapter, mngpdc);
	regs_buff[960] = IXGBE_GET_STAT(adapter, mngptc);
	regs_buff[961] = IXGBE_GET_STAT(adapter, tor);
	regs_buff[963] = IXGBE_GET_STAT(adapter, tpr);
	regs_buff[964] = IXGBE_GET_STAT(adapter, tpt);
	regs_buff[965] = IXGBE_GET_STAT(adapter, ptc64);
	regs_buff[966] = IXGBE_GET_STAT(adapter, ptc127);
	regs_buff[967] = IXGBE_GET_STAT(adapter, ptc255);
	regs_buff[968] = IXGBE_GET_STAT(adapter, ptc511);
	regs_buff[969] = IXGBE_GET_STAT(adapter, ptc1023);
	regs_buff[970] = IXGBE_GET_STAT(adapter, ptc1522);
	regs_buff[971] = IXGBE_GET_STAT(adapter, mptc);
	regs_buff[972] = IXGBE_GET_STAT(adapter, bptc);
	regs_buff[973] = IXGBE_GET_STAT(adapter, xec);
	for (i = 0; i < 16; i++)
		regs_buff[974 + i] = IXGBE_GET_STAT(adapter, qprc[i]);
	for (i = 0; i < 16; i++)
		regs_buff[990 + i] = IXGBE_GET_STAT(adapter, qptc[i]);
	for (i = 0; i < 16; i++)
		regs_buff[1006 + i] = IXGBE_GET_STAT(adapter, qbrc[i]);
	for (i = 0; i < 16; i++)
		regs_buff[1022 + i] = IXGBE_GET_STAT(adapter, qbtc[i]);

	/* MAC */
	regs_buff[1038] = IXGBE_READ_REG(hw, IXGBE_PCS1GCFIG);
	regs_buff[1039] = IXGBE_READ_REG(hw, IXGBE_PCS1GLCTL);
	regs_buff[1040] = IXGBE_READ_REG(hw, IXGBE_PCS1GLSTA);
	regs_buff[1041] = IXGBE_READ_REG(hw, IXGBE_PCS1GDBG0);
	regs_buff[1042] = IXGBE_READ_REG(hw, IXGBE_PCS1GDBG1);
	regs_buff[1043] = IXGBE_READ_REG(hw, IXGBE_PCS1GANA);
	regs_buff[1044] = IXGBE_READ_REG(hw, IXGBE_PCS1GANLP);
	regs_buff[1045] = IXGBE_READ_REG(hw, IXGBE_PCS1GANNP);
	regs_buff[1046] = IXGBE_READ_REG(hw, IXGBE_PCS1GANLPNP);
	regs_buff[1047] = IXGBE_READ_REG(hw, IXGBE_HLREG0);
	regs_buff[1048] = IXGBE_READ_REG(hw, IXGBE_HLREG1);
	regs_buff[1049] = IXGBE_READ_REG(hw, IXGBE_PAP);
	regs_buff[1050] = IXGBE_READ_REG(hw, IXGBE_MACA);
	regs_buff[1051] = IXGBE_READ_REG(hw, IXGBE_APAE);
	regs_buff[1052] = IXGBE_READ_REG(hw, IXGBE_ARD);
	regs_buff[1053] = IXGBE_READ_REG(hw, IXGBE_AIS);
	regs_buff[1054] = IXGBE_READ_REG(hw, IXGBE_MSCA);
	regs_buff[1055] = IXGBE_READ_REG(hw, IXGBE_MSRWD);
	regs_buff[1056] = IXGBE_READ_REG(hw, IXGBE_MLADD);
	regs_buff[1057] = IXGBE_READ_REG(hw, IXGBE_MHADD);
	regs_buff[1058] = IXGBE_READ_REG(hw, IXGBE_TREG);
	regs_buff[1059] = IXGBE_READ_REG(hw, IXGBE_PCSS1);
	regs_buff[1060] = IXGBE_READ_REG(hw, IXGBE_PCSS2);
	regs_buff[1061] = IXGBE_READ_REG(hw, IXGBE_XPCSS);
	regs_buff[1062] = IXGBE_READ_REG(hw, IXGBE_SERDESC);
	regs_buff[1063] = IXGBE_READ_REG(hw, IXGBE_MACS);
	regs_buff[1064] = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	regs_buff[1065] = IXGBE_READ_REG(hw, IXGBE_LINKS);
	regs_buff[1066] = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	regs_buff[1067] = IXGBE_READ_REG(hw, IXGBE_AUTOC3);
	regs_buff[1068] = IXGBE_READ_REG(hw, IXGBE_ANLP1);
	regs_buff[1069] = IXGBE_READ_REG(hw, IXGBE_ANLP2);
	regs_buff[1070] = IXGBE_READ_REG(hw, IXGBE_ATLASCTL);

	/* Diagnostic */
	regs_buff[1071] = IXGBE_READ_REG(hw, IXGBE_RDSTATCTL);
	for (i = 0; i < 8; i++)
		regs_buff[1072 + i] = IXGBE_READ_REG(hw, IXGBE_RDSTAT(i));
	regs_buff[1080] = IXGBE_READ_REG(hw, IXGBE_RDHMPN);
	for (i = 0; i < 4; i++)
		regs_buff[1081 + i] = IXGBE_READ_REG(hw, IXGBE_RIC_DW(i));
	regs_buff[1085] = IXGBE_READ_REG(hw, IXGBE_RDPROBE);
	regs_buff[1086] = IXGBE_READ_REG(hw, IXGBE_TDSTATCTL);
	for (i = 0; i < 8; i++)
		regs_buff[1087 + i] = IXGBE_READ_REG(hw, IXGBE_TDSTAT(i));
	regs_buff[1095] = IXGBE_READ_REG(hw, IXGBE_TDHMPN);
	for (i = 0; i < 4; i++)
		regs_buff[1096 + i] = IXGBE_READ_REG(hw, IXGBE_TIC_DW(i));
	regs_buff[1100] = IXGBE_READ_REG(hw, IXGBE_TDPROBE);
	regs_buff[1101] = IXGBE_READ_REG(hw, IXGBE_TXBUFCTRL);
	regs_buff[1102] = IXGBE_READ_REG(hw, IXGBE_TXBUFDATA0);
	regs_buff[1103] = IXGBE_READ_REG(hw, IXGBE_TXBUFDATA1);
	regs_buff[1104] = IXGBE_READ_REG(hw, IXGBE_TXBUFDATA2);
	regs_buff[1105] = IXGBE_READ_REG(hw, IXGBE_TXBUFDATA3);
	regs_buff[1106] = IXGBE_READ_REG(hw, IXGBE_RXBUFCTRL);
	regs_buff[1107] = IXGBE_READ_REG(hw, IXGBE_RXBUFDATA0);
	regs_buff[1108] = IXGBE_READ_REG(hw, IXGBE_RXBUFDATA1);
	regs_buff[1109] = IXGBE_READ_REG(hw, IXGBE_RXBUFDATA2);
	regs_buff[1110] = IXGBE_READ_REG(hw, IXGBE_RXBUFDATA3);
	for (i = 0; i < 8; i++)
		regs_buff[1111 + i] = IXGBE_READ_REG(hw, IXGBE_PCIE_DIAG(i));
	regs_buff[1119] = IXGBE_READ_REG(hw, IXGBE_RFVAL);
	regs_buff[1120] = IXGBE_READ_REG(hw, IXGBE_MDFTC1);
	regs_buff[1121] = IXGBE_READ_REG(hw, IXGBE_MDFTC2);
	regs_buff[1122] = IXGBE_READ_REG(hw, IXGBE_MDFTFIFO1);
	regs_buff[1123] = IXGBE_READ_REG(hw, IXGBE_MDFTFIFO2);
	regs_buff[1124] = IXGBE_READ_REG(hw, IXGBE_MDFTS);
	regs_buff[1125] = IXGBE_READ_REG(hw, IXGBE_PCIEECCCTL);
	regs_buff[1126] = IXGBE_READ_REG(hw, IXGBE_PBTXECC);
	regs_buff[1127] = IXGBE_READ_REG(hw, IXGBE_PBRXECC);

	/* 82599 X540 specific registers  */
	regs_buff[1128] = IXGBE_READ_REG(hw, IXGBE_MFLCN);

	/* 82599 X540 specific DCB registers  */
	regs_buff[1129] = IXGBE_READ_REG(hw, IXGBE_RTRUP2TC);
	regs_buff[1130] = IXGBE_READ_REG(hw, IXGBE_RTTUP2TC);
	for (i = 0; i < 4; i++)
		regs_buff[1131 + i] = IXGBE_READ_REG(hw, IXGBE_TXLLQ(i));
	regs_buff[1135] = IXGBE_READ_REG(hw, IXGBE_RTTBCNRM);
					/* same as RTTQCNRM */
	regs_buff[1136] = IXGBE_READ_REG(hw, IXGBE_RTTBCNRD);
					/* same as RTTQCNRR */

	/* X540 specific DCB registers  */
	regs_buff[1137] = IXGBE_READ_REG(hw, IXGBE_RTTQCNCR);
	regs_buff[1138] = IXGBE_READ_REG(hw, IXGBE_RTTQCNTG);
}

static int ixgbe_get_eeprom_len(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	return adapter->hw.eeprom.word_size * 2;
}

static int ixgbe_get_eeprom(struct net_device *netdev,
			    struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u16 *eeprom_buff;
	int first_word, last_word, eeprom_len;
	int ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EINVAL;

	eeprom->magic = hw->vendor_id | (hw->device_id << 16);

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;
	eeprom_len = last_word - first_word + 1;

	eeprom_buff = kmalloc(sizeof(u16) * eeprom_len, GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	ret_val = hw->eeprom.ops.read_buffer(hw, first_word, eeprom_len,
					     eeprom_buff);

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < eeprom_len; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(bytes, (u8 *)eeprom_buff + (eeprom->offset & 1), eeprom->len);
	kfree(eeprom_buff);

	return ret_val;
}

static int ixgbe_set_eeprom(struct net_device *netdev,
			    struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;
	u16 *eeprom_buff;
	void *ptr;
	int max_len, first_word, last_word, ret_val = 0;
	u16 i;

	if (eeprom->len == 0)
		return -EINVAL;

	if (eeprom->magic != (hw->vendor_id | (hw->device_id << 16)))
		return -EINVAL;

	max_len = hw->eeprom.word_size * 2;

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;
	eeprom_buff = kmalloc(max_len, GFP_KERNEL);
	if (!eeprom_buff)
		return -ENOMEM;

	ptr = eeprom_buff;

	if (eeprom->offset & 1) {
		/*
		 * need read/modify/write of first changed EEPROM word
		 * only the second byte of the word is being modified
		 */
		ret_val = hw->eeprom.ops.read(hw, first_word, &eeprom_buff[0]);
		if (ret_val)
			goto err;

		ptr++;
	}
	if ((eeprom->offset + eeprom->len) & 1) {
		/*
		 * need read/modify/write of last changed EEPROM word
		 * only the first byte of the word is being modified
		 */
		ret_val = hw->eeprom.ops.read(hw, last_word,
					  &eeprom_buff[last_word - first_word]);
		if (ret_val)
			goto err;
	}

	/* Device's eeprom is always little-endian, word addressable */
	for (i = 0; i < last_word - first_word + 1; i++)
		le16_to_cpus(&eeprom_buff[i]);

	memcpy(ptr, bytes, eeprom->len);

	for (i = 0; i < last_word - first_word + 1; i++)
		cpu_to_le16s(&eeprom_buff[i]);

	ret_val = hw->eeprom.ops.write_buffer(hw, first_word,
					      last_word - first_word + 1,
					      eeprom_buff);

	/* Update the checksum */
	if (ret_val == 0)
		hw->eeprom.ops.update_checksum(hw);

err:
	kfree(eeprom_buff);
	return ret_val;
}

static void ixgbe_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	u32 nvm_track_id;

	strlcpy(drvinfo->driver, ixgbe_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, ixgbe_driver_version,
		sizeof(drvinfo->version));

	nvm_track_id = (adapter->eeprom_verh << 16) |
			adapter->eeprom_verl;
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version), "0x%08x",
		 nvm_track_id);

	strlcpy(drvinfo->bus_info, pci_name(adapter->pdev),
		sizeof(drvinfo->bus_info));
	drvinfo->n_stats = IXGBE_STATS_LEN;
	drvinfo->testinfo_len = IXGBE_TEST_LEN;
	drvinfo->regdump_len = ixgbe_get_regs_len(netdev);
}

static void ixgbe_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_ring *tx_ring = adapter->tx_ring[0];
	struct ixgbe_ring *rx_ring = adapter->rx_ring[0];

	ring->rx_max_pending = IXGBE_MAX_RXD;
	ring->tx_max_pending = IXGBE_MAX_TXD;
	ring->rx_pending = rx_ring->count;
	ring->tx_pending = tx_ring->count;
}

static int ixgbe_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_ring *temp_ring;
	int i, err = 0;
	u32 new_rx_count, new_tx_count;

	if ((ring->rx_mini_pending) || (ring->rx_jumbo_pending))
		return -EINVAL;

	new_tx_count = clamp_t(u32, ring->tx_pending,
			       IXGBE_MIN_TXD, IXGBE_MAX_TXD);
	new_tx_count = ALIGN(new_tx_count, IXGBE_REQ_TX_DESCRIPTOR_MULTIPLE);

	new_rx_count = clamp_t(u32, ring->rx_pending,
			       IXGBE_MIN_RXD, IXGBE_MAX_RXD);
	new_rx_count = ALIGN(new_rx_count, IXGBE_REQ_RX_DESCRIPTOR_MULTIPLE);

	if ((new_tx_count == adapter->tx_ring_count) &&
	    (new_rx_count == adapter->rx_ring_count)) {
		/* nothing to do */
		return 0;
	}

	while (test_and_set_bit(__IXGBE_RESETTING, &adapter->state))
		usleep_range(1000, 2000);

	if (!netif_running(adapter->netdev)) {
		for (i = 0; i < adapter->num_tx_queues; i++)
			adapter->tx_ring[i]->count = new_tx_count;
		for (i = 0; i < adapter->num_rx_queues; i++)
			adapter->rx_ring[i]->count = new_rx_count;
		adapter->tx_ring_count = new_tx_count;
		adapter->rx_ring_count = new_rx_count;
		goto clear_reset;
	}

	/* allocate temporary buffer to store rings in */
	i = max_t(int, adapter->num_tx_queues, adapter->num_rx_queues);
	temp_ring = vmalloc(i * sizeof(struct ixgbe_ring));

	if (!temp_ring) {
		err = -ENOMEM;
		goto clear_reset;
	}

	ixgbe_down(adapter);

	/*
	 * Setup new Tx resources and free the old Tx resources in that order.
	 * We can then assign the new resources to the rings via a memcpy.
	 * The advantage to this approach is that we are guaranteed to still
	 * have resources even in the case of an allocation failure.
	 */
	if (new_tx_count != adapter->tx_ring_count) {
		for (i = 0; i < adapter->num_tx_queues; i++) {
			memcpy(&temp_ring[i], adapter->tx_ring[i],
			       sizeof(struct ixgbe_ring));

			temp_ring[i].count = new_tx_count;
			err = ixgbe_setup_tx_resources(&temp_ring[i]);
			if (err) {
				while (i) {
					i--;
					ixgbe_free_tx_resources(&temp_ring[i]);
				}
				goto err_setup;
			}
		}

		for (i = 0; i < adapter->num_tx_queues; i++) {
			ixgbe_free_tx_resources(adapter->tx_ring[i]);

			memcpy(adapter->tx_ring[i], &temp_ring[i],
			       sizeof(struct ixgbe_ring));
		}

		adapter->tx_ring_count = new_tx_count;
	}

	/* Repeat the process for the Rx rings if needed */
	if (new_rx_count != adapter->rx_ring_count) {
		for (i = 0; i < adapter->num_rx_queues; i++) {
			memcpy(&temp_ring[i], adapter->rx_ring[i],
			       sizeof(struct ixgbe_ring));

			temp_ring[i].count = new_rx_count;
			err = ixgbe_setup_rx_resources(&temp_ring[i]);
			if (err) {
				while (i) {
					i--;
					ixgbe_free_rx_resources(&temp_ring[i]);
				}
				goto err_setup;
			}

		}

		for (i = 0; i < adapter->num_rx_queues; i++) {
			ixgbe_free_rx_resources(adapter->rx_ring[i]);

			memcpy(adapter->rx_ring[i], &temp_ring[i],
			       sizeof(struct ixgbe_ring));
		}

		adapter->rx_ring_count = new_rx_count;
	}

err_setup:
	ixgbe_up(adapter);
	vfree(temp_ring);
clear_reset:
	clear_bit(__IXGBE_RESETTING, &adapter->state);
	return err;
}

static int ixgbe_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return IXGBE_TEST_LEN;
	case ETH_SS_STATS:
		return IXGBE_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void ixgbe_get_ethtool_stats(struct net_device *netdev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct rtnl_link_stats64 temp;
	const struct rtnl_link_stats64 *net_stats;
	unsigned int start;
	struct ixgbe_ring *ring;
	int i, j;
	char *p = NULL;

	ixgbe_update_stats(adapter);
	net_stats = dev_get_stats(netdev, &temp);
	for (i = 0; i < IXGBE_GLOBAL_STATS_LEN; i++) {
		switch (ixgbe_gstrings_stats[i].type) {
		case NETDEV_STATS:
			p = (char *) net_stats +
					ixgbe_gstrings_stats[i].stat_offset;
			break;
		case IXGBE_STATS:
			p = (char *) adapter +
					ixgbe_gstrings_stats[i].stat_offset;
			break;
		default:
			data[i] = 0;
			continue;
		}

		data[i] = (ixgbe_gstrings_stats[i].sizeof_stat ==
			   sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}
	for (j = 0; j < netdev->num_tx_queues; j++) {
		ring = adapter->tx_ring[j];
		if (!ring) {
			data[i] = 0;
			data[i+1] = 0;
			i += 2;
#ifdef BP_EXTENDED_STATS
			data[i] = 0;
			data[i+1] = 0;
			data[i+2] = 0;
			i += 3;
#endif
			continue;
		}

		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);
			data[i]   = ring->stats.packets;
			data[i+1] = ring->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));
		i += 2;
#ifdef BP_EXTENDED_STATS
		data[i] = ring->stats.yields;
		data[i+1] = ring->stats.misses;
		data[i+2] = ring->stats.cleaned;
		i += 3;
#endif
	}
	for (j = 0; j < IXGBE_NUM_RX_QUEUES; j++) {
		ring = adapter->rx_ring[j];
		if (!ring) {
			data[i] = 0;
			data[i+1] = 0;
			i += 2;
#ifdef BP_EXTENDED_STATS
			data[i] = 0;
			data[i+1] = 0;
			data[i+2] = 0;
			i += 3;
#endif
			continue;
		}

		do {
			start = u64_stats_fetch_begin_irq(&ring->syncp);
			data[i]   = ring->stats.packets;
			data[i+1] = ring->stats.bytes;
		} while (u64_stats_fetch_retry_irq(&ring->syncp, start));
		i += 2;
#ifdef BP_EXTENDED_STATS
		data[i] = ring->stats.yields;
		data[i+1] = ring->stats.misses;
		data[i+2] = ring->stats.cleaned;
		i += 3;
#endif
	}

	for (j = 0; j < IXGBE_MAX_PACKET_BUFFERS; j++) {
		data[i++] = adapter->stats.pxontxc[j];
		data[i++] = adapter->stats.pxofftxc[j];
	}
	for (j = 0; j < IXGBE_MAX_PACKET_BUFFERS; j++) {
		data[i++] = adapter->stats.pxonrxc[j];
		data[i++] = adapter->stats.pxoffrxc[j];
	}
}

static void ixgbe_get_strings(struct net_device *netdev, u32 stringset,
			      u8 *data)
{
	char *p = (char *)data;
	int i;

	switch (stringset) {
	case ETH_SS_TEST:
		for (i = 0; i < IXGBE_TEST_LEN; i++) {
			memcpy(data, ixgbe_gstrings_test[i], ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	case ETH_SS_STATS:
		for (i = 0; i < IXGBE_GLOBAL_STATS_LEN; i++) {
			memcpy(p, ixgbe_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < netdev->num_tx_queues; i++) {
			sprintf(p, "tx_queue_%u_packets", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "tx_queue_%u_bytes", i);
			p += ETH_GSTRING_LEN;
#ifdef BP_EXTENDED_STATS
			sprintf(p, "tx_queue_%u_bp_napi_yield", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "tx_queue_%u_bp_misses", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "tx_queue_%u_bp_cleaned", i);
			p += ETH_GSTRING_LEN;
#endif /* BP_EXTENDED_STATS */
		}
		for (i = 0; i < IXGBE_NUM_RX_QUEUES; i++) {
			sprintf(p, "rx_queue_%u_packets", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_bytes", i);
			p += ETH_GSTRING_LEN;
#ifdef BP_EXTENDED_STATS
			sprintf(p, "rx_queue_%u_bp_poll_yield", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_bp_misses", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_queue_%u_bp_cleaned", i);
			p += ETH_GSTRING_LEN;
#endif /* BP_EXTENDED_STATS */
		}
		for (i = 0; i < IXGBE_MAX_PACKET_BUFFERS; i++) {
			sprintf(p, "tx_pb_%u_pxon", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "tx_pb_%u_pxoff", i);
			p += ETH_GSTRING_LEN;
		}
		for (i = 0; i < IXGBE_MAX_PACKET_BUFFERS; i++) {
			sprintf(p, "rx_pb_%u_pxon", i);
			p += ETH_GSTRING_LEN;
			sprintf(p, "rx_pb_%u_pxoff", i);
			p += ETH_GSTRING_LEN;
		}
		/* BUG_ON(p - data != IXGBE_STATS_LEN * ETH_GSTRING_LEN); */
		break;
	}
}

static int ixgbe_link_test(struct ixgbe_adapter *adapter, u64 *data)
{
	struct ixgbe_hw *hw = &adapter->hw;
	bool link_up;
	u32 link_speed = 0;

	if (ixgbe_removed(hw->hw_addr)) {
		*data = 1;
		return 1;
	}
	*data = 0;

	hw->mac.ops.check_link(hw, &link_speed, &link_up, true);
	if (link_up)
		return *data;
	else
		*data = 1;
	return *data;
}

/* ethtool register test data */
struct ixgbe_reg_test {
	u16 reg;
	u8  array_len;
	u8  test_type;
	u32 mask;
	u32 write;
};

/* In the hardware, registers are laid out either singly, in arrays
 * spaced 0x40 bytes apart, or in contiguous tables.  We assume
 * most tests take place on arrays or single registers (handled
 * as a single-element array) and special-case the tables.
 * Table tests are always pattern tests.
 *
 * We also make provision for some required setup steps by specifying
 * registers to be written without any read-back testing.
 */

#define PATTERN_TEST	1
#define SET_READ_TEST	2
#define WRITE_NO_TEST	3
#define TABLE32_TEST	4
#define TABLE64_TEST_LO	5
#define TABLE64_TEST_HI	6

/* default 82599 register test */
static const struct ixgbe_reg_test reg_test_82599[] = {
	{ IXGBE_FCRTL_82599(0), 1, PATTERN_TEST, 0x8007FFF0, 0x8007FFF0 },
	{ IXGBE_FCRTH_82599(0), 1, PATTERN_TEST, 0x8007FFF0, 0x8007FFF0 },
	{ IXGBE_PFCTOP, 1, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_VLNCTRL, 1, PATTERN_TEST, 0x00000000, 0x00000000 },
	{ IXGBE_RDBAL(0), 4, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFF80 },
	{ IXGBE_RDBAH(0), 4, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_RDLEN(0), 4, PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	{ IXGBE_RXDCTL(0), 4, WRITE_NO_TEST, 0, IXGBE_RXDCTL_ENABLE },
	{ IXGBE_RDT(0), 4, PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ IXGBE_RXDCTL(0), 4, WRITE_NO_TEST, 0, 0 },
	{ IXGBE_FCRTH(0), 1, PATTERN_TEST, 0x8007FFF0, 0x8007FFF0 },
	{ IXGBE_FCTTV(0), 1, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_TDBAL(0), 4, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ IXGBE_TDBAH(0), 4, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_TDLEN(0), 4, PATTERN_TEST, 0x000FFF80, 0x000FFF80 },
	{ IXGBE_RXCTRL, 1, SET_READ_TEST, 0x00000001, 0x00000001 },
	{ IXGBE_RAL(0), 16, TABLE64_TEST_LO, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_RAL(0), 16, TABLE64_TEST_HI, 0x8001FFFF, 0x800CFFFF },
	{ IXGBE_MTA(0), 128, TABLE32_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ 0, 0, 0, 0 }
};

/* default 82598 register test */
static const struct ixgbe_reg_test reg_test_82598[] = {
	{ IXGBE_FCRTL(0), 1, PATTERN_TEST, 0x8007FFF0, 0x8007FFF0 },
	{ IXGBE_FCRTH(0), 1, PATTERN_TEST, 0x8007FFF0, 0x8007FFF0 },
	{ IXGBE_PFCTOP, 1, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_VLNCTRL, 1, PATTERN_TEST, 0x00000000, 0x00000000 },
	{ IXGBE_RDBAL(0), 4, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ IXGBE_RDBAH(0), 4, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_RDLEN(0), 4, PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	/* Enable all four RX queues before testing. */
	{ IXGBE_RXDCTL(0), 4, WRITE_NO_TEST, 0, IXGBE_RXDCTL_ENABLE },
	/* RDH is read-only for 82598, only test RDT. */
	{ IXGBE_RDT(0), 4, PATTERN_TEST, 0x0000FFFF, 0x0000FFFF },
	{ IXGBE_RXDCTL(0), 4, WRITE_NO_TEST, 0, 0 },
	{ IXGBE_FCRTH(0), 1, PATTERN_TEST, 0x8007FFF0, 0x8007FFF0 },
	{ IXGBE_FCTTV(0), 1, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_TIPG, 1, PATTERN_TEST, 0x000000FF, 0x000000FF },
	{ IXGBE_TDBAL(0), 4, PATTERN_TEST, 0xFFFFFF80, 0xFFFFFFFF },
	{ IXGBE_TDBAH(0), 4, PATTERN_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_TDLEN(0), 4, PATTERN_TEST, 0x000FFF80, 0x000FFFFF },
	{ IXGBE_RXCTRL, 1, SET_READ_TEST, 0x00000003, 0x00000003 },
	{ IXGBE_DTXCTL, 1, SET_READ_TEST, 0x00000005, 0x00000005 },
	{ IXGBE_RAL(0), 16, TABLE64_TEST_LO, 0xFFFFFFFF, 0xFFFFFFFF },
	{ IXGBE_RAL(0), 16, TABLE64_TEST_HI, 0x800CFFFF, 0x800CFFFF },
	{ IXGBE_MTA(0), 128, TABLE32_TEST, 0xFFFFFFFF, 0xFFFFFFFF },
	{ 0, 0, 0, 0 }
};

static bool reg_pattern_test(struct ixgbe_adapter *adapter, u64 *data, int reg,
			     u32 mask, u32 write)
{
	u32 pat, val, before;
	static const u32 test_pattern[] = {
		0x5A5A5A5A, 0xA5A5A5A5, 0x00000000, 0xFFFFFFFF};

	if (ixgbe_removed(adapter->hw.hw_addr)) {
		*data = 1;
		return 1;
	}
	for (pat = 0; pat < ARRAY_SIZE(test_pattern); pat++) {
		before = ixgbe_read_reg(&adapter->hw, reg);
		ixgbe_write_reg(&adapter->hw, reg, test_pattern[pat] & write);
		val = ixgbe_read_reg(&adapter->hw, reg);
		if (val != (test_pattern[pat] & write & mask)) {
			e_err(drv, "pattern test reg %04X failed: got 0x%08X expected 0x%08X\n",
			      reg, val, (test_pattern[pat] & write & mask));
			*data = reg;
			ixgbe_write_reg(&adapter->hw, reg, before);
			return true;
		}
		ixgbe_write_reg(&adapter->hw, reg, before);
	}
	return false;
}

static bool reg_set_and_check(struct ixgbe_adapter *adapter, u64 *data, int reg,
			      u32 mask, u32 write)
{
	u32 val, before;

	if (ixgbe_removed(adapter->hw.hw_addr)) {
		*data = 1;
		return 1;
	}
	before = ixgbe_read_reg(&adapter->hw, reg);
	ixgbe_write_reg(&adapter->hw, reg, write & mask);
	val = ixgbe_read_reg(&adapter->hw, reg);
	if ((write & mask) != (val & mask)) {
		e_err(drv, "set/check reg %04X test failed: got 0x%08X expected 0x%08X\n",
		      reg, (val & mask), (write & mask));
		*data = reg;
		ixgbe_write_reg(&adapter->hw, reg, before);
		return true;
	}
	ixgbe_write_reg(&adapter->hw, reg, before);
	return false;
}

static int ixgbe_reg_test(struct ixgbe_adapter *adapter, u64 *data)
{
	const struct ixgbe_reg_test *test;
	u32 value, before, after;
	u32 i, toggle;

	if (ixgbe_removed(adapter->hw.hw_addr)) {
		e_err(drv, "Adapter removed - register test blocked\n");
		*data = 1;
		return 1;
	}
	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82598EB:
		toggle = 0x7FFFF3FF;
		test = reg_test_82598;
		break;
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		toggle = 0x7FFFF30F;
		test = reg_test_82599;
		break;
	default:
		*data = 1;
		return 1;
		break;
	}

	/*
	 * Because the status register is such a special case,
	 * we handle it separately from the rest of the register
	 * tests.  Some bits are read-only, some toggle, and some
	 * are writeable on newer MACs.
	 */
	before = ixgbe_read_reg(&adapter->hw, IXGBE_STATUS);
	value = (ixgbe_read_reg(&adapter->hw, IXGBE_STATUS) & toggle);
	ixgbe_write_reg(&adapter->hw, IXGBE_STATUS, toggle);
	after = ixgbe_read_reg(&adapter->hw, IXGBE_STATUS) & toggle;
	if (value != after) {
		e_err(drv, "failed STATUS register test got: 0x%08X expected: 0x%08X\n",
		      after, value);
		*data = 1;
		return 1;
	}
	/* restore previous status */
	ixgbe_write_reg(&adapter->hw, IXGBE_STATUS, before);

	/*
	 * Perform the remainder of the register test, looping through
	 * the test table until we either fail or reach the null entry.
	 */
	while (test->reg) {
		for (i = 0; i < test->array_len; i++) {
			bool b = false;

			switch (test->test_type) {
			case PATTERN_TEST:
				b = reg_pattern_test(adapter, data,
						     test->reg + (i * 0x40),
						     test->mask,
						     test->write);
				break;
			case SET_READ_TEST:
				b = reg_set_and_check(adapter, data,
						      test->reg + (i * 0x40),
						      test->mask,
						      test->write);
				break;
			case WRITE_NO_TEST:
				ixgbe_write_reg(&adapter->hw,
						test->reg + (i * 0x40),
						test->write);
				break;
			case TABLE32_TEST:
				b = reg_pattern_test(adapter, data,
						     test->reg + (i * 4),
						     test->mask,
						     test->write);
				break;
			case TABLE64_TEST_LO:
				b = reg_pattern_test(adapter, data,
						     test->reg + (i * 8),
						     test->mask,
						     test->write);
				break;
			case TABLE64_TEST_HI:
				b = reg_pattern_test(adapter, data,
						     (test->reg + 4) + (i * 8),
						     test->mask,
						     test->write);
				break;
			}
			if (b)
				return 1;
		}
		test++;
	}

	*data = 0;
	return 0;
}

static int ixgbe_eeprom_test(struct ixgbe_adapter *adapter, u64 *data)
{
	struct ixgbe_hw *hw = &adapter->hw;
	if (hw->eeprom.ops.validate_checksum(hw, NULL))
		*data = 1;
	else
		*data = 0;
	return *data;
}

static irqreturn_t ixgbe_test_intr(int irq, void *data)
{
	struct net_device *netdev = (struct net_device *) data;
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	adapter->test_icr |= IXGBE_READ_REG(&adapter->hw, IXGBE_EICR);

	return IRQ_HANDLED;
}

static int ixgbe_intr_test(struct ixgbe_adapter *adapter, u64 *data)
{
	struct net_device *netdev = adapter->netdev;
	u32 mask, i = 0, shared_int = true;
	u32 irq = adapter->pdev->irq;

	*data = 0;

	/* Hook up test interrupt handler just for this test */
	if (adapter->msix_entries) {
		/* NOTE: we don't test MSI-X interrupts here, yet */
		return 0;
	} else if (adapter->flags & IXGBE_FLAG_MSI_ENABLED) {
		shared_int = false;
		if (request_irq(irq, ixgbe_test_intr, 0, netdev->name,
				netdev)) {
			*data = 1;
			return -1;
		}
	} else if (!request_irq(irq, ixgbe_test_intr, IRQF_PROBE_SHARED,
				netdev->name, netdev)) {
		shared_int = false;
	} else if (request_irq(irq, ixgbe_test_intr, IRQF_SHARED,
			       netdev->name, netdev)) {
		*data = 1;
		return -1;
	}
	e_info(hw, "testing %s interrupt\n", shared_int ?
	       "shared" : "unshared");

	/* Disable all the interrupts */
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC, 0xFFFFFFFF);
	IXGBE_WRITE_FLUSH(&adapter->hw);
	usleep_range(10000, 20000);

	/* Test each interrupt */
	for (; i < 10; i++) {
		/* Interrupt to test */
		mask = 1 << i;

		if (!shared_int) {
			/*
			 * Disable the interrupts to be reported in
			 * the cause register and then force the same
			 * interrupt and see if one gets posted.  If
			 * an interrupt was posted to the bus, the
			 * test failed.
			 */
			adapter->test_icr = 0;
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC,
					~mask & 0x00007FFF);
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS,
					~mask & 0x00007FFF);
			IXGBE_WRITE_FLUSH(&adapter->hw);
			usleep_range(10000, 20000);

			if (adapter->test_icr & mask) {
				*data = 3;
				break;
			}
		}

		/*
		 * Enable the interrupt to be reported in the cause
		 * register and then force the same interrupt and see
		 * if one gets posted.  If an interrupt was not posted
		 * to the bus, the test failed.
		 */
		adapter->test_icr = 0;
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMS, mask);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS, mask);
		IXGBE_WRITE_FLUSH(&adapter->hw);
		usleep_range(10000, 20000);

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
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC,
					~mask & 0x00007FFF);
			IXGBE_WRITE_REG(&adapter->hw, IXGBE_EICS,
					~mask & 0x00007FFF);
			IXGBE_WRITE_FLUSH(&adapter->hw);
			usleep_range(10000, 20000);

			if (adapter->test_icr) {
				*data = 5;
				break;
			}
		}
	}

	/* Disable all the interrupts */
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMC, 0xFFFFFFFF);
	IXGBE_WRITE_FLUSH(&adapter->hw);
	usleep_range(10000, 20000);

	/* Unhook test interrupt handler */
	free_irq(irq, netdev);

	return *data;
}

static void ixgbe_free_desc_rings(struct ixgbe_adapter *adapter)
{
	struct ixgbe_ring *tx_ring = &adapter->test_tx_ring;
	struct ixgbe_ring *rx_ring = &adapter->test_rx_ring;
	struct ixgbe_hw *hw = &adapter->hw;
	u32 reg_ctl;

	/* shut down the DMA engines now so they can be reinitialized later */

	/* first Rx */
	reg_ctl = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	reg_ctl &= ~IXGBE_RXCTRL_RXEN;
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, reg_ctl);
	ixgbe_disable_rx_queue(adapter, rx_ring);

	/* now Tx */
	reg_ctl = IXGBE_READ_REG(hw, IXGBE_TXDCTL(tx_ring->reg_idx));
	reg_ctl &= ~IXGBE_TXDCTL_ENABLE;
	IXGBE_WRITE_REG(hw, IXGBE_TXDCTL(tx_ring->reg_idx), reg_ctl);

	switch (hw->mac.type) {
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		reg_ctl = IXGBE_READ_REG(hw, IXGBE_DMATXCTL);
		reg_ctl &= ~IXGBE_DMATXCTL_TE;
		IXGBE_WRITE_REG(hw, IXGBE_DMATXCTL, reg_ctl);
		break;
	default:
		break;
	}

	ixgbe_reset(adapter);

	ixgbe_free_tx_resources(&adapter->test_tx_ring);
	ixgbe_free_rx_resources(&adapter->test_rx_ring);
}

static int ixgbe_setup_desc_rings(struct ixgbe_adapter *adapter)
{
	struct ixgbe_ring *tx_ring = &adapter->test_tx_ring;
	struct ixgbe_ring *rx_ring = &adapter->test_rx_ring;
	u32 rctl, reg_data;
	int ret_val;
	int err;

	/* Setup Tx descriptor ring and Tx buffers */
	tx_ring->count = IXGBE_DEFAULT_TXD;
	tx_ring->queue_index = 0;
	tx_ring->dev = &adapter->pdev->dev;
	tx_ring->netdev = adapter->netdev;
	tx_ring->reg_idx = adapter->tx_ring[0]->reg_idx;

	err = ixgbe_setup_tx_resources(tx_ring);
	if (err)
		return 1;

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_82599EB:
	case ixgbe_mac_X540:
		reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_DMATXCTL);
		reg_data |= IXGBE_DMATXCTL_TE;
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_DMATXCTL, reg_data);
		break;
	default:
		break;
	}

	ixgbe_configure_tx_ring(adapter, tx_ring);

	/* Setup Rx Descriptor ring and Rx buffers */
	rx_ring->count = IXGBE_DEFAULT_RXD;
	rx_ring->queue_index = 0;
	rx_ring->dev = &adapter->pdev->dev;
	rx_ring->netdev = adapter->netdev;
	rx_ring->reg_idx = adapter->rx_ring[0]->reg_idx;

	err = ixgbe_setup_rx_resources(rx_ring);
	if (err) {
		ret_val = 4;
		goto err_nomem;
	}

	rctl = IXGBE_READ_REG(&adapter->hw, IXGBE_RXCTRL);
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RXCTRL, rctl & ~IXGBE_RXCTRL_RXEN);

	ixgbe_configure_rx_ring(adapter, rx_ring);

	rctl |= IXGBE_RXCTRL_RXEN | IXGBE_RXCTRL_DMBYPS;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_RXCTRL, rctl);

	return 0;

err_nomem:
	ixgbe_free_desc_rings(adapter);
	return ret_val;
}

static int ixgbe_setup_loopback_test(struct ixgbe_adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	u32 reg_data;


	/* Setup MAC loopback */
	reg_data = IXGBE_READ_REG(hw, IXGBE_HLREG0);
	reg_data |= IXGBE_HLREG0_LPBK;
	IXGBE_WRITE_REG(hw, IXGBE_HLREG0, reg_data);

	reg_data = IXGBE_READ_REG(hw, IXGBE_FCTRL);
	reg_data |= IXGBE_FCTRL_BAM | IXGBE_FCTRL_SBP | IXGBE_FCTRL_MPE;
	IXGBE_WRITE_REG(hw, IXGBE_FCTRL, reg_data);

	/* X540 needs to set the MACC.FLU bit to force link up */
	if (adapter->hw.mac.type == ixgbe_mac_X540) {
		reg_data = IXGBE_READ_REG(hw, IXGBE_MACC);
		reg_data |= IXGBE_MACC_FLU;
		IXGBE_WRITE_REG(hw, IXGBE_MACC, reg_data);
	} else {
		if (hw->mac.orig_autoc) {
			reg_data = hw->mac.orig_autoc | IXGBE_AUTOC_FLU;
			IXGBE_WRITE_REG(hw, IXGBE_AUTOC, reg_data);
		} else {
			return 10;
		}
	}
	IXGBE_WRITE_FLUSH(hw);
	usleep_range(10000, 20000);

	/* Disable Atlas Tx lanes; re-enabled in reset path */
	if (hw->mac.type == ixgbe_mac_82598EB) {
		u8 atlas;

		hw->mac.ops.read_analog_reg8(hw, IXGBE_ATLAS_PDN_LPBK, &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_REG_EN;
		hw->mac.ops.write_analog_reg8(hw, IXGBE_ATLAS_PDN_LPBK, atlas);

		hw->mac.ops.read_analog_reg8(hw, IXGBE_ATLAS_PDN_10G, &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_10G_QL_ALL;
		hw->mac.ops.write_analog_reg8(hw, IXGBE_ATLAS_PDN_10G, atlas);

		hw->mac.ops.read_analog_reg8(hw, IXGBE_ATLAS_PDN_1G, &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_1G_QL_ALL;
		hw->mac.ops.write_analog_reg8(hw, IXGBE_ATLAS_PDN_1G, atlas);

		hw->mac.ops.read_analog_reg8(hw, IXGBE_ATLAS_PDN_AN, &atlas);
		atlas |= IXGBE_ATLAS_PDN_TX_AN_QL_ALL;
		hw->mac.ops.write_analog_reg8(hw, IXGBE_ATLAS_PDN_AN, atlas);
	}

	return 0;
}

static void ixgbe_loopback_cleanup(struct ixgbe_adapter *adapter)
{
	u32 reg_data;

	reg_data = IXGBE_READ_REG(&adapter->hw, IXGBE_HLREG0);
	reg_data &= ~IXGBE_HLREG0_LPBK;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_HLREG0, reg_data);
}

static void ixgbe_create_lbtest_frame(struct sk_buff *skb,
				      unsigned int frame_size)
{
	memset(skb->data, 0xFF, frame_size);
	frame_size >>= 1;
	memset(&skb->data[frame_size], 0xAA, frame_size / 2 - 1);
	memset(&skb->data[frame_size + 10], 0xBE, 1);
	memset(&skb->data[frame_size + 12], 0xAF, 1);
}

static bool ixgbe_check_lbtest_frame(struct ixgbe_rx_buffer *rx_buffer,
				     unsigned int frame_size)
{
	unsigned char *data;
	bool match = true;

	frame_size >>= 1;

	data = kmap(rx_buffer->page) + rx_buffer->page_offset;

	if (data[3] != 0xFF ||
	    data[frame_size + 10] != 0xBE ||
	    data[frame_size + 12] != 0xAF)
		match = false;

	kunmap(rx_buffer->page);

	return match;
}

static u16 ixgbe_clean_test_rings(struct ixgbe_ring *rx_ring,
				  struct ixgbe_ring *tx_ring,
				  unsigned int size)
{
	union ixgbe_adv_rx_desc *rx_desc;
	struct ixgbe_rx_buffer *rx_buffer;
	struct ixgbe_tx_buffer *tx_buffer;
	u16 rx_ntc, tx_ntc, count = 0;

	/* initialize next to clean and descriptor values */
	rx_ntc = rx_ring->next_to_clean;
	tx_ntc = tx_ring->next_to_clean;
	rx_desc = IXGBE_RX_DESC(rx_ring, rx_ntc);

	while (ixgbe_test_staterr(rx_desc, IXGBE_RXD_STAT_DD)) {
		/* check Rx buffer */
		rx_buffer = &rx_ring->rx_buffer_info[rx_ntc];

		/* sync Rx buffer for CPU read */
		dma_sync_single_for_cpu(rx_ring->dev,
					rx_buffer->dma,
					ixgbe_rx_bufsz(rx_ring),
					DMA_FROM_DEVICE);

		/* verify contents of skb */
		if (ixgbe_check_lbtest_frame(rx_buffer, size))
			count++;

		/* sync Rx buffer for device write */
		dma_sync_single_for_device(rx_ring->dev,
					   rx_buffer->dma,
					   ixgbe_rx_bufsz(rx_ring),
					   DMA_FROM_DEVICE);

		/* unmap buffer on Tx side */
		tx_buffer = &tx_ring->tx_buffer_info[tx_ntc];
		ixgbe_unmap_and_free_tx_resource(tx_ring, tx_buffer);

		/* increment Rx/Tx next to clean counters */
		rx_ntc++;
		if (rx_ntc == rx_ring->count)
			rx_ntc = 0;
		tx_ntc++;
		if (tx_ntc == tx_ring->count)
			tx_ntc = 0;

		/* fetch next descriptor */
		rx_desc = IXGBE_RX_DESC(rx_ring, rx_ntc);
	}

	netdev_tx_reset_queue(txring_txq(tx_ring));

	/* re-map buffers to ring, store next to clean values */
	ixgbe_alloc_rx_buffers(rx_ring, count);
	rx_ring->next_to_clean = rx_ntc;
	tx_ring->next_to_clean = tx_ntc;

	return count;
}

static int ixgbe_run_loopback_test(struct ixgbe_adapter *adapter)
{
	struct ixgbe_ring *tx_ring = &adapter->test_tx_ring;
	struct ixgbe_ring *rx_ring = &adapter->test_rx_ring;
	int i, j, lc, good_cnt, ret_val = 0;
	unsigned int size = 1024;
	netdev_tx_t tx_ret_val;
	struct sk_buff *skb;
	u32 flags_orig = adapter->flags;

	/* DCB can modify the frames on Tx */
	adapter->flags &= ~IXGBE_FLAG_DCB_ENABLED;

	/* allocate test skb */
	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return 11;

	/* place data into test skb */
	ixgbe_create_lbtest_frame(skb, size);
	skb_put(skb, size);

	/*
	 * Calculate the loop count based on the largest descriptor ring
	 * The idea is to wrap the largest ring a number of times using 64
	 * send/receive pairs during each loop
	 */

	if (rx_ring->count <= tx_ring->count)
		lc = ((tx_ring->count / 64) * 2) + 1;
	else
		lc = ((rx_ring->count / 64) * 2) + 1;

	for (j = 0; j <= lc; j++) {
		/* reset count of good packets */
		good_cnt = 0;

		/* place 64 packets on the transmit queue*/
		for (i = 0; i < 64; i++) {
			skb_get(skb);
			tx_ret_val = ixgbe_xmit_frame_ring(skb,
							   adapter,
							   tx_ring);
			if (tx_ret_val == NETDEV_TX_OK)
				good_cnt++;
		}

		if (good_cnt != 64) {
			ret_val = 12;
			break;
		}

		/* allow 200 milliseconds for packets to go from Tx to Rx */
		msleep(200);

		good_cnt = ixgbe_clean_test_rings(rx_ring, tx_ring, size);
		if (good_cnt != 64) {
			ret_val = 13;
			break;
		}
	}

	/* free the original skb */
	kfree_skb(skb);
	adapter->flags = flags_orig;

	return ret_val;
}

static int ixgbe_loopback_test(struct ixgbe_adapter *adapter, u64 *data)
{
	*data = ixgbe_setup_desc_rings(adapter);
	if (*data)
		goto out;
	*data = ixgbe_setup_loopback_test(adapter);
	if (*data)
		goto err_loopback;
	*data = ixgbe_run_loopback_test(adapter);
	ixgbe_loopback_cleanup(adapter);

err_loopback:
	ixgbe_free_desc_rings(adapter);
out:
	return *data;
}

static void ixgbe_diag_test(struct net_device *netdev,
			    struct ethtool_test *eth_test, u64 *data)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	bool if_running = netif_running(netdev);

	if (ixgbe_removed(adapter->hw.hw_addr)) {
		e_err(hw, "Adapter removed - test blocked\n");
		data[0] = 1;
		data[1] = 1;
		data[2] = 1;
		data[3] = 1;
		data[4] = 1;
		eth_test->flags |= ETH_TEST_FL_FAILED;
		return;
	}
	set_bit(__IXGBE_TESTING, &adapter->state);
	if (eth_test->flags == ETH_TEST_FL_OFFLINE) {
		struct ixgbe_hw *hw = &adapter->hw;

		if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED) {
			int i;
			for (i = 0; i < adapter->num_vfs; i++) {
				if (adapter->vfinfo[i].clear_to_send) {
					netdev_warn(netdev, "offline diagnostic is not supported when VFs are present\n");
					data[0] = 1;
					data[1] = 1;
					data[2] = 1;
					data[3] = 1;
					data[4] = 1;
					eth_test->flags |= ETH_TEST_FL_FAILED;
					clear_bit(__IXGBE_TESTING,
						  &adapter->state);
					goto skip_ol_tests;
				}
			}
		}

		/* Offline tests */
		e_info(hw, "offline testing starting\n");

		/* Link test performed before hardware reset so autoneg doesn't
		 * interfere with test result
		 */
		if (ixgbe_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		if (if_running)
			/* indicate we're in test mode */
			dev_close(netdev);
		else
			ixgbe_reset(adapter);

		e_info(hw, "register testing starting\n");
		if (ixgbe_reg_test(adapter, &data[0]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		ixgbe_reset(adapter);
		e_info(hw, "eeprom testing starting\n");
		if (ixgbe_eeprom_test(adapter, &data[1]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		ixgbe_reset(adapter);
		e_info(hw, "interrupt testing starting\n");
		if (ixgbe_intr_test(adapter, &data[2]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* If SRIOV or VMDq is enabled then skip MAC
		 * loopback diagnostic. */
		if (adapter->flags & (IXGBE_FLAG_SRIOV_ENABLED |
				      IXGBE_FLAG_VMDQ_ENABLED)) {
			e_info(hw, "Skip MAC loopback diagnostic in VT mode\n");
			data[3] = 0;
			goto skip_loopback;
		}

		ixgbe_reset(adapter);
		e_info(hw, "loopback testing starting\n");
		if (ixgbe_loopback_test(adapter, &data[3]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

skip_loopback:
		ixgbe_reset(adapter);

		/* clear testing bit and return adapter to previous state */
		clear_bit(__IXGBE_TESTING, &adapter->state);
		if (if_running)
			dev_open(netdev);
		else if (hw->mac.ops.disable_tx_laser)
			hw->mac.ops.disable_tx_laser(hw);
	} else {
		e_info(hw, "online testing starting\n");

		/* Online tests */
		if (ixgbe_link_test(adapter, &data[4]))
			eth_test->flags |= ETH_TEST_FL_FAILED;

		/* Offline tests aren't run; pass by default */
		data[0] = 0;
		data[1] = 0;
		data[2] = 0;
		data[3] = 0;

		clear_bit(__IXGBE_TESTING, &adapter->state);
	}

skip_ol_tests:
	msleep_interruptible(4 * 1000);
}

static int ixgbe_wol_exclusion(struct ixgbe_adapter *adapter,
			       struct ethtool_wolinfo *wol)
{
	struct ixgbe_hw *hw = &adapter->hw;
	int retval = 0;

	/* WOL not supported for all devices */
	if (!ixgbe_wol_supported(adapter, hw->device_id,
				 hw->subsystem_device_id)) {
		retval = 1;
		wol->supported = 0;
	}

	return retval;
}

static void ixgbe_get_wol(struct net_device *netdev,
			  struct ethtool_wolinfo *wol)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	wol->supported = WAKE_UCAST | WAKE_MCAST |
			 WAKE_BCAST | WAKE_MAGIC;
	wol->wolopts = 0;

	if (ixgbe_wol_exclusion(adapter, wol) ||
	    !device_can_wakeup(&adapter->pdev->dev))
		return;

	if (adapter->wol & IXGBE_WUFC_EX)
		wol->wolopts |= WAKE_UCAST;
	if (adapter->wol & IXGBE_WUFC_MC)
		wol->wolopts |= WAKE_MCAST;
	if (adapter->wol & IXGBE_WUFC_BC)
		wol->wolopts |= WAKE_BCAST;
	if (adapter->wol & IXGBE_WUFC_MAG)
		wol->wolopts |= WAKE_MAGIC;
}

static int ixgbe_set_wol(struct net_device *netdev, struct ethtool_wolinfo *wol)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (wol->wolopts & (WAKE_PHY | WAKE_ARP | WAKE_MAGICSECURE))
		return -EOPNOTSUPP;

	if (ixgbe_wol_exclusion(adapter, wol))
		return wol->wolopts ? -EOPNOTSUPP : 0;

	adapter->wol = 0;

	if (wol->wolopts & WAKE_UCAST)
		adapter->wol |= IXGBE_WUFC_EX;
	if (wol->wolopts & WAKE_MCAST)
		adapter->wol |= IXGBE_WUFC_MC;
	if (wol->wolopts & WAKE_BCAST)
		adapter->wol |= IXGBE_WUFC_BC;
	if (wol->wolopts & WAKE_MAGIC)
		adapter->wol |= IXGBE_WUFC_MAG;

	device_set_wakeup_enable(&adapter->pdev->dev, adapter->wol);

	return 0;
}

static int ixgbe_nway_reset(struct net_device *netdev)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	if (netif_running(netdev))
		ixgbe_reinit_locked(adapter);

	return 0;
}

static int ixgbe_set_phys_id(struct net_device *netdev,
			     enum ethtool_phys_id_state state)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_hw *hw = &adapter->hw;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		adapter->led_reg = IXGBE_READ_REG(hw, IXGBE_LEDCTL);
		return 2;

	case ETHTOOL_ID_ON:
		hw->mac.ops.led_on(hw, IXGBE_LED_ON);
		break;

	case ETHTOOL_ID_OFF:
		hw->mac.ops.led_off(hw, IXGBE_LED_ON);
		break;

	case ETHTOOL_ID_INACTIVE:
		/* Restore LED settings */
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_LEDCTL, adapter->led_reg);
		break;
	}

	return 0;
}

static int ixgbe_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);

	/* only valid if in constant ITR mode */
	if (adapter->rx_itr_setting <= 1)
		ec->rx_coalesce_usecs = adapter->rx_itr_setting;
	else
		ec->rx_coalesce_usecs = adapter->rx_itr_setting >> 2;

	/* if in mixed tx/rx queues per vector mode, report only rx settings */
	if (adapter->q_vector[0]->tx.count && adapter->q_vector[0]->rx.count)
		return 0;

	/* only valid if in constant ITR mode */
	if (adapter->tx_itr_setting <= 1)
		ec->tx_coalesce_usecs = adapter->tx_itr_setting;
	else
		ec->tx_coalesce_usecs = adapter->tx_itr_setting >> 2;

	return 0;
}

/*
 * this function must be called before setting the new value of
 * rx_itr_setting
 */
static bool ixgbe_update_rsc(struct ixgbe_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	/* nothing to do if LRO or RSC are not enabled */
	if (!(adapter->flags2 & IXGBE_FLAG2_RSC_CAPABLE) ||
	    !(netdev->features & NETIF_F_LRO))
		return false;

	/* check the feature flag value and enable RSC if necessary */
	if (adapter->rx_itr_setting == 1 ||
	    adapter->rx_itr_setting > IXGBE_MIN_RSC_ITR) {
		if (!(adapter->flags2 & IXGBE_FLAG2_RSC_ENABLED)) {
			adapter->flags2 |= IXGBE_FLAG2_RSC_ENABLED;
			e_info(probe, "rx-usecs value high enough to re-enable RSC\n");
			return true;
		}
	/* if interrupt rate is too high then disable RSC */
	} else if (adapter->flags2 & IXGBE_FLAG2_RSC_ENABLED) {
		adapter->flags2 &= ~IXGBE_FLAG2_RSC_ENABLED;
		e_info(probe, "rx-usecs set too low, disabling RSC\n");
		return true;
	}
	return false;
}

static int ixgbe_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *ec)
{
	struct ixgbe_adapter *adapter = netdev_priv(netdev);
	struct ixgbe_q_vector *q_vector;
	int i;
	u16 tx_itr_param, rx_itr_param, tx_itr_prev;
	bool need_reset = false;

	if (adapter->q_vector[0]->tx.count && adapter->q_vector[0]->rx.count) {
		/* reject Tx specific changes in case of mixed RxTx vectors */
		if (ec->tx_coalesce_usecs)
			return -EINVAL;
		tx_itr_prev = adapter->rx_itr_setting;
	} else {
		tx_itr_prev = adapter->tx_itr_setting;
	}

	if ((ec->rx_coalesce_usecs > (IXGBE_MAX_EITR >> 2)) ||
	    (ec->tx_coalesce_usecs > (IXGBE_MAX_EITR >> 2)))
		return -EINVAL;

	if (ec->rx_coalesce_usecs > 1)
		adapter->rx_itr_setting = ec->rx_coalesce_usecs << 2;
	else
		adapter->rx_itr_setting = ec->rx_coalesce_usecs;

	if (adapter->rx_itr_setting == 1)
		rx_itr_param = IXGBE_20K_ITR;
	else
		rx_itr_param = adapter->rx_itr_setting;

	if (ec->tx_coalesce_usecs > 1)
		adapter->tx_itr_setting = ec->tx_coalesce_usecs << 2;
	else
		adapter->tx_itr_setting = ec->tx_coalesce_usecs;

	if (adapter->tx_itr_setting == 1)
		tx_itr_param = IXGBE_10K_ITR;
	else
		tx_itr_param = adapter->tx_itr_setting;

	/* mixed Rx/Tx */
	if (adapter->q_vector[0]->tx.count && adapter->q_vector[0]->rx.count)
		adapter->tx_itr_setting = adapter->rx_itr_setting;

#if IS_ENABLED(CONFIG_BQL)
	/* detect ITR changes that require update of TXDCTL.WTHRESH */
	if ((adapter->tx_itr_setting != 1) &&
	    (adapter->tx_itr_setting < IXGBE_100K_ITR)) {
		if ((tx_itr_prev == 1) ||
		    (tx_itr_prev >= IXGBE_100K_ITR))
			need_reset = true;
	} else {
		if ((tx_itr_prev != 1) &&
		    (tx_itr_prev < IXGBE_100K_ITR))
			need_reset = true;
	}
#endif
	/* check the old value and enable RSC if necessary */
	need_reset |= ixgbe_update_rsc(adapter);

	for (i = 0; i < adapter->num_q_vectors; i++) {
		q_vector = adapter->q_vector[i];
		if (q_vector->tx.count && !q_vector->rx.count)
			/* tx only */
			q_vector->itr = tx_itr_param;
		else
			/* rx only or mixed */
			q_vector->itr = rx_itr_param;
		ixgbe_write_eitr(q_vector);
	}

	/*
	 * do reset here at the end to make sure EITR==0 case is handled
	 * correctly w.r.t stopping tx, and changing TXDCTL.WTHRESH settings
	 * also locks in RSC enable/disable which requires reset
	 */
	if (need_reset)
		ixgbe_do_reset(netdev);

	return 0;
}

static int ixgbe_get_ethtool_fdir_entry(struct ixgbe_adapter *adapter,
					struct ethtool_rxnfc *cmd)
{
	union ixgbe_atr_input *mask = &adapter->fdir_mask;
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct hlist_node *node2;
	struct ixgbe_fdir_filter *rule = NULL;

	/* report total rule count */
	cmd->data = (1024 << adapter->fdir_pballoc) - 2;

	hlist_for_each_entry_safe(rule, node2,
				  &adapter->fdir_filter_list, fdir_node) {
		if (fsp->location <= rule->sw_idx)
			break;
	}

	if (!rule || fsp->location != rule->sw_idx)
		return -EINVAL;

	/* fill out the flow spec entry */

	/* set flow type field */
	switch (rule->filter.formatted.flow_type) {
	case IXGBE_ATR_FLOW_TYPE_TCPV4:
		fsp->flow_type = TCP_V4_FLOW;
		break;
	case IXGBE_ATR_FLOW_TYPE_UDPV4:
		fsp->flow_type = UDP_V4_FLOW;
		break;
	case IXGBE_ATR_FLOW_TYPE_SCTPV4:
		fsp->flow_type = SCTP_V4_FLOW;
		break;
	case IXGBE_ATR_FLOW_TYPE_IPV4:
		fsp->flow_type = IP_USER_FLOW;
		fsp->h_u.usr_ip4_spec.ip_ver = ETH_RX_NFC_IP4;
		fsp->h_u.usr_ip4_spec.proto = 0;
		fsp->m_u.usr_ip4_spec.proto = 0;
		break;
	default:
		return -EINVAL;
	}

	fsp->h_u.tcp_ip4_spec.psrc = rule->filter.formatted.src_port;
	fsp->m_u.tcp_ip4_spec.psrc = mask->formatted.src_port;
	fsp->h_u.tcp_ip4_spec.pdst = rule->filter.formatted.dst_port;
	fsp->m_u.tcp_ip4_spec.pdst = mask->formatted.dst_port;
	fsp->h_u.tcp_ip4_spec.ip4src = rule->filter.formatted.src_ip[0];
	fsp->m_u.tcp_ip4_spec.ip4src = mask->formatted.src_ip[0];
	fsp->h_u.tcp_ip4_spec.ip4dst = rule->filter.formatted.dst_ip[0];
	fsp->m_u.tcp_ip4_spec.ip4dst = mask->formatted.dst_ip[0];
	fsp->h_ext.vlan_tci = rule->filter.formatted.vlan_id;
	fsp->m_ext.vlan_tci = mask->formatted.vlan_id;
	fsp->h_ext.vlan_etype = rule->filter.formatted.flex_bytes;
	fsp->m_ext.vlan_etype = mask->formatted.flex_bytes;
	fsp->h_ext.data[1] = htonl(rule->filter.formatted.vm_pool);
	fsp->m_ext.data[1] = htonl(mask->formatted.vm_pool);
	fsp->flow_type |= FLOW_EXT;

	/* record action */
	if (rule->action == IXGBE_FDIR_DROP_QUEUE)
		fsp->ring_cookie = RX_CLS_FLOW_DISC;
	else
		fsp->ring_cookie = rule->action;

	return 0;
}

static int ixgbe_get_ethtool_fdir_all(struct ixgbe_adapter *adapter,
				      struct ethtool_rxnfc *cmd,
				      u32 *rule_locs)
{
	struct hlist_node *node2;
	struct ixgbe_fdir_filter *rule;
	int cnt = 0;

	/* report total rule count */
	cmd->data = (1024 << adapter->fdir_pballoc) - 2;

	hlist_for_each_entry_safe(rule, node2,
				  &adapter->fdir_filter_list, fdir_node) {
		if (cnt == cmd->rule_cnt)
			return -EMSGSIZE;
		rule_locs[cnt] = rule->sw_idx;
		cnt++;
	}

	cmd->rule_cnt = cnt;

	return 0;
}

static int ixgbe_get_rss_hash_opts(struct ixgbe_adapter *adapter,
				   struct ethtool_rxnfc *cmd)
{
	cmd->data = 0;

	/* Report default options for RSS on ixgbe */
	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		/* fallthrough */
	case UDP_V4_FLOW:
		if (adapter->flags2 & IXGBE_FLAG2_RSS_FIELD_IPV4_UDP)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		/* fallthrough */
	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case IPV4_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	case TCP_V6_FLOW:
		cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		/* fallthrough */
	case UDP_V6_FLOW:
		if (adapter->flags2 & IXGBE_FLAG2_RSS_FIELD_IPV6_UDP)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		/* fallthrough */
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IPV6_FLOW:
		cmd->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ixgbe_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
			   u32 *rule_locs)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = adapter->num_rx_queues;
		ret = 0;
		break;
	case ETHTOOL_GRXCLSRLCNT:
		cmd->rule_cnt = adapter->fdir_filter_count;
		ret = 0;
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = ixgbe_get_ethtool_fdir_entry(adapter, cmd);
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = ixgbe_get_ethtool_fdir_all(adapter, cmd, rule_locs);
		break;
	case ETHTOOL_GRXFH:
		ret = ixgbe_get_rss_hash_opts(adapter, cmd);
		break;
	default:
		break;
	}

	return ret;
}

static int ixgbe_update_ethtool_fdir_entry(struct ixgbe_adapter *adapter,
					   struct ixgbe_fdir_filter *input,
					   u16 sw_idx)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct hlist_node *node2;
	struct ixgbe_fdir_filter *rule, *parent;
	int err = -EINVAL;

	parent = NULL;
	rule = NULL;

	hlist_for_each_entry_safe(rule, node2,
				  &adapter->fdir_filter_list, fdir_node) {
		/* hash found, or no matching entry */
		if (rule->sw_idx >= sw_idx)
			break;
		parent = rule;
	}

	/* if there is an old rule occupying our place remove it */
	if (rule && (rule->sw_idx == sw_idx)) {
		if (!input || (rule->filter.formatted.bkt_hash !=
			       input->filter.formatted.bkt_hash)) {
			err = ixgbe_fdir_erase_perfect_filter_82599(hw,
								&rule->filter,
								sw_idx);
		}

		hlist_del(&rule->fdir_node);
		kfree(rule);
		adapter->fdir_filter_count--;
	}

	/*
	 * If no input this was a delete, err should be 0 if a rule was
	 * successfully found and removed from the list else -EINVAL
	 */
	if (!input)
		return err;

	/* initialize node and set software index */
	INIT_HLIST_NODE(&input->fdir_node);

	/* add filter to the list */
	if (parent)
		hlist_add_after(&parent->fdir_node, &input->fdir_node);
	else
		hlist_add_head(&input->fdir_node,
			       &adapter->fdir_filter_list);

	/* update counts */
	adapter->fdir_filter_count++;

	return 0;
}

static int ixgbe_flowspec_to_flow_type(struct ethtool_rx_flow_spec *fsp,
				       u8 *flow_type)
{
	switch (fsp->flow_type & ~FLOW_EXT) {
	case TCP_V4_FLOW:
		*flow_type = IXGBE_ATR_FLOW_TYPE_TCPV4;
		break;
	case UDP_V4_FLOW:
		*flow_type = IXGBE_ATR_FLOW_TYPE_UDPV4;
		break;
	case SCTP_V4_FLOW:
		*flow_type = IXGBE_ATR_FLOW_TYPE_SCTPV4;
		break;
	case IP_USER_FLOW:
		switch (fsp->h_u.usr_ip4_spec.proto) {
		case IPPROTO_TCP:
			*flow_type = IXGBE_ATR_FLOW_TYPE_TCPV4;
			break;
		case IPPROTO_UDP:
			*flow_type = IXGBE_ATR_FLOW_TYPE_UDPV4;
			break;
		case IPPROTO_SCTP:
			*flow_type = IXGBE_ATR_FLOW_TYPE_SCTPV4;
			break;
		case 0:
			if (!fsp->m_u.usr_ip4_spec.proto) {
				*flow_type = IXGBE_ATR_FLOW_TYPE_IPV4;
				break;
			}
		default:
			return 0;
		}
		break;
	default:
		return 0;
	}

	return 1;
}

static int ixgbe_add_ethtool_fdir_entry(struct ixgbe_adapter *adapter,
					struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_fdir_filter *input;
	union ixgbe_atr_input mask;
	int err;

	if (!(adapter->flags & IXGBE_FLAG_FDIR_PERFECT_CAPABLE))
		return -EOPNOTSUPP;

	/*
	 * Don't allow programming if the action is a queue greater than
	 * the number of online Rx queues.
	 */
	if ((fsp->ring_cookie != RX_CLS_FLOW_DISC) &&
	    (fsp->ring_cookie >= adapter->num_rx_queues))
		return -EINVAL;

	/* Don't allow indexes to exist outside of available space */
	if (fsp->location >= ((1024 << adapter->fdir_pballoc) - 2)) {
		e_err(drv, "Location out of range\n");
		return -EINVAL;
	}

	input = kzalloc(sizeof(*input), GFP_ATOMIC);
	if (!input)
		return -ENOMEM;

	memset(&mask, 0, sizeof(union ixgbe_atr_input));

	/* set SW index */
	input->sw_idx = fsp->location;

	/* record flow type */
	if (!ixgbe_flowspec_to_flow_type(fsp,
					 &input->filter.formatted.flow_type)) {
		e_err(drv, "Unrecognized flow type\n");
		goto err_out;
	}

	mask.formatted.flow_type = IXGBE_ATR_L4TYPE_IPV6_MASK |
				   IXGBE_ATR_L4TYPE_MASK;

	if (input->filter.formatted.flow_type == IXGBE_ATR_FLOW_TYPE_IPV4)
		mask.formatted.flow_type &= IXGBE_ATR_L4TYPE_IPV6_MASK;

	/* Copy input into formatted structures */
	input->filter.formatted.src_ip[0] = fsp->h_u.tcp_ip4_spec.ip4src;
	mask.formatted.src_ip[0] = fsp->m_u.tcp_ip4_spec.ip4src;
	input->filter.formatted.dst_ip[0] = fsp->h_u.tcp_ip4_spec.ip4dst;
	mask.formatted.dst_ip[0] = fsp->m_u.tcp_ip4_spec.ip4dst;
	input->filter.formatted.src_port = fsp->h_u.tcp_ip4_spec.psrc;
	mask.formatted.src_port = fsp->m_u.tcp_ip4_spec.psrc;
	input->filter.formatted.dst_port = fsp->h_u.tcp_ip4_spec.pdst;
	mask.formatted.dst_port = fsp->m_u.tcp_ip4_spec.pdst;

	if (fsp->flow_type & FLOW_EXT) {
		input->filter.formatted.vm_pool =
				(unsigned char)ntohl(fsp->h_ext.data[1]);
		mask.formatted.vm_pool =
				(unsigned char)ntohl(fsp->m_ext.data[1]);
		input->filter.formatted.vlan_id = fsp->h_ext.vlan_tci;
		mask.formatted.vlan_id = fsp->m_ext.vlan_tci;
		input->filter.formatted.flex_bytes =
						fsp->h_ext.vlan_etype;
		mask.formatted.flex_bytes = fsp->m_ext.vlan_etype;
	}

	/* determine if we need to drop or route the packet */
	if (fsp->ring_cookie == RX_CLS_FLOW_DISC)
		input->action = IXGBE_FDIR_DROP_QUEUE;
	else
		input->action = fsp->ring_cookie;

	spin_lock(&adapter->fdir_perfect_lock);

	if (hlist_empty(&adapter->fdir_filter_list)) {
		/* save mask and program input mask into HW */
		memcpy(&adapter->fdir_mask, &mask, sizeof(mask));
		err = ixgbe_fdir_set_input_mask_82599(hw, &mask);
		if (err) {
			e_err(drv, "Error writing mask\n");
			goto err_out_w_lock;
		}
	} else if (memcmp(&adapter->fdir_mask, &mask, sizeof(mask))) {
		e_err(drv, "Only one mask supported per port\n");
		goto err_out_w_lock;
	}

	/* apply mask and compute/store hash */
	ixgbe_atr_compute_perfect_hash_82599(&input->filter, &mask);

	/* program filters to filter memory */
	err = ixgbe_fdir_write_perfect_filter_82599(hw,
				&input->filter, input->sw_idx,
				(input->action == IXGBE_FDIR_DROP_QUEUE) ?
				IXGBE_FDIR_DROP_QUEUE :
				adapter->rx_ring[input->action]->reg_idx);
	if (err)
		goto err_out_w_lock;

	ixgbe_update_ethtool_fdir_entry(adapter, input, input->sw_idx);

	spin_unlock(&adapter->fdir_perfect_lock);

	return err;
err_out_w_lock:
	spin_unlock(&adapter->fdir_perfect_lock);
err_out:
	kfree(input);
	return -EINVAL;
}

static int ixgbe_del_ethtool_fdir_entry(struct ixgbe_adapter *adapter,
					struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	int err;

	spin_lock(&adapter->fdir_perfect_lock);
	err = ixgbe_update_ethtool_fdir_entry(adapter, NULL, fsp->location);
	spin_unlock(&adapter->fdir_perfect_lock);

	return err;
}

#define UDP_RSS_FLAGS (IXGBE_FLAG2_RSS_FIELD_IPV4_UDP | \
		       IXGBE_FLAG2_RSS_FIELD_IPV6_UDP)
static int ixgbe_set_rss_hash_opt(struct ixgbe_adapter *adapter,
				  struct ethtool_rxnfc *nfc)
{
	u32 flags2 = adapter->flags2;

	/*
	 * RSS does not support anything other than hashing
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
			flags2 &= ~IXGBE_FLAG2_RSS_FIELD_IPV4_UDP;
			break;
		case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
			flags2 |= IXGBE_FLAG2_RSS_FIELD_IPV4_UDP;
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
			flags2 &= ~IXGBE_FLAG2_RSS_FIELD_IPV6_UDP;
			break;
		case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
			flags2 |= IXGBE_FLAG2_RSS_FIELD_IPV6_UDP;
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
	if (flags2 != adapter->flags2) {
		struct ixgbe_hw *hw = &adapter->hw;
		u32 mrqc = IXGBE_READ_REG(hw, IXGBE_MRQC);

		if ((flags2 & UDP_RSS_FLAGS) &&
		    !(adapter->flags2 & UDP_RSS_FLAGS))
			e_warn(drv, "enabling UDP RSS: fragmented packets may arrive out of order to the stack above\n");

		adapter->flags2 = flags2;

		/* Perform hash on these packet types */
		mrqc |= IXGBE_MRQC_RSS_FIELD_IPV4
		      | IXGBE_MRQC_RSS_FIELD_IPV4_TCP
		      | IXGBE_MRQC_RSS_FIELD_IPV6
		      | IXGBE_MRQC_RSS_FIELD_IPV6_TCP;

		mrqc &= ~(IXGBE_MRQC_RSS_FIELD_IPV4_UDP |
			  IXGBE_MRQC_RSS_FIELD_IPV6_UDP);

		if (flags2 & IXGBE_FLAG2_RSS_FIELD_IPV4_UDP)
			mrqc |= IXGBE_MRQC_RSS_FIELD_IPV4_UDP;

		if (flags2 & IXGBE_FLAG2_RSS_FIELD_IPV6_UDP)
			mrqc |= IXGBE_MRQC_RSS_FIELD_IPV6_UDP;

		IXGBE_WRITE_REG(hw, IXGBE_MRQC, mrqc);
	}

	return 0;
}

static int ixgbe_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		ret = ixgbe_add_ethtool_fdir_entry(adapter, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		ret = ixgbe_del_ethtool_fdir_entry(adapter, cmd);
		break;
	case ETHTOOL_SRXFH:
		ret = ixgbe_set_rss_hash_opt(adapter, cmd);
		break;
	default:
		break;
	}

	return ret;
}

static int ixgbe_get_ts_info(struct net_device *dev,
			     struct ethtool_ts_info *info)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);

	switch (adapter->hw.mac.type) {
	case ixgbe_mac_X540:
	case ixgbe_mac_82599EB:
		info->so_timestamping =
			SOF_TIMESTAMPING_TX_SOFTWARE |
			SOF_TIMESTAMPING_RX_SOFTWARE |
			SOF_TIMESTAMPING_SOFTWARE |
			SOF_TIMESTAMPING_TX_HARDWARE |
			SOF_TIMESTAMPING_RX_HARDWARE |
			SOF_TIMESTAMPING_RAW_HARDWARE;

		if (adapter->ptp_clock)
			info->phc_index = ptp_clock_index(adapter->ptp_clock);
		else
			info->phc_index = -1;

		info->tx_types =
			(1 << HWTSTAMP_TX_OFF) |
			(1 << HWTSTAMP_TX_ON);

		info->rx_filters =
			(1 << HWTSTAMP_FILTER_NONE) |
			(1 << HWTSTAMP_FILTER_PTP_V1_L4_SYNC) |
			(1 << HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ) |
			(1 << HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
			(1 << HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
			(1 << HWTSTAMP_FILTER_PTP_V2_SYNC) |
			(1 << HWTSTAMP_FILTER_PTP_V2_L2_SYNC) |
			(1 << HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
			(1 << HWTSTAMP_FILTER_PTP_V2_DELAY_REQ) |
			(1 << HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ) |
			(1 << HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ) |
			(1 << HWTSTAMP_FILTER_PTP_V2_EVENT);
		break;
	default:
		return ethtool_op_get_ts_info(dev, info);
		break;
	}
	return 0;
}

static unsigned int ixgbe_max_channels(struct ixgbe_adapter *adapter)
{
	unsigned int max_combined;
	u8 tcs = netdev_get_num_tc(adapter->netdev);

	if (!(adapter->flags & IXGBE_FLAG_MSIX_ENABLED)) {
		/* We only support one q_vector without MSI-X */
		max_combined = 1;
	} else if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED) {
		/* SR-IOV currently only allows one queue on the PF */
		max_combined = 1;
	} else if (tcs > 1) {
		/* For DCB report channels per traffic class */
		if (adapter->hw.mac.type == ixgbe_mac_82598EB) {
			/* 8 TC w/ 4 queues per TC */
			max_combined = 4;
		} else if (tcs > 4) {
			/* 8 TC w/ 8 queues per TC */
			max_combined = 8;
		} else {
			/* 4 TC w/ 16 queues per TC */
			max_combined = 16;
		}
	} else if (adapter->atr_sample_rate) {
		/* support up to 64 queues with ATR */
		max_combined = IXGBE_MAX_FDIR_INDICES;
	} else {
		/* support up to 16 queues with RSS */
		max_combined = IXGBE_MAX_RSS_INDICES;
	}

	return max_combined;
}

static void ixgbe_get_channels(struct net_device *dev,
			       struct ethtool_channels *ch)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);

	/* report maximum channels */
	ch->max_combined = ixgbe_max_channels(adapter);

	/* report info for other vector */
	if (adapter->flags & IXGBE_FLAG_MSIX_ENABLED) {
		ch->max_other = NON_Q_VECTORS;
		ch->other_count = NON_Q_VECTORS;
	}

	/* record RSS queues */
	ch->combined_count = adapter->ring_feature[RING_F_RSS].indices;

	/* nothing else to report if RSS is disabled */
	if (ch->combined_count == 1)
		return;

	/* we do not support ATR queueing if SR-IOV is enabled */
	if (adapter->flags & IXGBE_FLAG_SRIOV_ENABLED)
		return;

	/* same thing goes for being DCB enabled */
	if (netdev_get_num_tc(dev) > 1)
		return;

	/* if ATR is disabled we can exit */
	if (!adapter->atr_sample_rate)
		return;

	/* report flow director queues as maximum channels */
	ch->combined_count = adapter->ring_feature[RING_F_FDIR].indices;
}

static int ixgbe_set_channels(struct net_device *dev,
			      struct ethtool_channels *ch)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	unsigned int count = ch->combined_count;

	/* verify they are not requesting separate vectors */
	if (!count || ch->rx_count || ch->tx_count)
		return -EINVAL;

	/* verify other_count has not changed */
	if (ch->other_count != NON_Q_VECTORS)
		return -EINVAL;

	/* verify the number of channels does not exceed hardware limits */
	if (count > ixgbe_max_channels(adapter))
		return -EINVAL;

	/* update feature limits from largest to smallest supported values */
	adapter->ring_feature[RING_F_FDIR].limit = count;

	/* cap RSS limit at 16 */
	if (count > IXGBE_MAX_RSS_INDICES)
		count = IXGBE_MAX_RSS_INDICES;
	adapter->ring_feature[RING_F_RSS].limit = count;

#ifdef IXGBE_FCOE
	/* cap FCoE limit at 8 */
	if (count > IXGBE_FCRETA_SIZE)
		count = IXGBE_FCRETA_SIZE;
	adapter->ring_feature[RING_F_FCOE].limit = count;

#endif
	/* use setup TC to update any traffic class queue mapping */
	return ixgbe_setup_tc(dev, netdev_get_num_tc(dev));
}

static int ixgbe_get_module_info(struct net_device *dev,
				       struct ethtool_modinfo *modinfo)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 status;
	u8 sff8472_rev, addr_mode;
	bool page_swap = false;

	/* Check whether we support SFF-8472 or not */
	status = hw->phy.ops.read_i2c_eeprom(hw,
					     IXGBE_SFF_SFF_8472_COMP,
					     &sff8472_rev);
	if (status != 0)
		return -EIO;

	/* addressing mode is not supported */
	status = hw->phy.ops.read_i2c_eeprom(hw,
					     IXGBE_SFF_SFF_8472_SWAP,
					     &addr_mode);
	if (status != 0)
		return -EIO;

	if (addr_mode & IXGBE_SFF_ADDRESSING_MODE) {
		e_err(drv, "Address change required to access page 0xA2, but not supported. Please report the module type to the driver maintainers.\n");
		page_swap = true;
	}

	if (sff8472_rev == IXGBE_SFF_SFF_8472_UNSUP || page_swap) {
		/* We have a SFP, but it does not support SFF-8472 */
		modinfo->type = ETH_MODULE_SFF_8079;
		modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
	} else {
		/* We have a SFP which supports a revision of SFF-8472. */
		modinfo->type = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
	}

	return 0;
}

static int ixgbe_get_module_eeprom(struct net_device *dev,
					 struct ethtool_eeprom *ee,
					 u8 *data)
{
	struct ixgbe_adapter *adapter = netdev_priv(dev);
	struct ixgbe_hw *hw = &adapter->hw;
	u32 status = IXGBE_ERR_PHY_ADDR_INVALID;
	u8 databyte = 0xFF;
	int i = 0;

	if (ee->len == 0)
		return -EINVAL;

	for (i = ee->offset; i < ee->offset + ee->len; i++) {
		/* I2C reads can take long time */
		if (test_bit(__IXGBE_IN_SFP_INIT, &adapter->state))
			return -EBUSY;

		if (i < ETH_MODULE_SFF_8079_LEN)
			status = hw->phy.ops.read_i2c_eeprom(hw, i, &databyte);
		else
			status = hw->phy.ops.read_i2c_sff8472(hw, i, &databyte);

		if (status != 0)
			return -EIO;

		data[i - ee->offset] = databyte;
	}

	return 0;
}

static const struct ethtool_ops ixgbe_ethtool_ops = {
	.get_settings           = ixgbe_get_settings,
	.set_settings           = ixgbe_set_settings,
	.get_drvinfo            = ixgbe_get_drvinfo,
	.get_regs_len           = ixgbe_get_regs_len,
	.get_regs               = ixgbe_get_regs,
	.get_wol                = ixgbe_get_wol,
	.set_wol                = ixgbe_set_wol,
	.nway_reset             = ixgbe_nway_reset,
	.get_link               = ethtool_op_get_link,
	.get_eeprom_len         = ixgbe_get_eeprom_len,
	.get_eeprom             = ixgbe_get_eeprom,
	.set_eeprom             = ixgbe_set_eeprom,
	.get_ringparam          = ixgbe_get_ringparam,
	.set_ringparam          = ixgbe_set_ringparam,
	.get_pauseparam         = ixgbe_get_pauseparam,
	.set_pauseparam         = ixgbe_set_pauseparam,
	.get_msglevel           = ixgbe_get_msglevel,
	.set_msglevel           = ixgbe_set_msglevel,
	.self_test              = ixgbe_diag_test,
	.get_strings            = ixgbe_get_strings,
	.set_phys_id            = ixgbe_set_phys_id,
	.get_sset_count         = ixgbe_get_sset_count,
	.get_ethtool_stats      = ixgbe_get_ethtool_stats,
	.get_coalesce           = ixgbe_get_coalesce,
	.set_coalesce           = ixgbe_set_coalesce,
	.get_rxnfc		= ixgbe_get_rxnfc,
	.set_rxnfc		= ixgbe_set_rxnfc,
	.get_channels		= ixgbe_get_channels,
	.set_channels		= ixgbe_set_channels,
	.get_ts_info		= ixgbe_get_ts_info,
	.get_module_info	= ixgbe_get_module_info,
	.get_module_eeprom	= ixgbe_get_module_eeprom,
};

void ixgbe_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &ixgbe_ethtool_ops;
}
