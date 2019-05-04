// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include <linux/math64.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>

#include "cmsg.h"
#include "main.h"
#include "../nfp_port.h"

struct nfp_police_cfg_head {
	__be32 flags_opts;
	__be32 port;
};

/* Police cmsg for configuring a trTCM traffic conditioner (8W/32B)
 * See RFC 2698 for more details.
 * ----------------------------------------------------------------
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Flag options                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                          Port Ingress                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Token Bucket Peak                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     Token Bucket Committed                    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         Peak Burst Size                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Committed Burst Size                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Peak Information Rate                    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    Committed Information Rate                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
struct nfp_police_config {
	struct nfp_police_cfg_head head;
	__be32 bkt_tkn_p;
	__be32 bkt_tkn_c;
	__be32 pbs;
	__be32 cbs;
	__be32 pir;
	__be32 cir;
};

static int
nfp_flower_install_rate_limiter(struct nfp_app *app, struct net_device *netdev,
				struct tc_cls_matchall_offload *flow,
				struct netlink_ext_ack *extack)
{
	struct flow_action_entry *action = &flow->rule->action.entries[0];
	struct nfp_flower_repr_priv *repr_priv;
	struct nfp_police_config *config;
	struct nfp_repr *repr;
	struct sk_buff *skb;
	u32 netdev_port_id;
	u64 burst, rate;

	if (!nfp_netdev_is_nfp_repr(netdev)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload not supported on higher level port");
		return -EOPNOTSUPP;
	}
	repr = netdev_priv(netdev);

	if (tcf_block_shared(flow->common.block)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload not supported on shared blocks");
		return -EOPNOTSUPP;
	}

	if (repr->port->type != NFP_PORT_VF_PORT) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload not supported on non-VF ports");
		return -EOPNOTSUPP;
	}

	if (!flow_offload_has_one_action(&flow->rule->action)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload requires a single action");
		return -EOPNOTSUPP;
	}

	if (flow->common.prio != (1 << 16)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload requires highest priority");
		return -EOPNOTSUPP;
	}

	if (action->id != FLOW_ACTION_POLICE) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload requires police action");
		return -EOPNOTSUPP;
	}

	rate = action->police.rate_bytes_ps;
	burst = div_u64(rate * PSCHED_NS2TICKS(action->police.burst),
			PSCHED_TICKS_PER_SEC);
	netdev_port_id = nfp_repr_get_port_id(netdev);

	skb = nfp_flower_cmsg_alloc(repr->app, sizeof(struct nfp_police_config),
				    NFP_FLOWER_CMSG_TYPE_QOS_MOD, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	config = nfp_flower_cmsg_get_data(skb);
	memset(config, 0, sizeof(struct nfp_police_config));
	config->head.port = cpu_to_be32(netdev_port_id);
	config->bkt_tkn_p = cpu_to_be32(burst);
	config->bkt_tkn_c = cpu_to_be32(burst);
	config->pbs = cpu_to_be32(burst);
	config->cbs = cpu_to_be32(burst);
	config->pir = cpu_to_be32(rate);
	config->cir = cpu_to_be32(rate);
	nfp_ctrl_tx(repr->app->ctrl, skb);

	repr_priv = repr->app_priv;
	repr_priv->qos_table.netdev_port_id = netdev_port_id;

	return 0;
}

static int
nfp_flower_remove_rate_limiter(struct nfp_app *app, struct net_device *netdev,
			       struct tc_cls_matchall_offload *flow,
			       struct netlink_ext_ack *extack)
{
	struct nfp_flower_repr_priv *repr_priv;
	struct nfp_police_config *config;
	struct nfp_repr *repr;
	struct sk_buff *skb;
	u32 netdev_port_id;

	if (!nfp_netdev_is_nfp_repr(netdev)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload not supported on higher level port");
		return -EOPNOTSUPP;
	}
	repr = netdev_priv(netdev);

	netdev_port_id = nfp_repr_get_port_id(netdev);
	repr_priv = repr->app_priv;

	if (!repr_priv->qos_table.netdev_port_id) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: cannot remove qos entry that does not exist");
		return -EOPNOTSUPP;
	}

	skb = nfp_flower_cmsg_alloc(repr->app, sizeof(struct nfp_police_config),
				    NFP_FLOWER_CMSG_TYPE_QOS_DEL, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	/* Clear all qos associate data for this interface */
	memset(&repr_priv->qos_table, 0, sizeof(struct nfp_fl_qos));
	config = nfp_flower_cmsg_get_data(skb);
	memset(config, 0, sizeof(struct nfp_police_config));
	config->head.port = cpu_to_be32(netdev_port_id);
	nfp_ctrl_tx(repr->app->ctrl, skb);

	return 0;
}

int nfp_flower_setup_qos_offload(struct nfp_app *app, struct net_device *netdev,
				 struct tc_cls_matchall_offload *flow)
{
	struct netlink_ext_ack *extack = flow->common.extack;
	struct nfp_flower_priv *fl_priv = app->priv;

	if (!(fl_priv->flower_ext_feats & NFP_FL_FEATS_VF_RLIM)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: loaded firmware does not support qos rate limit offload");
		return -EOPNOTSUPP;
	}

	switch (flow->command) {
	case TC_CLSMATCHALL_REPLACE:
		return nfp_flower_install_rate_limiter(app, netdev, flow,
						       extack);
	case TC_CLSMATCHALL_DESTROY:
		return nfp_flower_remove_rate_limiter(app, netdev, flow,
						      extack);
	default:
		return -EOPNOTSUPP;
	}
}
