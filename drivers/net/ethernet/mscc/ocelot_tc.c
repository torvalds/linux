// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch TC driver
 *
 * Copyright (c) 2019 Microsemi Corporation
 */

#include "ocelot_tc.h"
#include "ocelot_police.h"
#include "ocelot_ace.h"
#include <net/pkt_cls.h>

static int ocelot_setup_tc_cls_matchall(struct ocelot_port *port,
					struct tc_cls_matchall_offload *f,
					bool ingress)
{
	struct netlink_ext_ack *extack = f->common.extack;
	struct ocelot_policer pol = { 0 };
	struct flow_action_entry *action;
	int err;

	netdev_dbg(port->dev, "%s: port %u command %d cookie %lu\n",
		   __func__, port->chip_port, f->command, f->cookie);

	if (!ingress) {
		NL_SET_ERR_MSG_MOD(extack, "Only ingress is supported");
		return -EOPNOTSUPP;
	}

	switch (f->command) {
	case TC_CLSMATCHALL_REPLACE:
		if (!flow_offload_has_one_action(&f->rule->action)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only one action is supported");
			return -EOPNOTSUPP;
		}

		if (port->tc.block_shared) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Rate limit is not supported on shared blocks");
			return -EOPNOTSUPP;
		}

		action = &f->rule->action.entries[0];

		if (action->id != FLOW_ACTION_POLICE) {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported action");
			return -EOPNOTSUPP;
		}

		if (port->tc.police_id && port->tc.police_id != f->cookie) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only one policer per port is supported\n");
			return -EEXIST;
		}

		pol.rate = (u32)div_u64(action->police.rate_bytes_ps, 1000) * 8;
		pol.burst = (u32)div_u64(action->police.rate_bytes_ps *
					 PSCHED_NS2TICKS(action->police.burst),
					 PSCHED_TICKS_PER_SEC);

		err = ocelot_port_policer_add(port, &pol);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Could not add policer\n");
			return err;
		}

		port->tc.police_id = f->cookie;
		port->tc.offload_cnt++;
		return 0;
	case TC_CLSMATCHALL_DESTROY:
		if (port->tc.police_id != f->cookie)
			return -ENOENT;

		err = ocelot_port_policer_del(port);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Could not delete policer\n");
			return err;
		}
		port->tc.police_id = 0;
		port->tc.offload_cnt--;
		return 0;
	case TC_CLSMATCHALL_STATS: /* fall through */
	default:
		return -EOPNOTSUPP;
	}
}

static int ocelot_setup_tc_block_cb(enum tc_setup_type type,
				    void *type_data,
				    void *cb_priv, bool ingress)
{
	struct ocelot_port *port = cb_priv;

	if (!tc_cls_can_offload_and_chain0(port->dev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSMATCHALL:
		netdev_dbg(port->dev, "tc_block_cb: TC_SETUP_CLSMATCHALL %s\n",
			   ingress ? "ingress" : "egress");

		return ocelot_setup_tc_cls_matchall(port, type_data, ingress);
	case TC_SETUP_CLSFLOWER:
		return 0;
	default:
		netdev_dbg(port->dev, "tc_block_cb: type %d %s\n",
			   type,
			   ingress ? "ingress" : "egress");

		return -EOPNOTSUPP;
	}
}

static int ocelot_setup_tc_block_cb_ig(enum tc_setup_type type,
				       void *type_data,
				       void *cb_priv)
{
	return ocelot_setup_tc_block_cb(type, type_data,
					cb_priv, true);
}

static int ocelot_setup_tc_block_cb_eg(enum tc_setup_type type,
				       void *type_data,
				       void *cb_priv)
{
	return ocelot_setup_tc_block_cb(type, type_data,
					cb_priv, false);
}

static LIST_HEAD(ocelot_block_cb_list);

static int ocelot_setup_tc_block(struct ocelot_port *port,
				 struct flow_block_offload *f)
{
	struct flow_block_cb *block_cb;
	flow_setup_cb_t *cb;
	int err;

	netdev_dbg(port->dev, "tc_block command %d, binder_type %d\n",
		   f->command, f->binder_type);

	if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS) {
		cb = ocelot_setup_tc_block_cb_ig;
		port->tc.block_shared = f->block_shared;
	} else if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS) {
		cb = ocelot_setup_tc_block_cb_eg;
	} else {
		return -EOPNOTSUPP;
	}

	f->driver_block_list = &ocelot_block_cb_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		if (flow_block_cb_is_busy(cb, port, &ocelot_block_cb_list))
			return -EBUSY;

		block_cb = flow_block_cb_alloc(cb, port, port, NULL);
		if (IS_ERR(block_cb))
			return PTR_ERR(block_cb);

		err = ocelot_setup_tc_block_flower_bind(port, f);
		if (err < 0) {
			flow_block_cb_free(block_cb);
			return err;
		}
		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, f->driver_block_list);
		return 0;
	case FLOW_BLOCK_UNBIND:
		block_cb = flow_block_cb_lookup(f->block, cb, port);
		if (!block_cb)
			return -ENOENT;

		ocelot_setup_tc_block_flower_unbind(port, f);
		flow_block_cb_remove(block_cb, f);
		list_del(&block_cb->driver_list);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

int ocelot_setup_tc(struct net_device *dev, enum tc_setup_type type,
		    void *type_data)
{
	struct ocelot_port *port = netdev_priv(dev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return ocelot_setup_tc_block(port, type_data);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}
