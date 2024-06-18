// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2020 Mellanox Technologies. All rights reserved */

#include "reg.h"
#include "core.h"
#include "spectrum.h"
#include "core_env.h"

static const char mlxsw_sp_driver_version[] = "1.0";

static void mlxsw_sp_port_get_drvinfo(struct net_device *dev,
				      struct ethtool_drvinfo *drvinfo)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	strscpy(drvinfo->driver, mlxsw_sp->bus_info->device_kind,
		sizeof(drvinfo->driver));
	strscpy(drvinfo->version, mlxsw_sp_driver_version,
		sizeof(drvinfo->version));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%d.%d.%d",
		 mlxsw_sp->bus_info->fw_rev.major,
		 mlxsw_sp->bus_info->fw_rev.minor,
		 mlxsw_sp->bus_info->fw_rev.subminor);
	strscpy(drvinfo->bus_info, mlxsw_sp->bus_info->device_name,
		sizeof(drvinfo->bus_info));
}

struct mlxsw_sp_ethtool_link_ext_state_opcode_mapping {
	u32 status_opcode;
	enum ethtool_link_ext_state link_ext_state;
	u8 link_ext_substate;
};

static const struct mlxsw_sp_ethtool_link_ext_state_opcode_mapping
mlxsw_sp_link_ext_state_opcode_map[] = {
	{2, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_NO_PARTNER_DETECTED},
	{3, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_ACK_NOT_RECEIVED},
	{4, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_NEXT_PAGE_EXCHANGE_FAILED},
	{36, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_NO_PARTNER_DETECTED_FORCE_MODE},
	{38, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_FEC_MISMATCH_DURING_OVERRIDE},
	{39, ETHTOOL_LINK_EXT_STATE_AUTONEG,
		ETHTOOL_LINK_EXT_SUBSTATE_AN_NO_HCD},

	{5, ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE,
		ETHTOOL_LINK_EXT_SUBSTATE_LT_KR_FRAME_LOCK_NOT_ACQUIRED},
	{6, ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE,
		ETHTOOL_LINK_EXT_SUBSTATE_LT_KR_LINK_INHIBIT_TIMEOUT},
	{7, ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE,
		ETHTOOL_LINK_EXT_SUBSTATE_LT_KR_LINK_PARTNER_DID_NOT_SET_RECEIVER_READY},
	{8, ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE, 0},
	{14, ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE,
		ETHTOOL_LINK_EXT_SUBSTATE_LT_REMOTE_FAULT},

	{9, ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH,
		ETHTOOL_LINK_EXT_SUBSTATE_LLM_PCS_DID_NOT_ACQUIRE_BLOCK_LOCK},
	{10, ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH,
		ETHTOOL_LINK_EXT_SUBSTATE_LLM_PCS_DID_NOT_ACQUIRE_AM_LOCK},
	{11, ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH,
		ETHTOOL_LINK_EXT_SUBSTATE_LLM_PCS_DID_NOT_GET_ALIGN_STATUS},
	{12, ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH,
		ETHTOOL_LINK_EXT_SUBSTATE_LLM_FC_FEC_IS_NOT_LOCKED},
	{13, ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH,
		ETHTOOL_LINK_EXT_SUBSTATE_LLM_RS_FEC_IS_NOT_LOCKED},

	{15, ETHTOOL_LINK_EXT_STATE_BAD_SIGNAL_INTEGRITY, 0},
	{17, ETHTOOL_LINK_EXT_STATE_BAD_SIGNAL_INTEGRITY,
		ETHTOOL_LINK_EXT_SUBSTATE_BSI_LARGE_NUMBER_OF_PHYSICAL_ERRORS},
	{42, ETHTOOL_LINK_EXT_STATE_BAD_SIGNAL_INTEGRITY,
		ETHTOOL_LINK_EXT_SUBSTATE_BSI_UNSUPPORTED_RATE},

	{1024, ETHTOOL_LINK_EXT_STATE_NO_CABLE, 0},

	{16, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE,
		ETHTOOL_LINK_EXT_SUBSTATE_CI_UNSUPPORTED_CABLE},
	{20, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE,
		ETHTOOL_LINK_EXT_SUBSTATE_CI_UNSUPPORTED_CABLE},
	{29, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE,
		ETHTOOL_LINK_EXT_SUBSTATE_CI_UNSUPPORTED_CABLE},
	{1025, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE,
		ETHTOOL_LINK_EXT_SUBSTATE_CI_UNSUPPORTED_CABLE},
	{1029, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE,
		ETHTOOL_LINK_EXT_SUBSTATE_CI_UNSUPPORTED_CABLE},
	{1031, ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE, 0},

	{1027, ETHTOOL_LINK_EXT_STATE_EEPROM_ISSUE, 0},

	{23, ETHTOOL_LINK_EXT_STATE_CALIBRATION_FAILURE, 0},

	{1032, ETHTOOL_LINK_EXT_STATE_POWER_BUDGET_EXCEEDED, 0},

	{1030, ETHTOOL_LINK_EXT_STATE_OVERHEAT, 0},

	{1042, ETHTOOL_LINK_EXT_STATE_MODULE,
	 ETHTOOL_LINK_EXT_SUBSTATE_MODULE_CMIS_NOT_READY},
};

static void
mlxsw_sp_port_set_link_ext_state(struct mlxsw_sp_ethtool_link_ext_state_opcode_mapping
				 link_ext_state_mapping,
				 struct ethtool_link_ext_state_info *link_ext_state_info)
{
	switch (link_ext_state_mapping.link_ext_state) {
	case ETHTOOL_LINK_EXT_STATE_AUTONEG:
		link_ext_state_info->autoneg =
			link_ext_state_mapping.link_ext_substate;
		break;
	case ETHTOOL_LINK_EXT_STATE_LINK_TRAINING_FAILURE:
		link_ext_state_info->link_training =
			link_ext_state_mapping.link_ext_substate;
		break;
	case ETHTOOL_LINK_EXT_STATE_LINK_LOGICAL_MISMATCH:
		link_ext_state_info->link_logical_mismatch =
			link_ext_state_mapping.link_ext_substate;
		break;
	case ETHTOOL_LINK_EXT_STATE_BAD_SIGNAL_INTEGRITY:
		link_ext_state_info->bad_signal_integrity =
			link_ext_state_mapping.link_ext_substate;
		break;
	case ETHTOOL_LINK_EXT_STATE_CABLE_ISSUE:
		link_ext_state_info->cable_issue =
			link_ext_state_mapping.link_ext_substate;
		break;
	case ETHTOOL_LINK_EXT_STATE_MODULE:
		link_ext_state_info->module =
			link_ext_state_mapping.link_ext_substate;
		break;
	default:
		break;
	}

	link_ext_state_info->link_ext_state = link_ext_state_mapping.link_ext_state;
}

static int
mlxsw_sp_port_get_link_ext_state(struct net_device *dev,
				 struct ethtool_link_ext_state_info *link_ext_state_info)
{
	struct mlxsw_sp_ethtool_link_ext_state_opcode_mapping link_ext_state_mapping;
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	char pddr_pl[MLXSW_REG_PDDR_LEN];
	int opcode, err, i;
	u32 status_opcode;

	if (netif_carrier_ok(dev))
		return -ENODATA;

	mlxsw_reg_pddr_pack(pddr_pl, mlxsw_sp_port->local_port,
			    MLXSW_REG_PDDR_PAGE_SELECT_TROUBLESHOOTING_INFO);

	opcode = MLXSW_REG_PDDR_TRBLSH_GROUP_OPCODE_MONITOR;
	mlxsw_reg_pddr_trblsh_group_opcode_set(pddr_pl, opcode);

	err = mlxsw_reg_query(mlxsw_sp_port->mlxsw_sp->core, MLXSW_REG(pddr),
			      pddr_pl);
	if (err)
		return err;

	status_opcode = mlxsw_reg_pddr_trblsh_status_opcode_get(pddr_pl);
	if (!status_opcode)
		return -ENODATA;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_link_ext_state_opcode_map); i++) {
		link_ext_state_mapping = mlxsw_sp_link_ext_state_opcode_map[i];
		if (link_ext_state_mapping.status_opcode == status_opcode) {
			mlxsw_sp_port_set_link_ext_state(link_ext_state_mapping,
							 link_ext_state_info);
			return 0;
		}
	}

	return -ENODATA;
}

static void mlxsw_sp_port_get_pauseparam(struct net_device *dev,
					 struct ethtool_pauseparam *pause)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	pause->rx_pause = mlxsw_sp_port->link.rx_pause;
	pause->tx_pause = mlxsw_sp_port->link.tx_pause;
}

static int mlxsw_sp_port_pause_set(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct ethtool_pauseparam *pause)
{
	char pfcc_pl[MLXSW_REG_PFCC_LEN];

	mlxsw_reg_pfcc_pack(pfcc_pl, mlxsw_sp_port->local_port);
	mlxsw_reg_pfcc_pprx_set(pfcc_pl, pause->rx_pause);
	mlxsw_reg_pfcc_pptx_set(pfcc_pl, pause->tx_pause);

	return mlxsw_reg_write(mlxsw_sp_port->mlxsw_sp->core, MLXSW_REG(pfcc),
			       pfcc_pl);
}

/* Maximum delay buffer needed in case of PAUSE frames. Similar to PFC delay, but is
 * measured in bytes. Assumes 100m cable and does not take into account MTU.
 */
#define MLXSW_SP_PAUSE_DELAY_BYTES 19476

static int mlxsw_sp_port_set_pauseparam(struct net_device *dev,
					struct ethtool_pauseparam *pause)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	bool pause_en = pause->tx_pause || pause->rx_pause;
	struct mlxsw_sp_hdroom orig_hdroom;
	struct mlxsw_sp_hdroom hdroom;
	int prio;
	int err;

	if (mlxsw_sp_port->dcb.pfc && mlxsw_sp_port->dcb.pfc->pfc_en) {
		netdev_err(dev, "PFC already enabled on port\n");
		return -EINVAL;
	}

	if (pause->autoneg) {
		netdev_err(dev, "PAUSE frames autonegotiation isn't supported\n");
		return -EINVAL;
	}

	orig_hdroom = *mlxsw_sp_port->hdroom;

	hdroom = orig_hdroom;
	if (pause_en)
		hdroom.delay_bytes = MLXSW_SP_PAUSE_DELAY_BYTES;
	else
		hdroom.delay_bytes = 0;

	for (prio = 0; prio < IEEE_8021QAZ_MAX_TCS; prio++)
		hdroom.prios.prio[prio].lossy = !pause_en;

	mlxsw_sp_hdroom_bufs_reset_lossiness(&hdroom);
	mlxsw_sp_hdroom_bufs_reset_sizes(mlxsw_sp_port, &hdroom);

	err = mlxsw_sp_hdroom_configure(mlxsw_sp_port, &hdroom);
	if (err) {
		netdev_err(dev, "Failed to configure port's headroom\n");
		return err;
	}

	err = mlxsw_sp_port_pause_set(mlxsw_sp_port, pause);
	if (err) {
		netdev_err(dev, "Failed to set PAUSE parameters\n");
		goto err_port_pause_configure;
	}

	mlxsw_sp_port->link.rx_pause = pause->rx_pause;
	mlxsw_sp_port->link.tx_pause = pause->tx_pause;

	return 0;

err_port_pause_configure:
	mlxsw_sp_hdroom_configure(mlxsw_sp_port, &orig_hdroom);
	return err;
}

struct mlxsw_sp_port_hw_stats {
	char str[ETH_GSTRING_LEN];
	u64 (*getter)(const char *payload);
	bool cells_bytes;
};

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_stats[] = {
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

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_rfc_2863_stats[] = {
	{
		.str = "if_in_discards",
		.getter = mlxsw_reg_ppcnt_if_in_discards_get,
	},
	{
		.str = "if_out_discards",
		.getter = mlxsw_reg_ppcnt_if_out_discards_get,
	},
	{
		.str = "if_out_errors",
		.getter = mlxsw_reg_ppcnt_if_out_errors_get,
	},
};

#define MLXSW_SP_PORT_HW_RFC_2863_STATS_LEN \
	ARRAY_SIZE(mlxsw_sp_port_hw_rfc_2863_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_rfc_2819_stats[] = {
	{
		.str = "ether_stats_undersize_pkts",
		.getter = mlxsw_reg_ppcnt_ether_stats_undersize_pkts_get,
	},
	{
		.str = "ether_stats_oversize_pkts",
		.getter = mlxsw_reg_ppcnt_ether_stats_oversize_pkts_get,
	},
	{
		.str = "ether_stats_fragments",
		.getter = mlxsw_reg_ppcnt_ether_stats_fragments_get,
	},
	{
		.str = "ether_pkts64octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts64octets_get,
	},
	{
		.str = "ether_pkts65to127octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts65to127octets_get,
	},
	{
		.str = "ether_pkts128to255octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts128to255octets_get,
	},
	{
		.str = "ether_pkts256to511octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts256to511octets_get,
	},
	{
		.str = "ether_pkts512to1023octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts512to1023octets_get,
	},
	{
		.str = "ether_pkts1024to1518octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts1024to1518octets_get,
	},
	{
		.str = "ether_pkts1519to2047octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts1519to2047octets_get,
	},
	{
		.str = "ether_pkts2048to4095octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts2048to4095octets_get,
	},
	{
		.str = "ether_pkts4096to8191octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts4096to8191octets_get,
	},
	{
		.str = "ether_pkts8192to10239octets",
		.getter = mlxsw_reg_ppcnt_ether_stats_pkts8192to10239octets_get,
	},
};

#define MLXSW_SP_PORT_HW_RFC_2819_STATS_LEN \
	ARRAY_SIZE(mlxsw_sp_port_hw_rfc_2819_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_rfc_3635_stats[] = {
	{
		.str = "dot3stats_fcs_errors",
		.getter = mlxsw_reg_ppcnt_dot3stats_fcs_errors_get,
	},
	{
		.str = "dot3stats_symbol_errors",
		.getter = mlxsw_reg_ppcnt_dot3stats_symbol_errors_get,
	},
	{
		.str = "dot3control_in_unknown_opcodes",
		.getter = mlxsw_reg_ppcnt_dot3control_in_unknown_opcodes_get,
	},
	{
		.str = "dot3in_pause_frames",
		.getter = mlxsw_reg_ppcnt_dot3in_pause_frames_get,
	},
};

#define MLXSW_SP_PORT_HW_RFC_3635_STATS_LEN \
	ARRAY_SIZE(mlxsw_sp_port_hw_rfc_3635_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_ext_stats[] = {
	{
		.str = "ecn_marked",
		.getter = mlxsw_reg_ppcnt_ecn_marked_get,
	},
};

#define MLXSW_SP_PORT_HW_EXT_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_hw_ext_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_discard_stats[] = {
	{
		.str = "discard_ingress_general",
		.getter = mlxsw_reg_ppcnt_ingress_general_get,
	},
	{
		.str = "discard_ingress_policy_engine",
		.getter = mlxsw_reg_ppcnt_ingress_policy_engine_get,
	},
	{
		.str = "discard_ingress_vlan_membership",
		.getter = mlxsw_reg_ppcnt_ingress_vlan_membership_get,
	},
	{
		.str = "discard_ingress_tag_frame_type",
		.getter = mlxsw_reg_ppcnt_ingress_tag_frame_type_get,
	},
	{
		.str = "discard_egress_vlan_membership",
		.getter = mlxsw_reg_ppcnt_egress_vlan_membership_get,
	},
	{
		.str = "discard_loopback_filter",
		.getter = mlxsw_reg_ppcnt_loopback_filter_get,
	},
	{
		.str = "discard_egress_general",
		.getter = mlxsw_reg_ppcnt_egress_general_get,
	},
	{
		.str = "discard_egress_hoq",
		.getter = mlxsw_reg_ppcnt_egress_hoq_get,
	},
	{
		.str = "discard_egress_policy_engine",
		.getter = mlxsw_reg_ppcnt_egress_policy_engine_get,
	},
	{
		.str = "discard_ingress_tx_link_down",
		.getter = mlxsw_reg_ppcnt_ingress_tx_link_down_get,
	},
	{
		.str = "discard_egress_stp_filter",
		.getter = mlxsw_reg_ppcnt_egress_stp_filter_get,
	},
	{
		.str = "discard_egress_sll",
		.getter = mlxsw_reg_ppcnt_egress_sll_get,
	},
};

#define MLXSW_SP_PORT_HW_DISCARD_STATS_LEN \
	ARRAY_SIZE(mlxsw_sp_port_hw_discard_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_prio_stats[] = {
	{
		.str = "rx_octets_prio",
		.getter = mlxsw_reg_ppcnt_rx_octets_get,
	},
	{
		.str = "rx_frames_prio",
		.getter = mlxsw_reg_ppcnt_rx_frames_get,
	},
	{
		.str = "tx_octets_prio",
		.getter = mlxsw_reg_ppcnt_tx_octets_get,
	},
	{
		.str = "tx_frames_prio",
		.getter = mlxsw_reg_ppcnt_tx_frames_get,
	},
	{
		.str = "rx_pause_prio",
		.getter = mlxsw_reg_ppcnt_rx_pause_get,
	},
	{
		.str = "rx_pause_duration_prio",
		.getter = mlxsw_reg_ppcnt_rx_pause_duration_get,
	},
	{
		.str = "tx_pause_prio",
		.getter = mlxsw_reg_ppcnt_tx_pause_get,
	},
	{
		.str = "tx_pause_duration_prio",
		.getter = mlxsw_reg_ppcnt_tx_pause_duration_get,
	},
};

#define MLXSW_SP_PORT_HW_PRIO_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_hw_prio_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_tc_stats[] = {
	{
		.str = "tc_transmit_queue_tc",
		.getter = mlxsw_reg_ppcnt_tc_transmit_queue_get,
		.cells_bytes = true,
	},
	{
		.str = "tc_no_buffer_discard_uc_tc",
		.getter = mlxsw_reg_ppcnt_tc_no_buffer_discard_uc_get,
	},
};

#define MLXSW_SP_PORT_HW_TC_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_hw_tc_stats)

struct mlxsw_sp_port_stats {
	char str[ETH_GSTRING_LEN];
	u64 (*getter)(struct mlxsw_sp_port *mlxsw_sp_port);
};

static u64
mlxsw_sp_port_get_transceiver_overheat_stats(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_core *mlxsw_core = mlxsw_sp_port->mlxsw_sp->core;
	u8 slot_index = mlxsw_sp_port->mapping.slot_index;
	u8 module = mlxsw_sp_port->mapping.module;
	u64 stats;
	int err;

	err = mlxsw_env_module_overheat_counter_get(mlxsw_core, slot_index,
						    module, &stats);
	if (err)
		return mlxsw_sp_port->module_overheat_initial_val;

	return stats - mlxsw_sp_port->module_overheat_initial_val;
}

static struct mlxsw_sp_port_stats mlxsw_sp_port_transceiver_stats[] = {
	{
		.str = "transceiver_overheat",
		.getter = mlxsw_sp_port_get_transceiver_overheat_stats,
	},
};

#define MLXSW_SP_PORT_HW_TRANSCEIVER_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_transceiver_stats)

#define MLXSW_SP_PORT_ETHTOOL_STATS_LEN (MLXSW_SP_PORT_HW_STATS_LEN + \
					 MLXSW_SP_PORT_HW_RFC_2863_STATS_LEN + \
					 MLXSW_SP_PORT_HW_RFC_2819_STATS_LEN + \
					 MLXSW_SP_PORT_HW_RFC_3635_STATS_LEN + \
					 MLXSW_SP_PORT_HW_EXT_STATS_LEN + \
					 MLXSW_SP_PORT_HW_DISCARD_STATS_LEN + \
					 (MLXSW_SP_PORT_HW_PRIO_STATS_LEN * \
					  IEEE_8021QAZ_MAX_TCS) + \
					 (MLXSW_SP_PORT_HW_TC_STATS_LEN * \
					  TC_MAX_QUEUE) + \
					  MLXSW_SP_PORT_HW_TRANSCEIVER_STATS_LEN)

static void mlxsw_sp_port_get_prio_strings(u8 **p, int prio)
{
	int i;

	for (i = 0; i < MLXSW_SP_PORT_HW_PRIO_STATS_LEN; i++) {
		snprintf(*p, ETH_GSTRING_LEN, "%.29s_%.1d",
			 mlxsw_sp_port_hw_prio_stats[i].str, prio);
		*p += ETH_GSTRING_LEN;
	}
}

static void mlxsw_sp_port_get_tc_strings(u8 **p, int tc)
{
	int i;

	for (i = 0; i < MLXSW_SP_PORT_HW_TC_STATS_LEN; i++) {
		snprintf(*p, ETH_GSTRING_LEN, "%.28s_%d",
			 mlxsw_sp_port_hw_tc_stats[i].str, tc);
		*p += ETH_GSTRING_LEN;
	}
}

static void mlxsw_sp_port_get_strings(struct net_device *dev,
				      u32 stringset, u8 *data)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < MLXSW_SP_PORT_HW_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < MLXSW_SP_PORT_HW_RFC_2863_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_rfc_2863_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < MLXSW_SP_PORT_HW_RFC_2819_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_rfc_2819_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < MLXSW_SP_PORT_HW_RFC_3635_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_rfc_3635_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < MLXSW_SP_PORT_HW_EXT_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_ext_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < MLXSW_SP_PORT_HW_DISCARD_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_discard_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
			mlxsw_sp_port_get_prio_strings(&p, i);

		for (i = 0; i < TC_MAX_QUEUE; i++)
			mlxsw_sp_port_get_tc_strings(&p, i);

		mlxsw_sp_port->mlxsw_sp->ptp_ops->get_stats_strings(&p);

		for (i = 0; i < MLXSW_SP_PORT_HW_TRANSCEIVER_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_transceiver_stats[i].str,
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

static int
mlxsw_sp_get_hw_stats_by_group(struct mlxsw_sp_port_hw_stats **p_hw_stats,
			       int *p_len, enum mlxsw_reg_ppcnt_grp grp)
{
	switch (grp) {
	case MLXSW_REG_PPCNT_IEEE_8023_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_stats;
		*p_len = MLXSW_SP_PORT_HW_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_RFC_2863_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_rfc_2863_stats;
		*p_len = MLXSW_SP_PORT_HW_RFC_2863_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_RFC_2819_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_rfc_2819_stats;
		*p_len = MLXSW_SP_PORT_HW_RFC_2819_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_RFC_3635_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_rfc_3635_stats;
		*p_len = MLXSW_SP_PORT_HW_RFC_3635_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_EXT_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_ext_stats;
		*p_len = MLXSW_SP_PORT_HW_EXT_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_DISCARD_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_discard_stats;
		*p_len = MLXSW_SP_PORT_HW_DISCARD_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_PRIO_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_prio_stats;
		*p_len = MLXSW_SP_PORT_HW_PRIO_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_TC_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_tc_stats;
		*p_len = MLXSW_SP_PORT_HW_TC_STATS_LEN;
		break;
	default:
		WARN_ON(1);
		return -EOPNOTSUPP;
	}
	return 0;
}

static void __mlxsw_sp_port_get_stats(struct net_device *dev,
				      enum mlxsw_reg_ppcnt_grp grp, int prio,
				      u64 *data, int data_index)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_port_hw_stats *hw_stats;
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];
	int i, len;
	int err;

	err = mlxsw_sp_get_hw_stats_by_group(&hw_stats, &len, grp);
	if (err)
		return;
	mlxsw_sp_port_get_stats_raw(dev, grp, prio, ppcnt_pl);
	for (i = 0; i < len; i++) {
		data[data_index + i] = hw_stats[i].getter(ppcnt_pl);
		if (!hw_stats[i].cells_bytes)
			continue;
		data[data_index + i] = mlxsw_sp_cells_bytes(mlxsw_sp,
							    data[data_index + i]);
	}
}

static void __mlxsw_sp_port_get_env_stats(struct net_device *dev, u64 *data, int data_index,
					  struct mlxsw_sp_port_stats *port_stats,
					  int len)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int i;

	for (i = 0; i < len; i++)
		data[data_index + i] = port_stats[i].getter(mlxsw_sp_port);
}

static void mlxsw_sp_port_get_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int i, data_index = 0;

	/* IEEE 802.3 Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_IEEE_8023_CNT, 0,
				  data, data_index);
	data_index = MLXSW_SP_PORT_HW_STATS_LEN;

	/* RFC 2863 Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_RFC_2863_CNT, 0,
				  data, data_index);
	data_index += MLXSW_SP_PORT_HW_RFC_2863_STATS_LEN;

	/* RFC 2819 Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_RFC_2819_CNT, 0,
				  data, data_index);
	data_index += MLXSW_SP_PORT_HW_RFC_2819_STATS_LEN;

	/* RFC 3635 Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_RFC_3635_CNT, 0,
				  data, data_index);
	data_index += MLXSW_SP_PORT_HW_RFC_3635_STATS_LEN;

	/* Extended Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_EXT_CNT, 0,
				  data, data_index);
	data_index += MLXSW_SP_PORT_HW_EXT_STATS_LEN;

	/* Discard Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_DISCARD_CNT, 0,
				  data, data_index);
	data_index += MLXSW_SP_PORT_HW_DISCARD_STATS_LEN;

	/* Per-Priority Counters */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_PRIO_CNT, i,
					  data, data_index);
		data_index += MLXSW_SP_PORT_HW_PRIO_STATS_LEN;
	}

	/* Per-TC Counters */
	for (i = 0; i < TC_MAX_QUEUE; i++) {
		__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_TC_CNT, i,
					  data, data_index);
		data_index += MLXSW_SP_PORT_HW_TC_STATS_LEN;
	}

	/* PTP counters */
	mlxsw_sp_port->mlxsw_sp->ptp_ops->get_stats(mlxsw_sp_port,
						    data, data_index);
	data_index += mlxsw_sp_port->mlxsw_sp->ptp_ops->get_stats_count();

	/* Transceiver counters */
	__mlxsw_sp_port_get_env_stats(dev, data, data_index, mlxsw_sp_port_transceiver_stats,
				      MLXSW_SP_PORT_HW_TRANSCEIVER_STATS_LEN);
	data_index += MLXSW_SP_PORT_HW_TRANSCEIVER_STATS_LEN;
}

static int mlxsw_sp_port_get_sset_count(struct net_device *dev, int sset)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		return MLXSW_SP_PORT_ETHTOOL_STATS_LEN +
			mlxsw_sp_port->mlxsw_sp->ptp_ops->get_stats_count();
	default:
		return -EOPNOTSUPP;
	}
}

static void
mlxsw_sp_port_get_link_supported(struct mlxsw_sp *mlxsw_sp, u32 eth_proto_cap,
				 struct ethtool_link_ksettings *cmd)
{
	const struct mlxsw_sp_port_type_speed_ops *ops;

	ops = mlxsw_sp->port_type_speed_ops;

	ethtool_link_ksettings_add_link_mode(cmd, supported, Asym_Pause);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Pause);

	ops->from_ptys_supported_port(mlxsw_sp, eth_proto_cap, cmd);
	ops->from_ptys_link(mlxsw_sp, eth_proto_cap,
			    cmd->link_modes.supported);
}

static void
mlxsw_sp_port_get_link_advertise(struct mlxsw_sp *mlxsw_sp,
				 u32 eth_proto_admin, bool autoneg,
				 struct ethtool_link_ksettings *cmd)
{
	const struct mlxsw_sp_port_type_speed_ops *ops;

	ops = mlxsw_sp->port_type_speed_ops;

	if (!autoneg)
		return;

	ethtool_link_ksettings_add_link_mode(cmd, advertising, Autoneg);
	ops->from_ptys_link(mlxsw_sp, eth_proto_admin,
			    cmd->link_modes.advertising);
}

static u8
mlxsw_sp_port_connector_port(enum mlxsw_reg_ptys_connector_type connector_type)
{
	switch (connector_type) {
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_UNKNOWN_OR_NO_CONNECTOR:
		return PORT_OTHER;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_NONE:
		return PORT_NONE;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_TP:
		return PORT_TP;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_AUI:
		return PORT_AUI;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_BNC:
		return PORT_BNC;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_MII:
		return PORT_MII;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_FIBRE:
		return PORT_FIBRE;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_DA:
		return PORT_DA;
	case MLXSW_REG_PTYS_CONNECTOR_TYPE_PORT_OTHER:
		return PORT_OTHER;
	default:
		WARN_ON_ONCE(1);
		return PORT_OTHER;
	}
}

static int mlxsw_sp_port_ptys_query(struct mlxsw_sp_port *mlxsw_sp_port,
				    u32 *p_eth_proto_cap, u32 *p_eth_proto_admin,
				    u32 *p_eth_proto_oper, u8 *p_connector_type)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	const struct mlxsw_sp_port_type_speed_ops *ops;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	int err;

	ops = mlxsw_sp->port_type_speed_ops;

	ops->reg_ptys_eth_pack(mlxsw_sp, ptys_pl, mlxsw_sp_port->local_port, 0, false);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err)
		return err;

	ops->reg_ptys_eth_unpack(mlxsw_sp, ptys_pl, p_eth_proto_cap, p_eth_proto_admin,
				 p_eth_proto_oper);
	if (p_connector_type)
		*p_connector_type = mlxsw_reg_ptys_connector_type_get(ptys_pl);
	return 0;
}

static int mlxsw_sp_port_get_link_ksettings(struct net_device *dev,
					    struct ethtool_link_ksettings *cmd)
{
	u32 eth_proto_cap, eth_proto_admin, eth_proto_oper;
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	const struct mlxsw_sp_port_type_speed_ops *ops;
	u8 connector_type;
	bool autoneg;
	int err;

	err = mlxsw_sp_port_ptys_query(mlxsw_sp_port, &eth_proto_cap, &eth_proto_admin,
				       &eth_proto_oper, &connector_type);
	if (err)
		return err;

	ops = mlxsw_sp->port_type_speed_ops;
	autoneg = mlxsw_sp_port->link.autoneg;

	mlxsw_sp_port_get_link_supported(mlxsw_sp, eth_proto_cap, cmd);

	mlxsw_sp_port_get_link_advertise(mlxsw_sp, eth_proto_admin, autoneg, cmd);

	cmd->base.autoneg = autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE;
	cmd->base.port = mlxsw_sp_port_connector_port(connector_type);
	ops->from_ptys_link_mode(mlxsw_sp, netif_carrier_ok(dev),
				 eth_proto_oper, cmd);

	return 0;
}

static int
mlxsw_sp_port_set_link_ksettings(struct net_device *dev,
				 const struct ethtool_link_ksettings *cmd)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	const struct mlxsw_sp_port_type_speed_ops *ops;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u32 eth_proto_cap, eth_proto_new;
	bool autoneg;
	int err;

	ops = mlxsw_sp->port_type_speed_ops;

	ops->reg_ptys_eth_pack(mlxsw_sp, ptys_pl, mlxsw_sp_port->local_port,
			       0, false);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err)
		return err;
	ops->reg_ptys_eth_unpack(mlxsw_sp, ptys_pl, &eth_proto_cap, NULL, NULL);

	autoneg = cmd->base.autoneg == AUTONEG_ENABLE;
	eth_proto_new = autoneg ?
		ops->to_ptys_advert_link(mlxsw_sp, cmd) :
		ops->to_ptys_speed_lanes(mlxsw_sp, mlxsw_sp_port->mapping.width,
					 cmd);

	eth_proto_new = eth_proto_new & eth_proto_cap;
	if (!eth_proto_new) {
		netdev_err(dev, "No supported speed or lanes requested\n");
		return -EINVAL;
	}

	ops->reg_ptys_eth_pack(mlxsw_sp, ptys_pl, mlxsw_sp_port->local_port,
			       eth_proto_new, autoneg);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err)
		return err;

	mlxsw_sp_port->link.autoneg = autoneg;

	if (!netif_running(dev))
		return 0;

	mlxsw_sp_port_admin_status_set(mlxsw_sp_port, false);
	mlxsw_sp_port_admin_status_set(mlxsw_sp_port, true);

	return 0;
}

static int mlxsw_sp_get_module_info(struct net_device *netdev,
				    struct ethtool_modinfo *modinfo)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(netdev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	return mlxsw_env_get_module_info(netdev, mlxsw_sp->core,
					 mlxsw_sp_port->mapping.slot_index,
					 mlxsw_sp_port->mapping.module,
					 modinfo);
}

static int mlxsw_sp_get_module_eeprom(struct net_device *netdev,
				      struct ethtool_eeprom *ee, u8 *data)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(netdev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 slot_index = mlxsw_sp_port->mapping.slot_index;
	u8 module = mlxsw_sp_port->mapping.module;

	return mlxsw_env_get_module_eeprom(netdev, mlxsw_sp->core, slot_index,
					   module, ee, data);
}

static int
mlxsw_sp_get_module_eeprom_by_page(struct net_device *dev,
				   const struct ethtool_module_eeprom *page,
				   struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 slot_index = mlxsw_sp_port->mapping.slot_index;
	u8 module = mlxsw_sp_port->mapping.module;

	return mlxsw_env_get_module_eeprom_by_page(mlxsw_sp->core, slot_index,
						   module, page, extack);
}

static int
mlxsw_sp_get_ts_info(struct net_device *netdev, struct ethtool_ts_info *info)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(netdev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	return mlxsw_sp->ptp_ops->get_ts_info(mlxsw_sp, info);
}

static void
mlxsw_sp_get_eth_phy_stats(struct net_device *dev,
			   struct ethtool_eth_phy_stats *phy_stats)
{
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];

	if (mlxsw_sp_port_get_stats_raw(dev, MLXSW_REG_PPCNT_IEEE_8023_CNT,
					0, ppcnt_pl))
		return;

	phy_stats->SymbolErrorDuringCarrier =
		mlxsw_reg_ppcnt_a_symbol_error_during_carrier_get(ppcnt_pl);
}

static void
mlxsw_sp_get_eth_mac_stats(struct net_device *dev,
			   struct ethtool_eth_mac_stats *mac_stats)
{
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];

	if (mlxsw_sp_port_get_stats_raw(dev, MLXSW_REG_PPCNT_IEEE_8023_CNT,
					0, ppcnt_pl))
		return;

	mac_stats->FramesTransmittedOK =
		mlxsw_reg_ppcnt_a_frames_transmitted_ok_get(ppcnt_pl);
	mac_stats->FramesReceivedOK =
		mlxsw_reg_ppcnt_a_frames_received_ok_get(ppcnt_pl);
	mac_stats->FrameCheckSequenceErrors =
		mlxsw_reg_ppcnt_a_frame_check_sequence_errors_get(ppcnt_pl);
	mac_stats->AlignmentErrors =
		mlxsw_reg_ppcnt_a_alignment_errors_get(ppcnt_pl);
	mac_stats->OctetsTransmittedOK =
		mlxsw_reg_ppcnt_a_octets_transmitted_ok_get(ppcnt_pl);
	mac_stats->OctetsReceivedOK =
		mlxsw_reg_ppcnt_a_octets_received_ok_get(ppcnt_pl);
	mac_stats->MulticastFramesXmittedOK =
		mlxsw_reg_ppcnt_a_multicast_frames_xmitted_ok_get(ppcnt_pl);
	mac_stats->BroadcastFramesXmittedOK =
		mlxsw_reg_ppcnt_a_broadcast_frames_xmitted_ok_get(ppcnt_pl);
	mac_stats->MulticastFramesReceivedOK =
		mlxsw_reg_ppcnt_a_multicast_frames_received_ok_get(ppcnt_pl);
	mac_stats->BroadcastFramesReceivedOK =
		mlxsw_reg_ppcnt_a_broadcast_frames_received_ok_get(ppcnt_pl);
	mac_stats->InRangeLengthErrors =
		mlxsw_reg_ppcnt_a_in_range_length_errors_get(ppcnt_pl);
	mac_stats->OutOfRangeLengthField =
		mlxsw_reg_ppcnt_a_out_of_range_length_field_get(ppcnt_pl);
	mac_stats->FrameTooLongErrors =
		mlxsw_reg_ppcnt_a_frame_too_long_errors_get(ppcnt_pl);
}

static void
mlxsw_sp_get_eth_ctrl_stats(struct net_device *dev,
			    struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];

	if (mlxsw_sp_port_get_stats_raw(dev, MLXSW_REG_PPCNT_IEEE_8023_CNT,
					0, ppcnt_pl))
		return;

	ctrl_stats->MACControlFramesTransmitted =
		mlxsw_reg_ppcnt_a_mac_control_frames_transmitted_get(ppcnt_pl);
	ctrl_stats->MACControlFramesReceived =
		mlxsw_reg_ppcnt_a_mac_control_frames_received_get(ppcnt_pl);
	ctrl_stats->UnsupportedOpcodesReceived =
		mlxsw_reg_ppcnt_a_unsupported_opcodes_received_get(ppcnt_pl);
}

static const struct ethtool_rmon_hist_range mlxsw_rmon_ranges[] = {
	{    0,    64 },
	{   65,   127 },
	{  128,   255 },
	{  256,   511 },
	{  512,  1023 },
	{ 1024,  1518 },
	{ 1519,  2047 },
	{ 2048,  4095 },
	{ 4096,  8191 },
	{ 8192, 10239 },
	{}
};

static void
mlxsw_sp_get_rmon_stats(struct net_device *dev,
			struct ethtool_rmon_stats *rmon,
			const struct ethtool_rmon_hist_range **ranges)
{
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];

	if (mlxsw_sp_port_get_stats_raw(dev, MLXSW_REG_PPCNT_RFC_2819_CNT,
					0, ppcnt_pl))
		return;

	rmon->undersize_pkts =
		mlxsw_reg_ppcnt_ether_stats_undersize_pkts_get(ppcnt_pl);
	rmon->oversize_pkts =
		mlxsw_reg_ppcnt_ether_stats_oversize_pkts_get(ppcnt_pl);
	rmon->fragments =
		mlxsw_reg_ppcnt_ether_stats_fragments_get(ppcnt_pl);

	rmon->hist[0] = mlxsw_reg_ppcnt_ether_stats_pkts64octets_get(ppcnt_pl);
	rmon->hist[1] =
		mlxsw_reg_ppcnt_ether_stats_pkts65to127octets_get(ppcnt_pl);
	rmon->hist[2] =
		mlxsw_reg_ppcnt_ether_stats_pkts128to255octets_get(ppcnt_pl);
	rmon->hist[3] =
		mlxsw_reg_ppcnt_ether_stats_pkts256to511octets_get(ppcnt_pl);
	rmon->hist[4] =
		mlxsw_reg_ppcnt_ether_stats_pkts512to1023octets_get(ppcnt_pl);
	rmon->hist[5] =
		mlxsw_reg_ppcnt_ether_stats_pkts1024to1518octets_get(ppcnt_pl);
	rmon->hist[6] =
		mlxsw_reg_ppcnt_ether_stats_pkts1519to2047octets_get(ppcnt_pl);
	rmon->hist[7] =
		mlxsw_reg_ppcnt_ether_stats_pkts2048to4095octets_get(ppcnt_pl);
	rmon->hist[8] =
		mlxsw_reg_ppcnt_ether_stats_pkts4096to8191octets_get(ppcnt_pl);
	rmon->hist[9] =
		mlxsw_reg_ppcnt_ether_stats_pkts8192to10239octets_get(ppcnt_pl);

	*ranges = mlxsw_rmon_ranges;
}

static int mlxsw_sp_reset(struct net_device *dev, u32 *flags)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 slot_index = mlxsw_sp_port->mapping.slot_index;
	u8 module = mlxsw_sp_port->mapping.module;

	return mlxsw_env_reset_module(dev, mlxsw_sp->core, slot_index,
				      module, flags);
}

static int
mlxsw_sp_get_module_power_mode(struct net_device *dev,
			       struct ethtool_module_power_mode_params *params,
			       struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 slot_index = mlxsw_sp_port->mapping.slot_index;
	u8 module = mlxsw_sp_port->mapping.module;

	return mlxsw_env_get_module_power_mode(mlxsw_sp->core, slot_index,
					       module, params, extack);
}

static int
mlxsw_sp_set_module_power_mode(struct net_device *dev,
			       const struct ethtool_module_power_mode_params *params,
			       struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 slot_index = mlxsw_sp_port->mapping.slot_index;
	u8 module = mlxsw_sp_port->mapping.module;

	return mlxsw_env_set_module_power_mode(mlxsw_sp->core, slot_index,
					       module, params->policy, extack);
}

const struct ethtool_ops mlxsw_sp_port_ethtool_ops = {
	.cap_link_lanes_supported	= true,
	.get_drvinfo			= mlxsw_sp_port_get_drvinfo,
	.get_link			= ethtool_op_get_link,
	.get_link_ext_state		= mlxsw_sp_port_get_link_ext_state,
	.get_pauseparam			= mlxsw_sp_port_get_pauseparam,
	.set_pauseparam			= mlxsw_sp_port_set_pauseparam,
	.get_strings			= mlxsw_sp_port_get_strings,
	.set_phys_id			= mlxsw_sp_port_set_phys_id,
	.get_ethtool_stats		= mlxsw_sp_port_get_stats,
	.get_sset_count			= mlxsw_sp_port_get_sset_count,
	.get_link_ksettings		= mlxsw_sp_port_get_link_ksettings,
	.set_link_ksettings		= mlxsw_sp_port_set_link_ksettings,
	.get_module_info		= mlxsw_sp_get_module_info,
	.get_module_eeprom		= mlxsw_sp_get_module_eeprom,
	.get_module_eeprom_by_page	= mlxsw_sp_get_module_eeprom_by_page,
	.get_ts_info			= mlxsw_sp_get_ts_info,
	.get_eth_phy_stats		= mlxsw_sp_get_eth_phy_stats,
	.get_eth_mac_stats		= mlxsw_sp_get_eth_mac_stats,
	.get_eth_ctrl_stats		= mlxsw_sp_get_eth_ctrl_stats,
	.get_rmon_stats			= mlxsw_sp_get_rmon_stats,
	.reset				= mlxsw_sp_reset,
	.get_module_power_mode		= mlxsw_sp_get_module_power_mode,
	.set_module_power_mode		= mlxsw_sp_set_module_power_mode,
};

struct mlxsw_sp1_port_link_mode {
	enum ethtool_link_mode_bit_indices mask_ethtool;
	u32 mask;
	u32 speed;
};

static const struct mlxsw_sp1_port_link_mode mlxsw_sp1_port_link_mode[] = {
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100BASE_T,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100baseT_Full_BIT,
		.speed		= SPEED_100,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_SGMII |
				  MLXSW_REG_PTYS_ETH_SPEED_1000BASE_KX,
		.mask_ethtool	= ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
		.speed		= SPEED_1000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_1000BASE_T,
		.mask_ethtool   = ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
		.speed          = SPEED_1000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CX4 |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
		.speed		= SPEED_10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_SR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_ER_LR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
		.speed		= SPEED_10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_SR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_LR4_ER4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_25GBASE_CR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
		.speed		= SPEED_25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_25GBASE_KR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
		.speed		= SPEED_25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_25GBASE_SR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
		.speed		= SPEED_25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_50GBASE_CR2,
		.mask_ethtool	= ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
		.speed		= SPEED_50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_50GBASE_KR2,
		.mask_ethtool	= ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
		.speed		= SPEED_50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_50GBASE_SR2,
		.mask_ethtool	= ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
		.speed		= SPEED_50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_CR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
		.speed		= SPEED_100000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
		.speed		= SPEED_100000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
		.speed		= SPEED_100000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_LR4_ER4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
		.speed		= SPEED_100000,
	},
};

#define MLXSW_SP1_PORT_LINK_MODE_LEN ARRAY_SIZE(mlxsw_sp1_port_link_mode)

static void
mlxsw_sp1_from_ptys_supported_port(struct mlxsw_sp *mlxsw_sp,
				   u32 ptys_eth_proto,
				   struct ethtool_link_ksettings *cmd)
{
	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CR |
			      MLXSW_REG_PTYS_ETH_SPEED_10GBASE_SR |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_SGMII))
		ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);

	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR |
			      MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4 |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_1000BASE_KX))
		ethtool_link_ksettings_add_link_mode(cmd, supported, Backplane);
}

static void
mlxsw_sp1_from_ptys_link(struct mlxsw_sp *mlxsw_sp, u32 ptys_eth_proto,
			 unsigned long *mode)
{
	int i;

	for (i = 0; i < MLXSW_SP1_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp1_port_link_mode[i].mask)
			__set_bit(mlxsw_sp1_port_link_mode[i].mask_ethtool,
				  mode);
	}
}

static u32
mlxsw_sp1_from_ptys_speed(struct mlxsw_sp *mlxsw_sp, u32 ptys_eth_proto)
{
	int i;

	for (i = 0; i < MLXSW_SP1_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp1_port_link_mode[i].mask)
			return mlxsw_sp1_port_link_mode[i].speed;
	}

	return SPEED_UNKNOWN;
}

static void
mlxsw_sp1_from_ptys_link_mode(struct mlxsw_sp *mlxsw_sp, bool carrier_ok,
			      u32 ptys_eth_proto,
			      struct ethtool_link_ksettings *cmd)
{
	struct mlxsw_sp1_port_link_mode link;
	int i;

	cmd->base.speed = SPEED_UNKNOWN;
	cmd->base.duplex = DUPLEX_UNKNOWN;
	cmd->lanes = 0;

	if (!carrier_ok)
		return;

	for (i = 0; i < MLXSW_SP1_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp1_port_link_mode[i].mask) {
			link = mlxsw_sp1_port_link_mode[i];
			ethtool_params_from_link_mode(cmd,
						      link.mask_ethtool);
		}
	}
}

static int mlxsw_sp1_ptys_max_speed(struct mlxsw_sp_port *mlxsw_sp_port, u32 *p_max_speed)
{
	u32 eth_proto_cap;
	u32 max_speed = 0;
	int err;
	int i;

	err = mlxsw_sp_port_ptys_query(mlxsw_sp_port, &eth_proto_cap, NULL, NULL, NULL);
	if (err)
		return err;

	for (i = 0; i < MLXSW_SP1_PORT_LINK_MODE_LEN; i++) {
		if ((eth_proto_cap & mlxsw_sp1_port_link_mode[i].mask) &&
		    mlxsw_sp1_port_link_mode[i].speed > max_speed)
			max_speed = mlxsw_sp1_port_link_mode[i].speed;
	}

	*p_max_speed = max_speed;
	return 0;
}

static u32
mlxsw_sp1_to_ptys_advert_link(struct mlxsw_sp *mlxsw_sp,
			      const struct ethtool_link_ksettings *cmd)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP1_PORT_LINK_MODE_LEN; i++) {
		if (test_bit(mlxsw_sp1_port_link_mode[i].mask_ethtool,
			     cmd->link_modes.advertising))
			ptys_proto |= mlxsw_sp1_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static u32 mlxsw_sp1_to_ptys_speed_lanes(struct mlxsw_sp *mlxsw_sp, u8 width,
					 const struct ethtool_link_ksettings *cmd)
{
	u32 ptys_proto = 0;
	int i;

	if (cmd->lanes > width)
		return ptys_proto;

	for (i = 0; i < MLXSW_SP1_PORT_LINK_MODE_LEN; i++) {
		if (cmd->base.speed == mlxsw_sp1_port_link_mode[i].speed)
			ptys_proto |= mlxsw_sp1_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static void
mlxsw_sp1_reg_ptys_eth_pack(struct mlxsw_sp *mlxsw_sp, char *payload,
			    u16 local_port, u32 proto_admin, bool autoneg)
{
	mlxsw_reg_ptys_eth_pack(payload, local_port, proto_admin, autoneg);
}

static void
mlxsw_sp1_reg_ptys_eth_unpack(struct mlxsw_sp *mlxsw_sp, char *payload,
			      u32 *p_eth_proto_cap, u32 *p_eth_proto_admin,
			      u32 *p_eth_proto_oper)
{
	mlxsw_reg_ptys_eth_unpack(payload, p_eth_proto_cap, p_eth_proto_admin,
				  p_eth_proto_oper);
}

static u32 mlxsw_sp1_ptys_proto_cap_masked_get(u32 eth_proto_cap)
{
	u32 ptys_proto_cap_masked = 0;
	int i;

	for (i = 0; i < MLXSW_SP1_PORT_LINK_MODE_LEN; i++) {
		if (mlxsw_sp1_port_link_mode[i].mask & eth_proto_cap)
			ptys_proto_cap_masked |=
				mlxsw_sp1_port_link_mode[i].mask;
	}

	return ptys_proto_cap_masked;
}

const struct mlxsw_sp_port_type_speed_ops mlxsw_sp1_port_type_speed_ops = {
	.from_ptys_supported_port	= mlxsw_sp1_from_ptys_supported_port,
	.from_ptys_link			= mlxsw_sp1_from_ptys_link,
	.from_ptys_speed		= mlxsw_sp1_from_ptys_speed,
	.from_ptys_link_mode		= mlxsw_sp1_from_ptys_link_mode,
	.ptys_max_speed			= mlxsw_sp1_ptys_max_speed,
	.to_ptys_advert_link		= mlxsw_sp1_to_ptys_advert_link,
	.to_ptys_speed_lanes		= mlxsw_sp1_to_ptys_speed_lanes,
	.reg_ptys_eth_pack		= mlxsw_sp1_reg_ptys_eth_pack,
	.reg_ptys_eth_unpack		= mlxsw_sp1_reg_ptys_eth_unpack,
	.ptys_proto_cap_masked_get	= mlxsw_sp1_ptys_proto_cap_masked_get,
};

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_sgmii_100m[] = {
	ETHTOOL_LINK_MODE_100baseT_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_SGMII_100M_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_sgmii_100m)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_1000base_x_sgmii[] = {
	ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_1000BASE_X_SGMII_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_1000base_x_sgmii)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_5gbase_r[] = {
	ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_5GBASE_R_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_5gbase_r)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_xfi_xaui_1_10g[] = {
	ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseR_FEC_BIT,
	ETHTOOL_LINK_MODE_10000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
	ETHTOOL_LINK_MODE_10000baseER_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_XFI_XAUI_1_10G_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_xfi_xaui_1_10g)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_xlaui_4_xlppi_4_40g[] = {
	ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_XLAUI_4_XLPPI_4_40G_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_xlaui_4_xlppi_4_40g)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_25gaui_1_25gbase_cr_kr[] = {
	ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_25GAUI_1_25GBASE_CR_KR_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_25gaui_1_25gbase_cr_kr)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_50gaui_2_laui_2_50gbase_cr2_kr2[] = {
	ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_50GAUI_2_LAUI_2_50GBASE_CR2_KR2_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_50gaui_2_laui_2_50gbase_cr2_kr2)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_50gaui_1_laui_1_50gbase_cr_kr[] = {
	ETHTOOL_LINK_MODE_50000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT,
	ETHTOOL_LINK_MODE_50000baseDR_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_50GAUI_1_LAUI_1_50GBASE_CR_KR_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_50gaui_1_laui_1_50gbase_cr_kr)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_caui_4_100gbase_cr4_kr4[] = {
	ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_CAUI_4_100GBASE_CR4_KR4_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_caui_4_100gbase_cr4_kr4)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_100gaui_2_100gbase_cr2_kr2[] = {
	ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseDR2_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_100GAUI_2_100GBASE_CR2_KR2_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_100gaui_2_100gbase_cr2_kr2)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_100gaui_1_100gbase_cr_kr[] = {
	ETHTOOL_LINK_MODE_100000baseKR_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseSR_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseLR_ER_FR_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseCR_Full_BIT,
	ETHTOOL_LINK_MODE_100000baseDR_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_100GAUI_1_100GBASE_CR_KR_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_100gaui_1_100gbase_cr_kr)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_200gaui_4_200gbase_cr4_kr4[] = {
	ETHTOOL_LINK_MODE_200000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseLR4_ER4_FR4_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseDR4_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseCR4_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_200GAUI_4_200GBASE_CR4_KR4_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_200gaui_4_200gbase_cr4_kr4)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_200gaui_2_200gbase_cr2_kr2[] = {
	ETHTOOL_LINK_MODE_200000baseKR2_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseSR2_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseLR2_ER2_FR2_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseDR2_Full_BIT,
	ETHTOOL_LINK_MODE_200000baseCR2_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_200GAUI_2_200GBASE_CR2_KR2_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_200gaui_2_200gbase_cr2_kr2)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_400gaui_8[] = {
	ETHTOOL_LINK_MODE_400000baseKR8_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseSR8_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseLR8_ER8_FR8_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseDR8_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseCR8_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_400GAUI_8_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_400gaui_8)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_400gaui_4_400gbase_cr4_kr4[] = {
	ETHTOOL_LINK_MODE_400000baseKR4_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseSR4_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseLR4_ER4_FR4_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseDR4_Full_BIT,
	ETHTOOL_LINK_MODE_400000baseCR4_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_400GAUI_4_400GBASE_CR4_KR4_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_400gaui_4_400gbase_cr4_kr4)

static const enum ethtool_link_mode_bit_indices
mlxsw_sp2_mask_ethtool_800gaui_8[] = {
	ETHTOOL_LINK_MODE_800000baseCR8_Full_BIT,
	ETHTOOL_LINK_MODE_800000baseKR8_Full_BIT,
	ETHTOOL_LINK_MODE_800000baseDR8_Full_BIT,
	ETHTOOL_LINK_MODE_800000baseDR8_2_Full_BIT,
	ETHTOOL_LINK_MODE_800000baseSR8_Full_BIT,
	ETHTOOL_LINK_MODE_800000baseVR8_Full_BIT,
};

#define MLXSW_SP2_MASK_ETHTOOL_800GAUI_8_LEN \
	ARRAY_SIZE(mlxsw_sp2_mask_ethtool_800gaui_8)

#define MLXSW_SP_PORT_MASK_WIDTH_1X	BIT(0)
#define MLXSW_SP_PORT_MASK_WIDTH_2X	BIT(1)
#define MLXSW_SP_PORT_MASK_WIDTH_4X	BIT(2)
#define MLXSW_SP_PORT_MASK_WIDTH_8X	BIT(3)

static u8 mlxsw_sp_port_mask_width_get(u8 width)
{
	switch (width) {
	case 1:
		return MLXSW_SP_PORT_MASK_WIDTH_1X;
	case 2:
		return MLXSW_SP_PORT_MASK_WIDTH_2X;
	case 4:
		return MLXSW_SP_PORT_MASK_WIDTH_4X;
	case 8:
		return MLXSW_SP_PORT_MASK_WIDTH_8X;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}

struct mlxsw_sp2_port_link_mode {
	const enum ethtool_link_mode_bit_indices *mask_ethtool;
	int m_ethtool_len;
	u32 mask;
	u32 speed;
	u32 width;
	u8 mask_sup_width;
};

static const struct mlxsw_sp2_port_link_mode mlxsw_sp2_port_link_mode[] = {
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_SGMII_100M,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_sgmii_100m,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_SGMII_100M_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_1X |
				  MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_100,
		.width		= 1,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_1000BASE_X_SGMII,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_1000base_x_sgmii,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_1000BASE_X_SGMII_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_1X |
				  MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_1000,
		.width		= 1,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_5GBASE_R,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_5gbase_r,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_5GBASE_R_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_1X |
				  MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_5000,
		.width		= 1,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_XFI_XAUI_1_10G,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_xfi_xaui_1_10g,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_XFI_XAUI_1_10G_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_1X |
				  MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_10000,
		.width		= 1,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_XLAUI_4_XLPPI_4_40G,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_xlaui_4_xlppi_4_40g,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_XLAUI_4_XLPPI_4_40G_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_40000,
		.width		= 4,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_25GAUI_1_25GBASE_CR_KR,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_25gaui_1_25gbase_cr_kr,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_25GAUI_1_25GBASE_CR_KR_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_1X |
				  MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_25000,
		.width		= 1,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_50GAUI_2_LAUI_2_50GBASE_CR2_KR2,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_50gaui_2_laui_2_50gbase_cr2_kr2,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_50GAUI_2_LAUI_2_50GBASE_CR2_KR2_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_2X |
				  MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_50000,
		.width		= 2,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_50GAUI_1_LAUI_1_50GBASE_CR_KR,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_50gaui_1_laui_1_50gbase_cr_kr,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_50GAUI_1_LAUI_1_50GBASE_CR_KR_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_1X,
		.speed		= SPEED_50000,
		.width		= 1,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_CAUI_4_100GBASE_CR4_KR4,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_caui_4_100gbase_cr4_kr4,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_CAUI_4_100GBASE_CR4_KR4_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_100000,
		.width		= 4,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_100GAUI_2_100GBASE_CR2_KR2,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_100gaui_2_100gbase_cr2_kr2,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_100GAUI_2_100GBASE_CR2_KR2_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_2X,
		.speed		= SPEED_100000,
		.width		= 2,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_100GAUI_1_100GBASE_CR_KR,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_100gaui_1_100gbase_cr_kr,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_100GAUI_1_100GBASE_CR_KR_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_1X,
		.speed		= SPEED_100000,
		.width		= 1,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_200GAUI_4_200GBASE_CR4_KR4,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_200gaui_4_200gbase_cr4_kr4,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_200GAUI_4_200GBASE_CR4_KR4_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_4X |
				  MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_200000,
		.width		= 4,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_200GAUI_2_200GBASE_CR2_KR2,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_200gaui_2_200gbase_cr2_kr2,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_200GAUI_2_200GBASE_CR2_KR2_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_2X,
		.speed		= SPEED_200000,
		.width		= 2,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_400GAUI_8,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_400gaui_8,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_400GAUI_8_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_400000,
		.width		= 8,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_400GAUI_4_400GBASE_CR4_KR4,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_400gaui_4_400gbase_cr4_kr4,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_400GAUI_4_400GBASE_CR4_KR4_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_4X,
		.speed		= SPEED_400000,
		.width		= 4,
	},
	{
		.mask		= MLXSW_REG_PTYS_EXT_ETH_SPEED_800GAUI_8,
		.mask_ethtool	= mlxsw_sp2_mask_ethtool_800gaui_8,
		.m_ethtool_len	= MLXSW_SP2_MASK_ETHTOOL_800GAUI_8_LEN,
		.mask_sup_width	= MLXSW_SP_PORT_MASK_WIDTH_8X,
		.speed		= SPEED_800000,
		.width		= 8,
	},
};

#define MLXSW_SP2_PORT_LINK_MODE_LEN ARRAY_SIZE(mlxsw_sp2_port_link_mode)

static void
mlxsw_sp2_from_ptys_supported_port(struct mlxsw_sp *mlxsw_sp,
				   u32 ptys_eth_proto,
				   struct ethtool_link_ksettings *cmd)
{
	ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Backplane);
}

static void
mlxsw_sp2_set_bit_ethtool(const struct mlxsw_sp2_port_link_mode *link_mode,
			  unsigned long *mode)
{
	int i;

	for (i = 0; i < link_mode->m_ethtool_len; i++)
		__set_bit(link_mode->mask_ethtool[i], mode);
}

static void
mlxsw_sp2_from_ptys_link(struct mlxsw_sp *mlxsw_sp, u32 ptys_eth_proto,
			 unsigned long *mode)
{
	int i;

	for (i = 0; i < MLXSW_SP2_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp2_port_link_mode[i].mask)
			mlxsw_sp2_set_bit_ethtool(&mlxsw_sp2_port_link_mode[i],
						  mode);
	}
}

static u32
mlxsw_sp2_from_ptys_speed(struct mlxsw_sp *mlxsw_sp, u32 ptys_eth_proto)
{
	int i;

	for (i = 0; i < MLXSW_SP2_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp2_port_link_mode[i].mask)
			return mlxsw_sp2_port_link_mode[i].speed;
	}

	return SPEED_UNKNOWN;
}

static void
mlxsw_sp2_from_ptys_link_mode(struct mlxsw_sp *mlxsw_sp, bool carrier_ok,
			      u32 ptys_eth_proto,
			      struct ethtool_link_ksettings *cmd)
{
	struct mlxsw_sp2_port_link_mode link;
	int i;

	cmd->base.speed = SPEED_UNKNOWN;
	cmd->base.duplex = DUPLEX_UNKNOWN;
	cmd->lanes = 0;

	if (!carrier_ok)
		return;

	for (i = 0; i < MLXSW_SP2_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp2_port_link_mode[i].mask) {
			link = mlxsw_sp2_port_link_mode[i];
			ethtool_params_from_link_mode(cmd,
						      link.mask_ethtool[1]);
		}
	}
}

static int mlxsw_sp2_ptys_max_speed(struct mlxsw_sp_port *mlxsw_sp_port, u32 *p_max_speed)
{
	u32 eth_proto_cap;
	u32 max_speed = 0;
	int err;
	int i;

	err = mlxsw_sp_port_ptys_query(mlxsw_sp_port, &eth_proto_cap, NULL, NULL, NULL);
	if (err)
		return err;

	for (i = 0; i < MLXSW_SP2_PORT_LINK_MODE_LEN; i++) {
		if ((eth_proto_cap & mlxsw_sp2_port_link_mode[i].mask) &&
		    mlxsw_sp2_port_link_mode[i].speed > max_speed)
			max_speed = mlxsw_sp2_port_link_mode[i].speed;
	}

	*p_max_speed = max_speed;
	return 0;
}

static bool
mlxsw_sp2_test_bit_ethtool(const struct mlxsw_sp2_port_link_mode *link_mode,
			   const unsigned long *mode)
{
	int cnt = 0;
	int i;

	for (i = 0; i < link_mode->m_ethtool_len; i++) {
		if (test_bit(link_mode->mask_ethtool[i], mode))
			cnt++;
	}

	return cnt == link_mode->m_ethtool_len;
}

static u32
mlxsw_sp2_to_ptys_advert_link(struct mlxsw_sp *mlxsw_sp,
			      const struct ethtool_link_ksettings *cmd)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP2_PORT_LINK_MODE_LEN; i++) {
		if (mlxsw_sp2_test_bit_ethtool(&mlxsw_sp2_port_link_mode[i],
					       cmd->link_modes.advertising))
			ptys_proto |= mlxsw_sp2_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static u32 mlxsw_sp2_to_ptys_speed_lanes(struct mlxsw_sp *mlxsw_sp, u8 width,
					 const struct ethtool_link_ksettings *cmd)
{
	u8 mask_width = mlxsw_sp_port_mask_width_get(width);
	struct mlxsw_sp2_port_link_mode link_mode;
	u32 ptys_proto = 0;
	int i;

	if (cmd->lanes > width)
		return ptys_proto;

	for (i = 0; i < MLXSW_SP2_PORT_LINK_MODE_LEN; i++) {
		if (cmd->base.speed == mlxsw_sp2_port_link_mode[i].speed) {
			link_mode = mlxsw_sp2_port_link_mode[i];

			if (!cmd->lanes) {
				/* If number of lanes was not set by user space,
				 * choose the link mode that supports the width
				 * of the port.
				 */
				if (mask_width & link_mode.mask_sup_width)
					ptys_proto |= link_mode.mask;
			} else if (cmd->lanes == link_mode.width) {
				/* Else if the number of lanes was set, choose
				 * the link mode that its actual width equals to
				 * it.
				 */
				ptys_proto |= link_mode.mask;
			}
		}
	}
	return ptys_proto;
}

static void
mlxsw_sp2_reg_ptys_eth_pack(struct mlxsw_sp *mlxsw_sp, char *payload,
			    u16 local_port, u32 proto_admin,
			    bool autoneg)
{
	mlxsw_reg_ptys_ext_eth_pack(payload, local_port, proto_admin, autoneg);
}

static void
mlxsw_sp2_reg_ptys_eth_unpack(struct mlxsw_sp *mlxsw_sp, char *payload,
			      u32 *p_eth_proto_cap, u32 *p_eth_proto_admin,
			      u32 *p_eth_proto_oper)
{
	mlxsw_reg_ptys_ext_eth_unpack(payload, p_eth_proto_cap,
				      p_eth_proto_admin, p_eth_proto_oper);
}

static u32 mlxsw_sp2_ptys_proto_cap_masked_get(u32 eth_proto_cap)
{
	u32 ptys_proto_cap_masked = 0;
	int i;

	for (i = 0; i < MLXSW_SP2_PORT_LINK_MODE_LEN; i++) {
		if (mlxsw_sp2_port_link_mode[i].mask & eth_proto_cap)
			ptys_proto_cap_masked |=
				mlxsw_sp2_port_link_mode[i].mask;
	}

	return ptys_proto_cap_masked;
}

const struct mlxsw_sp_port_type_speed_ops mlxsw_sp2_port_type_speed_ops = {
	.from_ptys_supported_port	= mlxsw_sp2_from_ptys_supported_port,
	.from_ptys_link			= mlxsw_sp2_from_ptys_link,
	.from_ptys_speed		= mlxsw_sp2_from_ptys_speed,
	.from_ptys_link_mode		= mlxsw_sp2_from_ptys_link_mode,
	.ptys_max_speed			= mlxsw_sp2_ptys_max_speed,
	.to_ptys_advert_link		= mlxsw_sp2_to_ptys_advert_link,
	.to_ptys_speed_lanes		= mlxsw_sp2_to_ptys_speed_lanes,
	.reg_ptys_eth_pack		= mlxsw_sp2_reg_ptys_eth_pack,
	.reg_ptys_eth_unpack		= mlxsw_sp2_reg_ptys_eth_unpack,
	.ptys_proto_cap_masked_get	= mlxsw_sp2_ptys_proto_cap_masked_get,
};
