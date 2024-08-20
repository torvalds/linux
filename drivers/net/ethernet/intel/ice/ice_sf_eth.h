/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024, Intel Corporation. */

#ifndef _ICE_SF_ETH_H_
#define _ICE_SF_ETH_H_

#include <linux/auxiliary_bus.h>
#include "ice.h"

struct ice_sf_priv {
	struct ice_sf_dev *dev;
	struct devlink_port devlink_port;
};

#endif /* _ICE_SF_ETH_H_ */
