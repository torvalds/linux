// SPDX-License-Identifier: GPL-2.0+

#include <linux/if_bridge.h>

#include "lan966x_main.h"

static void lan966x_lag_set_aggr_pgids(struct lan966x *lan966x)
{
	u32 visited = GENMASK(lan966x->num_phys_ports - 1, 0);
	int p, lag, i;

	/* Reset destination and aggregation PGIDS */
	for (p = 0; p < lan966x->num_phys_ports; ++p)
		lan_wr(ANA_PGID_PGID_SET(BIT(p)),
		       lan966x, ANA_PGID(p));

	for (p = PGID_AGGR; p < PGID_SRC; ++p)
		lan_wr(ANA_PGID_PGID_SET(visited),
		       lan966x, ANA_PGID(p));

	/* The visited ports bitmask holds the list of ports offloading any
	 * bonding interface. Initially we mark all these ports as unvisited,
	 * then every time we visit a port in this bitmask, we know that it is
	 * the lowest numbered port, i.e. the one whose logical ID == physical
	 * port ID == LAG ID. So we mark as visited all further ports in the
	 * bitmask that are offloading the same bonding interface. This way,
	 * we set up the aggregation PGIDs only once per bonding interface.
	 */
	for (p = 0; p < lan966x->num_phys_ports; ++p) {
		struct lan966x_port *port = lan966x->ports[p];

		if (!port || !port->bond)
			continue;

		visited &= ~BIT(p);
	}

	/* Now, set PGIDs for each active LAG */
	for (lag = 0; lag < lan966x->num_phys_ports; ++lag) {
		struct lan966x_port *port = lan966x->ports[lag];
		int num_active_ports = 0;
		struct net_device *bond;
		unsigned long bond_mask;
		u8 aggr_idx[16];

		if (!port || !port->bond || (visited & BIT(lag)))
			continue;

		bond = port->bond;
		bond_mask = lan966x_lag_get_mask(lan966x, bond);

		for_each_set_bit(p, &bond_mask, lan966x->num_phys_ports) {
			struct lan966x_port *port = lan966x->ports[p];

			if (!port)
				continue;

			lan_wr(ANA_PGID_PGID_SET(bond_mask),
			       lan966x, ANA_PGID(p));
			if (port->lag_tx_active)
				aggr_idx[num_active_ports++] = p;
		}

		for (i = PGID_AGGR; i < PGID_SRC; ++i) {
			u32 ac;

			ac = lan_rd(lan966x, ANA_PGID(i));
			ac &= ~bond_mask;
			/* Don't do division by zero if there was no active
			 * port. Just make all aggregation codes zero.
			 */
			if (num_active_ports)
				ac |= BIT(aggr_idx[i % num_active_ports]);
			lan_wr(ANA_PGID_PGID_SET(ac),
			       lan966x, ANA_PGID(i));
		}

		/* Mark all ports in the same LAG as visited to avoid applying
		 * the same config again.
		 */
		for (p = lag; p < lan966x->num_phys_ports; p++) {
			struct lan966x_port *port = lan966x->ports[p];

			if (!port)
				continue;

			if (port->bond == bond)
				visited |= BIT(p);
		}
	}
}

static void lan966x_lag_set_port_ids(struct lan966x *lan966x)
{
	struct lan966x_port *port;
	u32 bond_mask;
	u32 lag_id;
	int p;

	for (p = 0; p < lan966x->num_phys_ports; ++p) {
		port = lan966x->ports[p];
		if (!port)
			continue;

		lag_id = port->chip_port;

		bond_mask = lan966x_lag_get_mask(lan966x, port->bond);
		if (bond_mask)
			lag_id = __ffs(bond_mask);

		lan_rmw(ANA_PORT_CFG_PORTID_VAL_SET(lag_id),
			ANA_PORT_CFG_PORTID_VAL,
			lan966x, ANA_PORT_CFG(port->chip_port));
	}
}

static void lan966x_lag_update_ids(struct lan966x *lan966x)
{
	lan966x_lag_set_port_ids(lan966x);
	lan966x_update_fwd_mask(lan966x);
	lan966x_lag_set_aggr_pgids(lan966x);
}

int lan966x_lag_port_join(struct lan966x_port *port,
			  struct net_device *brport_dev,
			  struct net_device *bond,
			  struct netlink_ext_ack *extack)
{
	struct lan966x *lan966x = port->lan966x;
	struct net_device *dev = port->dev;
	u32 lag_id = -1;
	u32 bond_mask;
	int err;

	bond_mask = lan966x_lag_get_mask(lan966x, bond);
	if (bond_mask)
		lag_id = __ffs(bond_mask);

	port->bond = bond;
	lan966x_lag_update_ids(lan966x);

	err = switchdev_bridge_port_offload(brport_dev, dev, port,
					    &lan966x_switchdev_nb,
					    &lan966x_switchdev_blocking_nb,
					    false, extack);
	if (err)
		goto out;

	lan966x_port_stp_state_set(port, br_port_get_stp_state(brport_dev));

	if (lan966x_lag_first_port(port->bond, port->dev) &&
	    lag_id != -1)
		lan966x_mac_lag_replace_port_entry(lan966x,
						   lan966x->ports[lag_id],
						   port);

	return 0;

out:
	port->bond = NULL;
	lan966x_lag_update_ids(lan966x);

	return err;
}

void lan966x_lag_port_leave(struct lan966x_port *port, struct net_device *bond)
{
	struct lan966x *lan966x = port->lan966x;
	u32 bond_mask;
	u32 lag_id;

	if (lan966x_lag_first_port(port->bond, port->dev)) {
		bond_mask = lan966x_lag_get_mask(lan966x, port->bond);
		bond_mask &= ~BIT(port->chip_port);
		if (bond_mask) {
			lag_id = __ffs(bond_mask);
			lan966x_mac_lag_replace_port_entry(lan966x, port,
							   lan966x->ports[lag_id]);
		} else {
			lan966x_mac_lag_remove_port_entry(lan966x, port);
		}
	}

	port->bond = NULL;
	lan966x_lag_update_ids(lan966x);
	lan966x_port_stp_state_set(port, BR_STATE_FORWARDING);
}

static bool lan966x_lag_port_check_hash_types(struct lan966x *lan966x,
					      enum netdev_lag_hash hash_type)
{
	int p;

	for (p = 0; p < lan966x->num_phys_ports; ++p) {
		struct lan966x_port *port = lan966x->ports[p];

		if (!port || !port->bond)
			continue;

		if (port->hash_type != hash_type)
			return false;
	}

	return true;
}

int lan966x_lag_port_prechangeupper(struct net_device *dev,
				    struct netdev_notifier_changeupper_info *info)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	struct netdev_lag_upper_info *lui;
	struct netlink_ext_ack *extack;

	extack = netdev_notifier_info_to_extack(&info->info);
	lui = info->upper_info;
	if (!lui) {
		port->hash_type = NETDEV_LAG_HASH_NONE;
		return NOTIFY_DONE;
	}

	if (lui->tx_type != NETDEV_LAG_TX_TYPE_HASH) {
		NL_SET_ERR_MSG_MOD(extack,
				   "LAG device using unsupported Tx type");
		return -EINVAL;
	}

	if (!lan966x_lag_port_check_hash_types(lan966x, lui->hash_type)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "LAG devices can have only the same hash_type");
		return -EINVAL;
	}

	switch (lui->hash_type) {
	case NETDEV_LAG_HASH_L2:
		lan_wr(ANA_AGGR_CFG_AC_DMAC_ENA_SET(1) |
		       ANA_AGGR_CFG_AC_SMAC_ENA_SET(1),
		       lan966x, ANA_AGGR_CFG);
		break;
	case NETDEV_LAG_HASH_L34:
		lan_wr(ANA_AGGR_CFG_AC_IP6_TCPUDP_ENA_SET(1) |
		       ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA_SET(1) |
		       ANA_AGGR_CFG_AC_IP4_SIPDIP_ENA_SET(1),
		       lan966x, ANA_AGGR_CFG);
		break;
	case NETDEV_LAG_HASH_L23:
		lan_wr(ANA_AGGR_CFG_AC_DMAC_ENA_SET(1) |
		       ANA_AGGR_CFG_AC_SMAC_ENA_SET(1) |
		       ANA_AGGR_CFG_AC_IP6_TCPUDP_ENA_SET(1) |
		       ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA_SET(1),
		       lan966x, ANA_AGGR_CFG);
		break;
	default:
		NL_SET_ERR_MSG_MOD(extack,
				   "LAG device using unsupported hash type");
		return -EINVAL;
	}

	port->hash_type = lui->hash_type;

	return NOTIFY_OK;
}

int lan966x_lag_port_changelowerstate(struct net_device *dev,
				      struct netdev_notifier_changelowerstate_info *info)
{
	struct netdev_lag_lower_state_info *lag = info->lower_state_info;
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	bool is_active;

	if (!port->bond)
		return NOTIFY_DONE;

	is_active = lag->link_up && lag->tx_enabled;
	if (port->lag_tx_active == is_active)
		return NOTIFY_DONE;

	port->lag_tx_active = is_active;
	lan966x_lag_set_aggr_pgids(lan966x);

	return NOTIFY_OK;
}

int lan966x_lag_netdev_prechangeupper(struct net_device *dev,
				      struct netdev_notifier_changeupper_info *info)
{
	struct lan966x_port *port;
	struct net_device *lower;
	struct list_head *iter;
	int err;

	netdev_for_each_lower_dev(dev, lower, iter) {
		if (!lan966x_netdevice_check(lower))
			continue;

		port = netdev_priv(lower);
		if (port->bond != dev)
			continue;

		err = lan966x_port_prechangeupper(lower, dev, info);
		if (err)
			return err;
	}

	return NOTIFY_DONE;
}

int lan966x_lag_netdev_changeupper(struct net_device *dev,
				   struct netdev_notifier_changeupper_info *info)
{
	struct lan966x_port *port;
	struct net_device *lower;
	struct list_head *iter;
	int err;

	netdev_for_each_lower_dev(dev, lower, iter) {
		if (!lan966x_netdevice_check(lower))
			continue;

		port = netdev_priv(lower);
		if (port->bond != dev)
			continue;

		err = lan966x_port_changeupper(lower, dev, info);
		if (err)
			return err;
	}

	return NOTIFY_DONE;
}

bool lan966x_lag_first_port(struct net_device *lag, struct net_device *dev)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	unsigned long bond_mask;

	if (port->bond != lag)
		return false;

	bond_mask = lan966x_lag_get_mask(lan966x, lag);
	if (bond_mask && port->chip_port == __ffs(bond_mask))
		return true;

	return false;
}

u32 lan966x_lag_get_mask(struct lan966x *lan966x, struct net_device *bond)
{
	struct lan966x_port *port;
	u32 mask = 0;
	int p;

	if (!bond)
		return mask;

	for (p = 0; p < lan966x->num_phys_ports; p++) {
		port = lan966x->ports[p];
		if (!port)
			continue;

		if (port->bond == bond)
			mask |= BIT(p);
	}

	return mask;
}
