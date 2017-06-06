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

#endif
