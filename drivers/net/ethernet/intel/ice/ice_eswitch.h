/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2021, Intel Corporation. */

#ifndef _ICE_ESWITCH_H_
#define _ICE_ESWITCH_H_

#include <net/devlink.h>

#ifdef CONFIG_ICE_SWITCHDEV
int ice_eswitch_mode_get(struct devlink *devlink, u16 *mode);
int
ice_eswitch_mode_set(struct devlink *devlink, u16 mode,
		     struct netlink_ext_ack *extack);
bool ice_is_eswitch_mode_switchdev(struct ice_pf *pf);
#else /* CONFIG_ICE_SWITCHDEV */
static inline int
ice_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	return DEVLINK_ESWITCH_MODE_LEGACY;
}

static inline int
ice_eswitch_mode_set(struct devlink *devlink, u16 mode,
		     struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static inline bool
ice_is_eswitch_mode_switchdev(struct ice_pf *pf)
{
	return false;
}
#endif /* CONFIG_ICE_SWITCHDEV */
#endif /* _ICE_ESWITCH_H_ */
