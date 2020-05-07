// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch TC driver
 *
 * Copyright (c) 2019 Microsemi Corporation
 */

#include <soc/mscc/ocelot.h>
#include "ocelot_tc.h"
#include "ocelot_ace.h"
#include <net/pkt_cls.h>

static int ocelot_setup_tc_cls_matchall(struct ocelot_port_private *priv,
					struct tc_cls_matchall_offload *f,
					bool ingress)
{
	struct netlink_ext_ack *extack = f->common.extack;
	struct ocelot *ocelot = priv->port.ocelot;
	struct ocelot_policer pol = { 0 };
	struct flow_action_entry *action;
	int port = priv->chip_port;
	int err;

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

		if (priv->tc.block_shared) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Rate limit is not supported on shared blocks");
			return -EOPNOTSUPP;
		}

		action = &f->rule->action.entries[0];

		if (action->id != FLOW_ACTION_POLICE) {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported action");
			return -EOPNOTSUPP;
		}

		if (priv->tc.police_id && priv->tc.police_id != f->cookie) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only one policer per port is supported\n");
			return -EEXIST;
		}

		pol.rate = (u32)div_u64(action->police.rate_bytes_ps, 1000) * 8;
		pol.burst = (u32)div_u64(action->police.rate_bytes_ps *
					 PSCHED_NS2TICKS(action->police.burst),
					 PSCHED_TICKS_PER_SEC);

		err = ocelot_port_policer_add(ocelot, port, &pol);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Could not add policer\n");
			return err;
		}

		priv->tc.police_id = f->cookie;
		priv->tc.offload_cnt++;
		return 0;
	case TC_CLSMATCHALL_DESTROY:
		if (priv->tc.police_id != f->cookie)
			return -ENOENT;

		err = ocelot_port_policer_del(ocelot, port);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Could not delete policer\n");
			return err;
		}
		priv->tc.police_id = 0;
		priv->tc.offload_cnt--;
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
	struct ocelot_port_private *priv = cb_priv;

	if (!tc_cls_can_offload_and_chain0(priv->dev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSMATCHALL:
		return ocelot_setup_tc_cls_matchall(priv, type_data, ingress);
	case TC_SETUP_CLSFLOWER:
		return ocelot_setup_tc_cls_flower(priv, type_data, ingress);
	default:
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

static int ocelot_setup_tc_block(struct ocelot_port_private *priv,
				 struct flow_block_offload *f)
{
	struct flow_block_cb *block_cb;
	flow_setup_cb_t *cb;

	if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS) {
		cb = ocelot_setup_tc_block_cb_ig;
		priv->tc.block_shared = f->block_shared;
	} else if (f->binder_type == FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS) {
		cb = ocelot_setup_tc_block_cb_eg;
	} else {
		return -EOPNOTSUPP;
	}

	f->driver_block_list = &ocelot_block_cb_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		if (flow_block_cb_is_busy(cb, priv, &ocelot_block_cb_list))
			return -EBUSY;

		block_cb = flow_block_cb_alloc(cb, priv, priv, NULL);
		if (IS_ERR(block_cb))
			return PTR_ERR(block_cb);

		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, f->driver_block_list);
		return 0;
	case FLOW_BLOCK_UNBIND:
		block_cb = flow_block_cb_lookup(f->block, cb, priv);
		if (!block_cb)
			return -ENOENT;

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
	struct ocelot_port_private *priv = netdev_priv(dev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return ocelot_setup_tc_block(priv, type_data);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}
