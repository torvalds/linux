/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_LAG_H_
#define _ICE_LAG_H_

#include <linux/netdevice.h>

/* LAG roles for netdev */
enum ice_lag_role {
	ICE_LAG_NONE,
	ICE_LAG_PRIMARY,
	ICE_LAG_BACKUP,
	ICE_LAG_UNSET
};

struct ice_pf;

/* LAG info struct */
struct ice_lag {
	struct ice_pf *pf; /* backlink to PF struct */
	struct net_device *netdev; /* this PF's netdev */
	struct net_device *peer_netdev;
	struct net_device *upper_netdev; /* upper bonding netdev */
	struct notifier_block notif_block;
	u8 bonded:1; /* currently bonded */
	u8 master:1; /* this is a master */
	u8 handler:1; /* did we register a rx_netdev_handler */
	/* each thing blocking bonding will increment this value by one.
	 * If this value is zero, then bonding is allowed.
	 */
	u16 dis_lag;
	u8 role;
};

int ice_init_lag(struct ice_pf *pf);
void ice_deinit_lag(struct ice_pf *pf);
rx_handler_result_t ice_lag_nop_handler(struct sk_buff **pskb);

/**
 * ice_disable_lag - increment LAG disable count
 * @lag: LAG struct
 */
static inline void ice_disable_lag(struct ice_lag *lag)
{
	/* If LAG this PF is not already disabled, disable it */
	rtnl_lock();
	if (!netdev_is_rx_handler_busy(lag->netdev)) {
		if (!netdev_rx_handler_register(lag->netdev,
						ice_lag_nop_handler,
						NULL))
			lag->handler = true;
	}
	rtnl_unlock();
	lag->dis_lag++;
}

/**
 * ice_enable_lag - decrement disable count for a PF
 * @lag: LAG struct
 *
 * Decrement the disable counter for a port, and if that count reaches
 * zero, then remove the no-op Rx handler from that netdev
 */
static inline void ice_enable_lag(struct ice_lag *lag)
{
	if (lag->dis_lag)
		lag->dis_lag--;
	if (!lag->dis_lag && lag->handler) {
		rtnl_lock();
		netdev_rx_handler_unregister(lag->netdev);
		rtnl_unlock();
		lag->handler = false;
	}
}

/**
 * ice_is_lag_dis - is LAG disabled
 * @lag: LAG struct
 *
 * Return true if bonding is disabled
 */
static inline bool ice_is_lag_dis(struct ice_lag *lag)
{
	return !!(lag->dis_lag);
}
#endif /* _ICE_LAG_H_ */
