// SPDX-License-Identifier: GPL-2.0
/* Renesas Ethernet Switch device driver
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/kernel.h>
#include <net/switchdev.h>

#include "rswitch.h"
#include "rswitch_l2.h"

static bool rdev_for_l2_offload(struct rswitch_device *rdev)
{
	return rdev->priv->offload_brdev &&
	       rdev->brdev == rdev->priv->offload_brdev &&
	       (test_bit(rdev->port, rdev->priv->opened_ports));
}

static void rswitch_change_l2_hw_offloading(struct rswitch_device *rdev,
					    bool start, bool learning)
{
	u32 bits = learning ? FWPC0_MACSSA | FWPC0_MACHLA | FWPC0_MACHMA : FWPC0_MACDSA;
	u32 clear = start ? 0 : bits;
	u32 set = start ? bits : 0;

	if ((learning && rdev->learning_offloaded == start) ||
	    (!learning && rdev->forwarding_offloaded == start))
		return;

	rswitch_modify(rdev->priv->addr, FWPC0(rdev->port), clear, set);

	if (learning)
		rdev->learning_offloaded = start;
	else
		rdev->forwarding_offloaded = start;

	netdev_info(rdev->ndev, "%s hw %s\n", start ? "starting" : "stopping",
		    learning ? "learning" : "forwarding");
}

static void rswitch_update_l2_hw_learning(struct rswitch_private *priv)
{
	struct rswitch_device *rdev;
	bool learning_needed;

	rswitch_for_all_ports(priv, rdev) {
		if (rdev_for_l2_offload(rdev))
			learning_needed = rdev->learning_requested;
		else
			learning_needed = false;

		rswitch_change_l2_hw_offloading(rdev, learning_needed, true);
	}
}

static void rswitch_update_l2_hw_forwarding(struct rswitch_private *priv)
{
	struct rswitch_device *rdev;
	unsigned int fwd_mask;

	/* calculate fwd_mask with zeroes in bits corresponding to ports that
	 * shall participate in hardware forwarding
	 */
	fwd_mask = GENMASK(RSWITCH_NUM_AGENTS - 1, 0);

	rswitch_for_all_ports(priv, rdev) {
		if (rdev_for_l2_offload(rdev) && rdev->forwarding_requested)
			fwd_mask &= ~BIT(rdev->port);
	}

	rswitch_for_all_ports(priv, rdev) {
		if ((rdev_for_l2_offload(rdev) && rdev->forwarding_requested) ||
		    rdev->forwarding_offloaded) {
			/* Update allowed offload destinations even for ports
			 * with L2 offload enabled earlier.
			 *
			 * Do not allow L2 forwarding to self for hw port.
			 */
			iowrite32(FIELD_PREP(FWCP2_LTWFW_MASK, fwd_mask | BIT(rdev->port)),
				  priv->addr + FWPC2(rdev->port));
		}

		if (rdev_for_l2_offload(rdev) &&
		    rdev->forwarding_requested &&
		    !rdev->forwarding_offloaded) {
			rswitch_change_l2_hw_offloading(rdev, true, false);
		} else if (rdev->forwarding_offloaded) {
			rswitch_change_l2_hw_offloading(rdev, false, false);
		}
	}
}

void rswitch_update_l2_offload(struct rswitch_private *priv)
{
	rswitch_update_l2_hw_learning(priv);
	rswitch_update_l2_hw_forwarding(priv);
}

static void rswitch_update_offload_brdev(struct rswitch_private *priv)
{
	struct net_device *offload_brdev = NULL;
	struct rswitch_device *rdev, *rdev2;

	rswitch_for_all_ports(priv, rdev) {
		if (!rdev->brdev)
			continue;
		rswitch_for_all_ports(priv, rdev2) {
			if (rdev2 == rdev)
				break;
			if (rdev2->brdev == rdev->brdev) {
				offload_brdev = rdev->brdev;
				break;
			}
		}
		if (offload_brdev)
			break;
	}

	if (offload_brdev == priv->offload_brdev)
		dev_dbg(&priv->pdev->dev,
			"changing l2 offload from %s to %s\n",
			netdev_name(priv->offload_brdev),
			netdev_name(offload_brdev));
	else if (offload_brdev)
		dev_dbg(&priv->pdev->dev, "starting l2 offload for %s\n",
			netdev_name(offload_brdev));
	else if (!offload_brdev)
		dev_dbg(&priv->pdev->dev, "stopping l2 offload for %s\n",
			netdev_name(priv->offload_brdev));

	priv->offload_brdev = offload_brdev;

	rswitch_update_l2_offload(priv);
}

static bool rswitch_port_check(const struct net_device *ndev)
{
	return is_rdev(ndev);
}

static void rswitch_port_update_brdev(struct net_device *ndev,
				      struct net_device *brdev)
{
	struct rswitch_device *rdev;

	if (!is_rdev(ndev))
		return;

	rdev = netdev_priv(ndev);
	rdev->brdev = brdev;
	rswitch_update_offload_brdev(rdev->priv);
}

static int rswitch_port_update_stp_state(struct net_device *ndev, u8 stp_state)
{
	struct rswitch_device *rdev;

	if (!is_rdev(ndev))
		return -ENODEV;

	rdev = netdev_priv(ndev);
	rdev->learning_requested = (stp_state == BR_STATE_LEARNING ||
				    stp_state == BR_STATE_FORWARDING);
	rdev->forwarding_requested = (stp_state == BR_STATE_FORWARDING);
	rswitch_update_l2_offload(rdev->priv);

	return 0;
}

static int rswitch_netdevice_event(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info;
	struct net_device *brdev;

	if (!rswitch_port_check(ndev))
		return NOTIFY_DONE;
	if (event != NETDEV_CHANGEUPPER)
		return NOTIFY_DONE;

	info = ptr;

	if (netif_is_bridge_master(info->upper_dev)) {
		brdev = info->linking ? info->upper_dev : NULL;
		rswitch_port_update_brdev(ndev, brdev);
	}

	return NOTIFY_OK;
}

static int rswitch_update_ageing_time(struct net_device *ndev, clock_t time)
{
	struct rswitch_device *rdev = netdev_priv(ndev);
	u32 reg_val;

	if (!is_rdev(ndev))
		return -ENODEV;

	if (!FIELD_FIT(FWMACAGC_MACAGT, time))
		return -EINVAL;

	reg_val = FIELD_PREP(FWMACAGC_MACAGT, time);
	reg_val |= FWMACAGC_MACAGE | FWMACAGC_MACAGSL;
	iowrite32(reg_val, rdev->priv->addr + FWMACAGC);

	return 0;
}

static int rswitch_port_attr_set(struct net_device *ndev, const void *ctx,
				 const struct switchdev_attr *attr,
				 struct netlink_ext_ack *extack)
{
	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		return rswitch_port_update_stp_state(ndev, attr->u.stp_state);
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		return rswitch_update_ageing_time(ndev, attr->u.ageing_time);
	default:
		return -EOPNOTSUPP;
	}
}

static int rswitch_switchdev_event(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct net_device *ndev = switchdev_notifier_info_to_dev(ptr);
	int ret;

	if (event == SWITCHDEV_PORT_ATTR_SET) {
		ret = switchdev_handle_port_attr_set(ndev, ptr,
						     rswitch_port_check,
						     rswitch_port_attr_set);
		return notifier_from_errno(ret);
	}

	if (!rswitch_port_check(ndev))
		return NOTIFY_DONE;

	return notifier_from_errno(-EOPNOTSUPP);
}

static int rswitch_switchdev_blocking_event(struct notifier_block *nb,
					    unsigned long event, void *ptr)
{
	struct net_device *ndev = switchdev_notifier_info_to_dev(ptr);
	int ret;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		return -EOPNOTSUPP;
	case SWITCHDEV_PORT_OBJ_DEL:
		return -EOPNOTSUPP;
	case SWITCHDEV_PORT_ATTR_SET:
		ret = switchdev_handle_port_attr_set(ndev, ptr,
						     rswitch_port_check,
						     rswitch_port_attr_set);
		break;
	default:
		if (!rswitch_port_check(ndev))
			return NOTIFY_DONE;
		ret = -EOPNOTSUPP;
	}

	return notifier_from_errno(ret);
}

static struct notifier_block rswitch_netdevice_nb = {
	.notifier_call = rswitch_netdevice_event,
};

static struct notifier_block rswitch_switchdev_nb = {
	.notifier_call = rswitch_switchdev_event,
};

static struct notifier_block rswitch_switchdev_blocking_nb = {
	.notifier_call = rswitch_switchdev_blocking_event,
};

int rswitch_register_notifiers(void)
{
	int ret;

	ret = register_netdevice_notifier(&rswitch_netdevice_nb);
	if (ret)
		goto register_netdevice_notifier_failed;

	ret = register_switchdev_notifier(&rswitch_switchdev_nb);
	if (ret)
		goto register_switchdev_notifier_failed;

	ret = register_switchdev_blocking_notifier(&rswitch_switchdev_blocking_nb);
	if (ret)
		goto register_switchdev_blocking_notifier_failed;

	return 0;

register_switchdev_blocking_notifier_failed:
	unregister_switchdev_notifier(&rswitch_switchdev_nb);
register_switchdev_notifier_failed:
	unregister_netdevice_notifier(&rswitch_netdevice_nb);
register_netdevice_notifier_failed:

	return ret;
}

void rswitch_unregister_notifiers(void)
{
	unregister_switchdev_blocking_notifier(&rswitch_switchdev_blocking_nb);
	unregister_switchdev_notifier(&rswitch_switchdev_nb);
	unregister_netdevice_notifier(&rswitch_netdevice_nb);
}
