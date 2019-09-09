/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Thunderbolt control channel messages
 *
 * Copyright (C) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2017, Intel Corporation
 */

#ifndef _TB_MSGS
#define _TB_MSGS

#include <linux/types.h>
#include <linux/uuid.h>

enum tb_cfg_space {
	TB_CFG_HOPS = 0,
	TB_CFG_PORT = 1,
	TB_CFG_SWITCH = 2,
	TB_CFG_COUNTERS = 3,
};

enum tb_cfg_error {
	TB_CFG_ERROR_PORT_NOT_CONNECTED = 0,
	TB_CFG_ERROR_LINK_ERROR = 1,
	TB_CFG_ERROR_INVALID_CONFIG_SPACE = 2,
	TB_CFG_ERROR_NO_SUCH_PORT = 4,
	TB_CFG_ERROR_ACK_PLUG_EVENT = 7, /* send as reply to TB_CFG_PKG_EVENT */
	TB_CFG_ERROR_LOOP = 8,
	TB_CFG_ERROR_HEC_ERROR_DETECTED = 12,
	TB_CFG_ERROR_FLOW_CONTROL_ERROR = 13,
};

/* common header */
struct tb_cfg_header {
	u32 route_hi:22;
	u32 unknown:10; /* highest order bit is set on replies */
	u32 route_lo;
} __packed;

/* additional header for read/write packets */
struct tb_cfg_address {
	u32 offset:13; /* in dwords */
	u32 length:6; /* in dwords */
	u32 port:6;
	enum tb_cfg_space space:2;
	u32 seq:2; /* sequence number  */
	u32 zero:3;
} __packed;

/* TB_CFG_PKG_READ, response for TB_CFG_PKG_WRITE */
struct cfg_read_pkg {
	struct tb_cfg_header header;
	struct tb_cfg_address addr;
} __packed;

/* TB_CFG_PKG_WRITE, response for TB_CFG_PKG_READ */
struct cfg_write_pkg {
	struct tb_cfg_header header;
	struct tb_cfg_address addr;
	u32 data[64]; /* maximum size, tb_cfg_address.length has 6 bits */
} __packed;

/* TB_CFG_PKG_ERROR */
struct cfg_error_pkg {
	struct tb_cfg_header header;
	enum tb_cfg_error error:4;
	u32 zero1:4;
	u32 port:6;
	u32 zero2:2; /* Both should be zero, still they are different fields. */
	u32 zero3:16;
} __packed;

/* TB_CFG_PKG_EVENT */
struct cfg_event_pkg {
	struct tb_cfg_header header;
	u32 port:6;
	u32 zero:25;
	bool unplug:1;
} __packed;

/* TB_CFG_PKG_RESET */
struct cfg_reset_pkg {
	struct tb_cfg_header header;
} __packed;

/* TB_CFG_PKG_PREPARE_TO_SLEEP */
struct cfg_pts_pkg {
	struct tb_cfg_header header;
	u32 data;
} __packed;

/* ICM messages */

enum icm_pkg_code {
	ICM_GET_TOPOLOGY = 0x1,
	ICM_DRIVER_READY = 0x3,
	ICM_APPROVE_DEVICE = 0x4,
	ICM_CHALLENGE_DEVICE = 0x5,
	ICM_ADD_DEVICE_KEY = 0x6,
	ICM_GET_ROUTE = 0xa,
	ICM_APPROVE_XDOMAIN = 0x10,
	ICM_DISCONNECT_XDOMAIN = 0x11,
	ICM_PREBOOT_ACL = 0x18,
};

enum icm_event_code {
	ICM_EVENT_DEVICE_CONNECTED = 3,
	ICM_EVENT_DEVICE_DISCONNECTED = 4,
	ICM_EVENT_XDOMAIN_CONNECTED = 6,
	ICM_EVENT_XDOMAIN_DISCONNECTED = 7,
};

struct icm_pkg_header {
	u8 code;
	u8 flags;
	u8 packet_id;
	u8 total_packets;
};

#define ICM_FLAGS_ERROR			BIT(0)
#define ICM_FLAGS_NO_KEY		BIT(1)
#define ICM_FLAGS_SLEVEL_SHIFT		3
#define ICM_FLAGS_SLEVEL_MASK		GENMASK(4, 3)
#define ICM_FLAGS_WRITE			BIT(7)

struct icm_pkg_driver_ready {
	struct icm_pkg_header hdr;
};

/* Falcon Ridge only messages */

struct icm_fr_pkg_driver_ready_response {
	struct icm_pkg_header hdr;
	u8 romver;
	u8 ramver;
	u16 security_level;
};

#define ICM_FR_SLEVEL_MASK		0xf

/* Falcon Ridge & Alpine Ridge common messages */

struct icm_fr_pkg_get_topology {
	struct icm_pkg_header hdr;
};

#define ICM_GET_TOPOLOGY_PACKETS	14

struct icm_fr_pkg_get_topology_response {
	struct icm_pkg_header hdr;
	u32 route_lo;
	u32 route_hi;
	u8 first_data;
	u8 second_data;
	u8 drom_i2c_address_index;
	u8 switch_index;
	u32 reserved[2];
	u32 ports[16];
	u32 port_hop_info[16];
};

#define ICM_SWITCH_USED			BIT(0)
#define ICM_SWITCH_UPSTREAM_PORT_MASK	GENMASK(7, 1)
#define ICM_SWITCH_UPSTREAM_PORT_SHIFT	1

#define ICM_PORT_TYPE_MASK		GENMASK(23, 0)
#define ICM_PORT_INDEX_SHIFT		24
#define ICM_PORT_INDEX_MASK		GENMASK(31, 24)

struct icm_fr_event_device_connected {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u8 connection_key;
	u8 connection_id;
	u16 link_info;
	u32 ep_name[55];
};

#define ICM_LINK_INFO_LINK_MASK		0x7
#define ICM_LINK_INFO_DEPTH_SHIFT	4
#define ICM_LINK_INFO_DEPTH_MASK	GENMASK(7, 4)
#define ICM_LINK_INFO_APPROVED		BIT(8)
#define ICM_LINK_INFO_REJECTED		BIT(9)
#define ICM_LINK_INFO_BOOT		BIT(10)

struct icm_fr_pkg_approve_device {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u8 connection_key;
	u8 connection_id;
	u16 reserved;
};

struct icm_fr_event_device_disconnected {
	struct icm_pkg_header hdr;
	u16 reserved;
	u16 link_info;
};

struct icm_fr_event_xdomain_connected {
	struct icm_pkg_header hdr;
	u16 reserved;
	u16 link_info;
	uuid_t remote_uuid;
	uuid_t local_uuid;
	u32 local_route_hi;
	u32 local_route_lo;
	u32 remote_route_hi;
	u32 remote_route_lo;
};

struct icm_fr_event_xdomain_disconnected {
	struct icm_pkg_header hdr;
	u16 reserved;
	u16 link_info;
	uuid_t remote_uuid;
};

struct icm_fr_pkg_add_device_key {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u8 connection_key;
	u8 connection_id;
	u16 reserved;
	u32 key[8];
};

struct icm_fr_pkg_add_device_key_response {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u8 connection_key;
	u8 connection_id;
	u16 reserved;
};

struct icm_fr_pkg_challenge_device {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u8 connection_key;
	u8 connection_id;
	u16 reserved;
	u32 challenge[8];
};

struct icm_fr_pkg_challenge_device_response {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u8 connection_key;
	u8 connection_id;
	u16 reserved;
	u32 challenge[8];
	u32 response[8];
};

struct icm_fr_pkg_approve_xdomain {
	struct icm_pkg_header hdr;
	u16 reserved;
	u16 link_info;
	uuid_t remote_uuid;
	u16 transmit_path;
	u16 transmit_ring;
	u16 receive_path;
	u16 receive_ring;
};

struct icm_fr_pkg_approve_xdomain_response {
	struct icm_pkg_header hdr;
	u16 reserved;
	u16 link_info;
	uuid_t remote_uuid;
	u16 transmit_path;
	u16 transmit_ring;
	u16 receive_path;
	u16 receive_ring;
};

/* Alpine Ridge only messages */

struct icm_ar_pkg_driver_ready_response {
	struct icm_pkg_header hdr;
	u8 romver;
	u8 ramver;
	u16 info;
};

#define ICM_AR_FLAGS_RTD3		BIT(6)

#define ICM_AR_INFO_SLEVEL_MASK		GENMASK(3, 0)
#define ICM_AR_INFO_BOOT_ACL_SHIFT	7
#define ICM_AR_INFO_BOOT_ACL_MASK	GENMASK(11, 7)
#define ICM_AR_INFO_BOOT_ACL_SUPPORTED	BIT(13)

struct icm_ar_pkg_get_route {
	struct icm_pkg_header hdr;
	u16 reserved;
	u16 link_info;
};

struct icm_ar_pkg_get_route_response {
	struct icm_pkg_header hdr;
	u16 reserved;
	u16 link_info;
	u32 route_hi;
	u32 route_lo;
};

struct icm_ar_boot_acl_entry {
	u32 uuid_lo;
	u32 uuid_hi;
};

#define ICM_AR_PREBOOT_ACL_ENTRIES	16

struct icm_ar_pkg_preboot_acl {
	struct icm_pkg_header hdr;
	struct icm_ar_boot_acl_entry acl[ICM_AR_PREBOOT_ACL_ENTRIES];
};

struct icm_ar_pkg_preboot_acl_response {
	struct icm_pkg_header hdr;
	struct icm_ar_boot_acl_entry acl[ICM_AR_PREBOOT_ACL_ENTRIES];
};

/* Titan Ridge messages */

struct icm_tr_pkg_driver_ready_response {
	struct icm_pkg_header hdr;
	u16 reserved1;
	u16 info;
	u32 nvm_version;
	u16 device_id;
	u16 reserved2;
};

#define ICM_TR_FLAGS_RTD3		BIT(6)

#define ICM_TR_INFO_SLEVEL_MASK		GENMASK(2, 0)
#define ICM_TR_INFO_BOOT_ACL_SHIFT	7
#define ICM_TR_INFO_BOOT_ACL_MASK	GENMASK(12, 7)

struct icm_tr_event_device_connected {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u32 route_hi;
	u32 route_lo;
	u8 connection_id;
	u8 reserved;
	u16 link_info;
	u32 ep_name[55];
};

struct icm_tr_event_device_disconnected {
	struct icm_pkg_header hdr;
	u32 route_hi;
	u32 route_lo;
};

struct icm_tr_event_xdomain_connected {
	struct icm_pkg_header hdr;
	u16 reserved;
	u16 link_info;
	uuid_t remote_uuid;
	uuid_t local_uuid;
	u32 local_route_hi;
	u32 local_route_lo;
	u32 remote_route_hi;
	u32 remote_route_lo;
};

struct icm_tr_event_xdomain_disconnected {
	struct icm_pkg_header hdr;
	u32 route_hi;
	u32 route_lo;
	uuid_t remote_uuid;
};

struct icm_tr_pkg_approve_device {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u32 route_hi;
	u32 route_lo;
	u8 connection_id;
	u8 reserved1[3];
};

struct icm_tr_pkg_add_device_key {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u32 route_hi;
	u32 route_lo;
	u8 connection_id;
	u8 reserved[3];
	u32 key[8];
};

struct icm_tr_pkg_challenge_device {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u32 route_hi;
	u32 route_lo;
	u8 connection_id;
	u8 reserved[3];
	u32 challenge[8];
};

struct icm_tr_pkg_approve_xdomain {
	struct icm_pkg_header hdr;
	u32 route_hi;
	u32 route_lo;
	uuid_t remote_uuid;
	u16 transmit_path;
	u16 transmit_ring;
	u16 receive_path;
	u16 receive_ring;
};

struct icm_tr_pkg_disconnect_xdomain {
	struct icm_pkg_header hdr;
	u8 stage;
	u8 reserved[3];
	u32 route_hi;
	u32 route_lo;
	uuid_t remote_uuid;
};

struct icm_tr_pkg_challenge_device_response {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u32 route_hi;
	u32 route_lo;
	u8 connection_id;
	u8 reserved[3];
	u32 challenge[8];
	u32 response[8];
};

struct icm_tr_pkg_add_device_key_response {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u32 route_hi;
	u32 route_lo;
	u8 connection_id;
	u8 reserved[3];
};

struct icm_tr_pkg_approve_xdomain_response {
	struct icm_pkg_header hdr;
	u32 route_hi;
	u32 route_lo;
	uuid_t remote_uuid;
	u16 transmit_path;
	u16 transmit_ring;
	u16 receive_path;
	u16 receive_ring;
};

struct icm_tr_pkg_disconnect_xdomain_response {
	struct icm_pkg_header hdr;
	u8 stage;
	u8 reserved[3];
	u32 route_hi;
	u32 route_lo;
	uuid_t remote_uuid;
};

/* XDomain messages */

struct tb_xdomain_header {
	u32 route_hi;
	u32 route_lo;
	u32 length_sn;
};

#define TB_XDOMAIN_LENGTH_MASK	GENMASK(5, 0)
#define TB_XDOMAIN_SN_MASK	GENMASK(28, 27)
#define TB_XDOMAIN_SN_SHIFT	27

enum tb_xdp_type {
	UUID_REQUEST_OLD = 1,
	UUID_RESPONSE = 2,
	PROPERTIES_REQUEST,
	PROPERTIES_RESPONSE,
	PROPERTIES_CHANGED_REQUEST,
	PROPERTIES_CHANGED_RESPONSE,
	ERROR_RESPONSE,
	UUID_REQUEST = 12,
};

struct tb_xdp_header {
	struct tb_xdomain_header xd_hdr;
	uuid_t uuid;
	u32 type;
};

struct tb_xdp_uuid {
	struct tb_xdp_header hdr;
};

struct tb_xdp_uuid_response {
	struct tb_xdp_header hdr;
	uuid_t src_uuid;
	u32 src_route_hi;
	u32 src_route_lo;
};

struct tb_xdp_properties {
	struct tb_xdp_header hdr;
	uuid_t src_uuid;
	uuid_t dst_uuid;
	u16 offset;
	u16 reserved;
};

struct tb_xdp_properties_response {
	struct tb_xdp_header hdr;
	uuid_t src_uuid;
	uuid_t dst_uuid;
	u16 offset;
	u16 data_length;
	u32 generation;
	u32 data[0];
};

/*
 * Max length of data array single XDomain property response is allowed
 * to carry.
 */
#define TB_XDP_PROPERTIES_MAX_DATA_LENGTH	\
	(((256 - 4 - sizeof(struct tb_xdp_properties_response))) / 4)

/* Maximum size of the total property block in dwords we allow */
#define TB_XDP_PROPERTIES_MAX_LENGTH		500

struct tb_xdp_properties_changed {
	struct tb_xdp_header hdr;
	uuid_t src_uuid;
};

struct tb_xdp_properties_changed_response {
	struct tb_xdp_header hdr;
};

enum tb_xdp_error {
	ERROR_SUCCESS,
	ERROR_UNKNOWN_PACKET,
	ERROR_UNKNOWN_DOMAIN,
	ERROR_NOT_SUPPORTED,
	ERROR_NOT_READY,
};

struct tb_xdp_error_response {
	struct tb_xdp_header hdr;
	u32 error;
};

#endif
