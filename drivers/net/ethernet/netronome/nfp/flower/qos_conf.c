// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include <linux/math64.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>

#include "cmsg.h"
#include "main.h"
#include "../nfp_port.h"

#define NFP_FL_QOS_UPDATE		msecs_to_jiffies(1000)
#define NFP_FL_QOS_PPS  BIT(15)

struct nfp_police_cfg_head {
	__be32 flags_opts;
	__be32 port;
};

enum NFP_FL_QOS_TYPES {
	NFP_FL_QOS_TYPE_BPS,
	NFP_FL_QOS_TYPE_PPS,
	NFP_FL_QOS_TYPE_MAX,
};

/* Police cmsg for configuring a trTCM traffic conditioner (8W/32B)
 * See RFC 2698 for more details.
 * ----------------------------------------------------------------
 *    3                   2                   1
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |             Reserved          |p|         Reserved            |
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
 * Word[0](FLag options):
 * [15] p(pps) 1 for pps ,0 for bps
 *
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

struct nfp_police_stats_reply {
	struct nfp_police_cfg_head head;
	__be64 pass_bytes;
	__be64 pass_pkts;
	__be64 drop_bytes;
	__be64 drop_pkts;
};

static int
nfp_flower_install_rate_limiter(struct nfp_app *app, struct net_device *netdev,
				struct tc_cls_matchall_offload *flow,
				struct netlink_ext_ack *extack)
{
	struct flow_action_entry *paction = &flow->rule->action.entries[0];
	u32 action_num = flow->rule->action.num_entries;
	struct nfp_flower_priv *fl_priv = app->priv;
	struct flow_action_entry *action = NULL;
	struct nfp_flower_repr_priv *repr_priv;
	struct nfp_police_config *config;
	u32 netdev_port_id, i;
	struct nfp_repr *repr;
	struct sk_buff *skb;
	bool pps_support;
	u32 bps_num = 0;
	u32 pps_num = 0;
	u32 burst;
	u64 rate;

	if (!nfp_netdev_is_nfp_repr(netdev)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload not supported on higher level port");
		return -EOPNOTSUPP;
	}
	repr = netdev_priv(netdev);
	repr_priv = repr->app_priv;
	netdev_port_id = nfp_repr_get_port_id(netdev);
	pps_support = !!(fl_priv->flower_ext_feats & NFP_FL_FEATS_QOS_PPS);

	if (repr_priv->block_shared) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload not supported on shared blocks");
		return -EOPNOTSUPP;
	}

	if (repr->port->type != NFP_PORT_VF_PORT) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload not supported on non-VF ports");
		return -EOPNOTSUPP;
	}

	if (pps_support) {
		if (action_num > 2 || action_num == 0) {
			NL_SET_ERR_MSG_MOD(extack,
					   "unsupported offload: qos rate limit offload only support action number 1 or 2");
			return -EOPNOTSUPP;
		}
	} else {
		if (!flow_offload_has_one_action(&flow->rule->action)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "unsupported offload: qos rate limit offload requires a single action");
			return -EOPNOTSUPP;
		}
	}

	if (flow->common.prio != 1) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload requires highest priority");
		return -EOPNOTSUPP;
	}

	for (i = 0 ; i < action_num; i++) {
		action = paction + i;
		if (action->id != FLOW_ACTION_POLICE) {
			NL_SET_ERR_MSG_MOD(extack,
					   "unsupported offload: qos rate limit offload requires police action");
			return -EOPNOTSUPP;
		}
		if (action->police.rate_bytes_ps > 0) {
			if (bps_num++) {
				NL_SET_ERR_MSG_MOD(extack,
						   "unsupported offload: qos rate limit offload only support one BPS action");
				return -EOPNOTSUPP;
			}
		}
		if (action->police.rate_pkt_ps > 0) {
			if (!pps_support) {
				NL_SET_ERR_MSG_MOD(extack,
						   "unsupported offload: FW does not support PPS action");
				return -EOPNOTSUPP;
			}
			if (pps_num++) {
				NL_SET_ERR_MSG_MOD(extack,
						   "unsupported offload: qos rate limit offload only support one PPS action");
				return -EOPNOTSUPP;
			}
		}
	}

	for (i = 0 ; i < action_num; i++) {
		/* Set QoS data for this interface */
		action = paction + i;
		if (action->police.rate_bytes_ps > 0) {
			rate = action->police.rate_bytes_ps;
			burst = action->police.burst;
		} else if (action->police.rate_pkt_ps > 0) {
			rate = action->police.rate_pkt_ps;
			burst = action->police.burst_pkt;
		} else {
			NL_SET_ERR_MSG_MOD(extack,
					   "unsupported offload: qos rate limit is not BPS or PPS");
			continue;
		}

		if (rate != 0) {
			skb = nfp_flower_cmsg_alloc(repr->app, sizeof(struct nfp_police_config),
						    NFP_FLOWER_CMSG_TYPE_QOS_MOD, GFP_KERNEL);
			if (!skb)
				return -ENOMEM;

			config = nfp_flower_cmsg_get_data(skb);
			memset(config, 0, sizeof(struct nfp_police_config));
			if (action->police.rate_pkt_ps > 0)
				config->head.flags_opts = cpu_to_be32(NFP_FL_QOS_PPS);
			config->head.port = cpu_to_be32(netdev_port_id);
			config->bkt_tkn_p = cpu_to_be32(burst);
			config->bkt_tkn_c = cpu_to_be32(burst);
			config->pbs = cpu_to_be32(burst);
			config->cbs = cpu_to_be32(burst);
			config->pir = cpu_to_be32(rate);
			config->cir = cpu_to_be32(rate);
			nfp_ctrl_tx(repr->app->ctrl, skb);
		}
	}
	repr_priv->qos_table.netdev_port_id = netdev_port_id;
	fl_priv->qos_rate_limiters++;
	if (fl_priv->qos_rate_limiters == 1)
		schedule_delayed_work(&fl_priv->qos_stats_work,
				      NFP_FL_QOS_UPDATE);

	return 0;
}

static int
nfp_flower_remove_rate_limiter(struct nfp_app *app, struct net_device *netdev,
			       struct tc_cls_matchall_offload *flow,
			       struct netlink_ext_ack *extack)
{
	struct nfp_flower_priv *fl_priv = app->priv;
	struct nfp_flower_repr_priv *repr_priv;
	struct nfp_police_config *config;
	u32 netdev_port_id, i;
	struct nfp_repr *repr;
	struct sk_buff *skb;
	bool pps_support;

	if (!nfp_netdev_is_nfp_repr(netdev)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload not supported on higher level port");
		return -EOPNOTSUPP;
	}
	repr = netdev_priv(netdev);

	netdev_port_id = nfp_repr_get_port_id(netdev);
	repr_priv = repr->app_priv;
	pps_support = !!(fl_priv->flower_ext_feats & NFP_FL_FEATS_QOS_PPS);

	if (!repr_priv->qos_table.netdev_port_id) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: cannot remove qos entry that does not exist");
		return -EOPNOTSUPP;
	}

	memset(&repr_priv->qos_table, 0, sizeof(struct nfp_fl_qos));
	fl_priv->qos_rate_limiters--;
	if (!fl_priv->qos_rate_limiters)
		cancel_delayed_work_sync(&fl_priv->qos_stats_work);
	for (i = 0 ; i < NFP_FL_QOS_TYPE_MAX; i++) {
		if (i == NFP_FL_QOS_TYPE_PPS && !pps_support)
			break;
		/* 0:bps 1:pps
		 * Clear QoS data for this interface.
		 * There is no need to check if a specific QOS_TYPE was
		 * configured as the firmware handles clearing a QoS entry
		 * safely, even if it wasn't explicitly added.
		 */
		skb = nfp_flower_cmsg_alloc(repr->app, sizeof(struct nfp_police_config),
					    NFP_FLOWER_CMSG_TYPE_QOS_DEL, GFP_KERNEL);
		if (!skb)
			return -ENOMEM;

		config = nfp_flower_cmsg_get_data(skb);
		memset(config, 0, sizeof(struct nfp_police_config));
		if (i == NFP_FL_QOS_TYPE_PPS)
			config->head.flags_opts = cpu_to_be32(NFP_FL_QOS_PPS);
		config->head.port = cpu_to_be32(netdev_port_id);
		nfp_ctrl_tx(repr->app->ctrl, skb);
	}

	return 0;
}

void nfp_flower_stats_rlim_reply(struct nfp_app *app, struct sk_buff *skb)
{
	struct nfp_flower_priv *fl_priv = app->priv;
	struct nfp_flower_repr_priv *repr_priv;
	struct nfp_police_stats_reply *msg;
	struct nfp_stat_pair *curr_stats;
	struct nfp_stat_pair *prev_stats;
	struct net_device *netdev;
	struct nfp_repr *repr;
	u32 netdev_port_id;

	msg = nfp_flower_cmsg_get_data(skb);
	netdev_port_id = be32_to_cpu(msg->head.port);
	rcu_read_lock();
	netdev = nfp_app_dev_get(app, netdev_port_id, NULL);
	if (!netdev)
		goto exit_unlock_rcu;

	repr = netdev_priv(netdev);
	repr_priv = repr->app_priv;
	curr_stats = &repr_priv->qos_table.curr_stats;
	prev_stats = &repr_priv->qos_table.prev_stats;

	spin_lock_bh(&fl_priv->qos_stats_lock);
	curr_stats->pkts = be64_to_cpu(msg->pass_pkts) +
			   be64_to_cpu(msg->drop_pkts);
	curr_stats->bytes = be64_to_cpu(msg->pass_bytes) +
			    be64_to_cpu(msg->drop_bytes);

	if (!repr_priv->qos_table.last_update) {
		prev_stats->pkts = curr_stats->pkts;
		prev_stats->bytes = curr_stats->bytes;
	}

	repr_priv->qos_table.last_update = jiffies;
	spin_unlock_bh(&fl_priv->qos_stats_lock);

exit_unlock_rcu:
	rcu_read_unlock();
}

static void
nfp_flower_stats_rlim_request(struct nfp_flower_priv *fl_priv,
			      u32 netdev_port_id)
{
	struct nfp_police_cfg_head *head;
	struct sk_buff *skb;

	skb = nfp_flower_cmsg_alloc(fl_priv->app,
				    sizeof(struct nfp_police_cfg_head),
				    NFP_FLOWER_CMSG_TYPE_QOS_STATS,
				    GFP_ATOMIC);
	if (!skb)
		return;

	head = nfp_flower_cmsg_get_data(skb);
	memset(head, 0, sizeof(struct nfp_police_cfg_head));
	head->port = cpu_to_be32(netdev_port_id);

	nfp_ctrl_tx(fl_priv->app->ctrl, skb);
}

static void
nfp_flower_stats_rlim_request_all(struct nfp_flower_priv *fl_priv)
{
	struct nfp_reprs *repr_set;
	int i;

	rcu_read_lock();
	repr_set = rcu_dereference(fl_priv->app->reprs[NFP_REPR_TYPE_VF]);
	if (!repr_set)
		goto exit_unlock_rcu;

	for (i = 0; i < repr_set->num_reprs; i++) {
		struct net_device *netdev;

		netdev = rcu_dereference(repr_set->reprs[i]);
		if (netdev) {
			struct nfp_repr *priv = netdev_priv(netdev);
			struct nfp_flower_repr_priv *repr_priv;
			u32 netdev_port_id;

			repr_priv = priv->app_priv;
			netdev_port_id = repr_priv->qos_table.netdev_port_id;
			if (!netdev_port_id)
				continue;

			nfp_flower_stats_rlim_request(fl_priv, netdev_port_id);
		}
	}

exit_unlock_rcu:
	rcu_read_unlock();
}

static void update_stats_cache(struct work_struct *work)
{
	struct delayed_work *delayed_work;
	struct nfp_flower_priv *fl_priv;

	delayed_work = to_delayed_work(work);
	fl_priv = container_of(delayed_work, struct nfp_flower_priv,
			       qos_stats_work);

	nfp_flower_stats_rlim_request_all(fl_priv);
	schedule_delayed_work(&fl_priv->qos_stats_work, NFP_FL_QOS_UPDATE);
}

static int
nfp_flower_stats_rate_limiter(struct nfp_app *app, struct net_device *netdev,
			      struct tc_cls_matchall_offload *flow,
			      struct netlink_ext_ack *extack)
{
	struct nfp_flower_priv *fl_priv = app->priv;
	struct nfp_flower_repr_priv *repr_priv;
	struct nfp_stat_pair *curr_stats;
	struct nfp_stat_pair *prev_stats;
	u64 diff_bytes, diff_pkts;
	struct nfp_repr *repr;

	if (!nfp_netdev_is_nfp_repr(netdev)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: qos rate limit offload not supported on higher level port");
		return -EOPNOTSUPP;
	}
	repr = netdev_priv(netdev);

	repr_priv = repr->app_priv;
	if (!repr_priv->qos_table.netdev_port_id) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported offload: cannot find qos entry for stats update");
		return -EOPNOTSUPP;
	}

	spin_lock_bh(&fl_priv->qos_stats_lock);
	curr_stats = &repr_priv->qos_table.curr_stats;
	prev_stats = &repr_priv->qos_table.prev_stats;
	diff_pkts = curr_stats->pkts - prev_stats->pkts;
	diff_bytes = curr_stats->bytes - prev_stats->bytes;
	prev_stats->pkts = curr_stats->pkts;
	prev_stats->bytes = curr_stats->bytes;
	spin_unlock_bh(&fl_priv->qos_stats_lock);

	flow_stats_update(&flow->stats, diff_bytes, diff_pkts, 0,
			  repr_priv->qos_table.last_update,
			  FLOW_ACTION_HW_STATS_DELAYED);
	return 0;
}

void nfp_flower_qos_init(struct nfp_app *app)
{
	struct nfp_flower_priv *fl_priv = app->priv;

	spin_lock_init(&fl_priv->qos_stats_lock);
	INIT_DELAYED_WORK(&fl_priv->qos_stats_work, &update_stats_cache);
}

void nfp_flower_qos_cleanup(struct nfp_app *app)
{
	struct nfp_flower_priv *fl_priv = app->priv;

	cancel_delayed_work_sync(&fl_priv->qos_stats_work);
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
	case TC_CLSMATCHALL_STATS:
		return nfp_flower_stats_rate_limiter(app, netdev, flow,
						     extack);
	default:
		return -EOPNOTSUPP;
	}
}
