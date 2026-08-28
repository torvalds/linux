/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XE_AMC_H_
#define _XE_AMC_H_

#include <linux/i2c.h>

#include "xe_device.h"

struct xe_i2c;

static inline struct xe_device *i2c_adapter_to_xe_device(struct i2c_adapter *adapter)
{
	return kdev_to_xe_device(adapter->dev.parent->parent);
}

static inline struct xe_device *i2c_client_to_xe_device(struct i2c_client *client)
{
	return i2c_adapter_to_xe_device(client->adapter);
}

int xe_amc_init(struct xe_i2c *i2c);
void xe_amc_exit(struct xe_i2c *i2c);
void xe_amc_handle_alert(struct xe_i2c *i2c);

#endif /* _XE_AMC_H_ */
