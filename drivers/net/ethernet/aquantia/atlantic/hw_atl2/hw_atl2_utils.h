/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef HW_ATL2_UTILS_H
#define HW_ATL2_UTILS_H

#include "aq_hw.h"

/* F W    A P I */

struct link_options_s {
	u8 link_up:1;
	u8 link_renegotiate:1;
	u8 minimal_link_speed:1;
	u8 internal_loopback:1;
	u8 external_loopback:1;
	u8 rate_10M_hd:1;
	u8 rate_100M_hd:1;
	u8 rate_1G_hd:1;

	u8 rate_10M:1;
	u8 rate_100M:1;
	u8 rate_1G:1;
	u8 rate_2P5G:1;
	u8 rate_N2P5G:1;
	u8 rate_5G:1;
	u8 rate_N5G:1;
	u8 rate_10G:1;

	u8 eee_100M:1;
	u8 eee_1G:1;
	u8 eee_2P5G:1;
	u8 eee_5G:1;
	u8 eee_10G:1;
	u8 rsvd3:3;

	u8 pause_rx:1;
	u8 pause_tx:1;
	u8 rsvd4:1;
	u8 downshift:1;
	u8 downshift_retry:4;
};

struct link_control_s {
	u8 mode:4;
	u8 disable_crc_corruption:1;
	u8 discard_short_frames:1;
	u8 flow_control_mode:1;
	u8 disable_length_check:1;

	u8 discard_errored_frames:1;
	u8 control_frame_enable:1;
	u8 enable_tx_padding:1;
	u8 enable_crc_forwarding:1;
	u8 enable_frame_padding_removal_rx: 1;
	u8 promiscuous_mode: 1;
	u8 rsvd:2;

	u16 rsvd2;
};

struct thermal_shutdown_s {
	u8 enable:1;
	u8 warning_enable:1;
	u8 rsvd:6;

	u8 shutdown_temperature;
	u8 cold_temperature;
	u8 warning_temperature;
};

struct mac_address_s {
	u8 mac_address[6];
};

struct mac_address_aligned_s {
	struct mac_address_s aligned;
	u16 rsvd;
};

struct sleep_proxy_s {
	struct wake_on_lan_s {
		u8 wake_on_magic_packet:1;
		u8 wake_on_pattern:1;
		u8 wake_on_link_up:1;
		u8 wake_on_link_down:1;
		u8 wake_on_ping:1;
		u8 wake_on_timer:1;
		u8 rsvd:2;

		u8 rsvd2;
		u16 rsvd3;

		u32 link_up_timeout;
		u32 link_down_timeout;
		u32 timer;
	} wake_on_lan;

	struct {
		u32 mask[4];
		u32 crc32;
	} wake_up_pattern[8];

	struct __packed {
		u8 arp_responder:1;
		u8 echo_responder:1;
		u8 igmp_client:1;
		u8 echo_truncate:1;
		u8 address_guard:1;
		u8 ignore_fragmented:1;
		u8 rsvd:2;

		u16 echo_max_len;
		u8 rsvd2;
	} ipv4_offload;

	u32 ipv4_offload_addr[8];
	u32 reserved[8];

	struct __packed {
		u8 ns_responder:1;
		u8 echo_responder:1;
		u8 mld_client:1;
		u8 echo_truncate:1;
		u8 address_guard:1;
		u8 rsvd:3;

		u16 echo_max_len;
		u8 rsvd2;
	} ipv6_offload;

	u32 ipv6_offload_addr[16][4];

	struct {
		u16 port[16];
	} tcp_port_offload;

	struct {
		u16 port[16];
	} udp_port_offload;

	struct {
		u32 retry_count;
		u32 retry_interval;
	} ka4_offload;

	struct {
		u32 timeout;
		u16 local_port;
		u16 remote_port;
		u8 remote_mac_addr[6];
		u16 rsvd;
		u32 rsvd2;
		u32 rsvd3;
		u16 rsvd4;
		u16 win_size;
		u32 seq_num;
		u32 ack_num;
		u32 local_ip;
		u32 remote_ip;
	} ka4_connection[16];

	struct {
		u32 retry_count;
		u32 retry_interval;
	} ka6_offload;

	struct {
		u32 timeout;
		u16 local_port;
		u16 remote_port;
		u8 remote_mac_addr[6];
		u16 rsvd;
		u32 rsvd2;
		u32 rsvd3;
		u16 rsvd4;
		u16 win_size;
		u32 seq_num;
		u32 ack_num;
		u32 local_ip[4];
		u32 remote_ip[4];
	} ka6_connection[16];

	struct {
		u32 rr_count;
		u32 rr_buf_len;
		u32 idx_offset;
		u32 rr__offset;
	} mdns_offload;
};

struct pause_quanta_s {
	u16 quanta_10M;
	u16 threshold_10M;
	u16 quanta_100M;
	u16 threshold_100M;
	u16 quanta_1G;
	u16 threshold_1G;
	u16 quanta_2P5G;
	u16 threshold_2P5G;
	u16 quanta_5G;
	u16 threshold_5G;
	u16 quanta_10G;
	u16 threshold_10G;
};

struct data_buffer_status_s {
	u32 data_offset;
	u32 data_length;
};

struct device_caps_s {
	u8 finite_flashless:1;
	u8 cable_diag:1;
	u8 ncsi:1;
	u8 avb:1;
	u8 rsvd:4;

	u8 rsvd2;
	u16 rsvd3;
	u32 rsvd4;
};

struct version_s {
	struct bundle_version_t {
		u8 major;
		u8 minor;
		u16 build;
	} bundle;
	struct mac_version_t {
		u8 major;
		u8 minor;
		u16 build;
	} mac;
	struct phy_version_t {
		u8 major;
		u8 minor;
		u16 build;
	} phy;
	u32 drv_iface_ver:4;
	u32 rsvd:28;
};

struct link_status_s {
	u8 link_state:4;
	u8 link_rate:4;

	u8 pause_tx:1;
	u8 pause_rx:1;
	u8 eee:1;
	u8 duplex:1;
	u8 rsvd:4;

	u16 rsvd2;
};

struct wol_status_s {
	u8 wake_count;
	u8 wake_reason;

	u16 wake_up_packet_length :12;
	u16 wake_up_pattern_number :3;
	u16 rsvd:1;

	u32 wake_up_packet[379];
};

struct mac_health_monitor_s {
	u8 mac_ready:1;
	u8 mac_fault:1;
	u8 mac_flashless_finished:1;
	u8 rsvd:5;

	u8 mac_temperature;
	u16 mac_heart_beat;
	u16 mac_fault_code;
	u16 rsvd2;
};

struct phy_health_monitor_s {
	u8 phy_ready:1;
	u8 phy_fault:1;
	u8 phy_hot_warning:1;
	u8 rsvd:5;

	u8 phy_temperature;
	u16 phy_heart_beat;
	u16 phy_fault_code;
	u16 rsvd2;
};

struct device_link_caps_s {
	u8 rsvd:3;
	u8 internal_loopback:1;
	u8 external_loopback:1;
	u8 rate_10M_hd:1;
	u8 rate_100M_hd:1;
	u8 rate_1G_hd:1;

	u8 rate_10M:1;
	u8 rate_100M:1;
	u8 rate_1G:1;
	u8 rate_2P5G:1;
	u8 rate_N2P5G:1;
	u8 rate_5G:1;
	u8 rate_N5G:1;
	u8 rate_10G:1;

	u8 rsvd3:1;
	u8 eee_100M:1;
	u8 eee_1G:1;
	u8 eee_2P5G:1;
	u8 rsvd4:1;
	u8 eee_5G:1;
	u8 rsvd5:1;
	u8 eee_10G:1;

	u8 pause_rx:1;
	u8 pause_tx:1;
	u8 pfc:1;
	u8 downshift:1;
	u8 downshift_retry:4;
};

struct sleep_proxy_caps_s {
	u8 ipv4_offload:1;
	u8 ipv6_offload:1;
	u8 tcp_port_offload:1;
	u8 udp_port_offload:1;
	u8 ka4_offload:1;
	u8 ka6_offload:1;
	u8 mdns_offload:1;
	u8 wake_on_ping:1;

	u8 wake_on_magic_packet:1;
	u8 wake_on_pattern:1;
	u8 wake_on_timer:1;
	u8 wake_on_link:1;
	u8 wake_patterns_count:4;

	u8 ipv4_count;
	u8 ipv6_count;

	u8 tcp_port_offload_count;
	u8 udp_port_offload_count;

	u8 tcp4_ka_count;
	u8 tcp6_ka_count;

	u8 igmp_offload:1;
	u8 mld_offload:1;
	u8 rsvd:6;

	u8 rsvd2;
	u16 rsvd3;
};

struct lkp_link_caps_s {
	u8 rsvd:5;
	u8 rate_10M_hd:1;
	u8 rate_100M_hd:1;
	u8 rate_1G_hd:1;

	u8 rate_10M:1;
	u8 rate_100M:1;
	u8 rate_1G:1;
	u8 rate_2P5G:1;
	u8 rate_N2P5G:1;
	u8 rate_5G:1;
	u8 rate_N5G:1;
	u8 rate_10G:1;

	u8 rsvd2:1;
	u8 eee_100M:1;
	u8 eee_1G:1;
	u8 eee_2P5G:1;
	u8 rsvd3:1;
	u8 eee_5G:1;
	u8 rsvd4:1;
	u8 eee_10G:1;

	u8 pause_rx:1;
	u8 pause_tx:1;
	u8 rsvd5:6;
};

struct core_dump_s {
	u32 reg0;
	u32 reg1;
	u32 reg2;

	u32 hi;
	u32 lo;

	u32 regs[32];
};

struct trace_s {
	u32 sync_counter;
	u32 mem_buffer[0x1ff];
};

struct cable_diag_control_s {
	u8 toggle :1;
	u8 rsvd:7;

	u8 wait_timeout_sec;
	u16 rsvd2;
};

struct cable_diag_lane_data_s {
	u8 result_code;
	u8 dist;
	u8 far_dist;
	u8 rsvd;
};

struct cable_diag_status_s {
	struct cable_diag_lane_data_s lane_data[4];
	u8 transact_id;
	u8 status:4;
	u8 rsvd:4;
	u16 rsvd2;
};

struct statistics_a0_s {
	struct {
		u32 link_up;
		u32 link_down;
	} link;

	struct {
		u64 tx_unicast_octets;
		u64 tx_multicast_octets;
		u64 tx_broadcast_octets;
		u64 rx_unicast_octets;
		u64 rx_multicast_octets;
		u64 rx_broadcast_octets;

		u32 tx_unicast_frames;
		u32 tx_multicast_frames;
		u32 tx_broadcast_frames;
		u32 tx_errors;

		u32 rx_unicast_frames;
		u32 rx_multicast_frames;
		u32 rx_broadcast_frames;
		u32 rx_dropped_frames;
		u32 rx_error_frames;

		u32 tx_good_frames;
		u32 rx_good_frames;
		u32 reserve_fw_gap;
	} msm;
	u32 main_loop_cycles;
	u32 reserve_fw_gap;
};

struct __packed statistics_b0_s {
	u64 rx_good_octets;
	u64 rx_pause_frames;
	u64 rx_good_frames;
	u64 rx_errors;
	u64 rx_unicast_frames;
	u64 rx_multicast_frames;
	u64 rx_broadcast_frames;

	u64 tx_good_octets;
	u64 tx_pause_frames;
	u64 tx_good_frames;
	u64 tx_errors;
	u64 tx_unicast_frames;
	u64 tx_multicast_frames;
	u64 tx_broadcast_frames;

	u32 main_loop_cycles;
};

struct __packed statistics_s {
	union __packed {
		struct statistics_a0_s a0;
		struct statistics_b0_s b0;
	};
};

struct filter_caps_s {
	u8 l2_filters_base_index:6;
	u8 flexible_filter_mask:2;
	u8 l2_filter_count;
	u8 ethertype_filter_base_index;
	u8 ethertype_filter_count;

	u8 vlan_filter_base_index;
	u8 vlan_filter_count;
	u8 l3_ip4_filter_base_index:4;
	u8 l3_ip4_filter_count:4;
	u8 l3_ip6_filter_base_index:4;
	u8 l3_ip6_filter_count:4;

	u8 l4_filter_base_index:4;
	u8 l4_filter_count:4;
	u8 l4_flex_filter_base_index:4;
	u8 l4_flex_filter_count:4;
	u8 rslv_tbl_base_index;
	u8 rslv_tbl_count;
};

struct request_policy_s {
	struct {
		u8 all:1;
		u8 mcast:1;
		u8 rx_queue_tc_index:5;
		u8 queue_or_tc:1;
	} promisc;

	struct {
		u8 accept:1;
		u8 rsvd:1;
		u8 rx_queue_tc_index:5;
		u8 queue_or_tc:1;
	} bcast;

	struct {
		u8 accept:1;
		u8 rsvd:1;
		u8 rx_queue_tc_index:5;
		u8 queue_or_tc:1;
	} mcast;

	u8 rsvd:8;
};

struct fw_interface_in {
	u32 mtu;
	u32 rsvd1;
	struct mac_address_aligned_s mac_address;
	struct link_control_s link_control;
	u32 rsvd2;
	struct link_options_s link_options;
	u32 rsvd3;
	struct thermal_shutdown_s thermal_shutdown;
	u32 rsvd4;
	struct sleep_proxy_s sleep_proxy;
	u32 rsvd5;
	struct pause_quanta_s pause_quanta[8];
	struct cable_diag_control_s cable_diag_control;
	u32 rsvd6;
	struct data_buffer_status_s data_buffer_status;
	u32 rsvd7;
	struct request_policy_s request_policy;
};

struct transaction_counter_s {
	u16 transaction_cnt_a;
	u16 transaction_cnt_b;
};

struct management_status_s {
	struct mac_address_s mac_address;
	u16 vlan;

	struct{
		u32 enable : 1;
		u32 rsvd:31;
	} flags;

	u32 rsvd1;
	u32 rsvd2;
	u32 rsvd3;
	u32 rsvd4;
	u32 rsvd5;
};

struct __packed fw_interface_out {
	struct transaction_counter_s transaction_id;
	struct version_s version;
	struct link_status_s link_status;
	struct wol_status_s wol_status;
	u32 rsvd;
	u32 rsvd2;
	struct mac_health_monitor_s mac_health_monitor;
	u32 rsvd3;
	u32 rsvd4;
	struct phy_health_monitor_s phy_health_monitor;
	u32 rsvd5;
	u32 rsvd6;
	struct cable_diag_status_s cable_diag_status;
	u32 rsvd7;
	struct device_link_caps_s device_link_caps;
	u32 rsvd8;
	struct sleep_proxy_caps_s sleep_proxy_caps;
	u32 rsvd9;
	struct lkp_link_caps_s lkp_link_caps;
	u32 rsvd10;
	struct core_dump_s core_dump;
	u32 rsvd11;
	struct statistics_s stats;
	struct filter_caps_s filter_caps;
	struct device_caps_s device_caps;
	u32 rsvd13;
	struct management_status_s management_status;
	u32 reserve[21];
	struct trace_s trace;
};

#define  AQ_A2_FW_LINK_RATE_INVALID 0
#define  AQ_A2_FW_LINK_RATE_10M     1
#define  AQ_A2_FW_LINK_RATE_100M    2
#define  AQ_A2_FW_LINK_RATE_1G      3
#define  AQ_A2_FW_LINK_RATE_2G5     4
#define  AQ_A2_FW_LINK_RATE_5G      5
#define  AQ_A2_FW_LINK_RATE_10G     6

#define  AQ_HOST_MODE_INVALID      0U
#define  AQ_HOST_MODE_ACTIVE       1U
#define  AQ_HOST_MODE_SLEEP_PROXY  2U
#define  AQ_HOST_MODE_LOW_POWER    3U
#define  AQ_HOST_MODE_SHUTDOWN     4U

#define  AQ_A2_FW_INTERFACE_A0     0
#define  AQ_A2_FW_INTERFACE_B0     1

int hw_atl2_utils_initfw(struct aq_hw_s *self, const struct aq_fw_ops **fw_ops);

int hw_atl2_utils_soft_reset(struct aq_hw_s *self);

u32 hw_atl2_utils_get_fw_version(struct aq_hw_s *self);

int hw_atl2_utils_get_action_resolve_table_caps(struct aq_hw_s *self,
						u8 *base_index, u8 *count);

extern const struct aq_fw_ops aq_a2_fw_ops;

#endif /* HW_ATL2_UTILS_H */
