/* bnx2x_ethtool.c: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Eliezer Tamir
 * Based on code from Michael Chan's bnx2 driver
 * UDP CSUM errata workaround by Arik Gendelman
 * Slowpath and fastpath rework by Vladislav Zolotarov
 * Statistics and Link management by Yitchak Gertner
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/crc32.h>
#include "bnx2x.h"
#include "bnx2x_cmn.h"
#include "bnx2x_dump.h"
#include "bnx2x_init.h"

/* Note: in the format strings below %s is replaced by the queue-name which is
 * either its index or 'fcoe' for the fcoe queue. Make sure the format string
 * length does not exceed ETH_GSTRING_LEN - MAX_QUEUE_NAME_LEN + 2
 */
#define MAX_QUEUE_NAME_LEN	4
static const struct {
	long offset;
	int size;
	char string[ETH_GSTRING_LEN];
} bnx2x_q_stats_arr[] = {
/* 1 */	{ Q_STATS_OFFSET32(total_bytes_received_hi), 8, "[%s]: rx_bytes" },
	{ Q_STATS_OFFSET32(total_unicast_packets_received_hi),
						8, "[%s]: rx_ucast_packets" },
	{ Q_STATS_OFFSET32(total_multicast_packets_received_hi),
						8, "[%s]: rx_mcast_packets" },
	{ Q_STATS_OFFSET32(total_broadcast_packets_received_hi),
						8, "[%s]: rx_bcast_packets" },
	{ Q_STATS_OFFSET32(no_buff_discard_hi),	8, "[%s]: rx_discards" },
	{ Q_STATS_OFFSET32(rx_err_discard_pkt),
					 4, "[%s]: rx_phy_ip_err_discards"},
	{ Q_STATS_OFFSET32(rx_skb_alloc_failed),
					 4, "[%s]: rx_skb_alloc_discard" },
	{ Q_STATS_OFFSET32(hw_csum_err), 4, "[%s]: rx_csum_offload_errors" },

	{ Q_STATS_OFFSET32(total_bytes_transmitted_hi),	8, "[%s]: tx_bytes" },
/* 10 */{ Q_STATS_OFFSET32(total_unicast_packets_transmitted_hi),
						8, "[%s]: tx_ucast_packets" },
	{ Q_STATS_OFFSET32(total_multicast_packets_transmitted_hi),
						8, "[%s]: tx_mcast_packets" },
	{ Q_STATS_OFFSET32(total_broadcast_packets_transmitted_hi),
						8, "[%s]: tx_bcast_packets" },
	{ Q_STATS_OFFSET32(total_tpa_aggregations_hi),
						8, "[%s]: tpa_aggregations" },
	{ Q_STATS_OFFSET32(total_tpa_aggregated_frames_hi),
					8, "[%s]: tpa_aggregated_frames"},
	{ Q_STATS_OFFSET32(total_tpa_bytes_hi),	8, "[%s]: tpa_bytes"},
	{ Q_STATS_OFFSET32(driver_filtered_tx_pkt),
					4, "[%s]: driver_filtered_tx_pkt" }
};

#define BNX2X_NUM_Q_STATS ARRAY_SIZE(bnx2x_q_stats_arr)

static const struct {
	long offset;
	int size;
	u32 flags;
#define STATS_FLAGS_PORT		1
#define STATS_FLAGS_FUNC		2
#define STATS_FLAGS_BOTH		(STATS_FLAGS_FUNC | STATS_FLAGS_PORT)
	char string[ETH_GSTRING_LEN];
} bnx2x_stats_arr[] = {
/* 1 */	{ STATS_OFFSET32(total_bytes_received_hi),
				8, STATS_FLAGS_BOTH, "rx_bytes" },
	{ STATS_OFFSET32(error_bytes_received_hi),
				8, STATS_FLAGS_BOTH, "rx_error_bytes" },
	{ STATS_OFFSET32(total_unicast_packets_received_hi),
				8, STATS_FLAGS_BOTH, "rx_ucast_packets" },
	{ STATS_OFFSET32(total_multicast_packets_received_hi),
				8, STATS_FLAGS_BOTH, "rx_mcast_packets" },
	{ STATS_OFFSET32(total_broadcast_packets_received_hi),
				8, STATS_FLAGS_BOTH, "rx_bcast_packets" },
	{ STATS_OFFSET32(rx_stat_dot3statsfcserrors_hi),
				8, STATS_FLAGS_PORT, "rx_crc_errors" },
	{ STATS_OFFSET32(rx_stat_dot3statsalignmenterrors_hi),
				8, STATS_FLAGS_PORT, "rx_align_errors" },
	{ STATS_OFFSET32(rx_stat_etherstatsundersizepkts_hi),
				8, STATS_FLAGS_PORT, "rx_undersize_packets" },
	{ STATS_OFFSET32(etherstatsoverrsizepkts_hi),
				8, STATS_FLAGS_PORT, "rx_oversize_packets" },
/* 10 */{ STATS_OFFSET32(rx_stat_etherstatsfragments_hi),
				8, STATS_FLAGS_PORT, "rx_fragments" },
	{ STATS_OFFSET32(rx_stat_etherstatsjabbers_hi),
				8, STATS_FLAGS_PORT, "rx_jabbers" },
	{ STATS_OFFSET32(no_buff_discard_hi),
				8, STATS_FLAGS_BOTH, "rx_discards" },
	{ STATS_OFFSET32(mac_filter_discard),
				4, STATS_FLAGS_PORT, "rx_filtered_packets" },
	{ STATS_OFFSET32(mf_tag_discard),
				4, STATS_FLAGS_PORT, "rx_mf_tag_discard" },
	{ STATS_OFFSET32(pfc_frames_received_hi),
				8, STATS_FLAGS_PORT, "pfc_frames_received" },
	{ STATS_OFFSET32(pfc_frames_sent_hi),
				8, STATS_FLAGS_PORT, "pfc_frames_sent" },
	{ STATS_OFFSET32(brb_drop_hi),
				8, STATS_FLAGS_PORT, "rx_brb_discard" },
	{ STATS_OFFSET32(brb_truncate_hi),
				8, STATS_FLAGS_PORT, "rx_brb_truncate" },
	{ STATS_OFFSET32(pause_frames_received_hi),
				8, STATS_FLAGS_PORT, "rx_pause_frames" },
	{ STATS_OFFSET32(rx_stat_maccontrolframesreceived_hi),
				8, STATS_FLAGS_PORT, "rx_mac_ctrl_frames" },
	{ STATS_OFFSET32(nig_timer_max),
			4, STATS_FLAGS_PORT, "rx_constant_pause_events" },
/* 20 */{ STATS_OFFSET32(rx_err_discard_pkt),
				4, STATS_FLAGS_BOTH, "rx_phy_ip_err_discards"},
	{ STATS_OFFSET32(rx_skb_alloc_failed),
				4, STATS_FLAGS_BOTH, "rx_skb_alloc_discard" },
	{ STATS_OFFSET32(hw_csum_err),
				4, STATS_FLAGS_BOTH, "rx_csum_offload_errors" },

	{ STATS_OFFSET32(total_bytes_transmitted_hi),
				8, STATS_FLAGS_BOTH, "tx_bytes" },
	{ STATS_OFFSET32(tx_stat_ifhcoutbadoctets_hi),
				8, STATS_FLAGS_PORT, "tx_error_bytes" },
	{ STATS_OFFSET32(total_unicast_packets_transmitted_hi),
				8, STATS_FLAGS_BOTH, "tx_ucast_packets" },
	{ STATS_OFFSET32(total_multicast_packets_transmitted_hi),
				8, STATS_FLAGS_BOTH, "tx_mcast_packets" },
	{ STATS_OFFSET32(total_broadcast_packets_transmitted_hi),
				8, STATS_FLAGS_BOTH, "tx_bcast_packets" },
	{ STATS_OFFSET32(tx_stat_dot3statsinternalmactransmiterrors_hi),
				8, STATS_FLAGS_PORT, "tx_mac_errors" },
	{ STATS_OFFSET32(rx_stat_dot3statscarriersenseerrors_hi),
				8, STATS_FLAGS_PORT, "tx_carrier_errors" },
/* 30 */{ STATS_OFFSET32(tx_stat_dot3statssinglecollisionframes_hi),
				8, STATS_FLAGS_PORT, "tx_single_collisions" },
	{ STATS_OFFSET32(tx_stat_dot3statsmultiplecollisionframes_hi),
				8, STATS_FLAGS_PORT, "tx_multi_collisions" },
	{ STATS_OFFSET32(tx_stat_dot3statsdeferredtransmissions_hi),
				8, STATS_FLAGS_PORT, "tx_deferred" },
	{ STATS_OFFSET32(tx_stat_dot3statsexcessivecollisions_hi),
				8, STATS_FLAGS_PORT, "tx_excess_collisions" },
	{ STATS_OFFSET32(tx_stat_dot3statslatecollisions_hi),
				8, STATS_FLAGS_PORT, "tx_late_collisions" },
	{ STATS_OFFSET32(tx_stat_etherstatscollisions_hi),
				8, STATS_FLAGS_PORT, "tx_total_collisions" },
	{ STATS_OFFSET32(tx_stat_etherstatspkts64octets_hi),
				8, STATS_FLAGS_PORT, "tx_64_byte_packets" },
	{ STATS_OFFSET32(tx_stat_etherstatspkts65octetsto127octets_hi),
			8, STATS_FLAGS_PORT, "tx_65_to_127_byte_packets" },
	{ STATS_OFFSET32(tx_stat_etherstatspkts128octetsto255octets_hi),
			8, STATS_FLAGS_PORT, "tx_128_to_255_byte_packets" },
	{ STATS_OFFSET32(tx_stat_etherstatspkts256octetsto511octets_hi),
			8, STATS_FLAGS_PORT, "tx_256_to_511_byte_packets" },
/* 40 */{ STATS_OFFSET32(tx_stat_etherstatspkts512octetsto1023octets_hi),
			8, STATS_FLAGS_PORT, "tx_512_to_1023_byte_packets" },
	{ STATS_OFFSET32(etherstatspkts1024octetsto1522octets_hi),
			8, STATS_FLAGS_PORT, "tx_1024_to_1522_byte_packets" },
	{ STATS_OFFSET32(etherstatspktsover1522octets_hi),
			8, STATS_FLAGS_PORT, "tx_1523_to_9022_byte_packets" },
	{ STATS_OFFSET32(pause_frames_sent_hi),
				8, STATS_FLAGS_PORT, "tx_pause_frames" },
	{ STATS_OFFSET32(total_tpa_aggregations_hi),
			8, STATS_FLAGS_FUNC, "tpa_aggregations" },
	{ STATS_OFFSET32(total_tpa_aggregated_frames_hi),
			8, STATS_FLAGS_FUNC, "tpa_aggregated_frames"},
	{ STATS_OFFSET32(total_tpa_bytes_hi),
			8, STATS_FLAGS_FUNC, "tpa_bytes"},
	{ STATS_OFFSET32(recoverable_error),
			4, STATS_FLAGS_FUNC, "recoverable_errors" },
	{ STATS_OFFSET32(unrecoverable_error),
			4, STATS_FLAGS_FUNC, "unrecoverable_errors" },
	{ STATS_OFFSET32(driver_filtered_tx_pkt),
			4, STATS_FLAGS_FUNC, "driver_filtered_tx_pkt" },
	{ STATS_OFFSET32(eee_tx_lpi),
			4, STATS_FLAGS_PORT, "Tx LPI entry count"}
};

#define BNX2X_NUM_STATS		ARRAY_SIZE(bnx2x_stats_arr)

static int bnx2x_get_port_type(struct bnx2x *bp)
{
	int port_type;
	u32 phy_idx = bnx2x_get_cur_phy_idx(bp);
	switch (bp->link_params.phy[phy_idx].media_type) {
	case ETH_PHY_SFPP_10G_FIBER:
	case ETH_PHY_SFP_1G_FIBER:
	case ETH_PHY_XFP_FIBER:
	case ETH_PHY_KR:
	case ETH_PHY_CX4:
		port_type = PORT_FIBRE;
		break;
	case ETH_PHY_DA_TWINAX:
		port_type = PORT_DA;
		break;
	case ETH_PHY_BASE_T:
		port_type = PORT_TP;
		break;
	case ETH_PHY_NOT_PRESENT:
		port_type = PORT_NONE;
		break;
	case ETH_PHY_UNSPECIFIED:
	default:
		port_type = PORT_OTHER;
		break;
	}
	return port_type;
}

static int bnx2x_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bnx2x *bp = netdev_priv(dev);
	int cfg_idx = bnx2x_get_link_cfg_idx(bp);

	/* Dual Media boards present all available port types */
	cmd->supported = bp->port.supported[cfg_idx] |
		(bp->port.supported[cfg_idx ^ 1] &
		 (SUPPORTED_TP | SUPPORTED_FIBRE));
	cmd->advertising = bp->port.advertising[cfg_idx];
	if (bp->link_params.phy[bnx2x_get_cur_phy_idx(bp)].media_type ==
	    ETH_PHY_SFP_1G_FIBER) {
		cmd->supported &= ~(SUPPORTED_10000baseT_Full);
		cmd->advertising &= ~(ADVERTISED_10000baseT_Full);
	}

	if ((bp->state == BNX2X_STATE_OPEN) && bp->link_vars.link_up &&
	    !(bp->flags & MF_FUNC_DIS)) {
		cmd->duplex = bp->link_vars.duplex;

		if (IS_MF(bp) && !BP_NOMCP(bp))
			ethtool_cmd_speed_set(cmd, bnx2x_get_mf_speed(bp));
		else
			ethtool_cmd_speed_set(cmd, bp->link_vars.line_speed);
	} else {
		cmd->duplex = DUPLEX_UNKNOWN;
		ethtool_cmd_speed_set(cmd, SPEED_UNKNOWN);
	}

	cmd->port = bnx2x_get_port_type(bp);

	cmd->phy_address = bp->mdio.prtad;
	cmd->transceiver = XCVR_INTERNAL;

	if (bp->link_params.req_line_speed[cfg_idx] == SPEED_AUTO_NEG)
		cmd->autoneg = AUTONEG_ENABLE;
	else
		cmd->autoneg = AUTONEG_DISABLE;

	/* Publish LP advertised speeds and FC */
	if (bp->link_vars.link_status & LINK_STATUS_AUTO_NEGOTIATE_COMPLETE) {
		u32 status = bp->link_vars.link_status;

		cmd->lp_advertising |= ADVERTISED_Autoneg;
		if (status & LINK_STATUS_LINK_PARTNER_SYMMETRIC_PAUSE)
			cmd->lp_advertising |= ADVERTISED_Pause;
		if (status & LINK_STATUS_LINK_PARTNER_ASYMMETRIC_PAUSE)
			cmd->lp_advertising |= ADVERTISED_Asym_Pause;

		if (status & LINK_STATUS_LINK_PARTNER_10THD_CAPABLE)
			cmd->lp_advertising |= ADVERTISED_10baseT_Half;
		if (status & LINK_STATUS_LINK_PARTNER_10TFD_CAPABLE)
			cmd->lp_advertising |= ADVERTISED_10baseT_Full;
		if (status & LINK_STATUS_LINK_PARTNER_100TXHD_CAPABLE)
			cmd->lp_advertising |= ADVERTISED_100baseT_Half;
		if (status & LINK_STATUS_LINK_PARTNER_100TXFD_CAPABLE)
			cmd->lp_advertising |= ADVERTISED_100baseT_Full;
		if (status & LINK_STATUS_LINK_PARTNER_1000THD_CAPABLE)
			cmd->lp_advertising |= ADVERTISED_1000baseT_Half;
		if (status & LINK_STATUS_LINK_PARTNER_1000TFD_CAPABLE)
			cmd->lp_advertising |= ADVERTISED_1000baseT_Full;
		if (status & LINK_STATUS_LINK_PARTNER_2500XFD_CAPABLE)
			cmd->lp_advertising |= ADVERTISED_2500baseX_Full;
		if (status & LINK_STATUS_LINK_PARTNER_10GXFD_CAPABLE)
			cmd->lp_advertising |= ADVERTISED_10000baseT_Full;
		if (status & LINK_STATUS_LINK_PARTNER_20GXFD_CAPABLE)
			cmd->lp_advertising |= ADVERTISED_20000baseKR2_Full;
	}

	cmd->maxtxpkt = 0;
	cmd->maxrxpkt = 0;

	DP(BNX2X_MSG_ETHTOOL, "ethtool_cmd: cmd %d\n"
	   "  supported 0x%x  advertising 0x%x  speed %u\n"
	   "  duplex %d  port %d  phy_address %d  transceiver %d\n"
	   "  autoneg %d  maxtxpkt %d  maxrxpkt %d\n",
	   cmd->cmd, cmd->supported, cmd->advertising,
	   ethtool_cmd_speed(cmd),
	   cmd->duplex, cmd->port, cmd->phy_address, cmd->transceiver,
	   cmd->autoneg, cmd->maxtxpkt, cmd->maxrxpkt);

	return 0;
}

static int bnx2x_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 advertising, cfg_idx, old_multi_phy_config, new_multi_phy_config;
	u32 speed, phy_idx;

	if (IS_MF_SD(bp))
		return 0;

	DP(BNX2X_MSG_ETHTOOL, "ethtool_cmd: cmd %d\n"
	   "  supported 0x%x  advertising 0x%x  speed %u\n"
	   "  duplex %d  port %d  phy_address %d  transceiver %d\n"
	   "  autoneg %d  maxtxpkt %d  maxrxpkt %d\n",
	   cmd->cmd, cmd->supported, cmd->advertising,
	   ethtool_cmd_speed(cmd),
	   cmd->duplex, cmd->port, cmd->phy_address, cmd->transceiver,
	   cmd->autoneg, cmd->maxtxpkt, cmd->maxrxpkt);

	speed = ethtool_cmd_speed(cmd);

	/* If received a request for an unknown duplex, assume full*/
	if (cmd->duplex == DUPLEX_UNKNOWN)
		cmd->duplex = DUPLEX_FULL;

	if (IS_MF_SI(bp)) {
		u32 part;
		u32 line_speed = bp->link_vars.line_speed;

		/* use 10G if no link detected */
		if (!line_speed)
			line_speed = 10000;

		if (bp->common.bc_ver < REQ_BC_VER_4_SET_MF_BW) {
			DP(BNX2X_MSG_ETHTOOL,
			   "To set speed BC %X or higher is required, please upgrade BC\n",
			   REQ_BC_VER_4_SET_MF_BW);
			return -EINVAL;
		}

		part = (speed * 100) / line_speed;

		if (line_speed < speed || !part) {
			DP(BNX2X_MSG_ETHTOOL,
			   "Speed setting should be in a range from 1%% to 100%% of actual line speed\n");
			return -EINVAL;
		}

		if (bp->state != BNX2X_STATE_OPEN)
			/* store value for following "load" */
			bp->pending_max = part;
		else
			bnx2x_update_max_mf_config(bp, part);

		return 0;
	}

	cfg_idx = bnx2x_get_link_cfg_idx(bp);
	old_multi_phy_config = bp->link_params.multi_phy_config;
	switch (cmd->port) {
	case PORT_TP:
		if (bp->port.supported[cfg_idx] & SUPPORTED_TP)
			break; /* no port change */

		if (!(bp->port.supported[0] & SUPPORTED_TP ||
		      bp->port.supported[1] & SUPPORTED_TP)) {
			DP(BNX2X_MSG_ETHTOOL, "Unsupported port type\n");
			return -EINVAL;
		}
		bp->link_params.multi_phy_config &=
			~PORT_HW_CFG_PHY_SELECTION_MASK;
		if (bp->link_params.multi_phy_config &
		    PORT_HW_CFG_PHY_SWAPPED_ENABLED)
			bp->link_params.multi_phy_config |=
			PORT_HW_CFG_PHY_SELECTION_SECOND_PHY;
		else
			bp->link_params.multi_phy_config |=
			PORT_HW_CFG_PHY_SELECTION_FIRST_PHY;
		break;
	case PORT_FIBRE:
	case PORT_DA:
		if (bp->port.supported[cfg_idx] & SUPPORTED_FIBRE)
			break; /* no port change */

		if (!(bp->port.supported[0] & SUPPORTED_FIBRE ||
		      bp->port.supported[1] & SUPPORTED_FIBRE)) {
			DP(BNX2X_MSG_ETHTOOL, "Unsupported port type\n");
			return -EINVAL;
		}
		bp->link_params.multi_phy_config &=
			~PORT_HW_CFG_PHY_SELECTION_MASK;
		if (bp->link_params.multi_phy_config &
		    PORT_HW_CFG_PHY_SWAPPED_ENABLED)
			bp->link_params.multi_phy_config |=
			PORT_HW_CFG_PHY_SELECTION_FIRST_PHY;
		else
			bp->link_params.multi_phy_config |=
			PORT_HW_CFG_PHY_SELECTION_SECOND_PHY;
		break;
	default:
		DP(BNX2X_MSG_ETHTOOL, "Unsupported port type\n");
		return -EINVAL;
	}
	/* Save new config in case command complete successfully */
	new_multi_phy_config = bp->link_params.multi_phy_config;
	/* Get the new cfg_idx */
	cfg_idx = bnx2x_get_link_cfg_idx(bp);
	/* Restore old config in case command failed */
	bp->link_params.multi_phy_config = old_multi_phy_config;
	DP(BNX2X_MSG_ETHTOOL, "cfg_idx = %x\n", cfg_idx);

	if (cmd->autoneg == AUTONEG_ENABLE) {
		u32 an_supported_speed = bp->port.supported[cfg_idx];
		if (bp->link_params.phy[EXT_PHY1].type ==
		    PORT_HW_CFG_XGXS_EXT_PHY_TYPE_BCM84833)
			an_supported_speed |= (SUPPORTED_100baseT_Half |
					       SUPPORTED_100baseT_Full);
		if (!(bp->port.supported[cfg_idx] & SUPPORTED_Autoneg)) {
			DP(BNX2X_MSG_ETHTOOL, "Autoneg not supported\n");
			return -EINVAL;
		}

		/* advertise the requested speed and duplex if supported */
		if (cmd->advertising & ~an_supported_speed) {
			DP(BNX2X_MSG_ETHTOOL,
			   "Advertisement parameters are not supported\n");
			return -EINVAL;
		}

		bp->link_params.req_line_speed[cfg_idx] = SPEED_AUTO_NEG;
		bp->link_params.req_duplex[cfg_idx] = cmd->duplex;
		bp->port.advertising[cfg_idx] = (ADVERTISED_Autoneg |
					 cmd->advertising);
		if (cmd->advertising) {

			bp->link_params.speed_cap_mask[cfg_idx] = 0;
			if (cmd->advertising & ADVERTISED_10baseT_Half) {
				bp->link_params.speed_cap_mask[cfg_idx] |=
				PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_HALF;
			}
			if (cmd->advertising & ADVERTISED_10baseT_Full)
				bp->link_params.speed_cap_mask[cfg_idx] |=
				PORT_HW_CFG_SPEED_CAPABILITY_D0_10M_FULL;

			if (cmd->advertising & ADVERTISED_100baseT_Full)
				bp->link_params.speed_cap_mask[cfg_idx] |=
				PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_FULL;

			if (cmd->advertising & ADVERTISED_100baseT_Half) {
				bp->link_params.speed_cap_mask[cfg_idx] |=
				     PORT_HW_CFG_SPEED_CAPABILITY_D0_100M_HALF;
			}
			if (cmd->advertising & ADVERTISED_1000baseT_Half) {
				bp->link_params.speed_cap_mask[cfg_idx] |=
					PORT_HW_CFG_SPEED_CAPABILITY_D0_1G;
			}
			if (cmd->advertising & (ADVERTISED_1000baseT_Full |
						ADVERTISED_1000baseKX_Full))
				bp->link_params.speed_cap_mask[cfg_idx] |=
					PORT_HW_CFG_SPEED_CAPABILITY_D0_1G;

			if (cmd->advertising & (ADVERTISED_10000baseT_Full |
						ADVERTISED_10000baseKX4_Full |
						ADVERTISED_10000baseKR_Full))
				bp->link_params.speed_cap_mask[cfg_idx] |=
					PORT_HW_CFG_SPEED_CAPABILITY_D0_10G;

			if (cmd->advertising & ADVERTISED_20000baseKR2_Full)
				bp->link_params.speed_cap_mask[cfg_idx] |=
					PORT_HW_CFG_SPEED_CAPABILITY_D0_20G;
		}
	} else { /* forced speed */
		/* advertise the requested speed and duplex if supported */
		switch (speed) {
		case SPEED_10:
			if (cmd->duplex == DUPLEX_FULL) {
				if (!(bp->port.supported[cfg_idx] &
				      SUPPORTED_10baseT_Full)) {
					DP(BNX2X_MSG_ETHTOOL,
					   "10M full not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_10baseT_Full |
					       ADVERTISED_TP);
			} else {
				if (!(bp->port.supported[cfg_idx] &
				      SUPPORTED_10baseT_Half)) {
					DP(BNX2X_MSG_ETHTOOL,
					   "10M half not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_10baseT_Half |
					       ADVERTISED_TP);
			}
			break;

		case SPEED_100:
			if (cmd->duplex == DUPLEX_FULL) {
				if (!(bp->port.supported[cfg_idx] &
						SUPPORTED_100baseT_Full)) {
					DP(BNX2X_MSG_ETHTOOL,
					   "100M full not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_100baseT_Full |
					       ADVERTISED_TP);
			} else {
				if (!(bp->port.supported[cfg_idx] &
						SUPPORTED_100baseT_Half)) {
					DP(BNX2X_MSG_ETHTOOL,
					   "100M half not supported\n");
					return -EINVAL;
				}

				advertising = (ADVERTISED_100baseT_Half |
					       ADVERTISED_TP);
			}
			break;

		case SPEED_1000:
			if (cmd->duplex != DUPLEX_FULL) {
				DP(BNX2X_MSG_ETHTOOL,
				   "1G half not supported\n");
				return -EINVAL;
			}

			if (!(bp->port.supported[cfg_idx] &
			      SUPPORTED_1000baseT_Full)) {
				DP(BNX2X_MSG_ETHTOOL,
				   "1G full not supported\n");
				return -EINVAL;
			}

			advertising = (ADVERTISED_1000baseT_Full |
				       ADVERTISED_TP);
			break;

		case SPEED_2500:
			if (cmd->duplex != DUPLEX_FULL) {
				DP(BNX2X_MSG_ETHTOOL,
				   "2.5G half not supported\n");
				return -EINVAL;
			}

			if (!(bp->port.supported[cfg_idx]
			      & SUPPORTED_2500baseX_Full)) {
				DP(BNX2X_MSG_ETHTOOL,
				   "2.5G full not supported\n");
				return -EINVAL;
			}

			advertising = (ADVERTISED_2500baseX_Full |
				       ADVERTISED_TP);
			break;

		case SPEED_10000:
			if (cmd->duplex != DUPLEX_FULL) {
				DP(BNX2X_MSG_ETHTOOL,
				   "10G half not supported\n");
				return -EINVAL;
			}
			phy_idx = bnx2x_get_cur_phy_idx(bp);
			if (!(bp->port.supported[cfg_idx]
			      & SUPPORTED_10000baseT_Full) ||
			    (bp->link_params.phy[phy_idx].media_type ==
			     ETH_PHY_SFP_1G_FIBER)) {
				DP(BNX2X_MSG_ETHTOOL,
				   "10G full not supported\n");
				return -EINVAL;
			}

			advertising = (ADVERTISED_10000baseT_Full |
				       ADVERTISED_FIBRE);
			break;

		default:
			DP(BNX2X_MSG_ETHTOOL, "Unsupported speed %u\n", speed);
			return -EINVAL;
		}

		bp->link_params.req_line_speed[cfg_idx] = speed;
		bp->link_params.req_duplex[cfg_idx] = cmd->duplex;
		bp->port.advertising[cfg_idx] = advertising;
	}

	DP(BNX2X_MSG_ETHTOOL, "req_line_speed %d\n"
	   "  req_duplex %d  advertising 0x%x\n",
	   bp->link_params.req_line_speed[cfg_idx],
	   bp->link_params.req_duplex[cfg_idx],
	   bp->port.advertising[cfg_idx]);

	/* Set new config */
	bp->link_params.multi_phy_config = new_multi_phy_config;
	if (netif_running(dev)) {
		bnx2x_stats_handle(bp, STATS_EVENT_STOP);
		bnx2x_link_set(bp);
	}

	return 0;
}

#define DUMP_ALL_PRESETS		0x1FFF
#define DUMP_MAX_PRESETS		13

static int __bnx2x_get_preset_regs_len(struct bnx2x *bp, u32 preset)
{
	if (CHIP_IS_E1(bp))
		return dump_num_registers[0][preset-1];
	else if (CHIP_IS_E1H(bp))
		return dump_num_registers[1][preset-1];
	else if (CHIP_IS_E2(bp))
		return dump_num_registers[2][preset-1];
	else if (CHIP_IS_E3A0(bp))
		return dump_num_registers[3][preset-1];
	else if (CHIP_IS_E3B0(bp))
		return dump_num_registers[4][preset-1];
	else
		return 0;
}

static int __bnx2x_get_regs_len(struct bnx2x *bp)
{
	u32 preset_idx;
	int regdump_len = 0;

	/* Calculate the total preset regs length */
	for (preset_idx = 1; preset_idx <= DUMP_MAX_PRESETS; preset_idx++)
		regdump_len += __bnx2x_get_preset_regs_len(bp, preset_idx);

	return regdump_len;
}

static int bnx2x_get_regs_len(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	int regdump_len = 0;

	regdump_len = __bnx2x_get_regs_len(bp);
	regdump_len *= 4;
	regdump_len += sizeof(struct dump_header);

	return regdump_len;
}

#define IS_E1_REG(chips)	((chips & DUMP_CHIP_E1) == DUMP_CHIP_E1)
#define IS_E1H_REG(chips)	((chips & DUMP_CHIP_E1H) == DUMP_CHIP_E1H)
#define IS_E2_REG(chips)	((chips & DUMP_CHIP_E2) == DUMP_CHIP_E2)
#define IS_E3A0_REG(chips)	((chips & DUMP_CHIP_E3A0) == DUMP_CHIP_E3A0)
#define IS_E3B0_REG(chips)	((chips & DUMP_CHIP_E3B0) == DUMP_CHIP_E3B0)

#define IS_REG_IN_PRESET(presets, idx)  \
		((presets & (1 << (idx-1))) == (1 << (idx-1)))

/******* Paged registers info selectors ********/
static const u32 *__bnx2x_get_page_addr_ar(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return page_vals_e2;
	else if (CHIP_IS_E3(bp))
		return page_vals_e3;
	else
		return NULL;
}

static u32 __bnx2x_get_page_reg_num(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return PAGE_MODE_VALUES_E2;
	else if (CHIP_IS_E3(bp))
		return PAGE_MODE_VALUES_E3;
	else
		return 0;
}

static const u32 *__bnx2x_get_page_write_ar(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return page_write_regs_e2;
	else if (CHIP_IS_E3(bp))
		return page_write_regs_e3;
	else
		return NULL;
}

static u32 __bnx2x_get_page_write_num(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return PAGE_WRITE_REGS_E2;
	else if (CHIP_IS_E3(bp))
		return PAGE_WRITE_REGS_E3;
	else
		return 0;
}

static const struct reg_addr *__bnx2x_get_page_read_ar(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return page_read_regs_e2;
	else if (CHIP_IS_E3(bp))
		return page_read_regs_e3;
	else
		return NULL;
}

static u32 __bnx2x_get_page_read_num(struct bnx2x *bp)
{
	if (CHIP_IS_E2(bp))
		return PAGE_READ_REGS_E2;
	else if (CHIP_IS_E3(bp))
		return PAGE_READ_REGS_E3;
	else
		return 0;
}

static bool bnx2x_is_reg_in_chip(struct bnx2x *bp,
				       const struct reg_addr *reg_info)
{
	if (CHIP_IS_E1(bp))
		return IS_E1_REG(reg_info->chips);
	else if (CHIP_IS_E1H(bp))
		return IS_E1H_REG(reg_info->chips);
	else if (CHIP_IS_E2(bp))
		return IS_E2_REG(reg_info->chips);
	else if (CHIP_IS_E3A0(bp))
		return IS_E3A0_REG(reg_info->chips);
	else if (CHIP_IS_E3B0(bp))
		return IS_E3B0_REG(reg_info->chips);
	else
		return false;
}

static bool bnx2x_is_wreg_in_chip(struct bnx2x *bp,
	const struct wreg_addr *wreg_info)
{
	if (CHIP_IS_E1(bp))
		return IS_E1_REG(wreg_info->chips);
	else if (CHIP_IS_E1H(bp))
		return IS_E1H_REG(wreg_info->chips);
	else if (CHIP_IS_E2(bp))
		return IS_E2_REG(wreg_info->chips);
	else if (CHIP_IS_E3A0(bp))
		return IS_E3A0_REG(wreg_info->chips);
	else if (CHIP_IS_E3B0(bp))
		return IS_E3B0_REG(wreg_info->chips);
	else
		return false;
}

/**
 * bnx2x_read_pages_regs - read "paged" registers
 *
 * @bp		device handle
 * @p		output buffer
 *
 * Reads "paged" memories: memories that may only be read by first writing to a
 * specific address ("write address") and then reading from a specific address
 * ("read address"). There may be more than one write address per "page" and
 * more than one read address per write address.
 */
static void bnx2x_read_pages_regs(struct bnx2x *bp, u32 *p, u32 preset)
{
	u32 i, j, k, n;

	/* addresses of the paged registers */
	const u32 *page_addr = __bnx2x_get_page_addr_ar(bp);
	/* number of paged registers */
	int num_pages = __bnx2x_get_page_reg_num(bp);
	/* write addresses */
	const u32 *write_addr = __bnx2x_get_page_write_ar(bp);
	/* number of write addresses */
	int write_num = __bnx2x_get_page_write_num(bp);
	/* read addresses info */
	const struct reg_addr *read_addr = __bnx2x_get_page_read_ar(bp);
	/* number of read addresses */
	int read_num = __bnx2x_get_page_read_num(bp);
	u32 addr, size;

	for (i = 0; i < num_pages; i++) {
		for (j = 0; j < write_num; j++) {
			REG_WR(bp, write_addr[j], page_addr[i]);

			for (k = 0; k < read_num; k++) {
				if (IS_REG_IN_PRESET(read_addr[k].presets,
						     preset)) {
					size = read_addr[k].size;
					for (n = 0; n < size; n++) {
						addr = read_addr[k].addr + n*4;
						*p++ = REG_RD(bp, addr);
					}
				}
			}
		}
	}
}

static int __bnx2x_get_preset_regs(struct bnx2x *bp, u32 *p, u32 preset)
{
	u32 i, j, addr;
	const struct wreg_addr *wreg_addr_p = NULL;

	if (CHIP_IS_E1(bp))
		wreg_addr_p = &wreg_addr_e1;
	else if (CHIP_IS_E1H(bp))
		wreg_addr_p = &wreg_addr_e1h;
	else if (CHIP_IS_E2(bp))
		wreg_addr_p = &wreg_addr_e2;
	else if (CHIP_IS_E3A0(bp))
		wreg_addr_p = &wreg_addr_e3;
	else if (CHIP_IS_E3B0(bp))
		wreg_addr_p = &wreg_addr_e3b0;

	/* Read the idle_chk registers */
	for (i = 0; i < IDLE_REGS_COUNT; i++) {
		if (bnx2x_is_reg_in_chip(bp, &idle_reg_addrs[i]) &&
		    IS_REG_IN_PRESET(idle_reg_addrs[i].presets, preset)) {
			for (j = 0; j < idle_reg_addrs[i].size; j++)
				*p++ = REG_RD(bp, idle_reg_addrs[i].addr + j*4);
		}
	}

	/* Read the regular registers */
	for (i = 0; i < REGS_COUNT; i++) {
		if (bnx2x_is_reg_in_chip(bp, &reg_addrs[i]) &&
		    IS_REG_IN_PRESET(reg_addrs[i].presets, preset)) {
			for (j = 0; j < reg_addrs[i].size; j++)
				*p++ = REG_RD(bp, reg_addrs[i].addr + j*4);
		}
	}

	/* Read the CAM registers */
	if (bnx2x_is_wreg_in_chip(bp, wreg_addr_p) &&
	    IS_REG_IN_PRESET(wreg_addr_p->presets, preset)) {
		for (i = 0; i < wreg_addr_p->size; i++) {
			*p++ = REG_RD(bp, wreg_addr_p->addr + i*4);

			/* In case of wreg_addr register, read additional
			   registers from read_regs array
			*/
			for (j = 0; j < wreg_addr_p->read_regs_count; j++) {
				addr = *(wreg_addr_p->read_regs);
				*p++ = REG_RD(bp, addr + j*4);
			}
		}
	}

	/* Paged registers are supported in E2 & E3 only */
	if (CHIP_IS_E2(bp) || CHIP_IS_E3(bp)) {
		/* Read "paged" registers */
		bnx2x_read_pages_regs(bp, p, preset);
	}

	return 0;
}

static void __bnx2x_get_regs(struct bnx2x *bp, u32 *p)
{
	u32 preset_idx;

	/* Read all registers, by reading all preset registers */
	for (preset_idx = 1; preset_idx <= DUMP_MAX_PRESETS; preset_idx++) {
		/* Skip presets with IOR */
		if ((preset_idx == 2) ||
		    (preset_idx == 5) ||
		    (preset_idx == 8) ||
		    (preset_idx == 11))
			continue;
		__bnx2x_get_preset_regs(bp, p, preset_idx);
		p += __bnx2x_get_preset_regs_len(bp, preset_idx);
	}
}

static void bnx2x_get_regs(struct net_device *dev,
			   struct ethtool_regs *regs, void *_p)
{
	u32 *p = _p;
	struct bnx2x *bp = netdev_priv(dev);
	struct dump_header dump_hdr = {0};

	regs->version = 2;
	memset(p, 0, regs->len);

	if (!netif_running(bp->dev))
		return;

	/* Disable parity attentions as long as following dump may
	 * cause false alarms by reading never written registers. We
	 * will re-enable parity attentions right after the dump.
	 */

	/* Disable parity on path 0 */
	bnx2x_pretend_func(bp, 0);
	bnx2x_disable_blocks_parity(bp);

	/* Disable parity on path 1 */
	bnx2x_pretend_func(bp, 1);
	bnx2x_disable_blocks_parity(bp);

	/* Return to current function */
	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));

	dump_hdr.header_size = (sizeof(struct dump_header) / 4) - 1;
	dump_hdr.preset = DUMP_ALL_PRESETS;
	dump_hdr.version = BNX2X_DUMP_VERSION;

	/* dump_meta_data presents OR of CHIP and PATH. */
	if (CHIP_IS_E1(bp)) {
		dump_hdr.dump_meta_data = DUMP_CHIP_E1;
	} else if (CHIP_IS_E1H(bp)) {
		dump_hdr.dump_meta_data = DUMP_CHIP_E1H;
	} else if (CHIP_IS_E2(bp)) {
		dump_hdr.dump_meta_data = DUMP_CHIP_E2 |
		(BP_PATH(bp) ? DUMP_PATH_1 : DUMP_PATH_0);
	} else if (CHIP_IS_E3A0(bp)) {
		dump_hdr.dump_meta_data = DUMP_CHIP_E3A0 |
		(BP_PATH(bp) ? DUMP_PATH_1 : DUMP_PATH_0);
	} else if (CHIP_IS_E3B0(bp)) {
		dump_hdr.dump_meta_data = DUMP_CHIP_E3B0 |
		(BP_PATH(bp) ? DUMP_PATH_1 : DUMP_PATH_0);
	}

	memcpy(p, &dump_hdr, sizeof(struct dump_header));
	p += dump_hdr.header_size + 1;

	/* Actually read the registers */
	__bnx2x_get_regs(bp, p);

	/* Re-enable parity attentions on path 0 */
	bnx2x_pretend_func(bp, 0);
	bnx2x_clear_blocks_parity(bp);
	bnx2x_enable_blocks_parity(bp);

	/* Re-enable parity attentions on path 1 */
	bnx2x_pretend_func(bp, 1);
	bnx2x_clear_blocks_parity(bp);
	bnx2x_enable_blocks_parity(bp);

	/* Return to current function */
	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));
}

static int bnx2x_get_preset_regs_len(struct net_device *dev, u32 preset)
{
	struct bnx2x *bp = netdev_priv(dev);
	int regdump_len = 0;

	regdump_len = __bnx2x_get_preset_regs_len(bp, preset);
	regdump_len *= 4;
	regdump_len += sizeof(struct dump_header);

	return regdump_len;
}

static int bnx2x_set_dump(struct net_device *dev, struct ethtool_dump *val)
{
	struct bnx2x *bp = netdev_priv(dev);

	/* Use the ethtool_dump "flag" field as the dump preset index */
	bp->dump_preset_idx = val->flag;
	return 0;
}

static int bnx2x_get_dump_flag(struct net_device *dev,
			       struct ethtool_dump *dump)
{
	struct bnx2x *bp = netdev_priv(dev);

	/* Calculate the requested preset idx length */
	dump->len = bnx2x_get_preset_regs_len(dev, bp->dump_preset_idx);
	DP(BNX2X_MSG_ETHTOOL, "Get dump preset %d length=%d\n",
	   bp->dump_preset_idx, dump->len);

	dump->flag = ETHTOOL_GET_DUMP_DATA;
	return 0;
}

static int bnx2x_get_dump_data(struct net_device *dev,
			       struct ethtool_dump *dump,
			       void *buffer)
{
	u32 *p = buffer;
	struct bnx2x *bp = netdev_priv(dev);
	struct dump_header dump_hdr = {0};

	memset(p, 0, dump->len);

	/* Disable parity attentions as long as following dump may
	 * cause false alarms by reading never written registers. We
	 * will re-enable parity attentions right after the dump.
	 */

	/* Disable parity on path 0 */
	bnx2x_pretend_func(bp, 0);
	bnx2x_disable_blocks_parity(bp);

	/* Disable parity on path 1 */
	bnx2x_pretend_func(bp, 1);
	bnx2x_disable_blocks_parity(bp);

	/* Return to current function */
	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));

	dump_hdr.header_size = (sizeof(struct dump_header) / 4) - 1;
	dump_hdr.preset = bp->dump_preset_idx;
	dump_hdr.version = BNX2X_DUMP_VERSION;

	DP(BNX2X_MSG_ETHTOOL, "Get dump data of preset %d\n", dump_hdr.preset);

	/* dump_meta_data presents OR of CHIP and PATH. */
	if (CHIP_IS_E1(bp)) {
		dump_hdr.dump_meta_data = DUMP_CHIP_E1;
	} else if (CHIP_IS_E1H(bp)) {
		dump_hdr.dump_meta_data = DUMP_CHIP_E1H;
	} else if (CHIP_IS_E2(bp)) {
		dump_hdr.dump_meta_data = DUMP_CHIP_E2 |
		(BP_PATH(bp) ? DUMP_PATH_1 : DUMP_PATH_0);
	} else if (CHIP_IS_E3A0(bp)) {
		dump_hdr.dump_meta_data = DUMP_CHIP_E3A0 |
		(BP_PATH(bp) ? DUMP_PATH_1 : DUMP_PATH_0);
	} else if (CHIP_IS_E3B0(bp)) {
		dump_hdr.dump_meta_data = DUMP_CHIP_E3B0 |
		(BP_PATH(bp) ? DUMP_PATH_1 : DUMP_PATH_0);
	}

	memcpy(p, &dump_hdr, sizeof(struct dump_header));
	p += dump_hdr.header_size + 1;

	/* Actually read the registers */
	__bnx2x_get_preset_regs(bp, p, dump_hdr.preset);

	/* Re-enable parity attentions on path 0 */
	bnx2x_pretend_func(bp, 0);
	bnx2x_clear_blocks_parity(bp);
	bnx2x_enable_blocks_parity(bp);

	/* Re-enable parity attentions on path 1 */
	bnx2x_pretend_func(bp, 1);
	bnx2x_clear_blocks_parity(bp);
	bnx2x_enable_blocks_parity(bp);

	/* Return to current function */
	bnx2x_pretend_func(bp, BP_ABS_FUNC(bp));

	return 0;
}

static void bnx2x_get_drvinfo(struct net_device *dev,
			      struct ethtool_drvinfo *info)
{
	struct bnx2x *bp = netdev_priv(dev);

	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_MODULE_VERSION, sizeof(info->version));

	bnx2x_fill_fw_str(bp, info->fw_version, sizeof(info->fw_version));

	strlcpy(info->bus_info, pci_name(bp->pdev), sizeof(info->bus_info));
	info->n_stats = BNX2X_NUM_STATS;
	info->testinfo_len = BNX2X_NUM_TESTS(bp);
	info->eedump_len = bp->common.flash_size;
	info->regdump_len = bnx2x_get_regs_len(dev);
}

static void bnx2x_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (bp->flags & NO_WOL_FLAG) {
		wol->supported = 0;
		wol->wolopts = 0;
	} else {
		wol->supported = WAKE_MAGIC;
		if (bp->wol)
			wol->wolopts = WAKE_MAGIC;
		else
			wol->wolopts = 0;
	}
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static int bnx2x_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (wol->wolopts & ~WAKE_MAGIC) {
		DP(BNX2X_MSG_ETHTOOL, "WOL not supported\n");
		return -EINVAL;
	}

	if (wol->wolopts & WAKE_MAGIC) {
		if (bp->flags & NO_WOL_FLAG) {
			DP(BNX2X_MSG_ETHTOOL, "WOL not supported\n");
			return -EINVAL;
		}
		bp->wol = 1;
	} else
		bp->wol = 0;

	return 0;
}

static u32 bnx2x_get_msglevel(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	return bp->msg_enable;
}

static void bnx2x_set_msglevel(struct net_device *dev, u32 level)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (capable(CAP_NET_ADMIN)) {
		/* dump MCP trace */
		if (IS_PF(bp) && (level & BNX2X_MSG_MCP))
			bnx2x_fw_dump_lvl(bp, KERN_INFO);
		bp->msg_enable = level;
	}
}

static int bnx2x_nway_reset(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (!bp->port.pmf)
		return 0;

	if (netif_running(dev)) {
		bnx2x_stats_handle(bp, STATS_EVENT_STOP);
		bnx2x_force_link_reset(bp);
		bnx2x_link_set(bp);
	}

	return 0;
}

static u32 bnx2x_get_link(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (bp->flags & MF_FUNC_DIS || (bp->state != BNX2X_STATE_OPEN))
		return 0;

	return bp->link_vars.link_up;
}

static int bnx2x_get_eeprom_len(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

	return bp->common.flash_size;
}

/* Per pf misc lock must be acquired before the per port mcp lock. Otherwise,
 * had we done things the other way around, if two pfs from the same port would
 * attempt to access nvram at the same time, we could run into a scenario such
 * as:
 * pf A takes the port lock.
 * pf B succeeds in taking the same lock since they are from the same port.
 * pf A takes the per pf misc lock. Performs eeprom access.
 * pf A finishes. Unlocks the per pf misc lock.
 * Pf B takes the lock and proceeds to perform it's own access.
 * pf A unlocks the per port lock, while pf B is still working (!).
 * mcp takes the per port lock and corrupts pf B's access (and/or has it's own
 * access corrupted by pf B)
 */
static int bnx2x_acquire_nvram_lock(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int count, i;
	u32 val;

	/* acquire HW lock: protect against other PFs in PF Direct Assignment */
	bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_NVRAM);

	/* adjust timeout for emulation/FPGA */
	count = BNX2X_NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* request access to nvram interface */
	REG_WR(bp, MCP_REG_MCPR_NVM_SW_ARB,
	       (MCPR_NVM_SW_ARB_ARB_REQ_SET1 << port));

	for (i = 0; i < count*10; i++) {
		val = REG_RD(bp, MCP_REG_MCPR_NVM_SW_ARB);
		if (val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port))
			break;

		udelay(5);
	}

	if (!(val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port))) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "cannot get access to nvram interface\n");
		return -EBUSY;
	}

	return 0;
}

static int bnx2x_release_nvram_lock(struct bnx2x *bp)
{
	int port = BP_PORT(bp);
	int count, i;
	u32 val;

	/* adjust timeout for emulation/FPGA */
	count = BNX2X_NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* relinquish nvram interface */
	REG_WR(bp, MCP_REG_MCPR_NVM_SW_ARB,
	       (MCPR_NVM_SW_ARB_ARB_REQ_CLR1 << port));

	for (i = 0; i < count*10; i++) {
		val = REG_RD(bp, MCP_REG_MCPR_NVM_SW_ARB);
		if (!(val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port)))
			break;

		udelay(5);
	}

	if (val & (MCPR_NVM_SW_ARB_ARB_ARB1 << port)) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "cannot free access to nvram interface\n");
		return -EBUSY;
	}

	/* release HW lock: protect against other PFs in PF Direct Assignment */
	bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_NVRAM);
	return 0;
}

static void bnx2x_enable_nvram_access(struct bnx2x *bp)
{
	u32 val;

	val = REG_RD(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE);

	/* enable both bits, even on read */
	REG_WR(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE,
	       (val | MCPR_NVM_ACCESS_ENABLE_EN |
		      MCPR_NVM_ACCESS_ENABLE_WR_EN));
}

static void bnx2x_disable_nvram_access(struct bnx2x *bp)
{
	u32 val;

	val = REG_RD(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE);

	/* disable both bits, even after read */
	REG_WR(bp, MCP_REG_MCPR_NVM_ACCESS_ENABLE,
	       (val & ~(MCPR_NVM_ACCESS_ENABLE_EN |
			MCPR_NVM_ACCESS_ENABLE_WR_EN)));
}

static int bnx2x_nvram_read_dword(struct bnx2x *bp, u32 offset, __be32 *ret_val,
				  u32 cmd_flags)
{
	int count, i, rc;
	u32 val;

	/* build the command word */
	cmd_flags |= MCPR_NVM_COMMAND_DOIT;

	/* need to clear DONE bit separately */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, MCPR_NVM_COMMAND_DONE);

	/* address of the NVRAM to read from */
	REG_WR(bp, MCP_REG_MCPR_NVM_ADDR,
	       (offset & MCPR_NVM_ADDR_NVM_ADDR_VALUE));

	/* issue a read command */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, cmd_flags);

	/* adjust timeout for emulation/FPGA */
	count = BNX2X_NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* wait for completion */
	*ret_val = 0;
	rc = -EBUSY;
	for (i = 0; i < count; i++) {
		udelay(5);
		val = REG_RD(bp, MCP_REG_MCPR_NVM_COMMAND);

		if (val & MCPR_NVM_COMMAND_DONE) {
			val = REG_RD(bp, MCP_REG_MCPR_NVM_READ);
			/* we read nvram data in cpu order
			 * but ethtool sees it as an array of bytes
			 * converting to big-endian will do the work
			 */
			*ret_val = cpu_to_be32(val);
			rc = 0;
			break;
		}
	}
	if (rc == -EBUSY)
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "nvram read timeout expired\n");
	return rc;
}

static int bnx2x_nvram_read(struct bnx2x *bp, u32 offset, u8 *ret_buf,
			    int buf_size)
{
	int rc;
	u32 cmd_flags;
	__be32 val;

	if ((offset & 0x03) || (buf_size & 0x03) || (buf_size == 0)) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "Invalid parameter: offset 0x%x  buf_size 0x%x\n",
		   offset, buf_size);
		return -EINVAL;
	}

	if (offset + buf_size > bp->common.flash_size) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "Invalid parameter: offset (0x%x) + buf_size (0x%x) > flash_size (0x%x)\n",
		   offset, buf_size, bp->common.flash_size);
		return -EINVAL;
	}

	/* request access to nvram interface */
	rc = bnx2x_acquire_nvram_lock(bp);
	if (rc)
		return rc;

	/* enable access to nvram interface */
	bnx2x_enable_nvram_access(bp);

	/* read the first word(s) */
	cmd_flags = MCPR_NVM_COMMAND_FIRST;
	while ((buf_size > sizeof(u32)) && (rc == 0)) {
		rc = bnx2x_nvram_read_dword(bp, offset, &val, cmd_flags);
		memcpy(ret_buf, &val, 4);

		/* advance to the next dword */
		offset += sizeof(u32);
		ret_buf += sizeof(u32);
		buf_size -= sizeof(u32);
		cmd_flags = 0;
	}

	if (rc == 0) {
		cmd_flags |= MCPR_NVM_COMMAND_LAST;
		rc = bnx2x_nvram_read_dword(bp, offset, &val, cmd_flags);
		memcpy(ret_buf, &val, 4);
	}

	/* disable access to nvram interface */
	bnx2x_disable_nvram_access(bp);
	bnx2x_release_nvram_lock(bp);

	return rc;
}

static int bnx2x_nvram_read32(struct bnx2x *bp, u32 offset, u32 *buf,
			      int buf_size)
{
	int rc;

	rc = bnx2x_nvram_read(bp, offset, (u8 *)buf, buf_size);

	if (!rc) {
		__be32 *be = (__be32 *)buf;

		while ((buf_size -= 4) >= 0)
			*buf++ = be32_to_cpu(*be++);
	}

	return rc;
}

static bool bnx2x_is_nvm_accessible(struct bnx2x *bp)
{
	int rc = 1;
	u16 pm = 0;
	struct net_device *dev = pci_get_drvdata(bp->pdev);

	if (bp->pm_cap)
		rc = pci_read_config_word(bp->pdev,
					  bp->pm_cap + PCI_PM_CTRL, &pm);

	if ((rc && !netif_running(dev)) ||
	    (!rc && ((pm & PCI_PM_CTRL_STATE_MASK) != PCI_D0)))
		return false;

	return true;
}

static int bnx2x_get_eeprom(struct net_device *dev,
			    struct ethtool_eeprom *eeprom, u8 *eebuf)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (!bnx2x_is_nvm_accessible(bp)) {
		DP(BNX2X_MSG_ETHTOOL  | BNX2X_MSG_NVM,
		   "cannot access eeprom when the interface is down\n");
		return -EAGAIN;
	}

	DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM, "ethtool_eeprom: cmd %d\n"
	   "  magic 0x%x  offset 0x%x (%d)  len 0x%x (%d)\n",
	   eeprom->cmd, eeprom->magic, eeprom->offset, eeprom->offset,
	   eeprom->len, eeprom->len);

	/* parameters already validated in ethtool_get_eeprom */

	return bnx2x_nvram_read(bp, eeprom->offset, eebuf, eeprom->len);
}

static int bnx2x_get_module_eeprom(struct net_device *dev,
				   struct ethtool_eeprom *ee,
				   u8 *data)
{
	struct bnx2x *bp = netdev_priv(dev);
	int rc = -EINVAL, phy_idx;
	u8 *user_data = data;
	unsigned int start_addr = ee->offset, xfer_size = 0;

	if (!bnx2x_is_nvm_accessible(bp)) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "cannot access eeprom when the interface is down\n");
		return -EAGAIN;
	}

	phy_idx = bnx2x_get_cur_phy_idx(bp);

	/* Read A0 section */
	if (start_addr < ETH_MODULE_SFF_8079_LEN) {
		/* Limit transfer size to the A0 section boundary */
		if (start_addr + ee->len > ETH_MODULE_SFF_8079_LEN)
			xfer_size = ETH_MODULE_SFF_8079_LEN - start_addr;
		else
			xfer_size = ee->len;
		bnx2x_acquire_phy_lock(bp);
		rc = bnx2x_read_sfp_module_eeprom(&bp->link_params.phy[phy_idx],
						  &bp->link_params,
						  I2C_DEV_ADDR_A0,
						  start_addr,
						  xfer_size,
						  user_data);
		bnx2x_release_phy_lock(bp);
		if (rc) {
			DP(BNX2X_MSG_ETHTOOL, "Failed reading A0 section\n");

			return -EINVAL;
		}
		user_data += xfer_size;
		start_addr += xfer_size;
	}

	/* Read A2 section */
	if ((start_addr >= ETH_MODULE_SFF_8079_LEN) &&
	    (start_addr < ETH_MODULE_SFF_8472_LEN)) {
		xfer_size = ee->len - xfer_size;
		/* Limit transfer size to the A2 section boundary */
		if (start_addr + xfer_size > ETH_MODULE_SFF_8472_LEN)
			xfer_size = ETH_MODULE_SFF_8472_LEN - start_addr;
		start_addr -= ETH_MODULE_SFF_8079_LEN;
		bnx2x_acquire_phy_lock(bp);
		rc = bnx2x_read_sfp_module_eeprom(&bp->link_params.phy[phy_idx],
						  &bp->link_params,
						  I2C_DEV_ADDR_A2,
						  start_addr,
						  xfer_size,
						  user_data);
		bnx2x_release_phy_lock(bp);
		if (rc) {
			DP(BNX2X_MSG_ETHTOOL, "Failed reading A2 section\n");
			return -EINVAL;
		}
	}
	return rc;
}

static int bnx2x_get_module_info(struct net_device *dev,
				 struct ethtool_modinfo *modinfo)
{
	struct bnx2x *bp = netdev_priv(dev);
	int phy_idx, rc;
	u8 sff8472_comp, diag_type;

	if (!bnx2x_is_nvm_accessible(bp)) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "cannot access eeprom when the interface is down\n");
		return -EAGAIN;
	}
	phy_idx = bnx2x_get_cur_phy_idx(bp);
	bnx2x_acquire_phy_lock(bp);
	rc = bnx2x_read_sfp_module_eeprom(&bp->link_params.phy[phy_idx],
					  &bp->link_params,
					  I2C_DEV_ADDR_A0,
					  SFP_EEPROM_SFF_8472_COMP_ADDR,
					  SFP_EEPROM_SFF_8472_COMP_SIZE,
					  &sff8472_comp);
	bnx2x_release_phy_lock(bp);
	if (rc) {
		DP(BNX2X_MSG_ETHTOOL, "Failed reading SFF-8472 comp field\n");
		return -EINVAL;
	}

	bnx2x_acquire_phy_lock(bp);
	rc = bnx2x_read_sfp_module_eeprom(&bp->link_params.phy[phy_idx],
					  &bp->link_params,
					  I2C_DEV_ADDR_A0,
					  SFP_EEPROM_DIAG_TYPE_ADDR,
					  SFP_EEPROM_DIAG_TYPE_SIZE,
					  &diag_type);
	bnx2x_release_phy_lock(bp);
	if (rc) {
		DP(BNX2X_MSG_ETHTOOL, "Failed reading Diag Type field\n");
		return -EINVAL;
	}

	if (!sff8472_comp ||
	    (diag_type & SFP_EEPROM_DIAG_ADDR_CHANGE_REQ)) {
		modinfo->type = ETH_MODULE_SFF_8079;
		modinfo->eeprom_len = ETH_MODULE_SFF_8079_LEN;
	} else {
		modinfo->type = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
	}
	return 0;
}

static int bnx2x_nvram_write_dword(struct bnx2x *bp, u32 offset, u32 val,
				   u32 cmd_flags)
{
	int count, i, rc;

	/* build the command word */
	cmd_flags |= MCPR_NVM_COMMAND_DOIT | MCPR_NVM_COMMAND_WR;

	/* need to clear DONE bit separately */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, MCPR_NVM_COMMAND_DONE);

	/* write the data */
	REG_WR(bp, MCP_REG_MCPR_NVM_WRITE, val);

	/* address of the NVRAM to write to */
	REG_WR(bp, MCP_REG_MCPR_NVM_ADDR,
	       (offset & MCPR_NVM_ADDR_NVM_ADDR_VALUE));

	/* issue the write command */
	REG_WR(bp, MCP_REG_MCPR_NVM_COMMAND, cmd_flags);

	/* adjust timeout for emulation/FPGA */
	count = BNX2X_NVRAM_TIMEOUT_COUNT;
	if (CHIP_REV_IS_SLOW(bp))
		count *= 100;

	/* wait for completion */
	rc = -EBUSY;
	for (i = 0; i < count; i++) {
		udelay(5);
		val = REG_RD(bp, MCP_REG_MCPR_NVM_COMMAND);
		if (val & MCPR_NVM_COMMAND_DONE) {
			rc = 0;
			break;
		}
	}

	if (rc == -EBUSY)
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "nvram write timeout expired\n");
	return rc;
}

#define BYTE_OFFSET(offset)		(8 * (offset & 0x03))

static int bnx2x_nvram_write1(struct bnx2x *bp, u32 offset, u8 *data_buf,
			      int buf_size)
{
	int rc;
	u32 cmd_flags, align_offset, val;
	__be32 val_be;

	if (offset + buf_size > bp->common.flash_size) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "Invalid parameter: offset (0x%x) + buf_size (0x%x) > flash_size (0x%x)\n",
		   offset, buf_size, bp->common.flash_size);
		return -EINVAL;
	}

	/* request access to nvram interface */
	rc = bnx2x_acquire_nvram_lock(bp);
	if (rc)
		return rc;

	/* enable access to nvram interface */
	bnx2x_enable_nvram_access(bp);

	cmd_flags = (MCPR_NVM_COMMAND_FIRST | MCPR_NVM_COMMAND_LAST);
	align_offset = (offset & ~0x03);
	rc = bnx2x_nvram_read_dword(bp, align_offset, &val_be, cmd_flags);

	if (rc == 0) {
		/* nvram data is returned as an array of bytes
		 * convert it back to cpu order
		 */
		val = be32_to_cpu(val_be);

		val &= ~le32_to_cpu(0xff << BYTE_OFFSET(offset));
		val |= le32_to_cpu(*data_buf << BYTE_OFFSET(offset));

		rc = bnx2x_nvram_write_dword(bp, align_offset, val,
					     cmd_flags);
	}

	/* disable access to nvram interface */
	bnx2x_disable_nvram_access(bp);
	bnx2x_release_nvram_lock(bp);

	return rc;
}

static int bnx2x_nvram_write(struct bnx2x *bp, u32 offset, u8 *data_buf,
			     int buf_size)
{
	int rc;
	u32 cmd_flags;
	u32 val;
	u32 written_so_far;

	if (buf_size == 1)	/* ethtool */
		return bnx2x_nvram_write1(bp, offset, data_buf, buf_size);

	if ((offset & 0x03) || (buf_size & 0x03) || (buf_size == 0)) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "Invalid parameter: offset 0x%x  buf_size 0x%x\n",
		   offset, buf_size);
		return -EINVAL;
	}

	if (offset + buf_size > bp->common.flash_size) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "Invalid parameter: offset (0x%x) + buf_size (0x%x) > flash_size (0x%x)\n",
		   offset, buf_size, bp->common.flash_size);
		return -EINVAL;
	}

	/* request access to nvram interface */
	rc = bnx2x_acquire_nvram_lock(bp);
	if (rc)
		return rc;

	/* enable access to nvram interface */
	bnx2x_enable_nvram_access(bp);

	written_so_far = 0;
	cmd_flags = MCPR_NVM_COMMAND_FIRST;
	while ((written_so_far < buf_size) && (rc == 0)) {
		if (written_so_far == (buf_size - sizeof(u32)))
			cmd_flags |= MCPR_NVM_COMMAND_LAST;
		else if (((offset + 4) % BNX2X_NVRAM_PAGE_SIZE) == 0)
			cmd_flags |= MCPR_NVM_COMMAND_LAST;
		else if ((offset % BNX2X_NVRAM_PAGE_SIZE) == 0)
			cmd_flags |= MCPR_NVM_COMMAND_FIRST;

		memcpy(&val, data_buf, 4);

		rc = bnx2x_nvram_write_dword(bp, offset, val, cmd_flags);

		/* advance to the next dword */
		offset += sizeof(u32);
		data_buf += sizeof(u32);
		written_so_far += sizeof(u32);
		cmd_flags = 0;
	}

	/* disable access to nvram interface */
	bnx2x_disable_nvram_access(bp);
	bnx2x_release_nvram_lock(bp);

	return rc;
}

static int bnx2x_set_eeprom(struct net_device *dev,
			    struct ethtool_eeprom *eeprom, u8 *eebuf)
{
	struct bnx2x *bp = netdev_priv(dev);
	int port = BP_PORT(bp);
	int rc = 0;
	u32 ext_phy_config;

	if (!bnx2x_is_nvm_accessible(bp)) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "cannot access eeprom when the interface is down\n");
		return -EAGAIN;
	}

	DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM, "ethtool_eeprom: cmd %d\n"
	   "  magic 0x%x  offset 0x%x (%d)  len 0x%x (%d)\n",
	   eeprom->cmd, eeprom->magic, eeprom->offset, eeprom->offset,
	   eeprom->len, eeprom->len);

	/* parameters already validated in ethtool_set_eeprom */

	/* PHY eeprom can be accessed only by the PMF */
	if ((eeprom->magic >= 0x50485900) && (eeprom->magic <= 0x504859FF) &&
	    !bp->port.pmf) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "wrong magic or interface is not pmf\n");
		return -EINVAL;
	}

	ext_phy_config =
		SHMEM_RD(bp,
			 dev_info.port_hw_config[port].external_phy_config);

	if (eeprom->magic == 0x50485950) {
		/* 'PHYP' (0x50485950): prepare phy for FW upgrade */
		bnx2x_stats_handle(bp, STATS_EVENT_STOP);

		bnx2x_acquire_phy_lock(bp);
		rc |= bnx2x_link_reset(&bp->link_params,
				       &bp->link_vars, 0);
		if (XGXS_EXT_PHY_TYPE(ext_phy_config) ==
					PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101)
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_0,
				       MISC_REGISTERS_GPIO_HIGH, port);
		bnx2x_release_phy_lock(bp);
		bnx2x_link_report(bp);

	} else if (eeprom->magic == 0x50485952) {
		/* 'PHYR' (0x50485952): re-init link after FW upgrade */
		if (bp->state == BNX2X_STATE_OPEN) {
			bnx2x_acquire_phy_lock(bp);
			rc |= bnx2x_link_reset(&bp->link_params,
					       &bp->link_vars, 1);

			rc |= bnx2x_phy_init(&bp->link_params,
					     &bp->link_vars);
			bnx2x_release_phy_lock(bp);
			bnx2x_calc_fc_adv(bp);
		}
	} else if (eeprom->magic == 0x53985943) {
		/* 'PHYC' (0x53985943): PHY FW upgrade completed */
		if (XGXS_EXT_PHY_TYPE(ext_phy_config) ==
				       PORT_HW_CFG_XGXS_EXT_PHY_TYPE_SFX7101) {

			/* DSP Remove Download Mode */
			bnx2x_set_gpio(bp, MISC_REGISTERS_GPIO_0,
				       MISC_REGISTERS_GPIO_LOW, port);

			bnx2x_acquire_phy_lock(bp);

			bnx2x_sfx7101_sp_sw_reset(bp,
						&bp->link_params.phy[EXT_PHY1]);

			/* wait 0.5 sec to allow it to run */
			msleep(500);
			bnx2x_ext_phy_hw_reset(bp, port);
			msleep(500);
			bnx2x_release_phy_lock(bp);
		}
	} else
		rc = bnx2x_nvram_write(bp, eeprom->offset, eebuf, eeprom->len);

	return rc;
}

static int bnx2x_get_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *coal)
{
	struct bnx2x *bp = netdev_priv(dev);

	memset(coal, 0, sizeof(struct ethtool_coalesce));

	coal->rx_coalesce_usecs = bp->rx_ticks;
	coal->tx_coalesce_usecs = bp->tx_ticks;

	return 0;
}

static int bnx2x_set_coalesce(struct net_device *dev,
			      struct ethtool_coalesce *coal)
{
	struct bnx2x *bp = netdev_priv(dev);

	bp->rx_ticks = (u16)coal->rx_coalesce_usecs;
	if (bp->rx_ticks > BNX2X_MAX_COALESCE_TOUT)
		bp->rx_ticks = BNX2X_MAX_COALESCE_TOUT;

	bp->tx_ticks = (u16)coal->tx_coalesce_usecs;
	if (bp->tx_ticks > BNX2X_MAX_COALESCE_TOUT)
		bp->tx_ticks = BNX2X_MAX_COALESCE_TOUT;

	if (netif_running(dev))
		bnx2x_update_coalesce(bp);

	return 0;
}

static void bnx2x_get_ringparam(struct net_device *dev,
				struct ethtool_ringparam *ering)
{
	struct bnx2x *bp = netdev_priv(dev);

	ering->rx_max_pending = MAX_RX_AVAIL;

	if (bp->rx_ring_size)
		ering->rx_pending = bp->rx_ring_size;
	else
		ering->rx_pending = MAX_RX_AVAIL;

	ering->tx_max_pending = IS_MF_FCOE_AFEX(bp) ? 0 : MAX_TX_AVAIL;
	ering->tx_pending = bp->tx_ring_size;
}

static int bnx2x_set_ringparam(struct net_device *dev,
			       struct ethtool_ringparam *ering)
{
	struct bnx2x *bp = netdev_priv(dev);

	DP(BNX2X_MSG_ETHTOOL,
	   "set ring params command parameters: rx_pending = %d, tx_pending = %d\n",
	   ering->rx_pending, ering->tx_pending);

	if (bp->recovery_state != BNX2X_RECOVERY_DONE) {
		DP(BNX2X_MSG_ETHTOOL,
		   "Handling parity error recovery. Try again later\n");
		return -EAGAIN;
	}

	if ((ering->rx_pending > MAX_RX_AVAIL) ||
	    (ering->rx_pending < (bp->disable_tpa ? MIN_RX_SIZE_NONTPA :
						    MIN_RX_SIZE_TPA)) ||
	    (ering->tx_pending > (IS_MF_FCOE_AFEX(bp) ? 0 : MAX_TX_AVAIL)) ||
	    (ering->tx_pending <= MAX_SKB_FRAGS + 4)) {
		DP(BNX2X_MSG_ETHTOOL, "Command parameters not supported\n");
		return -EINVAL;
	}

	bp->rx_ring_size = ering->rx_pending;
	bp->tx_ring_size = ering->tx_pending;

	return bnx2x_reload_if_running(dev);
}

static void bnx2x_get_pauseparam(struct net_device *dev,
				 struct ethtool_pauseparam *epause)
{
	struct bnx2x *bp = netdev_priv(dev);
	int cfg_idx = bnx2x_get_link_cfg_idx(bp);
	int cfg_reg;

	epause->autoneg = (bp->link_params.req_flow_ctrl[cfg_idx] ==
			   BNX2X_FLOW_CTRL_AUTO);

	if (!epause->autoneg)
		cfg_reg = bp->link_params.req_flow_ctrl[cfg_idx];
	else
		cfg_reg = bp->link_params.req_fc_auto_adv;

	epause->rx_pause = ((cfg_reg & BNX2X_FLOW_CTRL_RX) ==
			    BNX2X_FLOW_CTRL_RX);
	epause->tx_pause = ((cfg_reg & BNX2X_FLOW_CTRL_TX) ==
			    BNX2X_FLOW_CTRL_TX);

	DP(BNX2X_MSG_ETHTOOL, "ethtool_pauseparam: cmd %d\n"
	   "  autoneg %d  rx_pause %d  tx_pause %d\n",
	   epause->cmd, epause->autoneg, epause->rx_pause, epause->tx_pause);
}

static int bnx2x_set_pauseparam(struct net_device *dev,
				struct ethtool_pauseparam *epause)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 cfg_idx = bnx2x_get_link_cfg_idx(bp);
	if (IS_MF(bp))
		return 0;

	DP(BNX2X_MSG_ETHTOOL, "ethtool_pauseparam: cmd %d\n"
	   "  autoneg %d  rx_pause %d  tx_pause %d\n",
	   epause->cmd, epause->autoneg, epause->rx_pause, epause->tx_pause);

	bp->link_params.req_flow_ctrl[cfg_idx] = BNX2X_FLOW_CTRL_AUTO;

	if (epause->rx_pause)
		bp->link_params.req_flow_ctrl[cfg_idx] |= BNX2X_FLOW_CTRL_RX;

	if (epause->tx_pause)
		bp->link_params.req_flow_ctrl[cfg_idx] |= BNX2X_FLOW_CTRL_TX;

	if (bp->link_params.req_flow_ctrl[cfg_idx] == BNX2X_FLOW_CTRL_AUTO)
		bp->link_params.req_flow_ctrl[cfg_idx] = BNX2X_FLOW_CTRL_NONE;

	if (epause->autoneg) {
		if (!(bp->port.supported[cfg_idx] & SUPPORTED_Autoneg)) {
			DP(BNX2X_MSG_ETHTOOL, "autoneg not supported\n");
			return -EINVAL;
		}

		if (bp->link_params.req_line_speed[cfg_idx] == SPEED_AUTO_NEG) {
			bp->link_params.req_flow_ctrl[cfg_idx] =
				BNX2X_FLOW_CTRL_AUTO;
		}
		bp->link_params.req_fc_auto_adv = 0;
		if (epause->rx_pause)
			bp->link_params.req_fc_auto_adv |= BNX2X_FLOW_CTRL_RX;

		if (epause->tx_pause)
			bp->link_params.req_fc_auto_adv |= BNX2X_FLOW_CTRL_TX;

		if (!bp->link_params.req_fc_auto_adv)
			bp->link_params.req_fc_auto_adv |= BNX2X_FLOW_CTRL_NONE;
	}

	DP(BNX2X_MSG_ETHTOOL,
	   "req_flow_ctrl 0x%x\n", bp->link_params.req_flow_ctrl[cfg_idx]);

	if (netif_running(dev)) {
		bnx2x_stats_handle(bp, STATS_EVENT_STOP);
		bnx2x_link_set(bp);
	}

	return 0;
}

static const char bnx2x_tests_str_arr[BNX2X_NUM_TESTS_SF][ETH_GSTRING_LEN] = {
	"register_test (offline)    ",
	"memory_test (offline)      ",
	"int_loopback_test (offline)",
	"ext_loopback_test (offline)",
	"nvram_test (online)        ",
	"interrupt_test (online)    ",
	"link_test (online)         "
};

enum {
	BNX2X_PRI_FLAG_ISCSI,
	BNX2X_PRI_FLAG_FCOE,
	BNX2X_PRI_FLAG_STORAGE,
	BNX2X_PRI_FLAG_LEN,
};

static const char bnx2x_private_arr[BNX2X_PRI_FLAG_LEN][ETH_GSTRING_LEN] = {
	"iSCSI offload support",
	"FCoE offload support",
	"Storage only interface"
};

static u32 bnx2x_eee_to_adv(u32 eee_adv)
{
	u32 modes = 0;

	if (eee_adv & SHMEM_EEE_100M_ADV)
		modes |= ADVERTISED_100baseT_Full;
	if (eee_adv & SHMEM_EEE_1G_ADV)
		modes |= ADVERTISED_1000baseT_Full;
	if (eee_adv & SHMEM_EEE_10G_ADV)
		modes |= ADVERTISED_10000baseT_Full;

	return modes;
}

static u32 bnx2x_adv_to_eee(u32 modes, u32 shift)
{
	u32 eee_adv = 0;
	if (modes & ADVERTISED_100baseT_Full)
		eee_adv |= SHMEM_EEE_100M_ADV;
	if (modes & ADVERTISED_1000baseT_Full)
		eee_adv |= SHMEM_EEE_1G_ADV;
	if (modes & ADVERTISED_10000baseT_Full)
		eee_adv |= SHMEM_EEE_10G_ADV;

	return eee_adv << shift;
}

static int bnx2x_get_eee(struct net_device *dev, struct ethtool_eee *edata)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 eee_cfg;

	if (!SHMEM2_HAS(bp, eee_status[BP_PORT(bp)])) {
		DP(BNX2X_MSG_ETHTOOL, "BC Version does not support EEE\n");
		return -EOPNOTSUPP;
	}

	eee_cfg = bp->link_vars.eee_status;

	edata->supported =
		bnx2x_eee_to_adv((eee_cfg & SHMEM_EEE_SUPPORTED_MASK) >>
				 SHMEM_EEE_SUPPORTED_SHIFT);

	edata->advertised =
		bnx2x_eee_to_adv((eee_cfg & SHMEM_EEE_ADV_STATUS_MASK) >>
				 SHMEM_EEE_ADV_STATUS_SHIFT);
	edata->lp_advertised =
		bnx2x_eee_to_adv((eee_cfg & SHMEM_EEE_LP_ADV_STATUS_MASK) >>
				 SHMEM_EEE_LP_ADV_STATUS_SHIFT);

	/* SHMEM value is in 16u units --> Convert to 1u units. */
	edata->tx_lpi_timer = (eee_cfg & SHMEM_EEE_TIMER_MASK) << 4;

	edata->eee_enabled    = (eee_cfg & SHMEM_EEE_REQUESTED_BIT)	? 1 : 0;
	edata->eee_active     = (eee_cfg & SHMEM_EEE_ACTIVE_BIT)	? 1 : 0;
	edata->tx_lpi_enabled = (eee_cfg & SHMEM_EEE_LPI_REQUESTED_BIT) ? 1 : 0;

	return 0;
}

static int bnx2x_set_eee(struct net_device *dev, struct ethtool_eee *edata)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 eee_cfg;
	u32 advertised;

	if (IS_MF(bp))
		return 0;

	if (!SHMEM2_HAS(bp, eee_status[BP_PORT(bp)])) {
		DP(BNX2X_MSG_ETHTOOL, "BC Version does not support EEE\n");
		return -EOPNOTSUPP;
	}

	eee_cfg = bp->link_vars.eee_status;

	if (!(eee_cfg & SHMEM_EEE_SUPPORTED_MASK)) {
		DP(BNX2X_MSG_ETHTOOL, "Board does not support EEE!\n");
		return -EOPNOTSUPP;
	}

	advertised = bnx2x_adv_to_eee(edata->advertised,
				      SHMEM_EEE_ADV_STATUS_SHIFT);
	if ((advertised != (eee_cfg & SHMEM_EEE_ADV_STATUS_MASK))) {
		DP(BNX2X_MSG_ETHTOOL,
		   "Direct manipulation of EEE advertisement is not supported\n");
		return -EINVAL;
	}

	if (edata->tx_lpi_timer > EEE_MODE_TIMER_MASK) {
		DP(BNX2X_MSG_ETHTOOL,
		   "Maximal Tx Lpi timer supported is %x(u)\n",
		   EEE_MODE_TIMER_MASK);
		return -EINVAL;
	}
	if (edata->tx_lpi_enabled &&
	    (edata->tx_lpi_timer < EEE_MODE_NVRAM_AGGRESSIVE_TIME)) {
		DP(BNX2X_MSG_ETHTOOL,
		   "Minimal Tx Lpi timer supported is %d(u)\n",
		   EEE_MODE_NVRAM_AGGRESSIVE_TIME);
		return -EINVAL;
	}

	/* All is well; Apply changes*/
	if (edata->eee_enabled)
		bp->link_params.eee_mode |= EEE_MODE_ADV_LPI;
	else
		bp->link_params.eee_mode &= ~EEE_MODE_ADV_LPI;

	if (edata->tx_lpi_enabled)
		bp->link_params.eee_mode |= EEE_MODE_ENABLE_LPI;
	else
		bp->link_params.eee_mode &= ~EEE_MODE_ENABLE_LPI;

	bp->link_params.eee_mode &= ~EEE_MODE_TIMER_MASK;
	bp->link_params.eee_mode |= (edata->tx_lpi_timer &
				    EEE_MODE_TIMER_MASK) |
				    EEE_MODE_OVERRIDE_NVRAM |
				    EEE_MODE_OUTPUT_TIME;

	/* Restart link to propagate changes */
	if (netif_running(dev)) {
		bnx2x_stats_handle(bp, STATS_EVENT_STOP);
		bnx2x_force_link_reset(bp);
		bnx2x_link_set(bp);
	}

	return 0;
}

enum {
	BNX2X_CHIP_E1_OFST = 0,
	BNX2X_CHIP_E1H_OFST,
	BNX2X_CHIP_E2_OFST,
	BNX2X_CHIP_E3_OFST,
	BNX2X_CHIP_E3B0_OFST,
	BNX2X_CHIP_MAX_OFST
};

#define BNX2X_CHIP_MASK_E1	(1 << BNX2X_CHIP_E1_OFST)
#define BNX2X_CHIP_MASK_E1H	(1 << BNX2X_CHIP_E1H_OFST)
#define BNX2X_CHIP_MASK_E2	(1 << BNX2X_CHIP_E2_OFST)
#define BNX2X_CHIP_MASK_E3	(1 << BNX2X_CHIP_E3_OFST)
#define BNX2X_CHIP_MASK_E3B0	(1 << BNX2X_CHIP_E3B0_OFST)

#define BNX2X_CHIP_MASK_ALL	((1 << BNX2X_CHIP_MAX_OFST) - 1)
#define BNX2X_CHIP_MASK_E1X	(BNX2X_CHIP_MASK_E1 | BNX2X_CHIP_MASK_E1H)

static int bnx2x_test_registers(struct bnx2x *bp)
{
	int idx, i, rc = -ENODEV;
	u32 wr_val = 0, hw;
	int port = BP_PORT(bp);
	static const struct {
		u32 hw;
		u32 offset0;
		u32 offset1;
		u32 mask;
	} reg_tbl[] = {
/* 0 */		{ BNX2X_CHIP_MASK_ALL,
			BRB1_REG_PAUSE_LOW_THRESHOLD_0,	4, 0x000003ff },
		{ BNX2X_CHIP_MASK_ALL,
			DORQ_REG_DB_ADDR0,		4, 0xffffffff },
		{ BNX2X_CHIP_MASK_E1X,
			HC_REG_AGG_INT_0,		4, 0x000003ff },
		{ BNX2X_CHIP_MASK_ALL,
			PBF_REG_MAC_IF0_ENABLE,		4, 0x00000001 },
		{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2 | BNX2X_CHIP_MASK_E3,
			PBF_REG_P0_INIT_CRD,		4, 0x000007ff },
		{ BNX2X_CHIP_MASK_E3B0,
			PBF_REG_INIT_CRD_Q0,		4, 0x000007ff },
		{ BNX2X_CHIP_MASK_ALL,
			PRS_REG_CID_PORT_0,		4, 0x00ffffff },
		{ BNX2X_CHIP_MASK_ALL,
			PXP2_REG_PSWRQ_CDU0_L2P,	4, 0x000fffff },
		{ BNX2X_CHIP_MASK_ALL,
			PXP2_REG_RQ_CDU0_EFIRST_MEM_ADDR, 8, 0x0003ffff },
		{ BNX2X_CHIP_MASK_ALL,
			PXP2_REG_PSWRQ_TM0_L2P,		4, 0x000fffff },
/* 10 */	{ BNX2X_CHIP_MASK_ALL,
			PXP2_REG_RQ_USDM0_EFIRST_MEM_ADDR, 8, 0x0003ffff },
		{ BNX2X_CHIP_MASK_ALL,
			PXP2_REG_PSWRQ_TSDM0_L2P,	4, 0x000fffff },
		{ BNX2X_CHIP_MASK_ALL,
			QM_REG_CONNNUM_0,		4, 0x000fffff },
		{ BNX2X_CHIP_MASK_ALL,
			TM_REG_LIN0_MAX_ACTIVE_CID,	4, 0x0003ffff },
		{ BNX2X_CHIP_MASK_ALL,
			SRC_REG_KEYRSS0_0,		40, 0xffffffff },
		{ BNX2X_CHIP_MASK_ALL,
			SRC_REG_KEYRSS0_7,		40, 0xffffffff },
		{ BNX2X_CHIP_MASK_ALL,
			XCM_REG_WU_DA_SET_TMR_CNT_FLG_CMD00, 4, 0x00000001 },
		{ BNX2X_CHIP_MASK_ALL,
			XCM_REG_WU_DA_CNT_CMD00,	4, 0x00000003 },
		{ BNX2X_CHIP_MASK_ALL,
			XCM_REG_GLB_DEL_ACK_MAX_CNT_0,	4, 0x000000ff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_T_BIT,		4, 0x00000001 },
/* 20 */	{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2,
			NIG_REG_EMAC0_IN_EN,		4, 0x00000001 },
		{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2,
			NIG_REG_BMAC0_IN_EN,		4, 0x00000001 },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_XCM0_OUT_EN,		4, 0x00000001 },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_BRB0_OUT_EN,		4, 0x00000001 },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_XCM_MASK,		4, 0x00000007 },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_ACPI_PAT_6_LEN,	68, 0x000000ff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_ACPI_PAT_0_CRC,	68, 0xffffffff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_DEST_MAC_0_0,	160, 0xffffffff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_DEST_IP_0_1,	160, 0xffffffff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_IPV4_IPV6_0,	160, 0x00000001 },
/* 30 */	{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_DEST_UDP_0,	160, 0x0000ffff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_DEST_TCP_0,	160, 0x0000ffff },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LLH0_VLAN_ID_0,	160, 0x00000fff },
		{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2,
			NIG_REG_XGXS_SERDES0_MODE_SEL,	4, 0x00000001 },
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_LED_CONTROL_OVERRIDE_TRAFFIC_P0, 4, 0x00000001},
		{ BNX2X_CHIP_MASK_ALL,
			NIG_REG_STATUS_INTERRUPT_PORT0,	4, 0x07ffffff },
		{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2,
			NIG_REG_XGXS0_CTRL_EXTREMOTEMDIOST, 24, 0x00000001 },
		{ BNX2X_CHIP_MASK_E1X | BNX2X_CHIP_MASK_E2,
			NIG_REG_SERDES0_CTRL_PHY_ADDR,	16, 0x0000001f },

		{ BNX2X_CHIP_MASK_ALL, 0xffffffff, 0, 0x00000000 }
	};

	if (!bnx2x_is_nvm_accessible(bp)) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "cannot access eeprom when the interface is down\n");
		return rc;
	}

	if (CHIP_IS_E1(bp))
		hw = BNX2X_CHIP_MASK_E1;
	else if (CHIP_IS_E1H(bp))
		hw = BNX2X_CHIP_MASK_E1H;
	else if (CHIP_IS_E2(bp))
		hw = BNX2X_CHIP_MASK_E2;
	else if (CHIP_IS_E3B0(bp))
		hw = BNX2X_CHIP_MASK_E3B0;
	else /* e3 A0 */
		hw = BNX2X_CHIP_MASK_E3;

	/* Repeat the test twice:
	 * First by writing 0x00000000, second by writing 0xffffffff
	 */
	for (idx = 0; idx < 2; idx++) {

		switch (idx) {
		case 0:
			wr_val = 0;
			break;
		case 1:
			wr_val = 0xffffffff;
			break;
		}

		for (i = 0; reg_tbl[i].offset0 != 0xffffffff; i++) {
			u32 offset, mask, save_val, val;
			if (!(hw & reg_tbl[i].hw))
				continue;

			offset = reg_tbl[i].offset0 + port*reg_tbl[i].offset1;
			mask = reg_tbl[i].mask;

			save_val = REG_RD(bp, offset);

			REG_WR(bp, offset, wr_val & mask);

			val = REG_RD(bp, offset);

			/* Restore the original register's value */
			REG_WR(bp, offset, save_val);

			/* verify value is as expected */
			if ((val & mask) != (wr_val & mask)) {
				DP(BNX2X_MSG_ETHTOOL,
				   "offset 0x%x: val 0x%x != 0x%x mask 0x%x\n",
				   offset, val, wr_val, mask);
				goto test_reg_exit;
			}
		}
	}

	rc = 0;

test_reg_exit:
	return rc;
}

static int bnx2x_test_memory(struct bnx2x *bp)
{
	int i, j, rc = -ENODEV;
	u32 val, index;
	static const struct {
		u32 offset;
		int size;
	} mem_tbl[] = {
		{ CCM_REG_XX_DESCR_TABLE,   CCM_REG_XX_DESCR_TABLE_SIZE },
		{ CFC_REG_ACTIVITY_COUNTER, CFC_REG_ACTIVITY_COUNTER_SIZE },
		{ CFC_REG_LINK_LIST,        CFC_REG_LINK_LIST_SIZE },
		{ DMAE_REG_CMD_MEM,         DMAE_REG_CMD_MEM_SIZE },
		{ TCM_REG_XX_DESCR_TABLE,   TCM_REG_XX_DESCR_TABLE_SIZE },
		{ UCM_REG_XX_DESCR_TABLE,   UCM_REG_XX_DESCR_TABLE_SIZE },
		{ XCM_REG_XX_DESCR_TABLE,   XCM_REG_XX_DESCR_TABLE_SIZE },

		{ 0xffffffff, 0 }
	};

	static const struct {
		char *name;
		u32 offset;
		u32 hw_mask[BNX2X_CHIP_MAX_OFST];
	} prty_tbl[] = {
		{ "CCM_PRTY_STS",  CCM_REG_CCM_PRTY_STS,
			{0x3ffc0, 0,   0, 0} },
		{ "CFC_PRTY_STS",  CFC_REG_CFC_PRTY_STS,
			{0x2,     0x2, 0, 0} },
		{ "DMAE_PRTY_STS", DMAE_REG_DMAE_PRTY_STS,
			{0,       0,   0, 0} },
		{ "TCM_PRTY_STS",  TCM_REG_TCM_PRTY_STS,
			{0x3ffc0, 0,   0, 0} },
		{ "UCM_PRTY_STS",  UCM_REG_UCM_PRTY_STS,
			{0x3ffc0, 0,   0, 0} },
		{ "XCM_PRTY_STS",  XCM_REG_XCM_PRTY_STS,
			{0x3ffc1, 0,   0, 0} },

		{ NULL, 0xffffffff, {0, 0, 0, 0} }
	};

	if (!bnx2x_is_nvm_accessible(bp)) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "cannot access eeprom when the interface is down\n");
		return rc;
	}

	if (CHIP_IS_E1(bp))
		index = BNX2X_CHIP_E1_OFST;
	else if (CHIP_IS_E1H(bp))
		index = BNX2X_CHIP_E1H_OFST;
	else if (CHIP_IS_E2(bp))
		index = BNX2X_CHIP_E2_OFST;
	else /* e3 */
		index = BNX2X_CHIP_E3_OFST;

	/* pre-Check the parity status */
	for (i = 0; prty_tbl[i].offset != 0xffffffff; i++) {
		val = REG_RD(bp, prty_tbl[i].offset);
		if (val & ~(prty_tbl[i].hw_mask[index])) {
			DP(BNX2X_MSG_ETHTOOL,
			   "%s is 0x%x\n", prty_tbl[i].name, val);
			goto test_mem_exit;
		}
	}

	/* Go through all the memories */
	for (i = 0; mem_tbl[i].offset != 0xffffffff; i++)
		for (j = 0; j < mem_tbl[i].size; j++)
			REG_RD(bp, mem_tbl[i].offset + j*4);

	/* Check the parity status */
	for (i = 0; prty_tbl[i].offset != 0xffffffff; i++) {
		val = REG_RD(bp, prty_tbl[i].offset);
		if (val & ~(prty_tbl[i].hw_mask[index])) {
			DP(BNX2X_MSG_ETHTOOL,
			   "%s is 0x%x\n", prty_tbl[i].name, val);
			goto test_mem_exit;
		}
	}

	rc = 0;

test_mem_exit:
	return rc;
}

static void bnx2x_wait_for_link(struct bnx2x *bp, u8 link_up, u8 is_serdes)
{
	int cnt = 1400;

	if (link_up) {
		while (bnx2x_link_test(bp, is_serdes) && cnt--)
			msleep(20);

		if (cnt <= 0 && bnx2x_link_test(bp, is_serdes))
			DP(BNX2X_MSG_ETHTOOL, "Timeout waiting for link up\n");

		cnt = 1400;
		while (!bp->link_vars.link_up && cnt--)
			msleep(20);

		if (cnt <= 0 && !bp->link_vars.link_up)
			DP(BNX2X_MSG_ETHTOOL,
			   "Timeout waiting for link init\n");
	}
}

static int bnx2x_run_loopback(struct bnx2x *bp, int loopback_mode)
{
	unsigned int pkt_size, num_pkts, i;
	struct sk_buff *skb;
	unsigned char *packet;
	struct bnx2x_fastpath *fp_rx = &bp->fp[0];
	struct bnx2x_fastpath *fp_tx = &bp->fp[0];
	struct bnx2x_fp_txdata *txdata = fp_tx->txdata_ptr[0];
	u16 tx_start_idx, tx_idx;
	u16 rx_start_idx, rx_idx;
	u16 pkt_prod, bd_prod;
	struct sw_tx_bd *tx_buf;
	struct eth_tx_start_bd *tx_start_bd;
	dma_addr_t mapping;
	union eth_rx_cqe *cqe;
	u8 cqe_fp_flags, cqe_fp_type;
	struct sw_rx_bd *rx_buf;
	u16 len;
	int rc = -ENODEV;
	u8 *data;
	struct netdev_queue *txq = netdev_get_tx_queue(bp->dev,
						       txdata->txq_index);

	/* check the loopback mode */
	switch (loopback_mode) {
	case BNX2X_PHY_LOOPBACK:
		if (bp->link_params.loopback_mode != LOOPBACK_XGXS) {
			DP(BNX2X_MSG_ETHTOOL, "PHY loopback not supported\n");
			return -EINVAL;
		}
		break;
	case BNX2X_MAC_LOOPBACK:
		if (CHIP_IS_E3(bp)) {
			int cfg_idx = bnx2x_get_link_cfg_idx(bp);
			if (bp->port.supported[cfg_idx] &
			    (SUPPORTED_10000baseT_Full |
			     SUPPORTED_20000baseMLD2_Full |
			     SUPPORTED_20000baseKR2_Full))
				bp->link_params.loopback_mode = LOOPBACK_XMAC;
			else
				bp->link_params.loopback_mode = LOOPBACK_UMAC;
		} else
			bp->link_params.loopback_mode = LOOPBACK_BMAC;

		bnx2x_phy_init(&bp->link_params, &bp->link_vars);
		break;
	case BNX2X_EXT_LOOPBACK:
		if (bp->link_params.loopback_mode != LOOPBACK_EXT) {
			DP(BNX2X_MSG_ETHTOOL,
			   "Can't configure external loopback\n");
			return -EINVAL;
		}
		break;
	default:
		DP(BNX2X_MSG_ETHTOOL, "Command parameters not supported\n");
		return -EINVAL;
	}

	/* prepare the loopback packet */
	pkt_size = (((bp->dev->mtu < ETH_MAX_PACKET_SIZE) ?
		     bp->dev->mtu : ETH_MAX_PACKET_SIZE) + ETH_HLEN);
	skb = netdev_alloc_skb(bp->dev, fp_rx->rx_buf_size);
	if (!skb) {
		DP(BNX2X_MSG_ETHTOOL, "Can't allocate skb\n");
		rc = -ENOMEM;
		goto test_loopback_exit;
	}
	packet = skb_put(skb, pkt_size);
	memcpy(packet, bp->dev->dev_addr, ETH_ALEN);
	memset(packet + ETH_ALEN, 0, ETH_ALEN);
	memset(packet + 2*ETH_ALEN, 0x77, (ETH_HLEN - 2*ETH_ALEN));
	for (i = ETH_HLEN; i < pkt_size; i++)
		packet[i] = (unsigned char) (i & 0xff);
	mapping = dma_map_single(&bp->pdev->dev, skb->data,
				 skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(&bp->pdev->dev, mapping))) {
		rc = -ENOMEM;
		dev_kfree_skb(skb);
		DP(BNX2X_MSG_ETHTOOL, "Unable to map SKB\n");
		goto test_loopback_exit;
	}

	/* send the loopback packet */
	num_pkts = 0;
	tx_start_idx = le16_to_cpu(*txdata->tx_cons_sb);
	rx_start_idx = le16_to_cpu(*fp_rx->rx_cons_sb);

	netdev_tx_sent_queue(txq, skb->len);

	pkt_prod = txdata->tx_pkt_prod++;
	tx_buf = &txdata->tx_buf_ring[TX_BD(pkt_prod)];
	tx_buf->first_bd = txdata->tx_bd_prod;
	tx_buf->skb = skb;
	tx_buf->flags = 0;

	bd_prod = TX_BD(txdata->tx_bd_prod);
	tx_start_bd = &txdata->tx_desc_ring[bd_prod].start_bd;
	tx_start_bd->addr_hi = cpu_to_le32(U64_HI(mapping));
	tx_start_bd->addr_lo = cpu_to_le32(U64_LO(mapping));
	tx_start_bd->nbd = cpu_to_le16(2); /* start + pbd */
	tx_start_bd->nbytes = cpu_to_le16(skb_headlen(skb));
	tx_start_bd->vlan_or_ethertype = cpu_to_le16(pkt_prod);
	tx_start_bd->bd_flags.as_bitfield = ETH_TX_BD_FLAGS_START_BD;
	SET_FLAG(tx_start_bd->general_data,
		 ETH_TX_START_BD_HDR_NBDS,
		 1);
	SET_FLAG(tx_start_bd->general_data,
		 ETH_TX_START_BD_PARSE_NBDS,
		 0);

	/* turn on parsing and get a BD */
	bd_prod = TX_BD(NEXT_TX_IDX(bd_prod));

	if (CHIP_IS_E1x(bp)) {
		u16 global_data = 0;
		struct eth_tx_parse_bd_e1x  *pbd_e1x =
			&txdata->tx_desc_ring[bd_prod].parse_bd_e1x;
		memset(pbd_e1x, 0, sizeof(struct eth_tx_parse_bd_e1x));
		SET_FLAG(global_data,
			 ETH_TX_PARSE_BD_E1X_ETH_ADDR_TYPE, UNICAST_ADDRESS);
		pbd_e1x->global_data = cpu_to_le16(global_data);
	} else {
		u32 parsing_data = 0;
		struct eth_tx_parse_bd_e2  *pbd_e2 =
			&txdata->tx_desc_ring[bd_prod].parse_bd_e2;
		memset(pbd_e2, 0, sizeof(struct eth_tx_parse_bd_e2));
		SET_FLAG(parsing_data,
			 ETH_TX_PARSE_BD_E2_ETH_ADDR_TYPE, UNICAST_ADDRESS);
		pbd_e2->parsing_data = cpu_to_le32(parsing_data);
	}
	wmb();

	txdata->tx_db.data.prod += 2;
	barrier();
	DOORBELL(bp, txdata->cid, txdata->tx_db.raw);

	mmiowb();
	barrier();

	num_pkts++;
	txdata->tx_bd_prod += 2; /* start + pbd */

	udelay(100);

	tx_idx = le16_to_cpu(*txdata->tx_cons_sb);
	if (tx_idx != tx_start_idx + num_pkts)
		goto test_loopback_exit;

	/* Unlike HC IGU won't generate an interrupt for status block
	 * updates that have been performed while interrupts were
	 * disabled.
	 */
	if (bp->common.int_block == INT_BLOCK_IGU) {
		/* Disable local BHes to prevent a dead-lock situation between
		 * sch_direct_xmit() and bnx2x_run_loopback() (calling
		 * bnx2x_tx_int()), as both are taking netif_tx_lock().
		 */
		local_bh_disable();
		bnx2x_tx_int(bp, txdata);
		local_bh_enable();
	}

	rx_idx = le16_to_cpu(*fp_rx->rx_cons_sb);
	if (rx_idx != rx_start_idx + num_pkts)
		goto test_loopback_exit;

	cqe = &fp_rx->rx_comp_ring[RCQ_BD(fp_rx->rx_comp_cons)];
	cqe_fp_flags = cqe->fast_path_cqe.type_error_flags;
	cqe_fp_type = cqe_fp_flags & ETH_FAST_PATH_RX_CQE_TYPE;
	if (!CQE_TYPE_FAST(cqe_fp_type) || (cqe_fp_flags & ETH_RX_ERROR_FALGS))
		goto test_loopback_rx_exit;

	len = le16_to_cpu(cqe->fast_path_cqe.pkt_len_or_gro_seg_len);
	if (len != pkt_size)
		goto test_loopback_rx_exit;

	rx_buf = &fp_rx->rx_buf_ring[RX_BD(fp_rx->rx_bd_cons)];
	dma_sync_single_for_cpu(&bp->pdev->dev,
				   dma_unmap_addr(rx_buf, mapping),
				   fp_rx->rx_buf_size, DMA_FROM_DEVICE);
	data = rx_buf->data + NET_SKB_PAD + cqe->fast_path_cqe.placement_offset;
	for (i = ETH_HLEN; i < pkt_size; i++)
		if (*(data + i) != (unsigned char) (i & 0xff))
			goto test_loopback_rx_exit;

	rc = 0;

test_loopback_rx_exit:

	fp_rx->rx_bd_cons = NEXT_RX_IDX(fp_rx->rx_bd_cons);
	fp_rx->rx_bd_prod = NEXT_RX_IDX(fp_rx->rx_bd_prod);
	fp_rx->rx_comp_cons = NEXT_RCQ_IDX(fp_rx->rx_comp_cons);
	fp_rx->rx_comp_prod = NEXT_RCQ_IDX(fp_rx->rx_comp_prod);

	/* Update producers */
	bnx2x_update_rx_prod(bp, fp_rx, fp_rx->rx_bd_prod, fp_rx->rx_comp_prod,
			     fp_rx->rx_sge_prod);

test_loopback_exit:
	bp->link_params.loopback_mode = LOOPBACK_NONE;

	return rc;
}

static int bnx2x_test_loopback(struct bnx2x *bp)
{
	int rc = 0, res;

	if (BP_NOMCP(bp))
		return rc;

	if (!netif_running(bp->dev))
		return BNX2X_LOOPBACK_FAILED;

	bnx2x_netif_stop(bp, 1);
	bnx2x_acquire_phy_lock(bp);

	res = bnx2x_run_loopback(bp, BNX2X_PHY_LOOPBACK);
	if (res) {
		DP(BNX2X_MSG_ETHTOOL, "  PHY loopback failed  (res %d)\n", res);
		rc |= BNX2X_PHY_LOOPBACK_FAILED;
	}

	res = bnx2x_run_loopback(bp, BNX2X_MAC_LOOPBACK);
	if (res) {
		DP(BNX2X_MSG_ETHTOOL, "  MAC loopback failed  (res %d)\n", res);
		rc |= BNX2X_MAC_LOOPBACK_FAILED;
	}

	bnx2x_release_phy_lock(bp);
	bnx2x_netif_start(bp);

	return rc;
}

static int bnx2x_test_ext_loopback(struct bnx2x *bp)
{
	int rc;
	u8 is_serdes =
		(bp->link_vars.link_status & LINK_STATUS_SERDES_LINK) > 0;

	if (BP_NOMCP(bp))
		return -ENODEV;

	if (!netif_running(bp->dev))
		return BNX2X_EXT_LOOPBACK_FAILED;

	bnx2x_nic_unload(bp, UNLOAD_NORMAL, false);
	rc = bnx2x_nic_load(bp, LOAD_LOOPBACK_EXT);
	if (rc) {
		DP(BNX2X_MSG_ETHTOOL,
		   "Can't perform self-test, nic_load (for external lb) failed\n");
		return -ENODEV;
	}
	bnx2x_wait_for_link(bp, 1, is_serdes);

	bnx2x_netif_stop(bp, 1);

	rc = bnx2x_run_loopback(bp, BNX2X_EXT_LOOPBACK);
	if (rc)
		DP(BNX2X_MSG_ETHTOOL, "EXT loopback failed  (res %d)\n", rc);

	bnx2x_netif_start(bp);

	return rc;
}

struct code_entry {
	u32 sram_start_addr;
	u32 code_attribute;
#define CODE_IMAGE_TYPE_MASK			0xf0800003
#define CODE_IMAGE_VNTAG_PROFILES_DATA		0xd0000003
#define CODE_IMAGE_LENGTH_MASK			0x007ffffc
#define CODE_IMAGE_TYPE_EXTENDED_DIR		0xe0000000
	u32 nvm_start_addr;
};

#define CODE_ENTRY_MAX			16
#define CODE_ENTRY_EXTENDED_DIR_IDX	15
#define MAX_IMAGES_IN_EXTENDED_DIR	64
#define NVRAM_DIR_OFFSET		0x14

#define EXTENDED_DIR_EXISTS(code)					  \
	((code & CODE_IMAGE_TYPE_MASK) == CODE_IMAGE_TYPE_EXTENDED_DIR && \
	 (code & CODE_IMAGE_LENGTH_MASK) != 0)

#define CRC32_RESIDUAL			0xdebb20e3
#define CRC_BUFF_SIZE			256

static int bnx2x_nvram_crc(struct bnx2x *bp,
			   int offset,
			   int size,
			   u8 *buff)
{
	u32 crc = ~0;
	int rc = 0, done = 0;

	DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
	   "NVRAM CRC from 0x%08x to 0x%08x\n", offset, offset + size);

	while (done < size) {
		int count = min_t(int, size - done, CRC_BUFF_SIZE);

		rc = bnx2x_nvram_read(bp, offset + done, buff, count);

		if (rc)
			return rc;

		crc = crc32_le(crc, buff, count);
		done += count;
	}

	if (crc != CRC32_RESIDUAL)
		rc = -EINVAL;

	return rc;
}

static int bnx2x_test_nvram_dir(struct bnx2x *bp,
				struct code_entry *entry,
				u8 *buff)
{
	size_t size = entry->code_attribute & CODE_IMAGE_LENGTH_MASK;
	u32 type = entry->code_attribute & CODE_IMAGE_TYPE_MASK;
	int rc;

	/* Zero-length images and AFEX profiles do not have CRC */
	if (size == 0 || type == CODE_IMAGE_VNTAG_PROFILES_DATA)
		return 0;

	rc = bnx2x_nvram_crc(bp, entry->nvm_start_addr, size, buff);
	if (rc)
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "image %x has failed crc test (rc %d)\n", type, rc);

	return rc;
}

static int bnx2x_test_dir_entry(struct bnx2x *bp, u32 addr, u8 *buff)
{
	int rc;
	struct code_entry entry;

	rc = bnx2x_nvram_read32(bp, addr, (u32 *)&entry, sizeof(entry));
	if (rc)
		return rc;

	return bnx2x_test_nvram_dir(bp, &entry, buff);
}

static int bnx2x_test_nvram_ext_dirs(struct bnx2x *bp, u8 *buff)
{
	u32 rc, cnt, dir_offset = NVRAM_DIR_OFFSET;
	struct code_entry entry;
	int i;

	rc = bnx2x_nvram_read32(bp,
				dir_offset +
				sizeof(entry) * CODE_ENTRY_EXTENDED_DIR_IDX,
				(u32 *)&entry, sizeof(entry));
	if (rc)
		return rc;

	if (!EXTENDED_DIR_EXISTS(entry.code_attribute))
		return 0;

	rc = bnx2x_nvram_read32(bp, entry.nvm_start_addr,
				&cnt, sizeof(u32));
	if (rc)
		return rc;

	dir_offset = entry.nvm_start_addr + 8;

	for (i = 0; i < cnt && i < MAX_IMAGES_IN_EXTENDED_DIR; i++) {
		rc = bnx2x_test_dir_entry(bp, dir_offset +
					      sizeof(struct code_entry) * i,
					  buff);
		if (rc)
			return rc;
	}

	return 0;
}

static int bnx2x_test_nvram_dirs(struct bnx2x *bp, u8 *buff)
{
	u32 rc, dir_offset = NVRAM_DIR_OFFSET;
	int i;

	DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM, "NVRAM DIRS CRC test-set\n");

	for (i = 0; i < CODE_ENTRY_EXTENDED_DIR_IDX; i++) {
		rc = bnx2x_test_dir_entry(bp, dir_offset +
					      sizeof(struct code_entry) * i,
					  buff);
		if (rc)
			return rc;
	}

	return bnx2x_test_nvram_ext_dirs(bp, buff);
}

struct crc_pair {
	int offset;
	int size;
};

static int bnx2x_test_nvram_tbl(struct bnx2x *bp,
				const struct crc_pair *nvram_tbl, u8 *buf)
{
	int i;

	for (i = 0; nvram_tbl[i].size; i++) {
		int rc = bnx2x_nvram_crc(bp, nvram_tbl[i].offset,
					 nvram_tbl[i].size, buf);
		if (rc) {
			DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
			   "nvram_tbl[%d] has failed crc test (rc %d)\n",
			   i, rc);
			return rc;
		}
	}

	return 0;
}

static int bnx2x_test_nvram(struct bnx2x *bp)
{
	const struct crc_pair nvram_tbl[] = {
		{     0,  0x14 }, /* bootstrap */
		{  0x14,  0xec }, /* dir */
		{ 0x100, 0x350 }, /* manuf_info */
		{ 0x450,  0xf0 }, /* feature_info */
		{ 0x640,  0x64 }, /* upgrade_key_info */
		{ 0x708,  0x70 }, /* manuf_key_info */
		{     0,     0 }
	};
	const struct crc_pair nvram_tbl2[] = {
		{ 0x7e8, 0x350 }, /* manuf_info2 */
		{ 0xb38,  0xf0 }, /* feature_info */
		{     0,     0 }
	};

	u8 *buf;
	int rc;
	u32 magic;

	if (BP_NOMCP(bp))
		return 0;

	buf = kmalloc(CRC_BUFF_SIZE, GFP_KERNEL);
	if (!buf) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM, "kmalloc failed\n");
		rc = -ENOMEM;
		goto test_nvram_exit;
	}

	rc = bnx2x_nvram_read32(bp, 0, &magic, sizeof(magic));
	if (rc) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "magic value read (rc %d)\n", rc);
		goto test_nvram_exit;
	}

	if (magic != 0x669955aa) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "wrong magic value (0x%08x)\n", magic);
		rc = -ENODEV;
		goto test_nvram_exit;
	}

	DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM, "Port 0 CRC test-set\n");
	rc = bnx2x_test_nvram_tbl(bp, nvram_tbl, buf);
	if (rc)
		goto test_nvram_exit;

	if (!CHIP_IS_E1x(bp) && !CHIP_IS_57811xx(bp)) {
		u32 hide = SHMEM_RD(bp, dev_info.shared_hw_config.config2) &
			   SHARED_HW_CFG_HIDE_PORT1;

		if (!hide) {
			DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
			   "Port 1 CRC test-set\n");
			rc = bnx2x_test_nvram_tbl(bp, nvram_tbl2, buf);
			if (rc)
				goto test_nvram_exit;
		}
	}

	rc = bnx2x_test_nvram_dirs(bp, buf);

test_nvram_exit:
	kfree(buf);
	return rc;
}

/* Send an EMPTY ramrod on the first queue */
static int bnx2x_test_intr(struct bnx2x *bp)
{
	struct bnx2x_queue_state_params params = {NULL};

	if (!netif_running(bp->dev)) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "cannot access eeprom when the interface is down\n");
		return -ENODEV;
	}

	params.q_obj = &bp->sp_objs->q_obj;
	params.cmd = BNX2X_Q_CMD_EMPTY;

	__set_bit(RAMROD_COMP_WAIT, &params.ramrod_flags);

	return bnx2x_queue_state_change(bp, &params);
}

static void bnx2x_self_test(struct net_device *dev,
			    struct ethtool_test *etest, u64 *buf)
{
	struct bnx2x *bp = netdev_priv(dev);
	u8 is_serdes, link_up;
	int rc, cnt = 0;

	if (bp->recovery_state != BNX2X_RECOVERY_DONE) {
		netdev_err(bp->dev,
			   "Handling parity error recovery. Try again later\n");
		etest->flags |= ETH_TEST_FL_FAILED;
		return;
	}

	DP(BNX2X_MSG_ETHTOOL,
	   "Self-test command parameters: offline = %d, external_lb = %d\n",
	   (etest->flags & ETH_TEST_FL_OFFLINE),
	   (etest->flags & ETH_TEST_FL_EXTERNAL_LB)>>2);

	memset(buf, 0, sizeof(u64) * BNX2X_NUM_TESTS(bp));

	if (!netif_running(dev)) {
		DP(BNX2X_MSG_ETHTOOL,
		   "Can't perform self-test when interface is down\n");
		return;
	}

	is_serdes = (bp->link_vars.link_status & LINK_STATUS_SERDES_LINK) > 0;
	link_up = bp->link_vars.link_up;
	/* offline tests are not supported in MF mode */
	if ((etest->flags & ETH_TEST_FL_OFFLINE) && !IS_MF(bp)) {
		int port = BP_PORT(bp);
		u32 val;

		/* save current value of input enable for TX port IF */
		val = REG_RD(bp, NIG_REG_EGRESS_UMP0_IN_EN + port*4);
		/* disable input for TX port IF */
		REG_WR(bp, NIG_REG_EGRESS_UMP0_IN_EN + port*4, 0);

		bnx2x_nic_unload(bp, UNLOAD_NORMAL, false);
		rc = bnx2x_nic_load(bp, LOAD_DIAG);
		if (rc) {
			etest->flags |= ETH_TEST_FL_FAILED;
			DP(BNX2X_MSG_ETHTOOL,
			   "Can't perform self-test, nic_load (for offline) failed\n");
			return;
		}

		/* wait until link state is restored */
		bnx2x_wait_for_link(bp, 1, is_serdes);

		if (bnx2x_test_registers(bp) != 0) {
			buf[0] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}
		if (bnx2x_test_memory(bp) != 0) {
			buf[1] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}

		buf[2] = bnx2x_test_loopback(bp); /* internal LB */
		if (buf[2] != 0)
			etest->flags |= ETH_TEST_FL_FAILED;

		if (etest->flags & ETH_TEST_FL_EXTERNAL_LB) {
			buf[3] = bnx2x_test_ext_loopback(bp); /* external LB */
			if (buf[3] != 0)
				etest->flags |= ETH_TEST_FL_FAILED;
			etest->flags |= ETH_TEST_FL_EXTERNAL_LB_DONE;
		}

		bnx2x_nic_unload(bp, UNLOAD_NORMAL, false);

		/* restore input for TX port IF */
		REG_WR(bp, NIG_REG_EGRESS_UMP0_IN_EN + port*4, val);
		rc = bnx2x_nic_load(bp, LOAD_NORMAL);
		if (rc) {
			etest->flags |= ETH_TEST_FL_FAILED;
			DP(BNX2X_MSG_ETHTOOL,
			   "Can't perform self-test, nic_load (for online) failed\n");
			return;
		}
		/* wait until link state is restored */
		bnx2x_wait_for_link(bp, link_up, is_serdes);
	}
	if (bnx2x_test_nvram(bp) != 0) {
		if (!IS_MF(bp))
			buf[4] = 1;
		else
			buf[0] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}
	if (bnx2x_test_intr(bp) != 0) {
		if (!IS_MF(bp))
			buf[5] = 1;
		else
			buf[1] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

	if (link_up) {
		cnt = 100;
		while (bnx2x_link_test(bp, is_serdes) && --cnt)
			msleep(20);
	}

	if (!cnt) {
		if (!IS_MF(bp))
			buf[6] = 1;
		else
			buf[2] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}
}

#define IS_PORT_STAT(i) \
	((bnx2x_stats_arr[i].flags & STATS_FLAGS_BOTH) == STATS_FLAGS_PORT)
#define IS_FUNC_STAT(i)		(bnx2x_stats_arr[i].flags & STATS_FLAGS_FUNC)
#define IS_MF_MODE_STAT(bp) \
			(IS_MF(bp) && !(bp->msg_enable & BNX2X_MSG_STATS))

/* ethtool statistics are displayed for all regular ethernet queues and the
 * fcoe L2 queue if not disabled
 */
static int bnx2x_num_stat_queues(struct bnx2x *bp)
{
	return BNX2X_NUM_ETH_QUEUES(bp);
}

static int bnx2x_get_sset_count(struct net_device *dev, int stringset)
{
	struct bnx2x *bp = netdev_priv(dev);
	int i, num_strings = 0;

	switch (stringset) {
	case ETH_SS_STATS:
		if (is_multi(bp)) {
			num_strings = bnx2x_num_stat_queues(bp) *
				      BNX2X_NUM_Q_STATS;
		} else
			num_strings = 0;
		if (IS_MF_MODE_STAT(bp)) {
			for (i = 0; i < BNX2X_NUM_STATS; i++)
				if (IS_FUNC_STAT(i))
					num_strings++;
		} else
			num_strings += BNX2X_NUM_STATS;

		return num_strings;

	case ETH_SS_TEST:
		return BNX2X_NUM_TESTS(bp);

	case ETH_SS_PRIV_FLAGS:
		return BNX2X_PRI_FLAG_LEN;

	default:
		return -EINVAL;
	}
}

static u32 bnx2x_get_private_flags(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 flags = 0;

	flags |= (!(bp->flags & NO_ISCSI_FLAG) ? 1 : 0) << BNX2X_PRI_FLAG_ISCSI;
	flags |= (!(bp->flags & NO_FCOE_FLAG)  ? 1 : 0) << BNX2X_PRI_FLAG_FCOE;
	flags |= (!!IS_MF_STORAGE_ONLY(bp)) << BNX2X_PRI_FLAG_STORAGE;

	return flags;
}

static void bnx2x_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	struct bnx2x *bp = netdev_priv(dev);
	int i, j, k, start;
	char queue_name[MAX_QUEUE_NAME_LEN+1];

	switch (stringset) {
	case ETH_SS_STATS:
		k = 0;
		if (is_multi(bp)) {
			for_each_eth_queue(bp, i) {
				memset(queue_name, 0, sizeof(queue_name));
				sprintf(queue_name, "%d", i);
				for (j = 0; j < BNX2X_NUM_Q_STATS; j++)
					snprintf(buf + (k + j)*ETH_GSTRING_LEN,
						ETH_GSTRING_LEN,
						bnx2x_q_stats_arr[j].string,
						queue_name);
				k += BNX2X_NUM_Q_STATS;
			}
		}

		for (i = 0, j = 0; i < BNX2X_NUM_STATS; i++) {
			if (IS_MF_MODE_STAT(bp) && IS_PORT_STAT(i))
				continue;
			strcpy(buf + (k + j)*ETH_GSTRING_LEN,
				   bnx2x_stats_arr[i].string);
			j++;
		}

		break;

	case ETH_SS_TEST:
		/* First 4 tests cannot be done in MF mode */
		if (!IS_MF(bp))
			start = 0;
		else
			start = 4;
		memcpy(buf, bnx2x_tests_str_arr + start,
		       ETH_GSTRING_LEN * BNX2X_NUM_TESTS(bp));
		break;

	case ETH_SS_PRIV_FLAGS:
		memcpy(buf, bnx2x_private_arr,
		       ETH_GSTRING_LEN * BNX2X_PRI_FLAG_LEN);
		break;
	}
}

static void bnx2x_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *buf)
{
	struct bnx2x *bp = netdev_priv(dev);
	u32 *hw_stats, *offset;
	int i, j, k = 0;

	if (is_multi(bp)) {
		for_each_eth_queue(bp, i) {
			hw_stats = (u32 *)&bp->fp_stats[i].eth_q_stats;
			for (j = 0; j < BNX2X_NUM_Q_STATS; j++) {
				if (bnx2x_q_stats_arr[j].size == 0) {
					/* skip this counter */
					buf[k + j] = 0;
					continue;
				}
				offset = (hw_stats +
					  bnx2x_q_stats_arr[j].offset);
				if (bnx2x_q_stats_arr[j].size == 4) {
					/* 4-byte counter */
					buf[k + j] = (u64) *offset;
					continue;
				}
				/* 8-byte counter */
				buf[k + j] = HILO_U64(*offset, *(offset + 1));
			}
			k += BNX2X_NUM_Q_STATS;
		}
	}

	hw_stats = (u32 *)&bp->eth_stats;
	for (i = 0, j = 0; i < BNX2X_NUM_STATS; i++) {
		if (IS_MF_MODE_STAT(bp) && IS_PORT_STAT(i))
			continue;
		if (bnx2x_stats_arr[i].size == 0) {
			/* skip this counter */
			buf[k + j] = 0;
			j++;
			continue;
		}
		offset = (hw_stats + bnx2x_stats_arr[i].offset);
		if (bnx2x_stats_arr[i].size == 4) {
			/* 4-byte counter */
			buf[k + j] = (u64) *offset;
			j++;
			continue;
		}
		/* 8-byte counter */
		buf[k + j] = HILO_U64(*offset, *(offset + 1));
		j++;
	}
}

static int bnx2x_set_phys_id(struct net_device *dev,
			     enum ethtool_phys_id_state state)
{
	struct bnx2x *bp = netdev_priv(dev);

	if (!bnx2x_is_nvm_accessible(bp)) {
		DP(BNX2X_MSG_ETHTOOL | BNX2X_MSG_NVM,
		   "cannot access eeprom when the interface is down\n");
		return -EAGAIN;
	}

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		return 1;	/* cycle on/off once per second */

	case ETHTOOL_ID_ON:
		bnx2x_acquire_phy_lock(bp);
		bnx2x_set_led(&bp->link_params, &bp->link_vars,
			      LED_MODE_ON, SPEED_1000);
		bnx2x_release_phy_lock(bp);
		break;

	case ETHTOOL_ID_OFF:
		bnx2x_acquire_phy_lock(bp);
		bnx2x_set_led(&bp->link_params, &bp->link_vars,
			      LED_MODE_FRONT_PANEL_OFF, 0);
		bnx2x_release_phy_lock(bp);
		break;

	case ETHTOOL_ID_INACTIVE:
		bnx2x_acquire_phy_lock(bp);
		bnx2x_set_led(&bp->link_params, &bp->link_vars,
			      LED_MODE_OPER,
			      bp->link_vars.line_speed);
		bnx2x_release_phy_lock(bp);
	}

	return 0;
}

static int bnx2x_get_rss_flags(struct bnx2x *bp, struct ethtool_rxnfc *info)
{
	switch (info->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		info->data = RXH_IP_SRC | RXH_IP_DST |
			     RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V4_FLOW:
		if (bp->rss_conf_obj.udp_rss_v4)
			info->data = RXH_IP_SRC | RXH_IP_DST |
				     RXH_L4_B_0_1 | RXH_L4_B_2_3;
		else
			info->data = RXH_IP_SRC | RXH_IP_DST;
		break;
	case UDP_V6_FLOW:
		if (bp->rss_conf_obj.udp_rss_v6)
			info->data = RXH_IP_SRC | RXH_IP_DST |
				     RXH_L4_B_0_1 | RXH_L4_B_2_3;
		else
			info->data = RXH_IP_SRC | RXH_IP_DST;
		break;
	case IPV4_FLOW:
	case IPV6_FLOW:
		info->data = RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		info->data = 0;
		break;
	}

	return 0;
}

static int bnx2x_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info,
			   u32 *rules __always_unused)
{
	struct bnx2x *bp = netdev_priv(dev);

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = BNX2X_NUM_ETH_QUEUES(bp);
		return 0;
	case ETHTOOL_GRXFH:
		return bnx2x_get_rss_flags(bp, info);
	default:
		DP(BNX2X_MSG_ETHTOOL, "Command parameters not supported\n");
		return -EOPNOTSUPP;
	}
}

static int bnx2x_set_rss_flags(struct bnx2x *bp, struct ethtool_rxnfc *info)
{
	int udp_rss_requested;

	DP(BNX2X_MSG_ETHTOOL,
	   "Set rss flags command parameters: flow type = %d, data = %llu\n",
	   info->flow_type, info->data);

	switch (info->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		/* For TCP only 4-tupple hash is supported */
		if (info->data ^ (RXH_IP_SRC | RXH_IP_DST |
				  RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
			DP(BNX2X_MSG_ETHTOOL,
			   "Command parameters not supported\n");
			return -EINVAL;
		}
		return 0;

	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
		/* For UDP either 2-tupple hash or 4-tupple hash is supported */
		if (info->data == (RXH_IP_SRC | RXH_IP_DST |
				   RXH_L4_B_0_1 | RXH_L4_B_2_3))
			udp_rss_requested = 1;
		else if (info->data == (RXH_IP_SRC | RXH_IP_DST))
			udp_rss_requested = 0;
		else
			return -EINVAL;
		if ((info->flow_type == UDP_V4_FLOW) &&
		    (bp->rss_conf_obj.udp_rss_v4 != udp_rss_requested)) {
			bp->rss_conf_obj.udp_rss_v4 = udp_rss_requested;
			DP(BNX2X_MSG_ETHTOOL,
			   "rss re-configured, UDP 4-tupple %s\n",
			   udp_rss_requested ? "enabled" : "disabled");
			return bnx2x_config_rss_pf(bp, &bp->rss_conf_obj, 0);
		} else if ((info->flow_type == UDP_V6_FLOW) &&
			   (bp->rss_conf_obj.udp_rss_v6 != udp_rss_requested)) {
			bp->rss_conf_obj.udp_rss_v6 = udp_rss_requested;
			DP(BNX2X_MSG_ETHTOOL,
			   "rss re-configured, UDP 4-tupple %s\n",
			   udp_rss_requested ? "enabled" : "disabled");
			return bnx2x_config_rss_pf(bp, &bp->rss_conf_obj, 0);
		}
		return 0;

	case IPV4_FLOW:
	case IPV6_FLOW:
		/* For IP only 2-tupple hash is supported */
		if (info->data ^ (RXH_IP_SRC | RXH_IP_DST)) {
			DP(BNX2X_MSG_ETHTOOL,
			   "Command parameters not supported\n");
			return -EINVAL;
		}
		return 0;

	case SCTP_V4_FLOW:
	case AH_ESP_V4_FLOW:
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
	case SCTP_V6_FLOW:
	case AH_ESP_V6_FLOW:
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
	case IP_USER_FLOW:
	case ETHER_FLOW:
		/* RSS is not supported for these protocols */
		if (info->data) {
			DP(BNX2X_MSG_ETHTOOL,
			   "Command parameters not supported\n");
			return -EINVAL;
		}
		return 0;

	default:
		return -EINVAL;
	}
}

static int bnx2x_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info)
{
	struct bnx2x *bp = netdev_priv(dev);

	switch (info->cmd) {
	case ETHTOOL_SRXFH:
		return bnx2x_set_rss_flags(bp, info);
	default:
		DP(BNX2X_MSG_ETHTOOL, "Command parameters not supported\n");
		return -EOPNOTSUPP;
	}
}

static u32 bnx2x_get_rxfh_indir_size(struct net_device *dev)
{
	return T_ETH_INDIRECTION_TABLE_SIZE;
}

static int bnx2x_get_rxfh_indir(struct net_device *dev, u32 *indir)
{
	struct bnx2x *bp = netdev_priv(dev);
	u8 ind_table[T_ETH_INDIRECTION_TABLE_SIZE] = {0};
	size_t i;

	/* Get the current configuration of the RSS indirection table */
	bnx2x_get_rss_ind_table(&bp->rss_conf_obj, ind_table);

	/*
	 * We can't use a memcpy() as an internal storage of an
	 * indirection table is a u8 array while indir->ring_index
	 * points to an array of u32.
	 *
	 * Indirection table contains the FW Client IDs, so we need to
	 * align the returned table to the Client ID of the leading RSS
	 * queue.
	 */
	for (i = 0; i < T_ETH_INDIRECTION_TABLE_SIZE; i++)
		indir[i] = ind_table[i] - bp->fp->cl_id;

	return 0;
}

static int bnx2x_set_rxfh_indir(struct net_device *dev, const u32 *indir)
{
	struct bnx2x *bp = netdev_priv(dev);
	size_t i;

	for (i = 0; i < T_ETH_INDIRECTION_TABLE_SIZE; i++) {
		/*
		 * The same as in bnx2x_get_rxfh_indir: we can't use a memcpy()
		 * as an internal storage of an indirection table is a u8 array
		 * while indir->ring_index points to an array of u32.
		 *
		 * Indirection table contains the FW Client IDs, so we need to
		 * align the received table to the Client ID of the leading RSS
		 * queue
		 */
		bp->rss_conf_obj.ind_table[i] = indir[i] + bp->fp->cl_id;
	}

	return bnx2x_config_rss_eth(bp, false);
}

/**
 * bnx2x_get_channels - gets the number of RSS queues.
 *
 * @dev:		net device
 * @channels:		returns the number of max / current queues
 */
static void bnx2x_get_channels(struct net_device *dev,
			       struct ethtool_channels *channels)
{
	struct bnx2x *bp = netdev_priv(dev);

	channels->max_combined = BNX2X_MAX_RSS_COUNT(bp);
	channels->combined_count = BNX2X_NUM_ETH_QUEUES(bp);
}

/**
 * bnx2x_change_num_queues - change the number of RSS queues.
 *
 * @bp:			bnx2x private structure
 *
 * Re-configure interrupt mode to get the new number of MSI-X
 * vectors and re-add NAPI objects.
 */
static void bnx2x_change_num_queues(struct bnx2x *bp, int num_rss)
{
	bnx2x_disable_msi(bp);
	bp->num_ethernet_queues = num_rss;
	bp->num_queues = bp->num_ethernet_queues + bp->num_cnic_queues;
	BNX2X_DEV_INFO("set number of queues to %d\n", bp->num_queues);
	bnx2x_set_int_mode(bp);
}

/**
 * bnx2x_set_channels - sets the number of RSS queues.
 *
 * @dev:		net device
 * @channels:		includes the number of queues requested
 */
static int bnx2x_set_channels(struct net_device *dev,
			      struct ethtool_channels *channels)
{
	struct bnx2x *bp = netdev_priv(dev);

	DP(BNX2X_MSG_ETHTOOL,
	   "set-channels command parameters: rx = %d, tx = %d, other = %d, combined = %d\n",
	   channels->rx_count, channels->tx_count, channels->other_count,
	   channels->combined_count);

	/* We don't support separate rx / tx channels.
	 * We don't allow setting 'other' channels.
	 */
	if (channels->rx_count || channels->tx_count || channels->other_count
	    || (channels->combined_count == 0) ||
	    (channels->combined_count > BNX2X_MAX_RSS_COUNT(bp))) {
		DP(BNX2X_MSG_ETHTOOL, "command parameters not supported\n");
		return -EINVAL;
	}

	/* Check if there was a change in the active parameters */
	if (channels->combined_count == BNX2X_NUM_ETH_QUEUES(bp)) {
		DP(BNX2X_MSG_ETHTOOL, "No change in active parameters\n");
		return 0;
	}

	/* Set the requested number of queues in bp context.
	 * Note that the actual number of queues created during load may be
	 * less than requested if memory is low.
	 */
	if (unlikely(!netif_running(dev))) {
		bnx2x_change_num_queues(bp, channels->combined_count);
		return 0;
	}
	bnx2x_nic_unload(bp, UNLOAD_NORMAL, true);
	bnx2x_change_num_queues(bp, channels->combined_count);
	return bnx2x_nic_load(bp, LOAD_NORMAL);
}

static const struct ethtool_ops bnx2x_ethtool_ops = {
	.get_settings		= bnx2x_get_settings,
	.set_settings		= bnx2x_set_settings,
	.get_drvinfo		= bnx2x_get_drvinfo,
	.get_regs_len		= bnx2x_get_regs_len,
	.get_regs		= bnx2x_get_regs,
	.get_dump_flag		= bnx2x_get_dump_flag,
	.get_dump_data		= bnx2x_get_dump_data,
	.set_dump		= bnx2x_set_dump,
	.get_wol		= bnx2x_get_wol,
	.set_wol		= bnx2x_set_wol,
	.get_msglevel		= bnx2x_get_msglevel,
	.set_msglevel		= bnx2x_set_msglevel,
	.nway_reset		= bnx2x_nway_reset,
	.get_link		= bnx2x_get_link,
	.get_eeprom_len		= bnx2x_get_eeprom_len,
	.get_eeprom		= bnx2x_get_eeprom,
	.set_eeprom		= bnx2x_set_eeprom,
	.get_coalesce		= bnx2x_get_coalesce,
	.set_coalesce		= bnx2x_set_coalesce,
	.get_ringparam		= bnx2x_get_ringparam,
	.set_ringparam		= bnx2x_set_ringparam,
	.get_pauseparam		= bnx2x_get_pauseparam,
	.set_pauseparam		= bnx2x_set_pauseparam,
	.self_test		= bnx2x_self_test,
	.get_sset_count		= bnx2x_get_sset_count,
	.get_priv_flags		= bnx2x_get_private_flags,
	.get_strings		= bnx2x_get_strings,
	.set_phys_id		= bnx2x_set_phys_id,
	.get_ethtool_stats	= bnx2x_get_ethtool_stats,
	.get_rxnfc		= bnx2x_get_rxnfc,
	.set_rxnfc		= bnx2x_set_rxnfc,
	.get_rxfh_indir_size	= bnx2x_get_rxfh_indir_size,
	.get_rxfh_indir		= bnx2x_get_rxfh_indir,
	.set_rxfh_indir		= bnx2x_set_rxfh_indir,
	.get_channels		= bnx2x_get_channels,
	.set_channels		= bnx2x_set_channels,
	.get_module_info	= bnx2x_get_module_info,
	.get_module_eeprom	= bnx2x_get_module_eeprom,
	.get_eee		= bnx2x_get_eee,
	.set_eee		= bnx2x_set_eee,
	.get_ts_info		= ethtool_op_get_ts_info,
};

static const struct ethtool_ops bnx2x_vf_ethtool_ops = {
	.get_settings		= bnx2x_get_settings,
	.set_settings		= bnx2x_set_settings,
	.get_drvinfo		= bnx2x_get_drvinfo,
	.get_msglevel		= bnx2x_get_msglevel,
	.set_msglevel		= bnx2x_set_msglevel,
	.get_link		= bnx2x_get_link,
	.get_coalesce		= bnx2x_get_coalesce,
	.get_ringparam		= bnx2x_get_ringparam,
	.set_ringparam		= bnx2x_set_ringparam,
	.get_sset_count		= bnx2x_get_sset_count,
	.get_strings		= bnx2x_get_strings,
	.get_ethtool_stats	= bnx2x_get_ethtool_stats,
	.get_rxnfc		= bnx2x_get_rxnfc,
	.set_rxnfc		= bnx2x_set_rxnfc,
	.get_rxfh_indir_size	= bnx2x_get_rxfh_indir_size,
	.get_rxfh_indir		= bnx2x_get_rxfh_indir,
	.set_rxfh_indir		= bnx2x_set_rxfh_indir,
	.get_channels		= bnx2x_get_channels,
	.set_channels		= bnx2x_set_channels,
};

void bnx2x_set_ethtool_ops(struct bnx2x *bp, struct net_device *netdev)
{
	if (IS_PF(bp))
		SET_ETHTOOL_OPS(netdev, &bnx2x_ethtool_ops);
	else /* vf */
		SET_ETHTOOL_OPS(netdev, &bnx2x_vf_ethtool_ops);
}
