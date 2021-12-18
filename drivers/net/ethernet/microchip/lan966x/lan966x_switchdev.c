// SPDX-License-Identifier: GPL-2.0+

#include <linux/if_bridge.h>
#include <net/switchdev.h>

#include "lan966x_main.h"

static struct notifier_block lan966x_netdevice_nb __read_mostly;
static struct notifier_block lan966x_switchdev_nb __read_mostly;
static struct notifier_block lan966x_switchdev_blocking_nb __read_mostly;

static void lan966x_update_fwd_mask(struct lan966x *lan966x)
{
	int i;

	for (i = 0; i < lan966x->num_phys_ports; i++) {
		struct lan966x_port *port = lan966x->ports[i];
		unsigned long mask = 0;

		if (port && lan966x->bridge_fwd_mask & BIT(i))
			mask = lan966x->bridge_fwd_mask & ~BIT(i);

		mask |= BIT(CPU_PORT);

		lan_wr(ANA_PGID_PGID_SET(mask),
		       lan966x, ANA_PGID(PGID_SRC + i));
	}
}

static void lan966x_port_stp_state_set(struct lan966x_port *port, u8 state)
{
	struct lan966x *lan966x = port->lan966x;
	bool learn_ena = false;

	if (state == BR_STATE_FORWARDING || state == BR_STATE_LEARNING)
		learn_ena = true;

	if (state == BR_STATE_FORWARDING)
		lan966x->bridge_fwd_mask |= BIT(port->chip_port);
	else
		lan966x->bridge_fwd_mask &= ~BIT(port->chip_port);

	lan_rmw(ANA_PORT_CFG_LEARN_ENA_SET(learn_ena),
		ANA_PORT_CFG_LEARN_ENA,
		lan966x, ANA_PORT_CFG(port->chip_port));

	lan966x_update_fwd_mask(lan966x);
}

static void lan966x_port_ageing_set(struct lan966x_port *port,
				    unsigned long ageing_clock_t)
{
	unsigned long ageing_jiffies = clock_t_to_jiffies(ageing_clock_t);
	u32 ageing_time = jiffies_to_msecs(ageing_jiffies) / 1000;

	lan966x_mac_set_ageing(port->lan966x, ageing_time);
}

static int lan966x_port_attr_set(struct net_device *dev, const void *ctx,
				 const struct switchdev_attr *attr,
				 struct netlink_ext_ack *extack)
{
	struct lan966x_port *port = netdev_priv(dev);
	int err = 0;

	if (ctx && ctx != port)
		return 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		lan966x_port_stp_state_set(port, attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		lan966x_port_ageing_set(port, attr->u.ageing_time);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int lan966x_port_bridge_join(struct lan966x_port *port,
				    struct net_device *bridge,
				    struct netlink_ext_ack *extack)
{
	struct lan966x *lan966x = port->lan966x;
	struct net_device *dev = port->dev;
	int err;

	if (!lan966x->bridge_mask) {
		lan966x->bridge = bridge;
	} else {
		if (lan966x->bridge != bridge) {
			NL_SET_ERR_MSG_MOD(extack, "Not allow to add port to different bridge");
			return -ENODEV;
		}
	}

	err = switchdev_bridge_port_offload(dev, dev, port,
					    &lan966x_switchdev_nb,
					    &lan966x_switchdev_blocking_nb,
					    false, extack);
	if (err)
		return err;

	lan966x->bridge_mask |= BIT(port->chip_port);

	return 0;
}

static void lan966x_port_bridge_leave(struct lan966x_port *port,
				      struct net_device *bridge)
{
	struct lan966x *lan966x = port->lan966x;

	lan966x->bridge_mask &= ~BIT(port->chip_port);

	if (!lan966x->bridge_mask)
		lan966x->bridge = NULL;

	lan966x_mac_cpu_learn(lan966x, port->dev->dev_addr, PORT_PVID);
}

static int lan966x_port_changeupper(struct net_device *dev,
				    struct netdev_notifier_changeupper_info *info)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct netlink_ext_ack *extack;
	int err = 0;

	extack = netdev_notifier_info_to_extack(&info->info);

	if (netif_is_bridge_master(info->upper_dev)) {
		if (info->linking)
			err = lan966x_port_bridge_join(port, info->upper_dev,
						       extack);
		else
			lan966x_port_bridge_leave(port, info->upper_dev);
	}

	return err;
}

static int lan966x_port_prechangeupper(struct net_device *dev,
				       struct netdev_notifier_changeupper_info *info)
{
	struct lan966x_port *port = netdev_priv(dev);

	if (netif_is_bridge_master(info->upper_dev) && !info->linking)
		switchdev_bridge_port_unoffload(port->dev, port,
						&lan966x_switchdev_nb,
						&lan966x_switchdev_blocking_nb);

	return NOTIFY_DONE;
}

static int lan966x_foreign_bridging_check(struct net_device *bridge,
					  struct netlink_ext_ack *extack)
{
	struct lan966x *lan966x = NULL;
	bool has_foreign = false;
	struct net_device *dev;
	struct list_head *iter;

	if (!netif_is_bridge_master(bridge))
		return 0;

	netdev_for_each_lower_dev(bridge, dev, iter) {
		if (lan966x_netdevice_check(dev)) {
			struct lan966x_port *port = netdev_priv(dev);

			if (lan966x) {
				/* Bridge already has at least one port of a
				 * lan966x switch inside it, check that it's
				 * the same instance of the driver.
				 */
				if (port->lan966x != lan966x) {
					NL_SET_ERR_MSG_MOD(extack,
							   "Bridging between multiple lan966x switches disallowed");
					return -EINVAL;
				}
			} else {
				/* This is the first lan966x port inside this
				 * bridge
				 */
				lan966x = port->lan966x;
			}
		} else {
			has_foreign = true;
		}

		if (lan966x && has_foreign) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Bridging lan966x ports with foreign interfaces disallowed");
			return -EINVAL;
		}
	}

	return 0;
}

static int lan966x_bridge_check(struct net_device *dev,
				struct netdev_notifier_changeupper_info *info)
{
	return lan966x_foreign_bridging_check(info->upper_dev,
					      info->info.extack);
}

static int lan966x_netdevice_port_event(struct net_device *dev,
					struct notifier_block *nb,
					unsigned long event, void *ptr)
{
	int err = 0;

	if (!lan966x_netdevice_check(dev)) {
		if (event == NETDEV_CHANGEUPPER)
			return lan966x_bridge_check(dev, ptr);
		return 0;
	}

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		err = lan966x_port_prechangeupper(dev, ptr);
		break;
	case NETDEV_CHANGEUPPER:
		err = lan966x_bridge_check(dev, ptr);
		if (err)
			return err;

		err = lan966x_port_changeupper(dev, ptr);
		break;
	}

	return err;
}

static int lan966x_netdevice_event(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	int ret;

	ret = lan966x_netdevice_port_event(dev, nb, event, ptr);

	return notifier_from_errno(ret);
}

static int lan966x_switchdev_event(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     lan966x_netdevice_check,
						     lan966x_port_attr_set);
		return notifier_from_errno(err);
	}

	return NOTIFY_DONE;
}

static int lan966x_switchdev_blocking_event(struct notifier_block *nb,
					    unsigned long event,
					    void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	int err;

	switch (event) {
	case SWITCHDEV_PORT_ATTR_SET:
		err = switchdev_handle_port_attr_set(dev, ptr,
						     lan966x_netdevice_check,
						     lan966x_port_attr_set);
		return notifier_from_errno(err);
	}

	return NOTIFY_DONE;
}

static struct notifier_block lan966x_netdevice_nb __read_mostly = {
	.notifier_call = lan966x_netdevice_event,
};

static struct notifier_block lan966x_switchdev_nb __read_mostly = {
	.notifier_call = lan966x_switchdev_event,
};

static struct notifier_block lan966x_switchdev_blocking_nb __read_mostly = {
	.notifier_call = lan966x_switchdev_blocking_event,
};

void lan966x_register_notifier_blocks(void)
{
	register_netdevice_notifier(&lan966x_netdevice_nb);
	register_switchdev_notifier(&lan966x_switchdev_nb);
	register_switchdev_blocking_notifier(&lan966x_switchdev_blocking_nb);
}

void lan966x_unregister_notifier_blocks(void)
{
	unregister_switchdev_blocking_notifier(&lan966x_switchdev_blocking_nb);
	unregister_switchdev_notifier(&lan966x_switchdev_nb);
	unregister_netdevice_notifier(&lan966x_netdevice_nb);
}
