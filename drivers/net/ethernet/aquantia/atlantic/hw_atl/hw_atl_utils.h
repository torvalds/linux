/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2019 aQuantia Corporation. All rights reserved
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
	__le16 vlan;
};

/* Hardware rx HW TIMESTAMP writeback */
struct __packed hw_atl_rxd_hwts_wb_s {
	u32 sec_hw;
	u32 ns;
	u32 sec_lw0;
	u32 sec_lw1;
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

struct __packed drv_msg_enable_wakeup {
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
};

struct __packed magic_packet_pattern_s {
	u8 mac_addr[ETH_ALEN];
};

struct __packed drv_msg_wol_add {
	u32 priority;
	u32 packet_type;
	u32 pattern_id;
	u32 next_pattern_offset;

	struct magic_packet_pattern_s magic_packet_pattern;
};

struct __packed drv_msg_wol_remove {
	u32 id;
};

struct __packed hw_atl_utils_mbox_header {
	u32 version;
	u32 transaction_id;
	u32 error;
};

struct __packed hw_atl_ptp_offset {
	u16 ingress_100;
	u16 egress_100;
	u16 ingress_1000;
	u16 egress_1000;
	u16 ingress_2500;
	u16 egress_2500;
	u16 ingress_5000;
	u16 egress_5000;
	u16 ingress_10000;
	u16 egress_10000;
};

struct __packed hw_atl_cable_diag {
	u8 fault;
	u8 distance;
	u8 far_distance;
	u8 reserved;
};

enum gpio_pin_function {
	GPIO_PIN_FUNCTION_NC,
	GPIO_PIN_FUNCTION_VAUX_ENABLE,
	GPIO_PIN_FUNCTION_EFUSE_BURN_ENABLE,
	GPIO_PIN_FUNCTION_SFP_PLUS_DETECT,
	GPIO_PIN_FUNCTION_TX_DISABLE,
	GPIO_PIN_FUNCTION_RATE_SEL_0,
	GPIO_PIN_FUNCTION_RATE_SEL_1,
	GPIO_PIN_FUNCTION_TX_FAULT,
	GPIO_PIN_FUNCTION_PTP0,
	GPIO_PIN_FUNCTION_PTP1,
	GPIO_PIN_FUNCTION_PTP2,
	GPIO_PIN_FUNCTION_SIZE
};

struct __packed hw_atl_info {
	u8 reserved[6];
	u16 phy_fault_code;
	u16 phy_temperature;
	u8 cable_len;
	u8 reserved1;
	struct hw_atl_cable_diag cable_diag_data[4];
	struct hw_atl_ptp_offset ptp_offset;
	u8 reserved2[12];
	u32 caps_lo;
	u32 caps_hi;
	u32 reserved_datapath;
	u32 reserved3[7];
	u32 reserved_simpleresp[3];
	u32 reserved_linkstat[7];
	u32 reserved_wakes_count;
	u32 reserved_eee_stat[12];
	u32 tx_stuck_cnt;
	u32 setting_address;
	u32 setting_length;
	u32 caps_ex;
	enum gpio_pin_function gpio_pin[3];
	u32 pcie_aer_dump[18];
	u16 snr_margin[4];
};

struct __packed hw_atl_utils_mbox {
	struct hw_atl_utils_mbox_header header;
	struct hw_atl_stats_s stats;
	struct hw_atl_info info;
};

struct __packed offload_ip_info {
	u8 v4_local_addr_count;
	u8 v4_addr_count;
	u8 v6_local_addr_count;
	u8 v6_addr_count;
	u32 v4_addr;
	u32 v4_prefix;
	u32 v6_addr;
	u32 v6_prefix;
};

struct __packed offload_port_info {
	u16 udp_port_count;
	u16 tcp_port_count;
	u32 udp_port;
	u32 tcp_port;
};

struct __packed offload_ka_info {
	u16 v4_ka_count;
	u16 v6_ka_count;
	u32 retry_count;
	u32 retry_interval;
	u32 v4_ka;
	u32 v6_ka;
};

struct __packed offload_rr_info {
	u32 rr_count;
	u32 rr_buf_len;
	u32 rr_id_x;
	u32 rr_buf;
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
	u8 buf[];
};

struct __packed hw_atl_utils_fw_rpc {
	u32 msg_id;

	union {
		/* fw1x structures */
		struct drv_msg_wol_add msg_wol_add;
		struct drv_msg_wol_remove msg_wol_remove;
		struct drv_msg_enable_wakeup msg_enable_wakeup;
		/* fw2x structures */
		struct offload_info fw2x_offloads;
	};
};

/* Mailbox FW Request interface */
struct __packed hw_fw_request_ptp_gpio_ctrl {
	u32 index;
	u32 period;
	u64 start;
};

struct __packed hw_fw_request_ptp_adj_freq {
	u32 ns_mac;
	u32 fns_mac;
	u32 ns_phy;
	u32 fns_phy;
	u32 mac_ns_adj;
	u32 mac_fns_adj;
};

struct __packed hw_fw_request_ptp_adj_clock {
	u32 ns;
	u32 sec;
	int sign;
};

#define HW_AQ_FW_REQUEST_PTP_GPIO_CTRL	         0x11
#define HW_AQ_FW_REQUEST_PTP_ADJ_FREQ	         0x12
#define HW_AQ_FW_REQUEST_PTP_ADJ_CLOCK	         0x13

struct __packed hw_fw_request_iface {
	u32 msg_id;
	union {
		/* PTP FW Request */
		struct hw_fw_request_ptp_gpio_ctrl ptp_gpio_ctrl;
		struct hw_fw_request_ptp_adj_freq ptp_adj_freq;
		struct hw_fw_request_ptp_adj_clock ptp_adj_clock;
	};
};

struct __packed hw_atl_utils_settings {
	u32 mtu;
	u32 downshift_retry_count;
	u32 link_pause_frame_quanta_100m;
	u32 link_pause_frame_threshold_100m;
	u32 link_pause_frame_quanta_1g;
	u32 link_pause_frame_threshold_1g;
	u32 link_pause_frame_quanta_2p5g;
	u32 link_pause_frame_threshold_2p5g;
	u32 link_pause_frame_quanta_5g;
	u32 link_pause_frame_threshold_5g;
	u32 link_pause_frame_quanta_10g;
	u32 link_pause_frame_threshold_10g;
	u32 pfc_quanta_class_0;
	u32 pfc_threshold_class_0;
	u32 pfc_quanta_class_1;
	u32 pfc_threshold_class_1;
	u32 pfc_quanta_class_2;
	u32 pfc_threshold_class_2;
	u32 pfc_quanta_class_3;
	u32 pfc_threshold_class_3;
	u32 pfc_quanta_class_4;
	u32 pfc_threshold_class_4;
	u32 pfc_quanta_class_5;
	u32 pfc_threshold_class_5;
	u32 pfc_quanta_class_6;
	u32 pfc_threshold_class_6;
	u32 pfc_quanta_class_7;
	u32 pfc_threshold_class_7;
	u32 eee_link_down_timeout;
	u32 eee_link_up_timeout;
	u32 eee_max_link_drops;
	u32 eee_rates_mask;
	u32 wake_timer;
	u32 thermal_shutdown_off_temp;
	u32 thermal_shutdown_warning_temp;
	u32 thermal_shutdown_cold_temp;
	u32 msm_options;
	u32 dac_cable_serdes_modes;
	u32 media_detect;
};

enum macsec_msg_type {
	macsec_cfg_msg = 0,
	macsec_add_rx_sc_msg,
	macsec_add_tx_sc_msg,
	macsec_add_rx_sa_msg,
	macsec_add_tx_sa_msg,
	macsec_get_stats_msg,
};

struct __packed macsec_cfg_request {
	u32 enabled;
	u32 egress_threshold;
	u32 ingress_threshold;
	u32 interrupts_enabled;
};

struct __packed macsec_msg_fw_request {
	u32 msg_id; /* not used */
	u32 msg_type;
	struct macsec_cfg_request cfg;
};

struct __packed macsec_msg_fw_response {
	u32 result;
};

enum hw_atl_rx_action_with_traffic {
	HW_ATL_RX_DISCARD,
	HW_ATL_RX_HOST,
	HW_ATL_RX_MNGMNT,
	HW_ATL_RX_HOST_AND_MNGMNT,
	HW_ATL_RX_WOL
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

#define HAL_ATLANTIC_UTILS_FW_MSG_WOL_ADD       0x4U
#define HAL_ATLANTIC_UTILS_FW_MSG_WOL_PRIOR     0x10000000U
#define HAL_ATLANTIC_UTILS_FW_MSG_WOL_PATTERN   0x1U
#define HAL_ATLANTIC_UTILS_FW_MSG_WOL_MAG_PKT   0x2U
#define HAL_ATLANTIC_UTILS_FW_MSG_WOL_DEL       0x5U
#define HAL_ATLANTIC_UTILS_FW_MSG_ENABLE_WAKEUP 0x6U

enum hw_atl_fw2x_rate {
	FW2X_RATE_100M    = 0x20,
	FW2X_RATE_1G      = 0x100,
	FW2X_RATE_2G5     = 0x200,
	FW2X_RATE_5G      = 0x400,
	FW2X_RATE_10G     = 0x800,
};

/* 0x370
 * Link capabilities resolution register
 */
enum hw_atl_fw2x_caps_lo {
	CAPS_LO_10BASET_HD        = 0,
	CAPS_LO_10BASET_FD,
	CAPS_LO_100BASETX_HD,
	CAPS_LO_100BASET4_HD,
	CAPS_LO_100BASET2_HD,
	CAPS_LO_100BASETX_FD      = 5,
	CAPS_LO_100BASET2_FD,
	CAPS_LO_1000BASET_HD,
	CAPS_LO_1000BASET_FD,
	CAPS_LO_2P5GBASET_FD,
	CAPS_LO_5GBASET_FD        = 10,
	CAPS_LO_10GBASET_FD,
	CAPS_LO_AUTONEG,
	CAPS_LO_SMBUS_READ,
	CAPS_LO_SMBUS_WRITE,
	CAPS_LO_MACSEC            = 15,
	CAPS_LO_RESERVED1,
	CAPS_LO_WAKE_ON_LINK_FORCED,
	CAPS_LO_HIGH_TEMP_WARNING = 29,
	CAPS_LO_DRIVER_SCRATCHPAD = 30,
	CAPS_LO_GLOBAL_FAULT      = 31
};

/* 0x374
 * Status register
 */
enum hw_atl_fw2x_caps_hi {
	CAPS_HI_TPO2EN            = 0,
	CAPS_HI_10BASET_EEE,
	CAPS_HI_RESERVED2,
	CAPS_HI_PAUSE,
	CAPS_HI_ASYMMETRIC_PAUSE,
	CAPS_HI_100BASETX_EEE     = 5,
	CAPS_HI_PHY_BUF_SEND,
	CAPS_HI_PHY_BUF_RECV,
	CAPS_HI_1000BASET_FD_EEE,
	CAPS_HI_2P5GBASET_FD_EEE,
	CAPS_HI_5GBASET_FD_EEE    = 10,
	CAPS_HI_10GBASET_FD_EEE,
	CAPS_HI_FW_REQUEST,
	CAPS_HI_PHY_LOG,
	CAPS_HI_EEE_AUTO_DISABLE_SETTINGS,
	CAPS_HI_PFC               = 15,
	CAPS_HI_WAKE_ON_LINK,
	CAPS_HI_CABLE_DIAG,
	CAPS_HI_TEMPERATURE,
	CAPS_HI_DOWNSHIFT,
	CAPS_HI_PTP_AVB_EN_FW2X   = 20,
	CAPS_HI_THERMAL_SHUTDOWN,
	CAPS_HI_LINK_DROP,
	CAPS_HI_SLEEP_PROXY,
	CAPS_HI_WOL,
	CAPS_HI_MAC_STOP          = 25,
	CAPS_HI_EXT_LOOPBACK,
	CAPS_HI_INT_LOOPBACK,
	CAPS_HI_EFUSE_AGENT,
	CAPS_HI_WOL_TIMER,
	CAPS_HI_STATISTICS        = 30,
	CAPS_HI_TRANSACTION_ID,
};

/* 0x36C
 * Control register
 */
enum hw_atl_fw2x_ctrl {
	CTRL_RESERVED1            = 0,
	CTRL_RESERVED2,
	CTRL_RESERVED3,
	CTRL_PAUSE,
	CTRL_ASYMMETRIC_PAUSE,
	CTRL_RESERVED4            = 5,
	CTRL_RESERVED5,
	CTRL_RESERVED6,
	CTRL_1GBASET_FD_EEE,
	CTRL_2P5GBASET_FD_EEE,
	CTRL_5GBASET_FD_EEE       = 10,
	CTRL_10GBASET_FD_EEE,
	CTRL_THERMAL_SHUTDOWN,
	CTRL_PHY_LOGS,
	CTRL_EEE_AUTO_DISABLE,
	CTRL_PFC                  = 15,
	CTRL_WAKE_ON_LINK,
	CTRL_CABLE_DIAG,
	CTRL_TEMPERATURE,
	CTRL_DOWNSHIFT,
	CTRL_PTP_AVB              = 20,
	CTRL_RESERVED7,
	CTRL_LINK_DROP,
	CTRL_SLEEP_PROXY,
	CTRL_WOL,
	CTRL_MAC_STOP             = 25,
	CTRL_EXT_LOOPBACK,
	CTRL_INT_LOOPBACK,
	CTRL_RESERVED8,
	CTRL_WOL_TIMER,
	CTRL_STATISTICS           = 30,
	CTRL_FORCE_RECONNECT,
};

enum hw_atl_caps_ex {
	CAPS_EX_LED_CONTROL       =  0,
	CAPS_EX_LED0_MODE_LO,
	CAPS_EX_LED0_MODE_HI,
	CAPS_EX_LED1_MODE_LO,
	CAPS_EX_LED1_MODE_HI,
	CAPS_EX_LED2_MODE_LO      =  5,
	CAPS_EX_LED2_MODE_HI,
	CAPS_EX_RESERVED07,
	CAPS_EX_RESERVED08,
	CAPS_EX_RESERVED09,
	CAPS_EX_RESERVED10        = 10,
	CAPS_EX_RESERVED11,
	CAPS_EX_RESERVED12,
	CAPS_EX_RESERVED13,
	CAPS_EX_RESERVED14,
	CAPS_EX_RESERVED15        = 15,
	CAPS_EX_PHY_PTP_EN,
	CAPS_EX_MAC_PTP_EN,
	CAPS_EX_EXT_CLK_EN,
	CAPS_EX_SCHED_DMA_EN,
	CAPS_EX_PTP_GPIO_EN       = 20,
	CAPS_EX_UPDATE_SETTINGS,
	CAPS_EX_PHY_CTRL_TS_PIN,
	CAPS_EX_SNR_OPERATING_MARGIN,
	CAPS_EX_RESERVED24,
	CAPS_EX_RESERVED25        = 25,
	CAPS_EX_RESERVED26,
	CAPS_EX_RESERVED27,
	CAPS_EX_RESERVED28,
	CAPS_EX_RESERVED29,
	CAPS_EX_RESERVED30        = 30,
	CAPS_EX_RESERVED31
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

int hw_atl_write_fwcfg_dwords(struct aq_hw_s *self, u32 *p, u32 cnt);

int hw_atl_write_fwsettings_dwords(struct aq_hw_s *self, u32 offset, u32 *p,
				   u32 cnt);

int hw_atl_utils_fw_set_wol(struct aq_hw_s *self, bool wol_enabled, u8 *mac);

int hw_atl_utils_fw_rpc_call(struct aq_hw_s *self, unsigned int rpc_size);

int hw_atl_utils_fw_rpc_wait(struct aq_hw_s *self,
			     struct hw_atl_utils_fw_rpc **rpc);

extern const struct aq_fw_ops aq_fw_1x_ops;
extern const struct aq_fw_ops aq_fw_2x_ops;

#endif /* HW_ATL_UTILS_H */
