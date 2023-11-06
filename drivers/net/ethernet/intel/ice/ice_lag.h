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
	u8 primary:1; /* this is primary */
	u8 role;
};

int ice_init_lag(struct ice_pf *pf);
void ice_deinit_lag(struct ice_pf *pf);
#endif /* _ICE_LAG_H_ */
