/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025, Intel Corporation. */

#ifndef _IXD_DEVLINK_H_
#define _IXD_DEVLINK_H_

#include <net/devlink.h>

#include "ixd.h"

struct ixd_adapter *ixd_adapter_alloc(struct device *dev);

/**
 * ixd_devlink_free - teardown the devlink
 * @adapter: the adapter structure to free
 *
 */
static inline void ixd_devlink_free(struct ixd_adapter *adapter)
{
	struct devlink *devlink = priv_to_devlink(adapter);

	devlink_free(devlink);
}

/**
 * ixd_devlink_unregister - Unregister devlink for this adapter.
 * @adapter: the adapter structure to cleanup
 *
 * Init task must be completed or cancelled beforehand.
 */
static inline void ixd_devlink_unregister(struct ixd_adapter *adapter)
{
	if (!adapter->init_task.success)
		return;

	devlink_unregister(priv_to_devlink(adapter));
}

/**
 * ixd_devlink_register - Register devlink interface for this adapter
 * @adapter: pointer to ixd adapter structure to be associated with devlink
 *
 * Register the devlink instance associated with this adapter
 */
static inline void ixd_devlink_register(struct ixd_adapter *adapter)
{
	devlink_register(priv_to_devlink(adapter));
}

#endif /* _IXD_DEVLINK_H_ */
