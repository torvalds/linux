/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#ifndef HINIC_PORT_H
#define HINIC_PORT_H

#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/bitops.h>

#include "hinic_dev.h"

#define HINIC_RSS_KEY_SIZE	40
#define HINIC_RSS_INDIR_SIZE	256
#define HINIC_PORT_STATS_VERSION	0
#define HINIC_FW_VERSION_NAME	16
#define HINIC_COMPILE_TIME_LEN	20
#define HINIC_MGMT_VERSION_MAX_LEN	32

struct hinic_version_info {
	u8 status;
	u8 version;
	u8 rsvd[6];

	u8 ver[HINIC_FW_VERSION_NAME];
	u8 time[HINIC_COMPILE_TIME_LEN];
};

enum hinic_rx_mode {
	HINIC_RX_MODE_UC        = BIT(0),
	HINIC_RX_MODE_MC        = BIT(1),
	HINIC_RX_MODE_BC        = BIT(2),
	HINIC_RX_MODE_MC_ALL    = BIT(3),
	HINIC_RX_MODE_PROMISC   = BIT(4),
};

enum hinic_port_link_state {
	HINIC_LINK_STATE_DOWN,
	HINIC_LINK_STATE_UP,
};

enum hinic_port_state {
	HINIC_PORT_DISABLE      = 0,
	HINIC_PORT_ENABLE       = 3,
};

enum hinic_func_port_state {
	HINIC_FUNC_PORT_DISABLE = 0,
	HINIC_FUNC_PORT_ENABLE  = 2,
};

enum hinic_autoneg_cap {
	HINIC_AUTONEG_UNSUPPORTED,
	HINIC_AUTONEG_SUPPORTED,
};

enum hinic_autoneg_state {
	HINIC_AUTONEG_DISABLED,
	HINIC_AUTONEG_ACTIVE,
};

enum hinic_duplex {
	HINIC_DUPLEX_HALF,
	HINIC_DUPLEX_FULL,
};

enum hinic_speed {
	HINIC_SPEED_10MB_LINK = 0,
	HINIC_SPEED_100MB_LINK,
	HINIC_SPEED_1000MB_LINK,
	HINIC_SPEED_10GB_LINK,
	HINIC_SPEED_25GB_LINK,
	HINIC_SPEED_40GB_LINK,
	HINIC_SPEED_100GB_LINK,

	HINIC_SPEED_UNKNOWN = 0xFF,
};

enum hinic_link_mode {
	HINIC_10GE_BASE_KR = 0,
	HINIC_40GE_BASE_KR4 = 1,
	HINIC_40GE_BASE_CR4 = 2,
	HINIC_100GE_BASE_KR4 = 3,
	HINIC_100GE_BASE_CR4 = 4,
	HINIC_25GE_BASE_KR_S = 5,
	HINIC_25GE_BASE_CR_S = 6,
	HINIC_25GE_BASE_KR = 7,
	HINIC_25GE_BASE_CR = 8,
	HINIC_GE_BASE_KX = 9,
	HINIC_LINK_MODE_NUMBERS,

	HINIC_SUPPORTED_UNKNOWN = 0xFFFF,
};

enum hinic_port_type {
	HINIC_PORT_TP,		/* BASET */
	HINIC_PORT_AUI,
	HINIC_PORT_MII,
	HINIC_PORT_FIBRE,	/* OPTICAL */
	HINIC_PORT_BNC,
	HINIC_PORT_ELEC,
	HINIC_PORT_COPPER,	/* PORT_DA */
	HINIC_PORT_AOC,
	HINIC_PORT_BACKPLANE,
	HINIC_PORT_NONE = 0xEF,
	HINIC_PORT_OTHER = 0xFF,
};

enum hinic_valid_link_settings {
	HILINK_LINK_SET_SPEED = 0x1,
	HILINK_LINK_SET_AUTONEG = 0x2,
	HILINK_LINK_SET_FEC = 0x4,
};

enum hinic_tso_state {
	HINIC_TSO_DISABLE = 0,
	HINIC_TSO_ENABLE  = 1,
};

struct hinic_port_mac_cmd {
	u8              status;
	u8              version;
	u8              rsvd0[6];

	u16             func_idx;
	u16             vlan_id;
	u16             rsvd1;
	unsigned char   mac[ETH_ALEN];
};

struct hinic_port_mtu_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     rsvd1;
	u32     mtu;
};

struct hinic_port_vlan_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     vlan_id;
};

struct hinic_port_rx_mode_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     rsvd;
	u32     rx_mode;
};

struct hinic_port_link_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u8      state;
	u8      rsvd1;
};

struct hinic_port_state_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u8      state;
	u8      rsvd1[3];
};

struct hinic_port_link_status {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_id;
	u8      link;
	u8      port_id;
};

struct hinic_cable_plug_event {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	plugged; /* 0: unplugged, 1: plugged */
	u8	port_id;
};

enum link_err_type {
	LINK_ERR_MODULE_UNRECOGENIZED,
	LINK_ERR_NUM,
};

struct hinic_link_err_event {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	err_type;
	u8	port_id;
};

struct hinic_port_func_state_cmd {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     rsvd1;
	u8      state;
	u8      rsvd2[3];
};

struct hinic_port_cap {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_idx;
	u16     rsvd1;
	u8      port_type;
	u8      autoneg_cap;
	u8      autoneg_state;
	u8      duplex;
	u8      speed;
	u8      rsvd2[3];
};

struct hinic_link_mode_cmd {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u16	supported;	/* 0xFFFF represents invalid value */
	u16	advertised;
};

struct hinic_speed_cmd {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	speed;
};

struct hinic_set_autoneg_cmd {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	enable;	/* 1: enable , 0: disable */
};

struct hinic_link_ksettings_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;

	u32	valid_bitmap;
	u32	speed;		/* enum nic_speed_level */
	u8	autoneg;	/* 0 - off; 1 - on */
	u8	fec;		/* 0 - RSFEC; 1 - BASEFEC; 2 - NOFEC */
	u8	rsvd2[18];	/* reserved for duplex, port, etc. */
};

struct hinic_tso_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u8	tso_en;
	u8	resv2[3];
};

struct hinic_checksum_offload {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	rx_csum_offload;
};

struct hinic_rq_num {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1[33];
	u32	num_rqs;
	u32	rq_depth;
};

struct hinic_lro_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u8	lro_ipv4_en;
	u8	lro_ipv6_en;
	u8	lro_max_wqe_num;
	u8	resv2[13];
};

struct hinic_lro_timer {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	type;   /* 0: set timer value, 1: get timer value */
	u8	enable; /* when set lro time, enable should be 1 */
	u16	rsvd1;
	u32	timer;
};

struct hinic_vlan_cfg {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_id;
	u8      vlan_rx_offload;
	u8      rsvd1[5];
};

struct hinic_rss_template_mgmt {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	cmd;
	u8	template_id;
	u8	rsvd1[4];
};

struct hinic_rss_template_key {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	template_id;
	u8	rsvd1;
	u8	key[HINIC_RSS_KEY_SIZE];
};

struct hinic_rss_context_tbl {
	u32 group_index;
	u32 offset;
	u32 size;
	u32 rsvd;
	u32 ctx;
};

struct hinic_rss_context_table {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_id;
	u8      template_id;
	u8      rsvd1;
	u32     context;
};

struct hinic_rss_indirect_tbl {
	u32 group_index;
	u32 offset;
	u32 size;
	u32 rsvd;
	u8 entry[HINIC_RSS_INDIR_SIZE];
};

struct hinic_rss_indir_table {
	u8      status;
	u8      version;
	u8      rsvd0[6];

	u16     func_id;
	u8      template_id;
	u8      rsvd1;
	u8      indir[HINIC_RSS_INDIR_SIZE];
};

struct hinic_rss_key {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	template_id;
	u8	rsvd1;
	u8	key[HINIC_RSS_KEY_SIZE];
};

struct hinic_rss_engine_type {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	template_id;
	u8	hash_engine;
	u8	rsvd1[4];
};

struct hinic_rss_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	rss_en;
	u8	template_id;
	u8	rq_priority_number;
	u8	rsvd1[11];
};

struct hinic_stats {
	char name[ETH_GSTRING_LEN];
	u32 size;
	int offset;
};

struct hinic_vport_stats {
	u64 tx_unicast_pkts_vport;
	u64 tx_unicast_bytes_vport;
	u64 tx_multicast_pkts_vport;
	u64 tx_multicast_bytes_vport;
	u64 tx_broadcast_pkts_vport;
	u64 tx_broadcast_bytes_vport;

	u64 rx_unicast_pkts_vport;
	u64 rx_unicast_bytes_vport;
	u64 rx_multicast_pkts_vport;
	u64 rx_multicast_bytes_vport;
	u64 rx_broadcast_pkts_vport;
	u64 rx_broadcast_bytes_vport;

	u64 tx_discard_vport;
	u64 rx_discard_vport;
	u64 tx_err_vport;
	u64 rx_err_vport;
};

struct hinic_phy_port_stats {
	u64 mac_rx_total_pkt_num;
	u64 mac_rx_total_oct_num;
	u64 mac_rx_bad_pkt_num;
	u64 mac_rx_bad_oct_num;
	u64 mac_rx_good_pkt_num;
	u64 mac_rx_good_oct_num;
	u64 mac_rx_uni_pkt_num;
	u64 mac_rx_multi_pkt_num;
	u64 mac_rx_broad_pkt_num;

	u64 mac_tx_total_pkt_num;
	u64 mac_tx_total_oct_num;
	u64 mac_tx_bad_pkt_num;
	u64 mac_tx_bad_oct_num;
	u64 mac_tx_good_pkt_num;
	u64 mac_tx_good_oct_num;
	u64 mac_tx_uni_pkt_num;
	u64 mac_tx_multi_pkt_num;
	u64 mac_tx_broad_pkt_num;

	u64 mac_rx_fragment_pkt_num;
	u64 mac_rx_undersize_pkt_num;
	u64 mac_rx_undermin_pkt_num;
	u64 mac_rx_64_oct_pkt_num;
	u64 mac_rx_65_127_oct_pkt_num;
	u64 mac_rx_128_255_oct_pkt_num;
	u64 mac_rx_256_511_oct_pkt_num;
	u64 mac_rx_512_1023_oct_pkt_num;
	u64 mac_rx_1024_1518_oct_pkt_num;
	u64 mac_rx_1519_2047_oct_pkt_num;
	u64 mac_rx_2048_4095_oct_pkt_num;
	u64 mac_rx_4096_8191_oct_pkt_num;
	u64 mac_rx_8192_9216_oct_pkt_num;
	u64 mac_rx_9217_12287_oct_pkt_num;
	u64 mac_rx_12288_16383_oct_pkt_num;
	u64 mac_rx_1519_max_bad_pkt_num;
	u64 mac_rx_1519_max_good_pkt_num;
	u64 mac_rx_oversize_pkt_num;
	u64 mac_rx_jabber_pkt_num;

	u64 mac_rx_pause_num;
	u64 mac_rx_pfc_pkt_num;
	u64 mac_rx_pfc_pri0_pkt_num;
	u64 mac_rx_pfc_pri1_pkt_num;
	u64 mac_rx_pfc_pri2_pkt_num;
	u64 mac_rx_pfc_pri3_pkt_num;
	u64 mac_rx_pfc_pri4_pkt_num;
	u64 mac_rx_pfc_pri5_pkt_num;
	u64 mac_rx_pfc_pri6_pkt_num;
	u64 mac_rx_pfc_pri7_pkt_num;
	u64 mac_rx_control_pkt_num;
	u64 mac_rx_y1731_pkt_num;
	u64 mac_rx_sym_err_pkt_num;
	u64 mac_rx_fcs_err_pkt_num;
	u64 mac_rx_send_app_good_pkt_num;
	u64 mac_rx_send_app_bad_pkt_num;

	u64 mac_tx_fragment_pkt_num;
	u64 mac_tx_undersize_pkt_num;
	u64 mac_tx_undermin_pkt_num;
	u64 mac_tx_64_oct_pkt_num;
	u64 mac_tx_65_127_oct_pkt_num;
	u64 mac_tx_128_255_oct_pkt_num;
	u64 mac_tx_256_511_oct_pkt_num;
	u64 mac_tx_512_1023_oct_pkt_num;
	u64 mac_tx_1024_1518_oct_pkt_num;
	u64 mac_tx_1519_2047_oct_pkt_num;
	u64 mac_tx_2048_4095_oct_pkt_num;
	u64 mac_tx_4096_8191_oct_pkt_num;
	u64 mac_tx_8192_9216_oct_pkt_num;
	u64 mac_tx_9217_12287_oct_pkt_num;
	u64 mac_tx_12288_16383_oct_pkt_num;
	u64 mac_tx_1519_max_bad_pkt_num;
	u64 mac_tx_1519_max_good_pkt_num;
	u64 mac_tx_oversize_pkt_num;
	u64 mac_tx_jabber_pkt_num;

	u64 mac_tx_pause_num;
	u64 mac_tx_pfc_pkt_num;
	u64 mac_tx_pfc_pri0_pkt_num;
	u64 mac_tx_pfc_pri1_pkt_num;
	u64 mac_tx_pfc_pri2_pkt_num;
	u64 mac_tx_pfc_pri3_pkt_num;
	u64 mac_tx_pfc_pri4_pkt_num;
	u64 mac_tx_pfc_pri5_pkt_num;
	u64 mac_tx_pfc_pri6_pkt_num;
	u64 mac_tx_pfc_pri7_pkt_num;
	u64 mac_tx_control_pkt_num;
	u64 mac_tx_y1731_pkt_num;
	u64 mac_tx_1588_pkt_num;
	u64 mac_tx_err_all_pkt_num;
	u64 mac_tx_from_app_good_pkt_num;
	u64 mac_tx_from_app_bad_pkt_num;

	u64 mac_rx_higig2_ext_pkt_num;
	u64 mac_rx_higig2_message_pkt_num;
	u64 mac_rx_higig2_error_pkt_num;
	u64 mac_rx_higig2_cpu_ctrl_pkt_num;
	u64 mac_rx_higig2_unicast_pkt_num;
	u64 mac_rx_higig2_broadcast_pkt_num;
	u64 mac_rx_higig2_l2_multicast_pkt_num;
	u64 mac_rx_higig2_l3_multicast_pkt_num;

	u64 mac_tx_higig2_message_pkt_num;
	u64 mac_tx_higig2_ext_pkt_num;
	u64 mac_tx_higig2_cpu_ctrl_pkt_num;
	u64 mac_tx_higig2_unicast_pkt_num;
	u64 mac_tx_higig2_broadcast_pkt_num;
	u64 mac_tx_higig2_l2_multicast_pkt_num;
	u64 mac_tx_higig2_l3_multicast_pkt_num;
};

struct hinic_port_stats_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	stats_version;
	u32	stats_size;
};

struct hinic_port_stats {
	u8 status;
	u8 version;
	u8 rsvd[6];

	struct hinic_phy_port_stats stats;
};

struct hinic_cmd_vport_stats {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	struct hinic_vport_stats stats;
};

struct hinic_tx_rate_cfg_max_min {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	min_rate;
	u32	max_rate;
	u8	rsvd2[8];
};

struct hinic_tx_rate_cfg {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	tx_rate;
};

enum nic_speed_level {
	LINK_SPEED_10MB = 0,
	LINK_SPEED_100MB,
	LINK_SPEED_1GB,
	LINK_SPEED_10GB,
	LINK_SPEED_25GB,
	LINK_SPEED_40GB,
	LINK_SPEED_100GB,
	LINK_SPEED_LEVELS,
};

struct hinic_spoofchk_set {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	state;
	u8	rsvd1;
	u16	func_id;
};

struct hinic_pause_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	auto_neg;
	u32	rx_pause;
	u32	tx_pause;
};

struct hinic_set_pfc {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	pfc_en;
	u8	pfc_bitmap;
	u8	rsvd1[4];
};

/* get or set loopback mode, need to modify by base API */
#define HINIC_INTERNAL_LP_MODE			5
#define LOOP_MODE_MIN				1
#define LOOP_MODE_MAX				6

struct hinic_port_loopback {
	u8	status;
	u8	version;
	u8	rsvd[6];

	u32	mode;
	u32	en;
};

struct hinic_led_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	port;
	u8	type;
	u8	mode;
	u8	reset;
};

#define STD_SFP_INFO_MAX_SIZE	640

struct hinic_cmd_get_light_module_abs {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u8 port_id;
	u8 abs_status; /* 0:present, 1:absent */
	u8 rsv[2];
};

#define STD_SFP_INFO_MAX_SIZE	640

struct hinic_cmd_get_std_sfp_info {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u8 port_id;
	u8 wire_type;
	u16 eeprom_len;
	u32 rsvd;
	u8 sfp_info[STD_SFP_INFO_MAX_SIZE];
};

struct hinic_cmd_update_fw {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	struct {
		u32 SL:1;
		u32 SF:1;
		u32 flag:1;
		u32 reserved:13;
		u32 fragment_len:16;
	} ctl_info;

	struct {
		u32 FW_section_CRC;
		u32 FW_section_type;
	} section_info;

	u32 total_len;
	u32 setion_total_len;
	u32 fw_section_version;
	u32 section_offset;
	u32 data[384];
};

int hinic_port_add_mac(struct hinic_dev *nic_dev, const u8 *addr,
		       u16 vlan_id);

int hinic_port_del_mac(struct hinic_dev *nic_dev, const u8 *addr,
		       u16 vlan_id);

int hinic_port_get_mac(struct hinic_dev *nic_dev, u8 *addr);

int hinic_port_set_mtu(struct hinic_dev *nic_dev, int new_mtu);

int hinic_port_add_vlan(struct hinic_dev *nic_dev, u16 vlan_id);

int hinic_port_del_vlan(struct hinic_dev *nic_dev, u16 vlan_id);

int hinic_port_set_rx_mode(struct hinic_dev *nic_dev, u32 rx_mode);

int hinic_port_link_state(struct hinic_dev *nic_dev,
			  enum hinic_port_link_state *link_state);

int hinic_port_set_state(struct hinic_dev *nic_dev,
			 enum hinic_port_state state);

int hinic_port_set_func_state(struct hinic_dev *nic_dev,
			      enum hinic_func_port_state state);

int hinic_port_get_cap(struct hinic_dev *nic_dev,
		       struct hinic_port_cap *port_cap);

int hinic_set_max_qnum(struct hinic_dev *nic_dev, u8 num_rqs);

int hinic_port_set_tso(struct hinic_dev *nic_dev, enum hinic_tso_state state);

int hinic_set_rx_csum_offload(struct hinic_dev *nic_dev, u32 en);

int hinic_set_rx_lro_state(struct hinic_dev *nic_dev, u8 lro_en,
			   u32 lro_timer, u32 wqe_num);

int hinic_set_rss_type(struct hinic_dev *nic_dev, u32 tmpl_idx,
		       struct hinic_rss_type rss_type);

int hinic_rss_set_indir_tbl(struct hinic_dev *nic_dev, u32 tmpl_idx,
			    const u32 *indir_table);

int hinic_rss_set_template_tbl(struct hinic_dev *nic_dev, u32 template_id,
			       const u8 *temp);

int hinic_rss_set_hash_engine(struct hinic_dev *nic_dev, u8 template_id,
			      u8 type);

int hinic_rss_cfg(struct hinic_dev *nic_dev, u8 rss_en, u8 template_id);

int hinic_rss_template_alloc(struct hinic_dev *nic_dev, u8 *tmpl_idx);

int hinic_rss_template_free(struct hinic_dev *nic_dev, u8 tmpl_idx);

void hinic_set_ethtool_ops(struct net_device *netdev);

int hinic_get_rss_type(struct hinic_dev *nic_dev, u32 tmpl_idx,
		       struct hinic_rss_type *rss_type);

int hinic_rss_get_indir_tbl(struct hinic_dev *nic_dev, u32 tmpl_idx,
			    u32 *indir_table);

int hinic_rss_get_template_tbl(struct hinic_dev *nic_dev, u32 tmpl_idx,
			       u8 *temp);

int hinic_rss_get_hash_engine(struct hinic_dev *nic_dev, u8 tmpl_idx,
			      u8 *type);

int hinic_get_phy_port_stats(struct hinic_dev *nic_dev,
			     struct hinic_phy_port_stats *stats);

int hinic_get_vport_stats(struct hinic_dev *nic_dev,
			  struct hinic_vport_stats *stats);

int hinic_set_rx_vlan_offload(struct hinic_dev *nic_dev, u8 en);

int hinic_get_mgmt_version(struct hinic_dev *nic_dev, u8 *mgmt_ver);

int hinic_set_link_settings(struct hinic_hwdev *hwdev,
			    struct hinic_link_ksettings_info *info);

int hinic_get_link_mode(struct hinic_hwdev *hwdev,
			struct hinic_link_mode_cmd *link_mode);

int hinic_set_autoneg(struct hinic_hwdev *hwdev, bool enable);

int hinic_set_speed(struct hinic_hwdev *hwdev, enum nic_speed_level speed);

int hinic_get_hw_pause_info(struct hinic_hwdev *hwdev,
			    struct hinic_pause_config *pause_info);

int hinic_set_hw_pause_info(struct hinic_hwdev *hwdev,
			    struct hinic_pause_config *pause_info);

int hinic_dcb_set_pfc(struct hinic_hwdev *hwdev, u8 pfc_en, u8 pfc_bitmap);

int hinic_set_loopback_mode(struct hinic_hwdev *hwdev, u32 mode, u32 enable);

enum hinic_led_mode {
	HINIC_LED_MODE_ON,
	HINIC_LED_MODE_OFF,
	HINIC_LED_MODE_FORCE_1HZ,
	HINIC_LED_MODE_FORCE_2HZ,
	HINIC_LED_MODE_FORCE_4HZ,
	HINIC_LED_MODE_1HZ,
	HINIC_LED_MODE_2HZ,
	HINIC_LED_MODE_4HZ,
	HINIC_LED_MODE_INVALID,
};

enum hinic_led_type {
	HINIC_LED_TYPE_LINK,
	HINIC_LED_TYPE_LOW_SPEED,
	HINIC_LED_TYPE_HIGH_SPEED,
	HINIC_LED_TYPE_INVALID,
};

int hinic_reset_led_status(struct hinic_hwdev *hwdev, u8 port);

int hinic_set_led_status(struct hinic_hwdev *hwdev, u8 port,
			 enum hinic_led_type type, enum hinic_led_mode mode);

int hinic_get_sfp_type(struct hinic_hwdev *hwdev, u8 *data0, u8 *data1);

int hinic_get_sfp_eeprom(struct hinic_hwdev *hwdev, u8 *data, u16 *len);

int hinic_open(struct net_device *netdev);

int hinic_close(struct net_device *netdev);

#endif
