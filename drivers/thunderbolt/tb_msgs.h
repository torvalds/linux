/*
 * Thunderbolt control channel messages
 *
 * Copyright (C) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2017, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _TB_MSGS
#define _TB_MSGS

#include <linux/types.h>
#include <linux/uuid.h>

enum tb_cfg_pkg_type {
	TB_CFG_PKG_READ = 1,
	TB_CFG_PKG_WRITE = 2,
	TB_CFG_PKG_ERROR = 3,
	TB_CFG_PKG_NOTIFY_ACK = 4,
	TB_CFG_PKG_EVENT = 5,
	TB_CFG_PKG_XDOMAIN_REQ = 6,
	TB_CFG_PKG_XDOMAIN_RESP = 7,
	TB_CFG_PKG_OVERRIDE = 8,
	TB_CFG_PKG_RESET = 9,
	TB_CFG_PKG_ICM_EVENT = 10,
	TB_CFG_PKG_ICM_CMD = 11,
	TB_CFG_PKG_ICM_RESP = 12,
	TB_CFG_PKG_PREPARE_TO_SLEEP = 0xd,

};

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
};

enum icm_event_code {
	ICM_EVENT_DEVICE_CONNECTED = 3,
	ICM_EVENT_DEVICE_DISCONNECTED = 4,
};

struct icm_pkg_header {
	u8 code;
	u8 flags;
	u8 packet_id;
	u8 total_packets;
} __packed;

#define ICM_FLAGS_ERROR			BIT(0)
#define ICM_FLAGS_NO_KEY		BIT(1)
#define ICM_FLAGS_SLEVEL_SHIFT		3
#define ICM_FLAGS_SLEVEL_MASK		GENMASK(4, 3)

struct icm_pkg_driver_ready {
	struct icm_pkg_header hdr;
} __packed;

struct icm_pkg_driver_ready_response {
	struct icm_pkg_header hdr;
	u8 romver;
	u8 ramver;
	u16 security_level;
} __packed;

/* Falcon Ridge & Alpine Ridge common messages */

struct icm_fr_pkg_get_topology {
	struct icm_pkg_header hdr;
} __packed;

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
} __packed;

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
} __packed;

#define ICM_LINK_INFO_LINK_MASK		0x7
#define ICM_LINK_INFO_DEPTH_SHIFT	4
#define ICM_LINK_INFO_DEPTH_MASK	GENMASK(7, 4)
#define ICM_LINK_INFO_APPROVED		BIT(8)

struct icm_fr_pkg_approve_device {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u8 connection_key;
	u8 connection_id;
	u16 reserved;
} __packed;

struct icm_fr_event_device_disconnected {
	struct icm_pkg_header hdr;
	u16 reserved;
	u16 link_info;
} __packed;

struct icm_fr_pkg_add_device_key {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u8 connection_key;
	u8 connection_id;
	u16 reserved;
	u32 key[8];
} __packed;

struct icm_fr_pkg_add_device_key_response {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u8 connection_key;
	u8 connection_id;
	u16 reserved;
} __packed;

struct icm_fr_pkg_challenge_device {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u8 connection_key;
	u8 connection_id;
	u16 reserved;
	u32 challenge[8];
} __packed;

struct icm_fr_pkg_challenge_device_response {
	struct icm_pkg_header hdr;
	uuid_t ep_uuid;
	u8 connection_key;
	u8 connection_id;
	u16 reserved;
	u32 challenge[8];
	u32 response[8];
} __packed;

/* Alpine Ridge only messages */

struct icm_ar_pkg_get_route {
	struct icm_pkg_header hdr;
	u16 reserved;
	u16 link_info;
} __packed;

struct icm_ar_pkg_get_route_response {
	struct icm_pkg_header hdr;
	u16 reserved;
	u16 link_info;
	u32 route_hi;
	u32 route_lo;
} __packed;

#endif
