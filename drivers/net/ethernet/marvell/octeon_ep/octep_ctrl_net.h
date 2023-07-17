/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */
#ifndef __OCTEP_CTRL_NET_H__
#define __OCTEP_CTRL_NET_H__

#define OCTEP_CTRL_NET_INVALID_VFID	(-1)

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

union octep_ctrl_net_req_hdr {
	u64 words[1];
	struct {
		/* sender id */
		u16 sender;
		/* receiver id */
		u16 receiver;
		/* octep_ctrl_net_h2t_cmd */
		u16 cmd;
		/* reserved */
		u16 rsvd0;
	} s;
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
	union octep_ctrl_net_req_hdr hdr;
	union {
		struct octep_ctrl_net_h2f_req_cmd_mtu mtu;
		struct octep_ctrl_net_h2f_req_cmd_mac mac;
		struct octep_ctrl_net_h2f_req_cmd_state link;
		struct octep_ctrl_net_h2f_req_cmd_state rx;
		struct octep_ctrl_net_h2f_req_cmd_link_info link_info;
	};
} __packed;

union octep_ctrl_net_resp_hdr {
	u64 words[1];
	struct {
		/* sender id */
		u16 sender;
		/* receiver id */
		u16 receiver;
		/* octep_ctrl_net_h2t_cmd */
		u16 cmd;
		/* octep_ctrl_net_reply */
		u16 reply;
	} s;
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

/* get if_stats, xstats, q_stats request */
struct octep_ctrl_net_h2f_resp_cmd_get_stats {
	struct octep_iface_rx_stats rx_stats;
	struct octep_iface_tx_stats tx_stats;
};

/* get link state, rx state response */
struct octep_ctrl_net_h2f_resp_cmd_state {
	/* enum octep_ctrl_net_state */
	u16 state;
};

/* Host to fw response data */
struct octep_ctrl_net_h2f_resp {
	union octep_ctrl_net_resp_hdr hdr;
	union {
		struct octep_ctrl_net_h2f_resp_cmd_mtu mtu;
		struct octep_ctrl_net_h2f_resp_cmd_mac mac;
		struct octep_ctrl_net_h2f_resp_cmd_get_stats if_stats;
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
	union octep_ctrl_net_req_hdr hdr;
	union {
		struct octep_ctrl_net_f2h_req_cmd_state link;
	};
};

/* Fw to host response data */
struct octep_ctrl_net_f2h_resp {
	union octep_ctrl_net_resp_hdr hdr;
};

/* Max data size to be transferred over mbox */
union octep_ctrl_net_max_data {
	struct octep_ctrl_net_h2f_req h2f_req;
	struct octep_ctrl_net_h2f_resp h2f_resp;
	struct octep_ctrl_net_f2h_req f2h_req;
	struct octep_ctrl_net_f2h_resp f2h_resp;
};

struct octep_ctrl_net_wait_data {
	struct list_head list;
	int done;
	struct octep_ctrl_mbox_msg msg;
	union {
		struct octep_ctrl_net_h2f_req req;
		struct octep_ctrl_net_h2f_resp resp;
	} data;
};

/** Initialize data for ctrl net.
 *
 * @param oct: non-null pointer to struct octep_device.
 *
 * return value: 0 on success, -errno on error.
 */
int octep_ctrl_net_init(struct octep_device *oct);

/** Get link status from firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param vfid: Index of virtual function.
 *
 * return value: link status 0=down, 1=up.
 */
int octep_ctrl_net_get_link_status(struct octep_device *oct, int vfid);

/** Set link status in firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param vfid: Index of virtual function.
 * @param up: boolean status.
 * @param wait_for_response: poll for response.
 *
 * return value: 0 on success, -errno on failure
 */
int octep_ctrl_net_set_link_status(struct octep_device *oct, int vfid, bool up,
				   bool wait_for_response);

/** Set rx state in firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param vfid: Index of virtual function.
 * @param up: boolean status.
 * @param wait_for_response: poll for response.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_net_set_rx_state(struct octep_device *oct, int vfid, bool up,
				bool wait_for_response);

/** Get mac address from firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param vfid: Index of virtual function.
 * @param addr: non-null pointer to mac address.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_net_get_mac_addr(struct octep_device *oct, int vfid, u8 *addr);

/** Set mac address in firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param vfid: Index of virtual function.
 * @param addr: non-null pointer to mac address.
 * @param wait_for_response: poll for response.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_net_set_mac_addr(struct octep_device *oct, int vfid, u8 *addr,
				bool wait_for_response);

/** Set mtu in firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param vfid: Index of virtual function.
 * @param mtu: mtu.
 * @param wait_for_response: poll for response.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_net_set_mtu(struct octep_device *oct, int vfid, int mtu,
			   bool wait_for_response);

/** Get interface statistics from firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param vfid: Index of virtual function.
 * @param rx_stats: non-null pointer struct octep_iface_rx_stats.
 * @param tx_stats: non-null pointer struct octep_iface_tx_stats.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_net_get_if_stats(struct octep_device *oct, int vfid,
				struct octep_iface_rx_stats *rx_stats,
				struct octep_iface_tx_stats *tx_stats);

/** Get link info from firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param vfid: Index of virtual function.
 * @param link_info: non-null pointer to struct octep_iface_link_info.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_net_get_link_info(struct octep_device *oct, int vfid,
				 struct octep_iface_link_info *link_info);

/** Set link info in firmware.
 *
 * @param oct: non-null pointer to struct octep_device.
 * @param vfid: Index of virtual function.
 * @param link_info: non-null pointer to struct octep_iface_link_info.
 * @param wait_for_response: poll for response.
 *
 * return value: 0 on success, -errno on failure.
 */
int octep_ctrl_net_set_link_info(struct octep_device *oct,
				 int vfid,
				 struct octep_iface_link_info *link_info,
				 bool wait_for_response);

/** Poll for firmware messages and process them.
 *
 * @param oct: non-null pointer to struct octep_device.
 */
void octep_ctrl_net_recv_fw_messages(struct octep_device *oct);

/** Uninitialize data for ctrl net.
 *
 * @param oct: non-null pointer to struct octep_device.
 *
 * return value: 0 on success, -errno on error.
 */
int octep_ctrl_net_uninit(struct octep_device *oct);

#endif /* __OCTEP_CTRL_NET_H__ */
