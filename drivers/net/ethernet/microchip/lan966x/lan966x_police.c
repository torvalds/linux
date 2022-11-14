// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

/* 0-8 : 9 port policers */
#define POL_IDX_PORT	0

/* Policer order: Serial (QoS -> Port -> VCAP) */
#define POL_ORDER	0x1d3

struct lan966x_tc_policer {
	/* kilobit per second */
	u32 rate;
	/* bytes */
	u32 burst;
};

static int lan966x_police_add(struct lan966x_port *port,
			      struct lan966x_tc_policer *pol,
			      u16 pol_idx)
{
	struct lan966x *lan966x = port->lan966x;

	/* Rate unit is 33 1/3 kpps */
	pol->rate = DIV_ROUND_UP(pol->rate * 3, 100);
	/* Avoid zero burst size */
	pol->burst = pol->burst ?: 1;
	/* Unit is 4kB */
	pol->burst = DIV_ROUND_UP(pol->burst, 4096);

	if (pol->rate > GENMASK(15, 0) ||
	    pol->burst > GENMASK(6, 0))
		return -EINVAL;

	lan_wr(ANA_POL_MODE_DROP_ON_YELLOW_ENA_SET(0) |
	       ANA_POL_MODE_MARK_ALL_FRMS_RED_ENA_SET(0) |
	       ANA_POL_MODE_IPG_SIZE_SET(20) |
	       ANA_POL_MODE_FRM_MODE_SET(1) |
	       ANA_POL_MODE_OVERSHOOT_ENA_SET(1),
	       lan966x, ANA_POL_MODE(pol_idx));

	lan_wr(ANA_POL_PIR_STATE_PIR_LVL_SET(0),
	       lan966x, ANA_POL_PIR_STATE(pol_idx));

	lan_wr(ANA_POL_PIR_CFG_PIR_RATE_SET(pol->rate) |
	       ANA_POL_PIR_CFG_PIR_BURST_SET(pol->burst),
	       lan966x, ANA_POL_PIR_CFG(pol_idx));

	return 0;
}

static int lan966x_police_del(struct lan966x_port *port,
			      u16 pol_idx)
{
	struct lan966x *lan966x = port->lan966x;

	lan_wr(ANA_POL_MODE_DROP_ON_YELLOW_ENA_SET(0) |
	       ANA_POL_MODE_MARK_ALL_FRMS_RED_ENA_SET(0) |
	       ANA_POL_MODE_IPG_SIZE_SET(20) |
	       ANA_POL_MODE_FRM_MODE_SET(2) |
	       ANA_POL_MODE_OVERSHOOT_ENA_SET(1),
	       lan966x, ANA_POL_MODE(pol_idx));

	lan_wr(ANA_POL_PIR_STATE_PIR_LVL_SET(0),
	       lan966x, ANA_POL_PIR_STATE(pol_idx));

	lan_wr(ANA_POL_PIR_CFG_PIR_RATE_SET(GENMASK(14, 0)) |
	       ANA_POL_PIR_CFG_PIR_BURST_SET(0),
	       lan966x, ANA_POL_PIR_CFG(pol_idx));

	return 0;
}

static int lan966x_police_validate(struct lan966x_port *port,
				   const struct flow_action *action,
				   const struct flow_action_entry *act,
				   unsigned long police_id,
				   bool ingress,
				   struct netlink_ext_ack *extack)
{
	if (act->police.exceed.act_id != FLOW_ACTION_DROP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when exceed action is not drop");
		return -EOPNOTSUPP;
	}

	if (act->police.notexceed.act_id != FLOW_ACTION_PIPE &&
	    act->police.notexceed.act_id != FLOW_ACTION_ACCEPT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is not pipe or ok");
		return -EOPNOTSUPP;
	}

	if (act->police.notexceed.act_id == FLOW_ACTION_ACCEPT &&
	    !flow_action_is_last_entry(action, act)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when conform action is ok, but action is not last");
		return -EOPNOTSUPP;
	}

	if (act->police.peakrate_bytes_ps ||
	    act->police.avrate || act->police.overhead) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Offload not supported when peakrate/avrate/overhead is configured");
		return -EOPNOTSUPP;
	}

	if (act->police.rate_pkt_ps) {
		NL_SET_ERR_MSG_MOD(extack,
				   "QoS offload not support packets per second");
		return -EOPNOTSUPP;
	}

	if (!ingress) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Policer is not supported on egress");
		return -EOPNOTSUPP;
	}

	if (port->tc.ingress_shared_block) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Policer is not supported on shared ingress blocks");
		return -EOPNOTSUPP;
	}

	if (port->tc.police_id && port->tc.police_id != police_id) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only one policer per port is supported");
		return -EEXIST;
	}

	return 0;
}

int lan966x_police_port_add(struct lan966x_port *port,
			    struct flow_action *action,
			    struct flow_action_entry *act,
			    unsigned long police_id,
			    bool ingress,
			    struct netlink_ext_ack *extack)
{
	struct lan966x *lan966x = port->lan966x;
	struct rtnl_link_stats64 new_stats;
	struct lan966x_tc_policer pol;
	struct flow_stats *old_stats;
	int err;

	err = lan966x_police_validate(port, action, act, police_id, ingress,
				      extack);
	if (err)
		return err;

	memset(&pol, 0, sizeof(pol));

	pol.rate = div_u64(act->police.rate_bytes_ps, 1000) * 8;
	pol.burst = act->police.burst;

	err = lan966x_police_add(port, &pol, POL_IDX_PORT + port->chip_port);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to add policer to port");
		return err;
	}

	lan_rmw(ANA_POL_CFG_PORT_POL_ENA_SET(1) |
		ANA_POL_CFG_POL_ORDER_SET(POL_ORDER),
		ANA_POL_CFG_PORT_POL_ENA |
		ANA_POL_CFG_POL_ORDER,
		lan966x, ANA_POL_CFG(port->chip_port));

	port->tc.police_id = police_id;

	/* Setup initial stats */
	old_stats = &port->tc.police_stat;
	lan966x_stats_get(port->dev, &new_stats);
	old_stats->bytes = new_stats.rx_bytes;
	old_stats->pkts = new_stats.rx_packets;
	old_stats->drops = new_stats.rx_dropped;
	old_stats->lastused = jiffies;

	return 0;
}

int lan966x_police_port_del(struct lan966x_port *port,
			    unsigned long police_id,
			    struct netlink_ext_ack *extack)
{
	struct lan966x *lan966x = port->lan966x;
	int err;

	if (port->tc.police_id != police_id) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Invalid policer id");
		return -EINVAL;
	}

	err = lan966x_police_del(port, port->tc.police_id);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to add policer to port");
		return err;
	}

	lan_rmw(ANA_POL_CFG_PORT_POL_ENA_SET(0) |
		ANA_POL_CFG_POL_ORDER_SET(POL_ORDER),
		ANA_POL_CFG_PORT_POL_ENA |
		ANA_POL_CFG_POL_ORDER,
		lan966x, ANA_POL_CFG(port->chip_port));

	port->tc.police_id = 0;

	return 0;
}

void lan966x_police_port_stats(struct lan966x_port *port,
			       struct flow_stats *stats)
{
	struct rtnl_link_stats64 new_stats;
	struct flow_stats *old_stats;

	old_stats = &port->tc.police_stat;
	lan966x_stats_get(port->dev, &new_stats);

	flow_stats_update(stats,
			  new_stats.rx_bytes - old_stats->bytes,
			  new_stats.rx_packets - old_stats->pkts,
			  new_stats.rx_dropped - old_stats->drops,
			  old_stats->lastused,
			  FLOW_ACTION_HW_STATS_IMMEDIATE);

	old_stats->bytes = new_stats.rx_bytes;
	old_stats->pkts = new_stats.rx_packets;
	old_stats->drops = new_stats.rx_dropped;
	old_stats->lastused = jiffies;
}
