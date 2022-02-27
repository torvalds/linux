/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */

#ifndef __FUNETH_DEVLINK_H
#define __FUNETH_DEVLINK_H

#include <net/devlink.h>

struct devlink *fun_devlink_alloc(struct device *dev);
void fun_devlink_free(struct devlink *devlink);
void fun_devlink_register(struct devlink *devlink);
void fun_devlink_unregister(struct devlink *devlink);

#endif /* __FUNETH_DEVLINK_H */
