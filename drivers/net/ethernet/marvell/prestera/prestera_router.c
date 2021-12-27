// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2021 Marvell International Ltd. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/inetdevice.h>

#include "prestera.h"
#include "prestera_router_hw.h"

static int __prestera_inetaddr_port_event(struct net_device *port_dev,
					  unsigned long event,
					  struct netlink_ext_ack *extack)
{
	struct prestera_port *port = netdev_priv(port_dev);
	int err;

	err = prestera_is_valid_mac_addr(port, port_dev->dev_addr);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "RIF MAC must have the same prefix");
		return err;
	}

	switch (event) {
	case NETDEV_UP:
	case NETDEV_DOWN:
		break;
	}

	return 0;
}

static int __prestera_inetaddr_event(struct prestera_switch *sw,
				     struct net_device *dev,
				     unsigned long event,
				     struct netlink_ext_ack *extack)
{
	if (prestera_netdev_check(dev) && !netif_is_bridge_port(dev) &&
	    !netif_is_lag_port(dev) && !netif_is_ovs_port(dev))
		return __prestera_inetaddr_port_event(dev, event, extack);

	return 0;
}

static int __prestera_inetaddr_cb(struct notifier_block *nb,
				  unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct net_device *dev = ifa->ifa_dev->dev;
	struct prestera_router *router = container_of(nb,
						      struct prestera_router,
						      inetaddr_nb);
	struct in_device *idev;
	int err = 0;

	if (event != NETDEV_DOWN)
		goto out;

	/* Ignore if this is not latest address */
	idev = __in_dev_get_rtnl(dev);
	if (idev && idev->ifa_list)
		goto out;

	err = __prestera_inetaddr_event(router->sw, dev, event, NULL);
out:
	return notifier_from_errno(err);
}

static int __prestera_inetaddr_valid_cb(struct notifier_block *nb,
					unsigned long event, void *ptr)
{
	struct in_validator_info *ivi = (struct in_validator_info *)ptr;
	struct net_device *dev = ivi->ivi_dev->dev;
	struct prestera_router *router = container_of(nb,
						      struct prestera_router,
						      inetaddr_valid_nb);
	struct in_device *idev;
	int err = 0;

	if (event != NETDEV_UP)
		goto out;

	/* Ignore if this is not first address */
	idev = __in_dev_get_rtnl(dev);
	if (idev && idev->ifa_list)
		goto out;

	if (ipv4_is_multicast(ivi->ivi_addr)) {
		err = -EINVAL;
		goto out;
	}

	err = __prestera_inetaddr_event(router->sw, dev, event, ivi->extack);
out:
	return notifier_from_errno(err);
}

int prestera_router_init(struct prestera_switch *sw)
{
	struct prestera_router *router;
	int err;

	router = kzalloc(sizeof(*sw->router), GFP_KERNEL);
	if (!router)
		return -ENOMEM;

	sw->router = router;
	router->sw = sw;

	err = prestera_router_hw_init(sw);
	if (err)
		goto err_router_lib_init;

	router->inetaddr_valid_nb.notifier_call = __prestera_inetaddr_valid_cb;
	err = register_inetaddr_validator_notifier(&router->inetaddr_valid_nb);
	if (err)
		goto err_register_inetaddr_validator_notifier;

	router->inetaddr_nb.notifier_call = __prestera_inetaddr_cb;
	err = register_inetaddr_notifier(&router->inetaddr_nb);
	if (err)
		goto err_register_inetaddr_notifier;

	return 0;

err_register_inetaddr_notifier:
	unregister_inetaddr_validator_notifier(&router->inetaddr_valid_nb);
err_register_inetaddr_validator_notifier:
	/* prestera_router_hw_fini */
err_router_lib_init:
	kfree(sw->router);
	return err;
}

void prestera_router_fini(struct prestera_switch *sw)
{
	unregister_inetaddr_notifier(&sw->router->inetaddr_nb);
	unregister_inetaddr_validator_notifier(&sw->router->inetaddr_valid_nb);
	/* router_hw_fini */
	kfree(sw->router);
	sw->router = NULL;
}
