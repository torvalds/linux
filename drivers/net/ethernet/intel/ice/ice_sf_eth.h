/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024, Intel Corporation. */

#ifndef _ICE_SF_ETH_H_
#define _ICE_SF_ETH_H_

#include <linux/auxiliary_bus.h>
#include "ice.h"

struct ice_sf_dev {
	struct auxiliary_device adev;
	struct ice_dynamic_port *dyn_port;
	struct ice_sf_priv *priv;
};

struct ice_sf_priv {
	struct ice_sf_dev *dev;
	struct devlink_port devlink_port;
};

static inline struct
ice_sf_dev *ice_adev_to_sf_dev(struct auxiliary_device *adev)
{
	return container_of(adev, struct ice_sf_dev, adev);
}

int ice_sf_driver_register(void);
void ice_sf_driver_unregister(void);

int ice_sf_eth_activate(struct ice_dynamic_port *dyn_port,
			struct netlink_ext_ack *extack);
void ice_sf_eth_deactivate(struct ice_dynamic_port *dyn_port);
#endif /* _ICE_SF_ETH_H_ */
