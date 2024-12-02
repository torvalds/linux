/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */
#ifndef __OCTEP_CTRL_NET_H__
#define __OCTEP_CTRL_NET_H__

/* Supported commands */
enum octep_ctrl_net_cmd {
	OCTEP_CTRL_NET_CMD_GET = 0,
	OCTEP_CTRL_NET_CMD_SET,
};

/* Supported states */
enum octep_ctrl_net_state {
	OCTEP_CTRL_NET_STATE_DOWN = 0,
	OCTEP_CTRL_NET_STATE_UP,
};

/* Supported replies */
enum octep_ctrl_net_reply {
	OCTEP_CTRL_NET_REPLY_OK = 0,
	OCTEP_CTRL_NET_REPLY_GENERIC_FAIL,
	OCTEP_CTRL_NET_REPLY_INVALID_PARAM,
};

/* Supported host to fw commands */
enum octep_ctrl_net_h2f_cmd {
	OCTEP_CTRL_NET_H2F_CMD_INVALID = 0,
	OCTEP_CTRL_NET_H2F_CMD_MTU,
	OCTEP_CTRL_NET_H2F_CMD_MAC,
	OCTEP_CTRL_NET_H2F_CMD_GET_IF_STATS,
	OCTEP_CTRL_NET_H2F_CMD_GET_XSTATS,
	OCTEP_CTRL_NET_H2F_CMD_GET_Q_STATS,
	OCTEP_CTRL_NET_H2F_CMD_LINK_STATUS,
	OCTEP_CTRL_NET_H2F_CMD_RX_STATE,
	OCTEP_CTRL_NET_H2F_CMD_LINK_INFO,
};

/* Supported fw to host commands */
enum octep_ctrl_net_f2h_cmd {
	OCTEP_CTRL_NET_F2H_CMD_INVALID = 0,
	OCTEP_CTRL_NET_F2H_CMD_LINK_STATUS,
};

struct octep_ctrl_net_req_hdr {
	/* sender id */
	u16 sender;
	/* receiver id */
	u16 receiver;
	/* octep_ctrl_net_h2t_cmd */
	u16 cmd;
	/* reserved */
	u16 rsvd0;
};

/* get/set mtu request */
struct octep_ctrl_net_h2f_req_cmd_mtu {
	/* enum octep_ctrl_net_cmd */
	u16 cmd;
	/* 0-65535 */
	u16 val;
};

/* get/set mac request */
struct octep_ctrl_net_h2f_req_cmd_mac {
	/* enum octep_ctrl_net_cmd */
	u16 cmd;
	/* xx:xx:xx:xx:xx:xx */
	u8 addr[ETH_ALEN];
};

/* get if_stats, xstats, q_stats request */
struct octep_ctrl_net_h2f_req_cmd_get_stats {
	/* offset into barmem where fw should copy over stats */
	u32 offset;
};

/* get/set link state, rx state */
struct octep_ctrl_net_h2f_req_cmd_state {
	/* enum octep_ctrl_net_cmd */
	u16 cmd;
	/* enum octep_ctrl_net_state */
	u16 state;
};

/* link info */
struct octep_ctrl_net_link_info {
	/* Bitmap of Supported link speeds/modes */
	u64 supported_modes;
	/* Bitmap of Advertised link speeds/modes */
	u64 advertised_modes;
	/* Autonegotation state; bit 0=disabled; bit 1=enabled */
	u8 autoneg;
	/* Pause frames setting. bit 0=disabled; bit 1=enabled */
	u8 pause;
	/* Negotiated link speed in Mbps */
	u32 speed;
};

/* get/set link info */
struct octep_ctrl_net_h2f_req_cmd_link_info {
	/* enum octep_ctrl_net_cmd */
	u16 cmd;
	/* struct octep_ctrl_net_link_info */
	struct octep_ctrl_net_link_info info;
};

/* Host to fw request data */
struct octep_ctrl_net_h2f_req {
	struct octep_ctrl_net_req_hdr hdr;
	union {
		struct octep_ctrl_net_h2f_req_cmd_mtu mtu;
		struct octep_ctrl_net_h2f_req_cmd_mac mac;
		struct octep_ctrl_net_h2f_req_cmd_get_stats get_stats;
		struct octep_ctrl_net_h2f_req_cmd_state link;
		struct octep_ctrl_net_h2f_req_cmd_state rx;
		struct octep_ctrl_net_h2f_req_cmd_link_info link_info;
	};
} __packed;

struct octep_ctrl_net_resp_hdr {
	/* sender id */
	u16 sender;
	/* receiver id */
	u16 receiver;
	/* octep_ctrl_net_h2t_cmd */
	u16 cmd;
	/* octep_ctrl_net_reply */
	u16 reply;
};

/* get mtu response */
struct octep_ctrl_net_h2f_resp_cmd_mtu {
	/* 0-65535 */
	u16 val;
};

/* get mac response */
struct octep_ctrl_net_h2f_resp_cmd_mac {
	/* xx:xx:xx:xx:xx:xx */
	u8 addr[ETH_ALEN];
};

/* get link state, rx state response */
struct octep_ctrl_net_h2f_resp_cmd_state {
	/* enum octep_ctrl_net_state */
	u16 state;
};

/* Host to fw response data */
struct octep_ctrl_net_h2f_resp {
	struct octep_ctrl_net_resp_hdr hdr;
	union {
		struct octep_ctrl_net_h2f_resp_cmd_mtu mtu;
		struct octep_ctrl_net_h2f_resp_cmd_mac mac;
		struct octep_ctrl_net_h2f_resp_cmd_state link;
		struct octep_ctrl_net_h2f_resp_cmd_state rx;
		struct octep_ctrl_net_link_info link_info;
	};
} __packed;

/* link state notofication */
struct octep_ctrl_net_f2h_req_cmd_state {
	/* enum octep_ctrl_net_state */
	u16 state;
};

/* Fw to host request data */
struct octep_ctrl_net_f2h_req {
	struct octep_ctrl_net_req_hdr hdr;
	union {
		struct octep_ctrl_net_f2h_req_cmd_state link;
	};
};

/* Fw to host response data */
struct octep_ctrl_net_f2h_resp {
	struct octep_ctrl_net_resp_hdr hdr;
};

/* Size of host to fw octep_ctrl_mbox queue element */
union octep_ctrl_net_h2f_data_sz {
	struct octep_ctrl_net_h2f_req h2f_req;
	struct octep_ctrl_net_h2f_resp h2f_resp;
};

/* Size of fw to host octep_ctrl_mbox queue element */
union octep_ctrl_net_f2h_data_sz {
	struct octep_ctrl_net_f2h_req f2h_req;
	struct octep_ctrl_net_f2h_resp f2h_resp;
};

/* size of host to fw data in words */
#define OCTEP_CTRL_NET_H2F_DATA_SZW		((sizeof(union octep_ctrl_net_h2f_data_sz)) / \
						 (sizeof(unsigned long)))

/* size of fw to host data in words */
#define OCTEP_CTRL_NET_F2H_DATA_SZW		((sizeof(union octep_ctrl_net_f2h_data_sz)) / \
						 (sizeof(unsigned long)))

/* size in words of get/set mtu request */
#define OCTEP_CTRL_NET_H2F_MTU_REQ_SZW			2
/* size in words of get/set mac request */
#define OCTEP_CTRL_NET_H2F_MAC_REQ_SZW			2
/* size in words of get stats request */
#define OCTEP_CTRL_NET_H2F_GET_STATS_REQ_SZW		2
/* size in words of get/set state request */
#define OCTEP_CTRL_NET_H2F_STATE_REQ_SZW		2
/* size in words of get/set link info request */
#define OCTEP_CTRL_NET_H2F_LINK_INFO_REQ_SZW		4

/* size in words of get mtu response */
#define OCTEP_CTRL_NET_H2F_GET_MTU_RESP_SZW		2
/* size in words of set mtu response */
#define OCTEP_CTRL_NET_H2F_SET_MTU_RESP_SZW		1
/* size in words of get mac response */
#define OCTEP_CTRL_NET_H2F_GET_MAC_RESP_SZW		2
/* size in words of set mac response */
#define OCTEP_CTRL_NET_H2F_SET_MAC_RESP_SZW		1
/* size in words of get state request */
#define OCTEP_CTRL_NET_H2F_GET_STATE_RESP_SZW		2
/* size in words of set state request */
#define OCTEP_CTRL_NET_H2F_SET_STATE_RESP_SZW		1
/* size in words of get link info request */
#define OCTEP_CTRL_NET_H2F_GET_LINK_INFO_RESP_SZW	4
/* size in words of set link info request */
#define OCTEP_CTRL_NET_H2F_SET_LINK_INFO_RESP_SZW	1

/** Get link status from firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 *
 * return value: link status 0=down, 1=up.
 */
int octep_get_link_status(struct octep_device *oct);

/** Set link status in firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param up: boolean status.
 */
void octep_set_link_status(struct octep_device *oct, bool up);

/** Set rx state in firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param up: boolean status.
 */
void octep_set_rx_state(struct octep_device *oct, bool up);

/** Get mac address from firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param addr: non-null pointer to mac address.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_get_mac_addr(struct octep_device *oct, u8 *addr);

/** Set mac address in firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param addr: non-null pointer to mac address.
 */
int octep_set_mac_addr(struct octep_device *oct, u8 *addr);

/** Set mtu in firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param mtu: mtu.
 */
int octep_set_mtu(struct octep_device *oct, int mtu);

/** Get interface statistics from firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_get_if_stats(struct octep_device *oct);

/** Get link info from firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_get_link_info(struct octep_device *oct);

/** Set link info in firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 */
int octep_set_link_info(struct octep_device *oct, struct octep_iface_link_info *link_info);

#endif /* __OCTEP_CTRL_NET_H__ */
