/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
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

struct __packed hw_aq_atl_utils_fw_rpc {
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
			u16 friendly_name_len;
			u16 friendly_name[65];
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
			} wol_pattern;
		} msg_wol;

		struct {
			u32 is_wake_on_link_down;
			u32 is_wake_on_link_up;
		} msg_wolink;
	};
};

struct __packed hw_aq_atl_utils_mbox_header {
	u32 version;
	u32 transaction_id;
	u32 error;
};

struct __packed hw_aq_atl_utils_mbox {
	struct hw_aq_atl_utils_mbox_header header;
	struct hw_atl_stats_s stats;
};

#define HAL_ATLANTIC_UTILS_CHIP_MIPS         0x00000001U
#define HAL_ATLANTIC_UTILS_CHIP_TPO2         0x00000002U
#define HAL_ATLANTIC_UTILS_CHIP_RPF2         0x00000004U
#define HAL_ATLANTIC_UTILS_CHIP_MPI_AQ       0x00000010U
#define HAL_ATLANTIC_UTILS_CHIP_REVISION_A0  0x01000000U
#define HAL_ATLANTIC_UTILS_CHIP_REVISION_B0  0x02000000U

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

struct aq_hw_s;
struct aq_fw_ops;
struct aq_hw_caps_s;
struct aq_hw_link_status_s;

int hw_atl_utils_initfw(struct aq_hw_s *self, const struct aq_fw_ops **fw_ops);

int hw_atl_utils_soft_reset(struct aq_hw_s *self);

void hw_atl_utils_hw_chip_features_init(struct aq_hw_s *self, u32 *p);

int hw_atl_utils_mpi_read_mbox(struct aq_hw_s *self,
			       struct hw_aq_atl_utils_mbox_header *pmbox);

void hw_atl_utils_mpi_read_stats(struct aq_hw_s *self,
				 struct hw_aq_atl_utils_mbox *pmbox);

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

extern const struct aq_fw_ops aq_fw_1x_ops;

#endif /* HW_ATL_UTILS_H */
