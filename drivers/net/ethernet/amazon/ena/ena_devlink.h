/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) Amazon.com, Inc. or its affiliates.
 * All rights reserved.
 */
#ifndef DEVLINK_H
#define DEVLINK_H

#include "ena_netdev.h"
#include <net/devlink.h>

#define ENA_DEVLINK_PRIV(devlink) \
	(*(struct ena_adapter **)devlink_priv(devlink))

struct devlink *ena_devlink_alloc(struct ena_adapter *adapter);
void ena_devlink_free(struct devlink *devlink);
void ena_devlink_register(struct devlink *devlink, struct device *dev);
void ena_devlink_unregister(struct devlink *devlink);
void ena_devlink_params_get(struct devlink *devlink);
void ena_devlink_disable_phc_param(struct devlink *devlink);

#endif /* DEVLINK_H */
