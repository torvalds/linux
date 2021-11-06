// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved */

#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/ethtool.h>
#include <linux/list.h>

#include "prestera.h"
#include "prestera_hw.h"
#include "prestera_acl.h"

#define PRESTERA_SWITCH_INIT_TIMEOUT_MS (30 * 1000)

#define PRESTERA_MIN_MTU 64

enum prestera_cmd_type_t {
	PRESTERA_CMD_TYPE_SWITCH_INIT = 0x1,
	PRESTERA_CMD_TYPE_SWITCH_ATTR_SET = 0x2,

	PRESTERA_CMD_TYPE_PORT_ATTR_SET = 0x100,
	PRESTERA_CMD_TYPE_PORT_ATTR_GET = 0x101,
	PRESTERA_CMD_TYPE_PORT_INFO_GET = 0x110,

	PRESTERA_CMD_TYPE_VLAN_CREATE = 0x200,
	PRESTERA_CMD_TYPE_VLAN_DELETE = 0x201,
	PRESTERA_CMD_TYPE_VLAN_PORT_SET = 0x202,
	PRESTERA_CMD_TYPE_VLAN_PVID_SET = 0x203,

	PRESTERA_CMD_TYPE_FDB_ADD = 0x300,
	PRESTERA_CMD_TYPE_FDB_DELETE = 0x301,
	PRESTERA_CMD_TYPE_FDB_FLUSH_PORT = 0x310,
	PRESTERA_CMD_TYPE_FDB_FLUSH_VLAN = 0x311,
	PRESTERA_CMD_TYPE_FDB_FLUSH_PORT_VLAN = 0x312,

	PRESTERA_CMD_TYPE_BRIDGE_CREATE = 0x400,
	PRESTERA_CMD_TYPE_BRIDGE_DELETE = 0x401,
	PRESTERA_CMD_TYPE_BRIDGE_PORT_ADD = 0x402,
	PRESTERA_CMD_TYPE_BRIDGE_PORT_DELETE = 0x403,

	PRESTERA_CMD_TYPE_ACL_RULE_ADD = 0x500,
	PRESTERA_CMD_TYPE_ACL_RULE_DELETE = 0x501,
	PRESTERA_CMD_TYPE_ACL_RULE_STATS_GET = 0x510,
	PRESTERA_CMD_TYPE_ACL_RULESET_CREATE = 0x520,
	PRESTERA_CMD_TYPE_ACL_RULESET_DELETE = 0x521,
	PRESTERA_CMD_TYPE_ACL_PORT_BIND = 0x530,
	PRESTERA_CMD_TYPE_ACL_PORT_UNBIND = 0x531,

	PRESTERA_CMD_TYPE_RXTX_INIT = 0x800,

	PRESTERA_CMD_TYPE_LAG_MEMBER_ADD = 0x900,
	PRESTERA_CMD_TYPE_LAG_MEMBER_DELETE = 0x901,
	PRESTERA_CMD_TYPE_LAG_MEMBER_ENABLE = 0x902,
	PRESTERA_CMD_TYPE_LAG_MEMBER_DISABLE = 0x903,

	PRESTERA_CMD_TYPE_STP_PORT_SET = 0x1000,

	PRESTERA_CMD_TYPE_SPAN_GET = 0x1100,
	PRESTERA_CMD_TYPE_SPAN_BIND = 0x1101,
	PRESTERA_CMD_TYPE_SPAN_UNBIND = 0x1102,
	PRESTERA_CMD_TYPE_SPAN_RELEASE = 0x1103,

	PRESTERA_CMD_TYPE_CPU_CODE_COUNTERS_GET = 0x2000,

	PRESTERA_CMD_TYPE_ACK = 0x10000,
	PRESTERA_CMD_TYPE_MAX
};

enum {
	PRESTERA_CMD_PORT_ATTR_ADMIN_STATE = 1,
	PRESTERA_CMD_PORT_ATTR_MTU = 3,
	PRESTERA_CMD_PORT_ATTR_MAC = 4,
	PRESTERA_CMD_PORT_ATTR_SPEED = 5,
	PRESTERA_CMD_PORT_ATTR_ACCEPT_FRAME_TYPE = 6,
	PRESTERA_CMD_PORT_ATTR_LEARNING = 7,
	PRESTERA_CMD_PORT_ATTR_FLOOD = 8,
	PRESTERA_CMD_PORT_ATTR_CAPABILITY = 9,
	PRESTERA_CMD_PORT_ATTR_PHY_MODE = 12,
	PRESTERA_CMD_PORT_ATTR_TYPE = 13,
	PRESTERA_CMD_PORT_ATTR_STATS = 17,
	PRESTERA_CMD_PORT_ATTR_MAC_AUTONEG_RESTART = 18,
	PRESTERA_CMD_PORT_ATTR_PHY_AUTONEG_RESTART = 19,
	PRESTERA_CMD_PORT_ATTR_MAC_MODE = 22,
};

enum {
	PRESTERA_CMD_SWITCH_ATTR_MAC = 1,
	PRESTERA_CMD_SWITCH_ATTR_AGEING = 2,
};

enum {
	PRESTERA_CMD_ACK_OK,
	PRESTERA_CMD_ACK_FAILED,

	PRESTERA_CMD_ACK_MAX
};

enum {
	PRESTERA_PORT_TP_NA,
	PRESTERA_PORT_TP_MDI,
	PRESTERA_PORT_TP_MDIX,
	PRESTERA_PORT_TP_AUTO,
};

enum {
	PRESTERA_PORT_FLOOD_TYPE_UC = 0,
	PRESTERA_PORT_FLOOD_TYPE_MC = 1,
};

enum {
	PRESTERA_PORT_GOOD_OCTETS_RCV_CNT,
	PRESTERA_PORT_BAD_OCTETS_RCV_CNT,
	PRESTERA_PORT_MAC_TRANSMIT_ERR_CNT,
	PRESTERA_PORT_BRDC_PKTS_RCV_CNT,
	PRESTERA_PORT_MC_PKTS_RCV_CNT,
	PRESTERA_PORT_PKTS_64L_CNT,
	PRESTERA_PORT_PKTS_65TO127L_CNT,
	PRESTERA_PORT_PKTS_128TO255L_CNT,
	PRESTERA_PORT_PKTS_256TO511L_CNT,
	PRESTERA_PORT_PKTS_512TO1023L_CNT,
	PRESTERA_PORT_PKTS_1024TOMAXL_CNT,
	PRESTERA_PORT_EXCESSIVE_COLLISIONS_CNT,
	PRESTERA_PORT_MC_PKTS_SENT_CNT,
	PRESTERA_PORT_BRDC_PKTS_SENT_CNT,
	PRESTERA_PORT_FC_SENT_CNT,
	PRESTERA_PORT_GOOD_FC_RCV_CNT,
	PRESTERA_PORT_DROP_EVENTS_CNT,
	PRESTERA_PORT_UNDERSIZE_PKTS_CNT,
	PRESTERA_PORT_FRAGMENTS_PKTS_CNT,
	PRESTERA_PORT_OVERSIZE_PKTS_CNT,
	PRESTERA_PORT_JABBER_PKTS_CNT,
	PRESTERA_PORT_MAC_RCV_ERROR_CNT,
	PRESTERA_PORT_BAD_CRC_CNT,
	PRESTERA_PORT_COLLISIONS_CNT,
	PRESTERA_PORT_LATE_COLLISIONS_CNT,
	PRESTERA_PORT_GOOD_UC_PKTS_RCV_CNT,
	PRESTERA_PORT_GOOD_UC_PKTS_SENT_CNT,
	PRESTERA_PORT_MULTIPLE_PKTS_SENT_CNT,
	PRESTERA_PORT_DEFERRED_PKTS_SENT_CNT,
	PRESTERA_PORT_GOOD_OCTETS_SENT_CNT,

	PRESTERA_PORT_CNT_MAX
};

enum {
	PRESTERA_FC_NONE,
	PRESTERA_FC_SYMMETRIC,
	PRESTERA_FC_ASYMMETRIC,
	PRESTERA_FC_SYMM_ASYMM,
};

enum {
	PRESTERA_HW_FDB_ENTRY_TYPE_REG_PORT = 0,
	PRESTERA_HW_FDB_ENTRY_TYPE_LAG = 1,
	PRESTERA_HW_FDB_ENTRY_TYPE_MAX = 2,
};

struct prestera_fw_event_handler {
	struct list_head list;
	struct rcu_head rcu;
	enum prestera_event_type type;
	prestera_event_cb_t func;
	void *arg;
};

struct prestera_msg_cmd {
	__le32 type;
};

struct prestera_msg_ret {
	struct prestera_msg_cmd cmd;
	__le32 status;
};

struct prestera_msg_common_req {
	struct prestera_msg_cmd cmd;
};

struct prestera_msg_common_resp {
	struct prestera_msg_ret ret;
};

union prestera_msg_switch_param {
	u8 mac[ETH_ALEN];
	__le32 ageing_timeout_ms;
} __packed;

struct prestera_msg_switch_attr_req {
	struct prestera_msg_cmd cmd;
	__le32 attr;
	union prestera_msg_switch_param param;
};

struct prestera_msg_switch_init_resp {
	struct prestera_msg_ret ret;
	__le32 port_count;
	__le32 mtu_max;
	u8  switch_id;
	u8  lag_max;
	u8  lag_member_max;
	__le32 size_tbl_router_nexthop;
} __packed __aligned(4);

struct prestera_msg_event_port_param {
	union {
		struct {
			u8 oper;
			__le32 mode;
			__le32 speed;
			u8 duplex;
			u8 fc;
			u8 fec;
		} __packed mac;
		struct {
			u8 mdix;
			__le64 lmode_bmap;
			u8 fc;
		} __packed phy;
	} __packed;
} __packed __aligned(4);

struct prestera_msg_port_cap_param {
	__le64 link_mode;
	u8  type;
	u8  fec;
	u8  fc;
	u8  transceiver;
};

struct prestera_msg_port_flood_param {
	u8 type;
	u8 enable;
};

union prestera_msg_port_param {
	u8 admin_state;
	u8 oper_state;
	__le32 mtu;
	u8 mac[ETH_ALEN];
	u8 accept_frm_type;
	__le32 speed;
	u8 learning;
	u8 flood;
	__le32 link_mode;
	u8 type;
	u8 duplex;
	u8 fec;
	u8 fc;

	union {
		struct {
			u8 admin:1;
			u8 fc;
			u8 ap_enable;
			union {
				struct {
					__le32 mode;
					u8  inband:1;
					__le32 speed;
					u8  duplex;
					u8  fec;
					u8  fec_supp;
				} __packed reg_mode;
				struct {
					__le32 mode;
					__le32 speed;
					u8  fec;
					u8  fec_supp;
				} __packed ap_modes[PRESTERA_AP_PORT_MAX];
			} __packed;
		} __packed mac;
		struct {
			u8 admin:1;
			u8 adv_enable;
			__le64 modes;
			__le32 mode;
			u8 mdix;
		} __packed phy;
	} __packed link;

	struct prestera_msg_port_cap_param cap;
	struct prestera_msg_port_flood_param flood_ext;
	struct prestera_msg_event_port_param link_evt;
} __packed;

struct prestera_msg_port_attr_req {
	struct prestera_msg_cmd cmd;
	__le32 attr;
	__le32 port;
	__le32 dev;
	union prestera_msg_port_param param;
} __packed __aligned(4);


struct prestera_msg_port_attr_resp {
	struct prestera_msg_ret ret;
	union prestera_msg_port_param param;
} __packed __aligned(4);


struct prestera_msg_port_stats_resp {
	struct prestera_msg_ret ret;
	__le64 stats[PRESTERA_PORT_CNT_MAX];
};

struct prestera_msg_port_info_req {
	struct prestera_msg_cmd cmd;
	__le32 port;
};

struct prestera_msg_port_info_resp {
	struct prestera_msg_ret ret;
	__le32 hw_id;
	__le32 dev_id;
	__le16 fp_id;
};

struct prestera_msg_vlan_req {
	struct prestera_msg_cmd cmd;
	__le32 port;
	__le32 dev;
	__le16 vid;
	u8  is_member;
	u8  is_tagged;
};

struct prestera_msg_fdb_req {
	struct prestera_msg_cmd cmd;
	u8 dest_type;
	union {
		struct {
			__le32 port;
			__le32 dev;
		};
		__le16 lag_id;
	} dest;
	u8  mac[ETH_ALEN];
	__le16 vid;
	u8  dynamic;
	__le32 flush_mode;
} __packed __aligned(4);

struct prestera_msg_bridge_req {
	struct prestera_msg_cmd cmd;
	__le32 port;
	__le32 dev;
	__le16 bridge;
};

struct prestera_msg_bridge_resp {
	struct prestera_msg_ret ret;
	__le16 bridge;
};

struct prestera_msg_acl_action {
	__le32 id;
	__le32 reserved[5];
};

struct prestera_msg_acl_match {
	__le32 type;
	union {
		struct {
			u8 key;
			u8 mask;
		} __packed u8;
		struct {
			__le16 key;
			__le16 mask;
		} u16;
		struct {
			__le32 key;
			__le32 mask;
		} u32;
		struct {
			__le64 key;
			__le64 mask;
		} u64;
		struct {
			u8 key[ETH_ALEN];
			u8 mask[ETH_ALEN];
		} __packed mac;
	} keymask;
};

struct prestera_msg_acl_rule_req {
	struct prestera_msg_cmd cmd;
	__le32 id;
	__le32 priority;
	__le16 ruleset_id;
	u8 n_actions;
	u8 n_matches;
};

struct prestera_msg_acl_rule_resp {
	struct prestera_msg_ret ret;
	__le32 id;
};

struct prestera_msg_acl_rule_stats_resp {
	struct prestera_msg_ret ret;
	__le64 packets;
	__le64 bytes;
};

struct prestera_msg_acl_ruleset_bind_req {
	struct prestera_msg_cmd cmd;
	__le32 port;
	__le32 dev;
	__le16 ruleset_id;
};

struct prestera_msg_acl_ruleset_req {
	struct prestera_msg_cmd cmd;
	__le16 id;
};

struct prestera_msg_acl_ruleset_resp {
	struct prestera_msg_ret ret;
	__le16 id;
};

struct prestera_msg_span_req {
	struct prestera_msg_cmd cmd;
	__le32 port;
	__le32 dev;
	u8 id;
};

struct prestera_msg_span_resp {
	struct prestera_msg_ret ret;
	u8 id;
};

struct prestera_msg_stp_req {
	struct prestera_msg_cmd cmd;
	__le32 port;
	__le32 dev;
	__le16 vid;
	u8  state;
};

struct prestera_msg_rxtx_req {
	struct prestera_msg_cmd cmd;
	u8 use_sdma;
};

struct prestera_msg_rxtx_resp {
	struct prestera_msg_ret ret;
	__le32 map_addr;
};

struct prestera_msg_lag_req {
	struct prestera_msg_cmd cmd;
	__le32 port;
	__le32 dev;
	__le16 lag_id;
};

struct prestera_msg_cpu_code_counter_req {
	struct prestera_msg_cmd cmd;
	u8 counter_type;
	u8 code;
};

struct mvsw_msg_cpu_code_counter_ret {
	struct prestera_msg_ret ret;
	__le64 packet_count;
};

struct prestera_msg_event {
	__le16 type;
	__le16 id;
};

struct prestera_msg_event_port {
	struct prestera_msg_event id;
	__le32 port_id;
	struct prestera_msg_event_port_param param;
};

union prestera_msg_event_fdb_param {
	u8 mac[ETH_ALEN];
};

struct prestera_msg_event_fdb {
	struct prestera_msg_event id;
	u8 dest_type;
	union {
		__le32 port_id;
		__le16 lag_id;
	} dest;
	__le32 vid;
	union prestera_msg_event_fdb_param param;
} __packed __aligned(4);

static inline void prestera_hw_build_tests(void)
{
	/* check requests */
	BUILD_BUG_ON(sizeof(struct prestera_msg_common_req) != 4);
	BUILD_BUG_ON(sizeof(struct prestera_msg_switch_attr_req) != 16);
	BUILD_BUG_ON(sizeof(struct prestera_msg_port_attr_req) != 120);
	BUILD_BUG_ON(sizeof(struct prestera_msg_port_info_req) != 8);
	BUILD_BUG_ON(sizeof(struct prestera_msg_vlan_req) != 16);
	BUILD_BUG_ON(sizeof(struct prestera_msg_fdb_req) != 28);
	BUILD_BUG_ON(sizeof(struct prestera_msg_bridge_req) != 16);
	BUILD_BUG_ON(sizeof(struct prestera_msg_acl_rule_req) != 16);
	BUILD_BUG_ON(sizeof(struct prestera_msg_acl_ruleset_bind_req) != 16);
	BUILD_BUG_ON(sizeof(struct prestera_msg_acl_ruleset_req) != 8);
	BUILD_BUG_ON(sizeof(struct prestera_msg_span_req) != 16);
	BUILD_BUG_ON(sizeof(struct prestera_msg_stp_req) != 16);
	BUILD_BUG_ON(sizeof(struct prestera_msg_rxtx_req) != 8);
	BUILD_BUG_ON(sizeof(struct prestera_msg_lag_req) != 16);
	BUILD_BUG_ON(sizeof(struct prestera_msg_cpu_code_counter_req) != 8);

	/* check responses */
	BUILD_BUG_ON(sizeof(struct prestera_msg_common_resp) != 8);
	BUILD_BUG_ON(sizeof(struct prestera_msg_switch_init_resp) != 24);
	BUILD_BUG_ON(sizeof(struct prestera_msg_port_attr_resp) != 112);
	BUILD_BUG_ON(sizeof(struct prestera_msg_port_stats_resp) != 248);
	BUILD_BUG_ON(sizeof(struct prestera_msg_port_info_resp) != 20);
	BUILD_BUG_ON(sizeof(struct prestera_msg_bridge_resp) != 12);
	BUILD_BUG_ON(sizeof(struct prestera_msg_acl_rule_resp) != 12);
	BUILD_BUG_ON(sizeof(struct prestera_msg_acl_rule_stats_resp) != 24);
	BUILD_BUG_ON(sizeof(struct prestera_msg_acl_ruleset_resp) != 12);
	BUILD_BUG_ON(sizeof(struct prestera_msg_span_resp) != 12);
	BUILD_BUG_ON(sizeof(struct prestera_msg_rxtx_resp) != 12);

	/* check events */
	BUILD_BUG_ON(sizeof(struct prestera_msg_event_port) != 20);
	BUILD_BUG_ON(sizeof(struct prestera_msg_event_fdb) != 20);
}

static u8 prestera_hw_mdix_to_eth(u8 mode);
static void prestera_hw_remote_fc_to_eth(u8 fc, bool *pause, bool *asym_pause);

static int __prestera_cmd_ret(struct prestera_switch *sw,
			      enum prestera_cmd_type_t type,
			      struct prestera_msg_cmd *cmd, size_t clen,
			      struct prestera_msg_ret *ret, size_t rlen,
			      int waitms)
{
	struct prestera_device *dev = sw->dev;
	int err;

	cmd->type = __cpu_to_le32(type);

	err = dev->send_req(dev, 0, cmd, clen, ret, rlen, waitms);
	if (err)
		return err;

	if (__le32_to_cpu(ret->cmd.type) != PRESTERA_CMD_TYPE_ACK)
		return -EBADE;
	if (__le32_to_cpu(ret->status) != PRESTERA_CMD_ACK_OK)
		return -EINVAL;

	return 0;
}

static int prestera_cmd_ret(struct prestera_switch *sw,
			    enum prestera_cmd_type_t type,
			    struct prestera_msg_cmd *cmd, size_t clen,
			    struct prestera_msg_ret *ret, size_t rlen)
{
	return __prestera_cmd_ret(sw, type, cmd, clen, ret, rlen, 0);
}

static int prestera_cmd_ret_wait(struct prestera_switch *sw,
				 enum prestera_cmd_type_t type,
				 struct prestera_msg_cmd *cmd, size_t clen,
				 struct prestera_msg_ret *ret, size_t rlen,
				 int waitms)
{
	return __prestera_cmd_ret(sw, type, cmd, clen, ret, rlen, waitms);
}

static int prestera_cmd(struct prestera_switch *sw,
			enum prestera_cmd_type_t type,
			struct prestera_msg_cmd *cmd, size_t clen)
{
	struct prestera_msg_common_resp resp;

	return prestera_cmd_ret(sw, type, cmd, clen, &resp.ret, sizeof(resp));
}

static int prestera_fw_parse_port_evt(void *msg, struct prestera_event *evt)
{
	struct prestera_msg_event_port *hw_evt;

	hw_evt = (struct prestera_msg_event_port *)msg;

	evt->port_evt.port_id = __le32_to_cpu(hw_evt->port_id);

	if (evt->id == PRESTERA_PORT_EVENT_MAC_STATE_CHANGED) {
		evt->port_evt.data.mac.oper = hw_evt->param.mac.oper;
		evt->port_evt.data.mac.mode =
			__le32_to_cpu(hw_evt->param.mac.mode);
		evt->port_evt.data.mac.speed =
			__le32_to_cpu(hw_evt->param.mac.speed);
		evt->port_evt.data.mac.duplex = hw_evt->param.mac.duplex;
		evt->port_evt.data.mac.fc = hw_evt->param.mac.fc;
		evt->port_evt.data.mac.fec = hw_evt->param.mac.fec;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int prestera_fw_parse_fdb_evt(void *msg, struct prestera_event *evt)
{
	struct prestera_msg_event_fdb *hw_evt = msg;

	switch (hw_evt->dest_type) {
	case PRESTERA_HW_FDB_ENTRY_TYPE_REG_PORT:
		evt->fdb_evt.type = PRESTERA_FDB_ENTRY_TYPE_REG_PORT;
		evt->fdb_evt.dest.port_id = __le32_to_cpu(hw_evt->dest.port_id);
		break;
	case PRESTERA_HW_FDB_ENTRY_TYPE_LAG:
		evt->fdb_evt.type = PRESTERA_FDB_ENTRY_TYPE_LAG;
		evt->fdb_evt.dest.lag_id = __le16_to_cpu(hw_evt->dest.lag_id);
		break;
	default:
		return -EINVAL;
	}

	evt->fdb_evt.vid = __le32_to_cpu(hw_evt->vid);

	ether_addr_copy(evt->fdb_evt.data.mac, hw_evt->param.mac);

	return 0;
}

static struct prestera_fw_evt_parser {
	int (*func)(void *msg, struct prestera_event *evt);
} fw_event_parsers[PRESTERA_EVENT_TYPE_MAX] = {
	[PRESTERA_EVENT_TYPE_PORT] = { .func = prestera_fw_parse_port_evt },
	[PRESTERA_EVENT_TYPE_FDB] = { .func = prestera_fw_parse_fdb_evt },
};

static struct prestera_fw_event_handler *
__find_event_handler(const struct prestera_switch *sw,
		     enum prestera_event_type type)
{
	struct prestera_fw_event_handler *eh;

	list_for_each_entry_rcu(eh, &sw->event_handlers, list) {
		if (eh->type == type)
			return eh;
	}

	return NULL;
}

static int prestera_find_event_handler(const struct prestera_switch *sw,
				       enum prestera_event_type type,
				       struct prestera_fw_event_handler *eh)
{
	struct prestera_fw_event_handler *tmp;
	int err = 0;

	rcu_read_lock();
	tmp = __find_event_handler(sw, type);
	if (tmp)
		*eh = *tmp;
	else
		err = -ENOENT;
	rcu_read_unlock();

	return err;
}

static int prestera_evt_recv(struct prestera_device *dev, void *buf, size_t size)
{
	struct prestera_switch *sw = dev->priv;
	struct prestera_msg_event *msg = buf;
	struct prestera_fw_event_handler eh;
	struct prestera_event evt;
	u16 msg_type;
	int err;

	msg_type = __le16_to_cpu(msg->type);
	if (msg_type >= PRESTERA_EVENT_TYPE_MAX)
		return -EINVAL;
	if (!fw_event_parsers[msg_type].func)
		return -ENOENT;

	err = prestera_find_event_handler(sw, msg_type, &eh);
	if (err)
		return err;

	evt.id = __le16_to_cpu(msg->id);

	err = fw_event_parsers[msg_type].func(buf, &evt);
	if (err)
		return err;

	eh.func(sw, &evt, eh.arg);

	return 0;
}

static void prestera_pkt_recv(struct prestera_device *dev)
{
	struct prestera_switch *sw = dev->priv;
	struct prestera_fw_event_handler eh;
	struct prestera_event ev;
	int err;

	ev.id = PRESTERA_RXTX_EVENT_RCV_PKT;

	err = prestera_find_event_handler(sw, PRESTERA_EVENT_TYPE_RXTX, &eh);
	if (err)
		return;

	eh.func(sw, &ev, eh.arg);
}

static u8 prestera_hw_mdix_to_eth(u8 mode)
{
	switch (mode) {
	case PRESTERA_PORT_TP_MDI:
		return ETH_TP_MDI;
	case PRESTERA_PORT_TP_MDIX:
		return ETH_TP_MDI_X;
	case PRESTERA_PORT_TP_AUTO:
		return ETH_TP_MDI_AUTO;
	default:
		return ETH_TP_MDI_INVALID;
	}
}

static u8 prestera_hw_mdix_from_eth(u8 mode)
{
	switch (mode) {
	case ETH_TP_MDI:
		return PRESTERA_PORT_TP_MDI;
	case ETH_TP_MDI_X:
		return PRESTERA_PORT_TP_MDIX;
	case ETH_TP_MDI_AUTO:
		return PRESTERA_PORT_TP_AUTO;
	default:
		return PRESTERA_PORT_TP_NA;
	}
}

int prestera_hw_port_info_get(const struct prestera_port *port,
			      u32 *dev_id, u32 *hw_id, u16 *fp_id)
{
	struct prestera_msg_port_info_req req = {
		.port = __cpu_to_le32(port->id),
	};
	struct prestera_msg_port_info_resp resp;
	int err;

	err = prestera_cmd_ret(port->sw, PRESTERA_CMD_TYPE_PORT_INFO_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	*dev_id = __le32_to_cpu(resp.dev_id);
	*hw_id = __le32_to_cpu(resp.hw_id);
	*fp_id = __le16_to_cpu(resp.fp_id);

	return 0;
}

int prestera_hw_switch_mac_set(struct prestera_switch *sw, const char *mac)
{
	struct prestera_msg_switch_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_SWITCH_ATTR_MAC),
	};

	ether_addr_copy(req.param.mac, mac);

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_SWITCH_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_switch_init(struct prestera_switch *sw)
{
	struct prestera_msg_switch_init_resp resp;
	struct prestera_msg_common_req req;
	int err;

	INIT_LIST_HEAD(&sw->event_handlers);

	prestera_hw_build_tests();

	err = prestera_cmd_ret_wait(sw, PRESTERA_CMD_TYPE_SWITCH_INIT,
				    &req.cmd, sizeof(req),
				    &resp.ret, sizeof(resp),
				    PRESTERA_SWITCH_INIT_TIMEOUT_MS);
	if (err)
		return err;

	sw->dev->recv_msg = prestera_evt_recv;
	sw->dev->recv_pkt = prestera_pkt_recv;
	sw->port_count = __le32_to_cpu(resp.port_count);
	sw->mtu_min = PRESTERA_MIN_MTU;
	sw->mtu_max = __le32_to_cpu(resp.mtu_max);
	sw->id = resp.switch_id;
	sw->lag_member_max = resp.lag_member_max;
	sw->lag_max = resp.lag_max;

	return 0;
}

void prestera_hw_switch_fini(struct prestera_switch *sw)
{
	WARN_ON(!list_empty(&sw->event_handlers));
}

int prestera_hw_switch_ageing_set(struct prestera_switch *sw, u32 ageing_ms)
{
	struct prestera_msg_switch_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_SWITCH_ATTR_AGEING),
		.param = {
			.ageing_timeout_ms = __cpu_to_le32(ageing_ms),
		},
	};

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_SWITCH_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_mac_mode_get(const struct prestera_port *port,
				  u32 *mode, u32 *speed, u8 *duplex, u8 *fec)
{
	struct prestera_msg_port_attr_resp resp;
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_MAC_MODE),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id)
	};
	int err;

	err = prestera_cmd_ret(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	if (mode)
		*mode = __le32_to_cpu(resp.param.link_evt.mac.mode);

	if (speed)
		*speed = __le32_to_cpu(resp.param.link_evt.mac.speed);

	if (duplex)
		*duplex = resp.param.link_evt.mac.duplex;

	if (fec)
		*fec = resp.param.link_evt.mac.fec;

	return err;
}

int prestera_hw_port_mac_mode_set(const struct prestera_port *port,
				  bool admin, u32 mode, u8 inband,
				  u32 speed, u8 duplex, u8 fec)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_MAC_MODE),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.param = {
			.link = {
				.mac = {
					.admin = admin,
					.reg_mode.mode = __cpu_to_le32(mode),
					.reg_mode.inband = inband,
					.reg_mode.speed = __cpu_to_le32(speed),
					.reg_mode.duplex = duplex,
					.reg_mode.fec = fec
				}
			}
		}
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_phy_mode_get(const struct prestera_port *port,
				  u8 *mdix, u64 *lmode_bmap,
				  bool *fc_pause, bool *fc_asym)
{
	struct prestera_msg_port_attr_resp resp;
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_PHY_MODE),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id)
	};
	int err;

	err = prestera_cmd_ret(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	if (mdix)
		*mdix = prestera_hw_mdix_to_eth(resp.param.link_evt.phy.mdix);

	if (lmode_bmap)
		*lmode_bmap = __le64_to_cpu(resp.param.link_evt.phy.lmode_bmap);

	if (fc_pause && fc_asym)
		prestera_hw_remote_fc_to_eth(resp.param.link_evt.phy.fc,
					     fc_pause, fc_asym);

	return err;
}

int prestera_hw_port_phy_mode_set(const struct prestera_port *port,
				  bool admin, bool adv, u32 mode, u64 modes,
				  u8 mdix)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_PHY_MODE),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.param = {
			.link = {
				.phy = {
					.admin = admin,
					.adv_enable = adv ? 1 : 0,
					.mode = __cpu_to_le32(mode),
					.modes = __cpu_to_le64(modes),
				}
			}
		}
	};

	req.param.link.phy.mdix = prestera_hw_mdix_from_eth(mdix);

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_mtu_set(const struct prestera_port *port, u32 mtu)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_MTU),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.param = {
			.mtu = __cpu_to_le32(mtu),
		}
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_mac_set(const struct prestera_port *port, const char *mac)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_MAC),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
	};

	ether_addr_copy(req.param.mac, mac);

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_accept_frm_type(struct prestera_port *port,
				     enum prestera_accept_frm_type type)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_ACCEPT_FRAME_TYPE),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.param = {
			.accept_frm_type = type,
		}
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_cap_get(const struct prestera_port *port,
			     struct prestera_port_caps *caps)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_CAPABILITY),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
	};
	struct prestera_msg_port_attr_resp resp;
	int err;

	err = prestera_cmd_ret(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	caps->supp_link_modes = __le64_to_cpu(resp.param.cap.link_mode);
	caps->transceiver = resp.param.cap.transceiver;
	caps->supp_fec = resp.param.cap.fec;
	caps->type = resp.param.cap.type;

	return err;
}

static void prestera_hw_remote_fc_to_eth(u8 fc, bool *pause, bool *asym_pause)
{
	switch (fc) {
	case PRESTERA_FC_SYMMETRIC:
		*pause = true;
		*asym_pause = false;
		break;
	case PRESTERA_FC_ASYMMETRIC:
		*pause = false;
		*asym_pause = true;
		break;
	case PRESTERA_FC_SYMM_ASYMM:
		*pause = true;
		*asym_pause = true;
		break;
	default:
		*pause = false;
		*asym_pause = false;
	}
}

int prestera_hw_acl_ruleset_create(struct prestera_switch *sw, u16 *ruleset_id)
{
	struct prestera_msg_acl_ruleset_resp resp;
	struct prestera_msg_acl_ruleset_req req;
	int err;

	err = prestera_cmd_ret(sw, PRESTERA_CMD_TYPE_ACL_RULESET_CREATE,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	*ruleset_id = __le16_to_cpu(resp.id);

	return 0;
}

int prestera_hw_acl_ruleset_del(struct prestera_switch *sw, u16 ruleset_id)
{
	struct prestera_msg_acl_ruleset_req req = {
		.id = __cpu_to_le16(ruleset_id),
	};

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_ACL_RULESET_DELETE,
			    &req.cmd, sizeof(req));
}

static int prestera_hw_acl_actions_put(struct prestera_msg_acl_action *action,
				       struct prestera_acl_rule *rule)
{
	struct list_head *a_list = prestera_acl_rule_action_list_get(rule);
	struct prestera_acl_rule_action_entry *a_entry;
	int i = 0;

	list_for_each_entry(a_entry, a_list, list) {
		action[i].id = __cpu_to_le32(a_entry->id);

		switch (a_entry->id) {
		case PRESTERA_ACL_RULE_ACTION_ACCEPT:
		case PRESTERA_ACL_RULE_ACTION_DROP:
		case PRESTERA_ACL_RULE_ACTION_TRAP:
			/* just rule action id, no specific data */
			break;
		default:
			return -EINVAL;
		}

		i++;
	}

	return 0;
}

static int prestera_hw_acl_matches_put(struct prestera_msg_acl_match *match,
				       struct prestera_acl_rule *rule)
{
	struct list_head *m_list = prestera_acl_rule_match_list_get(rule);
	struct prestera_acl_rule_match_entry *m_entry;
	int i = 0;

	list_for_each_entry(m_entry, m_list, list) {
		match[i].type = __cpu_to_le32(m_entry->type);

		switch (m_entry->type) {
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_ETH_TYPE:
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_L4_PORT_SRC:
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_L4_PORT_DST:
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_VLAN_ID:
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_VLAN_TPID:
			match[i].keymask.u16.key =
				__cpu_to_le16(m_entry->keymask.u16.key);
			match[i].keymask.u16.mask =
				__cpu_to_le16(m_entry->keymask.u16.mask);
			break;
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_ICMP_TYPE:
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_ICMP_CODE:
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_IP_PROTO:
			match[i].keymask.u8.key = m_entry->keymask.u8.key;
			match[i].keymask.u8.mask = m_entry->keymask.u8.mask;
			break;
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_ETH_SMAC:
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_ETH_DMAC:
			memcpy(match[i].keymask.mac.key,
			       m_entry->keymask.mac.key,
			       sizeof(match[i].keymask.mac.key));
			memcpy(match[i].keymask.mac.mask,
			       m_entry->keymask.mac.mask,
			       sizeof(match[i].keymask.mac.mask));
			break;
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_IP_SRC:
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_IP_DST:
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_L4_PORT_RANGE_SRC:
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_L4_PORT_RANGE_DST:
			match[i].keymask.u32.key =
				__cpu_to_le32(m_entry->keymask.u32.key);
			match[i].keymask.u32.mask =
				__cpu_to_le32(m_entry->keymask.u32.mask);
			break;
		case PRESTERA_ACL_RULE_MATCH_ENTRY_TYPE_PORT:
			match[i].keymask.u64.key =
				__cpu_to_le64(m_entry->keymask.u64.key);
			match[i].keymask.u64.mask =
				__cpu_to_le64(m_entry->keymask.u64.mask);
			break;
		default:
			return -EINVAL;
		}

		i++;
	}

	return 0;
}

int prestera_hw_acl_rule_add(struct prestera_switch *sw,
			     struct prestera_acl_rule *rule,
			     u32 *rule_id)
{
	struct prestera_msg_acl_action *actions;
	struct prestera_msg_acl_match *matches;
	struct prestera_msg_acl_rule_resp resp;
	struct prestera_msg_acl_rule_req *req;
	u8 n_actions;
	u8 n_matches;
	void *buff;
	u32 size;
	int err;

	n_actions = prestera_acl_rule_action_len(rule);
	n_matches = prestera_acl_rule_match_len(rule);

	size = sizeof(*req) + sizeof(*actions) * n_actions +
		sizeof(*matches) * n_matches;

	buff = kzalloc(size, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	req = buff;
	actions = buff + sizeof(*req);
	matches = buff + sizeof(*req) + sizeof(*actions) * n_actions;

	/* put acl actions into the message */
	err = prestera_hw_acl_actions_put(actions, rule);
	if (err)
		goto free_buff;

	/* put acl matches into the message */
	err = prestera_hw_acl_matches_put(matches, rule);
	if (err)
		goto free_buff;

	req->ruleset_id = __cpu_to_le16(prestera_acl_rule_ruleset_id_get(rule));
	req->priority = __cpu_to_le32(prestera_acl_rule_priority_get(rule));
	req->n_actions = prestera_acl_rule_action_len(rule);
	req->n_matches = prestera_acl_rule_match_len(rule);

	err = prestera_cmd_ret(sw, PRESTERA_CMD_TYPE_ACL_RULE_ADD,
			       &req->cmd, size, &resp.ret, sizeof(resp));
	if (err)
		goto free_buff;

	*rule_id = __le32_to_cpu(resp.id);
free_buff:
	kfree(buff);
	return err;
}

int prestera_hw_acl_rule_del(struct prestera_switch *sw, u32 rule_id)
{
	struct prestera_msg_acl_rule_req req = {
		.id = __cpu_to_le32(rule_id)
	};

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_ACL_RULE_DELETE,
			    &req.cmd, sizeof(req));
}

int prestera_hw_acl_rule_stats_get(struct prestera_switch *sw, u32 rule_id,
				   u64 *packets, u64 *bytes)
{
	struct prestera_msg_acl_rule_stats_resp resp;
	struct prestera_msg_acl_rule_req req = {
		.id = __cpu_to_le32(rule_id)
	};
	int err;

	err = prestera_cmd_ret(sw, PRESTERA_CMD_TYPE_ACL_RULE_STATS_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	*packets = __le64_to_cpu(resp.packets);
	*bytes = __le64_to_cpu(resp.bytes);

	return 0;
}

int prestera_hw_acl_port_bind(const struct prestera_port *port, u16 ruleset_id)
{
	struct prestera_msg_acl_ruleset_bind_req req = {
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.ruleset_id = __cpu_to_le16(ruleset_id),
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_ACL_PORT_BIND,
			    &req.cmd, sizeof(req));
}

int prestera_hw_acl_port_unbind(const struct prestera_port *port,
				u16 ruleset_id)
{
	struct prestera_msg_acl_ruleset_bind_req req = {
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.ruleset_id = __cpu_to_le16(ruleset_id),
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_ACL_PORT_UNBIND,
			    &req.cmd, sizeof(req));
}

int prestera_hw_span_get(const struct prestera_port *port, u8 *span_id)
{
	struct prestera_msg_span_resp resp;
	struct prestera_msg_span_req req = {
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
	};
	int err;

	err = prestera_cmd_ret(port->sw, PRESTERA_CMD_TYPE_SPAN_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	*span_id = resp.id;

	return 0;
}

int prestera_hw_span_bind(const struct prestera_port *port, u8 span_id)
{
	struct prestera_msg_span_req req = {
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.id = span_id,
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_SPAN_BIND,
			    &req.cmd, sizeof(req));
}

int prestera_hw_span_unbind(const struct prestera_port *port)
{
	struct prestera_msg_span_req req = {
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_SPAN_UNBIND,
			    &req.cmd, sizeof(req));
}

int prestera_hw_span_release(struct prestera_switch *sw, u8 span_id)
{
	struct prestera_msg_span_req req = {
		.id = span_id
	};

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_SPAN_RELEASE,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_type_get(const struct prestera_port *port, u8 *type)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_TYPE),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
	};
	struct prestera_msg_port_attr_resp resp;
	int err;

	err = prestera_cmd_ret(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	*type = resp.param.type;

	return 0;
}

int prestera_hw_port_speed_get(const struct prestera_port *port, u32 *speed)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_SPEED),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
	};
	struct prestera_msg_port_attr_resp resp;
	int err;

	err = prestera_cmd_ret(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	*speed = __le32_to_cpu(resp.param.speed);

	return 0;
}

int prestera_hw_port_autoneg_restart(struct prestera_port *port)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_PHY_AUTONEG_RESTART),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_stats_get(const struct prestera_port *port,
			       struct prestera_port_stats *st)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_STATS),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
	};
	struct prestera_msg_port_stats_resp resp;
	__le64 *hw = resp.stats;
	int err;

	err = prestera_cmd_ret(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	st->good_octets_received =
		__le64_to_cpu(hw[PRESTERA_PORT_GOOD_OCTETS_RCV_CNT]);
	st->bad_octets_received =
		__le64_to_cpu(hw[PRESTERA_PORT_BAD_OCTETS_RCV_CNT]);
	st->mac_trans_error =
		__le64_to_cpu(hw[PRESTERA_PORT_MAC_TRANSMIT_ERR_CNT]);
	st->broadcast_frames_received =
		__le64_to_cpu(hw[PRESTERA_PORT_BRDC_PKTS_RCV_CNT]);
	st->multicast_frames_received =
		__le64_to_cpu(hw[PRESTERA_PORT_MC_PKTS_RCV_CNT]);
	st->frames_64_octets = __le64_to_cpu(hw[PRESTERA_PORT_PKTS_64L_CNT]);
	st->frames_65_to_127_octets =
		__le64_to_cpu(hw[PRESTERA_PORT_PKTS_65TO127L_CNT]);
	st->frames_128_to_255_octets =
		__le64_to_cpu(hw[PRESTERA_PORT_PKTS_128TO255L_CNT]);
	st->frames_256_to_511_octets =
		__le64_to_cpu(hw[PRESTERA_PORT_PKTS_256TO511L_CNT]);
	st->frames_512_to_1023_octets =
		__le64_to_cpu(hw[PRESTERA_PORT_PKTS_512TO1023L_CNT]);
	st->frames_1024_to_max_octets =
		__le64_to_cpu(hw[PRESTERA_PORT_PKTS_1024TOMAXL_CNT]);
	st->excessive_collision =
		__le64_to_cpu(hw[PRESTERA_PORT_EXCESSIVE_COLLISIONS_CNT]);
	st->multicast_frames_sent =
		__le64_to_cpu(hw[PRESTERA_PORT_MC_PKTS_SENT_CNT]);
	st->broadcast_frames_sent =
		__le64_to_cpu(hw[PRESTERA_PORT_BRDC_PKTS_SENT_CNT]);
	st->fc_sent = __le64_to_cpu(hw[PRESTERA_PORT_FC_SENT_CNT]);
	st->fc_received = __le64_to_cpu(hw[PRESTERA_PORT_GOOD_FC_RCV_CNT]);
	st->buffer_overrun = __le64_to_cpu(hw[PRESTERA_PORT_DROP_EVENTS_CNT]);
	st->undersize = __le64_to_cpu(hw[PRESTERA_PORT_UNDERSIZE_PKTS_CNT]);
	st->fragments = __le64_to_cpu(hw[PRESTERA_PORT_FRAGMENTS_PKTS_CNT]);
	st->oversize = __le64_to_cpu(hw[PRESTERA_PORT_OVERSIZE_PKTS_CNT]);
	st->jabber = __le64_to_cpu(hw[PRESTERA_PORT_JABBER_PKTS_CNT]);
	st->rx_error_frame_received =
		__le64_to_cpu(hw[PRESTERA_PORT_MAC_RCV_ERROR_CNT]);
	st->bad_crc = __le64_to_cpu(hw[PRESTERA_PORT_BAD_CRC_CNT]);
	st->collisions = __le64_to_cpu(hw[PRESTERA_PORT_COLLISIONS_CNT]);
	st->late_collision =
		__le64_to_cpu(hw[PRESTERA_PORT_LATE_COLLISIONS_CNT]);
	st->unicast_frames_received =
		__le64_to_cpu(hw[PRESTERA_PORT_GOOD_UC_PKTS_RCV_CNT]);
	st->unicast_frames_sent =
		__le64_to_cpu(hw[PRESTERA_PORT_GOOD_UC_PKTS_SENT_CNT]);
	st->sent_multiple =
		__le64_to_cpu(hw[PRESTERA_PORT_MULTIPLE_PKTS_SENT_CNT]);
	st->sent_deferred =
		__le64_to_cpu(hw[PRESTERA_PORT_DEFERRED_PKTS_SENT_CNT]);
	st->good_octets_sent =
		__le64_to_cpu(hw[PRESTERA_PORT_GOOD_OCTETS_SENT_CNT]);

	return 0;
}

int prestera_hw_port_learning_set(struct prestera_port *port, bool enable)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_LEARNING),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.param = {
			.learning = enable,
		}
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

static int prestera_hw_port_uc_flood_set(struct prestera_port *port, bool flood)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_FLOOD),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.param = {
			.flood_ext = {
				.type = PRESTERA_PORT_FLOOD_TYPE_UC,
				.enable = flood,
			}
		}
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

static int prestera_hw_port_mc_flood_set(struct prestera_port *port, bool flood)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_FLOOD),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.param = {
			.flood_ext = {
				.type = PRESTERA_PORT_FLOOD_TYPE_MC,
				.enable = flood,
			}
		}
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

static int prestera_hw_port_flood_set_v2(struct prestera_port *port, bool flood)
{
	struct prestera_msg_port_attr_req req = {
		.attr = __cpu_to_le32(PRESTERA_CMD_PORT_ATTR_FLOOD),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.param = {
			.flood = flood,
		}
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_flood_set(struct prestera_port *port, unsigned long mask,
			       unsigned long val)
{
	int err;

	if (port->sw->dev->fw_rev.maj <= 2) {
		if (!(mask & BR_FLOOD))
			return 0;

		return prestera_hw_port_flood_set_v2(port, val & BR_FLOOD);
	}

	if (mask & BR_FLOOD) {
		err = prestera_hw_port_uc_flood_set(port, val & BR_FLOOD);
		if (err)
			goto err_uc_flood;
	}

	if (mask & BR_MCAST_FLOOD) {
		err = prestera_hw_port_mc_flood_set(port, val & BR_MCAST_FLOOD);
		if (err)
			goto err_mc_flood;
	}

	return 0;

err_mc_flood:
	prestera_hw_port_mc_flood_set(port, 0);
err_uc_flood:
	if (mask & BR_FLOOD)
		prestera_hw_port_uc_flood_set(port, 0);

	return err;
}

int prestera_hw_vlan_create(struct prestera_switch *sw, u16 vid)
{
	struct prestera_msg_vlan_req req = {
		.vid = __cpu_to_le16(vid),
	};

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_VLAN_CREATE,
			    &req.cmd, sizeof(req));
}

int prestera_hw_vlan_delete(struct prestera_switch *sw, u16 vid)
{
	struct prestera_msg_vlan_req req = {
		.vid = __cpu_to_le16(vid),
	};

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_VLAN_DELETE,
			    &req.cmd, sizeof(req));
}

int prestera_hw_vlan_port_set(struct prestera_port *port, u16 vid,
			      bool is_member, bool untagged)
{
	struct prestera_msg_vlan_req req = {
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.vid = __cpu_to_le16(vid),
		.is_member = is_member,
		.is_tagged = !untagged,
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_VLAN_PORT_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_vlan_port_vid_set(struct prestera_port *port, u16 vid)
{
	struct prestera_msg_vlan_req req = {
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.vid = __cpu_to_le16(vid),
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_VLAN_PVID_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_vlan_port_stp_set(struct prestera_port *port, u16 vid, u8 state)
{
	struct prestera_msg_stp_req req = {
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.vid = __cpu_to_le16(vid),
		.state = state,
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_STP_PORT_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_fdb_add(struct prestera_port *port, const unsigned char *mac,
			u16 vid, bool dynamic)
{
	struct prestera_msg_fdb_req req = {
		.dest = {
			.dev = __cpu_to_le32(port->dev_id),
			.port = __cpu_to_le32(port->hw_id),
		},
		.vid = __cpu_to_le16(vid),
		.dynamic = dynamic,
	};

	ether_addr_copy(req.mac, mac);

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_FDB_ADD,
			    &req.cmd, sizeof(req));
}

int prestera_hw_fdb_del(struct prestera_port *port, const unsigned char *mac,
			u16 vid)
{
	struct prestera_msg_fdb_req req = {
		.dest = {
			.dev = __cpu_to_le32(port->dev_id),
			.port = __cpu_to_le32(port->hw_id),
		},
		.vid = __cpu_to_le16(vid),
	};

	ether_addr_copy(req.mac, mac);

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_FDB_DELETE,
			    &req.cmd, sizeof(req));
}

int prestera_hw_lag_fdb_add(struct prestera_switch *sw, u16 lag_id,
			    const unsigned char *mac, u16 vid, bool dynamic)
{
	struct prestera_msg_fdb_req req = {
		.dest_type = PRESTERA_HW_FDB_ENTRY_TYPE_LAG,
		.dest = {
			.lag_id = __cpu_to_le16(lag_id),
		},
		.vid = __cpu_to_le16(vid),
		.dynamic = dynamic,
	};

	ether_addr_copy(req.mac, mac);

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_FDB_ADD,
			    &req.cmd, sizeof(req));
}

int prestera_hw_lag_fdb_del(struct prestera_switch *sw, u16 lag_id,
			    const unsigned char *mac, u16 vid)
{
	struct prestera_msg_fdb_req req = {
		.dest_type = PRESTERA_HW_FDB_ENTRY_TYPE_LAG,
		.dest = {
			.lag_id = __cpu_to_le16(lag_id),
		},
		.vid = __cpu_to_le16(vid),
	};

	ether_addr_copy(req.mac, mac);

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_FDB_DELETE,
			    &req.cmd, sizeof(req));
}

int prestera_hw_fdb_flush_port(struct prestera_port *port, u32 mode)
{
	struct prestera_msg_fdb_req req = {
		.dest = {
			.dev = __cpu_to_le32(port->dev_id),
			.port = __cpu_to_le32(port->hw_id),
		},
		.flush_mode = __cpu_to_le32(mode),
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_FDB_FLUSH_PORT,
			    &req.cmd, sizeof(req));
}

int prestera_hw_fdb_flush_vlan(struct prestera_switch *sw, u16 vid, u32 mode)
{
	struct prestera_msg_fdb_req req = {
		.vid = __cpu_to_le16(vid),
		.flush_mode = __cpu_to_le32(mode),
	};

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_FDB_FLUSH_VLAN,
			    &req.cmd, sizeof(req));
}

int prestera_hw_fdb_flush_port_vlan(struct prestera_port *port, u16 vid,
				    u32 mode)
{
	struct prestera_msg_fdb_req req = {
		.dest = {
			.dev = __cpu_to_le32(port->dev_id),
			.port = __cpu_to_le32(port->hw_id),
		},
		.vid = __cpu_to_le16(vid),
		.flush_mode = __cpu_to_le32(mode),
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_FDB_FLUSH_PORT_VLAN,
			    &req.cmd, sizeof(req));
}

int prestera_hw_fdb_flush_lag(struct prestera_switch *sw, u16 lag_id,
			      u32 mode)
{
	struct prestera_msg_fdb_req req = {
		.dest_type = PRESTERA_HW_FDB_ENTRY_TYPE_LAG,
		.dest = {
			.lag_id = __cpu_to_le16(lag_id),
		},
		.flush_mode = __cpu_to_le32(mode),
	};

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_FDB_FLUSH_PORT,
			    &req.cmd, sizeof(req));
}

int prestera_hw_fdb_flush_lag_vlan(struct prestera_switch *sw,
				   u16 lag_id, u16 vid, u32 mode)
{
	struct prestera_msg_fdb_req req = {
		.dest_type = PRESTERA_HW_FDB_ENTRY_TYPE_LAG,
		.dest = {
			.lag_id = __cpu_to_le16(lag_id),
		},
		.vid = __cpu_to_le16(vid),
		.flush_mode = __cpu_to_le32(mode),
	};

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_FDB_FLUSH_PORT_VLAN,
			    &req.cmd, sizeof(req));
}

int prestera_hw_bridge_create(struct prestera_switch *sw, u16 *bridge_id)
{
	struct prestera_msg_bridge_resp resp;
	struct prestera_msg_bridge_req req;
	int err;

	err = prestera_cmd_ret(sw, PRESTERA_CMD_TYPE_BRIDGE_CREATE,
			       &req.cmd, sizeof(req),
			       &resp.ret, sizeof(resp));
	if (err)
		return err;

	*bridge_id = __le16_to_cpu(resp.bridge);

	return 0;
}

int prestera_hw_bridge_delete(struct prestera_switch *sw, u16 bridge_id)
{
	struct prestera_msg_bridge_req req = {
		.bridge = __cpu_to_le16(bridge_id),
	};

	return prestera_cmd(sw, PRESTERA_CMD_TYPE_BRIDGE_DELETE,
			    &req.cmd, sizeof(req));
}

int prestera_hw_bridge_port_add(struct prestera_port *port, u16 bridge_id)
{
	struct prestera_msg_bridge_req req = {
		.bridge = __cpu_to_le16(bridge_id),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_BRIDGE_PORT_ADD,
			    &req.cmd, sizeof(req));
}

int prestera_hw_bridge_port_delete(struct prestera_port *port, u16 bridge_id)
{
	struct prestera_msg_bridge_req req = {
		.bridge = __cpu_to_le16(bridge_id),
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_BRIDGE_PORT_DELETE,
			    &req.cmd, sizeof(req));
}

int prestera_hw_rxtx_init(struct prestera_switch *sw,
			  struct prestera_rxtx_params *params)
{
	struct prestera_msg_rxtx_resp resp;
	struct prestera_msg_rxtx_req req;
	int err;

	req.use_sdma = params->use_sdma;

	err = prestera_cmd_ret(sw, PRESTERA_CMD_TYPE_RXTX_INIT,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	params->map_addr = __le32_to_cpu(resp.map_addr);

	return 0;
}

int prestera_hw_lag_member_add(struct prestera_port *port, u16 lag_id)
{
	struct prestera_msg_lag_req req = {
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.lag_id = __cpu_to_le16(lag_id),
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_LAG_MEMBER_ADD,
			    &req.cmd, sizeof(req));
}

int prestera_hw_lag_member_del(struct prestera_port *port, u16 lag_id)
{
	struct prestera_msg_lag_req req = {
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.lag_id = __cpu_to_le16(lag_id),
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_LAG_MEMBER_DELETE,
			    &req.cmd, sizeof(req));
}

int prestera_hw_lag_member_enable(struct prestera_port *port, u16 lag_id,
				  bool enable)
{
	struct prestera_msg_lag_req req = {
		.port = __cpu_to_le32(port->hw_id),
		.dev = __cpu_to_le32(port->dev_id),
		.lag_id = __cpu_to_le16(lag_id),
	};
	u32 cmd;

	cmd = enable ? PRESTERA_CMD_TYPE_LAG_MEMBER_ENABLE :
			PRESTERA_CMD_TYPE_LAG_MEMBER_DISABLE;

	return prestera_cmd(port->sw, cmd, &req.cmd, sizeof(req));
}

int
prestera_hw_cpu_code_counters_get(struct prestera_switch *sw, u8 code,
				  enum prestera_hw_cpu_code_cnt_t counter_type,
				  u64 *packet_count)
{
	struct prestera_msg_cpu_code_counter_req req = {
		.counter_type = counter_type,
		.code = code,
	};
	struct mvsw_msg_cpu_code_counter_ret resp;
	int err;

	err = prestera_cmd_ret(sw, PRESTERA_CMD_TYPE_CPU_CODE_COUNTERS_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	*packet_count = __le64_to_cpu(resp.packet_count);

	return 0;
}

int prestera_hw_event_handler_register(struct prestera_switch *sw,
				       enum prestera_event_type type,
				       prestera_event_cb_t fn,
				       void *arg)
{
	struct prestera_fw_event_handler *eh;

	eh = __find_event_handler(sw, type);
	if (eh)
		return -EEXIST;

	eh = kmalloc(sizeof(*eh), GFP_KERNEL);
	if (!eh)
		return -ENOMEM;

	eh->type = type;
	eh->func = fn;
	eh->arg = arg;

	INIT_LIST_HEAD(&eh->list);

	list_add_rcu(&eh->list, &sw->event_handlers);

	return 0;
}

void prestera_hw_event_handler_unregister(struct prestera_switch *sw,
					  enum prestera_event_type type,
					  prestera_event_cb_t fn)
{
	struct prestera_fw_event_handler *eh;

	eh = __find_event_handler(sw, type);
	if (!eh)
		return;

	list_del_rcu(&eh->list);
	kfree_rcu(eh, rcu);
}
