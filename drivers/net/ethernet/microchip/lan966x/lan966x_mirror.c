// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

int lan966x_mirror_port_add(struct lan966x_port *port,
			    struct flow_action_entry *action,
			    unsigned long mirror_id,
			    bool ingress,
			    struct netlink_ext_ack *extack)
{
	struct lan966x *lan966x = port->lan966x;
	struct lan966x_port *monitor_port;

	if (!lan966x_netdevice_check(action->dev)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Destination not an lan966x port");
		return -EOPNOTSUPP;
	}

	monitor_port = netdev_priv(action->dev);

	if (lan966x->mirror_mask[ingress] & BIT(port->chip_port)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Mirror already exists");
		return -EEXIST;
	}

	if (lan966x->mirror_monitor &&
	    lan966x->mirror_monitor != monitor_port) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot change mirror port while in use");
		return -EBUSY;
	}

	if (port == monitor_port) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot mirror the monitor port");
		return -EINVAL;
	}

	lan966x->mirror_mask[ingress] |= BIT(port->chip_port);

	lan966x->mirror_monitor = monitor_port;
	lan_wr(BIT(monitor_port->chip_port), lan966x, ANA_MIRRORPORTS);

	if (ingress) {
		lan_rmw(ANA_PORT_CFG_SRC_MIRROR_ENA_SET(1),
			ANA_PORT_CFG_SRC_MIRROR_ENA,
			lan966x, ANA_PORT_CFG(port->chip_port));
	} else {
		lan_wr(lan966x->mirror_mask[0], lan966x,
		       ANA_EMIRRORPORTS);
	}

	lan966x->mirror_count++;

	if (ingress)
		port->tc.ingress_mirror_id = mirror_id;
	else
		port->tc.egress_mirror_id = mirror_id;

	return 0;
}

int lan966x_mirror_port_del(struct lan966x_port *port,
			    bool ingress,
			    struct netlink_ext_ack *extack)
{
	struct lan966x *lan966x = port->lan966x;

	if (!(lan966x->mirror_mask[ingress] & BIT(port->chip_port))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "There is no mirroring for this port");
		return -ENOENT;
	}

	lan966x->mirror_mask[ingress] &= ~BIT(port->chip_port);

	if (ingress) {
		lan_rmw(ANA_PORT_CFG_SRC_MIRROR_ENA_SET(0),
			ANA_PORT_CFG_SRC_MIRROR_ENA,
			lan966x, ANA_PORT_CFG(port->chip_port));
	} else {
		lan_wr(lan966x->mirror_mask[0], lan966x,
		       ANA_EMIRRORPORTS);
	}

	lan966x->mirror_count--;

	if (lan966x->mirror_count == 0) {
		lan966x->mirror_monitor = NULL;
		lan_wr(0, lan966x, ANA_MIRRORPORTS);
	}

	if (ingress)
		port->tc.ingress_mirror_id = 0;
	else
		port->tc.egress_mirror_id = 0;

	return 0;
}

void lan966x_mirror_port_stats(struct lan966x_port *port,
			       struct flow_stats *stats,
			       bool ingress)
{
	struct rtnl_link_stats64 new_stats;
	struct flow_stats *old_stats;

	old_stats = &port->tc.mirror_stat;
	lan966x_stats_get(port->dev, &new_stats);

	if (ingress) {
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
	} else {
		flow_stats_update(stats,
				  new_stats.tx_bytes - old_stats->bytes,
				  new_stats.tx_packets - old_stats->pkts,
				  new_stats.tx_dropped - old_stats->drops,
				  old_stats->lastused,
				  FLOW_ACTION_HW_STATS_IMMEDIATE);

		old_stats->bytes = new_stats.tx_bytes;
		old_stats->pkts = new_stats.tx_packets;
		old_stats->drops = new_stats.tx_dropped;
		old_stats->lastused = jiffies;
	}
}
