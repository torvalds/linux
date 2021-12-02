/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2019-2021 Marvell International Ltd.
 */

#ifndef _QED_MFW_HSI_H
#define _QED_MFW_HSI_H

#define MFW_TRACE_SIGNATURE     0x25071946

/* The trace in the buffer */
#define MFW_TRACE_EVENTID_MASK          0x00ffff
#define MFW_TRACE_PRM_SIZE_MASK         0x0f0000
#define MFW_TRACE_PRM_SIZE_OFFSET	16
#define MFW_TRACE_ENTRY_SIZE            3

struct mcp_trace {
	u32 signature;		/* Help to identify that the trace is valid */
	u32 size;		/* the size of the trace buffer in bytes */
	u32 curr_level;		/* 2 - all will be written to the buffer
				 * 1 - debug trace will not be written
				 * 0 - just errors will be written to the buffer
				 */
	u32 modules_mask[2];	/* a bit per module, 1 means write it, 0 means
				 * mask it.
				 */

	/* Warning: the following pointers are assumed to be 32bits as they are
	 * used only in the MFW.
	 */
	u32 trace_prod; /* The next trace will be written to this offset */
	u32 trace_oldest; /* The oldest valid trace starts at this offset
			   * (usually very close after the current producer).
			   */
};

#define VF_MAX_STATIC 192
#define VF_BITMAP_SIZE_IN_DWORDS (VF_MAX_STATIC / 32)
#define VF_BITMAP_SIZE_IN_BYTES (VF_BITMAP_SIZE_IN_DWORDS * sizeof(u32))

#define EXT_VF_MAX_STATIC 240
#define EXT_VF_BITMAP_SIZE_IN_DWORDS (((EXT_VF_MAX_STATIC - 1) / 32) + 1)
#define EXT_VF_BITMAP_SIZE_IN_BYTES (EXT_VF_BITMAP_SIZE_IN_DWORDS * sizeof(u32))
#define ADDED_VF_BITMAP_SIZE 2

#define MCP_GLOB_PATH_MAX	2
#define MCP_PORT_MAX		2
#define MCP_GLOB_PORT_MAX	4
#define MCP_GLOB_FUNC_MAX	16

typedef u32 offsize_t;		/* In DWORDS !!! */
/* Offset from the beginning of the MCP scratchpad */
#define OFFSIZE_OFFSET_SHIFT	0
#define OFFSIZE_OFFSET_MASK	0x0000ffff
/* Size of specific element (not the whole array if any) */
#define OFFSIZE_SIZE_SHIFT	16
#define OFFSIZE_SIZE_MASK	0xffff0000

#define SECTION_OFFSET(_offsize) (((((_offsize) &			\
				     OFFSIZE_OFFSET_MASK) >>	\
				    OFFSIZE_OFFSET_SHIFT) << 2))

#define QED_SECTION_SIZE(_offsize) ((((_offsize) &		\
				      OFFSIZE_SIZE_MASK) >>	\
				     OFFSIZE_SIZE_SHIFT) << 2)

#define SECTION_ADDR(_offsize, idx) (MCP_REG_SCRATCH +			\
				     SECTION_OFFSET((_offsize)) +	\
				     (QED_SECTION_SIZE((_offsize)) * (idx)))

#define SECTION_OFFSIZE_ADDR(_pub_base, _section)	\
	((_pub_base) + offsetof(struct mcp_public_data, sections[_section]))

/* PHY configuration */
struct eth_phy_cfg {
	u32					speed;
#define ETH_SPEED_AUTONEG			0x0
#define ETH_SPEED_SMARTLINQ			0x8

	u32					pause;
#define ETH_PAUSE_NONE				0x0
#define ETH_PAUSE_AUTONEG			0x1
#define ETH_PAUSE_RX				0x2
#define ETH_PAUSE_TX				0x4

	u32					adv_speed;

	u32					loopback_mode;
#define ETH_LOOPBACK_NONE			0x0
#define ETH_LOOPBACK_INT_PHY			0x1
#define ETH_LOOPBACK_EXT_PHY			0x2
#define ETH_LOOPBACK_EXT			0x3
#define ETH_LOOPBACK_MAC			0x4
#define ETH_LOOPBACK_CNIG_AH_ONLY_0123		0x5
#define ETH_LOOPBACK_CNIG_AH_ONLY_2301		0x6
#define ETH_LOOPBACK_PCS_AH_ONLY		0x7
#define ETH_LOOPBACK_REVERSE_MAC_AH_ONLY	0x8
#define ETH_LOOPBACK_INT_PHY_FEA_AH_ONLY	0x9

	u32					eee_cfg;
#define EEE_CFG_EEE_ENABLED			BIT(0)
#define EEE_CFG_TX_LPI				BIT(1)
#define EEE_CFG_ADV_SPEED_1G			BIT(2)
#define EEE_CFG_ADV_SPEED_10G			BIT(3)
#define EEE_TX_TIMER_USEC_MASK			0xfffffff0
#define EEE_TX_TIMER_USEC_OFFSET		4
#define EEE_TX_TIMER_USEC_BALANCED_TIME		0xa00
#define EEE_TX_TIMER_USEC_AGGRESSIVE_TIME	0x100
#define EEE_TX_TIMER_USEC_LATENCY_TIME		0x6000

	u32					link_modes;

	u32					fec_mode;
#define FEC_FORCE_MODE_MASK			0x000000ff
#define FEC_FORCE_MODE_OFFSET			0
#define FEC_FORCE_MODE_NONE			0x00
#define FEC_FORCE_MODE_FIRECODE			0x01
#define FEC_FORCE_MODE_RS			0x02
#define FEC_FORCE_MODE_AUTO			0x07
#define FEC_EXTENDED_MODE_MASK			0xffffff00
#define FEC_EXTENDED_MODE_OFFSET		8
#define ETH_EXT_FEC_NONE			0x00000000
#define ETH_EXT_FEC_10G_NONE			0x00000100
#define ETH_EXT_FEC_10G_BASE_R			0x00000200
#define ETH_EXT_FEC_25G_NONE			0x00000400
#define ETH_EXT_FEC_25G_BASE_R			0x00000800
#define ETH_EXT_FEC_25G_RS528			0x00001000
#define ETH_EXT_FEC_40G_NONE			0x00002000
#define ETH_EXT_FEC_40G_BASE_R			0x00004000
#define ETH_EXT_FEC_50G_NONE			0x00008000
#define ETH_EXT_FEC_50G_BASE_R			0x00010000
#define ETH_EXT_FEC_50G_RS528			0x00020000
#define ETH_EXT_FEC_50G_RS544			0x00040000
#define ETH_EXT_FEC_100G_NONE			0x00080000
#define ETH_EXT_FEC_100G_BASE_R			0x00100000
#define ETH_EXT_FEC_100G_RS528			0x00200000
#define ETH_EXT_FEC_100G_RS544			0x00400000

	u32					extended_speed;
#define ETH_EXT_SPEED_MASK			0x0000ffff
#define ETH_EXT_SPEED_OFFSET			0
#define ETH_EXT_SPEED_NONE			0x00000001
#define ETH_EXT_SPEED_1G			0x00000002
#define ETH_EXT_SPEED_10G			0x00000004
#define ETH_EXT_SPEED_25G			0x00000008
#define ETH_EXT_SPEED_40G			0x00000010
#define ETH_EXT_SPEED_50G_BASE_R		0x00000020
#define ETH_EXT_SPEED_50G_BASE_R2		0x00000040
#define ETH_EXT_SPEED_100G_BASE_R2		0x00000080
#define ETH_EXT_SPEED_100G_BASE_R4		0x00000100
#define ETH_EXT_SPEED_100G_BASE_P4		0x00000200
#define ETH_EXT_ADV_SPEED_MASK			0xFFFF0000
#define ETH_EXT_ADV_SPEED_OFFSET		16
#define ETH_EXT_ADV_SPEED_1G			0x00010000
#define ETH_EXT_ADV_SPEED_10G			0x00020000
#define ETH_EXT_ADV_SPEED_25G			0x00040000
#define ETH_EXT_ADV_SPEED_40G			0x00080000
#define ETH_EXT_ADV_SPEED_50G_BASE_R		0x00100000
#define ETH_EXT_ADV_SPEED_50G_BASE_R2		0x00200000
#define ETH_EXT_ADV_SPEED_100G_BASE_R2		0x00400000
#define ETH_EXT_ADV_SPEED_100G_BASE_R4		0x00800000
#define ETH_EXT_ADV_SPEED_100G_BASE_P4		0x01000000
};

struct port_mf_cfg {
	u32 dynamic_cfg;
#define PORT_MF_CFG_OV_TAG_MASK		0x0000ffff
#define PORT_MF_CFG_OV_TAG_SHIFT	0
#define PORT_MF_CFG_OV_TAG_DEFAULT	PORT_MF_CFG_OV_TAG_MASK

	u32 reserved[1];
};

struct eth_stats {
	u64 r64;
	u64 r127;
	u64 r255;
	u64 r511;
	u64 r1023;
	u64 r1518;

	union {
		struct {
			u64 r1522;
			u64 r2047;
			u64 r4095;
			u64 r9216;
			u64 r16383;
		} bb0;
		struct {
			u64 unused1;
			u64 r1519_to_max;
			u64 unused2;
			u64 unused3;
			u64 unused4;
		} ah0;
	} u0;

	u64 rfcs;
	u64 rxcf;
	u64 rxpf;
	u64 rxpp;
	u64 raln;
	u64 rfcr;
	u64 rovr;
	u64 rjbr;
	u64 rund;
	u64 rfrg;
	u64 t64;
	u64 t127;
	u64 t255;
	u64 t511;
	u64 t1023;
	u64 t1518;

	union {
		struct {
			u64 t2047;
			u64 t4095;
			u64 t9216;
			u64 t16383;
		} bb1;
		struct {
			u64 t1519_to_max;
			u64 unused6;
			u64 unused7;
			u64 unused8;
		} ah1;
	} u1;

	u64 txpf;
	u64 txpp;

	union {
		struct {
			u64 tlpiec;
			u64 tncl;
		} bb2;
		struct {
			u64 unused9;
			u64 unused10;
		} ah2;
	} u2;

	u64 rbyte;
	u64 rxuca;
	u64 rxmca;
	u64 rxbca;
	u64 rxpok;
	u64 tbyte;
	u64 txuca;
	u64 txmca;
	u64 txbca;
	u64 txcf;
};

struct pkt_type_cnt {
	u64 tc_tx_pkt_cnt[8];
	u64 tc_tx_oct_cnt[8];
	u64 priority_rx_pkt_cnt[8];
	u64 priority_rx_oct_cnt[8];
};

struct brb_stats {
	u64 brb_truncate[8];
	u64 brb_discard[8];
};

struct port_stats {
	struct brb_stats brb;
	struct eth_stats eth;
};

struct couple_mode_teaming {
	u8 port_cmt[MCP_GLOB_PORT_MAX];
#define PORT_CMT_IN_TEAM	BIT(0)

#define PORT_CMT_PORT_ROLE	BIT(1)
#define PORT_CMT_PORT_INACTIVE	(0 << 1)
#define PORT_CMT_PORT_ACTIVE	BIT(1)

#define PORT_CMT_TEAM_MASK	BIT(2)
#define PORT_CMT_TEAM0		(0 << 2)
#define PORT_CMT_TEAM1		BIT(2)
};

#define LLDP_CHASSIS_ID_STAT_LEN	4
#define LLDP_PORT_ID_STAT_LEN		4
#define DCBX_MAX_APP_PROTOCOL		32
#define MAX_SYSTEM_LLDP_TLV_DATA	32
#define MAX_TLV_BUFFER			128

enum _lldp_agent {
	LLDP_NEAREST_BRIDGE = 0,
	LLDP_NEAREST_NON_TPMR_BRIDGE,
	LLDP_NEAREST_CUSTOMER_BRIDGE,
	LLDP_MAX_LLDP_AGENTS
};

struct lldp_config_params_s {
	u32 config;
#define LLDP_CONFIG_TX_INTERVAL_MASK	0x000000ff
#define LLDP_CONFIG_TX_INTERVAL_SHIFT	0
#define LLDP_CONFIG_HOLD_MASK		0x00000f00
#define LLDP_CONFIG_HOLD_SHIFT		8
#define LLDP_CONFIG_MAX_CREDIT_MASK	0x0000f000
#define LLDP_CONFIG_MAX_CREDIT_SHIFT	12
#define LLDP_CONFIG_ENABLE_RX_MASK	0x40000000
#define LLDP_CONFIG_ENABLE_RX_SHIFT	30
#define LLDP_CONFIG_ENABLE_TX_MASK	0x80000000
#define LLDP_CONFIG_ENABLE_TX_SHIFT	31
	u32 local_chassis_id[LLDP_CHASSIS_ID_STAT_LEN];
	u32 local_port_id[LLDP_PORT_ID_STAT_LEN];
};

struct lldp_status_params_s {
	u32 prefix_seq_num;
	u32 status;
	u32 peer_chassis_id[LLDP_CHASSIS_ID_STAT_LEN];
	u32 peer_port_id[LLDP_PORT_ID_STAT_LEN];
	u32 suffix_seq_num;
};

struct dcbx_ets_feature {
	u32 flags;
#define DCBX_ETS_ENABLED_MASK	0x00000001
#define DCBX_ETS_ENABLED_SHIFT	0
#define DCBX_ETS_WILLING_MASK	0x00000002
#define DCBX_ETS_WILLING_SHIFT	1
#define DCBX_ETS_ERROR_MASK	0x00000004
#define DCBX_ETS_ERROR_SHIFT	2
#define DCBX_ETS_CBS_MASK	0x00000008
#define DCBX_ETS_CBS_SHIFT	3
#define DCBX_ETS_MAX_TCS_MASK	0x000000f0
#define DCBX_ETS_MAX_TCS_SHIFT	4
#define DCBX_OOO_TC_MASK	0x00000f00
#define DCBX_OOO_TC_SHIFT	8
	u32 pri_tc_tbl[1];
#define DCBX_TCP_OOO_TC		(4)
#define DCBX_TCP_OOO_K2_4PORT_TC (3)

#define NIG_ETS_ISCSI_OOO_CLIENT_OFFSET	(DCBX_TCP_OOO_TC + 1)
#define DCBX_CEE_STRICT_PRIORITY	0xf
	u32 tc_bw_tbl[2];
	u32 tc_tsa_tbl[2];
#define DCBX_ETS_TSA_STRICT	0
#define DCBX_ETS_TSA_CBS	1
#define DCBX_ETS_TSA_ETS	2
};

#define DCBX_TCP_OOO_TC			(4)
#define DCBX_TCP_OOO_K2_4PORT_TC	(3)

struct dcbx_app_priority_entry {
	u32 entry;
#define DCBX_APP_PRI_MAP_MASK		0x000000ff
#define DCBX_APP_PRI_MAP_SHIFT		0
#define DCBX_APP_PRI_0			0x01
#define DCBX_APP_PRI_1			0x02
#define DCBX_APP_PRI_2			0x04
#define DCBX_APP_PRI_3			0x08
#define DCBX_APP_PRI_4			0x10
#define DCBX_APP_PRI_5			0x20
#define DCBX_APP_PRI_6			0x40
#define DCBX_APP_PRI_7			0x80
#define DCBX_APP_SF_MASK		0x00000300
#define DCBX_APP_SF_SHIFT		8
#define DCBX_APP_SF_ETHTYPE		0
#define DCBX_APP_SF_PORT		1
#define DCBX_APP_SF_IEEE_MASK		0x0000f000
#define DCBX_APP_SF_IEEE_SHIFT		12
#define DCBX_APP_SF_IEEE_RESERVED	0
#define DCBX_APP_SF_IEEE_ETHTYPE	1
#define DCBX_APP_SF_IEEE_TCP_PORT	2
#define DCBX_APP_SF_IEEE_UDP_PORT	3
#define DCBX_APP_SF_IEEE_TCP_UDP_PORT	4

#define DCBX_APP_PROTOCOL_ID_MASK	0xffff0000
#define DCBX_APP_PROTOCOL_ID_SHIFT	16
};

struct dcbx_app_priority_feature {
	u32 flags;
#define DCBX_APP_ENABLED_MASK		0x00000001
#define DCBX_APP_ENABLED_SHIFT		0
#define DCBX_APP_WILLING_MASK		0x00000002
#define DCBX_APP_WILLING_SHIFT		1
#define DCBX_APP_ERROR_MASK		0x00000004
#define DCBX_APP_ERROR_SHIFT		2
#define DCBX_APP_MAX_TCS_MASK		0x0000f000
#define DCBX_APP_MAX_TCS_SHIFT		12
#define DCBX_APP_NUM_ENTRIES_MASK	0x00ff0000
#define DCBX_APP_NUM_ENTRIES_SHIFT	16
	struct dcbx_app_priority_entry app_pri_tbl[DCBX_MAX_APP_PROTOCOL];
};

struct dcbx_features {
	struct dcbx_ets_feature ets;
	u32 pfc;
#define DCBX_PFC_PRI_EN_BITMAP_MASK	0x000000ff
#define DCBX_PFC_PRI_EN_BITMAP_SHIFT	0
#define DCBX_PFC_PRI_EN_BITMAP_PRI_0	0x01
#define DCBX_PFC_PRI_EN_BITMAP_PRI_1	0x02
#define DCBX_PFC_PRI_EN_BITMAP_PRI_2	0x04
#define DCBX_PFC_PRI_EN_BITMAP_PRI_3	0x08
#define DCBX_PFC_PRI_EN_BITMAP_PRI_4	0x10
#define DCBX_PFC_PRI_EN_BITMAP_PRI_5	0x20
#define DCBX_PFC_PRI_EN_BITMAP_PRI_6	0x40
#define DCBX_PFC_PRI_EN_BITMAP_PRI_7	0x80

#define DCBX_PFC_FLAGS_MASK		0x0000ff00
#define DCBX_PFC_FLAGS_SHIFT		8
#define DCBX_PFC_CAPS_MASK		0x00000f00
#define DCBX_PFC_CAPS_SHIFT		8
#define DCBX_PFC_MBC_MASK		0x00004000
#define DCBX_PFC_MBC_SHIFT		14
#define DCBX_PFC_WILLING_MASK		0x00008000
#define DCBX_PFC_WILLING_SHIFT		15
#define DCBX_PFC_ENABLED_MASK		0x00010000
#define DCBX_PFC_ENABLED_SHIFT		16
#define DCBX_PFC_ERROR_MASK		0x00020000
#define DCBX_PFC_ERROR_SHIFT		17

	struct dcbx_app_priority_feature app;
};

struct dcbx_local_params {
	u32 config;
#define DCBX_CONFIG_VERSION_MASK	0x00000007
#define DCBX_CONFIG_VERSION_SHIFT	0
#define DCBX_CONFIG_VERSION_DISABLED	0
#define DCBX_CONFIG_VERSION_IEEE	1
#define DCBX_CONFIG_VERSION_CEE		2
#define DCBX_CONFIG_VERSION_STATIC	4

	u32 flags;
	struct dcbx_features features;
};

struct dcbx_mib {
	u32 prefix_seq_num;
	u32 flags;
	struct dcbx_features features;
	u32 suffix_seq_num;
};

struct lldp_system_tlvs_buffer_s {
	u32 flags;
#define LLDP_SYSTEM_TLV_VALID_MASK 0x1
#define LLDP_SYSTEM_TLV_VALID_OFFSET 0
#define LLDP_SYSTEM_TLV_MANDATORY_MASK 0x2
#define LLDP_SYSTEM_TLV_MANDATORY_SHIFT 1
#define LLDP_SYSTEM_TLV_LENGTH_MASK 0xffff0000
#define LLDP_SYSTEM_TLV_LENGTH_SHIFT 16
	u32 data[MAX_SYSTEM_LLDP_TLV_DATA];
};

struct lldp_received_tlvs_s {
	u32 prefix_seq_num;
	u32 length;
	u32 tlvs_buffer[MAX_TLV_BUFFER];
	u32 suffix_seq_num;
};

struct dcb_dscp_map {
	u32 flags;
#define DCB_DSCP_ENABLE_MASK	0x1
#define DCB_DSCP_ENABLE_SHIFT	0
#define DCB_DSCP_ENABLE	1
	u32 dscp_pri_map[8];
};

struct mcp_val64 {
	u32 lo;
	u32 hi;
};

struct generic_idc_msg_s {
	u32 source_pf;
	struct mcp_val64 msg;
};

struct pcie_stats_stc {
	u32 sr_cnt_wr_byte_msb;
	u32 sr_cnt_wr_byte_lsb;
	u32 sr_cnt_wr_cnt;
	u32 sr_cnt_rd_byte_msb;
	u32 sr_cnt_rd_byte_lsb;
	u32 sr_cnt_rd_cnt;
};

enum _attribute_commands_e {
	ATTRIBUTE_CMD_READ = 0,
	ATTRIBUTE_CMD_WRITE,
	ATTRIBUTE_CMD_READ_CLEAR,
	ATTRIBUTE_CMD_CLEAR,
	ATTRIBUTE_NUM_OF_COMMANDS
};

struct public_global {
	u32 max_path;
	u32 max_ports;
#define MODE_1P 1
#define MODE_2P 2
#define MODE_3P 3
#define MODE_4P 4
	u32 debug_mb_offset;
	u32 phymod_dbg_mb_offset;
	struct couple_mode_teaming cmt;
	s32 internal_temperature;
	u32 mfw_ver;
	u32 running_bundle_id;
	s32 external_temperature;
	u32 mdump_reason;
	u32 ext_phy_upgrade_fw;
	u8 runtime_port_swap_map[MODE_4P];
	u32 data_ptr;
	u32 data_size;
	u32 bmb_error_status_cnt;
	u32 bmb_jumbo_frame_cnt;
	u32 sent_to_bmc_cnt;
	u32 handled_by_mfw;
	u32 sent_to_nw_cnt;
	u32 to_bmc_kb_per_second;
	u32 bcast_dropped_to_bmc_cnt;
	u32 mcast_dropped_to_bmc_cnt;
	u32 ucast_dropped_to_bmc_cnt;
	u32 ncsi_response_failure_cnt;
	u32 device_attr;
	u32 vpd_warning;
};

struct fw_flr_mb {
	u32 aggint;
	u32 opgen_addr;
	u32 accum_ack;
};

struct public_path {
	struct fw_flr_mb flr_mb;
	u32 mcp_vf_disabled[VF_MAX_STATIC / 32];

	u32 process_kill;
#define PROCESS_KILL_COUNTER_MASK	0x0000ffff
#define PROCESS_KILL_COUNTER_SHIFT	0
#define PROCESS_KILL_GLOB_AEU_BIT_MASK	0xffff0000
#define PROCESS_KILL_GLOB_AEU_BIT_SHIFT	16
#define GLOBAL_AEU_BIT(aeu_reg_id, aeu_bit) ((aeu_reg_id) * 32 + (aeu_bit))
};

#define FC_NPIV_WWPN_SIZE	8
#define FC_NPIV_WWNN_SIZE	8
struct dci_npiv_settings {
	u8 npiv_wwpn[FC_NPIV_WWPN_SIZE];
	u8 npiv_wwnn[FC_NPIV_WWNN_SIZE];
};

struct dci_fc_npiv_cfg {
	/* hdr used internally by the MFW */
	u32 hdr;
	u32 num_of_npiv;
};

#define MAX_NUMBER_NPIV    64
struct dci_fc_npiv_tbl {
	struct dci_fc_npiv_cfg fc_npiv_cfg;
	struct dci_npiv_settings settings[MAX_NUMBER_NPIV];
};

struct pause_flood_monitor {
	u8 period_cnt;
	u8 any_brb_prs_packet_hist;
	u8 any_brb_block_is_full_hist;
	u8 flags;
	u32 num_of_state_changes;
};

struct public_port {
	u32						validity_map;

	u32						link_status;
#define LINK_STATUS_LINK_UP				0x00000001
#define LINK_STATUS_SPEED_AND_DUPLEX_MASK		0x0000001e
#define LINK_STATUS_SPEED_AND_DUPLEX_1000THD		BIT(1)
#define LINK_STATUS_SPEED_AND_DUPLEX_1000TFD		(2 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_10G		(3 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_20G		(4 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_40G		(5 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_50G		(6 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_100G		(7 << 1)
#define LINK_STATUS_SPEED_AND_DUPLEX_25G		(8 << 1)
#define LINK_STATUS_AUTO_NEGOTIATE_ENABLED		0x00000020
#define LINK_STATUS_AUTO_NEGOTIATE_COMPLETE		0x00000040
#define LINK_STATUS_PARALLEL_DETECTION_USED		0x00000080
#define LINK_STATUS_PFC_ENABLED				0x00000100
#define LINK_STATUS_LINK_PARTNER_1000TFD_CAPABLE	0x00000200
#define LINK_STATUS_LINK_PARTNER_1000THD_CAPABLE	0x00000400
#define LINK_STATUS_LINK_PARTNER_10G_CAPABLE		0x00000800
#define LINK_STATUS_LINK_PARTNER_20G_CAPABLE		0x00001000
#define LINK_STATUS_LINK_PARTNER_40G_CAPABLE		0x00002000
#define LINK_STATUS_LINK_PARTNER_50G_CAPABLE		0x00004000
#define LINK_STATUS_LINK_PARTNER_100G_CAPABLE		0x00008000
#define LINK_STATUS_LINK_PARTNER_25G_CAPABLE		0x00010000
#define LINK_STATUS_LINK_PARTNER_FLOW_CONTROL_MASK	0x000c0000
#define LINK_STATUS_LINK_PARTNER_NOT_PAUSE_CAPABLE	(0 << 18)
#define LINK_STATUS_LINK_PARTNER_SYMMETRIC_PAUSE	BIT(18)
#define LINK_STATUS_LINK_PARTNER_ASYMMETRIC_PAUSE	(2 << 18)
#define LINK_STATUS_LINK_PARTNER_BOTH_PAUSE		(3 << 18)
#define LINK_STATUS_SFP_TX_FAULT			0x00100000
#define LINK_STATUS_TX_FLOW_CONTROL_ENABLED		0x00200000
#define LINK_STATUS_RX_FLOW_CONTROL_ENABLED		0x00400000
#define LINK_STATUS_RX_SIGNAL_PRESENT			0x00800000
#define LINK_STATUS_MAC_LOCAL_FAULT			0x01000000
#define LINK_STATUS_MAC_REMOTE_FAULT			0x02000000
#define LINK_STATUS_UNSUPPORTED_SPD_REQ			0x04000000

#define LINK_STATUS_FEC_MODE_MASK			0x38000000
#define LINK_STATUS_FEC_MODE_NONE			(0 << 27)
#define LINK_STATUS_FEC_MODE_FIRECODE_CL74		BIT(27)
#define LINK_STATUS_FEC_MODE_RS_CL91			(2 << 27)
#define LINK_STATUS_EXT_PHY_LINK_UP			BIT(30)

	u32 link_status1;
	u32 ext_phy_fw_version;
	u32 drv_phy_cfg_addr;

	u32 port_stx;

	u32 stat_nig_timer;

	struct port_mf_cfg port_mf_config;
	struct port_stats stats;

	u32 media_type;
#define MEDIA_UNSPECIFIED	0x0
#define MEDIA_SFPP_10G_FIBER	0x1
#define MEDIA_XFP_FIBER		0x2
#define MEDIA_DA_TWINAX		0x3
#define MEDIA_BASE_T		0x4
#define MEDIA_SFP_1G_FIBER	0x5
#define MEDIA_MODULE_FIBER	0x6
#define MEDIA_KR		0xf0
#define MEDIA_NOT_PRESENT	0xff

	u32 lfa_status;
	u32 link_change_count;

	struct lldp_config_params_s lldp_config_params[LLDP_MAX_LLDP_AGENTS];
	struct lldp_status_params_s lldp_status_params[LLDP_MAX_LLDP_AGENTS];
	struct lldp_system_tlvs_buffer_s system_lldp_tlvs_buf;

	/* DCBX related MIB */
	struct dcbx_local_params local_admin_dcbx_mib;
	struct dcbx_mib remote_dcbx_mib;
	struct dcbx_mib operational_dcbx_mib;

	u32 fc_npiv_nvram_tbl_addr;
	u32 fc_npiv_nvram_tbl_size;

	u32						transceiver_data;
#define ETH_TRANSCEIVER_STATE_MASK			0x000000ff
#define ETH_TRANSCEIVER_STATE_SHIFT			0x00000000
#define ETH_TRANSCEIVER_STATE_OFFSET			0x00000000
#define ETH_TRANSCEIVER_STATE_UNPLUGGED			0x00000000
#define ETH_TRANSCEIVER_STATE_PRESENT			0x00000001
#define ETH_TRANSCEIVER_STATE_VALID			0x00000003
#define ETH_TRANSCEIVER_STATE_UPDATING			0x00000008
#define ETH_TRANSCEIVER_STATE_IN_SETUP			0x10
#define ETH_TRANSCEIVER_TYPE_MASK			0x0000ff00
#define ETH_TRANSCEIVER_TYPE_OFFSET			0x8
#define ETH_TRANSCEIVER_TYPE_NONE			0x00
#define ETH_TRANSCEIVER_TYPE_UNKNOWN			0xff
#define ETH_TRANSCEIVER_TYPE_1G_PCC			0x01
#define ETH_TRANSCEIVER_TYPE_1G_ACC			0x02
#define ETH_TRANSCEIVER_TYPE_1G_LX			0x03
#define ETH_TRANSCEIVER_TYPE_1G_SX			0x04
#define ETH_TRANSCEIVER_TYPE_10G_SR			0x05
#define ETH_TRANSCEIVER_TYPE_10G_LR			0x06
#define ETH_TRANSCEIVER_TYPE_10G_LRM			0x07
#define ETH_TRANSCEIVER_TYPE_10G_ER			0x08
#define ETH_TRANSCEIVER_TYPE_10G_PCC			0x09
#define ETH_TRANSCEIVER_TYPE_10G_ACC			0x0a
#define ETH_TRANSCEIVER_TYPE_XLPPI			0x0b
#define ETH_TRANSCEIVER_TYPE_40G_LR4			0x0c
#define ETH_TRANSCEIVER_TYPE_40G_SR4			0x0d
#define ETH_TRANSCEIVER_TYPE_40G_CR4			0x0e
#define ETH_TRANSCEIVER_TYPE_100G_AOC			0x0f
#define ETH_TRANSCEIVER_TYPE_100G_SR4			0x10
#define ETH_TRANSCEIVER_TYPE_100G_LR4			0x11
#define ETH_TRANSCEIVER_TYPE_100G_ER4			0x12
#define ETH_TRANSCEIVER_TYPE_100G_ACC			0x13
#define ETH_TRANSCEIVER_TYPE_100G_CR4			0x14
#define ETH_TRANSCEIVER_TYPE_4x10G_SR			0x15
#define ETH_TRANSCEIVER_TYPE_25G_CA_N			0x16
#define ETH_TRANSCEIVER_TYPE_25G_ACC_S			0x17
#define ETH_TRANSCEIVER_TYPE_25G_CA_S			0x18
#define ETH_TRANSCEIVER_TYPE_25G_ACC_M			0x19
#define ETH_TRANSCEIVER_TYPE_25G_CA_L			0x1a
#define ETH_TRANSCEIVER_TYPE_25G_ACC_L			0x1b
#define ETH_TRANSCEIVER_TYPE_25G_SR			0x1c
#define ETH_TRANSCEIVER_TYPE_25G_LR			0x1d
#define ETH_TRANSCEIVER_TYPE_25G_AOC			0x1e
#define ETH_TRANSCEIVER_TYPE_4x10G			0x1f
#define ETH_TRANSCEIVER_TYPE_4x25G_CR			0x20
#define ETH_TRANSCEIVER_TYPE_1000BASET			0x21
#define ETH_TRANSCEIVER_TYPE_10G_BASET			0x22
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_SR	0x30
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_CR	0x31
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_LR	0x32
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_SR	0x33
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_CR	0x34
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_LR	0x35
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_AOC	0x36
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_25G_SR	0x37
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_25G_LR	0x38
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_1G_10G_SR	0x39
#define ETH_TRANSCEIVER_TYPE_MULTI_RATE_1G_10G_LR	0x3a

	u32 wol_info;
	u32 wol_pkt_len;
	u32 wol_pkt_details;
	struct dcb_dscp_map dcb_dscp_map;

	u32 eee_status;
#define EEE_ACTIVE_BIT			BIT(0)
#define EEE_LD_ADV_STATUS_MASK		0x000000f0
#define EEE_LD_ADV_STATUS_OFFSET	4
#define EEE_1G_ADV			BIT(1)
#define EEE_10G_ADV			BIT(2)
#define EEE_LP_ADV_STATUS_MASK		0x00000f00
#define EEE_LP_ADV_STATUS_OFFSET	8
#define EEE_SUPPORTED_SPEED_MASK	0x0000f000
#define EEE_SUPPORTED_SPEED_OFFSET	12
#define EEE_1G_SUPPORTED		BIT(1)
#define EEE_10G_SUPPORTED		BIT(2)

	u32 eee_remote;
#define EEE_REMOTE_TW_TX_MASK   0x0000ffff
#define EEE_REMOTE_TW_TX_OFFSET 0
#define EEE_REMOTE_TW_RX_MASK   0xffff0000
#define EEE_REMOTE_TW_RX_OFFSET 16

	u32 module_info;

	u32 oem_cfg_port;
#define OEM_CFG_CHANNEL_TYPE_MASK                       0x00000003
#define OEM_CFG_CHANNEL_TYPE_OFFSET                     0
#define OEM_CFG_CHANNEL_TYPE_VLAN_PARTITION             0x1
#define OEM_CFG_CHANNEL_TYPE_STAGGED                    0x2
#define OEM_CFG_SCHED_TYPE_MASK                         0x0000000C
#define OEM_CFG_SCHED_TYPE_OFFSET                       2
#define OEM_CFG_SCHED_TYPE_ETS                          0x1
#define OEM_CFG_SCHED_TYPE_VNIC_BW                      0x2

	struct lldp_received_tlvs_s lldp_received_tlvs[LLDP_MAX_LLDP_AGENTS];
	u32 system_lldp_tlvs_buf2[MAX_SYSTEM_LLDP_TLV_DATA];
	u32 phy_module_temperature;
	u32 nig_reg_stat_rx_bmb_packet;
	u32 nig_reg_rx_llh_ncsi_mcp_mask;
	u32 nig_reg_rx_llh_ncsi_mcp_mask_2;
	struct pause_flood_monitor pause_flood_monitor;
	u32 nig_drain_cnt;
	struct pkt_type_cnt pkt_tc_priority_cnt;
};

#define MCP_DRV_VER_STR_SIZE 16
#define MCP_DRV_VER_STR_SIZE_DWORD (MCP_DRV_VER_STR_SIZE / sizeof(u32))
#define MCP_DRV_NVM_BUF_LEN 32
struct drv_version_stc {
	u32 version;
	u8 name[MCP_DRV_VER_STR_SIZE - 4];
};

struct public_func {
	u32 iscsi_boot_signature;
	u32 iscsi_boot_block_offset;

	u32 mtu_size;

	u32 c2s_pcp_map_lower;
	u32 c2s_pcp_map_upper;
	u32 c2s_pcp_map_default;

	struct generic_idc_msg_s generic_idc_msg;

	u32 num_of_msix;

	u32 config;
#define FUNC_MF_CFG_FUNC_HIDE			0x00000001
#define FUNC_MF_CFG_PAUSE_ON_HOST_RING		0x00000002
#define FUNC_MF_CFG_PAUSE_ON_HOST_RING_SHIFT	0x00000001

#define FUNC_MF_CFG_PROTOCOL_MASK	0x000000f0
#define FUNC_MF_CFG_PROTOCOL_SHIFT	4
#define FUNC_MF_CFG_PROTOCOL_ETHERNET	0x00000000
#define FUNC_MF_CFG_PROTOCOL_ISCSI              0x00000010
#define FUNC_MF_CFG_PROTOCOL_FCOE               0x00000020
#define FUNC_MF_CFG_PROTOCOL_ROCE               0x00000030
#define FUNC_MF_CFG_PROTOCOL_MAX	0x00000030

#define FUNC_MF_CFG_MIN_BW_MASK		0x0000ff00
#define FUNC_MF_CFG_MIN_BW_SHIFT	8
#define FUNC_MF_CFG_MIN_BW_DEFAULT	0x00000000
#define FUNC_MF_CFG_MAX_BW_MASK		0x00ff0000
#define FUNC_MF_CFG_MAX_BW_SHIFT	16
#define FUNC_MF_CFG_MAX_BW_DEFAULT	0x00640000

	u32 status;
#define FUNC_STATUS_VIRTUAL_LINK_UP	0x00000001

	u32 mac_upper;
#define FUNC_MF_CFG_UPPERMAC_MASK	0x0000ffff
#define FUNC_MF_CFG_UPPERMAC_SHIFT	0
#define FUNC_MF_CFG_UPPERMAC_DEFAULT	FUNC_MF_CFG_UPPERMAC_MASK
	u32 mac_lower;
#define FUNC_MF_CFG_LOWERMAC_DEFAULT	0xffffffff

	u32 fcoe_wwn_port_name_upper;
	u32 fcoe_wwn_port_name_lower;

	u32 fcoe_wwn_node_name_upper;
	u32 fcoe_wwn_node_name_lower;

	u32 ovlan_stag;
#define FUNC_MF_CFG_OV_STAG_MASK	0x0000ffff
#define FUNC_MF_CFG_OV_STAG_SHIFT	0
#define FUNC_MF_CFG_OV_STAG_DEFAULT	FUNC_MF_CFG_OV_STAG_MASK

	u32 pf_allocation;

	u32 preserve_data;

	u32 driver_last_activity_ts;

	u32 drv_ack_vf_disabled[VF_MAX_STATIC / 32];

	u32 drv_id;
#define DRV_ID_PDA_COMP_VER_MASK	0x0000ffff
#define DRV_ID_PDA_COMP_VER_SHIFT	0

#define LOAD_REQ_HSI_VERSION		2
#define DRV_ID_MCP_HSI_VER_MASK		0x00ff0000
#define DRV_ID_MCP_HSI_VER_SHIFT	16
#define DRV_ID_MCP_HSI_VER_CURRENT	(LOAD_REQ_HSI_VERSION << \
					 DRV_ID_MCP_HSI_VER_SHIFT)

#define DRV_ID_DRV_TYPE_MASK		0x7f000000
#define DRV_ID_DRV_TYPE_SHIFT		24
#define DRV_ID_DRV_TYPE_UNKNOWN		(0 << DRV_ID_DRV_TYPE_SHIFT)
#define DRV_ID_DRV_TYPE_LINUX		BIT(DRV_ID_DRV_TYPE_SHIFT)

#define DRV_ID_DRV_INIT_HW_MASK		0x80000000
#define DRV_ID_DRV_INIT_HW_SHIFT	31
#define DRV_ID_DRV_INIT_HW_FLAG		BIT(DRV_ID_DRV_INIT_HW_SHIFT)

	u32 oem_cfg_func;
#define OEM_CFG_FUNC_TC_MASK                    0x0000000F
#define OEM_CFG_FUNC_TC_OFFSET                  0
#define OEM_CFG_FUNC_TC_0                       0x0
#define OEM_CFG_FUNC_TC_1                       0x1
#define OEM_CFG_FUNC_TC_2                       0x2
#define OEM_CFG_FUNC_TC_3                       0x3
#define OEM_CFG_FUNC_TC_4                       0x4
#define OEM_CFG_FUNC_TC_5                       0x5
#define OEM_CFG_FUNC_TC_6                       0x6
#define OEM_CFG_FUNC_TC_7                       0x7

#define OEM_CFG_FUNC_HOST_PRI_CTRL_MASK         0x00000030
#define OEM_CFG_FUNC_HOST_PRI_CTRL_OFFSET       4
#define OEM_CFG_FUNC_HOST_PRI_CTRL_VNIC         0x1
#define OEM_CFG_FUNC_HOST_PRI_CTRL_OS           0x2

	struct drv_version_stc drv_ver;
};

struct mcp_mac {
	u32 mac_upper;
	u32 mac_lower;
};

struct mcp_file_att {
	u32 nvm_start_addr;
	u32 len;
};

struct bist_nvm_image_att {
	u32 return_code;
	u32 image_type;
	u32 nvm_start_addr;
	u32 len;
};

struct lan_stats_stc {
	u64 ucast_rx_pkts;
	u64 ucast_tx_pkts;
	u32 fcs_err;
	u32 rserved;
};

struct fcoe_stats_stc {
	u64 rx_pkts;
	u64 tx_pkts;
	u32 fcs_err;
	u32 login_failure;
};

struct iscsi_stats_stc {
	u64 rx_pdus;
	u64 tx_pdus;
	u64 rx_bytes;
	u64 tx_bytes;
};

struct rdma_stats_stc {
	u64 rx_pkts;
	u64 tx_pkts;
	u64 rx_bytes;
	u64 tx_bytes;
};

struct ocbb_data_stc {
	u32 ocbb_host_addr;
	u32 ocsd_host_addr;
	u32 ocsd_req_update_interval;
};

struct fcoe_cap_stc {
	u32 max_ios;
	u32 max_log;
	u32 max_exch;
	u32 max_npiv;
	u32 max_tgt;
	u32 max_outstnd;
};

#define MAX_NUM_OF_SENSORS 7
struct temperature_status_stc {
	u32 num_of_sensors;
	u32 sensor[MAX_NUM_OF_SENSORS];
};

/* crash dump configuration header */
struct mdump_config_stc {
	u32 version;
	u32 config;
	u32 epoc;
	u32 num_of_logs;
	u32 valid_logs;
};

enum resource_id_enum {
	RESOURCE_NUM_SB_E = 0,
	RESOURCE_NUM_L2_QUEUE_E = 1,
	RESOURCE_NUM_VPORT_E = 2,
	RESOURCE_NUM_VMQ_E = 3,
	RESOURCE_FACTOR_NUM_RSS_PF_E = 4,
	RESOURCE_FACTOR_RSS_PER_VF_E = 5,
	RESOURCE_NUM_RL_E = 6,
	RESOURCE_NUM_PQ_E = 7,
	RESOURCE_NUM_VF_E = 8,
	RESOURCE_VFC_FILTER_E = 9,
	RESOURCE_ILT_E = 10,
	RESOURCE_CQS_E = 11,
	RESOURCE_GFT_PROFILES_E = 12,
	RESOURCE_NUM_TC_E = 13,
	RESOURCE_NUM_RSS_ENGINES_E = 14,
	RESOURCE_LL2_QUEUE_E = 15,
	RESOURCE_RDMA_STATS_QUEUE_E = 16,
	RESOURCE_BDQ_E = 17,
	RESOURCE_QCN_E = 18,
	RESOURCE_LLH_FILTER_E = 19,
	RESOURCE_VF_MAC_ADDR = 20,
	RESOURCE_LL2_CQS_E = 21,
	RESOURCE_VF_CNQS = 22,
	RESOURCE_MAX_NUM,
	RESOURCE_NUM_INVALID = 0xFFFFFFFF
};

/* Resource ID is to be filled by the driver in the MB request
 * Size, offset & flags to be filled by the MFW in the MB response
 */
struct resource_info {
	enum resource_id_enum res_id;
	u32 size;		/* number of allocated resources */
	u32 offset;		/* Offset of the 1st resource */
	u32 vf_size;
	u32 vf_offset;
	u32 flags;
#define RESOURCE_ELEMENT_STRICT BIT(0)
};

struct mcp_wwn {
	u32 wwn_upper;
	u32 wwn_lower;
};

#define DRV_ROLE_NONE           0
#define DRV_ROLE_PREBOOT        1
#define DRV_ROLE_OS             2
#define DRV_ROLE_KDUMP          3

struct load_req_stc {
	u32 drv_ver_0;
	u32 drv_ver_1;
	u32 fw_ver;
	u32 misc0;
#define LOAD_REQ_ROLE_MASK              0x000000FF
#define LOAD_REQ_ROLE_SHIFT             0
#define LOAD_REQ_LOCK_TO_MASK           0x0000FF00
#define LOAD_REQ_LOCK_TO_SHIFT          8
#define LOAD_REQ_LOCK_TO_DEFAULT        0
#define LOAD_REQ_LOCK_TO_NONE           255
#define LOAD_REQ_FORCE_MASK             0x000F0000
#define LOAD_REQ_FORCE_SHIFT            16
#define LOAD_REQ_FORCE_NONE             0
#define LOAD_REQ_FORCE_PF               1
#define LOAD_REQ_FORCE_ALL              2
#define LOAD_REQ_FLAGS0_MASK            0x00F00000
#define LOAD_REQ_FLAGS0_SHIFT           20
#define LOAD_REQ_FLAGS0_AVOID_RESET     (0x1 << 0)
};

struct load_rsp_stc {
	u32 drv_ver_0;
	u32 drv_ver_1;
	u32 fw_ver;
	u32 misc0;
#define LOAD_RSP_ROLE_MASK              0x000000FF
#define LOAD_RSP_ROLE_SHIFT             0
#define LOAD_RSP_HSI_MASK               0x0000FF00
#define LOAD_RSP_HSI_SHIFT              8
#define LOAD_RSP_FLAGS0_MASK            0x000F0000
#define LOAD_RSP_FLAGS0_SHIFT           16
#define LOAD_RSP_FLAGS0_DRV_EXISTS      (0x1 << 0)
};

struct mdump_retain_data_stc {
	u32 valid;
	u32 epoch;
	u32 pf;
	u32 status;
};

struct attribute_cmd_write_stc {
	u32 val;
	u32 mask;
	u32 offset;
};

struct lldp_stats_stc {
	u32 tx_frames_total;
	u32 rx_frames_total;
	u32 rx_frames_discarded;
	u32 rx_age_outs;
};

struct get_att_ctrl_stc {
	u32 disabled_attns;
	u32 controllable_attns;
};

struct trace_filter_stc {
	u32 level;
	u32 modules;
};

union drv_union_data {
	struct mcp_mac wol_mac;

	struct eth_phy_cfg drv_phy_cfg;

	struct mcp_val64 val64;

	u8 raw_data[MCP_DRV_NVM_BUF_LEN];

	struct mcp_file_att file_att;

	u32 ack_vf_disabled[EXT_VF_BITMAP_SIZE_IN_DWORDS];

	struct drv_version_stc drv_version;

	struct lan_stats_stc lan_stats;
	struct fcoe_stats_stc fcoe_stats;
	struct iscsi_stats_stc iscsi_stats;
	struct rdma_stats_stc rdma_stats;
	struct ocbb_data_stc ocbb_info;
	struct temperature_status_stc temp_info;
	struct resource_info resource;
	struct bist_nvm_image_att nvm_image_att;
	struct mdump_config_stc mdump_config;
	struct mcp_mac lldp_mac;
	struct mcp_wwn fcoe_fabric_name;
	u32 dword;

	struct load_req_stc load_req;
	struct load_rsp_stc load_rsp;
	struct mdump_retain_data_stc mdump_retain;
	struct attribute_cmd_write_stc attribute_cmd_write;
	struct lldp_stats_stc lldp_stats;
	struct pcie_stats_stc pcie_stats;

	struct get_att_ctrl_stc get_att_ctrl;
	struct fcoe_cap_stc fcoe_cap;
	struct trace_filter_stc trace_filter;
};

struct public_drv_mb {
	u32 drv_mb_header;
#define DRV_MSG_SEQ_NUMBER_MASK			0x0000ffff
#define DRV_MSG_SEQ_NUMBER_OFFSET		0
#define DRV_MSG_CODE_MASK			0xffff0000
#define DRV_MSG_CODE_OFFSET			16

	u32 drv_mb_param;

	u32 fw_mb_header;
#define FW_MSG_SEQ_NUMBER_MASK			0x0000ffff
#define FW_MSG_SEQ_NUMBER_OFFSET		0
#define FW_MSG_CODE_MASK			0xffff0000
#define FW_MSG_CODE_OFFSET			16

	u32 fw_mb_param;

	u32 drv_pulse_mb;
#define DRV_PULSE_SEQ_MASK			0x00007fff
#define DRV_PULSE_SYSTEM_TIME_MASK		0xffff0000
#define DRV_PULSE_ALWAYS_ALIVE			0x00008000

	u32 mcp_pulse_mb;
#define MCP_PULSE_SEQ_MASK			0x00007fff
#define MCP_PULSE_ALWAYS_ALIVE			0x00008000
#define MCP_EVENT_MASK				0xffff0000
#define MCP_EVENT_OTHER_DRIVER_RESET_REQ	0x00010000

	union drv_union_data union_data;
};

#define DRV_MSG_CODE(_code_)    ((_code_) << DRV_MSG_CODE_OFFSET)
enum drv_msg_code_enum {
	DRV_MSG_CODE_NVM_PUT_FILE_BEGIN = DRV_MSG_CODE(0x0001),
	DRV_MSG_CODE_NVM_PUT_FILE_DATA = DRV_MSG_CODE(0x0002),
	DRV_MSG_CODE_NVM_GET_FILE_ATT = DRV_MSG_CODE(0x0003),
	DRV_MSG_CODE_NVM_READ_NVRAM = DRV_MSG_CODE(0x0005),
	DRV_MSG_CODE_NVM_WRITE_NVRAM = DRV_MSG_CODE(0x0006),
	DRV_MSG_CODE_MCP_RESET = DRV_MSG_CODE(0x0009),
	DRV_MSG_CODE_SET_VERSION = DRV_MSG_CODE(0x000f),
	DRV_MSG_CODE_MCP_HALT = DRV_MSG_CODE(0x0010),
	DRV_MSG_CODE_SET_VMAC = DRV_MSG_CODE(0x0011),
	DRV_MSG_CODE_GET_VMAC = DRV_MSG_CODE(0x0012),
	DRV_MSG_CODE_GET_STATS = DRV_MSG_CODE(0x0013),
	DRV_MSG_CODE_TRANSCEIVER_READ = DRV_MSG_CODE(0x0016),
	DRV_MSG_CODE_MASK_PARITIES = DRV_MSG_CODE(0x001a),
	DRV_MSG_CODE_BIST_TEST = DRV_MSG_CODE(0x001e),
	DRV_MSG_CODE_SET_LED_MODE = DRV_MSG_CODE(0x0020),
	DRV_MSG_CODE_RESOURCE_CMD = DRV_MSG_CODE(0x0023),
	DRV_MSG_CODE_MDUMP_CMD = DRV_MSG_CODE(0x0025),
	DRV_MSG_CODE_GET_PF_RDMA_PROTOCOL = DRV_MSG_CODE(0x002b),
	DRV_MSG_CODE_OS_WOL = DRV_MSG_CODE(0x002e),
	DRV_MSG_CODE_GET_TLV_DONE = DRV_MSG_CODE(0x002f),
	DRV_MSG_CODE_FEATURE_SUPPORT = DRV_MSG_CODE(0x0030),
	DRV_MSG_CODE_GET_MFW_FEATURE_SUPPORT = DRV_MSG_CODE(0x0031),
	DRV_MSG_CODE_GET_ENGINE_CONFIG = DRV_MSG_CODE(0x0037),
	DRV_MSG_CODE_GET_NVM_CFG_OPTION = DRV_MSG_CODE(0x003e),
	DRV_MSG_CODE_SET_NVM_CFG_OPTION = DRV_MSG_CODE(0x003f),
	DRV_MSG_CODE_INITIATE_PF_FLR = DRV_MSG_CODE(0x0201),
	DRV_MSG_CODE_LOAD_REQ = DRV_MSG_CODE(0x1000),
	DRV_MSG_CODE_LOAD_DONE = DRV_MSG_CODE(0x1100),
	DRV_MSG_CODE_INIT_HW = DRV_MSG_CODE(0x1200),
	DRV_MSG_CODE_CANCEL_LOAD_REQ = DRV_MSG_CODE(0x1300),
	DRV_MSG_CODE_UNLOAD_REQ = DRV_MSG_CODE(0x2000),
	DRV_MSG_CODE_UNLOAD_DONE = DRV_MSG_CODE(0x2100),
	DRV_MSG_CODE_INIT_PHY = DRV_MSG_CODE(0x2200),
	DRV_MSG_CODE_LINK_RESET = DRV_MSG_CODE(0x2300),
	DRV_MSG_CODE_SET_DCBX = DRV_MSG_CODE(0x2500),
	DRV_MSG_CODE_OV_UPDATE_CURR_CFG = DRV_MSG_CODE(0x2600),
	DRV_MSG_CODE_OV_UPDATE_BUS_NUM = DRV_MSG_CODE(0x2700),
	DRV_MSG_CODE_OV_UPDATE_BOOT_PROGRESS = DRV_MSG_CODE(0x2800),
	DRV_MSG_CODE_OV_UPDATE_STORM_FW_VER = DRV_MSG_CODE(0x2900),
	DRV_MSG_CODE_NIG_DRAIN = DRV_MSG_CODE(0x3000),
	DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE = DRV_MSG_CODE(0x3100),
	DRV_MSG_CODE_BW_UPDATE_ACK = DRV_MSG_CODE(0x3200),
	DRV_MSG_CODE_OV_UPDATE_MTU = DRV_MSG_CODE(0x3300),
	DRV_MSG_GET_RESOURCE_ALLOC_MSG = DRV_MSG_CODE(0x3400),
	DRV_MSG_SET_RESOURCE_VALUE_MSG = DRV_MSG_CODE(0x3500),
	DRV_MSG_CODE_OV_UPDATE_WOL = DRV_MSG_CODE(0x3800),
	DRV_MSG_CODE_OV_UPDATE_ESWITCH_MODE = DRV_MSG_CODE(0x3900),
	DRV_MSG_CODE_S_TAG_UPDATE_ACK = DRV_MSG_CODE(0x3b00),
	DRV_MSG_CODE_GET_OEM_UPDATES = DRV_MSG_CODE(0x4100),
	DRV_MSG_CODE_GET_PPFID_BITMAP = DRV_MSG_CODE(0x4300),
	DRV_MSG_CODE_VF_DISABLED_DONE = DRV_MSG_CODE(0xc000),
	DRV_MSG_CODE_CFG_VF_MSIX = DRV_MSG_CODE(0xc001),
	DRV_MSG_CODE_CFG_PF_VFS_MSIX = DRV_MSG_CODE(0xc002),
	DRV_MSG_CODE_DEBUG_DATA_SEND = DRV_MSG_CODE(0xc004),
	DRV_MSG_CODE_GET_MANAGEMENT_STATUS = DRV_MSG_CODE(0xc007),
};

#define DRV_MSG_CODE_VMAC_TYPE_SHIFT            4
#define DRV_MSG_CODE_VMAC_TYPE_MASK             0x30
#define DRV_MSG_CODE_VMAC_TYPE_MAC              1
#define DRV_MSG_CODE_VMAC_TYPE_WWNN             2
#define DRV_MSG_CODE_VMAC_TYPE_WWPN             3

/* DRV_MSG_CODE_RETAIN_VMAC parameters */
#define DRV_MSG_CODE_RETAIN_VMAC_FUNC_SHIFT 0
#define DRV_MSG_CODE_RETAIN_VMAC_FUNC_MASK 0xf

#define DRV_MSG_CODE_RETAIN_VMAC_TYPE_SHIFT 4
#define DRV_MSG_CODE_RETAIN_VMAC_TYPE_MASK 0x70
#define DRV_MSG_CODE_RETAIN_VMAC_TYPE_L2 0
#define DRV_MSG_CODE_RETAIN_VMAC_TYPE_ISCSI 1
#define DRV_MSG_CODE_RETAIN_VMAC_TYPE_FCOE 2
#define DRV_MSG_CODE_RETAIN_VMAC_TYPE_WWNN 3
#define DRV_MSG_CODE_RETAIN_VMAC_TYPE_WWPN 4

#define DRV_MSG_CODE_MCP_RESET_FORCE 0xf04ce

#define DRV_MSG_CODE_STATS_TYPE_LAN             1
#define DRV_MSG_CODE_STATS_TYPE_FCOE            2
#define DRV_MSG_CODE_STATS_TYPE_ISCSI           3
#define DRV_MSG_CODE_STATS_TYPE_RDMA            4

#define BW_MAX_MASK 0x000000ff
#define BW_MAX_OFFSET 0
#define BW_MIN_MASK 0x0000ff00
#define BW_MIN_OFFSET 8

#define DRV_MSG_FAN_FAILURE_TYPE BIT(0)
#define DRV_MSG_TEMPERATURE_FAILURE_TYPE BIT(1)

#define RESOURCE_CMD_REQ_RESC_MASK		0x0000001F
#define RESOURCE_CMD_REQ_RESC_SHIFT		0
#define RESOURCE_CMD_REQ_OPCODE_MASK		0x000000E0
#define RESOURCE_CMD_REQ_OPCODE_SHIFT		5
#define RESOURCE_OPCODE_REQ			1
#define RESOURCE_OPCODE_REQ_WO_AGING		2
#define RESOURCE_OPCODE_REQ_W_AGING		3
#define RESOURCE_OPCODE_RELEASE			4
#define RESOURCE_OPCODE_FORCE_RELEASE		5
#define RESOURCE_CMD_REQ_AGE_MASK		0x0000FF00
#define RESOURCE_CMD_REQ_AGE_SHIFT		8

#define RESOURCE_CMD_RSP_OWNER_MASK		0x000000FF
#define RESOURCE_CMD_RSP_OWNER_SHIFT		0
#define RESOURCE_CMD_RSP_OPCODE_MASK		0x00000700
#define RESOURCE_CMD_RSP_OPCODE_SHIFT		8
#define RESOURCE_OPCODE_GNT			1
#define RESOURCE_OPCODE_BUSY			2
#define RESOURCE_OPCODE_RELEASED		3
#define RESOURCE_OPCODE_RELEASED_PREVIOUS	4
#define RESOURCE_OPCODE_WRONG_OWNER		5
#define RESOURCE_OPCODE_UNKNOWN_CMD		255

#define RESOURCE_DUMP				0

/* DRV_MSG_CODE_MDUMP_CMD parameters */
#define MDUMP_DRV_PARAM_OPCODE_MASK             0x000000ff
#define DRV_MSG_CODE_MDUMP_ACK                  0x01
#define DRV_MSG_CODE_MDUMP_SET_VALUES           0x02
#define DRV_MSG_CODE_MDUMP_TRIGGER              0x03
#define DRV_MSG_CODE_MDUMP_GET_CONFIG           0x04
#define DRV_MSG_CODE_MDUMP_SET_ENABLE           0x05
#define DRV_MSG_CODE_MDUMP_CLEAR_LOGS           0x06
#define DRV_MSG_CODE_MDUMP_GET_RETAIN           0x07
#define DRV_MSG_CODE_MDUMP_CLR_RETAIN           0x08

#define DRV_MSG_CODE_HW_DUMP_TRIGGER            0x0a

#define DRV_MSG_CODE_MDUMP_FREE_DRIVER_BUF 0x0b
#define DRV_MSG_CODE_MDUMP_GEN_LINK_DUMP 0x0c
#define DRV_MSG_CODE_MDUMP_GEN_IDLE_CHK 0x0d

/* DRV_MSG_CODE_MDUMP_CMD options */
#define MDUMP_DRV_PARAM_OPTION_MASK 0x00000f00
#define DRV_MSG_CODE_MDUMP_USE_DRIVER_BUF_OFFSET 8
#define DRV_MSG_CODE_MDUMP_USE_DRIVER_BUF_MASK 0x100

/* DRV_MSG_CODE_EXT_PHY_READ/DRV_MSG_CODE_EXT_PHY_WRITE parameters */
#define DRV_MB_PARAM_ADDR_SHIFT 0
#define DRV_MB_PARAM_ADDR_MASK 0x0000FFFF
#define DRV_MB_PARAM_DEVAD_SHIFT 16
#define DRV_MB_PARAM_DEVAD_MASK 0x001F0000
#define DRV_MB_PARAM_PORT_SHIFT 21
#define DRV_MB_PARAM_PORT_MASK 0x00600000

/* DRV_MSG_CODE_PMBUS_READ/DRV_MSG_CODE_PMBUS_WRITE parameters */
#define DRV_MB_PARAM_PMBUS_CMD_SHIFT 0
#define DRV_MB_PARAM_PMBUS_CMD_MASK 0xFF
#define DRV_MB_PARAM_PMBUS_LEN_SHIFT 8
#define DRV_MB_PARAM_PMBUS_LEN_MASK 0x300
#define DRV_MB_PARAM_PMBUS_DATA_SHIFT 16
#define DRV_MB_PARAM_PMBUS_DATA_MASK 0xFFFF0000

/* UNLOAD_REQ params */
#define DRV_MB_PARAM_UNLOAD_WOL_UNKNOWN 0x00000000
#define DRV_MB_PARAM_UNLOAD_WOL_MCP 0x00000001
#define DRV_MB_PARAM_UNLOAD_WOL_DISABLED 0x00000002
#define DRV_MB_PARAM_UNLOAD_WOL_ENABLED 0x00000003

/* UNLOAD_DONE_params */
#define DRV_MB_PARAM_UNLOAD_NON_D3_POWER 0x00000001

/* INIT_PHY params */
#define DRV_MB_PARAM_INIT_PHY_FORCE 0x00000001
#define DRV_MB_PARAM_INIT_PHY_DONT_CARE 0x00000002

/* LLDP / DCBX params*/
#define DRV_MB_PARAM_LLDP_SEND_MASK 0x00000001
#define DRV_MB_PARAM_LLDP_SEND_SHIFT 0
#define DRV_MB_PARAM_LLDP_AGENT_MASK 0x00000006
#define DRV_MB_PARAM_LLDP_AGENT_SHIFT 1
#define DRV_MB_PARAM_LLDP_TLV_RX_VALID_MASK 0x00000001
#define DRV_MB_PARAM_LLDP_TLV_RX_VALID_SHIFT 0
#define DRV_MB_PARAM_LLDP_TLV_RX_TYPE_MASK 0x000007f0
#define DRV_MB_PARAM_LLDP_TLV_RX_TYPE_SHIFT 4
#define DRV_MB_PARAM_DCBX_NOTIFY_MASK 0x00000008
#define DRV_MB_PARAM_DCBX_NOTIFY_SHIFT 3
#define DRV_MB_PARAM_DCBX_ADMIN_CFG_NOTIFY_MASK 0x00000010
#define DRV_MB_PARAM_DCBX_ADMIN_CFG_NOTIFY_SHIFT 4

#define DRV_MB_PARAM_NIG_DRAIN_PERIOD_MS_MASK 0x000000FF
#define DRV_MB_PARAM_NIG_DRAIN_PERIOD_MS_SHIFT 0

#define DRV_MB_PARAM_NVM_PUT_FILE_TYPE_MASK 0x000000ff
#define DRV_MB_PARAM_NVM_PUT_FILE_TYPE_SHIFT 0
#define DRV_MB_PARAM_NVM_PUT_FILE_BEGIN_MFW 0x1
#define DRV_MB_PARAM_NVM_PUT_FILE_BEGIN_IMAGE 0x2

#define DRV_MB_PARAM_NVM_PUT_FILE_BEGIN_MBI     0x3
#define DRV_MB_PARAM_NVM_OFFSET_OFFSET          0
#define DRV_MB_PARAM_NVM_OFFSET_MASK            0x00FFFFFF
#define DRV_MB_PARAM_NVM_LEN_OFFSET		24
#define DRV_MB_PARAM_NVM_LEN_MASK               0xFF000000

#define DRV_MB_PARAM_CFG_VF_MSIX_VF_ID_SHIFT	0
#define DRV_MB_PARAM_CFG_VF_MSIX_VF_ID_MASK	0x000000FF
#define DRV_MB_PARAM_CFG_VF_MSIX_SB_NUM_SHIFT	8
#define DRV_MB_PARAM_CFG_VF_MSIX_SB_NUM_MASK	0x0000FF00

#define DRV_MB_PARAM_OV_CURR_CFG_SHIFT		0
#define DRV_MB_PARAM_OV_CURR_CFG_MASK		0x0000000F
#define DRV_MB_PARAM_OV_CURR_CFG_NONE		0
#define DRV_MB_PARAM_OV_CURR_CFG_OS		1
#define DRV_MB_PARAM_OV_CURR_CFG_VENDOR_SPEC	2
#define DRV_MB_PARAM_OV_CURR_CFG_OTHER		3

#define DRV_MB_PARAM_OV_STORM_FW_VER_SHIFT	0
#define DRV_MB_PARAM_OV_STORM_FW_VER_MASK	0xFFFFFFFF
#define DRV_MB_PARAM_OV_STORM_FW_VER_MAJOR_MASK	0xFF000000
#define DRV_MB_PARAM_OV_STORM_FW_VER_MINOR_MASK	0x00FF0000
#define DRV_MB_PARAM_OV_STORM_FW_VER_BUILD_MASK	0x0000FF00
#define DRV_MB_PARAM_OV_STORM_FW_VER_DROP_MASK	0x000000FF

#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_SHIFT	0
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_MASK	0xF
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_UNKNOWN	0x1
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_NOT_LOADED	0x2
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_LOADING	0x3
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_DISABLED	0x4
#define DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_ACTIVE	0x5

#define DRV_MB_PARAM_OV_MTU_SIZE_SHIFT	0
#define DRV_MB_PARAM_OV_MTU_SIZE_MASK	0xFFFFFFFF

#define DRV_MB_PARAM_WOL_MASK	(DRV_MB_PARAM_WOL_DEFAULT | \
				 DRV_MB_PARAM_WOL_DISABLED | \
				 DRV_MB_PARAM_WOL_ENABLED)
#define DRV_MB_PARAM_WOL_DEFAULT	DRV_MB_PARAM_UNLOAD_WOL_MCP
#define DRV_MB_PARAM_WOL_DISABLED	DRV_MB_PARAM_UNLOAD_WOL_DISABLED
#define DRV_MB_PARAM_WOL_ENABLED	DRV_MB_PARAM_UNLOAD_WOL_ENABLED

#define DRV_MB_PARAM_ESWITCH_MODE_MASK	(DRV_MB_PARAM_ESWITCH_MODE_NONE | \
					 DRV_MB_PARAM_ESWITCH_MODE_VEB | \
					 DRV_MB_PARAM_ESWITCH_MODE_VEPA)
#define DRV_MB_PARAM_ESWITCH_MODE_NONE	0x0
#define DRV_MB_PARAM_ESWITCH_MODE_VEB	0x1
#define DRV_MB_PARAM_ESWITCH_MODE_VEPA	0x2

#define DRV_MB_PARAM_DUMMY_OEM_UPDATES_MASK	0x1
#define DRV_MB_PARAM_DUMMY_OEM_UPDATES_OFFSET	0

#define DRV_MB_PARAM_SET_LED_MODE_OPER		0x0
#define DRV_MB_PARAM_SET_LED_MODE_ON		0x1
#define DRV_MB_PARAM_SET_LED_MODE_OFF		0x2

#define DRV_MB_PARAM_TRANSCEIVER_PORT_OFFSET			0
#define DRV_MB_PARAM_TRANSCEIVER_PORT_MASK			0x00000003
#define DRV_MB_PARAM_TRANSCEIVER_SIZE_OFFSET			2
#define DRV_MB_PARAM_TRANSCEIVER_SIZE_MASK			0x000000fc
#define DRV_MB_PARAM_TRANSCEIVER_I2C_ADDRESS_OFFSET		8
#define DRV_MB_PARAM_TRANSCEIVER_I2C_ADDRESS_MASK		0x0000ff00
#define DRV_MB_PARAM_TRANSCEIVER_OFFSET_OFFSET			16
#define DRV_MB_PARAM_TRANSCEIVER_OFFSET_MASK			0xffff0000

	/* Resource Allocation params - Driver version support */
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_MASK		0xffff0000
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_SHIFT		16
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_MASK		0x0000ffff
#define DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_SHIFT		0

#define DRV_MB_PARAM_BIST_UNKNOWN_TEST				0
#define DRV_MB_PARAM_BIST_REGISTER_TEST				1
#define DRV_MB_PARAM_BIST_CLOCK_TEST				2
#define DRV_MB_PARAM_BIST_NVM_TEST_NUM_IMAGES			3
#define DRV_MB_PARAM_BIST_NVM_TEST_IMAGE_BY_INDEX		4

#define DRV_MB_PARAM_BIST_RC_UNKNOWN				0
#define DRV_MB_PARAM_BIST_RC_PASSED				1
#define DRV_MB_PARAM_BIST_RC_FAILED				2
#define DRV_MB_PARAM_BIST_RC_INVALID_PARAMETER			3

#define DRV_MB_PARAM_BIST_TEST_INDEX_SHIFT			0
#define DRV_MB_PARAM_BIST_TEST_INDEX_MASK			0x000000ff
#define DRV_MB_PARAM_BIST_TEST_IMAGE_INDEX_SHIFT		8
#define DRV_MB_PARAM_BIST_TEST_IMAGE_INDEX_MASK			0x0000ff00

#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_MASK			0x0000ffff
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_OFFSET		0
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_SMARTLINQ		0x00000001
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_EEE			0x00000002
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_FEC_CONTROL		0x00000004
#define DRV_MB_PARAM_FEATURE_SUPPORT_PORT_EXT_SPEED_FEC_CONTROL	0x00000008
#define DRV_MB_PARAM_FEATURE_SUPPORT_FUNC_VLINK			0x00010000

/* DRV_MSG_CODE_DEBUG_DATA_SEND parameters */
#define DRV_MSG_CODE_DEBUG_DATA_SEND_SIZE_OFFSET		0
#define DRV_MSG_CODE_DEBUG_DATA_SEND_SIZE_MASK			0xff

/* Driver attributes params */
#define DRV_MB_PARAM_ATTRIBUTE_KEY_OFFSET			0
#define DRV_MB_PARAM_ATTRIBUTE_KEY_MASK				0x00ffffff
#define DRV_MB_PARAM_ATTRIBUTE_CMD_OFFSET			24
#define DRV_MB_PARAM_ATTRIBUTE_CMD_MASK				0xff000000

#define DRV_MB_PARAM_NVM_CFG_OPTION_ID_OFFSET			0
#define DRV_MB_PARAM_NVM_CFG_OPTION_ID_MASK			0x0000ffff
#define DRV_MB_PARAM_NVM_CFG_OPTION_ID_IGNORE			0x0000ffff
#define DRV_MB_PARAM_NVM_CFG_OPTION_ID_SHIFT			0
#define DRV_MB_PARAM_NVM_CFG_OPTION_ALL_SHIFT			16
#define DRV_MB_PARAM_NVM_CFG_OPTION_ALL_MASK			0x00010000
#define DRV_MB_PARAM_NVM_CFG_OPTION_INIT_SHIFT			17
#define DRV_MB_PARAM_NVM_CFG_OPTION_INIT_MASK			0x00020000
#define DRV_MB_PARAM_NVM_CFG_OPTION_COMMIT_SHIFT		18
#define DRV_MB_PARAM_NVM_CFG_OPTION_COMMIT_MASK			0x00040000
#define DRV_MB_PARAM_NVM_CFG_OPTION_FREE_SHIFT			19
#define DRV_MB_PARAM_NVM_CFG_OPTION_FREE_MASK			0x00080000
#define DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_SEL_SHIFT		20
#define DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_SEL_MASK		0x00100000
#define DRV_MB_PARAM_NVM_CFG_OPTION_DEFAULT_RESTORE_ALL_SHIFT	21
#define DRV_MB_PARAM_NVM_CFG_OPTION_DEFAULT_RESTORE_ALL_MASK 0x00200000
#define DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_ID_SHIFT		24
#define DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_ID_MASK		0x0f000000

/*DRV_MSG_CODE_GET_PERM_MAC parametres*/
#define DRV_MSG_CODE_GET_PERM_MAC_TYPE_SHIFT		0
#define DRV_MSG_CODE_GET_PERM_MAC_TYPE_MASK		0xF
#define DRV_MSG_CODE_GET_PERM_MAC_TYPE_PF		0
#define DRV_MSG_CODE_GET_PERM_MAC_TYPE_BMC		1
#define DRV_MSG_CODE_GET_PERM_MAC_TYPE_VF		2
#define DRV_MSG_CODE_GET_PERM_MAC_TYPE_LLDP		3
#define DRV_MSG_CODE_GET_PERM_MAC_TYPE_MAX		4
#define DRV_MSG_CODE_GET_PERM_MAC_INDEX_SHIFT		8
#define DRV_MSG_CODE_GET_PERM_MAC_INDEX_MASK		0xFFFF00

#define FW_MSG_CODE(_code_)    ((_code_) << FW_MSG_CODE_OFFSET)
enum fw_msg_code_enum {
	FW_MSG_CODE_UNSUPPORTED = FW_MSG_CODE(0x0000),
	FW_MSG_CODE_NVM_OK = FW_MSG_CODE(0x0001),
	FW_MSG_CODE_NVM_PUT_FILE_FINISH_OK = FW_MSG_CODE(0x0040),
	FW_MSG_CODE_PHY_OK = FW_MSG_CODE(0x0011),
	FW_MSG_CODE_OK = FW_MSG_CODE(0x0016),
	FW_MSG_CODE_ERROR = FW_MSG_CODE(0x0017),
	FW_MSG_CODE_TRANSCEIVER_DIAG_OK = FW_MSG_CODE(0x0016),
	FW_MSG_CODE_TRANSCEIVER_NOT_PRESENT = FW_MSG_CODE(0x0002),
	FW_MSG_CODE_MDUMP_INVALID_CMD = FW_MSG_CODE(0x0003),
	FW_MSG_CODE_OS_WOL_SUPPORTED = FW_MSG_CODE(0x0080),
	FW_MSG_CODE_DRV_CFG_PF_VFS_MSIX_DONE = FW_MSG_CODE(0x0087),
	FW_MSG_CODE_DRV_LOAD_ENGINE = FW_MSG_CODE(0x1010),
	FW_MSG_CODE_DRV_LOAD_PORT = FW_MSG_CODE(0x1011),
	FW_MSG_CODE_DRV_LOAD_FUNCTION = FW_MSG_CODE(0x1012),
	FW_MSG_CODE_DRV_LOAD_REFUSED_PDA = FW_MSG_CODE(0x1020),
	FW_MSG_CODE_DRV_LOAD_REFUSED_HSI_1 = FW_MSG_CODE(0x1021),
	FW_MSG_CODE_DRV_LOAD_REFUSED_DIAG = FW_MSG_CODE(0x1022),
	FW_MSG_CODE_DRV_LOAD_REFUSED_HSI = FW_MSG_CODE(0x1023),
	FW_MSG_CODE_DRV_LOAD_REFUSED_REQUIRES_FORCE = FW_MSG_CODE(0x1030),
	FW_MSG_CODE_DRV_LOAD_REFUSED_REJECT = FW_MSG_CODE(0x1031),
	FW_MSG_CODE_DRV_LOAD_DONE = FW_MSG_CODE(0x1110),
	FW_MSG_CODE_DRV_UNLOAD_ENGINE = FW_MSG_CODE(0x2011),
	FW_MSG_CODE_DRV_UNLOAD_PORT = FW_MSG_CODE(0x2012),
	FW_MSG_CODE_DRV_UNLOAD_FUNCTION = FW_MSG_CODE(0x2013),
	FW_MSG_CODE_DRV_UNLOAD_DONE = FW_MSG_CODE(0x2110),
	FW_MSG_CODE_RESOURCE_ALLOC_OK = FW_MSG_CODE(0x3400),
	FW_MSG_CODE_RESOURCE_ALLOC_UNKNOWN = FW_MSG_CODE(0x3500),
	FW_MSG_CODE_S_TAG_UPDATE_ACK_DONE = FW_MSG_CODE(0x3b00),
	FW_MSG_CODE_DRV_CFG_VF_MSIX_DONE = FW_MSG_CODE(0xb001),
	FW_MSG_CODE_DEBUG_NOT_ENABLED = FW_MSG_CODE(0xb00a),
	FW_MSG_CODE_DEBUG_DATA_SEND_OK = FW_MSG_CODE(0xb00b),
};

#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_MASK		0xffff0000
#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_SHIFT		16
#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_MASK		0x0000ffff
#define FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_SHIFT		0

/* Get PF RDMA protocol command response */
#define FW_MB_PARAM_GET_PF_RDMA_NONE				0x0
#define FW_MB_PARAM_GET_PF_RDMA_ROCE				0x1
#define FW_MB_PARAM_GET_PF_RDMA_IWARP				0x2
#define FW_MB_PARAM_GET_PF_RDMA_BOTH				0x3

/* Get MFW feature support response */
#define FW_MB_PARAM_FEATURE_SUPPORT_SMARTLINQ			BIT(0)
#define FW_MB_PARAM_FEATURE_SUPPORT_EEE				BIT(1)
#define FW_MB_PARAM_FEATURE_SUPPORT_DRV_LOAD_TO			BIT(2)
#define FW_MB_PARAM_FEATURE_SUPPORT_LP_PRES_DET			BIT(3)
#define FW_MB_PARAM_FEATURE_SUPPORT_RELAXED_ORD			BIT(4)
#define FW_MB_PARAM_FEATURE_SUPPORT_FEC_CONTROL			BIT(5)
#define FW_MB_PARAM_FEATURE_SUPPORT_EXT_SPEED_FEC_CONTROL	BIT(6)
#define FW_MB_PARAM_FEATURE_SUPPORT_IGU_CLEANUP			BIT(7)
#define FW_MB_PARAM_FEATURE_SUPPORT_VF_DPM			BIT(8)
#define FW_MB_PARAM_FEATURE_SUPPORT_IDLE_CHK			BIT(9)
#define FW_MB_PARAM_FEATURE_SUPPORT_VLINK			BIT(16)
#define FW_MB_PARAM_FEATURE_SUPPORT_DISABLE_LLDP		BIT(17)
#define FW_MB_PARAM_FEATURE_SUPPORT_ENHANCED_SYS_LCK		BIT(18)
#define FW_MB_PARAM_FEATURE_SUPPORT_RESTORE_DEFAULT_CFG		BIT(19)

#define FW_MB_PARAM_MANAGEMENT_STATUS_LOCKDOWN_ENABLED		0x00000001

#define FW_MB_PARAM_LOAD_DONE_DID_EFUSE_ERROR			BIT(0)

#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALID_MASK		0x00000001
#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALID_SHIFT		0
#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALUE_MASK		0x00000002
#define FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALUE_SHIFT		1
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALID_MASK			0x00000004
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALID_SHIFT		2
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALUE_MASK			0x00000008
#define FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALUE_SHIFT		3

#define FW_MB_PARAM_PPFID_BITMAP_MASK				0xff
#define FW_MB_PARAM_PPFID_BITMAP_SHIFT				0

#define FW_MB_PARAM_NVM_PUT_FILE_REQ_OFFSET_MASK		0x00ffffff
#define FW_MB_PARAM_NVM_PUT_FILE_REQ_OFFSET_SHIFT		0
#define FW_MB_PARAM_NVM_PUT_FILE_REQ_SIZE_MASK			0xff000000
#define FW_MB_PARAM_NVM_PUT_FILE_REQ_SIZE_SHIFT			24

enum MFW_DRV_MSG_TYPE {
	MFW_DRV_MSG_LINK_CHANGE,
	MFW_DRV_MSG_FLR_FW_ACK_FAILED,
	MFW_DRV_MSG_VF_DISABLED,
	MFW_DRV_MSG_LLDP_DATA_UPDATED,
	MFW_DRV_MSG_DCBX_REMOTE_MIB_UPDATED,
	MFW_DRV_MSG_DCBX_OPERATIONAL_MIB_UPDATED,
	MFW_DRV_MSG_ERROR_RECOVERY,
	MFW_DRV_MSG_BW_UPDATE,
	MFW_DRV_MSG_S_TAG_UPDATE,
	MFW_DRV_MSG_GET_LAN_STATS,
	MFW_DRV_MSG_GET_FCOE_STATS,
	MFW_DRV_MSG_GET_ISCSI_STATS,
	MFW_DRV_MSG_GET_RDMA_STATS,
	MFW_DRV_MSG_FAILURE_DETECTED,
	MFW_DRV_MSG_TRANSCEIVER_STATE_CHANGE,
	MFW_DRV_MSG_CRITICAL_ERROR_OCCURRED,
	MFW_DRV_MSG_EEE_NEGOTIATION_COMPLETE,
	MFW_DRV_MSG_GET_TLV_REQ,
	MFW_DRV_MSG_OEM_CFG_UPDATE,
	MFW_DRV_MSG_LLDP_RECEIVED_TLVS_UPDATED,
	MFW_DRV_MSG_GENERIC_IDC,
	MFW_DRV_MSG_XCVR_TX_FAULT,
	MFW_DRV_MSG_XCVR_RX_LOS,
	MFW_DRV_MSG_GET_FCOE_CAP,
	MFW_DRV_MSG_GEN_LINK_DUMP,
	MFW_DRV_MSG_GEN_IDLE_CHK,
	MFW_DRV_MSG_DCBX_ADMIN_CFG_APPLIED,
	MFW_DRV_MSG_MAX
};

#define MFW_DRV_MSG_MAX_DWORDS(msgs)	((((msgs) - 1) >> 2) + 1)
#define MFW_DRV_MSG_DWORD(msg_id)	((msg_id) >> 2)
#define MFW_DRV_MSG_OFFSET(msg_id)	(((msg_id) & 0x3) << 3)
#define MFW_DRV_MSG_MASK(msg_id)	(0xff << MFW_DRV_MSG_OFFSET(msg_id))

struct public_mfw_mb {
	u32 sup_msgs;
	u32 msg[MFW_DRV_MSG_MAX_DWORDS(MFW_DRV_MSG_MAX)];
	u32 ack[MFW_DRV_MSG_MAX_DWORDS(MFW_DRV_MSG_MAX)];
};

enum public_sections {
	PUBLIC_DRV_MB,
	PUBLIC_MFW_MB,
	PUBLIC_GLOBAL,
	PUBLIC_PATH,
	PUBLIC_PORT,
	PUBLIC_FUNC,
	PUBLIC_MAX_SECTIONS
};

struct drv_ver_info_stc {
	u32 ver;
	u8 name[32];
};

/* Runtime data needs about 1/2K. We use 2K to be on the safe side.
 * Please make sure data does not exceed this size.
 */
#define NUM_RUNTIME_DWORDS    16
struct drv_init_hw_stc {
	u32 init_hw_bitmask[NUM_RUNTIME_DWORDS];
	u32 init_hw_data[NUM_RUNTIME_DWORDS * 32];
};

struct mcp_public_data {
	u32 num_sections;
	u32 sections[PUBLIC_MAX_SECTIONS];
	struct public_drv_mb drv_mb[MCP_GLOB_FUNC_MAX];
	struct public_mfw_mb mfw_mb[MCP_GLOB_FUNC_MAX];
	struct public_global global;
	struct public_path path[MCP_GLOB_PATH_MAX];
	struct public_port port[MCP_GLOB_PORT_MAX];
	struct public_func func[MCP_GLOB_FUNC_MAX];
};

#define I2C_TRANSCEIVER_ADDR		0xa0
#define MAX_I2C_TRANSACTION_SIZE	16
#define MAX_I2C_TRANSCEIVER_PAGE_SIZE	256

/* OCBB definitions */
enum tlvs {
	/* Category 1: Device Properties */
	DRV_TLV_CLP_STR,
	DRV_TLV_CLP_STR_CTD,
	/* Category 6: Device Configuration */
	DRV_TLV_SCSI_TO,
	DRV_TLV_R_T_TOV,
	DRV_TLV_R_A_TOV,
	DRV_TLV_E_D_TOV,
	DRV_TLV_CR_TOV,
	DRV_TLV_BOOT_TYPE,
	/* Category 8: Port Configuration */
	DRV_TLV_NPIV_ENABLED,
	/* Category 10: Function Configuration */
	DRV_TLV_FEATURE_FLAGS,
	DRV_TLV_LOCAL_ADMIN_ADDR,
	DRV_TLV_ADDITIONAL_MAC_ADDR_1,
	DRV_TLV_ADDITIONAL_MAC_ADDR_2,
	DRV_TLV_LSO_MAX_OFFLOAD_SIZE,
	DRV_TLV_LSO_MIN_SEGMENT_COUNT,
	DRV_TLV_PROMISCUOUS_MODE,
	DRV_TLV_TX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_RX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_NUM_OF_NET_QUEUE_VMQ_CFG,
	DRV_TLV_FLEX_NIC_OUTER_VLAN_ID,
	DRV_TLV_OS_DRIVER_STATES,
	DRV_TLV_PXE_BOOT_PROGRESS,
	/* Category 12: FC/FCoE Configuration */
	DRV_TLV_NPIV_STATE,
	DRV_TLV_NUM_OF_NPIV_IDS,
	DRV_TLV_SWITCH_NAME,
	DRV_TLV_SWITCH_PORT_NUM,
	DRV_TLV_SWITCH_PORT_ID,
	DRV_TLV_VENDOR_NAME,
	DRV_TLV_SWITCH_MODEL,
	DRV_TLV_SWITCH_FW_VER,
	DRV_TLV_QOS_PRIORITY_PER_802_1P,
	DRV_TLV_PORT_ALIAS,
	DRV_TLV_PORT_STATE,
	DRV_TLV_FIP_TX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_FCOE_RX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_LINK_FAILURE_COUNT,
	DRV_TLV_FCOE_BOOT_PROGRESS,
	/* Category 13: iSCSI Configuration */
	DRV_TLV_TARGET_LLMNR_ENABLED,
	DRV_TLV_HEADER_DIGEST_FLAG_ENABLED,
	DRV_TLV_DATA_DIGEST_FLAG_ENABLED,
	DRV_TLV_AUTHENTICATION_METHOD,
	DRV_TLV_ISCSI_BOOT_TARGET_PORTAL,
	DRV_TLV_MAX_FRAME_SIZE,
	DRV_TLV_PDU_TX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_PDU_RX_DESCRIPTORS_QUEUE_SIZE,
	DRV_TLV_ISCSI_BOOT_PROGRESS,
	/* Category 20: Device Data */
	DRV_TLV_PCIE_BUS_RX_UTILIZATION,
	DRV_TLV_PCIE_BUS_TX_UTILIZATION,
	DRV_TLV_DEVICE_CPU_CORES_UTILIZATION,
	DRV_TLV_LAST_VALID_DCC_TLV_RECEIVED,
	DRV_TLV_NCSI_RX_BYTES_RECEIVED,
	DRV_TLV_NCSI_TX_BYTES_SENT,
	/* Category 22: Base Port Data */
	DRV_TLV_RX_DISCARDS,
	DRV_TLV_RX_ERRORS,
	DRV_TLV_TX_ERRORS,
	DRV_TLV_TX_DISCARDS,
	DRV_TLV_RX_FRAMES_RECEIVED,
	DRV_TLV_TX_FRAMES_SENT,
	/* Category 23: FC/FCoE Port Data */
	DRV_TLV_RX_BROADCAST_PACKETS,
	DRV_TLV_TX_BROADCAST_PACKETS,
	/* Category 28: Base Function Data */
	DRV_TLV_NUM_OFFLOADED_CONNECTIONS_TCP_IPV4,
	DRV_TLV_NUM_OFFLOADED_CONNECTIONS_TCP_IPV6,
	DRV_TLV_TX_DESCRIPTOR_QUEUE_AVG_DEPTH,
	DRV_TLV_RX_DESCRIPTORS_QUEUE_AVG_DEPTH,
	DRV_TLV_PF_RX_FRAMES_RECEIVED,
	DRV_TLV_RX_BYTES_RECEIVED,
	DRV_TLV_PF_TX_FRAMES_SENT,
	DRV_TLV_TX_BYTES_SENT,
	DRV_TLV_IOV_OFFLOAD,
	DRV_TLV_PCI_ERRORS_CAP_ID,
	DRV_TLV_UNCORRECTABLE_ERROR_STATUS,
	DRV_TLV_UNCORRECTABLE_ERROR_MASK,
	DRV_TLV_CORRECTABLE_ERROR_STATUS,
	DRV_TLV_CORRECTABLE_ERROR_MASK,
	DRV_TLV_PCI_ERRORS_AECC_REGISTER,
	DRV_TLV_TX_QUEUES_EMPTY,
	DRV_TLV_RX_QUEUES_EMPTY,
	DRV_TLV_TX_QUEUES_FULL,
	DRV_TLV_RX_QUEUES_FULL,
	/* Category 29: FC/FCoE Function Data */
	DRV_TLV_FCOE_TX_DESCRIPTOR_QUEUE_AVG_DEPTH,
	DRV_TLV_FCOE_RX_DESCRIPTORS_QUEUE_AVG_DEPTH,
	DRV_TLV_FCOE_RX_FRAMES_RECEIVED,
	DRV_TLV_FCOE_RX_BYTES_RECEIVED,
	DRV_TLV_FCOE_TX_FRAMES_SENT,
	DRV_TLV_FCOE_TX_BYTES_SENT,
	DRV_TLV_CRC_ERROR_COUNT,
	DRV_TLV_CRC_ERROR_1_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_1_TIMESTAMP,
	DRV_TLV_CRC_ERROR_2_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_2_TIMESTAMP,
	DRV_TLV_CRC_ERROR_3_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_3_TIMESTAMP,
	DRV_TLV_CRC_ERROR_4_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_4_TIMESTAMP,
	DRV_TLV_CRC_ERROR_5_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_CRC_ERROR_5_TIMESTAMP,
	DRV_TLV_LOSS_OF_SYNC_ERROR_COUNT,
	DRV_TLV_LOSS_OF_SIGNAL_ERRORS,
	DRV_TLV_PRIMITIVE_SEQUENCE_PROTOCOL_ERROR_COUNT,
	DRV_TLV_DISPARITY_ERROR_COUNT,
	DRV_TLV_CODE_VIOLATION_ERROR_COUNT,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_1,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_2,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_3,
	DRV_TLV_LAST_FLOGI_ISSUED_COMMON_PARAMETERS_WORD_4,
	DRV_TLV_LAST_FLOGI_TIMESTAMP,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_1,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_2,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_3,
	DRV_TLV_LAST_FLOGI_ACC_COMMON_PARAMETERS_WORD_4,
	DRV_TLV_LAST_FLOGI_ACC_TIMESTAMP,
	DRV_TLV_LAST_FLOGI_RJT,
	DRV_TLV_LAST_FLOGI_RJT_TIMESTAMP,
	DRV_TLV_FDISCS_SENT_COUNT,
	DRV_TLV_FDISC_ACCS_RECEIVED,
	DRV_TLV_FDISC_RJTS_RECEIVED,
	DRV_TLV_PLOGI_SENT_COUNT,
	DRV_TLV_PLOGI_ACCS_RECEIVED,
	DRV_TLV_PLOGI_RJTS_RECEIVED,
	DRV_TLV_PLOGI_1_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_1_TIMESTAMP,
	DRV_TLV_PLOGI_2_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_2_TIMESTAMP,
	DRV_TLV_PLOGI_3_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_3_TIMESTAMP,
	DRV_TLV_PLOGI_4_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_4_TIMESTAMP,
	DRV_TLV_PLOGI_5_SENT_DESTINATION_FC_ID,
	DRV_TLV_PLOGI_5_TIMESTAMP,
	DRV_TLV_PLOGI_1_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_1_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_2_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_2_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_3_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_3_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_4_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_4_ACC_TIMESTAMP,
	DRV_TLV_PLOGI_5_ACC_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_PLOGI_5_ACC_TIMESTAMP,
	DRV_TLV_LOGOS_ISSUED,
	DRV_TLV_LOGO_ACCS_RECEIVED,
	DRV_TLV_LOGO_RJTS_RECEIVED,
	DRV_TLV_LOGO_1_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_1_TIMESTAMP,
	DRV_TLV_LOGO_2_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_2_TIMESTAMP,
	DRV_TLV_LOGO_3_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_3_TIMESTAMP,
	DRV_TLV_LOGO_4_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_4_TIMESTAMP,
	DRV_TLV_LOGO_5_RECEIVED_SOURCE_FC_ID,
	DRV_TLV_LOGO_5_TIMESTAMP,
	DRV_TLV_LOGOS_RECEIVED,
	DRV_TLV_ACCS_ISSUED,
	DRV_TLV_PRLIS_ISSUED,
	DRV_TLV_ACCS_RECEIVED,
	DRV_TLV_ABTS_SENT_COUNT,
	DRV_TLV_ABTS_ACCS_RECEIVED,
	DRV_TLV_ABTS_RJTS_RECEIVED,
	DRV_TLV_ABTS_1_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_1_TIMESTAMP,
	DRV_TLV_ABTS_2_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_2_TIMESTAMP,
	DRV_TLV_ABTS_3_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_3_TIMESTAMP,
	DRV_TLV_ABTS_4_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_4_TIMESTAMP,
	DRV_TLV_ABTS_5_SENT_DESTINATION_FC_ID,
	DRV_TLV_ABTS_5_TIMESTAMP,
	DRV_TLV_RSCNS_RECEIVED,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_1,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_2,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_3,
	DRV_TLV_LAST_RSCN_RECEIVED_N_PORT_4,
	DRV_TLV_LUN_RESETS_ISSUED,
	DRV_TLV_ABORT_TASK_SETS_ISSUED,
	DRV_TLV_TPRLOS_SENT,
	DRV_TLV_NOS_SENT_COUNT,
	DRV_TLV_NOS_RECEIVED_COUNT,
	DRV_TLV_OLS_COUNT,
	DRV_TLV_LR_COUNT,
	DRV_TLV_LRR_COUNT,
	DRV_TLV_LIP_SENT_COUNT,
	DRV_TLV_LIP_RECEIVED_COUNT,
	DRV_TLV_EOFA_COUNT,
	DRV_TLV_EOFNI_COUNT,
	DRV_TLV_SCSI_STATUS_CHECK_CONDITION_COUNT,
	DRV_TLV_SCSI_STATUS_CONDITION_MET_COUNT,
	DRV_TLV_SCSI_STATUS_BUSY_COUNT,
	DRV_TLV_SCSI_STATUS_INTERMEDIATE_COUNT,
	DRV_TLV_SCSI_STATUS_INTERMEDIATE_CONDITION_MET_COUNT,
	DRV_TLV_SCSI_STATUS_RESERVATION_CONFLICT_COUNT,
	DRV_TLV_SCSI_STATUS_TASK_SET_FULL_COUNT,
	DRV_TLV_SCSI_STATUS_ACA_ACTIVE_COUNT,
	DRV_TLV_SCSI_STATUS_TASK_ABORTED_COUNT,
	DRV_TLV_SCSI_CHECK_CONDITION_1_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_1_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_2_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_2_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_3_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_3_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_4_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_4_TIMESTAMP,
	DRV_TLV_SCSI_CHECK_CONDITION_5_RECEIVED_SK_ASC_ASCQ,
	DRV_TLV_SCSI_CHECK_5_TIMESTAMP,
	/* Category 30: iSCSI Function Data */
	DRV_TLV_PDU_TX_DESCRIPTOR_QUEUE_AVG_DEPTH,
	DRV_TLV_PDU_RX_DESCRIPTORS_QUEUE_AVG_DEPTH,
	DRV_TLV_ISCSI_PDU_RX_FRAMES_RECEIVED,
	DRV_TLV_ISCSI_PDU_RX_BYTES_RECEIVED,
	DRV_TLV_ISCSI_PDU_TX_FRAMES_SENT,
	DRV_TLV_ISCSI_PDU_TX_BYTES_SENT,
	DRV_TLV_RDMA_DRV_VERSION
};

#define I2C_DEV_ADDR_A2				0xa2
#define SFP_EEPROM_A2_TEMPERATURE_ADDR		0x60
#define SFP_EEPROM_A2_TEMPERATURE_SIZE		2
#define SFP_EEPROM_A2_VCC_ADDR			0x62
#define SFP_EEPROM_A2_VCC_SIZE			2
#define SFP_EEPROM_A2_TX_BIAS_ADDR		0x64
#define SFP_EEPROM_A2_TX_BIAS_SIZE		2
#define SFP_EEPROM_A2_TX_POWER_ADDR		0x66
#define SFP_EEPROM_A2_TX_POWER_SIZE		2
#define SFP_EEPROM_A2_RX_POWER_ADDR		0x68
#define SFP_EEPROM_A2_RX_POWER_SIZE		2

#define I2C_DEV_ADDR_A0				0xa0
#define QSFP_EEPROM_A0_TEMPERATURE_ADDR		0x16
#define QSFP_EEPROM_A0_TEMPERATURE_SIZE		2
#define QSFP_EEPROM_A0_VCC_ADDR			0x1a
#define QSFP_EEPROM_A0_VCC_SIZE			2
#define QSFP_EEPROM_A0_TX1_BIAS_ADDR		0x2a
#define QSFP_EEPROM_A0_TX1_BIAS_SIZE		2
#define QSFP_EEPROM_A0_TX1_POWER_ADDR		0x32
#define QSFP_EEPROM_A0_TX1_POWER_SIZE		2
#define QSFP_EEPROM_A0_RX1_POWER_ADDR		0x22
#define QSFP_EEPROM_A0_RX1_POWER_SIZE		2

struct nvm_cfg_mac_address {
	u32 mac_addr_hi;
#define NVM_CFG_MAC_ADDRESS_HI_MASK 0x0000ffff
#define NVM_CFG_MAC_ADDRESS_HI_OFFSET 0

	u32 mac_addr_lo;
};

struct nvm_cfg1_glob {
	u32 generic_cont0;
#define NVM_CFG1_GLOB_MF_MODE_MASK 0x00000ff0
#define NVM_CFG1_GLOB_MF_MODE_OFFSET 4
#define NVM_CFG1_GLOB_MF_MODE_MF_ALLOWED 0x0
#define NVM_CFG1_GLOB_MF_MODE_DEFAULT 0x1
#define NVM_CFG1_GLOB_MF_MODE_SPIO4 0x2
#define NVM_CFG1_GLOB_MF_MODE_NPAR1_0 0x3
#define NVM_CFG1_GLOB_MF_MODE_NPAR1_5 0x4
#define NVM_CFG1_GLOB_MF_MODE_NPAR2_0 0x5
#define NVM_CFG1_GLOB_MF_MODE_BD 0x6
#define NVM_CFG1_GLOB_MF_MODE_UFP 0x7

	u32 engineering_change[3];
	u32 manufacturing_id;
	u32 serial_number[4];
	u32 pcie_cfg;
	u32 mgmt_traffic;

	u32 core_cfg;
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_MASK 0x000000ff
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_OFFSET 0
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_2X40G 0x0
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X50G 0x1
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_1X100G 0x2
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_4X10G_F 0x3
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X10G_E 0x4
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X20G 0x5
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X40G 0xb
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X25G 0xc
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X25G 0xd
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_4X25G 0xe
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X10G 0xf
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_AHP_2X50G_R1 0x11
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_AHP_4X50G_R1 0x12
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_AHP_1X100G_R2 0x13
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_AHP_2X100G_R2 0x14
#define NVM_CFG1_GLOB_NETWORK_PORT_MODE_AHP_1X100G_R4 0x15

	u32 e_lane_cfg1;
	u32 e_lane_cfg2;
	u32 f_lane_cfg1;
	u32 f_lane_cfg2;
	u32 mps10_preemphasis;
	u32 mps10_driver_current;
	u32 mps25_preemphasis;
	u32 mps25_driver_current;
	u32 pci_id;
	u32 pci_subsys_id;
	u32 bar;
	u32 mps10_txfir_main;
	u32 mps10_txfir_post;
	u32 mps25_txfir_main;
	u32 mps25_txfir_post;
	u32 manufacture_ver;
	u32 manufacture_time;
	u32 led_global_settings;
	u32 generic_cont1;

	u32 mbi_version;
#define NVM_CFG1_GLOB_MBI_VERSION_0_MASK 0x000000ff
#define NVM_CFG1_GLOB_MBI_VERSION_0_OFFSET 0
#define NVM_CFG1_GLOB_MBI_VERSION_1_MASK 0x0000ff00
#define NVM_CFG1_GLOB_MBI_VERSION_1_OFFSET 8
#define NVM_CFG1_GLOB_MBI_VERSION_2_MASK 0x00ff0000
#define NVM_CFG1_GLOB_MBI_VERSION_2_OFFSET 16

	u32 mbi_date;
	u32 misc_sig;

	u32 device_capabilities;
#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ETHERNET 0x1
#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_FCOE 0x2
#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ISCSI 0x4
#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ROCE 0x8
#define NVM_CFG1_GLOB_DEVICE_CAPABILITIES_IWARP 0x10

	u32 power_dissipated;
	u32 power_consumed;
	u32 efi_version;
	u32 multi_network_modes_capability;
	u32 nvm_cfg_version;
	u32 nvm_cfg_new_option_seq;
	u32 nvm_cfg_removed_option_seq;
	u32 nvm_cfg_updated_value_seq;
	u32 extended_serial_number[8];
	u32 option_kit_pn[8];
	u32 spare_pn[8];
	u32 mps25_active_txfir_pre;
	u32 mps25_active_txfir_main;
	u32 mps25_active_txfir_post;
	u32 features;
	u32 tx_rx_eq_25g_hlpc;
	u32 tx_rx_eq_25g_llpc;
	u32 tx_rx_eq_25g_ac;
	u32 tx_rx_eq_10g_pc;
	u32 tx_rx_eq_10g_ac;
	u32 tx_rx_eq_1g;
	u32 tx_rx_eq_25g_bt;
	u32 tx_rx_eq_10g_bt;
	u32 generic_cont4;
	u32 preboot_debug_mode_std;
	u32 preboot_debug_mode_ext;
	u32 ext_phy_cfg1;
	u32 clocks;
	u32 pre2_generic_cont_1;
	u32 pre2_generic_cont_2;
	u32 pre2_generic_cont_3;
	u32 tx_rx_eq_50g_hlpc;
	u32 tx_rx_eq_50g_mlpc;
	u32 tx_rx_eq_50g_llpc;
	u32 tx_rx_eq_50g_ac;
	u32 trace_modules;
	u32 pcie_class_code_fcoe;
	u32 pcie_class_code_iscsi;
	u32 no_provisioned_mac;
	u32 lowest_mbi_version;
	u32 generic_cont5;
	u32 pre2_generic_cont_4;
	u32 reserved[40];
};

struct nvm_cfg1_path {
	u32 reserved[1];
};

struct nvm_cfg1_port {
	u32 rel_to_opt123;
	u32 rel_to_opt124;

	u32 generic_cont0;
#define NVM_CFG1_PORT_DCBX_MODE_MASK 0x000f0000
#define NVM_CFG1_PORT_DCBX_MODE_OFFSET 16
#define NVM_CFG1_PORT_DCBX_MODE_DISABLED 0x0
#define NVM_CFG1_PORT_DCBX_MODE_IEEE 0x1
#define NVM_CFG1_PORT_DCBX_MODE_CEE 0x2
#define NVM_CFG1_PORT_DCBX_MODE_DYNAMIC 0x3
#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_MASK 0x00f00000
#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_OFFSET 20
#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_ETHERNET 0x1
#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_FCOE 0x2
#define NVM_CFG1_PORT_DEFAULT_ENABLED_PROTOCOLS_ISCSI 0x4

	u32 pcie_cfg;
	u32 features;

	u32 speed_cap_mask;
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_MASK 0x0000ffff
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_OFFSET 0
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G 0x1
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G 0x2
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_20G 0x4
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G 0x8
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G 0x10
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G 0x20
#define NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G 0x40

	u32 link_settings;
#define NVM_CFG1_PORT_DRV_LINK_SPEED_MASK 0x0000000f
#define NVM_CFG1_PORT_DRV_LINK_SPEED_OFFSET 0
#define NVM_CFG1_PORT_DRV_LINK_SPEED_AUTONEG 0x0
#define NVM_CFG1_PORT_DRV_LINK_SPEED_1G 0x1
#define NVM_CFG1_PORT_DRV_LINK_SPEED_10G 0x2
#define NVM_CFG1_PORT_DRV_LINK_SPEED_20G 0x3
#define NVM_CFG1_PORT_DRV_LINK_SPEED_25G 0x4
#define NVM_CFG1_PORT_DRV_LINK_SPEED_40G 0x5
#define NVM_CFG1_PORT_DRV_LINK_SPEED_50G 0x6
#define NVM_CFG1_PORT_DRV_LINK_SPEED_BB_100G 0x7
#define NVM_CFG1_PORT_DRV_LINK_SPEED_SMARTLINQ 0x8
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_MASK 0x00000070
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_OFFSET 4
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_AUTONEG 0x1
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_RX 0x2
#define NVM_CFG1_PORT_DRV_FLOW_CONTROL_TX 0x4
#define NVM_CFG1_PORT_FEC_FORCE_MODE_MASK 0x000e0000
#define NVM_CFG1_PORT_FEC_FORCE_MODE_OFFSET 17
#define NVM_CFG1_PORT_FEC_FORCE_MODE_NONE 0x0
#define NVM_CFG1_PORT_FEC_FORCE_MODE_FIRECODE 0x1
#define NVM_CFG1_PORT_FEC_FORCE_MODE_RS 0x2
#define NVM_CFG1_PORT_FEC_FORCE_MODE_AUTO 0x7

	u32 phy_cfg;
	u32 mgmt_traffic;

	u32 ext_phy;
	/* EEE power saving mode */
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_MASK 0x00ff0000
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_OFFSET 16
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_DISABLED 0x0
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_BALANCED 0x1
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_AGGRESSIVE 0x2
#define NVM_CFG1_PORT_EEE_POWER_SAVING_MODE_LOW_LATENCY 0x3

	u32 mba_cfg1;
	u32 mba_cfg2;
	u32							vf_cfg;
	struct nvm_cfg_mac_address lldp_mac_address;
	u32 led_port_settings;
	u32 transceiver_00;
	u32 device_ids;

	u32 board_cfg;
#define NVM_CFG1_PORT_PORT_TYPE_MASK 0x000000ff
#define NVM_CFG1_PORT_PORT_TYPE_OFFSET 0
#define NVM_CFG1_PORT_PORT_TYPE_UNDEFINED 0x0
#define NVM_CFG1_PORT_PORT_TYPE_MODULE 0x1
#define NVM_CFG1_PORT_PORT_TYPE_BACKPLANE 0x2
#define NVM_CFG1_PORT_PORT_TYPE_EXT_PHY 0x3
#define NVM_CFG1_PORT_PORT_TYPE_MODULE_SLAVE 0x4

	u32 mnm_10g_cap;
	u32 mnm_10g_ctrl;
	u32 mnm_10g_misc;
	u32 mnm_25g_cap;
	u32 mnm_25g_ctrl;
	u32 mnm_25g_misc;
	u32 mnm_40g_cap;
	u32 mnm_40g_ctrl;
	u32 mnm_40g_misc;
	u32 mnm_50g_cap;
	u32 mnm_50g_ctrl;
	u32 mnm_50g_misc;
	u32 mnm_100g_cap;
	u32 mnm_100g_ctrl;
	u32 mnm_100g_misc;

	u32 temperature;
	u32 ext_phy_cfg1;

	u32 extended_speed;
#define NVM_CFG1_PORT_EXTENDED_SPEED_MASK 0x0000ffff
#define NVM_CFG1_PORT_EXTENDED_SPEED_OFFSET 0
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_AN 0x1
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_1G 0x2
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_10G 0x4
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_20G 0x8
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_25G 0x10
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_40G 0x20
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_50G_R 0x40
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_50G_R2 0x80
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_100G_R2 0x100
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_100G_R4 0x200
#define NVM_CFG1_PORT_EXTENDED_SPEED_EXTND_SPD_100G_P4 0x400
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_MASK 0xffff0000
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_OFFSET 16
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_RESERVED 0x1
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_1G 0x2
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_10G 0x4
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_20G 0x8
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_25G 0x10
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_40G 0x20
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_50G_R 0x40
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_50G_R2 0x80
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_100G_R2 0x100
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_100G_R4 0x200
#define NVM_CFG1_PORT_EXTENDED_SPEED_CAP_EXTND_SPD_100G_P4 0x400

	u32 extended_fec_mode;
	u32 port_generic_cont_01;
	u32 port_generic_cont_02;
	u32 phy_temp_monitor;
	u32 reserved[109];
};

struct nvm_cfg1_func {
	struct nvm_cfg_mac_address mac_address;
	u32 rsrv1;
	u32 rsrv2;
	u32 device_id;
	u32 cmn_cfg;
	u32 pci_cfg;
	struct nvm_cfg_mac_address fcoe_node_wwn_mac_addr;
	struct nvm_cfg_mac_address fcoe_port_wwn_mac_addr;
	u32 preboot_generic_cfg;
	u32 features;
	u32 mf_mode_feature;
	u32 reserved[6];
};

struct nvm_cfg1 {
	struct nvm_cfg1_glob glob;
	struct nvm_cfg1_path path[MCP_GLOB_PATH_MAX];
	struct nvm_cfg1_port port[MCP_GLOB_PORT_MAX];
	struct nvm_cfg1_func func[MCP_GLOB_FUNC_MAX];
};

struct board_info {
	u16 vendor_id;
	u16 eth_did_suffix;
	u16 sub_vendor_id;
	u16 sub_device_id;
	char *board_name;
	char *friendly_name;
};

struct trace_module_info {
	char *module_name;
};

#define NUM_TRACE_MODULES    25

enum nvm_cfg_sections {
	NVM_CFG_SECTION_NVM_CFG1,
	NVM_CFG_SECTION_MAX
};

struct nvm_cfg {
	u32 num_sections;
	u32 sections_offset[NVM_CFG_SECTION_MAX];
	struct nvm_cfg1 cfg1;
};

#define PORT_0		0
#define PORT_1		1
#define PORT_2		2
#define PORT_3		3

extern struct spad_layout g_spad;
struct spad_layout {
	struct nvm_cfg nvm_cfg;
	struct mcp_public_data public_data;
};

#define MCP_SPAD_SIZE    0x00028000	/* 160 KB */

#define SPAD_OFFSET(addr)    (((u32)(addr) - (u32)CPU_SPAD_BASE))

#define TO_OFFSIZE(_offset, _size)                               \
		((u32)((((u32)(_offset) >> 2) << OFFSIZE_OFFSET_OFFSET) | \
		 (((u32)(_size) >> 2) << OFFSIZE_SIZE_OFFSET)))

enum spad_sections {
	SPAD_SECTION_TRACE,
	SPAD_SECTION_NVM_CFG,
	SPAD_SECTION_PUBLIC,
	SPAD_SECTION_PRIVATE,
	SPAD_SECTION_MAX
};

#define STRUCT_OFFSET(f)    (STATIC_INIT_BASE + \
			     __builtin_offsetof(struct static_init, f))

/* This section is located at a fixed location in the beginning of the
 * scratchpad, to ensure that the MCP trace is not run over during MFW upgrade.
 * All the rest of data has a floating location which differs from version to
 * version, and is pointed by the mcp_meta_data below.
 * Moreover, the spad_layout section is part of the MFW firmware, and is loaded
 * with it from nvram in order to clear this portion.
 */
struct static_init {
	u32 num_sections;
	offsize_t sections[SPAD_SECTION_MAX];
#define SECTION(_sec_) (*((offsize_t *)(STRUCT_OFFSET(sections[_sec_]))))

	u32 tim_hash[8];
#define PRESERVED_TIM_HASH	((u8 *)(STRUCT_OFFSET(tim_hash)))
	u32 tpu_hash[8];
#define PRESERVED_TPU_HASH	((u8 *)(STRUCT_OFFSET(tpu_hash)))
	u32 secure_pcie_fw_ver;
#define SECURE_PCIE_FW_VER	(*((u32 *)(STRUCT_OFFSET(secure_pcie_fw_ver))))
	u32 secure_running_mfw;
#define SECURE_RUNNING_MFW	(*((u32 *)(STRUCT_OFFSET(secure_running_mfw))))
	struct mcp_trace trace;
};

#define CRC_MAGIC_VALUE		0xDEBB20E3
#define CRC32_POLYNOMIAL	0xEDB88320
#define _KB(x)			((x) * 1024)
#define _MB(x)			(_KB(x) * 1024)
#define NVM_CRC_SIZE		(sizeof(u32))
enum nvm_sw_arbitrator {
	NVM_SW_ARB_HOST,
	NVM_SW_ARB_MCP,
	NVM_SW_ARB_UART,
	NVM_SW_ARB_RESERVED
};

struct legacy_bootstrap_region {
	u32 magic_value;
#define NVM_MAGIC_VALUE    0x669955aa
	u32 sram_start_addr;
	u32 code_len;
	u32 code_start_addr;
	u32 crc;
};

struct nvm_code_entry {
	u32 image_type;
	u32 nvm_start_addr;
	u32 len;
	u32 sram_start_addr;
	u32 sram_run_addr;
};

enum nvm_image_type {
	NVM_TYPE_TIM1 = 0x01,
	NVM_TYPE_TIM2 = 0x02,
	NVM_TYPE_MIM1 = 0x03,
	NVM_TYPE_MIM2 = 0x04,
	NVM_TYPE_MBA = 0x05,
	NVM_TYPE_MODULES_PN = 0x06,
	NVM_TYPE_VPD = 0x07,
	NVM_TYPE_MFW_TRACE1 = 0x08,
	NVM_TYPE_MFW_TRACE2 = 0x09,
	NVM_TYPE_NVM_CFG1 = 0x0a,
	NVM_TYPE_L2B = 0x0b,
	NVM_TYPE_DIR1 = 0x0c,
	NVM_TYPE_EAGLE_FW1 = 0x0d,
	NVM_TYPE_FALCON_FW1 = 0x0e,
	NVM_TYPE_PCIE_FW1 = 0x0f,
	NVM_TYPE_HW_SET = 0x10,
	NVM_TYPE_LIM = 0x11,
	NVM_TYPE_AVS_FW1 = 0x12,
	NVM_TYPE_DIR2 = 0x13,
	NVM_TYPE_CCM = 0x14,
	NVM_TYPE_EAGLE_FW2 = 0x15,
	NVM_TYPE_FALCON_FW2 = 0x16,
	NVM_TYPE_PCIE_FW2 = 0x17,
	NVM_TYPE_AVS_FW2 = 0x18,
	NVM_TYPE_INIT_HW = 0x19,
	NVM_TYPE_DEFAULT_CFG = 0x1a,
	NVM_TYPE_MDUMP = 0x1b,
	NVM_TYPE_NVM_META = 0x1c,
	NVM_TYPE_ISCSI_CFG = 0x1d,
	NVM_TYPE_FCOE_CFG = 0x1f,
	NVM_TYPE_ETH_PHY_FW1 = 0x20,
	NVM_TYPE_ETH_PHY_FW2 = 0x21,
	NVM_TYPE_BDN = 0x22,
	NVM_TYPE_8485X_PHY_FW = 0x23,
	NVM_TYPE_PUB_KEY = 0x24,
	NVM_TYPE_RECOVERY = 0x25,
	NVM_TYPE_PLDM = 0x26,
	NVM_TYPE_UPK1 = 0x27,
	NVM_TYPE_UPK2 = 0x28,
	NVM_TYPE_MASTER_KC = 0x29,
	NVM_TYPE_BACKUP_KC = 0x2a,
	NVM_TYPE_HW_DUMP = 0x2b,
	NVM_TYPE_HW_DUMP_OUT = 0x2c,
	NVM_TYPE_BIN_NVM_META = 0x30,
	NVM_TYPE_ROM_TEST = 0xf0,
	NVM_TYPE_88X33X0_PHY_FW = 0x31,
	NVM_TYPE_88X33X0_PHY_SLAVE_FW = 0x32,
	NVM_TYPE_IDLE_CHK = 0x33,
	NVM_TYPE_MAX,
};

#define MAX_NVM_DIR_ENTRIES 100

struct nvm_dir_meta {
	u32 dir_id;
	u32 nvm_dir_addr;
	u32 num_images;
	u32 next_mfw_to_run;
};

struct nvm_dir {
	s32 seq;
#define NVM_DIR_NEXT_MFW_MASK 0x00000001
#define NVM_DIR_SEQ_MASK 0xfffffffe
#define NVM_DIR_NEXT_MFW(seq) ((seq) & NVM_DIR_NEXT_MFW_MASK)
#define NVM_DIR_UPDATE_SEQ(_seq, swap_mfw)\
	({ \
		_seq =  (((_seq + 2) & \
			 NVM_DIR_SEQ_MASK) | \
			 (NVM_DIR_NEXT_MFW(_seq ^ (swap_mfw))));\
	})

#define IS_DIR_SEQ_VALID(seq) (((seq) & NVM_DIR_SEQ_MASK) != \
			       NVM_DIR_SEQ_MASK)

	u32 num_images;
	u32 rsrv;
	struct nvm_code_entry code[1];	/* Up to MAX_NVM_DIR_ENTRIES */
};

#define NVM_DIR_SIZE(_num_images) (sizeof(struct nvm_dir) + \
				   ((_num_images) - 1) *\
				   sizeof(struct nvm_code_entry) +\
				   NVM_CRC_SIZE)

struct nvm_vpd_image {
	u32 format_revision;
#define VPD_IMAGE_VERSION 1

	u8 vpd_data[1];
};

#define DIR_ID_1    (0)
#define DIR_ID_2    (1)
#define MAX_DIR_IDS (2)

#define MFW_BUNDLE_1 (0)
#define MFW_BUNDLE_2 (1)
#define MAX_MFW_BUNDLES (2)

#define FLASH_PAGE_SIZE 0x1000
#define NVM_DIR_MAX_SIZE (FLASH_PAGE_SIZE)
#define LEGACY_ASIC_MIM_MAX_SIZE (_KB(1200))

#define FPGA_MIM_MAX_SIZE (0x40000)

#define LIM_MAX_SIZE ((2 * FLASH_PAGE_SIZE) - \
		      sizeof(struct legacy_bootstrap_region) \
		      - NVM_RSV_SIZE)
#define LIM_OFFSET (NVM_OFFSET(lim_image))
#define NVM_RSV_SIZE (44)
#define GET_MIM_MAX_SIZE(is_asic, is_e4) (LEGACY_ASIC_MIM_MAX_SIZE)
#define GET_MIM_OFFSET(idx, is_asic, is_e4) (NVM_OFFSET(dir[MAX_MFW_BUNDLES])\
					     + (((idx) == NVM_TYPE_MIM2) ? \
					     GET_MIM_MAX_SIZE(is_asic, is_e4)\
					     : 0))
#define GET_NVM_FIXED_AREA_SIZE(is_asic, is_e4)	(sizeof(struct nvm_image) + \
						 GET_MIM_MAX_SIZE(is_asic,\
						is_e4) * 2)

union nvm_dir_union {
	struct nvm_dir dir;
	u8 page[FLASH_PAGE_SIZE];
};

struct nvm_image {
	struct legacy_bootstrap_region bootstrap;
	u8 rsrv[NVM_RSV_SIZE];
	u8 lim_image[LIM_MAX_SIZE];
	union nvm_dir_union dir[MAX_MFW_BUNDLES];
};

#define NVM_OFFSET(f) ((u32_t)((int_ptr_t)(&(((struct nvm_image *)0)->(f)))))

struct hw_set_info {
	u32 reg_type;
#define GRC_REG_TYPE 1
#define PHY_REG_TYPE 2
#define PCI_REG_TYPE 4

	u32 bank_num;
	u32 pf_num;
	u32 operation;
#define READ_OP 1
#define WRITE_OP 2
#define RMW_SET_OP 3
#define RMW_CLR_OP 4

	u32 reg_addr;
	u32 reg_data;

	u32 reset_type;
#define POR_RESET_TYPE BIT(0)
#define HARD_RESET_TYPE BIT(1)
#define CORE_RESET_TYPE BIT(2)
#define MCP_RESET_TYPE BIT(3)
#define PERSET_ASSERT BIT(4)
#define PERSET_DEASSERT BIT(5)
};

struct hw_set_image {
	u32 format_version;
#define HW_SET_IMAGE_VERSION 1
	u32 no_hw_sets;
	struct hw_set_info hw_sets[1];
};

#define MAX_SUPPORTED_NVM_OPTIONS 1000

#define NVM_META_BIN_OPTION_OFFSET_MASK 0x0000ffff
#define NVM_META_BIN_OPTION_OFFSET_SHIFT 0
#define NVM_META_BIN_OPTION_LEN_MASK 0x00ff0000
#define NVM_META_BIN_OPTION_LEN_OFFSET 16
#define NVM_META_BIN_OPTION_ENTITY_MASK 0x03000000
#define NVM_META_BIN_OPTION_ENTITY_SHIFT 24
#define NVM_META_BIN_OPTION_ENTITY_GLOB 0
#define NVM_META_BIN_OPTION_ENTITY_PORT 1
#define NVM_META_BIN_OPTION_ENTITY_FUNC 2
#define NVM_META_BIN_OPTION_CONFIG_TYPE_MASK 0x0c000000
#define NVM_META_BIN_OPTION_CONFIG_TYPE_SHIFT 26
#define NVM_META_BIN_OPTION_CONFIG_TYPE_USER 0
#define NVM_META_BIN_OPTION_CONFIG_TYPE_FIXED 1
#define NVM_META_BIN_OPTION_CONFIG_TYPE_FORCED 2

struct nvm_meta_bin_t {
	u32 magic;
#define NVM_META_BIN_MAGIC 0x669955bb
	u32 version;
#define NVM_META_BIN_VERSION 1
	u32 num_options;
	u32 options[0];
};
#endif
