// SPDX-License-Identifier: GPL-2.0

#include <linux/netdevice.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>

#include "netdevsim.h"

static int
nsim_setup_tc_block_cb(enum tc_setup_type type, void *type_data, void *cb_priv)
{
	return nsim_bpf_setup_tc_block_cb(type, type_data, cb_priv);
}

static void nsim_taprio_stats(struct tc_taprio_qopt_stats *stats)
{
	stats->window_drops = 0;
	stats->tx_overruns = 0;
}

static int nsim_setup_tc_taprio(struct net_device *dev,
				struct tc_taprio_qopt_offload *offload)
{
	int err = 0;

	switch (offload->cmd) {
	case TAPRIO_CMD_REPLACE:
	case TAPRIO_CMD_DESTROY:
		break;
	case TAPRIO_CMD_STATS:
		nsim_taprio_stats(&offload->stats);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

static int nsim_setup_tc_ets(struct net_device *dev,
			     struct tc_ets_qopt_offload *offload)
{
	int err = 0;

	switch (offload->command) {
	case TC_ETS_REPLACE:
	case TC_ETS_DESTROY:
		break;
	case TC_ETS_STATS:
		_bstats_update(offload->stats.bstats, 0, 0);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	return err;
}

static LIST_HEAD(nsim_block_cb_list);

int
nsim_setup_tc(struct net_device *dev, enum tc_setup_type type, void *type_data)
{
	struct netdevsim *ns = netdev_priv(dev);

	switch (type) {
	case TC_SETUP_QDISC_TAPRIO:
		return nsim_setup_tc_taprio(dev, type_data);
	case TC_SETUP_QDISC_ETS:
		return nsim_setup_tc_ets(dev, type_data);
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple(type_data,
						  &nsim_block_cb_list,
						  nsim_setup_tc_block_cb,
						  ns, ns, true);
	default:
		return -EOPNOTSUPP;
	}
}
