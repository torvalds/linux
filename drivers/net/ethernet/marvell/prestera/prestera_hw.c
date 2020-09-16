// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2020 Marvell International Ltd. All rights reserved */

#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/list.h>

#include "prestera.h"
#include "prestera_hw.h"

#define PRESTERA_SWITCH_INIT_TIMEOUT_MS (30 * 1000)

#define PRESTERA_MIN_MTU 64

enum prestera_cmd_type_t {
	PRESTERA_CMD_TYPE_SWITCH_INIT = 0x1,
	PRESTERA_CMD_TYPE_SWITCH_ATTR_SET = 0x2,

	PRESTERA_CMD_TYPE_PORT_ATTR_SET = 0x100,
	PRESTERA_CMD_TYPE_PORT_ATTR_GET = 0x101,
	PRESTERA_CMD_TYPE_PORT_INFO_GET = 0x110,

	PRESTERA_CMD_TYPE_RXTX_INIT = 0x800,
	PRESTERA_CMD_TYPE_RXTX_PORT_INIT = 0x801,

	PRESTERA_CMD_TYPE_ACK = 0x10000,
	PRESTERA_CMD_TYPE_MAX
};

enum {
	PRESTERA_CMD_PORT_ATTR_ADMIN_STATE = 1,
	PRESTERA_CMD_PORT_ATTR_MTU = 3,
	PRESTERA_CMD_PORT_ATTR_MAC = 4,
	PRESTERA_CMD_PORT_ATTR_CAPABILITY = 9,
	PRESTERA_CMD_PORT_ATTR_AUTONEG = 15,
	PRESTERA_CMD_PORT_ATTR_STATS = 17,
};

enum {
	PRESTERA_CMD_SWITCH_ATTR_MAC = 1,
};

enum {
	PRESTERA_CMD_ACK_OK,
	PRESTERA_CMD_ACK_FAILED,

	PRESTERA_CMD_ACK_MAX
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

struct prestera_fw_event_handler {
	struct list_head list;
	struct rcu_head rcu;
	enum prestera_event_type type;
	prestera_event_cb_t func;
	void *arg;
};

struct prestera_msg_cmd {
	u32 type;
};

struct prestera_msg_ret {
	struct prestera_msg_cmd cmd;
	u32 status;
};

struct prestera_msg_common_req {
	struct prestera_msg_cmd cmd;
};

struct prestera_msg_common_resp {
	struct prestera_msg_ret ret;
};

union prestera_msg_switch_param {
	u8 mac[ETH_ALEN];
};

struct prestera_msg_switch_attr_req {
	struct prestera_msg_cmd cmd;
	u32 attr;
	union prestera_msg_switch_param param;
};

struct prestera_msg_switch_init_resp {
	struct prestera_msg_ret ret;
	u32 port_count;
	u32 mtu_max;
	u8  switch_id;
};

struct prestera_msg_port_autoneg_param {
	u64 link_mode;
	u8  enable;
	u8  fec;
};

struct prestera_msg_port_cap_param {
	u64 link_mode;
	u8  type;
	u8  fec;
	u8  transceiver;
};

union prestera_msg_port_param {
	u8  admin_state;
	u8  oper_state;
	u32 mtu;
	u8  mac[ETH_ALEN];
	struct prestera_msg_port_autoneg_param autoneg;
	struct prestera_msg_port_cap_param cap;
};

struct prestera_msg_port_attr_req {
	struct prestera_msg_cmd cmd;
	u32 attr;
	u32 port;
	u32 dev;
	union prestera_msg_port_param param;
};

struct prestera_msg_port_attr_resp {
	struct prestera_msg_ret ret;
	union prestera_msg_port_param param;
};

struct prestera_msg_port_stats_resp {
	struct prestera_msg_ret ret;
	u64 stats[PRESTERA_PORT_CNT_MAX];
};

struct prestera_msg_port_info_req {
	struct prestera_msg_cmd cmd;
	u32 port;
};

struct prestera_msg_port_info_resp {
	struct prestera_msg_ret ret;
	u32 hw_id;
	u32 dev_id;
	u16 fp_id;
};

struct prestera_msg_rxtx_req {
	struct prestera_msg_cmd cmd;
	u8 use_sdma;
};

struct prestera_msg_rxtx_resp {
	struct prestera_msg_ret ret;
	u32 map_addr;
};

struct prestera_msg_rxtx_port_req {
	struct prestera_msg_cmd cmd;
	u32 port;
	u32 dev;
};

struct prestera_msg_event {
	u16 type;
	u16 id;
};

union prestera_msg_event_port_param {
	u32 oper_state;
};

struct prestera_msg_event_port {
	struct prestera_msg_event id;
	u32 port_id;
	union prestera_msg_event_port_param param;
};

static int __prestera_cmd_ret(struct prestera_switch *sw,
			      enum prestera_cmd_type_t type,
			      struct prestera_msg_cmd *cmd, size_t clen,
			      struct prestera_msg_ret *ret, size_t rlen,
			      int waitms)
{
	struct prestera_device *dev = sw->dev;
	int err;

	cmd->type = type;

	err = dev->send_req(dev, cmd, clen, ret, rlen, waitms);
	if (err)
		return err;

	if (ret->cmd.type != PRESTERA_CMD_TYPE_ACK)
		return -EBADE;
	if (ret->status != PRESTERA_CMD_ACK_OK)
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
	struct prestera_msg_event_port *hw_evt = msg;

	if (evt->id != PRESTERA_PORT_EVENT_STATE_CHANGED)
		return -EINVAL;

	evt->port_evt.data.oper_state = hw_evt->param.oper_state;
	evt->port_evt.port_id = hw_evt->port_id;

	return 0;
}

static struct prestera_fw_evt_parser {
	int (*func)(void *msg, struct prestera_event *evt);
} fw_event_parsers[PRESTERA_EVENT_TYPE_MAX] = {
	[PRESTERA_EVENT_TYPE_PORT] = { .func = prestera_fw_parse_port_evt },
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
	int err;

	if (msg->type >= PRESTERA_EVENT_TYPE_MAX)
		return -EINVAL;
	if (!fw_event_parsers[msg->type].func)
		return -ENOENT;

	err = prestera_find_event_handler(sw, msg->type, &eh);
	if (err)
		return err;

	evt.id = msg->id;

	err = fw_event_parsers[msg->type].func(buf, &evt);
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

int prestera_hw_port_info_get(const struct prestera_port *port,
			      u32 *dev_id, u32 *hw_id, u16 *fp_id)
{
	struct prestera_msg_port_info_req req = {
		.port = port->id,
	};
	struct prestera_msg_port_info_resp resp;
	int err;

	err = prestera_cmd_ret(port->sw, PRESTERA_CMD_TYPE_PORT_INFO_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	*dev_id = resp.dev_id;
	*hw_id = resp.hw_id;
	*fp_id = resp.fp_id;

	return 0;
}

int prestera_hw_switch_mac_set(struct prestera_switch *sw, const char *mac)
{
	struct prestera_msg_switch_attr_req req = {
		.attr = PRESTERA_CMD_SWITCH_ATTR_MAC,
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

	err = prestera_cmd_ret_wait(sw, PRESTERA_CMD_TYPE_SWITCH_INIT,
				    &req.cmd, sizeof(req),
				    &resp.ret, sizeof(resp),
				    PRESTERA_SWITCH_INIT_TIMEOUT_MS);
	if (err)
		return err;

	sw->dev->recv_msg = prestera_evt_recv;
	sw->dev->recv_pkt = prestera_pkt_recv;
	sw->port_count = resp.port_count;
	sw->mtu_min = PRESTERA_MIN_MTU;
	sw->mtu_max = resp.mtu_max;
	sw->id = resp.switch_id;

	return 0;
}

void prestera_hw_switch_fini(struct prestera_switch *sw)
{
	WARN_ON(!list_empty(&sw->event_handlers));
}

int prestera_hw_port_state_set(const struct prestera_port *port,
			       bool admin_state)
{
	struct prestera_msg_port_attr_req req = {
		.attr = PRESTERA_CMD_PORT_ATTR_ADMIN_STATE,
		.port = port->hw_id,
		.dev = port->dev_id,
		.param = {
			.admin_state = admin_state,
		}
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_mtu_set(const struct prestera_port *port, u32 mtu)
{
	struct prestera_msg_port_attr_req req = {
		.attr = PRESTERA_CMD_PORT_ATTR_MTU,
		.port = port->hw_id,
		.dev = port->dev_id,
		.param = {
			.mtu = mtu,
		}
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_mac_set(const struct prestera_port *port, const char *mac)
{
	struct prestera_msg_port_attr_req req = {
		.attr = PRESTERA_CMD_PORT_ATTR_MAC,
		.port = port->hw_id,
		.dev = port->dev_id,
	};

	ether_addr_copy(req.param.mac, mac);

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_cap_get(const struct prestera_port *port,
			     struct prestera_port_caps *caps)
{
	struct prestera_msg_port_attr_req req = {
		.attr = PRESTERA_CMD_PORT_ATTR_CAPABILITY,
		.port = port->hw_id,
		.dev = port->dev_id,
	};
	struct prestera_msg_port_attr_resp resp;
	int err;

	err = prestera_cmd_ret(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	caps->supp_link_modes = resp.param.cap.link_mode;
	caps->transceiver = resp.param.cap.transceiver;
	caps->supp_fec = resp.param.cap.fec;
	caps->type = resp.param.cap.type;

	return err;
}

int prestera_hw_port_autoneg_set(const struct prestera_port *port,
				 bool autoneg, u64 link_modes, u8 fec)
{
	struct prestera_msg_port_attr_req req = {
		.attr = PRESTERA_CMD_PORT_ATTR_AUTONEG,
		.port = port->hw_id,
		.dev = port->dev_id,
		.param = {
			.autoneg = {
				.link_mode = link_modes,
				.enable = autoneg,
				.fec = fec,
			}
		}
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_SET,
			    &req.cmd, sizeof(req));
}

int prestera_hw_port_stats_get(const struct prestera_port *port,
			       struct prestera_port_stats *st)
{
	struct prestera_msg_port_attr_req req = {
		.attr = PRESTERA_CMD_PORT_ATTR_STATS,
		.port = port->hw_id,
		.dev = port->dev_id,
	};
	struct prestera_msg_port_stats_resp resp;
	u64 *hw = resp.stats;
	int err;

	err = prestera_cmd_ret(port->sw, PRESTERA_CMD_TYPE_PORT_ATTR_GET,
			       &req.cmd, sizeof(req), &resp.ret, sizeof(resp));
	if (err)
		return err;

	st->good_octets_received = hw[PRESTERA_PORT_GOOD_OCTETS_RCV_CNT];
	st->bad_octets_received = hw[PRESTERA_PORT_BAD_OCTETS_RCV_CNT];
	st->mac_trans_error = hw[PRESTERA_PORT_MAC_TRANSMIT_ERR_CNT];
	st->broadcast_frames_received = hw[PRESTERA_PORT_BRDC_PKTS_RCV_CNT];
	st->multicast_frames_received = hw[PRESTERA_PORT_MC_PKTS_RCV_CNT];
	st->frames_64_octets = hw[PRESTERA_PORT_PKTS_64L_CNT];
	st->frames_65_to_127_octets = hw[PRESTERA_PORT_PKTS_65TO127L_CNT];
	st->frames_128_to_255_octets = hw[PRESTERA_PORT_PKTS_128TO255L_CNT];
	st->frames_256_to_511_octets = hw[PRESTERA_PORT_PKTS_256TO511L_CNT];
	st->frames_512_to_1023_octets = hw[PRESTERA_PORT_PKTS_512TO1023L_CNT];
	st->frames_1024_to_max_octets = hw[PRESTERA_PORT_PKTS_1024TOMAXL_CNT];
	st->excessive_collision = hw[PRESTERA_PORT_EXCESSIVE_COLLISIONS_CNT];
	st->multicast_frames_sent = hw[PRESTERA_PORT_MC_PKTS_SENT_CNT];
	st->broadcast_frames_sent = hw[PRESTERA_PORT_BRDC_PKTS_SENT_CNT];
	st->fc_sent = hw[PRESTERA_PORT_FC_SENT_CNT];
	st->fc_received = hw[PRESTERA_PORT_GOOD_FC_RCV_CNT];
	st->buffer_overrun = hw[PRESTERA_PORT_DROP_EVENTS_CNT];
	st->undersize = hw[PRESTERA_PORT_UNDERSIZE_PKTS_CNT];
	st->fragments = hw[PRESTERA_PORT_FRAGMENTS_PKTS_CNT];
	st->oversize = hw[PRESTERA_PORT_OVERSIZE_PKTS_CNT];
	st->jabber = hw[PRESTERA_PORT_JABBER_PKTS_CNT];
	st->rx_error_frame_received = hw[PRESTERA_PORT_MAC_RCV_ERROR_CNT];
	st->bad_crc = hw[PRESTERA_PORT_BAD_CRC_CNT];
	st->collisions = hw[PRESTERA_PORT_COLLISIONS_CNT];
	st->late_collision = hw[PRESTERA_PORT_LATE_COLLISIONS_CNT];
	st->unicast_frames_received = hw[PRESTERA_PORT_GOOD_UC_PKTS_RCV_CNT];
	st->unicast_frames_sent = hw[PRESTERA_PORT_GOOD_UC_PKTS_SENT_CNT];
	st->sent_multiple = hw[PRESTERA_PORT_MULTIPLE_PKTS_SENT_CNT];
	st->sent_deferred = hw[PRESTERA_PORT_DEFERRED_PKTS_SENT_CNT];
	st->good_octets_sent = hw[PRESTERA_PORT_GOOD_OCTETS_SENT_CNT];

	return 0;
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

	params->map_addr = resp.map_addr;

	return 0;
}

int prestera_hw_rxtx_port_init(struct prestera_port *port)
{
	struct prestera_msg_rxtx_port_req req = {
		.port = port->hw_id,
		.dev = port->dev_id,
	};

	return prestera_cmd(port->sw, PRESTERA_CMD_TYPE_RXTX_PORT_INIT,
			    &req.cmd, sizeof(req));
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
