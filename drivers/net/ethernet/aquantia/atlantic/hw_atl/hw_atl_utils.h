/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 */

/* File hw_atl_utils.h: Declaration of common functions for Atlantic hardware
 * abstraction layer.
 */

#ifndef HW_ATL_UTILS_H
#define HW_ATL_UTILS_H

#define HW_ATL_FLUSH() { (void)aq_hw_read_reg(self, 0x10); }

/* Hardware tx descriptor */
struct __packed hw_atl_txd_s {
	u64 buf_addr;
	u32 ctl;
	u32 ctl2; /* 63..46 - payload length, 45 - ctx enable, 44 - ctx index */
};

/* Hardware tx context descriptor */
struct __packed hw_atl_txc_s {
	u32 rsvd;
	u32 len;
	u32 ctl;
	u32 len2;
};

/* Hardware rx descriptor */
struct __packed hw_atl_rxd_s {
	u64 buf_addr;
	u64 hdr_addr;
};

/* Hardware rx descriptor writeback */
struct __packed hw_atl_rxd_wb_s {
	u32 type;
	u32 rss_hash;
	u16 status;
	u16 pkt_len;
	u16 next_desc_ptr;
	u16 vlan;
};

struct __packed hw_atl_stats_s {
	u32 uprc;
	u32 mprc;
	u32 bprc;
	u32 erpt;
	u32 uptc;
	u32 mptc;
	u32 bptc;
	u32 erpr;
	u32 mbtc;
	u32 bbtc;
	u32 mbrc;
	u32 bbrc;
	u32 ubrc;
	u32 ubtc;
	u32 dpc;
};

union __packed ip_addr {
	struct {
		u8 addr[16];
	} v6;
	struct {
		u8 padding[12];
		u8 addr[4];
	} v4;
};

struct __packed hw_atl_utils_fw_rpc {
	u32 msg_id;

	union {
		struct {
			u32 pong;
		} msg_ping;

		struct {
			u8 mac_addr[6];
			u32 ip_addr_cnt;

			struct {
				union ip_addr addr;
				union ip_addr mask;
			} ip[1];
		} msg_arp;

		struct {
			u32 len;
			u8 packet[1514U];
		} msg_inject;

		struct {
			u32 priority;
			u32 wol_packet_type;
			u32 pattern_id;
			u32 next_wol_pattern_offset;

			union {
				struct {
					u32 flags;
					u8 ipv4_source_address[4];
					u8 ipv4_dest_address[4];
					u16 tcp_source_port_number;
					u16 tcp_dest_port_number;
				} ipv4_tcp_syn_parameters;

				struct {
					u32 flags;
					u8 ipv6_source_address[16];
					u8 ipv6_dest_address[16];
					u16 tcp_source_port_number;
					u16 tcp_dest_port_number;
				} ipv6_tcp_syn_parameters;

				struct {
					u32 flags;
				} eapol_request_id_message_parameters;

				struct {
					u32 flags;
					u32 mask_offset;
					u32 mask_size;
					u32 pattern_offset;
					u32 pattern_size;
				} wol_bit_map_pattern;

				struct {
					u8 mac_addr[ETH_ALEN];
				} wol_magic_packet_patter;
			} wol_pattern;
		} msg_wol;

		struct {
			union {
				u32 pattern_mask;

				struct {
					u32 reason_arp_v4_pkt : 1;
					u32 reason_ipv4_ping_pkt : 1;
					u32 reason_ipv6_ns_pkt : 1;
					u32 reason_ipv6_ping_pkt : 1;
					u32 reason_link_up : 1;
					u32 reason_link_down : 1;
					u32 reason_maximum : 1;
				};
			};

			union {
				u32 offload_mask;
			};
		} msg_enable_wakeup;

		struct {
			u32 id;
		} msg_del_id;
	};
};

struct __packed hw_atl_utils_mbox_header {
	u32 version;
	u32 transaction_id;
	u32 error;
};

struct __packed hw_aq_info {
	u8 reserved[6];
	u16 phy_fault_code;
	u16 phy_temperature;
	u8 cable_len;
	u8 reserved1;
	u32 cable_diag_data[4];
	u8 reserved2[32];
	u32 caps_lo;
	u32 caps_hi;
};

struct __packed hw_atl_utils_mbox {
	struct hw_atl_utils_mbox_header header;
	struct hw_atl_stats_s stats;
	struct hw_aq_info info;
};

/* fw2x */
typedef u32	fw_offset_t;

struct __packed offload_ip_info {
	u8 v4_local_addr_count;
	u8 v4_addr_count;
	u8 v6_local_addr_count;
	u8 v6_addr_count;
	fw_offset_t v4_addr;
	fw_offset_t v4_prefix;
	fw_offset_t v6_addr;
	fw_offset_t v6_prefix;
};

struct __packed offload_port_info {
	u16 udp_port_count;
	u16 tcp_port_count;
	fw_offset_t udp_port;
	fw_offset_t tcp_port;
};

struct __packed offload_ka_info {
	u16 v4_ka_count;
	u16 v6_ka_count;
	u32 retry_count;
	u32 retry_interval;
	fw_offset_t v4_ka;
	fw_offset_t v6_ka;
};

struct __packed offload_rr_info {
	u32 rr_count;
	u32 rr_buf_len;
	fw_offset_t rr_id_x;
	fw_offset_t rr_buf;
};

struct __packed offload_info {
	u32 version;
	u32 len;
	u8 mac_addr[ETH_ALEN];

	u8 reserved[2];

	struct offload_ip_info ips;
	struct offload_port_info ports;
	struct offload_ka_info kas;
	struct offload_rr_info rrs;
	u8 buf[0];
};

enum hw_atl_rx_action_with_traffic {
	HW_ATL_RX_DISCARD,
	HW_ATL_RX_HOST,
};

struct aq_rx_filter_vlan {
	u8 enable;
	u8 location;
	u16 vlan_id;
	u8 queue;
};

struct aq_rx_filter_l2 {
	s8 queue;
	u8 location;
	u8 user_priority_en;
	u8 user_priority;
	u16 ethertype;
};

struct aq_rx_filter_l3l4 {
	u32 cmd;
	u8 location;
	u32 ip_dst[4];
	u32 ip_src[4];
	u16 p_dst;
	u16 p_src;
	u8 is_ipv6;
};

enum hw_atl_rx_protocol_value_l3l4 {
	HW_ATL_RX_TCP,
	HW_ATL_RX_UDP,
	HW_ATL_RX_SCTP,
	HW_ATL_RX_ICMP
};

enum hw_atl_rx_ctrl_registers_l3l4 {
	HW_ATL_RX_ENABLE_MNGMNT_QUEUE_L3L4 = BIT(22),
	HW_ATL_RX_ENABLE_QUEUE_L3L4        = BIT(23),
	HW_ATL_RX_ENABLE_ARP_FLTR_L3       = BIT(24),
	HW_ATL_RX_ENABLE_CMP_PROT_L4       = BIT(25),
	HW_ATL_RX_ENABLE_CMP_DEST_PORT_L4  = BIT(26),
	HW_ATL_RX_ENABLE_CMP_SRC_PORT_L4   = BIT(27),
	HW_ATL_RX_ENABLE_CMP_DEST_ADDR_L3  = BIT(28),
	HW_ATL_RX_ENABLE_CMP_SRC_ADDR_L3   = BIT(29),
	HW_ATL_RX_ENABLE_L3_IPV6           = BIT(30),
	HW_ATL_RX_ENABLE_FLTR_L3L4         = BIT(31)
};

#define HW_ATL_RX_QUEUE_FL3L4_SHIFT       8U
#define HW_ATL_RX_ACTION_FL3F4_SHIFT      16U

#define HW_ATL_RX_CNT_REG_ADDR_IPV6       4U

#define HW_ATL_GET_REG_LOCATION_FL3L4(location) \
	((location) - AQ_RX_FIRST_LOC_FL3L4)

#define HAL_ATLANTIC_UTILS_CHIP_MIPS         0x00000001U
#define HAL_ATLANTIC_UTILS_CHIP_TPO2         0x00000002U
#define HAL_ATLANTIC_UTILS_CHIP_RPF2         0x00000004U
#define HAL_ATLANTIC_UTILS_CHIP_MPI_AQ       0x00000010U
#define HAL_ATLANTIC_UTILS_CHIP_REVISION_A0  0x01000000U
#define HAL_ATLANTIC_UTILS_CHIP_REVISION_B0  0x02000000U
#define HAL_ATLANTIC_UTILS_CHIP_REVISION_B1  0x04000000U

#define IS_CHIP_FEATURE(_F_) (HAL_ATLANTIC_UTILS_CHIP_##_F_ & \
	self->chip_features)

enum hal_atl_utils_fw_state_e {
	MPI_DEINIT = 0,
	MPI_RESET = 1,
	MPI_INIT = 2,
	MPI_POWER = 4,
};

#define HAL_ATLANTIC_RATE_10G        BIT(0)
#define HAL_ATLANTIC_RATE_5G         BIT(1)
#define HAL_ATLANTIC_RATE_5GSR       BIT(2)
#define HAL_ATLANTIC_RATE_2GS        BIT(3)
#define HAL_ATLANTIC_RATE_1G         BIT(4)
#define HAL_ATLANTIC_RATE_100M       BIT(5)
#define HAL_ATLANTIC_RATE_INVALID    BIT(6)

#define HAL_ATLANTIC_UTILS_FW_MSG_PING          0x1U
#define HAL_ATLANTIC_UTILS_FW_MSG_ARP           0x2U
#define HAL_ATLANTIC_UTILS_FW_MSG_INJECT        0x3U
#define HAL_ATLANTIC_UTILS_FW_MSG_WOL_ADD       0x4U
#define HAL_ATLANTIC_UTILS_FW_MSG_WOL_PRIOR     0x10000000U
#define HAL_ATLANTIC_UTILS_FW_MSG_WOL_PATTERN   0x1U
#define HAL_ATLANTIC_UTILS_FW_MSG_WOL_MAG_PKT   0x2U
#define HAL_ATLANTIC_UTILS_FW_MSG_WOL_DEL       0x5U
#define HAL_ATLANTIC_UTILS_FW_MSG_ENABLE_WAKEUP 0x6U
#define HAL_ATLANTIC_UTILS_FW_MSG_MSM_PFC       0x7U
#define HAL_ATLANTIC_UTILS_FW_MSG_PROVISIONING  0x8U
#define HAL_ATLANTIC_UTILS_FW_MSG_OFFLOAD_ADD   0x9U
#define HAL_ATLANTIC_UTILS_FW_MSG_OFFLOAD_DEL   0xAU
#define HAL_ATLANTIC_UTILS_FW_MSG_CABLE_DIAG    0xDU

enum hw_atl_fw2x_rate {
	FW2X_RATE_100M    = 0x20,
	FW2X_RATE_1G      = 0x100,
	FW2X_RATE_2G5     = 0x200,
	FW2X_RATE_5G      = 0x400,
	FW2X_RATE_10G     = 0x800,
};

enum hw_atl_fw2x_caps_lo {
	CAPS_LO_10BASET_HD = 0x00,
	CAPS_LO_10BASET_FD,
	CAPS_LO_100BASETX_HD,
	CAPS_LO_100BASET4_HD,
	CAPS_LO_100BASET2_HD,
	CAPS_LO_100BASETX_FD,
	CAPS_LO_100BASET2_FD,
	CAPS_LO_1000BASET_HD,
	CAPS_LO_1000BASET_FD,
	CAPS_LO_2P5GBASET_FD,
	CAPS_LO_5GBASET_FD,
	CAPS_LO_10GBASET_FD,
};

enum hw_atl_fw2x_caps_hi {
	CAPS_HI_RESERVED1 = 0x00,
	CAPS_HI_10BASET_EEE,
	CAPS_HI_RESERVED2,
	CAPS_HI_PAUSE,
	CAPS_HI_ASYMMETRIC_PAUSE,
	CAPS_HI_100BASETX_EEE,
	CAPS_HI_RESERVED3,
	CAPS_HI_RESERVED4,
	CAPS_HI_1000BASET_FD_EEE,
	CAPS_HI_2P5GBASET_FD_EEE,
	CAPS_HI_5GBASET_FD_EEE,
	CAPS_HI_10GBASET_FD_EEE,
	CAPS_HI_RESERVED5,
	CAPS_HI_RESERVED6,
	CAPS_HI_RESERVED7,
	CAPS_HI_RESERVED8,
	CAPS_HI_RESERVED9,
	CAPS_HI_CABLE_DIAG,
	CAPS_HI_TEMPERATURE,
	CAPS_HI_DOWNSHIFT,
	CAPS_HI_PTP_AVB_EN,
	CAPS_HI_MEDIA_DETECT,
	CAPS_HI_LINK_DROP,
	CAPS_HI_SLEEP_PROXY,
	CAPS_HI_WOL,
	CAPS_HI_MAC_STOP,
	CAPS_HI_EXT_LOOPBACK,
	CAPS_HI_INT_LOOPBACK,
	CAPS_HI_EFUSE_AGENT,
	CAPS_HI_WOL_TIMER,
	CAPS_HI_STATISTICS,
	CAPS_HI_TRANSACTION_ID,
};

enum hw_atl_fw2x_ctrl {
	CTRL_RESERVED1 = 0x00,
	CTRL_RESERVED2,
	CTRL_RESERVED3,
	CTRL_PAUSE,
	CTRL_ASYMMETRIC_PAUSE,
	CTRL_RESERVED4,
	CTRL_RESERVED5,
	CTRL_RESERVED6,
	CTRL_1GBASET_FD_EEE,
	CTRL_2P5GBASET_FD_EEE,
	CTRL_5GBASET_FD_EEE,
	CTRL_10GBASET_FD_EEE,
	CTRL_THERMAL_SHUTDOWN,
	CTRL_PHY_LOGS,
	CTRL_EEE_AUTO_DISABLE,
	CTRL_PFC,
	CTRL_WAKE_ON_LINK,
	CTRL_CABLE_DIAG,
	CTRL_TEMPERATURE,
	CTRL_DOWNSHIFT,
	CTRL_PTP_AVB,
	CTRL_RESERVED7,
	CTRL_LINK_DROP,
	CTRL_SLEEP_PROXY,
	CTRL_WOL,
	CTRL_MAC_STOP,
	CTRL_EXT_LOOPBACK,
	CTRL_INT_LOOPBACK,
	CTRL_RESERVED8,
	CTRL_WOL_TIMER,
	CTRL_STATISTICS,
	CTRL_FORCE_RECONNECT,
};

struct aq_hw_s;
struct aq_fw_ops;
struct aq_hw_caps_s;
struct aq_hw_link_status_s;

int hw_atl_utils_initfw(struct aq_hw_s *self, const struct aq_fw_ops **fw_ops);

int hw_atl_utils_soft_reset(struct aq_hw_s *self);

void hw_atl_utils_hw_chip_features_init(struct aq_hw_s *self, u32 *p);

int hw_atl_utils_mpi_read_mbox(struct aq_hw_s *self,
			       struct hw_atl_utils_mbox_header *pmbox);

void hw_atl_utils_mpi_read_stats(struct aq_hw_s *self,
				 struct hw_atl_utils_mbox *pmbox);

void hw_atl_utils_mpi_set(struct aq_hw_s *self,
			  enum hal_atl_utils_fw_state_e state,
			  u32 speed);

int hw_atl_utils_mpi_get_link_status(struct aq_hw_s *self);

int hw_atl_utils_get_mac_permanent(struct aq_hw_s *self,
				   u8 *mac);

unsigned int hw_atl_utils_mbps_2_speed_index(unsigned int mbps);

int hw_atl_utils_hw_get_regs(struct aq_hw_s *self,
			     const struct aq_hw_caps_s *aq_hw_caps,
			     u32 *regs_buff);

int hw_atl_utils_hw_set_power(struct aq_hw_s *self,
			      unsigned int power_state);

int hw_atl_utils_hw_deinit(struct aq_hw_s *self);

int hw_atl_utils_get_fw_version(struct aq_hw_s *self, u32 *fw_version);

int hw_atl_utils_update_stats(struct aq_hw_s *self);

struct aq_stats_s *hw_atl_utils_get_hw_stats(struct aq_hw_s *self);

int hw_atl_utils_fw_downld_dwords(struct aq_hw_s *self, u32 a,
				  u32 *p, u32 cnt);

int hw_atl_utils_fw_set_wol(struct aq_hw_s *self, bool wol_enabled, u8 *mac);

int hw_atl_utils_fw_rpc_call(struct aq_hw_s *self, unsigned int rpc_size);

int hw_atl_utils_fw_rpc_wait(struct aq_hw_s *self,
			     struct hw_atl_utils_fw_rpc **rpc);

extern const struct aq_fw_ops aq_fw_1x_ops;
extern const struct aq_fw_ops aq_fw_2x_ops;

#endif /* HW_ATL_UTILS_H */
